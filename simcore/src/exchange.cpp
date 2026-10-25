// fun-r9 (design/stitching-fable.md 三): every bushel that changes hands has someone on the other side.
//  - 缺糧 is a question of whom to ask: up to three named houses (a creditor, a house that owes you, one that wants
//    something of yours, the rival), each wanting one plain thing, and the corn dealer last: dearer, owing no one.
//  - In a famine a house with grain to spare is asked for it: lend, sell dear, give, or keep it.
//  - One rival house a generation, the one the family has met most often (contests, feuds, elections); it is the
//    first to come to the grain cards, and it moves once while the family is down.
//  - Michaelmas on one screen: the rent, the fine, what each creditor wants, how the houses lean before the vote and
//    why, the offers made for the family's vote.
// The cards' words are in data/events/exchange.json, rival.json and michaelmas.json ("texts").
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <map>
#include <set>

#include "simcore/world.h"

namespace sim {

namespace {
constexpr double kFarmGate = 0.6;   // as economy.cpp: what the corn dealer pays at the door, of the market price
constexpr double kAcreValue = 64;   // as ask.cpp: what an acre of customary land fetches between neighbours
std::string num(double v) { return std::to_string(static_cast<long>(std::lround(v))); }
std::string join(const std::vector<std::string>& xs, const std::string& sep) {
    std::string s;
    for (const auto& x : xs) s += (s.empty() ? "" : sep) + x;
    return s;
}
// 「也牽動：A、B」 from at most two names (fun-r9 C: names, never numbers).
std::string touches(std::vector<std::string> xs) {
    xs.erase(std::remove(xs.begin(), xs.end(), std::string()), xs.end());
    if (xs.size() > 2) xs.resize(2);
    return xs.empty() ? "不牽動別的" : "也牽動：" + join(xs, "、");
}
}  // namespace

std::string World::cardText(const std::string& key, const std::map<std::string, std::string>& vars) const {
    auto it = defs_.texts.find(key);
    std::string s = it == defs_.texts.end() ? key : it->second;
    for (const auto& [k, v] : vars) {
        const std::string mark = "{" + k + "}";
        for (size_t at = s.find(mark); at != std::string::npos; at = s.find(mark, at + v.size())) s.replace(at, mark.size(), v);
    }
    return s;
}

int World::generation() const {
    return playerDynasty_ == kNone ? 0 : static_cast<int>(dynasties_[playerDynasty_].heads.size());
}

double World::grainFloor(const Household& h) const {
    double fine = 0;
    for (const auto& f : fines_)
        if (f.house == h.id) fine += f.amount;
    return std::round(rentOf(h)) + fine;
}

int World::debtDueYear(const Household& h) const {
    const bool past = month() > 8 || (month() == 8 && dayOfMonth() == 29);  // this year's rent day is behind us
    if (h.debtYears >= 1) return year() + (past ? 1 : 0);                    // overdue: the next one
    return year() + (past ? 1 : 0) - h.debtYears;                            // as debtText: 1 - debtYears Michaelmases ahead
}

int World::loanDueYear(const Household& h, Id lender) const {
    if (h.debt > 0.5 && h.creditor != kNone && lender != kNone && people_[h.creditor].household == people_[lender].household) return debtDueYear(h);
    const bool past = month() > 8 || (month() == 8 && dayOfMonth() == 29);
    return year() + (past ? 1 : 0) + (lender != lord_ && month() >= 2 ? 1 : 0);
}

Id World::ballotHead() const {
    if (playerHousehold_ == kNone) return kNone;
    Id hd = households_[playerHousehold_].head;
    if (hd == kNone || !people_[hd].alive() || people_[hd].occupation == "priest" || ageYears(people_[hd]) < 21) return kNone;
    return hd;
}

void World::favourLine(Id debtor, Id creditor, double weight, const std::string& why) {
    if (debtor == kNone || creditor == kNone || debtor == creditor) return;
    oweFavour(debtor, creditor, weight, why);
    const bool ours = inPlayerHouse(people_[debtor].household), theirs = inPlayerHouse(people_[creditor].household);
    if (ours) log(1, "【人情】你家欠 " + fullName(creditor) + " 一份情：" + why + "。");
    else if (theirs) log(1, "【人情】" + fullName(debtor) + " 欠你家一份情：" + why + "。");
}

void World::payCreditor(Household& h, Id creditor, double amount) {
    amount = std::min(amount, std::floor(std::max(0.0, h.money)));
    if (amount < 1) return;
    const double before = owedAll(h.id);
    if (creditor == kNone || creditor == lord_) {
        double paid = reduceLordDebt(h, amount);
        h.money -= paid;
        book(h, "還莊園的舊帳", -paid);
        if (lord_ != kNone && people_[lord_].household != kNone) households_[people_[lord_].household].money += paid, book(households_[people_[lord_].household], "收欠租", paid);
    } else {
        double& owed = h.debt > 0.5 && h.creditor == creditor ? h.debt : h.heldDebt;
        const bool main = &owed == &h.debt;
        double paid = std::min(owed, amount);
        owed -= paid;
        h.money -= paid;
        book(h, "還債", -paid);
        if (people_[creditor].household != kNone) households_[people_[creditor].household].money += paid, book(households_[people_[creditor].household], "收回借款", paid);
        if (owed <= 0.5) {
            if (main) clearDebt(h);
            else h.heldDebt = 0, h.heldCreditor = kNone;
            if (h.head != kNone) addOpinion(creditor, h.head, "paid_up", 6, 8);
        }
    }
    if (inPlayerHouse(h.id)) log(1, "  記帳：錢 −" + num(before - owedAll(h.id)) + "；欠債 " + num(before) + " → " + num(owedAll(h.id)) + " 便士。");
}

// ---- The rival -----------------------------------------------------------------------------------

void World::noteClash(Id houseA, Id houseB, const std::string& what, double weight) {
    if (houseA == kNone || houseB == kNone || houseA == houseB) return;
    Id a = households_[houseA].dynasty, b = households_[houseB].dynasty;
    if (a == kNone || b == kNone) return;
    clashes_.push_back({day_, a, b, what, houseA, houseB, weight});
    if (clashes_.size() > 600) clashes_.erase(clashes_.begin());
}

Id World::rivalHouse() const {
    if (rival_.house == kNone || rival_.forHead == kNone) return kNone;
    const Household& h = households_[rival_.house];
    if (h.extinct || h.head == kNone || h.members.empty()) return kNone;
    return rival_.house;
}

std::string World::rivalName() const {
    Id r = rivalHouse();
    if (r == kNone) return "";
    return dynasties_[rival_.dynasty].surname + "家（" + fullName(households_[r].head) + "）";
}

// Where the family's house and the rival's house met (not the rest of either blood): newest first.
std::vector<std::string> World::rivalClashes() const {
    std::vector<std::string> out;
    Id r = rivalHouse();
    if (r == kNone || playerHousehold_ == kNone) return out;
    for (auto it = clashes_.rbegin(); it != clashes_.rend() && out.size() < 6; ++it)
        if ((inPlayerHouse(it->ha) && it->hb == r) || (inPlayerHouse(it->hb) && it->ha == r))
            out.push_back(std::to_string(cfg_.startYear + it->day / kDaysPerYear) + " 年 " + clashWords(*it));
    return out;
}

// A clash as the family reads it: one written before the hand-over named the family by its surname (「莊頭：阿特伍德家選上」,
// read as the rival's win); the family's own win is 「你家選上」.
std::string World::clashWords(const Clash& c) const {
    std::string t = c.what;
    if (playerDynasty_ == kNone) return t;
    const std::string mine = dynasties_[playerDynasty_].surname + "家選上";
    if (const size_t at = t.find(mine); at != std::string::npos && inPlayerHouse(c.hb)) t.replace(at, mine.size(), "你家選上");
    return t;
}

// Close kin are never the rival (fun-r12 A4): the house of the head's parents, brothers and sisters, or children.
bool World::kinHouse(Id house) const {
    const Id head = actingHead();
    if (head == kNone || house == kNone) return false;
    for (Id m : households_[house].members) {
        const std::string t = kinTie(head, m);
        // (and the wife's own father: seed 195, 1348 chose the house of the head's 岳父)
        if (t == "parent" || t == "sibling" || t == "child" || t == "step_parent" || t == "step_child" || t == "spouse" || t == "parent_in_law" ||
            t == "child_in_law")
            return true;
        const Id sp = people_[m].spouse;
        if (m == households_[house].head && sp != kNone && people_[sp].alive()) {
            const std::string ts = kinTie(head, sp);
            if (ts == "parent" || ts == "sibling" || ts == "child") return true;
        }
    }
    // 親家: the house a son's wife came from, while she lives in the family's house (a match is how feuds were ended).
    if (people_[head].household != kNone)
        for (Id m : households_[people_[head].household].members) {
            const Person& p = people_[m];
            if (!p.alive() || p.spouse == kNone || kinTie(head, p.spouse) != "child") continue;
            for (Id par : {p.father, p.mother})
                if (par != kNone && people_[par].alive() && people_[par].household == house) return true;
        }
    return false;
}

// How much this house has clashed with the family's, lately (fun-r12 A4): contests, suits, lost bids and lost matches
// (the clash book), what the head believes they did to the family, the grudge between the heads, and what both are
// after now. The newer, the heavier: a clash of six years ago counts half.
double World::rivalScore(Id house, std::vector<std::string>* why) const {
    const Id head = actingHead();
    if (head == kNone || house == kNone) return 0;
    const Household& h = households_[house];
    if (h.extinct || h.head == kNone || h.members.empty()) return 0;
    auto fresh = [&](int day) { return std::pow(0.5, (day_ - day) / double(6 * kDaysPerYear)); };
    double s = 0;
    for (const auto& c : clashes_) {
        if (day_ - c.day > 15 * kDaysPerYear) continue;
        if (!((inPlayerHouse(c.ha) && c.hb == house) || (inPlayerHouse(c.hb) && c.ha == house))) continue;
        s += 3 * c.weight * fresh(c.day);
        if (why && why->size() < 3) why->push_back(clashWords(c));
    }
    // A wrong the head believes they did the family (a theft, a fire, slander): the deed, not the talk about it.
    std::set<Id> deeds;
    for (const auto& b : head < static_cast<Id>(beliefs_.size()) ? beliefs_[head] : std::vector<Belief>{}) {
        if (b.deed == kNone || b.culprit == kNone || b.confidence < 50 || deeds.count(b.deed)) continue;
        const Deed& d = deeds_[b.deed];
        if (day_ - d.day > 15 * kDaysPerYear || people_[b.culprit].household != house) continue;
        if (d.target == kNone || !inPlayerHouse(people_[d.target].household)) continue;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || std::find(def->tags.begin(), def->tags.end(), "crime") == def->tags.end()) continue;
        deeds.insert(b.deed);
        s += 4 * fresh(d.day);
    }
    if (h.dynasty != kNone && h.dynasty != playerDynasty_) {
        for (const auto& f : feuds_)
            if ((f.a == playerDynasty_ && f.b == h.dynasty) || (f.b == playerDynasty_ && f.a == h.dynasty)) s += f.stage * 3 + f.heat / 25;
    }
    if (opinion(h.head, head) <= -20) s += 1.5;
    if (opinion(head, h.head) <= -20) s += 1.5;
    for (const auto& wt : wants_) {
        if (!wt.open || wt.house != house) continue;
        bool hits = wt.kind == "parcel" && wt.object != kNone && inPlayerHouse(parcels_[wt.object].holder);
        for (const auto& w2 : wants_)
            hits = hits || (w2.open && inPlayerHouse(w2.house) && w2.kind == wt.kind && w2.object == wt.object && w2.office == wt.office);
        if (hits) s += 2;
    }
    return s;
}

std::string World::rivalAfter() const {
    Id r = rivalHouse();
    if (r == kNone) return "";
    std::vector<std::string> xs;
    auto add = [&](const std::string& s) {
        if (!s.empty() && std::find(xs.begin(), xs.end(), s) == xs.end() && xs.size() < 3) xs.push_back(s);
    };
    for (const auto& wt : wants_) {
        if (!wt.open || wt.house != r) continue;
        if (wt.kind == "parcel" && wt.object != kNone && inPlayerHouse(parcels_[wt.object].holder)) add("你家的" + parcelName(parcels_[wt.object]));
        if (wt.kind == "office") {
            const Office* of = office(wt.office);
            bool ours = of && of->holder != kNone && inPlayerHouse(people_[of->holder].household);
            for (const auto& w2 : wants_)
                ours = ours || (w2.open && inPlayerHouse(w2.house) && w2.kind == "office" && w2.office == wt.office);
            if (of && ours) add(of->name + "的位子");
        }
        for (const auto& w2 : wants_)
            if (w2.open && inPlayerHouse(w2.house) && w2.kind == wt.kind && w2.object == wt.object && w2.office == wt.office && wt.kind != "office")
                add(wantThing(wt));
    }
    if (xs.empty()) {
        auto past = rivalClashes();
        if (!past.empty()) return "眼下沒有正面在搶的；上回是 " + past.front();
        return "眼下沒有正面在搶的";
    }
    return join(xs, "、");
}

std::string World::ambitionRace(const Person& head) const {
    const AmbitionDef* a = defs_.ambition(head.ambition);
    if (!a) return "";
    bool contested = !a->office.empty() || (a->goal.isObject() && (a->goal.has("acres") || a->goal.has("freehold") || a->goal.has("top")));
    std::string before = contested && rivalHouse() != kNone ? dynasties_[rival_.dynasty].surname + "家"
                                                            : std::to_string(ambitionDeadline(head)) + " 年底";
    return "在 " + before + "之前，" + ambitionFinal(head);
}

void World::updateRival() {
    if (playerDynasty_ == kNone || playerHousehold_ == kNone) return;
    const Dynasty& our = dynasties_[playerDynasty_];
    Id genHead = our.heads.empty() ? kNone : our.heads.back();
    if (genHead == kNone) return;
    const bool newGen = rival_.forHead != genHead;
    // The rival's house changed hands: the family hears who it has to reckon with now.
    if (!newGen && rivalHouse() != kNone && households_[rival_.house].head != rival_.headOf) {
        const Id was = rival_.headOf, now = households_[rival_.house].head;
        rival_.headOf = now;
        if (day_ >= handedOver_ && now != kNone)
            log(1, "【宿敵】" + dynasties_[rival_.dynasty].surname + "家換了當家的：" + fullName(now) + "（" + std::to_string(int(ageYears(people_[now]))) + " 歲）接下了" +
                       (was != kNone ? fullName(was) + (people_[was].alive() ? "" : "（已過世）") + "的家業" : "家業") + "，兩家的帳也跟著到了" +
                       (people_[now].sex == Sex::Male ? "他" : "她") + "手上（" + people_[now].firstName + " 對你家 " +
                       std::to_string(std::lround(actingHead() != kNone ? opinion(now, actingHead()) : 0)) + "）。");
    }
    // Weighed again at each year's start (and for a new generation, or when the rival is gone or has become kin).
    const bool gone = rivalHouse() == kNone || kinHouse(rival_.house);
    if (!newGen && !gone && !(month() == 0 && dayOfMonth() == 0)) return;
    if (!newGen && rivalHouse() != kNone && kinHouse(rival_.house) && day_ >= handedOver_)
        log(1, "【宿敵】" + dynasties_[rival_.dynasty].surname + "家跟你家結了親，兩家成了親家，不再算宿敵了。");
    if (newGen) {
        Rival fresh;
        fresh.forHead = genHead;
        rival_ = fresh;
    }
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id bestHouse = kNone;
    double top = 3.5;
    for (const auto& h : households_) {
        if (h.extinct || h.head == kNone || h.members.empty() || h.id == lordHouse || inPlayerHouse(h.id) || h.dynasty == kNone) continue;
        if (people_[h.head].occupation == "priest" || kinHouse(h.id)) continue;
        double v = rivalScore(h.id);
        if (v > top) top = v, bestHouse = h.id;
    }
    const double now = gone ? 0 : rivalScore(rival_.house);
    // A standing rival is kept unless another house has clearly clashed more with the family since.
    if (bestHouse == kNone || bestHouse == rival_.house || (!gone && top < now * 1.3 + 2)) {
        if (gone && !newGen && bestHouse == kNone) rival_.house = kNone, rival_.dynasty = kNone;
        return;
    }
    const bool had = !gone && rival_.house != kNone;
    rival_.dynasty = households_[bestHouse].dynasty;
    rival_.house = bestHouse;
    rival_.headOf = households_[bestHouse].head;
    rival_.since = day_;
    if (day_ < handedOver_) return;
    std::vector<std::string> past = rivalClashes();
    if (past.size() > 3) past.resize(3);
    log(1, cardText(had ? "rival.changed" : "rival.chosen", {{"house", rivalName()}, {"clashes", past.empty() ? cardText("rival.no_clash") : join(past, "；")},
                                                             {"after", rivalAfter()}}));
}

// ---- 缺糧：跟誰開口 -----------------------------------------------------------------------------

namespace {
struct GrainOffer {
    Id who = kNone;        // the head of the house giving the grain
    std::string kind;      // favour | loan | vote | service | pledge
    double days = 0;       // of the asking house's bread
    double owed = 0;       // pence back at Michaelmas (loan, pledge)
    Id child = kNone;
    int years = 0;
    std::string office;
    Id man = kNone;
    Id parcel = kNone;
    double score = 0;
    bool rival = false;
};
}  // namespace

int World::grainExchange(Household& h) {
    Id head = actingHead();
    if (head == kNone || h.head == kNone) return 0;
    const double daily = std::max(0.5, consumption(h));
    const double price = param("grain_price", 0.5);
    const int toHarvest = daysToHarvest();
    const int season = harvestYear();
    const double days = foodDays(h);
    const double all = toHarvest + 15;
    const double need = std::max(0.0, all - days);
    if (need < 5) return 0;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id rh = rivalHouse();
    // A child the family could send into another house's service for the grain.
    Id child = kNone;
    for (Id m : h.members) {
        const Person& c = people_[m];
        double age = ageYears(c);
        if (m == h.head || m == head || age < 10 || age > 16 || c.home != kNone || (c.spouse != kNone && people_[c.spouse].alive())) continue;
        if (!c.station.empty() && c.station != "home") continue;
        if (child == kNone || c.birthDay < people_[child].birthDay) child = m;
    }
    // Our smallest customary parcel, for a pledge (a holding of one parcel is not pledged: it is the croft).
    // The parcel he wants only when it is such a parcel too (small, customary, not the last, a quarter of the holding
    // at most).
    auto pledgeable = [&](Id wanted) -> Id {
        Id best = kNone;
        int n = 0;
        auto fits = [&](const Parcel& pc) { return !pc.free && pc.acres <= 4 && pc.acres <= h.acres * 0.25 + 0.01; };
        for (const auto& pc : parcels_)
            if (!pc.gone && pc.holder == h.id) {
                ++n;
                if (fits(pc) && (best == kNone || pc.acres < parcels_[best].acres)) best = pc.id;
            }
        if (n < 2) return kNone;
        if (wanted != kNone && parcels_[wanted].holder == h.id && !parcels_[wanted].gone && fits(parcels_[wanted])) return wanted;
        return best;
    };
    // What the house could pay back at Michaelmas without losing the roof: two years' rent at most, less what it
    // already owes; a loan is never more than sixty days (fun-r9 review M1).
    const double perDay = std::max(0.01, grainLoanOwed(h, 1));
    const double loanDays = std::floor(std::max(0.0, std::max(30.0, 2 * std::round(rentOf(h))) - owedAll(h.id)) / perDay);
    // The family's ballot, and the posts it seeks itself (no vote is sold against its own man).
    const Id ballot = ballotHead();
    auto ownStake = [&](const std::string& officeId) {
        for (const auto& wt : wants_)
            if (wt.open && wt.kind == "office" && wt.office == officeId && inPlayerHouse(wt.house)) return true;
        return false;
    };
    auto dueYearFor = [&](Id lender) { return loanDueYear(h, lender); };
    // An election before the harvest is in: the house's man who stands for it.
    auto standing = [&](const Household& o, std::string& officeId) -> Id {
        auto mid = [](double m) { return m / 2; };
        std::vector<std::string> ids{"aletaster", "hayward"};
        if (reeveDue()) ids.insert(ids.begin(), "reeve");
        for (const auto& id : ids) {
            const Office* of = office(id);
            if (!of) continue;
            Id old = id == "reeve" ? currentReeve() : of->holder;
            std::vector<Id> cands = officeCandidates(id, old, mid);
            if (ownStake(id) || promises_.count({ballot, id}) ||
                std::any_of(cands.begin(), cands.end(), [&](Id c) { return inPlayerHouse(people_[c].household); }))
                continue;
            for (Id c : cands)
                if (people_[c].household == o.id) {
                    officeId = id;
                    return c;
                }
        }
        return kNone;
    };
    std::vector<GrainOffer> offers;
    for (const auto& o : households_) {
        if (o.extinct || o.members.empty() || o.head == kNone || o.id == h.id || o.id == lordHouse) continue;
        const Person& oh = people_[o.head];
        if (!oh.alive() || oh.occupation == "priest" || ageYears(oh) < 18) continue;
        const double spare = o.food / std::max(0.5, consumption(o)) - (toHarvest + 45);
        const double give = std::min({need, 60.0, spare * consumption(o) / daily});
        if (give < std::min(need, 20.0)) continue;
        GrainOffer f;
        f.who = o.head;
        f.days = std::floor(give);
        f.rival = o.id == rh;
        const double op = opinion(o.head, head);
        const bool creditor = (h.debt > 0.5 && people_[h.creditor].household == o.id) || (h.heldDebt > 0.5 && h.heldCreditor != kNone && people_[h.heldCreditor].household == o.id);
        const double owesUs = favourOwed(o.head, head);
        Id wantsParcel = kNone;
        for (const auto& wt : wants_)
            if (wt.open && wt.house == o.id && wt.kind == "parcel" && wt.object != kNone && parcels_[wt.object].holder == h.id) wantsParcel = wt.object;
        const bool intends = oh.intent.target != kNone && inPlayerHouse(people_[oh.intent.target].household);
        if (feudStage(o.head, head) >= 2 && !f.rival) continue;
        if (op < -10 && !f.rival && !creditor && owesUs < 4) continue;
        f.score = op / 2 + (f.rival ? 100 : 0) + (owesUs >= 4 ? 60 + owesUs : 0) + (creditor ? 50 : 0) + (wantsParcel != kNone ? 40 : 0) + (intends ? 30 : 0);
        // What he wants for it: one plain thing.
        std::string officeId;
        Id man = ballot != kNone ? standing(o, officeId) : kNone;
        Id pledge = pledgeable(wantsParcel);
        // Lent at the neighbours' price, the rival too (his terms are harder in kind, not dearer than the corn dealer),
        // and no more than the house can pay back. A pledge goes on the first account, so its day is the card's.
        const double lendDays = std::min(f.days, loanDays);
        const bool canLend = lendDays >= std::min(need, 15.0) && debtRoom(h, o.head);
        const bool firstAccount = h.debt <= 0.5 || (h.creditor != kNone && people_[h.creditor].household == o.id);
        if (owesUs >= 4 && !f.rival) {
            f.kind = "favour";
        } else if (f.rival && child != kNone) {
            f.kind = "service", f.child = child, f.years = f.days >= 45 ? 3 : 2;
        } else if ((creditor || wantsParcel != kNone || f.rival) && pledge != kNone && canLend && firstAccount) {
            f.kind = "pledge", f.parcel = pledge, f.days = lendDays, f.owed = grainLoanOwed(h, lendDays);
        } else if (man != kNone && !inPlayerHouse(people_[man].household)) {
            f.kind = "vote", f.office = officeId, f.man = man;
        } else if (canLend) {
            f.kind = "loan", f.days = lendDays, f.owed = grainLoanOwed(h, lendDays);
        } else if (child != kNone) {
            f.kind = "service", f.child = child, f.years = f.days >= 45 ? 3 : 2;
        } else {
            continue;
        }
        offers.push_back(f);
    }
    std::stable_sort(offers.begin(), offers.end(), [](const GrainOffer& a, const GrainOffer& b) { return a.score > b.score; });
    if (offers.size() > 3) offers.resize(3);

    // The words: 「對方｜他要什麼｜也牽動」, and what it costs is in the line before it is chosen.
    std::vector<std::string> opts, unavailable;
    std::vector<int> tag;  // index into offers; -1 belt, -2 the corn dealer
    for (size_t i = 0; i < offers.size(); ++i) {
        const GrainOffer& f = offers[i];
        const std::string name = fullName(f.who) + (f.rival ? cardText("tag.rival") : "");
        const std::string surname = people_[f.who].dynasty != kNone ? dynasties_[people_[f.who].dynasty].surname + "家" : name;
        std::map<std::string, std::string> v{{"name", name}, {"days", num(f.days)}, {"owed", num(f.owed)}, {"surname", surname},
                                             {"due", std::to_string(dueYearFor(f.who))}};
        std::string wants, also;
        if (f.kind == "favour") {
            int y = cfg_.startYear;
            std::string why;
            for (const auto& fv : favours_)
                if (fv.debtor == f.who && fv.creditor == head) y = cfg_.startYear + fv.day / kDaysPerYear, why = fv.why;
            v["year"] = std::to_string(y);
            v["why"] = why;
            wants = cardText("short.favour", v);
            also = touches({cardText("short.also_favour", v)});
        } else if (f.kind == "loan") {
            wants = cardText("short.loan", v);
            also = touches({cardText("short.also_debt", v)});
        } else if (f.kind == "pledge") {
            v["parcel"] = parcelName(parcels_[f.parcel]);
            wants = cardText("short.pledge", v);
            also = touches({parcels_[f.parcel].place + "那塊地", cardText("short.also_debt", v)});
        } else if (f.kind == "vote") {
            const Office* of = office(f.office);
            v["office"] = of ? of->name : f.office;
            v["man"] = fullName(f.man);
            wants = cardText("short.vote", v);
            also = touches({cardText("short.also_vote", v)});
        } else {
            v["child"] = shortName(f.child);
            v["years"] = std::to_string(f.years);
            wants = cardText("short.service", v);
            also = touches({shortName(f.child), cardText("short.also_house", v)});
        }
        opts.push_back(cardText("short.line", {{"who", cardText("short.lender", v)}, {"wants", wants}, {"also", also}}));
        tag.push_back(static_cast<int>(i));
    }
    // Two meals a day, while there is room on the card (the corn dealer always closes it).
    if (opts.size() < 3) {
        opts.push_back(cardText("short.belt"));
        tag.push_back(-1);
    }
    const double costAll = costToHarvest(h);
    const double floorMoney = grainFloor(h);
    const double cash = std::floor(std::max(0.0, h.money));
    std::map<std::string, std::string> mv{{"cost", num(costAll)}, {"left", num(cash - costAll)}, {"money", num(cash)},
                                          {"part", num(cash / price / daily)}, {"floor", num(floorMoney)}};
    if (cash >= costAll) {
        opts.push_back(cardText(cash - costAll < floorMoney ? "short.merchant_tight" : "short.merchant", mv));
        tag.push_back(-2);
    } else if (cash >= daily * 15 * price) {
        opts.push_back(cardText("short.merchant_part", mv));
        tag.push_back(-2);
    } else {
        unavailable.push_back(cardText("short.merchant_no", mv));
    }
    std::string sit = cardText("short.situation", {{"days", num(std::floor(days))}, {"to_harvest", std::to_string(toHarvest)}, {"short", num(need)},
                                                   {"why", hungerWhy(h.id)}, {"money", num(cash)},
                                                   {"debt", owedAll(h.id) > 0.5 ? "，" + debtText(h.id) : ""}});
    if (offers.empty()) sit += cardText("short.nobody");
    int r = askPlayer(cardText("short.title"), sit, opts, unavailable);
    if (r < 0) return -1;
    ++budgetUsed_[{h.id, "hunger", budgetYear("hunger")}];
    h.grainAsked = season;
    h.grainCard = season;
    // The rest of the harvest year: the house's own rule if it is 買到收成 (its floor kept), else a month at a time.
    h.grainChoice = grainRule_ == "harvest" ? 0 : 1;
    ++counters_["grain_card"];
    const std::string me = fullName(head);
    const int t = tag[r];
    const double before = h.money, grainBefore = h.food;
    auto ledger = [&](const std::string& whom) {  // the grain in the days the card counted (before a child went into service)
        std::vector<std::string> parts;
        if (std::abs(h.money - before) >= 0.5) parts.push_back("錢 " + std::string(h.money > before ? "+" : "−") + num(std::abs(h.money - before)));
        parts.push_back("存糧 " + std::string(h.food >= grainBefore ? "+" : "−") + num(std::abs(h.food - grainBefore) / daily) + " 天");
        log(1, "  記帳：" + join(parts, "、") + "（" + whom + "）。");
    };
    if (t == -2) {
        double b = std::clamp(daily * all - h.food, 0.0, std::max(0.0, h.money) / price);
        h.food += b;
        bookFood(h, "買糧", b);
        h.money = std::max(0.0, h.money - b * price);
        book(h, "買糧", -b * price);
        log(1, "〔" + cardText("short.title") + "〕" + me + " 誰也沒求，去市集跟糧商買了 " + num(b / daily) + " 天的糧（" + num(b * price) + " 便士）。（選擇：糧商）");
        ledger("糧商；不欠誰人情");
        return 1;
    }
    if (t == -1) {
        h.food += daily * 15;
        bookFood(h, "勒緊褲帶省下的", daily * 15);
        for (Id m : h.members) {
            Person& p = people_[m];
            p.health = std::clamp(p.health - (ageYears(p) < 10 ? 4 : 2), 0.0, 100.0);
            p.needs[Hunger] = std::clamp(p.needs[Hunger] - 30, 0.0, 100.0);
        }
        addStress(head, 6, "勒緊褲帶過日子");
        log(1, "〔" + cardText("short.title") + "〕" + me + " 家誰也沒求，勒緊褲帶，一天只吃兩頓。（選擇：勒緊褲帶）");
        ledger("自家省下的");
        return 1;
    }
    const GrainOffer& f = offers[static_cast<size_t>(t)];
    Household& oh = households_[people_[f.who].household];
    const std::string them = fullName(f.who);
    // The child goes first: if the service cannot be (he is wanted elsewhere), there is no grain either.
    if (f.kind == "service" && !sendToService(f.child, f.who, f.years)) {
        log(1, "〔" + cardText("short.title") + "〕" + cardText("short.service_failed", {{"name", them}, {"child", fullName(f.child)}}) + "（選擇：跟" + them + "開口）");
        return 1;
    }
    const double grain = f.days * daily;
    oh.food = std::max(0.0, oh.food - grain);
    bookFood(oh, "借糧給人", -grain);
    h.food += grain;
    bookFood(h, "借來的糧", grain);
    std::string what;
    if (f.kind == "favour") {
        what = cardText("short.done_favour", {{"name", them}, {"days", num(f.days)}});
        callInFavour(f.who, head);
        addOpinion(f.who, head, "helped_them", 4, 5);
    } else if (f.kind == "loan" || f.kind == "pledge") {
        const int due = dueYearFor(f.who);
        addDebt(h, f.owed, f.who);  // paid back at Michaelmas, out of the next harvest (addDebt: a neighbour's, the one after next)
        if (f.kind == "pledge") {
            Pledge pl;
            pl.house = h.id, pl.creditor = f.who, pl.parcel = f.parcel, pl.due = due, pl.credHouse = oh.id, pl.amount = f.owed;
            pledges_.push_back(pl);
        }
        what = cardText(f.kind == "loan" ? "short.done_loan" : "short.done_pledge",
                        {{"name", them}, {"days", num(f.days)}, {"owed", num(f.owed)}, {"due", std::to_string(due)},
                         {"parcel", f.parcel != kNone ? parcelName(parcels_[f.parcel]) : ""}});
        recordDeed("lent_money", f.who, head);
    } else if (f.kind == "vote") {
        promises_[{ballot, f.office}] = f.man;
        const Office* of = office(f.office);
        what = cardText("short.done_vote", {{"name", them}, {"days", num(f.days)}, {"man", fullName(f.man)}, {"office", of ? of->name : f.office}});
    } else {
        people_[f.child].flags.insert("grain_service");
        what = cardText("short.done_service", {{"name", them}, {"days", num(f.days)}, {"child", fullName(f.child)}, {"years", std::to_string(f.years)}});
    }
    log(1, "〔" + cardText("short.title") + "〕" + what + "（選擇：跟" + them + "開口）");
    ledger(them);
    const std::string why = cardText("favour.short_" + f.kind, {{"year", std::to_string(year())}, {"days", num(f.days)}});
    // A loan is a kindness owed on top of the pence; a vote or a child's service pays for the grain: settled, said so.
    if (f.kind == "loan" || f.kind == "pledge") favourLine(head, f.who, f.kind == "loan" ? 8 : 6, why);
    else if (f.kind == "favour") log(1, "【人情】" + them + " 還了欠你家的那份情：" + why + "。");
    else log(1, cardText("favour.settled", {{"name", them}, {"why", why}}));
    if (f.rival) noteClash(h.id, oh.id, cardText("clash.grain", {{"name", them}}));
    return 1;
}

// ---- 荒年：有人上門求糧 --------------------------------------------------------------------------

void World::famineAsk() {
    if (playerHousehold_ == kNone || actingHead() == kNone || month() >= 8) return;
    const int key = year() * 4 + month() / 3;
    if (famineAsked_ == key) return;
    if (famineYear_ != harvestYear()) famineYear_ = harvestYear(), famineAsks_ = 0;
    Household& ph = households_[playerHousehold_];
    Id head = actingHead();
    // fun-r12 A5: a real dearth, by the village's barns (not the family's own harvest): a fifth of the houses short and
    // unable to buy. Without one, only a house that is itself short comes, once a harvest year.
    Id lordHouse0 = lord_ != kNone ? people_[lord_].household : kNone;
    int shortHouses = 0, houses = 0;
    for (const auto& o : households_) {
        if (o.extinct || o.members.empty() || o.head == kNone || o.id == ph.id || o.id == lordHouse0 || gentry(o.head)) continue;
        ++houses;
        if (breadShort(o) >= 20 && o.money < costToHarvest(o)) ++shortHouses;
    }
    // A dearth is a bad year for the village: a wet summer with several houses short, or a third of them short whatever
    // the weather. Three landless houses short as they are every spring are not a famine (seed 195: 「荒年」 every year).
    const bool dearth = shortHouses >= 3 && (lastWeather_ < 0.85 || param("harvest_mult", 1) < 0.85 ? shortHouses * 5 >= houses : shortHouses * 3 >= houses);
    if (famineAsks_ >= (dearth ? 2 : 1)) return;
    const double ourDaily = std::max(0.5, consumption(ph));
    const double spare = foodDays(ph) - (daysToHarvest() + 60);
    if (spare < 20) return;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id rh = rivalHouse();
    Id pick = kNone;
    double best = -1e9;
    for (const auto& o : households_) {
        if (o.extinct || o.members.empty() || o.head == kNone || o.id == ph.id || o.id == lordHouse) continue;
        if (people_[o.head].occupation == "priest" || breadShort(o) < 20 || o.money >= costToHarvest(o)) continue;
        // Not the lord's own kin (their lord feeds them), and the same house at most twice a generation.
        // Not two years running either, and not a house the family was just asked about as kin going hungry (親友挨餓).
        const FamineSeen& fs = famineBy_[{generation(), o.id}];
        // (The two years run across a change of head: a new head is not asked by the house that came last year.)
        int lastCame = fs.n >= 1 ? fs.year : -100000;
        for (const auto& [k, seen] : famineBy_)
            if (k.second == o.id && seen.n >= 1) lastCame = std::max(lastCame, seen.year);
        if (gentry(o.head) || fs.n >= 2 || year() - lastCame < 2) continue;
        if (auto fa = feedAskedUntil_.find(o.id); fa != feedAskedUntil_.end() && fa->second > day_) continue;
        double s = (o.id == rh ? 100 : 0) + houseLink(ph.id, o.id) * 50 + std::abs(opinion(o.head, head)) / 2 +
                   (favourOwed(o.head, head) + favourOwed(head, o.head) > 2 ? 20 : 0);
        for (const auto& wt : wants_)
            if (wt.open && wt.house == o.id && wt.kind == "parcel" && wt.object != kNone && parcels_[wt.object].holder == ph.id) s += 20;
        if (s > best) best = s, pick = o.id;
    }
    if (pick == kNone) return;
    Household& oh = households_[pick];
    const double theirDaily = std::max(0.5, consumption(oh));
    const double days = std::floor(std::min({60.0, std::max(20.0, static_cast<double>(breadShort(oh))), spare * ourDaily / theirDaily}));
    if (days < 15) return;
    famineAsked_ = key;
    ++famineAsks_;
    // Kin who came to the door for grain are not the 親友挨餓 card the same half-year (seed 195, 1350: lent, then fed).
    feedAskedUntil_[pick] = std::max(feedAskedUntil_[pick], day_ + kDaysPerYear / 2);
    const double grain = days * theirDaily;
    const double ourDays = grain / ourDaily;
    const double market = grain * param("grain_price", 0.5);
    const double dear = std::min(std::round(market * 3), std::floor(std::max(0.0, oh.money)));
    const double owed = std::round(grain * param("grain_price", 0.5) * kFarmGate * 1.25);
    Id asker = oh.head;
    for (Id m : oh.members)
        if (m != oh.head && people_[m].sex == Sex::Male && ageYears(people_[m]) >= 12 && ageYears(people_[m]) < 30) asker = m;
    const bool rival = pick == rh;
    const std::string name = fullName(oh.head) + (rival ? cardText("tag.rival") : "");
    const std::string surname = oh.dynasty != kNone ? dynasties_[oh.dynasty].surname + "家" : name;
    std::string wantsOurs, officeNote;
    for (const auto& wt : wants_)
        if (wt.open && wt.house == pick && wt.kind == "parcel" && wt.object != kNone && parcels_[wt.object].holder == ph.id) wantsOurs = parcels_[wt.object].place + "那塊地";  // (也牽動 names it, not its size)
    for (const auto& wt : wants_)
        if (wt.open && wt.house == pick && wt.kind == "office") {
            if (const Office* of = office(wt.office)) officeNote = cardText("famine.also_office", {{"office", of->name}});
        }
    const bool room = debtRoom(oh, head);
    const double times = market > 0 ? std::floor(dear / market * 10 + 0.5) / 10 : 0;
    char timesText[16];
    std::snprintf(timesText, sizeof timesText, times == std::floor(times) ? "%.0f" : "%.1f", times);
    std::map<std::string, std::string> v{{"name", name}, {"surname", surname}, {"asker", asker == oh.head ? fullName(asker) : fullName(oh.head) + "的" + kinWord(oh.head, asker) + shortName(asker)},
                                         {"days", num(days)}, {"our_days", num(ourDays)}, {"owed", num(owed)}, {"dear", num(dear)},
                                         {"times", timesText},
                                         {"due", std::to_string(loanDueYear(oh, head))},
                                         {"have", num(foodDays(ph))}, {"to_harvest", std::to_string(daysToHarvest())}, {"wants", wantsOurs}};
    // A loan the asker's books have no room for would be a gift called a loan: not offered as one (fun-r9 review M2).
    std::vector<std::string> opts, unavailable;
    std::vector<int> tag;
    if (room) {
        opts.push_back(cardText("famine.lend", v) + "｜" + touches({cardText("famine.also_favour", v), officeNote}));
        tag.push_back(0);
    } else {
        unavailable.push_back(cardText("famine.lend_no", v));
    }
    if (dear >= market * 1.5) {
        opts.push_back(cardText("famine.sell", v) + "｜" + touches({cardText("famine.also_name"), cardText("famine.also_opinion", v)}));
        tag.push_back(1);
    } else {
        unavailable.push_back(cardText("famine.sell_no", v));
    }
    opts.push_back(cardText("famine.give", v) + "｜" + touches({cardText("famine.also_name"), cardText("famine.also_bigfavour", v)}));
    tag.push_back(2);
    opts.push_back(cardText("famine.keep", v) + "｜" + touches({cardText("famine.also_opinion", v), wantsOurs.empty() ? "" : wantsOurs}));
    tag.push_back(3);
    // The telling: the village's dearth, or this house alone and why; a second coming says what was done the first time.
    FamineSeen& seen = famineBy_[{generation(), pick}];
    {
        std::string hw = hungerWhy(pick);
        const std::string lead = "缺糧的緣故：";
        if (hw.rfind(lead, 0) == 0) hw = hw.substr(lead.size());
        v["why"] = hw;
    }
    v["short_houses"] = std::to_string(shortHouses);
    v["when"] = dearth ? "荒年" : "斷糧時";  // the favour and the talk: 荒年 only for a real dearth (fun-r12 A5)
    v["last_year"] = std::to_string(seen.year);
    v["last"] = seen.answer;
    const bool again = seen.n >= 1;
    std::string sit = cardText(again ? "famine.situation_again" : dearth ? "famine.situation" : "famine.situation_one", v) + cardText("famine.state", v) +
                      (rival ? cardText("famine.rival", v) : "");
    const std::string ttl = cardText(again ? "famine.title_again" : dearth ? "famine.title" : "famine.title_one", v);
    int r = askPlayer(ttl, sit, opts, unavailable);
    int t = r < 0 ? (opinion(head, oh.head) >= 30 && room ? 0 : 3) : tag[r];
    const std::string me = fullName(head);
    const std::string title = "〔" + ttl + "〕";
    ++seen.n, seen.year = year();
    seen.answer = t == 0 ? "借了糧給他家" : t == 1 ? "高價賣了糧給他家" : t == 2 ? "白送了糧" : "沒有給";
    const double before = ph.money, foodBefore = foodDays(ph);
    auto ledger = [&]() {
        std::vector<std::string> parts;
        if (std::abs(ph.money - before) >= 0.5) parts.push_back("錢 +" + num(ph.money - before));
        if (std::abs(foodDays(ph) - foodBefore) >= 0.5) parts.push_back("存糧 −" + num(foodBefore - foodDays(ph)) + " 天");
        if (!parts.empty()) log(1, "  記帳：" + join(parts, "、") + "（" + fullName(oh.head) + "）。");
    };
    auto hand = [&]() {
        ph.food = std::max(0.0, ph.food - grain);
        oh.food += grain;
        bookFood(oh, "別人給的糧", grain);
    };
    if (t == 0) {
        hand();
        bookFood(ph, "借糧給人", -grain);
        addDebt(oh, owed, head);  // (offered only when his books have room)
        log(1, title + cardText("famine.done_lend", v) + "（選擇：借）");
        ledger();
        favourLine(oh.head, head, 15, cardText("favour.famine_lend", {{"year", std::to_string(year())}, {"days", num(days)}, {"when", v["when"]}}));
        addOpinion(oh.head, head, "helped_me", 10, 10);
    } else if (t == 1) {
        hand();
        bookFood(ph, "高價賣糧", -grain);
        oh.money -= dear;
        ph.money += dear;
        book(ph, dearth ? "荒年高價賣糧" : "高價賣糧", dear);
        book(oh, "高價買糧", -dear);
        log(1, title + cardText("famine.done_sell", v) + "（選擇：高價賣）");
        ledger();
        addReputation(head, -6, cardText("rep.famine_sell", v));
        log(1, "  " + cardText("famine.rep_sell"));
        addOpinion(oh.head, head, "famine_gouge", -15, 12);
        favourLine(oh.head, head, 2, cardText("favour.famine_sell", {{"year", std::to_string(year())}, {"when", v["when"]}}));
    } else if (t == 2) {
        hand();
        bookFood(ph, "白送的糧", -grain);
        log(1, title + cardText("famine.done_give", v) + "（選擇：送）");
        ledger();
        addReputation(head, 6, cardText("rep.famine_give", v));
        log(1, "  " + cardText("famine.rep_give", v));
        addOpinion(oh.head, head, "famine_gift", 25, 15);
        recordDeed("kindness", head, oh.head);
        favourLine(oh.head, head, 30, cardText("favour.famine_give", {{"year", std::to_string(year())}, {"days", num(days)}, {"when", v["when"]}}));
    } else {
        log(1, title + cardText("famine.done_keep", v) + "（選擇：囤著）");
        addOpinion(oh.head, head, "refused_loan", -8, 8);
    }
    if (rival) {
        noteClash(ph.id, oh.id, cardText("clash.famine", {{"name", fullName(oh.head)}, {"when", v["when"]}}), 0.4);  // a plea at the door, no fight
        // The rival's own house at the family's door for bread: a head-on meeting of the generation (fun-r11 6).
        rival_.headOn = day_;
        ++rival_.headOns;
        ++counters_["rival_head_on"];
    }
}

// ---- 宿敵趁你弱時出手 ----------------------------------------------------------------------------

void World::rivalStrike() {
    Id rh = rivalHouse();
    if (rh == kNone || rival_.struck || playerHousehold_ == kNone || day_ < handedOver_ + kDaysPerYear / 2) return;
    Household& ph = households_[playerHousehold_];
    Household& rv = households_[rh];
    Id head = actingHead();
    if (head == kNone) return;
    const EntryFine* fine = nullptr;
    for (const auto& f : fines_)
        if (f.house == ph.id) fine = &f;
    // Only real weakness (fun-r9 review H4): the purse below what the year still needs (bread to the harvest, the rent
    // and the fine), with a fine falling due, a head or grown worker just buried, a famine, or debts beyond the purse.
    const double need = costToHarvest(ph) + grainFloor(ph);
    const bool famine = lastWeather_ < 0.85 || param("harvest_mult", 1) < 0.85;
    // 趁虛而入 (widow_rival) asked within the year is the same blow: not twice after one death.
    auto wr = cooldowns_.find({head, "widow_rival"});
    const bool landAskedLately = wr != cooldowns_.end() && wr->second > day_ + 2 * kDaysPerYear;
    std::string weak;
    if (ph.money < need && !landAskedLately) {
        if (fine && ph.money < fine->amount) weak = cardText("rival.weak_fine");
        else if (day_ - lastDeathDay_ < 90) weak = cardText("rival.weak_death");
        else if (famine && breadShort(ph) > 0) weak = cardText("rival.weak_bread");
        else if (breadShort(ph) > 30 && ph.money < costToHarvest(ph)) weak = cardText("rival.weak_bread");
        else if (owedAll(ph.id) >= 40 && ph.money < owedAll(ph.id)) weak = cardText("rival.weak_debt");
    }
    if (weak.empty() || !rngContest_.chance(0.5)) return;
    // The smallest customary parcel (never the last, a quarter of the holding at most, and the house still able to
    // feed itself without it); the one he has his eye on only when it is such a parcel too.
    const double yearNeed = consumption(ph) * kDaysPerYear;
    auto fits = [&](const Parcel& pc) {
        return !pc.free && pc.acres <= ph.acres * 0.25 + 0.01 && keepsHouse(ph.acres - pc.acres) >= std::min(yearNeed, keepsHouse(ph.acres)) * 0.9;
    };
    Id parcel = kNone;
    int held = 0;
    for (const auto& pc : parcels_)
        if (!pc.gone && pc.holder == ph.id) {
            ++held;
            if (fits(pc) && (parcel == kNone || pc.acres < parcels_[parcel].acres)) parcel = pc.id;
        }
    for (const auto& wt : wants_)
        if (wt.open && wt.house == rh && wt.kind == "parcel" && wt.object != kNone && parcels_[wt.object].holder == ph.id && fits(parcels_[wt.object]))
            parcel = wt.object;
    if (parcel == kNone || held < 2) return;
    cooldowns_[{head, "widow_rival"}] = std::max(cooldowns_[{head, "widow_rival"}], day_ + kDaysPerYear);  // and not 趁虛而入 on top
    rival_.struck = true;
    const Parcel& pc = parcels_[parcel];
    const std::string pname = parcelName(pc);
    const std::string them = fullName(rv.head), surname = dynasties_[rival_.dynasty].surname + "家";
    std::map<std::string, std::string> v{{"name", them}, {"surname", surname}, {"weak", weak}, {"parcel", pname}};
    // The refusal first: the first line of a card is not the one that gives the land away.
    std::vector<std::string> opts;
    int kind;
    double fineTotal = 0;
    for (const auto& f : fines_)
        if (f.house == ph.id) fineTotal += f.amount;
    if (fine && ph.money < fine->amount && rv.money >= fineTotal + 20) {
        kind = 0;
        v["fine"] = num(fineTotal);
        opts = {cardText("rival.lease_no", v) + "｜" + touches({cardText("rival.also_steward"), cardText("rival.also_house", v)}),
                cardText("rival.lease_yes", v) + "｜" + touches({pc.place + "那塊地", cardText("rival.also_house", v)})};
    } else {
        kind = 1;
        const double value = std::round(pc.acres * kAcreValue);
        const double offer = std::round(value * 0.6);
        if (rv.money < offer + 20) {
            rival_.struck = false;
            return;
        }
        v["price"] = num(offer);
        v["value"] = num(value);
        opts = {cardText("rival.sell_no", v) + "｜" + touches({cardText("rival.also_house", v)}),
                cardText("rival.sell_yes", v) + "｜" + touches({pc.place + "那塊地", owedAll(ph.id) > 0.5 ? cardText("rival.also_debt") : ""})};
    }
    std::string sit = cardText(kind == 0 ? "rival.lease_situation" : "rival.sell_situation", v);
    int r = askPlayer(cardText("rival.title", v), sit, opts);
    const bool yes = r == 1;  // the family's ways (r < 0): not to the rival
    const std::string title = "〔" + cardText("rival.title", v) + "〕";
    const double before = ph.money;
    if (kind == 0 && yes) {
        // Every fine of the house is paid, as the card says, and the manor's books show it.
        fines_.erase(std::remove_if(fines_.begin(), fines_.end(), [&](const EntryFine& f) { return f.house == ph.id; }), fines_.end());
        rv.money -= fineTotal;
        book(rv, "替人墊入地費", -fineTotal);
        if (lord_ != kNone && people_[lord_].household != kNone) {
            households_[people_[lord_].household].money += fineTotal;
            book(households_[people_[lord_].household], "入地費", fineTotal);
        }
        moveParcel(parcel, rh, "租給宿敵三年");
        Pledge pl;
        pl.house = ph.id, pl.creditor = rv.head, pl.parcel = parcel, pl.due = year() + 3, pl.lease = true, pl.back = day_ + 3 * kDaysPerYear, pl.credHouse = rh;
        pledges_.push_back(pl);
        log(1, title + cardText("rival.lease_done", v) + "（選擇：答應）");
        log(1, "  記帳：入地費 " + num(fineTotal) + " 由" + surname + "代交；" + pname + "三年歸他種。");
        favourLine(head, rv.head, 6, cardText("favour.rival_lease", {{"year", std::to_string(year())}}));
    } else if (kind == 1 && yes) {
        double price = std::stod(v["price"]);
        rv.money -= price;
        book(rv, "買地", -price);
        ph.money += price;
        book(ph, "賣地給" + surname, price);
        moveParcel(parcel, rh, "賤價買來");
        log(1, title + cardText("rival.sell_done", v) + "（選擇：賣）");
        log(1, "  記帳：錢 +" + num(ph.money - before) + "（" + them + "）；地少了" + pname + "。");
    } else {
        log(1, title + cardText(kind == 0 ? "rival.lease_refused" : "rival.sell_refused", v) + "（選擇：不答應）");
        feudHeat(ph.dynasty, rv.dynasty, 8, kNone);
        addOpinion(rv.head, head, "turned_down_terms", -8, 8);
    }
    noteClash(ph.id, rh, cardText(kind == 0 ? "clash.lease" : "clash.price", v) + (yes ? "：他家得手" : "：你家沒讓"));
}

// ---- 正面撞上：宿敵在地、親事、糧上跟你家搶 (fun-r11 6) ---------------------------------------------------
// The rival does not only wait for the family to be down: now and then, and at least once a generation (from the
// third year of the rivalry he looks for it most months), he goes after the same thing the family is after: the match
// it is making (with a matchmaker of his own, a race), a field a neighbour has to sell, or a neighbour's spare grain
// in a year the family is short. A rival come begging grain in a famine (famineCall) counts as one too.

void World::rivalHeadOn() {
    const Id rh = rivalHouse();
    if (rh == kNone || playerHousehold_ == kNone || day_ < handedOver_ + kDaysPerYear) return;
    if (rival_.headOn >= 0 && day_ - rival_.headOn < 3 * kDaysPerYear) return;
    const Id head = actingHead();
    if (head == kNone || people_[head].flags.count("plague_shelter") || households_[rh].head == kNone) return;
    const bool owed = rival_.headOns == 0 && day_ - std::max(rival_.since, handedOver_) >= 2 * kDaysPerYear;
    if (!rngContest_.chance(owed ? 0.3 : 0.05)) return;
    if (rivalMatch(rh) || rivalLand(rh, owed) || rivalGrain(rh)) {
        rival_.headOn = day_;
        ++rival_.headOns;
        ++counters_["rival_head_on"];
    }
}

// A match the family is making, and a son or daughter of his of an age for the same match: he sends a matchmaker of
// his own (a plan of his, racing the family's), and the family is asked what it does about it.
bool World::rivalMatch(Id rh) {
    const Id head = actingHead();
    Household& ph = households_[playerHousehold_];
    const std::string surname = dynasties_[rival_.dynasty].surname + "家";
    for (size_t i = 0; i < plans_.size(); ++i) {
        const ActivePlan& ap0 = plans_[i];
        if (ap0.plan != "arrange_match" || ap0.aborted || ap0.subject == kNone || ap0.target == kNone || !inPlayerHouse(people_[ap0.actor].household)) continue;
        const Person& t = people_[ap0.target];
        if (!t.alive() || t.household == kNone || t.household == rh || households_[t.household].head == kNone) continue;
        Id cand = kNone;
        for (Id m : households_[rh].members) {
            const Person& q = people_[m];
            if (q.sex == t.sex || !marriageable(q) || ageYears(q) < 15 || ageYears(q) > 35 || closeKin(m, ap0.target)) continue;
            if (cand == kNone || std::abs(ageYears(q) - ageYears(t)) < std::abs(ageYears(people_[cand]) - ageYears(t))) cand = m;
        }
        if (cand == kNone) continue;
        const Id target = ap0.target, subject = ap0.subject, th = households_[t.household].head;
        const double ours = ap0.progress;
        // His matchmaker starts where the family's has got to: from here it is a race (the first to settle has the match).
        const double his = std::max(ours, 30.0);
        const double gift = std::min(40.0, std::floor(std::max(0.0, ph.money) * 0.2));
        const double op = opinion(th, head);
        const int lift = op >= 20 ? 30 : op >= 0 ? 12 : 0;
        const std::string sit = surname + "也託了媒人去 " + fullName(th) + " 家，替 " + fullName(cand) + " 說 " + fullName(target) + "。誰家先談妥，親事就歸誰：你家替 " +
                                shortName(subject) + " 已經談到 " + num(ours) + " 成（滿 100 成事），他家一上門就談到 " + num(his) + "。（" + fullName(th) + " 對你家 " +
                                std::to_string(int(std::lround(op))) + "）";
        std::vector<std::string> opts;
        std::vector<int> what;
        if (gift >= 10)
            opts.push_back("多添 " + num(gift) + " 便士的禮（錢 " + num(ph.money) + " → " + num(ph.money - gift) + "；你家這邊 +45 成，" + surname + "會記恨）"), what.push_back(0);
        opts.push_back("託人去 " + fullName(th) + " 家說情（不花錢；照他家對你家的交情，你家這邊 +" + std::to_string(lift) + " 成）"), what.push_back(1);
        opts.push_back("讓給" + surname + "（你家的媒人不再上門，這門親由他家去談）"), what.push_back(2);
        int r = askPlayer("宿敵搶親", sit, opts);
        if (r < 0) r = static_cast<int>(opts.size()) - 2;  // the family's ways: ask a friend to speak, spend nothing
        const int w = what[r];
        std::string line = "〔宿敵搶親〕" + sit + "（選擇：" + opts[r].substr(0, opts[r].find("（")) + "）";
        ActivePlan& ap = plans_[i];
        if (w == 0) {
            ph.money -= gift, book(ph, "說親添的禮", -gift);
            ap.progress += 45;
            addOpinion(households_[rh].head, head, "rival_outbid", -8, 8);
            line += "禮送到了，" + fullName(th) + " 家的口氣鬆了：你家這邊談到 " + num(ap.progress) + " 成，他家 " + num(his) + "。";
            noteClash(ph.id, rh, "搶 " + fullName(target) + " 的親事：你家多添了禮");
        } else if (w == 1) {
            ap.progress += lift;
            line += (lift >= 30 ? fullName(th) + " 家念著跟你家的交情，答應再等等你家的媒人" : lift > 0 ? "說了情，" + fullName(th) + " 家只說要再想想"
                                                                                                   : fullName(th) + " 家跟你家不親，說情沒用") +
                    "：你家這邊談到 " + num(ap.progress) + " 成，他家 " + num(his) + "。";
            noteClash(ph.id, rh, "搶 " + fullName(target) + " 的親事：你家託人說情");
        } else {
            ap.aborted = true;
            line += "你家的媒人不再上門了，" + surname + "的媒人接著談。";
            noteClash(ph.id, rh, "搶 " + fullName(target) + " 的親事：讓給了他家");
        }
        log(1, line);
        // His own plan, racing the family's (ap is not used from here: plans_ may grow).
        const Id rhead = households_[rh].head;
        bool started = false;
        for (auto& p : plans_)
            if (p.actor == rhead && p.plan == "arrange_match") started = true;
        if (!started) {
            plans_.push_back({rhead, target, cand, "arrange_match", his, day_, {}});
            ++counters_["plan:arrange_match:start"];
        }
        return true;
    }
    return false;
}

// A neighbour with a field to sell: the rival has bid for it, and the family is asked first whether to stand aside.
bool World::rivalLand(Id rh, bool owed) {
    const Id head = actingHead();
    Household& ph = households_[playerHousehold_];
    Household& rv = households_[rh];
    const std::string surname = dynasties_[rival_.dynasty].surname + "家";
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    std::vector<Id> sellers;
    // (A field that has to go, or, when he is owed his clash, any holding whose purse is near empty.)
    for (const auto& o : households_)
        if (!o.extinct && o.head != kNone && o.id != rh && o.id != lordHouse && !inPlayerHouse(o.id) && o.acres >= (owed ? 6 : 8) &&
            people_[o.head].occupation != "priest" && (!sellReason(o).empty() || (owed && o.money < 60)))
            sellers.push_back(o.id);
    if (sellers.empty()) return false;
    Household& sh = households_[sellers[rngContest_.range(0, static_cast<int>(sellers.size()) - 1)]];
    const double acres = std::min(4.0, std::floor(sh.acres / 4));
    const double value = std::round(acres * kAcreValue), bid = std::round(value * 1.25);
    if (acres < 1 || rv.money < value || ph.money < value + 20) return false;
    const std::string why = sellReason(sh).empty() ? std::string("手頭緊") : sellReason(sh);
    const double op = opinion(sh.head, head);
    const std::string seller = fullName(sh.head);
    const int chance = static_cast<int>(std::lround(std::clamp(0.3 + op / 100, 0.1, 0.9) * 100));
    const std::string sit = seller + " 家" + why + "，要賣 " + num(acres) + " 畝地。" + surname + "搶先出了 " + num(value) + " 便士，" + seller +
                            "還沒點頭。讓給他，這 " + num(acres) + " 畝就歸" + surname + "。（" + seller + " 對你家 " + std::to_string(int(std::lround(op))) + "；你家有 " +
                            num(ph.money) + " 便士、" + num(ph.acres) + " 畝地，" + surname + " " + num(rv.acres) + " 畝。）";
    std::vector<std::string> opts;
    std::vector<int> what;
    if (ph.money >= bid + 20) opts.push_back("出得比他高：" + num(bid) + " 便士買下（錢 " + num(ph.money) + " → " + num(ph.money - bid) + "；" + surname + "會記恨）"), what.push_back(0);
    opts.push_back("去求 " + seller + " 看在交情上照他的價賣給你家（" + num(value) + " 便士；約 " + std::to_string(chance) + "% 肯，不肯就歸" + surname + "）"), what.push_back(1);
    opts.push_back("讓給" + surname + "（地歸他家，你家不花錢）"), what.push_back(2);
    int r = askPlayer("宿敵搶地", sit, opts);
    if (r < 0) r = static_cast<int>(opts.size()) - 1;  // the family's ways: not into a bidding war
    const int w = what[r];
    std::string line = "〔宿敵搶地〕" + sit + "（選擇：" + opts[r].substr(0, opts[r].find("（")) + "）";
    auto sell = [&](Household& buyer, double price, const std::string& who) {
        double got = moveLand(sh.id, buyer.id, acres, LandPick::CustomaryFirst, "買來");
        const double paid = std::round(price * got / acres);  // for the acres that actually changed hands (review L4)
        buyer.money -= paid, book(buyer, "買地", -paid);
        sh.money += paid, book(sh, "賣地", paid);
        return who + "用 " + num(paid) + " 便士買下了 " + num(got) + " 畝。";
    };
    bool ours = false;
    if (w == 0) {
        line += sell(ph, bid, "你家");
        ours = true;
        addOpinion(rv.head, head, "rival_outbid", -8, 8);
    } else if (w == 1 && rngContest_.chance(chance / 100.0)) {
        line += seller + " 念著跟你家的交情，" + sell(ph, value, "你家");
        ours = true;
        addOpinion(rv.head, head, "rival_outbid", -5, 6);
    } else {
        line += (w == 1 ? seller + " 搖了頭：「" + surname + "的錢先到。」" : std::string()) + sell(rv, value, surname);
    }
    log(1, line);
    noteClash(ph.id, rh, "搶買 " + seller + " 家的 " + num(acres) + " 畝：" + (ours ? "你家得手" : "他家得手"));
    return true;
}

// A short year for the family, and a neighbour with grain to spare: the rival has offered for all of it.
bool World::rivalGrain(Id rh) {
    const Id head = actingHead();
    Household& ph = households_[playerHousehold_];
    Household& rv = households_[rh];
    const std::string surname = dynasties_[rival_.dynasty].surname + "家";
    const double ourDaily = std::max(0.5, consumption(ph));
    if (foodDays(ph) >= std::max(150.0, daysToHarvest() + 60.0)) return false;  // the family is not short: no fight over grain
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id best = kNone;
    double most = 0;
    for (const auto& o : households_) {
        if (o.extinct || o.head == kNone || o.id == rh || o.id == lordHouse || inPlayerHouse(o.id) || people_[o.head].occupation == "priest") continue;
        const double spare = o.food - std::max(0.5, consumption(o)) * (daysToHarvest() + 90);
        if (spare > most) most = spare, best = o.id;
    }
    const double grain = std::min(most, ourDaily * 90);
    if (best == kNone || grain < ourDaily * 30) return false;
    Household& sh = households_[best];
    const double price = param("grain_price", 0.5);
    const double value = std::round(grain * price), bid = std::round(value * 1.3);
    if (rv.money < value || ph.money < value + 10) return false;
    const double op = opinion(sh.head, head);
    const int chance = static_cast<int>(std::lround(std::clamp(0.3 + op / 100, 0.1, 0.9) * 100));
    const std::string seller = fullName(sh.head);
    const int days = static_cast<int>(grain / ourDaily);
    const std::string sit = "你家的糧只夠吃 " + num(foodDays(ph)) + " 天，離秋收還有 " + std::to_string(daysToHarvest()) + " 天。" + seller + " 家倉裡有多的麥子（夠你家吃 " +
                            std::to_string(days) + " 天），" + surname + "搶先出了 " + num(value) + " 便士要全買下。讓給他，這批糧就進了" + surname + "的倉。（" + seller +
                            " 對你家 " + std::to_string(int(std::lround(op))) + "；你家有 " + num(ph.money) + " 便士。）";
    std::vector<std::string> opts;
    std::vector<int> what;
    if (ph.money >= bid + 10)
        opts.push_back("出得比他高：" + num(bid) + " 便士買下（錢 " + num(ph.money) + " → " + num(ph.money - bid) + "；存糧 " + num(foodDays(ph)) + " → " +
                       num(foodDays(ph) + days) + " 天；" + surname + "會記恨）"),
            what.push_back(0);
    opts.push_back("去求 " + seller + " 照他的價賣給你家（" + num(value) + " 便士；約 " + std::to_string(chance) + "% 肯，不肯就歸" + surname + "）"), what.push_back(1);
    opts.push_back("讓給" + surname + "（不花錢，存糧照舊 " + num(foodDays(ph)) + " 天，到時候再去市集買）"), what.push_back(2);
    int r = askPlayer("宿敵搶糧", sit, opts);
    if (r < 0) r = static_cast<int>(opts.size()) - 2;  // the family's ways: ask, at the fair price
    const int w = what[r];
    std::string line = "〔宿敵搶糧〕" + sit + "（選擇：" + opts[r].substr(0, opts[r].find("（")) + "）";
    auto sell = [&](Household& buyer, double paid, const std::string& who) {
        sh.food -= grain, bookFood(sh, "賣糧", -grain);
        buyer.food += grain, bookFood(buyer, "買糧", grain);
        buyer.money -= paid, book(buyer, "買糧", -paid);
        sh.money += paid, book(sh, "賣餘糧", paid);
        return who + "用 " + num(paid) + " 便士買下了這批糧。";
    };
    bool ours = false;
    if (w == 0) {
        line += sell(ph, bid, "你家");
        ours = true;
        addOpinion(rv.head, head, "rival_outbid", -8, 8);
    } else if (w == 1 && rngContest_.chance(chance / 100.0)) {
        line += seller + " 念著跟你家的交情，" + sell(ph, value, "你家");
        ours = true;
        addOpinion(rv.head, head, "rival_outbid", -5, 6);
    } else {
        line += (w == 1 ? seller + " 搖了頭：「" + surname + "的錢先到。」" : std::string()) + sell(rv, value, surname);
    }
    if (ours) line += "如今你家的糧夠吃 " + num(foodDays(ph)) + " 天。";
    log(1, line);
    noteClash(ph.id, rh, "搶買 " + seller + " 家的餘糧：" + std::string(ours ? "你家得手" : "他家得手"));
    return true;
}

// ---- 抵押與租約到期 ------------------------------------------------------------------------------

void World::settlePledges() {
    for (size_t i = 0; i < pledges_.size();) {
        Pledge& pl = pledges_[i];
        Parcel& pc = parcels_[pl.parcel];
        Household& h = households_[pl.house];
        // The creditor's house holds the debt and the land after him (his heir takes his accounts: life.cpp).
        Id credHouse = pl.credHouse != kNone ? pl.credHouse : pl.creditor != kNone ? people_[pl.creditor].household : kNone;
        bool drop = pc.gone || h.extinct || credHouse == kNone || households_[credHouse].extinct;
        if (!drop && pl.lease) {
            if (day_ >= pl.back) {
                if (pc.holder == credHouse) moveParcel(pl.parcel, pl.house, "租約到期收回");
                if (inPlayerHouse(pl.house)) log(1, cardText("rival.lease_back", {{"parcel", parcelName(pc)}}));
                drop = true;
            }
        } else if (!drop && month() == 8 && dayOfMonth() == 29 && year() >= pl.due) {
            // fun-r9 review H2: only at the Michaelmas the card named, only once the Michaelmas screen has said the field
            // is at stake, and only for a debt still half unpaid; the field then goes at its ordinary worth, the creditor
            // paying back what it is worth above the debt.
            auto ownedTo = [&](Id c) { return c != kNone && c != lord_ && people_[c].household == credHouse; };
            double owed = 0;
            if (h.debt > 0.5 && ownedTo(h.creditor)) owed += h.debt;
            if (h.heldDebt > 0.5 && ownedTo(h.heldCreditor)) owed += h.heldDebt;
            const Id holder = households_[credHouse].head != kNone ? households_[credHouse].head : pl.creditor;
            if (owed > 0.5 && !pl.warned && h.head != kNone && inPlayerHouse(pl.house)) {
                ++i;  // not said yet: held over to the next Michaelmas
                continue;
            }
            if (owed >= pl.amount * 0.5 && pc.holder == pl.house) {
                const double worth = std::round(pc.acres * kAcreValue);
                const double refund = std::max(0.0, worth - owed);
                moveParcel(pl.parcel, credHouse, "抵押收去");
                if (h.debt > 0.5 && ownedTo(h.creditor)) clearDebt(h);
                if (h.heldDebt > 0.5 && ownedTo(h.heldCreditor)) h.heldDebt = 0, h.heldCreditor = kNone;
                if (refund >= 1) {
                    Household& ch = households_[credHouse];
                    ch.money -= refund;
                    book(ch, "收抵押地補差價", -refund);
                    h.money += refund;
                    book(h, "抵押地補的差價", refund);
                }
                if (inPlayerHouse(pl.house))
                    log(1, cardText("pledge.forfeit", {{"name", fullName(holder)}, {"parcel", parcelName(pc)}, {"owed", num(owed)},
                                                       {"refund", refund >= 1 ? cardText("pledge.refund", {{"refund", num(refund)}}) : ""}}));
                noteClash(pl.house, credHouse, cardText("clash.pledge", {{"parcel", parcelName(pc)}}));
            } else if (inPlayerHouse(pl.house) && pc.holder == pl.house) {
                if (owed > 0.5) log(1, cardText("pledge.half_paid", {{"name", fullName(holder)}, {"parcel", parcelName(pc)}, {"owed", num(owed)}}));
                else log(1, cardText("pledge.redeemed", {{"name", fullName(holder)}, {"parcel", parcelName(pc)}}));
            }
            drop = true;
        }
        if (drop) pledges_.erase(pledges_.begin() + static_cast<long>(i));
        else ++i;
    }
}

// ---- 米迦勒節，一個畫面 ---------------------------------------------------------------------------

std::vector<std::pair<Id, std::string>> World::voterLeans(const std::string& id) {
    std::vector<std::pair<Id, std::string>> out;
    const Office* of = office(id);
    Id head = actingHead();
    if (!of || head == kNone) return out;
    Id old = id == "reeve" ? currentReeve() : of->holder;
    auto mid = [](double m) { return m / 2; };
    std::vector<Id> cands = officeCandidates(id, old, mid);
    if (cands.size() < 2) return out;
    Tally t = countVotes(id, cands, old, mid);
    const Household& ph = households_[playerHousehold_];
    for (const auto& [voter, pick] : t.ballots) {
        if (inPlayerHouse(people_[voter].household)) continue;
        const Id vh = people_[voter].household;
        double weOwe = 0;
        if (ph.debt > 0.5 && ph.creditor != kNone && people_[ph.creditor].household == vh) weOwe += ph.debt;
        if (ph.heldDebt > 0.5 && ph.heldCreditor != kNone && people_[ph.heldCreditor].household == vh) weOwe += ph.heldDebt;
        std::string why;
        const std::string to = inPlayerHouse(people_[pick].household) ? (pick == head ? "你" : shortName(pick)) : fullName(pick);
        if (feudStage(voter, head) >= 2) why = cardText("vote.feud");
        else if (weOwe > 0.5) why = cardText("vote.debt", {{"owed", num(weOwe)}});
        else if (favourOwed(voter, head) >= 4) {
            // A favour owed to the family is a reason for the family's man only; one who owes it and still votes the
            // other way says so (fun-r10: 「記得收留了家裡的人」 was shown as a reason to vote for the rival).
            const bool ours = inPlayerHouse(people_[pick].household);
            for (const auto& f : favours_)
                if (f.debtor == voter && f.creditor == head)
                    why = cardText(ours ? "vote.favour" : "vote.favour_but", {{"year", std::to_string(cfg_.startYear + f.day / kDaysPerYear)}, {"why", f.why}});
        } else if (favourOwed(head, voter) >= 4) why = cardText("vote.we_owe");
        else if (auto pr = promises_.find({voter, id}); pr != promises_.end()) why = cardText("vote.promised", {{"man", fullName(pr->second)}});
        if (why.empty()) continue;
        out.push_back({voter, fullName(voter) + "：「" + why + "」→ 投" + (pick == voter ? std::string("他自己") : to)});
    }
    return out;
}

void World::michaelmasScreen(Household& h) {
    Id head = actingHead();
    if (head == kNone || h.head == kNone) return;
    std::vector<std::string> lines;
    std::string why;
    const double rent = std::round(rentOf(h, &why));
    double fine = 0;
    for (const auto& f : fines_)
        if (f.house == h.id) fine += f.amount;
    lines.push_back(cardText("mich.rent", {{"rent", num(rent)}, {"why", why}}) +
                    (fine > 0.5 ? cardText("mich.fine", {{"fine", num(fine)}}) : "") + cardText("mich.purse", {{"money", num(std::floor(std::max(0.0, h.money)))}}));
    // The creditors, each with what comes next.
    std::vector<std::string> creds;
    auto credLine = [&](Id c, double amount, bool first) {
        if (amount <= 0.5) return;
        bool lord = c == kNone || c == lord_;
        // When it falls due, as the books have it (a neighbour's loan after the winter is not due this year).
        const int dueYear = first ? debtDueYear(h) : year() + 1;
        const std::string when = dueYear <= year() ? cardText("mich.when_now") : dueYear == year() + 1 ? cardText("mich.when_next")
                                                                                                        : cardText("mich.when_year", {{"year", std::to_string(dueYear)}});
        std::string next = lord ? (first && h.debtYears >= 2 ? cardText("mich.next_court") : first && h.debtYears >= 1 ? cardText("mich.next_ox") : cardText("mich.next_lord"))
                                : (first && h.debtYears >= 1 ? cardText("mich.next_court") : !first ? cardText("mich.next_held") : cardText("mich.next_due", {{"when", when}}));
        const Id ch = lord || c == kNone ? kNone : people_[c].household;
        for (auto& pl : pledges_)
            if (pl.house == h.id && !pl.lease && ch != kNone && (pl.credHouse == ch || pl.creditor == c)) {
                const std::string pw = pl.due <= year() ? cardText("mich.when_now") : pl.due == year() + 1 ? cardText("mich.when_next")
                                                                                                           : cardText("mich.when_year", {{"year", std::to_string(pl.due)}});
                next = cardText("mich.next_pledge", {{"parcel", parcelName(parcels_[pl.parcel])}, {"when", pw}});
                if (pl.due <= year()) pl.warned = true;  // said on the screen before the reckoning
            }
        creds.push_back((lord ? std::string("莊園") : fullName(c)) + " " + num(amount) + "（" + next + "）");
    };
    credLine(h.creditor, h.debt, true);
    credLine(h.heldCreditor, h.heldDebt, false);
    if (!creds.empty()) lines.push_back(cardText("mich.creditors", {{"list", join(creds, "；")}}));
    // Before the vote: whom the houses that have something to do with the family lean to, and why.
    std::vector<std::string> votes;
    for (const char* id : {"aletaster", "hayward"}) {
        const Office* of = office(id);
        if (!of) continue;
        auto leans = voterLeans(id);
        if (leans.empty()) continue;
        std::vector<std::string> xs;
        for (size_t k = 0; k < leans.size() && k < 4; ++k) xs.push_back(leans[k].second);
        votes.push_back(of->name + "：" + join(xs, "；"));
    }
    if (!votes.empty()) lines.push_back(cardText("mich.votes", {{"list", join(votes, "。")}}));
    // The family's own post this year.
    for (const auto& of : offices_)
        if (of.holder != kNone && people_[of.holder].alive() && inPlayerHouse(people_[of.holder].household))
            lines.push_back(cardText("mich.office." + of.id, {{"name", of.holder == head ? std::string("你") : shortName(of.holder)}, {"office", of.name}}));
    // Offers for the family's vote: a creditor standing (or his house's man), the reeve for the manor's arrears.
    struct Offer { Id from = kNone, man = kNone; std::string office; double forgive = 0; bool manor = false; };
    std::vector<Offer> offers;
    auto mid = [](double m) { return m / 2; };
    // Only a ballot the family really casts (its head, 21 or over), and never against a post it seeks itself.
    const Id ballot = ballotHead();
    for (const char* oid : {"aletaster", "hayward"}) {
        const std::string id = oid;
        const Office* of = office(id);
        if (!of || ballot == kNone) continue;
        Id old = of->holder;
        std::vector<Id> cands = officeCandidates(id, old, mid);
        bool ours = false;
        for (Id c : cands) ours = ours || inPlayerHouse(people_[c].household);
        for (const auto& wt : wants_) ours = ours || (wt.open && wt.kind == "office" && wt.office == id && inPlayerHouse(wt.house));
        if (ours || cands.size() < 2 || promises_.count({ballot, id})) continue;
        for (Id c : cands) {
            const Id ch = people_[c].household;
            Id cred = kNone;
            double owed = 0;
            if (h.debt > 0.5 && h.creditor != kNone && h.creditor != lord_ && people_[h.creditor].household == ch) cred = h.creditor, owed = h.debt;
            else if (h.heldDebt > 0.5 && h.heldCreditor != kNone && h.heldCreditor != lord_ && people_[h.heldCreditor].household == ch) cred = h.heldCreditor, owed = h.heldDebt;
            if (cred != kNone && offers.size() < 2) offers.push_back({cred, c, id, std::min(20.0, std::floor(owed)), false});
        }
        Id reeve = currentReeve();
        if (reeve != kNone && !inPlayerHouse(people_[reeve].household) && owedLord(h) >= 10 && offers.size() < 2) {
            Id pick = kNone;
            // His own house's man if it stands, else the one he likes best.
            for (Id c : cands)
                if (people_[c].household == people_[reeve].household) pick = c;
            if (pick == kNone)
                for (Id c : cands)
                    if (pick == kNone || opinion(reeve, c) > opinion(reeve, pick)) pick = c;
            bool dup = std::any_of(offers.begin(), offers.end(), [](const Offer& o) { return o.manor; });
            if (pick != kNone && !dup) offers.push_back({reeve, pick, id, std::min(20.0, std::floor(owedLord(h))), true});
        }
    }
    std::vector<std::string> opts;
    std::vector<int> tag;
    for (size_t i = 0; i < offers.size(); ++i) {
        const Offer& o = offers[i];
        const Office* of = office(o.office);
        std::map<std::string, std::string> v{{"name", fullName(o.from)}, {"man", o.man == o.from ? std::string("他") : fullName(o.man)},
                                             {"office", of ? of->name : o.office}, {"forgive", num(o.forgive)}};
        opts.push_back(cardText(o.manor ? "mich.offer_reeve" : "mich.offer", v) + "｜" +
                       touches({cardText("mich.also_vote", v), o.manor ? cardText("mich.also_manor") : cardText("mich.also_debt", v)}));
        tag.push_back(static_cast<int>(i));
    }
    // Paying a neighbour off now, when the purse holds the rent and the debt.
    Id payTo = kNone;
    double payAmt = 0;
    if (h.debt > 0.5 && h.creditor != kNone && h.creditor != lord_ && h.money >= rent + h.debt) payTo = h.creditor, payAmt = std::ceil(h.debt);
    else if (h.heldDebt > 0.5 && h.heldCreditor != kNone && h.heldCreditor != lord_ && h.money >= rent + h.heldDebt) payTo = h.heldCreditor, payAmt = std::ceil(h.heldDebt);
    if (payTo != kNone && opts.size() < 3) {
        opts.push_back(cardText("mich.pay", {{"name", fullName(payTo)}, {"amount", num(payAmt)}}) + "｜" + touches({cardText("mich.also_debt", {{"name", fullName(payTo)}})}));
        tag.push_back(-2);
    }
    opts.push_back(cardText(h.money >= rent - 1 ? "mich.go_on" : "mich.go_short", {{"short", num(rent - std::floor(std::max(0.0, h.money)))}}));
    tag.push_back(-1);
    int r = askPlayer(cardText("mich.title", {{"year", std::to_string(year())}}), join(lines, "\n"), opts, {}, true);
    ++counters_["michaelmas_screen"];
    if (r < 0) r = static_cast<int>(opts.size()) - 1;
    const int t = tag[r];
    if (t == -1) return;
    const double before = h.money;
    if (t == -2) {
        Household& ch = households_[people_[payTo].household];
        h.money -= payAmt;
        ch.money += payAmt;
        book(h, "還債", -payAmt);
        book(ch, "收回借款", payAmt);
        if (h.debt > 0.5 && h.creditor == payTo) clearDebt(h);
        else h.heldDebt = 0, h.heldCreditor = kNone;
        addOpinion(payTo, head, "paid_up", 6, 8);
        log(1, "〔" + cardText("mich.title", {{"year", std::to_string(year())}}) + "〕" + fullName(head) + " 把欠 " + fullName(payTo) + " 的 " + num(payAmt) +
                   " 便士一次還清了。（選擇：還債）");
        log(1, "  記帳：錢 −" + num(before - h.money) + "（還 " + fullName(payTo) + "）；欠債 −" + num(payAmt) + "。");
        return;
    }
    const Offer& o = offers[static_cast<size_t>(t)];
    promises_[{ballot, o.office}] = o.man;
    if (o.manor) {
        double off = reduceLordDebt(h, o.forgive);
        log(1, "〔" + cardText("mich.title", {{"year", std::to_string(year())}}) + "〕" + cardText("mich.done_reeve", {{"name", fullName(o.from)}, {"man", fullName(o.man)}, {"forgive", num(off)}}) +
                   "（選擇：答應）");
        log(1, "  記帳：欠莊園 −" + num(off) + "。");
    } else {
        double off = std::min(o.forgive, h.debt > 0.5 && h.creditor == o.from ? h.debt : h.heldDebt);
        if (h.debt > 0.5 && h.creditor == o.from) {
            h.debt -= off;
            if (h.debt <= 0.5) clearDebt(h);
        } else {
            h.heldDebt -= off;
            if (h.heldDebt <= 0.5) h.heldDebt = 0, h.heldCreditor = kNone;
        }
        log(1, "〔" + cardText("mich.title", {{"year", std::to_string(year())}}) + "〕" + cardText("mich.done_offer", {{"name", fullName(o.from)}, {"man", fullName(o.man)}, {"forgive", num(off)}}) +
                   "（選擇：答應）");
        log(1, "  記帳：欠 " + fullName(o.from) + " 的債 −" + num(off) + "。");
    }
    // A trade, settled: the vote for the debt, no favour on top (fun-r9 review M3).
    log(1, cardText("favour.settled", {{"name", fullName(o.from)}, {"why", cardText("favour.vote_deal")}}));
}

}  // namespace sim
