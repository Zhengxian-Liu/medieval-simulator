// The drive inside the family (design/stages-and-drive.md 三、四、六): how a holding passes and who is left aggrieved
// by it, the widow's third, the old parent's keep, and the ladder a grievance climbs (a complaint, the manor court,
// leaving home, theft, the rival, poison or the knife), with what a killing sets off after it. Nothing is dealt out:
// every rung is someone's choice (the rules list the rungs open to them, the decider picks, the rules weigh in when
// there is no model), from a cause worked out of the state, and every rung past a complaint is seen coming.
#include <algorithm>
#include <cmath>
#include <set>

#include "simcore/events.h"

namespace sim {

namespace {

std::string n0(double v) { return std::to_string(static_cast<long>(std::lround(v))); }
std::string sgn(double v) {
    long r = std::lround(v);
    return (r > 0 ? "+" : r < 0 ? "−" : "") + std::to_string(std::labs(r));
}
const char* he(const Person& p) { return p.sex == Sex::Male ? "他" : "她"; }

}  // namespace

double World::payLord(Household& h, double amount, const std::string& why) {
    double paid = std::floor(std::min(amount, std::max(0.0, h.money)));
    if (paid <= 0) return 0;
    h.money -= paid;
    book(h, why, -paid);
    Id lh = lord_ != kNone ? people_[lord_].household : kNone;
    if (lh != kNone && lh != h.id) {
        households_[lh].money += paid;
        book(households_[lh], "收" + why, paid);
    }
    return paid;
}

// ---- How the holding passes --------------------------------------------------------------------------------------

std::vector<Id> World::sonsOf(const Household& h, Id head) const {
    std::vector<Id> out;
    if (head == kNone) return out;
    const Person& hp = people_[head];
    for (Id c : hp.children) {
        const Person& s = people_[c];
        if (!s.alive() || s.sex != Sex::Male || s.flags.count("felon") || s.flags.count("disowned")) continue;
        if (hp.dynasty != kNone && s.dynasty != hp.dynasty) continue;
        const StationDef* sd = defs_.station(s.station);
        if (s.occupation == "priest" || (sd && sd->lasting)) continue;
        bool here = s.household == h.id || s.home == h.id || (isAway(s) && s.awayFrom == h.id);
        bool own = s.household != kNone && s.household != h.id && households_[s.household].head == c;
        if (here || own) out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [&](Id a, Id b) { return people_[a].birthDay < people_[b].birthDay; });
    return out;
}

double World::namingFine(const Household& h) const {
    double factor = lord_ != kNone && h.head != kNone ? std::clamp(1 - opinion(lord_, h.head) / 100, 0.6, 1.4) : 1;
    return std::round((param("naming_fine", 20) + h.acres * param("naming_fine_acre", 1.5)) * factor);
}

double World::dowerOf(const Household& h, Id head, Id heir) const {
    if (head == kNone || h.acres < 3) return 0;
    Id w = people_[head].spouse;
    if (w == kNone || !people_[w].alive() || people_[w].household != h.id || people_[w].sex != Sex::Female) return 0;  // a widow's third: no widower's
    // Her own son keeps the house: she lives on with him and the third is hers only on paper (until she marries again).
    if (heir != kNone && (people_[heir].mother == w || heir == w)) return 0;
    return std::round(h.acres / 3);
}

bool World::namedHeirStands(const Household& h, Id x) const {
    if (x == kNone || !people_[x].alive() || people_[x].flags.count("felon")) return false;
    const Person& q = people_[x];
    const StationDef* sd = defs_.station(q.station);
    if (q.occupation == "priest" || (sd && sd->lasting)) return false;
    if (q.household == h.id || q.home == h.id || (isAway(q) && q.awayFrom == h.id)) return true;
    // Set up in a house of his own in the village: he comes back to the holding with his household (onHeadDied).
    return q.household != kNone && !households_[q.household].extinct && households_[q.household].head == x && households_[q.household].house != "manor";
}

Id World::planHeir(const Household& h, Id head, const std::string& by, Id named) const {
    if (by == "named" && namedHeirStands(h, named)) return named;
    Household custom = h;  // heirOf by custom alone, without the court's naming
    custom.passBy.clear(), custom.passHeir = kNone;
    return heirOf(custom, head);
}

World::SettleTerms World::settleTerms(Id hid, double want, double money) const {
    SettleTerms t;
    t.vac = std::min(vacantAcres(), want);
    t.own = std::clamp(want - t.vac, 0.0, std::max(0.0, households_[hid].acres - 8));
    if (t.vac + t.own < 2) t.vac = t.own = 0;  // no land to rent or spare: a cottage and its garden, and day labour
    t.gift = std::min(money, std::max(0.0, households_[hid].money));
    return t;
}

bool World::askSettleSon(Id hid, Id son, double want, double money) {
    if (!inPlayerHouse(hid) || son == kNone || people_[son].household != hid || households_[hid].head == son) return true;
    const Household& ph = households_[hid];
    const SettleTerms t = settleTerms(hid, want, money);
    const Person& sub = people_[son];
    // fun-r9 (seed 118): the price is said before the son goes, not found in a 「·」 line after (8 acres).
    std::string sit = "給 " + fullName(son) + " 分家：" +
                      (t.vac + t.own < 2 ? std::string("村裡沒有空地，自家的地也分不出來，只能給他一間小屋和屋後的菜園，靠做工過活；")
                                         : (t.vac >= 1 ? "向領主租村裡的空地 " + n0(t.vac) + " 畝" : std::string()) + (t.vac >= 1 && t.own >= 1 ? "、" : "") +
                                               (t.own >= 1 ? "從自家的地分出 " + n0(t.own) + " 畝" : std::string()) + "；") +
                      (t.gift >= 1 ? "再給 " + n0(t.gift) + " 便士起家。" : "家裡拿不出錢給他起家。") + "你家的地從 " + n0(ph.acres) + " 畝剩 " + n0(ph.acres - t.own) +
                      " 畝（平常年景收的糧夠家裡吃 " + n0(keepsHouse(ph.acres - t.own) / std::max(1.0, consumption(ph) * kDaysPerMonth)) + " 個月，" + sub.firstName +
                      "一家搬走後少幾張嘴）" + (t.gift >= 1 ? "，錢從 " + n0(ph.money) + " 剩 " + n0(ph.money - t.gift) + " 便士" : "") + "。";
    if (son == planHeir(ph, ph.head, ph.passBy, ph.passHeir)) sit += sub.firstName + "是繼承人：分出去自立門戶，就不再接這個家的家業。";
    std::string cost;
    if (t.own >= 1) cost += "地 −" + n0(t.own) + " 畝";
    if (t.gift >= 1) cost += (cost.empty() ? "" : "，") + std::string("錢 −") + n0(t.gift);
    std::vector<std::string> opts = {"照這樣分" + (cost.empty() ? std::string() : "（" + cost + "）")};
    if (t.own >= 1)
        opts.push_back(t.vac >= 1 ? "只給錢和村裡的空地，自家的地不動（" + n0(t.vac) + " 畝空地" + (t.gift >= 1 ? "、" + n0(t.gift) + " 便士" : "") + "）"
                                  : "自家的地不動，" + std::string(t.gift >= 1 ? "只給 " + n0(t.gift) + " 便士，" : "") + "讓他靠做工過活");
    opts.push_back("先不分了");
    int r = askPlayer("分家", sit, opts);
    if (r == static_cast<int>(opts.size()) - 1) {
        log(1, "〔分家〕" + sub.firstName + "的分家先擱下了。（選擇：先不分了）");
        return false;
    }
    if (r == 1 && t.own >= 1) settleNoOwn_.insert(son);
    return true;
}

std::vector<World::Share> World::passPlan(const Household& h, Id head, const std::string& by, const std::string& younger, Id named) const {
    std::vector<Share> out;
    std::vector<Id> sons = sonsOf(h, head);
    if (sons.empty()) return out;
    // The same heir the succession will find (round 10 H1): a son already set up in a house of his own has had his
    // share and is not the heir by custom; a chosen heir counts while that choice stands.
    const Id heir = planHeir(h, head, by, named);
    if (heir == kNone) return out;
    double land = h.acres - dowerOf(h, head, heir), money = std::max(0.0, h.money);
    std::vector<Id> ys;  // the grown younger sons, in line for a provision
    for (Id s : sons)
        if (s != heir && ageYears(people_[s]) >= 16) ys.push_back(s);
    Share hs{heir, land, money, "heir"};
    if (by == "split" && sons.size() >= 2) {
        double each = std::floor(land / sons.size());
        for (Id s : ys) out.push_back({s, each, 0, "split"}), hs.acres -= each;
    } else if (by != "named") {
        const std::string how = younger.empty() ? "cash" : younger;
        const double k = static_cast<double>(std::max<size_t>(1, ys.size()));
        double portion = std::min(3.0, std::floor(land * 0.12));
        if (land - portion * k < 8) portion = std::max(0.0, std::floor((land - 8) / k));
        const double cash = std::floor(std::min(param("pass_cash", 30), money * 0.4 / k));
        for (Id s : ys) {
            const Person& q = people_[s];
            if (how == "portion" && portion >= 1) {
                out.push_back({s, portion, 0, "portion"});
                hs.acres -= portion;
            } else if (how == "trade" && ageYears(q) <= 25 && q.spouse == kNone && std::min(20.0, hs.money) >= 1) {
                out.push_back({s, 0, std::min(20.0, hs.money), "trade"});
                hs.money -= out.back().money;
            } else {
                out.push_back({s, 0, std::min(cash, hs.money), "cash"});
                hs.money -= out.back().money;
            }
        }
    } else {
        // A chosen heir takes it all; the other grown sons (the eldest has his own grievance) get nothing.
        for (Id s : ys)
            if (s != sons[0] || heir == sons[0]) out.push_back({s, 0, 0, "none"});
    }
    out.insert(out.begin(), hs);
    return out;
}

std::string World::passWords(const Household& h, Id head, const std::string& by, const std::string& younger, Id named) const {
    std::vector<Share> plan = passPlan(h, head, by, younger, named);
    if (plan.empty()) return "";
    std::vector<Id> sons = sonsOf(h, head);
    const Id heir = plan[0].who;
    std::string out = shortName(heir) + (heir == sons[0] ? "（長子）" : "") + "繼承 " + n0(plan[0].acres) + " 畝和房子";
    std::vector<std::string> sore, onlyCash;
    double givenAll = 0;
    for (Id s : sons) givenAll += givenWorth(s);
    const double fair = (h.acres + std::max(0.0, h.money) / 60 + givenAll) / std::max<size_t>(1, sons.size());
    auto ownHouse = [&](Id x) {
        const Id sh = people_[x].household;
        return sh != kNone && sh != h.id && households_[sh].head == x;
    };
    for (size_t i = 1; i < plan.size(); ++i) {
        const Share& s = plan[i];
        std::string nm = shortName(s.who);
        const bool own = ownHouse(s.who);
        if (s.what == "cash") {
            out += "；" + nm + (s.money >= 1 ? "拿 " + n0(s.money) + " 便士" : "什麼也分不到（家裡沒錢）") +
                   (own ? "（他已經自立門戶）" : people_[s.who].spouse != kNone || ageYears(people_[s.who]) >= 21 ? "，搬去村邊小屋做工過活" : "，出去謀生");
            if (younger == "trade") onlyCash.push_back(nm);
        } else if (s.what == "none") out += "；" + nm + "什麼也分不到";
        else if (s.what == "trade") out += "；" + nm + "帶 " + n0(s.money) + " 便士拜師錢去鎮上學手藝";
        else out += "；" + nm + "分 " + n0(s.acres) + (own ? " 畝，併進他自己家" : " 畝自立門戶");
        if (s.acres + s.money / 60 + givenWorth(s.who) < fair * 0.6 && !(s.who == sons[0] && by == "named")) sore.push_back(nm);
    }
    if (!onlyCash.empty()) {
        std::string who;
        for (const auto& x : onlyCash) who += (who.empty() ? "" : "、") + x;
        out += "（" + who + "成了家或過了學藝的年紀，只能給錢）";
    }
    // The eldest passed over, or left with a share: he is sore too. One already set up on his own is not the heir.
    if (heir != sons[0]) {
        if (by == "named") sore.insert(sore.begin(), shortName(sons[0]) + "（長子，什麼也分不到）");
        else if (ownHouse(sons[0])) out += "；長子" + shortName(sons[0]) + "已經分出去自立門戶，不接這份家業";
    } else if (by == "split" && sons.size() >= 2) {
        sore.insert(sore.begin(), shortName(sons[0]) + "（長子，照習俗該全拿，平分只得 1/" + std::to_string(sons.size()) + "）");
    }
    if (by == "named" && named != kNone && heir != named) out += "；立的繼承人" + shortName(named) + "已經不在，照習俗改由" + shortName(heir) + "接";
    // A son still a child gets nothing at the passing, whichever way: what each way means for him once he is grown
    // (fun-r12 A7: the three ways read the same while the boys were small).
    std::vector<std::string> small;
    for (Id s : sons)
        if (ageYears(people_[s]) < 16 && s != heir) small.push_back(shortName(s));
    if (!small.empty()) {
        std::string who;
        for (const auto& x : small) who += (who.empty() ? "" : "、") + x;
        const double land = h.acres - dowerOf(h, head, heir);
        const double k = static_cast<double>(std::max<size_t>(1, sons.size() - 1));
        double portion = std::min(3.0, std::floor(land * 0.12));
        if (land - portion * k < 8) portion = std::max(0.0, std::floor((land - 8) / k));
        const double cash = std::floor(std::min(param("pass_cash", 30), std::max(0.0, h.money) * 0.4 / k));
        const std::string yg = younger.empty() ? "cash" : younger;
        if (by == "split" && sons.size() >= 2)
            out += "；" + who + "還小，照平分本該有約 " + n0(std::floor(land / sons.size())) + " 畝，可交班時沒成年，地先留在哥哥手裡";
        else if (by == "named")
            out += "；" + who + "還小，照這個分法往後也什麼都分不到";
        else if (yg == "portion" && portion >= 1)
            out += "；" + who + "還小：交班時沒成年就什麼也分不到，成年了才照這個分法各分約 " + n0(portion) + " 畝自立門戶";
        else if (yg == "trade")
            out += "；" + who + "還小：交班時沒成年就什麼也分不到，成年了才照這個分法送去鎮上學手藝（拜師錢約 20 便士）";
        else
            out += "；" + who + "還小：交班時沒成年就什麼也分不到，成年了才照這個分法各拿約 " + n0(cash) + " 便士";
    }
    if (double d = dowerOf(h, head, heir); d > 0)
        out += "；" + shortName(people_[head].spouse) + "不是 " + shortName(heir) + " 的親娘，守寡時帶走三分之一（" + n0(d) + " 畝），她死了才回來";
    return out + (sore.empty() ? "。" : "。會不滿的：" + [&] { std::string x; for (const auto& y : sore) x += (x.empty() ? "" : "、") + y; return x; }() + "。");
}

std::string World::inheritText() const {
    if (playerHousehold_ == kNone || playerHead() == kNone) return "";
    const Household& h = households_[playerHousehold_];
    Id head = playerHead();
    std::string t;
    std::vector<Id> sons = sonsOf(h, head);
    const Id heir = planHeir(h, head, h.passBy, h.passHeir);
    const char* how = h.passBy == "split" ? "兒子們平分" : h.passBy == "named" ? "立了繼承人" : h.passBy == "eldest" ? "長子繼承" : "照習俗（長子繼承，弟弟們給幾個錢）";
    if (sons.size() >= 2) t = std::string(how) + "：" + passWords(h, head, h.passBy, h.passYounger, h.passHeir);
    else if (heir != kNone) t = "家業歸 " + shortName(heir) + "。";
    Id w = people_[head].spouse;
    // The widow's third, said once: passWords says it when it leaves the house; here only when it stays on paper.
    if (w != kNone && people_[w].alive() && people_[w].household == h.id && h.acres >= 3 && dowerOf(h, head, heir) <= 0)
        t += "寡婦產：你死後，" + shortName(w) + "終身有三分之一的地（約 " + n0(std::round(h.acres / 3)) + " 畝），跟著兒子住就照舊一起種，改嫁就帶走。";
    for (const auto& d : dowers_)
        if (d.open && d.in == h.id && people_[d.widow].alive())
            t += "寡婦產：" + shortName(d.widow) + "守著 " + n0(d.acres) + " 畝，改嫁就帶走，她死了才" + (d.from == h.id ? "算回家裡的" : "回到前夫家") + "。";
    const double rep = people_[head].reputation;
    t += "名聲：交班時家名傳一半（你現在 " + sgn(rep) + "，繼承人接手時加上 " + sgn(rep * param("rep_carry", 0.5)) + "）。";
    return t;
}


// ---- The family's own succession, always in view (fun-r11 4) ---------------------------------------------------

// One person as the succession line names them: 「馬修（兒子，11 歲，在文法學校讀書）」.
std::string World::heirTag(Id x) const {
    const Person& q = people_[x];
    Id head = playerHead();
    std::string kin = head != kNone ? kinWord(head, x) : "";
    std::string s = shortName(x) + "（" + (kin.empty() ? std::string() : kin + "，") + std::to_string(static_cast<int>(ageYears(q))) + " 歲";
    if (q.station != "home")
        if (const StationDef* sd = defs_.station(q.station)) s += "，" + sd->name;
    return s + "）";
}

// Who takes the house under the arrangement as it stands, what each son gets, and who would be sore.
std::string World::successionLine() const {
    if (playerHousehold_ == kNone || playerHead() == kNone) return "";
    const Household& h = households_[playerHousehold_];
    const Id head = playerHead();
    const std::vector<Id> sons = sonsOf(h, head);
    const Id heir = planHeir(h, head, h.passBy, h.passHeir);
    if (heir == kNone) return "照現在的情形：家裡沒有能接家業的人。你死後，這份佃地要回到領主手上另找人種。";
    if (sons.size() >= 2) {
        const char* how = h.passBy == "split" ? "兒子們平分" : h.passBy == "named" ? "立了繼承人" : h.passBy == "eldest" ? "長子繼承" : "照習俗";
        // (passWords names the heir and his share itself: not said twice, review L2.)
        return std::string("照現在的分法（") + how + "）：" + passWords(h, head, h.passBy, h.passYounger, h.passHeir);
    }
    std::string t = "照現在的分法：";
    if (!sons.empty() && heir == sons[0]) t += heirTag(heir) + "是獨子，房子和 " + n0(h.acres - dowerOf(h, head, heir)) + " 畝地全歸他，沒有人分。";
    else t += "沒有兒子，家業照習俗歸 " + heirTag(heir) + "。";
    if (double d = dowerOf(h, head, heir); d > 0)
        t += shortName(people_[head].spouse) + "不是" + shortName(heir) + "的親娘，守寡時帶走三分之一（" + n0(d) + " 畝）。";
    return t;
}

// The sons and brothers of the head living at home (or away from it for a while): who would have a share.
std::set<Id> World::homeKin() const {
    std::set<Id> out;
    if (playerHousehold_ == kNone || playerHead() == kNone) return out;
    const Id head = playerHead();
    const Person& hp = people_[head];
    auto mine = [&](Id x) {
        if (x == kNone || x == head) return false;
        const Person& q = people_[x];
        if (!q.alive() || q.sex != Sex::Male || q.flags.count("felon") || ageYears(q) < 12) return false;
        if (hp.dynasty != kNone && q.dynasty != hp.dynasty) return false;
        const StationDef* sd = defs_.station(q.station);
        if (sd && sd->lasting) return false;
        // At home, away for a while, or in service / hired out / apprenticed in another house (home stays this one).
        return q.household == playerHousehold_ || q.home == playerHousehold_ || (isAway(q) && q.awayFrom == playerHousehold_);
    };
    for (Id c : hp.children)
        if (mine(c)) out.insert(c);
    for (Id par : {hp.father, hp.mother})
        if (par != kNone)
            for (Id b : people_[par].children)
                if (mine(b)) out.insert(b);
    return out;
}

void World::watchSuccession() {
    if (playerHousehold_ == kNone || playerHead() == kNone) return;
    Household& h = households_[playerHousehold_];
    const Id head = playerHead();
    const Id heir = planHeir(h, head, h.passBy, h.passHeir);
    std::set<Id> kin = homeKin();
    if (head != heirWatchHead_) {
        // A new head (or the first look): the line as it stands is the starting point, said by the hand-over itself.
        heirWatchHead_ = head, lastHeir_ = heir, homeKin_ = kin;
        return;
    }
    if (heir != lastHeir_) {
        const Id old = lastHeir_;
        lastHeir_ = heir;
        std::string next = heir != kNone ? "家業往後改由 " + heirTag(heir) + " 接。" : "眼下家裡沒有能接家業的人了。";
        if (old != kNone) {
            const Person& o = people_[old];
            std::string why;
            const StationDef* sd = defs_.station(o.station);
            if (!o.alive()) why = "死了";
            else if (o.flags.count("felon")) why = "成了罪犯，什麼也不能繼承";
            else if (sd && sd->lasting) why = sd->name + "，不回來接家業了";
            else if (o.household != playerHousehold_ && !(isAway(o) && o.awayFrom == playerHousehold_))
                why = o.household != kNone && households_[o.household].head != kNone && households_[o.household].head != old
                          ? "搬到 " + fullName(households_[o.household].head) + " 家去了"
                          : "離了家";
            else why = "照現在的分法排到了後面";
            log(1, "【繼承人】原本要接家業的 " + shortName(old) + why + "。" + next);
        } else if (heir != kNone) {
            log(1, "【繼承人】" + next);
        }
    }
    // A son or brother leaving the house for good (marrying into another, a lasting place away): his share is asked.
    for (Id x : homeKin_) {
        if (kin.count(x)) continue;
        const Person& q = people_[x];
        if (!q.alive() || q.flags.count("felon") || x == head || q.flags.count("share_asked")) continue;
        // Gone only for a while (a place in service, hired, an apprenticeship, the wars): still a son of the house.
        const StationDef* qs = defs_.station(q.station);
        if (q.home == playerHousehold_ || (qs && !qs->lasting && q.station != "home" && isAway(q))) continue;
        // Set up in a house of his own by the family (分家) has had his share already.
        if (q.household != kNone && households_[q.household].head == x && households_[q.household].dynasty == people_[head].dynasty) continue;
        askShare(x);
    }
    homeKin_ = kin;
}

// 「他的那一份」: a son or brother of the head has left the house for good; what he takes of it. Asked once for each.
void World::askShare(Id x) {
    Household& h = households_[playerHousehold_];
    people_[x].flags.insert("share_asked");
    const Person& q = people_[x];
    const Id head = playerHead();
    const StationDef* sd = defs_.station(q.station);
    // Where he went: a house of his own, one he married into, or a lasting place (heirTag already names that).
    const bool has = q.household != kNone && !households_[q.household].extinct && q.household != playerHousehold_;
    const bool heads = has && households_[q.household].head == x;
    const bool marriedIn = has && !heads && q.spouse != kNone && people_[q.spouse].household == q.household;
    const Id there = heads || marriedIn ? q.household : kNone;  // the only house his share may go to
    std::string where = heads       ? std::string("自己當了家")
                        : marriedIn ? "入贅到 " + fullName(households_[q.household].head) + " 家"
                        : sd && sd->lasting ? std::string("不回來了")
                        : has && households_[q.household].head != kNone ? "搬進了 " + fullName(households_[q.household].head) + " 家"
                                                                        : std::string("離了家");
    const double cash = std::floor(std::min(30.0, std::max(0.0, h.money) * 0.15));
    const double acres = there != kNone && h.acres >= 12 ? std::min(3.0, std::floor(h.acres * 0.1)) : 0;
    std::string sit = heirTag(x) + where + "。往後家業沒有他的份了：他那一份怎麼算？（你家有 " + n0(h.money) + " 便士、" + n0(h.acres) + " 畝地。）";
    std::vector<std::string> opts;
    std::vector<int> what;
    if (cash >= 5) opts.push_back("給他 " + n0(cash) + " 便士帶走（錢 " + n0(h.money) + " → " + n0(h.money - cash) + "；他記你的情）"), what.push_back(1);
    if (acres >= 1) opts.push_back("分 " + n0(acres) + " 畝地給他（地 " + n0(h.acres) + " → " + n0(h.acres - acres) + " 畝；他記你的情）"), what.push_back(2);
    opts.push_back("什麼也不給：離了家就沒他的份（他會覺得被虧待）"), what.push_back(0);
    int r = askPlayer("他的那一份", sit, opts);
    if (r < 0) r = cash >= 5 && h.money >= 200 ? 0 : static_cast<int>(opts.size()) - 1;
    const int w = what[r];
    std::string line = "〔他的那一份〕" + sit + "（選擇：" + opts[r].substr(0, opts[r].find("（")) + "）";
    if (w == 1) {
        h.money -= cash, book(h, "給離家的" + q.firstName + "的錢", -cash);
        if (there != kNone) households_[there].money += cash, book(households_[there], q.firstName + "從本家帶來的錢", cash);
        else purse_[x].money += cash, purse_[x].from = h.id;
        noteGiven(x, 0, cash, "離家時給");
        addOpinion(x, head, "pass_favoured", 10, 15);
        line += q.firstName + "收下了錢。";
    } else if (w == 2) {
        double got = moveLand(h.id, there, acres, LandPick::CustomaryFirst, "離家時分的");
        noteGiven(x, got, 0, "離家時分");
        addOpinion(x, head, "pass_favoured", 14, 20);
        line += "分了 " + n0(got) + " 畝給 " + q.firstName + "。";
    } else {
        addOpinion(x, head, "pass_slighted", -10, 15);
        line += q.firstName + "什麼也沒拿到，心裡記著。";
    }
    log(1, line);
}

void World::settlePassing(Household& h, bool change) {
    const Id head = h.head;
    if (head == kNone) return;
    std::vector<Id> sons = sonsOf(h, head);
    if (sons.size() < 2 || ageYears(people_[sons[0]]) < 14) return;
    people_[head].flags.insert("pass_asked");
    const bool player = inPlayerHouse(h.id) && head == actingHead();
    const double fine = namingFine(h);
    const double fee = change && !h.passBy.empty() ? param("pass_change_fee", 10) : 0;
    struct Opt { std::string by, younger; Id named = kNone; std::string label; };
    std::vector<Opt> opts = {{"eldest", "cash", kNone, "長子繼承，弟弟們給錢"}, {"eldest", "trade", kNone, "長子繼承，弟弟們送去學手藝"}};
    if (h.acres >= 12) opts.push_back({"eldest", "portion", kNone, "長子繼承，弟弟們各分一小塊地"});
    opts.push_back({"split", "", kNone, "兒子們平分"});
    for (size_t i = 1; i < sons.size() && i < 3; ++i)
        if (ageYears(people_[sons[i]]) >= 12) opts.push_back({"named", "", sons[i], "立 " + shortName(sons[i]) + " 當繼承人"});
    auto same = [&](const Opt& o) { return o.by == (h.passBy.empty() ? "eldest" : h.passBy) && (o.by != "eldest" || o.younger == (h.passYounger.empty() ? "cash" : h.passYounger)) && (o.by != "named" || o.named == h.passHeir); };
    int pick = -1;
    if (player) {
        std::vector<std::string> texts, closed;
        std::vector<int> idx;
        for (size_t i = 0; i < opts.size(); ++i) {
            const Opt& o = opts[i];
            if (change && same(o)) continue;
            double cost = fee + (o.by == "named" ? fine : 0);
            std::string t = o.label + (cost > 0 ? "（上領主法庭，" + std::string(o.by == "named" ? "罰金" : "規費") + " " + n0(cost) + " 便士）" : "") + "：" +
                            passWords(h, head, o.by, o.younger, o.named);
            if (cost > h.money + 0.5) closed.push_back(t + "（錢不夠）");
            else texts.push_back(t), idx.push_back(static_cast<int>(i));
        }
        texts.push_back(change ? "照現在的不改" : "先不定，照習俗來（長子繼承，弟弟們給幾個錢）");
        std::string sons_;
        for (Id s : sons) sons_ += (sons_.empty() ? "" : "、") + shortName(s) + "（" + n0(ageYears(people_[s])) + " 歲" + (people_[s].spouse != kNone ? "，成了親" : "") + "）";
        std::string sit = "你有 " + std::to_string(sons.size()) + " 個兒子：" + sons_ + "。家裡 " + n0(h.acres) + " 畝地、" + n0(h.money) +
                          " 便士。你死後家業怎麼分，現在由你定（照現在的家業算）；怎麼分都有人滿意、有人不滿，不滿的人會記著。" +
                          (change ? "改分法要再上一次莊園法庭。" : "定了以後要改，得再上一次莊園法庭。") +
                          "另外，寡婦按規矩終身有三分之一的地；家名（名聲）交班時傳一半給繼承人。";
        int r = askPlayer("身後的家業", sit, texts, closed);
        if (r == static_cast<int>(texts.size()) - 1) {
            log(1, "〔身後的家業〕" + std::string(change ? "分法照舊。" : "先照習俗來。") + "（選擇：" + texts.back() + "）");
            return;
        }
        if (r >= 0) pick = idx[r];
    }
    if (pick < 0) {
        // The rules: most heads keep to custom; a kind or generous one gives the younger more; one who has fallen out
        // with his eldest and dotes on another names that one, if the purse runs to the fine.
        const Person& hp = people_[head];
        std::vector<double> w;
        for (const auto& o : opts) {
            double x = 0;
            if (o.by == "eldest" && o.younger == "cash") x = 40 + (hasTrait(hp, "greedy") ? 12 : 0);
            else if (o.by == "eldest" && o.younger == "trade") x = 10 + (h.money >= 80 ? 8 : 0) + (hasTrait(hp, "ambitious") ? 6 : 0);
            // Villein land went whole to one heir by the custom of the manor: carving it up was rare, for a big holding
            // and a soft-hearted father (round 10: at 14/8 the 25-year pre-run broke the village into cottages).
            else if (o.by == "eldest") x = 3 + (hasTrait(hp, "kind") ? 3 : 0) + (h.acres >= 25 ? 6 : 0);
            else if (o.by == "split") x = 0.5 + (hasTrait(hp, "kind") ? 1 : 0) + (hasTrait(hp, "generous") ? 1 : 0);
            else if (o.by == "named" && h.money >= fine + fee + 20) {
                double gap = opinion(head, o.named) - opinion(head, sons[0]);
                x = gap > 40 ? 18 + (gap - 40) * 0.4 + (hasTrait(hp, "wrathful") || hasTrait(hp, "cruel") ? 8 : 0) : 0;
            }
            if (change && same(o)) x = 0;
            w.push_back(x);
        }
        // A change is rarely worth the court's fee and the bad blood: the rule changes only for a reason (named).
        if (change) {
            bool reason = false;
            for (size_t i = 0; i < opts.size(); ++i) reason = reason || (opts[i].by == "named" && w[i] > 0);
            if (!reason) return;
            for (size_t i = 0; i < opts.size(); ++i)
                if (opts[i].by != "named") w[i] = 0;
        }
        pick = rngKin_.weighted(w);
        if (pick < 0) return;
    }
    const Opt& o = opts[static_cast<size_t>(pick)];
    // What the sons stood to get before, against what they get now: who is sore, who is pleased.
    std::vector<Share> before = passPlan(h, head, h.passBy, h.passYounger, h.passHeir), now = passPlan(h, head, o.by, o.younger, o.named);
    const double fair = (h.acres + std::max(0.0, h.money) / 60) / std::max<size_t>(1, sons.size());
    double cost = fee + (o.by == "named" ? fine : 0);
    if (cost > 0) payLord(h, cost, o.by == "named" ? "立繼承人的罰金" : "改分家產的規費");
    const bool had = !h.passBy.empty();
    h.passBy = o.by, h.passYounger = o.younger, h.passHeir = o.named, h.passSince = day_;
    auto worth = [](const std::vector<Share>& v, Id s) {
        for (const auto& x : v)
            if (x.who == s) return x.acres + x.money / 60;
        return 0.0;
    };
    for (Id s : sons) {
        double got = worth(now, s), was = worth(before, s);
        if (got < fair * 0.6) addOpinion(s, head, "pass_slighted", -std::min(20.0, 6 + 12 * (1 - got / fair)), 15);
        else if (got > fair * 1.2 && s != sons[0]) addOpinion(s, head, "pass_favoured", 6, 10);
        if (had && got + 0.5 < was) addOpinion(s, head, "pass_changed", -6, 10);
    }
    if (o.by == "named") recordDeed("passed_over", head, sons[0]);
    std::string words = passWords(h, head, o.by, o.younger, o.named);
    if (o.by == "named") addNews("kin:named", head, o.named, h.id);
    else addNews("kin:settled", head, kNone, h.id, kNone, o.label);
    ++counters_["kin_pass:" + o.by + (o.younger.empty() ? "" : ":" + o.younger)];
    if (inPlayerHouse(h.id))
        log(1, "〔身後的家業〕" + o.label + (cost > 0 ? "，在莊園法庭立了案（交了 " + n0(cost) + " 便士）" : "") + "：" + words + "（選擇：" + o.label + "）");
}

void World::noteGiven(Id who, double acres, double money, const std::string& what) {
    if (who == kNone || acres + money / 60 < 0.05) return;
    given_[who].push_back({day_, acres, money, what});
}

double World::givenWorth(Id who, int after, bool moneyOnly) const {
    auto it = given_.find(who);
    if (it == given_.end()) return 0;
    double v = 0;
    for (const auto& g : it->second)
        if (g.day > after) v += (moneyOnly ? 0 : g.acres) + g.money / 60;
    return v;
}

std::string World::givenText(Id who, int after) const {
    auto it = given_.find(who);
    if (it == given_.end()) return "";
    std::string out;
    for (const auto& g : it->second) {
        if (g.day <= after) continue;
        std::string x = std::to_string(cfg_.startYear + g.day / kDaysPerYear) + " 年" + g.what + " ";
        if (g.acres >= 0.5) x += n0(g.acres) + " 畝";
        if (g.money >= 1) x += (g.acres >= 0.5 ? "、" : "") + n0(g.money) + " 便士";
        out += (out.empty() ? "" : "；") + x;
    }
    return out;
}

Id World::setUpSon(Id hid, Id son, double own, double vac, double money, const std::string& how) {
    Person& sub = people_[son];
    if (vac + own < 1) vac = own = 0;  // no land: a cottage and its garden, and day labour
    own = std::clamp(own, 0.0, std::max(0.0, households_[hid].acres - 1));
    double gift = std::min(money, std::max(0.0, households_[hid].money));
    noteGiven(son, own >= 1 ? own : 0, gift, how.rfind("安撫", 0) == 0 ? "安撫" : "分家");  // (named here: L7)
    Id nh = addHousehold(sub.dynasty, 0, gift);  // households_ may move: by index from here on
    households_[hid].money -= gift;
    book(households_[hid], "給" + sub.firstName + "分家的錢", -gift);
    if (vac >= 1) moveLand(kNone, nh, vac, LandPick::CustomaryFirst, "分家時向領主租來");
    if (own >= 1) moveLand(hid, nh, own, LandPick::CustomaryFirst, how);
    std::vector<Id> going{son};
    bool wife = sub.spouse != kNone && people_[sub.spouse].alive() && people_[sub.spouse].household == hid && households_[hid].head != sub.spouse;
    if (wife) going.push_back(sub.spouse);
    int kids = 0;
    // (Not a child who heads the house he leaves: a father who won his share from his son goes without him.)
    for (Id c : sub.children)
        if (people_[c].alive() && people_[c].household == hid && people_[c].home == kNone && c != households_[hid].head) going.push_back(c), ++kids;
    Household& old = households_[hid];
    Household& fresh = households_[nh];
    for (Id who : going) {
        if (who == kNone || people_[who].household != hid) continue;
        old.members.erase(std::remove(old.members.begin(), old.members.end(), who), old.members.end());
        people_[who].household = nh;
        fresh.members.push_back(who);
    }
    fresh.head = son;
    settleAsHead(son);
    // What he had of his own under his father's roof goes with him.
    for (Id who : going)
        if (auto it = purse_.find(who); it != purse_.end()) {
            fresh.money += it->second.money, book(fresh, "帶出來的私房錢", it->second.money);
            purse_.erase(it);
        }
    double food = std::min(old.food * 0.3, consumption(fresh) * 120);
    fresh.food = food;
    old.food -= food;
    bookFood(old, "分家帶走的糧（" + people_[son].firstName + "）", -food);
    fresh.firewood = 20;
    noteLife(son, "分家自立門戶");
    if (inPlayerHouse(hid) && !quietSetUp_)
        log(1, "【分家】" + fullName(son) + (wife && kids ? " 帶著妻兒" : wife ? " 帶著妻子" : kids ? " 帶著孩子" : " 一個人") + "搬出去，自立門戶：" +
                   (fresh.acres < 0.5 ? std::string("沒有地，只有屋後一塊菜園，靠做工過活；") : n0(fresh.acres) + " 畝地（" + (own >= 1 ? "自家分出 " + n0(own) + " 畝" : std::string("向領主租的")) + "）、") +
                   n0(gift) + " 便士。你家剩 " + n0(old.acres) + " 畝。往後他家是另一戶，可還是你家的血脈。");
    return nh;
}

void World::applyPassing(Household& hRef, Id dead, Id heir, bool living) {
    const Id hid = hRef.id;
    const std::string by = hRef.passBy, younger = hRef.passYounger;
    const Id named = hRef.passHeir;
    hRef.passBy.clear(), hRef.passYounger.clear(), hRef.passHeir = kNone, hRef.passSince = -1;
    const bool player = inPlayerHouse(hid);
    const Id line = households_[hid].ward != kNone ? households_[hid].ward : heir;  // the one the house is for
    // On the footing the card showed (round 10 M4): the widow's third is reckoned first, on the holding as it stands,
    // and the sons' shares (passPlan) come out of the rest.
    const double third = households_[hid].acres >= 3 ? std::round(households_[hid].acres / 3) : 0;
    std::vector<Id> sons = sonsOf(households_[hid], dead);
    std::vector<Share> plan = passPlan(households_[hid], dead, by, younger, named);
    const double fair = (households_[hid].acres + std::max(0.0, households_[hid].money) / 60) / std::max<size_t>(1, sons.size());
    // A son named for his father: the father is 「已故的…」 (or 「老…」 while he lives), so the line reads.
    bool clash = false;
    for (const auto& x : plan) clash = clash || (x.who != dead && people_[x.who].firstName == people_[dead].firstName);
    const std::string deadName = (clash ? std::string(living ? "老" : "已故的") : std::string()) + people_[dead].firstName;
    std::vector<std::string> said;
    // What the sons had already been given (a portion when they were set up, coin to quiet them): their share is
    // weighed with it, and the fair share counts it too (fun-r12 A6).
    std::map<Id, double> prior;
    double priorAll = 0;
    for (Id s : sons) priorAll += prior[s] = givenWorth(s);
    const double fairAll = (households_[hid].acres + std::max(0.0, households_[hid].money) / 60 + priorAll) / std::max<size_t>(1, sons.size());
    quietSetUp_ = true;  // one 【分家產】 line says it all
    for (size_t i = 1; i < plan.size(); ++i) {
        const Share& s = plan[i];
        if (s.who == line || s.who == heir) continue;
        Person& q = people_[s.who];
        if (!q.alive()) continue;
        const std::string before = givenText(s.who);
        const bool here = q.household == hid;
        const bool own = q.household != kNone && q.household != hid && households_[q.household].head == q.id;
        double gotA = 0, gotM = 0;  // what he really had (L6)
        if (s.what == "cash" && s.money >= 1) {
            double cash = std::min(s.money, std::max(0.0, households_[hid].money));
            if (cash >= 1 && own) {
                households_[hid].money -= cash, book(households_[hid], "分家產給" + q.firstName + "的錢", -cash);
                households_[q.household].money += cash, book(households_[q.household], "分家產分到的錢", cash);
                said.push_back(q.firstName + "拿了 " + n0(cash) + " 便士"), gotM = cash;
                noteGiven(q.id, 0, cash, "分家產");
            } else if (cash >= 1 && here && (q.spouse != kNone || ageYears(q) >= 21)) {
                setUpSon(hid, q.id, 0, 0, cash, "分家產");
                said.push_back(q.firstName + "拿了 " + n0(cash) + " 便士，搬去村邊小屋"), gotM = cash;
            } else if (cash >= 1 && here) {
                households_[hid].money -= cash, book(households_[hid], "給" + q.firstName + "出去謀生的錢", -cash);
                setStation(q.id, "town_labourer", 3, "拿了 " + n0(cash) + " 便士出去謀生");
                said.push_back(q.firstName + "拿了 " + n0(cash) + " 便士去鎮上謀生"), gotM = cash;
                noteGiven(q.id, 0, cash, "分家產");
            }
        } else if (s.what == "trade" && here) {
            double fee = std::min(s.money, std::max(0.0, households_[hid].money));
            households_[hid].money -= fee, book(households_[hid], q.firstName + "的拜師錢", -fee);
            setStation(q.id, "town_apprentice", 5, "照" + deadName + (living ? "定的安排" : "生前的安排") + "，到鎮上拜師學手藝");
            said.push_back(q.firstName + "帶著 " + n0(fee) + " 便士拜師錢去鎮上學手藝"), gotM = fee;
            noteGiven(q.id, 0, fee, "拜師錢");
        } else if ((s.what == "portion" || s.what == "split") && s.acres >= 1) {
            double a = std::min(s.acres, households_[hid].acres - 1);
            if (a >= 1 && own) {
                gotA = moveLand(hid, q.household, a, LandPick::CustomaryFirst, "分家產分到的");
                said.push_back(q.firstName + "分到 " + n0(gotA) + " 畝，併進他自己家");
                noteGiven(q.id, gotA, 0, "分家產");
            } else if (a >= 1 && here) {
                setUpSon(hid, q.id, a, 0, 0, "分家產分到的");
                said.push_back(q.firstName + "分到 " + n0(a) + " 畝，自立門戶"), gotA = a;
            }
        }
        if (s.acres + s.money >= 1 && gotA + gotM < 1)
            said.push_back(q.firstName + (!here && !own ? "人在外地，那一份沒分到" : "那一份家裡給不出來"));
        // A meagre share stays with him for years: against the brother who got the rest. What he had been given
        // before (his portion when he was set up, coin to quiet him) counts with what the passing gave him.
        (void)fair;
        const double got = gotA + gotM / 60 + prior[q.id];
        if (got < fairAll * 0.6 && line != kNone)
            passGrudges_.push_back({q.id, line, (8 + 18 * (1 - got / std::max(1.0, fairAll))) * std::clamp(fairAll * sons.size() / 12, 0.3, 1.8), day_,
                                    deadName + "的家業 " + shortName(line) + " 拿了大半，" + q.firstName + "這回" +
                                        (gotA >= 1 ? "只分到 " + n0(gotA) + " 畝" : gotM >= 1 ? "只分到 " + n0(gotM) + " 便士" : std::string("什麼也沒分到")) +
                                        (before.empty() ? std::string() : "（先前給過：" + before + "）"),
                                    "照習俗分一份家業", 0.25, fairAll - got});
    }
    quietSetUp_ = false;
    // The eldest passed over by a named heir: the strongest grievance a passing leaves, and a claim a court may hear.
    if (by == "named" && line == named && !sons.empty() && sons[0] != line && people_[sons[0]].alive() && line != kNone) {
        passGrudges_.push_back({sons[0], line, 30, day_, deadName + "立了 " + shortName(line) + " 當繼承人，長子 " + shortName(sons[0]) + " 什麼也沒分到", "長子的繼承權", 0.5});
        said.push_back("長子 " + shortName(sons[0]) + " 什麼也沒分到");
    }
    // Split: the eldest has only his share of what would have been his.
    if (by == "split" && sons.size() >= 2 && sons[0] == line)
        for (size_t i = 1; i < plan.size(); ++i)
            if (plan[i].acres >= 1) {
                passGrudges_.push_back({line, plan[i].who, 12, day_, "兒子們平分，" + shortName(plan[i].who) + "分走了 " + n0(plan[i].acres) + " 畝", "", 0});
                break;
            }
    if (player && !said.empty()) {
        std::string t;
        for (const auto& x : said) t += (t.empty() ? "" : "；") + x;
        const std::string how = by == "split" ? "兒子們平分" : by == "named" ? "立了繼承人" : "長子繼承";
        log(1, "【分家產】" + (by.empty() ? std::string("照習俗（長子繼承，弟弟們給幾個錢）") : "照" + deadName + (living ? "定的分法" : "生前定的分法") + "（" + how + "）") +
                   "：" + t + "。家裡剩 " + n0(households_[hid].acres) + " 畝、" + n0(households_[hid].money) + " 便士。");
    }
    if (!living) applyDower(hid, dead, line, third);
}

void World::applyDower(Id hid, Id dead, Id line, double a) {
    // The widow's third, for her life.
    if (hid == kNone || line == kNone || a < 1) return;
    Id w = people_[dead].spouse;
    if (w == kNone || !people_[w].alive() || people_[w].household != hid || households_[hid].head == w || line == w || people_[w].sex != Sex::Female) return;  // widows only (review M4)
    const bool player = inPlayerHouse(hid);
    Person& wp = people_[w];
    if (people_[line].mother == w) {
        dowers_.push_back({w, hid, hid, a, day_, true});
        if (player) log(1, "【寡婦產】" + wp.firstName + "按規矩終身有三分之一的地（" + n0(a) + " 畝）；她跟著兒子住，地照舊一起種，要是改嫁就帶走。");
        return;
    }
    a = std::min(a, households_[hid].acres);
    if (a < 1) return;
    // Not her son: she takes her third to a cottage of her own, with her own little ones.
    const Id keep = households_[hid].head;
    Id nh = addHousehold(wp.dynasty, 0, 0);
    std::vector<Id> going{w};
    for (Id c : households_[hid].members)
        if (c != line && c != keep && people_[c].mother == w && ageYears(people_[c]) < 16 && people_[c].home == kNone) going.push_back(c);
    for (Id who : going) {
        auto& mem = households_[hid].members;
        mem.erase(std::remove(mem.begin(), mem.end(), who), mem.end());
        people_[who].household = nh;
        households_[nh].members.push_back(who);
    }
    households_[nh].head = w;
    settleAsHead(w);
    double got = moveLand(hid, nh, a, LandPick::CustomaryFirst, "寡婦產");
    double m = std::floor(std::max(0.0, households_[hid].money) * 0.2);
    households_[hid].money -= m, book(households_[hid], "寡婦帶走的錢", -m);
    households_[nh].money += m;
    double food = std::min(households_[hid].food * 0.3, consumption(households_[nh]) * 150);
    households_[hid].food -= food, households_[nh].food = food, households_[nh].firewood = 20;
    bookFood(households_[hid], "寡婦帶走的糧", -food);
    dowers_.push_back({w, hid, nh, got, day_, true});
    addNews("kin:dower", w, kNone, nh, kNone, "三分之一的地（" + n0(got) + " 畝）");
    passGrudges_.push_back({line, w, 10 + std::min(10.0, got / 2), day_, "後母" + wp.firstName + "守著 " + n0(got) + " 畝寡婦產，地分不回來", "", 0});
    ++counters_["kin_dower_out"];
    if (player)
        log(1, "【寡婦產】" + wp.firstName + "不是 " + shortName(line) + " 的親娘，按規矩帶著三分之一的地（" + n0(got) + " 畝）和 " + n0(m) +
                   " 便士，搬到村邊的小屋守寡終身" + (going.size() > 1 ? "，她自己的孩子跟著她" : "") + "。她死了，地才回到這個家。");
}

std::string World::dowerNote(Id widow) const {
    if (widow == kNone || people_[widow].household == kNone) return "";
    double a = 0;
    for (const auto& d : dowers_)
        if (d.open && d.widow == widow && d.in == people_[widow].household) a += d.acres;
    if (a < 1) return "";
    return "（" + people_[widow].firstName + "改嫁會帶走寡婦產 " + n0(a) + " 畝，她死了才回來）";
}

void World::pursesMonth() {
    // What one took or was given under another's roof is his own: it goes with him to a house he heads or marries into,
    // and back to the house it came from if he dies or is outlawed.
    for (auto it = purse_.begin(); it != purse_.end();) {
        const Person& q = people_[it->first];
        const bool gone = !q.alive() || q.flags.count("felon");
        Id to = kNone;
        if (gone) to = it->second.from;
        else if (q.household != kNone && (households_[q.household].head == q.id || q.household != it->second.from)) to = q.household;
        else {
            ++it;
            continue;
        }
        if (to != kNone && !households_[to].extinct && it->second.money >= 0.5)
            households_[to].money += it->second.money, book(households_[to], gone ? "回到家裡的私房錢" : "帶過來的私房錢", it->second.money);
        it = purse_.erase(it);
    }
}

void World::dowerReturns(Id widow) {
    for (auto& d : dowers_) {
        if (!d.open || d.widow != widow) continue;
        d.open = false;
        if (d.in == d.from || households_[d.from].extinct || households_[d.from].head == kNone || households_[d.in].extinct) continue;
        double got = moveLand(d.in, d.from, std::min(d.acres, households_[d.in].acres), LandPick::CustomaryFirst, "寡婦產歸還");
        if (got >= 0.5 && inPlayerHouse(d.from)) log(1, "【寡婦產】" + people_[widow].firstName + "去世，她守的 " + n0(got) + " 畝寡婦產回到了家裡。");
        else if (got >= 0.5 && inPlayerHouse(d.in)) log(1, "【寡婦產】" + people_[widow].firstName + "去世，她帶來的 " + n0(got) + " 畝寡婦產回到了前夫家。");
    }
}

void World::dowerMoves(Id widow, Id to) {
    for (auto& d : dowers_) {
        if (!d.open || d.widow != widow || d.in == to || to == kNone || households_[d.in].extinct) continue;
        double got = moveLand(d.in, to, std::min(d.acres, households_[d.in].acres), LandPick::CustomaryFirst, "寡婦改嫁帶過來的寡婦產");
        if (got >= 0.5 && (inPlayerHouse(d.in) || inPlayerHouse(d.from)))
            log(1, "【寡婦產】" + people_[widow].firstName + "改嫁，帶走了她那三分之一的地（" + n0(got) + " 畝）；她死了才回來。");
        if (got >= 0.5) addNews("kin:dower", widow, kNone, to, kNone, "改嫁時帶走的 " + n0(got) + " 畝寡婦產");
        d.in = to;
    }
}

void World::dowerHouseEnds(Id house) {
    for (auto& d : dowers_) {
        if (!d.open || d.in != house) continue;
        Id to = people_[d.widow].alive() && people_[d.widow].household != kNone && people_[d.widow].household != house ? people_[d.widow].household : d.from;
        if (to == house || households_[to].extinct || households_[to].head == kNone) {
            d.open = false;
            continue;
        }
        moveLand(house, to, std::min(d.acres, households_[house].acres), LandPick::CustomaryFirst, "寡婦產歸還");
        d.in = to;
        if (to == d.from) d.open = people_[d.widow].alive();
    }
}

void World::passReputation(Id from, Id to) {
    if (from == kNone || to == kNone || from == to) return;
    if (inPlayerHouse(people_[to].household)) repPassed_.clear();
    const double carried = std::round(people_[from].reputation * param("rep_carry", 0.5));
    const double own = people_[to].reputation;
    if (std::abs(carried) < 1) return;
    addReputation(to, carried, "接下" + people_[from].firstName + "的家名（傳一半）");
    // Only this house's hand-over scene says it (round 10 L1): a branch's passing never leaves a line behind.
    if (inPlayerHouse(people_[to].household))
        repPassed_ = "名聲：" + people_[from].firstName + "是 " + sgn(people_[from].reputation) + "，照規矩家名傳一半（" + sgn(carried) + "）；" +
                     people_[to].firstName + "自己原有 " + sgn(own) + "，現在是 " + sgn(people_[to].reputation) + "。";
}

// ---- The old parent's keep (養老契約) --------------------------------------------------------------------------

void World::considerUpkeep(Household& h) {
    const Id head = h.head;
    if (head == kNone || h.ward != kNone || h.house == "manor" || head == lord_ || head == priest_) return;
    const Person& hp = people_[head];
    const double age = ageYears(hp);
    if (!(age >= param("upkeep_age", 62) || (age >= 52 && hp.health < 45))) return;
    for (const auto& u : upkeeps_)
        if (u.open && u.parent == head) return;
    Id heir = heirOf(h, head);
    if (heir == kNone || people_[heir].household != h.id || ageYears(people_[heir]) < 21 || people_[heir].flags.count("felon")) return;
    if (closeKin(heir, head) == false && people_[heir].father != head && people_[heir].mother != head) return;
    const bool player = inPlayerHouse(h.id) && head == actingHead();
    const double fine = std::round((h.acres - h.freehold) * param("entry_fine_per_acre", 4) * 0.5);
    int r = -1;
    if (player) {
        auto cd = cooldowns_.find({head, "upkeep_ask"});
        if (cd != cooldowns_.end() && cd->second > day_) return;
        cooldowns_[{head, "upkeep_ask"}] = day_ + 3 * kDaysPerYear;
        std::string sit = "你 " + n0(age) + " 歲了" + (hp.health < 45 ? "，身子也大不如前" : "") + "。按村裡的老規矩，可以在莊園法庭把地交給 " + fullName(heir) +
                          "（" + n0(ageYears(people_[heir])) + " 歲），由法庭記下奉養的條件：每年的口糧、灶邊一間屋、一年一身衣服。從此由" + shortName(heir) +
                          "當家（往後由你扮演 " + people_[heir].firstName + "），入地費約 " + n0(fine) + " 便士由新家主交。" +
                          "他要是不照約奉養，老人可以到莊園法庭告他；照不照做，看他的性子、對你的感情和家裡的難處。";
        // What was settled for the younger sons is carried out at the hand-over, not left for a death that no longer passes the house (M3).
        if (sonsOf(h, head).size() >= 2)
            sit += "交家時，家業就照" + std::string(h.passBy.empty() ? "習俗" : "你定的分法") + "分下去：" + passWords(h, head, h.passBy, h.passYounger, h.passHeir);
        r = askPlayer("養老", sit, {"在莊園法庭把地交給 " + shortName(heir) + "，立下奉養的約", "還不交，自己當家"});
        if (r == 1) {
            log(1, "〔養老〕" + hp.firstName + "還不想交家。（選擇：還不交，自己當家）");
            return;
        }
    }
    if (r < 0) {
        double c = 0.18 + (hp.health < 45 ? 0.2 : 0) + (age >= 70 ? 0.15 : 0) - (hasTrait(hp, "ambitious") || hasTrait(hp, "greedy") ? 0.12 : 0) -
                   (opinion(head, heir) < -10 ? 0.15 : 0);
        if (!rngKin_.chance(std::max(0.02, c))) return;
    }
    upkeeps_.push_back({head, heir, h.id, day_, 0, 0, -1, true});
    ++counters_["kin_upkeep"];
    addNews("kin:upkeep", head, heir, h.id);
    if (inPlayerHouse(h.id))
        log(1, "【養老】" + fullName(head) + " 在莊園法庭把家業交給 " + fullName(heir) + "，法庭記下奉養的條件：每年的口糧、灶邊一間屋、一年一身衣服。" +
                   (player ? "（選擇：在莊園法庭把地交給 " + shortName(heir) + "）" : ""));
    passReputation(head, heir);
    const Id hid = h.id;
    // The old head's arrangement is carried out now (the widow's third waits for his death: life.cpp kill), and the new
    // head starts with none of his own (round 10 M3).
    applyPassing(households_[hid], head, heir, true);
    handOver(households_[hid], heir, "在莊園法庭立了養老的約，接下家業");
    chargeEntryFine(households_[people_[heir].household], heir, head, households_[hid].acres - households_[hid].freehold);
}

void World::upkeepYear(Upkeep& u) {
    if (!u.open) return;
    const Person& par = people_[u.parent];
    const Person& hr = people_[u.heir];
    if (!par.alive() || !hr.alive() || households_[u.house].extinct || households_[u.house].head != u.heir || par.household != u.house) {
        u.open = false;
        return;
    }
    const Household& h = households_[u.house];
    const bool hard = breadShort(h) > 0 || h.lean;
    const double op = opinion(u.heir, u.parent);
    const bool ordered = u.ordered >= 0 && day_ - u.ordered < 4 * kDaysPerYear;
    std::vector<std::string> opts = {"照約奉養", "剋扣老人的口糧", "把老人趕到牲口棚去住"};
    std::vector<double> w = {60 + std::max(-30.0, op / 2) + (hasTrait(hr, "kind") ? 20 : 0) + (hasTrait(hr, "pious") ? 10 : 0) + (ordered ? 25 : 0),
                             4 + (hasTrait(hr, "greedy") ? 12 : 0) + (hasTrait(hr, "cruel") ? 8 : 0) + (hard ? 22 : 0) + std::max(0.0, -op / 3),
                             u.state >= 1 ? 1.0 + (hasTrait(hr, "cruel") ? 6 : 0) + (hasTrait(hr, "greedy") ? 3 : 0) + (op < -30 ? 8 : 0) : 0};
    if (ordered) w[1] *= 0.3, w[2] *= 0.3;
    int pick = -1;
    if (inPlayerHouse(u.house) && u.heir == actingHead()) {
        // Asked only when it costs something: a lean year, or the terms already broken.
        if (!hard && u.state == 0) return;
        const double share = portion(par) * kDaysPerYear;
        std::string sit = "養老的約：" + par.firstName + "（" + n0(ageYears(par)) + " 歲）的口糧一年約 " + n0(share) + " 天的糧" +
                          (hard ? "，今年家裡的糧不夠吃到秋收" : "") + "。剋扣一半，家裡一年省下約 " + n0(share / 2) + " 天的糧，可" + par.firstName +
                          "的身子會一天天壞下去，還可能到莊園法庭告你（判下來要罰錢、丟名聲）。";
        std::vector<std::string> shown = {"照約奉養（一年 " + n0(share) + " 天的糧）", "剋扣老人的口糧（省下約 " + n0(share / 2) + " 天的糧）"};
        if (u.state >= 1) shown.push_back("把老人趕到牲口棚去住（省得更多，村裡會傳開）");
        pick = askPlayer("養老的約", sit, shown);
        if (pick >= 0) log(1, "〔養老的約〕" + std::string(pick == 0 ? par.firstName + "的那份照給。" : pick == 1 ? par.firstName + "的口糧剋扣了一半。" : par.firstName + "被趕到牲口棚去住。") + "（選擇：" + shown[pick] + "）");
    } else {
        pick = npcDecide(hr, "養老的約", fullName(u.heir) + " 在莊園法庭立過約，要奉養" + kinWord(u.heir, u.parent) + " " + fullName(u.parent) + "。" +
                                       (hard ? "今年家裡的糧不夠吃。" : "") + (ordered ? "莊園法庭前不久才判過要照約奉養。" : ""),
                         opts, w, {u.parent}, 0);
    }
    if (pick < 0) pick = rngKin_.weighted(w);
    if (pick < 0) pick = 0;
    if (pick == 0) {
        if (u.state > 0) addOpinion(u.parent, u.heir, "kept_upkeep", 5, 4);
        u.state = 0;
        return;
    }
    u.state = pick;
    ++u.breaches;
    ++counters_["kin_upkeep_broken"];
    recordDeed("upkeep_broken", u.heir, u.parent, pick == 2 ? 1.0 : 0.6);
    addNews("kin:upkeep_broken", u.parent, u.heir, u.house, kNone, pick == 2 ? "把老人趕到牲口棚去住" : "口糧剋扣了一半");
    if (inPlayerHouse(u.house) && u.heir != actingHead())
        log(1, "【養老】" + fullName(u.heir) + (pick == 2 ? " 把 " + par.firstName + " 趕到牲口棚去住了。" : " 剋扣了 " + par.firstName + " 的口糧。"));
}

// ---- Grievances, worked out from the state ---------------------------------------------------------------------

void World::kinMotives() {
    struct Found { double v = 0; std::vector<std::pair<double, std::string>> why; std::string claim; double merit = 0; };
    std::map<std::pair<Id, Id>, Found> found;
    auto add = [&](Id who, Id against, double v, const std::string& why, const std::string& claim = "", double merit = 0) {
        if (who == kNone || against == kNone || who == against || v < 2) return;
        Found& f = found[{who, against}];
        f.v += v;
        f.why.push_back({v, why});
        if (!claim.empty() && merit >= f.merit) f.claim = claim, f.merit = merit;
    };
    auto grown = [&](Id x) { return x != kNone && people_[x].alive() && people_[x].household != kNone && ageYears(people_[x]) >= 16; };
    for (const auto& h : households_) {
        if (h.extinct || h.head == kNone || h.members.size() < 2 || h.house == "manor" || h.head == priest_) continue;
        const Id head = h.head;
        const Person& hp = people_[head];
        if (!hp.alive()) continue;
        const Id heir = heirOf(h, head);
        std::vector<Id> sons = sonsOf(h, head);
        std::vector<Share> plan = sons.size() >= 2 ? passPlan(h, head, h.passBy, h.passYounger, h.passHeir) : std::vector<Share>{};
        // The fair share counts what the sons were already given (a portion, coin to quiet them: fun-r12 A6).
        double givenAll = 0;
        for (Id s : sons) givenAll += givenWorth(s);
        const double fair = (h.acres + std::max(0.0, h.money) / 60 + givenAll) / std::max<size_t>(1, sons.size());
        // What there is to fight over: a cottage little, a big holding and a full purse a great deal (the late game).
        const double stake = std::clamp((h.acres + std::max(0.0, h.money) / 120) / 12, 0.3, 1.8);
        for (Id m : sons) {
            if (m == head || !grown(m) || people_[m].home != kNone) continue;
            const Person& p = people_[m];
            // A younger son's share, against the brother who would get the rest (and the father who settled it so).
            if (!plan.empty() && std::find(sons.begin(), sons.end(), m) != sons.end() && m != plan[0].who && stake > 0.2) {
                double got = 0;
                for (const auto& s : plan)
                    if (s.who == m) got = s.acres + s.money / 60;
                const double share = got;
                got += givenWorth(m);
                double deficit = std::clamp((fair - got) / std::max(1.0, fair), 0.0, 1.0);
                std::string what = share <= 0 ? "什麼也分不到" : "只分得到 " + [&] { for (const auto& s : plan) if (s.who == m) return s.acres >= 1 ? n0(s.acres) + " 畝" : n0(s.money) + " 便士"; return std::string(); }();
                if (const std::string gv = givenText(m); !gv.empty()) what += "（先前給過：" + gv + "）";
                bool eldestBypassed = m == sons[0] && plan[0].who != m && h.passBy == "named" && plan[0].who == h.passHeir;
                double v = (deficit > 0.2 ? 8 + 22 * deficit : 0) * stake + (eldestBypassed ? 25 * stake : 0);
                std::string why = eldestBypassed ? "父親立了 " + shortName(plan[0].who) + " 當繼承人，長子" + shortName(m) + "什麼也分不到"
                                                 : "照現在的分法，" + shortName(plan[0].who) + "要拿 " + n0(plan[0].acres) + " 畝，" + shortName(m) + what;
                add(m, plan[0].who, v, why);
                add(m, head, v * 0.5, why);
            }
        }
        for (Id m : h.members) {
            if (m == head || !grown(m) || people_[m].home != kNone) continue;
            const Person& p = people_[m];
            // Split among the sons: the eldest has a share of what was his by custom.
            if (!plan.empty() && h.passBy == "split" && m == sons[0] && m == plan[0].who && sons.size() >= 2)
                add(m, head, 18 * (1 - 1.0 / sons.size()) * stake, "父親定了兒子們平分，長子" + shortName(m) + "只拿得到 1/" + std::to_string(sons.size()));
            // The heir kept waiting.
            if (m == heir && (p.father == head || p.mother == head) && ageYears(p) >= 26 && ageYears(hp) >= 50 && hp.health >= 50)
                add(m, head, (5 + (p.spouse != kNone ? 5 : 0) + (hasTrait(p, "ambitious") ? 8 : 0) + (hasTrait(p, "greedy") ? 6 : 0) + (ageYears(p) - 26) * 1.2) * std::max(0.7, stake),
                    shortName(m) + "都 " + n0(ageYears(p)) + " 歲" + (p.spouse != kNone ? "、成了家" : "") + "，還得事事聽" + kinWord(m, head) + "的");
            // A second wife with sons of her own, and a stepson to have it all.
            if (m == hp.spouse && hp.sex == Sex::Male && p.sex == Sex::Female && heir != kNone && heir != m && people_[heir].mother != m && grown(heir)) {
                std::vector<Id> mine;
                for (Id c : p.children)
                    if (people_[c].alive() && people_[c].sex == Sex::Male && people_[c].household == h.id) mine.push_back(c);
                if (!mine.empty())
                    add(m, heir, (14 + 5.0 * mine.size() + (hasTrait(p, "ambitious") ? 6 : 0)) * std::max(0.6, stake), "想讓自己生的 " + shortName(mine[0]) + " 接家業，不是前妻生的 " + shortName(heir));
            }
            // The stepson heir, and the third his stepmother would take.
            if (m == heir && hp.sex == Sex::Male && hp.spouse != kNone && people_[hp.spouse].alive() && people_[hp.spouse].household == h.id && p.mother != hp.spouse &&
                ageYears(hp) >= 50 && h.acres >= 6)
                add(m, hp.spouse, 6 + std::min(10.0, h.acres / 6), kinWord(m, hp.spouse) + "守寡會帶走三分之一的地（約 " + n0(h.acres / 3) + " 畝）");
            // A son-in-law under his wife's father's roof.
            if (p.sex == Sex::Male && p.spouse != kNone && people_[p.spouse].father == head && p.father != head && heir != kNone && heir != m)
                add(m, heir, 6 + (hasTrait(p, "ambitious") ? 6 : 0), "入贅在岳家，家業將來是 " + shortName(heir) + " 的");
            // Husband and wife under one roof, when one has wronged the other (an affair found out, a beating, a match forced).
            if (p.spouse == head && opinion(m, head) < -15)
                add(m, head, 6 + std::min(12.0, (-opinion(m, head) - 15) * 0.3), "跟" + kinWord(m, head) + "早就貌合神離");
            if (m == hp.spouse && opinion(head, m) < -15)
                add(head, m, 6 + std::min(12.0, (-opinion(head, m) - 15) * 0.3), "跟" + kinWord(head, m) + "早就貌合神離");
            // The purse piled up in the chest, and nothing of it in one's own hand (the late game: money no one can touch).
            if (h.money >= 300 && m != heir && m != hp.spouse && ageYears(p) >= 18 && (closeKin(m, head) || (p.spouse != kNone && closeKin(p.spouse, head))))
                add(m, head, std::min(18.0, 4 + h.money / 250) * (hasTrait(p, "greedy") ? 1.5 : 1),
                    "家裡的錢箱攢了 " + n0(h.money) + " 便士，" + p.firstName + "手裡一個子兒也沒有");
            // Parent and grown child who have fallen out under one roof.
            if (p.father == head || p.mother == head) {
                if (opinion(m, head) < -20) add(m, head, 5 + std::min(12.0, (-opinion(m, head) - 20) * 0.3), "跟" + kinWord(m, head) + "鬧得很僵");
                if (opinion(head, m) < -20) add(head, m, 5 + std::min(12.0, (-opinion(head, m) - 20) * 0.3), "跟" + kinWord(head, m) + "鬧得很僵");
            }
            // A son's wife under her mother-in-law, the house crowded.
            if (p.sex == Sex::Female && p.spouse != kNone && people_[p.spouse].father == head && hp.spouse != kNone && hp.spouse != m &&
                people_[hp.spouse].alive() && people_[hp.spouse].household == h.id && opinion(m, hp.spouse) < -5)
                add(m, hp.spouse, 5 + std::min(10.0, (-opinion(m, hp.spouse) - 5) * 0.3) + (h.members.size() > 7 ? 4 : 0),
                    "在" + kinWord(m, hp.spouse) + "手底下過日子" + (h.members.size() > 7 ? "，一屋子 " + std::to_string(h.members.size()) + " 口人" : ""));
            // A grown daughter kept at home, with no portion offered to marry on.
            if (p.sex == Sex::Female && p.father == head && p.spouse == kNone && ageYears(p) >= 22 && h.money >= 60)
                add(m, head, (5 + (ageYears(p) - 22) * 1.5) * std::max(0.6, stake), p.firstName + "都 " + n0(ageYears(p)) + " 歲了，父親手裡有錢，卻不肯出嫁妝");
            // Brothers under one roof who cannot abide each other: the slight decides it, the house is what they fight over.
            if (p.sex == Sex::Male && m != heir && heir != kNone && people_[heir].father == p.father && p.father != kNone && opinion(m, heir) < -15)
                add(m, heir, 5 + std::min(10.0, (-opinion(m, heir) - 15) * 0.25), "跟" + kinWord(m, heir) + "處不來，往後還得在" + std::string(he(people_[heir])) + "手底下過");
        }
    }
    // What a passing left behind (a son passed over, the heir with a widow's third out of the house), fading.
    for (const auto& g : passGrudges_) {
        if (!grown(g.who) || !people_[g.against].alive() || people_[g.against].household == kNone) continue;
        double years = (day_ - g.day) / double(kDaysPerYear);
        // Given since (land, coin to quiet him): it makes up what the passing left him short, and says so.
        const double later = givenWorth(g.who, g.day);
        double points = g.points;
        std::string why = g.why;
        if (later >= 0.05 && g.shortBy > 0) {
            points *= std::max(0.0, 1 - later / g.shortBy);
            why += "；後來又給過：" + givenText(g.who, g.day);
        }
        add(g.who, g.against, points * std::exp(-years / 12), why, g.claim, g.merit);
    }
    // Brothers when the father is dead and one of them sits on the land (passed before the story began, or by a passing
    // already counted above): the fields themselves are the reminder, fading over the years.
    for (const auto& h : households_) {
        if (h.extinct || h.head == kNone || h.house == "manor" || h.head == priest_) continue;
        const Id b = h.head;
        const Person& bp = people_[b];
        if (!grown(b) || bp.father == kNone || people_[bp.father].alive()) continue;
        const Person& fa = people_[bp.father];
        const double yrs = fa.deathDay >= 0 ? (day_ - fa.deathDay) / double(kDaysPerYear) : 10;
        for (Id c : fa.children) {
            if (c == b || !grown(c) || people_[c].sex != Sex::Male || people_[c].occupation == "priest") continue;
            const Person& cp = people_[c];
            const double mine = cp.household != kNone && cp.household != h.id && households_[cp.household].head == c ? households_[cp.household].acres : 0;
            // (His land is counted in his own house; the coin he was given is counted on top: fun-r12 A6.)
            const double gap = h.acres - mine - givenWorth(c, -1, true);
            if (gap < 8) continue;
            bool counted = false;
            for (const auto& g : passGrudges_)
                if (g.who == c && g.against == b) counted = true;
            if (counted) continue;
            add(c, b, (6 + std::min(18.0, gap * 0.6)) * std::exp(-yrs / 15),
                "父親的地歸了" + shortName(b) + "（如今 " + n0(h.acres) + " 畝），" + cp.firstName + (mine >= 1 ? "自己只有 " + n0(mine) + " 畝" : "一壟也沒有") +
                    (givenText(c).empty() ? std::string() : "（家裡先後給過：" + givenText(c) + "）"),
                "分父親留下的地", 0.25);
        }
    }
    // What the climb itself left behind.
    for (const auto& g : griefs_)
        if (g.bitter >= 3 && grown(g.who) && people_[g.against].alive()) add(g.who, g.against, g.bitter, g.bitterWhy);
    // The old parent's keep.
    for (const auto& u : upkeeps_) {
        if (!u.open || u.breaches == 0) continue;
        add(u.parent, u.heir, std::min(60.0, 14.0 * u.breaches + (u.state == 2 ? 20 : u.state == 1 ? 10 : 0)),
            "在莊園法庭立了養老的約，" + shortName(u.heir) + (u.state == 2 ? "卻把老人趕到牲口棚去住" : u.state == 1 ? "卻剋扣老人的口糧" : "以前沒照約奉養"),
            "照養老的約奉養", u.state > 0 ? 0.85 : 0.55);
    }
    // The rival from outside, against the head of the family.
    if (Id rh = rivalHouse(); rh != kNone && playerHead() != kNone && households_[rh].head != kNone && grown(households_[rh].head)) {
        Id rv = households_[rh].head, ph = playerHead();
        double op = opinion(rv, ph);
        add(rv, ph, 10 + feudStage(rv, ph) * 8 + std::max(0.0, -op) / 2.5, "兩家這一代撞在一起" + (rivalAfter().empty() ? std::string() : "：" + rivalAfter()));
    }
    // Slights and temper, on every pair found; and the grievance kept on the books.
    for (auto& g : griefs_) g.open = false;
    for (auto& [key, f] : found) {
        auto [who, against] = key;
        const Person& p = people_[who];
        double op = opinion(who, against);
        if (op < -10) {
            std::string why;
            for (const auto& [lab, v] : opinionBreakdown(who, against))
                if (v < -3 && lab.find("（你做的）") == std::string::npos) {
                    why = opinionLabel(lab);
                    for (const char* s : {"（上一代）", "（對他家人）"})
                        if (auto at = why.find(s); at != std::string::npos) why.erase(at);
                    break;
                }
            f.v += (-op - 10) * 0.45;
            if (!why.empty()) f.why.push_back({(-op - 10) * 0.45, "記著" + why});
        }
        double mult = (hasTrait(p, "greedy") ? 1.3 : 1) * (hasTrait(p, "ambitious") ? 1.2 : 1) * (hasTrait(p, "cruel") ? 1.15 : 1) *
                      (hasTrait(p, "wrathful") ? 1.2 : 1) * (hasTrait(p, "vengeful") ? 1.2 : 1) * (hasTrait(p, "kind") ? 0.6 : 1) *
                      (hasTrait(p, "content") ? 0.7 : 1) * (hasTrait(p, "forgiving") ? 0.6 : 1) * (hasTrait(p, "pious") ? 0.85 : 1) *
                      (op > 40 ? 0.5 : op > 20 ? 0.75 : 1);
        double motive = f.v * mult;
        Grievance* g = nullptr;
        for (auto& x : griefs_)
            if (x.who == who && x.against == against) g = &x;
        if (motive < 15 && (!g || g->pending == 0)) {
            if (g) g->motive = motive;
            continue;
        }
        if (!g) {
            griefs_.push_back({});
            g = &griefs_.back();
            g->who = who, g->against = against;
        }
        if (g->calmUntil > day_) motive *= 0.4;
        std::stable_sort(f.why.begin(), f.why.end(), [](auto& a, auto& b) { return a.first > b.first; });
        g->motive = motive;
        g->why.clear();
        for (size_t i = 0; i < f.why.size() && i < 2; ++i) g->why += (i ? "；" : "") + f.why[i].second;
        g->claim = f.claim, g->merit = f.merit;
        g->open = true;
    }
    // Pending acts stay open whatever the year's sums say: the signs are out.
    for (auto& g : griefs_)
        if (g.pending) g.open = true;
}

// ---- The ladder --------------------------------------------------------------------------------------------------

std::string World::kinWhy(const Grievance& g) const {
    return g.why.empty() ? "心裡不平" : g.why;
}

void World::kinDecide(Grievance& g) {
    const Person& p = people_[g.who];
    const Person& t = people_[g.against];
    if (!p.alive() || !t.alive() || p.household == kNone || t.household == kNone || g.pending) return;
    if (g.who == playerHead() || g.who == actingHead()) return;  // the player climbs it by hand (agenda: 暗地裡對付)
    if (g.calmUntil > day_ || g.motive < param("kin_act_motive", 20)) return;
    const bool same = p.household == t.household;
    const bool spouse = p.spouse == g.against;
    const Household& th = households_[t.household];
    // Who stands with the target's house against it from outside: the rival this generation, or the house it feuds with most.
    Id ally = kNone;
    if (inPlayerHouse(t.household) && rivalHouse() != kNone) ally = households_[rivalHouse()].head;
    if (ally == kNone) {
        double worst = 1;
        for (const auto& f : feuds_) {
            if (f.heat <= 0 || f.stage < 2 || (f.a != t.dynasty && f.b != t.dynasty)) continue;
            Id other = f.a == t.dynasty ? f.b : f.a;
            for (const auto& oh : households_)
                if (!oh.extinct && oh.dynasty == other && oh.head != kNone && f.stage > worst) worst = f.stage, ally = oh.head;
        }
    }
    if (ally == g.who || (ally != kNone && people_[ally].household == p.household)) ally = kNone;
    // Opportunity: alone, old or sick; another at home with the same grievance; the means.
    bool alone = th.members.size() <= 3 || ageYears(t) >= 60 || t.health < 50;
    bool partner = false;
    for (const auto& o : griefs_)
        if (o.open && o.who != g.who && o.against == g.against && o.motive >= 25 && people_[o.who].household == p.household) partner = true;
    bool watched = g.watchUntil > day_;
    const double m = g.motive;
    auto up = [&](int rung) { return std::pow(std::max(0.5, m / 40), rung / 3.0); };
    struct O { int rung; std::string text; double w; };
    std::vector<O> opts;
    opts.push_back({kNothing, "忍著，什麼也不做", 40.0 + (hasTrait(p, "content") ? 15 : 0) + (hasTrait(p, "craven") ? 8 : 0) + (hasTrait(p, "calm") ? 6 : 0)});
    const double moan = (18 + m / 3) * (hasTrait(p, "shy") ? 0.6 : 1) * (hasTrait(p, "gregarious") ? 1.3 : 1);
    // Said once, a complaint goes round for a while: not again within three years (its weight stays with keeping quiet).
    if (g.rung >= kComplain && day_ - g.lastStep < 3 * kDaysPerYear) opts[0].w += moan;
    else opts.push_back({kComplain, "到處跟人訴苦，說" + shortName(g.against) + "待自己不公", moan});
    // A suit judged is closed: brought again only for a new claim or a new reason (fun-r11 1).
    if (!g.claim.empty() && g.rung >= kComplain && (g.sued.empty() || g.sued != suitCause(g) || day_ - g.suedDay >= 5 * kDaysPerYear))
        opts.push_back({kSue, "為了「" + g.claim + "」把" + shortName(g.against) + "告上莊園法庭（鄰居陪審；輸了要罰錢、丟臉）", (6 + g.merit * 30 + m / 6) * up(kSue)});
    if (same && !spouse && households_[p.household].head != g.who && ageYears(p) <= 50 && g.rung >= kComplain)
        opts.push_back({kLeave, "跟" + shortName(g.against) + "鬧翻，離家出走", (5 + m / 8 + (hasTrait(p, "brave") ? 4 : 0)) * up(kLeave)});
    if (g.rung >= kComplain && th.money >= 20 && (same || closeKin(g.who, g.against)))
        opts.push_back({kSteal, "偷偷拿" + shortName(g.against) + "家裡的錢（被撞破就沒臉做人）",
                        (2 + (hasTrait(p, "greedy") ? 7 : 0) + m / 12) * (hasTrait(p, "pious") || hasTrait(p, "kind") ? 0.3 : 1) * up(kSteal)});
    if (ally != kNone && g.rung >= kComplain && g.signs >= 1)
        opts.push_back({kCollude, "去找 " + fullName(ally) + "，把" + shortName(g.against) + "家的底細透給他們",
                        (1.5 + (hasTrait(p, "ambitious") ? 3 : 0) + (hasTrait(p, "greedy") ? 3 : 0) + m / 15) * up(kCollude)});
    // The worst rungs: only after the grievance has climbed (a lawsuit, leaving, theft or the rival) and been seen.
    if (g.rung >= kSue && g.signs >= 2 && m >= param("kin_kill_motive", 45)) {
        double mild = (hasTrait(p, "kind") ? 0.15 : 1) * (hasTrait(p, "pious") ? 0.3 : 1) * (hasTrait(p, "forgiving") ? 0.3 : 1) * (hasTrait(p, "content") ? 0.5 : 1);
        // What a death would open: the head or the heir of a house standing between a kinsman and the holding (and its purse).
        const bool between = (g.against == th.head || g.against == heirOf(th, th.head)) && (same || p.dynasty == t.dynasty);
        const double gain = between ? 1 + 0.6 * std::clamp((th.acres + std::max(0.0, th.money) / 120) / 12, 0.3, 1.8) : 1;
        double opp = (alone ? 1.5 : 1) * (partner ? 1.3 : 1) * (watched ? 0.5 : 1) * gain;
        const std::string risk = "（驗屍官會來驗屍，全村追兇，鄰居陪審；定了罪就是絞刑，家產歸領主）";
        opts.push_back({kPoison, "從藥婆那裡弄藥，給" + shortName(g.against) + "下毒" + risk,
                        (1.4 + (hasTrait(p, "cruel") ? 3 : 0) + (hasTrait(p, "greedy") ? 1.5 : 0) + (p.sex == Sex::Female ? 1.5 : 0) + (m - 45) / 12) * mild * opp * up(kPoison)});
        if (p.sex == Sex::Male && ageYears(p) <= 60)
            opts.push_back({kMurder, "找機會親手殺了" + shortName(g.against) + risk,
                            (0.9 + (hasTrait(p, "wrathful") ? 3 : 0) + (hasTrait(p, "cruel") ? 2 : 0) + (hasTrait(p, "brave") ? 1 : 0) + (m - 45) / 12) * mild *
                                (hasTrait(p, "craven") ? 0.2 : 1) * opp * up(kMurder)});
    }
    std::vector<std::string> texts;
    std::vector<double> w;
    for (const auto& o : opts) texts.push_back(o.text), w.push_back(std::max(0.05, o.w));
    std::string sit = fullName(g.who) + " 心裡對" + kinWord(g.who, g.against) + " " + fullName(g.against) + " 不平：" + kinWhy(g) + "。" +
                      (g.rung > 0 ? "已經" + std::string(g.rung >= kSue ? "鬧到過莊園法庭" : "跟人訴過苦") + "。" : "") +
                      (alone ? fullName(g.against) + "常一個人，" : "") + (partner ? "家裡還有人跟" + std::string(he(p)) + "一樣不平，" : "") +
                      (watched ? "可家裡有人盯著" + std::string(he(p)) + "。" : "");
    int r = npcDecide(p, "心裡的不平", sit, texts, w, {g.against}, 0);
    if (r < 0) r = rngKin_.weighted(w);
    if (r < 0) r = 0;
    int rung = opts[static_cast<size_t>(r)].rung;
    ++counters_["kin_choice:" + std::to_string(rung)];
    if (rung == kNothing) return;
    if (g.chain.empty()) g.chain = "起因：" + kinWhy(g);
    if (rung <= kSue) {
        kinAct(g, rung);
        return;
    }
    // A rung past the court is seen coming: the signs go out now, the act a few months on.
    g.pending = rung;
    g.pendingDay = day_ + rngKin_.range(rung >= kPoison ? 75 : 45, rung >= kPoison ? 150 : 110);
    kinSign(g, rung);
}

void World::kinSign(Grievance& g, int rung) {
    const Person& p = people_[g.who];
    const Person& t = people_[g.against];
    const std::string pn = fullName(g.who), tn = shortName(g.against);
    std::string sign;
    int k = rngKin_.range(0, 1);
    switch (rung) {
        case kLeave: sign = k ? pn + "這陣子悶著不說話，夜裡在收拾東西" : pn + "跟" + tn + "說話越來越衝，說這個家待不下去了"; break;
        case kSteal: sign = k ? pn + "這陣子手頭忽然寬了" : pn + "老在放錢的箱子旁邊轉"; break;
        case kCollude: {
            Id rh = inPlayerHouse(t.household) ? rivalHouse() : kNone;
            std::string who = rh != kNone ? dynasties_[households_[rh].dynasty].surname + "家" : tn + "家的對頭";
            sign = "有人看見" + pn + "跟" + who + "的人在酒館裡喝酒";
            break;
        }
        case kPoison: sign = k ? pn + "常往村邊的藥婆家跑" : pn + "向藥婆打聽，什麼草讓人吃了說不出話"; break;
        case kMurder: sign = k ? pn + "這陣子看" + tn + "的眼神不對，還把那把刀磨了又磨" : pn + "喝了酒就說，" + tn + "早死早好"; break;
        default: sign = pn + "這陣子悶著不說話"; break;
    }
    ++g.signs;
    g.lastSign = day_;
    g.chain += "；" + seasonLabel(day_) + " 徵兆：" + sign;
    ++counters_["kin_sign"];
    addNews("kin:sign", g.who, g.against, p.household, kNone, sign);
    const bool home = inPlayerHouse(t.household) || inPlayerHouse(p.household);
    if (home) {
        log(1, "【苗頭】" + sign + "。");
        if (actingHead() != kNone && actingHead() != g.who) addConcern(actingHead(), "worry", g.who, 30);
    }
    // The target's side may answer it: the player on a card (with the costs), anyone else by the rules.
    // The player's own hand too (L7): the other house watches him as it would anyone.
    kinRespond(g);
}

void World::kinRespond(Grievance& g) {
    Person& p = people_[g.who];
    const Person& t = people_[g.against];
    if (!p.alive() || !t.alive() || t.household == kNone || p.household == kNone) return;
    const int rungWas = g.pending;
    const Household& th = households_[t.household];
    const Id answerer = inPlayerHouse(t.household) ? actingHead() : th.head;
    if (answerer == kNone || answerer == g.who) return;
    const bool player = answerer == actingHead() && inPlayerHouse(t.household);
    const bool same = p.household == t.household;
    // What placating costs: land to set him up when there is land and he is married, else coin.
    Household& ph = households_[t.household];
    // Not the heir (H1): land to set him up would take the holding from him unsaid; the heir is placated with coin.
    const bool heirHim = g.who == planHeir(ph, ph.head, ph.passBy, ph.passHeir);
    const bool landed = same && p.sex == Sex::Male && p.spouse != kNone && ph.acres >= 10 && ph.head != g.who && !heirHim;
    const double coin = std::floor(std::clamp(ph.money * 0.15, 10.0, 40.0));
    const double gift = std::floor(std::min(20.0, std::max(0.0, ph.money)));
    std::vector<std::string> texts = {
        landed ? "安撫：分 3 畝地" + (gift >= 1 ? "和 " + n0(gift) + " 便士" : std::string()) + "讓" + std::string(he(p)) + "自立門戶（你家剩 " + n0(ph.acres - 3) + " 畝）"
               : "安撫：給" + std::string(he(p)) + " " + n0(coin) + " 便士" + (same ? "，算" + std::string(he(p)) + "自己的私房錢" : std::string()),
        "盯著" + std::string(he(p)) + "（" + (player ? "用一段空閒；" : "") + std::string(he(p)) + "要是動手，多半會被撞見）",
        "當面攤牌（" + std::string(he(p)) + "可能就此收手，也可能翻臉）"};
    std::vector<int> kinds = {0, 1, 2};
    bool canSend = same && p.spouse != g.against && answerer != g.who && households_[p.household].head != g.who && ageYears(p) <= 50;
    if (canSend) texts.push_back("打發" + std::string(he(p)) + "出門，去鎮上做工三年（家裡少一個人手，" + std::string(he(p)) + "會記恨）"), kinds.push_back(3);
    if (player) texts.push_back("先下手為強……（同一套規矩：驗屍、追兇、陪審）"), kinds.push_back(4);
    texts.push_back("先不管"), kinds.push_back(5);
    std::vector<std::string> closed;
    if (!landed && coin > ph.money + 0.5) closed.push_back(texts[0] + "（錢不夠）"), texts.erase(texts.begin()), kinds.erase(kinds.begin());
    int r = -1;
    if (player) {
        if (timeLeft() < 1)
            for (size_t i = 0; i < kinds.size(); ++i)
                if (kinds[i] == 1) closed.push_back(texts[i] + "（這一季沒有空了）"), texts.erase(texts.begin() + i), kinds.erase(kinds.begin() + i);
        const size_t signAt = g.chain.rfind("徵兆："), stepAt = g.chain.rfind("；");
        const std::string lately = signAt != std::string::npos ? g.chain.substr(signAt + std::string("徵兆：").size())
                                   : stepAt != std::string::npos ? g.chain.substr(stepAt + std::string("；").size()) : "還沒有";
        std::string sit = fullName(g.who) + "（" + kinWord(answerer, g.who) + "）心裡不平：" + kinWhy(g) + "。最近的風聲：" + lately + "。";
        r = askPlayer("家裡的不平", sit, texts, closed);
    }
    if (r < 0) {
        // The rules: half the time nobody makes anything of a sign; a kind head placates, a suspicious one watches.
        const Person& a = people_[answerer];
        if (!rngKin_.chance(0.5)) return;
        std::vector<double> w;
        for (int kd : kinds)
            w.push_back(kd == 0 ? 4 + (hasTrait(a, "kind") ? 10 : 0) + (hasTrait(a, "generous") ? 8 : 0) - (hasTrait(a, "greedy") ? 3 : 0)
                        : kd == 1 ? 8 + (hasTrait(a, "cynical") ? 10 : 0)
                        : kd == 2 ? 4 + (hasTrait(a, "wrathful") ? 10 : 0) + (hasTrait(a, "brave") ? 5 : 0)
                        : kd == 3 ? 3 + (hasTrait(a, "cruel") ? 4 : 0)
                        : kd == 4 ? 0 : 10);
        // The player's own plot: the other house can only keep an eye on him.
        if (g.who == playerHead())
            for (size_t i = 0; i < kinds.size(); ++i)
                if (kinds[i] != 1 && kinds[i] != 5) w[i] = 0;
        r = rngKin_.weighted(w);
        if (r < 0) return;
    }
    const int kind = kinds[static_cast<size_t>(r)];
    const std::string pn = p.firstName;
    std::string done;
    ++counters_["kin_answer:" + std::to_string(kind)];
    if (kind == 0) {
        if (landed) {
            setUpSon(t.household, g.who, 3, 0, gift, "安撫分給" + pn + "的");
            done = "分了 3 畝地" + (gift >= 1 ? "和 " + n0(gift) + " 便士" : std::string()) + "給" + pn + "，讓" + std::string(he(p)) + "自立門戶";
        } else {
            double c = std::min(coin, std::max(0.0, households_[t.household].money));
            households_[t.household].money -= c, book(households_[t.household], "安撫" + pn + "的錢", -c);
            noteGiven(g.who, 0, c, "安撫");
            if (p.household != t.household) households_[p.household].money += c, book(households_[p.household], "人家讓的一份", c);
            else if (c >= 1) {
                Purse& pu = purse_[g.who];
                pu.money += c;
                if (pu.from == kNone) pu.from = t.household;
            }
            done = "給了" + pn + " " + n0(c) + " 便士";
        }
        recordDeed("placated", answerer, g.who);
        g.calmUntil = day_ + 3 * kDaysPerYear;
        g.pending = 0;
        g.motive *= 0.5;
        g.bitter *= 0.3;
        for (auto& pg : passGrudges_)
            if (pg.who == g.who && pg.against == g.against) pg.points *= 0.4;
        // What he was given answers the same complaint against the rest of that house too (the brother who would get
        // the land): not a second card the same day, worded on the shares before he was set up (fun-r12 A6).
        for (auto& o : griefs_)
            if (&o != &g && o.who == g.who && o.against != kNone && people_[o.against].household == t.household) {
                o.calmUntil = std::max(o.calmUntil, g.calmUntil);
                o.pending = 0;
                o.motive *= 0.5;
                o.bitter *= 0.3;
            }
    } else if (kind == 1) {
        if (player) spendTime(1);
        g.watchUntil = day_ + kDaysPerYear;
        done = "暗中盯著" + pn;
    } else if (kind == 2) {
        recordDeed("confronted", answerer, g.who);
        double back = 0.45 + (hasTrait(p, "craven") ? 0.25 : 0) + (hasTrait(p, "kind") ? 0.15 : 0) - (hasTrait(p, "wrathful") ? 0.25 : 0) - (hasTrait(p, "brave") ? 0.15 : 0);
        if (rngKin_.chance(std::clamp(back, 0.1, 0.9))) {
            g.pending = 0;
            g.calmUntil = day_ + kDaysPerYear;
            g.motive *= 0.7;
            g.bitter *= 0.5;
            addOpinion(g.who, answerer, "backed_down", -4, 4);
            done = "當面攤了牌，" + pn + "低下頭，說是自己想岔了";
        } else {
            g.motive += 10;
            g.bitter += 10, g.bitterWhy = "被" + shortName(answerer) + "當面攤牌、數落了一頓";
            if (g.pending) g.pendingDay = std::min(g.pendingDay, day_ + 20);
            done = "當面攤了牌，" + pn + "翻了臉，摔門出去";
        }
    } else if (kind == 3) {
        recordDeed("sent_away", answerer, g.who);
        g.bitter += 8, g.bitterWhy = "被" + shortName(answerer) + "打發出門做工";
        g.pending = 0;
        setStation(g.who, "town_labourer", 3, "被家裡打發到鎮上做工");
        done = "把" + pn + "打發到鎮上去做工三年";
    } else if (kind == 4) {
        log(1, "〔家裡的不平〕" + fullName(answerer) + "動了先下手的念頭，對象是 " + fullName(g.who) + "。（選擇：" + texts[static_cast<size_t>(r)] + "）");
        playerStrike(g.who);
        return;
    } else {
        done = "";
    }
    if (g.pending == 0 && (kind == 0 || kind == 2 || kind == 3) && rungWas >= kPoison && isPlayerFamily(p) && (isPlayerFamily(t) || t.id == playerHead()))
        ++counters_["kin_player:violent_stopped"];
    if (!done.empty()) {
        g.chain += "；" + seasonLabel(day_) + " " + fullName(answerer) + done;
        if (inPlayerHouse(t.household)) log(1, "〔家裡的不平〕" + fullName(answerer) + done + "。" + (player ? "（選擇：" + texts[static_cast<size_t>(r)] + "）" : ""));
    } else if (player) {
        log(1, "〔家裡的不平〕先不管。（選擇：先不管）");
    }
}

// Six neighbours, heads of houses other than the two (grown sons make up the numbers): each by the truth (a guilty man
// leaves traces), what they believe, whom they like, the favours they owe either side, and the name of the accused.
// Who sits on a jury of neighbours: up to six grown men of other houses, heads first (kNone-free, shuffled).
std::vector<Id> World::pickJury(Id accused, Id accuser) {
    std::vector<Id> pool, sons;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id ah = people_[accused].household, uh = accuser != kNone ? people_[accuser].household : kNone;
    for (const auto& h : households_) {
        if (h.extinct || h.members.empty() || h.head == kNone || h.id == lordHouse || h.id == ah || h.id == uh) continue;
        for (Id m : h.members) {
            const Person& j = people_[m];
            if (!j.alive() || j.sex != Sex::Male || ageYears(j) < 21 || ageYears(j) > 70 || j.occupation == "priest") continue;
            (m == h.head ? pool : sons).push_back(m);
        }
    }
    for (size_t i = pool.size(); i > 1; --i) std::swap(pool[i - 1], pool[rngKin_.range(0, static_cast<int>(i) - 1)]);
    for (size_t i = sons.size(); i > 1; --i) std::swap(sons[i - 1], sons[rngKin_.range(0, static_cast<int>(i) - 1)]);
    for (size_t i = 0; pool.size() < 6 && i < sons.size(); ++i) pool.push_back(sons[i]);
    if (pool.size() > 6) pool.resize(6);
    return pool;
}

// How the head voted, for a verdict line when he sat on the jury: 「（你說有罪）」.
std::string World::juryAside() const { return juryHeadSat_ && !juryNote_.empty() ? "（" + juryNote_ + "）" : std::string(); }

int World::neighbourJury(Id accused, Id accuser, double truth, std::vector<Id>& ayes, int& panel, const std::string& what, const std::vector<Id>* drawn) {
    std::vector<Id> pool = drawn ? *drawn : pickJury(accused, accuser);
    panel = static_cast<int>(pool.size());
    juryNote_.clear();
    juryHeadSat_ = false;
    if (panel < 3) {
        panel = 1;
        return rngKin_.chance(0.25 + 0.4 * truth) ? 1 : 0;
    }
    // The family's head votes last when he goes with the room (a court day's 「跟著多數」).
    const Id me = playerHead();
    std::stable_partition(pool.begin(), pool.end(), [&](Id j) { return j != me; });
    // The head sat on it: the verdict is the family's to hear, whoever stood in the dock (fun-r12 A1).
    juryHeadSat_ = me != kNone && std::find(pool.begin(), pool.end(), me) != pool.end();
    int votes = 0, others = 0;
    for (Id j : pool) {
        bool aye;
        if (j == me && juryStance_ >= 0) {
            // A court day with several cases: the head said once how he would sit (World::hearDocket).
            const double toAccuser = accuser != kNone ? opinion(me, accuser) : 0, toAccused = opinion(me, accused);
            aye = juryStance_ == 0 ? truth >= 0.5 : juryStance_ == 1 ? toAccuser > toAccused : votes * 2 > others;
            juryNote_ = std::string("你") + (juryStance_ == 0 ? "照道理" : juryStance_ == 1 ? "站在跟你家親近的一邊" : "跟著多數") + "，說" +
                        (aye ? (accuser != kNone ? shortName(accuser) + "有理" : "有罪") : shortName(accused) + (accuser != kNone ? "有理" : "無罪"));
        } else if (j == me) {
            const bool civil = what.find("為了「") != std::string::npos;  // a suit between kin, not a felony
            int r = civil ? askPlayer("陪審", "你被叫去當陪審：" + what + "。你站哪一邊？", {"說原告有理（" + shortName(accused) + "要輸，他會記你）", "說 " + shortName(accused) + " 有理"})
                          : askPlayer("陪審", "你被叫去當陪審：" + what + "。你說 " + fullName(accused) + " 有罪嗎？", {"說有罪（" + shortName(accused) + "和他家會記你）", "說無罪"});
            aye = r == 0 || (r < 0 && rngKin_.chance(0.2 + 0.45 * truth));
            juryNote_ = std::string("你說") + (civil ? (aye ? "原告有理" : shortName(accused) + "有理") : (aye ? "有罪" : "無罪"));
        } else {
            double pr = 0.2 + 0.45 * truth;
            pr += std::clamp(((accuser != kNone ? opinion(j, accuser) : 0) - opinion(j, accused)) / 200, -0.25, 0.25);
            pr += 0.1 * feudStage(j, accused) - (accuser != kNone ? 0.1 * feudStage(j, accuser) : 0);
            pr -= std::min(0.25, favourOwed(j, accused) / 60);
            if (accuser != kNone) pr += std::min(0.15, favourOwed(j, accuser) / 80);
            pr -= people_[accused].reputation / 250;
            aye = rngKin_.chance(std::clamp(pr, 0.03, 0.95));
            ++others;
        }
        if (aye) ++votes, ayes.push_back(j);
    }
    return votes;
}

// The civil suits of a year are heard together, on one court day (fun-r11 1): when the family's head sits on two or
// more of them he is asked once how he will sit, not once a case.
void World::hearDocket() {
    std::vector<std::pair<Id, Id>> cases;
    cases.swap(docket_);
    if (cases.empty()) return;
    struct Case { Grievance* g; std::vector<Id> pool; };
    std::vector<Case> todo;
    const Id me = playerHead();
    std::vector<std::string> mine;
    for (auto [who, against] : cases)
        for (auto& g : griefs_)
            if (g.open && g.who == who && g.against == against) {
                Case c{&g, pickJury(against, who)};
                if (me != kNone && std::find(c.pool.begin(), c.pool.end(), me) != c.pool.end() && c.pool.size() >= 3)
                    mine.push_back(std::to_string(mine.size() + 1) + ". " + fullName(who) + " 為了「" + g.claim + "」告 " + fullName(against) + "（" + kinWhy(g) + "）");
                todo.push_back(std::move(c));
                break;
            }
    juryStance_ = -1;
    if (mine.size() >= 2) {
        std::string list;
        for (const auto& m : mine) list += (list.empty() ? "" : "；") + m;
        int r = askPlayer("莊園開庭日", "今天莊園開庭，你在陪審席上要聽 " + std::to_string(mine.size()) + " 件官司：" + list + "。你打算怎麼說？",
                          {"照道理一件件說（誰的理由站得住就站誰；被你說輸的被告對你 −8，記 12 年）",
                           "站在跟你家親近的一邊（你幫的那邊對你 +6，記 8 年；被你說輸的被告對你 −8，記 12 年）",
                           "不出頭，跟著多數走（誰也不得罪：沒有人因此記你）"});
        juryStance_ = r >= 0 ? r : 2;
        if (r >= 0) log(1, "〔莊園開庭日〕" + std::to_string(mine.size()) + " 件官司，你坐在陪審席上（選擇：" +
                               std::string(r == 0 ? "照道理一件件說" : r == 1 ? "站在跟你家親近的一邊" : "跟著多數走") + "）");
    }
    for (auto& c : todo) sueHear(*c.g, &c.pool);
    juryStance_ = -1;
}

// A kinsman's suit in the manor court, heard: what it leaves on the grievance. A suit once judged is closed; it is
// heard again only for a new claim or a new reason, and then said to be brought again (fun-r11 1).
void World::sueHear(Grievance& g, const std::vector<Id>* pool) {
    const Person& p = people_[g.who];
    const Person& t = people_[g.against];
    if (!p.alive() || !t.alive() || p.household == kNone || t.household == kNone) return;
    ++g.signs, g.lastSign = day_;
    const bool again = !g.sued.empty();
    g.suedDay = day_;
    const std::string about = again ? "又告了一回，這回說的是：" + kinWhy(g) : kinWhy(g);
    g.sued = suitCause(g);
    bool won = kinCourt(g.who, g.against, g.claim, g.merit, about, pool);
    g.chain += "；" + seasonLabel(day_) + (again ? " 又告上莊園法庭（" : " 告上莊園法庭（") + (won ? "贏了" : "輸了") + "）";
    if (won) g.calmUntil = day_ + 2 * kDaysPerYear, g.motive *= 0.5, g.bitter *= 0.5;
    else g.bitter += 14, g.bitterWhy = "在莊園法庭輸給了" + shortName(g.against) + "，還被罰了錢";
}

// What a suit was about, to tell a new one from the same one again: what the court is asked for. The reasons are
// re-weighed every year and swap places, so they are no key (review L6); the same claim is heard again only after
// five years (kinDecide), and then said to be brought again.
std::string World::suitCause(const Grievance& g) const { return g.claim; }

bool World::kinCourt(Id plaintiff, Id defendant, const std::string& claim, double merit, const std::string& about, const std::vector<Id>* pool) {
    const bool fam = isPlayerFamily(people_[plaintiff]) || isPlayerFamily(people_[defendant]);
    std::vector<Id> ayes;
    int panel = 0;
    const std::string what = fullName(plaintiff) + " 為了「" + claim + "」告 " + fullName(defendant);
    int votes = neighbourJury(defendant, plaintiff, merit, ayes, panel, what, pool);
    const bool won = votes * 3 >= panel * 2;
    recordDeed("court_case", plaintiff, defendant);
    ++counters_["kin_suit"];
    std::set<std::string> houses;
    // The court day's way of sitting (hearDocket), as its card says: going with the room makes no one sore; siding
    // with the nearer house is remembered by that side.
    const Id me = playerHead();
    const bool sat = me != kNone && !juryNote_.empty() && juryStance_ >= 0;
    for (Id j : ayes) {
        if (people_[j].dynasty != kNone) houses.insert(dynasties_[people_[j].dynasty].surname);
        if (sat && j == me && juryStance_ == 2) continue;
        addOpinion(defendant, j, "voted_against_me", -8, 12);
    }
    if (sat && juryStance_ == 1) {
        const bool aye = std::find(ayes.begin(), ayes.end(), me) != ayes.end();
        addOpinion(aye ? plaintiff : defendant, me, "jury_sided_with_me", 6, 8);
    }
    std::string names;
    for (const auto& s : houses) names += (names.empty() ? "" : "、") + s;
    std::string jury = panel >= 3 ? "陪審的 " + std::to_string(panel) + " 個鄰居裡，" + (votes ? std::to_string(votes) + " 個站在原告這邊（" + names + " 家的人）" : std::string("沒有一個站在原告這邊")) : "管家一個人斷了案";
    std::string outcome;
    Household* dh = people_[defendant].household != kNone ? &households_[people_[defendant].household] : nullptr;
    Household* ph = people_[plaintiff].household != kNone ? &households_[people_[plaintiff].household] : nullptr;
    if (won) {
        if (claim == "照養老的約奉養") {
            for (auto& u : upkeeps_)
                if (u.open && u.parent == plaintiff && u.heir == defendant) u.state = 0, u.ordered = day_;
            double f = dh ? payLord(*dh, 12, "不養老人的罰款") : 0;
            addReputation(defendant, -5, "被莊園法庭判了不養老人");
            outcome = "判 " + shortName(defendant) + " 照約奉養，罰 " + n0(f) + " 便士";
        } else if (dh && ph == dh && plaintiff == planHeir(*dh, dh->head, dh->passBy, dh->passHeir)) {
            // The heir under the same roof: setting him up would take the holding from him; he is awarded coin of his own.
            double c = std::floor(std::min(30.0, std::max(0.0, dh->money) * 0.2));
            if (c >= 1) {
                dh->money -= c, book(*dh, "法庭判給" + people_[plaintiff].firstName + "的錢", -c);
                Purse& pu = purse_[plaintiff];
                pu.money += c;
                if (pu.from == kNone) pu.from = dh->id;
            }
            outcome = "判 " + shortName(defendant) + " 給 " + shortName(plaintiff) + " " + n0(c) + " 便士（" + shortName(plaintiff) + "是繼承人，地不分出去）";
        } else if (dh && dh->acres >= 6 && ph) {
            double a = std::min(5.0, std::floor(dh->acres / 4));
            if (ph == dh) {
                setUpSon(dh->id, plaintiff, a, 0, 0, "法庭判給" + people_[plaintiff].firstName + "的");
            } else {
                noteGiven(plaintiff, moveLand(dh->id, ph->id, a, LandPick::CustomaryFirst, "法庭判分的家業"), 0, "法庭判分");
            }
            outcome = "判 " + shortName(defendant) + " 分 " + n0(a) + " 畝地給 " + shortName(plaintiff);
        } else {
            double c = dh ? std::floor(std::min(30.0, std::max(0.0, dh->money) * 0.2)) : 0;
            if (dh && ph && dh != ph && c >= 1) dh->money -= c, book(*dh, "法庭判賠的錢", -c), ph->money += c, book(*ph, "法庭判給的錢", c), noteGiven(plaintiff, 0, c, "法庭判賠");
            outcome = "判 " + shortName(defendant) + " 賠 " + n0(c) + " 便士";
        }
    } else {
        double f = ph ? payLord(*ph, 6, "官司輸了的罰款") : 0;
        addReputation(plaintiff, -3, "跟家裡人打官司輸了");
        outcome = "沒告贏，" + shortName(plaintiff) + "反被罰了 " + n0(f) + " 便士";
    }
    addNews("kin:verdict", plaintiff, defendant, people_[plaintiff].household, kNone, what + "，" + outcome);
    log(fam || juryHeadSat_ ? 1 : 2, "【官司】" + what + "（" + about + "）。" + jury + (juryNote_.empty() ? std::string() : "（" + juryNote_ + "）") + "，" + outcome + "。");
    juryNote_.clear();
    return won;
}

void World::kinAct(Grievance& g, int rung) {
    Person& p = people_[g.who];
    Person& t = people_[g.against];
    if (!p.alive() || !t.alive() || p.household == kNone || t.household == kNone) return;
    const bool fam = isPlayerFamily(p) || isPlayerFamily(t) || inPlayerHouse(t.household);
    const bool inFamily = isPlayerFamily(p) && (isPlayerFamily(t) || t.id == playerHead());
    const int imp = fam ? 1 : 2;
    const std::string pn = fullName(g.who), tn = fullName(g.against);
    const bool watched = g.watchUntil > day_;
    g.rung = std::max(g.rung, rung);
    g.lastStep = day_;
    ++counters_["kin_act:" + std::to_string(rung)];
    if (rung >= kSue && rung <= kCollude) ++counters_[inFamily ? "kin_player:serious" : "kin_village:case"];
    if (rung >= kPoison) ++counters_[inFamily ? "kin_player:violent" : "kin_village:violent"];
    if (rung >= kSue && g.signs == 0 && rung != kSue) ++counters_["kin_unsigned"];  // must stay 0: no act past the court unseen
    std::string because = "（前因：" + kinWhy(g) + "）";
    Household& th = households_[t.household];
    switch (rung) {
        case kComplain: {
            Id d = recordDeed("kin_complaint", g.who, g.against);
            addNews("kin:complain", g.who, g.against, p.household, d, kinWhy(g));
            ++g.signs, g.lastSign = day_;
            g.chain += "；" + seasonLabel(day_) + " 到處訴苦";
            log(imp, "【家裡的不平】" + pn + " 到處跟人說" + shortName(g.against) + "待" + he(p) + "不公：「" + kinWhy(g) + "」。");
            return;
        }
        case kSue: {
            // The year's suits wait for the court day and are heard together (hearDocket); one brought any other
            // time is heard at once.
            if (docketing_) docket_.push_back({g.who, g.against});
            else sueHear(g, nullptr);
            return;
        }
        case kLeave: {
            recordDeed("left_home", g.who, g.against);
            addNews("kin:left", g.who, g.against, p.household);
            g.chain += "；" + seasonLabel(day_) + " 離家";
            g.bitter += 6, g.bitterWhy = "跟" + shortName(g.against) + "鬧翻離了家";
            log(imp, "【離家】" + pn + " 跟" + shortName(g.against) + "鬧翻，離家走了。" + because);
            setStation(g.who, p.sex == Sex::Male ? "town_labourer" : "town_servant", rngKin_.range(2, 4), "跟家裡鬧翻，離家去了鎮上");
            return;
        }
        case kSteal: {
            double take = std::floor(std::clamp(th.money * (0.15 + rngKin_.uniform() * 0.15), 5.0, 80.0));
            take = std::min(take, std::max(0.0, th.money));
            if (take < 1) return;
            Id d = recordDeed("kin_theft", g.who, g.against);
            bool caught = rngKin_.chance(watched ? 0.8 : 0.25);
            th.money -= take, book(th, "被偷的錢", -take);
            Household& mine = households_[p.household];
            if (caught) th.money += take, book(th, "追回被偷的錢", take);  // caught at it: the coin goes back in the box
            else if (mine.id != th.id) mine.money += take, book(mine, "拿來的錢", take);
            else {  // under the same roof: his own hoard (round 10 M2), out with him when he leaves (pursesMonth)
                Purse& pu = purse_[g.who];
                pu.money += take;
                if (pu.from == kNone) pu.from = th.id;
            }
            g.chain += "；" + seasonLabel(day_) + " 偷了 " + n0(take) + " 便士";
            if (caught) {
                addBelief(g.against, d, g.who, 95, kNone);
                if (th.head != kNone && th.head != g.against) addBelief(th.head, d, g.who, 90, g.against);
                addOpinion(g.against, g.who, "caught_stealing", -25, 15);
                addNews("kin:caught", g.who, g.against, t.household, kNone, pn + "偷家裡的錢，被撞個正著");
                addReputation(g.who, -6, "偷家裡的錢被撞見");
                log(imp, "【家醜】" + pn + " 偷拿家裡的 " + n0(take) + " 便士，被" + (watched ? std::string("盯著") + he(p) + "的人" : shortName(g.against)) + "撞個正著，錢追了回來。" + because);
                ++counters_["kin_theft_caught"];
                g.bitter += 10, g.bitterWhy = "偷錢被撞破，在" + shortName(g.against) + "面前抬不起頭";
            } else if (inPlayerHouse(th.id)) {
                log(1, "【失竊】家裡放錢的箱子少了 " + n0(take) + " 便士，不知道是誰拿的。");
            }
            return;
        }
        case kCollude: {
            Id ally = inPlayerHouse(t.household) && rivalHouse() != kNone ? households_[rivalHouse()].head : kNone;
            if (ally == kNone) {
                double worst = 1;
                for (const auto& f : feuds_) {
                    if (f.heat <= 0 || f.stage < 2 || (f.a != t.dynasty && f.b != t.dynasty)) continue;
                    Id other = f.a == t.dynasty ? f.b : f.a;
                    for (const auto& oh : households_)
                        if (!oh.extinct && oh.dynasty == other && oh.head != kNone && f.stage > worst) worst = f.stage, ally = oh.head;
                }
            }
            if (ally == kNone || people_[ally].household == kNone) return;
            Id d = recordDeed("collusion", g.who, g.against);
            oweFavour(ally, g.who, 20, "透了底細");
            addOpinion(g.who, ally, "colluded", 12, 8);
            addOpinion(ally, g.who, "colluded", 12, 8);
            // What the rival gets: a secret of the house if there is one to tell, else a purse to buy up its debts.
            std::string got;
            Id secret = kNone;
            for (const auto& b : beliefs_[g.who])
                if (b.culprit == g.against && b.confidence >= 50 && deeds_[b.deed].actor == g.against) secret = b.deed;
            if (secret != kNone) {
                addBelief(ally, secret, g.against, 85, g.who);
                got = "把" + shortName(g.against) + "的一件秘密告訴了他";
            } else {
                double take = std::floor(std::min(30.0, std::max(0.0, th.money) * 0.12));
                if (take >= 1) {
                    th.money -= take, book(th, "被偷的錢", -take);
                    households_[people_[ally].household].money += take, book(households_[people_[ally].household], "人家送來的錢", take);
                }
                got = take >= 1 ? "偷了家裡 " + n0(take) + " 便士送去，換他答應幫自己" : std::string("把家裡的事一五一十說給他聽，換他答應幫自己");
            }
            g.chain += "；" + seasonLabel(day_) + " 勾結 " + fullName(ally);
            feudHeat(t.dynasty, people_[ally].dynasty, 6, d);
            if (rngKin_.chance(watched ? 0.7 : 0.2)) {
                addBelief(g.against, d, g.who, 90, kNone);
                addOpinion(g.against, g.who, "caught_colluding", -35, 20);
                addNews("kin:caught", g.who, g.against, t.household, kNone, pn + "背著家裡跟" + fullName(ally) + "勾結，被撞破了");
                addReputation(g.who, -8, "勾結外人坑自己家");
                log(imp, "【家醜】" + pn + " 背著家裡去找 " + fullName(ally) + "，" + got + "。事情被撞破了。" + because);
                ++counters_["kin_collusion_caught"];
                g.bitter += 10, g.bitterWhy = "勾結外人的事被" + shortName(g.against) + "撞破";
            } else if (inPlayerHouse(th.id) && got.find("便士") != std::string::npos) {
                log(1, "【失竊】家裡放錢的箱子少了一些錢，不知道是誰拿的。");
            }
            return;
        }
        case kPoison:
        case kMurder: {
            const bool poison = rung == kPoison;
            bool alone = th.members.size() <= 3 || ageYears(t) >= 60 || t.health < 50;
            bool partner = false;
            for (const auto& o : griefs_)
                if (o.open && o.who != g.who && o.against == g.against && o.motive >= 25 && people_[o.who].household == p.household) partner = true;
            double ok = (poison ? 0.5 : 0.55) + (alone ? 0.15 : 0) + (partner ? 0.1 : 0) - (watched ? 0.35 : 0) -
                        (!poison && hasTrait(t, "strong") ? 0.15 : 0);
            Id d = recordDeed(poison ? "poisoning" : "murder", g.who, g.against);
            ++counters_["kin_attempt"];
            if (fam) ++counters_["kin_attempt:player"];
            g.chain += "；" + seasonLabel(day_) + (poison ? " 下毒" : " 動手");
            if (rngKin_.chance(std::clamp(ok, 0.1, 0.85))) {
                const std::string why = "  （" + pn + " 為什麼下手：" + kinWhy(g) + "。一路下來：" + g.chain + "）";
                const std::string out = killing(g.who, g.against, d, poison, watched);
                if (fam && (out == "hanged" || out == "abjured" || out == "acquitted")) log(1, why);
                g.open = false, g.pending = 0;
                return;
            }
            // It failed: the victim lives, and the attempt may come out.
            t.health = std::max(5.0, t.health - (poison ? 30 : 20));
            if (rngKin_.chance(watched ? 0.8 : 0.4)) {
                recordDeed("attempt_exposed", g.who, g.against);
                addNews("kin:attempt", g.who, g.against, t.household);
                for (Id v : livingInVillage())
                    if (ageYears(people_[v]) >= 12) addBelief(v, d, g.who, 85, g.against);
                log(imp, "【謀害】" + pn + (poison ? " 在" + shortName(g.against) + "的湯裡下了藥，" + shortName(g.against) + "吐了一夜，撐過來了。"
                                                     : " 夜裡對" + shortName(g.against) + "動了手，" + shortName(g.against) + "挨了一刀，撿回一條命。") +
                                "事情被撞破了。" + because);
                std::vector<Id> ayes;
                int panel = 0;
                int votes = neighbourJury(g.who, g.against, 1, ayes, panel, pn + " 謀害 " + tn + " 未遂");
                if (votes * 3 >= panel * 2) {
                    people_[g.who].flags.insert("felon");
                    forfeitGoods(g.who);
                    addReputation(households_[p.household].head == g.who ? g.who : households_[p.household].head, -6, "家裡出了謀害人的");
                    addNews("kin:verdict", g.who, g.against, t.household, kNone, pn + "謀害" + tn + "未遂，被判逐出莊園，永遠不准回來");
                    log(juryHeadSat_ ? 1 : imp, "【判決】陪審的 " + std::to_string(panel) + " 個鄰居裡 " + std::to_string(votes) + " 個說有罪" + juryAside() + "：" + pn + " 被逐出莊園，永遠不准回來，名下的東西歸了領主。");
                    setStation(g.who, "abjured", 0, "謀害" + people_[g.against].firstName + "未遂，被逐出莊園");
                    ++counters_["kin_attempt:banished"];
                } else {
                    addReputation(g.who, -8, "謀害人的嫌疑");
                    addNews("kin:verdict", g.who, g.against, t.household, kNone, "陪審的鄰居沒判" + pn + "有罪，可" + tn + "家不信");
                    log(juryHeadSat_ ? 1 : imp, "【判決】陪審的 " + std::to_string(panel) + " 個鄰居裡只有 " + std::to_string(votes) + " 個說有罪" + juryAside() + "，" + pn + " 沒被定罪，可" + shortName(g.against) + "家沒人信。");
                }
                g.open = false, g.pending = 0;
            } else if (fam) {
                log(1, poison ? "【生病】" + tn + " 忽然吐了一夜，好歹撐過來了。" : "【受傷】" + tn + " 夜裡回家時被人從背後打了一棍，沒看清是誰。");
            }
            return;
        }
        default: return;
    }
}

void World::forfeitGoods(Id felon) {
    Person& f = people_[felon];
    if (f.household == kNone) return;
    Household& h = households_[f.household];
    const bool head = h.head == felon;
    // A felon's chattels are the lord's: the whole purse and half the grain of a head, a son's own share of a house.
    double take = head ? std::max(0.0, h.money) : std::floor(std::min(30.0, std::max(0.0, h.money) * 0.15));
    double paid = payLord(h, take, "兇手的家財充公");
    Id lh = lord_ != kNone ? people_[lord_].household : kNone;
    double grain = 0;
    if (head && lh != kNone && lh != h.id) {
        grain = h.food * 0.5;
        h.food -= grain, households_[lh].food += grain;
        bookFood(h, "充公的糧", -grain);
    }
    ++counters_["kin_forfeit"];
    if (inPlayerHouse(h.id) || isPlayerFamily(f))
        log(1, "【沒收】" + fullName(felon) + " 的東西歸了領主：" + n0(paid) + " 便士" + (grain > 0 ? "、半倉的糧（" + n0(grain / std::max(0.5, consumption(h))) + " 天）" : "") + "。");
}

// What a killing sets off: the coroner's inquest, the hue and cry, a jury of neighbours, and one of four ends — hanged,
// fled to sanctuary and the realm abjured, acquitted, or never solved — then the goods forfeit, the feud between the
// houses, the succession worked out again, and the names of both houses.
std::string World::killing(Id felon, Id victim, Id deed, bool poison, bool watched) {
    Person& f = people_[felon];
    Person& v = people_[victim];
    const bool fam = isPlayerFamily(f) || isPlayerFamily(v) || inPlayerHouse(v.household) || inPlayerHouse(f.household);
    const int imp = fam ? 1 : 2;
    const Id vh = v.household;
    const std::string fn = fullName(felon), vn = fullName(victim);
    ++counters_["kin_kill"];
    if (fam) ++counters_["kin_kill:player"];
    // The inquest: a body found stabbed is plain; poison is found about half the time.
    const bool found = !poison || rngKin_.chance(0.5 + (watched ? 0.3 : 0));
    // Who it falls on: caught at it (the hue and cry), seen, or the one everyone heard complaining.
    const Belief* seen = nullptr;
    for (Id x : livingInVillage())
        if (const Belief* b = beliefAbout(x, deed); b && x != felon && b->culprit == felon) seen = b;
    int complaints = 0;
    for (const auto& g : griefs_)
        if (g.who == felon && g.against == victim) complaints = g.signs;
    bool caught = found && rngKin_.chance(0.2 + (watched ? 0.35 : 0) + (seen ? 0.25 : 0));
    bool accused = caught || (found && rngKin_.chance(complaints >= 2 ? 0.6 : 0.25));
    std::string outcome;  // hanged | abjured | acquitted | unsolved
    int votes = 0, panel = 0;
    std::vector<Id> ayes;
    if (!accused) outcome = "unsolved";
    else if (!caught && rngKin_.chance(0.35 + (hasTrait(f, "craven") ? 0.2 : 0))) outcome = "abjured";
    else {
        Id accuser = vh != kNone && households_[vh].head != victim ? households_[vh].head : v.spouse;
        if (accuser == felon) accuser = kNone;
        votes = neighbourJury(felon, accuser, caught ? 1.0 : 0.85, ayes, panel, fn + " 害死 " + vn);
        outcome = votes * 3 >= panel * 2 ? "hanged" : "acquitted";
    }
    ++counters_["kin_kill:" + outcome];
    if (outcome == "hanged" || outcome == "abjured") f.flags.insert("felon");  // takes nothing: the succession goes past him
    // The death itself, and the succession it sets off.
    log(imp, !found ? "【急病】" + vn + " 忽然病倒，當夜就走了。" : "【命案】" + vn + (poison ? " 喝了一碗湯，當夜就不行了。" : " 被人發現倒在路邊，身上有傷。"));
    kill(victim, !found ? "急病" : poison ? "中毒" : "被人害死");
    std::string inquest = !found ? "看不出什麼，說是急病" : poison ? "是被毒死的" : "是被人害死的";
    addNews("kin:inquest", victim, felon, vh != kNone && !households_[vh].extinct ? vh : f.household, kNone, inquest);
    std::string jurors;
    {
        std::set<std::string> houses;
        for (Id j : ayes)
            if (people_[j].dynasty != kNone) houses.insert(dynasties_[people_[j].dynasty].surname);
        for (const auto& s : houses) jurors += (jurors.empty() ? "" : "、") + s;
    }
    std::string lines = "【驗屍】驗屍官來驗屍：" + inquest + "。";
    if (found) lines += caught ? "全村敲鑼追兇，當場拿住了 " + fn + "。" : accused ? "全村敲鑼追兇；大家都記得 " + fn + " 這陣子說過的話。" : "全村敲鑼追兇，什麼也沒找到。";
    log(imp, lines);
    if (outcome == "hanged") {
        log(juryHeadSat_ ? 1 : imp, "【判決】陪審的 " + std::to_string(panel) + " 個鄰居裡 " + std::to_string(votes) + " 個說有罪（" + jurors + " 家的人）" + juryAside() + "：" + fn + " 被判絞刑，家產歸領主。");
        addNews("kin:verdict", felon, victim, f.household, kNone, fn + "害死了" + vn + "，被吊死在十字路口");
        for (Id x : livingInVillage())
            if (ageYears(people_[x]) >= 12) addBelief(x, deed, felon, 95, kNone);
        forfeitGoods(felon);
        Id fh = f.household;
        kill(felon, "被絞死");
        if (fh != kNone && !households_[fh].extinct && households_[fh].head != kNone) addReputation(households_[fh].head, -8, "家裡出了殺人犯");
    } else if (outcome == "abjured") {
        log(imp, "【避難】" + fn + " 逃進教堂避難，四十天後在驗屍官面前發誓永遠離開英格蘭，赤著腳往港口去了。家產歸領主。");
        addNews("kin:verdict", felon, victim, f.household, kNone, fn + "害死了" + vn + "，逃進教堂避難，發誓永遠離開英格蘭");
        for (Id x : livingInVillage())
            if (ageYears(people_[x]) >= 12) addBelief(x, deed, felon, 85, kNone);
        forfeitGoods(felon);
        Id fh = f.household;
        setStation(felon, "abjured", 0, "害死" + v.firstName + "，逃進教堂避難，發誓永遠離開英格蘭");
        if (fh != kNone && !households_[fh].extinct && households_[fh].head != kNone) addReputation(households_[fh].head, -6, "家裡出了殺人犯");
    } else if (outcome == "acquitted") {
        log(juryHeadSat_ ? 1 : imp, "【判決】陪審的 " + std::to_string(panel) + " 個鄰居裡只有 " + std::to_string(votes) + " 個說有罪" + juryAside() + "：" + fn + " 無罪開釋。" +
                     (vh != kNone && !households_[vh].extinct ? "可死者家裡沒人信。" : ""));
        addNews("kin:verdict", felon, victim, f.household, kNone, "陪審的鄰居判" + fn + "沒有害死" + vn + "，可村裡不少人不信");
        addReputation(felon, -6, "人命案的嫌疑沒洗清");
        if (vh != kNone && !households_[vh].extinct)
            for (Id m : households_[vh].members)
                if (m != felon) addBelief(m, deed, felon, 70, kNone);
    } else if (found) {
        log(imp, "【懸案】" + vn + " 是誰害死的，一直沒查出來。");
        addNews("kin:verdict", victim, kNone, vh != kNone && !households_[vh].extinct ? vh : f.household, kNone, vn + "是誰害死的，到現在也沒人知道");
    }
    // The blood between the houses: the felon's and the dead's own (a second wife's people, too).
    auto bornTo = [&](const Person& x) { return x.father != kNone ? people_[x.father].dynasty : x.mother != kNone ? people_[x.mother].dynasty : x.dynasty; };
    Id df = f.dynasty, dv = bornTo(v);
    Id lordDyn = lord_ != kNone ? people_[lord_].dynasty : kNone;
    if (outcome != "unsolved" && df != kNone && dv != kNone && df != dv && df != lordDyn && dv != lordDyn) {
        feudHeat(df, dv, 60, deed);
        if (fam) log(1, "【血仇】" + dynasties_[df].surname + "家和" + dynasties_[dv].surname + "家從此結下了血仇。");
    }
    if (vh != kNone && !households_[vh].extinct && households_[vh].head != kNone && households_[vh].head != felon && outcome != "unsolved")
        addReputation(households_[vh].head, 3, "家裡出了命案，村裡同情");
    return found || outcome != "unsolved" ? outcome : "unseen";
}

// ---- The player's own hand on the ladder (same rules, same risks) ------------------------------------------------

void World::playerStrike(Id target) {
    Id head = playerHead();
    if (head == kNone || target == kNone || !people_[target].alive() || people_[target].household == kNone) return;
    const Person& t = people_[target];
    const Household& th = households_[t.household];
    const std::string tn = shortName(target);
    const std::string risk = "（驗屍、全村追兇、鄰居陪審；定了罪就是絞刑，家產歸領主）";
    std::vector<std::string> texts;
    std::vector<int> rungs;
    if (th.money >= 20 && t.household != playerHousehold_)
        texts.push_back("偷他家的錢（約 " + n0(th.money * 0.2) + " 便士；被撞破就丟臉）"), rungs.push_back(kSteal);
    texts.push_back("從藥婆那裡弄藥，給" + tn + "下毒" + risk), rungs.push_back(kPoison);
    if (people_[head].sex == Sex::Male) texts.push_back("找機會親手殺了" + tn + risk), rungs.push_back(kMurder);
    texts.push_back("算了"), rungs.push_back(kNothing);
    std::string sit = "你想對 " + fullName(target) + " 下手。跟村裡別人一樣：動手前，旁人可能看出苗頭；事後有驗屍官、全村追兇、鄰居陪審，鄰居對你的看法和欠你的人情都算數。";
    int r = askPlayer("暗地裡的手段", sit, texts);
    if (r < 0 || rungs[static_cast<size_t>(r)] == kNothing) {
        if (r >= 0) log(1, "〔暗地裡的手段〕算了。（選擇：算了）");
        return;
    }
    int rung = rungs[static_cast<size_t>(r)];
    Grievance* g = nullptr;
    for (auto& x : griefs_)
        if (x.who == head && x.against == target) g = &x;
    if (!g) {
        griefs_.push_back({});
        g = &griefs_.back();
        g->who = head, g->against = target;
    }
    g->open = true;
    g->motive = std::max(g->motive, 50.0);
    if (g->why.empty()) g->why = "你自己拿的主意";
    g->chain = g->chain.empty() ? "起因：你自己拿的主意" : g->chain;
    g->pending = rung;
    g->pendingDay = day_ + rngKin_.range(30, 75);
    log(1, "〔暗地裡的手段〕" + texts[static_cast<size_t>(r)].substr(0, texts[static_cast<size_t>(r)].find("（")) + "。（選擇：" + texts[static_cast<size_t>(r)] + "）");
    kinSign(*g, rung);  // the other side may see it coming too
}

// ---- The year and the month --------------------------------------------------------------------------------------

void World::yearlyKin() {
    // How the holdings will pass: a head with two sons grown enough to quarrel settles it (once a head).
    for (size_t i = 0; i < households_.size(); ++i) {
        Household& h = households_[i];
        if (h.extinct || h.head == kNone || h.house == "manor" || h.ward != kNone) continue;
        if (h.passBy == "named" && !namedHeirStands(h, h.passHeir)) {
            // The one named is dead, outlawed, gone into the church or married into another house: the naming lapses
            // (the fine stays paid) and the head may settle it again.
            if (inPlayerHouse(h.id)) {
                const Id y = planHeir(h, h.head, "", kNone);
                log(1, "〔身後的家業〕立 " + shortName(h.passHeir) + " 當繼承人的安排落空了（罰金不退）" + (y != kNone ? "，照習俗由 " + shortName(y) + " 接" : std::string()) + "。");
            }
            h.passBy.clear(), h.passYounger.clear(), h.passHeir = kNone, h.passSince = -1;
            people_[h.head].flags.erase("pass_asked");
        }
        if (!people_[h.head].flags.count("pass_asked")) settlePassing(households_[i], false);
        else if (!inPlayerHouse(h.id) && rngKin_.chance(0.08)) settlePassing(households_[i], true);
    }
    for (size_t i = 0; i < households_.size(); ++i)
        if (!households_[i].extinct && households_[i].head != kNone) considerUpkeep(households_[i]);
    for (size_t i = 0; i < upkeeps_.size(); ++i) upkeepYear(upkeeps_[i]);
    for (auto& g : griefs_) {
        g.bitter *= 0.85;
        // A grievance said out loud and never answered sours year on year (the slow build: never straight to the knife).
        if (g.open && g.rung >= kComplain && g.motive >= param("kin_act_motive", 20) && g.calmUntil < day_) {
            g.bitter += 3;
            if (g.bitterWhy.empty()) g.bitterWhy = "這口氣憋了好幾年，沒人理會";
        }
    }
    kinMotives();
    // Each grievance takes its step for the year (by index: an act can add more).
    docketing_ = true;
    for (size_t i = 0; i < griefs_.size(); ++i)
        if (griefs_[i].open) kinDecide(griefs_[i]);
    docketing_ = false;
    hearDocket();
    // The old: drop what is long over.
    griefs_.erase(std::remove_if(griefs_.begin(), griefs_.end(), [&](const Grievance& g) {
                      return !g.pending && (!people_[g.who].alive() || !people_[g.against].alive());
                  }), griefs_.end());
    passGrudges_.erase(std::remove_if(passGrudges_.begin(), passGrudges_.end(), [&](const PassGrudge& g) {
                           return !people_[g.who].alive() || !people_[g.against].alive() || day_ - g.day > 30 * kDaysPerYear;
                       }), passGrudges_.end());
}

void World::monthlyKin() {
    pursesMonth();
    // The keep cut short shows on the old parent; the grain saved shows in the barn.
    for (auto& u : upkeeps_) {
        if (!u.open || u.state == 0) continue;
        Person& par = people_[u.parent];
        if (!par.alive() || par.household != u.house) continue;
        par.health = std::max(5.0, par.health - (u.state == 2 ? 2.5 : 1.2));
        addStress(u.parent, u.state == 2 ? 4 : 2, "兒子不照約奉養");
        double saved = portion(par) * (u.state == 2 ? 20 : 15);
        households_[u.house].food += saved;
        bookFood(households_[u.house], "剋扣老人的口糧", saved);
    }
    // A widow married again takes her third with her.
    for (auto& d : dowers_) {
        if (!d.open) continue;
        const Person& w = people_[d.widow];
        if (w.alive() && w.household != kNone && w.household != d.in && w.spouse != kNone && people_[w.spouse].alive()) dowerMoves(d.widow, w.household);
    }
    // Acts coming due (by index: an act can add grievances).
    for (size_t i = 0; i < griefs_.size(); ++i) {
        Grievance& g = griefs_[i];
        if (!g.pending) continue;
        const Person& p = people_[g.who];
        const Person& t = people_[g.against];
        if (!p.alive() || !t.alive() || p.household == kNone || t.household == kNone || g.calmUntil > day_) {
            g.pending = 0;
            continue;
        }
        // The worst rungs show more than once before they land.
        if (g.pending >= kPoison && g.signs < 3 && day_ < g.pendingDay && day_ - g.lastSign >= 30 && rngKin_.chance(0.35)) kinSign(griefs_[i], griefs_[i].pending);
        if (day_ < griefs_[i].pendingDay || !griefs_[i].pending) continue;
        int rung = griefs_[i].pending;
        griefs_[i].pending = 0;
        kinAct(griefs_[i], rung);
    }
    watchSuccession();
}

// ---- The season's list: what the head can do about it ----------------------------------------------------------

void World::kinAgenda(std::vector<AgendaItem>& items) {
    Id head = playerHead();
    if (head == kNone || head != actingHead() || playerHousehold_ == kNone) return;
    Household& ph = households_[playerHousehold_];
    // Someone at home with a grievance that has shown.
    for (size_t i = 0; i < griefs_.size(); ++i) {
        const Grievance& g = griefs_[i];
        if (!g.open || g.against == g.who || g.who == head || g.signs == 0 || day_ - g.lastSign > kDaysPerYear || g.calmUntil > day_) continue;
        if (!people_[g.who].alive() || people_[g.who].household == kNone || !people_[g.against].alive() || !inPlayerHouse(people_[g.against].household)) continue;
        AgendaItem a;
        a.id = "kin_answer:" + std::to_string(g.who) + ":" + std::to_string(g.against);
        a.title = "處理" + shortName(g.who) + "的不平";
        a.desc = fullName(g.who) + "（" + kinWord(head, g.who) + "）心裡不平：" + kinWhy(g) + "。安撫、盯著、攤牌、打發出門，或者先下手。";
        a.kind = "kin", a.who = g.who, a.rank = 96;
        items.push_back(a);
    }
    std::vector<Id> sons = sonsOf(ph, head);
    if (sons.size() >= 2 && ageYears(people_[sons[0]]) >= 14) {
        AgendaItem a;
        a.id = "kin_pass";
        a.title = ph.passBy.empty() ? "定下身後的家業" : "改定身後的家業";
        a.desc = inheritText() + (ph.passBy.empty() ? "" : "要改，得再上一次莊園法庭（規費 " + n0(param("pass_change_fee", 10)) + " 便士；立繼承人罰金 " + n0(namingFine(ph)) + " 便士）。");
        a.kind = "kin", a.rank = 40;
        items.push_back(a);
    }
    std::vector<Id> foes = strikeTargets();
    if (!foes.empty()) {
        AgendaItem a;
        a.id = "kin_strike";
        a.title = "暗地裡對付人……";
        std::string who;
        for (size_t i = 0; i < foes.size() && i < 3; ++i) who += (i ? "、" : "") + fullName(foes[i]);
        a.desc = "對象：" + who + "。偷、下毒、動手，都跟村裡別人同一套規矩：動手前旁人看得出苗頭，事後有驗屍、追兇、陪審。";
        a.kind = "kin", a.rank = 10;
        items.push_back(a);
    }
}

std::vector<Id> World::strikeTargets() const {
    // Someone the head has real cause to take the ladder against (L11): one whose grievance against him has shown, the
    // rival's head once it has come to bad blood, anyone he has come to hate.
    std::vector<Id> foes;
    const Id head = playerHead();
    if (head == kNone) return foes;
    for (const auto& g : griefs_)
        if (g.open && g.against == head && g.signs > 0 && people_[g.who].alive() && people_[g.who].household != kNone) foes.push_back(g.who);
    if (Id rh = rivalHouse(); rh != kNone && households_[rh].head != kNone) {
        const Id r = households_[rh].head;
        if (opinion(head, r) <= -20 || feudStage(head, r) >= 2) foes.push_back(r);
    }
    for (const auto& q : people_)
        if (q.alive() && q.household != kNone && q.id != head && ageYears(q) >= 16 && opinion(head, q.id) <= -40) foes.push_back(q.id);
    std::sort(foes.begin(), foes.end());
    foes.erase(std::unique(foes.begin(), foes.end()), foes.end());
    return foes;
}

void World::doKinAgenda(const AgendaItem& a) {
    Id head = playerHead();
    if (head == kNone || playerHousehold_ == kNone) return;
    Household& ph = households_[playerHousehold_];
    if (a.id.rfind("kin_answer:", 0) == 0) {
        const size_t c = a.id.find(':', 11);
        if (c == std::string::npos) return;
        const Id who = static_cast<Id>(std::stol(a.id.substr(11, c - 11))), against = static_cast<Id>(std::stol(a.id.substr(c + 1)));
        for (size_t i = 0; i < griefs_.size(); ++i)
            if (griefs_[i].open && griefs_[i].who == who && griefs_[i].against == against) {
                kinRespond(griefs_[i]);
                break;
            }
    } else if (a.id == "kin_pass") {
        settlePassing(ph, !ph.passBy.empty());
    } else if (a.id == "kin_strike") {
        std::vector<Id> foes = strikeTargets();
        if (foes.size() > 4) foes.resize(4);
        std::vector<std::string> texts;
        for (Id f : foes) texts.push_back(fullName(f) + "（" + (kinWord(head, f).empty() ? std::string("你對他的好感 ") + n0(opinion(head, f)) : kinWord(head, f)) + "）");
        texts.push_back("算了");
        int r = askPlayer("暗地裡對付誰", "你要對誰下手？", texts, {}, true);
        if (r >= 0 && r < static_cast<int>(foes.size())) playerStrike(foes[static_cast<size_t>(r)]);
    }
}

}  // namespace sim
