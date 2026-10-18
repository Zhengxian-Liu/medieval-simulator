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

std::vector<const ActivePlan*> World::plansOf(Id actor) const {
    std::vector<const ActivePlan*> out;
    for (const auto& p : plans_)
        if (p.actor == actor) out.push_back(&p);
    return out;
}

bool World::planRoom(Id actor) const {
    if (actor == kNone) return false;
    size_t slots = actor == actingHead() ? 2 : 1;
    return plansOf(actor).size() < slots;
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
            if (ours && !planRoom(c.actor)) {
                log(1, "〔計畫〕" + name + "沒辦成：" + (c.actor == actingHead() ? "手上已經有兩件事在辦。" : "手上已經有一件事在辦。"));
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
            if (c.plan == "parcel" && (c.subject < 0 || c.subject >= static_cast<Id>(parcels_.size()) || parcels_[c.subject].gone)) continue;
            if (c.plan == "parcel") addWant(playerHousehold_, "parcel", c.subject, kNone, "", "盯上了");
            else if (c.plan.rfind("office:", 0) == 0) {
                const Office* of = office(c.plan.substr(7));
                if (!of) continue;
                Id man = familyOfficeMan(of->id, c.subject);
                std::string bar = familyOfficeBar(of->id);
                if (man != kNone) {
                    if (Want* wt = wantOf(playerHousehold_, "office", kNone, of->id)) wt->subject = man;
                    else addWant(playerHousehold_, "office", kNone, man, of->id, "想當");
                }
                else log(1, "【差事】你家爭不了" + of->name + "：" + bar + "。");
            }
            else if (c.plan == "drop" && c.subject >= 0 && c.subject < static_cast<Id>(wants_.size()) && inPlayerHouse(wants_[c.subject].house))
                dropWant(wants_[c.subject], "放下了");
        } else if (c.type == "focus") {
            // plan = the focus id, target = whose (design/life-paths.md 年度重心). Free: it is how they spend the year.
            setFocus(c.target, c.plan);
        } else if (c.type == "cancel_plan") {
            // c.plan names which one when two are going; empty: the first.
            for (size_t k = 0; k < plans_.size(); ++k)
                if (plans_[k].actor == c.actor && (c.plan.empty() || plans_[k].plan == c.plan)) {
                    endPlan(k, "cancel");
                    break;
                }
        }
    }
}

// Why a plan cannot be started now, in the player's words ("" when it can, or when nothing better is known).
std::string World::planBlock(Id actorId, const PlanDef& pd) {
    if (actorId == kNone) return "";
    auto seasonsTo = [&](int until) { return std::to_string(std::max(1, static_cast<int>(std::ceil((until - day_) / (kDaysPerYear / 4.0))))); };
    auto cd = cooldowns_.find({actorId, "plan:" + pd.id});
    if (cd != cooldowns_.end() && cd->second > day_) return "上回才辦過，還要 " + seasonsTo(cd->second) + " 季才能再辦";
    for (const auto& p : plans_)
        if (p.actor == actorId && p.plan == pd.id) return "正在辦";
    if (!planRoom(actorId)) return actorId == actingHead() ? "手上已經有兩件事在辦" : "手上已經有一件事在辦";
    Person& actor = people_[actorId];
    EventRunner ev(*this);
    if (!ev.eval(pd.actor, actor, kNone)) return pd.needs.empty() ? "眼下不合條件" : "要：" + pd.needs;
    if (!pd.subject.isNull()) {
        std::vector<Id> pool = actor.children;
        if (pd.subject.stringOr("pick", "any_child") == "kin_at_home")
            for (Id m : households_[actor.household].members)
                if (m != actorId && m != actor.spouse && people_[m].home == kNone && std::find(pool.begin(), pool.end(), m) == pool.end())
                    pool.push_back(m);
        Id waiting = kNone;
        int until = 0;
        bool any = false;
        for (Id c : pool) {
            const Person& cp = people_[c];
            if (!cp.alive() || cp.household == kNone || !ev.eval(pd.subject["conditions"], cp, actorId)) continue;
            any = true;
            auto scd = cooldowns_.find({c, "plan:" + pd.id});
            if (scd != cooldowns_.end() && scd->second > day_) {
                if (waiting == kNone) waiting = c, until = scd->second;
            } else {
                waiting = kNone;
                break;
            }
        }
        if (!any) return "家裡沒有合適的人";
        if (waiting != kNone) return people_[waiting].firstName + " 上回的事才落空，還要 " + seasonsTo(until) + " 季才好再辦";
    }
    if (pd.id == "arrange_match" || pd.id == "court") return "村裡眼下沒有年紀相當、身分相配、還沒成親的人";
    return "眼下找不到合適的對象";
}

std::vector<PlanOption> World::availablePlans(Id actorId) {
    std::vector<PlanOption> out;
    if (actorId == kNone) return out;
    Person& actor = people_[actorId];
    if (!actor.alive() || actor.household == kNone || !planRoom(actorId)) return out;
    EventRunner ev(*this);
    for (const auto& pd : defs_.plans) {
        if (std::any_of(plans_.begin(), plans_.end(), [&](const ActivePlan& p) { return p.actor == actorId && p.plan == pd.id; })) continue;
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
                if ((pd.id == "arrange_match" || pd.id == "court") && inPlayerHouse(tp.household)) {
                    auto q = matchQuietUntil_.find(t);
                    if (q != matchQuietUntil_.end() && q->second > day_) return false;
                }
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
    if (!pd || actorId == kNone || !planRoom(actorId)) return false;
    for (const auto& p : plans_)
        if (p.actor == actorId && p.plan == planId) return false;
    Person& actor = people_[actorId];
    if (!actor.alive() || actor.household == kNone) return false;
    EventRunner check(*this);
    if (!check.eval(pd->actor, actor, kNone)) return false;
    if (!pd->target.isNull() && (target == kNone || !people_[target].alive() || people_[target].household == kNone))
        return false;
    // Life-changing for the family: the one keeping the house says yes or no first.
    if (!familyAllows(actorId, *pd, target)) return false;
    bool consented = false;
    if (pd->consent && target != kNone && (target == actingHead() || target == playerHead()) && !inPlayerHouse(actor.household)) {
        if (!headConsents(actorId, *pd, target)) return false;
        consented = true;
    }

    plans_.push_back({actorId, target, subject, planId, 0, day_, {}});
    if (consented) plans_.back().beatsSeen.push_back("_consent");
    ++counters_["plan:" + planId + ":start"];
    if (isPlayerFamily(actor)) ++counters_["plan_family:" + planId + ":start"];
    // The head taking up a child's cause (a match for them) is what that child was waiting for.
    if (actorId == playerHead() && subject != kNone) answerWant(subject, "plan:" + planId);
    bool home = inPlayerHouse(actor.household);
    if (home && !pd->startLog.empty()) {
        EventRunner ev(*this, &plans_.back());
        log(1, (actorId == actingHead() ? "【計畫】" : "【家人】") + ev.format(pd->startLog, actor, target));
    }
    return true;
}

namespace {
// What a family member asks the head, per plan: the plans a grown son or daughter may take up on their own that change
// the family's life (whom they marry, money for letters, a scheme against another house).
const char* familyAskText(const std::string& plan) {
    if (plan == "court") return "{first}來跟你說：心裡有了 {target}，想去追求{target_pron}。成了，就是一門親事。";
    if (plan == "study") return "{first}想每天收工後去跟神父學認字，每月要花家裡 1 便士，少說也得一兩年。";
    if (plan == "revenge") return "{first}咽不下 {target} 家那口氣，想暗中報復。萬一敗露，全家都抬不起頭。";
    if (plan == "slander") return "{first}看 {target} 不順眼，想在井邊說{target_pron}的閒話。萬一被查出來，要上莊園法庭。";
    return nullptr;
}
}  // namespace

bool World::familyAllows(Id actorId, const PlanDef& pd, Id target) {
    const Person& a = people_[actorId];
    Id acting = actingHead();
    const char* ask = familyAskText(pd.id);
    if (!ask || acting == kNone || actorId == acting || !inPlayerHouse(a.household)) return true;
    EventRunner ev(*this);
    std::string sit = ev.format(ask, a, target);
    int r = askPlayer("家人的打算", sit, {"准", "不准"});
    bool yes;
    if (r >= 0) {
        yes = r == 0;
    } else {
        // Without a player the head answers by temper: a match and letters are welcome, a scheme only to one who
        // hates that house too.
        const Person& h = people_[acting];
        bool scheme = pd.id == "revenge" || pd.id == "slander";
        double hate = target != kNone ? -opinion(acting, target) : 0;
        double c = !scheme ? (pd.id == "study" && households_[a.household].money < 40 ? 0.5 : 0.85)
                           : 0.15 + (hate > 30 ? 0.35 : 0) + (hasTrait(h, "vengeful") || hasTrait(h, "cruel") ? 0.3 : 0) - (hasTrait(h, "kind") ? 0.1 : 0);
        yes = rngPlans_.chance(std::clamp(c, 0.0, 1.0));
    }
    log(1, "〔家人的打算〕" + sit + (r >= 0 ? "（選擇：" + std::string(yes ? "准" : "不准") + "）" : yes ? "家裡點了頭。" : "家裡沒答應。"));
    if (!yes) {
        // Told no: not asked again for a year, and it stings a little.
        cooldowns_[{actorId, "plan:" + pd.id}] = day_ + kDaysPerYear;
        addOpinion(actorId, acting, "forbade_plan", -6, 4);
    }
    return yes;
}

bool World::headConsents(Id actorId, const PlanDef& pd, Id target) {
    const Person& a = people_[actorId];
    const Person& h = people_[target];
    std::string sit = fullName(actorId) + "（" + std::to_string(int(ageYears(a))) + " 歲" +
                      (a.spouse != kNone && people_[a.spouse].alive() ? "，有家室" : "") + "；你對" + (a.sex == Sex::Male ? "他" : "她") + " " +
                      std::to_string(std::lround(opinion(target, actorId))) + "）這陣子總找機會跟你說話，看你的眼神不一樣。" +
                      (h.spouse != kNone && people_[h.spouse].alive() ? "你家裡還有 " + fullName(h.spouse) + "。" : "");
    const std::string title = shortName(actorId) + " 對你有意思";
    int r = askPlayer(title, sit, {"拒絕", "順水推舟"});
    bool yes;
    if (r >= 0) {
        yes = r == 1;
    } else {
        EventRunner ev(*this);
        double w = ev.weight(pd.aiWeight, h);  // the same temper that would start one
        yes = rngPlans_.chance(std::clamp(w / (w + 25), 0.0, 0.9));
    }
    log(1, "〔" + title + "〕" + sit + (r >= 0 ? "（選擇：" + std::string(yes ? "順水推舟" : "拒絕") + "）" : yes ? "你沒有推開。" : "你裝作沒看懂。"));
    if (!yes) {
        cooldowns_[{actorId, "plan:" + pd.id}] = day_ + std::max(1, pd.retryYears) * kDaysPerYear;
        addOpinion(actorId, target, "rebuffed", -8, 5);
    }
    return yes;
}

void World::endPlan(size_t index, const std::string& requested) {
    std::string outcome = requested;
    ActivePlan plan = plans_[index];
    plans_.erase(plans_.begin() + static_cast<long>(index));
    ++counters_["plan:" + plan.plan + ":" + outcome];
    const PlanDef* pd = defs_.plan(plan.plan);
    // After a failure the plan waits its full time; one that came off can be taken up again within the year.
    if (pd->retryYears > 0)
        cooldowns_[{plan.actor, "plan:" + plan.plan}] = day_ + (outcome == "success" ? std::min(pd->retryYears, 1) : pd->retryYears) * kDaysPerYear;
    if (plan.plan == "arrange_match" && outcome == "fail" && plan.subject != kNone) lastMatchSubject_[plan.actor] = plan.subject;
    // A child who has just been through a match (or refused one) is not offered round again at once.
    if (plan.subject != kNone) cooldowns_[{plan.subject, "plan:" + plan.plan}] = day_ + 2 * kDaysPerYear;
    Person& actor = people_[plan.actor];
    if (!actor.alive() || actor.household == kNone) return;
    bool actorHome = inPlayerHouse(actor.household);
    bool victimHome = plan.target != kNone && inPlayerHouse(people_[plan.target].household);
    EventRunner ev(*this, &plan);
    // A family member's own plan is theirs, told as theirs (「艾格妮絲 自己的「追求」落空了」), not in the head's voice.
    bool own = actorHome && plan.actor != actingHead();
    std::string whose = own ? "【家人】" + fullName(plan.actor) + " 自己的「" + pd->name + "」" : "「" + pd->name + "」";

    if (outcome == "aborted") return;  // called off in a beat that already said so: no failure, no deed, no talk
    if (outcome == "cancel") {
        if (actorHome) log(1, fullName(plan.actor) + " 放棄了「" + pd->name + "」。");
        return;
    }
    if (outcome == "lost_target") {
        Id gone = plan.target != kNone && (!people_[plan.target].alive() || people_[plan.target].household == kNone) ? plan.target : plan.subject;
        if (actorHome && gone != kNone) log(1, whose + "落空了：" + fullName(gone) + " 已經不在了。");
        return;
    }
    if (outcome == "married_off") {
        // The one who married someone else, by the same test monthlyPlans ended it on.
        auto wedTo = [&](Id x, Id other) {
            return x != kNone && people_[x].spouse != kNone && people_[x].spouse != other && people_[people_[x].spouse].alive();
        };
        Id partner = plan.plan == "court" ? plan.actor : plan.subject;
        Id wed = wedTo(plan.subject, plan.target) ? plan.subject : wedTo(plan.target, partner) ? plan.target : plan.actor;
        if (actorHome) log(1, whose + "落空了：" + fullName(wed) + " 已經另結了親。");
        return;
    }
    // An affair aimed at the one keeping the player's house goes nowhere without their yes (asked here when the plan
    // began before they kept the house).
    if (pd->consent && outcome == "success" && plan.target != kNone && (plan.target == actingHead() || plan.target == playerHead()) && !actorHome &&
        std::find(plan.beatsSeen.begin(), plan.beatsSeen.end(), "_consent") == plan.beatsSeen.end() && !headConsents(plan.actor, *pd, plan.target))
        return;
    // Someone courting the player's head, or matching their own child with one of the player's house: the family answers.
    bool matching = plan.plan == "court" || plan.plan == "arrange_match";
    if (matching && !actorHome && victimHome && (outcome == "success" || outcome == "fail")) {
        Id acting = actingHead();
        const Person& t = people_[plan.target];
        bool ours = plan.target == acting || t.father == acting || t.mother == acting || inPlayerHouse(t.household);
        auto quiet = matchQuietUntil_.find(plan.target);
        if (ours && acting != kNone && quiet != matchQuietUntil_.end() && quiet->second > day_) return;  // the family said no lately
        if (ours && acting != kNone) {
            Id suitor = plan.plan == "arrange_match" && plan.subject != kNone ? plan.subject : plan.actor;
            const Person& su = people_[suitor];
            const Household& ah = households_[actor.household];
            std::string sit = fullName(suitor) + "（" + std::to_string(int(ageYears(su))) + " 歲，家裡 " +
                              std::to_string(int(ah.acres)) + " 畝地；你對他們 " + std::to_string(int(opinion(acting, suitor))) + "）" +
                              (plan.plan == "arrange_match" ? "家託媒人來" : "託人來") + "向 " + (plan.target == acting ? std::string("你") : fullName(plan.target)) + " 求親。";
            const Household& th = households_[t.household];
            if (t.sex == Sex::Female && plan.target != th.head)
                sit += "（照你家的身分，嫁妝約 " + std::to_string(int(std::min(th.money * 0.2, dowryCap(th)))) + " 便士。）";
            int r = askPlayer("求親", sit, {"答應", "回絕"});
            if (r == 1) {
                log(1, "〔求親〕" + sit + "（選擇：回絕）");
                recordDeed("refused_proposal", plan.target, plan.actor);
                matchQuietUntil_[plan.target] = day_ + 2 * kDaysPerYear;
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
                        (*buy)["price"].asNumber(), !((*buy)["wanted"].isBool() && !(*buy)["wanted"].asBool()), plan.priceAdd);
        if (!deal.ok) {
            if (actorHome) log(1, "「" + pd->name + "」沒談成：" + (deal.acres <= 0 ? fullName(plan.target) + " 家已經沒有地可讓" : "手上的錢不夠了") + "。");
            return;
        }
    }
    // Someone after something of the head's own (a parcel, a hand): the head says yes or no, not the dice.
    if (!pd->targetAsk.empty() && pd->targetAnswers.size() >= 2 && !actorHome && plan.target == actingHead() &&
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
    } else if (own && pd->secret && pd->consent && outcome == "success") {
        // A family member's affair is theirs to hide: the head comes to hear of it by stages (hooks.cpp familyAffairs).
    } else if (!line.empty() && (actorHome || (victimHome && pd->notifyTarget))) {
        log(1, (own ? std::string("【家人】") : outcome == "success" ? "【成功】" : outcome == "exposed" ? "【敗露】" : "【失敗】") + line);
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
        // A suit to the lord goes on to the new lord when the old one dies: it is the manor that is asked.
        if (plan.target != kNone && !people_[plan.target].alive() && pd && pd->target.stringOr("pick", "") == "lord" && lord_ != kNone &&
            lord_ != plan.target && people_[lord_].alive()) {
            plan.target = lord_;
            if (inPlayerHouse(actor.household)) log(1, "「" + pd->name + "」改向新的領主 " + fullName(lord_) + " 說。");
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
                // A beat is the doer's side of it (their haggling, their doubts): told only when the doer is the family.
                int importance = actorHome ? 1 : 0;
                ev.runEvent(beat, actor, plan.target, importance);
                break;
            }
            // Effects may have added people; re-find the plan by actor and plan (a head may have two going).
            for (auto& p : plans_)
                if (p.actor == copy.actor && p.plan == copy.plan) {
                    p.progress = copy.progress;
                    p.beatsSeen = copy.beatsSeen;
                    p.priceAdd = copy.priceAdd;
                    p.aborted = copy.aborted;
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
        if (cur.aborted) {
            endPlan(i, "aborted");
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
