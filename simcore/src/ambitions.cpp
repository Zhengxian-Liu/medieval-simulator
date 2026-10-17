// Per-generation ambitions of every head of household, the family ladder, and family traditions.
#include <algorithm>

#include "simcore/events.h"

namespace sim {

int World::standing(Id hid) const {
    if (hid == kNone) return 0;
    const Household& h = households_[hid];
    if (h.head == kNone) return 0;
    const Person& hp = people_[h.head];
    if (hp.occupation == "lord" || hp.occupation == "lady" || hp.occupation == "priest" || gentry(h.head)) return -1;
    if (!hp.free) return 0;
    if (h.freehold >= 20 && h.acres >= 40) return 3;
    if (h.freehold >= 6) return 2;
    return 1;
}

const char* World::standingName(int rung) {
    switch (rung) {
        case 0: return "農奴";
        case 1: return "自由佃農";
        case 2: return "自耕農";
        case 3: return "小地主";
        default: return "莊園的人";
    }
}

bool World::kinInChurch(Id dyn) const {
    if (dyn == kNone) return false;
    for (const auto& p : people_)
        if (p.alive() && p.dynasty == dyn && (p.occupation == "clerk" || p.occupation == "priest" || p.station == "monastery")) return true;
    return false;
}

bool World::kinInTown(Id dyn) const {
    if (dyn == kNone) return false;
    for (const auto& p : people_)
        if (isAway(p) && p.dynasty == dyn && p.station.rfind("town_", 0) == 0 && p.station != "town_servant") return true;
    return false;
}

void World::chooseAmbition(Person& head) {
    EventRunner ev(*this);
    std::vector<const AmbitionDef*> pool;
    std::vector<double> weights;
    const std::string regret = head.dynasty != kNone ? dynasties_[head.dynasty].regret : "";
    for (const auto& a : defs_.ambitions) {
        if (!ev.eval(a.offer, head, kNone)) continue;
        // Skip goals that are already met.
        if (ev.eval(a.goal, head, kNone)) continue;
        pool.push_back(&a);
        double w = ev.weight(a.aiWeight, head);
        if (a.id == regret) w = w * 2 + 10;  // a parent's unfinished wish weighs on the child
        // What the last goal done opened up: the harder next step on the same road.
        if (const AmbitionDef* last = defs_.ambition(head.lastAmbition))
            if (std::find(last->next.begin(), last->next.end(), a.id) != last->next.end()) w = w * 3 + 20;
        weights.push_back(w);
    }
    if (pool.empty()) return;
    bool player = inPlayerHouse(head.household) && head.id == playerHead();
    const AmbitionDef* pick = nullptr;
    if (player) {
        // Five to choose from (Zhengxian, 2026-10-16), drawn by how much this person would want them.
        std::vector<const AmbitionDef*> offer;
        std::vector<double> ow;
        std::vector<double> w = weights;
        for (int k = 0; k < 5; ++k) {
            int i = rngMinds_.weighted(w);
            if (i < 0) break;
            offer.push_back(pool[i]);
            ow.push_back(weights[i]);
            w[i] = 0;
        }
        if (offer.empty()) return;
        std::vector<std::string> opts;
        for (const auto* a : offer) {
            std::string o = a->name + "：" + a->desc;
            if (!a->milestones.empty()) o += "（" + std::to_string(a->milestones.size() + 1) + " 步）";
            if (a->id == regret) o += "【上一代沒做完的心願】";
            opts.push_back(o);
        }
        std::string sit = fullName(head.id) + "（" + std::to_string(int(ageYears(head))) + " 歲）當了家，家族現在是" +
                          standingName(standing(head.household)) + "。這一輩子，想做成哪件事？";
        int r = offer.size() > 1 ? askPlayer("立志", sit, opts) : -1;
        if (r >= 0 && r < static_cast<int>(offer.size())) {
            pick = offer[r];
        } else {
            int i = rngMinds_.weighted(ow);
            pick = offer[i < 0 ? 0 : i];
        }
    } else {
        int i = rngMinds_.weighted(weights);
        if (i < 0) return;
        pick = pool[i];
    }
    head.ambition = pick->id;
    ++counters_[std::string(player ? "ambition_family:" : "ambition:") + pick->id + ":chosen"];
    head.ambitionSince = day_;
    head.ambitionStep = 0;
    head.ambitionDone = 0;
    if (player) {
        log(1, "【志向】" + fullName(head.id) + " 立志「" + pick->name + "」：" + pick->desc);
        if (!pick->milestones.empty()) log(1, "  第一步：" + pick->milestones[0].name + "。");
    }
    checkMilestones(head, true);
}

void World::checkMilestones(Person& head, bool quiet) {
    const AmbitionDef* a = defs_.ambition(head.ambition);
    if (!a) return;
    EventRunner ev(*this);
    bool player = inPlayerHouse(head.household) && head.id == playerHead();
    // Steps count whenever they are reached, in any order: 35 acres held before the freehold is still 35 acres.
    for (size_t i = 0; i < a->milestones.size() && i < 32; ++i) {
        if (head.ambitionDone & (1u << i) || !ev.eval(a->milestones[i].goal, head, kNone)) continue;
        const auto& m = a->milestones[i];
        head.ambitionDone |= 1u << i;
        ++head.ambitionStep;
        if (quiet) continue;
        head.reputation += 1;
        addStress(head.id, -5, "");
        if (player) {
            int nx = nextMilestone(head);
            std::string next = nx >= 0 ? "下一步：" + a->milestones[nx].name + "。" : "只差最後一步了。";
            log(1, "【里程碑】「" + a->name + "」：" + m.name + "（" + std::to_string(head.ambitionStep) + "/" +
                       std::to_string(a->milestones.size() + 1) + "）。" + next);
        }
    }
}

int World::nextMilestone(const Person& head) const {
    const AmbitionDef* a = defs_.ambition(head.ambition);
    if (!a) return -1;
    for (size_t i = 0; i < a->milestones.size() && i < 32; ++i)
        if (!(head.ambitionDone & (1u << i))) return static_cast<int>(i);
    return -1;
}

void World::yearlyAmbitions() {
    EventRunner ev(*this);
    for (auto& h : households_) {
        if (h.extinct || h.head == kNone || h.members.empty()) continue;
        Person& head = people_[h.head];
        if (!head.alive() || ageYears(head) < 16) continue;
        bool player = inPlayerHouse(h.id);
        if (head.ambition.empty()) {
            chooseAmbition(head);
            continue;
        }
        const AmbitionDef* a = defs_.ambition(head.ambition);
        if (!a) continue;
        if (!player) checkMilestones(head, true);
        Dynasty* dyn = head.dynasty != kNone ? &dynasties_[head.dynasty] : nullptr;
        if (ev.eval(a->goal, head, kNone)) {
            bool regret = dyn && dyn->regret == a->id;
            ++counters_[std::string(player ? "ambition_family:" : "ambition:") + a->id + ":done"];
            if (player) log(1, "【達成】" + fullName(h.head) + " 完成了志向「" + a->name + "」。" + (regret ? "這也是上一代的心願。" : ""));
            else if (!a->milestones.empty()) log(2, fullName(h.head) + " 完成了「" + a->name + "」。");
            if (dyn) {
                dyn->prestige += regret ? 20 : 10;
                if (regret) dyn->regret.clear();
            }
            head.reputation += 5;
            head.lastAmbition = a->id;
            noteLife(head.id, "做成了「" + a->name + "」");
            if (dyn && !a->tradition.empty() &&
                std::find(dyn->traditions.begin(), dyn->traditions.end(), a->tradition) == dyn->traditions.end()) {
                dyn->traditions.push_back(a->tradition);
                const auto& td = defs_.traditions.at(a->tradition);
                if (player) log(1, "【家族傳統】" + dyn->surname + " 家從此以「" + td.name + "」聞名：" + td.desc);
            }
            head.ambition.clear();
            chooseAmbition(head);
        } else if ((day_ - head.ambitionSince) / kDaysPerYear >= a->maxYears) {
            ++counters_[std::string(player ? "ambition_family:" : "ambition:") + a->id + ":dropped"];
            if (player) log(1, fullName(h.head) + " 放下了「" + a->name + "」的志向。這成了家裡一樁沒了的心願。");
            if (dyn) dyn->regret = a->id;
            head.ambition.clear();
            chooseAmbition(head);
        }
    }
}

void World::checkStandings() {
    for (auto& h : households_) {
        if (h.extinct || h.head == kNone || h.members.empty()) continue;
        int now = standing(h.id);
        auto it = lastStanding_.find(h.id);
        int before = it == lastStanding_.end() ? now : it->second;
        lastStanding_[h.id] = now;
        if (now == before || now < 0 || before < 0) continue;
        bool player = inPlayerHouse(h.id);
        Person& head = people_[h.head];
        std::string surname = h.dynasty != kNone ? dynasties_[h.dynasty].surname : head.firstName;
        if (now < before) {
            if (player) log(1, "【家族地位】" + surname + " 家從" + standingName(before) + "落回了" + standingName(now) + "。");
            continue;
        }
        ++counters_[std::string(player ? "rise_family:" : "rise:") + std::to_string(now)];
        if (h.dynasty != kNone) dynasties_[h.dynasty].prestige += 5 * (now - before);
        head.reputation += 3;
        if (!player) {
            log(2, surname + " 家成了" + std::string(standingName(now)) + "。");
            continue;
        }
        log(1, "【家族地位】" + surname + " 家成了" + standingName(now) + "（第 " + std::to_string(now + 1) + " 階，共 4 階）。");
        // The village notices: the greedy and the ambitious envy, others think more of the family.
        std::string envious, respectful;
        int ne = 0, nr = 0;
        for (const auto& o : households_) {
            if (o.extinct || o.head == kNone || o.id == h.id || o.members.empty()) continue;
            const Person& oh = people_[o.head];
            if (!oh.alive() || standing(o.id) < 0) continue;
            if (standing(o.id) < now && (hasTrait(oh, "greedy") || hasTrait(oh, "ambitious") || opinion(oh.id, head.id) < -10)) {
                addOpinion(oh.id, head.id, "envy", -8, 8);
                if (ne++ < 3) envious += (envious.empty() ? "" : "、") + fullName(oh.id);
            } else if (opinion(oh.id, head.id) > -10) {
                addOpinion(oh.id, head.id, "respect", 4, 8);
                if (nr++ < 3) respectful += (respectful.empty() ? "" : "、") + fullName(oh.id);
            }
        }
        if (!envious.empty()) log(1, "  " + envious + (ne > 3 ? " 等人" : "") + "眼紅了（好感 -8）。");
        if (!respectful.empty()) log(1, "  " + respectful + (nr > 3 ? " 等人" : "") + "對你家另眼相看（好感 +4）。");
        if (now == 3 && lord_ != kNone && people_[lord_].alive()) addOpinion(lord_, head.id, "worthy_tenant", 10, 0);
    }
}

void World::yearlyTown() {
    // A master in town sends something home now and then (the town stations themselves: paths.cpp).
    for (Id id : livingAway()) {
        const Person& t = people_[id];
        if (t.station != "town_master" || !rngMinds_.chance(0.4)) continue;
        Id home = t.awayFrom;
        if (home == kNone || households_[home].extinct || households_[home].head == kNone) continue;
        int gift = rngMinds_.range(10, 25);
        households_[home].money += gift;
        book(households_[home], "鎮上捎回", gift);
        if (inPlayerHouse(home)) log(1, "鎮上的 " + t.firstName + " 托人捎回 " + std::to_string(gift) + " 便士。");
    }
}

}  // namespace sim
