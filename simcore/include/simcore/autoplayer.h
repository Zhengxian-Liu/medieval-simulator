// A stand-in for the human player: plays the head of the family through commands only.
// Used by the chronicle tool and by replay tests. It never touches World internals.
#pragma once

#include "simcore/world.h"

namespace sim {

class AutoPlayer {
public:
    explicit AutoPlayer(uint64_t seed) : rng_(seed, hashString("autoplayer")) {}

    // Call once before each World::stepDay().
    void act(World& w) {
        if (w.dayOfMonth() != 0 || w.gameOver()) return;
        Id head = w.playerHead();
        if (head == kNone) return;
        if (w.month() % 3 == 0) seePeople(w, head);
        if (w.activePlan(head) || !rng_.chance(0.35)) return;
        std::vector<PlanOption> opts = w.availablePlans(head);
        int pick = choosePlanOption(opts, rng_);
        if (pick < 0) return;
        const PlanOption& o = opts[pick];
        w.issue({w.today(), "start_plan", head, o.plan, o.target, o.subject});
    }

private:
    Rng rng_;

    // At the start of a season: answer most of what the family wants of the head, and now and then go and
    // see someone, keeping a day for a plan.
    void seePeople(World& w, Id head) {
        w.startSeason();
        int spare = w.timeLeft() - 1;
        std::vector<Id> people = w.peopleToSee();
        auto open = [&](Id who, const std::string& id) {
            for (const auto& in : w.interactionsWith(who))
                if (in.open && in.ev->id == id) return true;
            return false;
        };
        for (Id id : people) {
            if (spare <= 0) return;
            const Person& p = w.people()[id];
            if (p.household != w.people()[head].household) continue;
            const WantDef* want = w.defs().want(p.want);
            if (!want || !rng_.chance(0.65)) continue;
            for (const auto& by : want->answeredBy)
                if (open(id, by)) {
                    w.issue({w.today(), "interact", head, by, id, kNone});
                    --spare;
                    break;
                }
        }
        // Now and then ask someone for something (開口): land, a vote, a match, a place for a son.
        if (spare > 0 && rng_.chance(0.3)) {
            std::vector<Id> pool;
            for (Id id : people)
                if (w.people()[id].household != w.people()[head].household) pool.push_back(id);
            if (!pool.empty()) {
                Id who = pool[rng_.range(0, static_cast<int>(std::min<size_t>(pool.size(), 10)) - 1)];
                std::vector<AskOption> asks;
                for (const auto& a : w.asksFor(who))
                    if (a.open) asks.push_back(a);
                if (!asks.empty()) {
                    const AskOption& a = asks[rng_.range(0, static_cast<int>(asks.size()) - 1)];
                    w.issue({w.today(), "ask", head, a.ask + "|" + a.office + "||" + (a.subject == kNone ? "" : std::to_string(a.subject)), who, a.object});
                    --spare;
                }
            }
        }
        if (spare <= 0 || people.empty() || !rng_.chance(0.5)) return;
        Id who = people[rng_.range(0, static_cast<int>(std::min<size_t>(people.size(), 8)) - 1)];
        std::vector<std::string> ids;
        for (const auto& in : w.interactionsWith(who))
            if (in.open && in.ev->id != "talk_comfort") ids.push_back(in.ev->id);
        if (!ids.empty()) w.issue({w.today(), "interact", head, ids[rng_.range(0, static_cast<int>(ids.size()) - 1)], who, kNone});
    }
};

}  // namespace sim
