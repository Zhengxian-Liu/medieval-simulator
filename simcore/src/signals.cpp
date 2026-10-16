// Typed signals and the delayed-event queue. Events with an "on" block listen for a signal
// and are scheduled some months later, so a consequence can arrive long after its cause.
#include <algorithm>

#include "simcore/events.h"

namespace sim {

namespace {

bool matchesList(const Json& want, const std::string& have) {
    if (want.isNull()) return true;
    if (want.isString()) return want.asString() == have;
    for (const auto& w : want.asArray())
        if (w.asString() == have) return true;
    return false;
}

}  // namespace

void World::emit(const Signal& s) {
    for (const auto& ev : defs_.events) {
        const Json& on = ev.on;
        if (on.isNull() || on.stringOr("signal", "") != s.type) continue;

        const Deed* d = s.deed != kNone ? &deeds_[s.deed] : nullptr;
        if (!on["deed_type"].isNull() && (!d || !matchesList(on["deed_type"], d->type))) continue;
        if (!on["tag"].isNull()) {
            const DeedDef* dd = d ? defs_.deed(d->type) : nullptr;
            if (!dd || std::find(dd->tags.begin(), dd->tags.end(), on["tag"].asString()) == dd->tags.end()) continue;
        }
        if (on["was_head"].isBool() && on["was_head"].asBool() != s.flag) continue;

        std::string as = on.stringOr("as", "target");
        Id person = kNone, target = kNone;
        if (s.type == "deed") {
            person = as == "actor" ? s.a : s.b;
            target = as == "actor" ? s.b : s.a;
        } else if (s.type == "belief") {
            // "as": "culprit": the event happens to the one believed guilty; the target is the believer.
            bool culprit = as == "culprit";
            person = culprit ? s.b : s.a;
            target = culprit ? s.a : s.b;
            // "victim_side": only when the holder is the victim or lives with the victim.
            if (on["victim_side"].isBool() && on["victim_side"].asBool() && d) {
                Id victim = d->target;
                bool side = person == victim ||
                            (victim != kNone && people_[victim].household != kNone &&
                             people_[person].household == people_[victim].household);
                if (!side) continue;
            }
        } else if (s.type == "stress") {
            person = s.a;
            if (s.value < on.numberOr("min_level", 1) || s.value > on.numberOr("max_level", 3)) continue;
        } else if (s.type == "death") {
            person = as == "spouse" ? s.b : s.a;
            target = kNone;
        } else if (as == "head") {
            // The head of the house of the one it happened to, who is then the target (a wife's grief, a son's
            // longing: what the head of the house hears of it).
            Id hh = s.a != kNone ? people_[s.a].household : kNone;
            person = hh != kNone ? households_[hh].head : kNone;
            target = s.a;
            if (person == s.a) continue;
        } else {
            // Signals raised by events: "as": "actor" (default) is the one who raised it.
            person = as == "target" && !on["as"].isNull() ? s.b : s.a;
            target = person == s.a ? s.b : s.a;
        }
        if (person == kNone || !people_[person].alive()) continue;
        if (!rngEvents_.chance(ev.chance)) continue;

        int lo = 0, hi = 0;
        if (on["after_months"].isArray()) {
            lo = on["after_months"][0].asInt();
            hi = on["after_months"][1].asInt();
        }
        int months = lo + (hi > lo ? rngEvents_.range(0, hi - lo) : 0);
        int delay = months * kDaysPerMonth + (months > 0 ? rngEvents_.range(0, kDaysPerMonth - 1) : 1);
        scheduled_.push_back({day_ + delay, ev.id, person, target, s.deed, scheduleSeq_++});
    }
}

void World::runScheduled() {
    if (scheduled_.empty()) return;
    std::vector<Scheduled> due;
    for (size_t i = 0; i < scheduled_.size();) {
        if (scheduled_[i].day <= day_) {
            due.push_back(scheduled_[i]);
            scheduled_.erase(scheduled_.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
    std::sort(due.begin(), due.end(), [](const Scheduled& a, const Scheduled& b) { return a.seq < b.seq; });
    for (const auto& s : due) {
        const EventDef* ev = nullptr;
        for (const auto& e : defs_.events)
            if (e.id == s.event) ev = &e;
        if (!ev || !people_[s.person].alive()) continue;
        EventRunner runner(*this);
        runner.fireScheduled(*ev, people_[s.person], s.target, s.deed);
        if (gameOver_) return;
    }
}

}  // namespace sim
