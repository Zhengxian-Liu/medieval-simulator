#include <algorithm>

#include "simcore/world.h"

namespace sim {

Id World::addDynasty(const std::string& surname) {
    Dynasty d;
    d.id = static_cast<Id>(dynasties_.size());
    d.surname = surname;
    dynasties_.push_back(d);
    return d.id;
}

Id World::addHousehold(Id dynasty, double acres, double money) {
    Household h;
    h.id = static_cast<Id>(households_.size());
    h.dynasty = dynasty;
    h.acres = acres;
    h.money = money;
    households_.push_back(h);
    return h.id;
}

Id World::addPerson(const std::string& name, Id dynasty, Sex sex, int birthDay, Id household) {
    Person p;
    p.id = static_cast<Id>(people_.size());
    p.firstName = name;
    p.dynasty = dynasty;
    p.sex = sex;
    p.birthDay = birthDay;
    p.household = household;
    p.needs.fill(70);
    people_.push_back(p);
    memories_.emplace_back();
    beliefs_.emplace_back();
    if (household != kNone) households_[household].members.push_back(p.id);
    return p.id;
}

Id World::addItem(const std::string& type, Id maker, Id household) {
    Item it;
    it.id = static_cast<Id>(items_.size());
    it.type = type;
    it.maker = maker;
    it.madeDay = day_;
    it.household = household;
    if (household != kNone) {
        Id head = households_[household].head;
        if (head != kNone) it.owners.push_back(head);
        households_[household].items.push_back(it.id);
    }
    items_.push_back(it);
    return it.id;
}

void World::rollPersonality(Person& p, int count) {
    // A child brought up close (fond of a parent or the head, praised and taught) seldom grows up mean or grasping: the
    // home shows in the grown character (fun-r12 A7, seed 195: Matilda at +100 「長成了一個貪婪、刻薄的孩子」).
    double warmth = -100;
    if (ageYears(p) < 18 && playerHousehold_ != kNone && isPlayerFamily(p)) {
        for (Id par : {p.father, p.mother, playerHead()})
            if (par != kNone && par != p.id && people_[par].alive()) warmth = std::max(warmth, opinion(p.id, par));
    }
    const bool close = warmth >= 60;
    for (int n = 0; n < count; ++n) {
        std::vector<std::string> pool;
        std::vector<double> weights;
        for (const auto& id : defs_.traitOrder) {
            const TraitDef& t = defs_.traits.at(id);
            if (t.category != "personality" || hasTrait(p, id)) continue;
            if (!t.opposite.empty() && hasTrait(p, t.opposite)) continue;
            double w = t.weight;
            // Children tend to take after their parents.
            for (Id par : {p.father, p.mother})
                if (par != kNone && hasTrait(people_[par], id)) w += 3;
            if (p.flags.count("childhood_" + id)) w += 6;
            if (close && (t.opposite == "kind" || t.opposite == "generous")) w *= 0.3;
            if (close && (id == "kind" || id == "generous")) w += 3;
            pool.push_back(id);
            weights.push_back(w);
        }
        int pick = rngLife_.weighted(weights);
        if (pick >= 0) p.traits.push_back(pool[pick]);
    }
}

void World::rollCongenital(Person& p) {
    for (const auto& id : defs_.traitOrder) {
        const TraitDef& t = defs_.traits.at(id);
        if (t.category != "congenital" || hasTrait(p, id)) continue;
        if (!t.opposite.empty() && hasTrait(p, t.opposite)) continue;
        double c = 0.03 * t.weight;
        for (Id par : {p.father, p.mother})
            if (par != kNone && hasTrait(people_[par], id)) c += t.inheritChance;
        if (!rngLife_.chance(c)) continue;
        // Born with it, but it is only a trait once they are grown (yearlyAdulthood brings it out).
        if (t.fromAge > 0 && ageYears(p) < t.fromAge) p.flags.insert("latent:" + id);
        else p.traits.push_back(id);
    }
}

void World::setupVillage() {
    const Json& v = defs_.village;
    std::string origin = cfg_.origin.empty() ? v.stringOr("default_origin", "peasant") : cfg_.origin;

    auto randAgeDays = [&](double years) {
        return -static_cast<int>(years * kDaysPerYear) - rngLife_.range(0, kDaysPerYear - 1);
    };

    for (const auto& t : v["households"].asArray()) {
        Id dyn = addDynasty(t["surname"].asString());
        Id h = addHousehold(dyn, t.numberOr("acres", 0), t.numberOr("money", 60));
        households_[h].freehold = std::min(households_[h].acres, t.numberOr("freehold", 0));
        Household& hh = households_[h];
        hh.house = t.stringOr("house", "cottage");
        std::string occ = t.stringOr("occupation", "peasant");

        int headAge = rngLife_.range(t["head_age"][0].asInt(), t["head_age"][1].asInt());
        Id head = addPerson(pickName(Sex::Male, kNone, kNone), dyn, Sex::Male, randAgeDays(headAge), h);
        households_[h].head = head;
        people_[head].occupation = occ;
        dynasties_[dyn].heads.push_back(head);
        dynasties_[dyn].headSince.push_back(day_);
        dynasties_[dyn].headStart.push_back({households_[h].acres, households_[h].money});

        Id wife = kNone;
        bool married = !t["married"].isBool() || t["married"].asBool();
        if (married) {
            int wifeAge = std::max(17, headAge - rngLife_.range(1, 8));
            wife = addPerson(pickName(Sex::Female, kNone, kNone), dyn, Sex::Female, randAgeDays(wifeAge), h);
            people_[wife].occupation = occ == "lord" ? "lady" : "housewife";
            people_[head].spouse = wife;
            people_[wife].spouse = head;

            int kids = rngLife_.range(t["children"][0].asInt(), t["children"][1].asInt());
            double age = std::min<double>(wifeAge - 17, rngLife_.range(1, 4) + kids * 2.5);
            for (int k = 0; k < kids && age >= 0; ++k) {
                Sex s = rngLife_.chance(0.5) ? Sex::Male : Sex::Female;
                Id c = addPerson(pickName(s, head, wife), dyn, s, randAgeDays(age), h);
                people_[c].father = head;
                people_[c].mother = wife;
                people_[head].children.push_back(c);
                people_[wife].children.push_back(c);
                people_[wife].lastBirthDay = people_[c].birthDay;
                age -= rngLife_.range(2, 3);
            }
        }
        if (t["elder"].isBool() && t["elder"].asBool()) {
            Id grandma = addPerson(pickName(Sex::Female, kNone, kNone), dyn, Sex::Female,
                                   randAgeDays(headAge + rngLife_.range(20, 26)), h);
            people_[grandma].occupation = "housewife";
            people_[grandma].children.push_back(head);
            people_[head].mother = grandma;
        }

        for (Id m : households_[h].members) {
            Person& p = people_[m];
            double age = ageYears(p);
            rollCongenital(p);
            if (age >= 12) rollPersonality(p, age >= 25 ? 3 : 2);
            double scale = std::min(1.0, age / 25.0);
            for (int s = 0; s < kSkillCount; ++s) p.skills[s] = rngLife_.range(1, 5) * scale;
            const OccupationDef* od = defs_.occupation(p.occupation);
            if (od) p.skills[skillFromKey(od->skill)] = rngLife_.range(8, 14) * scale;
            if (p.sex == Sex::Female) p.skills[Farming] = std::max(p.skills[Farming], 5 * scale);
            p.skills[Charm] += rngLife_.range(0, 5) * scale;
            p.health = rngLife_.range(75, 95);
            p.reputation = rngLife_.range(-5, 10);
        }

        Household& hh2 = households_[h];
        hh2.food = consumption(hh2) * 200;
        hh2.firewood = 40;
        for (const auto& it : t["items"].asArray()) addItem(it.asString(), kNone, h);

        std::string role = t.stringOr("role", "");
        // Lords, clergy and some craftsmen are free; most tenants are villeins bound to the manor.
        bool free = t["free"].isBool() ? t["free"].asBool() : (role == "lord" || role == "priest");
        for (Id m : hh2.members) people_[m].free = free;
        if (role == "lord") lord_ = head;
        if (role == "priest") priest_ = head;
        if (t.stringOr("origin", "") == origin && playerHousehold_ == kNone) {
            playerHousehold_ = h;
            playerDynasty_ = dyn;
        }
    }
    if (playerHousehold_ == kNone) throw JsonError("village.json has no household for origin " + origin);

    // Neighbours start out with some history between them.
    auto all = livingInVillage();
    for (Id a : all)
        for (int k = 0; k < 3; ++k) {
            Id b = all[rngSocial_.range(0, static_cast<int>(all.size()) - 1)];
            if (a == b || people_[a].household == people_[b].household) continue;
            double val = rngSocial_.range(-15, 20);
            addOpinion(a, b, "old_acquaintance", val, 0);
            addOpinion(b, a, "old_acquaintance", val * 0.8, 0);
        }

    villageName_ = v.stringOr("name", "村莊");
    Person& head = people_[households_[playerHousehold_].head];
    log(1, "【開局】" + std::to_string(year()) + " 年，" + villageName_ + "。" +
               fullName(head.id) + "（" + std::to_string(int(ageYears(head))) + " 歲，" +
               defs_.occupation(head.occupation)->name + "）是一家之主。");
    // Every head wants something; the player's head chooses on the first day, when there is someone to ask.
    pendingAmbition_ = true;
    for (const auto& hh : households_)
        if (!inPlayerHouse(hh.id) && hh.head != kNone && ageYears(people_[hh.head]) >= 16) chooseAmbition(people_[hh.head]);
    for (const auto& hh : households_) lastStanding_[hh.id] = standing(hh.id);
}

std::string World::pickName(Sex sex, Id father, Id mother) {
    const auto& list = sex == Sex::Male ? defs_.maleNames : defs_.femaleNames;
    // Medieval English families reused names: often after a parent or grandparent.
    Id namesake = kNone;
    if (father != kNone && mother != kNone && rngNames_.chance(0.45)) {
        Id src = sex == Sex::Male ? father : mother;
        std::vector<Id> cands{src};
        const Person& f = people_[father];
        const Person& m = people_[mother];
        for (Id g : {f.father, f.mother, m.father, m.mother})
            if (g != kNone && people_[g].sex == sex) cands.push_back(g);
        namesake = cands[rngNames_.range(0, static_cast<int>(cands.size()) - 1)];
    }
    // A living brother or sister already has the name: pick another.
    auto taken = [&](const std::string& n) {
        if (mother == kNone) return false;
        for (Id c : people_[mother].children)
            if (people_[c].alive() && people_[c].firstName == n) return true;
        return false;
    };
    if (namesake != kNone && people_[namesake].sex == sex && !taken(people_[namesake].firstName))
        return people_[namesake].firstName;
    std::string name;
    for (int i = 0; i < 8; ++i) {
        name = list[rngNames_.range(0, static_cast<int>(list.size()) - 1)];
        if (!taken(name)) break;
    }
    return name;
}

}  // namespace sim
