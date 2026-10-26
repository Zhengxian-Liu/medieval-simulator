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
    static void giveLordsLand(World& w, Id hid, double acres) { w.moveLand(w.people_[w.lord_].household, hid, acres, LandPick::CustomaryFirst, "test"); }
    static void makeFranklin(World& w, Id hid) {
        Household& h = w.households_[hid];
        for (Id m : h.members) w.people_[m].free = true;
        Id lordHouse = w.people_[w.lord_].household;
        if (h.acres < 45) w.moveLand(lordHouse, hid, 45 - h.acres, LandPick::CustomaryFirst, "test");
        if (h.freehold < 22) w.makeFree(h, 22 - h.freehold);
        w.addOpinion(w.lord_, h.head, "test", 60, 30);
    }
    static void rivalForLand(World& w) { w.rivalForLand(w.people_[w.playerHead()]); }
    // Round 10, the drive inside the family.
    static void killing(World& w, Id felon, Id victim, bool poison) {
        Id d = w.recordDeed(poison ? "poisoning" : "murder", felon, victim);
        w.killing(felon, victim, d, poison, false);
    }
    static void addUpkeep(World& w, Id parent, Id heir, int state, int breaches) {
        w.upkeeps_.push_back({parent, heir, w.people_[heir].household, w.day_, state, breaches, -1, true});
    }
    static bool kinCourt(World& w, Id plaintiff, Id defendant, const std::string& claim, double merit) {
        return w.kinCourt(plaintiff, defendant, claim, merit, "test");
    }
    static void kinMotives(World& w) { w.kinMotives(); }
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
    static void setAcres(World& w, Id hid, double acres) {  // to or from the lord's own
        const double now = w.households_[hid].acres;
        if (now < acres) giveLand(w, hid, acres - now);
        if (now > acres) w.moveLand(hid, w.people_[w.lord_].household, now - acres, LandPick::CustomaryFirst, "test");
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
    static void yearlyDebts(World& w) { w.yearlyDebts(); }
    static void like(World& w, Id a, Id b, double v) { w.addOpinion(a, b, "test", v, 30); }
    static void yearlyFeuds(World& w) { w.yearlyFeuds(); }
    static void yearlyFeudTurns(World& w) { w.yearlyFeudTurns(); }
    static void takeOverScene(World& w, Id newHead, Id from) { w.takeOverScene(w.households_[w.playerHousehold_], newHead, from, false); }
    static std::vector<ThreadEnd>& threadEnds(World& w) { return w.threadEnds_; }
    static std::vector<Memory>& memories(World& w, Id id) { return w.memories_[id]; }
    // fun-r9 (src/exchange.cpp)
    static void setWeather(World& w, double v) { w.lastWeather_ = v; }
    static void famineAsk(World& w) { w.famineAsk(); }
    static int famineAsks(const World& w) { return w.famineAsks_; }
    static void clearFamineAsks(World& w) { w.famineAsks_ = 0, w.famineAsked_ = -1; }  // a fresh famine season
    static int grainExchange(World& w) { return w.grainExchange(w.households_[w.playerHousehold_]); }
    static void michaelmas(World& w) { w.michaelmasScreen(w.households_[w.playerHousehold_]); }
    static void setRival(World& w, Id house) {
        w.rival_ = World::Rival{};
        w.rival_.dynasty = w.households_[house].dynasty;
        w.rival_.house = house;
        w.rival_.forHead = w.dynasties_[w.playerDynasty_].heads.back();
    }
    static void rivalStrike(World& w) { w.rivalStrike(); }
    static void addFine(World& w, Id house, double amount) { w.fines_.push_back({house, w.households_[house].head, amount, w.day_ + 150, false}); }
    static double grainDaysBought(const World& w) { return w.grainDays_; }  // days of bread the family has bought (bookFood)
    static void clearFines(World& w) { w.fines_.clear(); }
    static std::vector<World::Pledge>& pledges(World& w) { return w.pledges_; }
    static void settlePledges(World& w) { w.settlePledges(); }
    static void setLastDeath(World& w, int day) { w.lastDeathDay_ = day; }
    static void flag(World& w, Id id, const std::string& f) { w.people_[id].flags.insert(f); }
    static bool kinHouse(const World& w, Id house) { return w.kinHouse(house); }
    static void holdEvent(World& w, Id id, const std::string& ev, int days) { w.cooldowns_[{id, ev}] = w.day_ + days; }
    static void addGrief(World& w, Id who, Id against, const std::string& why) {
        World::Grievance g;
        g.who = who, g.against = against, g.motive = 40, g.why = why, g.signs = 1, g.lastSign = w.day_;
        w.griefs_.push_back(g);
    }
    static void kinRespond(World& w, size_t i) { w.kinRespond(w.griefs_[i]); }
    static void oweFavour(World& w, Id debtor, Id creditor, double weight, const std::string& why) { w.oweFavour(debtor, creditor, weight, why); }
    static void spendFavour(World& w, Id debtor, Id creditor) { w.spendFavour(debtor, creditor); }
    static std::vector<Id> lineage(const World& w) { return w.lineage(); }
    static std::string kinTie(const World& w, Id a, Id b) { return w.kinTie(a, b); }
    static void setQuarrelRule(World& w, const std::string& r) { w.setQuarrelRule(r); }
    static void noMillFarmer(World& w) {  // nobody farms the mill (its toll comes out of every purse at Michaelmas)
        for (auto& h : w.households_) h.millUntil = -1;
    }
    // 地方 (src/district.cpp, design/district.md)
    static bool districtBuilt(const World& w) { return w.dist_.built; }
    static World::Stake* stake(World& w, const std::string& key) { return w.stakeByKey(key); }
    static std::vector<World::DLoan>& dloans(World& w) { return w.dloans_; }
    static std::vector<World::DSuit>& dsuits(World& w) { return w.dsuits_; }
    static Id dshell(World& w, const std::string& key) {
        const auto* d = w.dhouseByKey(key);
        return d ? d->shell : kNone;
    }
    static double& dacres(World& w, const std::string& key) { return w.dhouseByKey(key)->acres; }
    static void give(World& w, Id hid, double amount) {  // booked, as any purse change must be
        w.households_[hid].money += amount;
        w.book(w.households_[hid], "test", amount);
    }
    static int addAsk(World& w, const std::string& house, double amount, double pledge) {
        World::DAsk a;
        a.id = w.dist_.nextAsk++;
        a.kind = "loan";
        a.house = dshell(w, house);
        a.amount = amount, a.rate = 0.15, a.years = 1, a.pledge = pledge, a.risk = 0.2;
        a.until = w.day_ + 90;
        a.why = "test";
        w.dasks_.push_back(a);
        return a.id;
    }
    static void addLoan(World& w, Id lender, Id borrower, double lent, double owed) {  // due this Michaelmas
        World::DLoan l;
        l.lender = lender, l.borrower = borrower, l.lent = lent, l.owed = owed, l.due = w.year(), l.since = w.day_;
        w.dloans_.push_back(l);
    }
    static void lend(World& w, int id, int level) { w.lendTo(id, level); }
    static void loansDue(World& w) { w.loansDue(); }
    static void court(World& w) {
        for (auto& s : w.dsuits_)
            if (!s.done) s.filed = std::min(s.filed, w.day_ - 1);
        w.hundredCourt();
    }
    static void districtMichaelmas(World& w) { w.districtMichaelmas(); }
    static void bidAlone(World& w, const std::string& key, int level) {  // the family the only bidder, and the round closed
        World::Stake* s = w.stakeByKey(key);
        s->bids.clear();
        std::vector<Id> men = w.stakeMen(*s, w.playerHousehold_);
        w.placeBid(*s, w.playerHousehold_, level, men.empty() ? kNone : men.front());
        w.closeRound(*s);
    }
    static void enterTier(World& w, const std::string& why) { w.enterTier(w.playerHousehold_, why); }
    static void blow(World& w, const std::string& why) { w.districtBlow(why); }
    static double landOut(const World& w) {  // every acre out there: the houses' own and the village's 外村地
        double a = 0;
        for (const auto& d : w.dhouses_) a += d.acres;
        for (const auto& h : w.households_) a += h.outAcres;
        return a;
    }
    static double allMoney(const World& w) {
        double m = 0;
        for (const auto& h : w.households_) m += h.money;
        return m;
    }
};
}  // namespace sim

static bool w_ok(const World& w, Id house) {  // a house of the village that can be someone's counterparty
    const auto& h = w.household(house);
    return !h.members.empty() && w.person(h.head).alive() && w.person(h.head).occupation != "priest" && w.ageYears(w.person(h.head)) >= 18 &&
           h.dynasty != w.playerDynasty() && w.person(h.head).occupation != "lord";
}

// The first seed from `from` on whose family is handed over to a grown head with living children: the house some
// cases are set up on, whatever the 25 years before the hand-over did (a change in the data can leave a child heir).
static Config grownHouse(const Defs& defs, int from) {
    for (int s = from; s < from + 40; ++s) {
        Config c;
        c.seed = s;
        c.prerun = 25;
        World w(defs, c);
        const Id head = w.playerHead();
        if (head == kNone || w.gameOver() || w.ageYears(w.person(head)) < 21) continue;
        for (Id k : w.person(head).children)
            if (w.person(k).alive()) return c;
    }
    Config c;
    c.seed = from;
    c.prerun = 25;
    return c;
}

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
            const double bought = WorldTest::grainDaysBought(w);
            w.stepDay();  // month 7, day 0: thirty days to the harvest
            // What was bought (an event on the same day may also take grain from the barn).
            const double to = 10 + WorldTest::grainDaysBought(w) - bought;
            CHECK(to > 31 && to < 48, "grain was bought past the harvest");
        }
        // fun-r9 A: asked, the card is whom to ask (缺糧：跟誰開口): how far the barn is from the harvest and why it is
        // short; at most four lines, each 「對方｜他要什麼｜也牽動」 with at most two things touched, by name; the corn
        // dealer last, and buying from him to the harvest buys just that.
        {
            World w = base;
            std::vector<World::ChoicePrompt> asked;
            w.setChooser([&](const World::ChoicePrompt& p) {
                asked.push_back(p);
                if (p.title != "缺糧：跟誰開口") return -1;
                return static_cast<int>(p.options.size()) - 1;  // the corn dealer
            });
            runTo(w, 6, 0);
            w.setMoneyForTest(hh, 400);
            w.setFoodForTest(hh, 20);
            // (No neighbour bringing two sacks the same day for an old kindness: the barn shows only what was bought.)
            WorldTest::holdEvent(w, w.playerHead(), "remembered_kindness", 30);
            w.stepDay();  // month 6, day 0: sixty days to the harvest
            const World::ChoicePrompt* card = nullptr;
            for (const auto& p : asked)
                if (p.title == "缺糧：跟誰開口") card = &p;
            CHECK(card && card->situation.find("離秋收 60 天") != std::string::npos, "the family was not asked whom to turn to for grain");
            CHECK(card && card->situation.find("缺糧的緣故：") != std::string::npos, "the grain card does not say why the house is short");
            if (card) {
                CHECK(card->options.size() <= 4 && card->options.back().rfind("糧商｜", 0) == 0, "the corn dealer is not the grain card's last line");
                for (const auto& o : card->options) {
                    int bars = 0;
                    for (size_t at = o.find("｜"); at != std::string::npos; at = o.find("｜", at + 1)) ++bars;
                    CHECK(bars == 2, ("a grain card line is not 對方｜他要什麼｜也牽動: " + o).c_str());
                    size_t also = o.find("也牽動：");
                    if (also != std::string::npos) {
                        std::string tail = o.substr(also);
                        int commas = 0;
                        for (size_t at = tail.find("、"); at != std::string::npos; at = tail.find("、", at + 1)) ++commas;
                        CHECK(commas <= 1 && tail.find_first_of("0123456789") == std::string::npos, ("也牽動 names more than two things, or numbers: " + o).c_str());
                    }
                }
            }
            CHECK(foodDays(w) > 70 && foodDays(w) < 77, "buying to the harvest did not buy to the harvest");
            CHECK(w.grainRule().empty(), "the grain card set a standing rule");
        }
        // fun-r8 1 / r9 0: buying grain is a standing rule the family sets in 家裡的規矩. A month at a time: bought when
        // the last is eaten, with no card, nothing more than the rule says, every grain on the books. 買到收成 keeps a
        // year's rent and the entry fine in the purse. "Ask me" asks once a harvest year and no more.
        {
            World w = base;
            int cards = 0;
            bool dealer = true;
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "缺糧：跟誰開口") return -1;
                ++cards;
                return dealer ? static_cast<int>(p.options.size()) - 1 : -1;
            });
            auto cardAt = [&](int month, double food, double money = 400) {  // the barn set just before the month's reckoning
                runTo(w, month, 0);
                w.setMoneyForTest(hh, money);
                w.setFoodForTest(hh, food);
                int before = cards;
                w.stepDay();
                return cards - before;
            };
            w.issue({w.today(), "rule", w.actingHead(), "grain:monthly", kNone, kNone});
            w.runCommands();
            CHECK(w.grainRule() == "monthly", "the grain rule could not be set");
            CHECK(cardAt(3, 20) == 0 && foodDays(w) > 45, "a month's grain was not bought by the rule");
            auto bought = [&] { return w.ledger().count("買糧") ? w.ledger().at("買糧") : 0.0; };
            const double b4 = bought();  // (a neighbour's gift of grain may still come: only what is bought counts)
            CHECK(cardAt(4, 40) == 0 && std::abs(bought() - b4) < 0.01, "grain was bought while the month's bread was not eaten");
            double booked = bought();
            // What the rule bought, in days (a neighbour's sacks of wheat on the same day are not the rule's).
            const double daysBefore = WorldTest::grainDaysBought(w);
            CHECK(cardAt(5, 10) == 0 && WorldTest::grainDaysBought(w) - daysBefore > 25 && WorldTest::grainDaysBought(w) - daysBefore < 37,
                  "the rule did not buy the next month, or bought more");
            CHECK((w.ledger().count("買糧") ? w.ledger().at("買糧") : 0) < booked - 1, "the grain bought by the rule is not on the books");
            // 買到收成 keeps the rent and the fine back from what it buys, and the family is asked (once) whom to turn
            // to when the purse cannot do both. But the month's bread comes before the rent (review H3): a barn going
            // empty is filled for the month out of what was kept back, and that is said.
            runTo(w, 9, 0);
            w.issue({w.today(), "rule", w.actingHead(), "grain:harvest", kNone, kNone});
            w.runCommands();
            runTo(w, 3, 0);  // (an entry fine set earlier would be paid on the way)
            WorldTest::addFine(w, hh, 20);
            const double keep = w.grainFloor(w.household(hh));
            CHECK(keep >= WorldTest::rent(w, hh) + 19, "the harvest rule's floor leaves out the rent or the entry fine");
            dealer = false;
            size_t from = w.chronicle().size();
            // (the barn nearly bare: the day's wages and craft money, earned before the reckoning, buy a few days of their own)
            CHECK(cardAt(3, 5, keep + 3) == 1, "買到收成 with the purse at its floor did not ask whom to turn to");
            bool saidDip = false;
            for (size_t i = from; i < w.chronicle().size(); ++i) saidDip = saidDip || w.chronicle()[i].text.find("糧比租要緊") != std::string::npos;
            CHECK(foodDays(w) >= 30 && saidDip, "買到收成 let the barn run down to keep the rent, or did not say it spent the rent on bread");
            CHECK(cardAt(4, 10, keep + 3) == 0 && foodDays(w) >= 30, "the harvest rule's card came twice in a harvest year, or the barn ran down");
            // With enough above the floor for a month, the month is bought and the rent and fine stay in the purse.
            CHECK(cardAt(5, 20, keep + w.dailyBread(hh) * 40 * 0.5) == 0 && foodDays(w) >= 30 && w.household(hh).money >= keep - 0.5,
                  "買到收成 spent the rent and the entry fine on bread it could buy without them");
            dealer = true;
            // "Ask me": once in a harvest year.
            runTo(w, 9, 0);
            w.issue({w.today(), "rule", w.actingHead(), "grain:ask", kNone, kNone});
            w.runCommands();
            CHECK(cardAt(4, 10) == 1, "ask-me did not ask");
            CHECK(cardAt(5, 10) == 0 && foodDays(w) > 30, "ask-me asked twice in a harvest year, or left the barn bare");
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
            const double bought = WorldTest::grainDaysBought(w);
            w.stepDay();  // thirty days to the harvest
            bool said = false;
            for (size_t i = lines; i < w.chronicle().size(); ++i) said = said || w.chronicle()[i].text.find("夠吃到收成") != std::string::npos;
            CHECK(cards == 0 && said && 20 + WorldTest::grainDaysBought(w) - bought > 41, "a small gap with a full purse was put to the family as a hunger card");
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
        Config c = grownHouse(defs, 58);  // a grown head with hands to send out (not a child heir)
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
    for (const char* pick : {"每月買一個月的", "勒緊褲帶"}) {
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
                // fun-r9 0: selling spare grain and paying a debt are standing actions, listed beyond the four; so are
                // the family's own matters (round 10: a grievance to answer, how the holding passes).
                size_t picked = 0;
                for (const auto& a : items) picked += a.id != "sell_grain" && a.id.rfind("pay_debt:", 0) != 0 && a.kind != "kin";
                CHECK(picked <= 4, "the season's list has more than four things");
                for (const auto& a : items) CHECK(ids.insert(a.id).second, "the season's list names one thing twice");
                agendaShown += !items.empty();
                for (const auto& a : items) {
                    if (!a.open) continue;
                    int before = w.timeLeft();
                    w.issue({w.today(), "agenda", w.actingHead(), a.id, kNone, kNone});
                    w.runCommands();
                    CHECK(w.household(w.playerHousehold()).money > -0.01, "a thing on the season's list left the family in the red");
                    const bool standing = a.id == "sell_grain" || a.id.rfind("pay_debt:", 0) == 0;  // fun-r9 0: no free day
                    CHECK(w.timeLeft() == before - (standing ? 0 : 1), "a thing on the season's list did not take one free day");
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
        // Terms on the spot are rare in a free run (a seller in debt is what brings them), so they are staged: a house
        // in debt that likes the family is asked for a parcel; somewhere it names its price and the card takes it.
        int staged = 0;
        for (int seed : {3, 5, 8, 11}) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World w(defs, c);
            bool card = false;
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "開口") return -1;
                card = true;
                return 0;
            });
            w.startSeason();
            for (Id who : w.peopleToSee()) {
                if (w.playerHead() == kNone || w.timeLeft() < 1) break;
                for (const auto& o : w.asksFor(who)) {
                    if (!o.open || o.ask != "sell_parcel") continue;
                    WorldTest::house(w, w.person(who).household).debt = 60;
                    WorldTest::like(w, who, w.playerHead(), 25);
                    card = false;
                    w.issue({w.today(), "ask", w.playerHead(), o.ask + "|||", who, o.object});
                    w.runCommands();
                    staged += card;
                    CHECK(w.household(w.playerHousehold()).money > -0.01, "taking the terms left the family in the red");
                    break;
                }
            }
        }
        CHECK(staged > 0, "a seller in debt who likes the family never named terms to answer on the spot");
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
        int finished = 0, diedAtSchool = 0;
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
            for (int d = 0; d < 5 * kDaysPerYear && w.person(heir).station == "scholar" && w.person(heir).alive(); ++d) w.stepDay();
            if (!w.person(heir).alive()) {
                // Died at school (a fever takes a schoolboy too): told, and the house passed on; then the next seed.
                bool told = false;
                for (size_t i = before; i < w.chronicle().size(); ++i)
                    told = told || (w.chronicle()[i].text.find(w.person(heir).firstName) != std::string::npos && w.chronicle()[i].text.find("去世") != std::string::npos);
                CHECK(told, "the heir who died at school was not told");
                CHECK(w.household(hid).ward != heir, "the land is still held for an heir who died at school");
                ++diedAtSchool;
                continue;
            }
            ++finished;
            bool ended = false;
            for (const auto& e : w.threadEnds()) ended = ended || (e.kind == "path" && e.day >= w.today() - 5 * kDaysPerYear && e.text.rfind(w.person(heir).firstName, 0) == 0);
            CHECK(w.person(heir).station != "scholar" && ended, "the school term did not end with an outcome kept for the family");
            checkInvariants(w, "heir at school");
            break;
        }
        CHECK(tried, "no seed had a head with a wife and an heir at home");
        CHECK(finished == 1, "no seed's heir lived to the end of the school term");
        std::printf("heir at school: %d died there first\n", diedAtSchool);
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
            // A grown head (one who can take up a plan at all), cleared of the sworn friends that would refuse a third.
            World w(defs, grownHouse(defs, 58));
            const Id head = w.playerHead(), hh = w.playerHousehold();
            bool started = false;
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
            WorldTest::noMillFarmer(b);  // (seed 58 since round 12: a neighbour farms the mill and its toll takes the 0.9)
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
            for (int seed : {58, 72, 3, 5, 1, 4}) {
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
                            } else if (a.id.rfind("help_feed:", 0) == 0) {
                                // (round 11: the hungry house is one line that opens the card with every way to help;
                                // taking a child in is one of them, and still says what it does to the stores)
                                w.setChooser([&](const World::ChoicePrompt& p) {
                                    if (p.title != "親友挨餓") return -1;
                                    for (const auto& o : p.options)
                                        if (o.find("接到家裡來住") != std::string::npos) {
                                            ++takes;
                                            CHECK(o.find("存糧從夠吃") != std::string::npos, "taking someone in does not say what it does to the stores");
                                        }
                                    return static_cast<int>(p.options.size()) - 1;
                                });
                                w.doAgenda(a.id);
                                w.setChooser({});
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
        // (round 10: with holdings no longer carved up in the pre-run, 64's kin no longer go hungry; 118's do)
        for (int seed : {64, 58, 61, 118}) {
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

    // fun-r8 2: consequences stick. A neighbour's debt taken to court with coin in the purse is not let go: the court
    // takes the purse (leaving 10 to live on), then whole acres the debt covers, then the ox (sold for 40, the change
    // back); what is taken is the creditor's, no more than is owed, and the rest runs another year. The card's
    // 「先還」 does what it says: the purse now, the rest next year, no land and no ox.
    {
        struct Court {
            bool letGo = false, court = false, settled = false;
            double paidIn = 0, landIn = 0, debtWas = 0, debtNow = 0, moneyNow = 0, acresLost = 0;
            bool oxLost = false;
            Id creditor = kNone, creditorNow = kNone;
        };
        // debt owed to a neighbour, purse, acres: answered with the card option starting `pick` ("" = no answer).
        auto run = [&](double debt, double money, double acres, const std::string& pick) {
            Court r;
            Config c;
            c.seed = 58;
            c.prerun = 25;
            World w(defs, c);
            const Id hh = w.playerHousehold();
            for (const auto& h : w.households())
                if (!h.extinct && h.id != hh && h.head != kNone && h.head != w.lordId() && !h.members.empty() && w.person(h.head).alive()) r.creditor = h.head;
            if (r.creditor == kNone) return r;
            Household& h = WorldTest::house(w, hh);
            WorldTest::setAcres(w, hh, acres);
            h.debt = 0, h.heldDebt = 0;
            WorldTest::addDebt(w, hh, debt, r.creditor);
            h.debtYears = 2;
            h.noOxUntil = 0;
            w.setMoneyForTest(hh, money);
            const Id ch = w.person(r.creditor).household;
            const double chMoney = w.household(ch).money, chAcres = w.household(ch).acres, myAcres = w.household(hh).acres;
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title != "莊園法庭" || pick.empty()) return -1;
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].rfind(pick, 0) == 0) return static_cast<int>(i);
                return -1;
            });
            size_t lines = w.chronicle().size();
            r.debtWas = w.household(hh).debt;
            WorldTest::yearlyDebts(w);
            for (size_t i = lines; i < w.chronicle().size(); ++i) {
                const std::string& t = w.chronicle()[i].text;
                r.letGo = r.letGo || t.find("要不回來了") != std::string::npos;
                r.court = r.court || t.find("法庭判") != std::string::npos || t.find("法庭照") != std::string::npos;
                r.settled = r.settled || t.find("這筆債了了") != std::string::npos;
            }
            r.paidIn = w.household(ch).money - chMoney;
            r.landIn = w.household(ch).acres - chAcres;
            r.acresLost = myAcres - w.household(hh).acres;
            r.oxLost = w.household(hh).noOxUntil > w.today();
            r.debtNow = w.household(hh).debt;
            r.moneyNow = w.household(hh).money;
            r.creditorNow = w.household(hh).creditor;
            w.setChooser({});
            return r;
        };
        // A big debt by the court's own order: purse, a quarter of the land, the ox; the rest stands.
        Court a = run(2000, 108, 20, "");
        CHECK(a.creditor != kNone, "no neighbour to owe");
        CHECK(!a.letGo && a.court, "a debt taken to court was let go while the debtor had coin");
        CHECK(a.moneyNow <= 10.5, "the court did not take the purse");
        CHECK(!a.settled && a.debtNow > 0.5 && a.creditorNow == a.creditor, "what the court could not take was forgiven");
        CHECK(a.oxLost && a.acresLost >= 1 && std::abs(a.landIn - a.acresLost) < 0.01, "the court did not take land and the ox for a big debt");
        CHECK(std::abs(a.paidIn + a.acresLost * 40 + (a.debtNow - a.debtWas)) < 0.01, "what the court took did not go to the creditor");
        // A small remainder: no acre for it; the ox sold for 40, the change back; the creditor gets exactly the debt.
        Court b = run(60, 50, 20, "");
        CHECK(b.acresLost < 0.01, "an acre was taken for a remainder smaller than its worth");
        CHECK(b.oxLost && std::abs(b.paidIn - 60) < 0.01 && b.debtNow < 0.5, "the ox for a small remainder did not pay exactly what was owed");
        CHECK(std::abs(b.moneyNow - 30) < 0.01, "the change from the ox did not come back");
        // 「先還」 on the card: the purse now, the rest next year; nothing else taken.
        Court d = run(100, 50, 20, "先還");
        CHECK(d.acresLost < 0.01 && !d.oxLost, "「先還」 took land or the ox");
        CHECK(std::abs(d.paidIn - 40) < 0.01 && std::abs(d.debtNow - 60) < 0.01 && d.creditorNow == d.creditor, "「先還」 did not leave the rest owed for next year");
    }
    // fun-r8 2: money is never kept once spent (a sum a plan still needs shows as needed again); land bought passes
    // the savings step; a thing reached once (an office, inside an "any" too) stays reached.
    {
        Config c;
        c.seed = 58;
        c.prerun = 25;
        World w(defs, c);
        const Id hh = w.playerHousehold(), head = w.playerHead();
        WorldTest::setAmbition(w, head, "more_land");
        w.setMoneyForTest(hh, 400);
        w.runCommands();
        CHECK(!w.milestoneState(head).empty() && w.milestoneState(head)[0] == 1, "the savings were not seen as reached");
        w.setMoneyForTest(hh, 0);
        CHECK(w.milestoneState(head)[0] != 1, "money spent on something else still showed as saved");
        WorldTest::setAmbition(w, head, "more_land");
        WorldTest::giveLand(w, hh, 2);
        w.runCommands();
        CHECK(!w.milestoneState(head).empty() && w.milestoneState(head)[0] == 1, "land bought before the savings left the savings as the next step");
        // A money gate a plan still needs (the gentry match's 400 pence) is not kept.
        for (const auto& a : defs.ambitions) {
            for (const auto& m : a.milestones) {
                const bool money = m.goal.has("money_ge") || m.goal.has("money_ge_chantry");
                CHECK(!(money && m.kept), ("a money step is kept once reached: " + a.id).c_str());
            }
            if (a.id == "village_offices")
                CHECK(a.milestones.size() > 1 && a.milestones[0].kept && a.milestones[1].kept, "an office reached (inside any) is not kept");
        }
    }
    // fun-r8 3: a clerk, or one the family is getting a church post for, is off the marriage market.
    {
        Config c;
        c.seed = 58;
        c.prerun = 25;
        World w(defs, c);
        Id who = kNone;
        for (Id m : w.household(w.playerHousehold()).members)
            if (m != w.playerHead() && w.ageYears(w.person(m)) >= 15 && w.marriageable(w.person(m))) who = m;
        if (who != kNone) {
            WorldTest::person(w, who).occupation = "clerk";
            CHECK(!w.marriageable(w.person(who)), "a clerk is still on the marriage market");
        }
        // May Day is in May.
        for (const auto& ev : defs.events)
            if (ev.id == "courtship_m") CHECK(ev.conditions.has("month"), "the May Day courtship has no month");
    }

    // fun-r9 0: in an ordinary year a family of 6–8 on 10 acres (about two acres a grown man's bread) feeds itself
    // from its own land and pays the rent.
    {
        std::vector<double> cover;
        // (Three villages, not two: on two the median of some twenty harvests moved by ±0.1 with any change elsewhere.)
        for (uint64_t seed : {118ull, 33ull, 34ull}) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            World w(defs, c);
            for (int y = 0; y < 8 && !w.gameOver(); ++y) {
                while (!w.gameOver() && !(w.month() == 8 && w.dayOfMonth() == 1)) w.stepDay();
                const double lw = std::clamp(w.lastHarvestWeather(), 0.3, 1.5);
                for (const auto& h : w.households()) {
                    if (h.extinct || h.house == "manor" || h.head == kNone || w.person(h.head).occupation == "priest" || h.members.size() < 4) continue;
                    const double per = h.acres / std::max(0.5, w.dailyBread(h.id));
                    if (per < 1.6 || per > 2.3 || h.acres < 6 || h.acres > 14) continue;
                    cover.push_back((h.harvestBread / lw - h.lastRent / 0.3) / (w.dailyBread(h.id) * kDaysPerYear));
                }
                w.stepDay();
            }
        }
        std::sort(cover.begin(), cover.end());
        const double median = cover.empty() ? 0 : cover[cover.size() / 2];
        std::printf("ordinary year, 10 acres for 6-8 mouths: %zu harvests, median %.2f of the bread after the rent\n", cover.size(), median);
        CHECK(cover.size() >= 5 && median >= 1.0, "in an ordinary year a family of 6-8 on 10 acres cannot live on its own land");
        // ...and no more than that with a margin (review M7): the land feeds the house, it does not make it rich.
        CHECK(median <= 1.3, "in an ordinary year a family of 6-8 on 10 acres grows far more than it eats and pays");
    }
    // fun-r9 A, C, D, 4: the grain that changes hands, the rival, Michaelmas on one screen, the standing actions.
    {
        Config c;
        c.seed = 118;
        c.prerun = 25;
        World base(defs, c);
        const Id hh = base.playerHousehold();
        while (!base.gameOver() && !(base.month() == 4 && base.dayOfMonth() == 0)) base.stepDay();
        // A rival with grain to spare, first among those the card names; what he wants is said, and the choice is
        // on the books and in the favours at once.
        Id other = kNone;
        for (const auto& h : base.households())
            if (!h.extinct && h.head != kNone && h.id != hh && h.house != "manor" && w_ok(base, h.id) && (other == kNone || h.acres > base.household(other).acres)) other = h.id;
        CHECK(other != kNone, "no other house to be the rival");
        if (other != kNone) {
            World w = base;
            WorldTest::setRival(w, other);
            WorldTest::house(w, other).food = w.dailyBread(other) * 500;
            w.setFoodForTest(hh, 10);
            w.setMoneyForTest(hh, 30);
            World::ChoicePrompt card;
            w.setChooser([&](const World::ChoicePrompt& p) {
                card = p;
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].find("（宿敵）") != std::string::npos) return static_cast<int>(i);
                return 0;
            });
            size_t lines = w.chronicle().size();
            int r = WorldTest::grainExchange(w);
            CHECK(r == 1 && card.title == "缺糧：跟誰開口", "the grain card was not asked");
            CHECK(!card.options.empty() && card.options[0].find("（宿敵）") != std::string::npos, "the rival is not the grain card's first counterparty");
            CHECK(card.options.size() <= 4, "more than four lines on the grain card");
            bool ledger = false, favour = false;
            for (size_t i = lines; i < w.chronicle().size(); ++i) {
                ledger = ledger || w.chronicle()[i].text.find("記帳：") != std::string::npos;
                favour = favour || w.chronicle()[i].text.rfind("【人情】", 0) == 0;
            }
            CHECK(ledger && favour, "a grain exchange was not booked, or left no favour line");
            CHECK(!w.rivalName().empty() && !w.rivalAfter().empty(), "the rival has no name or nothing it is after");
            CHECK(w.ambitionRace(w.person(w.playerHead())).rfind("在", 0) == 0 || w.person(w.playerHead()).ambition.empty(), "the ambition is not told as a race");
        }
        // A famine, and grain to spare: someone comes to ask. A gift is a big +, selling at three times the price a
        // big −, a loan moves nothing (fun-r9 B: only public acts move the name).
        auto famine = [&](const std::string& pick, double& moved, std::string& title) {
            World w = base;
            WorldTest::setWeather(w, 0.7);
            WorldTest::clearFamineAsks(w);  // (the year played so far may have had its own famine and its asks)
            w.setFoodForTest(hh, 400);
            WorldTest::setRival(w, other);  // the one who comes to ask
            // fun-r12 A5: a famine is the village's, not one house's: the other houses' barns are low too.
            for (const auto& oh : w.households())
                if (oh.id != hh && oh.id != other && w_ok(w, oh.id)) WorldTest::house(w, oh.id).food = 0, WorldTest::house(w, oh.id).money = 0;
            WorldTest::house(w, other).food = 0;
            WorldTest::house(w, other).money = w.costToHarvest(w.household(other)) * 0.95;  // short of the harvest at market, enough to pay dear
            const Id me = w.playerHead();
            WorldTest::person(w, me).reputation = 0;
            title.clear();
            w.setChooser([&](const World::ChoicePrompt& p) {
                title = p.title;
                for (size_t i = 0; i < p.options.size(); ++i)
                    if (p.options[i].rfind(pick, 0) == 0) return static_cast<int>(i);
                return -1;
            });
            WorldTest::famineAsk(w);
            moved = w.person(me).reputation;
        };
        if (other != kNone) {
            double give = 0, sell = 0, lend = 0;
            std::string t1, t2, t3;
            famine("送", give, t1);
            famine("高價賣", sell, t2);
            famine("借", lend, t3);
            CHECK(t1 == "荒年有人上門", "no one came to ask for grain in a famine");
            CHECK(give >= 5 && sell <= -5 && std::abs(lend) < 0.5, "the name did not move by real amounts on a famine gift and sale, or moved on a loan");
        }
        // Michaelmas is one screen: the rent first, and a line for each creditor.
        {
            World w = base;
            WorldTest::addDebt(w, hh, 30, w.lordId());
            std::string sit, title;
            w.setChooser([&](const World::ChoicePrompt& p) {
                if (p.title.find("米迦勒節") != std::string::npos && title.empty()) title = p.title, sit = p.situation;
                return -1;
            });
            WorldTest::michaelmas(w);
            CHECK(title.find("年米迦勒節") != std::string::npos && sit.find("地租") != std::string::npos && sit.find("債主：") != std::string::npos,
                  "Michaelmas was not one screen with the rent and the creditors");
        }
        // review H2: a land pledge goes only at the Michaelmas the card named, only once the Michaelmas screen has said
        // the field is at stake, and only for a debt still half unpaid; the creditor then pays what the field is worth
        // above the debt.
        if (other != kNone) {
            auto runTo = [&](World& w, int month, int day) {
                while (!w.gameOver() && !(w.month() == month && w.dayOfMonth() == day)) w.stepDay();
            };
            // 0 kept, 1 taken at the due Michaelmas, 2 taken a year early, -1 no case to make.
            auto pledged = [&](double owedAtEnd, bool screen, double& refund) {
                World w = base;
                w.setChooser([](const World::ChoicePrompt&) { return -1; });
                WorldTest::giveLordsLand(w, hh, 6);  // a field to pledge besides the one the house lives on (round 10: seed 118 starts on one)
                Id parcel = kNone;
                int n = 0;
                for (const auto& pc : w.parcels())
                    if (!pc.gone && pc.holder == hh) {
                        ++n;
                        if (parcel == kNone || pc.acres < w.parcels()[parcel].acres) parcel = pc.id;
                    }
                if (n < 2) return -1;
                Household& ph = WorldTest::house(w, hh);
                ph.debt = 0, ph.creditor = kNone, ph.heldDebt = 0, ph.heldCreditor = kNone;
                const Id cred = w.household(other).head;
                WorldTest::addDebt(w, hh, 100, cred);
                World::Pledge pl;
                pl.house = hh, pl.creditor = cred, pl.parcel = parcel, pl.credHouse = other, pl.amount = 100;
                pl.due = w.debtDueYear(w.household(hh));
                if (pl.due != w.year() + 1) return -1;  // a loan after the winter is due at the Michaelmas after next
                WorldTest::pledges(w).push_back(pl);
                runTo(w, 8, 29);
                WorldTest::settlePledges(w);
                if (w.parcels()[parcel].holder != hh) return 2;
                w.stepDay();
                runTo(w, 8, 29);  // the due Michaelmas, before it runs
                if (w.parcels()[parcel].holder != hh || WorldTest::pledges(w).empty()) return -1;  // (lost some other way)
                Household& ph2 = WorldTest::house(w, hh);
                ph2.debt = owedAtEnd, ph2.creditor = cred, ph2.heldDebt = 0, ph2.heldCreditor = kNone;
                w.setMoneyForTest(hh, 0);
                if (screen) WorldTest::michaelmas(w);
                WorldTest::settlePledges(w);
                refund = w.household(hh).money;
                return w.parcels()[parcel].holder == hh ? 0 : 1;
            };
            double refund = 0, none = 0;
            const int unsaid = pledged(100, false, none), taken = pledged(100, true, refund), half = pledged(30, true, none);
            std::printf("pledge: unsaid %d, said %d (refund %.0f), mostly paid %d\n", unsaid, taken, refund, half);
            CHECK(unsaid != -1 && taken != -1 && half != -1, "no case to try a land pledge on");
            CHECK(unsaid == 0, "a pledged field was taken before the Michaelmas screen said it was at stake");
            CHECK(taken == 1 && refund >= 0, "a pledged field still unpaid at the Michaelmas named was not taken");
            CHECK(half == 0, "a pledged field was taken for a debt mostly paid");
        }
        // review H4: the rival does not strike a house with a full purse, whoever has died; when he does, the first line
        // of the card is the refusal, and the field he asks for is a quarter of the holding at most.
        if (other != kNone) {
            int rich = 0, poor = 0;
            bool refusalFirst = true, small = true;
            for (int k = 0; k < 2; ++k) {
                World w = base;
                while (!w.gameOver() && w.month() < 7) w.stepDay();  // half a year after the hand-over
                WorldTest::setRival(w, other);
                WorldTest::house(w, other).money = 5000;
                w.setMoneyForTest(hh, k == 0 ? 5000 : 0);
                WorldTest::setLastDeath(w, w.today());
                const double acres = w.household(hh).acres;
                w.setChooser([&](const World::ChoicePrompt& p) {
                    if (p.title.find("宿敵上門") == std::string::npos) return -1;
                    ++(k == 0 ? rich : poor);
                    refusalFirst = refusalFirst && !p.options.empty() && p.options[0].rfind("不", 0) == 0;
                    return -1;
                });
                for (int t = 0; t < 30 && !w.rival().struck; ++t) WorldTest::rivalStrike(w);
                small = small && w.household(hh).acres >= acres * 0.75 - 0.01;
            }
            std::printf("rival strike: full purse %d, empty purse %d\n", rich, poor);
            CHECK(rich == 0, "the rival struck a house with a full purse");
            CHECK(poor <= 1 && refusalFirst && small, "the rival struck twice, or the card's first line gives the land away, or he took more than a quarter");
        }
        // review H1: what the rules bring is always asked, however often it comes in a generation (an entry fine falling
        // due three times is three cards, not two and then silence).
        {
            World w = base;
            int asked = 0;
            w.setChooser([&](const World::ChoicePrompt& p) {
                asked += p.title == "入地費";
                return -1;
            });
            for (int k = 0; k < 3; ++k) {  // three fines in turn, each falling due with an empty purse
                WorldTest::clearFines(w);
                WorldTest::addFine(w, hh, 50);
                w.setMoneyForTest(hh, 0);
                WorldTest::runEvent(w, "entry_fine_due", w.playerHead(), kNone);
            }
            CHECK(asked == 3, ("an entry fine falling due was not always asked: " + std::to_string(asked) + " of 3").c_str());
        }
        // Standing actions: grain beyond what must be kept can always be sold, and money can always pay a debt.
        {
            World w = base;
            w.setFoodForTest(hh, 500);
            w.setMoneyForTest(hh, 100);
            WorldTest::addDebt(w, hh, 30, w.lordId());
            w.startSeason();
            bool sell = false, pay = false, closedCompany = false;
            for (const auto& a : w.seasonAgenda()) {
                sell = sell || a.id == "sell_grain";
                pay = pay || a.id.rfind("pay_debt:", 0) == 0;
                closedCompany = closedCompany || (a.id.rfind("company:", 0) == 0 && !a.open);
            }
            CHECK(sell && pay, "selling spare grain or paying a debt is not on the season's list");
            CHECK(!closedCompany, "a company visit that cannot be made is listed");
            const double owed = w.owedAll(hh);
            w.doAgenda("pay_debt:-1");
            CHECK(w.owedAll(hh) < owed - 29 && w.household(hh).money < 71, "paying the debt did not pay it");
        }
    }
    // fun-r9 0, D: fifteen years played through: no card the family is asked comes more than twice a generation in the
    // same words, the grain cards stay few, and a rival house is found.
    {
        Config c;
        c.seed = 118;
        c.prerun = 25;
        World w(defs, c);
        // (fun-r12 L4: on this seed the head now dies of dysentery in the tenth year, by a shifted draw; the round's
        // fold needs one head living out the fifteen years, so his life is fixed rather than the seed changed.)
        w.spareForTest(w.playerHead());
        std::map<std::tuple<size_t, std::string, std::string>, int> seen;
        // fun-r12 M3: 〔家裡這一票〕 once for the same man and office under one head (the words with the numbers taken out).
        std::map<std::tuple<size_t, std::string>, int> ballots;
        int ballotWorst = 0;
        int grain = 0, worst = 0, grainYear = 0;
        std::map<int, int> grainIn;  // grain cards by harvest year (as the feed test counts them)
        std::string worstCard;
        // fun-r11 1: a card of the yearly round (marked fold: the tax, letters, the manor's call for money, the reeve's
        // books) is asked twice a generation, by its id whatever its words, unless the player's last answer may not
        // be given for them (a crime, a way out of the house, real money: EventRunner::foldSafe); one hungry house a season.
        // The family answers a card of the round with a way that may be given for it (its words up to the first 「{」 or
        // 「（」 open the option); once it answered otherwise, the card may come again.
        std::map<std::string, std::vector<std::string>> safeWays;
        for (const auto& ev : defs.events)
            if (ev.fold)
                for (const auto& o : ev.options)
                    if (EventRunner::foldSafe(o)) safeWays[ev.id].push_back(o.text.substr(0, std::min(o.text.find('{'), o.text.find("（"))));
        std::map<std::pair<size_t, std::string>, int> foldAsked;
        std::set<std::pair<size_t, std::string>> foldOpen;  // answered with a way that is asked again
        std::map<int, int> feedSeason;
        int foldWorst = 0, feedWorst = 0;
        w.setChooser([&](const World::ChoicePrompt& p) {
            const auto& heads = w.dynasties()[w.playerDynasty()].heads;
            int& n = seen[{heads.size(), p.title, p.situation}];
            if (++n > worst) worst = n, worstCard = p.title;
            if (p.title == "家裡這一票") {
                std::string bare;
                for (char ch : p.situation.substr(0, p.situation.find("。")))
                    if (ch < '0' || ch > '9') bare += ch;
                ballotWorst = std::max(ballotWorst, ++ballots[{heads.size(), bare}]);
            }
            if (auto sw = safeWays.find(p.event); sw != safeWays.end()) {
                const std::pair<size_t, std::string> key{heads.size(), p.event};
                if (!foldOpen.count(key)) foldWorst = std::max(foldWorst, ++foldAsked[key]);
                for (size_t i = 0; i < p.options.size(); ++i)
                    for (const auto& pre : sw->second)
                        if (!pre.empty() && p.options[i].rfind(pre, 0) == 0) return static_cast<int>(i);
                foldOpen.insert(key);
                return 0;
            }
            if (p.title == "親友挨餓") feedWorst = std::max(feedWorst, ++feedSeason[w.today() / (3 * kDaysPerMonth)]);
            if (p.title == "缺糧：跟誰開口") ++grain, grainYear = std::max(grainYear, ++grainIn[(w.today() + kDaysPerYear - 8 * kDaysPerMonth) / kDaysPerYear]);
            return 0;
        });
        AutoPlayer ai(119);
        for (int d = 0; d < 15 * kDaysPerYear && !w.gameOver(); ++d) {
            if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0) w.startSeason();
            ai.act(w);
            w.stepDay();
        }
        std::printf("15 years on seed 118: grain cards %d, a card's same words at most %d times a generation (%s), rival %s, family ballot asked at most %d\n", grain, worst,
                    worstCard.c_str(), w.rivalName().c_str(), ballotWorst);
        CHECK(worst <= 2, ("a card came more than twice a generation in the same words: " + worstCard).c_str());
        CHECK(ballotWorst <= 1, "the family's ballot for the same man and office was asked twice under one head");
        std::printf("   yearly round asked at most %d times a generation, hungry houses at most %d a season\n", foldWorst, feedWorst);
        CHECK(foldWorst <= 2, "a card of the yearly round was asked more than twice a generation");
        int folded = 0;
        for (const auto& [k, v] : w.counters()) {
            if (k.rfind("fold:", 0) != 0) continue;
            folded += v;
            const size_t colon = k.rfind(':');
            const std::string id = k.substr(5, colon - 5);
            const size_t opt = std::stoul(k.substr(colon + 1));
            for (const auto& ev : defs.events)
                if (ev.id == id)
                    CHECK(opt < ev.options.size() && EventRunner::foldSafe(ev.options[opt]),
                          ("an old question was answered for the player with a crime, a way out or real money: " + id).c_str());
        }
        std::printf("   old questions answered as last time: %d\n", folded);
        CHECK(folded > 0, "no card of the yearly round was ever answered as last time in fifteen years");
        CHECK(feedWorst <= 1, "more than one hungry house was put to the family in a season");
        // Round 10: the 25-year pre-run now runs the family's own quarrels and passings too, and this seed's family starts
        // poorer than it did (5 acres, not 13), so a hungry house asks most years. What the test guards is the card coming
        // back within the harvest year: at most one a harvest year, and no more than one for each year played (plus the first).
        CHECK(grainYear <= 1, "the grain card came more than once in one harvest year");
        CHECK(grain <= 16, "more grain cards than years played, plus one");
        // (The rival's checks run on seed 119 below: on this seed, with near kin kept out (fun-r12 H1), the one rival the
        // fifteen years find, 謝帕德家 in 1338, takes the head's wife's sister as a wife the next year and is kin from then.)
        // fun-r12 A1: every plan of the family's ends with a line on screen (endPlan counts the ones that did not).
        auto cnt = [&](const std::string& k) { return w.counters().count(k) ? w.counters().at(k) : 0; };
        std::printf("   family plans ended %d, told %d, ended with no line %d\n", cnt("plan_end_ours"), cnt("plan_told"), cnt("plan_untold"));
        CHECK(cnt("plan_end_ours") > 0, "no plan of the family's ended in fifteen years");
        CHECK(cnt("plan_untold") == 0, "a plan of the family's ended with no line saying how");
        CHECK(cnt("plan_told") >= cnt("plan_end_ours"), "fewer outcome lines than plans of the family's ended");
        // fun-r12 A4: the rival is never close kin (a parent's, a sibling's or a child's house).
        CHECK(w.rivalHouse() == kNone || !WorldTest::kinHouse(w, w.rivalHouse()), "the rival is the house of the head's own kin");
    }
    // fun-r11 6: a rival is found in fifteen years and meets the family head-on (a match, a field, grain, or his house at
    // the door for bread). Seed 119, the head's life fixed, the family taking the first way on each card.
    {
        Config c;
        c.seed = 119;
        c.prerun = 25;
        World w(defs, c);
        w.spareForTest(w.playerHead());
        w.setChooser([](const World::ChoicePrompt&) { return 0; });
        AutoPlayer ai(119);
        for (int d = 0; d < 15 * kDaysPerYear && !w.gameOver(); ++d) {
            if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0) w.startSeason();
            ai.act(w);
            w.stepDay();
        }
        const int headOn = w.counters().count("rival_head_on") ? w.counters().at("rival_head_on") : 0;
        std::printf("15 years on seed 119: rival %s, head-on %d times\n", w.rivalName().c_str(), headOn);
        CHECK(w.rivalHouse() != kNone, "no rival house in fifteen years");
        CHECK(headOn >= 1, "the rival never met the family head-on in fifteen years");
        CHECK(w.rivalHouse() == kNone || !WorldTest::kinHouse(w, w.rivalHouse()), "the rival is the house of the head's own kin");
    }
    // fun-r12 A1/A2/A7: the outcome of a plan the family gave up, the sitting reeve's card, the cost on an option, the
    // dead told as dead.
    {
        Config c = grownHouse(defs, 30);
        World w(defs, c);
        const Id head = w.playerHead();
        std::vector<World::ChoicePrompt> asked;
        w.setChooser([&](const World::ChoicePrompt& p) { asked.push_back(p); return -1; });
        // The sitting reeve standing again is asked as one: the card has its title and the words for a sitting reeve.
        WorldTest::flag(w, head, "reeve");
        WorldTest::runEvent(w, "reeve_campaign", head, w.lordId());
        bool reeveCard = false;
        for (const auto& p : asked)
            if (p.event == "reeve_campaign") {
                reeveCard = true;
                CHECK(p.title == "選莊頭" && p.situation.find("你當了這一任") != std::string::npos, "the sitting reeve's card has no title or not its words");
            }
        CHECK(reeveCard, "the reeve's card was not put to the head");
        // 帶頭抗議: what it can do to the rent is on the option before it is chosen.
        asked.clear();
        WorldTest::runEvent(w, "rent_raised", head, w.lordId());
        bool rentNote = false;
        for (const auto& p : asked)
            for (const auto& o : p.options)
                if (o.find("帶頭抗議") != std::string::npos) rentNote = o.find("地租往後加") != std::string::npos;
        CHECK(!asked.empty(), "the rent-raise card was not put to the head");
        CHECK(rentNote, "the rent the protest can cost is not on the option");
        // A plan given up says so on the family's page.
        w.setChooser(nullptr);
        const size_t before = w.chronicle().size();
        const std::string why = WorldTest::startPlan(w, head, "seek_patron", w.lordId());
        CHECK(why.empty(), ("the plan to test could not be started: " + why).c_str());
        if (why.empty()) {
            CHECK(WorldTest::failPlan(w, head, "seek_patron"), "the plan just started is not there");
            bool told = false;
            for (size_t i = before; i < w.chronicle().size(); ++i)
                told = told || (w.chronicle()[i].importance == 1 && w.chronicle()[i].text.find("【失敗】") != std::string::npos);
            CHECK(told, "a failed plan of the family's ended with no 【失敗】 line");
        }
    }

    // Round 10 (design/stages-and-drive.md 三、四、六): the drive inside the family.
    // Sixty years on four seeds: the land and the purse stay in balance through dowers, forfeits and killings; no act past
    // the court comes unseen; every poisoning or killing was preceded by a sign about the one who did it.
    {
        int dowers = 0, forfeits = 0, kills = 0, serious = 0;
        for (int seed : {118, 119, 120, 121}) {
            Config c;
            c.seed = seed;
            World w(defs, c);
            AutoPlayer ai(seed);
            for (int d = 0; d < 60 * kDaysPerYear && !w.gameOver(); ++d) {
                if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0) w.startSeason();
                ai.act(w);
                w.stepDay();
            }
            checkInvariants(w, "kin60");
            CHECK(w.unbookedMoney() < 1, "money moved by the family's quarrels without a book entry");
            auto n = [&](const std::string& k) { return w.counters().count(k) ? w.counters().at(k) : 0; };
            CHECK(n("kin_unsigned") == 0, "an act past the court came with no sign before it");
            dowers += n("kin_dower_out"), forfeits += n("kin_forfeit"), kills += n("kin_kill"), serious += n("kin_player:serious");
            for (const auto& dd : w.deeds()) {
                if (dd.type != "poisoning" && dd.type != "murder") continue;
                bool signed_ = false;
                for (const auto& nw : w.news())
                    if (nw.kind == "kin:sign" && nw.subject == dd.actor && nw.other == dd.target && nw.day <= dd.day) signed_ = true;
                CHECK(signed_, ("a " + dd.type + " with no sign about " + w.fullName(dd.actor) + " before it").c_str());
            }
            for (const auto& dw : w.dowers())
                if (dw.open) CHECK(w.person(dw.widow).alive(), "a dead widow still holds her third");
        }
        std::printf("kin, 4 x 60 years: dowers out %d, goods forfeit %d, killings %d, serious quarrels in the family %d\n", dowers, forfeits, kills, serious);
        CHECK(dowers > 0, "no widow ever took her third in sixty years on four seeds");
        CHECK(serious > 0, "no serious quarrel in the family in sixty years on four seeds");
    }
    // The decider picks on the ladder through the usual path, and the answers replay: the same answers in order give the
    // same world.
    {
        Config c;
        c.seed = 120;
        std::vector<int> answers;
        int kinAsked = 0;
        World a(defs, c);
        a.setNpcScope(World::kFar);
        a.setNpcDecider([&](const World::NpcChoice& ch, std::string& why) {
            int r = ch.title == "心裡的不平" ? static_cast<int>(ch.options.size()) - 1 : -1;  // the hardest rung offered
            kinAsked += ch.title == "心裡的不平";
            why = r >= 0 ? "test" : "";
            answers.push_back(r);
            return r;
        });
        a.runYears(30);
        World b(defs, c);
        b.setNpcScope(World::kFar);
        size_t at = 0;
        b.setNpcDecider([&](const World::NpcChoice&, std::string& why) {
            int r = at < answers.size() ? answers[at++] : -1;
            why = r >= 0 ? "test" : "";
            return r;
        });
        b.runYears(30);
        std::printf("kin decider: %d grievance choices asked, %zu answers replayed\n", kinAsked, answers.size());
        CHECK(kinAsked > 0, "the decider was never asked on the ladder");
        CHECK(a.digest() == b.digest(), "the decider's answers did not replay to the same world");
        CHECK(a.counters().count("kin_unsigned") == 0, "a rung the model chose came with no sign before it");
    }
    // The old parent's keep: terms broken, the parent's grievance carries the claim, and the court's verdict holds.
    {
        Config c;
        c.seed = 119;
        c.prerun = 25;
        World w(defs, c);
        w.runYears(2);
        Id parent = kNone, heir = kNone;
        for (const auto& h : w.households()) {
            if (h.extinct || h.head == kNone || h.id == w.playerHousehold() || h.house == "manor" || h.head == w.lordId()) continue;
            for (Id m : h.members)
                if (w.person(m).father == h.head && w.ageYears(w.person(m)) >= 16 && w.person(m).sex == Sex::Male) parent = h.head, heir = m;
            if (parent != kNone) break;
        }
        CHECK(parent != kNone, "no house with a grown son to hand over to");
        if (parent != kNone) {
            WorldTest::addUpkeep(w, parent, heir, 2, 2);
            WorldTest::kinMotives(w);
            bool claim = false;
            for (const auto& g : w.grievances())
                if (g.who == parent && g.against == heir && g.claim == "照養老的約奉養") claim = true;
            CHECK(claim, "a parent turned out of the house has no claim in the manor court");
            const double rep = w.person(heir).reputation;
            const size_t news = w.news().size();
            bool won = WorldTest::kinCourt(w, parent, heir, "照養老的約奉養", 0.85);
            const auto& u = w.upkeeps().back();
            if (won) CHECK(u.state == 0 && u.ordered >= 0 && w.person(heir).reputation < rep, "the court found for the parent but the terms were not restored");
            else CHECK(u.state == 2, "the parent lost but the terms changed anyway");
            bool heard = false;
            for (size_t i = news; i < w.news().size(); ++i) heard = heard || w.news()[i].kind == "kin:verdict";
            CHECK(heard, "the maintenance suit's verdict did not go into the news");
            checkInvariants(w, "upkeep suit");
        }
    }
    // A son placated with his share has had it from the whole house: his complaint against the brother who would get
    // the land sleeps too, not a second card worded on the shares before he was given anything (fun-r12 A6).
    {
        Config c;
        c.seed = 119;
        c.prerun = 25;
        World w(defs, c);
        w.runYears(1);
        const Id head = w.playerHead();
        Id son = kNone, other = kNone;
        for (Id m : w.household(w.playerHousehold()).members)
            if (m != head && w.person(m).alive() && w.ageYears(w.person(m)) >= 16) {
                if (son == kNone) son = m;
                else if (other == kNone) other = m;
            }
        CHECK(son != kNone && other != kNone, "no two grown people besides the head in the family's house");
        if (son != kNone && other != kNone) {
            const size_t at = w.grievances().size();
            WorldTest::addGrief(w, son, head, "照現在的分法，自己分不到地");
            WorldTest::addGrief(w, son, other, "照現在的分法，自己分不到地");
            int cards = 0;
            w.setChooser([&](const World::ChoicePrompt& p) { cards += p.title == "家裡的不平"; return 0; });
            WorldTest::kinRespond(w, at);
            CHECK(cards == 1, "placating a son did not ask on a card");
            const auto& g = w.grievances();
            CHECK(g.size() > at + 1 && g[at].calmUntil > w.today() && g[at + 1].calmUntil > w.today() && g[at + 1].pending == 0,
                  "a son placated by the head still has an open grievance against the rest of the house");
        }
    }
    // fun-r12 M4: favours are kept one by one; calling one in spends the largest only, and the rest stay owed.
    {
        Config c;
        c.seed = 119;
        c.prerun = 25;
        World w(defs, c);
        const Id head = w.playerHead();
        Id other = kNone;
        for (const auto& h : w.households())
            if (!h.extinct && h.head != kNone && h.id != w.playerHousehold() && h.head != w.lordId()) other = h.head;
        if (other != kNone) {
            const double was = w.favourOwed(other, head);
            WorldTest::oweFavour(w, other, head, 30, "白送了 60 天的糧");
            WorldTest::oweFavour(w, other, head, 6, "答應投你一票");
            WorldTest::spendFavour(w, other, head);
            CHECK(std::abs(w.favourOwed(other, head) - was - 6) < 0.5, "calling in one favour spent more (or less) than that one");
        }
    }
    // fun-r12 L9: 〔家人要出頭〕 by each rule: asked (a card, and the head's yes holds), forbidden (a line, no card, the
    // other way taken), free (neither).
    {
        Config c = grownHouse(defs, 31);
        World base(defs, c);
        const Id head = base.playerHead();
        Id member = kNone, other = kNone;
        for (Id m : base.household(base.playerHousehold()).members)
            if (m != head && base.person(m).alive() && base.ageYears(base.person(m)) >= 18 && base.ageYears(base.person(m)) <= 70) member = m;
        for (const auto& h : base.households())
            if (!h.extinct && h.head != kNone && h.id != base.playerHousehold() && h.head != base.lordId() && base.person(h.head).dynasty != base.person(head).dynasty &&
                base.ageYears(base.person(h.head)) >= 16)
                other = h.head;
        CHECK(member != kNone && other != kNone, "no grown family member and other house for the family-act test");
        if (member != kNone && other != kNone) {
            for (const char* rule : {"ask", "forbid", "free"}) {
                bool gated = false, card = false, ruleLine = false;
                for (int tries = 0; tries < 40 && !gated; ++tries) {
                    World w = base;
                    WorldTest::setQuarrelRule(w, rule);
                    int cards = 0;
                    w.setChooser([&](const World::ChoicePrompt& p) { cards += p.title == "家人要出頭"; return p.title == "家人要出頭" ? 0 : -1; });
                    // Each try draws its own way: skip ahead a few days so the pick differs.
                    for (int d = 0; d < tries; ++d) w.stepDay();
                    auto cnt = [&](const std::string& k) { return w.counters().count(k) ? w.counters().at(k) : 0; };
                    const int yes = cnt("family_act:yes"), no = cnt("family_act:no");
                    const size_t before = w.chronicle().size();
                    WorldTest::runEvent(w, "rival_dispute", member, other);
                    bool line = false;
                    for (size_t i = before; i < w.chronicle().size(); ++i) line = line || w.chronicle()[i].text.find("【家裡的規矩】") != std::string::npos;
                    if (cnt("family_act:yes") != yes || cnt("family_act:no") != no) {
                        gated = true, card = cards > 0, ruleLine = line;
                        if (std::string(rule) == "ask") CHECK(cnt("family_act:yes") == yes + 1, "the head said yes on the card but the family act was not let through");
                    }
                    if (std::string(rule) == "free") CHECK(cards == 0 && cnt("family_act:yes") == yes && cnt("family_act:no") == no, "under 「由他們自己」 a family act was still gated");
                }
                std::printf("family act under %s: gated %d, card %d, rule line %d\n", rule, gated, card, ruleLine);
                if (std::string(rule) == "ask") CHECK(gated && card, "under 「先問我」 a family member's suit or brawl was not put to the head");
                if (std::string(rule) == "forbid") CHECK(gated && !card && ruleLine, "under 「不准」 the family act was not stopped by the rule, with its line");
            }
        }
    }
    // fun-r12 H1, M2: across a change of head (seed 118: the head dies in the tenth year and his widow keeps the house),
    // the rival is never near kin of the head keeping the house or of the one before, never of the family's surname;
    // a standing rival changes at most once a head; no 【宿敵】 line is said twice.
    {
        Config c;
        c.seed = 118;
        c.prerun = 25;
        World w(defs, c);
        static const std::set<std::string> kNear{"parent", "child", "sibling", "spouse", "step_parent", "step_child", "parent_in_law", "child_in_law",
                                                 "sibling_in_law", "uncle", "nephew", "grandparent", "grandchild"};
        AutoPlayer ai(118);
        Id lastHead = w.actingHead(), lastRival = w.rivalHouse();
        int changes = 0, worstChanges = 0, headChanges = 0;
        bool kinRival = false;
        std::string kinWhy;
        for (int d = 0; d < 13 * kDaysPerYear && !w.gameOver(); ++d) {
            if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0) w.startSeason();
            ai.act(w);
            w.stepDay();
            const Id head = w.actingHead(), rival = w.rivalHouse();
            if (head != lastHead) ++headChanges, changes = 0;
            if (rival != lastRival && rival != kNone && lastRival != kNone && w.household(lastRival).head != kNone &&
                !w.household(lastRival).extinct && !WorldTest::kinHouse(w, lastRival) && head == lastHead)
                worstChanges = std::max(worstChanges, ++changes);
            if (rival != kNone && (rival != lastRival || head != lastHead || w.dayOfMonth() == 0)) {
                for (Id m : w.household(rival).members) {
                    if (w.person(m).dynasty == w.playerDynasty()) kinRival = true, kinWhy = "the family's own surname";
                    for (Id l : WorldTest::lineage(w))
                        if (kNear.count(WorldTest::kinTie(w, l, m)) || kNear.count(WorldTest::kinTie(w, m, l))) kinRival = true, kinWhy = w.fullName(m) + " is " + WorldTest::kinTie(w, l, m);
                }
            }
            lastHead = head, lastRival = rival;
        }
        std::map<std::string, int> said;
        int saidTwice = 0;
        for (const auto& e : w.chronicle())
            if (e.text.rfind("【宿敵】", 0) == 0 && ++said[e.text] == 2) ++saidTwice;
        std::printf("rival over 13 years on seed 118: %d changes of head, a standing rival changed at most %d times a head, %zu rival lines\n", headChanges,
                    worstChanges, said.size());
        CHECK(headChanges >= 1, "no change of head on seed 118 to test the rival across");
        CHECK(!kinRival, ("the rival was near kin of the family's line: " + kinWhy).c_str());
        CHECK(worstChanges <= 1, "a standing rival changed more than once under one head");
        CHECK(saidTwice == 0, "the same 【宿敵】 line was said twice");
    }
    // A killing by the family's head: the inquest and jury run, the goods go to the lord on the books, the land stays
    // whole, and the succession is worked out again. And the name passes in part, by the rule on screen.
    {
        Config c;
        c.seed = 121;
        World w(defs, c);
        w.runYears(20);
        Id head = w.playerHead(), victim = kNone;
        for (const auto& h : w.households())
            if (!h.extinct && h.head != kNone && h.id != w.playerHousehold() && h.head != w.lordId() && h.house != "manor") victim = h.head;
        if (head != kNone && victim != kNone) {
            const double unbooked = w.unbookedMoney();
            WorldTest::killing(w, head, victim, false);
            for (int d = 0; d < 40; ++d) w.stepDay();
            checkInvariants(w, "killing");
            CHECK(w.unbookedMoney() - unbooked < 1, "the goods forfeit to the lord were not booked");
            CHECK(!w.person(victim).alive(), "the victim of a killing still lives");
        }
        // The first seed from 122 on whose head after twenty years holds the land in his own right (a regent's name is
        // not the line's to pass), with a grown heir at home. (Round 12B: on seed 122 itself the district now takes the
        // heir to town as an apprentice and leaves his grandmother holding the house for him.)
        Config c2;
        for (c2.seed = 122; c2.seed < 132; ++c2.seed) {
            World t(defs, c2);
            t.runYears(20);
            const Id th = t.playerHead(), tn = th == kNone ? kNone : WorldTest::heirOf(t, t.playerHousehold());
            if (tn != kNone && t.household(t.playerHousehold()).ward == kNone && !t.isAway(t.person(tn)) && t.ageYears(t.person(tn)) >= 14) break;
        }
        World r(defs, c2);
        r.runYears(20);
        Id old = r.playerHead();
        Id next = WorldTest::heirOf(r, r.playerHousehold());
        CHECK(c2.seed < 132, "no seed from 122 with a head in his own right and a grown heir at home after twenty years");
        if (old != kNone && next != kNone && r.ageYears(r.person(next)) >= 14) {
            WorldTest::person(r, old).reputation = 30;
            WorldTest::person(r, next).reputation = 4;
            WorldTest::kill(r, old, "test");
            const double got = r.person(next).reputation;
            std::printf("reputation carried: head 30, heir 4 -> %.0f\n", got);
            CHECK(std::abs(got - 19) <= 2, "the heir's name is not his own plus half the old head's");
        }
    }

    // 地方 (design/district.md): the way in by buying land out there. Land and money only move, the family is in, the
    // panel opens, and blows put it back out (the way up stays open).
    {
        Config c;
        c.seed = 195;
        c.startYear = 1340;
        c.prerun = 25;
        World w(defs, c);
        CHECK(WorldTest::districtBuilt(w), "the district was not set up at the hand-over");
        CHECK(!w.inDistrict() && !w.districtView().in, "the family starts in the district");
        const Id ph = w.playerHousehold();
        if (w.household(ph).money < 400) WorldTest::give(w, ph, 400);
        const double land = WorldTest::landOut(w), money = WorldTest::allMoney(w);
        const double hadley = WorldTest::dacres(w, "hadley");
        WorldTest::bidAlone(w, "brook", 0);
        CHECK(std::abs(WorldTest::landOut(w) - land) < 1e-6, "buying land out there made or lost acres");
        CHECK(std::abs(WorldTest::allMoney(w) - money) < 0.01, "buying land out there made or lost money");
        CHECK(std::abs(w.household(ph).outAcres - 6) < 1e-6 && std::abs(WorldTest::dacres(w, "hadley") - (hadley - 6)) < 1e-6, "the brook land did not pass from the seller to the family");
        CHECK(w.inDistrict(), "buying the brook land did not take the family into the district");
        const World::DistrictView v = w.districtView();
        CHECK(v.in && !v.status.empty() && !v.footholds.empty() && !v.people.empty(), "the district panel is empty once in");
        CHECK(w.counters().count("district:in") && w.counters().at("district:in") == 1, "entering not counted once");
        WorldTest::blow(w, "test 1");
        WorldTest::blow(w, "test 2");
        CHECK(w.inDistrict(), "two blows put the family out (three are needed)");
        WorldTest::blow(w, "test 3");
        CHECK(!w.inDistrict() && !w.districtView().in, "three blows in four years did not put the family out");
        CHECK(w.counters().count("district:fell") && w.counters().at("district:fell") == 1, "the fall not counted");
        WorldTest::enterTier(w, "test");
        CHECK(w.inDistrict(), "the family could not climb back in after a fall");
        checkInvariants(w, "district gate");
    }
    // Loans out there (放貸): lent, repaid, defaulted, the pledge taken, sued for at the hundred court. Money and land
    // only move between purses; the loan's state follows; the family's books are kept to the penny.
    {
        Config c;
        c.seed = 195;
        c.startYear = 1340;
        c.prerun = 25;
        World w(defs, c);
        w.runYears(1);  // past the opening, so the cards below are asked
        std::string pick;
        int asked = 0;
        w.setChooser([&](const World::ChoicePrompt& p) {
            for (size_t i = 0; i < p.options.size(); ++i)
                if (!pick.empty() && p.options[i].rfind(pick, 0) == 0) return ++asked, static_cast<int>(i);
            return -1;
        });
        const Id ph = w.playerHousehold();
        const Id hadley = WorldTest::dshell(w, "hadley");
        WorldTest::give(w, ph, 3000);
        if (WorldTest::dacres(w, "hadley") < 12) WorldTest::dacres(w, "hadley") = 12;
        const double unbooked = w.unbookedMoney();
        // Lent against land, at the lower rate.
        double money = WorldTest::allMoney(w), land = WorldTest::landOut(w);
        int id = WorldTest::addAsk(w, "hadley", 100, 4);
        WorldTest::lend(w, id, 1);
        auto& loans = WorldTest::dloans(w);
        CHECK(!loans.empty() && loans.back().lender == ph && loans.back().borrower == hadley && loans.back().lent == 100 && loans.back().owed == 110 &&
                  loans.back().pledge == 4 && loans.back().state == 0,
              "a pledged loan not booked as lent (100 at 10%, 4 acres)");
        CHECK(std::abs(WorldTest::allMoney(w) - money) < 0.01, "lending made or lost money");
        // Not paid: the pledge taken.
        const size_t first = loans.size() - 1;
        WorldTest::house(w, hadley).money = 0;
        loans[first].due = w.year();
        money = WorldTest::allMoney(w);
        const double out = w.household(ph).outAcres;
        pick = "收下抵押的";
        WorldTest::loansDue(w);
        CHECK(WorldTest::dloans(w)[first].state == 5, "the pledge was not taken when chosen");
        CHECK(std::abs(w.household(ph).outAcres - out - 4) < 1e-6, "the pledged acres did not come to the family");
        CHECK(std::abs(WorldTest::landOut(w) - land) < 1e-6, "taking a pledge made or lost acres");
        CHECK(std::abs(WorldTest::allMoney(w) - money) < 0.01, "a default made or lost money");
        CHECK(w.inDistrict(), "taking land out there for a debt did not take the family into the district");
        // Not paid, no pledge: to the hundred court.
        id = WorldTest::addAsk(w, "hadley", 200, 0);
        WorldTest::lend(w, id, 0);
        const size_t second = WorldTest::dloans(w).size() - 1;
        CHECK(WorldTest::dloans(w)[second].owed == 230 && WorldTest::dloans(w)[second].pledge == 0, "an unpledged loan not at the full rate");
        WorldTest::house(w, hadley).money = 0;
        WorldTest::dloans(w)[second].due = w.year();
        pick = "上百戶法庭告他";
        const size_t suits = WorldTest::dsuits(w).size();
        WorldTest::loansDue(w);
        CHECK(WorldTest::dloans(w)[second].state == 3 && WorldTest::dsuits(w).size() == suits + 1, "the default was not taken to the hundred court");
        money = WorldTest::allMoney(w), land = WorldTest::landOut(w);
        pick = "自己去說";
        WorldTest::court(w);
        const auto& suit = WorldTest::dsuits(w)[suits];
        const int st = WorldTest::dloans(w)[second].state;
        std::printf("district loans: pledge taken, suit over 230 pence: %s (loan state %d), %d cards answered\n", suit.result.c_str(), st, asked);
        CHECK(suit.done && !suit.result.empty(), "the hundred court did not hear the debt");
        CHECK(st == 1 || st == 4, "the loan not settled by the verdict");
        CHECK(std::abs(WorldTest::landOut(w) - land) < 1e-6, "the hundred court made or lost acres");
        CHECK(std::abs(WorldTest::allMoney(w) - money) < 0.01, "the hundred court made or lost money");
        // Paid back: the family's own debt to the town's lender, at Michaelmas.
        const Id draper = WorldTest::dshell(w, "draper");
        WorldTest::addLoan(w, draper, ph, 100, 125);
        const double before = w.household(ph).money, lenderBefore = w.household(draper).money;
        money = WorldTest::allMoney(w);
        WorldTest::loansDue(w);
        CHECK(WorldTest::dloans(w).back().state == 1, "the family's debt out there not paid when it could be");
        CHECK(std::abs(before - w.household(ph).money - 125) < 0.01 && std::abs(w.household(draper).money - lenderBefore - 125) < 0.01, "the repayment did not go from the family to the lender");
        CHECK(std::abs(WorldTest::allMoney(w) - money) < 0.01, "repaying made or lost money");
        w.stepDay();
        CHECK(w.unbookedMoney() - unbooked < 0.01, "a loan moved the family's purse without a line in its books");
        checkInvariants(w, "district loans");
    }
    // The lease (包租): the rent is the lord's whatever the summer; in a bad year the town's lender covers the gap,
    // at his rate, as a debt on the books.
    {
        Config c;
        c.seed = 195;
        c.startYear = 1340;
        c.prerun = 25;
        World w(defs, c);
        w.runYears(1);
        const Id ph = w.playerHousehold();
        auto* s = WorldTest::stake(w, "seat_farm");
        CHECK(s && s->kind == "lease", "no lease in the district data");
        if (s) {
            s->holder = ph, s->holderMan = w.playerHead(), s->until = w.today() + 2 * kDaysPerYear;
            WorldTest::setWeather(w, 0.5);
            WorldTest::give(w, ph, 10 - w.household(ph).money);
            const size_t loans = WorldTest::dloans(w).size();
            const double crop = std::round(s->acres * s->yield * 0.5);
            WorldTest::districtMichaelmas(w);
            const auto& all = WorldTest::dloans(w);
            double lent = 0, owed = 0;
            for (size_t i = loans; i < all.size(); ++i)
                if (all[i].borrower == ph && all[i].lender == WorldTest::dshell(w, "draper")) lent = all[i].lent, owed = all[i].owed;
            std::printf("district lease in a bad year: crop %.0f, rent %.0f, borrowed %.0f, owed %.0f\n", crop, s->rent, lent, owed);
            CHECK(lent > 0 && std::abs(lent - (s->rent - crop - 10)) < 0.01 && std::abs(owed - std::round(lent * 1.25)) < 0.01,
                  "the lease's bad-year shortfall was not borrowed from the town's lender at his rate");
            CHECK(s->holder == ph, "the lease was lost though the rent was paid");
            w.stepDay();
            checkInvariants(w, "district lease");
        }
    }
    // Rich start (seed 195 from 1340), fifteen years: the family goes into the district, meets someone over it there,
    // and the books stay whole; the decider's district answers replay to the same world.
    {
        Config c;
        c.seed = 195;
        c.startYear = 1340;
        c.prerun = 25;
        World w(defs, c);
        const double unbooked = w.unbookedMoney();
        w.runYears(15);
        auto n = [&](const std::string& k) { return w.counters().count(k) ? w.counters().at(k) : 0; };
        std::printf("district, seed 195 from 1340, 15 years: in %d, fell %d, rounds %d, our bids %d, won %d, clashes %d, suits %d, village houses in %d, blows %d\n",
                    n("district:in"), n("district:fell"), n("district:round"), n("district:bid_ours"), n("district:won_ours"), n("district:clash"), n("district:suit"),
                    n("district:npc_in"), n("district:blow"));
        CHECK(n("district:in") >= 1, "a rich family never got into the district in fifteen years");
        CHECK(n("district:clash") >= 1, "no clash over anything in the district in fifteen years");
        CHECK(n("district:npc_in") >= 1, "no village house went into the district in fifteen years");
        CHECK(w.unbookedMoney() - unbooked < 1, "the district moved the family's purse without a line in its books");
        checkInvariants(w, "district 15 years");

        std::vector<int> answers;
        int districtAsked = 0;
        auto ours = [](const std::string& t) { return t.rfind("出價：", 0) == 0 || t.rfind("誰拿到", 0) == 0 || t.rfind("這一年怎麼對付", 0) == 0; };
        World a(defs, c);
        a.setNpcDecider([&](const World::NpcChoice& ch, std::string& why) {
            int r = ours(ch.title) ? 0 : -1;
            districtAsked += r == 0;
            why = r >= 0 ? "test" : "";
            answers.push_back(r);
            return r;
        });
        a.runYears(6);
        World b(defs, c);
        size_t at = 0;
        b.setNpcDecider([&](const World::NpcChoice&, std::string& why) {
            int r = at < answers.size() ? answers[at++] : -1;
            why = r >= 0 ? "test" : "";
            return r;
        });
        b.runYears(6);
        std::printf("district decider: %d district choices asked, %zu answers replayed\n", districtAsked, answers.size());
        CHECK(districtAsked > 0, "the decider was never asked a district choice");
        CHECK(a.digest() == b.digest(), "the decider's district answers did not replay to the same world");
    }

    if (failures == 0) std::printf("all tests passed\n");
    return failures == 0 ? 0 : 1;
}
