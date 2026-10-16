// Headless tests for SimCore: determinism and state invariants over long runs.
#include <cmath>
#include <cstdio>
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
    (void)label;
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
