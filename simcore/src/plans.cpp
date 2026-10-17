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
            // Setting a plan going takes the one keeping the house a day of the season, like going to see someone:
            // taken once it is known to start, and a plan that cannot start says why.
            const bool ours = c.actor == actingHead();
            const PlanDef* pd = defs_.plan(c.plan);
            const std::string name = pd ? "「" + pd->name + "」" : "這件事";
            if (ours && timeLeft() <= 0) {
                log(1, "〔計畫〕" + name + "沒辦成：這一季沒有空閒了。");
                continue;
            }
            if (ours && activePlan(c.actor)) {
                log(1, "〔計畫〕" + name + "沒辦成：手上已經有一件事在辦。");
                continue;
            }
            if (startPlan(c.actor, c.plan, c.target, c.subject)) {
                if (ours) spendTime(1);
            } else if (ours) {
                log(1, "〔計畫〕" + name + "眼下辦不成。");
            }
        } else if (c.type == "interact") {
            for (const auto& ev : defs_.events)
                if (ev.id == c.plan && ev.scope == "interaction" && c.target != kNone) {
                    EventRunner er(*this);
                    er.interact(ev, people_[c.target], actingHead());
                    break;
                }
        } else if (c.type == "ask") {
            // plan = "<ask>|<office>|<lever>|<subject>", subject = the thing asked for (contest/ask.cpp).
            std::vector<std::string> f;
            size_t a = 0;
            for (size_t b; (b = c.plan.find('|', a)) != std::string::npos; a = b + 1) f.push_back(c.plan.substr(a, b - a));
            f.push_back(c.plan.substr(a));
            if (f.size() == 4) ask(c.target, f[0], c.subject, f[3].empty() ? kNone : static_cast<Id>(std::stol(f[3])), f[1], f[2]);
        } else if (c.type == "want") {
            // The family lets it be known it wants a parcel (plan "parcel") or a post (plan "office:<id>"), or drops it.
            if (c.plan == "parcel") addWant(playerHousehold_, "parcel", c.subject, kNone, "", "盯上了");
            else if (c.plan.rfind("office:", 0) == 0) {
                std::string bar = officeBar(playerHead(), c.plan.substr(7));
                const Office* of = office(c.plan.substr(7));
                if (!of) continue;
                if (bar.empty()) addWant(playerHousehold_, "office", kNone, playerHead(), of->id, "想當");
                else log(1, "【差事】你家爭不了" + of->name + "：" + bar + "。");
            }
            else if (c.plan == "drop" && c.subject >= 0 && c.subject < static_cast<Id>(wants_.size()) && inPlayerHouse(wants_[c.subject].house))
                dropWant(wants_[c.subject], "放下了");
        } else if (c.type == "focus") {
            // plan = the focus id, target = whose (design/life-paths.md 年度重心). Free: it is how they spend the year.
            setFocus(c.target, c.plan);
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
            std::vector<Id> pool = actor.children;
            // "kin_at_home": also the others living under the actor's roof (a brother, a niece), not servants.
            if (pd.subject.stringOr("pick", "any_child") == "kin_at_home")
                for (Id m : households_[actor.household].members)
                    if (m != actorId && m != actor.spouse && people_[m].home == kNone &&
                        std::find(pool.begin(), pool.end(), m) == pool.end())
                        pool.push_back(m);
            for (Id c : pool) {
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
                // The one the subject has set their heart on comes first.
                if (sub != kNone && concernOf(people_[sub], "longing", tp.id)) tw += 6;
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
    bool home = inPlayerHouse(actor.household);
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
    bool actorHome = inPlayerHouse(actor.household);
    bool victimHome = plan.target != kNone && inPlayerHouse(people_[plan.target].household);
    EventRunner ev(*this, &plan);

    if (outcome == "cancel") {
        if (actorHome) log(1, fullName(plan.actor) + " 放棄了「" + pd->name + "」。");
        return;
    }
    if (outcome == "lost_target") {
        if (actorHome) log(1, "「" + pd->name + "」落空了：" + fullName(plan.target) + " 已經不在了。");
        return;
    }
    if (outcome == "married_off") {
        Id wed = plan.subject != kNone && people_[plan.subject].spouse != kNone && people_[plan.subject].spouse != plan.target ? plan.subject
               : plan.target != kNone && people_[plan.target].spouse != kNone && people_[plan.target].spouse != plan.subject && people_[plan.target].spouse != plan.actor
                   ? plan.target : plan.actor;
        if (actorHome) log(1, "「" + pd->name + "」落空了：" + fullName(wed) + " 已經另結了親。");
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
    // A land deal is struck at what the buyer can pay for what the seller holds now; if it no longer adds up, it
    // comes to nothing rather than to a sale that moves no land.
    const Json* buy = nullptr;
    if (pd->success.isArray())
        for (const auto& e : pd->success.asArray())
            if (e.has("take_acres")) buy = &e["take_acres"];
    World::LandDeal deal;
    if (buy && (outcome == "success" || outcome == "fail") && plan.target != kNone && people_[plan.target].household != kNone) {
        deal = landDeal(households_[actor.household], households_[people_[plan.target].household], (*buy)["acres"].asNumber(),
                        (*buy)["price"].asNumber(), !((*buy)["wanted"].isBool() && !(*buy)["wanted"].asBool()));
        if (!deal.ok && outcome == "success") {
            if (actorHome) log(1, "「" + pd->name + "」沒談成：" + (deal.acres <= 0 ? fullName(plan.target) + " 家已經沒有地可讓" : "手上的錢不夠了") + "。");
            return;
        }
    }
    // Someone after something of the head's own (a parcel, a hand): the head says yes or no, not the dice.
    if (!pd->targetAsk.empty() && pd->targetAnswers.size() >= 2 && !actorHome && plan.target == actingHead() && (!buy || deal.ok) &&
        (outcome == "success" || outcome == "fail")) {
        std::string ask = pd->targetAsk;
        for (auto [key, val] : {std::pair<std::string, std::string>{"{deal}", deal.what},
                                {"{deal_price}", std::to_string(static_cast<long>(deal.price))}})
            for (size_t at; (at = ask.find(key)) != std::string::npos;) ask.replace(at, key.size(), val);
        std::string sit = ev.format(ask, actor, plan.target);
        int r = askPlayer(pd->name, sit, pd->targetAnswers);
        if (r == 0) outcome = "success";
        else if (r == 1) outcome = "fail";
        if (r >= 0) log(1, "〔" + pd->name + "〕" + sit + "（選擇：" + pd->targetAnswers[r] + "）");
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
        // A match or a courtship is off once either of the pair has married someone else.
        auto wedElsewhere = [&](Id x, Id other) {
            return x != kNone && people_[x].spouse != kNone && people_[x].spouse != other && people_[people_[x].spouse].alive();
        };
        if ((plan.plan == "arrange_match" && (wedElsewhere(plan.subject, plan.target) || wedElsewhere(plan.target, plan.subject))) ||
            (plan.plan == "court" && (wedElsewhere(plan.actor, plan.target) || wedElsewhere(plan.target, plan.actor)))) {
            endPlan(i, "married_off");
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

        bool actorHome = inPlayerHouse(actor.household);
        bool victimHome = plan.target != kNone && inPlayerHouse(people_[plan.target].household);

        // At most one beat a month.
        {
            ActivePlan copy = plan;
            EventRunner ev(*this, &copy);
            for (const auto& beat : pd->beats) {
                if (std::find(copy.beatsSeen.begin(), copy.beatsSeen.end(), beat.id) != copy.beatsSeen.end()) continue;
                // Tried again (a second go at buying the freehold): the same scene does not play out word for word.
                // Keyed by who it is with and for: a second child's match still gets its own haggling.
                const std::string beatKey = "beat:" + pd->id + ":" + beat.id + ":" + std::to_string(plan.target) + ":" + std::to_string(plan.subject);
                auto again = cooldowns_.find({actor.id, beatKey});
                if (again != cooldowns_.end() && again->second > day_) continue;
                if (!ev.eval(beat.conditions, actor, plan.target)) continue;
                if (!rngPlans_.chance(beat.chance)) continue;
                copy.beatsSeen.push_back(beat.id);
                cooldowns_[{actor.id, beatKey}] = day_ + 6 * kDaysPerYear;
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
        if (!p.alive() || p.household == kNone || p.id == head || p.id == actingHead()) continue;
        if (ageYears(p) < 16 || activePlan(p.id)) continue;
        // Beside what they mean to do (minds.cpp), people also act on impulse now and then.
        double c = inPlayerHouse(p.household) ? 0.006 : 0.006;
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
