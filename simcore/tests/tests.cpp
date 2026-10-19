// Headless tests for SimCore: determinism and state invariants over long runs.
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
            double per = 0;
            for (Id m : w.household(hh).members) per += w.ageYears(w.person(m)) < 10 ? 0.6 : 1.0;
            return w.household(hh).food / std::max(0.5, per);
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
            // Once a harvest year: short again a month later, the house keeps to what it said.
            asked.clear();
            w.setFoodForTest(hh, 10);
            runTo(w, 7, 1);
            bool again = false;
            for (const auto& p : asked) again = again || p.title == "買糧";
            CHECK(!again, "the grain card was asked twice before one harvest");
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
            w.setFoodForTest(hh, 600);
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
        CHECK(foodDays(sold) < 600 - 5, "the grain sold on the reckoning card is still in the barn");
        // A season's money, item by item: what came in apart from what went out.
        World w = base;
        World::FamilySnapshot before = w.snapshot();
        w.runYears(1);
        std::string money = w.moneySince(before);
        CHECK(money.find("進：") != std::string::npos && money.find("出：") != std::string::npos && money.find("其他") == std::string::npos,
              "a year's money is not listed as incomings and outgoings");
        std::printf("money: %s\n", money.c_str());
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
        CHECK(World::booksText(10.0, 2.6, {{"甲", -2.4}, {"乙", -2.3}, {"丙", -2.7}}) == "存錢 10 → 2 便士（出：丙 -3、甲 -3、乙 -2）。",
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
            for (int seed : {58, 3, 5, 7, 72, 91, 64}) {
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

    if (failures == 0) std::printf("all tests passed\n");
    return failures == 0 ? 0 : 1;
}
