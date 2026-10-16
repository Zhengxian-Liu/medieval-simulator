// The manor: villeinage, the lord's dues on marriage and death, and life-cycle service.
#include <algorithm>
#include <cmath>

#include "simcore/world.h"

namespace sim {

void World::moveTo(Id id, Id to) {
    Person& p = people_[id];
    if (p.household == to) return;
    if (p.household != kNone) {
        auto& m = households_[p.household].members;
        m.erase(std::remove(m.begin(), m.end(), id), m.end());
    }
    p.household = to;
    if (to != kNone) households_[to].members.push_back(id);
}

// Merchet: a villein father pays the lord for leave to marry off his daughter,
// more if she marries off the manor and her children are lost to it.
void World::chargeMerchet(Id woman, bool outsideManor) {
    const Person& w = people_[woman];
    // The lord may be between heirs, with no household to pay into.
    if (w.free || w.household == kNone || lord_ == kNone || people_[lord_].household == kNone) return;
    // A servant's father pays, not her master.
    Household& h = households_[w.home != kNone ? w.home : w.household];
    if (h.id == people_[lord_].household) return;
    double fee = (outsideManor ? 24 : 12) * param("rent_mult", 1);
    double paid = std::min(h.money, fee);
    h.money -= paid;
    households_[people_[lord_].household].money += paid;
    if (h.head != kNone) addOpinion(h.head, lord_, "merchet", -3, 4);
    if (h.id == playerHousehold_ && paid >= 1)
        log(1, "  照莊園的規矩，嫁 " + w.firstName + " 要先向領主繳 " + std::to_string(int(paid)) + " 便士婚姻稅" +
                   (outsideManor ? "（嫁出莊園，加倍）。" : "。"));
}

// Heriot and mortuary: when a villein tenant dies the lord takes the best beast
// and the parish priest the second best.
void World::chargeHeriot(Household& h, Id dead) {
    const Person& d = people_[dead];
    if (d.free || lord_ == kNone || people_[lord_].household == kNone || h.id == people_[lord_].household || h.extinct) return;
    if (h.acres < 1) return;
    double beast = std::clamp(h.money * 0.15 + h.acres * 2, 20.0, 160.0);
    double heriot = std::min(h.money, beast);
    h.money -= heriot;
    households_[people_[lord_].household].money += heriot;
    double mortuary = std::min(h.money, beast * 0.5);
    h.money -= mortuary;
    if (priest_ != kNone && people_[priest_].household != kNone) households_[people_[priest_].household].money += mortuary;
    if (h.head != kNone) {
        addOpinion(h.head, lord_, "heriot", -5, 5);
        if (priest_ != kNone) addOpinion(h.head, priest_, "mortuary", -3, 5);
    }
    double owed = beast * 0.5 - heriot;
    if (owed > 1) addDebt(h, owed, lord_);
    if (h.id != playerHousehold_) return;
    if (owed > 1) {
        log(1, "  領主的管家來收死後稅，家裡已經沒有像樣的牲口可牽，他在帳上記了 " + std::to_string(int(owed)) +
                   " 便士的欠款。");
    } else {
        log(1, "  領主的管家來牽走了家裡最好的一頭牛當死後稅（約 " + std::to_string(int(heriot)) +
                   " 便士），神父也照例收走了第二好的牲口。");
    }
}

void World::setFree(Id id) {
    Person& p = people_[id];
    p.free = true;
    if (p.household == kNone) return;
    for (Id m : households_[p.household].members)
        if (people_[m].dynasty == p.dynasty || m == p.spouse) people_[m].free = true;
    // Children away in service are named in the charter too.
    for (auto& q : people_)
        if (q.alive() && q.home == p.household) q.free = true;
    for (Id c : p.children) people_[c].free = true;
}

void World::sendToService(Id servant, Id master, int years) {
    Person& s = people_[servant];
    if (s.household == kNone || master == kNone || people_[master].household == kNone) return;
    if (households_[s.household].head == servant || s.home != kNone) return;  // a head cannot leave to serve
    s.home = s.household;
    s.serviceEnd = day_ + years * kDaysPerYear;
    moveTo(servant, people_[master].household);
    s.flags.insert("in_service");
}

void World::recallServants(Id household) {
    for (auto& p : people_)
        if (p.alive() && p.home == household && p.household != kNone) {
            p.home = kNone;
            p.serviceEnd = -1;
            p.flags.erase("in_service");
            if (households_[p.household].head == p.id) continue;  // heads their master's house now: stays
            moveTo(p.id, household);
            if (household == playerHousehold_) log(1, "  " + fullName(p.id) + " 從主人家趕回來。");
        }
}

// Once a year: servants learn their master's trade, send wages home, and come back
// when their term ends or they are grown.
void World::yearlyService() {
    for (auto& p : people_) {
        if (!p.alive() || p.home == kNone || p.household == kNone) continue;
        Household& home = households_[p.home];
        if (home.extinct || home.head == kNone) {
            p.home = kNone;
            p.flags.erase("in_service");
            continue;
        }
        const Household& master = households_[p.household];
        const Person* mh = master.head != kNone ? &people_[master.head] : nullptr;
        if (mh) {
            const OccupationDef* od = defs_.occupation(mh->occupation);
            int skill = od ? skillFromKey(od->skill) : Farming;
            if (mh->occupation == "lord") skill = Charm;
            p.skills[skill] += 0.8;
        }
        double wage = 6 * param("wage_mult", 1);
        home.money += wage;
        if (ageYears(p) >= 21 || day_ >= p.serviceEnd || p.spouse != kNone) {
            bool family = p.home == playerHousehold_;
            Id h = p.home;
            p.home = kNone;
            p.serviceEnd = -1;
            p.flags.erase("in_service");
            if (p.spouse != kNone && people_[p.spouse].alive()) continue;  // married: stays where they are
            moveTo(p.id, h);
            if (family) log(1, fullName(p.id) + " 幫傭期滿，回到家裡。");
        }
    }
}

}  // namespace sim
