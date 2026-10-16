// Plans (schemes): long-running intentions started by the player through commands, or by NPCs.
#include <algorithm>
#include <cmath>

#include "simcore/events.h"

namespace sim {

int choosePlanOption(const std::vector<PlanOption>& opts, Rng& rng) {
    std::vector<std::string> plans;
    std::vector<double> pw;
    for (const auto& o : opts)
        if (std::find(plans.begin(), plans.end(), o.plan) == plans.end()) {
            plans.push_back(o.plan);
            pw.push_back(o.weight);
        }
    int p = rng.weighted(pw);
    if (p < 0) return -1;
    std::vector<int> idx;
    std::vector<double> tw;
    for (size_t i = 0; i < opts.size(); ++i)
        if (opts[i].plan == plans[p]) {
            idx.push_back(static_cast<int>(i));
            tw.push_back(opts[i].targetWeight);
        }
    int t = rng.weighted(tw);
    return t < 0 ? -1 : idx[t];
}

void World::issue(const Command& c) {
    Command cmd = c;
    if (cmd.day < day_) cmd.day = day_;
    pending_.push_back(cmd);
}

const ActivePlan* World::activePlan(Id actor) const {
    for (const auto& p : plans_)
        if (p.actor == actor) return &p;
    return nullptr;
}

void World::processCommands() {
    for (size_t i = 0; i < pending_.size();) {
        if (pending_[i].day > day_) {
            ++i;
            continue;
        }
        Command c = pending_[i];
        pending_.erase(pending_.begin() + static_cast<long>(i));
        c.day = day_;
        commandLog_.push_back(c);
        if (c.type == "start_plan") {
            // Setting a plan going takes the head a day of the season, like going to see someone.
            if (c.actor == playerHead() && !spendTime(1)) continue;
            startPlan(c.actor, c.plan, c.target, c.subject);
        } else if (c.type == "interact") {
            for (const auto& ev : defs_.events)
                if (ev.id == c.plan && ev.scope == "interaction" && c.target != kNone) {
                    EventRunner er(*this);
                    er.interact(ev, people_[c.target], playerHead());
                    break;
                }
        } else if (c.type == "cancel_plan") {
            for (size_t k = 0; k < plans_.size(); ++k)
                if (plans_[k].actor == c.actor) {
                    endPlan(k, "cancel");
                    break;
                }
        }
    }
}

std::vector<PlanOption> World::availablePlans(Id actorId) {
    std::vector<PlanOption> out;
    if (actorId == kNone) return out;
    Person& actor = people_[actorId];
    if (!actor.alive() || actor.household == kNone || activePlan(actorId)) return out;
    EventRunner ev(*this);
    for (const auto& pd : defs_.plans) {
        auto cd = cooldowns_.find({actorId, "plan:" + pd.id});
        if (cd != cooldowns_.end() && cd->second > day_) continue;
        if (!ev.eval(pd.actor, actor, kNone)) continue;
        double w = ev.weight(pd.aiWeight, actor);
        if (const AmbitionDef* amb = defs_.ambition(actor.ambition))
            if (std::find(amb->plans.begin(), amb->plans.end(), pd.id) != amb->plans.end()) w += 25;

        std::vector<Id> subjects{kNone};
        if (!pd.subject.isNull()) {
            subjects.clear();
            for (Id c : actor.children) {
                const Person& cp = people_[c];
                if (cp.alive() && cp.household != kNone && ev.eval(pd.subject["conditions"], cp, actorId))
                    subjects.push_back(c);
            }
        }
        for (Id sub : subjects) {
            if (sub != kNone) {
                auto scd = cooldowns_.find({sub, "plan:" + pd.id});
                if (scd != cooldowns_.end() && scd->second > day_) continue;
            }
            if (pd.target.isNull()) {
                out.push_back({pd.id, kNone, sub, w, 1});
                continue;
            }
            std::string pick = pd.target.stringOr("pick", "villager");
            Id other = sub != kNone ? sub : actorId;
            const Json& cond = pd.target["conditions"];
            auto ok = [&](Id t) {
                if (t == kNone || t == actorId || t == sub) return false;
                const Person& tp = people_[t];
                return tp.alive() && tp.household != kNone && ev.eval(cond, tp, other);
            };
            if (pick == "lord") {
                if (ok(lord_)) out.push_back({pd.id, lord_, sub, w, 1});
                continue;
            }
            for (const auto& tp : people_) {
                if (!ok(tp.id) || tp.household == actor.household) continue;
                double op = opinion(actorId, tp.id);
                int feud = feudStage(actorId, tp.id);
                if (pick == "rival" && op > -30 && feud < 2) continue;
                if (pick == "friend" && op < 30) continue;
                // Prefer targets the person feels strongly about; a family feud makes anyone of that house a target.
                double tw = pick == "rival" ? 1 + std::max(0.0, -op - 30) / 40 + feud * 0.5 : 1 + std::max(0.0, op) / 50;
                out.push_back({pd.id, tp.id, sub, w, tw});
            }
        }
    }
    return out;
}

bool World::startPlan(Id actorId, const std::string& planId, Id target, Id subject) {
    const PlanDef* pd = defs_.plan(planId);
    if (!pd || actorId == kNone || activePlan(actorId)) return false;
    Person& actor = people_[actorId];
    if (!actor.alive() || actor.household == kNone) return false;
    EventRunner check(*this);
    if (!check.eval(pd->actor, actor, kNone)) return false;
    if (!pd->target.isNull() && (target == kNone || !people_[target].alive() || people_[target].household == kNone))
        return false;

    plans_.push_back({actorId, target, subject, planId, 0, day_, {}});
    ++counters_["plan:" + planId + ":start"];
    if (isPlayerFamily(actor)) ++counters_["plan_family:" + planId + ":start"];
    // The head taking up a child's cause (a match for them) is what that child was waiting for.
    if (actorId == playerHead() && subject != kNone) answerWant(subject, "plan:" + planId);
    bool home = actor.household == playerHousehold_;
    if (home && !pd->startLog.empty()) {
        EventRunner ev(*this, &plans_.back());
        log(1, "【計畫】" + ev.format(pd->startLog, actor, target));
    }
    return true;
}

void World::endPlan(size_t index, const std::string& requested) {
    std::string outcome = requested;
    ActivePlan plan = plans_[index];
    plans_.erase(plans_.begin() + static_cast<long>(index));
    ++counters_["plan:" + plan.plan + ":" + outcome];
    const PlanDef* pd = defs_.plan(plan.plan);
    if (pd->retryYears > 0) cooldowns_[{plan.actor, "plan:" + plan.plan}] = day_ + pd->retryYears * kDaysPerYear;
    // A child who has just been through a match (or refused one) is not offered round again at once.
    if (plan.subject != kNone) cooldowns_[{plan.subject, "plan:" + plan.plan}] = day_ + 2 * kDaysPerYear;
    Person& actor = people_[plan.actor];
    if (!actor.alive() || actor.household == kNone) return;
    bool actorHome = actor.household == playerHousehold_;
    bool victimHome = plan.target != kNone && people_[plan.target].household == playerHousehold_;
    EventRunner ev(*this, &plan);

    if (outcome == "cancel") {
        if (actorHome) log(1, fullName(plan.actor) + " 放棄了「" + pd->name + "」。");
        return;
    }
    if (outcome == "lost_target") {
        if (actorHome) log(1, "「" + pd->name + "」落空了：" + fullName(plan.target) + " 已經不在了。");
        return;
    }
    // Someone courting the player's head, or an unmarried child of the house: the family answers.
    if (plan.plan == "court" && !actorHome && victimHome && (outcome == "success" || outcome == "fail")) {
        Id acting = actingHead();
        const Person& t = people_[plan.target];
        bool ours = plan.target == acting || t.father == acting || t.mother == acting ||
                    (acting != kNone && (t.father == people_[acting].spouse || t.mother == people_[acting].spouse));
        if (ours && acting != kNone) {
            const Household& ah = households_[actor.household];
            std::string sit = fullName(plan.actor) + "（" + std::to_string(int(ageYears(actor))) + " 歲，家裡 " +
                              std::to_string(int(ah.acres)) + " 畝地；你對他們 " + std::to_string(int(opinion(acting, plan.actor))) +
                              "）託人來向 " + fullName(plan.target) + " 求親。";
            int r = askPlayer("求親", sit, {"答應", "回絕"});
            if (r == 1) {
                log(1, "〔求親〕" + sit + "（選擇：回絕）");
                recordDeed("refused_proposal", plan.target, plan.actor);
                return;
            }
            if (r == 0) outcome = "success";
        }
    }
    const std::string& text = outcome == "success" ? pd->successLog : outcome == "exposed" ? pd->exposedLog : pd->failLog;
    const Json& effects = outcome == "success" ? pd->success : outcome == "exposed" ? pd->exposed : pd->fail;
    std::string line = text.empty() ? "" : ev.format(text, actor, plan.target);
    if (pd->secret && outcome == "success" && victimHome && !actorHome) {
        // The victim sees what happened, not who did it.
        if (!pd->victimLog.empty()) log(1, ev.format(pd->victimLog, actor, plan.target));
    } else if (!line.empty() && (actorHome || (victimHome && pd->notifyTarget))) {
        log(1, (outcome == "success" ? "【成功】" : outcome == "exposed" ? "【敗露】" : "【失敗】") + line);
    } else if (!line.empty() && outcome == "exposed") {
        log(2, line);
    }
    size_t before = deeds_.size();
    ev.apply(effects, actor, plan.target);
    // Say why, when the doer's family is the player's: a grudge answered is part of the story.
    if (actorHome && outcome == "success")
        for (size_t k = before; k < deeds_.size(); ++k)
            if (deeds_[k].actor == plan.actor && deeds_[k].cause != kNone) {
                log(1, "  " + people_[plan.actor].firstName + " 心裡記著的，是 " + describeDeed(deeds_[k].cause) + "。");
                break;
            }
}

void World::monthlyPlans() {
    for (size_t i = 0; i < plans_.size();) {
        ActivePlan& plan = plans_[i];
        const PlanDef* pd = defs_.plan(plan.plan);
        Person& actor = people_[plan.actor];
        if (!actor.alive() || actor.household == kNone) {
            plans_.erase(plans_.begin() + static_cast<long>(i));
            continue;
        }
        if (plan.target != kNone && (!people_[plan.target].alive() || people_[plan.target].household == kNone)) {
            endPlan(i, "lost_target");
            continue;
        }
        // A match is off once either of the pair has married someone else.
        if (plan.plan == "arrange_match" && ((plan.subject != kNone && people_[plan.subject].spouse != kNone) ||
                                             (plan.target != kNone && people_[plan.target].spouse != kNone))) {
            endPlan(i, "lost_target");
            continue;
        }
        if (plan.subject != kNone && (!people_[plan.subject].alive() || people_[plan.subject].household == kNone)) {
            endPlan(i, "lost_target");
            continue;
        }
        Household& h = households_[actor.household];
        h.money = std::max(0.0, h.money - pd->monthlyCost);

        // Progress this month.
        const Json& pr = pd->progress;
        double gain = pr.numberOr("base", 5);
        if (pr["skill"].isObject())
            for (const auto& [sk, wgt] : pr["skill"].asObject()) gain += actor.skills[skillFromKey(sk)] * wgt.asNumber() / 10;
        if (plan.target != kNone) gain += pr.numberOr("opinion", 0) * opinion(plan.target, plan.actor) / 50;
        gain += pr.numberOr("reputation", 0) * actor.reputation / 10;
        if (pr["traits"].isObject())
            for (const auto& [t, wgt] : pr["traits"].asObject())
                if (hasTrait(actor, t)) gain += wgt.asNumber();
        gain *= 0.6 + rngPlans_.uniform() * 0.8;
        plan.progress += gain;

        bool actorHome = actor.household == playerHousehold_;
        bool victimHome = plan.target != kNone && people_[plan.target].household == playerHousehold_;

        // At most one beat a month.
        {
            ActivePlan copy = plan;
            EventRunner ev(*this, &copy);
            for (const auto& beat : pd->beats) {
                if (std::find(copy.beatsSeen.begin(), copy.beatsSeen.end(), beat.id) != copy.beatsSeen.end()) continue;
                if (!ev.eval(beat.conditions, actor, plan.target)) continue;
                if (!rngPlans_.chance(beat.chance)) continue;
                copy.beatsSeen.push_back(beat.id);
                int importance = actorHome || (victimHome && pd->notifyTarget && !pd->secret) ? 1 : 0;
                ev.runEvent(beat, actor, plan.target, importance);
                break;
            }
            // Effects may have added people; re-find the plan by actor.
            for (auto& p : plans_)
                if (p.actor == copy.actor) {
                    p.progress = copy.progress;
                    p.beatsSeen = copy.beatsSeen;
                }
        }
        ActivePlan& cur = plans_[i];
        // Foreshadowing: the family a secret plan is aimed at may sense something before it lands.
        if (victimHome && !actorHome && !pd->foreshadow.empty() && day_ - lastForeshadow_ > 3 * kDaysPerYear &&
            rngPlans_.chance(0.08) && std::find(cur.beatsSeen.begin(), cur.beatsSeen.end(), "_foreshadow") == cur.beatsSeen.end()) {
            cur.beatsSeen.push_back("_foreshadow");
            lastForeshadow_ = day_;
            EventRunner ev(*this, &cur);
            log(1, ev.format(pd->foreshadow, actor, cur.target));
        }
        if (!people_[cur.actor].alive()) {
            plans_.erase(plans_.begin() + static_cast<long>(i));
            continue;
        }
        if (pd->secret && rngPlans_.chance(pd->discovery)) {
            endPlan(i, "exposed");
            continue;
        }
        int months = (day_ - cur.startDay) / kDaysPerMonth;
        if (cur.progress >= 100) {
            endPlan(i, "success");
            continue;
        }
        if (cur.progress < 0 || months >= pd->maxMonths) {
            endPlan(i, "fail");
            continue;
        }
        ++i;
    }
}

void World::npcStartPlans() {
    Id head = playerHead();
    for (size_t idx = 0; idx < people_.size(); ++idx) {
        Person& p = people_[idx];
        if (!p.alive() || p.household == kNone || p.id == head) continue;
        if (ageYears(p) < 16 || activePlan(p.id)) continue;
        // Beside what they mean to do (minds.cpp), people also act on impulse now and then.
        double c = p.household == playerHousehold_ ? 0.006 : 0.006;
        if (!rngPlans_.chance(c)) continue;
        std::vector<PlanOption> opts = availablePlans(p.id);
        opts.erase(std::remove_if(opts.begin(), opts.end(), [&](const PlanOption& o) { return !defs_.plan(o.plan)->npc; }),
                   opts.end());
        int pick = choosePlanOption(opts, rngPlans_);
        if (pick < 0) continue;
        startPlan(p.id, opts[pick].plan, opts[pick].target, opts[pick].subject);
    }
}

}  // namespace sim
