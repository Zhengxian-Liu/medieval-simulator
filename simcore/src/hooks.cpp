// Secrets as currency (Crusader Kings "hooks"): believing you know someone's secret gives you
// leverage over them, whether or not you are right. And the truth has ways of coming out: a
// dying man wants to go to God with a clean conscience.
#include <algorithm>

#include "simcore/events.h"

namespace sim {

// The secret `holder` is most sure `culprit` did, if it is still worth anything: believed firmly,
// never tried in court, not yet spent, and not too old to matter.
Id World::hookDeed(Id holder, Id culprit) const {
    if (holder == kNone || culprit == kNone || holder == culprit) return kNone;
    Id best = kNone;
    double bestConf = 0;
    for (const auto& b : beliefs_[holder]) {
        if (b.culprit != culprit || b.confidence < 60 || b.used) continue;
        const Deed& d = deeds_[b.deed];
        if (d.actor == holder || day_ - d.day > 30 * kDaysPerYear) continue;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret" || deedTried(d.id)) continue;
        if (b.confidence > bestConf) {
            bestConf = b.confidence;
            best = d.id;
        }
    }
    return best;
}

bool World::hasHook(Id holder) const {
    for (const auto& b : beliefs_[holder]) {
        if (b.confidence < 60 || b.used || b.culprit == holder) continue;
        const Person& c = people_[b.culprit];
        if (!c.alive() || c.household == kNone || c.household == people_[holder].household) continue;
        if (hookDeed(holder, b.culprit) != kNone) return true;
    }
    return false;
}

// Before dying, someone who did a secret wrong may send for the one they wronged. Families who
// spent years blaming the wrong house learn it here.
void World::deathbedConfession(Id dying) {
    const Person& p = people_[dying];
    if (ageYears(p) < 30) return;
    double chance = 0.2 + (hasTrait(p, "pious") ? 0.3 : 0) + p.stress / 200 - (hasTrait(p, "cruel") ? 0.15 : 0);
    if (!rngKnow_.chance(std::clamp(chance, 0.0, 0.8))) return;
    int told = 0;
    for (size_t i = deeds_.size(); i-- > 0 && told < 2;) {
        const Deed& d = deeds_[i];
        if (d.actor != dying) continue;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret" || d.type == "affair") continue;
        // Tell the victim, or whoever heads the victim's house now.
        Id victim = d.target;
        if (victim == kNone) continue;
        if (!people_[victim].alive() || people_[victim].household == kNone) {
            Id hh = people_[victim].household;
            if (hh == kNone || households_[hh].extinct) continue;
            victim = households_[hh].head;
        }
        if (victim == kNone || !people_[victim].alive()) continue;
        const Belief* before = beliefAbout(victim, d.id);
        if (before && before->culprit == dying && before->confidence >= 90) continue;
        ++told;
        Id wrong = before && before->culprit != dying && before->confidence >= 50 ? before->culprit : kNone;
        if (people_[victim].household == playerHousehold_) {
            std::string line = "【臨終】" + fullName(dying) + " 臨死前派人把 " + fullName(victim) + " 叫到床前，承認 " +
                               describeDeed(d.id) + "是" + (p.sex == Sex::Male ? "他" : "她") + "幹的。";
            if (wrong != kNone) line += "原來這些年家裡一直錯怪了 " + fullName(wrong) + "。";
            log(1, line);
            toldPlayer_.insert({d.id, dying});
        }
        addBelief(victim, d.id, dying, 100, dying);
        // Everyone in the victim's house hears it from them.
        for (Id m : households_[people_[victim].household].members)
            if (m != victim) addBelief(m, d.id, dying, 95, victim);
        if (wrong != kNone && people_[wrong].alive() && people_[wrong].household != kNone)
            runEventById("make_amends", victim, wrong);
    }
}

}  // namespace sim
