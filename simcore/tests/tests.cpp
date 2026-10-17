// Headless tests for SimCore: determinism and state invariants over long runs.
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "simcore/autoplayer.h"
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
    for (const auto& h : w.households()) {
        if (h.extinct) {
            CHECK(h.members.empty(), "extinct household has members");
            continue;
        }
        if (h.members.empty()) continue;
        CHECK(h.head != kNone, "household without head");
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
    (void)label;
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
    // and a no is pressed only once more, in the same season, for another free day.
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
                        if (w.lastAsk().text == said) break;  // not pressed (no time left, or no money for the lever)
                        CHECK(w.timeLeft() == before - 1, "pressing an ask cost no time");
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
                        // pay for takes no day and changes nothing; one it can takes exactly one day; the front ends
                        // (leverOpen) offer exactly the presses that go through.
                        for (const std::string& lever : std::vector<std::string>(lv)) {
                            for (double m = 0; m <= 400; m += 2) {
                                World b = a;
                                b.setMoneyForTest(b.playerHousehold(), m);
                                bool offered = b.leverOpen(lever);
                                int before = b.timeLeft();
                                std::string said = b.lastAsk().text;
                                b.issue({b.today(), "ask", b.playerHead(), o.ask + "|" + o.office + "|" + lever + "|" + sub, who, o.object});
                                b.runCommands();
                                bool pressed = b.timeLeft() != before;
                                CHECK(b.household(b.playerHousehold()).money > -0.01, "a press left the family in the red");
                                CHECK(!pressed || b.timeLeft() == before - 1, "a press took more than one day");
                                CHECK(pressed || b.lastAsk().text == said, "a press that took no day still changed the answer");
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
    // 年度重心 (data/focus.json): the same children, years spent playing or in the fields, grow up differently;
    // a focus the person cannot take is refused; the choice shows in the command log.
    {
        // The first village (by seed) whose family has young children at home when the player takes over.
        std::optional<World> found;
        std::vector<Id> kids;
        for (int seed = 4; kids.empty() && seed < 30; ++seed) {
            Config c;
            c.seed = seed;
            c.prerun = 25;
            found.emplace(defs, c);
            for (Id m : found->household(found->playerHousehold()).members) {
                double age = found->ageYears(found->person(m));
                if (age >= 5 && age < 10) kids.push_back(m);
            }
        }
        const World& base = *found;
        CHECK(!kids.empty(), "no young child at home to test the focus on");
        auto grow = [&](const std::string& focus) {
            World w = base;
            for (Id k : kids) w.issue({w.today(), "focus", w.playerHead(), focus, k, kNone});
            w.runCommands();
            for (Id k : kids) CHECK(w.person(k).focus == focus, "a focus the family chose was not taken");
            w.runYears(3);
            double charm = 0, farming = 0;
            for (Id k : kids) charm += w.person(k).skills[Charm], farming += w.person(k).skills[Farming];
            return std::make_pair(charm, farming);
        };
        auto play = grow("play"), field = grow("farmhand");
        CHECK(play.first > field.first + 0.5 && field.second > play.second + 0.5, "years playing and years in the fields grew the same child");
        World w = base;
        w.issue({w.today(), "focus", w.playerHead(), "craft", kids[0], kNone});  // an adult's focus, for a child
        w.runCommands();
        CHECK(w.person(kids[0]).focus.empty(), "a child took an adult's focus");
        std::printf("focus: %zu children, charm %.1f vs %.1f, farming %.1f vs %.1f after 3 years\n", kids.size(), play.first, field.first,
                    play.second, field.second);
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

    if (failures == 0) std::printf("all tests passed\n");
    return failures == 0 ? 0 : 1;
}
