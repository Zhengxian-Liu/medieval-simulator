// Deeds and memories: what happened, and how each person remembers it.
#include <algorithm>
#include <cmath>

#include "simcore/world.h"

namespace sim {

double World::memoryValue(const Memory& m) const {
    if (m.expiresDay >= 0 && day_ >= m.expiresDay) return 0;
    if (m.halfLife <= 0) return m.value;
    double years = (day_ - m.sinceDay) / double(kDaysPerYear);
    return m.value * std::pow(0.5, years / m.halfLife);
}

void World::addMemory(Id holder, const Memory& m) {
    if (holder == kNone || holder == m.about) return;
    // Grudges fade slower for the vengeful and faster for the forgiving.
    Memory copy = m;
    if (copy.value < 0 && copy.halfLife > 0)
        copy.halfLife *= std::max(0.25, 1.0 + traitMod(people_[holder], "grudge_memory"));
    memories_[holder].push_back(copy);
}

namespace {
// How far one kind of everyday impression can go (design/fun-r5.md C): a run of good evenings is worth so much and
// no more, and so is a run of squabbles; only deeds (recordDeed) reach ±60.
double impressionCap(const std::string& label) {
    if (label == "good_times" || label == "quarrels") return 30;
    return 40;
}
}  // namespace

// Small everyday impressions (chats, quarrels, gifts). Repeats of the same kind add up, less and less as the
// impression nears its cap, and the whole of it fades slowly (half gone in its years) instead of all at once
// on a set day. years 0: it does not fade.
void World::addOpinion(Id from, Id to, const std::string& modId, double value, int years) {
    const double cap = impressionCap(modId);
    const double halfLife = years > 0 ? years : 0;
    for (auto& m : memories_[from])
        if (m.deed == kNone && m.about == to && m.label == modId) {
            double cur = memoryValue(m);
            double add = cur * value > 0 ? value * std::max(0.0, 1 - std::abs(cur) / cap) : value;
            m.value = std::clamp(cur + add, -cap, cap);
            m.sinceDay = day_;
            m.expiresDay = -1;  // (its half-life stays the one it was made with, a grudge's lengthened for the vengeful)
            return;
        }
    Memory m;
    m.about = to;
    m.label = modId;
    m.value = std::clamp(value, -cap, cap);
    m.sinceDay = day_;
    m.halfLife = halfLife;
    addMemory(from, m);
}

Id World::recordDeed(const std::string& type, Id actor, Id target, double scale, Id cause) {
    const DeedDef* def = defs_.deed(type);
    if (!def) throw JsonError("unknown deed type " + type);
    Deed d;
    d.id = static_cast<Id>(deeds_.size());
    d.type = type;
    d.day = day_;
    d.actor = actor;
    d.target = target;
    d.scale = scale;
    // Without an explicit cause, a deed answers the doer's strongest memory of the other family
    // with the same sign: revenge answers an old wrong, help answers an old kindness.
    // Only grudges are answered automatically; friendly deeds are too common to chain.
    double sign = 0;
    for (const auto& [r, v] : def->roles)
        if (r == "target" || r == "target_family") sign = std::min(sign, v);
    if (cause == kNone && target != kNone && sign < 0) {
        double best = 0;
        Id dyn = people_[target].dynasty;
        for (const auto& m : memories_[actor]) {
            if (m.deed == kNone) continue;
            if (m.about != target && (dyn == kNone || m.aboutDynasty != dyn)) continue;
            double v = memoryValue(m);
            if (v * sign > 0 && std::abs(v) > std::abs(best) && std::abs(v) >= 5) {
                best = v;
                cause = m.deed;
            }
        }
    }
    d.cause = cause;
    deeds_.push_back(d);

    // What the deed weighs on each side (the victim feels the loss even without knowing who).
    if (!def->stress.empty()) {
        std::string what = def->name + "的事";
        auto st = [&](const char* r) {
            auto it = def->stress.find(r);
            return it == def->stress.end() ? 0.0 : it->second * scale;
        };
        if (actor != kNone && st("actor") != 0) addStress(actor, st("actor"), what);
        if (target != kNone && st("target") != 0) addStress(target, st("target"), what);
        if (target != kNone && st("target_family") != 0 && people_[target].household != kNone)
            for (Id m : households_[people_[target].household].members)
                if (m != target) addStress(m, st("target_family"), what);
    }
    // A wrong done openly stays on the victim's mind as a grudge against the one who did it (design/season-loop.md).
    if (actor != kNone && target != kNone && actor != target && def->visibility != "secret") {
        auto it = def->roles.find("target");
        if (it != def->roles.end() && it->second * scale <= -12) addConcern(target, "grievance", actor, std::min(80.0, -it->second * scale * 2));
    }

    auto role = [&](const char* r) {
        auto it = def->roles.find(r);
        return it == def->roles.end() ? 0.0 : it->second * scale;
    };
    auto remember = [&](Id holder, Id about, double value) {
        if (value == 0 || holder == kNone || about == kNone || !people_[holder].alive()) return;
        Memory m;
        m.deed = d.id;
        m.about = about;
        // A quarrel inside one family stays between the two people.
        bool sameFamily = people_[holder].dynasty == people_[about].dynasty;
        m.aboutDynasty = def->family && !sameFamily ? people_[about].dynasty : kNone;
        m.label = type;
        m.value = value;
        m.sinceDay = day_;
        m.halfLife = def->halfLife;
        addMemory(holder, m);
    };

    // A secret deed is a fact the victim does not know; only the doer remembers it for now.
    bool secret = def->visibility == "secret";
    if (secret) {
        remember(actor, target, role("actor"));
        knowAtDeed(deeds_.back(), *def);
        emit({"deed", d.id, actor, target});
        return d.id;
    }
    remember(target, actor, role("target"));
    remember(actor, target, role("actor"));
    std::vector<Id> told{actor, target};
    if (target != kNone && people_[target].household != kNone)
        for (Id m : households_[people_[target].household].members)
            if (m != target && m != actor) {
                remember(m, actor, role("target_family"));
                told.push_back(m);
            }
    // Who hears of it, and when, is up to the web of acquaintance (design/news.md); a deed with no news kind
    // is known to the whole village at once.
    std::string newsKind = "deed:" + type;
    bool travels = defs_.newsKind(newsKind) != nullptr;
    if (def->visibility == "public" && role("witness") != 0 && !travels)
        for (const auto& p : people_)
            if (p.alive() && p.household != kNone && ageYears(p) >= 10 &&
                std::find(told.begin(), told.end(), p.id) == told.end())
                remember(p.id, actor, role("witness"));
    // Choices show their weight at once: when the family does something to another house, say how it landed.
    if (actor != kNone && target != kNone && inPlayerHouse(people_[actor].household) && playerHousehold_ != kNone &&
        !inPlayerHouse(people_[target].household) && people_[target].alive()) {
        double v = role("target");
        // Once per house and kind of deed every few years: the first time is news, the fifth is noise.
        auto& last = reacted_[{target, type}];
        bool fresh = last == 0 || day_ - last >= 3 * kDaysPerYear;
        if (std::abs(v) >= 8 && fresh) last = day_;
        if (std::abs(v) >= 8 && fresh)
            log(1, "  → " + fullName(target) + (v < 0 ? " 記下了這筆帳" : " 記下了這份情") + "（「" + def->name + "」，對你家 " +
                       (v > 0 ? "+" : "") + std::to_string(int(std::lround(v))) + "）。");
    }
    feudOnDeed(deeds_.back(), *def, actor);
    if (travels) {
        Id house = actor != kNone && people_[actor].household != kNone ? people_[actor].household
                   : target != kNone ? people_[target].household : kNone;
        addNews(newsKind, actor, target, house, d.id);
    }
    emit({"deed", d.id, actor, target});
    return d.id;
}

void World::inheritMemories(Id dead) {
    const Person& p = people_[dead];
    std::vector<Id> heirs;
    for (Id c : p.children)
        if (people_[c].alive() && people_[c].household != kNone) heirs.push_back(c);
    // The one taking up the house carries its quarrels and debts of gratitude too, child of the dead or not (a niece
    // named heir): the house's story goes on with them (design/fun-r6.md D).
    if (p.household != kNone && households_[p.household].head == dead) {
        Id heir = heirOf(households_[p.household], dead);
        if (heir != kNone && people_[heir].alive() && people_[heir].household != kNone && std::find(heirs.begin(), heirs.end(), heir) == heirs.end())
            heirs.push_back(heir);
    }
    // Strongest grudge and strongest debt of gratitude passed on, for the chronicle.
    double worst = 0, best = 0;
    std::string worstLine, bestLine;
    for (const auto& m : memories_[dead]) {
        const DeedDef* def = defs_.deed(m.label);
        if (!def || !def->inheritable) continue;
        // A secret the dead kept goes to the grave with them; the children never knew to resent or cherish it.
        if (m.deed != kNone && deeds_[m.deed].actor == dead && def->visibility == "secret") continue;
        double v = memoryValue(m);
        if (std::abs(v) < 8) continue;
        // An inherited memory is about the other family, not only the person.
        Id dyn = m.aboutDynasty != kNone ? m.aboutDynasty : people_[m.about].dynasty;
        std::vector<Id> told;  // the player's house's people who take it up
        for (Id h : heirs) {
            if (h == m.about || people_[h].dynasty == dyn) continue;
            // One who has their own memory of the same deed (a widow wronged with her husband) keeps that, not a second.
            if (m.deed != kNone && std::any_of(memories_[h].begin(), memories_[h].end(), [&](const Memory& x) { return x.deed == m.deed; }))
                continue;
            Memory copy = m;
            // A deathbed charge changes how much of the old grudges the heir takes up.
            double share = 0.5;
            if (v < 0 && people_[h].flags.count("father_grudges")) share = 0.8;
            if (v < 0 && people_[h].flags.count("father_counsel")) share = 0.25;
            copy.value = v * share;
            copy.sinceDay = day_;
            copy.aboutDynasty = dyn;
            copy.inherited = true;
            addMemory(h, copy);
            if (inPlayerHouse(people_[h].household)) told.push_back(h);
        }
        if (told.empty() || dyn == kNone || dyn == playerDynasty_) continue;
        // Said by who takes it up: the children, the widow, or the kinsman who has the house.
        bool children = std::all_of(told.begin(), told.end(), [&](Id x) { return people_[x].father == dead || people_[x].mother == dead; });
        std::string who = children ? (p.sex == Sex::Male ? "孩子們記著父親" : "孩子們記著母親")
                          : told.size() == 1 && told[0] == p.spouse ? people_[told[0]].firstName + " 記著" + (p.sex == Sex::Male ? "亡夫" : "亡妻")
                          : told.size() == 1 ? people_[told[0]].firstName + " 記著 " + p.firstName
                                             : "家裡的人記著 " + p.firstName;
        std::string line = who + "和 " + dynasties_[dyn].surname + " 家的" + (v < 0 ? "仇" : "恩") + "（" + def->name + "）。";
        if (v < worst) worst = v, worstLine = line;
        if (v > best) best = v, bestLine = line;
    }
    if (worst <= -12) log(1, worstLine);
    if (best >= 20) log(1, bestLine);
    memories_[dead].clear();
}

void World::pruneOpinions() {
    for (size_t i = 0; i < memories_.size(); ++i) {
        auto& mem = memories_[i];
        if (!people_[i].alive()) {
            mem.clear();
            continue;
        }
        mem.erase(std::remove_if(mem.begin(), mem.end(),
                                 [&](const Memory& m) { return std::abs(memoryValue(m)) < 0.5; }),
                  mem.end());
    }
}

std::vector<std::pair<std::string, double>> World::opinionBreakdown(Id from, Id to) const {
    std::map<std::string, double> sums;
    const Person& b = people_[to];
    for (const auto& m : memories_[from]) {
        double v = 0;
        if (m.about == to) v = memoryValue(m);
        else if (m.aboutDynasty != kNone && m.aboutDynasty == b.dynasty) v = memoryValue(m) * 0.4;
        if (v == 0) continue;
        const DeedDef* def = defs_.deed(m.label);
        std::string label = def ? def->name : m.label;
        // What you did to them yourself: getting even (or making amends) eases how you feel about them.
        if (m.deed != kNone && deeds_[m.deed].actor == from && def) {
            auto harm = def->roles.find("target");
            bool hurtThem = harm != def->roles.end() && harm->second < 0;
            label = (v > 0 && hurtThem ? "出過氣：" : "") + label + "（你做的）";
        }
        if (m.inherited) label += "（上一代）";
        else if (m.about != to) label += "（對他家人）";
        sums[label] += v;
    }
    if (double k = kinBase(from, to); k != 0) sums[kinBaseLabel(from, to)] += k;
    const Person& a = people_[from];
    if (a.lover != kNone && a.lover == b.spouse && people_[b.spouse].alive()) sums["心裡有鬼"] -= kGuilt;  // World::opinion
    std::vector<std::pair<std::string, double>> out(sums.begin(), sums.end());
    std::sort(out.begin(), out.end(), [](auto& x, auto& y) { return std::abs(x.second) > std::abs(y.second); });
    return out;
}

// How b stands to a by blood or marriage, the near ties only ("" for none).
std::string World::kinTie(Id from, Id to) const {
    const Person& a = people_[from];
    const Person& b = people_[to];
    auto sib = [&](const Person& x, const Person& y) {
        return x.id != y.id && ((x.father != kNone && x.father == y.father) || (x.mother != kNone && x.mother == y.mother));
    };
    auto spouseOf = [&](const Person& x) -> const Person* { return x.spouse != kNone ? &people_[x.spouse] : nullptr; };
    if (a.father == to || a.mother == to) return "parent";
    if (b.father == from || b.mother == from) return "child";
    if (a.spouse == to && b.spouse == from) return "spouse";
    if (sib(a, b)) return "sibling";
    // A parent's spouse who is not the parent; a spouse's child who is not one's own.
    for (Id par : {a.father, a.mother})
        if (par != kNone && people_[par].spouse == to) return "step_parent";
    if (const Person* s = spouseOf(a); s && (b.father == s->id || b.mother == s->id)) return "step_child";
    if (const Person* s = spouseOf(b); s && sib(a, *s)) return "sibling_in_law";  // a brother's wife, a sister's husband
    if (const Person* s = spouseOf(a); s && sib(*s, b)) return "sibling_in_law";  // a husband's brother, a wife's sister
    if (const Person* s = spouseOf(a); s && (s->father == to || s->mother == to)) return "parent_in_law";
    if (const Person* s = spouseOf(b); s && (s->father == from || s->mother == from)) return "child_in_law";
    for (Id par : {a.father, a.mother})
        if (par != kNone && sib(people_[par], b)) return "uncle";
    for (Id par : {b.father, b.mother})
        if (par != kNone && sib(people_[par], a)) return "nephew";
    for (Id par : {b.father, b.mother})
        if (par != kNone && (people_[par].father == from || people_[par].mother == from)) return "grandchild";
    for (Id par : {a.father, a.mother})
        if (par != kNone && (people_[par].father == to || people_[par].mother == to)) return "grandparent";
    return "";
}

// Blood and marriage: what is felt before anything is done.
double World::kinBase(Id from, Id to) const {
    const std::string t = kinTie(from, to);
    if (t == "parent" || t == "child") return 25;
    if (t == "spouse") return people_[to].spouse == from ? 20 : 0;
    if (t == "sibling") return 10;
    if (t == "grandparent" || t == "grandchild") return 12;
    if (t == "step_parent" || t == "step_child" || t == "uncle" || t == "nephew" || t == "parent_in_law" || t == "child_in_law") return 6;
    if (t == "sibling_in_law") return 4;
    return 0;
}

std::string World::kinBaseLabel(Id from, Id to) const {
    const std::string t = kinTie(from, to);
    if (t == "parent" || t == "child" || t == "grandparent" || t == "grandchild") return "骨肉之親";
    if (t == "spouse") return "夫妻情分";
    if (t == "sibling") return "手足之情";
    return "一家親戚";
}

}  // namespace sim
