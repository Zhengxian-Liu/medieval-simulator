#include "simcore/events.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

bool inRange(double v, const Json& r) {
    if (r.isNumber()) return v == r.asNumber();
    return v >= r[0].asNumber() && v <= r[1].asNumber();
}

// A concern about a husband or wife now dead whom this person has since replaced: a widow who has remarried still
// carries her first husband, but it is not hers to bring up with the new family.
bool aboutFormerSpouse(const std::vector<Person>& people, const Person& who, const Concern& c) {
    return c.about != kNone && c.about != who.spouse && people[c.about].spouse == who.id && !people[c.about].alive();
}

}  // namespace

void EventRunner::pulse(const std::string& kind) {
    std::vector<Id> ids = w_.livingInVillage();
    std::vector<Id> away = w_.livingAway();  // for "away" events: the family's people living outside the village
    Id playerHead = w_.playerHousehold_ != kNone ? w_.households_[w_.playerHousehold_].head : kNone;

    for (const auto& ev : w_.defs_.events) {
        if (ev.pulse != kind) continue;
        for (Id id : ev.scope == "away" ? away : ids) {
            Person& p = w_.people_[id];
            if (!eligible(ev, p, playerHead)) continue;
            if (!eval(ev.conditions, p, kNone)) continue;
            if (!w_.rngEvents_.chance(ev.chance)) continue;

            Id target = kNone;
            if (!ev.target.isNull()) {
                target = pickTarget(ev, p);
                if (target == kNone) continue;
            }
            pulsing_ = true;
            fire(ev, p, target);
            pulsing_ = false;
            if (w_.gameOver_) return;
        }
    }
}

// One card held back at the opening, if it still fits (the person about, the target too, its conditions still holding,
// and not met since); one held more than a season is let go, its moment past.
void World::releaseDeferredCard() {
    while (!deferredCards_.empty() && !gameOver_) {
        DeferredCard d = deferredCards_.front();
        deferredCards_.erase(deferredCards_.begin());
        if (day_ - d.day > 3 * kDaysPerMonth) continue;
        const EventDef* ev = nullptr;
        for (const auto& e : defs_.events)
            if (e.id == d.event) ev = &e;
        if (!ev || d.who == kNone) continue;
        Person& p = people_[d.who];
        if (d.target != kNone && (!people_[d.target].alive() || people_[d.target].household == kNone)) continue;
        if (EventRunner(*this).refire(*ev, p, d.target)) return;
    }
}

std::vector<std::pair<std::string, Id>> World::deferredForTest() const {
    std::vector<std::pair<std::string, Id>> out;
    for (const auto& d : deferredCards_) out.push_back({d.event, d.who});
    return out;
}

bool EventRunner::refire(const EventDef& ev, Person& p, Id target) {
    Id playerHead = w_.playerHousehold_ != kNone ? w_.households_[w_.playerHousehold_].head : kNone;
    if (!eligible(ev, p, playerHead) || !eval(ev.conditions, p, target)) return false;
    if (target != kNone && !ev.target["conditions"].isNull() && !eval(ev.target["conditions"], w_.people_[target], p.id)) return false;
    fire(ev, p, target);
    return true;
}

bool EventRunner::eligible(const EventDef& ev, const Person& p, Id playerHead) const {
    if (!p.alive()) return false;
    if (ev.scope == "away" ? !w_.isAway(p) : p.household == kNone) return false;
    if (ev.scope == "player_family" && !w_.isPlayerFamily(p)) return false;
    if (ev.scope == "player_head" && p.id != playerHead) return false;
    if (ev.once && p.flags.count("ev_" + ev.id)) return false;
    if (!ev.budget.empty()) {
        auto it = w_.budgetUsed_.find({p.household, ev.budget, w_.budgetYear(ev.budget)});
        if (it != w_.budgetUsed_.end() && it->second >= 2) return false;
    }
    if (auto key = storyKey(ev, p); key.first != kNone && w_.metLately(key.first, key.second)) return false;
    auto cd = w_.cooldowns_.find({p.id, ev.id});
    return cd == w_.cooldowns_.end() || cd->second <= w_.day_;
}

std::pair<Id, std::string> EventRunner::storyKey(const EventDef& ev, const Person& p) const {
    if (ev.institution || ev.repeat == "free" || ev.chance >= 1 || ev.scope == "away" || (ev.pulse != "monthly" && ev.pulse != "yearly"))
        return {kNone, ""};
    if (!w_.isPlayerFamily(p) && !w_.inPlayerHouse(p.household)) return {kNone, ""};
    if (ev.repeat == "person") return {p.id, "p:" + ev.id};
    return {p.household, ev.id};
}

int World::freshVariant(Id house, const std::string& story, int n, Rng& rng) {
    auto& told = toldPrompts_[{house, story}];
    if (static_cast<int>(told.size()) >= n) told.clear();
    std::vector<int> fresh;
    for (int k = 0; k < n; ++k)
        if (std::find(told.begin(), told.end(), k) == told.end()) fresh.push_back(k);
    int k = fresh[rng.range(0, static_cast<int>(fresh.size()) - 1)];
    told.push_back(k);
    return k;
}

// A signal-driven event whose day has come: the target and the deed come from the signal.
void EventRunner::fireScheduled(const EventDef& ev, Person& p, Id target, Id deed) {
    Id playerHead = w_.playerHousehold_ != kNone ? w_.households_[w_.playerHousehold_].head : kNone;
    if (!eligible(ev, p, playerHead)) {
        // The same blow again before the last has healed: a quiet line, not the same card a second time.
        if (!ev.quietLog.empty() && target != kNone && p.alive() && w_.inPlayerHouse(p.household) && eval(ev.conditions, p, target))
            w_.log(1, format(ev.quietLog, p, target));
        return;
    }
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
    // A second wake or wedding the same season: the head's answer to the first stands (World::occasionByPolicy).
    if ((sig == "news:funeral" || sig == "news:wedding") && target != kNone &&
        w_.occasionByPolicy(sig.substr(5), target, w_.houseLink(p.household, w_.people_[target].household)))
        return;
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

// The house that would take a child into service for bed and board: the best-off with a servant's place free.
Id EventRunner::serviceMaster(const Household& h) const {
    Id master = kNone;
    double best = 0;
    for (const auto& o : w_.households_)
        if (!o.extinct && o.id != h.id && o.head != kNone && o.money > best && w_.placeOpen(o.id, "service") &&
            w_.people_[o.head].occupation != "priest" && w_.people_[o.head].occupation != "lord")
            best = o.money, master = o.head;
    return master;
}

// The child a household would send out to work off a debt: the eldest at home who is free to go. Not one with a road
// of their own already (fun-r8 3): a clerk, an apprentice, a child the family is getting a church post or a match for.
Id EventRunner::serviceChild(const Household& h) const {
    Id best = kNone;
    for (Id m : h.members) {
        const Person& c = w_.people_[m];
        double age = w_.ageYears(c);
        if (m == h.head || age < 10 || age > 18 || c.home != kNone || w_.gentry(m)) continue;
        if (c.spouse != kNone && w_.people_[c.spouse].alive()) continue;
        if (c.occupation == "clerk" || c.occupation == "priest" || (!c.station.empty() && c.station != "home")) continue;
        bool planned = false;
        for (const auto& ap : w_.plans_)
            if (ap.subject == m) planned = true;
        if (planned) continue;
        if (best == kNone || c.birthDay < w_.people_[best].birthDay) best = m;
    }
    return best;
}

const std::string& EventRunner::pickLog(const OptionDef& o, Id who) {
    if (o.logs.size() < 2) return o.log;
    int n = static_cast<int>(o.logs.size());
    int k = w_.rngEvents_.range(0, n - 1);
    auto key = std::make_pair(who, o.text + "|" + o.logs[0]);
    auto last = w_.lastLog_.find(key);
    if (last != w_.lastLog_.end() && last->second == k) k = (k + 1) % n;  // not the same words twice running
    w_.lastLog_[key] = k;
    return o.logs[k];
}

// Choosing against one's nature weighs on a person; choosing true to it eases them a little.
// An option is "against" a trait when another option suited that trait clearly better.
void EventRunner::natureStress(const EventDef& ev, const OptionDef& chosen, Person& p, Id target) {
    if (ev.options.size() < 2) return;
    if (ev.on.isObject() && ev.on.stringOr("signal", "") == "stress") return;  // breaking down is not itself a choice
    // Paying what one owes in full is not yielding or money thrown away, however much welching would have suited.
    if (chosen.effects.isArray())
        for (const auto& e : chosen.effects.asArray())
            if (e.isObject() && e["pay_off_debt"].isBool()) return;
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

// The line for a choice the data writes none for: who chose it. A child's road (or a small child's moment) is the call
// of the one keeping the house, and is said as theirs, for the child.
std::string EventRunner::choseLine(const Person& p, Id target, const std::string& option) {
    Id acting = w_.actingHead();
    if (acting != kNone && acting != p.id)
        return w_.fullName(acting) + " 替 " + format("{name}", p, target) + " 選了：" + option + "。";
    return format("{name}", p, target) + "選了：" + option + "。";
}

// Whose answer it is: the one keeping the house decides for themselves, for the young children in it, and on the big
// roads (parent_decides: school, the cloister, the levy, a match) for anyone of the house, a brother or a stepchild as
// much as a son, at home or away from it. Someone the head goes to see answers for themselves, child or not.
bool EventRunner::playersToAnswer(const EventDef& ev, const Person& p) const {
    Id acting = w_.actingHead();
    bool ofHouse = acting != kNone && p.id != acting &&
                   (p.household == w_.playerHousehold_ || (w_.isAway(p) && p.awayFrom == w_.playerHousehold_));
    return ev.scope != "interaction" && (p.id == acting || (p.household == w_.playerHousehold_ && w_.ageYears(p) < 14) ||
                                         (ev.parentDecides && ofHouse && (p.spouse == kNone || p.household == w_.playerHousehold_)));
}

void EventRunner::fire(const EventDef& ev, Person& p, Id target) {
    // Past the opening's few questions, a card that would stop the player waits for the first year to be under way.
    // Only a pulse card waits: one sent by a signal (a death, a deed, a fine) is about its moment and is asked now.
    if (pulsing_ && w_.openingFull() && ev.ask && ev.options.size() > 1 && !(ev.routine && w_.routineByRule_) && playersToAnswer(ev, p)) {
        auto& q = w_.deferredCards_;
        bool queued = std::any_of(q.begin(), q.end(), [&](const World::DeferredCard& d) { return d.event == ev.id && d.who == p.id; });
        if (!queued) q.push_back({ev.id, p.id, target, w_.day_});
        return;
    }
    // Asked now: a copy still waiting is not asked a second time.
    w_.deferredCards_.erase(std::remove_if(w_.deferredCards_.begin(), w_.deferredCards_.end(),
                                           [&](const World::DeferredCard& d) { return d.event == ev.id && d.who == p.id; }),
                            w_.deferredCards_.end());
    bookAs_ = ev.title;
    const OptionDef& o = ev.options[chooseOption(ev, p, target)];
    natureStress(ev, o, p, target);
    if (ev.once) p.flags.insert("ev_" + ev.id);
    if (auto key = storyKey(ev, p); key.first != kNone) w_.meetStory(key.first, key.second);
    if (!ev.budget.empty()) ++w_.budgetUsed_[{p.household, ev.budget, w_.budgetYear(ev.budget)}];
    if (ev.cooldownMonths > 0) w_.cooldowns_[{p.id, ev.id}] = w_.day_ + ev.cooldownMonths * kDaysPerMonth;
    if (ev.cooldownYears > 0) {
        w_.cooldowns_[{p.id, ev.id}] = w_.day_ + ev.cooldownYears * kDaysPerYear;
        if (ev.householdCooldown && p.household != kNone)
            for (Id m : w_.households_[p.household].members) w_.cooldowns_[{m, ev.id}] = w_.day_ + ev.cooldownYears * kDaysPerYear;
    }

    bool home = p.household == w_.playerHousehold_ || p.home == w_.playerHousehold_ || (w_.isAway(p) && p.awayFrom == w_.playerHousehold_) ||
                (target != kNone && w_.people_[target].household == w_.playerHousehold_);
    bool family = w_.isPlayerFamily(p) || (target != kNone && w_.isPlayerFamily(w_.people_[target]));
    ++w_.counters_["event:" + ev.id];
    if (family) ++w_.counters_["event_family:" + ev.id];
    std::string line = o.log.empty() ? "" : format(pickLog(o, p.id), p, target);
    // Never silent: what the player chose is on the record, even where the data writes no line for it.
    const bool bare = line.empty() && asked_ && ev.options.size() > 1;
    if (bare) line = choseLine(p, target, optionText(o, p, target));
    if (!line.empty()) {
        if (family) {
            std::string choice = ev.options.size() > 1 && asked_ && !bare ? "（選擇：" + optionText(o, p, target) + "）" : "";
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
    bookAs_ = ev.title;
    const OptionDef& o = ev.options[chooseOption(ev, p, target)];
    natureStress(ev, o, p, target);
    std::string line = o.log.empty() ? "" : format(pickLog(o, p.id), p, target);
    const bool bare = line.empty() && asked_ && ev.options.size() > 1 && importance > 0;
    if (bare) line = choseLine(p, target, optionText(o, p, target));
    if (!line.empty() && importance > 0) {
        bool chose = ev.options.size() > 1 && asked_ && !bare;  // only what the player was asked is the player's choice
        w_.log(importance, "  " + line + (chose ? "（選擇：" + optionText(o, p, target) + "）" : ""));
    }
    if (!w_.pendingReason_.empty()) {
        if (!line.empty() && importance > 0) w_.log(importance, "  （" + w_.pendingReason_ + "）");
        w_.pendingReason_.clear();
    }
    apply(o.effects, p, target);
}

void EventRunner::interact(const EventDef& ev, Person& p, Id head) {
    if (!p.alive() || p.household == kNone || head == kNone) return;
    // Never silent: a visit that cannot be made says why.
    auto no = [&](const std::string& why) { w_.log(1, "〔" + ev.title + "〕沒去成：" + why); };
    // Things may have changed since the button was shown (a wake eased the whole house's grief): say what.
    if (!eval(ev.conditions, p, head))
        return no(ev.interaction["gone"].isString() ? format(ev.interaction["gone"].asString(), p, head) + "。"
                                                    : w_.shortName(p.id) + "那邊的情形變了，眼下用不著" + ev.title + "。");
    auto cd = w_.cooldowns_.find({p.id, ev.id});
    if (cd != w_.cooldowns_.end() && cd->second > w_.day_) return no("才找過" + w_.shortName(p.id) + "，過些日子再說。");
    // Sheltering from the pestilence: nobody outside the house is seen.
    if (w_.people_[head].flags.count("plague_shelter") && p.household != w_.people_[head].household)
        return no("全家躲著瘟疫，不和外人來往。");
    double money = ev.interaction.numberOr("money", 0);  // what it costs
    Household& hh = w_.households_[w_.people_[head].household];
    if (money > hh.money + 0.5) return no("錢不夠（要 " + std::to_string(int(money)) + " 便士）。");
    if (!w_.spendTime(static_cast<int>(ev.interaction.numberOr("time", 1)))) return no("這一季沒有空閒了。");
    hh.money = std::max(0.0, hh.money - money);
    w_.book(hh, ev.title, -money);
    bookAs_ = ev.title;
    // How they take it is up to them: their nature, their mood, what they feel for the head. Except when it is the
    // very thing they were hoping for: that is taken gladly.
    const WantDef* hoped = w_.defs_.want(p.want);
    bool wanted = hoped && std::find(hoped->answeredBy.begin(), hoped->answeredBy.end(), ev.id) != hoped->answeredBy.end() &&
                  optionOpen(ev.options.front(), p, head);
    int pick = wanted ? 0 : chooseOption(ev, p, head);
    const OptionDef& o = ev.options[pick];
    natureStress(ev, o, p, head);
    if (ev.cooldownMonths > 0) w_.cooldowns_[{p.id, ev.id}] = w_.day_ + ev.cooldownMonths * kDaysPerMonth;
    ++w_.counters_["interaction:" + ev.id];
    std::string line = o.log.empty() ? "" : format(pickLog(o, p.id), p, head);
    // What they talked about: their griefs and hopes, the season, their temper, the talk of the village.
    // Only when they took the visit well (the first answer of every interaction is the warm one).
    if (!line.empty() && ev.interaction["chatter"].isBool() && ev.interaction["chatter"].asBool() && &o == &ev.options.front() &&
        w_.opinion(p.id, head) > -40) {
        std::vector<double> pool;  // weight of each of ids
        std::vector<std::string> ids;
        // The family does not hear the same words twice in five years, whoever says them.
        Id ear = w_.people_[head].household;
        auto heardLately = [&](const std::string& l) {
            auto it = w_.chatterHeard_.find({ear, l});
            return it != w_.chatterHeard_.end() && w_.day_ - it->second < 5 * kDaysPerYear;
        };
        for (const auto& c : w_.defs_.chatter) {
            auto said = w_.chatterSaid_.find({p.id, c.id});
            if (c.lines.empty() || (said != w_.chatterSaid_.end() && w_.day_ - said->second < 2 * kDaysPerYear)) continue;
            if (std::find(c.notIn.begin(), c.notIn.end(), ev.id) != c.notIn.end()) continue;
            if (!eval(c.when, p, head)) continue;
            if (std::none_of(c.lines.begin(), c.lines.end(), [&](const std::string& l) { return !heardLately(l); })) continue;
            pool.push_back(c.weight);
            ids.push_back(c.id);
        }
        // Word going round that this house has heard lately, not about itself or the family.
        const News* gossip = nullptr;
        for (auto it = w_.news_.rbegin(); it != w_.news_.rend() && w_.day_ - it->day < 120; ++it)
            if (it->heard.count(p.household) && it->house != p.household && !w_.inPlayerHouse(it->house) &&
                it->subject != p.id && !(it->subject != kNone && w_.isPlayerFamily(w_.people_[it->subject])) &&
                !(it->other != kNone && (it->other == p.id || w_.isPlayerFamily(w_.people_[it->other]))) &&
                !(it->subject != kNone && w_.closeKin(p.id, it->subject)) && !(it->other != kNone && w_.closeKin(p.id, it->other))) {
                auto said = w_.chatterSaid_.find({p.id, "news:" + std::to_string(it->id)});
                if (said == w_.chatterSaid_.end()) {
                    gossip = &*it;
                    break;
                }
            }
        if (gossip) {
            pool.push_back(3);
            ids.push_back("news:" + std::to_string(gossip->id));
        }
        double total = 0;
        for (double wt : pool) total += wt;
        if (total > 0) {
            double r = w_.rngEvents_.uniform() * total;
            size_t k = 0;
            while (k + 1 < pool.size() && r >= pool[k]) r -= pool[k++];
            w_.chatterSaid_[{p.id, ids[k]}] = w_.day_;
            std::string extra;
            if (ids[k].rfind("news:", 0) == 0) {
                extra = format("{first}說，村裡在傳：", p, head) + w_.newsText(*gossip) + "。";
            } else {
                for (const auto& c : w_.defs_.chatter)
                    if (c.id == ids[k]) {
                        OptionDef tmp;
                        tmp.text = "chatter:" + c.id;
                        for (const auto& l : c.lines)
                            if (!heardLately(l)) tmp.logs.push_back(l);
                        tmp.log = tmp.logs.front();
                        const std::string& said = pickLog(tmp, p.id);
                        w_.chatterHeard_[{ear, said}] = w_.day_;
                        extra = format(said, p, head);
                        break;
                    }
            }
            if (!extra.empty()) line += extra;
        }
    }
    if (!line.empty()) w_.log(1, "〔" + ev.title + "〕" + line);
    // A cold answer says why, so the next visit can be better chosen.
    // Only when the warm answer was there to give, and not for visits that are cold by nature (a summons to court).
    if (!line.empty() && pick != 0 && w_.pendingReason_.empty() && ev.options.size() > 1 && optionOpen(ev.options.front(), p, head) &&
        !(ev.interaction["no_reason"].isBool() && ev.interaction["no_reason"].asBool())) {
        double op = w_.opinion(p.id, head);
        bool close = w_.closeness(p.id, head) >= 50 || op >= 30;
        w_.pendingReason_ = op <= -20 ? p.firstName + " 對你家有成見，好感 " + std::to_string(std::lround(op))
                            : op < 0  ? p.firstName + " 對你家沒什麼好感，好感 " + std::to_string(std::lround(op))
                            : w_.mood(p) < 35 ? p.firstName + " 這陣子心裡不痛快"
                            : close ? "看 " + p.firstName + " 的脾氣，這回就是不領情，下回也許就好了"
                                    : "看 " + p.firstName + " 的脾氣，這回就是不領情；交情再深一點會好些";
    }
    if (!w_.pendingReason_.empty()) {
        if (!line.empty()) w_.log(1, "  （" + w_.pendingReason_ + "）");
        w_.pendingReason_.clear();
    }
    apply(o.effects, p, head);
    p.soughtDay = w_.day_;
    if (p.household != w_.playerHousehold_) w_.befriend(p.id, head, 8);  // seeking someone out is how you come to know them
    // The summary says what came of it: what they were waiting for, if this was it, or how they took it.
    if (!w_.answerWant(p.id, ev.id))
        w_.seasonNotes_.push_back("你找" + w_.shortName(p.id) + "（" + ev.title + "）：" + optionText(o, p, head) + "。");
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

double EventRunner::weight(const Json& ai, const Person& p, Id target, const Person* traitsOf) const {
    if (ai.isNull()) return 10;
    double w = ai.numberOr("base", 10);
    if (ai["traits"].isObject())
        for (const auto& [t, v] : ai["traits"].asObject())
            if (w_.hasTrait(traitsOf ? *traitsOf : p, t)) w += v.asNumber();
    if (ai["if"].isArray())
        for (const auto& c : ai["if"].asArray())
            if (eval(c["cond"], p, target)) w += c["add"].asNumber();
    return std::max(0.0, w);
}

bool EventRunner::optionOpen(const OptionDef& o, const Person& p, Id target) const {
    if (!o.conditions.isNull() && !eval(o.conditions, p, target)) return false;
    // Only an option with a stated price ("（40 便士）") needs the coin in hand; tithes, fines and
    // the like are paid in kind or owed.
    Id hid = p.household != kNone ? p.household : w_.isAway(p) ? p.awayFrom : kNone;
    if (hid == kNone || !o.effects.isArray() || o.text.find("便士") == std::string::npos) return true;
    double cost = 0;
    for (const auto& e : o.effects.asArray())
        if (e.isObject() && e["money"].isNumber() && e["money"].asNumber() < 0) cost -= e["money"].asNumber();
    return cost <= w_.households_[hid].money + 0.5;
}

int EventRunner::chooseOption(const EventDef& ev, const Person& p, Id target) {
    // A child's road is the parents' call (parent_decides), in every family: their temper weighs the options and,
    // when a model decides, it decides as them. The child's own state (age, skills, flags) still sets what is open.
    const Person* decider = nullptr;
    if (ev.parentDecides && p.spouse == kNone) {
        Id hid = p.household != kNone ? p.household : w_.isAway(p) ? p.awayFrom : kNone;
        auto there = [&](Id x) { return x != kNone && x != p.id && w_.people_[x].alive() && w_.people_[x].household == hid && hid != kNone; };
        Id d = there(p.father) ? p.father : there(p.mother) ? p.mother : hid != kNone && there(w_.households_[hid].head) ? w_.households_[hid].head : kNone;
        if (d != kNone) decider = &w_.people_[d];
    }
    std::vector<double> ws;
    for (const auto& o : ev.options) ws.push_back(optionOpen(o, p, target) ? weight(o.aiWeight, p, target, decider) : 0.0);
    // Nothing affordable: fall back to the conditions alone, so the event still resolves.
    // Nothing anyone would pick: any open option, evenly; failing that (nothing affordable), the conditions alone,
    // so the event still resolves. A priced option is never chosen without the coin for it.
    if (std::all_of(ws.begin(), ws.end(), [](double v) { return v <= 0; })) {
        ws.clear();
        for (const auto& o : ev.options) ws.push_back(optionOpen(o, p, target) ? 1.0 : 0.0);
    }
    if (std::all_of(ws.begin(), ws.end(), [](double v) { return v <= 0; })) {
        ws.clear();
        for (const auto& o : ev.options)
            ws.push_back((o.conditions.isNull() || eval(o.conditions, p, target)) && o.text.find("便士") == std::string::npos ? 1.0 : 0.0);
    }
    if (std::all_of(ws.begin(), ws.end(), [](double v) { return v <= 0; })) {
        ws.clear();
        for (const auto& o : ev.options)
            ws.push_back(o.conditions.isNull() || eval(o.conditions, p, target) ? weight(o.aiWeight, p, target, decider) : 0.0);
    }
    // The one keeping the house decides for themselves, for the young children in it, and on the big roads
    // (parent_decides: school, the cloister, the levy, a match) for anyone of the house, a brother or a
    // stepchild as much as a son, at home or away from it.
    bool asks = playersToAnswer(ev, p);
    asked_ = ev.ask && asks && !(ev.routine && w_.routineByRule_);
    picked_ = false;
    // While a year is let run, an everyday moment is not a stop: the AI answers it as the family would.
    if (w_.chooser_ && ev.ask && asks && !(ev.routine && w_.routineByRule_)) {
        std::vector<int> open;
        World::ChoicePrompt prompt;
        std::vector<std::string> lines;
        prompt.title = ev.title;
        for (size_t k = 0; k < ev.options.size(); ++k) {
            const OptionDef& o = ev.options[k];
            if (ws[k] <= 0 && !optionOpen(o, p, target)) {
                if (o.conditions.isNull() || eval(o.conditions, p, target)) {
                    // What it costs against what the house has: 「✗ …（錢不夠：要 40 便士，手上 12）」.
                    double cost = 0;
                    if (o.effects.isArray())
                        for (const auto& e : o.effects.asArray())
                            if (e.isObject() && e["money"].isNumber() && e["money"].asNumber() < 0) cost -= e["money"].asNumber();
                    Id hid = p.household != kNone ? p.household : w_.isAway(p) ? p.awayFrom : kNone;
                    double have = hid != kNone ? std::floor(w_.households_[hid].money) : 0;
                    prompt.unavailable.push_back("✗ " + optionText(o, p, target) +
                                                 (cost > 0 ? "（錢不夠：要 " + std::to_string(long(std::ceil(cost))) + " 便士，手上 " + std::to_string(long(have)) + "）"
                                                           : "（錢不夠）"));
                }
                continue;
            }
            open.push_back(static_cast<int>(k));
            prompt.options.push_back(optionText(o, p, target));
            lines.push_back(format(o.logs.empty() ? o.log : o.logs[0], p, target));
        }
        const std::string* when = nullptr;  // a telling for this case (prompt_if): the first whose conditions hold
        if (ev.promptIf.isArray())
            for (const auto& c : ev.promptIf.asArray())
                if (eval(c["cond"], p, target)) {
                    when = &c["prompt"].asString();
                    break;
                }
        if (when) {
            prompt.situation = format(*when, p, target);
        } else if (!ev.prompt.empty()) {
            // Another way of telling it each time, until the house has heard them all.
            std::string said = ev.prompts.size() > 1
                                   ? ev.prompts[w_.freshVariant(p.household, ev.id, static_cast<int>(ev.prompts.size()), w_.rngEvents_)]
                                   : ev.prompt;
            // No priest in the village: a way of telling it that has him in it gives way to one without.
            bool priestHere = w_.priest_ != kNone && w_.people_[w_.priest_].alive() && w_.people_[w_.priest_].household != kNone;
            if (!priestHere && said.find("神父") != std::string::npos) {
                for (const auto& v : ev.prompts)
                    if (v.find("神父") == std::string::npos) {
                        said = v;
                        break;
                    }
            }
            prompt.situation = format(said, p, target);
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
        // fun-r9 0: a story card (one the monthly or yearly pulse turns up by chance) is put to the player at most twice
        // a generation in the same words; after that the house's ways answer it, as the AI would. What the rules, a
        // signal or a state bring (a fine falling due, a reckoning, hunger, a funeral) is always asked.
        const bool story = ev.on.isNull() && ev.interaction.isNull() && (ev.pulse == "monthly" || ev.pulse == "yearly") &&
                           ev.repeat != "free" && !ev.institution && ev.budget != "hunger";
        int& told = w_.genCards_[{w_.generation(), ev.title + "\n" + prompt.situation}];
        if ((open.size() > 1 || (!open.empty() && !prompt.unavailable.empty())) && (!story || told < 2)) {
            w_.noteCardAsked();
            ++told;
            int r = w_.chooser_(prompt);
            if (r >= 0 && r < static_cast<int>(open.size())) {
                picked_ = true;
                return open[r];
            }
        }
    }
    // A villager in scope (by default, the spotlight) may have a model decide among the options the rules allow.
    const Person& chooser = decider ? *decider : p;
    if (!asks && ev.ask && w_.npcInScope(chooser.id)) {
        std::vector<int> open;
        std::vector<std::string> texts;
        std::vector<double> weights;
        for (size_t k = 0; k < ev.options.size(); ++k)
            if (ws[k] > 0) {
                open.push_back(static_cast<int>(k));
                texts.push_back(optionText(ev.options[k], p, target));
                weights.push_back(ws[k]);
            }
        if (open.size() > 1) {
            std::string sit = ev.prompt.empty() ? ev.title : format(ev.prompt, p, target);
            int r = w_.npcDecide(chooser, ev.title, sit, texts, weights, {decider ? p.id : kNone, target});
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
    // Someone living away still counts on the family's purse: money and the like are the house they belong to.
    Id hid = p.household != kNone ? p.household : w.isAway(p) ? p.awayFrom : kNone;
    const Household* h = hid != kNone ? &w.households_[hid] : nullptr;
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
    // Days of bread left in p's house; and whether the target's house could spare v days of it for p's (keeping four months).
    if (k == "food_days_lt") return h && h->food < w.consumption(*h) * v.asNumber();
    if (k == "target_spares_food") {
        if (!h || target == kNone || w.people_[target].household == kNone) return false;
        const Household& th = w.households_[w.people_[target].household];
        return (th.food - w.consumption(th) * 120 >= w.consumption(*h) * v.asNumber());
    }
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
    // "own": true leaves out a remarried widow's grief for the spouse she had before.
    auto hasConcern = [&](const Person& who, const Json& c, Id other) {
        std::string kind = c.isString() ? c.asString() : c.stringOr("kind", "");
        std::string about = c.isObject() ? c.stringOr("about", "any") : "any";
        double min = c.isObject() ? c.numberOr("min", 5) : 5;
        bool own = c.isObject() && c["own"].isBool() && c["own"].asBool();
        for (const auto& x : who.concerns)
            if (x.kind == kind && x.strength >= min && (about == "any" || x.about == other) &&
                !(own && aboutFormerSpouse(w.people_, who, x)))
                return true;
        return false;
    };
    if (k == "concern") return hasConcern(p, v, target);
    if (k == "no_concern") return !hasConcern(p, v, target);
    if (k == "target_concern") return target != kNone && hasConcern(w.people_[target], v, p.id);
    if (k == "child_of_target") return target != kNone && (p.father == target || p.mother == target) == v.asBool();
    if (k == "parent_of_target") return target != kNone && (w.people_[target].father == p.id || w.people_[target].mother == p.id) == v.asBool();
    if (k == "poorer_than_target") {
        // Standing as a family weighs it in a match: land, savings, name (the same yardstick as an ask for a match).
        if (target == kNone || !h || w.people_[target].household == kNone) return false;
        const Household& th = w.households_[w.people_[target].household];
        double ours = h->acres + h->money / 30 + (h->head != kNone ? w.people_[h->head].reputation : 0), theirs = th.acres + th.money / 30 + (th.head != kNone ? w.people_[th.head].reputation : 0);
        return (ours < theirs * 0.8) == v.asBool();
    }
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
    if (k == "house_boy") {
        // A boy the house could send down a road (design/fun-r6.md D): the head's own sons, or the house's blood under its
        // roof (a stepson, a younger brother), never the heir, who stays for the land; at home, in service, or away and
        // coming back. The same boys the plans that serve it can pick (kin_at_home).
        if (p.household == kNone) return false;
        const Household& hh = w.households_[p.household];
        Id heir = hh.head != kNone ? w.heirOf(hh, hh.head) : kNone;
        for (const auto& q : w.people_) {
            if (!q.alive() || q.sex != Sex::Male || q.id == p.id || q.id == heir || q.id == hh.ward) continue;
            bool mine = std::find(p.children.begin(), p.children.end(), q.id) != p.children.end();
            if (!mine && (hh.dynasty == kNone || q.dynasty != hh.dynasty)) continue;
            bool here = (q.household == hh.id && q.home == kNone) || q.home == hh.id || (w.isAway(q) && q.awayFrom == hh.id);
            if (here && eval(v, q, p.id)) return true;
        }
        return false;
    }
    if (k == "any_descendant") {
        // A living child or grandchild, under the roof or away (in service, at the monastery, in town).
        for (Id c : p.children) {
            const Person& cp = w.people_[c];
            if (cp.alive() && eval(v, cp, p.id)) return true;
            for (Id g : cp.children)
                if (w.people_[g].alive() && eval(v, w.people_[g], p.id)) return true;
        }
        return false;
    }
    if (k == "ambition") return p.ambition == v.asString();
    if (k == "reeve") return (p.flags.count("reeve") > 0) == v.asBool();
    if (k == "opposite_sex") return target != kNone && (w.people_[target].sex != p.sex) == v.asBool();
    if (k == "not_kin") return target != kNone && (!w.closeKin(p.id, target)) == v.asBool();
    // Kin by blood or marriage, the in-laws too: one does not become sworn friends with one's father-in-law.
    if (k == "kin_tie") return target != kNone && (w.closeKin(p.id, target) || !w.kinTie(p.id, target).empty()) == v.asBool();
    if (k == "gentle") return (w.cfg_.harshness == Harshness::Gentle) == v.asBool();
    // Someone of the family, other than the head and spouse, lives at home to inherit.
    if (k == "heir_at_home") {
        // Someone of the blood to carry the family on, wherever they live: a son at home or with a house of his own,
        // a son's son, a nephew (as inheritance and followSurvivingBranch find them). A grandson by a daughter
        // carries another name, and a wife married in is not the blood.
        bool any = h && h->ward != kNone && w.people_[h->ward].alive();  // held for a child: the child is the heir
        if (h && p.dynasty != kNone)
            for (const auto& q : w.people_) {
                if (!q.alive() || q.id == p.id || q.dynasty != p.dynasty || (q.household == kNone && !w.isAway(q))) continue;
                // Not one who can never come back for the land (heirOf, onHeadDied): a priest, a monk or a nun, or one
                // settled away for good who is not this head's own child.
                const StationDef* qs = w.defs_.station(q.station);
                if (q.occupation == "priest" || q.station == "priest_away" || q.station == "monastery" || q.station == "nunnery") continue;
                if (w.isAway(q) && qs && qs->lasting && q.father != p.id && q.mother != p.id) continue;
                bool blood = (q.father != kNone && w.people_[q.father].dynasty == p.dynasty) ||
                             (q.mother != kNone && w.people_[q.mother].dynasty == p.dynasty);
                if (blood && q.spouse != p.id) any = true;
            }
        return any == v.asBool();
    }
    if (k == "deed_noted") return deed_ != kNone && w.deedNotes_.count({deed_, v.asString()}) > 0;
    if (k == "heir_apparent") return (h && h->head != kNone && h->head != p.id && w.heirOf(*h, h->head) == p.id) == v.asBool();
    if (k == "has_lover") return (p.lover != kNone) == v.asBool();
    if (k == "is_lord") return (p.id == w.lord_) == v.asBool();  // the lord himself, not his sons or a steward
    if (k == "deed_with_here") {  // the other party of the affair this event is about is still about
        Id o = deed_ != kNone ? w.deeds_[deed_].with : kNone;
        return (o != kNone && w.people_[o].alive() && w.people_[o].household != kNone) == v.asBool();
    }
    if (k == "deed_with_lord") return (deed_ != kNone && w.deeds_[deed_].with != kNone && w.deeds_[deed_].with == w.lord_) == v.asBool();
    if (k == "died_suddenly") {  // on the dead: an accident, childbed or a sword, not a sickness that came on slowly
        const std::string& c = p.deathCause;
        return (!p.alive() && (c.rfind("意外", 0) == 0 || c == "難產" || c.find("戰死") != std::string::npos)) == v.asBool();
    }
    if (k == "married_years_ge" || k == "married_years_lt") {
        // Since the wedding (the "wed:<day>" flag marry() sets); for older marriages, since the eldest child came.
        int wed = -1;
        for (const auto& f : p.flags)
            if (f.rfind("wed:", 0) == 0) wed = std::stoi(f.substr(4));
        if (wed < 0)
            for (Id c : p.children)
                if (p.spouse != kNone && (w.people_[c].father == p.spouse || w.people_[c].mother == p.spouse) && (wed < 0 || w.people_[c].birthDay < wed))
                    wed = w.people_[c].birthDay - kDaysPerYear;
        int end = p.spouse != kNone && !w.people_[p.spouse].alive() ? w.people_[p.spouse].deathDay : w.day_;
        double years = wed < 0 ? 10 : (end - wed) / double(kDaysPerYear);
        return k == "married_years_ge" ? years >= v.asNumber() : years < v.asNumber();
    }
    if (k == "kin_boy_at_home") return (w.boyForMaster(p) != kNone) == v.asBool();
    if (k == "hired") return (h && h->hiredUntil > w.day_) == v.asBool();
    if (k == "hire_master") return (h && w.hireMaster(h->id) != kNone) == v.asBool();
    if (k == "would_sell") return h && !w.sellReason(*h).empty() == v.asBool();  // a house that would let a few acres go
    if (k == "money_covers_debt") return h && h->debt > 0.5 && (h->money + 0.5 >= h->debt) == v.asBool();
    if (k == "owes_lord_ge") return h && w.owedLord(*h) >= v.asNumber();
    if (k == "demesne_free") return (w.leasedOut() < 0.5) == v.asBool();  // nobody holds the lord's fields on lease
    if (k == "mill_free") return std::none_of(w.households_.begin(), w.households_.end(), [&](const Household& o) { return !o.extinct && o.millUntil > w.day_; }) == v.asBool();
    if (k == "younger_son_wed") {
        // A married son under the roof who is not the heir: one the family could set up on his own.
        bool any = false;
        if (h)
            for (Id m : h->members) {
                const Person& q = w.people_[m];
                any = any || (q.alive() && q.father == p.id && q.sex == Sex::Male && q.spouse != kNone && w.people_[q.spouse].alive() &&
                              m != h->head && w.heirOf(*h, h->head) != m && q.home == kNone);
            }
        return any == v.asBool();
    }
    if (k == "has_secret") {
        // Something this person did in secret that `to` (the target, or nobody: anyone) is not yet sure of.
        Id to = v.asString() == "target" ? target : kNone;
        return w.hasUntoldSecret(p.id, to);
    }
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
    // p's house is the manor (its debts are paid on the steward's terms, not sued for or collected).
    if (k == "manor") return (h && w.lord_ != kNone && w.people_[w.lord_].household == h->id) == v.asBool();
    if (k == "owes_target") {  // this house owes the target (a loan, an old debt) money
        bool owes = h && target != kNone && ((h->debt > 0.5 && h->creditor == target) || (h->heldDebt > 0.5 && h->heldCreditor == target));
        return owes == v.asBool();
    }
    if (k == "lends_to_target") {  // p's house is owed money by the target's house now (a neighbour's loan, not the manor's)
        bool lends = false;
        if (h && target != kNone && w.people_[target].household != kNone) {
            const Household& th = w.households_[w.people_[target].household];
            auto ours = [&](Id c) { return c != kNone && c != w.lord_ && w.people_[c].household == h->id; };
            lends = (th.debt > 0.5 && ours(th.creditor)) || (th.heldDebt > 0.5 && ours(th.heldCreditor));
        }
        return lends == v.asBool();
    }
    if (k == "debt_ge") return h && std::floor(w.owedAll(h->id) + 1e-9) >= v.asNumber();  // as the header counts it
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
    if (k == "service_child") return h && (serviceChild(*h) != kNone && serviceMaster(*h) != kNone) == v.asBool();
    // The contested ledger (contest.cpp): a place free with a master of that trade, a servant's place in the
    // person's house, and the entry fine.
    if (k == "place_open") return w.openApprenticeship(v.asString(), p.household) != kNone;
    if (k == "any_place") return p.household != kNone && w.openPlace(v.asString(), p.household) != kNone;
    if (k == "service_place") return p.household != kNone && w.placeOpen(p.household, "service") == v.asBool();
    if (k == "fine_extended") {
        const EntryFine* f = nullptr;
        for (const auto& x : w.fines_)
            if (x.house == p.household) f = &x;
        return f && f->extended == v.asBool();
    }
    if (k == "fine_lender") {
        double need = 0;
        for (const auto& x : w.fines_)
            if (x.house == p.household && h) need = std::max(0.0, x.amount - h->money);
        return h && (w.fineLender(*h, need) != kNone) == v.asBool();
    }
    if (k == "stress_ge") return p.stress >= v.asNumber();
    if (k == "stress_lt") return p.stress < v.asNumber();
    if (k == "same_class") return target != kNone && (w.gentry(p.id) == w.gentry(target)) == v.asBool();
    if (k == "other_dynasty")
        return target != kNone && (p.dynasty == kNone || p.dynasty != w.people_[target].dynasty) == v.asBool();
    if (k == "sworn_friends_lt") return w.swornFriends(p.id) < v.asNumber();
    if (k == "has_plan") return (w.activePlan(p.id) != nullptr) == v.asBool();
    if (k == "marriageable") return w.marriageable(p) == v.asBool();
    if (k == "match_pending") {
        // Someone is already arranging this person's marriage.
        bool pending = false;
        for (const auto& ap : w.plans_)
            if ((ap.subject == p.id || ap.target == p.id) && ap.plan == "arrange_match") pending = true;
        return pending == v.asBool();
    }
    if (k == "vacant_acres_ge") return w.vacantAcres() >= v.asNumber();
    if (k == "village_craft") {
        // Someone else in the village keeps a craft shop a child could run errands in (an apprentice has his master's).
        return (p.station == "apprentice" || w.villageCraft(p.household)) == v.asBool();
    }
    if (k == "village_priest") {
        // A priest lives in the village to teach a child his letters (the first step of the Church roads).
        bool has = w.priest_ != kNone && w.people_[w.priest_].alive() && w.people_[w.priest_].household != kNone;
        return has == v.asBool();
    }
    if (k == "plague_active") return (w.param("plague_monthly", 0) > 0) == v.asBool();
    if (k == "household_size_ge") return h && h->members.size() >= v.asNumber();
    if (k == "remembers") return remembers(v, p, target);
    if (k == "free") {
        // For the one holding a house, the house's freedom (a stepfather keeping it for a free-born heir).
        // And for the blood of a free house: the brothers and sons of a freed line are free with it.
        bool f = p.free;
        if (p.household != kNone) {
            const Household& ph = w.households_[p.household];
            Id line = w.lineHead(ph);
            if (ph.head == p.id) f = w.houseFree(ph);
            else if (!f && line != kNone && p.dynasty == w.people_[line].dynasty && w.houseFree(ph)) f = true;
        }
        return f == v.asBool();
    }
    if (k == "deed_tried") return deed_ != kNone && w.deedTried(deed_) == v.asBool();
    if (k == "unsolved_secret") return (w.unsolvedDeed(p.id) != kNone) == v.asBool();
    if (k == "in_service") return (p.home != kNone) == v.asBool();
    if (k == "gentry") return w.gentry(p.id) == v.asBool();  // the lord's family
    // 處境 (design/life-paths.md): "home" is the empty station.
    if (k == "station") {
        std::string st = p.station.empty() ? "home" : p.station;
        if (v.isString()) return st == v.asString();
        for (const auto& x : v.asArray())
            if (st == x.asString()) return true;
        return false;
    }
    if (k == "station_years_ge") return w.day_ - p.stationSince >= v.asNumber() * kDaysPerYear;
    if (k == "station_due") return (p.stationUntil >= 0 && w.day_ >= p.stationUntil) == v.asBool();
    if (k == "away") return w.isAway(p) == v.asBool();
    if (k == "walked") {
        // Has been at this station before, at any point of their road.
        for (const auto& r : p.road)
            if (r.station == v.asString()) return true;
        return false;
    }
    // The family ladder.
    if (k == "standing") return h && w.standing(h->id) == static_cast<int>(v.asNumber());
    if (k == "standing_ge") return h && w.standing(h->id) >= v.asNumber();
    if (k == "standing_lt") return h && w.standing(h->id) >= 0 && w.standing(h->id) < v.asNumber();
    if (k == "freehold_ge") return h && h->freehold >= v.asNumber();
    if (k == "customary_ge") return h && h->acres - h->freehold >= v.asNumber();
    if (k == "kin_in_church") return w.kinInChurch(p.dynasty) == v.asBool();
    if (k == "kin_in_town") return w.kinInTown(p.dynasty) == v.asBool();
    if (k == "kin_priest" || k == "kin_town_master") {
        // Someone of the blood ordained, or a master with his own shop in town.
        bool any = false;
        for (const auto& q : w.people_)
            if (q.alive() && p.dynasty != kNone && q.dynasty == p.dynasty &&
                (k == "kin_priest" ? q.occupation == "priest" : q.station == "town_master")) any = true;
        return any == v.asBool();
    }
    if (k == "kin_station") {
        // Someone of the blood, alive, on one of these roads (a clerk who can be ordained, a lad learning a trade in town).
        std::set<std::string> ids;
        if (v.isArray())
            for (const auto& x : v.asArray()) ids.insert(x.asString());
        else
            ids.insert(v.asString());
        for (const auto& q : w.people_)
            if (q.alive() && p.dynasty != kNone && q.dynasty == p.dynasty && ids.count(q.station)) return true;
        return false;
    }
    if (k == "lord_opinion_ge")
        return w.lord_ != kNone && w.people_[w.lord_].alive() && w.lord_ != p.id && w.opinion(w.lord_, p.id) >= v.asNumber();
    // The later rungs (design/fun-r5.md D): goals counted from what the house had when the ambition was taken up,
    // the chantry priced by the house's standing, the lead in land, the posts the blood holds, the lord's family.
    if (k == "money_gain_ge") return h && h->money >= p.ambitionBase.money + w.ambitionGain(p, "money", v);
    if (k == "acres_gain_ge") return h && h->acres >= p.ambitionBase.acres + w.ambitionGain(p, "acres", v);
    if (k == "freehold_gain_ge") return h && h->freehold >= p.ambitionBase.freehold + w.ambitionGain(p, "freehold", v);
    if (k == "money_ge_chantry") return h && h->money >= w.chantryPrice(h->id) * v.asNumber();
    if (k == "acres_lead_ge") return h && w.acresLead(h->id) >= v.asNumber();
    if (k == "lords_debtor") {  // begged the lord to forgive a debt (debt_reckoning): no reeve for ten years
        auto c = w.cooldowns_.find({p.id, "lords_debtor"});
        return (c != w.cooldowns_.end() && c->second > w.day_) == v.asBool();
    }
    if (k == "manor_owes_ge") return h && w.manorBond_.house == h->id && w.manorBond_.owed >= v.asNumber();
    if (k == "family_offices_ge") return w.familyOffices(p.dynasty) >= v.asNumber();
    if (k == "reeve_terms_ge") return w.reeveTerms(p.dynasty) >= v.asNumber();
    if (k == "gentry_single") return w.gentrySingle() == v.asBool();
    if (k == "gentry_kin") {  // someone of the blood is married into the lord's family
        bool any = false;
        Id lordDyn = w.lord_ != kNone ? w.people_[w.lord_].dynasty : kNone;
        for (const auto& q : w.people_)
            if (q.alive() && p.dynasty != kNone && q.dynasty == p.dynasty && q.spouse != kNone && w.people_[q.spouse].alive() &&
                lordDyn != kNone && w.people_[q.spouse].dynasty == lordDyn && q.dynasty != lordDyn)
                any = true;
        return any == v.asBool();
    }
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
            Id hid = p.household != kNone ? p.household : w.isAway(p) ? p.awayFrom : kNone;  // away: the family's house
            Household* h = hid != kNone ? &w.households_[hid] : nullptr;
            if (k == "add_trait") {
                const TraitDef* td = w.defs_.trait(v.asString());
                if (!td) throw JsonError("unknown trait " + v.asString());
                if (!w.hasTrait(p, td->id)) {
                    if (!td->opposite.empty())
                        p.traits.erase(std::remove(p.traits.begin(), p.traits.end(), td->opposite), p.traits.end());
                    p.traits.push_back(td->id);
                    // Learning to read, coming back from the wars: turns in a life worth telling at the end of it.
                    if (td->id == "literate") w.noteLife(p.id, "學會了讀寫");
                    else if (td->id == "veteran") w.noteLife(p.id, "打過仗回來");
                }
            } else if (k == "remove_trait") {
                p.traits.erase(std::remove(p.traits.begin(), p.traits.end(), v.asString()), p.traits.end());
            } else if (k == "trait_chance") {
                if (w.rngEvents_.chance(v["chance"].asNumber() / 100.0)) {
                    Json one = Json::parse("[{\"add_trait\":\"" + v["trait"].asString() + "\"}]");
                    apply(one, p, target);
                }
            } else if (k == "book_as") {
                bookAs_ = v.asString();  // {"book_as": "折價賣貨"}: what the money after it is booked under
            } else if (k == "money") {
                if (h) {
                    double before = h->money;
                    h->money = std::max(0.0, h->money + v.asNumber());
                    std::string as = bookAs_;
                    if (as.empty() && plan_) {
                        const PlanDef* pd = w.defs_.plan(plan_->plan);
                        if (pd) as = pd->name;
                    }
                    w.book(*h, as.empty() ? "各種事情" : as, h->money - before);
                }
            } else if (k == "food") {
                if (h) h->food = std::max(0.0, h->food + v.asNumber());
            } else if (k == "firewood") {
                if (h) h->firewood = std::max(0.0, h->firewood + v.asNumber());
            } else if (k == "health") {
                p.health = std::clamp(p.health + v.asNumber(), 0.0, 100.0);
            } else if (k == "reputation") {
                w.addReputation(p.id, v.asNumber(), bookAs_.empty() ? "各種事情" : "〔" + bookAs_ + "〕");
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
                // back_mod: how the other side remembers it ("found_out" for the one whose affair was confronted).
                if (k != "opinion") w.addOpinion(target, p.id, v.stringOr("back_mod", mod), val, years);
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
                w.moveLand(h->id, kNone, give, LandPick::CustomaryOnly, "交回領主");
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
                const OccupationDef* od = w.defs_.occupation(v.asString());
                if (!od) throw JsonError("unknown occupation " + v.asString());
                // A new trade is a turn in the life (開始賣酒, 當上書記), unless a change of station alongside it says so
                // already ("學成回村當織工"); going back to the plough is not one.
                bool told = std::any_of(effects.asArray().begin(), effects.asArray().end(), [](const Json& x) { return x.has("station"); });
                if (p.occupation != od->id && od->id != "peasant" && od->id != "housewife" && !told) w.noteLife(p.id, "當了" + od->name);
                p.occupation = od->id;
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
                if (d > 0) w.moveLand(kNone, h->id, d, LandPick::CustomaryFirst, "向領主租來");
                else w.moveLand(h->id, kNone, -d, LandPick::CustomaryFirst, "交回領主");
            } else if (k == "house") {
                if (!h) continue;
                if (h->head != kNone && h->house != v.asString() && (v.asString() == "stone" || v.asString() == "timber"))
                    w.noteLife(h->head, v.asString() == "stone" ? "蓋起了石屋" : "蓋起了木屋");
                h->house = v.asString();
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
                w.makeFree(*h, v.asNumber());
                if (h->head != kNone) w.noteLife(h->head, "把 " + std::to_string(int(v.asNumber())) + " 畝地買成了自由地");
            } else if (k == "to_town") {
                // Off to a master in town: the family's town branch.
                if (!h) continue;
                w.setStation(p.id, "town_apprentice", 7, "進城拜師");
                return;
            } else if (k == "yield") {
                // A side road takes days from the household's own fields this year (or adds hands to them).
                Id yh = p.household != kNone ? p.household : w.isAway(p) ? p.awayFrom : kNone;
                if (yh != kNone) w.households_[yh].focusYield += v.asNumber();  // capped at the harvest, with the foci
            } else if (k == "focus") {
                p.focus = v.asString();  // a choice at a gate sets where the days go (跟神父認字 -> letters)
            } else if (k == "station") {
                // { "station": "archer" } or { "station": { "id": "archer", "years": [2, 4], "note": "應徵出征" } }
                std::string id = v.isString() ? v.asString() : v["id"].asString();
                int years = 0;
                if (v.isObject() && v["years"].isNumber()) years = static_cast<int>(v["years"].asNumber());
                if (v.isObject() && v["years"].isArray())
                    years = w.rngEvents_.range(static_cast<int>(v["years"][0].asNumber()), static_cast<int>(v["years"][1].asNumber()));
                w.setStation(p.id, id, years, v.isObject() ? v.stringOr("note", "") : "");
            } else if (k == "send_home") {
                // Money from someone away (or in service) to the house they belong to.
                Id to = w.isAway(p) ? p.awayFrom : p.home != kNone ? p.home : p.household;
                if (to == kNone || w.households_[to].extinct) continue;
                w.households_[to].money += v.asNumber();
                w.book(w.households_[to], "在外的家人捎回", v.asNumber());
            } else if (k == "odds") {
                // A try whose chance comes from who they are (design/life-paths.md): "effects" on success, "else" not.
                bool hit = w.rngEvents_.chance(odds(v, p, target) / 100.0);
                apply(hit ? v["effects"] : v["else"], p, target);
                if (!p.alive()) return;
            } else if (k == "leave_village") {
                if (!h) continue;
                h->members.erase(std::remove(h->members.begin(), h->members.end(), p.id), h->members.end());
                p.household = kNone;
                p.flags.insert(v.asString());
                if (h->head == p.id) w.afterSuccession(*h, p.id, w.onHeadDied(*h, p.id));
                return;
            } else if (k == "marry_target") {
                if (target == kNone) continue;
                Person& t = w.people_[target];
                if (p.sex == t.sex || w.closeKin(p.id, t.id) || !w.mayWed(p.id, t.id)) continue;
                auto married = [&](const Person& x) { return x.spouse != kNone && w.people_[x.spouse].alive(); };
                if (married(p) || married(t)) continue;
                Id man = p.sex == Sex::Male ? p.id : t.id;
                Id woman = p.sex == Sex::Male ? t.id : p.id;
                w.marry(man, woman);
            } else if (k == "target_owes_favour") {
                // {"weight": N, "why": "..."}: the target now owes p a favour (the currency the grain cards run on).
                if (target != kNone) w.oweFavour(target, p.id, v.numberOr("weight", 6), v.stringOr("why", "幫過他家"));
            } else if (k == "use_up_memory") {
                // A favour called in is spent: the holder's good memories of the other's family that the "remembers"
                // condition with the same keys would count are gone.
                const bool targetHolds = v.stringOr("holder", "self") == "target";
                const Id holder = targetHolds ? target : p.id, other = targetHolds ? p.id : target;
                if (holder == kNone || other == kNone) continue;
                const std::string type = v.stringOr("type", ""), tag = v.stringOr("tag", "");
                const Id otherDyn = w.people_[other].dynasty;
                auto& mems = w.memories_[holder];
                mems.erase(std::remove_if(mems.begin(), mems.end(),
                                          [&](const Memory& m) {
                                              if (m.deed == kNone && type.empty() && tag.empty()) return false;
                                              if (!type.empty() && m.label != type) return false;
                                              if (!tag.empty()) {
                                                  const DeedDef* d = w.defs_.deed(m.label);
                                                  if (!d || std::find(d->tags.begin(), d->tags.end(), tag) == d->tags.end()) return false;
                                              }
                                              const bool them = m.about == other || (otherDyn != kNone && (m.aboutDynasty == otherDyn ||
                                                                                                          (m.about != kNone && w.people_[m.about].dynasty == otherDyn)));
                                              return them && w.memoryValue(m) > 0;
                                          }),
                           mems.end());
            } else if (k == "random") {
                // "opinion": percent points per point of the target's opinion of p (成不成看他心情).
                double pct = v["percent"].asNumber();
                if (v.has("opinion") && target != kNone) pct = std::clamp(pct + w.opinion(target, p.id) * v.numberOr("opinion", 0), 5.0, 95.0);
                bool hit = w.rngEvents_.chance(pct / 100.0);
                apply(hit ? v["effects"] : v["else"], p, target);
                if (!p.alive()) return;
            } else if (k == "progress") {
                if (plan_) plan_->progress += v.asNumber();
            } else if (k == "raise_price") {
                // The buyer bids more for the land (outbid by another house): paid to the seller when the deal is struck.
                if (plan_) plan_->priceAdd += v.asNumber();
            } else if (k == "abort_plan") {
                if (plan_) plan_->aborted = true;  // ends quietly at month end: called off, not refused
            } else if (k == "subject") {
                if (plan_ && plan_->subject != kNone) apply(v, w.people_[plan_->subject], target);
            } else if (k == "marry_subject_target") {
                if (!plan_ || plan_->subject == kNone || target == kNone) continue;
                Person& sub = w.people_[plan_->subject];
                Json one = Json::parse("[{\"marry_target\": true}]");
                apply(one, sub, target);
            } else if (k == "pay_tax") {
                // The king's fifteenth, or the share of it the house owns up to: {"pay_tax": 0.5}
                if (!h) continue;
                double pay = std::min(std::round(w.taxDue(*h) * v.asNumber()), std::max(0.0, h->money));
                h->money -= pay;
                w.book(*h, "國王的稅", -pay);
            } else if (k == "take_acres") {
                // Buy land from the target's household: the parcel this house was after, if it is theirs
                // ("wanted": false for a set lot, as when the seller names it).
                if (!h || target == kNone || w.people_[target].household == kNone) continue;
                Household& th = w.households_[w.people_[target].household];
                World::LandDeal d = w.landDeal(*h, th, v["acres"].asNumber(), v["price"].asNumber(), !(v["wanted"].isBool() && !v["wanted"].asBool()),
                                               plan_ ? plan_->priceAdd : 0);
                if (!d.ok) continue;
                double price = d.price;
                if (d.parcel != kNone) {
                    std::string name = w.parcelName(w.parcels_[d.parcel]);
                    w.moveParcel(d.parcel, h->id, "買來");
                    if (h->id == w.playerHousehold_ || th.id == w.playerHousehold_)
                        w.log(1, "  " + name + "換了 " + std::to_string(static_cast<long>(price)) + " 便士。");
                } else {
                    double moved = w.moveLand(th.id, h->id, d.acres, LandPick::CustomaryFirst, "買來");
                    price = std::round(price * moved / d.acres);
                }
                th.money += price;
                w.book(th, "賣地", price);
                h->money -= price;
                w.book(*h, "買地", -price);
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
                np.flags.insert("designated_heir");  // brought in to carry the land: ahead of the widow (life.cpp heirOf)
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
            } else if (k == "target_lord_debt") {
                // What the target's house owes the manor comes down: paid by the lord's man out of his own purse
                // ("payer": "actor"), in kind straight to the manor ("lord"), or only written off in the book ("none").
                if (target == kNone || w.people_[target].household == kNone) continue;
                Household& th = w.households_[w.people_[target].household];
                const std::string payer = v.stringOr("payer", "lord");
                double amount = std::min(v.numberOr("reduce", 0), w.owedLord(th));
                if (payer == "actor" && h) amount = std::min(amount, std::max(0.0, h->money));
                amount = w.reduceLordDebt(th, amount);
                if (payer == "none") writtenOff_ = amount;
                if (amount < 0.5) continue;
                Id lordHouse = w.lord_ != kNone ? w.people_[w.lord_].household : kNone;
                if (payer == "actor" && h) {
                    h->money -= amount;
                    w.book(*h, "替人墊租", -amount);
                }
                if (payer != "none" && lordHouse != kNone) {
                    w.households_[lordHouse].money += amount;
                    w.book(w.households_[lordHouse], "收欠租", amount);
                }
                if (h && (h->id == w.playerHousehold_ || th.id == w.playerHousehold_))
                    w.log(1, "  " + w.fullName(target) + " 家欠莊園的帳" + (payer == "none" ? "在帳本上" : "") + "少了 " + std::to_string(int(amount)) +
                                 " 便士（還欠 " + std::to_string(int(w.owedLord(th))) + "）。");
            } else if (k == "target_owes_lord") {
                // The write-off found out: what came off the book goes back on it.
                if (target == kNone || w.people_[target].household == kNone || writtenOff_ < 0.5) continue;
                w.addDebt(w.households_[w.people_[target].household], writtenOff_, w.lord_);
                writtenOff_ = 0;
            } else if (k == "hook_payment") {
                // Money pressed out of the target with what p knows: hush money (a number), or amends ({amount, label: "賠償"}),
                // up to the amount or what the target's house can spare.
                if (!h || target == kNone || w.people_[target].household == kNone) continue;
                Household& th = w.households_[w.people_[target].household];
                const bool amends = v.isObject() && v.stringOr("label", "") == "賠償";
                double amount = std::round(std::min(v.isObject() ? v.numberOr("amount", 40) : v.asNumber(), std::max(0.0, th.money * 0.4)));
                const bool seen = h->id == w.playerHousehold_ || th.id == w.playerHousehold_;
                if (amount < 1) {
                    if (seen) w.log(1, "  " + w.fullName(target) + " 家裡實在拿不出錢來。");
                    continue;
                }
                th.money -= amount;
                w.book(th, amends ? "賠人家的錢" : "封口費", -amount);
                h->money += amount;
                w.book(*h, amends ? "人家賠的錢" : "封口費", amount);
                if (seen)
                    w.log(1, "  " + w.fullName(target) + (amends ? " 咬著牙賠了 " : " 付了 ") + std::to_string(int(amount)) + " 便士" + (amends ? "。" : "封口費。"));
            } else if (k == "owe_target") {
                // p's house owes the target money (an old loan, a debt from the father's time).
                if (h && target != kNone) w.addDebt(*h, v.asNumber(), target);  // a second debt waits its turn
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
                    w.book(*h, "酒錢", -amount);
                    continue;
                }
                double owed = amount - h->money;
                w.book(*h, "酒錢", -h->money);
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
                w.book(*h, "賠錢了事", -amount);
                w.households_[w.people_[target].household].money += amount;
                w.book(w.households_[w.people_[target].household], "人家賠的錢", amount);
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
            } else if (k == "rematch") {
                // Straight to another matchmaker for the one just turned down: no waiting out the usual time.
                auto it = w.lastMatchSubject_.find(p.id);
                if (it == w.lastMatchSubject_.end() || !w.people_[it->second].alive()) continue;
                w.cooldowns_.erase({it->second, "plan:arrange_match"});
                if (w.inPlayerHouse(p.household)) {
                    bool open = false;
                    for (const auto& op : w.availablePlans(p.id)) open = open || (op.plan == "arrange_match" && op.subject == it->second);
                    const PlanDef* pd = w.defs_.plan("arrange_match");
                    std::string why = open || !pd ? "" : w.planBlock(p.id, *pd);
                    w.log(1, "  （" + w.people_[it->second].firstName + " 的親事不用再等了" +
                                 (open ? "，計畫裡的「說親」可以另找人家。）" : "，可是眼下說不成：" + why + "。）"));
                }
            } else if (k == "feed_from_target") {
                // The target's house sends p's v days of bread from its own store.
                if (!h || target == kNone || w.people_[target].household == kNone) continue;
                Household& th = w.households_[w.people_[target].household];
                double amount = std::min(w.consumption(*h) * v.asNumber(), std::max(0.0, th.food - w.consumption(th) * 120));
                th.food -= amount;
                h->food += amount;
                w.bookFood(th, "送人的糧", -amount);
                w.bookFood(*h, "別人送的糧", amount);
            } else if (k == "forgive_target_debt") {
                // What p's house owes the target is written off.
                if (!h || target == kNone) continue;
                if (h->heldDebt > 0.5 && h->heldCreditor == target) h->heldDebt = 0, h->heldCreditor = kNone;
                if (h->debt > 0.5 && h->creditor == target) w.clearDebt(*h);
            } else if (k == "repay_target") {
                // Ordered to pay back what this house owes the target: that share of it, as far as the purse goes.
                if (!h || target == kNone || w.people_[target].household == kNone) continue;
                Household& th = w.households_[w.people_[target].household];
                bool first = h->debt > 0.5 && h->creditor == target;
                double& owed = first ? h->debt : h->heldDebt;
                double amount = std::min(std::round(owed * v.asNumber()), h->money);
                if (amount < 1) continue;
                h->money -= amount;
                owed -= amount;
                th.money += amount;
                w.book(*h, "法庭判還的債", -amount);
                w.book(th, "法庭討回的債", amount);
                if (owed <= 0.5) {
                    if (first) w.clearDebt(*h);
                    else h->heldDebt = 0, h->heldCreditor = kNone;
                }
                if (w.inPlayerHouse(th.id) || w.inPlayerHouse(h->id)) w.log(1, "  當庭還了 " + std::to_string(int(amount)) + " 便士" + (owed > 0.5 ? "，還欠 " + std::to_string(int(owed)) + " 便士。" : "，債清了。"));
            } else if (k == "pay_off_debt") {
                // Pay the whole debt out of what the house has, to whoever it is owed.
                if (!h || h->debt <= 0.5 || h->money + 0.5 < h->debt) continue;
                double paid = std::min(h->debt, h->money);
                h->money -= paid;
                w.book(*h, "還債", -paid);
                Id cred = h->creditor != kNone ? h->creditor : w.lord_;
                if (cred != kNone && w.people_[cred].household != kNone) {
                    // The creditor gets what was paid (half a penny short is let go, not made up from nowhere).
                    w.households_[w.people_[cred].household].money += paid;
                    w.book(w.households_[w.people_[cred].household], "收回借款", paid);
                }
                w.clearDebt(*h);
            } else if (k == "pay_debt") {
                if (h) h->debt = std::max(0.0, h->debt - v.asNumber());
            } else if (k == "sell_ox") {
                if (!h) continue;
                h->noOxUntil = w.day_ + 40 * kDaysPerYear;  // until they can buy another
                h->money += 40, w.book(*h, "賣耕牛", 40);
                // Against a debt, the 40 pence go to the creditor (not just off the books).
                if (h->debt > 0.5) w.payCreditor(*h, h->creditor, 40);
            } else if (k == "service_for_debt") {
                // A child goes to work for the creditor; the first years' wages pay the debt.
                if (!h) continue;
                Id child = serviceChild(*h);
                Id master = h->creditor != kNone && w.people_[h->creditor].alive() ? h->creditor : w.lord_;
                if (child == kNone || master == kNone || w.people_[master].household == kNone) continue;
                int years = static_cast<int>(v.numberOr("years", 3));
                if (!w.sendToService(child, w.households_[w.people_[master].household].head, years)) continue;
                // The child's years are the settlement: what is left is small enough to forgive, or the
                // creditor waits for it from the start again.
                h->debt = std::max(0.0, h->debt - v.numberOr("value", 30));
                if (h->debt < 20) w.clearDebt(*h);
                h->debtYears = 0;
                if (h->id == w.playerHousehold_)
                    w.log(1, "  " + w.fullName(child) + " 去了 " + w.fullName(master) + " 家幹活，頭 " + std::to_string(years) +
                                 " 年的工錢先拿來抵債。");
            } else if (k == "food_days") {
                if (!h) continue;
                const double before = h->food;
                h->food = std::max(0.0, h->food + w.consumption(*h) * v.asNumber());
                w.bookFood(*h, v.asNumber() >= 0 ? "得來的糧" : "糧食耗用", h->food - before);
            } else if (k == "give_food_target") {
                // p's house carries v days of its bread to the target's house: it arrives there, and both books show it.
                if (!h || target == kNone) continue;
                // The dead are out of every house: the grain goes to the family they leave (a spouse's house, else
                // their own name's).
                Id to = w.people_[target].household;
                if (to == kNone && w.people_[target].spouse != kNone && w.people_[w.people_[target].spouse].alive()) to = w.people_[w.people_[target].spouse].household;
                if (to == kNone)
                    for (const auto& o : w.households_)
                        if (!o.extinct && o.head != kNone && o.dynasty == w.people_[target].dynasty && o.id != h->id) to = o.id;
                if (to == kNone || to == h->id) continue;
                Household& th = w.households_[to];
                const double amount = std::min(w.consumption(*h) * v.asNumber(), std::max(0.0, h->food));
                h->food -= amount;
                th.food += amount;
                w.bookFood(*h, "送人的糧", -amount);
                w.bookFood(th, "別人送的糧", amount);
            } else if (k == "give_food_priest") {
                // v days of p's bread to the priest's house (school paid in grain).
                if (!h || w.priest_ == kNone || !w.people_[w.priest_].alive() || w.people_[w.priest_].household == kNone) continue;
                Household& th = w.households_[w.people_[w.priest_].household];
                if (th.id == h->id) continue;
                const double amount = std::min(w.consumption(*h) * v.asNumber(), std::max(0.0, h->food));
                h->food -= amount;
                th.food += amount;
                w.bookFood(*h, "送人的糧", -amount);
                w.bookFood(th, "別人送的糧", amount);
            } else if (k == "give_food_village") {
                // v days of p's bread shared out among the (up to three) houses with the least in their barns.
                if (!h) continue;
                Id lordHouse = w.lord_ != kNone ? w.people_[w.lord_].household : kNone;
                std::vector<std::pair<double, Id>> poor;
                for (const auto& o : w.households_)
                    if (!o.extinct && o.head != kNone && !o.members.empty() && o.id != h->id && o.id != lordHouse)
                        poor.push_back({w.foodDays(o), o.id});
                std::sort(poor.begin(), poor.end());
                if (poor.size() > 3) poor.resize(3);
                const double amount = std::min(w.consumption(*h) * v.asNumber(), std::max(0.0, h->food));
                if (poor.empty() || amount <= 0) continue;
                h->food -= amount;
                w.bookFood(*h, "送人的糧", -amount);
                for (const auto& [d, id] : poor) {
                    w.households_[id].food += amount / poor.size();
                    w.bookFood(w.households_[id], "別人送的糧", amount / poor.size());
                }
            } else if (k == "borrow_grain") {
                // A neighbour who thinks well of the family lends grain against the next harvest (economy.cpp).
                if (h) w.borrowGrain(*h, p.id, v.numberOr("days", 60), cause_);
            } else if (k == "buy_food_days") {
                // Grain from the market at its price, as many days of it as the purse covers.
                if (!h) continue;
                double price = w.param("grain_price", 0.5);
                // {"days": N, "pence": P}: no more than P pence of it (the ox's 40, not the whole purse).
                const double days = v.isObject() ? v.numberOr("days", 0) : v.asNumber();
                const double purse = v.isObject() && v.has("pence") ? std::min(h->money, v.numberOr("pence", 0)) : h->money;
                double b = std::clamp(w.consumption(*h) * days, 0.0, std::max(0.0, purse) / price);
                h->food += b;
                h->money = std::max(0.0, h->money - b * price);
                w.book(*h, "買糧", -b * price);
            } else if (k == "surrender_acres") {
                // A few customary acres back into the lord's hand against the arrears (kSurrenderCredit pence an acre):
                // a number, or "debt" for as many as clear what is owed the manor, at most a quarter of the customary land.
                if (!h) continue;
                double customary = h->acres - h->freehold;
                double n = v.isNumber() ? v.asNumber() : std::ceil(w.owedLord(*h) / World::kSurrenderCredit);
                n = std::min(n, std::floor(customary * 0.25));
                if (n < 1 || customary < 6) continue;
                double moved = w.moveLand(h->id, kNone, n, LandPick::CustomaryOnly, "交回領主");
                double off = w.reduceLordDebt(*h, moved * World::kSurrenderCredit);
                if (h->id == w.playerHousehold_)
                    w.log(1, "  把" + w.movedText() + "交還給領主，抵了 " + std::to_string(int(off)) + " 便士的欠租。");
            } else if (k == "service_for_food") {
                // One mouth fewer: a child goes to serve a better-off house for bed and board.
                if (!h) continue;
                Id child = serviceChild(*h);
                Id master = serviceMaster(*h);
                if (child == kNone || master == kNone || !w.sendToService(child, master, static_cast<int>(v.numberOr("years", 3)))) {
                    if (h->id == w.playerHousehold_) w.log(1, "  可是眼下沒有哪家肯收，孩子還是留在家裡。");
                    continue;
                }
                h->food += w.consumption(*h) * 20;
                if (h->id == w.playerHousehold_)
                    w.log(1, "  " + w.people_[child].firstName + "去了 " + w.fullName(master) + " 家，說好幹 " + std::to_string(static_cast<int>(v.numberOr("years", 3))) + " 年，管吃管住。");
            } else if (k == "borrow") {
                // A better-off neighbour pays the steward; now the debt is to them.
                if (!h || h->debt <= 0.5) continue;
                double owed = 0;
                Id lender = w.seekLender(*h, h->debt, "還莊園的欠租", owed);
                if (lender == kNone) continue;
                Household& lh = w.households_[w.people_[lender].household];
                lh.money -= h->debt;
                w.book(lh, owed > 0 ? "借錢給人" : "接濟別人", -h->debt);
                if (h->creditor != kNone && w.people_[h->creditor].household != kNone) {
                    w.households_[w.people_[h->creditor].household].money += h->debt;
                    w.book(w.households_[w.people_[h->creditor].household], "收回欠債", h->debt);
                }
                if (owed <= 0) {
                    w.clearDebt(*h);
                    continue;
                }
                h->debt = owed;
                h->creditor = lender;
                h->debtYears = 0;
                w.recordDeed("lent_money", lender, p.id, 1, cause_);
                if (h->id == w.playerHousehold_)
                    w.log(1, "  " + w.fullName(lender) + " 借了 " + std::to_string(int(h->debt)) + " 便士給 " + w.fullName(p.id) +
                                 " 還給莊園，這下欠的是" + (w.people_[lender].sex == Sex::Male ? "他" : "她") + "的人情和錢。");
            } else if (k == "debt_grace") {
                if (h) h->debtYears = 0;
            } else if (k == "debt_grace_target") {  // grace on the debt owed to the target's house only (never the manor's)
                if (h && target != kNone && h->debt > 0.5 && h->creditor != kNone && h->creditor != w.lord_ &&
                    w.people_[h->creditor].household == w.people_[target].household)
                    h->debtYears = 0;
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
            } else if (k == "ease_concern_house") {
                // Everyone in p's house who carries this concern, a little: {"kind": "grief", "by": 12}
                if (!h) continue;
                for (Id m : h->members)
                    if (m != p.id) w.easeConcern(m, v["kind"].asString(), v.numberOr("by", 12), kNone);
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
                else if (w.isPlayerFamily(p)) {
                    // Say what stands in the way: the plan's first unmet condition, or that the pair it was for is
                    // not one it can be started for now (another match already under way, one of them not free).
                    const PlanDef* pd = w.defs_.plan(plan);
                    std::string why = pd ? w.planBlock(p.id, *pd) : "";
                    if (why.empty()) {
                        bool busy = false;
                        for (const auto& ap : w.plans_)
                            if (ap.plan == plan && (ap.actor == p.id || (subj != kNone && ap.subject == subj))) busy = true;
                        why = busy ? "家裡已經在辦一樁「" + (pd ? pd->name : plan) + "」了" : "對方眼下不合適（已經許了人家、年紀或身分不合）";
                    }
                    w.log(1, "  可是眼下這件事辦不成：" + why + "。");
                }
            } else if (k == "lease_mill") {
                // {"lease_mill": {"years": 5, "fine": 120}}: the mill's farm for so many years, the fine to the manor.
                if (!h) continue;
                double fine = std::min(v.numberOr("fine", 0), std::max(0.0, h->money));
                h->money -= fine;
                w.book(*h, "包磨坊的押金", -fine);
                Id lordHouse = w.lord_ != kNone ? w.people_[w.lord_].household : kNone;
                if (lordHouse != kNone) w.households_[lordHouse].money += fine, w.book(w.households_[lordHouse], "收包磨坊的押金", fine);
                h->millUntil = w.day_ + static_cast<int>(v.numberOr("years", 5)) * kDaysPerYear;
            } else if (k == "settle_subject") {
                // {"settle_subject": {"acres": 8, "money": 30}}: the married son of the plan set up in a house of his
                // own, on land rented from the lord if any lies empty, the rest out of the family's (it keeps 8: a
                // half-yardland split between sons is how holdings came to be small; fun-r8 1).
                if (!plan_ || plan_->subject == kNone || hid == kNone) continue;
                Person& sub = w.people_[plan_->subject];
                if (sub.household != hid || w.households_[hid].head == sub.id) {
                    if (w.inPlayerHouse(hid)) w.log(1, "  " + sub.firstName + " 已經不在家裡住了，分家的事作罷。");
                    continue;
                }
                // The 分家 card (World::askSettleSon) was asked before the plan was called a success (round 10 M1):
                // here the terms it showed are carried out, with only money and vacant land if that was the choice.
                World::SettleTerms st = w.settleTerms(hid, v.numberOr("acres", 8), v.numberOr("money", 30));
                double vac = st.vac, own = st.own, gift = st.gift;
                if (w.settleNoOwn_.erase(sub.id)) own = 0;
                w.setUpSon(hid, sub.id, own, vac, gift, "分給" + sub.firstName + "成家");
            } else if (k == "lease_demesne") {
                // Take the lord's own fields on a lease of so many years.
                // One lease at a time; the deposit goes to the manor, as much of it as the house still has.
                if (!h || w.leasedOut() > 0.5) continue;
                double deposit = std::min(v.numberOr("deposit", 0), std::max(0.0, h->money));
                h->money -= deposit;
                w.book(*h, "包租押金", -deposit);
                Id lordHouse = w.lord_ != kNone ? w.people_[w.lord_].household : kNone;
                if (lordHouse != kNone) {
                    w.households_[lordHouse].money += deposit;
                    w.book(w.households_[lordHouse], "收包租押金", deposit);
                }
                h->leaseAcres = v.numberOr("acres", 30);
                h->leaseUntil = w.day_ + static_cast<int>(v.numberOr("years", 7)) * kDaysPerYear;
            } else if (k == "pay_share") {
                // A price set by what the house has, not by what was agreed: a share of its savings to the target.
                if (!h) continue;
                double amount = std::round(std::min(std::max(0.0, h->money) * v.numberOr("share", 0.2), v.numberOr("max", 1e9)));
                if (amount < 1) continue;
                h->money -= amount;
                w.book(*h, v.stringOr("label", "加價"), -amount);
                if (target != kNone && w.people_[target].household != kNone) {
                    w.households_[w.people_[target].household].money += amount;
                    w.book(w.households_[w.people_[target].household], v.stringOr("label", "加價"), amount);
                }
                if (w.isPlayerFamily(p)) w.log(1, "  多付了 " + std::to_string(int(amount)) + " 便士。");
            } else if (k == "note_life") {
                w.noteLife(p.id, format(v.asString(), p, target));  // a line in the person's road through life
            } else if (k == "kin_to_master") {
                // A master in town takes a boy of the family at home into his own shop.
                Id boy = w.boyForMaster(p);
                if (boy == kNone) continue;
                w.people_[boy].flags.insert("trade_decided");
                w.setStation(boy, "town_apprentice", 7, "去鎮上跟" + p.firstName + "學手藝");
                if (w.isPlayerFamily(p) || w.inPlayerHouse(w.people_[boy].awayFrom))
                    w.log(1, "  " + w.fullName(boy) + " 跟著趕集的人進了城，到 " + p.firstName + " 的舖子裡當學徒。");
            } else if (k == "hire_on") {
                // The grown of the house hire on with the biggest farm (or the manor) for a year: board and a wage.
                if (!h) continue;
                Id master = w.hireMaster(h->id);
                if (master == kNone) continue;
                h->hiredUntil = w.day_ + static_cast<int>(v.numberOr("months", 12)) * kDaysPerYear / 12;
                h->hiredBy = master;
                int n = w.hiredHands(*h);
                if (w.inPlayerHouse(h->id)) {
                    const Household& mh = w.households_[master];
                    std::string where = master == (w.lord_ != kNone ? w.people_[w.lord_].household : kNone) ? "莊園的自營地上" : w.fullName(mh.head) + " 家";
                    w.log(1, "  家裡 " + std::to_string(n) + " 個能幹活的人去" + where + "當長工，管吃，一個月每人 4 便士，做到明年這時候。");
                }
            } else if (k == "sell_grain") {
                // {"sell_grain": 90}: up to that many days of the house's bread carted to the town market (it keeps
                // enough to see it past the next harvest).
                if (!h) continue;
                double got = w.sellGrainAtMarket(*h, v.asNumber());
                if (w.inPlayerHouse(h->id)) w.log(1, got > 0 ? "  賣了 " + std::to_string(static_cast<long>(got)) + " 便士。" : "  到了市集才發現，家裡的糧其實不夠賣。");
            } else if (k == "spare_work") {
                // {"spare_work": {"kind": "day", "pay": 6, "meals": 6}} (glean/craft/alms): a poor season's free day, paid at once.
                if (h) w.spareWork(*h, p.id, v, target);
            } else if (k == "hire_hand") {
                // {"hire_hand": {"pay": 24, "bonus": 0.05}}: the target works the house's harvest for a season's wage.
                if (!h || target == kNone) continue;
                w.hireHand(*h, target, v.numberOr("pay", 24), v.numberOr("bonus", 0.05));
            } else if (k == "give_parcel") {
                // {"give_parcel": <parcel id>}: one of the house's parcels given to the target's house to till.
                if (!h || target == kNone || w.people_[target].household == kNone) continue;
                Id pc = static_cast<Id>(v.asNumber());
                if (pc < 0 || pc >= static_cast<Id>(w.parcels_.size()) || w.parcels_[pc].holder != h->id) continue;
                w.giveParcelTo(pc, w.people_[target].household, p.id);
            } else if (k == "take_in") {
                // The target comes to live in p's house.
                if (!h || target == kNone) continue;
                w.takeIn(target, h->id, p.id);
            } else if (k == "hire_kin") {
                // The grown of the target's house hire on with p's house for the year (bed, board and a wage).
                if (!h || target == kNone || w.people_[target].household == kNone) continue;
                w.hireKin(w.people_[target].household, h->id);
            } else if (k == "rent_vacant_credit") {
                if (!h) continue;
                std::string no = w.rentVacantOnCredit(*h, v.numberOr("min", 3));
                if (!no.empty() && w.inPlayerHouse(h->id)) w.log(1, "  " + no);
            } else if (k == "on_deed_actor") {
                // Effects on whoever did the deed this event is about, with this person as their target.
                if (deed_ != kNone) {
                    Id a = w.deeds_[deed_].actor;
                    if (a != kNone && a != p.id && w.people_[a].alive()) apply(v, w.people_[a], p.id);
                }
            } else if (k == "end_affair") {
                // The affair this event is about stops, if it is still going on.
                if (deed_ != kNone) {
                    const Deed& d = w.deeds_[deed_];
                    if (d.actor != kNone && d.with != kNone && w.people_[d.actor].lover == d.with) w.endLiaison(d.actor);
                }
            } else if (k == "expose_affair") {
                // The affair this event is about, taken to the church court: both of them, in front of the parish.
                if (deed_ != kNone && w.deeds_[deed_].with != kNone) w.exposeAffair(w.deeds_[deed_].actor, w.deeds_[deed_].with);
            } else if (k == "fight_lover") {
                // Going for the other man (or woman) of the affair this event is about: a brawl the village sees, and the
                // affair is over. Against the lord it is the manor that remembers, and its court may fine.
                if (deed_ == kNone) continue;
                const Id lv = w.deeds_[deed_].with, doer = w.deeds_[deed_].actor;  // copies: recordDeed grows deeds_
                if (lv == kNone || !w.people_[lv].alive() || w.people_[lv].household == kNone) continue;
                w.recordDeed("brawl", p.id, lv, 1, deed_);
                p.health = std::max(0.0, p.health - 6);
                w.deedNotes_.insert({deed_, "spoken"});
                if (doer != kNone && w.people_[doer].lover == lv) w.endLiaison(doer);
                if (lv == w.lord_) {
                    w.addOpinion(lv, p.id, "struck_the_lord", -40, 20);
                    if (h && w.rngEvents_.chance(0.5)) {
                        double fine = std::min(12.0, std::max(0.0, h->money));
                        h->money -= fine;
                        w.book(*h, "毆打領主的罰款", -fine);
                        if (w.inPlayerHouse(h->id)) w.log(1, "  管家把這事記上了莊園法庭的案卷，罰了 " + std::to_string(int(fine)) + " 便士。");
                    }
                }
            } else if (k == "suspect") {
                // Believes a little more firmly that the one in the deed did it (a neighbour's hint, pressed).
                if (deed_ != kNone) w.addBelief(p.id, deed_, w.deeds_[deed_].actor, v.asNumber(), target != kNone ? target : kNone);
            } else if (k == "nurse") {
                // Care for the one lying sick (the target): the odds the illness kills move by this much.
                Id who = target != kNone ? target : p.id;
                auto it = w.sick_.find(who);
                if (it != w.sick_.end()) it->second.odds = std::clamp(it->second.odds + v.asNumber(), 0.05, 0.95);
            } else if (k == "disown_target") {
                // Not acknowledged as the head's child: does not take the land (life.cpp heirOf).
                if (target != kNone) w.people_[target].flags.insert("disowned");
            } else if (k == "on_target") {
                // Effects on the other party, who then has this person as their target.
                if (target != kNone && w.people_[target].alive()) apply(v, w.people_[target], p.id);
            } else if (k == "stress") {
                w.addStress(p.id, v.asNumber(), "");
            } else if (k == "confess_secrets") {
                // "priest" keeps the seal of confession; "spouse" may not.
                std::string to = v.asString();
                Id who = to == "priest" ? w.priest_ : to == "spouse" ? p.spouse : target;
                if (who == kNone || who == p.id) continue;
                std::vector<Id> told = w.confessSecrets(p.id, who);
                // What was said, when the family is there to hear it (a priest's ear stays shut).
                if (to != "priest" && (w.isPlayerFamily(p) || w.isPlayerFamily(w.people_[who]))) {
                    if (told.empty()) w.log(1, "  說來說去，也沒有什麼見不得人的事。");
                    for (size_t i = 0; i < told.size() && i < 3; ++i)
                        w.log(1, "  「" + w.secretWords(told[i], who) + "。」");
                    if (told.size() > 3) w.log(1, "  還有幾件，" + w.people_[who].firstName + "聽不下去了。");
                }
            } else if (k == "village_opinion") {
                // How the other heads of the village take what p did to them all (assessing the king's tax).
                std::string mod = v.stringOr("mod", "event");
                Id lordHouse = w.lord_ != kNone ? w.people_[w.lord_].household : kNone;
                for (const auto& oh : w.households_)
                    if (!oh.extinct && oh.head != kNone && oh.id != p.household && oh.id != lordHouse && oh.head != w.priest_)
                        w.addOpinion(oh.head, p.id, mod, v["value"].asNumber(), static_cast<int>(v.numberOr("years", 4)));
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
                w.noteLife(p.id, "替全家贖了身");
            } else if (k == "take_any_place") {
                // No place after all: nothing after it in the list happens either (no fee, no "went to the master").
                Id master = p.household != kNone ? w.openPlace(v.asString(), p.household) : kNone;
                Id from = p.household;
                if (master == kNone || !w.takePlace(master, v.asString(), p.id)) return;
                if (v.asString() == "apprentice") {  // the master's fee, as when he is asked or takes a boy on himself
                    w.households_[from].money -= 24;
                    w.households_[master].money += 24;
                    w.book(w.households_[master], "收徒的拜師錢", 24);
                    w.book(w.households_[from], "拜師錢", -24);
                }
            } else if (k == "take_place") {
                // The person (an apprentice) goes to the master of that trade who has a place free.
                Id master = w.openApprenticeship(v.asString(), p.household);
                if (master != kNone) w.takePlace(master, "apprentice", p.id);
            } else if (k == "entry_fine") {
                if (h) w.fineChoice(*h, v.asString());
            } else if (k == "send_subject_to_service") {
                // The plan's subject goes to serve in the target's household for some years.
                if (plan_ && plan_->subject != kNone && target != kNone)
                    w.sendToService(plan_->subject, target, static_cast<int>(v.numberOr("years", 5)));
            } else if (k == "occasion_policy") {
                // The head's answer becomes the family's rule for wakes and weddings (World::occasionByPolicy).
                // Only the player's own answer: the AI standing in for one card does not make the family's rules.
                if (picked_ && w.isPlayerFamily(p)) w.setOccasionRule(v.asString());
            } else if (k == "occasion") {
                // {"came": true, "value": 6, "mod": "came_to_wake"}: at (or missing) the wake or wedding of the target's house.
                w.attendOccasion(p.id, target, !v["came"].isBool() || v["came"].asBool(), v.numberOr("value", 0), v.stringOr("mod", "event"));
            } else if (k == "spend_time") {
                // An occasion takes one of the head's free days, if there are any left this season.
                if (w.timeLeft() > 0) w.spendTime(static_cast<int>(v.asNumber()));
            } else if (k == "tell") {
                // A line for the family's chronicle only: how a try turned out, news from someone away.
                if (!w.isPlayerFamily(p)) continue;
                bool home = w.inPlayerHouse(p.household) || w.inPlayerHouse(p.home) || (w.isAway(p) && w.inPlayerHouse(p.awayFrom));
                w.log(home ? 1 : 2, format(v.asString(), p, target));
            } else if (k == "log") {
                w.log(p.household == w.playerHousehold_ ? 1 : 2, format(v.asString(), p, target));
            } else {
                throw JsonError("unknown effect key: " + k);
            }
        }
    }
}

// The chance of a try, in percent: { base, skill: {learning: per point}, traits: {clever: +}, free: +,
// lord_opinion / target_opinion / reputation: per point }. Never certain either way.
double EventRunner::odds(const Json& o, const Person& p, Id target) const {
    double c = o.numberOr("base", 50);
    if (o["skill"].isObject())
        for (const auto& [k, per] : o["skill"].asObject()) c += p.skills[skillFromKey(k)] * per.asNumber();
    if (o["traits"].isObject())
        for (const auto& [t, add] : o["traits"].asObject())
            if (w_.hasTrait(p, t)) c += add.asNumber();
    if (p.free) c += o.numberOr("free", 0);
    if (o["lord_opinion"].isNumber() && w_.lord_ != kNone && w_.people_[w_.lord_].alive() && w_.lord_ != p.id)
        c += w_.opinion(w_.lord_, p.id) * o["lord_opinion"].asNumber();
    if (o["target_opinion"].isNumber() && target != kNone) c += w_.opinion(target, p.id) * o["target_opinion"].asNumber();
    if (o["reputation"].isNumber()) c += p.reputation * o["reputation"].asNumber();
    return std::clamp(c, 5.0, 95.0);
}

// An option's text, with {odds} spelled out the way the player would weigh it: "機會偏高，看讀書的本事".
// The odds are told in words, not numbers, and say what they hang on ("depends"), not what was missed.
std::string EventRunner::optionText(const OptionDef& o, const Person& p, Id target) const {
    std::string t = o.text;
    size_t at = t.find("{odds}");
    if (at != std::string::npos) {
        std::string said = "機會難說";
        if (o.effects.isArray())
            for (const auto& e : o.effects.asArray())
                if (e["odds"].isObject()) {
                    double c = odds(e["odds"], p, target);
                    said = std::string("機會") + (c < 30 ? "偏低" : c < 50 ? "不到一半" : c < 70 ? "一半多" : "偏高");
                    std::string dep = e["odds"].stringOr("depends", "");
                    if (!dep.empty()) said += "，看" + dep;
                    break;
                }
        t.replace(at, 6, said);
    }
    return format(t, p, target);
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
            else if (key == "service_child" || key == "service_master") {  // who would go into service, and to whom
                const Household* sh = p.household != kNone ? &w_.households_[p.household] : nullptr;
                Id who = !sh ? kNone : key == "service_child" ? serviceChild(*sh) : serviceMaster(*sh);
                val = who == kNone ? "一個孩子" : w_.fullName(who);
            } else if (key == "target_keepsake") {  // what of a dead child is put away: a baby's shoes, a girl's dress
                const Person* t = target != kNone ? &w_.people_[target] : nullptr;
                double a = t ? ((t->alive() ? w_.day_ : t->deathDay) - t->birthDay) / double(kDaysPerYear) : 0;
                val = a < 7 ? "小鞋子" : a < 14 ? "舊衣裳" : "東西";
            }
            else if (key == "year") val = std::to_string(w_.year());
            else if (key == "house") val = p.dynasty != kNone ? w_.dynasties_[p.dynasty].surname : "";
            else if (key == "subject") val = plan_ ? w_.fullName(plan_->subject) : "";
            else if (key == "plan") val = plan_ ? w_.defs_.plan(plan_->plan)->name : "";
            else if (key == "deed") {
                const DeedDef* dd = deed_ != kNone ? w_.defs_.deed(w_.deeds_[deed_].type) : nullptr;
                val = dd ? dd->name : "";
            } else if (key == "hook") {
                Id d = target != kNone ? w_.hookDeed(p.id, target) : kNone;
                val = d != kNone ? w_.describeDeed(d) : "那件事";
            } else if (key == "tax") val = p.household != kNone ? std::to_string(static_cast<long>(w_.taxDue(w_.households_[p.household]))) : "0";
            else if (key == "debt") {
                // What is owed to the one at the door (the steward, the neighbour), else all the house owes.
                double d = 0;
                if (p.household != kNone) {
                    const Household& dh = w_.households_[p.household];
                    d = w_.owedAll(dh.id);
                    double t = (dh.debt > 0.5 && dh.creditor == target ? dh.debt : 0.0) + (dh.heldDebt > 0.5 && dh.heldCreditor == target ? dh.heldDebt : 0.0);
                    if (target != kNone && t > 0.5) d = t;
                }
                val = std::to_string(int(d));
            } else if (key.rfind("grain_loan:", 0) == 0) {
                // {grain_loan:60}: what N days of the house's bread borrowed now will cost back at Michaelmas.
                val = p.household != kNone ? std::to_string(static_cast<long>(w_.grainLoanOwed(w_.households_[p.household], std::stod(key.substr(11))))) : "0";
            } else if (key.rfind("food_cost:", 0) == 0) {
                // {food_cost:20}: what N days of the house's bread cost at the market today.
                val = p.household != kNone ? std::to_string(static_cast<long>(std::lround(w_.consumption(w_.households_[p.household]) * std::stod(key.substr(10)) * w_.param("grain_price", 0.5)))) : "0";
            }
            else if (key == "feud_cause") {
                const Feud* f = target != kNone ? w_.feudBetween(p.dynasty, w_.people_[target].dynasty) : nullptr;
                val = f ? w_.describeDeed(f->cause) : "";
            } else if (key == "target_house") val = target != kNone && w_.people_[target].dynasty != kNone ? w_.dynasties_[w_.people_[target].dynasty].surname : "";
            else if (key == "target_home") {
                // The family of the house they live (or lived) in: a wife married in is mourned as one of that house.
                Id th = target != kNone ? w_.people_[target].household : kNone;
                Id dyn = th != kNone ? w_.households_[th].dynasty : kNone;
                if (dyn == kNone && target != kNone) dyn = w_.people_[target].dynasty;  // the dead are out of every house: their own name
                val = dyn != kNone ? w_.dynasties_[dyn].surname : "";
            } else if (key.rfind("about:", 0) == 0) {
                // {about:grief}: who this person's grief (or other concern) is about. A remarried widow speaks of
                // her other griefs before the first husband (the "own" concern condition), if she has any.
                const Concern* c = nullptr;
                for (const auto& x : p.concerns)
                    if (x.kind == key.substr(6) && !aboutFormerSpouse(w_.people_, p, x) && (!c || x.strength > c->strength)) c = &x;
                if (!c) c = w_.concernOf(p, key.substr(6));
                Id a = c ? c->about : kNone;
                val = a == kNone ? "" : a == target && target == w_.playerHead() ? "你" : w_.isPlayerFamily(w_.people_[a]) ? w_.shortName(a) : w_.fullName(a);
            } else if (key.rfind("target_about:", 0) == 0) {
                const Concern* c = target != kNone ? w_.concernOf(w_.people_[target], key.substr(13)) : nullptr;
                Id a = c ? c->about : kNone;
                val = a == kNone ? "" : w_.isPlayerFamily(w_.people_[a]) ? w_.shortName(a) : w_.fullName(a);
            } else if (key == "kin") val = target != kNone ? w_.kinWord(target, p.id) : "";
            else if (key == "target_kin") val = target != kNone ? w_.kinWord(p.id, target) : "";
            else if (key == "sell_reason")
                val = target != kNone && w_.people_[target].household != kNone ? w_.sellReason(w_.households_[w_.people_[target].household]) : "";
            else if (key == "stress_cause") val = p.stressCause.empty() ? "說不出的心事" : p.stressCause;
            else if (key == "hunger_why") val = w_.hungerWhy(p.household);  // why the house is short this year (economy.cpp)
            else if (key == "deed_actor" || key == "deed_victim") {
                Id a = deed_ == kNone ? kNone : key == "deed_actor" ? w_.deeds_[deed_].actor : w_.deeds_[deed_].target;
                val = a == kNone ? "" : w_.isPlayerFamily(w_.people_[a]) ? w_.shortName(a) : w_.fullName(a);
            } else if (key == "deed_with") {  // the other party of an affair
                Id a = deed_ == kNone ? kNone : w_.deeds_[deed_].with;
                val = a == kNone ? "" : w_.isPlayerFamily(w_.people_[a]) ? w_.shortName(a) : w_.fullName(a);
            } else if (key == "deed_year") val = deed_ != kNone ? std::to_string(w_.cfg_.startYear + w_.deeds_[deed_].day / kDaysPerYear) : "";
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
