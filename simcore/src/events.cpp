#include "simcore/events.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

bool inRange(double v, const Json& r) {
    if (r.isNumber()) return v == r.asNumber();
    return v >= r[0].asNumber() && v <= r[1].asNumber();
}

}  // namespace

void EventRunner::pulse(const std::string& kind) {
    std::vector<Id> ids = w_.livingInVillage();
    Id playerHead = w_.playerHousehold_ != kNone ? w_.households_[w_.playerHousehold_].head : kNone;

    for (const auto& ev : w_.defs_.events) {
        if (ev.pulse != kind) continue;
        for (Id id : ids) {
            Person& p = w_.people_[id];
            if (!eligible(ev, p, playerHead)) continue;
            if (!eval(ev.conditions, p, kNone)) continue;
            if (!w_.rngEvents_.chance(ev.chance)) continue;

            Id target = kNone;
            if (!ev.target.isNull()) {
                target = pickTarget(ev, p);
                if (target == kNone) continue;
            }
            fire(ev, p, target);
            if (w_.gameOver_) return;
        }
    }
}

bool EventRunner::eligible(const EventDef& ev, const Person& p, Id playerHead) const {
    if (!p.alive() || p.household == kNone) return false;
    if (ev.scope == "player_family" && !w_.isPlayerFamily(p)) return false;
    if (ev.scope == "player_head" && p.id != playerHead) return false;
    if (ev.once && p.flags.count("ev_" + ev.id)) return false;
    if (!ev.budget.empty()) {
        auto it = w_.budgetUsed_.find({p.household, ev.budget, w_.year()});
        if (it != w_.budgetUsed_.end() && it->second >= 2) return false;
    }
    auto cd = w_.cooldowns_.find({p.id, ev.id});
    return cd == w_.cooldowns_.end() || cd->second <= w_.day_;
}

// A signal-driven event whose day has come: the target and the deed come from the signal.
void EventRunner::fireScheduled(const EventDef& ev, Person& p, Id target, Id deed) {
    Id playerHead = w_.playerHousehold_ != kNone ? w_.households_[w_.playerHousehold_].head : kNone;
    if (!eligible(ev, p, playerHead)) return;
    // A mourner's moment is about the dead; every other target has to be still about.
    std::string sig = ev.on.stringOr("signal", "");
    bool aboutDead = sig.rfind("bereaved:", 0) == 0 || sig == "news:funeral";
    if (target != kNone && !aboutDead && (!w_.people_[target].alive() || w_.people_[target].household == kNone)) return;
    if (target == kNone && ev.target["pick"].isString()) {
        target = pickTarget(ev, p);
        if (target == kNone) return;
    } else if (!ev.target["conditions"].isNull() &&
               (target == kNone || !eval(ev.target["conditions"], w_.people_[target], p.id))) {
        return;
    }
    deed_ = deed;
    cause_ = deed;
    if (!eval(ev.conditions, p, target)) return;
    fire(ev, p, target);
}

// A marriage that could end a feud: an unmarried child of p's and someone unmarried in the target's house.
std::pair<Id, Id> EventRunner::peaceMatch(const Person& p, Id target) const {
    if (target == kNone || w_.people_[target].household == kNone || p.household == kNone) return {kNone, kNone};
    auto single = [&](const Person& x, double lo, double hi) {
        double a = w_.ageYears(x);
        return x.alive() && x.household != kNone && a >= lo && a <= hi && x.occupation != "priest" &&
               (x.spouse == kNone || !w_.people_[x.spouse].alive()) && x.home == kNone &&
               w_.households_[x.household].head != x.id;
    };
    for (Id c : p.children) {
        const Person& cp = w_.people_[c];
        if (!single(cp, 15, 28)) continue;
        for (Id o : w_.households_[w_.people_[target].household].members) {
            const Person& op = w_.people_[o];
            if (op.sex == cp.sex || !single(op, 15, 32) || w_.closeKin(c, o) || w_.gentry(c) != w_.gentry(o)) continue;
            return {c, o};
        }
    }
    return {kNone, kNone};
}

// The child a household would send out to work off a debt: the eldest at home who is free to go.
Id EventRunner::serviceChild(const Household& h) const {
    Id best = kNone;
    for (Id m : h.members) {
        const Person& c = w_.people_[m];
        double age = w_.ageYears(c);
        if (m == h.head || age < 10 || age > 18 || c.home != kNone) continue;
        if (c.spouse != kNone && w_.people_[c.spouse].alive()) continue;
        if (best == kNone || c.birthDay < w_.people_[best].birthDay) best = m;
    }
    return best;
}

const std::string& EventRunner::pickLog(const OptionDef& o) {
    if (o.logs.size() < 2) return o.log;
    return o.logs[w_.rngEvents_.range(0, static_cast<int>(o.logs.size()) - 1)];
}

// Choosing against one's nature weighs on a person; choosing true to it eases them a little.
// An option is "against" a trait when another option suited that trait clearly better.
void EventRunner::natureStress(const EventDef& ev, const OptionDef& chosen, Person& p, Id target) {
    if (ev.options.size() < 2) return;
    if (ev.on.isObject() && ev.on.stringOr("signal", "") == "stress") return;  // breaking down is not itself a choice
    auto aff = [](const OptionDef& o, const std::string& t) {
        const Json& tr = o.aiWeight["traits"];
        return tr.isObject() ? tr.numberOr(t, 0) : 0.0;
    };
    for (const auto& t : p.traits) {
        const TraitDef& td = w_.defs_.traits.at(t);
        if (td.category != "personality") continue;
        double best = -1e9;
        for (const auto& o : ev.options)
            if (optionOpen(o, p, target)) best = std::max(best, aff(o, t));
        double mine = aff(chosen, t);
        if (best - mine >= 8) {
            // Name the moment, not just the feeling: "仇家作梗那回一口嚥不下去的氣".
            std::string cause = ev.title + "那回" + (td.against.empty() ? "違心的事" : td.against);
            w_.addStress(p.id, std::min(25.0, (best - mine) * 1.5), cause);
        } else if (mine >= 8 && mine >= best) {
            w_.addStress(p.id, -4, "");
        }
    }
}

void EventRunner::fire(const EventDef& ev, Person& p, Id target) {
    const OptionDef& o = ev.options[chooseOption(ev, p, target)];
    natureStress(ev, o, p, target);
    if (ev.once) p.flags.insert("ev_" + ev.id);
    if (!ev.budget.empty()) ++w_.budgetUsed_[{p.household, ev.budget, w_.year()}];
    if (ev.cooldownYears > 0) {
        w_.cooldowns_[{p.id, ev.id}] = w_.day_ + ev.cooldownYears * kDaysPerYear;
        if (ev.householdCooldown && p.household != kNone)
            for (Id m : w_.households_[p.household].members) w_.cooldowns_[{m, ev.id}] = w_.day_ + ev.cooldownYears * kDaysPerYear;
    }

    bool home = p.household == w_.playerHousehold_ || p.home == w_.playerHousehold_ ||
                (target != kNone && w_.people_[target].household == w_.playerHousehold_);
    bool family = w_.isPlayerFamily(p) || (target != kNone && w_.isPlayerFamily(w_.people_[target]));
    ++w_.counters_["event:" + ev.id];
    if (family) ++w_.counters_["event_family:" + ev.id];
    std::string line = o.log.empty() ? "" : format(pickLog(o), p, target);
    if (!line.empty()) {
        if (family) {
            std::string choice = ev.options.size() > 1 && w_.isPlayerFamily(p) ? "（選擇：" + format(o.text, p, target) + "）" : "";
            w_.log(home ? 1 : 2, "〔" + ev.title + "〕" + line + choice);
        } else if (ev.npcLog) {
            w_.log(2, line);
        }
    }
    if (!w_.pendingReason_.empty()) {
        if (family && !line.empty()) w_.log(home ? 1 : 2, "  （" + w_.pendingReason_ + "）");
        w_.pendingReason_.clear();
    }
    apply(o.effects, p, target);
}

void EventRunner::runEvent(const EventDef& ev, Person& p, Id target, int importance) {
    const OptionDef& o = ev.options[chooseOption(ev, p, target)];
    natureStress(ev, o, p, target);
    std::string line = o.log.empty() ? "" : format(pickLog(o), p, target);
    if (!line.empty() && importance > 0) {
        bool chose = ev.options.size() > 1 && p.household == w_.playerHousehold_;
        w_.log(importance, "  " + line + (chose ? "（選擇：" + format(o.text, p, target) + "）" : ""));
    }
    if (!w_.pendingReason_.empty()) {
        if (!line.empty() && importance > 0) w_.log(importance, "  （" + w_.pendingReason_ + "）");
        w_.pendingReason_.clear();
    }
    apply(o.effects, p, target);
}

void EventRunner::interact(const EventDef& ev, Person& p, Id head) {
    if (!p.alive() || p.household == kNone || head == kNone) return;
    if (!eval(ev.conditions, p, head)) return;
    auto cd = w_.cooldowns_.find({p.id, ev.id});
    if (cd != w_.cooldowns_.end() && cd->second > w_.day_) return;
    double money = -ev.interaction.numberOr("money", 0);
    Household& hh = w_.households_[w_.people_[head].household];
    if (money > hh.money + 0.5) return;
    if (!w_.spendTime(static_cast<int>(ev.interaction.numberOr("time", 1)))) return;
    hh.money = std::max(0.0, hh.money - money);
    w_.book(hh, ev.title, -money);
    // How they take it is up to them: their nature, their mood, what they feel for the head.
    const OptionDef& o = ev.options[chooseOption(ev, p, head)];
    natureStress(ev, o, p, head);
    if (ev.cooldownMonths > 0) w_.cooldowns_[{p.id, ev.id}] = w_.day_ + ev.cooldownMonths * kDaysPerMonth;
    ++w_.counters_["interaction:" + ev.id];
    std::string line = o.log.empty() ? "" : format(pickLog(o), p, head);
    if (!line.empty()) w_.log(1, "〔" + ev.title + "〕" + line);
    if (!w_.pendingReason_.empty()) {
        if (!line.empty()) w_.log(1, "  （" + w_.pendingReason_ + "）");
        w_.pendingReason_.clear();
    }
    apply(o.effects, p, head);
    p.soughtDay = w_.day_;
    if (p.household != w_.playerHousehold_) w_.befriend(p.id, head, 8);  // seeking someone out is how you come to know them
    // The summary says what came of it: what they were waiting for, if this was it, or how they took it.
    if (!w_.answerWant(p.id, ev.id))
        w_.seasonNotes_.push_back("你找" + w_.shortName(p.id) + "（" + ev.title + "）：" + format(o.text, p, head) + "。");
}

Id EventRunner::pickTarget(const EventDef& ev, const Person& p) {
    std::string pick = ev.target.stringOr("pick", "random_villager");
    const Json& cond = ev.target["conditions"];
    auto ok = [&](Id t) {
        if (t == kNone || t == p.id) return false;
        const Person& tp = w_.people_[t];
        if (!tp.alive() || tp.household == kNone) return false;
        return cond.isNull() || eval(cond, tp, p.id);
    };
    auto first = [&](Id t) { return ok(t) ? t : kNone; };

    if (pick == "spouse") return first(p.spouse);
    if (pick == "father") return first(p.father);
    if (pick == "mother") return first(p.mother);
    if (pick == "lord") return first(w_.lord_);
    if (pick == "priest") return first(w_.priest_);
    if (pick == "head") return first(w_.households_[p.household].head);
    if (pick == "reeve") {
        for (const auto& r : w_.people_)
            if (r.alive() && r.flags.count("reeve")) return first(r.id);
        return kNone;
    }

    std::vector<Id> pool;
    if (pick == "eldest_child" || pick == "youngest_child" || pick == "any_child") {
        for (Id c : p.children)
            if (ok(c)) pool.push_back(c);
        if (pool.empty()) return kNone;
        if (pick == "eldest_child") return pool.front();
        if (pick == "youngest_child") return pool.back();
    } else {
        for (const auto& t : w_.people_) {
            if (!ok(t.id) || t.household == p.household) continue;
            if (pick == "friend" && w_.opinion(p.id, t.id) < 40) continue;
            if (pick == "rival" && w_.opinion(p.id, t.id) > -30) continue;
            if (pick == "remembered" && !remembers(ev.target["remembers"], p, t.id)) continue;
            if (pick == "feud" && w_.feudStage(p.id, t.id) < ev.target.numberOr("min_stage", 1)) continue;
            pool.push_back(t.id);
        }
        if (pool.empty()) return kNone;
    }
    return pool[w_.rngEvents_.range(0, static_cast<int>(pool.size()) - 1)];
}

// {"holder": "self"|"target", "about": "other"|"other_family"|"anyone", "type": "...", "tag": "...",
//  "inherited": bool, "negative": bool, "within_years": n, "min": |value|}
bool EventRunner::remembers(const Json& v, const Person& p, Id target) const {
    const World& w = w_;
    bool targetHolds = v.stringOr("holder", "self") == "target";
    Id holder = targetHolds ? target : p.id;
    Id other = targetHolds ? p.id : target;
    if (holder == kNone) return false;
    std::string about = v.stringOr("about", other == kNone ? "anyone" : "other");
    if (about != "anyone" && other == kNone) return false;
    Id otherDyn = other != kNone ? w.people_[other].dynasty : kNone;
    if (about == "other_family" && otherDyn == w.people_[holder].dynasty) return false;
    std::string type = v.stringOr("type", ""), tag = v.stringOr("tag", "");
    double minV = v.numberOr("min", 3);
    for (const auto& m : w.memories_[holder]) {
        if (m.deed == kNone && type.empty() && tag.empty()) continue;  // everyday impressions don't count
        if (!type.empty() && m.label != type) continue;
        if (!tag.empty()) {
            const DeedDef* d = w.defs_.deed(m.label);
            if (!d || std::find(d->tags.begin(), d->tags.end(), tag) == d->tags.end()) continue;
        }
        if (about == "other" && m.about != other) continue;
        if (about == "other_family" && m.about != other &&
            (otherDyn == kNone || (m.aboutDynasty != otherDyn && w.people_[m.about].dynasty != otherDyn)))
            continue;
        if (!v["inherited"].isNull() && m.inherited != v["inherited"].asBool()) continue;
        double val = w.memoryValue(m);
        if (!v["negative"].isNull() && (val < 0) != v["negative"].asBool()) continue;
        if (std::abs(val) < minV) continue;
        if (!v["within_years"].isNull() && w.day_ - m.sinceDay > v["within_years"].asNumber() * kDaysPerYear)
            continue;
        return true;
    }
    return false;
}

double EventRunner::weight(const Json& ai, const Person& p, Id target) const {
    if (ai.isNull()) return 10;
    double w = ai.numberOr("base", 10);
    if (ai["traits"].isObject())
        for (const auto& [t, v] : ai["traits"].asObject())
            if (w_.hasTrait(p, t)) w += v.asNumber();
    if (ai["if"].isArray())
        for (const auto& c : ai["if"].asArray())
            if (eval(c["cond"], p, target)) w += c["add"].asNumber();
    return std::max(0.0, w);
}

bool EventRunner::optionOpen(const OptionDef& o, const Person& p, Id target) const {
    if (!o.conditions.isNull() && !eval(o.conditions, p, target)) return false;
    // Only an option with a stated price ("（40 便士）") needs the coin in hand; tithes, fines and
    // the like are paid in kind or owed.
    if (p.household == kNone || !o.effects.isArray() || o.text.find("便士") == std::string::npos) return true;
    double cost = 0;
    for (const auto& e : o.effects.asArray())
        if (e.isObject() && e["money"].isNumber() && e["money"].asNumber() < 0) cost -= e["money"].asNumber();
    return cost <= w_.households_[p.household].money + 0.5;
}

int EventRunner::chooseOption(const EventDef& ev, const Person& p, Id target) {
    std::vector<double> ws;
    for (const auto& o : ev.options) ws.push_back(optionOpen(o, p, target) ? weight(o.aiWeight, p, target) : 0.0);
    // Nothing affordable: fall back to the conditions alone, so the event still resolves.
    if (std::all_of(ws.begin(), ws.end(), [](double v) { return v <= 0; })) {
        ws.clear();
        for (const auto& o : ev.options)
            ws.push_back(o.conditions.isNull() || eval(o.conditions, p, target) ? weight(o.aiWeight, p, target) : 0.0);
    }
    // The one keeping the house decides for themselves and for the young children in it.
    Id acting = w_.actingHead();
    bool child = p.household == w_.playerHousehold_ && acting != kNone &&
                 (p.father == acting || p.mother == acting || p.father == w_.people_[acting].spouse || p.mother == w_.people_[acting].spouse);
    // Someone the head goes to see answers for themselves, child or not.
    bool asks = ev.scope != "interaction" && (p.id == acting || (p.household == w_.playerHousehold_ && w_.ageYears(p) < 14) ||
                (ev.parentDecides && child && p.spouse == kNone));
    if (w_.chooser_ && ev.ask && asks) {
        std::vector<int> open;
        World::ChoicePrompt prompt;
        std::vector<std::string> lines;
        prompt.title = ev.title;
        for (size_t k = 0; k < ev.options.size(); ++k) {
            const OptionDef& o = ev.options[k];
            if (ws[k] <= 0 && !optionOpen(o, p, target)) {
                if (o.conditions.isNull() || eval(o.conditions, p, target))
                    prompt.unavailable.push_back(format(o.text, p, target) + "（錢不夠）");
                continue;
            }
            open.push_back(static_cast<int>(k));
            prompt.options.push_back(format(o.text, p, target));
            lines.push_back(format(o.logs.empty() ? o.log : o.logs[0], p, target));
        }
        if (!ev.prompt.empty()) {
            prompt.situation = format(ev.prompt, p, target);
        } else {
            // The situation: what every option's line agrees on, up to the last full stop or comma in it,
            // is what is happening before the choice is made.
            std::string common = lines.empty() ? "" : lines[0];
            for (const auto& l : lines) {
                size_t n = 0;
                while (n < common.size() && n < l.size() && common[n] == l[n]) ++n;
                common.resize(n);
            }
            size_t cut = std::string::npos;
            for (const char* mark : {"。", "，", "："}) {
                size_t at = common.rfind(mark);
                if (at != std::string::npos && (cut == std::string::npos || at > cut)) cut = at;
            }
            prompt.situation = cut != std::string::npos && cut >= 9 ? common.substr(0, cut) + "……" : "";
        }
        if (open.size() > 1 || (!open.empty() && !prompt.unavailable.empty())) {
            int r = w_.chooser_(prompt);
            if (r >= 0 && r < static_cast<int>(open.size())) return open[r];
        }
    }
    // A villager in scope (by default, the spotlight) may have a model decide among the options the rules allow.
    if (!asks && ev.ask && w_.npcInScope(p.id)) {
        std::vector<int> open;
        std::vector<std::string> texts;
        std::vector<double> weights;
        for (size_t k = 0; k < ev.options.size(); ++k)
            if (ws[k] > 0) {
                open.push_back(static_cast<int>(k));
                texts.push_back(format(ev.options[k].text, p, target));
                weights.push_back(ws[k]);
            }
        if (open.size() > 1) {
            std::string sit = ev.prompt.empty() ? ev.title : format(ev.prompt, p, target);
            int r = w_.npcDecide(p, ev.title, sit, texts, weights, {target});
            if (r >= 0) return open[r];
        }
    }
    int i = w_.rngEvents_.weighted(ws);
    return i < 0 ? 0 : i;
}

bool EventRunner::eval(const Json& cond, const Person& p, Id target) const {
    if (cond.isNull()) return true;
    if (cond.isArray()) {
        for (const auto& c : cond.asArray())
            if (!eval(c, p, target)) return false;
        return true;
    }
    for (const auto& [k, v] : cond.asObject())
        if (!evalKey(k, v, p, target)) return false;
    return true;
}

bool EventRunner::evalKey(const std::string& k, const Json& v, const Person& p, Id target) const {
    const World& w = w_;
    const Household* h = p.household != kNone ? &w.households_[p.household] : nullptr;
    auto spouseAlive = [&] { return p.spouse != kNone && w.people_[p.spouse].alive(); };

    if (k == "all") return eval(v, p, target);
    if (k == "any") {
        for (const auto& c : v.asArray())
            if (eval(c, p, target)) return true;
        return false;
    }
    if (k == "not") return !eval(v, p, target);
    if (k == "age") return inRange(w.ageYears(p), v);
    if (k == "sex") return (v.asString() == "male") == (p.sex == Sex::Male);
    if (k == "trait") return w.hasTrait(p, v.asString());
    if (k == "no_trait") return !w.hasTrait(p, v.asString());
    if (k == "married") return spouseAlive() == v.asBool();
    if (k == "widowed") return (p.spouse != kNone && !spouseAlive()) == v.asBool();
    if (k == "recently_widowed")
        return (p.spouse != kNone && !spouseAlive() && w.people_[p.spouse].deathDay >= 0 &&
                w.day_ - w.people_[p.spouse].deathDay < 2 * kDaysPerYear) == v.asBool();
    if (k == "is_head") return (h && h->head == p.id) == v.asBool();
    if (k == "player_family") return w.isPlayerFamily(p) == v.asBool();
    if (k == "occupation") {
        if (v.isString()) return p.occupation == v.asString();
        for (const auto& o : v.asArray())
            if (p.occupation == o.asString()) return true;
        return false;
    }
    if (k == "month") return inRange(w.month(), v);
    if (k == "winter") return w.isWinter() == v.asBool();
    if (k == "year") return inRange(w.year(), v);
    if (k == "money_ge") return h && h->money >= v.asNumber();
    if (k == "money_lt") return h && h->money < v.asNumber();
    if (k == "food_days_lt") return h && h->food < w.consumption(*h) * v.asNumber();
    if (k == "acres_ge") return h && h->acres >= v.asNumber();
    if (k == "acres_lt") return h && h->acres < v.asNumber();
    if (k == "house") return h && h->house == v.asString();
    if (k == "skill_ge") {
        for (const auto& [s, val] : v.asObject())
            if (p.skills[skillFromKey(s)] < val.asNumber()) return false;
        return true;
    }
    if (k == "skill_lt") {
        for (const auto& [s, val] : v.asObject())
            if (p.skills[skillFromKey(s)] >= val.asNumber()) return false;
        return true;
    }
    if (k == "need_lt") {
        for (const auto& [n, val] : v.asObject())
            if (p.needs[needFromKey(n)] >= val.asNumber()) return false;
        return true;
    }
    if (k == "flag") return p.flags.count(v.asString()) > 0;
    if (k == "no_flag") return p.flags.count(v.asString()) == 0;
    if (k == "world_flag") return w.worldFlags_.count(v.asString()) > 0;
    if (k == "has_children") return (!p.children.empty()) == v.asBool();
    if (k == "living_children_ge") {
        int n = 0;
        for (Id c : p.children) n += w.people_[c].alive() ? 1 : 0;
        return n >= v.asNumber();
    }
    if (k == "health_lt") return p.health < v.asNumber();
    if (k == "health_ge") return p.health >= v.asNumber();
    // What is on someone's mind: "grief", or {"kind": "grief", "about": "target", "min": 20}.
    auto hasConcern = [&](const Person& who, const Json& c, Id other) {
        std::string kind = c.isString() ? c.asString() : c.stringOr("kind", "");
        std::string about = c.isObject() ? c.stringOr("about", "any") : "any";
        double min = c.isObject() ? c.numberOr("min", 5) : 5;
        for (const auto& x : who.concerns)
            if (x.kind == kind && x.strength >= min && (about == "any" || x.about == other)) return true;
        return false;
    };
    if (k == "concern") return hasConcern(p, v, target);
    if (k == "no_concern") return !hasConcern(p, v, target);
    if (k == "target_concern") return target != kNone && hasConcern(w.people_[target], v, p.id);
    if (k == "child_of_target") return target != kNone && (p.father == target || p.mother == target) == v.asBool();
    if (k == "parent_of_target") return target != kNone && (w.people_[target].father == p.id || w.people_[target].mother == p.id) == v.asBool();
    if (k == "same_household") return target != kNone && (p.household == w.people_[target].household) == v.asBool();
    if (k == "opinion_from_target_ge") return target != kNone && w.opinion(target, p.id) >= v.asNumber();
    if (k == "opinion_from_target_lt") return target != kNone && w.opinion(target, p.id) < v.asNumber();
    if (k == "target_mood_lt") return target != kNone && w.mood(w.people_[target]) < v.asNumber();
    if (k == "mood_lt") return w.mood(p) < v.asNumber();
    // The head has not sought this person out for so many months (or ever, since the game began).
    if (k == "news_link_ge")  // how close the family's house is to the target's (design/news.md)
        return target != kNone && w.houseLink(p.household, w.people_[target].household) >= v.asNumber();
    if (k == "news_link_lt") return target != kNone && w.houseLink(p.household, w.people_[target].household) < v.asNumber();
    if (k == "not_sought_months") return w.day_ - std::max(p.soughtDay, 0) >= v.asNumber() * kDaysPerMonth;
    if (k == "mood_ge") return w.mood(p) >= v.asNumber();
    if (k == "reputation_ge") return p.reputation >= v.asNumber();
    if (k == "reputation_lt") return p.reputation < v.asNumber();
    if (k == "prestige_ge") return p.dynasty != kNone && w.dynasties_[p.dynasty].prestige >= v.asNumber();
    if (k == "tradition") {
        if (p.dynasty == kNone) return false;
        const auto& tr = w.dynasties_[p.dynasty].traditions;
        return std::find(tr.begin(), tr.end(), v.asString()) != tr.end();
    }
    if (k == "has_item") {
        if (!h) return false;
        for (Id it : h->items)
            if (w.items_[it].type == v.asString()) return true;
        return false;
    }
    if (k == "items_ge") {
        int n = 0;
        if (h)
            for (Id it : h->items) n += w.items_[it].lost ? 0 : 1;
        return n >= v.asNumber();
    }
    if (k == "opinion_of_target_ge") return target != kNone && w.opinion(p.id, target) >= v.asNumber();
    if (k == "opinion_of_target_lt") return target != kNone && w.opinion(p.id, target) < v.asNumber();
    if (k == "target") return target != kNone && eval(v, w.people_[target], p.id);
    if (k == "spouse") return spouseAlive() && eval(v, w.people_[p.spouse], p.id);
    if (k == "any_child") {
        for (Id c : p.children)
            if (w.people_[c].alive() && w.people_[c].household != kNone && eval(v, w.people_[c], p.id)) return true;
        return false;
    }
    if (k == "ambition") return p.ambition == v.asString();
    if (k == "reeve") return (p.flags.count("reeve") > 0) == v.asBool();
    if (k == "opposite_sex") return target != kNone && (w.people_[target].sex != p.sex) == v.asBool();
    if (k == "not_kin") return target != kNone && (!w.closeKin(p.id, target)) == v.asBool();
    if (k == "gentle") return (w.cfg_.harshness == Harshness::Gentle) == v.asBool();
    // Someone of the family, other than the head and spouse, lives at home to inherit.
    if (k == "heir_at_home") {
        bool any = false;
        if (h)
            for (Id m : h->members)
                if (m != h->head && m != w.people_[h->head].spouse && w.people_[m].dynasty == p.dynasty && w.people_[m].home == kNone)
                    any = true;
        return any == v.asBool();
    }
    if (k == "deed_noted") return deed_ != kNone && w.deedNotes_.count({deed_, v.asString()}) > 0;
    if (k == "heir_apparent") return (h && h->head != kNone && h->head != p.id && w.heirOf(*h, h->head) == p.id) == v.asBool();
    if (k == "has_lover") return (p.lover != kNone) == v.asBool();
    if (k == "is_lover") {
        bool lovers = target != kNone && p.lover == target;
        if (target != kNone)
            for (const auto& m : w.memories_[p.id])
                if (m.about == target && m.label == "lovers") lovers = true;
        return lovers == v.asBool();
    }
    if (k == "is_spouse") return (target != kNone && p.spouse == target) == v.asBool();
    if (k == "has_hook") return w.hasHook(p.id) == v.asBool();
    // On a target: the other party (the actor) holds a secret over this person.
    if (k == "hooked") return (w.hookDeed(target, p.id) != kNone) == v.asBool();
    // With a target: the secret p holds over the target is true.
    if (k == "target_guilty") {
        Id d = w.hookDeed(p.id, target);
        return (d != kNone && w.deeds_[d].actor == target) == v.asBool();
    }
    // In an event about a deed: p really did it.
    if (k == "deed_actor") return (deed_ != kNone && w.deeds_[deed_].actor == p.id) == v.asBool();
    if (k == "in_debt") return (h && h->debt > 0.5) == v.asBool();
    if (k == "debt_ge") return h && h->debt >= v.asNumber();
    if (k == "has_lender") return h && (w.findLender(*h) != kNone) == v.asBool();
    if (k == "feud_truce") {
        const Feud* f = target != kNone ? w.feudBetween(p.dynasty, w.people_[target].dynasty) : nullptr;
        return (f && f->truceUntil > w.day_) == v.asBool();
    }
    if (k == "feud_ge") return w.feudStage(p.id, target) >= v.asNumber();
    if (k == "feud_lt") return w.feudStage(p.id, target) < v.asNumber();
    if (k == "peace_match") return (peaceMatch(p, target).first != kNone) == v.asBool();
    if (k == "lord_opinion_lt")
        return w.lord_ != kNone && w.people_[w.lord_].alive() && w.lord_ != p.id && w.opinion(w.lord_, p.id) < v.asNumber();
    if (k == "lean") return (h && h->lean) == v.asBool();
    if (k == "lodger") return (h && h->lodgerUntil > w.day_) == v.asBool();
    if (k == "no_ox") return (h && h->noOxUntil > w.day_) == v.asBool();
    if (k == "service_child") return h && (serviceChild(*h) != kNone) == v.asBool();
    if (k == "stress_ge") return p.stress >= v.asNumber();
    if (k == "stress_lt") return p.stress < v.asNumber();
    if (k == "same_class") return target != kNone && (w.gentry(p.id) == w.gentry(target)) == v.asBool();
    if (k == "other_dynasty")
        return target != kNone && (p.dynasty == kNone || p.dynasty != w.people_[target].dynasty) == v.asBool();
    if (k == "sworn_friends_lt") return w.swornFriends(p.id) < v.asNumber();
    if (k == "has_plan") return (w.activePlan(p.id) != nullptr) == v.asBool();
    if (k == "match_pending") {
        // Someone is already arranging this person's marriage.
        bool pending = false;
        for (const auto& ap : w.plans_)
            if ((ap.subject == p.id || ap.target == p.id) && ap.plan == "arrange_match") pending = true;
        return pending == v.asBool();
    }
    if (k == "vacant_acres_ge") return w.vacantAcres_ >= v.asNumber();
    if (k == "plague_active") return (w.param("plague_monthly", 0) > 0) == v.asBool();
    if (k == "household_size_ge") return h && h->members.size() >= v.asNumber();
    if (k == "remembers") return remembers(v, p, target);
    if (k == "free") return p.free == v.asBool();
    if (k == "deed_tried") return deed_ != kNone && w.deedTried(deed_) == v.asBool();
    if (k == "unsolved_secret") return (w.unsolvedDeed(p.id) != kNone) == v.asBool();
    if (k == "in_service") return (p.home != kNone) == v.asBool();
    // The family ladder.
    if (k == "standing") return h && w.standing(h->id) == static_cast<int>(v.asNumber());
    if (k == "standing_ge") return h && w.standing(h->id) >= v.asNumber();
    if (k == "standing_lt") return h && w.standing(h->id) >= 0 && w.standing(h->id) < v.asNumber();
    if (k == "freehold_ge") return h && h->freehold >= v.asNumber();
    if (k == "customary_ge") return h && h->acres - h->freehold >= v.asNumber();
    if (k == "kin_in_church") return w.kinInChurch(p.dynasty) == v.asBool();
    if (k == "kin_in_town") return w.kinInTown(p.dynasty) == v.asBool();
    if (k == "lord_opinion_ge")
        return w.lord_ != kNone && w.people_[w.lord_].alive() && w.lord_ != p.id && w.opinion(w.lord_, p.id) >= v.asNumber();
    if (k == "feud_any_ge") {
        bool any = false;
        if (p.dynasty != kNone)
            for (const auto& f : w.feuds_)
                if ((f.a == p.dynasty || f.b == p.dynasty) && f.stage >= v.asNumber()) any = true;
        return any;
    }
    throw JsonError("unknown condition key: " + k);
}

void EventRunner::apply(const Json& effects, Person& p, Id target) {
    if (!effects.isArray()) return;
    World& w = w_;
    for (const auto& e : effects.asArray()) {
        for (const auto& [k, v] : e.asObject()) {
            Household* h = p.household != kNone ? &w.households_[p.household] : nullptr;
            if (k == "add_trait") {
                const TraitDef* td = w.defs_.trait(v.asString());
                if (!td) throw JsonError("unknown trait " + v.asString());
                if (!w.hasTrait(p, td->id)) {
                    if (!td->opposite.empty())
                        p.traits.erase(std::remove(p.traits.begin(), p.traits.end(), td->opposite), p.traits.end());
                    p.traits.push_back(td->id);
                }
            } else if (k == "remove_trait") {
                p.traits.erase(std::remove(p.traits.begin(), p.traits.end(), v.asString()), p.traits.end());
            } else if (k == "trait_chance") {
                if (w.rngEvents_.chance(v["chance"].asNumber() / 100.0)) {
                    Json one = Json::parse("[{\"add_trait\":\"" + v["trait"].asString() + "\"}]");
                    apply(one, p, target);
                }
            } else if (k == "money") {
                if (h) {
                    double before = h->money;
                    h->money = std::max(0.0, h->money + v.asNumber());
                    w.book(*h, "各種事情", h->money - before);
                }
            } else if (k == "food") {
                if (h) h->food = std::max(0.0, h->food + v.asNumber());
            } else if (k == "firewood") {
                if (h) h->firewood = std::max(0.0, h->firewood + v.asNumber());
            } else if (k == "health") {
                p.health = std::clamp(p.health + v.asNumber(), 0.0, 100.0);
            } else if (k == "reputation") {
                p.reputation += v.asNumber();
            } else if (k == "prestige") {
                if (p.dynasty != kNone) w.dynasties_[p.dynasty].prestige += v.asNumber();
            } else if (k == "skill") {
                for (const auto& [s, d] : v.asObject()) {
                    double& sk = p.skills[skillFromKey(s)];
                    sk = std::clamp(sk + d.asNumber(), 0.0, 20.0);
                }
            } else if (k == "need") {
                for (const auto& [n, d] : v.asObject()) {
                    double& nd = p.needs[needFromKey(n)];
                    nd = std::clamp(nd + d.asNumber(), 0.0, 100.0);
                }
            } else if (k == "set_flag") {
                p.flags.insert(v.asString());
            } else if (k == "clear_flag") {
                p.flags.erase(v.asString());
            } else if (k == "opinion" || k == "mutual_opinion" || k == "target_opinion") {
                if (target == kNone) continue;
                std::string mod = v.stringOr("mod", "event");
                double val = v["value"].asNumber();
                int years = static_cast<int>(v.numberOr("years", 10));
                if (k != "target_opinion") w.addOpinion(p.id, target, mod, val, years);
                if (k != "opinion") w.addOpinion(target, p.id, mod, val, years);
            } else if (k == "forfeit_land") {
                // The manor court takes a share of the holding for the arrears.
                if (!h) continue;
                double acres = w.forfeitLand(*h, true);
                if (acres > 0) w.addStress(p.id, 30, "抵債失去的地");
                if (acres > 0 && h->id == w.playerHousehold_)
                    w.log(1, "  莊園法庭判把 " + std::to_string(int(acres)) + " 畝地收回去抵債。");
                w.clearDebt(*h);
            } else if (k == "boon_for_debt") {
                if (!h) continue;
                h->boonUntil = w.day_ + static_cast<int>(v.asNumber()) * kDaysPerYear;
                w.clearDebt(*h);
            } else if (k == "take_lodger") {
                if (!h) continue;
                h->lodgerUntil = w.day_ + static_cast<int>(v.asNumber()) * kDaysPerYear;
                h->debt *= 0.5;
                h->debtYears = 0;
            } else if (k == "surrender_holding") {
                // Give the holding back and keep the croft: no more rent worth the name, and every
                // pair of hands goes out for wages.
                if (!h) continue;
                double give = std::max(0.0, h->acres - std::max(1.0, h->freehold));
                h->acres -= give;
                w.vacantAcres_ += give;
                w.clearDebt(*h);
            } else if (k == "clear_debt") {
                if (h) w.clearDebt(*h);
            } else if (k == "lords_debtor") {
                // Beholden to the lord: no village will elect his debtor reeve for a while.
                w.cooldowns_[{p.id, "lords_debtor"}] = w.day_ + static_cast<int>(v.asNumber()) * kDaysPerYear;
            } else if (k == "take_lover") {
                if (target != kNone) w.takeLover(p.id, target);
            } else if (k == "lord_opinion") {
                if (w.lord_ == kNone || !w.people_[w.lord_].alive() || w.lord_ == p.id) continue;
                w.addOpinion(w.lord_, p.id, v.stringOr("mod", "event"), v["value"].asNumber(), static_cast<int>(v.numberOr("years", 10)));
            } else if (k == "set_occupation") {
                p.occupation = v.asString();
                if (!w.defs_.occupation(p.occupation)) throw JsonError("unknown occupation " + p.occupation);
            } else if (k == "kill") {
                w.kill(p.id, v.asString());
                return;
            } else if (k == "add_item") {
                // One of each kind per house: a second longbow is just a bow, not an heirloom.
                if (!h) continue;
                bool have = false;
                for (Id it : h->items)
                    if (w.items_[it].type == v.asString() && w.items_[it].household == h->id) have = true;
                if (!have) w.addItem(v.asString(), p.id, h->id);
            } else if (k == "acres") {
                if (!h) continue;
                double d = v.asNumber();
                if (d > 0) d = std::min(d, w.vacantAcres_);
                d = std::max(d, -h->acres);
                h->acres += d;
                h->freehold = std::min(h->freehold, h->acres);
                w.vacantAcres_ -= d;
            } else if (k == "house") {
                if (h) h->house = v.asString();
            } else if (k == "target") {
                if (target != kNone) apply(v, w.people_[target], p.id);
            } else if (k == "world_flag") {
                w.worldFlags_.insert(v.asString());
            } else if (k == "tradition") {
                if (p.dynasty == kNone) continue;
                auto& tr = w.dynasties_[p.dynasty].traditions;
                if (std::find(tr.begin(), tr.end(), v.asString()) == tr.end()) tr.push_back(v.asString());
            } else if (k == "freehold") {
                // Turn held acres into freehold (bought by charter).
                if (!h) continue;
                h->freehold = std::min(h->acres, h->freehold + v.asNumber());
            } else if (k == "to_town") {
                // Off to a master in town: the family's town branch.
                if (!h) continue;
                h->members.erase(std::remove(h->members.begin(), h->members.end(), p.id), h->members.end());
                p.household = kNone;
                p.flags.insert("in_town");
                if (p.dynasty != kNone) {
                    w.dynasties_[p.dynasty].townKin = p.id;
                    w.dynasties_[p.dynasty].townSince = w.day_;
                }
                if (h->head == p.id) w.onHeadDied(*h, p.id);
                return;
            } else if (k == "leave_village") {
                if (!h) continue;
                h->members.erase(std::remove(h->members.begin(), h->members.end(), p.id), h->members.end());
                p.household = kNone;
                p.flags.insert(v.asString());
                if (h->head == p.id) w.onHeadDied(*h, p.id);
                return;
            } else if (k == "marry_target") {
                if (target == kNone) continue;
                Person& t = w.people_[target];
                if (p.sex == t.sex || w.closeKin(p.id, t.id) || w.gentry(p.id) != w.gentry(t.id)) continue;
                auto married = [&](const Person& x) { return x.spouse != kNone && w.people_[x.spouse].alive(); };
                if (married(p) || married(t)) continue;
                Id man = p.sex == Sex::Male ? p.id : t.id;
                Id woman = p.sex == Sex::Male ? t.id : p.id;
                w.marry(man, woman);
            } else if (k == "random") {
                bool hit = w.rngEvents_.chance(v["percent"].asNumber() / 100.0);
                apply(hit ? v["effects"] : v["else"], p, target);
                if (!p.alive()) return;
            } else if (k == "progress") {
                if (plan_) plan_->progress += v.asNumber();
            } else if (k == "abort_plan") {
                if (plan_) plan_->progress = -1000;  // resolved as a failure at month end
            } else if (k == "subject") {
                if (plan_ && plan_->subject != kNone) apply(v, w.people_[plan_->subject], target);
            } else if (k == "marry_subject_target") {
                if (!plan_ || plan_->subject == kNone || target == kNone) continue;
                Person& sub = w.people_[plan_->subject];
                Json one = Json::parse("[{\"marry_target\": true}]");
                apply(one, sub, target);
            } else if (k == "take_acres") {
                // Buy land from the target's household.
                if (!h || target == kNone || w.people_[target].household == kNone) continue;
                Household& th = w.households_[w.people_[target].household];
                double acres = std::min(v["acres"].asNumber(), th.acres);
                double price = v["price"].asNumber();
                if (acres <= 0 || h->money < price) continue;
                double fh = th.acres > 0 ? th.freehold * acres / th.acres : 0;
                th.freehold -= fh;
                h->freehold += fh;
                th.acres -= acres;
                th.money += price;
                h->acres += acres;
                h->money -= price;
            } else if (k == "deed") {
                // {"type": "arson", "doer": "self"|"target", "scale": 1}; the other party is the victim.
                // "to": "subject" makes the plan's subject the other party; "to": "none" for no one;
                // "to": "spouse" / "lord" for those.
                bool byTarget = v.stringOr("doer", "self") == "target";
                std::string to = v.stringOr("to", "");
                Id other = to == "subject" ? (plan_ ? plan_->subject : kNone) : to == "none" ? kNone
                           : to == "spouse" ? (p.spouse != kNone && w.people_[p.spouse].alive() ? p.spouse : kNone)
                           : to == "lord" ? (w.lord_ != kNone && w.people_[w.lord_].alive() ? w.lord_ : kNone) : target;
                if ((to == "spouse" || to == "lord") && other == kNone) continue;
                Id actor = byTarget ? target : p.id;
                Id victim = byTarget ? p.id : other;
                if (actor == kNone) continue;
                w.recordDeed(v.stringOr("type", ""), actor, victim, v.numberOr("scale", 1), cause_);
            } else if (k == "accuse") {
                // Take the target to the manor court for the deed this event is about.
                if (target != kNone && deed_ != kNone) w.accuse(p.id, target, deed_);
            } else if (k == "confess_to_target") {
                // The doer tells the victim the truth; the victim now knows for certain.
                if (deed_ != kNone && target != kNone) {
                    w.addBelief(target, deed_, p.id, 100, p.id);
                    w.recordDeed("confessed", p.id, target, 1, deed_);
                }
            } else if (k == "adopt_heir") {
                // A nephew from the next village comes to live, work the land and carry the name.
                if (!h) continue;
                Sex sex = w.rngEvents_.chance(0.75) ? Sex::Male : Sex::Female;
                int age = w.rngEvents_.range(static_cast<int>(v.numberOr("min_age", 14)), static_cast<int>(v.numberOr("max_age", 20)));
                Id id = w.addPerson(w.pickName(sex, kNone, kNone), p.dynasty, sex, w.day_ - age * kDaysPerYear - w.rngEvents_.range(0, 300), h->id);
                Person& np = w.people_[id];
                np.occupation = sex == Sex::Male ? w.people_[h->head].occupation : "housewife";
                if (!w.defs_.occupation(np.occupation)) np.occupation = "peasant";
                np.free = w.people_[h->head].free;
                w.rollCongenital(np);
                w.rollPersonality(np, 2);
                np.health = w.rngEvents_.range(70, 95);
                np.skills[Farming] = w.rngEvents_.range(3, 8);
                if (h->id == w.playerHousehold_)
                    w.log(1, "  遠房的" + std::string(sex == Sex::Male ? "姪子 " : "姪女 ") + w.fullName(id) + "（" + std::to_string(age) +
                                 " 歲）從鄰村搬來，往後就是這個家的人了。");
            } else if (k == "jury_vote") {
                w.juryVote_ = static_cast<int>(v.asNumber());
            } else if (k == "note_deed") {
                if (deed_ != kNone) w.deedNotes_.insert({deed_, v.asString()});
            } else if (k == "if") {
                apply(eval(v["cond"], p, target) ? v["then"] : v["else"], p, target);
                if (!p.alive()) return;
            } else if (k == "run_event") {
                if (target != kNone) w.runEventById(v.asString(), p.id, target);
            } else if (k == "accuse_hook") {
                Id d = target != kNone ? w.hookDeed(p.id, target) : kNone;
                if (d != kNone) w.accuse(p.id, target, d);
            } else if (k == "use_hook" || k == "doubt_hook") {
                // Spend the leverage; "doubt_hook": the holder no longer trusts what they heard.
                Id d = target != kNone ? w.hookDeed(p.id, target) : kNone;
                for (auto& b : w.beliefs_[p.id])
                    if (b.deed == d) {
                        b.used = true;
                        if (k == "doubt_hook") b.confidence = std::min(b.confidence, 20.0);
                    }
            } else if (k == "hook_payment") {
                // Hush money: up to the amount, or what the target's house can spare.
                if (!h || target == kNone || w.people_[target].household == kNone) continue;
                Household& th = w.households_[w.people_[target].household];
                double amount = std::min(v.asNumber(), std::max(0.0, th.money * 0.4));
                th.money -= amount;
                h->money += amount;
                if (h->id == w.playerHousehold_ || th.id == w.playerHousehold_)
                    w.log(1, "  " + w.fullName(target) + " 付了 " + std::to_string(int(amount)) + " 便士封口費。");
            } else if (k == "owe_target") {
                // p's house owes the target money (an old loan, a debt from the father's time).
                if (h && target != kNone && h->debt <= 0.5) w.addDebt(*h, v.asNumber(), target);
            } else if (k == "beat_children") {
                // The young ones in the house remember it, and it shapes who they become.
                if (!h) continue;
                for (Id m : h->members) {
                    Person& c = w.people_[m];
                    double age = w.ageYears(c);
                    if (m == p.id || age < 3 || age >= 12) continue;
                    w.recordDeed("beaten_child", p.id, m);
                    c.flags.insert(w.rngEvents_.chance(0.5) ? "childhood_craven" : "childhood_wrathful");
                    c.flags.insert("beaten_by_" + std::to_string(p.id));
                }
            } else if (k == "ale_debt") {
                // Drink on the slate: if the purse is empty, the alewife writes it down.
                if (!h) continue;
                double amount = v.asNumber();
                if (h->money >= amount) {
                    h->money -= amount;
                    continue;
                }
                double owed = amount - h->money;
                h->money = 0;
                Id alewife = kNone;
                for (const auto& o : w.people_)
                    if (o.alive() && o.occupation == "alewife" && o.household != kNone && o.household != h->id) alewife = o.id;
                if (alewife != kNone && h->debt <= 0.5) {
                    w.addDebt(*h, owed * 3, alewife);  // a winter's drinking, not one night's
                    if (h->id == w.playerHousehold_)
                        w.log(1, "  酒錢付不出來，" + w.fullName(alewife) + " 在門後的木板上又劃了幾道。");
                }
            } else if (k == "hush_money") {
                // Pay the target to keep quiet: more for a worse secret, more from a richer house.
                if (!h || target == kNone || deed_ == kNone || w.people_[target].household == kNone) continue;
                const DeedDef* dd = w.defs_.deed(w.deeds_[deed_].type);
                double sev = 1;
                if (dd) {
                    auto it = dd->roles.find("target");
                    if (it != dd->roles.end()) sev = std::clamp(std::abs(it->second) / 25, 0.6, 2.5);
                }
                double amount = std::round(v.numberOr("base", 15) * sev * (h->money > 500 ? 1.6 : 1));
                amount = std::min(amount, h->money);
                h->money -= amount;
                w.households_[w.people_[target].household].money += amount;
                if (h->id == w.playerHousehold_)
                    w.log(1, "  一共塞了 " + std::to_string(int(amount)) + " 便士。");
            } else if (k == "signal") {
                // Raise a signal about p and the target, for events that wait on it ("as": "actor" = p).
                Signal sg{v.asString(), deed_, p.id, target, false, 0};
                w.emit(sg);
            } else if (k == "tell_victim") {
                // The target (who knows p's secret) goes to the victim's house with it.
                if (target == kNone || deed_ == kNone) continue;
                const Deed& d = w.deeds_[deed_];
                Id victim = d.target;
                if (victim == kNone) continue;
                if (!w.people_[victim].alive() || w.people_[victim].household == kNone) {
                    Id vh = w.people_[victim].household;
                    if (vh == kNone || w.households_[vh].extinct) continue;
                    victim = w.households_[vh].head;
                }
                if (victim == kNone || victim == target) continue;
                for (auto& b : w.beliefs_[target])
                    if (b.deed == deed_) b.quietUntil = -1;
                w.addBelief(victim, deed_, p.id, 85, target);
            } else if (k == "silence_target") {
                // The target (who knows p's secret) agrees to keep quiet for some years.
                if (target == kNone || deed_ == kNone) continue;
                for (auto& b : w.beliefs_[target])
                    if (b.deed == deed_) b.quietUntil = w.day_ + static_cast<int>(v.numberOr("years", 10) * kDaysPerYear);
            } else if (k == "clear_suspicion") {
                // The target is talked out of believing p did the deed.
                if (target == kNone || deed_ == kNone) continue;
                for (auto& b : w.beliefs_[target])
                    if (b.deed == deed_ && b.culprit == p.id) b.confidence = std::min(b.confidence, v.numberOr("to", 20));
            } else if (k == "pay_debt") {
                if (h) h->debt = std::max(0.0, h->debt - v.asNumber());
            } else if (k == "sell_ox") {
                if (!h) continue;
                h->noOxUntil = w.day_ + 40 * kDaysPerYear;  // until they can buy another
                if (h->debt > 0) h->debt = std::max(0.0, h->debt - 40);
                else h->money += 40;
            } else if (k == "service_for_debt") {
                // A child goes to work for the creditor; the first years' wages pay the debt.
                if (!h) continue;
                Id child = serviceChild(*h);
                Id master = h->creditor != kNone && w.people_[h->creditor].alive() ? h->creditor : w.lord_;
                if (child == kNone || master == kNone || w.people_[master].household == kNone) continue;
                int years = static_cast<int>(v.numberOr("years", 3));
                w.sendToService(child, w.households_[w.people_[master].household].head, years);
                // The child's years are the settlement: what is left is small enough to forgive, or the
                // creditor waits for it from the start again.
                h->debt = std::max(0.0, h->debt - v.numberOr("value", 30));
                if (h->debt < 20) w.clearDebt(*h);
                h->debtYears = 0;
                if (h->id == w.playerHousehold_)
                    w.log(1, "  " + w.fullName(child) + " 去了 " + w.fullName(master) + " 家幹活，頭 " + std::to_string(years) +
                                 " 年的工錢先拿來抵債。");
            } else if (k == "food_days") {
                if (h) h->food = std::max(0.0, h->food + w.consumption(*h) * v.asNumber());
            } else if (k == "borrow_grain") {
                // A neighbour who thinks well of the family lends grain against the next harvest.
                if (!h) continue;
                Id lender = w.findLender(*h);
                if (lender == kNone) {
                    if (h->id == w.playerHousehold_) w.log(1, "  可是村裡沒有一家肯借，" + p.firstName + " 空著手回家。");
                    continue;
                }
                h->food += w.consumption(*h) * v.numberOr("days", 90);
                double owed = v.numberOr("debt", 30);
                if (h->debt <= 0.5 || h->creditor == lender) {
                    w.addDebt(*h, owed, lender);
                    w.households_[w.people_[lender].household].money -= owed * 0.6;
                }
                w.recordDeed("lent_money", lender, p.id, 1, cause_);
                if (h->id == w.playerHousehold_)
                    w.log(1, "  " + w.fullName(lender) + " 借了一袋燕麥和豆子，說好秋收後還 " + std::to_string(int(owed)) + " 便士。");
            } else if (k == "service_for_food") {
                // One mouth fewer: a child goes to serve a better-off house for bed and board.
                if (!h) continue;
                Id child = serviceChild(*h);
                Id master = kNone;
                double best = 0;
                for (const auto& o : w.households_)
                    if (!o.extinct && o.id != h->id && o.head != kNone && o.money > best &&
                        w.people_[o.head].occupation != "priest" && w.people_[o.head].occupation != "lord")
                        best = o.money, master = o.head;
                if (child == kNone || master == kNone) continue;
                w.sendToService(child, master, static_cast<int>(v.numberOr("years", 3)));
                h->food += w.consumption(*h) * 20;
                if (h->id == w.playerHousehold_) w.log(1, "  " + w.fullName(child) + " 去了 " + w.fullName(master) + " 家幫傭，管吃管住。");
            } else if (k == "borrow") {
                // A better-off neighbour pays the steward; now the debt is to them.
                if (!h || h->debt <= 0.5) continue;
                Id lender = w.findLender(*h);
                if (lender == kNone) continue;
                Household& lh = w.households_[w.people_[lender].household];
                lh.money -= h->debt;
                if (h->creditor != kNone && w.people_[h->creditor].household != kNone)
                    w.households_[w.people_[h->creditor].household].money += h->debt;
                h->creditor = lender;
                h->debtYears = 0;
                w.recordDeed("lent_money", lender, p.id, 1, cause_);
                if (h->id == w.playerHousehold_)
                    w.log(1, "  " + w.fullName(lender) + " 借了 " + std::to_string(int(h->debt)) + " 便士給 " + w.fullName(p.id) +
                                 " 還給莊園，這下欠的是" + (w.people_[lender].sex == Sex::Male ? "他" : "她") + "的人情和錢。");
            } else if (k == "debt_grace") {
                if (h) h->debtYears = 0;
            } else if (k == "peace_marriage") {
                auto [mine, theirs] = peaceMatch(p, target);
                if (mine == kNone) continue;
                Id man = w.people_[mine].sex == Sex::Male ? mine : theirs;
                Id woman = man == mine ? theirs : mine;
                w.marry(man, woman);
                w.endFeud(p.dynasty, w.people_[target].dynasty, w.fullName(mine) + " 和 " + w.fullName(theirs) + " 結了親");
            } else if (k == "rent_factor") {
                if (h) h->rentFactor = std::clamp(h->rentFactor + v.asNumber(), 0.7, 1.8);
            } else if (k == "feud_heat") {
                if (target != kNone) w.feudHeat(p.dynasty, w.people_[target].dynasty, v.asNumber(), cause_);
            } else if (k == "end_feud") {
                if (target != kNone) w.endFeud(p.dynasty, w.people_[target].dynasty, v.asString());
            } else if (k == "concern") {
                // {"kind": "grievance", "about": "target"|"self"|"none", "strength": 40}
                std::string about = v.stringOr("about", "target");
                Id a = about == "target" ? target : about == "self" ? p.id : kNone;
                if (about.rfind("about:", 0) == 0) {
                    // Whoever another of this person's concerns is about: a longing turned to heartbreak.
                    const Concern* c = w.concernOf(p, about.substr(6));
                    if (!c) continue;
                    a = c->about;
                }
                w.addConcern(p.id, v["kind"].asString(), a, v.numberOr("strength", 40));
            } else if (k == "ease_concern") {
                // {"kind": "grief", "by": 25, "about": "any"|"target"}
                w.easeConcern(p.id, v["kind"].asString(), v.numberOr("by", 20), v.stringOr("about", "any") == "target" ? target : kNone);
            } else if (k == "work_bonus") {
                if (h) h->workBonus = std::clamp(h->workBonus + v.asNumber(), 0.0, 0.12);
            } else if (k == "start_plan") {
                // {"plan": "arrange_match", "subject": "target", "target": "target_about:longing"}: the head takes up
                // a plan for someone, when the rules allow it now.
                auto who = [&](const std::string& s) -> Id {
                    if (s == "target") return target;
                    if (s == "self") return p.id;
                    if (s.rfind("target_about:", 0) == 0 && target != kNone) {
                        const Concern* c = w.concernOf(w.people_[target], s.substr(13));
                        return c ? c->about : kNone;
                    }
                    return kNone;
                };
                Id subj = who(v.stringOr("subject", "")), tgt = who(v.stringOr("target", ""));
                std::string plan = v["plan"].asString();
                bool ok = false;
                for (const auto& o : w.availablePlans(p.id))
                    if (o.plan == plan && o.target == tgt && (subj == kNone || o.subject == subj)) ok = true;
                if (ok) w.startPlan(p.id, plan, tgt, subj);
                else if (w.isPlayerFamily(p)) w.log(1, "  可是眼下這件事辦不成。");
            } else if (k == "on_target") {
                // Effects on the other party, who then has this person as their target.
                if (target != kNone && w.people_[target].alive()) apply(v, w.people_[target], p.id);
            } else if (k == "stress") {
                w.addStress(p.id, v.asNumber(), "");
            } else if (k == "confess_secrets") {
                // "priest" keeps the seal of confession; "spouse" may not.
                std::string to = v.asString();
                Id who = to == "priest" ? w.priest_ : to == "spouse" ? p.spouse : target;
                if (who != kNone && who != p.id) w.confessSecrets(p.id, who);
            } else if (k == "household_opinion") {
                // Everyone at home thinks differently of this person now.
                if (!h) continue;
                std::string mod = v.stringOr("mod", "event");
                for (Id m : h->members)
                    if (m != p.id) w.addOpinion(m, p.id, mod, v["value"].asNumber(), static_cast<int>(v.numberOr("years", 5)));
            } else if (k == "confess_to_priest") {
                if (deed_ != kNone && w.priest_ != kNone) w.addBelief(w.priest_, deed_, p.id, 100, p.id);
            } else if (k == "investigate") {
                if (target != kNone) w.investigate(p.id, target);
            } else if (k == "end_service") {
                if (p.home != kNone) {
                    Id home = p.home;
                    p.home = kNone;
                    p.serviceEnd = -1;
                    p.flags.erase("in_service");
                    w.moveTo(p.id, home);
                }
            } else if (k == "set_free") {
                w.setFree(p.id);
            } else if (k == "send_subject_to_service") {
                // The plan's subject goes to serve in the target's household for some years.
                if (plan_ && plan_->subject != kNone && target != kNone)
                    w.sendToService(plan_->subject, target, static_cast<int>(v.numberOr("years", 5)));
            } else if (k == "occasion") {
                // {"came": true, "value": 6, "mod": "came_to_wake"}: at (or missing) the wake or wedding of the target's house.
                w.attendOccasion(p.id, target, !v["came"].isBool() || v["came"].asBool(), v.numberOr("value", 0), v.stringOr("mod", "event"));
            } else if (k == "spend_time") {
                // An occasion takes one of the head's free days, if there are any left this season.
                if (w.timeLeft() > 0) w.spendTime(static_cast<int>(v.asNumber()));
            } else if (k == "log") {
                w.log(p.household == w.playerHousehold_ ? 1 : 2, format(v.asString(), p, target));
            } else {
                throw JsonError("unknown effect key: " + k);
            }
        }
    }
}

std::string EventRunner::format(const std::string& tmpl, const Person& p, Id target) const {
    auto pron = [&](const Person& x) { return std::string(x.sex == Sex::Male ? "他" : "她"); };
    std::string out;
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] == '{') {
            size_t end = tmpl.find('}', i);
            std::string key = tmpl.substr(i + 1, end - i - 1);
            std::string val;
            if (key == "name") val = w_.fullName(p.id);
            else if (key == "first") val = w_.shortName(p.id);
            else if (key == "pron") val = pron(p);
            else if (key == "age") val = std::to_string(int(w_.ageYears(p)));
            else if (key == "spouse") val = w_.fullName(p.spouse);
            else if (key == "target") val = w_.fullName(target);
            else if (key == "target_first") val = target != kNone ? w_.shortName(target) : "";
            else if (key == "target_pron") val = target != kNone ? pron(w_.people_[target]) : "";
            else if (key == "year") val = std::to_string(w_.year());
            else if (key == "subject") val = plan_ ? w_.fullName(plan_->subject) : "";
            else if (key == "plan") val = plan_ ? w_.defs_.plan(plan_->plan)->name : "";
            else if (key == "deed") {
                const DeedDef* dd = deed_ != kNone ? w_.defs_.deed(w_.deeds_[deed_].type) : nullptr;
                val = dd ? dd->name : "";
            } else if (key == "hook") {
                Id d = target != kNone ? w_.hookDeed(p.id, target) : kNone;
                val = d != kNone ? w_.describeDeed(d) : "那件事";
            } else if (key == "debt") val = p.household != kNone ? std::to_string(int(w_.households_[p.household].debt)) : "0";
            else if (key == "feud_cause") {
                const Feud* f = target != kNone ? w_.feudBetween(p.dynasty, w_.people_[target].dynasty) : nullptr;
                val = f ? w_.describeDeed(f->cause) : "";
            } else if (key == "target_house") val = target != kNone && w_.people_[target].dynasty != kNone ? w_.dynasties_[w_.people_[target].dynasty].surname : "";
            else if (key.rfind("about:", 0) == 0) {
                // {about:grief}: who this person's grief (or other concern) is about.
                const Concern* c = w_.concernOf(p, key.substr(6));
                Id a = c ? c->about : kNone;
                val = a == kNone ? "" : w_.isPlayerFamily(w_.people_[a]) ? w_.shortName(a) : w_.fullName(a);
            } else if (key.rfind("target_about:", 0) == 0) {
                const Concern* c = target != kNone ? w_.concernOf(w_.people_[target], key.substr(13)) : nullptr;
                Id a = c ? c->about : kNone;
                val = a == kNone ? "" : w_.isPlayerFamily(w_.people_[a]) ? w_.shortName(a) : w_.fullName(a);
            } else if (key == "kin") val = target != kNone ? w_.kinWord(target, p.id) : "";
            else if (key == "target_kin") val = target != kNone ? w_.kinWord(p.id, target) : "";
            else if (key == "stress_cause") val = p.stressCause.empty() ? "說不出的心事" : p.stressCause;
            else if (key == "deed_year") val = deed_ != kNone ? std::to_string(w_.cfg_.startYear + w_.deeds_[deed_].day / kDaysPerYear) : "";
            else val = "{" + key + "}";
            out += val;
            i = end + 1;
        } else {
            out += tmpl[i++];
        }
    }
    return out;
}

}  // namespace sim
