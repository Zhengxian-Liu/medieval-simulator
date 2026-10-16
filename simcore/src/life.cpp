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

    // People in households the player could continue with: the player's own and its branches.
    auto inLine = [&](const Person& p) {
        if (p.household == kNone) return false;
        const Household& hh = households_[p.household];
        if (hh.id == playerHousehold_) return true;
        return !hh.extinct && hh.dynasty == playerDynasty_ && hh.head != kNone &&
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
            log(people_[motherId].household == playerHousehold_ ? 1 : 2, fullName(motherId) + " 生下了" + (s == Sex::Male ? "兒子 " : "女兒 ") + fullName(c) +
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
            if (people_[c].alive() && ageYears(people_[c]) >= 14 && people_[c].home == kNone &&
                (bedside == kNone || (people_[c].household == p.household && people_[bedside].household != p.household) ||
                 (people_[c].household == people_[bedside].household && people_[c].birthDay < people_[bedside].birthDay)))
                bedside = c;
    if (bedside != kNone) runEventById("deathbed_wishes", id, bedside);
    else if (lingering) deathbedConfession(id);
    p.deathDay = day_;
    p.deathCause = cause;
    p.pregnantDue = -1;
    Id hid = p.household;
    bool family = isPlayerFamily(p);
    double age = ageYears(p);
    if (family || (hid != kNone && households_[hid].head == id))
        log(family && hid == playerHousehold_ ? 1 : 2,
            age < 1 ? fullName(id) + " 出生沒多久就夭折了。"
                    : fullName(id) + " 因" + cause + "去世，享年 " + std::to_string(int(age)) + " 歲。");
    if (hid == playerHousehold_ && hid != kNone && households_[hid].head == id) obituary(id);

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
    if (hid != kNone) addNews(age < 1 ? "infant_death" : age < 14 ? "child_death" : "death", id, kNone, hid);
    inheritMemories(id);
    if (hid == kNone) return;
    Household& h = households_[hid];
    emit({"death", kNone, id, p.spouse != kNone && people_[p.spouse].alive() ? p.spouse : kNone, h.head == id});
    h.members.erase(std::remove(h.members.begin(), h.members.end(), id), h.members.end());
    if (h.head == id) {
        onHeadDied(h, id);
        if (id == lord_) {
            // The manor passes to the lord's heir, who carries his father's grudges at half strength like any heir.
            if (!h.extinct && h.head != kNone && h.head != id) {
                // Debts owed to the old lord are owed to the manor, and so to the new one.
                for (auto& oh : households_) {
                    if (oh.creditor == id) oh.creditor = h.head;
                    if (oh.heldCreditor == id) oh.heldCreditor = h.head;
                }
                lord_ = h.head;
                if (people_[lord_].sex == Sex::Male) people_[lord_].occupation = "lord";
                log(1, "【領主】" + fullName(id) + " 去世了，" + fullName(lord_) + " 繼承了莊園，成了新的領主。");
            }
            return;
        }
        if (!h.extinct && h.head != kNone) chargeHeriot(h, id);
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

// Who inherits from `dead`, head of `h`, by English custom: eldest son, then a dead son's son,
// then eldest daughter, then a brother, then the widow, then anyone left in the house.
Id World::heirOf(const Household& h, Id dead) const {
    const Person& d = people_[dead];
    auto inHouse = [&](Id x) {
        return x != kNone && people_[x].alive() && people_[x].household == h.id;
    };
    Id heir = kNone;
    auto eldest = [&](const std::vector<Id>& pool, Sex want) {
        Id best = kNone;
        for (Id c : pool)
            if (inHouse(c) && people_[c].sex == want && (best == kNone || people_[c].birthDay < people_[best].birthDay))
                best = c;
        return best;
    };
    heir = eldest(d.children, Sex::Male);
    if (heir == kNone) {
        std::vector<Id> grandsons;
        for (Id c : d.children)
            for (Id g : people_[c].children) grandsons.push_back(g);
        heir = eldest(grandsons, Sex::Male);
    }
    if (heir == kNone) heir = eldest(d.children, Sex::Female);
    if (heir == kNone && d.father != kNone) heir = eldest(people_[d.father].children, Sex::Male);
    if (heir == kNone && inHouse(d.spouse)) heir = d.spouse;
    if (heir == kNone)
        for (Id m : h.members)
            if (m != dead && people_[m].alive() && people_[m].home == kNone &&
                (heir == kNone || people_[m].birthDay < people_[heir].birthDay))
                heir = m;
    return heir;
}

void World::onHeadDied(Household& h, Id dead) {
    recallServants(h.id);
    Id heir = heirOf(h, dead);

    bool player = h.id == playerHousehold_;
    if (heir == kNone) {
        h.extinct = true;
        h.head = kNone;
        // Servants living here go back to their own families.
        for (Id m : std::vector<Id>(h.members))
            if (people_[m].home != kNone) {
                Id home = people_[m].home;
                people_[m].home = kNone;
                people_[m].flags.erase("in_service");
                moveTo(m, home);
            }
        vacantAcres_ += h.acres;
        log(2, dynasties_[h.dynasty].surname + " 家絕戶了，" + std::to_string(int(h.acres)) +
                   " 畝地回到領主手上。");
        h.acres = h.freehold = 0;
        for (Id it : h.items) items_[it].lost = true;
        if (player) followSurvivingBranch();
        return;
    }

    h.head = heir;
    // The house goes on, but to someone not of the player's blood (a widower who married in, a
    // stepchild): the family line ends here unless another branch carries it.
    if (player && people_[heir].dynasty != playerDynasty_ &&
        dynasties_[people_[heir].dynasty].surname != dynasties_[playerDynasty_].surname) {
        // First, a son or kinsman of the blood living elsewhere (in service, lodging with in-laws)
        // comes home to take it up.
        Id kin = kNone;
        for (const auto& q : people_) {
            if (!q.alive() || q.id == dead || q.dynasty != playerDynasty_ || q.household == kNone || ageYears(q) < 12) continue;
            if (q.household != h.id && households_[q.household].head == q.id) continue;  // heads their own house: a branch
            bool child = q.father == dead || q.mother == dead;
            bool better = kin == kNone || (child && !(people_[kin].father == dead || people_[kin].mother == dead)) ||
                          ((child == (people_[kin].father == dead || people_[kin].mother == dead)) && q.birthDay < people_[kin].birthDay);
            if (better) kin = q.id;
        }
        if (kin != kNone) {
            if (people_[kin].household != h.id) {
                people_[kin].home = kNone;
                people_[kin].flags.erase("in_service");
                moveTo(kin, h.id);
                log(1, fullName(kin) + " 回到家裡，接下了這份地。");
            }
            heir = kin;
            h.head = heir;
        }
    }
    if (player && people_[heir].dynasty != playerDynasty_ &&
        dynasties_[people_[heir].dynasty].surname != dynasties_[playerDynasty_].surname) {
        log(1, "房子和地傳給了 " + fullName(heir) + "，可是" + fullName(heir) + "不姓 " + dynasties_[playerDynasty_].surname + "。");
        h.dynasty = people_[heir].dynasty;
        followSurvivingBranch(true);
        return;
    }
    for (Id it : h.items) items_[it].owners.push_back(heir);
    if (!player) return;

    Person& hp = people_[heir];
    dynasties_[playerDynasty_].heads.push_back(heir);
    dynasties_[playerDynasty_].headSince.push_back(day_);
    std::string line = "【繼承】" + fullName(heir) + "（" + std::to_string(int(ageYears(hp))) + " 歲）成為新的家主。";
    const Person& d = people_[dead];
    if (ageYears(hp) < 16 && d.spouse != kNone && people_[d.spouse].alive() && people_[d.spouse].household == h.id)
        line += std::string(hp.sex == Sex::Male ? "他" : "她") + "還年幼，由母親 " + fullName(d.spouse) + " 代為持家。";
    log(1, line);
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
}

void World::followSurvivingBranch(bool lineEnded) {
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
            if (x == &m && x->household != kNone && households_[x->household].head != man && !households_[home].extinct)
                moveTo(man, home);
        }
    recordDeed("married", man, woman);
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
    int imp = m.household == playerHousehold_ || w.household == playerHousehold_ ? 1 : 2;

    Id oldHouse = w.household;
    bool heiress = oldHouse != kNone && households_[oldHouse].head == woman;
    // A family with no son keeps one daughter at home; her husband marries in to carry the land.
    if (!heiress && oldHouse == playerHousehold_ && households_[m.household].head != man) {
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
    if (!heiress && oldHouse == playerHousehold_ && households_[oldHouse].head != kNone &&
        heirOf(households_[oldHouse], households_[oldHouse].head) == woman)
        heiress = true;
    if (heiress && households_[m.household].head == man) {
        // Two heads marry: her household, land and goods join his, unless hers is the player's,
        // in which case he brings his into hers so the player's line goes on.
        bool intoHers = oldHouse == playerHousehold_;
        Household& from = households_[intoHers ? m.household : oldHouse];
        Household& to = households_[intoHers ? oldHouse : m.household];
        for (Id x : from.members) {
            people_[x].household = to.id;
            to.members.push_back(x);
        }
        for (Id it : from.items) {
            items_[it].household = to.id;
            to.items.push_back(it);
        }
        to.acres += from.acres;
        to.freehold += from.freehold;
        to.money += from.money;
        to.food += from.food;
        from.members.clear();
        from.items.clear();
        from.acres = from.freehold = from.money = from.food = 0;
        from.head = kNone;
        from.extinct = true;
        if (intoHers) {
            if (family) log(imp, fullName(man) + " 娶了 " + fullName(woman) + "，帶著家當搬進她家，兩家合為一戶。");
            return;
        }
        if (family) log(imp, fullName(man) + " 娶了寡婦 " + fullName(woman) + "，兩家合為一戶。");
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
        households_[m.household].money += dowry;
        from.members.erase(std::remove(from.members.begin(), from.members.end(), woman), from.members.end());
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
            vacantAcres_ += from.acres;
            from.acres = from.freehold = 0;
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
        bool isHeir = true;
        const Person& head = people_[home.head];
        for (Id c : head.children)
            if (c != man && people_[c].alive() && people_[c].sex == Sex::Male &&
                people_[c].birthDay < m.birthDay && people_[c].household == home.id)
                isHeir = false;
        if (home.head == m.father && isHeir) return;  // the heir stays on the family holding
        double land = std::min(vacantAcres_, rngSocial_.chance(0.5) ? 10.0 : 5.0);
        if (land < 2 && !rngSocial_.chance(0.5)) return;
        vacantAcres_ -= land;
        Id nh = addHousehold(m.dynasty, std::max(land, 1.0), home.money * 0.15);
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
            Id ours = people_[best].household == playerHousehold_ ? best : m.household == playerHousehold_ ? mid : kNone;
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
        if (w.household == playerHousehold_ && w.id != actingHead() &&
            askPlayer("外村來的媒人", "外村有戶人家託媒人來說，想娶 " + fullName(w.id) + "。嫁過去就很少回來了。",
                      {"答應", "留她在家"}) == 1)
            continue;
        chargeMerchet(w.id, true);
        Household& from = households_[w.household];
        double dowry = std::min(from.money * 0.15, 80.0);
        from.money -= dowry;
        from.members.erase(std::remove(from.members.begin(), from.members.end(), w.id), from.members.end());
        if (isPlayerFamily(w)) log(w.household == playerHousehold_ ? 1 : 2, fullName(w.id) + " 嫁到了外村。");
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
                log(p.household == playerHousehold_ ? 1 : 2, fullName(p.id) + " 長成了一個" + ts.substr(3) + "的孩子" + why + "。");
            }
        }
        if (age >= 25 && personality == 2 && rngLife_.chance(0.3)) rollPersonality(p, 1);

        if (age >= 14 && p.occupation.empty()) {
            std::string occ;
            for (const auto& f : p.flags)
                if (f.rfind("apprentice_", 0) == 0) occ = f.substr(11);
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
            p.occupation = occ;
            if (family && p.sex == Sex::Male)
                log(p.household == playerHousehold_ ? 1 : 2, fullName(p.id) + " 十四歲了，開始當" + defs_.occupation(occ)->name + "。");
        }
        if (family && households_[p.household].head == p.id && p.ambition.empty() && age >= 16 &&
            p.household == playerHousehold_)
            chooseAmbition(p);
    }
}

void World::growSkills() {
    for (auto& p : people_) {
        if (!p.alive() || p.household == kNone) continue;
        double age = ageYears(p);
        if (age < 5) continue;
        double learn = std::max(0.2, 1 + traitMod(p, "skill_gain"));
        const OccupationDef* od = defs_.occupation(p.occupation);
        if (od && age >= 14) {
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
            if (p.flags.count("schooled")) p.skills[Learning] += 0.06 * learn;
        }
        p.skills[Charm] += 0.015 * std::max(0.0, 1 + traitMod(p, "charm"));
        if (age > 60)
            for (auto& s : p.skills) s -= 0.03;
        for (auto& s : p.skills) s = std::clamp(s, 0.0, 20.0);
    }
}

}  // namespace sim
