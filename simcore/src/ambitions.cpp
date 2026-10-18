// Per-generation ambitions of every head of household, the family ladder, family traditions, and the late game:
// the rivals and the steward who stand between a well-off family and its last goals (design/fun-r5.md D).
#include <algorithm>
#include <cmath>

#include "simcore/events.h"

namespace sim {

namespace {
std::string num(double v) { return std::to_string(static_cast<long>(std::lround(v))); }
std::string pence(double v) { return num(v) + " 便士"; }

// The first goal in a condition tree counted from what the house had (money_gain_ge…): which, and its value.
const Json* gainIn(const Json& cond, std::string& what) {
    if (cond.isArray()) {
        for (const auto& c : cond.asArray())
            if (const Json* g = gainIn(c, what)) return g;
        return nullptr;
    }
    if (!cond.isObject()) return nullptr;
    for (const auto& [k, v] : cond.asObject()) {
        for (const char* w : {"money", "acres", "freehold"})
            if (k == std::string(w) + "_gain_ge") return what = w, &v;
        if (k == "any" || k == "all" || k == "not")
            if (const Json* g = gainIn(v, what)) return g;
    }
    return nullptr;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t i = s.find(from); i != std::string::npos; i = s.find(from, i + to.size())) s.replace(i, from.size(), to);
}

bool landHungry(const std::string& ambition) {
    return ambition == "more_land" || ambition == "big_landholder" || ambition == "become_franklin" || ambition == "first_house";
}
}  // namespace

// Whose blood holds the house: the heir under age while a widow or stepfather keeps it for them, else the head.
Id World::lineHead(const Household& h) const {
    if (h.ward != kNone && people_[h.ward].alive() && h.head != kNone && people_[h.head].dynasty != people_[h.ward].dynasty) return h.ward;
    return h.head;
}

// A house is free when the line that holds it is: a stepfather who married in does not make it villein again.
bool World::houseFree(const Household& h) const {
    Id l = lineHead(h);
    return l != kNone && people_[l].free;
}

int World::standing(Id hid) const {
    if (hid == kNone) return 0;
    const Household& h = households_[hid];
    if (h.head == kNone) return 0;
    const Person& hp = people_[h.head];
    if (hp.occupation == "lord" || hp.occupation == "lady" || hp.occupation == "priest" || gentry(h.head)) return -1;
    if (!houseFree(h)) return 0;
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

// ---- What an ambition asks of this house ------------------------------------------------------

double World::ambitionGain(const Person& p, const std::string& what, const Json& v) const {
    double base = what == "money" ? p.ambitionBase.money : what == "acres" ? p.ambitionBase.acres : p.ambitionBase.freehold;
    double need = v.isNumber() ? v.asNumber() : std::max(v.numberOr("min", 0), base * v.numberOr("pct", 0) / 100);
    return what == "money" ? std::ceil(need / 10) * 10 : std::ceil(need);
}

// A small landholder's chantry has to be seen to be one: the price follows the house's standing.
double World::chantryPrice(Id house) const { return standing(house) >= 3 ? 1200 : 700; }

Id World::biggestOther(Id house) const {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id best = kNone;
    for (const auto& o : households_)
        if (!o.extinct && !o.members.empty() && o.head != kNone && o.id != house && o.id != lordHouse && people_[o.head].occupation != "priest" &&
            (best == kNone || o.acres > households_[best].acres))
            best = o.id;
    return best;
}

double World::acresLead(Id house) const {
    if (house == kNone) return -1e9;
    Id o = biggestOther(house);
    return households_[house].acres - (o != kNone ? households_[o].acres : 0);
}

std::string World::ambitionText(const Person& head, const std::string& text, const Json& goal) const {
    if (text.find('{') == std::string::npos) return text;
    std::string s = text;
    std::string what;
    if (const Json* v = gainIn(goal, what)) {
        double base = what == "money" ? head.ambitionBase.money : what == "acres" ? head.ambitionBase.acres : head.ambitionBase.freehold;
        double gain = ambitionGain(head, what, *v);
        replaceAll(s, "{gain}", num(gain));
        replaceAll(s, "{target}", num(base + gain));
        replaceAll(s, "{base}", num(base));
    }
    Id hid = head.household != kNone ? head.household : isAway(head) ? head.awayFrom : kNone;
    if (s.find("{chantry") != std::string::npos) {
        replaceAll(s, "{chantry_half}", num(chantryPrice(hid) / 2));
        replaceAll(s, "{chantry}", num(chantryPrice(hid)));
    }
    if (s.find("{top}") != std::string::npos) {
        Id o = biggestOther(hid);
        std::string top = o == kNone ? "別家" : (households_[o].dynasty != kNone ? dynasties_[households_[o].dynasty].surname : fullName(households_[o].head)) +
                                                     " 家（" + num(households_[o].acres) + " 畝）";
        replaceAll(s, "{top}", top);
    }
    return s;
}

std::string World::milestoneName(const Person& head, size_t i) const {
    const AmbitionDef* a = defs_.ambition(head.ambition);
    return a && i < a->milestones.size() ? ambitionText(head, a->milestones[i].name, a->milestones[i].goal) : "";
}

std::string World::ambitionFinal(const Person& head) const {
    const AmbitionDef* a = defs_.ambition(head.ambition);
    return a ? ambitionText(head, a->final, a->goal) : "";
}

std::string World::ambitionDesc(const Person& head) const {
    const AmbitionDef* a = defs_.ambition(head.ambition);
    return a ? ambitionText(head, a->desc, a->goal) : "";
}

// It is put down at the first turn of the year after max_years have gone by (reviewAmbition): the year before
// that is the last one it can still be done in.
int World::ambitionDeadline(const Person& head) const {
    const AmbitionDef* a = defs_.ambition(head.ambition);
    if (!a) return 0;
    return cfg_.startYear + (head.ambitionSince + kDaysPerYear - 1) / kDaysPerYear + a->maxYears - 1;
}

// ---- Choosing, and the road ---------------------------------------------------------------------

void World::chooseAmbition(Person& head, const std::string& why) {
    EventRunner ev(*this);
    // Money and land goals are counted from what the house has now (「再存 200」, not 「存到 200」).
    if (head.household != kNone) {
        const Household& hh = households_[head.household];
        head.ambitionBase.money = hh.money;
        head.ambitionBase.acres = hh.acres;
        head.ambitionBase.freehold = hh.freehold;
    }
    std::vector<const AmbitionDef*> pool;
    std::vector<double> weights;
    std::vector<int> metOf;  // steps already in hand (shown on the card)
    std::string nearly;      // left out for being half done: the plan that finishes it, for the card
    int nNearly = 0;
    const std::string regret = head.dynasty != kNone ? dynasties_[head.dynasty].regret : "";
    for (const auto& a : defs_.ambitions) {
        if (!ev.eval(a.offer, head, kNone)) continue;
        // Skip goals that are already met, or that could not be kept to.
        if (ev.eval(a.goal, head, kNone) || (!a.keep.isNull() && !ev.eval(a.keep, head, kNone))) continue;
        // Half the road already behind them is not a lifetime's goal (design/fun-r5.md D). A condition of the road
        // (no debts for a reeve) is not progress along it and does not count.
        int steps = 1, done = 0, shown = 0;
        for (const auto& m : a.milestones) {
            bool now = ev.eval(m.goal, head, kNone);
            shown += now;
            if (m.gate) continue;
            ++steps;
            done += now;
        }
        double w = ev.weight(a.aiWeight, head);
        if (done > 0 && done * 2 >= steps) {
            if (steps >= 3) {
                if (nNearly++ < 3)
                    nearly += (nearly.empty() ? "" : "、") + a.name + "（" + std::to_string(shown) + "/" + std::to_string(a.milestones.size() + 1) + "）";
                continue;
            }
            w *= 0.25;  // a two-step road with its one step (the fee) in hand: still a road, seldom offered
        }
        pool.push_back(&a);
        metOf.push_back(shown);
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
        std::vector<int> met;
        std::vector<double> ow;
        std::vector<double> w = weights;
        for (int k = 0; k < 5; ++k) {
            int i = rngMinds_.weighted(w);
            if (i < 0) break;
            offer.push_back(pool[i]);
            met.push_back(metOf[i]);
            ow.push_back(weights[i]);
            w[i] = 0;
        }
        if (offer.empty()) return;
        std::vector<std::string> opts;
        for (size_t k = 0; k < offer.size(); ++k) {
            const AmbitionDef* a = offer[k];
            std::string o = a->name + "：" + ambitionText(head, a->desc, a->goal);
            std::string tail;
            if (!a->milestones.empty()) {
                std::string total = std::to_string(a->milestones.size() + 1);
                tail = total + " 步" + (met[k] > 0 ? "，已完成 " + std::to_string(met[k]) + "/" + total : "");
            }
            tail += (tail.empty() ? "" : "，") + std::string("限 ") + std::to_string(a->maxYears) + " 年";
            o += "（" + tail + "）";
            if (const Office* of = office(a->office)) o += "〔" + of->name + "的條件：" + gatesText(officeGates(head.id, a->office)) + "〕";
            if (a->id == regret) o += "【上一代沒做完的心願】";
            opts.push_back(o);
        }
        opts.push_back("先不立志，明年再說");
        std::string sit = fullName(head.id) + "（" + std::to_string(int(ageYears(head))) + " 歲）" +
                          (why.empty() ? "當了家" : why) + "，家族現在是" + standingName(standing(head.household)) + "。這一輩子，想做成哪件事？" +
                          (nearly.empty() ? "" : "（已經做到一半以上的不算志向，不列出來，要辦直接去辦：" + nearly + "。）");
        int r = askPlayer("立志", sit, opts);
        if (r == static_cast<int>(offer.size())) {
            log(1, "〔立志〕" + fullName(head.id) + " 想再看看，明年再說。");
            ambitionAskAfter_ = day_ + kDaysPerYear;
            return;
        }
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
        log(1, "【志向】" + fullName(head.id) + " 立志「" + pick->name + "」：" + ambitionDesc(head));
        std::string steps;
        for (size_t i = 0; i < pick->milestones.size(); ++i)
            steps += std::string(ev.eval(pick->milestones[i].goal, head, kNone) ? "✓" : "") + milestoneName(head, i) + " → ";
        log(1, "  路怎麼走：" + steps + ambitionFinal(head) + "。" + (pick->hint.empty() ? "" : "（" + pick->hint + "）") + "限到 " +
                   std::to_string(ambitionDeadline(head)) + " 年底；過了期限還沒做成，就成了留給下一代的心願。");
        if (const Office* of = office(pick->office)) log(1, "  " + of->name + "的條件：" + gatesText(officeGates(head.id, pick->office)));
        std::string rival = rivalLine(head);
        if (!rival.empty()) log(1, "  " + rival);
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
        head.ambitionDone |= 1u << i;
        ++head.ambitionStep;
        if (quiet) continue;
        head.reputation += 1;
        addStress(head.id, -5, "");
        if (player) {
            int nx = nextMilestone(head);
            std::string next = nx >= 0 ? "下一步：" + milestoneName(head, nx) + "。" : "只差最後一步：" + ambitionFinal(head) + "。" + (a->hint.empty() ? "" : "（" + a->hint + "）");
            log(1, "【里程碑】「" + a->name + "」：" + milestoneName(head, i) + "（" + std::to_string(head.ambitionStep) + "/" +
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

std::vector<int> World::milestoneState(Id headId) const {
    std::vector<int> out;
    if (headId == kNone) return out;
    const Person& head = people_[headId];
    const AmbitionDef* a = defs_.ambition(head.ambition);
    if (!a) return out;
    EventRunner ev(const_cast<World&>(*this));  // conditions only read the world
    for (size_t i = 0; i < a->milestones.size(); ++i) {
        bool now = ev.eval(a->milestones[i].goal, head, kNone);
        bool was = i < 32 && (head.ambitionDone & (1u << i));
        out.push_back(now ? 1 : was ? 2 : 0);
    }
    return out;
}

void World::yearlyAmbitions() {
    for (auto& h : households_) {
        if (h.extinct || h.head == kNone || h.members.empty() || inPlayerHouse(h.id)) continue;
        reviewAmbition(h, true);
    }
    // A loan to the manor held by a house that is no longer the family's (the line moved on): paid at term.
    if (manorBond_.house != kNone && !inPlayerHouse(manorBond_.house) && year() >= manorBond_.due) {
        Household& h = households_[manorBond_.house];
        if (!h.extinct) h.money += manorBond_.owed, book(h, "莊園還的債", manorBond_.owed);
        manorBond_ = ManorBond{};
    }
}

// The player's head is looked at every month: a goal reached counts that month, a road that has closed is said
// at once (with a new choice), and a head who put off choosing is asked again at the turn of the year.
void World::monthlyAmbition() {
    if (playerHousehold_ == kNone) return;
    Household& h = households_[playerHousehold_];
    if (h.extinct || h.head == kNone) return;
    Person& head = people_[h.head];
    if (!head.alive()) return;
    // The loan falls due whoever holds the house now, and whatever they are set on.
    if (month() == 9 && manorBond_.house == h.id && year() >= manorBond_.due) manorBondDue(head);
    if (ageYears(head) < 16) return;
    if (head.ambition.empty()) {
        if (day_ >= ambitionAskAfter_) chooseAmbition(head);
        return;
    }
    checkMilestones(head, false);
    reviewAmbition(h, month() == 0);
    if (h.head == head.id && !head.ambition.empty()) lateGameMonth(head);
}

void World::reviewAmbition(Household& h, bool yearTurn) {
    EventRunner ev(*this);
    {
        Person& head = people_[h.head];
        if (!head.alive() || ageYears(head) < 16) return;
        bool player = inPlayerHouse(h.id);
        if (head.ambition.empty()) {
            chooseAmbition(head);
            return;
        }
        const AmbitionDef* a = defs_.ambition(head.ambition);
        if (!a) return;
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
            chooseAmbition(head, "做成了一樁心願");
        } else if (!a->keep.isNull() && !ev.eval(a->keep, head, kNone)) {
            // The road has closed under them (the last son gone, the lord's fields let to another): no point waiting out the years.
            ++counters_[std::string(player ? "ambition_family:" : "ambition:") + a->id + ":thwarted"];
            if (player) log(1, "【志向受挫】「" + a->name + "」做不成了：" + ambitionFinal(head) + "這條路，眼下已經走不通。");
            head.ambition.clear();
            chooseAmbition(head, "的心願落了空");
        } else if (yearTurn && (day_ - head.ambitionSince) / kDaysPerYear >= a->maxYears) {
            ++counters_[std::string(player ? "ambition_family:" : "ambition:") + a->id + ":dropped"];
            if (player) log(1, fullName(h.head) + " 放下了「" + a->name + "」的志向：期限過了，還沒做成。這成了家裡一樁沒了的心願。");
            if (dyn) dyn->regret = a->id;
            head.ambition.clear();
            chooseAmbition(head, "放下了一樁做不成的心願");
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
        log(1, "【家族地位】" + surname + " 家成了" + standingName(now) + "（第 " + std::to_string(now + 1) + " 階，共 4 階）。" +
                   (now == 3 ? "梯子到頂了；往後的志向是跟村裡別的大戶、跟莊園去爭：第一大戶、和領主家結親、讓莊園欠你家的錢、一家把持村裡的差事。" : ""));
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

// ---- The late game ------------------------------------------------------------------------------
// Each of the last ambitions has someone on the other side: a house after the same land, a house after the same
// match, a house that will not have one family hold the village's posts, and the steward minding the manor's purse.

namespace {
std::string houseName(const std::vector<Household>& hs, const std::vector<Dynasty>& ds, Id h) {
    return hs[h].dynasty != kNone ? ds[hs[h].dynasty].surname + " 家" : "那一家";
}
}  // namespace

// Who stands in the way, said when the ambition is taken up.
std::string World::rivalLine(const Person& head) const {
    Id hid = head.household;
    if (hid == kNone) return "";
    if (head.ambition == "first_house") {
        Id o = biggestOther(hid);
        if (o == kNone) return "";
        return "對手：" + houseName(households_, dynasties_, o) + "有 " + num(households_[o].acres) + " 畝，是你家以外地最多的。想擴地的人家也會盯上你家的地。";
    }
    if (head.ambition == "village_offices") {
        Id best = kNone;
        for (const auto& p : people_)
            if (p.alive() && p.household != kNone && p.dynasty != head.dynasty && officeEligible(p.id, "reeve") &&
                (best == kNone || officeMerit(p, "reeve") > officeMerit(people_[best], "reeve")))
                best = p.id;
        return best == kNone ? "" : "對手：村裡最有望當莊頭的是 " + fullName(best) + "（" + houseName(households_, dynasties_, people_[best].household) +
                                        "）。連任到第三任，村裡人也會嫌同一家收租收太久。";
    }
    if (head.ambition == "manor_creditor")
        return "對手是莊園的管家：錢借出去容易，到期要莊園拿地抵債，他會在老爺耳邊替莊園說話。老爺對你家的好感越高，他越擋不住。";
    if (head.ambition == "gentry_match")
        return "德·阿什科姆家的婚事，村裡殷實的人家都想要：會有別家來搶。";
    return "";
}

void World::lateGameMonth(Person& head) {
    const std::string& a = head.ambition;
    if (a == "first_house" && month() == 2) rivalForLand(head);
    if (a == "gentry_match" && month() == 4) rivalForMatch(head);
    if (a == "village_offices" && (month() == 5 || month() == 8)) rivalForOffice(head);
    if (a == "manor_creditor" && month() == 4) stewardLoan(head);
}

// 「村裡第一大戶」: once a year a land-hungry house (or the biggest other) bids for one of the family's parcels.
// Refused, it goes after that parcel openly (村裡在爭什麼) and asks for it in return for what the family asks of it.
void World::rivalForLand(Person& head) {
    auto asked = lateAsked_.find("land");
    if (asked != lateAsked_.end() && asked->second > day_) return;
    Household& h = households_[head.household];
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id top = biggestOther(h.id), rivalHouse = kNone;
    double best = -1e9;
    for (const auto& o : households_) {
        if (o.extinct || o.members.empty() || o.head == kNone || inPlayerHouse(o.id) || o.id == lordHouse) continue;
        const Person& op = people_[o.head];
        bool hungry = landHungry(op.ambition);
        if (op.occupation == "priest" || (!hungry && o.id != top)) continue;
        double s = o.acres + o.money / 30 + (hungry ? 20 : 0);
        if (s > best) best = s, rivalHouse = o.id;
    }
    if (rivalHouse == kNone) return;
    Household& rh = households_[rivalHouse];
    Id want = kNone;  // ours they would most like: a small parcel, customary first
    for (const auto& pc : parcels_)
        if (!pc.gone && pc.holder == h.id && pc.acres >= 2 && pc.acres <= 8 &&
            (want == kNone || pc.free < parcels_[want].free || (pc.free == parcels_[want].free && pc.acres < parcels_[want].acres)))
            want = pc.id;
    if (want == kNone) return;
    const double acrePrice = 64;
    double price = std::round(parcels_[want].acres * acrePrice * 1.25);
    if (rh.money < price) return;
    lateAsked_["land"] = day_ + kDaysPerYear;
    // And one of theirs, should the family bid back.
    Id theirs = kNone;
    for (const auto& pc : parcels_)
        if (!pc.gone && pc.holder == rivalHouse && pc.acres >= 2 && pc.acres <= 8 && (theirs == kNone || pc.acres < parcels_[theirs].acres)) theirs = pc.id;
    double back = theirs != kNone ? std::round(parcels_[theirs].acres * acrePrice * 1.3) : 0;
    std::string rn = houseName(households_, dynasties_, rivalHouse);
    double lead = acresLead(h.id);
    std::string sit = rn + "（" + num(rh.acres) + " 畝，" + fullName(rh.head) + "當家）託人來說，想出 " + pence(price) + "買你家的" +
                      parcelName(parcels_[want]) + "。他家也想做村裡數一數二的人家。你家現在 " + num(h.acres) + " 畝，" +
                      (lead >= 0 ? "比地第二多的人家多 " + num(lead) + " 畝。" : "比地最多的" + ambitionText(head, "{top}", Json()) + "少 " + num(-lead) + " 畝。");
    std::vector<std::string> opts{"不賣：地是要傳給子孫的（" + rn + "會記著，往後還會來爭這塊地）", "賣給他（" + pence(price) + "）"};
    bool canBack = theirs != kNone && h.money >= back + 20;
    if (canBack) opts.push_back("反過來出 " + pence(back) + "買他家的" + parcelName(parcels_[theirs]) + "（看他肯不肯）");
    int r = askPlayer("搶地", sit, opts);
    if (r < 0) r = h.money < 40 ? 1 : 0;  // the rules: keep the land unless the house is short of bread money
    if (r == 1) {
        moveParcel(want, rivalHouse, "買來");
        rh.money -= price;
        h.money += price;
        book(h, "賣地", price);
        log(1, "〔搶地〕" + head.firstName + " 把地賣給了" + rn + "，得了 " + pence(price) + "。");
    } else if (r == 2 && canBack) {
        bool yes = rngMinds_.chance(std::clamp((sellReason(rh).empty() ? 0.25 : 0.7) + opinion(rh.head, head.id) / 200, 0.05, 0.85));
        if (yes) {
            std::string nm = parcelName(parcels_[theirs]);
            moveParcel(theirs, h.id, "買來");
            h.money -= back;
            book(h, "買地", -back);
            rh.money += back;
            log(1, "〔搶地〕" + fullName(rh.head) + " 想了幾天，到底把" + nm + "賣給了你家（" + pence(back) + "）。");
        } else {
            addOpinion(rh.head, head.id, "land_rival", -4, 6);
            log(1, "〔搶地〕" + fullName(rh.head) + " 冷笑一聲：「我家的地不賣，倒是你家那塊，遲早是我家的。」");
            addWant(rivalHouse, "parcel", want, kNone, "", "想跟你家爭村裡第一大戶");
        }
    } else {
        addOpinion(rh.head, head.id, "land_rival", -6, 6);
        addWant(rivalHouse, "parcel", want, kNone, "", "想跟你家爭村裡第一大戶");
        log(1, "〔搶地〕" + head.firstName + " 不肯賣。" + rn + "放話說，那塊地遲早是他家的（好感 -6）。");
    }
}

// 「和德·阿什科姆家結親」: a well-off house sends to the manor too; each year the lord may settle it with them, the
// likelier the more he favours them over the family.
void World::rivalForMatch(Person& head) {
    if (lord_ == kNone || !people_[lord_].alive()) return;
    Id g = kNone;
    for (const auto& p : people_)
        if (p.alive() && p.household != kNone && gentry(p.id) && p.occupation != "lord" && p.occupation != "lady" && p.occupation != "priest" &&
            (p.spouse == kNone || !people_[p.spouse].alive()) && ageYears(p) >= 16 && ageYears(p) <= 40) {
            g = p.id;
            break;
        }
    if (g == kNone) return;
    const Person& gp = people_[g];
    Want* rw = nullptr;
    for (auto& wt : wants_)
        if (wt.open && wt.kind == "match" && wt.object == g && !inPlayerHouse(wt.house) && wt.subject != kNone && people_[wt.subject].alive() &&
            (people_[wt.subject].spouse == kNone || !people_[people_[wt.subject].spouse].alive()))
            rw = &wt;
    if (!rw) {
        Id lordHouse = people_[lord_].household;
        Id bestHouse = kNone, bestChild = kNone;
        double best = -1e9;
        for (const auto& o : households_) {
            if (o.extinct || o.members.empty() || o.head == kNone || inPlayerHouse(o.id) || o.id == lordHouse || standing(o.id) < 2) continue;
            for (Id m : o.members) {
                const Person& c = people_[m];
                double a = ageYears(c);
                if (m == o.head || c.sex == gp.sex || (c.spouse != kNone && people_[c.spouse].alive()) || a < 16 || a > 32 || c.home != kNone ||
                    closeKin(m, g) || c.occupation == "priest")
                    continue;
                double s = o.acres + o.money / 30 + opinion(lord_, o.head) / 2;
                if (s > best) best = s, bestHouse = o.id, bestChild = m;
            }
        }
        if (bestHouse == kNone) return;
        addWant(bestHouse, "match", g, bestChild, "", "想跟領主家結親");
        log(1, "〔搶親〕" + houseName(households_, dynasties_, bestHouse) + "也託人去莊園說親，想讓 " + fullName(bestChild) + " 跟 " + fullName(g) +
                   " 成親。誰家先說成，就看老爺偏向哪一家（老爺對你 " + num(opinion(lord_, head.id)) + "，對他家 " +
                   num(opinion(lord_, households_[bestHouse].head)) + "）。");
        return;
    }
    Id rh = households_[rw->house].head;
    if (rh == kNone) return;
    double edge = opinion(lord_, rh) - opinion(lord_, head.id);
    if (!rngMinds_.chance(std::clamp(0.15 + edge / 200, 0.05, 0.4))) return;
    Id c = rw->subject, rival = rw->house;
    Id man = people_[c].sex == Sex::Male ? c : g, woman = man == c ? g : c;
    marry(man, woman);
    settleWants("match", g, "", rival, "說成了");
    if (head.dynasty != kNone) dynasties_[head.dynasty].prestige -= 3;
    log(1, "【搶親】老爺把 " + fullName(g) + " 許給了" + houseName(households_, dynasties_, rival) + "的 " + fullName(c) +
               "。你家想說的這門親，讓人家搶了先（家族聲望 -3）。");
}

// 「十年莊頭」: before the reeve's election (June) and the Michaelmas posts (September), the likeliest man of
// another house lets it be known he will stand against the family's.
void World::rivalForOffice(Person& head) {
    std::string id = month() == 5 ? "reeve" : "aletaster";
    if (id == "reeve" && !reeveDue()) return;
    const Office* of = office(id);
    if (!of) return;
    bool ours = of->holder != kNone && people_[of->holder].dynasty == head.dynasty;
    bool wanted = false;
    for (const auto& wt : wants_) wanted = wanted || (wt.open && wt.kind == "office" && wt.office == id && inPlayerHouse(wt.house));
    if (!ours && !wanted) return;
    Id best = kNone;
    for (const auto& p : people_)
        if (p.alive() && p.household != kNone && p.dynasty != head.dynasty && !inPlayerHouse(p.household) && officeEligible(p.id, id) &&
            (best == kNone || officeMerit(p, id) > officeMerit(people_[best], id)))
            best = p.id;
    if (best == kNone) return;
    for (const auto& wt : wants_)
        if (wt.open && wt.kind == "office" && wt.office == id && wt.house == people_[best].household) return;  // said so already
    std::string us = head.dynasty != kNone ? dynasties_[head.dynasty].surname : head.firstName;
    addWant(people_[best].household, "office", kNone, best, id, "不能讓" + us + "家把村裡的差事包了");
    log(1, "〔對手〕" + fullName(best) + " 放話要出來選" + of->name + "：「不能讓" + us + " 家把村裡的差事包了。」去各家「開口」拉票吧。");
}

// 「莊園欠我家的錢」: each May the steward comes to borrow, the family says how much. Three years on, at Michaelmas,
// the manor pays it back with a quarter more, or pledges what it has.
void World::stewardLoan(Person& head) {
    if (manorBond_.house != kNone || lord_ == kNone || !people_[lord_].alive()) return;
    auto asked = lateAsked_.find("loan");
    if (asked != lateAsked_.end() && asked->second > day_) return;
    Household& h = households_[head.household];
    double big = std::max(400.0, std::floor(h.money * 0.6 / 50) * 50), small = std::max(200.0, std::floor(h.money * 0.3 / 50) * 50);
    if (h.money < small + 40) return;
    lateAsked_["loan"] = day_ + kDaysPerYear;
    std::string sit = "管家上門來，說老爺要替國王出人出錢，莊園的現錢一時周轉不過來，想向你家借一筆，" + std::to_string(year() + 3) +
                      " 年米迦勒節連本帶利多還四分之一。到時候莊園要是拿不出錢，就拿自營地或磨坊來抵。你家現在有 " + pence(h.money) + "。";
    std::vector<std::string> opts;
    std::vector<double> amounts;
    if (h.money >= big + 40) opts.push_back("借 " + pence(big) + "（到期還 " + pence(std::round(big * 1.25)) + "）"), amounts.push_back(big);
    if (small < big || amounts.empty()) opts.push_back("借 " + pence(small) + "（到期還 " + pence(std::round(small * 1.25)) + "）"), amounts.push_back(small);
    opts.push_back("這回不借");
    int r = askPlayer("莊園要借錢", sit, opts);
    if (r < 0) r = static_cast<int>(amounts.size()) - 1;  // the rules: the smaller sum
    if (r >= static_cast<int>(amounts.size())) {
        log(1, "〔莊園要借錢〕" + head.firstName + " 推說手頭緊。管家點點頭走了，說明年再來。");
        return;
    }
    double amt = amounts[r];
    Household& manor = households_[people_[lord_].household];
    h.money -= amt;
    book(h, "借給莊園", -amt);
    manor.money += amt;
    manorBond_ = ManorBond{h.id, amt, std::round(amt * 1.25), year() + 3};
    addOpinion(lord_, head.id, "lent_to_manor", 10, 10);
    ++counters_["manor_loan:lent"];
    log(1, "〔莊園要借錢〕" + head.firstName + " 借給莊園 " + pence(amt) + "。管家在帳上寫明：" + std::to_string(manorBond_.due) + " 年米迦勒節還 " +
               pence(manorBond_.owed) + "，還不出就拿地或磨坊抵（老爺好感 +10）。");
}

void World::manorBondDue(Person& head) {
    ManorBond& b = manorBond_;
    Household& h = households_[b.house];
    if (lord_ == kNone || people_[lord_].household == kNone) {
        manorBond_ = ManorBond{};
        return;
    }
    Id lordHouse = people_[lord_].household;
    Household& manor = households_[lordHouse];
    // The manor's books run to thousands, but its ready coin goes to the king, the lawyers and the lord's table: at
    // term it has the money about half the time, in a year of war, dearth or plague much less often.
    bool hard = param("war_levy", 0) > 0 || param("harvest_mult", 1) < 0.8 || param("plague_monthly", 0) > 0;
    if (rngMinds_.chance(hard ? 0.25 : 0.5)) {
        manor.money = std::max(0.0, manor.money - b.owed);
        h.money += b.owed;
        book(h, "莊園還的債", b.owed);
        addOpinion(lord_, head.id, "trusty_lender", 5, 8);
        ++counters_["manor_loan:repaid"];
        log(1, "【莊園還債】管家按約把 " + pence(b.owed) + "送來了，連本帶利，賺了 " + pence(b.owed - b.lent) + "。老爺說你家是靠得住的人家（好感 +5）。");
        manorBond_ = ManorBond{};
        return;
    }
    double acres = std::clamp(std::round(b.owed / 50), 4.0, 20.0);
    bool landOk = manor.acres >= acres + 40;
    bool millOk = std::none_of(households_.begin(), households_.end(), [&](const Household& o) { return !o.extinct && o.millUntil > day_; });
    int millYears = static_cast<int>(std::clamp(std::round(b.owed / 60), 3.0, 10.0));
    double lordOp = opinion(lord_, head.id);
    std::string why = param("war_levy", 0) > 0          ? "今年的現錢都填了國王打仗的軍餉"
                      : param("plague_monthly", 0) > 0  ? "瘟疫以後租子收不齊"
                      : param("harvest_mult", 1) < 0.8  ? "荒年租子收不上來"
                                                        : "老爺在倫敦的官司和排場把現錢花光了";
    std::string sit = "莊園該還你家的 " + pence(b.owed) + "到期了。管家攤開帳本：" + why + "，老爺的意思是拿東西抵。管家自己是不想讓莊園的地落到佃戶手裡的（老爺對你 " +
                      num(lordOp) + "）。";
    std::vector<std::string> opts;
    std::vector<int> kind;  // 0 land, 1 mill, 2 wait, 3 forgive
    if (landOk) opts.push_back("要自營地抵債：" + num(acres) + " 畝，立契成為你家的自由地（老爺好感越高，管家越擋不住）"), kind.push_back(0);
    if (millOk) opts.push_back("要磨坊抵債：白管磨坊 " + std::to_string(millYears) + " 年，不收押金"), kind.push_back(1);
    opts.push_back("再寬限兩年，利錢照算（到時還 " + pence(std::round(b.owed * 1.1)) + "）"), kind.push_back(2);
    opts.push_back("免了這筆債，換老爺一份大人情"), kind.push_back(3);
    int r = askPlayer("莊園還不出錢", sit, opts);
    int k = r >= 0 && r < static_cast<int>(kind.size()) ? kind[r] : kind[0];
    if (k == 0 && !rngMinds_.chance(std::clamp(0.4 + lordOp / 100, 0.2, 0.9))) {
        // The steward has the lord's ear: no land to a tenant. The mill instead, or more time.
        log(1, "【莊園抵債】管家在老爺耳邊說了幾句：「祖上的地，哪能給佃戶。」老爺改了口。");
        k = millOk ? 1 : 2;
    }
    if (k == 0) {
        moveLand(lordHouse, h.id, acres, LandPick::CustomaryFirst, "莊園抵債");
        std::string names = movedText();
        for (Id id : lastMoved_) parcels_[id].free = true;
        syncLand(h.id);
        head.flags.insert("manor_pledge");
        if (head.dynasty != kNone) dynasties_[head.dynasty].prestige += 5;
        addOpinion(lord_, head.id, "took_demesne", -5, 10);
        ++counters_["manor_loan:land"];
        log(1, "【莊園抵債】莊園還不出錢，老爺立契把" + names + "抵給了你家，從此是你家的自由地。村裡都在說，莊園欠了" +
                   houseName(households_, dynasties_, h.id) + "的錢。");
    } else if (k == 1) {
        h.millUntil = day_ + millYears * kDaysPerYear;
        head.flags.insert("mill_farmer");
        head.flags.insert("manor_pledge");
        ++counters_["manor_loan:mill"];
        log(1, "【莊園抵債】莊園還不出錢，老爺把磨坊交給你家白管 " + std::to_string(millYears) + " 年抵債。往後全村的麥都要到你家管的磨坊磨。");
    } else if (k == 2) {
        b.owed = std::round(b.owed * 1.1);
        b.due = year() + 2;
        addOpinion(lord_, head.id, "patient_lender", 8, 8);
        log(1, "【莊園還債】你家答應再寬限兩年：" + std::to_string(b.due) + " 年米迦勒節還 " + pence(b.owed) + "（老爺好感 +8）。");
        return;
    } else {
        addOpinion(lord_, head.id, "forgave_manor", 30, 20);
        head.flags.insert("lord_favour");
        if (head.dynasty != kNone) dynasties_[head.dynasty].prestige += 5;
        head.reputation += 3;
        ++counters_["manor_loan:forgiven"];
        log(1, "【莊園還債】" + head.firstName + " 把借據還給管家，說這筆錢不用還了。老爺親自來道謝（好感 +30），從此把你家當自己人。");
    }
    manorBond_ = ManorBond{};
}

}  // namespace sim
