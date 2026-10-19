// Before the family is handed over (design/contested.md 5): the village lives on its own for some years, with
// nobody playing anyone. Land changes hands, matches are made, grudges and debts and favours pile up, so the
// player takes over a family already in the middle of things, instead of being handed a written crisis.
#include <algorithm>

#include "simcore/world.h"

namespace sim {

void World::prerun() {
    const int playYear = cfg_.startYear + cfg_.prerun;
    Id house = playerHousehold_, dynasty = playerDynasty_;
    // Nobody is the player yet: the family's house is one more house of the village, its head an NPC like the
    // others, with an ambition and wants of his own.
    playerHousehold_ = kNone;
    playerDynasty_ = kNone;
    pendingAmbition_ = false;
    if (Id h = households_[house].head; h != kNone && ageYears(people_[h]) >= 16) chooseAmbition(people_[h]);
    std::string occupation = households_[house].head != kNone ? people_[households_[house].head].occupation : "peasant";
    runYears(cfg_.prerun);

    // The family goes on in its own house if the line held, else in a branch of it; failing both, the player
    // takes another house of the same standing (it does not matter to them which name was first written down).
    auto alive = [&](const Household& h) { return !h.extinct && !h.members.empty() && h.head != kNone; };
    Id pick = kNone;
    if (alive(households_[house]) && people_[households_[house].head].dynasty == dynasty) pick = house;
    for (const auto& h : households_)
        if (pick == kNone && alive(h) && h.dynasty == dynasty && people_[h.head].dynasty == dynasty) pick = h.id;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id priestHouse = priest_ != kNone ? people_[priest_].household : kNone;
    for (const auto& h : households_)
        if (pick == kNone && alive(h) && h.id != lordHouse && h.id != priestHouse && people_[h.head].occupation == occupation) pick = h.id;
    for (const auto& h : households_)
        if (pick == kNone && alive(h) && h.id != lordHouse && h.id != priestHouse) pick = h.id;
    if (pick == kNone) {
        // Nobody left to take over: the game is over before it began, but the tools still have a family to name.
        playerHousehold_ = house;
        playerDynasty_ = dynasty;
        handedOver_ = day_;
        log(1, "【開局】" + std::to_string(playYear) + " 年，" + villageName_ + "已經沒有人家可以接手了。");
        gameOver_ = true;
        return;
    }
    handOver(pick, households_[pick].dynasty, playYear);
}

void World::handOver(Id house, Id dynasty, int playYear) {
    playerHousehold_ = house;
    playerDynasty_ = dynasty;
    handedOver_ = day_;
    openBooks();
    Household& h = households_[house];
    Person& head = people_[h.head];
    Dynasty& d = dynasties_[dynasty];
    if (d.heads.empty() || d.heads.back() != h.head) {
        d.heads.push_back(h.head);
        d.headSince.push_back(day_);
        d.headStart.push_back({h.acres, h.money});
    }
    log(1, "【開局】" + std::to_string(playYear) + " 年，" + villageName_ + "。" + fullName(head.id) + "（" +
               std::to_string(int(ageYears(head))) + " 歲" +
               (defs_.occupation(head.occupation) ? "，" + defs_.occupation(head.occupation)->name : std::string()) + "）是一家之主。");

    // What the family carries in from the years before: the dead it still mourns, the houses it has quarrels
    // with, what it owes. Told once, so the player knows what they walked into.
    const int since = day_ - cfg_.prerun * kDaysPerYear;
    for (Id kin : {head.father, head.mother, head.spouse})
        if (kin != kNone && !people_[kin].alive() && people_[kin].deathDay >= since)
            log(1, "【前情】" + std::string(kin == head.spouse ? "" : kin == head.father ? "父親" : "母親") + fullName(kin) + " " +
                       std::to_string(cfg_.startYear + people_[kin].deathDay / kDaysPerYear) + " 年因" + people_[kin].deathCause + "去世。");
    static const char* kStage[] = {"", "有嫌隙", "結了怨", "是世仇"};
    for (auto& f : feuds_) {
        if (f.stage <= 0 || (f.a != dynasty && f.b != dynasty)) continue;
        Id other = f.a == dynasty ? f.b : f.a;
        f.told = true;  // the family's thread from the first day: its end will be told (arcs.cpp)
        f.ourFace = head.id;
        log(1, "【前情】" + d.surname + " 家跟 " + dynasties_[other].surname + " 家" + kStage[std::min(3, f.stage)] +
                   (f.cause != kNone ? "，從" + describeDeed(f.cause) + "起。" : "。"));
    }
    if (h.debt >= 1)
        log(1, "【前情】家裡還欠" + (h.creditor != kNone ? fullName(h.creditor) + " " : std::string()) + std::to_string(int(h.debt)) + " 便士。");

    // What the family heard in the last two years, as if they had been listening all along.
    for (const auto& n : news_) {
        if (day_ - n.day > 2 * kDaysPerYear || !n.heard.count(house)) continue;
        auto ours = [&](Id x) { return x != kNone && (people_[x].household == house || people_[x].dynasty == dynasty); };
        if (n.house == house || ours(n.subject) || ours(n.other)) continue;  // their own doings they lived, not heard
        Heard x;
        x.day = n.day;
        x.news = n.id;
        x.text = newsText(n);
        x.weight = 1;
        heard_.push_back(x);
    }
    // The head who takes over chooses what this generation is for; the wants he had stay the family's.
    head.ambition.clear();
    pendingAmbition_ = true;
    computeRings();
    for (const auto& hh : households_) lastStanding_[hh.id] = standing(hh.id);
}

}  // namespace sim
