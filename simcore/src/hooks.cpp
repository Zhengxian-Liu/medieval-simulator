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

// A secret that is no longer one between the people in it: said out loud, or tried in a court.
bool World::deedSpoken(Id deed) const { return deedNotes_.count({deed, "spoken"}) > 0 || deedTried(deed); }

// What the head knows and has never said comes up when the people in it cross paths: a wife's old lover at the
// church door, the man who fired the ricks greeting you at the mill. Now and then, once for each secret.
void World::secretSurfaces() {
    Id head = playerHead();
    if (head == kNone || ageYears(people_[head]) < 16 || playerHousehold_ == kNone) return;
    if (!rngKnow_.chance(0.12)) return;
    const Household& home = households_[playerHousehold_];
    std::vector<std::pair<Id, Id>> found;  // (deed, the other party)
    for (const auto& b : beliefs_[head]) {
        // Not what has been said out loud already (shouted in the street, taken to court, thrown in someone's face),
        // nor what was turned into money once.
        if (b.confidence < 60 || b.used || deedSpoken(b.deed) || deedNotes_.count({b.deed, "surfaced"})) continue;
        const Deed& d = deeds_[b.deed];
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret" || day_ - d.day > 30 * kDaysPerYear || d.actor == head) continue;
        const Person& a = people_[d.actor];
        if (!a.alive() || b.culprit != d.actor) continue;
        if (d.type == "affair") {
            // One of the family with someone outside it.
            if (a.household != home.id || d.with == kNone || d.with == head || a.flags.count("adulterer")) continue;
            const Person& o = people_[d.with];
            if (!o.alive() || o.household == kNone || o.household == home.id) continue;
            found.push_back({d.id, d.with});
        } else {
            // A wrong done to this house by someone outside it, never tried.
            if (a.household == kNone || a.household == home.id || d.target == kNone || deedTried(d.id)) continue;
            const Person& v = people_[d.target];
            if (v.household != home.id && !(playerDynasty_ != kNone && v.dynasty == playerDynasty_)) continue;
            found.push_back({d.id, d.actor});
        }
    }
    if (found.empty()) return;
    auto [deed, other] = found[rngKnow_.range(0, static_cast<int>(found.size()) - 1)];
    deedNotes_.insert({deed, "surfaced"});
    runEventById(deeds_[deed].type == "affair" ? "old_flame" : "old_wrong", head, other, deed);
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
        if (inPlayerHouse(people_[victim].household)) {
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
