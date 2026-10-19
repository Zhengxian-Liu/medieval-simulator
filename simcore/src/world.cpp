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
    cfg_.prerun = std::max(0, cfg_.prerun);
    cfg_.startYear -= cfg_.prerun;  // the world begins before play does
    rngMinds_.reseed(cfg.seed, hashString("minds"));
    rngWants_.reseed(cfg.seed, hashString("wants"));
    rngNews_.reseed(cfg.seed, hashString("news"));
    rngLand_.reseed(cfg.seed, hashString("land"));
    rngContest_.reseed(cfg.seed, hashString("contest"));
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
    setupLand();
    setupOffices();
    seedAcquaintance();
    if (cfg_.prerun > 0) {
        prerun();
        return;
    }
    openBooks();
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
            yearlyWants();
            yearlyIntentions();
            yearlyService();
            yearlyAway();
            yearlyFeuds();
            yearlyFeudTurns();
            yearlyLiaisons();
            events.pulse("yearly");
            pruneOpinions();
        }
        if (month() == 7) electReeve();
        if (month() == 8) harvest();
        if (month() == 10) yearlyTax();
        if (month() == 9) {
            chooseOfficers();
            fillPlaces();
            reletVacant();
            gatherFirewood();
            yearlyMarriages();
        }
        monthlyEconomy();
        monthlyFines();
        monthlyHealth();
        monthlyBirths();
        monthlySocial();
        monthlyNews();
        monthlyRumours();
        monthlyStress();
        monthlyConcerns();
        monthlyFocus();
        monthlyWards();
        growSkills();
        monthlyAway();
        if (month() % 3 == 0) computeRings();
        monthlyPlans();
        npcStartPlans();
        actOnIntentions();
        checkStandings();
        monthlyAmbition();
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
    ambitionNow();

    ++day_;
    // The family's books are kept to the penny: what moved the purse without a line is caught here (and is a bug).
    if (playerHousehold_ != kNone) {
        unbooked_ += std::abs(settleBooks("沒記上帳的"));
        if (dayOfMonth() == 0 && month() % 3 == 0) closeSeasonBooks();
    }
}

// Families reuse names. Namesakes under one roof go by age: the eldest plain, the youngest "小", any between "二",
// "三"… (a mother and two daughters: 塞西莉, 二塞西莉, 小塞西莉). A mark once given is kept while there is still a
// namesake at home to tell apart (design/fun-r6.md A): the 小尼可拉斯 does not turn into 二尼可拉斯 because a baby
// Nicholas was born; the newcomer takes the next mark free (三尼可拉斯).
std::string World::houseMark(Id id, int* youngerOut) const {
    const Person& p = people_[id];
    if (youngerOut) *youngerOut = 0;
    if (p.household == kNone) return "";
    static const char* nth[] = {"二", "三", "四", "五", "六", "七", "八", "九", "十"};
    auto rule = [&](Id who, int* yo) {
        const Person& q = people_[who];
        int older = 0, younger = 0;
        for (Id m : households_[q.household].members) {
            if (m == who || people_[m].firstName != q.firstName || !people_[m].alive()) continue;
            if (people_[m].birthDay < q.birthDay || (people_[m].birthDay == q.birthDay && m < who)) ++older;
            else ++younger;
        }
        // An elder namesake of the house who died lately still holds the plain name for a while: the son stays
        // 小威廉 the first years after his father's death, rather than turning into the one just buried.
        // (Only when this one would otherwise go plain and alone: with a younger namesake at home he stays plain, the
        // child takes the 小, and nobody is a 二 to a dead man.)
        if (q.alive() && q.dynasty != kNone && older == 0 && younger == 0)
            for (const auto& o : people_)
                if (o.id != who && !o.alive() && o.household == q.household && o.dynasty == q.dynasty && o.firstName == q.firstName &&
                    o.birthDay < q.birthDay && day_ - o.deathDay <= 5 * kDaysPerYear) {
                    older = 1;
                    break;
                }
        if (yo) *yo = younger;
        return older > 0 ? (younger == 0 ? std::string("小") : std::string(nth[std::min(older, 9) - 1])) : std::string();
    };
    if (!p.alive()) return rule(id, youngerOut);
    // The living namesakes of the house, eldest first, each given its mark: its old one if it still has one to keep and
    // nobody older kept it first, else the rule's, else the next one free.
    std::vector<Id> group;
    for (Id m : households_[p.household].members)
        if (people_[m].alive() && people_[m].firstName == p.firstName) group.push_back(m);
    std::sort(group.begin(), group.end(), [&](Id a, Id b) { return people_[a].birthDay != people_[b].birthDay ? people_[a].birthDay < people_[b].birthDay : a < b; });
    std::set<std::string> taken;
    std::string mine;
    for (Id m : group) {
        int yo = 0;
        std::string want = rule(m, &yo);
        if (m == id && youngerOut) *youngerOut = yo;
        auto old = marks_.find(m);
        std::string got;
        // Kept as long as there is a namesake at home: 二羅伯特 stays 二羅伯特 when his father dies and 小羅伯特 is left.
        bool keep = old != marks_.end() && !old->second.empty() && !taken.count(old->second) && group.size() > 1;
        if (keep) {
            got = old->second;
        } else if (!want.empty()) {
            if (old != marks_.end() && !old->second.empty() && !taken.count(old->second)) got = old->second;
            else if (!taken.count(want)) got = want;
            else
                for (const char* x : {"小", "二", "三", "四", "五", "六", "七", "八", "九", "十"})
                    if (!taken.count(x)) {
                        got = x;
                        break;
                    }
        }
        if (got.empty()) marks_.erase(m);
        else marks_[m] = got, taken.insert(got);
        if (m == id) mine = got;
    }
    return mine;
}

std::string World::fullName(Id id) const {
    if (id == kNone) return "（無）";
    const Person& p = people_[id];
    int younger = 0;  // namesakes at home younger than this one (who take the 小)
    std::string s = houseMark(id, &younger) + p.firstName;
    // The dead whose name lives on in the family (a son named for his father, a girl for the sister who
    // died before her): "老約翰" for the elder gone, "早夭的茱莉安娜" for a child.
    // Not while the death is still news (the notice, the wake, word going round): only once they are a memory.
    if (!p.alive() && p.dynasty != kNone && p.deathDay < day_ - kDaysPerMonth) {
        double age = static_cast<double>(p.deathDay - p.birthDay) / kDaysPerYear;
        for (const auto& o : people_)
            if (o.id != id && o.alive() && o.dynasty == p.dynasty && o.firstName == p.firstName) {
                // The younger one gone keeps the 小 the rules above gave (or none): only the elder is marked.
                if (o.birthDay > p.birthDay) s = (age < 5 ? "早夭的" : age >= 55 ? "老" : "已故的") + s;
                break;
            }
    }
    bool marked = s != p.firstName;  // already told apart above
    if (p.dynasty != kNone) s += "·" + dynasties_[p.dynasty].surname;
    // A namesake elsewhere: someone of the same surname (any family of it) in another house, alive here or away, or
    // dead not ten years. Say who this one is: the family's own by what they are to the head (你的弟弟), others by
    // whose child (or wife) they are, and by the year they were born when even that is shared.
    auto counts = [&](const Person& x) {
        return x.alive() ? (x.household != kNone || isAway(x)) : x.deathDay >= 0 && day_ - x.deathDay <= 10 * kDaysPerYear;
    };
    const std::string surname = p.dynasty != kNone ? dynasties_[p.dynasty].surname : "";
    if (!marked && p.dynasty != kNone && counts(p)) {
        std::vector<const Person*> namesakes;
        for (const auto& o : people_)
            if (o.id != id && o.firstName == p.firstName && o.dynasty != kNone && dynasties_[o.dynasty].surname == surname && counts(o) &&
                (o.household != p.household || p.household == kNone) && o.father != id && o.mother != id)
                namesakes.push_back(&o);
        if (!namesakes.empty()) {
            std::string kin = kinByname(id);
            bool kinClash = false;  // another namesake the head would call the same
            for (const Person* o : namesakes) kinClash = kinClash || (!kin.empty() && kinByname(o->id) == kin);
            if (!kin.empty() && !kinClash) return s + "（" + kin + "）";
            // Named for a father in another house, and he is the only other one: 小 (大 with a namesake child at home
            // who is 小 already).
            // (Not when another house has a 小 of that name already: then by whose son he is.)
            if (namesakes.size() == 1 && namesakes[0]->id == p.father) {
                std::string small = (younger > 0 ? "大" : "小") + s;
                bool clash = false;
                for (const auto& o : people_)
                    clash = clash || (o.id != id && o.alive() && o.household != p.household && o.firstName == p.firstName && o.dynasty != kNone &&
                                      dynasties_[o.dynasty].surname == surname && houseMark(o.id) + o.firstName + "·" + surname == small);
                if (!clash) return small;
            }
            auto fatherName = [&](const Person& x) { return x.father != kNone ? people_[x.father].firstName : std::string(); };
            std::string fn = fatherName(p);
            bool fatherClash = fn.empty();
            for (const Person* o : namesakes) fatherClash = fatherClash || fatherName(*o) == fn;
            if (!fatherClash) return s + "（" + fn + (p.sex == Sex::Male ? " 的兒子）" : " 的女兒）");
            if (fn.empty() && p.spouse != kNone) {
                bool spouseClash = false;
                for (const Person* o : namesakes)
                    spouseClash = spouseClash || (o->spouse != kNone && people_[o->spouse].firstName == people_[p.spouse].firstName);
                if (!spouseClash) return s + "（" + people_[p.spouse].firstName + (p.sex == Sex::Male ? " 的丈夫）" : " 的妻子）");
            }
            return s + "（" + std::to_string(cfg_.startYear + p.birthDay / kDaysPerYear) + " 年生）";
        }
    }
    // Told apart at home but not in the village: two houses each with their own 小羅伯特·里夫 (seed 91, 1330: a man of
    // 41 and a boy of 17, and the boy's death read as the man's). Whoever else would go by the very same name, alive or
    // away (a 小 by his house's rule, or by a father in another house), both say whose they are.
    if (marked && p.alive() && p.dynasty != kNone) {
        std::vector<const Person*> same;
        for (const auto& o : people_) {
            if (o.id == id || !o.alive() || (o.household == kNone && !isAway(o)) || o.firstName != p.firstName || o.dynasty == kNone ||
                dynasties_[o.dynasty].surname != surname || (o.household == p.household && p.household != kNone))
                continue;
            std::string om = houseMark(o.id);
            if (om.empty() && o.father != kNone && people_[o.father].firstName == o.firstName && people_[o.father].alive() &&
                people_[o.father].household != o.household)
                om = "小";  // a son named for his father in another house goes by 小 (above)
            if (om + o.firstName + "·" + surname == s) same.push_back(&o);
        }
        if (!same.empty()) {
            std::string kin = kinByname(id);
            bool kinClash = false;
            for (const Person* o : same) kinClash = kinClash || (!kin.empty() && kinByname(o->id) == kin);
            if (!kin.empty() && !kinClash) return s + "（" + kin + "）";
            auto fatherName = [&](const Person& x) { return x.father != kNone ? people_[x.father].firstName : std::string(); };
            std::string fn = fatherName(p);
            bool fatherClash = fn.empty();
            for (const Person* o : same) fatherClash = fatherClash || fatherName(*o) == fn;
            if (!fatherClash) return s + "（" + fn + (p.sex == Sex::Male ? " 的兒子）" : " 的女兒）");
            Id hh = p.household != kNone ? p.household : p.awayFrom;
            Id hd = hh != kNone ? households_[hh].head : kNone;
            if (hd != kNone && hd != id) {
                bool headClash = false;
                for (const Person* o : same) {
                    Id oh = o->household != kNone ? o->household : o->awayFrom;
                    headClash = headClash || (oh != kNone && households_[oh].head != kNone && people_[households_[oh].head].firstName == people_[hd].firstName);
                }
                if (!headClash) return s + "（" + people_[hd].firstName + "家的）";
            }
            return s + "（" + std::to_string(cfg_.startYear + p.birthDay / kDaysPerYear) + " 年生）";
        }
    }
    return s;
}

// What the head calls one of the family, for telling namesakes apart: "你的弟弟", "你的兒子" ("" for anyone else).
std::string World::kinByname(Id id) const {
    Id head = playerHead();
    if (head == kNone || id == head || !isPlayerFamily(people_[id])) return id == head && head != kNone ? "你" : "";
    std::string t = kinTie(head, id);
    const Person& h = people_[head];
    const Person& p = people_[id];
    bool f = p.sex == Sex::Female, older = p.birthDay < h.birthDay;
    if (t == "sibling") return std::string("你的") + (f ? (older ? "姊姊" : "妹妹") : (older ? "哥哥" : "弟弟"));
    std::string w = kinWord(head, id);
    return w.empty() || w == "親戚" ? "" : "你的" + w;
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
    v = std::clamp(v, -100.0, 100.0);
    // Lying with someone's wife (or husband) sits between you and them however well you get on: guilt or contempt.
    if (a.lover != kNone && a.lover == b.spouse && people_[b.spouse].alive()) v = std::max(-100.0, v - kGuilt);
    return v;
}

void World::addReputation(Id who, double by, const std::string& why) {
    if (who == kNone || by == 0) return;
    Person& p = people_[who];
    double before = p.reputation;
    p.reputation = std::clamp(p.reputation + by, -50.0, 50.0);
    double moved = p.reputation - before;
    // A whole point or more is a thing people talk about; less is a drift that is told once it comes to a point.
    if (std::abs(moved) < 1) {
        p.repDrift += moved;
        if (std::abs(p.repDrift) < 1) return;
        moved = p.repDrift;
        p.repDrift = 0;
    }
    if (std::abs(moved) < 0.5) return;
    p.repNotes.push_back({day_, moved, why});
    if (p.repNotes.size() > 24) p.repNotes.erase(p.repNotes.begin());
}

// Bands as the asking counts them (ask.cpp: half the reputation, at most ±12, said aloud from +6 / −5 on).
World::RepView World::reputationView(Id who) const {
    RepView v;
    if (who == kNone) return v;
    const Person& p = people_[who];
    v.value = static_cast<int>(std::lround(p.reputation));
    v.word = p.reputation >= 12 ? "好" : p.reputation <= -25 ? "很壞" : p.reputation <= -10 ? "不好" : "平常";
    double pull = std::clamp(p.reputation * 0.5, -12.0, 12.0);
    v.effect = p.reputation >= 12 ? "開口求人時，人家會說「你家在村裡名聲好」，多給幾分面子（+" + std::to_string(std::lround(pull)) + "）"
               : p.reputation <= -10 ? "開口求人（買地、租地、拉票）時，人家會拿「你家名聲不好」推你（−" + std::to_string(std::lround(-pull)) + "）；選差事也吃虧"
                                     : "不好不壞，開口時不算數";
    // The last three things that moved it; the same thing year after year (a good year's trade) is one line,
    // so it does not push out the one bad thing people still talk about.
    for (auto it = p.repNotes.rbegin(); it != p.repNotes.rend() && v.recent.size() < 3;) {
        int last = cfg_.startYear + it->day / kDaysPerYear, first = last, times = 0;
        double sum = 0;
        auto jt = it;
        for (; jt != p.repNotes.rend() && jt->why == it->why; ++jt) sum += jt->delta, first = cfg_.startYear + jt->day / kDaysPerYear, ++times;
        long d = std::lround(sum);
        std::string when = first == last ? std::to_string(last) + " 年" : std::to_string(first) + "–" + std::to_string(last) + " 年";
        v.recent.push_back(when + " " + it->why + "（" + (d > 0 ? "+" : "") + std::to_string(d) + (times > 1 ? "，" + std::to_string(times) + " 回" : "") + "）");
        it = jt;
    }
    v.mend = p.reputation >= 12 ? "保住它：別欠租、別上莊園法庭、別讓醜事敗露。"
             : "怎麼修：家主的年度重心放在「交際」（上酒館、串門、幫人忙）或「教會」，每年慢慢回來一點；"
               "當上村裡的差事、莊頭帳記得清白、替村裡擔待大事（替窮人家多擔稅、瘟疫時留下來照顧鄰居），一次回來幾點。"
               "欠租交不出、被告上法庭定罪、醜事敗露，都會一下子掉下去。";
    return v;
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
    if (p.dynasty == playerDynasty_ && p.dynasty != kNone) return true;
    if (playerHousehold_ == kNone) return false;
    // Under the roof (not a servant of another house's), out serving from it, or away on the road from it.
    if (p.household == playerHousehold_ && (p.home == kNone || p.home == playerHousehold_)) return true;
    return p.home == playerHousehold_ || (p.household == kNone && p.awayFrom == playerHousehold_);
}

bool World::headsNearKin(Id id) const {
    for (Id head : {playerHead(), actingHead()}) {
        if (head == kNone || head == id) continue;
        const std::string t = kinTie(head, id);
        if (t == "parent" || t == "child" || t == "spouse" || t == "sibling" || t == "grandchild" || t == "grandparent") return true;
    }
    return false;
}

bool World::familyTold(Id id) const {
    if (id == kNone || playerHousehold_ == kNone) return false;
    const Person& p = people_[id];
    if (p.household == playerHousehold_ || p.home == playerHousehold_ || (p.household == kNone && p.awayFrom == playerHousehold_)) return true;
    return headsNearKin(id);
}

bool World::gentry(Id id) const {
    if (id == kNone || lord_ == kNone) return false;
    const Person& p = people_[id];
    const Person& lord = people_[lord_];
    return (p.dynasty != kNone && p.dynasty == lord.dynasty) ||
           (p.household != kNone && p.household == lord.household && p.home == kNone) || p.occupation == "lord" || p.occupation == "lady";
}

bool World::mayWed(Id a, Id b) const {
    if (gentry(a) == gentry(b)) return true;
    // Across the line only into a small landholder's house the lord thinks well of: a match talked over with the
    // steward (gentry_match), never one made at the church door.
    Id c = gentry(a) ? b : a;
    const Person& cp = people_[c];
    if (cp.household == kNone || lord_ == kNone || !people_[lord_].alive()) return false;
    const Household& h = households_[cp.household];
    return h.head != kNone && standing(h.id) == 3 && opinion(lord_, h.head) >= 30;
}

bool World::gentrySingle() const {
    for (const auto& p : people_)
        if (p.alive() && p.household != kNone && gentry(p.id) && p.occupation != "lord" && p.occupation != "lady" &&
            p.occupation != "priest" && (p.spouse == kNone || !people_[p.spouse].alive()) && ageYears(p) >= 16 && ageYears(p) <= 40)
            return true;
    return false;
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
        mix(hashString(p.station) + uint64_t(p.awayFrom + 10) * 13 + uint64_t(p.road.size()));
        mix(hashString(p.focus));
        for (const auto& b : beliefs_[p.id]) mix(uint64_t(b.deed) * 31 + uint64_t(b.culprit + 10));
        for (const auto& t : p.traits) mix(hashString(t));
        mix(uint64_t(std::llround(p.health * 1000)));
        for (double s : p.skills) mix(uint64_t(std::llround(s * 1000)));
    }
    for (const auto& hh : households_) {
        mix(uint64_t(std::llround(hh.money * 100)));
        mix(uint64_t(std::llround((hh.focusYield + 1) * 1e6)));
        mix(uint64_t(hh.ward + 10)); mix(uint64_t(hh.wardLine + 10));
        mix(uint64_t(std::llround(hh.food * 100)));
        mix(uint64_t(std::llround(hh.acres * 100)));
        mix(uint64_t(std::llround(hh.debt * 100)) + uint64_t(hh.debtYears));
    }
    for (const auto& f : feuds_) mix(uint64_t(f.a) * 1000 + uint64_t(f.b) * 10 + uint64_t(f.stage));
    for (const auto& c : chronicle_) mix(hashString(c.text));
    return h;
}

}  // namespace sim
