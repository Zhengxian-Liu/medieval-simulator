// Stress (after Crusader Kings 3): grief, hardship and, above all, acting against one's own
// nature weigh on a person. Past a threshold they break, and how they break depends on who
// they are: the bottle, the confessional, the fields, a fist on the table.
#include <algorithm>

#include "simcore/events.h"

namespace sim {

void World::addStress(Id id, double amount, const std::string& cause) {
    if (id == kNone) return;
    Person& p = people_[id];
    if (!p.alive() || p.household == kNone || ageYears(p) < 10) return;
    if (amount > 0) {
        amount *= 1 + traitMod(p, "stress_gain");
        if (p.breaks >= 3) amount *= 0.5;  // someone who has broken three times has learned to bear things
        // The heaviest recent weight is what the chronicle names.
        double fading = p.stressCauseValue * 0.5;
        if (amount >= fading && !cause.empty()) {
            p.stressCause = cause;
            p.stressCauseValue = amount;
        }
    }
    p.stress = std::clamp(p.stress + amount, 0.0, 130.0);
}

void World::monthlyStress() {
    for (auto& p : people_) {
        if (!p.alive() || p.household == kNone) continue;
        const Household& h = households_[p.household];
        if (h.food <= 0 && p.needs[Hunger] < 30) addStress(p.id, 2, "挨餓的日子");
        if (isWinter() && p.needs[Warmth] < 30) addStress(p.id, 2, "冷得睡不著的冬夜");
        // Time, prayer and company ease it.
        double relief = 2 + traitMod(p, "stress_decay");
        if (p.needs[Spirit] > 70) relief += 1;
        if (p.needs[Social] > 70) relief += 1;
        if (p.spouse != kNone && people_[p.spouse].alive() && opinion(p.id, p.spouse) > 40) relief += 1;
        p.stress = std::max(0.0, p.stress - std::max(0.5, relief));
        p.stressCauseValue *= 0.9;

        int level = p.stress >= 100 ? 3 : p.stress >= 70 ? 2 : p.stress >= 40 ? 1 : 0;
        if (level > p.stressLevel && day_ - p.lastBreakDay >= kDaysPerYear) {
            // Breaking twice over the same thing is no longer a bad patch: it is a collapse.
            int shown = !p.lastBreakCause.empty() && p.lastBreakCause == p.stressCause ? 3 : level;
            p.stressLevel = level;
            p.lastBreakDay = day_;
            p.lastBreakCause = p.stressCause;
            ++p.breaks;
            Signal s{"stress", kNone, p.id, kNone, false, static_cast<double>(shown)};
            emit(s);
        } else if (level < p.stressLevel) {
            p.stressLevel = level;
        }
    }
}

// Unburdening: the listener learns every secret deed this person did and is still hiding.
std::vector<Id> World::confessSecrets(Id id, Id to) {
    std::vector<Id> told;
    if (to == kNone || !people_[to].alive()) return told;
    for (const auto& d : deeds_) {
        if (d.actor != id) continue;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret") continue;
        const Belief* b = beliefAbout(to, d.id);
        if (b && b->culprit == id && b->confidence >= 90) continue;
        addBelief(to, d.id, id, 95, id);
        told.push_back(d.id);
    }
    return told;
}

bool World::hasUntoldSecret(Id id, Id to) const {
    for (const auto& d : deeds_) {
        if (d.actor != id) continue;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret") continue;
        const Belief* b = to != kNone ? beliefAbout(to, d.id) : nullptr;
        if (!(b && b->culprit == id && b->confidence >= 90)) return true;
    }
    return false;
}

// "1317 年背著羅伯特和尼可拉斯·史密斯好過" / "1322 年的「放火」，苦主是威廉·格林"
std::string World::secretWords(Id deed) const {
    const Deed& d = deeds_[deed];
    std::string year = std::to_string(cfg_.startYear + d.day / kDaysPerYear) + " 年";
    if (d.type == "affair" && d.with != kNone)
        return year + (d.target != kNone ? "背著" + people_[d.target].firstName : std::string()) + "和 " + fullName(d.with) + " 好過";
    std::string out = describeDeed(deed);
    if (d.target != kNone && d.target != d.actor) out += "，苦主是 " + fullName(d.target);
    return out;
}

// The lover `who` is sure `member` had: an affair `who` believes in, with the other party known.
Id World::knownFlame(Id who, Id member) const {
    if (who == kNone || member == kNone) return kNone;
    for (const auto& b : beliefs_[who]) {
        if (b.culprit != member || b.confidence < 60) continue;
        const Deed& d = deeds_[b.deed];
        if (d.type == "affair" && d.actor == member && d.with != kNone) return d.with;
    }
    return kNone;
}

}  // namespace sim
