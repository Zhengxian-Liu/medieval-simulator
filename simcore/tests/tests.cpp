// Headless tests for SimCore: determinism and state invariants over long runs.
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <tuple>
#include <set>
#include <string>

#include "simcore/autoplayer.h"
#include "simcore/events.h"
#include "simcore/world.h"

using namespace sim;

static int failures = 0;
#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg);       \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static void checkInvariants(const World& w, const char* label) {
    std::set<Id> seen;
    // The lord lives at the manor (the fines and rents are paid into his household).
    if (w.lordId() != kNone && w.person(w.lordId()).alive()) CHECK(w.person(w.lordId()).household != kNone, "the lord has no household");
    for (const auto& h : w.households()) {
        if (h.extinct) {
            CHECK(h.members.empty(), "extinct household has members");
            continue;
        }
        if (h.members.empty()) continue;
        CHECK(h.head != kNone, "household without head");
        // An heir held for (寡婦產 / 代管) is not the head, and is a child at home or away in service.
        if (h.ward != kNone) {
            const Person& wd = w.person(h.ward);
            // At home (a child, or just back from the road and handed the house at the month's end), in service, or away.
            bool held = wd.household == h.id || wd.home == h.id || (w.isAway(wd) && wd.awayFrom == h.id);
            CHECK(wd.alive() && h.head != h.ward && held, "ward not this house's child, held by another");
        }
        bool headIn = false;
        for (Id m : h.members) {
            const Person& p = w.person(m);
            CHECK(p.alive(), "dead person listed as member");
            CHECK(p.household == h.id, "member points at another household");
            CHECK(seen.insert(m).second, "person in two households");
            headIn |= m == h.head;
        }
        CHECK(headIn, "head not a member");
        CHECK(h.money >= 0 && h.food >= 0, "negative money or food");
    }
    for (const auto& p : w.people()) {
        if (p.alive() && w.isAway(p)) {
            // Living away (design/life-paths.md): out of every household, on an away station, still someone's.
            const StationDef* sd = w.defs().station(p.station);
            CHECK(sd && sd->away, "away person on a home station");
            CHECK(!seen.count(p.id), "away person still listed in a household");
            CHECK(p.awayFrom >= 0 && p.awayFrom < static_cast<Id>(w.households().size()), "away person belongs nowhere");
            // Their house still stands, or they have settled out there: money sent home reaches someone.
            if (p.awayFrom >= 0 && p.awayFrom < static_cast<Id>(w.households().size()))
                CHECK(!w.household(p.awayFrom).extinct || sd->lasting, "away person belongs to a house that is gone");
        }
        if (p.alive() && p.household != kNone && w.household(p.household).head == p.id &&
            (p.station == "labourer" || p.station == "veteran" || p.station == "apprentice")) {
            std::string road;
            for (const auto& r : p.road) road += " " + std::to_string(r.day / kDaysPerYear) + ":" + r.station + "/" + r.note;
            std::printf("  (%s) %s on %s:%s\n", label, w.fullName(p.id).c_str(), p.station.c_str(), road.c_str());
            CHECK(false, "a head still on a youth's station");
        }
        if (p.alive() && p.household == kNone && !w.isAway(p)) CHECK(p.station.empty(), "married away but still on a station");
        if (p.alive() && p.household != kNone && !p.station.empty()) {
            const StationDef* sd = w.defs().station(p.station);
            CHECK(sd && !sd->away, "villager on an away station");
        }
        if (!p.alive() || p.household == kNone) continue;
        CHECK(seen.count(p.id), "living villager missing from household list");
        double age = w.ageYears(p);
        CHECK(age >= 0 && age < 110, "implausible age");
        if (p.spouse != kNone && w.person(p.spouse).alive())
            CHECK(w.person(p.spouse).spouse == p.id, "spouse link not mutual");
        for (double n : p.needs) CHECK(n >= 0 && n <= 100, "need out of range");
    }
    // Namesakes under one roof can be told apart (塞西莉, 二塞西莉, 小塞西莉).
    for (const auto& h : w.households()) {
        std::set<std::string> names;
        for (Id m : h.members)
            if (!names.insert(w.fullName(m)).second) {
                std::printf("  same name in one house: %s\n", w.fullName(m).c_str());
                CHECK(false, "two people in one house with the same name");
            }
    }
    // Land (design/contested.md): every household's acres are the parcels it holds, no map cell is in two parcels.
    std::map<Id, double> acres, freehold;
    std::set<int> cells;
    for (const auto& pc : w.parcels()) {
        if (pc.gone) continue;
        CHECK(pc.acres > 0, "empty parcel");
        if (pc.holder != kNone) {
            acres[pc.holder] += pc.acres;
            if (pc.free) freehold[pc.holder] += pc.acres;
        }
        for (int c : pc.cells) CHECK(cells.insert(c).second, "map cell in two parcels");
    }
    for (const auto& h : w.households()) {
        CHECK(std::abs(acres[h.id] - h.acres) < 1e-6, "household acres differ from its parcels");
        CHECK(std::abs(freehold[h.id] - h.freehold) < 1e-6, "household freehold differs from its parcels");
        if (h.extinct) CHECK(h.acres < 1e-6, "extinct household still holds land");
    }
    // Village posts: one man, one post, and only the living hold one.
    std::set<Id> holders;
    for (const auto& o : w.offices())
        if (o.holder != kNone) {
            CHECK(w.person(o.holder).alive(), "a dead man holds a post");
            CHECK(holders.insert(o.holder).second, "one man holds two posts");
        }
    (void)label;
}

namespace sim {
// The cases a run rarely reaches, set up by hand: a head with money to spare, a family at the top of the ladder,
// a manor short of coin.
struct WorldTest {
    static Person& person(World& w, Id id) { return w.people_[id]; }
    static Household& house(World& w, Id id) { return w.households_[id]; }
    static void choose(World& w, Id head) {
        w.people_[head].ambition.clear();
        w.chooseAmbition(w.people_[head]);
    }
    static void setAmbition(World& w, Id head, const std::string& id) {
        Person& p = w.people_[head];
        const Household& h = w.households_[p.household];
        p.ambition = id;
        p.ambitionSince = w.day_;
        p.ambitionStep = 0;
        p.ambitionDone = 0;
        p.ambitionBase.money = h.money, p.ambitionBase.acres = h.acres, p.ambitionBase.freehold = h.freehold;
    }
    // A small landholder: free, 45 acres, 22 of them freehold, from the lord's own; and the lord thinks well of them.
    static void makeFranklin(World& w, Id hid) {
        Household& h = w.households_[hid];
        for (Id m : h.members) w.people_[m].free = true;
        Id lordHouse = w.people_[w.lord_].household;
        if (h.acres < 45) w.moveLand(lordHouse, hid, 45 - h.acres, LandPick::CustomaryFirst, "test");
        if (h.freehold < 22) w.makeFree(h, 22 - h.freehold);
        w.addOpinion(w.lord_, h.head, "test", 60, 30);
    }
    static void rivalForLand(World& w) { w.rivalForLand(w.people_[w.playerHead()]); }
};
}  // namespace sim

static double villageAcres(const World& w) {
    double a = 0;
    for (const auto& pc : w.parcels())
        if (!pc.gone) a += pc.acres;
    return a;
}

int main() {
    Defs defs = Defs::loadFromDir(SIMCORE_DATA_DIR);

    // Same seed -> identical history.
    {
        Config c;
        c.seed = 42;
        World a(defs, c), b(defs, c);
        a.runYears(60);
        b.runYears(60);
        CHECK(a.digest() == b.digest(), "same seed produced different worlds");
        CHECK(a.chronicle().size() == b.chronicle().size(), "chronicle length differs");
        checkInvariants(a, "seed42");
    }
    // The village lives 25 years on its own, then the family is handed over alive, in the start year.
    {
        Config c;
        c.seed = 5;
        c.prerun = 25;
        World a(defs, c), b(defs, c);
        CHECK(a.playerHead() != kNone && !a.gameOver(), "no family to hand over after the pre-run");
        CHECK(a.year() == 1330 && a.handedOver() == a.today(), "hand-over not in the start year");
        CHECK(a.config().startYear == 1305, "world did not begin before play");
        int moved = 0;
        for (const auto& pc : a.parcels()) moved += pc.since > 0;
        CHECK(moved > 0, "no land changed hands before the hand-over");
        a.runYears(10);
        b.runYears(10);
        CHECK(a.digest() == b.digest(), "pre-run worlds differ for one seed");
        checkInvariants(a, "prerun");
    }
    // Every ask on everyone, pressed with every lever offered: the family never spends money it does not have,
    // and a no is pressed only once more, in the same season and the same talk (no second day).
    std::map<std::string, int> presses;
    for (int seed : {3, 5, 8}) {
        Config c;
        c.seed = seed;
        c.prerun = 25;
        World w(defs, c);
        for (int season = 0; season < 24 && !w.gameOver(); ++season) {
            for (Id who : w.peopleToSee()) {
                if (w.playerHead() == kNone) break;
                for (const auto& o : w.asksFor(who)) {
                    if (!o.open) continue;
                    std::string sub = o.subject == kNone ? "" : std::to_string(o.subject);
                    w.issue({w.today(), "ask", w.playerHead(), o.ask + "|" + o.office + "||" + sub, who, o.object});
                    w.runCommands();
                    CHECK(w.household(w.playerHousehold()).money > -0.01, "an ask left the family in the red");
                    for (const auto& lever : std::vector<std::string>(w.lastAsk().levers)) {
                        int before = w.timeLeft();
                        std::string said = w.lastAsk().text;
                        w.issue({w.today(), "ask", w.playerHead(), o.ask + "|" + o.office + "|" + lever + "|" + sub, who, o.object});
                        w.runCommands();
                        CHECK(w.household(w.playerHousehold()).money > -0.01, "pressing an ask left the family in the red");
                        if (w.lastAsk().text == said) break;  // not pressed (no money for the lever)
                        CHECK(w.timeLeft() == before, "pressing an ask took another day (it is the same talk)");
                        ++presses[lever];
                        break;  // one press per answer
                    }
                    break;  // one ask per person a season
                }
            }
            for (int d = 0; d < kDaysPerYear / 4 && !w.gameOver(); ++d) w.stepDay();
        }
        checkInvariants(w, "asks");
    }
    std::printf("asks: pressed with");
    for (const auto& [l, n] : presses) std::printf(" %s %d", l.c_str(), n);
    std::printf("\n");
    // Lives fork (design/life-paths.md): over a few villages and decades, people leave for town, for war and
    // for school, some come back, and the roads they walked are kept.
    {
        std::map<std::string, int> walked;
        int cameBack = 0;
        for (int seed : {1, 2, 3, 4}) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World w(defs, c);
            w.runYears(30);
            checkInvariants(w, "paths");
            for (const auto& p : w.people()) {
                bool wasAway = false;
                for (const auto& r : p.road) {
                    const StationDef* sd = w.defs().station(r.station);
                    CHECK(sd != nullptr, "road through an unknown station");
                    if (sd && sd->away) wasAway = true;
                    else if (wasAway && sd) ++cameBack, wasAway = false;
                    ++walked[r.station];
                }
            }
        }
        for (const char* st : {"servant", "apprentice", "labourer", "town_apprentice", "town_servant", "archer"})
            CHECK(walked[st] > 0, "nobody walked a common road");
        CHECK(cameBack > 0, "nobody who left ever came back");
        CHECK(walked["labourer"] > 0, "nobody hired out as a labourer");
        std::printf("paths: %d servants, %d town apprentices, %d archers, %d came back\n", walked["servant"], walked["town_apprentice"],
                    walked["archer"], cameBack);
    }
    // The cases the sweep above rarely reaches, made on purpose: a gift pressed with every sum of money around what
    // the deal costs, and a no pressed once the season has turned.
    {
        bool giftTried = false, seasonTried = false;
        for (int seed = 1; seed <= 40 && !(giftTried && seasonTried); ++seed) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World w(defs, c);
            for (int season = 0; season < 16 && !(giftTried && seasonTried) && !w.gameOver(); ++season) {
                for (Id who : w.peopleToSee()) {
                    if (giftTried && seasonTried) break;
                    for (const auto& o : w.asksFor(who)) {
                        if (!o.open || (o.ask != "rent_vacant" && o.ask != "sell_parcel" && o.ask != "apprentice")) continue;
                        std::string sub = o.subject == kNone ? "" : std::to_string(o.subject);
                        World a = w;
                        a.issue({a.today(), "ask", a.playerHead(), o.ask + "|" + o.office + "||" + sub, who, o.object});
                        a.runCommands();
                        const auto& lv = a.lastAsk().levers;
                        if (a.lastAsk().answer < 2 || std::find(lv.begin(), lv.end(), "gift") == lv.end() || a.timeLeft() < 1) continue;
                        // Every lever the answer offers, pressed with every sum of money: a press the family cannot
                        // pay for changes nothing; none takes a day (it is the same talk); the front ends (leverOpen)
                        // offer exactly the presses that go through.
                        for (const std::string& lever : std::vector<std::string>(lv)) {
                            for (double m = 0; m <= 400; m += 2) {
                                World b = a;
                                b.setMoneyForTest(b.playerHousehold(), m);
                                bool offered = b.leverOpen(lever);
                                int before = b.timeLeft();
                                std::string said = b.lastAsk().text;
                                b.issue({b.today(), "ask", b.playerHead(), o.ask + "|" + o.office + "|" + lever + "|" + sub, who, o.object});
                                b.runCommands();
                                bool pressed = b.lastAsk().text != said;
                                CHECK(b.household(b.playerHousehold()).money > -0.01, "a press left the family in the red");
                                CHECK(b.timeLeft() == before, "a press took a day");
                                CHECK(pressed == offered, "the front ends offer a press the engine refuses, or hide one it takes");
                                CHECK(!pressed || b.lastAsk().text.find("湊夠") == std::string::npos, "a press took the day only to say 湊夠");
                                if (lever == "gift" && m < a.lastAsk().giftSum + a.lastAsk().dealCost) CHECK(!pressed, "a gift the family cannot afford took the day");
                            }
                        }
                        giftTried = true;
                        World d = a;
                        for (int k = 0; k < kDaysPerYear / 4; ++k) d.stepDay();
                        std::string said = d.lastAsk().text;
                        int before = d.timeLeft();
                        d.issue({d.today(), "ask", d.playerHead(), o.ask + "|" + o.office + "|gift|" + sub, who, o.object});
                        d.runCommands();
                        CHECK(d.lastAsk().text == said && d.timeLeft() == before, "a no was pressed in another season");
                        seasonTried = true;
                        break;
                    }
                }
                for (int k = 0; k < kDaysPerYear / 4 && !w.gameOver(); ++k) w.stepDay();
            }
        }
        CHECK(giftTried && seasonTried, "found no ask to press with a gift");
    }
    // Money is the family's to decide (design/fun-r5.md A): grain is bought to the harvest and no further, on a card
    // for the player's house; at Michaelmas a rent the purse cannot meet is reckoned on a card before anything goes on
    // the books; a season's money lists every item.
    {
        Config c;
        c.seed = 58;
        c.prerun = 25;
        World base(defs, c);
        const Id hh = base.playerHousehold();
        auto foodDays = [&](const World& w) {
            return w.household(hh).food / std::max(0.5, w.dailyBread(hh));
        };
        auto runTo = [&](World& w, int month, int day) {  // to the start of that day, before it runs
            while (!w.gameOver() && !(w.month() == month && w.dayOfMonth() == day)) w.stepDay();
        };
        // No one to ask: a month before the harvest the house buys to the harvest, not three months.
        {
            World w = base;
            runTo(w, 7, 0);
            w.setMoneyForTest(hh, 300);
            w.setFoodForTest(hh, 10);
            w.stepDay();  // month 7, day 0: thirty days to the harvest
            CHECK(foodDays(w) > 30 && foodDays(w) < 47, "grain was bought past the harvest");
        }
        // Asked: the card says how far the barn is from the harvest, and "buy to the harvest" buys just that.
        {
            World w = base;
            std::vector<World::ChoicePrompt> asked;
            w.setChooser([&](const World::ChoicePrompt& p) {
                asked.push_back(p);
                if (p.title != "買糧") return -1;
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].rfind("買到收成", 0) == 0) return static_cast<int>(i);
                return -1;
            });
            runTo(w, 6, 0);
            w.setMoneyForTest(hh, 400);
            w.setFoodForTest(hh, 20);
            w.stepDay();  // month 6, day 0: sixty days to the harvest
            bool card = false;
            for (const auto& p : asked)
                if (p.title == "買糧" && p.situation.find("離秋收還有 60 天") != std::string::npos) card = true;
            CHECK(card, "the family was not asked about buying grain");
            CHECK(foodDays(w) > 70 && foodDays(w) < 77, "buying to the harvest did not buy to the harvest");
            CHECK(asked.back().situation.find("缺糧的緣故：") != std::string::npos, "the grain card does not say why the house is short");
        }
        // fun-r6 C: an answer holds once. "A month" buys one month; when it is eaten the family is asked again (the
        // year's hunger budget allowing), and past the budget a month is bought only near the bare barn, in words.
        {
            World w = base;
            int cards = 0;
            std::string lastSit;
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "買糧") return -1;
                ++cards;
                lastSit = p.situation;
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].rfind("先買一個月的", 0) == 0) return static_cast<int>(i);
                return -1;
            });
            auto cardAt = [&](int month, double food) {  // the barn set just before the month's reckoning
                runTo(w, month, 0);
                w.setMoneyForTest(hh, 400);
                w.setFoodForTest(hh, food);
                int before = cards;
                w.stepDay();
                return cards - before;
            };
            CHECK(cardAt(3, 20) == 1 && foodDays(w) > 45 && foodDays(w) < 55, "a month's grain was not bought on the card");
            CHECK(cardAt(4, 40) == 0 && foodDays(w) < 41, "grain was bought again on the strength of last time's answer");
            CHECK(cardAt(5, 10) == 1 && lastSit.find("上回買的糧又快吃完了") != std::string::npos, "the month eaten, the family was not asked again");
            size_t lines = w.chronicle().size();
            CHECK(cardAt(6, 10) == 0, "a third grain card in one year");
            CHECK(foodDays(w) > 30 && foodDays(w) < 50, "past the year's cards, a month was not bought at the bare barn");
            bool said = false, standing = false;
            for (size_t i = lines; i < w.chronicle().size(); ++i) {
                said = said || w.chronicle()[i].text.find("便士買了") != std::string::npos;
                standing = standing || w.chronicle()[i].text.find("照今年說好的") != std::string::npos;
            }
            CHECK(said && !standing, "the grain bought past the cards was not told with its price, or was told as a standing order");
            w.setChooser({});
        }
        // A few weeks short with coin to spare is no crisis: bought without a card, and said.
        {
            World w = base;
            int cards = 0;
            w.setChooser([&](const World::ChoicePrompt& p) {
                cards += p.title == "買糧";
                return -1;
            });
            runTo(w, 7, 0);
            w.setMoneyForTest(hh, 400);
            w.setFoodForTest(hh, 20);
            size_t lines = w.chronicle().size();
            w.stepDay();  // thirty days to the harvest
            bool said = false;
            for (size_t i = lines; i < w.chronicle().size(); ++i) said = said || w.chronicle()[i].text.find("夠吃到收成") != std::string::npos;
            CHECK(cards == 0 && said && foodDays(w) > 40, "a small gap with a full purse was put to the family as a hunger card");
            w.setChooser({});
        }
        // Michaelmas with an empty purse: the reckoning card comes before any debt, and its levers work.
        auto michaelmas = [&](const std::string& pick, double& owedAtCard, std::string& sit) {
            World w = base;
            owedAtCard = -1;
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "米迦勒節算帳") return -1;
                sit = p.situation;
                owedAtCard = w.household(hh).debt + w.household(hh).heldDebt;
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].rfind(pick, 0) == 0) return static_cast<int>(i);
                return static_cast<int>(p.options.size()) - 1;
            });
            runTo(w, 8, 29);
            w.setMoneyForTest(hh, 3);
            w.setFoodForTest(hh, 1000);  // a grain day is worth less now (fun-r6 C), so the rent takes more of them
            w.stepDay();  // rent day
            w.setChooser({});  // it holds this frame's `w`
            return w;
        };
        double owed = 0;
        std::string sit;
        World books = michaelmas("差的先記在", owed, sit);
        CHECK(owed >= 0 && owed < 1, "the rent shortfall went on the books before the family was asked");
        CHECK(sit.find("今年的租") != std::string::npos && sit.find("×5") != std::string::npos, "the reckoning card does not say how the rent is made up");
        CHECK(books.owedAll(hh) > 5 && books.debtText(hh).find("欠莊園") != std::string::npos, "the shortfall the family let go was not on the manor's books");
        World sold = michaelmas("賣 ", owed, sit);
        CHECK(sold.owedAll(hh) < 1, "selling grain on the reckoning card did not pay the rent");
        CHECK(foodDays(sold) < 1000 - 5, "the grain sold on the reckoning card is still in the barn");
        // A season's money, item by item: what came in apart from what went out.
        World w = base;
        World::FamilySnapshot before = w.snapshot();
        w.runYears(1);
        std::string money = w.moneySince(before);
        CHECK(money.find("進：") != std::string::npos && money.find("出：") != std::string::npos && money.find("其他") == std::string::npos,
              "a year's money is not listed as incomings and outgoings");
        std::printf("money: %s\n", money.c_str());
    }
    // fun-r6 C: hunger is a bad year's crisis. In an ordinary summer a house with land enough for its mouths (10 acres
    // for two grown and three children is about 2.5 acres a grown man's bread) has a year's bread in the barn after the
    // harvest; a house with far too little land for its mouths still has not.
    {
        Config c;
        c.seed = 72;
        c.prerun = 25;
        World w(defs, c);
        int roomy = 0, roomyLean = 0, tight = 0, tightLean = 0, bad = 0, badLean = 0;
        for (int y = 0; y < 12 && !w.gameOver(); ++y) {
            while (!w.gameOver() && !(w.month() == 8 && w.dayOfMonth() == 1)) w.stepDay();  // the day after the harvest
            for (const auto& h : w.households()) {
                if (h.extinct || h.members.empty() || h.head == kNone || h.house == "manor" || w.person(h.head).occupation == "priest") continue;
                double perMouth = h.acres / std::max(0.5, w.dailyBread(h.id));
                double weather = w.lastHarvestWeather();
                if (weather < 0.8) bad += perMouth >= 2.4, badLean += perMouth >= 2.4 && h.lean;
                if (weather < 0.85 || weather > 1.15) continue;
                if (perMouth >= 2.4) ++roomy, roomyLean += h.lean;
                if (perMouth < 1.2) ++tight, tightLean += h.lean;
            }
            w.stepDay();
        }
        std::printf("ordinary harvests: %d of %d houses with 2.4+ acres a mouth short, %d of %d with under 1.2; bad years %d of %d\n", roomyLean, roomy,
                    tightLean, tight, badLean, bad);
        CHECK(roomy >= 10 && roomyLean * 10 <= roomy, "in an ordinary year a house with land enough for its mouths was short of bread");
        CHECK(tight == 0 || tightLean * 10 >= tight * 7, "a house with far too little land for its mouths was fed by an ordinary harvest");
    }
    // fun-r6 C: a poor season's free day can be worked for pay at once (这一季的事), paid by the neighbour who needs hands.
    {
        Config c;
        c.seed = 58;
        c.prerun = 25;
        World w(defs, c);
        const Id hh = w.playerHousehold();
        while (!(w.month() == 3 && w.dayOfMonth() == 0)) w.stepDay();
        w.startSeason();
        w.setMoneyForTest(hh, 5);
        w.setFoodForTest(hh, 40);
        const World::AgendaItem* day = nullptr;
        std::vector<World::AgendaItem> items = w.seasonAgenda();
        for (const auto& a : items)
            if (a.id == "spare:day") day = &a;
        CHECK(day != nullptr, "a poor season's list has no day's work to do");
        if (day) {
            Id payer = w.person(day->target).household;
            double theirs = w.household(payer).money, ours = w.household(hh).money;
            int left = w.timeLeft();
            w.issue({w.today(), "agenda", w.actingHead(), day->id, kNone, kNone});
            w.runCommands();
            double got = w.household(hh).money - ours;
            CHECK(got >= 4 && std::abs(theirs - w.household(payer).money - got) < 0.01, "the day's work was not paid, or not by the house it was done for");
            CHECK(w.timeLeft() == left - 1, "the day's work did not take a free day");
            CHECK(w.chronicle().back().text.find("拿到") != std::string::npos, "the day's work and its pay were not told");
        }
        w.setMoneyForTest(hh, 500);
        w.setFoodForTest(hh, 400);
        bool still = false;
        for (const auto& a : w.seasonAgenda()) still = still || a.id.rfind("spare:", 0) == 0;
        CHECK(!still, "a house with coin and bread to spare is offered day labour");
    }
    // fun-r6 C: wakes and weddings follow a standing family rule. The player's first answer sets it and later
    // invitations follow it without a card; the AI standing in sets none; a year let run goes by custom.
    {
        Config c;
        c.seed = 58;
        c.prerun = 25;
        auto run = [&](int pick, bool routine, int& cards, int& byRule) {
            World w(defs, c);
            cards = byRule = 0;
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "喪事" && p.title != "喜酒") return -1;
                ++cards;
                if (pick < 0) return -1;
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].find("親近的人家才去") != std::string::npos) return static_cast<int>(i);
                return -1;
            });
            if (routine) w.issue({w.today(), "rule", w.actingHead(), "routine:1", kNone, kNone});
            size_t from = w.chronicle().size();
            w.runYears(12);
            for (size_t i = from; i < w.chronicle().size(); ++i) byRule += w.chronicle()[i].text.rfind("【紅白事】", 0) == 0;
            w.setChooser({});
            return w.occasionRule();
        };
        int cards = 0, byRule = 0;
        std::string rule = run(1, false, cards, byRule);
        std::printf("occasions: answered once -> rule \"%s\", %d cards, %d by the rule\n", rule.c_str(), cards, byRule);
        CHECK(cards <= 1 && (cards == 0 || rule == "close"), "the family's answer did not become its rule for wakes and weddings");
        CHECK(cards == 0 || byRule > 0, "later wakes and weddings did not follow the family's rule");
        rule = run(-1, false, cards, byRule);
        std::printf("occasions: AI answers -> rule \"%s\" (%d cards)\n", rule.c_str(), cards);
        CHECK(rule.empty(), "the AI standing in for a card set the family's rule");
        rule = run(1, true, cards, byRule);
        std::printf("occasions: year let run -> %d cards, %d by custom\n", cards, byRule);
        CHECK(cards == 0, "a year let run stopped for a wake or a wedding");
    }
    // 年度重心 (data/focus.json): the same children, years spent playing or in the fields, grow up differently;
    // a focus the person cannot take is refused; the choice shows in the command log.
    {
        // Villages (by seed) whose family has young children at home when the player takes over. Several, since a
        // child's three years also hang on chance (a father who dies, a place in service): the sum shows the focus.
        std::vector<std::pair<World, std::vector<Id>>> bases;
        for (int seed = 4; bases.size() < 6 && seed < 40; ++seed) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World v(defs, c);
            std::vector<Id> ks;
            for (Id m : v.household(v.playerHousehold()).members) {
                double age = v.ageYears(v.person(m));
                if (age >= 5 && age < 10) ks.push_back(m);
            }
            if (!ks.empty()) bases.emplace_back(std::move(v), ks);
        }
        CHECK(!bases.empty(), "no young child at home to test the focus on");
        auto grow = [&](const World& base, const std::vector<Id>& kids, const std::string& focus) {
            World w = base;
            for (Id k : kids) w.issue({w.today(), "focus", w.playerHead(), focus, k, kNone});
            w.runCommands();
            for (Id k : kids) CHECK(w.person(k).focus == focus, "a focus the family chose was not taken");
            w.runYears(3);
            // Per child: skills, and whether they still spent their days that way (not sent to service or a master).
            std::vector<std::tuple<double, double, bool>> out;
            for (Id k : kids) out.push_back({w.person(k).skills[Charm], w.person(k).skills[Farming], w.person(k).alive() && w.person(k).focus == focus});
            return out;
        };
        std::pair<double, double> play{0, 0}, field{0, 0};
        int both = 0;
        for (const auto& [b, ks] : bases) {
            auto pk = grow(b, ks, "play"), fk = grow(b, ks, "farmhand");
            for (size_t i = 0; i < ks.size(); ++i)
                if (std::get<2>(pk[i]) && std::get<2>(fk[i])) {
                    ++both;
                    play.first += std::get<0>(pk[i]), play.second += std::get<1>(pk[i]);
                    field.first += std::get<0>(fk[i]), field.second += std::get<1>(fk[i]);
                }
        }
        CHECK(both > 0, "no child kept the chosen focus for three years in both runs");
        CHECK(play.first > field.first + 0.5 * both && field.second > play.second + 0.5 * both, "years playing and years in the fields grew the same child");
        const World& base = bases[0].first;
        const std::vector<Id>& kids = bases[0].second;
        World w = base;
        std::string before = w.person(kids[0]).focus;
        w.issue({w.today(), "focus", w.playerHead(), "craft", kids[0], kNone});  // an adult's focus, for a child
        w.runCommands();
        CHECK(w.person(kids[0]).focus == before && before != "craft", "a child took an adult's focus");
        std::printf("focus: %d children in %zu villages, charm %.1f vs %.1f, farming %.1f vs %.1f after 3 years\n", both, bases.size(), play.first, field.first,
                    play.second, field.second);
        CHECK(!w.steersFocus(-5), "a negative id was taken for a person");
    }
    // The chapman settles every year on the road, the last one too; a year's focus never carries past a harvest,
    // with or without land; a child who goes to a master stops going to letters.
    {
        Config c;
        c.seed = 5;
        World w(defs, c);
        int roadYears = 0, maxDrift = 0;
        double drift = 0;
        for (int y = 0; y < 40 && !w.gameOver(); ++y) {
            w.runYears(1);
            // A year on the road is settled at the next yearly pulse, so the run's last year is not counted.
            for (const auto& p : w.people())
                if (p.alive() && p.station == "chapman" && w.today() - p.stationSince >= kDaysPerYear) roadYears += y < 39;
                else if (p.alive() && p.station == "apprentice" && p.focus == "letters") CHECK(false, "an apprentice still goes to letters");
            for (const auto& h : w.households())
                if (!h.extinct && std::abs(h.focusYield) > drift) drift = std::abs(h.focusYield), maxDrift = h.id;
        }
        auto n = [&](const std::string& k) { return w.counters().count(k) ? w.counters().at(k) : 0; };
        std::printf("chapman: %d years on the road after the first, %d settled, %d lost; largest focus yield %.3f (house %d)\n", roadYears,
                    n("event:chapman_year"), n("event:chapman_loss"), drift, maxDrift);
        CHECK(roadYears < 5 || n("event:chapman_year") + n("event:chapman_loss") + n("event:chapman_due") >= roadYears * 9 / 10, "chapman years on the road went unsettled");
        CHECK(drift < 0.1, "a household's focus yield carried past the harvest");
    }
    // A run driven by the auto-player replays exactly from its command log alone.
    {
        Config c;
        c.seed = 9;
        World a(defs, c);
        AutoPlayer player(9);
        int steps = 0;
        for (; steps < 40 * kDaysPerYear && !a.gameOver(); ++steps) {
            player.act(a);
            a.stepDay();
        }
        CHECK(!a.commandLog().empty(), "auto-player issued no commands");
        CHECK(std::any_of(a.commandLog().begin(), a.commandLog().end(), [](const Command& x) { return x.type == "focus"; }),
              "auto-player never chose a focus");
        World b(defs, c);
        for (const auto& cmd : a.commandLog()) b.issue(cmd);
        for (int d = 0; d < steps; ++d) b.stepDay();
        CHECK(a.digest() == b.digest(), "command replay diverged");
        std::printf("replay: %zu commands reproduced the same world\n", a.commandLog().size());
    }
    // News travels the web of acquaintance (design/news.md): ties are mutual, a death is known at once by the
    // houses close to it, and old news has reached most of the village.
    {
        Config c;
        c.seed = 5;
        World w(defs, c);
        w.runYears(25);
        for (const auto& p : w.people())
            for (const auto& [id, v] : p.known) {
                auto back = w.person(id).known.find(p.id);
                CHECK(back != w.person(id).known.end() && std::abs(back->second - v) < 0.01, "acquaintance not mutual");
                CHECK(v >= 0 && v <= 100, "acquaintance out of range");
            }
        int live = 0, deaths = 0, wide = 0;
        for (const auto& h : w.households()) live += !h.extinct && !h.members.empty();
        for (const auto& n : w.news()) {
            if (n.kind != "death" || w.today() - n.day < 2 * kDaysPerYear) continue;
            ++deaths;
            wide += static_cast<int>(n.heard.size()) * 2 >= live;
        }
        CHECK(deaths > 0, "no deaths became news");
        CHECK(wide * 10 >= deaths * 8, "deaths did not get round the village");
        CHECK(!w.heard().empty(), "the family heard no news in 25 years");
        std::printf("news: %zu items, %d deaths of which %d reached half the village, family heard %zu\n", w.news().size(), deaths,
                    wide, w.heard().size());
    }
    // The village's land neither grows nor shrinks: it only changes hands.
    {
        Config c;
        c.seed = 9;
        World w(defs, c);
        double before = villageAcres(w);
        int movedStart = 0;
        for (const auto& pc : w.parcels()) movedStart += pc.since > 0;
        w.runYears(60);
        checkInvariants(w, "land");
        CHECK(std::abs(villageAcres(w) - before) < 1e-6, "the village's land changed in size");
        int moved = 0;
        for (const auto& pc : w.parcels()) moved += !pc.gone && pc.since > 0;
        CHECK(moved > movedStart, "no land changed hands in 60 years");
        std::printf("land: %.0f acres in %zu parcels, %d moved in 60 years\n", before, w.parcels().size(), moved);
    }
    // Round 5 C (design/fun-r5.md): impressions that stop growing and fade, asks that can fail and name terms
    // answered on the spot, the season's list of things to do, plans that say why they cannot start.
    {
        Config c;
        c.seed = 72;
        c.prerun = 25;
        World w(defs, c);
        w.runYears(30);
        std::vector<Id> heads;
        for (const auto& h : w.households())
            if (!h.extinct && h.head != kNone && !h.members.empty() && h.id != w.playerHousehold()) heads.push_back(h.head);
        int pairs = 0, fast = 0;
        for (Id a : heads)
            for (Id b : heads) {
                if (a == b) continue;
                ++pairs;
                fast += w.opinion(a, b) >= 90;
                for (const auto& [lab, v] : w.opinionBreakdown(a, b))
                    if (lab == "good_times" || lab == "quarrels") CHECK(std::abs(v) <= 25.01, "everyday impressions grew past their cap");
            }
        CHECK(fast * 20 <= pairs, "most of the village's heads became fast friends");
        std::printf("opinions: %d of %d head pairs at 90 or more after 30 years\n", fast, pairs);
    }
    {
        bool threw = false;
        try {
            parseEvent(Json::parse(R"({"id": "x", "institution": true, "prompt": "a", "prompts": ["a", "b"], "options": [{"text": "t"}]})"));
        } catch (const JsonError&) {
            threw = true;
        }
        CHECK(threw, "an institution with fewer than three ways of telling it was loaded");
    }
    {
        std::map<int, int> answers;
        int cards = 0, agendaDone = 0, agendaShown = 0;
        std::map<std::string, int> kinds;
        for (int seed : {3, 5, 8, 11}) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World w(defs, c);
            bool card = false;
            // The family takes whatever terms are named (the first option of the 開口 card); nothing else is asked.
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "開口") return -1;
                card = true;
                return 0;
            });
            for (int season = 0; season < 20 && !w.gameOver(); ++season) {
                w.startSeason();
                std::vector<World::AgendaItem> items = w.seasonAgenda();
                std::set<std::string> ids;
                CHECK(items.size() <= 4, "the season's list has more than four things");
                for (const auto& a : items) CHECK(ids.insert(a.id).second, "the season's list names one thing twice");
                agendaShown += !items.empty();
                for (const auto& a : items) {
                    if (!a.open) continue;
                    int before = w.timeLeft();
                    w.issue({w.today(), "agenda", w.actingHead(), a.id, kNone, kNone});
                    w.runCommands();
                    CHECK(w.household(w.playerHousehold()).money > -0.01, "a thing on the season's list left the family in the red");
                    CHECK(w.timeLeft() == before - 1, "a thing on the season's list did not take one free day");
                    ++agendaDone;
                    ++kinds[a.id.substr(0, a.id.find(':'))];
                    break;
                }
                for (Id who : w.peopleToSee()) {
                    if (w.playerHead() == kNone || w.timeLeft() < 1) break;
                    for (const auto& o : w.asksFor(who)) {
                        if (!o.open) continue;
                        std::string sub = o.subject == kNone ? "" : std::to_string(o.subject);
                        int before = w.timeLeft();
                        card = false;
                        w.issue({w.today(), "ask", w.playerHead(), o.ask + "|" + o.office + "||" + sub, who, o.object});
                        w.runCommands();
                        CHECK(w.timeLeft() == before - 1, "an ask (with its terms) took other than one day");
                        CHECK(w.household(w.playerHousehold()).money > -0.01, "taking the terms left the family in the red");
                        if (card) {
                            ++cards;
                            CHECK(w.lastAsk().answer != 2 || w.lastAsk().text.find("你") == 0, "terms taken on the card were left hanging");
                        }
                        ++answers[w.lastAsk().answer];
                        break;
                    }
                }
                for (int d = 0; d < kDaysPerYear / 4 && !w.gameOver(); ++d) w.stepDay();
            }
            checkInvariants(w, "season list");
            // A plan clicked for the wrong person says why it cannot start, and takes no day.
            if (!w.gameOver() && w.actingHead() != kNone) {
                w.startSeason();
                int before = w.timeLeft();
                size_t lines = w.chronicle().size();
                w.issue({w.today(), "start_plan", w.actingHead(), "befriend", w.actingHead(), kNone});
                w.runCommands();
                bool said = w.chronicle().size() > lines && w.chronicle().back().text.find("辦不成：") != std::string::npos;
                CHECK(said || before == 0, "a plan that could not start did not say why");
                CHECK(w.timeLeft() == before, "a plan that did not start took a day");
            }
        }
        CHECK(answers[3] + answers[4] > 0, "nobody ever said no to an ask");
        CHECK(cards > 0, "no ask ever named terms to answer on the spot");
        CHECK(agendaShown > 0 && agendaDone > 0, "the season's list was never there to act on");
        std::printf("season list: %d done (", agendaDone);
        for (const auto& [k, n] : kinds) std::printf(" %s %d", k.c_str(), n);
        std::printf(" ); asks: yes %d, terms %d (%d answered on the spot), no %d, offence %d\n", answers[1], answers[2], cards, answers[3], answers[4]);
    }
    // Different seeds -> different histories.
    {
        Config c1, c2;
        c1.seed = 1;
        c2.seed = 2;
        World a(defs, c1), b(defs, c2);
        a.runYears(20);
        b.runYears(20);
        CHECK(a.digest() != b.digest(), "different seeds produced the same world");
    }
    // Long runs across start years and origins stay consistent, and the village does not die out.
    int gentleSurvived = 0, runs = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        for (int start : {1300, 1340, 1370}) {
            Config c;
            c.seed = seed;
            c.startYear = start;
            c.origin = seed % 3 == 0 ? "artisan" : seed % 3 == 1 ? "peasant" : "noble";
            World w(defs, c);
            for (int y = 0; y < 100 && !w.gameOver(); y += 10) {
                w.runYears(10);
                checkInvariants(w, "long run");
            }
            if (w.livingCount() <= 20)
                std::printf("  population %d after 100 years: seed %llu, start %d, origin %s\n", w.livingCount(),
                            static_cast<unsigned long long>(seed), start, c.origin.c_str());
            CHECK(w.livingCount() > 20, "village population collapsed");

            Config g = c;
            g.harshness = Harshness::Gentle;
            World gw(defs, g);
            gw.runYears(60);
            ++runs;
            gentleSurvived += gw.gameOver() ? 0 : 1;
        }
    }
    std::printf("gentle mode: family survived 60 years in %d / %d runs\n", gentleSurvived, runs);
    CHECK(gentleSurvived * 10 >= runs * 9, "gentle mode should keep most families alive");

    // Ambitions follow the family's fortunes (design/fun-r5.md D). Every head made rich and asked again: none takes
    // up a goal with half its road already behind it, and a money goal is not met the day it is chosen. Every post's
    // conditions are listed, and what bars a man is the first one he does not meet.
    {
        Config c;
        c.seed = 11;
        World w(defs, c);
        w.runYears(15);
        int chosen = 0, halfDone = 0, metAtOnce = 0;
        std::map<std::string, int> picks;
        for (const auto& hc : w.households()) {
            if (hc.extinct || hc.head == kNone || hc.members.empty() || w.ageYears(w.person(hc.head)) < 16) continue;
            Id head = hc.head;
            WorldTest::house(w, hc.id).money = 3000;
            WorldTest::choose(w, head);
            const Person& p = w.person(head);
            const AmbitionDef* a = w.defs().ambition(p.ambition);
            if (!a) continue;
            ++chosen;
            ++picks[a->id];
            EventRunner ev(w);
            int steps = 1, done = 0;
            for (const auto& m : a->milestones)
                if (!m.gate) ++steps, done += ev.eval(m.goal, p, kNone);
            halfDone += done > 0 && done * 2 >= steps && steps > 2;
            metAtOnce += ev.eval(a->goal, p, kNone) || (a->id == "save_money" && ev.eval(a->milestones[0].goal, p, kNone));
            if (a->id == "chantry") CHECK(w.standing(hc.id) == 3, "a chantry goal offered to a house that can already pay for it");
        }
        std::string top;
        for (const auto& [id, n] : picks) top += " " + id + ":" + std::to_string(n);
        std::printf("ambitions: %d rich heads chose:%s\n", chosen, top.c_str());
        CHECK(chosen >= 10, "too few heads chose an ambition");
        CHECK(halfDone == 0, "an ambition taken up with half its road already behind it");
        CHECK(metAtOnce == 0, "a goal met the day it was chosen");
        int people = 0;
        for (const auto& q : w.people())
            for (const char* of : {"reeve", "aletaster", "hayward"}) {
                if (!q.alive()) continue;
                ++people;
                std::vector<OfficeGate> gs = w.officeGates(q.id, of);
                std::string first;
                for (const auto& g : gs)
                    if (!g.ok && first.empty()) first = g.detail;
                CHECK(!gs.empty() && first == w.officeBar(q.id, of), "officeBar is not the first unmet condition");
            }
        CHECK(people > 100, "no one to check the posts on");
    }
    // The late game: a small landholder lends the manor a large sum; when the manor cannot pay at term, land or the
    // mill passes to the family (the rules answer for the player). A rival bids for the family's land, and the
    // lord's family may marry into the house.
    {
        Config c;
        c.seed = 7;
        World w(defs, c);
        w.runYears(3);
        Id hid = w.playerHousehold();
        WorldTest::makeFranklin(w, hid);
        CHECK(w.standing(hid) == 3, "the family is not a small landholder after the setup");
        Id gent = kNone;
        for (const auto& q : w.people())
            if (q.alive() && q.household != kNone && w.gentry(q.id) && q.occupation != "lord") gent = q.id;
        Id kid = kNone;
        for (Id m : w.household(hid).members)
            if (m != w.playerHead() && w.person(m).dynasty == w.playerDynasty()) kid = m;
        if (gent != kNone && kid != kNone) CHECK(w.mayWed(kid, gent), "a small landholder the lord favours may not ask for his family's hand");
        WorldTest::house(w, hid).money = 3000;
        WorldTest::setAmbition(w, w.playerHead(), "manor_creditor");
        auto n = [&](const std::string& k) { return w.counters().count(k) ? w.counters().at(k) : 0; };
        for (int d = 0; d < 12 * kDaysPerYear && !w.gameOver() && n("manor_loan:land") + n("manor_loan:mill") == 0; ++d) {
            w.stepDay();
            if (w.person(w.playerHead()).ambition != "manor_creditor" && w.manorBond().house == kNone) {
                WorldTest::house(w, w.playerHousehold()).money += 1000;
                WorldTest::setAmbition(w, w.playerHead(), "manor_creditor");  // repaid: lend again
            }
        }
        std::printf("late game: lent %d, land %d, mill %d, repaid %d\n", n("manor_loan:lent"), n("manor_loan:land"), n("manor_loan:mill"), n("manor_loan:repaid"));
        CHECK(n("manor_loan:lent") >= 1, "the steward never came to borrow");
        CHECK(n("manor_loan:land") + n("manor_loan:mill") >= 1, "the manor never pledged land or the mill");
        CHECK(n("manor_loan:lent") <= n("manor_loan:land") + n("manor_loan:mill") + n("manor_loan:repaid") + 1, "a loan to the manor never came due");
        checkInvariants(w, "manor loan");
        // 「村裡第一大戶」: a rival house bids for one of the family's parcels; refused (the rules), it goes after it.
        hid = w.playerHousehold();
        WorldTest::setAmbition(w, w.playerHead(), "first_house");
        if (Id big = w.biggestOther(hid); big != kNone) WorldTest::house(w, big).money += 2000;
        size_t before = w.chronicle().size();
        WorldTest::rivalForLand(w);
        bool bid = false, pressed = false;
        for (size_t i = before; i < w.chronicle().size(); ++i) bid = bid || w.chronicle()[i].text.find("〔搶地〕") != std::string::npos;
        for (const auto& wt : w.wants())
            pressed = pressed || (wt.open && wt.kind == "parcel" && wt.house != hid && w.parcels()[wt.object].holder == hid);
        CHECK(bid, "no rival came for the family's land");
        CHECK(pressed, "a rival refused his bid did not go after the land");
        checkInvariants(w, "first house");
    }

    if (failures == 0) std::printf("all tests passed\n");
    return failures == 0 ? 0 : 1;
}
