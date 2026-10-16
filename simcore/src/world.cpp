#include "simcore/world.h"

#include <algorithm>
#include <cmath>

#include "simcore/events.h"

namespace sim {

namespace {
const char* kSkillKeys[] = {"farming", "craft", "learning", "charm"};
const char* kNeedKeys[] = {"hunger", "rest", "warmth", "social", "spirit"};
}  // namespace

const char* skillKey(int s) { return kSkillKeys[s]; }
const char* needKey(int n) { return kNeedKeys[n]; }
int skillFromKey(const std::string& k) {
    for (int i = 0; i < kSkillCount; ++i)
        if (k == kSkillKeys[i]) return i;
    throw JsonError("unknown skill " + k);
}
int needFromKey(const std::string& k) {
    for (int i = 0; i < kNeedCount; ++i)
        if (k == kNeedKeys[i]) return i;
    throw JsonError("unknown need " + k);
}

World::World(const Defs& defs, Config cfg)
    : defs_(defs),
      cfg_(cfg),
      rngLife_(cfg.seed, hashString("life")),
      rngEcon_(cfg.seed, hashString("economy")),
      rngSocial_(cfg.seed, hashString("social")),
      rngEvents_(cfg.seed, hashString("events")),
      rngNames_(cfg.seed, hashString("names")),
      rngPlans_(cfg.seed, hashString("plans")),
      rngKnow_(cfg.seed, hashString("knowledge")) {
    rngMinds_.reseed(cfg.seed, hashString("minds"));
    rngWants_.reseed(cfg.seed, hashString("wants"));
    rngNews_.reseed(cfg.seed, hashString("news"));
    baseParams_ = {
        {"mortality_mult", 1.0}, {"harvest_mult", 1.0}, {"grain_price", 0.4},
        {"wage_mult", 1.0},      {"rent_mult", 1.0},    {"plague_monthly", 0.0},
        {"war_levy", 0.0},
    };
    // Apply the permanent effects of everything that happened before the start year,
    // so a 1360 start already has post-plague wages.
    while (nextHistory_ < defs_.history.size() && defs_.history[nextHistory_].year < cfg_.startYear) {
        const auto& h = defs_.history[nextHistory_++];
        for (const auto& e : h.effects.asArray()) {
            if (!e["permanent"].isBool() || !e["permanent"].asBool()) continue;
            std::string p = e["param"].asString();
            if (e.has("mult")) baseParams_[p] *= e["mult"].asNumber();
            else if (e.has("value")) baseParams_[p] = e["value"].asNumber();
        }
        if (h.effects.isArray())
            for (const auto& e : h.effects.asArray())
                if (e.has("world_flag")) worldFlags_.insert(e["world_flag"].asString());
    }
    setupVillage();
    seedAcquaintance();
    // Each game opens on one of a few situations the family is already in (data/events/openings.json).
    std::vector<std::string> openings;
    for (const auto& ev : defs_.events)
        if (ev.id.rfind("opening_", 0) == 0) openings.push_back(ev.id);
    // It runs on the first day rather than here, so an interactive chooser set after construction gets asked.
    if (!openings.empty()) pendingOpening_ = openings[rngEvents_.range(0, static_cast<int>(openings.size()) - 1)];
}

void World::runYears(int years) {
    int end = day_ + years * kDaysPerYear;
    while (day_ < end && !gameOver_) stepDay();
}

void World::stepDay() {
    if (gameOver_) return;
    EventRunner events(*this);
    if (dayOfMonth() == 0 && month() % 3 == 0) rollSeason();
    processCommands();
    if (!pendingOpening_.empty()) {
        std::string id;
        std::swap(id, pendingOpening_);
        runEventById(id, playerHead(), kNone);
    }
    if (pendingAmbition_) {
        pendingAmbition_ = false;
        if (playerHead() != kNone && ageYears(people_[playerHead()]) >= 16) chooseAmbition(people_[playerHead()]);
        computeRings();
    }

    if (dayOfMonth() == 0) {
        applyHistory();
        if (month() == 0) {
            yearlyAdulthood();
            yearlyAmbitions();
            yearlyTown();
            yearlyIntentions();
            yearlyService();
            yearlyFeuds();
            yearlyLiaisons();
            events.pulse("yearly");
            pruneOpinions();
        }
        if (month() == 7) electReeve();
        if (month() == 8) harvest();
        if (month() == 9) {
            reletVacant();
            gatherFirewood();
            yearlyMarriages();
        }
        monthlyEconomy();
        monthlyHealth();
        monthlyBirths();
        monthlySocial();
        monthlyNews();
        monthlyRumours();
        monthlyStress();
        monthlyConcerns();
        growSkills();
        if (month() % 3 == 0) computeRings();
        monthlyPlans();
        npcStartPlans();
        actOnIntentions();
        checkStandings();
        if (playerHead() != kNone) checkMilestones(people_[playerHead()], false);
        events.pulse("monthly");
    }
    if (month() == 8 && dayOfMonth() == 29) {
        yearlyDebts();
        payRent();
    }

    if (!gameOver_) followSurvivingBranch();
    if (gameOver_) return;
    runScheduled();
    if (!gameOver_) followSurvivingBranch();
    if (gameOver_) return;
    dailyNeeds();
    for (size_t i = 0; i < people_.size(); ++i)
        if (people_[i].alive() && people_[i].pregnantDue == day_) {
            // A woman who has left the village (married away, fled) has her child out of the sim's sight.
            if (people_[i].household == kNone) people_[i].pregnantDue = -1;
            else giveBirth(people_[i]);
        }

    ++day_;
}

std::string World::fullName(Id id) const {
    if (id == kNone) return "（無）";
    const Person& p = people_[id];
    std::string s = p.firstName;
    // Families reuse names. Namesakes under one roof go by age: the eldest plain, the youngest "小",
    // any between "二", "三"… (a mother and two daughters: 塞西莉, 二塞西莉, 小塞西莉).
    int younger = 0;  // namesakes at home younger than this one (who take the 小)
    if (p.household != kNone) {
        int older = 0;
        for (Id m : households_[p.household].members) {
            if (m == id || people_[m].firstName != p.firstName || !people_[m].alive()) continue;
            if (people_[m].birthDay < p.birthDay || (people_[m].birthDay == p.birthDay && m < id)) ++older;
            else ++younger;
        }
        static const char* nth[] = {"二", "三", "四", "五", "六", "七", "八", "九", "十"};
        if (older > 0) s = (younger == 0 ? std::string("小") : std::string(nth[std::min(older, 9) - 1])) + s;
    }
    // The dead whose name lives on in the family (a son named for his father, a girl for the sister who
    // died before her): "老約翰" for the elder gone, "早夭的茱莉安娜" for a child.
    // Not while the death is still news (the notice, the wake, word going round): only once they are a memory.
    if (!p.alive() && p.dynasty != kNone && p.deathDay < day_ - kDaysPerMonth) {
        double age = static_cast<double>(p.deathDay - p.birthDay) / kDaysPerYear;
        for (const auto& o : people_)
            if (o.id != id && o.alive() && o.dynasty == p.dynasty && o.firstName == p.firstName) {
                // The younger one gone keeps the 小 the rules above gave (or none): only the elder is marked.
                if (o.birthDay > p.birthDay) s = (age < 5 ? "早夭的" : age >= 30 ? "老" : "已故的") + s;
                break;
            }
    }
    bool marked = s != p.firstName;  // already told apart above
    if (p.dynasty != kNone) s += "·" + dynasties_[p.dynasty].surname;
    // Another living namesake in another house: say whose child (or wife) this one is.
    if (p.alive() && p.household != kNone && p.dynasty != kNone) {
        bool clash = false;
        for (const auto& o : people_)
            if (o.id != id && o.alive() && o.household != kNone && o.household != p.household &&
                o.dynasty == p.dynasty && o.firstName == p.firstName && o.father != id && o.mother != id) {
                clash = true;
                break;
            }
        if (clash) {
            auto it = bynames_.find(id);
            if (it == bynames_.end()) {
                std::string by;
                if (p.father != kNone && people_[p.father].firstName == p.firstName) by = "小";
                else if (p.father != kNone) by = "（" + people_[p.father].firstName + (p.sex == Sex::Male ? " 的兒子）" : " 的女兒）");
                else if (p.spouse != kNone) by = "（" + people_[p.spouse].firstName + (p.sex == Sex::Male ? " 的丈夫）" : " 的妻子）");
                it = bynames_.emplace(id, by).first;
            }
            if (it->second == "小") {
                // Named for a father in another house, with a namesake child at home who is 小 already: 大.
                if (!marked) s = (younger > 0 ? "大" : "小") + s;
            } else if (!marked) {
                s += it->second;
            }
        }
    }
    return s;
}

std::string World::shortName(Id id) const {
    if (id == kNone) return "";
    // fullName without the surname (and whatever follows it), keeping 小 / 老 / 早夭的.
    std::string s = fullName(id);
    auto dot = s.find("·");
    return dot == std::string::npos ? s : s.substr(0, dot);
}

double World::opinion(Id from, Id to) const {
    if (from == to) return 100;
    const Person& a = people_[from];
    const Person& b = people_[to];
    double v = 0;
    // Shared and opposed personality traits.
    for (const auto& t : a.traits) {
        const TraitDef* td = defs_.trait(t);
        if (!td || td->category != "personality") continue;
        if (hasTrait(b, t)) v += 8;
        if (!td->opposite.empty() && hasTrait(b, td->opposite)) v -= 8;
    }
    v += traitMod(b, "attraction") * 10;
    v += kinBase(from, to);
    for (const auto& m : memories_[from]) {
        if (m.about == to) v += memoryValue(m);
        else if (m.aboutDynasty != kNone && m.aboutDynasty == b.dynasty) v += memoryValue(m) * 0.4;
    }
    return std::clamp(v, -100.0, 100.0);
}

bool World::hasTrait(const Person& p, const std::string& t) const {
    return std::find(p.traits.begin(), p.traits.end(), t) != p.traits.end();
}

double World::traitMod(const Person& p, const std::string& key) const {
    double sum = 0;
    for (const auto& t : p.traits) {
        const TraitDef* td = defs_.trait(t);
        if (!td) continue;
        auto it = td->mods.find(key);
        if (it != td->mods.end()) sum += it->second;
    }
    if (p.dynasty != kNone)
        for (const auto& tr : dynasties_[p.dynasty].traditions) {
            auto td = defs_.traditions.find(tr);
            if (td == defs_.traditions.end()) continue;
            auto it = td->second.mods.find(key);
            if (it != td->second.mods.end()) sum += it->second;
        }
    return sum;
}

double World::param(const std::string& name, double fallback) const {
    auto it = baseParams_.find(name);
    double v = it == baseParams_.end() ? fallback : it->second;
    for (const auto& e : effects_) {
        if (e.param != name || e.untilDay <= day_) continue;
        if (name == "plague_monthly" || name == "war_levy") v = std::max(v, e.value);
        else v *= e.value;
    }
    return v;
}

bool World::isPlayerFamily(const Person& p) const {
    return p.dynasty == playerDynasty_ && p.dynasty != kNone;
}

bool World::gentry(Id id) const {
    if (id == kNone || lord_ == kNone) return false;
    const Person& p = people_[id];
    const Person& lord = people_[lord_];
    return (p.dynasty != kNone && p.dynasty == lord.dynasty) ||
           (p.household != kNone && p.household == lord.household && p.home == kNone) || p.occupation == "lord" || p.occupation == "lady";
}

int World::swornFriends(Id id) const {
    std::set<Id> friends;
    for (const auto& m : memories_[id])
        if (m.label == "sworn_friend" && !m.inherited && m.about != kNone && people_[m.about].alive() &&
            people_[m.about].household != kNone)
            friends.insert(m.about);
    return static_cast<int>(friends.size());
}

bool World::closeKin(Id a, Id b) const {
    const Person& x = people_[a];
    const Person& y = people_[b];
    auto parents = [&](const Person& p) {
        std::vector<Id> v;
        if (p.father != kNone) v.push_back(p.father);
        if (p.mother != kNone) v.push_back(p.mother);
        return v;
    };
    auto grand = [&](const Person& p) {
        std::vector<Id> v = parents(p);
        for (Id par : parents(p))
            for (Id g : parents(people_[par])) v.push_back(g);
        return v;
    };
    for (Id g1 : grand(x)) {
        if (g1 == b) return true;
        for (Id g2 : grand(y))
            if (g1 == g2) return true;
    }
    for (Id g2 : grand(y))
        if (g2 == a) return true;
    return false;
}

int World::livingCount() const {
    int n = 0;
    for (const auto& p : people_) n += p.alive() ? 1 : 0;
    return n;
}

std::vector<Id> World::livingInVillage() const {
    std::vector<Id> v;
    for (const auto& p : people_)
        if (p.alive() && p.household != kNone) v.push_back(p.id);
    return v;
}

void World::log(int importance, const std::string& text) {
    chronicle_.push_back({day_, importance, text});
}

void World::applyHistory() {
    while (nextHistory_ < defs_.history.size()) {
        const auto& h = defs_.history[nextHistory_];
        if (h.year > year() || (h.year == year() && h.month > month())) break;
        ++nextHistory_;
        if (!h.text.empty()) log(3, h.text);
        applyHistoryEffects(h.effects);
    }
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                  [&](const ActiveEffect& e) { return e.untilDay <= day_; }),
                   effects_.end());
}

void World::applyHistoryEffects(const Json& effects) {
    if (!effects.isArray()) return;
    for (const auto& e : effects.asArray()) {
        if (e.has("world_flag")) {
            worldFlags_.insert(e["world_flag"].asString());
            continue;
        }
        std::string p = e["param"].asString();
        bool permanent = e["permanent"].isBool() && e["permanent"].asBool();
        if (permanent) {
            if (e.has("mult")) baseParams_[p] *= e["mult"].asNumber();
            else baseParams_[p] = e["value"].asNumber();
            continue;
        }
        double v = e.has("mult") ? e["mult"].asNumber() : e["value"].asNumber();
        int months = static_cast<int>(e.numberOr("months", 12));
        effects_.push_back({p, v, day_ + months * kDaysPerMonth});
    }
}

uint64_t World::digest() const {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    mix(uint64_t(day_));
    for (const auto& p : people_) {
        mix(uint64_t(p.id));
        mix(uint64_t(p.deathDay + 1000000));
        mix(uint64_t(p.spouse + 10));
        mix(uint64_t(p.household + 10));
        mix(hashString(p.occupation));
        mix(uint64_t(p.free) * 7 + uint64_t(p.home + 10));
        for (const auto& b : beliefs_[p.id]) mix(uint64_t(b.deed) * 31 + uint64_t(b.culprit + 10));
        for (const auto& t : p.traits) mix(hashString(t));
        mix(uint64_t(std::llround(p.health * 1000)));
        for (double s : p.skills) mix(uint64_t(std::llround(s * 1000)));
    }
    for (const auto& hh : households_) {
        mix(uint64_t(std::llround(hh.money * 100)));
        mix(uint64_t(std::llround(hh.food * 100)));
        mix(uint64_t(std::llround(hh.acres * 100)));
        mix(uint64_t(std::llround(hh.debt * 100)) + uint64_t(hh.debtYears));
    }
    for (const auto& f : feuds_) mix(uint64_t(f.a) * 1000 + uint64_t(f.b) * 10 + uint64_t(f.stage));
    for (const auto& c : chronicle_) mix(hashString(c.text));
    return h;
}

}  // namespace sim
