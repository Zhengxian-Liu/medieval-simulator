// Headless tests for SimCore: determinism and state invariants over long runs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
    static void kill(World& w, Id id, const std::string& cause) { w.kill(id, cause); }
    static void marry(World& w, Id a, Id b) { w.marry(a, b); }
    static void yearlyWants(World& w) { w.yearlyWants(); }
    static std::vector<Want>& wants(World& w) { return w.wants_; }
    static std::vector<Feud>& feuds(World& w) { return w.feuds_; }
    static void feudTurn(World& w, size_t i) { w.feudTurn(i); }
    static void setStation(World& w, Id id, const std::string& st, int years, const std::string& note) { w.setStation(id, st, years, note); }
    static Id heirOf(World& w, Id hid) { return w.heirOf(w.households_[hid], w.households_[hid].head); }
    static ManorBond& bond(World& w) { return w.manorBond_; }
    static void sendToService(World& w, Id who, Id master) {
        w.moveTo(who, master);
        w.people_[who].home = w.playerHousehold();
        w.people_[who].flags.insert("in_service");
    }
    static void giveLand(World& w, Id hid, double acres) {
        w.moveLand(w.people_[w.lord_].household, hid, acres, LandPick::CustomaryFirst, "test");
    }

    static void moveTo(World& w, Id id, Id house) { w.moveTo(id, house); }
    static double rent(World& w, Id house) { return w.rentOf(w.households_[house]); }
    static void houseStyle(World& w, Id house) { w.askHouseStyle(w.households_[house], 6, 40); }
    static Id houseLine(World& w, Id head) { return w.houseLine(w.people_[head]); }
    static void runEvent(World& w, const std::string& id, Id p, Id t) { w.runEventById(id, p, t); }
    static void addDebt(World& w, Id house, double amount, Id creditor) { w.addDebt(w.households_[house], amount, creditor); }
    static std::string startPlan(World& w, Id actor, const std::string& plan, Id target) { return w.startPlan(actor, plan, target, kNone); }
    static bool failPlan(World& w, Id actor, const std::string& plan) {
        for (size_t i = 0; i < w.plans_.size(); ++i)
            if (w.plans_[i].actor == actor && w.plans_[i].plan == plan) return w.endPlan(i, "fail"), true;
        return false;
    }
    static int toHarvest(World& w) { return w.daysToHarvest(); }
    static void yearlyFeuds(World& w) { w.yearlyFeuds(); }
    static void yearlyFeudTurns(World& w) { w.yearlyFeudTurns(); }
    static void takeOverScene(World& w, Id newHead, Id from) { w.takeOverScene(w.households_[w.playerHousehold_], newHead, from, false); }
    static std::vector<ThreadEnd>& threadEnds(World& w) { return w.threadEnds_; }
    static std::vector<Memory>& memories(World& w, Id id) { return w.memories_[id]; }
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
                    // A gift is offered only where it can turn the answer (design/fun-r6.md A: 13 pence spent in 1335
                    // against 「祖上傳下來的地」); when none is offered after a no, the reason is there to be told.
                    int first = w.lastAsk().answer;
                    const auto& lv = w.lastAsk().levers;
                    bool gift = std::find(lv.begin(), lv.end(), "gift") != lv.end();
                    if (first == 3 && !gift) CHECK(!w.lastAsk().noLever.empty() || w.household(w.playerHousehold()).money < 12, "a no without a gift lever did not say why");
                    for (const auto& lever : std::vector<std::string>(w.lastAsk().levers)) {
                        int before = w.timeLeft();
                        std::string said = w.lastAsk().text;
                        w.issue({w.today(), "ask", w.playerHead(), o.ask + "|" + o.office + "|" + lever + "|" + sub, who, o.object});
                        w.runCommands();
                        CHECK(w.household(w.playerHousehold()).money > -0.01, "pressing an ask left the family in the red");
                        if (w.lastAsk().text == said) break;  // not pressed (no money for the lever)
                        CHECK(w.timeLeft() == before, "pressing an ask took another day (it is the same talk)");
                        if (lever == "gift") {
                            ++presses["gift_" + std::to_string(w.lastAsk().answer)];
                            CHECK(w.lastAsk().answer != 3, "a gift was offered that could not turn the no");
                        }
                        ++presses[lever];
                        break;  // one press per answer
                    }
                    // A press that cannot be made says which thing it lacks.
                    for (const char* l : {"gift", "favour", "hook", "meet"})
                        if (std::find(w.lastAsk().levers.begin(), w.lastAsk().levers.end(), l) == w.lastAsk().levers.end())
                            CHECK(!w.pressWhy(who, o.ask, o.object, o.subject, l).empty(), "a press that cannot be made gave no reason");
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
            // (What the card bought, from its line: a neighbour's sack the same day is not the card's doing.)
            auto lastGrainLine = [&] {
                for (size_t i = w.chronicle().size(); i-- > 0;)
                    if (w.chronicle()[i].text.rfind("〔買糧〕", 0) == 0) return w.chronicle()[i].text;
                return std::string();
            };
            CHECK(cardAt(3, 20) == 1 && lastGrainLine().find("買了 30 天的糧") != std::string::npos, "a month's grain was not bought on the card");
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
        World sickBase = w;  // before the day's work is done (it is not offered twice in a season)
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
            bool again = false;
            for (const auto& a : w.seasonAgenda()) again = again || a.id == "spare:day";
            CHECK(!again, "the day's work was offered a second time in the same season");
        }
        // A day's work is only at a house really short of hands (or the manor); a house all of whose hands are
        // already hired out by the month is not paid twice for the same days.
        for (const auto& a : sickBase.seasonAgenda())
            if (a.id == "spare:day" && a.target != sickBase.lordId()) {
                const Household& o = sickBase.household(sickBase.person(a.target).household);
                double hands = 0;
                for (Id m : o.members)
                    if (sickBase.ageYears(sickBase.person(m)) >= 14 && sickBase.person(m).health >= 40) hands += sickBase.person(m).sex == Sex::Male ? 1.0 : 0.5;
                CHECK(o.acres / 12 - hands >= 0.5, "a house with hands enough was said to be short of them");
            }
        CHECK(sickBase.spareHands(sickBase.household(hh)) < sickBase.farmHands(sickBase.household(hh)) || day == nullptr,
              "day work offered to a house whose every hand is already hired out by the month");
        // A head too sick to work: the fittest grown member of the house goes instead.
        {
            World sick = sickBase;
            const Id head = sick.actingHead();
            sick.setHealthForTest(head, 20);
            sick.setMoneyForTest(hh, 5);
            Id other = kNone;
            for (Id m : sick.household(hh).members)
                if (m != head && sick.ageYears(sick.person(m)) >= 14 && sick.person(m).health >= 40) other = m;
            const World::AgendaItem* job = nullptr;
            std::vector<World::AgendaItem> list = sick.seasonAgenda();
            for (const auto& a : list)
                if (a.id == "spare:day") job = &a;
            if (other != kNone) {
                CHECK(job != nullptr, "with the head sick in bed, nobody else in the house was offered the day's work");
                if (job) {
                    sick.issue({sick.today(), "agenda", head, job->id, kNone, kNone});
                    sick.runCommands();
                    CHECK(sick.chronicle().back().text.rfind(sick.fullName(head), 0) != 0 && sick.chronicle().back().text.find("拿到") != std::string::npos,
                          "the sick head went to the day's work himself");
                }
            }
        }
        w.setMoneyForTest(hh, 500);
        w.setFoodForTest(hh, 400);
        bool still = false;
        for (const auto& a : w.seasonAgenda()) still = still || a.id.rfind("spare:", 0) == 0;
        CHECK(!still, "a house with coin and bread to spare is offered day labour");
    }
    // r6 review (money/food): a house with coin never sits on an empty barn. The grain card is asked on the first of
    // the month: whatever is answered, the barn lasts to the next first while the purse can buy bread (seed 58, 1330s:
    // 22 days empty holding 405 pence).
    for (const char* pick : {"先買一個月的", "勒緊褲帶"}) {
        Config c;
        c.seed = 58;
        c.prerun = 25;
        World w(defs, c);
        const Id hh = w.playerHousehold();
        w.setMoneyForTest(hh, 500);
        w.setFoodForTest(hh, 50);
        std::string want = pick;
        w.setChooser([&](const World::ChoicePrompt& p) {
            if (p.title != "買糧") return -1;
            for (size_t k = 0; k < p.options.size(); ++k)
                if (p.options[k].find(want) != std::string::npos) return static_cast<int>(k);
            return -1;
        });
        int empty = 0;
        const double price = w.param("grain_price", 0.5);
        for (int d = 0; d < 14 * kDaysPerMonth && !w.gameOver(); ++d) {
            w.stepDay();
            const Household& h = w.household(w.playerHousehold());
            if (h.food <= 0.01 && h.money >= w.dailyBread(h.id) * 30 * price) ++empty;
        }
        w.setChooser(nullptr);
        if (empty) std::printf("  (%s) %d days with an empty barn and coin for a month's bread\n", pick, empty);
        CHECK(empty == 0, "the family sat on an empty barn with money in the purse");
    }
    // r6 review: the manor's own money is booked, so playing the manor house leaves nothing 「沒記上帳的」.
    {
        Config c;
        c.seed = 58;
        c.origin = "noble";
        World w(defs, c);
        w.runYears(2);
        if (w.unbookedMoney() >= 1) std::printf("  noble: %.1f pence unbooked\n", w.unbookedMoney());
        CHECK(w.unbookedMoney() < 1, "money the manor house took in was not booked");
    }
    // r6 review: a mouth taken in is counted as the barn counts it.
    {
        Config c;
        c.seed = 58;
        World w(defs, c);
        const Household& h = w.household(w.playerHousehold());
        double sum = 0;
        for (Id m : h.members) sum += w.portion(w.person(m));
        CHECK(std::abs(sum - w.dailyBread(h.id)) < 1e-9, "a person's portion is not what the barn counts");
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
                // (not another house's child in service here: their year is their own family's to set)
                if (age >= 5 && age < 10 && v.person(m).home == kNone) ks.push_back(m);
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
                    // Pressed again once done (a stale button): told so, and no day taken.
                    auto now = w.seasonAgenda(true);
                    if (std::none_of(now.begin(), now.end(), [&](const World::AgendaItem& x) { return x.id == a.id; })) {
                        size_t lines = w.chronicle().size();
                        int left = w.timeLeft();
                        w.issue({w.today(), "agenda", w.actingHead(), a.id, kNone, kNone});
                        w.runCommands();
                        CHECK(w.timeLeft() == left, "a done thing pressed again took a day");
                        CHECK(w.chronicle().size() > lines && w.chronicle().back().text.find("已經辦過了") != std::string::npos,
                              "a done thing pressed again was not told as done");
                    }
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
        // The manor pays at term about half the time, when it has the coin: here what it owes is more than its chest
        // could hold, so the term comes to a pledge however the dice fall (the odds are not what this test is about).
        for (int d = 0; d < 20 * kDaysPerYear && !w.gameOver() && n("manor_loan:land") + n("manor_loan:mill") == 0; ++d) {
            if (WorldTest::bond(w).house != kNone) WorldTest::bond(w).owed = 1e7;
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

    // ---- Round 6 D: every generation has a thread, and threads end (design/fun-r6.md D) ----
    auto said = [](const World& w, size_t from, const std::string& what) {
        for (size_t i = from; i < w.chronicle().size(); ++i)
            if (w.chronicle()[i].text.find(what) != std::string::npos) return true;
        return false;
    };
    // The family's own want, carried over from before the hand-over, is not dropped as stale on the first day.
    {
        Config c;
        c.seed = 91;
        c.prerun = 25;
        World w(defs, c);
        Id hid = w.playerHousehold();
        Id parcel = kNone;
        for (const auto& pc : w.parcels())
            if (!pc.gone && pc.holder != hid && pc.holder != kNone) parcel = pc.id;
        auto& ws = WorldTest::wants(w);
        Want old;
        old.id = static_cast<Id>(ws.size());
        old.house = hid, old.kind = "parcel", old.object = parcel, old.why = "test", old.since = w.today() - 10 * kDaysPerYear;
        ws.push_back(old);
        WorldTest::yearlyWants(w);
        CHECK(WorldTest::wants(w)[old.id].open, "the family's own want was dropped for being old");
    }
    // An ambition reached is said the same day, not at the next month's review.
    {
        Config c;
        c.seed = 64;
        World w(defs, c);
        w.runYears(2);
        while (w.dayOfMonth() != 10) w.stepDay();  // mid-month: the monthly review is not what tells it
        Id hid = w.playerHousehold();
        WorldTest::setAmbition(w, w.playerHead(), "more_land");
        size_t before = w.chronicle().size();
        WorldTest::giveLand(w, hid, std::max(5.0, std::ceil(w.household(hid).acres * 0.25)) + 1);
        w.stepDay();
        CHECK(said(w, before, "完成了志向「多種幾畝地」"), "a goal reached in the middle of a month was not told that day");
    }
    // Never offer an ambition its own check rejects: whatever a head is offered, its road is open at that moment.
    {
        for (uint64_t seed : {7ull, 64ull}) {
            Config c;
            c.seed = seed;
            World w(defs, c);
            EventRunner ev(const_cast<World&>(w));
            int bad = 0;
            for (int y = 0; y < 30 && !w.gameOver(); ++y) {
                w.runYears(1);
                for (const auto& h : w.households()) {
                    if (h.extinct || h.head == kNone) continue;
                    const Person& hp = w.person(h.head);
                    for (const auto& a : defs.ambitions)
                        if (!a.keep.isNull() && ev.eval(a.offer, hp, kNone) && !ev.eval(a.keep, hp, kNone)) {
                            if (bad++ < 3) std::printf("  offered but closed: %s for %s\n", a.id.c_str(), w.fullName(hp.id).c_str());
                        }
                }
            }
            CHECK(bad == 0, "an ambition was offered whose own check would close it");
        }
    }
    // 送一個兒子進教會 counts the house's boys, not only the head's own; the heir is not one of them; and when the road
    // closes the line says why.
    {
        Config c;
        c.seed = 72;
        World w(defs, c);
        w.runYears(3);
        Id hid = w.playerHousehold();
        Id head = w.playerHead();
        Id heir = WorldTest::heirOf(w, hid);
        EventRunner ev(w);
        Json boy = Json::parse(R"({"house_boy": {}})");
        if (heir != kNone) {
            bool others = false;
            for (Id m : w.household(hid).members)
                others = others || (m != head && m != heir && w.person(m).sex == Sex::Male && w.person(m).dynasty == w.household(hid).dynasty &&
                                    w.person(m).home == kNone);
            CHECK(ev.eval(boy, w.person(head), kNone) == others, "house_boy does not match the boys at home other than the heir");
        }
        // Take every boy but the heir out of the house: the church road closes, and the line says why.
        for (Id m : std::vector<Id>(w.household(hid).members))
            if (m != head && m != heir && w.person(m).sex == Sex::Male) WorldTest::kill(w, m, "意外");
        WorldTest::setAmbition(w, w.playerHead(), "son_to_church");
        size_t before = w.chronicle().size();
        w.runCommands();
        if (!w.kinInChurch(w.playerDynasty()))
            CHECK(said(w, before, "家裡已經沒有能送進教會的男孩了"), "the church ambition closed without saying why (or did not close)");
    }
    // A feud with a second wrong in it is told as the family's thread, shows in 這一代的線, and a wedding ends it, said so.
    {
        Config c;
        c.seed = 64;
        World w(defs, c);
        w.runYears(2);
        Id head = w.playerHead();
        Id hid = w.playerHousehold();
        Id foe = kNone, bride = kNone, groom = kNone;
        Id lordDyn = w.person(w.lordId()).dynasty;
        for (const auto& h : w.households()) {
            if (h.extinct || h.head == kNone || h.id == hid || h.dynasty == w.playerDynasty() || h.dynasty == lordDyn) continue;
            if (w.person(h.head).occupation == "priest") continue;
            foe = h.head;
            break;
        }
        CHECK(foe != kNone, "no other house to quarrel with");
        if (foe != kNone) {
            size_t before = w.chronicle().size();
            w.recordDeed("public_insult", foe, head);
            w.recordDeed("public_insult", foe, head);
            CHECK(said(w, before, "有了嫌隙"), "a quarrel with two wrongs in it was not told as a thread");
            bool listed = false;
            for (const auto& t : w.threads()) listed = listed || t.kind == "feud";
            CHECK(listed, "the feud is not in 這一代的線");
            // The new head is asked whether to take on the old quarrel; with a yes to peace it ends, and is said to.
            auto& fs = WorldTest::feuds(w);
            size_t fi = fs.size();
            for (size_t i = 0; i < fs.size(); ++i)
                if (fs[i].told && (fs[i].a == w.playerDynasty() || fs[i].b == w.playerDynasty())) fi = i;
            CHECK(fi < fs.size(), "no told feud");
            if (fi < fs.size()) {
                fs[fi].ourFace = foe;  // as if another had headed the family when it began
                w.setChooser([](const World::ChoicePrompt& p) { return p.title == "上一代的帳" ? 0 : -1; });
                before = w.chronicle().size();
                WorldTest::feudTurn(w, fi);
                CHECK(said(w, before, "【恩怨了結】"), "the new head's peace with the other house was not told as an end");
                CHECK(!fs[fi].told && fs[fi].stage == 0, "the feud did not end on the new head's peace");
                // Again, and this time to the manor court: either way the verdict ends it.
                w.recordDeed("public_insult", foe, head);
                w.recordDeed("public_insult", foe, head);
                w.setChooser([](const World::ChoicePrompt& p) {
                    for (size_t k = 0; k < p.options.size(); ++k)
                        if (p.options[k].find("莊園法庭") != std::string::npos) return static_cast<int>(k);
                    return -1;
                });
                fs[fi].nextTurn = 0;
                before = w.chronicle().size();
                WorldTest::feudTurn(w, fi);
                CHECK(said(w, before, "【莊園法庭】") && said(w, before, "【恩怨了結】"), "the manor court's verdict did not end the feud");
                w.setChooser(nullptr);
                // A third time, and a wedding between the two houses ends it.
                w.recordDeed("public_insult", foe, head);
                w.recordDeed("public_insult", foe, head);
                Id fh = w.person(foe).household;
                for (Id m : w.household(fh).members)
                    if (w.person(m).spouse == kNone && w.person(m).sex == Sex::Female && bride == kNone) bride = m;
                for (Id m : w.household(hid).members)
                    if (w.person(m).spouse == kNone && w.person(m).sex == Sex::Male && groom == kNone) groom = m;
                if (bride != kNone && groom != kNone) {
                    before = w.chronicle().size();
                    WorldTest::marry(w, groom, bride);
                    CHECK(said(w, before, "兩家結了親"), "a wedding between feuding houses did not end the feud");
                }
                bool kept = false;
                for (const auto& e : w.threadEnds()) kept = kept || e.kind == "feud";
                CHECK(kept, "the feud's end was not kept for the generation's account");
            }
            checkInvariants(w, "feud thread");
        }
    }
    // The deathbed speaks to the real heir, and the hand-over says in one place what the new head takes on.
    {
        bool tried = false;
        for (uint64_t seed : {64ull, 72ull, 7ull, 91ull}) {
            Config c;
            c.seed = seed;
            World w(defs, c);
            w.runYears(4);
            Id hid = w.playerHousehold();
            Id head = w.playerHead();
            Id heir = WorldTest::heirOf(w, hid);
            if (heir == kNone || w.person(heir).household == kNone || w.ageYears(w.person(heir)) < 12) continue;
            tried = true;
            std::string heirName = w.fullName(heir);
            w.setChooser([](const World::ChoicePrompt& p) {
                if (p.title != "床前") return -1;
                for (size_t k = 0; k < p.options.size(); ++k)
                    if (p.options[k].find("叮囑") != std::string::npos) return static_cast<int>(k);
                return -1;
            });
            size_t before = w.chronicle().size();
            WorldTest::kill(w, head, "熱病");
            w.setChooser(nullptr);
            bool toHeir = false;
            for (size_t i = before; i < w.chronicle().size(); ++i)
                toHeir = toHeir || (w.chronicle()[i].text.find("對 " + heirName) != std::string::npos);
            CHECK(toHeir, "the deathbed words were not said to the heir");
            CHECK(said(w, before, "【接下的】"), "the hand-over did not say what the new head takes on");
            CHECK(said(w, before, "記得的"), "the hand-over did not say what the new head remembers of the old");
            // The scene comes after the lord's dues, so what it says is what is left; and the estate is said once.
            size_t scene = 0, dues = 0;
            for (size_t i = before; i < w.chronicle().size(); ++i) {
                const std::string& t = w.chronicle()[i].text;
                if (t.find("【接下的】") != std::string::npos) scene = i;
                if (t.find("【接下的】") == std::string::npos && (t.find("死後稅") != std::string::npos || t.find("入地費") != std::string::npos))
                    dues = std::max(dues, i);
            }
            CHECK(dues == 0 || scene > dues, "the hand-over scene was told before the death duties");
            CHECK(!said(w, before, "【分家產】"), "the estate is told twice (【分家產】 and 【接下的】)");
            checkInvariants(w, "deathbed");
            break;
        }
        CHECK(tried, "no seed had a grown heir to test the deathbed with");
    }
    // ---- r6 review fixes (events, feuds, generations) ----
    {
        Config c;
        c.seed = 64;
        World base(defs, c);
        base.runYears(2);
        Id lordDyn = base.person(base.lordId()).dynasty;
        std::vector<Id> foes;  // heads of two other families
        for (const auto& h : base.households()) {
            if (h.extinct || h.head == kNone || h.id == base.playerHousehold() || h.dynasty == base.playerDynasty() || h.dynasty == lordDyn) continue;
            if (base.person(h.head).occupation == "priest") continue;
            bool dup = false;
            for (Id f : foes) dup = dup || base.person(f).dynasty == h.dynasty;
            if (!dup) foes.push_back(h.head);
        }
        CHECK(foes.size() >= 2, "no two other houses to quarrel with");
        auto feudWith = [](World& w, Id foe) {
            auto& fs = WorldTest::feuds(w);
            Id a = w.playerDynasty(), b = w.person(foe).dynasty;
            for (size_t i = 0; i < fs.size(); ++i)
                if ((fs[i].a == a && fs[i].b == b) || (fs[i].a == b && fs[i].b == a)) return i;
            return fs.size();
        };
        if (foes.size() >= 2) {
            const Id foe = foes[0];
            // 1. Who headed the family is set when the quarrel is told, not by a wrong nobody was told of; a quarrel the
            // head began is not asked about as 「上一代的帳」.
            {
                World w = base;
                Id head = w.playerHead();
                w.recordDeed("public_insult", foe, head);
                size_t fi = feudWith(w, foe);
                CHECK(fi < WorldTest::feuds(w).size() && !WorldTest::feuds(w)[fi].told && WorldTest::feuds(w)[fi].ourFace == kNone,
                      "an untold coolness already names who headed the family");
                // An old head's name left on a coolness that faded and came back: reset with its cause.
                if (fi < WorldTest::feuds(w).size()) {
                    auto& f = WorldTest::feuds(w)[fi];
                    f.heat = 0, f.stage = 0, f.ourFace = foe;
                }
                w.recordDeed("public_insult", foe, head);
                w.recordDeed("public_insult", foe, head);
                auto& f = WorldTest::feuds(w)[fi];
                CHECK(f.told && f.ourFace == w.actingHead(), "the quarrel told under this head does not name this head");
                std::string asked;
                w.setChooser([&](const World::ChoicePrompt& p) { asked += p.title + "|"; return -1; });
                f.nextTurn = 0;
                WorldTest::feudTurn(w, fi);
                w.setChooser(nullptr);
                CHECK(asked.find("上一代的帳") == std::string::npos, "a quarrel the head began was asked about as the last generation's");
            }
            // 2. The priest's truce stands: no 「過去了」 in the same breath, and the quarrel stays the family's (休兵).
            // 3. A quarrel that faded is over: a wrong after it starts a new one with its own cause.
            {
                World w = base;
                Id head = w.playerHead();
                w.recordDeed("public_insult", foe, head);
                w.recordDeed("public_insult", foe, head);
                size_t fi = feudWith(w, foe);
                CHECK(fi < WorldTest::feuds(w).size() && WorldTest::feuds(w)[fi].told, "two insults were not told as a quarrel");
                WorldTest::feuds(w)[fi].heat = 25;  // cooled by the truce to below a coolness: no fading said with it
                size_t before = w.chronicle().size();
                w.recordDeed("mediated", head, foe);
                CHECK(!said(w, before, "過去了") && !said(w, before, "淡了"), "a priest's truce was told as the quarrel fading");
                CHECK(WorldTest::feuds(w)[fi].told, "a priest's truce dropped the quarrel from the family's threads");
                bool truce = false;
                for (const auto& t : w.threads()) truce = truce || (t.kind == "feud" && t.text.find("暫時休兵") != std::string::npos);
                CHECK(truce, "the truce is not shown on the quarrel's thread");
                CHECK(WorldTest::feuds(w)[fi].stage == 0, "the test truce did not cool the quarrel to nothing");
                // Two quiet years on (no wrong since the handshake), it is over, said as after the priest.
                WorldTest::yearlyFeuds(w);
                CHECK(WorldTest::feuds(w)[fi].told, "a truce was told as over the next year");
                WorldTest::feuds(w)[fi].truceUntil -= 2 * kDaysPerYear;
                WorldTest::feuds(w)[fi].last = kNone;
                WorldTest::yearlyFeuds(w);
                CHECK(said(w, before, "神父調解之後") && !WorldTest::feuds(w)[fi].told, "a truce that held was never told as the end");
                CHECK(WorldTest::feuds(w)[fi].heat == 0, "a faded quarrel kept its heat");
                Id d = w.recordDeed("public_insult", foe, head);
                CHECK(WorldTest::feuds(w)[fi].cause == d, "a wrong after a quarrel faded brought back the old cause");
                // The fade by years (no truce) zeroes it too.
                w.recordDeed("public_insult", foe, head);
                auto& f = WorldTest::feuds(w)[fi];
                CHECK(f.told, "the second quarrel was not told");
                f.truceUntil = -1;
                f.heat = 13, f.stage = 1;
                before = w.chronicle().size();
                WorldTest::yearlyFeuds(w);
                CHECK(said(w, before, "慢慢也就過去了") && f.heat == 0 && !f.told, "a quarrel that faded by years kept its heat");
            }
            // 6. A hot quarrel with nothing to ask does not keep a cooler one from its turn.
            {
                World w = base;
                Id head = w.playerHead();
                for (Id x : {foes[0], foes[1]}) w.recordDeed("public_insult", x, head), w.recordDeed("public_insult", x, head);
                size_t hot = feudWith(w, foes[0]), cool = feudWith(w, foes[1]);
                auto& fs = WorldTest::feuds(w);
                CHECK(hot < fs.size() && cool < fs.size() && fs[hot].told && fs[cool].told, "two quarrels were not told");
                if (hot < fs.size() && cool < fs.size()) {
                    fs[hot].heat = 80, fs[hot].last = kNone, fs[hot].ourFace = w.actingHead(), fs[hot].nextTurn = 0;
                    fs[cool].heat = 25, fs[cool].ourFace = foes[1], fs[cool].nextTurn = 0;  // as if begun under another head
                    std::string asked;
                    w.setChooser([&](const World::ChoicePrompt& p) { asked += p.title + "|"; return -1; });
                    WorldTest::yearlyFeudTurns(w);
                    w.setChooser(nullptr);
                    CHECK(asked.find("上一代的帳") != std::string::npos, "a hot quarrel with nothing new kept a cooler one from its turn");
                }
            }
            // 11. The one it was with died years ago: no wake offered, a visit to the grave.
            {
                World w = base;
                Id head = w.playerHead();
                w.recordDeed("public_insult", foe, head);
                w.recordDeed("public_insult", foe, head);
                size_t fi = feudWith(w, foe);
                Id other = kNone;  // someone else of that family to speak for it
                for (const auto& h : w.households())
                    if (!h.extinct && h.head != kNone && h.head != foe && h.dynasty == w.person(foe).dynasty) other = h.head;
                for (Id m : w.household(w.person(foe).household).members)
                    if (other == kNone && m != foe && w.ageYears(w.person(m)) >= 16) other = m;
                if (fi < WorldTest::feuds(w).size() && other != kNone) {
                    WorldTest::kill(w, foe, "熱病");
                    WorldTest::person(w, foe).deathDay -= 2 * kDaysPerYear;
                    WorldTest::feuds(w)[fi].face = foe;
                    std::string opts;
                    w.setChooser([&](const World::ChoicePrompt& p) {
                        if (p.title == "人死帳了？")
                            for (const auto& o : p.options) opts += o + "|";
                        return -1;
                    });
                    WorldTest::feudTurn(w, fi);
                    w.setChooser(nullptr);
                    CHECK(!opts.empty() && opts.find("守靈") == std::string::npos && opts.find("墳前") != std::string::npos,
                          "a death years ago still offered a wake");
                }
            }
        }
        // 4. 家用排場: nobody answering a re-ask keeps the house's way (no silent change), and five quiet years with
        // nothing changed do not bring the same card again.
        {
            World w = base;
            w.runYears(5);  // (days to count six years back from)
            Id hid = w.playerHousehold();
            WorldTest::house(w, hid).money = 400;
            WorldTest::house(w, hid).styleAsked = -1;
            w.setChooser([](const World::ChoicePrompt& p) { return p.title == "家用排場" ? 1 : -1; });
            WorldTest::houseStyle(w, hid);
            CHECK(w.household(hid).frugal, "the house did not take up saving");
            int asks = 0;
            w.setChooser([&](const World::ChoicePrompt& p) { asks += p.title == "家用排場"; return -1; });
            auto& h = WorldTest::house(w, hid);
            h.styleAsked -= 6 * kDaysPerYear;
            WorldTest::houseStyle(w, hid);
            CHECK(asks == 0, "the house was asked again with nothing changed");
            h.styleLapsed = true;
            size_t before = w.chronicle().size();
            WorldTest::houseStyle(w, hid);
            CHECK(asks == 1, "the house was not asked again after its purse ran low and filled");
            CHECK(w.household(hid).frugal && !said(w, before, "〔家用排場〕"), "nobody answering changed the house's way of living");
            w.setChooser(nullptr);
        }
        // 8, 9. The words quoted at the hand-over are the ones said (to a child, 「長大了」); the ends kept are the
        // quarrels and ambitions first.
        {
            World w = base;
            Id head = w.playerHead();
            Id heir = WorldTest::heirOf(w, w.playerHousehold());
            if (heir != kNone) {
                WorldTest::person(w, heir).flags.insert("father_grudges");
                WorldTest::person(w, heir).flags.insert("deathbed_child");
                auto& ends = WorldTest::threadEnds(w);
                for (int k = 0; k < 5; ++k) ends.push_back({w.today(), head, "path", "路" + std::to_string(k)});
                ends.insert(ends.begin(), {w.today() - 100, head, "feud", "和某家的恩怨：淡了"});
                size_t before = w.chronicle().size();
                WorldTest::takeOverScene(w, heir, head);
                CHECK(said(w, before, "你長大了要記著") && !said(w, before, "你給我記著"), "the deathbed words to a child were misquoted");
                CHECK(said(w, before, "和某家的恩怨：淡了"), "a quarrel's end was dropped from the generation's account for older roads");
                CHECK(said(w, before, "路4") && !said(w, before, "路0"), "the generation's account kept the oldest roads, not the latest");
            }
        }
        // 10. One who can never come back for the land (a monk) does not count as an heir.
        {
            World w = base;
            Id head = w.playerHead();
            EventRunner ev(w);
            Json q = Json::parse(R"({"heir_at_home": true})");
            Id son = kNone;
            for (Id k : w.person(head).children)
                if (w.person(k).alive() && w.person(k).household == w.playerHousehold() && w.person(k).sex == Sex::Male) son = k;
            if (son != kNone) {
                std::vector<Id> others;
                for (const auto& p : w.people())
                    if (p.alive() && p.id != head && p.id != son && p.dynasty == w.person(head).dynasty && p.id != w.person(head).spouse)
                        others.push_back(p.id);
                for (Id x : others) WorldTest::kill(w, x, "意外（test）");
                CHECK(ev.eval(q, w.person(head), kNone), "the son at home is not counted as an heir");
                WorldTest::setStation(w, son, "monastery", 0, "進了修道院");
                CHECK(!ev.eval(q, w.person(head), kNone), "a monk was counted as an heir at home");
            }
        }
        // 11. A road with no term set does not promise a term's end.
        {
            World w = base;
            Id hid = w.playerHousehold();
            Id kid = kNone;
            for (Id m : w.household(hid).members)
                if (m != w.playerHead() && w.ageYears(w.person(m)) >= 10 && w.person(m).station.empty()) kid = m;
            if (kid != kNone) {
                WorldTest::setStation(w, kid, "labourer", 0, "給人做雇工");
                WorldTest::person(w, kid).serviceEnd = -1;
                WorldTest::person(w, kid).stationUntil = -1;
                for (const auto& t : w.threads())
                    if (t.kind == "path" && t.text.find(w.person(kid).firstName) != std::string::npos)
                        CHECK(t.ends.find("期滿") == std::string::npos && t.text.find("期滿") == std::string::npos, "a road with no term promises one");
            }
        }
        // 12. An heir who remembers the deed already does not take it up a second time from the dead.
        {
            World w = base;
            Id head = w.playerHead();
            Id heir = WorldTest::heirOf(w, w.playerHousehold());
            Id d = w.recordDeed("public_insult", foes[0], head);
            if (heir != kNone && heir != head) {
                Memory m;
                for (const auto& x : w.memoriesOf(head))
                    if (x.deed == d) m = x;
                if (m.deed == d) {
                    auto count = [&] {
                        int n = 0;
                        for (const auto& x : w.memoriesOf(heir)) n += x.deed == d;
                        return n;
                    };
                    if (count() == 0) m.about = foes[0], WorldTest::memories(w, heir).push_back(m);  // (one of the family wronged: they have it already)
                    const int had = count();
                    WorldTest::kill(w, head, "熱病");
                    CHECK(count() == had, "the heir took up a second memory of a deed they remember themselves");
                }
            }
        }
        // Extra: a head hanged is not given a bedside scene.
        {
            World w = base;
            size_t before = w.chronicle().size();
            int asked = 0;
            w.setChooser([&](const World::ChoicePrompt& p) { asked += p.title == "床前"; return -1; });
            WorldTest::kill(w, w.playerHead(), "起義失敗後被絞死");
            w.setChooser(nullptr);
            CHECK(asked == 0 && !said(w, before, "病得起不了床") && !said(w, before, "〔床前〕"), "a hanged head got the sickbed scene");
        }
    }
    // One out in service who will have the house (seed 72, 1347: the words went to a neighbour while the heir was at a
    // master's): the servant is sent for before the last words, and those words and the house go to the same person.
    {
        bool tried = false;
        for (uint64_t seed : {72ull, 64ull, 7ull, 91ull, 58ull}) {
            Config c;
            c.seed = seed;
            World w(defs, c);
            w.runYears(4);
            Id hid = w.playerHousehold();
            Id head = w.playerHead();
            if (w.person(head).sex != Sex::Male) continue;
            Id son = kNone;
            for (Id k : w.person(head).children)
                if (w.person(k).alive() && w.person(k).household == hid && w.ageYears(w.person(k)) >= 12) son = k;
            Id master = kNone;
            for (const auto& oh : w.households())
                if (!oh.extinct && oh.id != hid && oh.head != kNone && oh.house != "manor") master = oh.id;
            if (son == kNone || master == kNone) continue;
            tried = true;
            // No child of his own takes it by blood: the son he named, now away at a master's, does.
            for (Id k : w.person(head).children) WorldTest::person(w, k).flags.insert("disowned");
            WorldTest::person(w, son).flags.insert("designated_heir");
            WorldTest::sendToService(w, son, master);
            std::string sonName = w.fullName(son);
            w.setChooser([](const World::ChoicePrompt& p) {
                if (p.title != "床前") return -1;
                for (size_t k = 0; k < p.options.size(); ++k)
                    if (p.options[k].find("叮囑") != std::string::npos) return static_cast<int>(k);
                return -1;
            });
            size_t before = w.chronicle().size();
            WorldTest::kill(w, head, "熱病");
            w.setChooser(nullptr);
            bool toSon = false, toOther = false;
            for (size_t i = before; i < w.chronicle().size(); ++i) {
                const std::string& t = w.chronicle()[i].text;
                if (t.find("〔床前〕") == std::string::npos) continue;
                (t.find("對 " + sonName) != std::string::npos ? toSon : toOther) = true;
            }
            const Household& h = w.household(hid);
            CHECK(h.head == son || h.ward == son, "the son the head named did not get the house");
            CHECK(toSon && !toOther, "the last words went to someone other than the one who got the house");
            checkInvariants(w, "deathbed servant heir");
            break;
        }
        CHECK(tried, "no seed had a son to send into service for the deathbed test");
    }
    // A boy at school whose land is held for him stays at his books when the one holding it dies; the term ends with a line.
    {
        bool tried = false;
        for (uint64_t seed : {64ull, 72ull, 7ull, 91ull, 58ull}) {
            Config c;
            c.seed = seed;
            World w(defs, c);
            w.runYears(3);
            Id hid = w.playerHousehold();
            Id head = w.playerHead();
            const Person& hp = w.person(head);
            Id heir = WorldTest::heirOf(w, hid);
            if (heir == kNone || hp.spouse == kNone || !w.person(hp.spouse).alive() || w.person(hp.spouse).household != hid ||
                w.person(heir).household != hid || heir == hp.spouse)
                continue;
            tried = true;
            WorldTest::person(w, heir).birthDay = w.today() - 12 * kDaysPerYear;
            WorldTest::setStation(w, heir, "scholar", 3, "進了文法學校");
            WorldTest::kill(w, head, "熱病");
            CHECK(w.person(heir).station == "scholar", "the heir at school was called home though his mother keeps the land for him");
            CHECK(w.household(hid).ward == heir, "the land is not held for the heir at school");
            size_t before = w.chronicle().size();
            for (int d = 0; d < 5 * kDaysPerYear && w.person(heir).station == "scholar"; ++d) w.stepDay();
            bool ended = false;
            for (const auto& e : w.threadEnds()) ended = ended || (e.kind == "path" && e.day >= w.today() - 5 * kDaysPerYear && e.text.rfind(w.person(heir).firstName, 0) == 0);
            CHECK(w.person(heir).station != "scholar" && ended, "the school term did not end with an outcome kept for the family");
            (void)before;
            checkInvariants(w, "heir at school");
            break;
        }
        CHECK(tried, "no seed had a head with a wife and an heir at home");
    }

    // Round 6 B (design/fun-r6.md B): no silent outcomes, one ledger, no contradictions.
    {
        // Every option of every card the player is asked has a line of its own (or one on each branch of its odds).
        std::function<bool(const Json&)> logged = [&](const Json& effects) {
            if (!effects.isArray()) return false;
            for (const auto& e : effects.asArray()) {
                if (!e.isObject()) continue;
                for (const auto& [k, v] : e.asObject()) {
                    if ((k == "log" || k == "tell") && v.isString() && !v.asString().empty()) return true;
                    if ((k == "random" || k == "odds") && v.isObject() && logged(v["effects"]) && logged(v["else"])) return true;
                }
            }
            return false;
        };
        int unsaid = 0;
        for (const auto& ev : defs.events) {
            if (ev.options.size() < 2 || !ev.ask || ev.scope == "interaction") continue;
            for (const auto& o : ev.options)
                if (o.log.empty() && o.logs.empty() && !logged(o.effects)) std::printf("silent option: %s「%s」\n", ev.id.c_str(), o.text.c_str()), ++unsaid;
        }
        CHECK(unsaid == 0, "a card the player is asked has an option that leaves no line");

        // Played, with every card answered: each answer leaves a line, every penny that moves the purse is on the
        // books, each season's books add up (start + items = end), and every season's summary lists the money.
        for (int seed : {72, 91}) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World w(defs, c);
            int k = 0, asked = 0, quiet = 0;
            size_t at = 0;
            std::string pending;
            auto check = [&]() {
                if (pending.empty()) return;
                bool said = false;
                for (size_t i = at; i < w.chronicle().size(); ++i) {
                    const std::string& t = w.chronicle()[i].text;
                    said = said || (w.chronicle()[i].importance == 1 && t.rfind("【消息】", 0) != 0 && t.rfind("【風聲】", 0) != 0);
                }
                if (!said) std::printf("silent answer (seed %d): %s\n", seed, pending.c_str()), ++quiet;
                pending.clear();
            };
            w.setChooser([&](const World::ChoicePrompt& p) {
                check();
                int pick = (k++ * 7 + seed) % static_cast<int>(p.options.size());
                pending = p.title + "「" + p.options[pick] + "」";
                at = w.chronicle().size();
                ++asked;
                return pick;
            });
            World::FamilySnapshot snap = w.snapshot();
            int seasons = 0, listed = 0;
            for (int d = 0; d < 5 * kDaysPerYear && !w.gameOver(); ++d) {
                w.stepDay();
                check();
                if (w.dayOfMonth() == 0 && w.month() % 3 == 0) {
                    ++seasons;
                    listed += w.seasonChanges(snap).find("存錢 ") != std::string::npos;
                    snap = w.snapshot();
                }
            }
            w.setChooser({});
            CHECK(asked >= 10 && quiet == 0, "an answered card left no line");
            CHECK(w.unbookedMoney() < 0.01, "money moved the purse without a book entry");
            CHECK(seasons > 0 && listed == seasons, "a season's summary left out the money");
            CHECK(w.seasonBooks().size() >= 12, "the seasons' books were not kept");
            for (const auto& b : w.seasonBooks()) {
                double sum = b.start;
                for (const auto& [why, d] : b.items) sum += d;
                CHECK(std::abs(sum - b.end) < 0.01, "a season's books do not add up to the purse");
            }
            std::printf("books (seed %d): %d cards answered, %s\n", seed, asked, w.seasonBooksText(w.seasonBooks().back()).c_str());
        }
        // Shown in whole pence, the items add up to the purse as the header shows it.
        CHECK(World::booksText(10.0, 2.6, {{"甲", -2.4}, {"乙", -2.3}, {"丙", -2.7}}) == "存錢 10 → 2 便士（出：丙 −3、甲 −3、乙 −2）。",
              "the books do not add up in whole pence");
        CHECK(World::booksText(40.2, 40.9, {}) == "存錢 40 便士，沒有進出。", "a season with no money moving is not said so");

        Config c;
        c.seed = 58;  // a house with land, money and children: the cases below are set up on it by hand
        c.prerun = 25;
        World base(defs, c);
        const Id hh = base.playerHousehold();
        const Id head = base.playerHead();
        auto saidSince = [](const World& w, size_t from, const std::string& a, const std::string& b = "") {
            for (size_t i = from; i < w.chronicle().size(); ++i) {
                const auto& e = w.chronicle()[i];
                if (e.importance == 1 && e.text.find(a) != std::string::npos && e.text.find(b) != std::string::npos) return true;
            }
            return false;
        };

        // Births and deaths in the house (not only of the blood) and of the head's near kin elsewhere are told,
        // a death with its cause; a grown life is summed up.
        {
            World w = base;
            int deaths = 0, births = 0, untold = 0;
            for (int d = 0; d < 8 * kDaysPerYear && !w.gameOver(); ++d) {
                std::map<Id, std::string> watched;
                for (const auto& p : w.people())
                    if (p.alive() && w.familyTold(p.id)) watched[p.id] = w.fullName(p.id);
                size_t from = w.chronicle().size();
                Id first = static_cast<Id>(w.people().size());
                w.stepDay();
                for (const auto& [id, name] : watched)
                    if (!w.person(id).alive()) {
                        ++deaths;
                        if (!saidSince(w, from, name)) std::printf("untold death: %s\n", name.c_str()), ++untold;
                    }
                for (Id id = first; id < static_cast<Id>(w.people().size()); ++id)
                    if (w.person(id).alive() && w.familyTold(id)) {
                        ++births;
                        if (!saidSince(w, from, w.fullName(id))) std::printf("untold birth: %s\n", w.fullName(id).c_str()), ++untold;
                    }
            }
            std::printf("family news: %d deaths, %d births told\n", deaths, births);
            CHECK(untold == 0, "a birth or death in the family went untold");
        }
        // (in the first of a few villages with someone married in and near kin in another house)
        {
            int tested = 0;
            for (int seed : {58, 3, 5, 7, 72, 91, 64, 9, 14, 16, 18, 20}) {
                Config vc;
                vc.seed = seed;
                vc.prerun = 25;
                World w(defs, vc);
                const Id vh = w.playerHousehold(), vhead = w.playerHead();
                Id stranger = kNone, kinAway = kNone;
                for (Id m : w.household(vh).members)
                    if (m != vhead && w.person(m).dynasty != w.playerDynasty() && w.ageYears(w.person(m)) >= 14) stranger = m;
                for (const auto& p : w.people())
                    if (p.alive() && p.household != kNone && p.household != vh && w.headsNearKin(p.id) && w.ageYears(p) >= 14) kinAway = p.id;
                if (stranger == kNone || kinAway == kNone) continue;
                for (Id who : {stranger, kinAway}) {
                    std::string name = w.fullName(who), first = w.person(who).firstName;
                    std::string kin = who == kinAway ? w.kinWord(vhead, who) + " " : "";
                    size_t from = w.chronicle().size();
                    WorldTest::kill(w, who, "熱病");
                    CHECK(saidSince(w, from, kin + name, "因熱病去世"), "a death in the family has no line with its cause");
                    CHECK(saidSince(w, from, first + "這一生："), "a grown life in the family is not summed up");
                }
                ++tested;
                break;
            }
            CHECK(tested == 1, "no village with someone married in and near kin elsewhere to test with");
        }

        // 後繼無人 is not said while a son of the blood lives, at home or in a house of his own.
        {
            World w = base;
            EventRunner ev(w);
            const Json heir = Json::parse("{\"heir_at_home\": true}");
            Id son = kNone, other = kNone;
            for (const auto& p : w.people())
                if (p.alive() && p.father == head && p.household == hh) son = p.id;
            for (const auto& h : w.households())
                if (!h.extinct && h.id != hh && h.head != kNone && h.house != "manor") other = h.id;
            if (son != kNone && other != kNone) {
                WorldTest::moveTo(w, son, other);
                CHECK(ev.eval(heir, w.person(head), kNone), "a son with a house of his own is not counted an heir");
            }
        }

        // One number for what the house owes: the reeve's gate counts every debt, in the header's whole pence.
        {
            World w = base;
            WorldTest::house(w, hh).debt = 10;
            WorldTest::house(w, hh).heldDebt = 12;
            for (const auto& g : w.officeGates(head, "reeve"))
                if (g.label.find("欠債") != std::string::npos)
                    CHECK(!g.ok && g.detail.find("欠債 22") != std::string::npos, "the reeve's debt gate does not count all the house owes");
            WorldTest::house(w, hh).debt = 19.7;
            WorldTest::house(w, hh).heldDebt = 0;
            for (const auto& g : w.officeGates(head, "reeve"))
                if (g.label.find("欠債") != std::string::npos) CHECK(g.ok && g.detail.find("欠債 19") != std::string::npos, "19.7 owed is not 19 as shown");
        }

        // 缺糧 is today's barn against the days to the harvest, not a flag from the last harvest.
        {
            World w = base;
            w.setFoodForTest(hh, WorldTest::toHarvest(w) + 200);
            WorldTest::house(w, hh).lean = true;
            CHECK(w.breadShort(w.household(hh)) == 0, "a full barn is called short");
            w.setFoodForTest(hh, WorldTest::toHarvest(w) - 30);
            int s = w.breadShort(w.household(hh));
            CHECK(s >= 29 && s <= 31, "the days short of the harvest are wrong");
        }

        // 家用排場 is asked, and the answer says what it costs and whether it can be changed.
        {
            World w = base;
            std::string asked;
            w.setChooser([&](const World::ChoicePrompt& p) { asked = p.title; return p.title == "家用排場" ? 1 : 0; });
            size_t from = w.chronicle().size();
            WorldTest::houseStyle(w, hh);
            w.setChooser({});
            CHECK(asked == "家用排場" && w.household(hh).frugal, "the house was not asked how it lives");
            CHECK(saidSince(w, from, "〔家用排場〕", "五年後"), "the house's way of living has no line saying when it is decided again");
        }

        // A stepfather keeping the house wins its name for the house's line, not his own.
        {
            World w = base;
            Id own = w.person(head).dynasty;
            for (const auto& p : w.people())
                if (p.dynasty != kNone && p.dynasty != own) {
                    WorldTest::person(w, head).dynasty = p.dynasty;
                    break;
                }
            CHECK(WorldTest::houseLine(w, head) == w.playerDynasty(), "the house's fame goes to the keeper's own surname");
        }

        // Paying a debt in full on the spot is not 「不得不忍讓的窩囊」, however cruel the head.
        {
            World w = base;
            Person& hp = WorldTest::person(w, head);
            hp.traits = {"cruel"};
            hp.stressCause.clear();
            Id lender = kNone;
            for (const auto& h : w.households())
                if (!h.extinct && h.id != hh && h.head != kNone && h.house != "manor") lender = h.head;
            WorldTest::house(w, hh).money = 500;
            WorldTest::addDebt(w, hh, 27, lender);
            w.setChooser([&](const World::ChoicePrompt& p) {
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].find("一次還清") != std::string::npos) return static_cast<int>(i);
                return 0;
            });
            WorldTest::runEvent(w, "debt_neighbour", head, lender);
            w.setChooser({});
            CHECK(w.owedAll(hh) < 1, "the debt was not paid on the spot");
            CHECK(w.person(head).stressCause.find("窩囊") == std::string::npos, "paying a debt in full weighed on the head as yielding");
        }

        // A confronted affair: the one wronged remembers a betrayal, the one found out remembers being found out.
        {
            World w = base;
            Id wife = w.person(head).spouse;
            if (wife != kNone) {
                EventRunner ev(w);
                ev.apply(Json::parse("[{\"mutual_opinion\": {\"value\": -20, \"mod\": \"betrayed\", \"back_mod\": \"found_out\", \"years\": 12}}]"),
                         WorldTest::person(w, head), wife);
                auto has = [&](Id from, Id to, const std::string& mod) {
                    for (const auto& [label, v] : w.opinionBreakdown(from, to))
                        if (label == mod) return true;
                    return false;
                };
                CHECK(has(head, wife, "betrayed") && has(wife, head, "found_out") && !has(wife, head, "betrayed"),
                      "the betrayal is remembered on the wrong side");
            }
        }

        // A 結交 that comes to nothing says so.
        {
            World w = base;
            bool started = false;
            // (the head of seed 58 has his two sworn friends already: a third is refused, with that reason)
            auto& mine = WorldTest::memories(w, head);
            mine.erase(std::remove_if(mine.begin(), mine.end(), [](const Memory& m) { return m.label == "sworn_friend"; }), mine.end());
            for (const auto& p : w.people()) {
                if (!p.alive() || p.household == kNone || p.household == hh || w.ageYears(p) < 18) continue;
                if (WorldTest::startPlan(w, head, "befriend", p.id).empty()) {
                    size_t from = w.chronicle().size();
                    CHECK(WorldTest::failPlan(w, head, "befriend"), "the befriend plan did not start");
                    CHECK(saidSince(w, from, p.firstName, "沒交上心"), "a befriend plan that failed left no line");
                    started = true;
                    break;
                }
            }
            CHECK(started, "no one to befriend");
        }

        // Michaelmas: a rent met in full is said; on the card, 租 − 手上 = 還差, and a few pence short can still be
        // met with a little grain.
        {
            auto toRentDay = [&](World& w) {
                while (!w.gameOver() && !(w.month() == 8 && w.dayOfMonth() == 29)) w.stepDay();
            };
            World w = base;
            toRentDay(w);
            w.setMoneyForTest(hh, 1000);
            size_t from = w.chronicle().size();
            w.stepDay();
            CHECK(saidSince(w, from, "〔米迦勒節〕今年的地租", "照數交清"), "a rent paid in full is not said");

            World s = base;
            toRentDay(s);
            double rent = WorldTest::rent(s, hh);
            s.setMoneyForTest(hh, std::max(0.0, std::floor(rent) - 4.4));
            s.setFoodForTest(hh, 600);
            WorldTest::house(s, hh).debt = WorldTest::house(s, hh).heldDebt = 0;
            bool smallSale = false, adds = false;
            s.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "米迦勒節算帳") return -1;
                auto after = [&](const std::string& key) {
                    size_t at = p.situation.find(key);
                    return at == std::string::npos ? -1L : std::atol(p.situation.c_str() + at + key.size());
                };
                adds = after("今年的租 ") - after("手上 ") == after("還差 ");
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].rfind("賣 ", 0) == 0) return smallSale = true, static_cast<int>(i);
                return static_cast<int>(p.options.size()) - 1;
            });
            s.stepDay();
            s.setChooser({});
            CHECK(adds, "the reckoning card's numbers do not add up");
            CHECK(smallSale && s.owedAll(hh) < 1, "a few pence short, the card offered no grain to sell");
            // Let go on the books: the steward takes whole pennies, so paid + owed = the rent, in whole numbers.
            World b = base;
            toRentDay(b);
            double due = std::round(WorldTest::rent(b, hh));
            b.setMoneyForTest(hh, 2.9);
            b.setFoodForTest(hh, 600);
            WorldTest::house(b, hh).debt = WorldTest::house(b, hh).heldDebt = 0;
            b.setChooser([&](const World::ChoicePrompt& p) { return p.title == "米迦勒節算帳" ? static_cast<int>(p.options.size()) - 1 : -1; });
            b.stepDay();
            b.setChooser({});
            CHECK(std::abs(b.owedAll(hh) - (due - 2)) < 0.01 && std::abs(b.household(hh).money - 0.9) < 0.01,
                  "a rent short is not paid and owed in whole pence");
        }

        // The hand on the season's list is the hand hired; taking someone in says what it does to the barn.
        {
            int hires = 0, takes = 0;
            for (int seed : {58, 72, 3, 5}) {
                if (hires > 0 && takes > 0) break;
                Config vc;
                vc.seed = seed;
                vc.prerun = 25;
                World w(defs, vc);
                for (int d = 0; d < 3 * kDaysPerYear && !w.gameOver(); ++d) {
                    if (w.dayOfMonth() == 0)
                        for (const auto& a : w.seasonAgenda())
                            if (a.id.rfind("hire_hand:", 0) == 0) {
                                ++hires;
                                CHECK(a.id == "hire_hand:" + std::to_string(a.target) && a.title.find(w.fullName(a.target)) != std::string::npos,
                                      "the hand on the list is not the hand hired");
                            } else if (a.id.rfind("help_take:", 0) == 0) {
                                ++takes;
                                CHECK(a.desc.find("存糧從夠吃") != std::string::npos, "taking someone in does not say what it does to the stores");
                            }
                    w.stepDay();
                }
            }
            std::printf("agenda: %d hire and %d take-in items checked\n", hires, takes);
            CHECK(hires > 0 && takes > 0, "no hand to hire and no one to take in on the season's list");
        }
    }
    // What blocks a plan is shown, condition by condition (design/fun-r6.md A): every plan's ✓/✗ list agrees with
    // what availablePlans offers, for every grown villager; and a 19-year-old free head with the money and the land is
    // told it is his age that keeps him from buying the freehold (seed 91, 1361: 「條件全達成卻開不了」).
    {
        Config c;
        c.seed = 91;
        World w(defs, c);
        w.runYears(6);
        int checked = 0, disagree = 0, silent = 0;
        for (const auto& q : w.people()) {
            if (!q.alive() || q.household == kNone || w.ageYears(q) < 14 || q.id % 3) continue;
            std::set<std::string> offered;
            for (const auto& op : w.availablePlans(q.id)) offered.insert(op.plan);
            for (const auto& pd : w.defs().plans) {
                bool ok = true;
                for (const auto& g : w.planGates(q.id, pd, false)) {
                    ok = ok && g.ok;
                    if (!g.ok && g.detail.empty()) ++silent;
                }
                ++checked;
                if (ok != (offered.count(pd.id) > 0) || ok != w.planBlock(q.id, pd).empty()) {
                    if (disagree < 3) std::printf("  plan gates disagree: %s %s (%s)\n", w.fullName(q.id).c_str(), pd.id.c_str(), w.planBlock(q.id, pd).c_str());
                    ++disagree;
                }
            }
        }
        CHECK(checked > 200, "too few plans checked");
        CHECK(disagree == 0, "a plan's ✓/✗ list disagrees with whether it can start");
        CHECK(silent == 0, "an unmet plan condition has no words");
        Id head = w.playerHead();
        Person& hp = WorldTest::person(w, head);
        hp.birthDay = w.today() - 19 * kDaysPerYear - 60;
        hp.free = true;
        Household& h = WorldTest::house(w, w.playerHousehold());
        h.money = 202;
        if (h.acres - h.freehold >= 6 && w.actingHead() == head) {
            std::string why = w.planBlock(head, *w.defs().plan("buy_freehold"));
            CHECK(why.find("19 歲") != std::string::npos && why.find("20") != std::string::npos, "the freehold's age bar was not told");
            CHECK(w.planBlock(head, *w.defs().plan("seek_patron")).find("19 歲") != std::string::npos, "seeking the lord's favour: the age bar was not told");
        }
        std::printf("plan gates: %d checked\n", checked);
        // An ask that is no longer there says why (「這件事現在跟華特談不了了」 with no reason, 1352 and 1359).
        for (const auto& q : w.people())
            if (q.alive() && q.household != kNone && !w.inPlayerHouse(q.household) && w.household(q.household).head != q.id && w.ageYears(q) >= 21) {
                std::string why = w.askGoneWhy(q.id, "vote", kNone, kNone, "hayward");
                CHECK(why.find("不是一家之主") != std::string::npos, "a vote asked of one who does not head a house: no reason why not");
                break;
            }
    }

    // Names (design/fun-r6.md A): no two living villagers go by the same name at the same time, and a mark given at
    // home is not swapped for another (小尼可拉斯 does not become 二尼可拉斯 when a baby Nicholas comes).
    {
        int dup = 0, swapped = 0, sampled = 0;
        for (uint64_t seed : {91ull, 58ull}) {
            Config c;
            c.seed = seed;
            World w(defs, c);
            std::map<Id, std::pair<Id, std::string>> last;  // person -> (household, mark)
            for (int y = 0; y < 30 && !w.gameOver(); ++y) {
                for (int q = 0; q < 4; ++q) {
                    for (int d = 0; d < kDaysPerYear / 4 && !w.gameOver(); ++d) w.stepDay();
                    for (const auto& p : w.people()) {
                        if (!p.alive() || p.household == kNone) continue;
                        std::string m = w.houseMark(p.id);
                        auto it = last.find(p.id);
                        if (it != last.end() && it->second.first == p.household && !it->second.second.empty() && !m.empty() && it->second.second != m) {
                            if (swapped < 3) std::printf("  name mark changed: %s %s -> %s\n", w.fullName(p.id).c_str(), it->second.second.c_str(), m.c_str());
                            ++swapped;
                        }
                        last[p.id] = {p.household, m};
                    }
                }
                std::map<std::string, Id> seen;
                for (const auto& p : w.people()) {
                    if (!p.alive() || p.household == kNone) continue;
                    ++sampled;
                    auto [it, fresh] = seen.insert({w.fullName(p.id), p.id});
                    if (!fresh) {
                        if (dup < 3) std::printf("  two villagers called %s (%d, %d)\n", it->first.c_str(), it->second, p.id);
                        ++dup;
                    }
                }
            }
        }
        std::printf("names: %d sampled, %d shared, %d marks swapped\n", sampled, dup, swapped);
        CHECK(dup == 0, "two living villagers go by the same name");
        CHECK(swapped == 0, "a namesake's mark at home was swapped for another");
    }
    // The family's name on the main screen (design/fun-r6.md A): a number on a stated scale, a word that changes where
    // asking starts to count it, the last three things that moved it with their years, and how to mend it.
    {
        Config c;
        c.seed = 72;
        World w(defs, c);
        Id h = w.playerHead();
        CHECK(h != kNone, "reputation: no head");
        double start = w.person(h).reputation;
        w.addReputation(h, -start, "歸零");
        for (int i = 0; i < 8; ++i) w.addReputation(h, 0.4, "小事");  // drift under a point is told once it makes one
        w.addReputation(h, -12, "地租沒交齊");
        for (int i = 0; i < 3; ++i) w.addReputation(h, 1, "〔這一年的買賣〕");
        auto v = w.reputationView(h);
        CHECK(v.value == -6 && v.word == "平常", "reputation: value or word wrong at -6");
        CHECK(v.recent.size() == 3 && v.recent[0].find("3 回") != std::string::npos && v.recent[1].find("地租沒交齊") != std::string::npos &&
                  v.recent[2].find("小事") != std::string::npos,
              "reputation: the same thing year after year pushed out the bad one");
        w.addReputation(h, -5, "又欠租");
        v = w.reputationView(h);
        CHECK(v.word == "不好" && v.effect.find("名聲不好") != std::string::npos && !v.mend.empty(), "reputation: at -10 asking counts it, and the screen must say so");
        w.addReputation(h, 200, "大功");
        CHECK(w.reputationView(h).value == 50, "reputation: not held to the stated scale");
    }
    // Round 7 (design/ui-text.md, part 1): at most two cards before the first season page; the rest wait. A card sent
    // by a signal (a death, a deed found out) is about its moment and does not count.
    {
        Config c;
        c.seed = 64;
        c.prerun = 25;  // as the game starts (tools/webplay.cpp)
        World w(defs, c);
        int firstSeason = (w.handedOver() / 90 + 1) * 90, early = 0, signals = 0;
        w.setChooser([&](const World::ChoicePrompt& p) {
            if (w.today() >= firstSeason) return -1;
            bool signal = false;
            for (const auto& ev : defs.events)
                if (ev.title == p.title && ev.pulse == "signal") signal = true;
            ++(signal ? signals : early);
            return -1;
        });
        while (w.today() < firstSeason) w.stepDay();
        std::printf("opening: %d cards before the first season (and %d sent by a signal)\n", early, signals);
        CHECK(early >= 1 && early <= 2, "opening: more than two cards before the first season (or none)");
    }
    // 〔親友挨餓〕: at most two a harvest year for the whole family, however many kin houses run short (the family kept
    // rich so it is always worth asking), and every gift said in days of bread.
    {
        int total = 0, most = 0;
        bool sane = true;
        for (int seed : {64, 58, 61}) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World w(defs, c);
            const Id hh = w.playerHousehold();
            std::map<int, int> perYear;
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "親友挨餓") return -1;
                ++perYear[(w.today() + kDaysPerYear - 8 * kDaysPerMonth) / kDaysPerYear];
                for (const auto& o : p.options)
                    if ((o.rfind("從自家糧倉送去", 0) == 0 || o.rfind("送 ", 0) == 0) && o.find("天") == std::string::npos) sane = false;
                return 0;
            });
            for (int d = 0; d < 4 * kDaysPerYear; ++d) {
                if (w.dayOfMonth() == 0) w.setMoneyForTest(hh, 2000), w.setFoodForTest(hh, 400);
                w.stepDay();
            }
            for (auto [y, n] : perYear) total += n, most = std::max(most, n);
        }
        std::printf("feed cards: %d in 12 years of play, at most %d in a harvest year\n", total, most);
        CHECK(total >= 1 && most <= 2, "feed: more than two 〔親友挨餓〕 cards in a harvest year (or none at all)");
        CHECK(sane, "feed: a gift option without the days it feeds");
    }
    // Kin labels: a married daughter in her husband's house is 「女兒（嫁到X家）」; congenital fecundity waits for 16.
    {
        int married = 0, labelled = 0, early = 0;
        for (int seed = 40; seed < 80; ++seed) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World w(defs, c);
            Id head = w.playerHead();
            for (const auto& p : w.people()) {
                if (!p.alive()) continue;
                if (w.hasTrait(p, "fecund") && w.ageYears(p) < 16) ++early;
                std::string k = p.sex == Sex::Female ? w.kinWord(head, p.id) : "";
                bool blood = !k.empty() && k.find("妻") == std::string::npos && k.find("媳") == std::string::npos && k.find("繼") == std::string::npos;
                if (blood && p.spouse != kNone && p.household != kNone && p.household != w.playerHousehold() &&
                    w.person(p.spouse).household == p.household && w.household(p.household).head != p.id &&
                    w.household(p.household).dynasty == w.person(p.spouse).dynasty &&
                    w.household(p.household).dynasty != w.household(w.playerHousehold()).dynasty) {
                    ++married;
                    if (w.kinLabel(head, p.id).find("嫁到") != std::string::npos) ++labelled;
                }
            }
        }
        std::printf("kin labels: %d of %d married-out kinswomen say 嫁到; %d under-16s fecund\n", labelled, married, early);
        CHECK(labelled == married, "kin label: a kinswoman married into another house not shown as 嫁到X家");
        CHECK(early == 0, "a child already shows 多子 before 16");
    }
    // The priest: with none in the village no card offers him; the bishop sends another within half a year.
    {
        Config c;
        c.seed = 64;
        c.prerun = 25;  // as the game starts (tools/webplay.cpp)
        World w(defs, c);
        auto priest = [&]() {
            for (const auto& p : w.people())
                if (p.alive() && p.occupation == "priest" && p.household != kNone) return p.id;
            return kNone;
        };
        Id old = priest();
        CHECK(old != kNone, "priest: the village starts without one");
        w.killForTest(old);
        bool offered = false, back = false;
        w.setChooser([&](const World::ChoicePrompt& p) {
            if (priest() == kNone)
                for (const auto& o : p.options)
                    if (o.find("神父") != std::string::npos) offered = true;
            return -1;
        });
        for (int d = 0; d < 180 && !back; ++d) {
            w.stepDay();
            back = priest() != kNone && priest() != old;
        }
        CHECK(!offered, "priest: a card offered the priest while there was none");
        CHECK(back, "priest: no new priest within half a year");
    }
    // Waiting cards (review r7 1–3): only pulse cards wait (a death, a deed or a fine is asked at its moment), never
    // twice in the queue, and the first year asks no more than a few cards a month.
    {
        Config c;
        c.seed = 64;
        c.prerun = 25;
        World w(defs, c);
        std::map<int, int> perMonth;
        w.setChooser([&](const World::ChoicePrompt&) {
            if (w.today() < w.handedOver() + kDaysPerYear) ++perMonth[w.today() / kDaysPerMonth];
            return -1;
        });
        bool signalWaits = false, twice = false;
        for (int d = 0; d < 2 * kDaysPerYear; ++d) {
            w.stepDay();
            std::set<std::pair<std::string, Id>> seen;
            for (const auto& q : w.deferredForTest()) {
                twice = twice || !seen.insert(q).second;
                for (const auto& ev : defs.events)
                    if (ev.id == q.first && ev.pulse == "signal") signalWaits = true;
            }
        }
        int most = 0;
        for (auto [m, n] : perMonth) most = std::max(most, n);
        std::printf("first year: at most %d cards a month\n", most);
        CHECK(!signalWaits, "a signal card (death, deed, fine) was held back past its moment");
        CHECK(!twice, "the same card waiting twice");
        CHECK(most <= 4, "the first year piles up more than four cards in a month");
    }
    // The season's list never shows the same item twice.
    {
        Config c;
        c.seed = 64;
        c.prerun = 25;  // as the game starts (tools/webplay.cpp)
        World w(defs, c);
        w.setChooser([&](const World::ChoicePrompt&) { return -1; });
        bool dup = false;
        for (int s = 0; s < 8; ++s) {
            for (int d = 0; d < 90; ++d) w.stepDay();
            std::set<std::string> ids;
            for (const auto& a : w.seasonAgenda(true)) dup = dup || !ids.insert(a.id).second;
        }
        CHECK(!dup, "agenda: the same item twice in a season's list");
    }

    if (failures == 0) std::printf("all tests passed\n");
    return failures == 0 ? 0 : 1;
}
