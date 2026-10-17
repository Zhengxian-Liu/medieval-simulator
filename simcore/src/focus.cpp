// 年度重心 (design/life-paths.md, data/focus.json): where each person puts their days. It is set once and kept;
// every month it adds a little to skills, acquaintance, standing and the harvest. The gates read the skills
// later, so a road nobody trained for quietly drops off the menu or has poor odds.
#include <algorithm>
#include <cmath>

#include "simcore/events.h"
#include "simcore/world.h"

namespace sim {

namespace {
const char* stageOf(double age) { return age < 14 ? "child" : "adult"; }
}  // namespace

std::vector<const FocusDef*> World::focusChoices(Id who) const {
    std::vector<const FocusDef*> out;
    const Person& p = people_[who];
    if (!p.alive() || p.household == kNone) return out;
    double age = ageYears(p);
    if (age < 5) return out;
    EventRunner er(const_cast<World&>(*this));
    for (const auto& f : defs_.foci)
        if (f.stage == stageOf(age) && (f.conditions.isNull() || er.eval(f.conditions, p, kNone))) out.push_back(&f);
    return out;
}

const FocusDef* World::focusOf(const Person& p) const {
    if (!p.alive() || p.household == kNone || ageYears(p) < 5) return nullptr;
    if (!p.focus.empty())
        for (const FocusDef* f : focusChoices(p.id))
            if (f->id == p.focus) return f;
    // Nobody chose, or what was chosen is no longer open (a child's focus at fourteen): the default.
    return focusDefault(p);
}

const FocusDef* World::focusDefault(const Person& p) const {
    if (!p.alive() || p.household == kNone || ageYears(p) < 5) return nullptr;
    std::vector<const FocusDef*> open = focusChoices(p.id);
    auto pick = [&](const std::string& id) -> const FocusDef* {
        for (const FocusDef* f : open)
            if (f->id == id) return f;
        return nullptr;
    };
    const FocusDef* f = nullptr;
    if (ageYears(p) < 14) {
        if (p.station == "apprentice") f = pick("errand");  // at the master's bench all day
        if (!f && p.flags.count("schooled")) f = pick("letters");
        if (!f) f = pick(defs_.focusDefaults.at(p.sex == Sex::Male ? "child_male" : "child_female"));
    } else {
        const OccupationDef* od = defs_.occupation(p.occupation);
        if (od && od->monthlyIncome > 0) f = pick(defs_.focusDefaults.at("adult_craft"));
        if (!f) f = pick(defs_.focusDefaults.at("adult"));
    }
    return f ? f : (open.empty() ? nullptr : open.front());
}

bool World::steersFocus(Id who) const {
    if (who == kNone || who < 0 || who >= static_cast<Id>(people_.size())) return false;
    const Person& p = people_[who];
    return p.home != kNone ? inPlayerHouse(p.home) : inPlayerHouse(p.household);
}

void World::monthlyFocus() {
    std::vector<Id> village = livingInVillage();
    if (village.empty()) return;
    for (Id id : village) {
        Person& p = people_[id];
        const FocusDef* f = focusOf(p);
        if (!f) continue;
        const FocusDef* def = focusDefault(p);
        double age = ageYears(p);
        double learn = std::max(0.2, 1 + traitMod(p, "skill_gain"));
        // Practice tells most at the start: the closer to mastery, the less a month adds.
        for (const auto& [k, v] : f->skills) {
            int s = skillFromKey(k);
            p.skills[s] = std::clamp(p.skills[s] + v * learn * (1 - p.skills[s] / 20), 0.0, 20.0);
        }
        for (const auto& [k, v] : f->needs) {
            double& n = p.needs[needFromKey(k)];
            n = std::clamp(n + v, 0.0, 100.0);
        }
        // Hands in the family's own fields, against what they would be doing anyway (a servant works another's).
        if (p.home == kNone && def) households_[p.household].focusYield += f->work - def->work;
        if (p.reputation < 30) p.reputation += f->reputation;
        p.stress = std::max(0.0, p.stress + f->stress);
        p.health = std::min(100.0, p.health + f->health);
        // Getting to know people: of about their own age, or anyone in the village.
        for (int k = 0; k < f->knownPeers; ++k)
            for (int tries = 0; tries < 6; ++tries) {
                Id q = village[static_cast<size_t>(rngSocial_.range(0, static_cast<int>(village.size()) - 1))];
                if (q == id || std::abs(ageYears(people_[q]) - age) > 3) continue;
                befriend(id, q, 2);
                break;
            }
        for (int k = 0; k < f->knownVillage; ++k) {
            Id q = village[static_cast<size_t>(rngSocial_.range(0, static_cast<int>(village.size()) - 1))];
            if (q != id) befriend(id, q, 1.5);
        }
        if (f->priestKnown > 0 && priest_ != kNone && people_[priest_].alive()) befriend(id, priest_, f->priestKnown);
        // A child who helps at home: the parents notice, up to a point.
        if (f->opinionParents > 0)
            for (Id par : {p.father, p.mother}) {
                if (par == kNone || !people_[par].alive() || people_[par].household != p.household) continue;
                double now = 0;
                for (const auto& m : memories_[par])
                    if (m.deed == kNone && m.about == id && m.label == "helps_at_home") now = m.value;
                if (now < 12) addOpinion(par, id, "helps_at_home", f->opinionParents, 3);
            }
    }
}

void World::setFocus(Id who, const std::string& id) {
    if (!steersFocus(who)) return;
    Person& p = people_[who];
    for (const FocusDef* f : focusChoices(who))
        if (f->id == id) {
            p.focus = id;
            return;
        }
}

}  // namespace sim
