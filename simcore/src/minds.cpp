// The villagers' minds. Every villager is simulated at a level of detail set by how close they are to
// the player's family in the web of relations (rings, recomputed each season). Each one holds a yearly
// intention (what they mean to do and why) drawn from their ambition, grudges, friendships and hardship,
// and acts on it. Choices of villagers in the spotlight can be handed to a model through the decider.
#include <algorithm>
#include <set>
#include <cmath>

#include "simcore/events.h"

namespace sim {

namespace {

const char* pron(const Person& p) { return p.sex == Sex::Male ? "他" : "她"; }

struct Cand {
    Intention in;
    double score = 0;
};

}  // namespace

void World::computeRings() {
    ring_.assign(people_.size(), kFar);
    ringScore_.assign(people_.size(), 0);
    shownScore_.assign(people_.size(), 0);
    if (playerHousehold_ == kNone) return;
    Id head = playerHead();
    const Household& ph = households_[playerHousehold_];

    // Deeds of the last two years between the family and others draw those others closer.
    // (The family only sees what is not secret: `shown` is the part of the score it knows about.)
    std::map<Id, double> recent, recentShown;
    for (auto it = deeds_.rbegin(); it != deeds_.rend() && day_ - it->day < 2 * kDaysPerYear; ++it) {
        if (it->actor == kNone || it->target == kNone) continue;
        bool a = isPlayerFamily(people_[it->actor]), t = isPlayerFamily(people_[it->target]);
        const DeedDef* def = defs_.deed(it->type);
        bool secret = def && def->visibility == "secret";
        if (a && !t) recent[it->target] += 20;
        if (t && !a) recent[it->actor] += 20;
        if (a && !t && !secret) recentShown[it->target] += 20;
        if (t && !a && !secret) recentShown[it->actor] += 20;
    }
    std::vector<std::pair<double, Id>> spot;
    for (const auto& p : people_) {
        if (!p.alive() || p.household == kNone) continue;
        if (inPlayerHouse(p.household) || inPlayerHouse(p.home)) {
            ring_[p.id] = kHome;
            continue;
        }
        double s = 0, hidden = 0;
        for (const auto& ap : plans_) {
            bool secret = defs_.plan(ap.plan)->secret;
            if (ap.actor == p.id && ap.target != kNone && inPlayerHouse(people_[ap.target].household)) {
                s += 100;
                if (secret) hidden += 100;
            }
            if (ap.target == p.id && inPlayerHouse(people_[ap.actor].household)) s += 100;
        }
        if (head != kNone && ageYears(p) >= 16) {
            int st = feudStage(p.id, head);
            s += st == 3 ? 90 : st == 2 ? 70 : st == 1 ? 25 : 0;
            double m = std::max(std::abs(opinion(p.id, head)), std::abs(opinion(head, p.id)));
            s += m >= 60 ? 60 : m >= 30 ? 30 : m >= 15 ? 10 : 0;
            if (closeKin(p.id, head)) s += 40;
        }
        const Household& h = households_[p.household];
        if (h.head == p.id) {
            if ((ph.creditor == p.id && ph.debt > 0.5) || (ph.heldCreditor == p.id && ph.heldDebt > 0.5)) s += 80;
            if ((h.creditor == head && h.debt > 0.5) || (h.heldCreditor == head && h.heldDebt > 0.5)) s += 60;
        }
        if (p.id == lord_ || p.id == priest_ || p.flags.count("reeve")) s += 30;
        auto r = recent.find(p.id);
        if (r != recent.end()) s += std::min(60.0, r->second);
        auto rs = recentShown.find(p.id);
        hidden += std::min(60.0, r != recent.end() ? r->second : 0.0) - std::min(60.0, rs != recentShown.end() ? rs->second : 0.0);
        auto n = noticed_.find(p.id);
        if (n != noticed_.end() && n->second > day_) s += 30;
        ringScore_[p.id] = static_cast<float>(s);
        shownScore_[p.id] = static_cast<float>(s - hidden);
        if (s >= 80) spot.push_back({s, p.id});
        else if (s >= 25) ring_[p.id] = kNear;
    }
    // The spotlight holds a handful at most; the rest of those with business stay near.
    std::sort(spot.begin(), spot.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    for (size_t i = 0; i < spot.size(); ++i) ring_[spot[i].second] = i < 6 ? kSpotlight : kNear;
    // Those just stepped into the spotlight make up their minds again, now that the family matters to them.
    for (size_t i = 0; i < spot.size() && i < 6; ++i) {
        Person& p = people_[spot[i].second];
        if (ageYears(p) >= 16 && p.intent.kind != "done" && day_ - p.intent.since >= 80) chooseIntention(p);
    }
}

std::vector<Id> World::peopleOfNote() const {
    // As the family sees it: schemes against it do not show.
    std::vector<Id> out;
    for (const auto& p : people_)
        if (p.alive() && p.household != kNone && p.id < static_cast<Id>(shownScore_.size()) && shownScore_[p.id] >= 25 &&
            ringOf(p.id) != kHome)
            out.push_back(p.id);
    std::sort(out.begin(), out.end(), [&](Id a, Id b) { return shownScore_[a] != shownScore_[b] ? shownScore_[a] > shownScore_[b] : a < b; });
    return out;
}

bool World::seemsBusy(Id id) const { return id >= 0 && id < static_cast<Id>(shownScore_.size()) && shownScore_[id] >= 80; }

std::string World::mindOf(const Person& p) const {
    std::string s = fullName(p.id) + "，" + std::to_string(int(ageYears(p))) + " 歲";
    if (std::string occ = occupationName(p); !occ.empty()) s += "，" + occ;
    std::string t;
    for (const auto& tr : p.traits)
        if (defs_.traits.at(tr).category == "personality") t += (t.empty() ? "" : "、") + defs_.traits.at(tr).name;
    if (!t.empty()) s += "。個性：" + t;
    if (p.household != kNone) {
        const Household& h = households_[p.household];
        s += "。家裡 " + std::to_string(int(h.acres)) + " 畝地，存錢 " + std::to_string(int(h.money)) + " 便士" +
             (breadShort(h) > 0 ? "，糧不夠吃到秋收" : "") + (h.debt > 0.5 ? "，欠著債" : "") + "，是" + standingName(standing(h.id));
    }
    if (const AmbitionDef* a = defs_.ambition(p.ambition)) s += "。這輩子想要：" + a->name;
    if (!p.intent.why.empty() && p.intent.kind != "none") s += "。今年的打算：" + p.intent.why;
    if (p.stress >= 40) s += "。心裡壓著" + (p.stressCause.empty() ? std::string("很多事") : p.stressCause);
    return s + "。";
}

std::string World::feelingOf(const Person& p, Id about) const {
    if (about == kNone || about == p.id) return "";
    double op = opinion(p.id, about);
    // Words first: a decision model weighs "a close friend" more readily than "+78".
    const char* word = op >= 60 ? "非常親近" : op >= 30 ? "有好感" : op >= 10 ? "還算友善" : op > -10 ? "沒什麼感覺" : op > -30 ? "不太喜歡" : op > -60 ? "厭惡" : "恨之入骨";
    std::string s = "對 " + fullName(about) + "：" + word + "（好感 " + std::to_string(int(op));
    int n = 0;
    for (const auto& [lab, v] : opinionBreakdown(p.id, about)) {
        if (std::abs(v) < 2 || n >= 3) continue;
        std::string l = opinionLabel(lab);
        // The labels are written for the family's report ("you did it"); here it is this person who did.
        if (auto at = l.find("（你做的）"); at != std::string::npos) l.replace(at, std::string("（你做的）").size(), "（自己做的）");
        // "眼紅你家" means the family being looked at, named from this villager's side.
        if (auto at = l.find("你家"); at != std::string::npos) {
            const Person& a = people_[about];
            l.replace(at, std::string("你家").size(), " " + (a.dynasty != kNone ? dynasties_[a.dynasty].surname : a.firstName) + " 家");
        }
        s += "；" + l + (v > 0 ? " +" : " ") + std::to_string(int(v));
        ++n;
    }
    s += "）";
    int st = feudStage(p.id, about);
    if (st > 0) s += std::string("，兩家") + (st == 3 ? "是世仇" : st == 2 ? "結了怨" : "有嫌隙");
    return s + "。";
}

bool World::npcChoiceFor(const Person& p, const std::string& title, const std::string& situation, const std::vector<std::string>& options,
                         const std::vector<double>& weights, const std::vector<Id>& about, int fallback, NpcChoice& c) const {
    if (options.size() < 2 || !npcInScope(p.id)) return false;
    c = NpcChoice{};
    c.who = p.id;
    c.title = title;
    c.situation = situation;
    c.mind = mindOf(p);
    // Feelings about everyone the options touch, and about the family's head, who is why they are in the spotlight.
    std::vector<Id> seen;
    for (Id a : about) {
        if (a == kNone || a == p.id || std::find(seen.begin(), seen.end(), a) != seen.end()) continue;
        seen.push_back(a);
        c.feelings += feelingOf(p, a);
    }
    // The family's head only when they matter to this person; a far villager's choice is about their own circle.
    if (playerHead() != kNone && playerHead() != p.id && std::find(seen.begin(), seen.end(), playerHead()) == seen.end() &&
        (ringOf(p.id) >= kNear || std::abs(opinion(p.id, playerHead())) >= 15))
        c.feelings += feelingOf(p, playerHead());
    c.options = options;
    c.weights = weights;
    c.fallback = fallback;
    return true;
}

int World::npcAnswer(const NpcChoice& c, int r, const std::string& reason) {
    ++counters_["npc_decider:asked"];
    if (r < 0 || r >= static_cast<int>(c.options.size())) return -1;
    ++counters_["npc_decider:answered"];
    Person& p = people_[c.who];
    p.lastReason = reason;
    pendingReason_ = reason.empty() ? "" : p.firstName + " 心想：「" + reason + "」";
    return r;
}

int World::npcDecide(const Person& p, const std::string& title, const std::string& situation, const std::vector<std::string>& options,
                     const std::vector<double>& weights, const std::vector<Id>& about, int fallback) {
    NpcChoice c;
    if (!npcChoiceFor(p, title, situation, options, weights, about, fallback, c)) return -1;
    std::string reason;
    int r = npcDecider_(c, reason);
    return npcAnswer(c, r, reason);
}

void World::chooseIntention(Person& p) {
    IntentDraft d;
    if (!draftIntention(p, d)) return;
    int pick = -1;
    if (d.ask) {
        std::string reason;
        pick = npcAnswer(d.choice, npcDecider_(d.choice, reason), reason);
    }
    settleIntention(p, d, pick);
}

bool World::draftIntention(Person& p, IntentDraft& d) {
    if (!p.alive() || p.household == kNone || p.id == playerHead() || p.id == actingHead()) return false;
    if (const ActivePlan* ap = activePlan(p.id)) {
        // Already at it: the intention is the plan under way.
        if (p.intent.kind != "plan" || p.intent.plan != ap->plan) {
            const PlanDef* pd = defs_.plan(ap->plan);
            p.intent = {"plan", ap->plan, ap->target, ap->subject, "正在「" + pd->name + "」", pd->secret, day_};
        }
        return false;
    }
    std::vector<PlanOption> opts = availablePlans(p.id);
    opts.erase(std::remove_if(opts.begin(), opts.end(), [&](const PlanOption& o) { return !defs_.plan(o.plan)->npc; }), opts.end());
    auto bestFor = [&](const std::string& plan, Id onlyTarget = kNone) -> const PlanOption* {
        const PlanOption* best = nullptr;
        for (const auto& o : opts)
            if (o.plan == plan && (onlyTarget == kNone || o.target == onlyTarget) && (!best || o.targetWeight > best->targetWeight)) best = &o;
        return best;
    };
    auto about = [&](Id t) -> std::string {
        if (t == kNone || people_[t].household == kNone) return "";
        const Household& th = households_[people_[t].household];
        if (breadShort(th) > 0) return "；" + people_[t].firstName + " 家的糧不夠吃到秋收";
        if (th.debt > 0.5) return "；" + people_[t].firstName + " 家欠著債";
        return "";
    };
    auto planText = [&](const PlanOption& o) {
        // Said plainly: "謀地" alone read to the model like an honest purchase.
        if (o.plan == "acquire_land" && o.target != kNone)
            return "「謀地」：想把 " + fullName(o.target) + " 家的地弄到手，最好趁他家手頭緊的時候壓價";
        std::string s = "「" + defs_.plan(o.plan)->name + "」";
        if (o.subject != kNone) s += "，替 " + fullName(o.subject);
        if (o.target != kNone) s += "，對象 " + fullName(o.target);
        return s;
    };
    // Plans that take from or hurt their target.
    auto takes = [](const std::string& plan) {
        static const std::set<std::string> taking = {"acquire_land", "slander", "blackmail", "expose", "revenge", "poach"};
        return taking.count(plan) > 0;
    };
    auto towardFamily = [&](Id t) { return t != kNone && inPlayerHouse(people_[t].household); };
    bool spotlight = ringOf(p.id) == kSpotlight;
    std::vector<Cand> cands;
    auto add = [&](Intention in, double score) {
        // In the spotlight the family is on their mind: what concerns it comes first.
        if (spotlight && towardFamily(in.target)) score += 15;
        // Few set out to take from or hurt someone they are fond of.
        if (in.kind == "plan" && in.target != kNone && takes(in.plan)) {
            double op = opinion(p.id, in.target);
            if (op >= 60) score *= 0.2;
            else if (op >= 30) score *= 0.5;
        }
        in.since = day_;
        cands.push_back({std::move(in), score});
    };
    const Household& h = households_[p.household];
    double daily = consumption(h);

    // What they want out of life.
    // A goal does not make a kind or contented man ruthless: when the way to it is taking from someone,
    // those traits hold it back (the sixth live test had a kind man set on more land squeeze a friend).
    double scruple = (hasTrait(p, "kind") ? 0.6 : 1) * (hasTrait(p, "content") ? 0.7 : 1) * (hasTrait(p, "pious") ? 0.8 : 1) *
                     (hasTrait(p, "forgiving") ? 0.8 : 1) * (hasTrait(p, "generous") ? 0.8 : 1);
    if (const AmbitionDef* amb = defs_.ambition(p.ambition))
        for (const auto& pl : amb->plans)
            if (const PlanOption* o = bestFor(pl))
                add({"plan", pl, o->target, o->subject, "想「" + amb->name + "」，打算" + planText(*o) + about(o->target), defs_.plan(pl)->secret},
                    (25 + o->weight) * (takes(pl) ? scruple : 1));

    // What the house is after (contest.cpp): a parcel another house holds, a match for one of the children.
    if (h.head == p.id)
        for (const auto& wt : wants_) {
            if (!wt.open || wt.house != p.household) continue;
            if (wt.kind == "parcel") {
                const Parcel& pc = parcels_[wt.object];
                if (pc.holder == kNone || pc.holder == p.household || households_[pc.holder].head == kNone) continue;
                Id holder = households_[pc.holder].head;
                if (const PlanOption* o = bestFor("acquire_land", holder))
                    add({"plan", "acquire_land", holder, kNone, "看上了 " + fullName(holder) + " 家的" + parcelName(pc) + "（" + wt.why + "）" + about(holder), false},
                        (30 + o->weight * 0.5) * scruple);
            } else if (wt.kind == "match") {
                for (const auto& o : opts)
                    if (o.plan == "arrange_match" && o.target == wt.object && o.subject == wt.subject) {
                        add({"plan", "arrange_match", o.target, o.subject, "替 " + fullName(o.subject) + " 說親，看中了 " + fullName(o.target) + "（" + wt.why + "）", false},
                            28 + o.weight * 0.5);
                        break;
                    }
            }
        }

    // A grudge: the person they hate most, and whether there is a way to hurt them.
    Id foe = kNone;
    double worst = -30;
    for (const auto& q : people_) {
        if (!q.alive() || q.household == kNone || q.household == p.household || ageYears(q) < 14) continue;
        double op = opinion(p.id, q.id);
        if (op < worst) {
            worst = op;
            foe = q.id;
        }
    }
    if (foe != kNone) {
        std::string why;
        for (const auto& [lab, v] : opinionBreakdown(p.id, foe))
            if (v < 0) {
                why = opinionLabel(lab);
                break;
            }
        double heat = (-worst - 20) / 2 + (hasTrait(p, "vengeful") ? 10 : 0) + (hasTrait(p, "wrathful") ? 6 : 0) -
                      (hasTrait(p, "forgiving") ? 10 : 0) - (hasTrait(p, "kind") ? 6 : 0) + (hasTrait(p, "cruel") ? 4 : 0);
        // Mild people swallow a grudge more often than they act on it: acting is scaled down, brooding is not.
        double mild = (hasTrait(p, "content") ? 0.5 : 1) * (hasTrait(p, "pious") ? 0.6 : 1) * (hasTrait(p, "shy") ? 0.7 : 1) *
                      (hasTrait(p, "forgiving") ? 0.5 : 1) * (hasTrait(p, "kind") ? 0.6 : 1);
        for (const char* pl : {"revenge", "expose", "blackmail", "slander"})
            if (const PlanOption* o = bestFor(pl, foe)) {
                add({"plan", pl, foe, kNone, "記恨 " + fullName(foe) + (why.empty() ? "" : "（" + why + "）") + "，打算" + planText(*o), defs_.plan(pl)->secret}, (heat + o->weight * 0.5) * mild);
                break;
            }
        // Most grudges simmer for years before anyone acts on them.
        add({"brood", "", foe, kNone, "對 " + fullName(foe) + " 懷恨在心" + (why.empty() ? "" : "（" + why + "）") + "，在等機會", true}, 8 + heat * 0.5);
    }

    // A friend in trouble, if the house can spare something. (The lord's house gives through the steward, not by hand.)
    if ((h.food > daily * 250 || h.money >= 40) && !gentry(p.id)) {
        Id friendId = kNone;
        double best = 35;
        for (const auto& q : people_) {
            if (!q.alive() || q.household == kNone || q.household == p.household) continue;
            const Household& qh = households_[q.household];
            if (qh.head != q.id || !(qh.lean || qh.debt > 0.5 || qh.food < consumption(qh) * 90)) continue;
            auto hu = helpedUntil_.find(qh.id);
            if (hu != helpedUntil_.end() && hu->second > day_) continue;
            double op = opinion(p.id, q.id);
            if (op > best) {
                best = op;
                friendId = q.id;
            }
        }
        if (friendId != kNone) {
            std::string why;
            for (const auto& [lab, v] : opinionBreakdown(p.id, friendId))
                if (v > 0) {
                    why = opinionLabel(lab);
                    break;
                }
            add({"help", "", friendId, kNone,
                 fullName(friendId) + " 家今年日子難過，" + pron(p) + "想幫一把" + (why.empty() ? "" : "（" + why + "）"), false},
                best / 3 + (hasTrait(p, "kind") ? 8 : 0) + (hasTrait(p, "generous") ? 8 : 0) - (hasTrait(p, "greedy") ? 8 : 0));
        }
    }

    // Family business, hardship, and the young.
    if (const PlanOption* o = bestFor("arrange_match"))
        add({"plan", "arrange_match", o->target, o->subject, "替 " + fullName(o->subject) + " 說親，看中了 " + fullName(o->target), false}, 15 + o->weight * 0.5);
    if (h.lean || h.money < 20)
        for (const char* pl : {"hire_out", "poach"})
            if (const PlanOption* o = bestFor(pl))
                add({"plan", pl, o->target, kNone, std::string(h.lean ? "家裡缺糧" : "手頭沒錢") + "，打算" + planText(*o), defs_.plan(pl)->secret}, 18 + o->weight);
    if (const PlanOption* o = bestFor("court"))
        add({"plan", "court", o->target, kNone, "看上了 " + fullName(o->target), false}, 8 + o->weight * 0.5);
    if (const PlanOption* o = bestFor("befriend"))
        add({"plan", "befriend", o->target, kNone, "想跟 " + fullName(o->target) + " 交個朋友", false}, 4 + o->weight * 0.3);
    // Anything else they might take up, weakly.
    for (const auto& o : opts) {
        bool seen = std::any_of(cands.begin(), cands.end(), [&](const Cand& c) { return c.in.plan == o.plan; });
        if (seen) continue;
        if (const PlanOption* b = bestFor(o.plan))
            add({"plan", b->plan, b->target, b->subject, "打算" + planText(*b), defs_.plan(b->plan)->secret}, b->weight * 0.3);
    }

    // Most years most people just get on with the harvest.
    add({"none", "", kNone, kNone, "過一天算一天", false},
        30 + (hasTrait(p, "content") ? 15 : 0) + (hasTrait(p, "diligent") ? 5 : 0) + (hasTrait(p, "lazy") ? 8 : 0) -
            (hasTrait(p, "ambitious") ? 12 : 0));
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    for (const auto& c : cands) {
        d.options.push_back(c.in);
        d.scores.push_back(c.score);
    }
    // In the spotlight the model weighs in only when one of the leading ideas touches the family: that is where
    // the player sees it. With a wider scope it decides everyone's year.
    bool touchesFamily = false;
    for (size_t i = 0; i < cands.size() && i < 3; ++i) touchesFamily = touchesFamily || towardFamily(cands[i].in.target) || towardFamily(cands[i].in.subject);
    if (npcInScope(p.id) && cands.size() > 1 && (touchesFamily || npcScope_ < kSpotlight)) {
        std::vector<std::string> options;
        std::vector<double> weights;
        std::vector<Id> about;
        int fallback = -1;
        for (size_t i = 0; i < cands.size() && i < 3; ++i) {
            if (cands[i].in.kind == "none") fallback = static_cast<int>(i);
            options.push_back(cands[i].in.why);
            weights.push_back(std::max(0.1, cands[i].score));
            about.push_back(cands[i].in.target);
        }
        d.ask = npcChoiceFor(p, "今年的打算", fullName(p.id) + " 在想今年要做什麼。", options, weights, about, fallback, d.choice);
    }
    return !d.options.empty();
}

void World::settleIntention(Person& p, const IntentDraft& d, int pick) {
    pendingReason_.clear();
    if (pick < 0) {
        std::vector<double> w;
        for (size_t i = 0; i < d.scores.size() && i < 5; ++i) w.push_back(std::max(0.1, d.scores[i]));
        pick = rngMinds_.weighted(w);
        if (pick < 0) pick = 0;
    }
    p.intent = d.options[pick];
    // Setting one's heart on someone is a longing, which their family hears of (design/season-loop.md).
    if (p.intent.kind == "plan" && p.intent.plan == "court" && p.intent.target != kNone && p.spouse == kNone)
        addConcern(p.id, "longing", p.intent.target, 45);
    ++counters_["intent:" + p.intent.kind + (p.intent.plan.empty() ? "" : ":" + p.intent.plan)];
}

void World::yearlyIntentions() {
    // A couple of far villagers start to take notice of the family this year: surprises come from afar too.
    std::vector<Id> far;
    for (const auto& p : people_)
        if (p.alive() && p.household != kNone && ringOf(p.id) == kFar && ageYears(p) >= 16 && !inPlayerHouse(p.household))
            far.push_back(p.id);
    for (int k = 0; k < 2 && !far.empty(); ++k) {
        int i = rngMinds_.range(0, static_cast<int>(far.size()) - 1);
        noticed_[far[i]] = day_ + kDaysPerYear;
        far.erase(far.begin() + i);
    }
    for (auto it = noticed_.begin(); it != noticed_.end();) it = it->second <= day_ ? noticed_.erase(it) : std::next(it);
    computeRings();
    // Every grown villager holds an intention, near the family or not: the whole village moves on its own,
    // and its stories cross one another, not only the ones that touch the player. Everyone makes up their
    // mind from how things stand at the turn of the year, so the decider can be asked about all of them at
    // once (a whole village asks some 45 questions a year; one by one that is 15–20 seconds of waiting).
    std::vector<std::pair<Id, IntentDraft>> drafts;
    std::vector<NpcChoice> asks;
    for (auto& p : people_) {
        if (!p.alive() || p.household == kNone || ageYears(p) < 16) continue;
        IntentDraft d;
        if (!draftIntention(p, d)) continue;
        if (d.ask) asks.push_back(d.choice);
        drafts.emplace_back(p.id, std::move(d));
    }
    std::vector<std::pair<int, std::string>> answers;
    if (!asks.empty()) {
        if (npcBatchDecider_) answers = npcBatchDecider_(asks);
        for (size_t i = answers.size(); i < asks.size(); ++i) {
            std::string reason;
            int r = npcDecider_(asks[i], reason);
            answers.emplace_back(r, reason);
        }
    }
    size_t a = 0;
    for (auto& [id, d] : drafts) {
        int pick = -1;
        if (d.ask) {
            pick = npcAnswer(d.choice, answers[a].first, answers[a].second);
            ++a;
        }
        settleIntention(people_[id], d, pick);
    }
}

void World::actOnIntentions() {
    for (auto& p : people_) {
        if (!p.alive() || p.household == kNone || p.id == playerHead() || p.id == actingHead() || ageYears(p) < 16) continue;
        if (p.intent.kind != "plan" && p.intent.kind != "help") continue;
        // The same pace everywhere: the rings decide who the family notices and who the model decides for,
        // not how fast the rest of the village lives.
        if (p.intent.kind == "help") {
            if (rngMinds_.chance(0.15)) helpFriend(p);
            continue;
        }
        if (activePlan(p.id)) continue;
        double c = 0.07;
        if (!rngMinds_.chance(c)) continue;
        // Still possible? Prefer the person they had in mind; failing that, anyone the plan fits.
        std::vector<PlanOption> opts = availablePlans(p.id);
        const PlanOption* pick = nullptr;
        for (const auto& o : opts)
            if (o.plan == p.intent.plan && (o.target == p.intent.target || !pick)) {
                if (o.target == p.intent.target && o.subject == p.intent.subject) {
                    pick = &o;
                    break;
                }
                if (!pick) pick = &o;
            }
        if (!pick) {
            p.intent.kind = "none";
            continue;
        }
        if (startPlan(p.id, pick->plan, pick->target, pick->subject).empty()) {
            ++counters_["intent_acted:" + pick->plan];
            p.intent.kind = "done";  // one go a year; what they meant to do still shows in the report
        }
    }
}

void World::helpFriend(Person& p) {
    Id t = p.intent.target;
    p.intent.kind = "done";
    if (t == kNone || !people_[t].alive() || people_[t].household == kNone || p.household == kNone) return;
    Household& mine = households_[p.household];
    Household& th = households_[people_[t].household];
    if (th.id == mine.id) return;
    auto hu = helpedUntil_.find(th.id);
    if (hu != helpedUntil_.end() && hu->second > day_) return;
    double theirDaily = consumption(th);
    std::string gift;
    double food = std::min(mine.food - consumption(mine) * 200, theirDaily * 60);
    double coin = 0;
    bool giveFood = (th.lean || th.food < theirDaily * 120) && food >= theirDaily * 15;
    if (giveFood) {
        gift = "一袋燕麥（夠他們家吃 " + std::to_string(int(food / theirDaily)) + " 天）";
    } else {
        coin = std::floor(std::min(mine.money * 0.3, 20.0));
        if (coin < 5) return;
        gift = " " + std::to_string(int(coin)) + " 便士";
    }
    std::string why;
    double whyV = 0;
    for (const auto& [lab, v] : opinionBreakdown(p.id, t))
        if (v > 0) {
            why = opinionLabel(lab);
            whyV = v;
            break;
        }
    std::string because = why.empty() ? "" : "（" + std::string(pron(p)) + "記著：" + why + " +" + std::to_string(int(whyV)) + "）";
    if (inPlayerHouse(mine.id)) {
        // One of the family wants to give away the family's grain or coin: the head has the say.
        // The head may know what the friend once was to this one: then it is not only grain that is asked for.
        Id head = actingHead();
        bool flame = head != kNone && head != p.id && knownFlame(head, p.id) == t;
        std::vector<std::string> opts = {"答應", "不准"};
        // Asked of the head once in a while, not every year the same.
        auto asked = cooldowns_.find({p.id, "help_ask"});
        if (asked != cooldowns_.end() && asked->second > day_) return;
        cooldowns_[{p.id, "help_ask"}] = day_ + 2 * kDaysPerYear;
        std::string sit = fullName(p.id) + " 想給 " + fullName(t) + " 家送去" + gift + "：" +
                          (giveFood ? "他們家今年日子難過" : th.lean ? "他們家手頭緊" : because.empty() ? "不為什麼，就是想表一表心意" : "想表一表心意") + because + "。";
        if (flame) {
            sit += "你心裡清楚，" + fullName(t) + " 是" + pron(p) + "當年的人。";
            opts.push_back("不准，而且挑明了：「你以為我不知道？」");
        }
        int r = askPlayer("人情", sit, opts);
        if (r == 2 && flame) {
            log(1, "〔人情〕" + fullName(p.id) + " 想接濟 " + fullName(t) + " 家。" + fullName(head) + " 冷冷地說：「你以為我不知道你跟" +
                       people_[t].firstName + "的事？」" + p.firstName + "的臉一下子白了。（選擇：挑明了）");
            addOpinion(p.id, head, "found_out", -15, 10);  // the one found out remembers being found out, not a betrayal
            addOpinion(head, p.id, "betrayed", -5, 10);
            addStress(p.id, 15, "當年的事被挑明了");
            addStress(head, -5, "");
            if (p.lover == t) endLiaison(p.id);
            for (const auto& b : beliefs_[head])
                if (b.culprit == p.id && deeds_[b.deed].type == "affair" && deeds_[b.deed].with == t) {
                    addBelief(p.id, b.deed, p.id, 100, head);
                    deedNotes_.insert({b.deed, "spoken"});
                }
            return;
        }
        if (r == 1) {
            log(1, "〔人情〕" + fullName(p.id) + " 想接濟 " + fullName(t) + " 家，家主不准。（選擇：不准）");
            addOpinion(p.id, actingHead(), "refused_kindness", -6, 3);
            addStress(p.id, 6, "沒能幫上朋友");
            return;
        }
        if (r == 0) log(1, "〔人情〕" + fullName(p.id) + " 想接濟 " + fullName(t) + " 家。（選擇：答應）");
    }
    if (giveFood) {
        mine.food -= food;
        th.food += food;
        bookFood(mine, "送人的糧", -food);
        bookFood(th, "別人送的糧", food);
    } else {
        mine.money -= coin;
        book(mine, "接濟朋友", -coin);
        th.money += coin;
        book(th, "朋友接濟", coin);
    }
    helpedUntil_[th.id] = day_ + kDaysPerYear;
    ++counters_["help_given"];
    if (inPlayerHouse(th.id)) {
        ++counters_["help_to_player"];
        log(1, "【人情】" + fullName(p.id) + " 送來" + gift + "。" + because);
    } else if (inPlayerHouse(mine.id)) {
        log(1, "【人情】" + fullName(p.id) + " 給 " + fullName(t) + " 家送去" + gift + "。");
    } else {
        log(2, fullName(p.id) + " 給 " + fullName(t) + " 家送去" + gift + "。");
    }
    recordDeed("kindness", p.id, t);
    oweFavour(t, p.id, 15, "荒年接濟過");
}

}  // namespace sim
