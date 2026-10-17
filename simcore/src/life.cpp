// Needs, health, birth, death, marriage, inheritance.
#include <algorithm>
#include <cmath>

#include "simcore/world.h"

namespace sim {

namespace {

double approach(double v, double target, double rate) { return v + (target - v) * rate; }
double clamp100(double v) { return std::clamp(v, 0.0, 100.0); }

bool isFestival(int month, int day) {
    return (month == 11 && day == 24) || (month == 3 && day == 5) || (month == 5 && day == 23) ||
           (month == 8 && day == 28);
}

}  // namespace

void World::dailyNeeds() {
    bool winter = isWinter();
    bool sunday = day_ % 7 == 6;
    bool festival = isFestival(month(), dayOfMonth());
    bool harvestSeason = month() == 7 || month() == 8;

    for (auto& h : households_) {
        if (h.extinct || h.members.empty()) continue;
        double need = consumption(h);
        bool fed = h.food >= need;
        h.food = fed ? h.food - need : 0;
        bool warm = !winter;
        if (winter && h.firewood >= 1) {
            h.firewood -= 1;
            warm = true;
        }
        for (Id m : h.members) {
            Person& p = people_[m];
            auto& n = p.needs;
            n[Hunger] = clamp100(fed ? n[Hunger] + 35 : n[Hunger] - 18);
            if (!winter) n[Warmth] = approach(n[Warmth], 100, 0.3);
            else if (warm) n[Warmth] = approach(n[Warmth], h.house == "stone" || h.house == "manor" ? 95 : 80, 0.3);
            else n[Warmth] = clamp100(n[Warmth] - (h.house == "stone" || h.house == "manor" ? 3 : 7));

            double restTarget = 75 + traitMod(p, "rest") * 10;
            if (harvestSeason && ageYears(p) >= 12) restTarget -= 25;
            n[Rest] = clamp100(approach(n[Rest], restTarget, 0.2));

            n[Social] = clamp100(n[Social] - 0.8);
            n[Spirit] = clamp100(n[Spirit] - 1.2);
            if (sunday) n[Spirit] = clamp100(n[Spirit] + 10 * (1 + traitMod(p, "piety")));
            if (festival) {
                n[Spirit] = clamp100(n[Spirit] + 25);
                n[Social] = clamp100(n[Social] + 25);
            }
        }
    }
}

void World::monthlyHealth() {
    double plague = param("plague_monthly", 0);
    double mortMult = param("mortality_mult", 1);
    bool gentle = cfg_.harshness == Harshness::Gentle;
    int plagueDeaths = 0;
    // Hiding in the woods lasts as long as the pestilence: when it has passed, the family comes back.
    if (plague <= 0)
        for (auto& p : people_)
            if (p.flags.erase("plague_shelter") && p.id == playerHead()) log(1, "瘟疫過去了，" + fullName(p.id) + " 帶著全家從林子裡的小屋回到村裡。");

    // People in households the player could continue with: the player's own and its branches.
    auto inLine = [&](const Person& p) {
        if (p.household == kNone) return false;
        const Household& hh = households_[p.household];
        if (inPlayerHouse(hh.id)) return true;
        return !hh.extinct && playerDynasty_ != kNone && hh.dynasty == playerDynasty_ && hh.head != kNone &&
               people_[hh.head].dynasty == playerDynasty_;
    };
    int dynastyAlive = 0;
    for (const auto& p : people_)
        if (p.alive() && inLine(p)) ++dynastyAlive;

    for (size_t i = 0; i < people_.size(); ++i) {
        Person& p = people_[i];
        if (!p.alive() || p.household == kNone) continue;
        double age = ageYears(p);
        double cap = age < 40 ? 100 : std::max(20.0, 100 - (age - 40) * 1.6);
        double delta = 3 + traitMod(p, "health");
        if (p.needs[Hunger] < 25) delta -= 10;
        if (p.needs[Warmth] < 25) delta -= 6;
        if (mood(p) < 30) delta -= 2;
        p.health = std::clamp(p.health + delta, 0.0, cap);

        double annual = age < 1 ? 0.20 : age < 5 ? 0.05 : age < 15 ? 0.012 : age < 40 ? 0.015
                      : age < 50 ? 0.025 : age < 60 ? 0.045 : age < 70 ? 0.09 : 0.2;
        double mult = mortMult * std::max(0.2, 1 + traitMod(p, "mortality"));
        if (p.health < 25) mult *= 4;
        else if (p.health < 50) mult *= 1.8;
        double monthly = 1 - std::pow(1 - std::min(0.95, annual * mult), 1.0 / 12);

        double plagueChance = plague * std::max(0.3, 1 + traitMod(p, "mortality")) * (p.health < 50 ? 1.5 : 1);
        bool family = isPlayerFamily(p);
        bool line = inLine(p);
        Id hhHead = households_[p.household].head;
        if (hhHead != kNone && people_[hhHead].flags.count("plague_shelter")) plagueChance *= 0.5;
        if (gentle && family) plagueChance *= 0.5;
        if (gentle && line && dynastyAlive <= 2 && age < 55) continue;  // gentle mode keeps the line alive

        if (plagueChance > 0 && rngLife_.chance(plagueChance)) {
            ++plagueDeaths;
            if (line) --dynastyAlive;
            kill(p.id, "黑死病");
            continue;
        }
        // Wasted to nothing with nothing to eat: hunger kills outright, the small and the old first.
        if (p.needs[Hunger] < 25 && p.health <= 5 && rngLife_.chance(age < 5 || age >= 60 ? 0.2 : 0.1)) {
            if (line) --dynastyAlive;
            kill(p.id, "飢餓");
            if (gameOver_) return;
            continue;
        }
        if (rngLife_.chance(monthly)) {
            std::string cause;
            if (age < 1) cause = "夭折";
            else if (age >= 62) cause = "年老";
            else if (p.needs[Hunger] < 25) cause = "飢餓";
            else if (isWinter() && rngLife_.chance(0.5)) cause = "風寒";
            else {
                static const char* kCauses[] = {"熱病", "痢疾", "意外", "傷口感染", "咳疾"};
                cause = kCauses[rngLife_.range(0, 4)];
            }
            if (line) --dynastyAlive;
            kill(p.id, cause);
        }
        if (gameOver_) return;
    }
    if (plagueDeaths > 0)
        log(2, "這個月村裡有 " + std::to_string(plagueDeaths) + " 人死於黑死病。");
}

void World::monthlyBirths() {
    for (auto& w : people_) {
        if (!w.alive() || w.sex != Sex::Female || w.household == kNone || w.pregnantDue >= 0) continue;
        if (w.spouse == kNone || !people_[w.spouse].alive()) continue;
        if (people_[w.spouse].household != w.household) continue;
        double age = ageYears(w);
        if (age < 16 || age > 44) continue;
        if (day_ - w.lastBirthDay < 420) continue;
        const Person& h = people_[w.spouse];
        double c = 0.09 * std::max(0.1, 1 + traitMod(w, "fertility") + traitMod(h, "fertility")) * (w.health / 100);
        if (age > 38) c *= 0.5;
        if (rngLife_.chance(c)) w.pregnantDue = day_ + 270;
    }
}

void World::giveBirth(Person& mother) {
    Id motherId = mother.id;
    mother.pregnantDue = -1;
    mother.lastBirthDay = day_;
    Id father = mother.spouse;
    int count = rngLife_.chance(0.015) ? 2 : 1;
    // Children of an heiress who heads her own household carry her name.
    Id dyn = mother.dynasty;
    // Children take the father's name, unless he married into the mother's family's house.
    const Household& home = households_[mother.household];
    bool marriedIn = home.head != motherId && home.dynasty == mother.dynasty && father != kNone &&
                     people_[father].dynasty != home.dynasty;
    if (father != kNone && home.head != motherId && !marriedIn) dyn = people_[father].dynasty;

    for (int k = 0; k < count; ++k) {
        Sex s = rngLife_.chance(0.51) ? Sex::Male : Sex::Female;
        std::string name = pickName(s, father, motherId);
        Id c = addPerson(name, dyn, s, day_, people_[motherId].household);
        Person& child = people_[c];
        child.father = father;
        child.mother = motherId;
        child.health = 70;
        // Villeinage passes by blood, through the father when there is one.
        child.free = father != kNone ? people_[father].free : people_[motherId].free;
        rollCongenital(child);
        people_[motherId].children.push_back(c);
        if (father != kNone) people_[father].children.push_back(c);
        addNews("birth", c, motherId, people_[motherId].household);
        addConcern(motherId, "joy", c, 55);
        if (father != kNone) addConcern(father, "joy", c, 45);
        if (isPlayerFamily(child) || isPlayerFamily(people_[motherId])) {
            std::string traits;
            for (const auto& t : child.traits) traits += "、" + defs_.traits.at(t).name;
            log(inPlayerHouse(people_[motherId].household) ? 1 : 2, fullName(motherId) + " 生下了" + (s == Sex::Male ? "兒子 " : "女兒 ") + fullName(c) +
                       (traits.empty() ? "" : "（天生" + traits.substr(3) + "）") + "。");
        }
    }
    double risk = 0.02 * std::max(0.2, 1 + traitMod(people_[motherId], "mortality"));
    if (rngLife_.chance(risk)) kill(motherId, "難產");
}

void World::kill(Id id, const std::string& cause) {
    Person& p = people_[id];
    if (!p.alive()) return;
    bool lingering = cause != "意外" && cause != "戰死沙場" && p.household != kNone;
    // The head of the player's family gets a last word at the bedside, if a grown child is there to hear it.
    Id bedside = kNone;
    if (lingering && id == playerHead())
        for (Id c : p.children)
            if (people_[c].alive() && ageYears(people_[c]) >= 14 && people_[c].home == kNone && people_[c].household != kNone &&
                (bedside == kNone || (people_[c].household == p.household && people_[bedside].household != p.household) ||
                 (people_[c].household == people_[bedside].household && people_[c].birthDay < people_[bedside].birthDay)))
                bedside = c;
    if (bedside != kNone) runEventById("deathbed_wishes", id, bedside);
    else if (lingering) deathbedConfession(id);
    p.deathDay = day_;
    p.deathCause = cause;
    p.pregnantDue = -1;
    officeVacated(id);
    Id hid = p.household;
    bool family = isPlayerFamily(p);
    double age = ageYears(p);
    bool away = isAway(p) || (p.deathDay >= 0 && hid == kNone && p.awayFrom != kNone);
    if (family || (hid != kNone && households_[hid].head == id))
        log(family && (inPlayerHouse(hid) || (away && inPlayerHouse(p.awayFrom))) ? 1 : 2,
            age < 1 ? fullName(id) + " 出生沒多久就夭折了。"
                    : (away ? "從外頭傳來消息：" + fullName(id) + "（" + stationName(p) + "）" : fullName(id)) + " 因" + cause +
                          "去世，享年 " + std::to_string(int(age)) + " 歲。");
    if (inPlayerHouse(hid) && hid != kNone && households_[hid].head == id) obituary(id);
    // The road they walked, in one line (design/life-paths.md): what a life came to, forks and all.
    if (family && age >= 12 && (inPlayerHouse(hid) || (away && inPlayerHouse(p.awayFrom))))
        if (std::string line = lifeLine(id); !line.empty()) log(1, "  " + p.firstName + "這一生：" + line + "。");

    // Grief.
    bool male = p.sex == Sex::Male;
    if (p.spouse != kNone) addStress(p.spouse, 35, std::string(male ? "丈夫 " : "妻子 ") + p.firstName + " 的死");
    for (Id par : {p.father, p.mother})
        if (par != kNone) addStress(par, age < 1 ? 10 : age < 16 ? 22 : 25, std::string(male ? "兒子 " : "女兒 ") + p.firstName + " 的死");
    if (age >= 1)
        for (Id c : p.children)
            if (people_[c].alive() && ageYears(people_[c]) < 30)
                addStress(c, 15, std::string(male ? "父親 " : "母親 ") + p.firstName + " 的死");
    // What stays after the stress of the day: grief, to be lived with (and answered, design/season-loop.md).
    // "bereaved:<relation>" lets events give the mourner a moment with it, the dead as the target.
    auto mourn = [&](Id who, double strength, const char* relation) {
        if (who == kNone || !people_[who].alive() || people_[who].household == kNone) return;
        // Love, not blood, sets the depth of it.
        double op = opinion(who, id);
        addConcern(who, "grief", id, std::clamp(strength + op * 0.2, 15.0, 100.0));
        emit({std::string("bereaved:") + relation, kNone, who, id, false, 0});
    };
    // Gladness about the dead one (a birth, a match) turns to grief; it does not linger beside it.
    for (auto& o : people_)
        if (o.alive())
            o.concerns.erase(std::remove_if(o.concerns.begin(), o.concerns.end(),
                                            [&](const Concern& c) { return c.about == id && (c.kind == "joy" || c.kind == "longing"); }),
                             o.concerns.end());
    if (p.spouse != kNone) mourn(p.spouse, 75, "spouse");
    // Babies died often and were mourned all the same, the mother most.
    mourn(p.mother, age < 1 ? 60 : age < 16 ? 70 : 65, "child");
    mourn(p.father, age < 1 ? 50 : age < 16 ? 70 : 65, "child");
    if (age >= 1)
        for (Id c : p.children)
            if (people_[c].alive() && ageYears(people_[c]) < 40) mourn(c, ageYears(people_[c]) < 16 ? 60 : 45, "parent");
    if (p.household != kNone && age >= 1)
        for (Id m : households_[p.household].members)
            if (m != id && m != p.spouse && m != p.father && m != p.mother && people_[m].alive() &&
                ((p.father != kNone && people_[m].father == p.father) || (p.mother != kNone && people_[m].mother == p.mother)))
                mourn(m, 40, "sibling");
    // Word goes round (design/news.md): the houses close to this one know today, and are asked to the wake.
    if (hid != kNone) addNews(age < 1 ? "infant_death" : age < 14 ? "child_death" : "death", id, age < 1 ? p.mother : kNone, hid);
    inheritMemories(id);
    if (hid == kNone) return;
    Household& h = households_[hid];
    emit({"death", kNone, id, p.spouse != kNone && people_[p.spouse].alive() ? p.spouse : kNone, h.head == id});
    h.members.erase(std::remove(h.members.begin(), h.members.end(), id), h.members.end());
    if (h.head == id) {
        Estate left = onHeadDied(h, id);
        if (id == lord_) {
            // The manor passes to the lord's heir, who carries his father's grudges at half strength like any heir.
            if (!h.extinct && h.head != kNone && h.head != id) {
                // Debts owed to the old lord are owed to the manor, and so to the new one.
                for (auto& oh : households_) {
                    if (oh.creditor == id) oh.creditor = h.head;
                    if (oh.heldCreditor == id) oh.heldCreditor = h.head;
                }
                lord_ = h.head;
                if (h.ward != kNone) {
                    // Held for a child: the one holding it acts as lord until the heir is grown.
                    Person& r = people_[lord_];
                    if (r.sex == Sex::Male && r.occupation != "lord") r.flags.insert("occ:" + r.occupation), r.occupation = "lord";
                    log(1, "【領主】" + fullName(id) + " 去世了，莊園歸 " + fullName(h.ward) + "（" + std::to_string(int(ageYears(people_[h.ward]))) +
                               " 歲），由 " + fullName(lord_) + " 代管到繼承人成年。");
                } else {
                    if (people_[lord_].sex == Sex::Male) people_[lord_].occupation = "lord";
                    log(1, "【領主】" + fullName(id) + " 去世了，" + (people_[lord_].father == id ? std::string(people_[lord_].sex == Sex::Male ? "他的兒子 " : "他的女兒 ") : std::string()) +
                               fullName(lord_) + " 繼承了莊園，成了新的領主。");
                }
            }
            return;
        }
        // The dues are on what the dead held, not on what an heir brought with them; none on one who only held it for a child.
        if (!h.extinct && h.head != kNone && !left.regent) {
            chargeHeriot(h, id, left.acres, left.money);
            chargeEntryFine(h, h.ward != kNone ? h.ward : h.head, id, left.acres - left.freehold);
        }
    }
    // What was owed to the dead is owed to their house, to whoever heads it now (the manor's, if nobody is left).
    Id to = !h.extinct && h.head != kNone && h.head != id ? h.head : kNone;
    for (auto& oh : households_) {
        if (oh.creditor == id) oh.creditor = to;
        if (oh.heldCreditor == id) oh.heldCreditor = to;
    }
}

// What a head of the family took to the grave: the grudge never forgiven, the kindness never
// forgotten, the weight still on their mind.
void World::obituary(Id id) {
    const Person& p = people_[id];
    const Memory* worst = nullptr;
    const Memory* best = nullptr;
    double wv = -10, bv = 12;
    for (const auto& m : memories_[id]) {
        if (m.deed == kNone || m.about == kNone) continue;
        // Their own deeds are not what they "never forgave" or "always remembered" others for.
        if (deeds_[m.deed].actor == id) continue;
        Id dyn = m.aboutDynasty != kNone ? m.aboutDynasty : people_[m.about].dynasty;
        if (dyn == kNone || dyn == p.dynasty) continue;
        double v = memoryValue(m);
        if (v < wv) wv = v, worst = &m;
        if (v > bv && m.label != "married") bv = v, best = &m;
    }
    auto house = [&](const Memory& m) {
        Id dyn = m.aboutDynasty != kNone ? m.aboutDynasty : people_[m.about].dynasty;
        return dynasties_[dyn].surname + " 家";
    };
    std::string parts;
    if (worst && best && house(*worst) == house(*best)) {
        parts = "對 " + house(*worst) + "又恨又念（恨的是" + describeDeed(worst->deed) + "，念的是" + describeDeed(best->deed) + "）";
    } else {
        if (worst) parts += "一輩子沒原諒 " + house(*worst) + "（" + describeDeed(worst->deed) + "）";
        if (best) parts += std::string(parts.empty() ? "" : "，也") + "一直記著 " + house(*best) + "的情（" + describeDeed(best->deed) + "）";
    }
    if (ageYears(p) < 14) parts.clear();  // a child head carried no grudges of their own
    if (p.stress >= 40 && !p.stressCause.empty())
        parts += std::string(parts.empty() ? "" : "，") + "臨終前還放不下" + p.stressCause;
    if (!parts.empty()) log(1, "  " + p.firstName + " " + parts + "。");
}

// Who inherits from `dead`, head of `h`, by English custom: the minor heir the land is already held for, eldest
// son, then a dead son's son, then an heir brought in and named (a nephew from the next village), then eldest
// daughter, then a brother, then the nearest of the blood keeping a house of their own in the village (they move
// back in), then the widow, then anyone left in the house. A son or brother away on the road (at war, at school,
// apprenticed in town) counts as if at home: he is called back for it.
Id World::heirOf(const Household& h, Id dead) const {
    const Person& d = people_[dead];
    if (h.ward != kNone && h.ward != dead && wardHeld(h)) return h.ward;
    auto inHouse = [&](Id x) {
        if (x == kNone || x == dead || !people_[x].alive()) return false;
        const Person& q = people_[x];
        if (q.home != kNone && q.home != h.id) return false;  // a servant from another house: theirs, not ours
        if (q.household == h.id || q.home == h.id) return true;
        const StationDef* sd = defs_.station(q.station);
        return isAway(q) && q.awayFrom == h.id && !(sd && sd->lasting);
    };
    Id heir = kNone;
    auto eldest = [&](const std::vector<Id>& pool, Sex want) {
        Id best = kNone;
        for (Id c : pool)
            if (inHouse(c) && people_[c].sex == want && (best == kNone || people_[c].birthDay < people_[best].birthDay))
                best = c;
        return best;
    };
    // The dead's own lawful children: a by-blow raised under another name does not take the land.
    std::vector<Id> children;
    for (Id c : d.children)
        if (d.dynasty == kNone || people_[c].dynasty == d.dynasty) children.push_back(c);
    heir = eldest(children, Sex::Male);
    if (heir == kNone) {
        std::vector<Id> grandsons;
        for (Id c : d.children)
            for (Id g : people_[c].children) grandsons.push_back(g);
        heir = eldest(grandsons, Sex::Male);
    }
    if (heir == kNone)
        for (Id m : h.members)
            if (m != dead && people_[m].alive() && people_[m].flags.count("designated_heir") && people_[m].home == kNone &&
                (heir == kNone || people_[m].birthDay < people_[heir].birthDay))
                heir = m;
    if (heir == kNone) heir = eldest(children, Sex::Female);
    // One who married in holds the house's land for its line: past their own children, it goes back to the
    // blood of the one they married (a widow's late husband's brother or sister at home before her own kin).
    auto bornTo = [&](const Person& x) { return x.father != kNone ? people_[x.father].dynasty : x.mother != kNone ? people_[x.mother].dynasty : x.dynasty; };
    Id line = dead;
    if (d.spouse != kNone && h.dynasty != kNone && bornTo(d) != h.dynasty && bornTo(people_[d.spouse]) == h.dynasty) line = d.spouse;
    const Person& lp = people_[line];
    if (heir == kNone && lp.father != kNone) heir = eldest(people_[lp.father].children, Sex::Male);
    if (heir == kNone && line != dead)
        for (Id m : h.members) {
            const Person& q = people_[m];
            if (!inHouse(m) || q.household != h.id || ageYears(q) < 16 || !closeKin(m, line)) continue;
            if (heir == kNone || (q.sex == Sex::Male) > (people_[heir].sex == Sex::Male) ||
                ((q.sex == Sex::Male) == (people_[heir].sex == Sex::Male) && q.birthDay < people_[heir].birthDay))
                heir = m;
        }
    // Of the blood but keeping a house of their own here: the dead's own kin first (a brother's son), men first,
    // then the eldest.
    Id lordHouseId = lord_ != kNone ? people_[lord_].household : kNone;
    if (heir == kNone) {
        auto nearer = [&](const Person& a, const Person& b) {
            auto nephew = [&](const Person& x) { return lp.father != kNone && x.father != kNone && people_[x.father].father == lp.father; };
            if (nephew(a) != nephew(b)) return nephew(a);
            if ((a.sex == Sex::Male) != (b.sex == Sex::Male)) return a.sex == Sex::Male;
            return a.birthDay < b.birthDay;
        };
        Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
        for (const auto& q : people_)
            // Blood, not name: a wife takes her husband's name but is no kin of his brother's.
            if (q.alive() && q.id != dead && closeKin(q.id, line) && q.household != kNone && q.household != h.id &&
                (h.id != lordHouseId || q.dynasty == h.dynasty) &&  // the manor stays in the lord's own family
                q.household != lordHouse && households_[q.household].house != "manor" &&  // the manor is not given up for a cottage
                households_[q.household].head == q.id && households_[q.household].ward == kNone &&  // not one holding for a child
                !households_[q.household].extinct && ageYears(q) >= 16 &&
                q.occupation != "priest" && (heir == kNone || nearer(q, people_[heir])))
                heir = q.id;
    }
    if (heir == kNone && d.spouse != kNone && people_[d.spouse].alive() && people_[d.spouse].household == h.id) heir = d.spouse;
    if (heir == kNone)
        for (Id m : h.members)
            if (m != dead && people_[m].alive() && people_[m].home == kNone &&
                (heir == kNone || people_[m].birthDay < people_[heir].birthDay))
                heir = m;
    // Last, a child settled away for good (a master in town, a wife in town): better than the land going back.
    if (heir == kNone)
        for (Id c : d.children)
            if (c != dead && isAway(people_[c]) && people_[c].awayFrom == h.id && people_[c].station != "priest_away" &&
                (heir == kNone || people_[c].birthDay < people_[heir].birthDay))
                heir = c;
    return heir;
}

World::Estate World::onHeadDied(Household& h, Id dead) {
    recallServants(h.id);
    // What the dead leaves, for the 【分家產】 line and the lord's dues: before anyone moves in with their own.
    Estate left{h.acres, h.freehold, h.money, h.house, h.items.size(), false};
    // One holding the land for a child dies: it stays the child's, and owes no death duty or entry fine of its own.
    left.regent = h.ward != kNone && h.ward != dead && wardHeld(h);
    const Id heldFor = left.regent ? h.ward : kNone;
    Id heir = heirOf(h, dead);
    if (heir != kNone && isAway(people_[heir])) setStation(heir, "home", 0, "回家接下家裡的地");
    bool player = inPlayerHouse(h.id);
    if (heir == kNone) {
        h.extinct = true;
        h.head = kNone;
        h.ward = h.wardLine = kNone;
        // Servants living here go back to their own families.
        for (Id m : std::vector<Id>(h.members))
            if (people_[m].home != kNone) {
                Id home = people_[m].home;
                people_[m].home = kNone;
                people_[m].flags.erase("in_service");
                moveTo(m, home);
            }
        double lost = moveLand(h.id, kNone, h.acres, LandPick::CustomaryFirst, "絕戶後收回");
        log(2, dynasties_[h.dynasty].surname + " 家絕戶了，" + std::to_string(int(lost)) +
                   " 畝地回到領主手上。");
        for (Id it : h.items) items_[it].lost = true;
        rehomeAway(h.id, kNone);
        if (player) followSurvivingBranch();
        return left;
    }
    for (Id m : h.members) people_[m].flags.erase("designated_heir");  // named for this succession only
    const Person& d = people_[dead];
    auto plural = [](double x) { return std::to_string(int(std::lround(x))); };
    auto houseName = [](const std::string& k) {
        return std::string(k == "stone" ? "石屋" : k == "timber" ? "木屋" : k == "manor" ? "莊園宅邸" : "泥牆茅屋");
    };
    auto estateText = [&](const Estate& e) {
        std::string t = e.acres >= 0.5 ? "地 " + plural(e.acres) + " 畝" : "沒有地";
        if (e.freehold >= 1) t += "（其中自由地 " + plural(e.freehold) + " 畝）";
        t += "、" + houseName(e.house);
        if (e.money >= 0.5) t += "、" + plural(e.money) + " 便士";
        if (e.items > 0) t += "、家什 " + std::to_string(e.items) + " 件";
        return t;
    };

    // The nearest of the blood is the family the player plays: the holding comes to the player's house, which stays
    // where it is (its people, its wants, its ledger), rather than the family moving into a cottage.
    if (!player && playerHousehold_ != kNone && people_[heir].household == playerHousehold_ &&
        (lord_ == kNone || people_[lord_].household != h.id)) {
        Household& ours = households_[playerHousehold_];
        h.head = heir;
        chargeHeriot(h, dead, left.acres, left.money);
        h.head = kNone;
        for (auto& oh : households_) {
            if (oh.creditor == dead) oh.creditor = heir;
            if (oh.heldCreditor == dead) oh.heldCreditor = heir;
        }
        std::string rel = kinWord(heir, dead);
        log(1, "【繼承】" + (rel.empty() ? std::string() : rel + " ") + fullName(dead) + " 去世了，身後沒有更近的人，家業歸了你家的 " + fullName(heir) + "。");
        log(1, "【分家產】" + d.firstName + " 留下" + estateText(left) + "，" + (h.members.empty() ? "" : "家裡的人也一起搬過來，") + "全併進你家。");
        mergeHousehold(h, ours, "繼承時併進來");
        noteLife(heir, "接下" + fullName(dead) + "的家業");
        chargeEntryFine(ours, heir, dead, left.acres - left.freehold);
        return left;
    }

    h.head = heir;
    if (!left.regent) h.ward = kNone;
    // An heir under 16: the widow (or the eldest grown kinsman at home) holds the land for them until then.
    Id regent = kNone;
    if (ageYears(people_[heir]) < 16) {
        if (d.spouse != kNone && people_[d.spouse].alive() && people_[d.spouse].household == h.id && ageYears(people_[d.spouse]) >= 16 &&
            people_[d.spouse].home == kNone)
            regent = d.spouse;
        for (int pass = 0; pass < 2 && regent == kNone; ++pass)
            for (Id m : h.members) {
                const Person& q = people_[m];
                if (m == heir || m == dead || !q.alive() || q.home != kNone || ageYears(q) < 16) continue;
                if (pass == 0 && !closeKin(m, heir)) continue;  // the blood first, then anyone grown at home
                if (regent == kNone || q.birthDay < people_[regent].birthDay) regent = m;
            }
    }
    if (regent != kNone) {
        if (!left.regent) h.wardLine = dead;  // whose heirs the land is held for (the line, should this child be lost)
        h.ward = heir;
        heir = regent;
        h.head = regent;
    } else {
        h.ward = kNone;
    }
    // The house goes on, but to someone not of the player's blood (a widower who married in, a
    // stepchild): the family line ends here unless another branch carries it. With a minor heir held for, it is the
    // heir's blood that counts, not the one holding the land.
    auto outside = [&](Id x) {
        return people_[x].dynasty != playerDynasty_ && dynasties_[people_[x].dynasty].surname != dynasties_[playerDynasty_].surname;
    };
    if (player && outside(h.ward != kNone ? h.ward : heir)) {
        // First, a son or kinsman of the blood living elsewhere (in service, lodging with in-laws)
        // comes home to take it up.
        Id kin = kNone;
        for (const auto& q : people_) {
            if (!q.alive() || q.id == dead || q.dynasty != playerDynasty_ || ageYears(q) < 12) continue;
            const StationDef* qs = defs_.station(q.station);
            if (q.household == kNone && !(isAway(q) && !(qs && qs->lasting))) continue;  // away: only on the road
            if (q.household != kNone && q.household != h.id && households_[q.household].head == q.id) continue;  // heads their own house: a branch
            bool child = q.father == dead || q.mother == dead;
            bool better = kin == kNone || (child && !(people_[kin].father == dead || people_[kin].mother == dead)) ||
                          ((child == (people_[kin].father == dead || people_[kin].mother == dead)) && q.birthDay < people_[kin].birthDay);
            if (better) kin = q.id;
        }
        if (kin != kNone) {
            if (isAway(people_[kin])) setStation(kin, "home", 0, "回家接下家裡的地");
            if (people_[kin].household != h.id) {
                people_[kin].home = kNone;
                people_[kin].flags.erase("in_service");
                moveTo(kin, h.id);
                log(1, fullName(kin) + " 回到家裡，接下了這份地。");
            }
            heir = kin;
            h.head = heir;
            h.ward = kNone;
        }
    }
    // Kin keeping a house of their own come to the holding, with their household and goods.
    std::string cameBack;
    if (people_[heir].household != kNone && people_[heir].household != h.id) {
        Household& theirs = households_[people_[heir].household];
        cameBack = fullName(heir) + (theirs.members.size() > 1 ? " 帶著一家人" : " ") + "搬過來，接下這份地";
        if (theirs.acres >= 0.5) cameBack += "，自家原有的 " + plural(theirs.acres) + " 畝也一併帶過來";
        cameBack += "。";
        mergeHousehold(theirs, h, "搬來接家業時帶過來");
        noteLife(heir, "搬進" + fullName(dead) + "家，接下那份地");
    }
    noteLife(heir, h.ward != kNone ? "替" + shortName(h.ward) + "持家" : "當了家");
    settleAsHead(heir);
    if (!left.regent) {
        inheritRegard(dead, heir);
        if (h.ward != kNone) inheritRegard(dead, h.ward);
    }
    if (player && outside(h.ward != kNone ? h.ward : heir)) {
        log(1, "房子和地傳給了 " + fullName(heir) + "，可是" + fullName(heir) + "不姓 " + dynasties_[playerDynasty_].surname + "。");
        h.dynasty = people_[heir].dynasty;
        followSurvivingBranch(true);
        return left;
    }
    for (Id it : h.items) items_[it].owners.push_back(heir);
    if (!player) return left;

    Person& hp = people_[heir];
    // What the family wanted of the old head (to be comforted, a match spoken for) is not what they want of the new.
    for (Id m : h.members) {
        people_[m].want.clear();
        people_[m].wantSince = -1;
    }
    dynasties_[playerDynasty_].heads.push_back(heir);
    dynasties_[playerDynasty_].headSince.push_back(day_);
    if (h.ward != kNone) dynasties_[playerDynasty_].heldFor[dynasties_[playerDynasty_].heads.size() - 1] = h.ward;
    if (!cameBack.empty()) log(1, cameBack);
    std::string line;
    // Namesakes (a son named for his father): the dead is "已故的…", the heir by what they were to the dead.
    bool same = d.firstName == hp.firstName || (h.ward != kNone && people_[h.ward].firstName == d.firstName);
    std::string deadRel = kinWord(dead, heir);
    std::string heirName = d.firstName == hp.firstName && !deadRel.empty() ? deadRel + " " + hp.firstName : hp.firstName;
    std::string deadName = same ? "已故的" + d.firstName : d.firstName;
    if (h.ward != kNone) {
        const Person& wd = people_[h.ward];
        std::string rel = kinWord(h.ward, heir);
        std::string pron = wd.sex == Sex::Male ? "他" : "她";
        line = "【繼承】家業歸 " + fullName(h.ward) + "（" + std::to_string(int(ageYears(wd))) + " 歲），" + pron +
               "還小，由" + (rel.empty() ? "" : rel + " ") + fullName(heir) + " 代為持家，等" + pron + "滿 16 歲再交出去。往後由你扮演 " +
               heirName + "。";
    } else {
        line = "【繼承】" + fullName(heir) + "（" + std::to_string(int(ageYears(hp))) + " 歲）成為新的家主。";
    }
    log(1, line);
    // What came down, and to whom (分家產): nothing changes hands unseen.
    std::string merged = h.acres > left.acres + 0.5 ? "連同帶過來的，家裡現在有地 " + plural(h.acres) + " 畝" +
                                                          (h.money >= 0.5 ? "、" + plural(h.money) + " 便士" : "") + "。"
                                                    : "";
    if (left.regent)
        log(1, "【分家產】" + deadName + " 代管的家業（" + estateText(left) + "）照舊是 " + people_[heldFor].firstName + " 的，" +
                   (heir == heldFor ? "沒有別人能代管，由" + people_[heldFor].firstName + "自己當家。" : "改由 " + heirName + " 接著代管。") + merged);
    else
        log(1, "【分家產】" + deadName + " 留下" + estateText(left) + "，全歸這個家，由 " + heirName + " 掌管。" + merged);
    for (Id it : h.items) {
        const Item& item = items_[it];
        if (item.owners.size() >= 3)
            log(1, "  " + defs_.items.at(item.type).name +
                       (item.maker != kNone ? "（" + fullName(item.maker) + " 所製）" : "") + "傳到了第 " +
                       std::to_string(item.owners.size()) + " 任家主手上。");
    }
    // What the dead head did not live to finish stays with the family.
    if (!d.ambition.empty()) dynasties_[playerDynasty_].regret = d.ambition;
    hp.ambition.clear();
    if (ageYears(hp) >= 16) chooseAmbition(hp);
    return left;
}

// Two households become one (an heir with a house of their own takes up the holding): people, goods, land, money and
// debts, the better house and the trade, and what the village knows the house is after or owes.
void World::mergeHousehold(Household& from, Household& to, const std::string& how) {
    if (from.id == to.id || from.extinct) return;
    for (Id x : from.members) {
        people_[x].household = to.id;
        to.members.push_back(x);
    }
    for (Id it : from.items) {
        items_[it].household = to.id;
        to.items.push_back(it);
    }
    moveLand(from.id, to.id, from.acres, LandPick::CustomaryFirst, how);
    to.money += from.money;
    to.food += from.food;
    to.firewood += from.firewood;
    auto rank = [](const std::string& k) { return k == "manor" ? 3 : k == "stone" ? 2 : k == "timber" ? 1 : 0; };
    if (rank(from.house) > rank(to.house)) to.house = from.house;
    if (to.trade.empty() && !from.trade.empty()) to.trade = from.trade, to.tradeLet = from.tradeLet;
    // What they owed comes with them, still owed to whom it was owed.
    auto owe = [&](double amount, Id creditor) {
        if (amount <= 0.5) return;
        if (to.debt <= 0.5) to.debt = amount, to.creditor = creditor;
        else if (to.creditor == creditor) to.debt += amount;
        else if (to.heldDebt <= 0.5 || to.heldCreditor == creditor) to.heldDebt += amount, to.heldCreditor = creditor;
        else to.debt += amount;  // a third creditor: lumped with the first, the books of a village are not so fine
    };
    owe(from.debt, from.creditor);
    owe(from.heldDebt, from.heldCreditor);
    to.debtYears = std::max(to.debtYears, from.debtYears);
    to.noOxUntil = std::max(to.noOxUntil, from.noOxUntil);
    to.rentFactor = std::max(to.rentFactor, from.rentFactor);
    if (from.leaseUntil > to.leaseUntil) to.leaseAcres = from.leaseAcres, to.leaseUntil = from.leaseUntil;
    for (const auto& [k, v] : from.ledger) to.ledger[k] += v;
    // The village's view of the old house is now of this one: its wants, the fine it owes, its servants away.
    for (auto& wt : wants_)
        if (wt.open && wt.house == from.id) {
            bool dup = false;
            for (const auto& o : wants_)
                dup = dup || (o.open && o.house == to.id && o.kind == wt.kind && o.object == wt.object && o.office == wt.office);
            if (dup) dropWant(wt, "兩家併了");
            else wt.house = to.id;
        }
    for (auto& f : fines_)
        if (f.house == from.id) f.house = to.id;
    for (auto& p : people_) {
        if (!p.alive()) continue;
        if (p.home == from.id) p.home = to.id;
        if (p.flags.erase("apprentice_at_" + std::to_string(from.id))) p.flags.insert("apprentice_at_" + std::to_string(to.id));
        // Serving in what is now their own house: they are simply at home.
        if (p.home == to.id && p.household == to.id) {
            p.home = kNone;
            p.serviceEnd = -1;
            p.flags.erase("in_service");
            if (p.station == "servant") p.station.clear();
        }
    }
    // A debt one of the two houses owed the other is now owed to itself: gone.
    auto own = [&](Id creditor) { return creditor != kNone && people_[creditor].household == to.id; };
    if (own(to.heldCreditor)) to.heldDebt = 0, to.heldCreditor = kNone;
    if (own(to.creditor)) {
        to.debt = to.heldDebt, to.creditor = to.heldCreditor;
        to.heldDebt = 0, to.heldCreditor = kNone;
    }
    from.members.clear();
    from.items.clear();
    from.money = from.food = from.firewood = from.debt = from.heldDebt = 0;
    from.creditor = from.heldCreditor = kNone;
    from.head = kNone;
    from.ward = from.wardLine = kNone;
    from.extinct = true;
    rehomeAway(from.id, to.id);
}

// The village does not forget the dead in a day: half of what each villager felt for them (kindness done, grudges
// held) carries over to whoever takes up the house, as "for the old man's sake", fading over ten years; so does
// half of how well they knew each other.
void World::inheritRegard(Id dead, Id heir) {
    if (dead == kNone || heir == kNone || dead == heir) return;
    for (auto& q : people_) {
        if (!q.alive() || q.id == heir || q.id == dead || q.household == people_[heir].household) continue;
        double sum = 0;
        for (const auto& m : memories_[q.id])
            if (m.about == dead && m.aboutDynasty == kNone && m.label != "married") sum += memoryValue(m);
        if (std::abs(sum) >= 4) addOpinion(q.id, heir, sum > 0 ? "for_the_dead" : "against_the_dead", std::clamp(sum * 0.5, -30.0, 30.0), 10);
        auto k = q.known.find(dead);
        if (k != q.known.end() && k->second >= 4) {
            float half = k->second * 0.5f;
            float& a = q.known[heir];
            a = std::max(a, half);
            float& b = people_[heir].known[q.id];
            b = std::max(b, half);
        }
    }
}

bool World::wardHeld(const Household& h) const {
    if (h.ward == kNone) return false;
    const Person& w = people_[h.ward];
    if (!w.alive()) return false;
    if (w.household == h.id || w.home == h.id) return true;  // at home, or serving in another house for a few years
    const StationDef* sd = defs_.station(w.station);
    return isAway(w) && w.awayFrom == h.id && !(sd && sd->lasting);  // at school, in town, at the war: coming back
}

// The house passes from whoever held it for a child to the child, now grown (or to the next of the line).
void World::handOver(Household& h, Id to, const std::string& why) {
    Id regent = h.head;
    Person& w = people_[to];
    h.ward = kNone;
    h.wardLine = kNone;
    h.head = to;
    if (regent != kNone && regent == lord_) {  // the manor's heir comes of age: the lordship is theirs now
        for (auto& oh : households_) {
            if (oh.creditor == regent) oh.creditor = to;
            if (oh.heldCreditor == regent) oh.heldCreditor = to;
        }
        lord_ = to;
        if (w.sex == Sex::Male) w.occupation = "lord";
        Person& r = people_[regent];
        for (auto it = r.flags.begin(); it != r.flags.end(); ++it)
            if (it->rfind("occ:", 0) == 0) {
                r.occupation = it->substr(4);  // back to what they were before holding the manor
                r.flags.erase(it);
                break;
            }
        log(1, "【領主】" + fullName(to) + " 從 " + fullName(regent) + " 手上接下莊園。");
    }
    noteLife(to, why);
    settleAsHead(to);
    if (!inPlayerHouse(h.id)) return;
    for (Id m : h.members) {
        people_[m].want.clear();
        people_[m].wantSince = -1;
    }
    dynasties_[playerDynasty_].heads.push_back(to);
    dynasties_[playerDynasty_].headSince.push_back(day_);
    std::string pron = w.sex == Sex::Male ? "他" : "她";
    log(1, "【交家】" + fullName(to) + "（" + std::to_string(int(ageYears(w))) + " 歲）" + why + "，" +
               (regent != kNone ? fullName(regent) + " 把家交給了" + pron + "。" : "從此當家。") + "往後由你扮演 " + w.firstName + "。");
    if (regent != kNone && !people_[regent].ambition.empty()) dynasties_[playerDynasty_].regret = people_[regent].ambition;
    if (regent != kNone) people_[regent].ambition.clear();
    w.ambition.clear();
    chooseAmbition(w);
}

// An heir the land was held for comes of age, or is lost: the house is theirs, or goes to the next of the line.
void World::monthlyWards() {
    for (auto& h : households_) {
        if (h.extinct || h.ward == kNone) continue;
        if (!wardHeld(h)) {
            // Dead, or settled away for good: the next of the same line, not the one holding the land.
            Id lost = h.ward;
            h.ward = kNone;
            Id next = h.wardLine != kNone ? heirOf(h, h.wardLine) : kNone;
            const Person& np = people_[next != kNone ? next : 0];
            bool ours = next != kNone && (np.household == h.id || np.home == h.id || (isAway(np) && np.awayFrom == h.id));
            if (next == kNone || next == h.head || !ours) {
                h.wardLine = kNone;
                continue;
            }
            if (inPlayerHouse(h.id) && playerDynasty_ != kNone && !dynasties_[playerDynasty_].heads.empty())
                dynasties_[playerDynasty_].heldFor[dynasties_[playerDynasty_].heads.size() - 1] = next;
            if (ageYears(np) < 16 || np.home == h.id) {
                h.ward = next;
                if (inPlayerHouse(h.id))
                    log(1, "【繼承】家業本來是 " + people_[lost].firstName + " 的，如今改歸 " + fullName(next) + "（" +
                               std::to_string(int(ageYears(people_[next]))) + " 歲），仍由 " + fullName(h.head) + " 代為持家。");
                continue;
            }
            if (isAway(np)) setStation(next, "home", 0, "回家接下家裡的地");
            handOver(h, next, "接下" + people_[lost].firstName + "沒能接的家業");
            continue;
        }
        Person& w = people_[h.ward];
        // A servant takes the house over when the term is up; one away at school or in town, when back.
        if (ageYears(w) < 16 || w.household != h.id || w.home != kNone) continue;
        handOver(h, w.id, ageYears(w) < 17 ? "成年了，當了家" : "回到家裡，當了家");
    }
}

void World::followSurvivingBranch(bool lineEnded) {
    if (playerHousehold_ == kNone) return;  // nobody plays the village yet (Config::prerun)
    const Household& cur = households_[playerHousehold_];
    if (!lineEnded && !cur.extinct && cur.head != kNone && !cur.members.empty()) return;
    // The player follows another branch of the same family, if one exists.
    for (auto& other : households_) {
        if (other.extinct || other.dynasty != playerDynasty_ || other.head == kNone) continue;
        if (people_[other.head].dynasty != playerDynasty_) continue;
        playerHousehold_ = other.id;
        dynasties_[playerDynasty_].heads.push_back(other.head);
        dynasties_[playerDynasty_].headSince.push_back(day_);
        log(1, "家族由分家的 " + fullName(other.head) + " 延續下去。");
        if (ageYears(people_[other.head]) >= 16) chooseAmbition(people_[other.head]);
        return;
    }
    gameOver_ = true;
    log(1, "【終局】" + dynasties_[playerDynasty_].surname + " 家族斷絕了。");
}

void World::marry(Id man, Id woman) {
    Person& m = people_[man];
    Person& w = people_[woman];
    m.spouse = woman;
    w.spouse = man;
    // Marriage ends a term of service: a groom goes home first, so the bride follows him there.
    for (Person* x : {&m, &w})
        if (x->home != kNone) {
            Id home = x->home;
            x->home = kNone;
            x->serviceEnd = -1;
            x->flags.erase("in_service");
            if (x->station == "servant") setStation(x->id, "home", 0, "");  // the wedding itself is the note (noteLife below)
            if (x == &m && x->household != kNone && households_[x->household].head != man && !households_[home].extinct)
                moveTo(man, home);
            // A girl the land is held for marries from her own house, as the heiress she is.
            if (x == &w && households_[home].ward == woman && !households_[home].extinct) moveTo(woman, home);
        }
    recordDeed("married", man, woman);
    noteLife(man, "娶了" + fullName(woman));
    noteLife(woman, "嫁給" + fullName(man));
    // The match each house was after for either of them is settled: theirs, or another's who got there first.
    for (auto& wt : wants_) {
        if (!wt.open || wt.kind != "match") continue;
        bool ours = (wt.subject == man && wt.object == woman) || (wt.subject == woman && wt.object == man);
        if (ours) {
            dropWant(wt, "成了親");
        } else if (wt.object == man || wt.object == woman) {
            Id other = wt.object == man ? woman : man;
            Id ohh = people_[other].household;
            std::string thing = wantThing(wt);
            std::string lost = ohh != kNone && households_[ohh].dynasty != kNone ? "讓" + dynasties_[households_[ohh].dynasty].surname + "家訂走了" : "許了別人";
            dropWant(wt, lost);
            Id loser = households_[wt.house].head;
            Id winner = ohh != kNone ? households_[ohh].head : kNone;
            if (loser != kNone && winner != kNone && winner != loser) addOpinion(loser, winner, "took_our_want", -6, 6);
            if (inPlayerHouse(wt.house)) log(1, "  （你家看中的" + thing + "，" + lost + "。）");
        } else if (wt.subject == man || wt.subject == woman) {
            dropWant(wt, "另結了親");
        }
    }
    addNews("marriage", man, woman, m.household != kNone ? m.household : w.household);
    chargeMerchet(woman, false);
    // A wedding is a joy to those who wanted it; whoever they had longed for before becomes a heartbreak.
    for (auto [a, b] : {std::pair{man, woman}, std::pair{woman, man}}) {
        Person& x = people_[a];
        for (auto& c : x.concerns)
            if (c.kind == "longing" && c.about != b) {
                Id other = c.about;
                c.strength = 0;
                addConcern(a, "heartbreak", other, 55);
                break;
            }
        x.concerns.erase(std::remove_if(x.concerns.begin(), x.concerns.end(), [](const Concern& c) { return c.strength <= 0.5; }),
                         x.concerns.end());
        double op = opinion(a, b);
        if (op > -5) addConcern(a, "joy", b, std::clamp(30 + op * 0.5, 20.0, 70.0));
    }
    bool family = isPlayerFamily(m) || isPlayerFamily(w);
    int imp = inPlayerHouse(m.household) || inPlayerHouse(w.household) ? 1 : 2;

    Id oldHouse = w.household;
    bool wardBride = oldHouse != kNone && households_[oldHouse].ward == woman;
    bool regentBride = oldHouse != kNone && households_[oldHouse].head == woman && households_[oldHouse].ward != kNone;
    bool heiress = oldHouse != kNone && (households_[oldHouse].head == woman || wardBride);
    // A widow holding the land for a child weds a man with a house of his own: the land is the child's, so she goes
    // to him and the eldest grown at home (or the child alone) holds it on; with nobody to, his house joins the child's.
    Id holdsOn = kNone;
    if (regentBride && households_[m.household].head == man && !inPlayerHouse(oldHouse)) {
        const Household& oh = households_[oldHouse];
        for (Id x : oh.members) {
            const Person& q = people_[x];
            if (x == woman || x == oh.ward || !q.alive() || q.home != kNone || ageYears(q) < 16) continue;
            if (holdsOn == kNone || q.birthDay < people_[holdsOn].birthDay) holdsOn = x;
        }
        // Nobody grown to leave it with: the child is called home from service or from away to keep the house.
        Id ward = oh.ward;
        if (holdsOn == kNone && people_[ward].alive() && people_[ward].household != oldHouse) {
            Person& c = people_[ward];
            if (c.home == oldHouse) {
                c.home = kNone;
                c.serviceEnd = -1;
                c.flags.erase("in_service");
                moveTo(ward, oldHouse);
            } else if (isAway(c) && c.awayFrom == oldHouse) {
                setStation(ward, "home", 0, "母親改嫁，回村守家");
            }
        }
        if (holdsOn == kNone && people_[ward].household == oldHouse) holdsOn = ward;
        if (holdsOn != kNone) heiress = false;
    }
    // A family with no son keeps one daughter at home; her husband marries in to carry the land.
    if (!heiress && inPlayerHouse(oldHouse) && households_[m.household].head != man) {
        bool heirAtHome = false;
        for (Id x : households_[oldHouse].members) {
            const Person& q = people_[x];
            if (x == woman || x == households_[oldHouse].head || !q.alive()) continue;
            if (q.dynasty == playerDynasty_ && (q.sex == Sex::Male || (q.spouse != kNone && people_[q.spouse].household == oldHouse)))
                heirAtHome = true;
        }
        heiress = !heirAtHome;
    }
    // The player's only heir does not marry away, even to a man with his own holding: he brings it to her.
    if (!heiress && inPlayerHouse(oldHouse) && households_[oldHouse].head != kNone &&
        heirOf(households_[oldHouse], households_[oldHouse].head) == woman)
        heiress = true;
    if (heiress && households_[m.household].head == man) {
        // Two heads marry: her household, land and goods join his, unless hers is the player's,
        // in which case he brings his into hers so the player's line goes on.
        // A regent's house is the child's: his joins it (unless his is the player's, which stays where it is).
        bool intoHers = inPlayerHouse(oldHouse) || (regentBride && !inPlayerHouse(m.household));
        Household& from = households_[intoHers ? m.household : oldHouse];
        Household& to = households_[intoHers ? oldHouse : m.household];
        mergeHousehold(from, to, "成親時帶進門");
        if (intoHers) {
            if (family) log(imp, fullName(man) + " 娶了 " + fullName(woman) + "，帶著家當搬進她家，兩家合為一戶。");
            return;
        }
        if (family) log(imp, fullName(man) + " 娶了" + (wardBride ? "" : "寡婦 ") + fullName(woman) + "，兩家合為一戶。");
        w.dynasty = m.dynasty;
        return;
    }
    if (heiress) {
        // He moves in with her; their children keep her family name.
        Household& from = households_[m.household];
        from.members.erase(std::remove(from.members.begin(), from.members.end(), man), from.members.end());
        m.household = oldHouse;
        households_[oldHouse].members.push_back(man);
        if (family) log(imp, fullName(man) + " 入贅，娶了 " + fullName(woman) + "。");
        return;
    }

    // Dowry: money, and sometimes a chest or an heirloom from her family.
    if (oldHouse != kNone) {
        Household& from = households_[oldHouse];
        double dowry = std::min(from.money * 0.2, 120.0);
        from.money -= dowry;
        book(from, "嫁妝", -dowry);
        households_[m.household].money += dowry;
        book(households_[m.household], "媳婦帶來的嫁妝", dowry);
        from.members.erase(std::remove(from.members.begin(), from.members.end(), woman), from.members.end());
        if (regentBride && holdsOn != kNone) {
            if (holdsOn == from.ward) from.ward = kNone, from.wardLine = kNone;
            from.head = holdsOn;
            settleAsHead(holdsOn);
            noteLife(holdsOn, from.ward != kNone ? "替 " + people_[from.ward].firstName + " 持家" : "當了家");
            if (inPlayerHouse(m.household) || inPlayerHouse(from.id))
                log(1, "  " + fullName(woman) + " 改嫁出去，" + (from.ward != kNone ? fullName(holdsOn) + " 接著替 " + fullName(from.ward) + " 持家。"
                                                                                  : fullName(holdsOn) + " 自己當家。"));
        }
        std::string gift;
        Id movable = kNone;
        for (Id it : from.items)
            if (defs_.items.at(items_[it].type).dowry) movable = it;
        if (movable != kNone && rngSocial_.chance(0.25)) {
            Id it = movable;
            from.items.erase(std::remove(from.items.begin(), from.items.end(), it), from.items.end());
            items_[it].household = m.household;
            households_[m.household].items.push_back(it);
            gift = defs_.items.at(items_[it].type).name;
        } else if (rngSocial_.chance(0.35)) {
            Id it = addItem("dowry_chest", w.mother, m.household);
            gift = defs_.items.at(items_[it].type).name;
        }
        // Marrying into a better-off family counts towards the "good match" ambition.
        const Household& groomHome = households_[m.household];
        if (family) {
            bool brideFamily = isPlayerFamily(w);
            const Household& theirs = brideFamily ? groomHome : from;
            const Household& ours = brideFamily ? from : groomHome;
            bool better = theirs.acres > ours.acres * 1.3 + 2 || (theirs.house != "cottage" && ours.house == "cottage");
            Id ph = playerHousehold_ != kNone ? households_[playerHousehold_].head : kNone;
            if (better && ph != kNone) people_[ph].flags.insert("made_good_match");
        }
        if (family)
            log(imp, fullName(man) + " 娶了 " + fullName(woman) +
                       (dowry >= 1 ? "，嫁妝 " + std::to_string(int(dowry)) + " 便士" + (gift.empty() ? "" : "和一件" + gift)
                                   : (gift.empty() ? "" : "，嫁妝是一件" + gift)) + "。");
        if (from.members.empty()) {
            from.head = kNone;
            from.extinct = true;
            rehomeAway(from.id, kNone);
            moveLand(from.id, kNone, from.acres, LandPick::CustomaryFirst, "嫁出去後收回");
        }
    } else if (family) {
        log(imp, fullName(man) + " 娶了外村的 " + w.firstName + "。");
    }
    w.household = m.household;
    w.dynasty = m.dynasty;
    if (std::find(households_[m.household].members.begin(), households_[m.household].members.end(), woman) ==
        households_[m.household].members.end())
        households_[m.household].members.push_back(woman);

    // Younger sons set up their own household when land is available.
    Household& home = households_[m.household];
    if (home.head != man && home.dynasty == m.dynasty) {
        if (home.head != kNone && (heirOf(home, home.head) == man || home.ward == man)) return;  // the heir stays on the family holding
        double land = std::min(vacantAcres(), rngSocial_.chance(0.5) ? 10.0 : 5.0);
        if (land < 2) return;  // nothing to rent: they stay under the family roof
        Id nh = addHousehold(m.dynasty, 0, home.money * 0.15);
        moveLand(kNone, nh, land, LandPick::CustomaryFirst, "分家時向領主租來");
        Household& oldHome = households_[m.household];
        oldHome.money *= 0.85;
        Household& fresh = households_[nh];
        for (Id who : {man, woman}) {
            oldHome.members.erase(std::remove(oldHome.members.begin(), oldHome.members.end(), who),
                                  oldHome.members.end());
            people_[who].household = nh;
            fresh.members.push_back(who);
        }
        fresh.head = man;
        settleAsHead(man);
        fresh.food = consumption(fresh) * 120;
        oldHome.food = std::max(0.0, oldHome.food - fresh.food);
        fresh.firewood = 20;
        if (isPlayerFamily(m))
            log(imp, fullName(man) + " 成家後搬出去，自立門戶，向領主租了 " + std::to_string(int(fresh.acres)) +
                       " 畝地。");
    }
}

int World::askPlayer(const std::string& title, const std::string& situation, const std::vector<std::string>& options) {
    if (!chooser_ || options.size() < 2) return -1;
    ChoicePrompt c{title, situation, options, {}};
    int r = chooser_(c);
    return r >= 0 && r < static_cast<int>(options.size()) ? r : -1;
}

void World::yearlyMarriages() {
    auto available = [&](const Person& p) {
        return p.alive() && p.household != kNone && (p.spouse == kNone || !people_[p.spouse].alive()) &&
               !p.flags.count("celibate") && p.occupation != "priest";
    };
    std::vector<Id> men;
    for (const auto& p : people_)
        if (available(p) && p.sex == Sex::Male && ageYears(p) >= 18 && ageYears(p) <= 50) men.push_back(p.id);
    // Deterministic shuffle.
    for (size_t i = men.size(); i > 1; --i) std::swap(men[i - 1], men[rngSocial_.range(0, int(i) - 1)]);

    for (Id mid : men) {
        Person& m = people_[mid];
        if (!available(m)) continue;
        double age = ageYears(m);
        double readiness = 0.25 + std::max(0.0, age - 22) * 0.08 + traitMod(m, "marriage") * 0.2;
        if (!rngSocial_.chance(std::min(0.9, readiness))) continue;

        Id best = kNone;
        double bestScore = -1e9;
        for (const auto& w : people_) {
            if (w.sex != Sex::Female || !available(w) || w.household == m.household) continue;
            double wa = ageYears(w);
            if (wa < 16 || wa > 40) continue;
            if (closeKin(mid, w.id)) continue;
            // (When both are heads and she heads the player's house, his household moves into hers.)
            bool lordly = gentry(mid) != gentry(w.id);
            if (lordly) continue;  // the manor family does not marry its tenants
            double score = opinion(mid, w.id) + opinion(w.id, mid) - std::abs(age - 4 - wa) * 2 +
                           rngSocial_.range(-10, 10);
            double wealthGap = std::abs(households_[m.household].acres - households_[w.household].acres);
            score -= wealthGap * 0.5;
            if (const Feud* f = feudBetween(m.dynasty, w.dynasty)) score -= f->heat;
            if (score > bestScore) {
                bestScore = score;
                best = w.id;
            }
        }
        if (best != kNone && bestScore > -25) {
            // A match for one of the player's unmarried children goes through the head.
            Id acting = actingHead();
            Id ours = inPlayerHouse(people_[best].household) ? best : inPlayerHouse(m.household) ? mid : kNone;
            if (ours != kNone && ours != acting) {
                Id other = ours == best ? mid : best;
                const Household& oh = households_[people_[other].household];
                std::string about = fullName(other) + "（" + std::to_string(int(ageYears(people_[other]))) + " 歲，家裡 " +
                                    std::to_string(int(oh.acres)) + " 畝地；你對他們 " + std::to_string(int(opinion(acting, other))) + "）";
                std::string sit = other == mid ? about + "託人來說親，想娶 " + fullName(best) + "。"
                                               : about + "家託人來說親，想把她嫁給 " + fullName(mid) + "。";
                if (askPlayer("說親", sit, {"答應", "回絕"}) == 1) {
                    recordDeed("refused_proposal", ours, other);
                    log(1, "〔說親〕" + sit.substr(0, sit.find("。") + 3) + "（選擇：回絕）");
                    continue;
                }
            }
            marry(mid, best);
            continue;
        }
        // Nobody suitable at home: bring a bride from another village.
        if (age > 23 && rngSocial_.chance(0.5)) {
            std::string husbandOcc = m.occupation;
            Id dyn = addDynasty(defs_.surnames[rngNames_.range(0, int(defs_.surnames.size()) - 1)]);
            int bdays = day_ - static_cast<int>((age - rngSocial_.range(2, 7)) * kDaysPerYear);
            Id wid = addPerson(pickName(Sex::Female, kNone, kNone), dyn, Sex::Female, bdays, kNone);
            Person& w = people_[wid];
            w.occupation = husbandOcc == "lord" ? "lady" : "housewife";
            rollCongenital(w);
            rollPersonality(w, 2);
            w.health = rngSocial_.range(70, 95);
            w.skills[Farming] = rngSocial_.range(4, 10);
            w.skills[Charm] = rngSocial_.range(2, 10);
            marry(mid, wid);
        }
    }

    // A woman heading the player's house with no match at home may take a husband from another
    // village, who marries in and carries the family name on through her.
    Id ph = playerHead();
    if (ph != kNone && people_[ph].sex == Sex::Female && available(people_[ph]) && ageYears(people_[ph]) >= 18 &&
        ageYears(people_[ph]) <= 38 && rngSocial_.chance(0.35)) {
        int r = askPlayer("入贅", "鄰村有個後生託媒人來說，願意入贅 " + dynasties_[playerDynasty_].surname + " 家，幫著種地。",
                          {"答應", "再等等"});
        if (r != 1) {
            Id dyn = addDynasty(defs_.surnames[rngNames_.range(0, int(defs_.surnames.size()) - 1)]);
            int bdays = day_ - static_cast<int>((ageYears(people_[ph]) + rngSocial_.range(0, 6)) * kDaysPerYear);
            Id hid = addPerson(pickName(Sex::Male, kNone, kNone), dyn, Sex::Male, bdays, playerHousehold_);
            Person& g = people_[hid];
            g.occupation = "peasant";
            rollCongenital(g);
            rollPersonality(g, 2);
            g.health = rngSocial_.range(70, 95);
            g.skills[Farming] = rngSocial_.range(6, 12);
            marry(hid, ph);
        }
    }

    // Unmarried women past their mid-twenties often marry out of the village.
    for (auto& w : people_) {
        if (w.sex != Sex::Female || !available(w) || ageYears(w) < 22 || ageYears(w) > 35) continue;
        if (w.household != kNone && households_[w.household].head == w.id) continue;
        if (!rngSocial_.chance(0.3)) continue;
        if (inPlayerHouse(w.household) && w.id != actingHead() &&
            askPlayer("外村來的媒人", "外村有戶人家託媒人來說，想娶 " + fullName(w.id) + "。嫁過去就很少回來了。",
                      {"答應", "留她在家"}) == 1)
            continue;
        chargeMerchet(w.id, true);
        Household& from = households_[w.household];
        double dowry = std::min(from.money * 0.15, 80.0);
        from.money -= dowry;
        book(from, "嫁妝", -dowry);
        from.members.erase(std::remove(from.members.begin(), from.members.end(), w.id), from.members.end());
        if (isPlayerFamily(w)) log(inPlayerHouse(w.household) ? 1 : 2, fullName(w.id) + " 嫁到了外村。");
        w.station.clear();
        noteLife(w.id, "嫁到了外村");
        w.household = kNone;
        w.flags.insert("married_away");
    }
}

void World::yearlyAdulthood() {
    for (auto& p : people_) {
        if (!p.alive() || p.household == kNone) continue;
        double age = ageYears(p);
        bool family = isPlayerFamily(p);
        int personality = 0;
        for (const auto& t : p.traits)
            if (defs_.traits.at(t).category == "personality") ++personality;

        if (age >= 12 && personality == 0) {
            rollPersonality(p, 2);
            if (family) {
                std::string ts;
                for (const auto& t : p.traits)
                    if (defs_.traits.at(t).category == "personality") ts += "、" + defs_.traits.at(t).name;
                // A child who was beaten and grew up timid or quick-tempered: say so.
                std::string why;
                bool marked = hasTrait(p, "craven") || hasTrait(p, "wrathful") || hasTrait(p, "shy") || hasTrait(p, "melancholy");
                for (const auto& f : p.flags)
                    if (marked && why.empty() && f.rfind("beaten_by_", 0) == 0) {
                        Id by = static_cast<Id>(std::stoi(f.substr(10)));
                        why = "（小時候常挨" + (by == p.father ? std::string("父親") : by == p.mother ? std::string("母親") : fullName(by)) + "的打）";
                    }
                log(inPlayerHouse(p.household) ? 1 : 2, fullName(p.id) + " 長成了一個" + ts.substr(3) + "的孩子" + why + "。");
            }
        }
        if (age >= 25 && personality == 2 && rngLife_.chance(0.3)) rollPersonality(p, 1);

        if (age >= 14 && p.occupation.empty()) {
            std::string occ;
            for (const auto& f : p.flags)
                if (f.rfind("apprentice_", 0) == 0 && f.rfind("apprentice_at_", 0) != 0) occ = f.substr(11);
            if (occ.empty()) {
                if (p.sex == Sex::Female) {
                    occ = "housewife";
                } else {
                    Id elder = p.father != kNone && people_[p.father].alive() ? p.father : households_[p.household].head;
                    occ = elder != kNone && elder != p.id ? people_[elder].occupation : "";
                    // A son who grows up in a house with a trade takes it up, even with his father gone.
                    const std::string& trade = households_[p.household].trade;
                    if (!trade.empty() && (occ.empty() || occ == "housewife" || occ == "peasant") &&
                        p.id == households_[p.household].head)
                        occ = trade;
                    if (occ == "housewife" || occ == "lady" || occ.empty()) occ = "peasant";
                    if (occ == "lord") occ = "lord";
                }
            }
            // A son does not take up his widowed mother's brewing (釀酒婦), nor a daughter her father's forge.
            if (const OccupationDef* od = defs_.occupation(occ)) {
                if (od->sex == "female" && p.sex == Sex::Male) occ = "peasant";
                if (od->sex == "male" && p.sex == Sex::Female) occ = "housewife";
            }
            p.occupation = occ;
            if (p.station == "apprentice") setStation(p.id, "home", 0, "出師，當" + defs_.occupation(occ)->name);
            if (family && p.sex == Sex::Male)
                log(inPlayerHouse(p.household) ? 1 : 2, fullName(p.id) + " 十四歲了，開始當" + defs_.occupation(occ)->name + "。");
        } else if (age >= 14 && p.station == "apprentice" && day_ - p.stationSince >= 3 * kDaysPerYear) {
            // Apprenticed late, after already working: three years, then the trade is his.
            std::string occ;
            for (const auto& f : p.flags)
                if (f.rfind("apprentice_", 0) == 0 && f.rfind("apprentice_at_", 0) != 0) occ = f.substr(11);
            if (!occ.empty() && defs_.occupation(occ)) p.occupation = occ;
            const OccupationDef* od = defs_.occupation(p.occupation);
            setStation(p.id, "home", 0, "出師，當" + (od ? od->name : std::string("手藝人")));
            if (family) log(inPlayerHouse(p.household) ? 1 : 2, fullName(p.id) + " 出師了，從此當" + (od ? od->name : std::string("手藝人")) + "。");
        }
        if (family && households_[p.household].head == p.id && p.ambition.empty() && age >= 16 &&
            inPlayerHouse(p.household))
            chooseAmbition(p);
    }
}

void World::growSkills() {
    for (auto& p : people_) {
        if (!p.alive() || (p.household == kNone && !isAway(p))) continue;
        double age = ageYears(p);
        if (age < 5) continue;
        double learn = std::max(0.2, 1 + traitMod(p, "skill_gain"));
        const OccupationDef* od = defs_.occupation(p.occupation);
        // Away at school or at a trade in town: years there are what the exams at the end weigh.
        bool town = p.station == "town_apprentice" || p.station == "town_journeyman" || p.station == "town_master";
        if (p.station == "scholar") {
            p.skills[Learning] += 0.1 * learn;
        } else if (town) {
            p.skills[skillFromKey("craft")] += 0.08 * learn;
        } else if (od && age >= 14) {
            int s = skillFromKey(od->skill);
            p.skills[s] += 0.07 * learn * (1 + traitMod(p, std::string("skill_") + od->skill));
        } else if (age < 14) {
            // Children learn from the parent of the same sex.
            Id teacher = p.sex == Sex::Male ? p.father : p.mother;
            if (teacher != kNone && people_[teacher].alive()) {
                const OccupationDef* tod = defs_.occupation(people_[teacher].occupation);
                int s = tod ? skillFromKey(tod->skill) : Farming;
                p.skills[s] += 0.04 * learn * (people_[teacher].skills[s] / 10);
            }
            // What sticks from the priest's first lessons; the reading itself comes from the focus (跟神父認字).
            if (p.flags.count("schooled")) p.skills[Learning] += 0.02 * learn;
        }
        p.skills[Charm] += 0.015 * std::max(0.0, 1 + traitMod(p, "charm"));
        if (age > 60)
            for (auto& s : p.skills) s -= 0.03;
        for (auto& s : p.skills) s = std::clamp(s, 0.0, 20.0);
    }
}

}  // namespace sim
