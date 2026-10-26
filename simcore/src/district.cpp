// 第二層舞台：地方 (design/district.md). Four places outside the village, each a few named people and a few things that
// can be contested (data/district.json): a neighbouring village with a rising house of its own (the new rival), the
// market town (loans, the wool price, an apprentice's and a clerk's place), the lord's household (the steward's and
// the bailiff's posts, loans to the lord, the demesne farm on lease) and the hundred court (suits over debts and bounds,
// the juror's seat). Nothing here is a second simulation: the people are the sim's own people (they age, marry, die,
// and their heads choose through the decider like the village's), living outside the village's daily round; what they
// contest goes through the village's own pieces (opinions, favours, news, the clash book, the jury, the ledger).
//
// Three verbs: 出價 (bid), 託人 (have someone speak for the family: a favour spent, a son at a clerk's desk, kin out
// there), 放貸 (lend). A house is in 地方 once one of the big decisions of design/district.md 五 is made, and falls out
// of it again when it has no foothold left there or takes too many blows in a few years.
#include <algorithm>
#include <cmath>
#include <set>

#include "simcore/world.h"

namespace sim {

namespace {
std::string num(double v) { return std::to_string(static_cast<long>(std::lround(v))); }
std::string pence(double v) { return num(v) + " 便士"; }
// 「4 成」: a chance as the cards say it.
std::string odds(double p) {
    if (p < 0.05) return "不到 1 成";
    if (p > 0.95) return "9 成以上";
    return num(std::clamp(std::round(p * 10), 1.0, 9.0)) + " 成";
}
// 「領主府總管的位子」 -> 「領主府總管」: the post itself, as in 「當上了…」.
std::string postName(const std::string& n) {
    for (const char* t : {"的位子", "的席位"})
        if (size_t at = n.find(t); at != std::string::npos) return n.substr(0, at);
    return n;
}
int toIndex(const std::string& s) {
    if (s.empty()) return -1;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int>(v) : -1;
}
}  // namespace

// ---- Looking things up -------------------------------------------------------------------------

World::DHouse* World::dhouseOf(Id shell) {
    for (auto& d : dhouses_)
        if (d.shell == shell) return &d;
    return nullptr;
}
const World::DHouse* World::dhouseOf(Id shell) const {
    for (const auto& d : dhouses_)
        if (d.shell == shell) return &d;
    return nullptr;
}
World::DHouse* World::dhouseByKey(const std::string& key) {
    for (auto& d : dhouses_)
        if (d.key == key) return &d;
    return nullptr;
}
const World::DHouse* World::dhouseByKey(const std::string& key) const {
    for (const auto& d : dhouses_)
        if (d.key == key) return &d;
    return nullptr;
}
const World::DHouse* World::dhouseOfPerson(Id person) const {
    for (const auto& d : dhouses_)
        if (std::find(d.members.begin(), d.members.end(), person) != d.members.end()) return &d;
    return nullptr;
}
bool World::isDistrictShell(Id house) const { return house != kNone && dhouseOf(house) != nullptr; }
World::Stake* World::stakeByKey(const std::string& key) {
    for (auto& s : stakes_)
        if (s.key == key) return &s;
    return nullptr;
}
const World::Stake* World::stakeByKey(const std::string& key) const {
    for (const auto& s : stakes_)
        if (s.key == key) return &s;
    return nullptr;
}

Id World::houseHead(Id house) const {
    if (house == kNone) return kNone;
    if (const DHouse* d = dhouseOf(house)) return d->gone ? kNone : d->head;
    return households_[house].head;
}

std::string World::houseTag(Id house) const {
    if (house == kNone) return "沒有人家";
    if (inPlayerHouse(house)) return "你家";
    const Household& h = households_[house];
    std::string surname = h.dynasty != kNone ? dynasties_[h.dynasty].surname : "那";
    if (const DHouse* d = dhouseOf(house)) {
        const Json& places = defs_.district["places"];
        return places.stringOr(d->place, "") + "的" + surname + "家";
    }
    return surname + "家";
}

Id World::stakeDecider(const Stake& s) const {
    if (s.decider == "lord") return lord_ != kNone && people_[lord_].alive() ? lord_ : kNone;
    const DHouse* d = dhouseByKey(s.decider);
    return d && !d->gone ? d->head : kNone;
}

bool World::districtKinHouse(const DHouse& d) const {
    const std::vector<Id> line = lineage();
    for (Id m : d.members) {
        if (!people_[m].alive()) continue;
        if (people_[m].dynasty == playerDynasty_ && playerDynasty_ != kNone) return true;
        for (Id l : line)
            if (kinTo(l, m)) return true;
    }
    // A wife of the family's house who came from there.
    if (playerHousehold_ != kNone)
        for (Id m : households_[playerHousehold_].members)
            if (people_[m].flags.count("from:" + d.key)) return true;
    return false;
}

const World::DHouse* World::districtRival() const {
    for (const auto& d : dhouses_)
        if (d.rival && !d.gone && d.head != kNone && !dist_.rivalKin) return &d;
    return nullptr;
}

std::string World::districtRivalName() const {
    const DHouse* d = districtRival();
    if (!d) return "";
    return houseTag(d->shell) + "（" + fullName(d->head) + "）";
}

std::string World::districtRivalAfter() const {
    const DHouse* d = districtRival();
    if (!d) return "";
    std::string out;
    for (const auto& s : stakes_)
        if (s.open)
            for (const auto& b : s.bids)
                if (b.house == d->shell && b.known) out += (out.empty() ? "" : "、") + s.name;
    return out;
}

std::vector<std::string> World::districtRivalClashes() const {
    std::vector<std::string> out;
    const DHouse* d = districtRival();
    if (!d || playerHousehold_ == kNone) return out;
    for (auto it = clashes_.rbegin(); it != clashes_.rend() && out.size() < 6; ++it)
        if ((inPlayerHouse(it->ha) && it->hb == d->shell) || (inPlayerHouse(it->hb) && it->ha == d->shell))
            out.push_back(std::to_string(cfg_.startYear + it->day / kDaysPerYear) + " 年 " + clashWords(*it));
    return out;
}

std::string World::districtPersonLine(Id id) const {
    const DHouse* d = dhouseOfPerson(id);
    if (!d) return "";
    std::string role = d->head == id ? "當家的" : "家裡人";
    for (const auto& s : stakes_)
        if ((s.kind == "post" || s.kind == "seat") && s.holderMan == id) role = postName(s.name);
    return houseTag(d->shell) + "（" + role + "）：" + d->note;
}

bool World::inDistrict() const { return dist_.in; }
bool World::districtHouse(Id house) const {
    if (house == kNone) return false;
    if (inPlayerHouse(house)) return dist_.in;
    return dist_.houses.count(house) > 0;
}

std::string World::levelName(int level) const {
    const Json& l = defs_.district["levels"];
    if (!l.isArray() || level < 0 || level >= static_cast<int>(l.size())) return "";
    return l[static_cast<size_t>(level)].stringOr("name", "");
}
double World::levelMult(int level) const {
    const Json& l = defs_.district["levels"];
    if (!l.isArray() || level < 0 || level >= static_cast<int>(l.size())) return 1;
    return l[static_cast<size_t>(level)].numberOr("mult", 1);
}
int World::levelCount() const {
    const Json& l = defs_.district["levels"];
    return l.isArray() ? static_cast<int>(l.size()) : 1;
}

std::string World::sonInTown() const {
    if (playerDynasty_ == kNone) return "";
    for (const auto& p : people_)
        if (p.alive() && isPlayerFamily(p) && (p.station == "town_clerk" || p.station == "town_apprentice" || p.station == "town_journeyman" ||
                                               p.station == "town_master" || p.station == "clerk"))
            return p.firstName;
    return "";
}

double World::districtTaxMult(const Household& h) const {
    if (!districtHouse(h.id)) return 1;
    return defs_.district["tier"].numberOr("tax_mult", 1.2);
}

// ---- Setting it up -----------------------------------------------------------------------------

// When the family is handed over (prerun.cpp, or at once without a pre-run): the places out there, their people and
// what can be contested. Before that nobody plays, and the village's years are its own.
void World::setupDistrict() {
    if (dist_.built || !defs_.district.isObject()) return;
    dist_.built = true;
    rngDistrict_.reseed(cfg_.seed, hashString("district"));
    const Json& D = defs_.district;
    dist_.wool = dist_.woolNext = D["wool"].numberOr("price", 60);
    dist_.lordAsk = year() + 1;
    for (const auto& hj : D["houses"].asArray()) {
        DHouse d;
        d.key = hj["key"].asString();
        d.place = hj.stringOr("place", "neighbour");
        d.note = hj.stringOr("note", "");
        d.dynasty = addDynasty(hj["surname"].asString());
        d.shell = addHousehold(d.dynasty, 0, hj.numberOr("money", 100));
        households_[d.shell].extinct = true;  // out of every village round; news and the clash book still name it
        d.acres = hj.numberOr("acres", 0);
        d.income = hj.numberOr("income", 100);
        d.spend = hj.numberOr("spend", 0.8);
        d.rival = hj["rival"].isBool() && hj["rival"].asBool();
        d.gentry = hj["gentry"].isBool() && hj["gentry"].asBool();
        d.lender = hj["lender"].isBool() && hj["lender"].asBool();
        d.borrow = hj["borrow"];
        Id wife = kNone;
        for (const auto& pj : hj["people"].asArray()) {
            const Sex sex = pj.stringOr("sex", "m") == "f" ? Sex::Female : Sex::Male;
            const int bday = day_ - static_cast<int>(pj.numberOr("age", 30) * kDaysPerYear) - rngDistrict_.range(0, kDaysPerYear - 1);
            Id id = addPerson(pj["name"].asString(), d.dynasty, sex, bday, kNone);
            Person& p = people_[id];
            p.occupation = pj.stringOr("occupation", "");
            p.free = true;
            p.health = 85;
            p.flags.insert("district");
            p.skills = {6, 3, 3, 4};
            if (pj.has("skills"))
                for (const auto& [k, v] : pj["skills"].asObject()) p.skills[static_cast<size_t>(skillFromKey(k))] = v.asNumber();
            if (pj.has("traits"))
                for (const auto& t : pj["traits"].asArray())
                    if (defs_.trait(t.asString())) p.traits.push_back(t.asString());
            const std::string role = pj.stringOr("role", "child");
            if (role == "head") {
                d.head = id;
            } else if (role == "wife" && d.head != kNone) {
                wife = id;
                p.spouse = d.head;
                people_[d.head].spouse = id;
            } else if (role == "child" && d.head != kNone) {
                Person& hp = people_[d.head];
                (hp.sex == Sex::Male ? p.father : p.mother) = d.head;
                hp.children.push_back(id);
                if (wife != kNone) p.mother = wife, people_[wife].children.push_back(id);
            }
            d.members.push_back(id);
        }
        if (d.head != kNone) districtNotable_.insert(d.head);
        dhouses_.push_back(d);
    }
    for (const auto& sj : D["stakes"].asArray()) {
        Stake s;
        s.key = sj["key"].asString();
        s.kind = sj["kind"].asString();
        s.place = sj.stringOr("place", "neighbour");
        s.name = sj["name"].asString();
        s.decider = sj.stringOr("decider", "");
        s.seller = sj.stringOr("seller", "");
        s.master = sj.stringOr("master", "");
        s.house = sj.stringOr("house", "");
        s.bride = sj.stringOr("bride", "");
        s.station = sj.stringOr("station", "");
        s.bounds = sj.stringOr("bounds", "");
        s.dowryName = sj.stringOr("dowry_name", "");
        s.base = sj.numberOr("base", 100);
        s.acres = sj.numberOr("acres", 0);
        s.rent = sj.numberOr("rent", 0);
        s.yield = sj.numberOr("yield", 0);
        s.fee = sj.numberOr("fee", 0);
        s.learning = sj.numberOr("learning", 0);
        s.dowryAcres = sj.numberOr("dowry_acres", 0);
        s.years = static_cast<int>(sj.numberOr("years", 0));
        s.month = static_cast<int>(sj.numberOr("month", 0));
        s.every = static_cast<int>(sj.numberOr("every", 0));
        s.minAge = static_cast<int>(sj.numberOr("min_age", 0));
        s.maxAge = static_cast<int>(sj.numberOr("max_age", 99));
        s.standing = static_cast<int>(sj.numberOr("standing", 0));
        s.retire = static_cast<int>(sj.numberOr("retire", 0));
        s.brideAge = static_cast<int>(sj.numberOr("bride_age", 0));
        s.free = sj["free"].isBool() && sj["free"].asBool();
        s.tier = sj["tier"].isBool() && sj["tier"].asBool();
        s.minor = sj["minor"].isBool() && sj["minor"].asBool();
        s.gate = sj["gate"].isBool() && sj["gate"].asBool();
        s.nextOpen = year() + static_cast<int>(sj.numberOr("first", 1));
        if (const DHouse* sel = dhouseByKey(s.seller)) s.holder = sel->shell;
        if (const DHouse* hd = dhouseByKey(sj.stringOr("holder", ""))) {
            s.holder = hd->shell;
            s.holderMan = hd->head;
            // A post held for a term runs out the year it first comes up.
            if (s.years > 0) s.until = (s.nextOpen - cfg_.startYear) * kDaysPerYear + s.month * kDaysPerMonth;
        }
        // A match: holderMan is the bride it is for.
        if (s.kind == "match")
            if (const DHouse* bh = dhouseByKey(s.house))
                for (Id m : bh->members)
                    if (people_[m].firstName == s.bride) s.holderMan = m;
        if (const DHouse* dd = dhouseByKey(s.decider); dd && dd->head != kNone) districtNotable_.insert(dd->head);
        stakes_.push_back(s);
    }
    dist_.rivalHead = districtRival() ? districtRival()->head : kNone;
}

// ---- The months ----------------------------------------------------------------------------------

void World::monthlyDistrict() {
    if (!dist_.built || playerHousehold_ == kNone || gameOver_) return;
    if (month() == 0) districtYear();
    for (auto& s : stakes_) {
        if (s.open && day_ >= s.closeDay) closeRound(s);
        else if (!s.open && !s.done && month() == s.month && year() >= s.nextOpen && stakeVacant(s)) openRound(s);
    }
    // A post or a seat whose man died or left: it falls vacant at once (the next round is its month).
    for (auto& s : stakes_)
        if ((s.kind == "post" || s.kind == "seat") && s.holderMan != kNone && !people_[s.holderMan].alive()) {
            if (inPlayerHouse(s.holder)) log(1, "【地方】" + fullName(s.holderMan) + "死了，" + s.name + "空了出來。");
            s.holderMan = kNone, s.holder = kNone, s.until = -1;
            s.nextOpen = std::min(s.nextOpen, year() + (month() < s.month ? 0 : 1));
        }
    if (month() % 3 == 0) seasonAsks();
    const Json& months = defs_.district["court"]["months"];
    if (months.isArray())
        for (const auto& m : months.asArray())
            if (m.asInt() == month()) hundredCourt();
    if (month() == static_cast<int>(defs_.district["proposal"].numberOr("month", 4))) proposals();
    // The lord's steward comes to a house with money put by (design/district.md 四: 領主會要你借錢給他).
    if (month() == 4 && actingHead() != kNone && year() >= dist_.lordAsk) {
        Person& head = people_[actingHead()];
        const Household& h = households_[playerHousehold_];
        if (head.ambition != "manor_creditor" && h.money >= defs_.district["tier"].numberOr("rich", 520) && manorBond_.house == kNone) {
            dist_.lordAsk = year() + static_cast<int>(defs_.district["tier"].numberOr("lord_every", 2));
            stewardLoan(head, true);
        }
    }
}

// The turn of the year: the people out there live their year, the rival makes his plans, a house short of money
// sells, and the family's place in 地方 is weighed.
void World::districtYear() {
    districtLives();
    rivalYear();
    // A house short of money puts some land up for sale.
    const Json& sales = defs_.district["sales"];
    for (auto& d : dhouses_) {
        if (d.gone || d.rival || d.place != "neighbour" || d.head == kNone) continue;
        if (households_[d.shell].money >= sales.numberOr("below", 40) || d.acres < sales.numberOr("keep", 12) + 3) continue;
        bool selling = false;
        for (const auto& s : stakes_)
            if (s.kind == "land" && !s.done && s.holder == d.shell) selling = true;
        if (selling || !rngDistrict_.chance(0.6)) continue;
        const Json& a = sales["acres"];
        double acres = rngDistrict_.range(a[0].asInt(), a[1].asInt());
        Stake s;
        s.key = "sale" + std::to_string(stakes_.size());
        s.kind = "land";
        s.place = d.place;
        s.name = defs_.district["places"].stringOr(d.place, "") + dynasties_[d.dynasty].surname + "家賣的 " + num(acres) + " 畝";
        s.decider = s.seller = d.key;
        s.base = acres * sales.numberOr("price", 42);
        s.acres = acres;
        s.free = true;
        s.gate = true;
        s.month = month() + 2;
        s.nextOpen = year();
        s.holder = d.shell;
        stakes_.push_back(s);
    }
    checkFall();
}

void World::districtLives() {
    const double plague = param("plague_monthly", 0);
    auto pickName = [&](Sex sex) {
        const auto& names = sex == Sex::Male ? defs_.maleNames : defs_.femaleNames;
        return names.empty() ? std::string("無名") : names[static_cast<size_t>(rngDistrict_.range(0, static_cast<int>(names.size()) - 1))];
    };
    for (auto& d : dhouses_) {
        if (d.gone) continue;
        const bool notable = d.rival || dist_.in;
        // Deaths.
        std::vector<Id> ms = d.members;
        for (Id m : ms) {
            Person& p = people_[m];
            if (!p.alive()) continue;
            const double a = ageYears(p);
            double q = a < 1 ? 0.12 : a < 5 ? 0.04 : a < 40 ? 0.008 : a < 55 ? 0.02 : a < 65 ? 0.045 : a < 75 ? 0.1 : 0.22;
            q *= 1 + plague * 24;
            if (!rngDistrict_.chance(q)) continue;
            static const char* causes[] = {"熱病", "痢疾", "肺病", "意外受傷"};
            std::string cause = a < 5 ? "熱病" : a >= 62 ? "年老" : causes[rngDistrict_.range(0, 3)];
            if (cause == "意外受傷") cause = "意外：在路上摔死";
            kill(m, cause);
            if (p.alive()) continue;  // spared (tests)
            Id n = addNews("district:death", m, kNone, d.shell);
            if ((notable || isPlayerFamily(p)) && a >= 14) {
                dHear(n, kNone);
                if (!isPlayerFamily(p)) log(d.head == m ? 1 : 2, "【地方】" + houseTag(d.shell) + "的 " + fullName(m) + " 因" + cause + "去世，享年 " + num(std::floor(a)) + " 歲。");
            }
        }
        d.members.erase(std::remove_if(d.members.begin(), d.members.end(), [&](Id m) { return !people_[m].alive(); }), d.members.end());
        // The house passes: the eldest son grown, else the widow, else the eldest grown; else it ends.
        if (d.head == kNone || !people_[d.head].alive()) {
            Id old = d.head, next = kNone;
            for (Id m : d.members)
                if (people_[m].sex == Sex::Male && ageYears(people_[m]) >= 16 && (old == kNone || people_[m].father == old || people_[m].mother == old) &&
                    (next == kNone || people_[m].birthDay < people_[next].birthDay))
                    next = m;
            if (next == kNone && old != kNone && people_[old].spouse != kNone && people_[people_[old].spouse].alive() &&
                std::find(d.members.begin(), d.members.end(), people_[old].spouse) != d.members.end())
                next = people_[old].spouse;
            for (Id m : d.members)
                if (next == kNone && ageYears(people_[m]) >= 16) next = m;
            if (next == kNone && !d.members.empty()) next = d.members.front();
            districtNotable_.erase(old);
            // The town's trades and the lord's and the hundred's men do not end with one man: someone else comes to
            // the counting-house, the clerk's desk, the court (a new name, the same work).
            if (next == kNone && d.place != "neighbour" && old != kNone) {
                std::string surname = defs_.surnames.empty() ? dynasties_[d.dynasty].surname
                                                             : defs_.surnames[static_cast<size_t>(rngDistrict_.range(0, static_cast<int>(defs_.surnames.size()) - 1))];
                d.dynasty = addDynasty(surname);
                households_[d.shell].dynasty = d.dynasty;
                const std::string occ = people_[old].occupation;
                const std::vector<std::string> traits = people_[old].traits;
                const auto skills = people_[old].skills;
                next = addPerson(pickName(Sex::Male), d.dynasty, Sex::Male, day_ - rngDistrict_.range(32, 46) * kDaysPerYear, kNone);
                Person& np = people_[next];
                np.occupation = occ;
                np.free = true;
                np.flags.insert("district");
                np.skills = skills;
                for (const auto& t : traits)
                    if (t == "literate" || t == "clever") np.traits.push_back(t);
                d.members.push_back(next);
                if (dist_.in) log(2, "【地方】" + houseTag(d.shell) + "：" + fullName(next) + "接了 " + fullName(old) + "的營生。");
            }
            d.head = next;
            if (next == kNone) {
                d.gone = true;
                if (notable) log(1, "【地方】" + houseTag(d.shell) + "沒有人了。");
            } else {
                districtNotable_.insert(next);
                std::rotate(d.members.begin(), std::find(d.members.begin(), d.members.end(), next), std::find(d.members.begin(), d.members.end(), next) + 1);
                if (notable) log(d.rival ? 1 : 2, "【地方】" + houseTag(d.shell) + "如今是 " + fullName(next) + " 當家。");
            }
        }
        if (d.gone) continue;
        // Births: a married woman of the house, while young enough.
        std::vector<Id> wives;
        for (Id m : d.members) {
            const Person& w = people_[m];
            if (w.sex == Sex::Female && w.spouse != kNone && people_[w.spouse].alive() && ageYears(w) >= 17 && ageYears(w) <= 42) wives.push_back(m);
        }
        for (Id wid : wives) {
            if (!rngDistrict_.chance(0.22) || d.members.size() >= 9) continue;
            const Sex sex = rngDistrict_.chance(0.5) ? Sex::Male : Sex::Female;
            Id c = addPerson(pickName(sex), d.dynasty, sex, day_ - rngDistrict_.range(0, kDaysPerYear - 1), kNone);
            Person& ch = people_[c];
            ch.flags.insert("district");
            ch.free = true;
            ch.mother = wid;
            ch.father = people_[wid].spouse;
            people_[wid].children.push_back(c);
            people_[ch.father].children.push_back(c);
            d.members.push_back(c);
            if (isPlayerFamily(people_[wid]) || familyTold(wid))
                log(1, people_[wid].firstName + "在" + houseTag(d.shell) + "生了個" + (sex == Sex::Male ? "兒子" : "女兒") + "，取名" + ch.firstName + "。");
        }
        // Marriages: a son brings a wife in from round about; a daughter marries away (not one a match is open for).
        std::vector<Id> ms2 = d.members;
        for (Id m : ms2) {
            Person& p = people_[m];
            if (!p.alive() || (p.spouse != kNone && people_[p.spouse].alive())) continue;
            const double a = ageYears(p);
            bool contested = false;
            for (const auto& s : stakes_)
                if (s.kind == "match" && !s.done && s.holderMan == m) contested = true;
            if (contested || isPlayerFamily(p)) continue;
            if (p.sex == Sex::Male && a >= 21 && a <= 45 && rngDistrict_.chance(0.2)) {
                Id w = addPerson(pickName(Sex::Female), d.dynasty, Sex::Female, p.birthDay + rngDistrict_.range(2, 7) * kDaysPerYear, kNone);
                people_[w].flags.insert("district");
                people_[w].occupation = "housewife";
                people_[w].spouse = m;
                people_[m].spouse = w;  // (addPerson may have moved people_: no reference held across it)
                d.members.push_back(w);
                if (notable && d.rival) log(2, "【地方】" + houseTag(d.shell) + "的 " + fullName(m) + " 娶了親。");
            } else if (p.sex == Sex::Female && a >= 18 && a <= 30 && rngDistrict_.chance(0.2)) {
                d.members.erase(std::remove(d.members.begin(), d.members.end(), m), d.members.end());
                p.flags.insert("married_away");
            }
        }
    }
}

// ---- What is contested -----------------------------------------------------------------------

bool World::stakeVacant(const Stake& s) const {
    if (s.done) return false;
    if (s.kind == "land") return s.holder != kNone && isDistrictShell(s.holder) && dhouseOf(s.holder)->acres >= s.acres;
    if (s.kind == "lease") return s.holder == kNone || s.until <= day_;
    if (s.kind == "post") {
        if (s.holderMan == kNone || !people_[s.holderMan].alive()) return true;
        if (s.retire > 0 && ageYears(people_[s.holderMan]) >= s.retire) return true;
        return s.until >= 0 && day_ >= s.until;
    }
    if (s.kind == "seat") return s.holderMan == kNone || !people_[s.holderMan].alive() || (s.until >= 0 && day_ >= s.until);
    if (s.kind == "place") return true;  // the master takes another every few years
    if (s.kind == "match") {
        if (s.holderMan == kNone) return false;
        const Person& b = people_[s.holderMan];
        return b.alive() && (b.spouse == kNone || !people_[b.spouse].alive()) && ageYears(b) >= s.brideAge && dhouseOfPerson(b.id);
    }
    return false;
}

std::vector<Id> World::stakeMen(const Stake& s, Id house) const {
    std::vector<Id> pool, out;
    if (const DHouse* d = dhouseOf(house)) pool = d->members;
    else if (house != kNone) pool = households_[house].members;
    const Id head = houseHead(house);
    for (Id m : pool) {
        const Person& p = people_[m];
        if (!p.alive() || p.sex != Sex::Male) continue;
        const double a = ageYears(p);
        if (s.kind == "land" || s.kind == "lease") {
            if (m == head) out.push_back(m);
        } else if (s.kind == "post") {
            bool holds = false;
            for (const auto& o : stakes_)
                if (o.kind == "post" && o.holderMan == m) holds = true;
            if (!holds && a >= s.minAge && a <= 60 && p.skills[Learning] >= s.learning && (p.station.empty() || p.station == "home")) out.push_back(m);
        } else if (s.kind == "seat") {
            if (m == head && a >= 25) out.push_back(m);
        } else if (s.kind == "place") {
            if (m != head && a >= s.minAge && a <= s.maxAge && (p.station.empty() || p.station == "home") && p.skills[Learning] >= s.learning &&
                (p.spouse == kNone || !people_[p.spouse].alive()))
                out.push_back(m);
        } else if (s.kind == "match") {
            if (a >= s.minAge && a <= s.maxAge && (p.spouse == kNone || !people_[p.spouse].alive()) && p.station != "archer" &&
                p.occupation != "priest" && p.occupation != "clerk" && !p.flags.count("celibate") && (!isAway(p)))
                out.push_back(m);
        }
    }
    // The likeliest first: the head for a post when he can, else the eldest; for a place the youngest who can still go.
    std::stable_sort(out.begin(), out.end(), [&](Id a, Id b) {
        if (s.kind == "post" && (a == head) != (b == head)) return a == head;
        return people_[a].birthDay < people_[b].birthDay;
    });
    return out;
}

std::string World::stakeBar(const Stake& s, Id house) const {
    if (house == kNone) return "沒有人家";
    const bool shell = isDistrictShell(house);
    if (!shell) {
        const Household& h = households_[house];
        if (h.extinct || h.head == kNone) return "沒有人當家";
        if (s.tier && !districtHouse(house)) return "要先在地方上站穩腳（買下外村的地、包租自營地、讓兒子進城、跟外村人家結親，或借一大筆錢給領主）";
        if (s.standing > 0 && standing(house) < s.standing) return std::string("要") + standingName(s.standing) + "以上的人家";
    }
    if (s.holder == house && (s.kind == "lease" || s.kind == "land")) return "已經在你家手上";
    if (s.kind == "match") {
        if (const DHouse* bh = dhouseByKey(s.house); bh && bh->shell == house) return "是她自己家";
    }
    if (s.kind == "land" && s.holder == house) return "是自己家的地";
    if (stakeMen(s, house).empty()) {
        if (s.kind == "post") return "家裡沒有合適的男人（滿 " + std::to_string(s.minAge) + " 歲" + (s.learning > 0 ? "、識字" : "") + "，沒有別的差事）";
        if (s.kind == "place") return "家裡沒有合適的孩子（" + std::to_string(s.minAge) + "–" + std::to_string(s.maxAge) + " 歲、在家、沒成親" + (s.learning > 0 ? "、識字" : "") + "）";
        if (s.kind == "match") return "家裡沒有合適的兒子（" + std::to_string(s.minAge) + "–" + std::to_string(s.maxAge) + " 歲、沒成親）";
        if (s.kind == "seat") return "當家的要滿 25 歲";
        return "沒有人當家";
    }
    if (households_[house].money < s.base) return "錢不夠（照價要 " + pence(s.base) + "）";
    return "";
}

double World::bidScore(const Stake& s, const DBid& b) const {
    double score = b.amount / std::max(1.0, s.base);
    double edge = 0;
    const Id dec = stakeDecider(s), bh = houseHead(b.house);
    if (dec != kNone && bh != kNone && dec != bh) edge += std::clamp(opinion(dec, bh) / 150, -0.3, 0.3);
    if (b.speak != kNone) edge += 0.2 + (dec != kNone ? std::clamp(opinion(dec, b.speak) / 200, -0.1, 0.15) : 0);
    if (b.man != kNone) {
        const Person& m = people_[b.man];
        if (s.kind == "post") edge += (m.skills[Learning] + m.skills[Charm]) / 60;
        if (s.kind == "place") edge += m.skills[Learning] / 50;
    }
    if (s.kind == "match" || s.kind == "seat") {
        const DHouse* d = dhouseOf(b.house);
        const int rung = d ? (d->acres >= 30 ? 2 : 1) : standing(b.house);
        edge += 0.12 * (rung - 1);
    }
    // Out there they know their own: a house of the place has a little the better of it.
    if (const DHouse* d = dhouseOf(b.house); d && d->place == s.place) edge += 0.1;
    return score * std::max(0.2, 1 + edge);
}

double World::stakeOdds(const Stake& s, Id house, double amount, Id speak, bool knownOnly) const {
    std::vector<DBid> bids;
    DBid mine;
    mine.house = house;
    mine.amount = amount;
    mine.speak = speak;
    std::vector<Id> men = stakeMen(s, house);
    mine.man = men.empty() ? kNone : men.front();
    for (const auto& b : s.bids) {
        if (b.house == house) {
            if (mine.speak == kNone) mine.speak = b.speak;
            if (b.man != kNone) mine.man = b.man;
            continue;
        }
        if (!knownOnly || b.known) bids.push_back(b);
    }
    bids.push_back(mine);
    double total = 0, me = 0;
    for (const auto& b : bids) {
        double v = std::pow(bidScore(s, b), 3);
        total += v;
        if (b.house == house) me = v;
    }
    return total > 0 ? me / total : 0;
}

void World::openRound(Stake& s) {
    s.open = true;
    s.openDay = day_;
    s.closeDay = day_ + static_cast<int>(defs_.district.numberOr("round_months", 2)) * kDaysPerMonth;
    s.bids.clear();
    s.carded = false;
    ++counters_["district:round"];
    npcBids(s);
    // The family hears of it when it is in 地方, has a son in town, or it is one of the ways in.
    const bool hears = dist_.in || !sonInTown().empty() || s.gate;
    if (!hears || actingHead() == kNone) return;
    if (stakeBar(s, playerHousehold_).empty() && !s.minor) bidCard(s, false);
}

void World::npcBids(Stake& s) {
    // The houses out there: the rising house goes after what makes a house (land, the lease, the steward's post, the
    // match), its head choosing how much through the decider like any villager.
    for (auto& d : dhouses_) {
        if (d.gone || d.head == kNone || !stakeBar(s, d.shell).empty()) continue;
        const bool rival = d.rival;
        double want = rival ? (s.kind == "land" || s.kind == "match" || s.key == "steward" || s.kind == "lease" ? 0.85 : 0.45) : 0;
        if (s.kind == "seat" && d.place == "neighbour" && d.acres >= 20 && !rival) want = 0.3;
        if (want <= 0 || !rngDistrict_.chance(want)) continue;
        const Person& hp = people_[d.head];
        std::vector<std::string> opts;
        std::vector<double> w;
        std::vector<int> lv;
        for (int l = 0; l < levelCount(); ++l) {
            double amt = std::round(s.base * levelMult(l) / 5) * 5;
            if (households_[d.shell].money < amt + 60) break;
            opts.push_back("出 " + pence(amt) + "爭" + s.name + "（" + levelName(l) + "）");
            w.push_back(l == 0 ? 3 : l == 1 ? (hasTrait(hp, "ambitious") ? 3 : 1.5) : (hasTrait(hp, "greedy") ? 0.5 : 1));
            lv.push_back(l);
        }
        if (opts.empty()) continue;
        opts.push_back("這回不出價");
        w.push_back(rival ? 0.4 : 1.5);
        std::vector<Id> about{d.head};
        if (Id ph = playerHead(); ph != kNone) about.push_back(ph);
        int r = npcDecide(hp, "出價：" + s.name, s.name + "要" + (s.kind == "land" ? "賣" : "給人") + "了，照價 " + pence(s.base) + "，家裡有 " +
                                                     pence(households_[d.shell].money) + "。",
                          opts, w, about, static_cast<int>(opts.size()) - 1);
        if (r < 0) r = rngDistrict_.weighted(w);
        if (r < 0 || r >= static_cast<int>(lv.size())) continue;
        std::vector<Id> men = stakeMen(s, d.shell);
        placeBid(s, d.shell, lv[static_cast<size_t>(r)], men.empty() ? kNone : men.front());
    }
    // The village's own houses with money put by step up too (and so into 地方: design/district.md 六).
    for (const auto& h : households_) {
        if (h.extinct || h.head == kNone || h.members.empty() || inPlayerHouse(h.id) || isDistrictShell(h.id)) continue;
        if (lord_ != kNone && h.id == people_[lord_].household) continue;
        if (people_[h.head].occupation == "priest" || standing(h.id) < 1) continue;
        if (!stakeBar(s, h.id).empty() || h.money < s.base + 150) continue;
        if (!rngDistrict_.chance(s.kind == "post" ? 0.4 : 0.25)) continue;
        std::vector<Id> men = stakeMen(s, h.id);
        placeBid(s, h.id, h.money > s.base * 2.5 && rngDistrict_.chance(0.3) ? 1 : 0, men.empty() ? kNone : men.front());
    }
}

void World::placeBid(Stake& s, Id house, int level, Id man) {
    level = std::clamp(level, 0, levelCount() - 1);
    const double amount = std::round(s.base * levelMult(level) / 5) * 5;
    DBid* b = nullptr;
    for (auto& o : s.bids)
        if (o.house == house) b = &o;
    if (!b) {
        s.bids.push_back(DBid{});
        b = &s.bids.back();
        b->house = house;
    }
    b->amount = amount;
    b->level = level;
    if (man != kNone) b->man = man;
    b->day = day_;
    Id head = houseHead(house);
    Id news = addNews("district:bid", head, kNone, house, kNone, s.name);
    if (inPlayerHouse(house)) {
        b->known = true;
        // The rival hears of it, and goes one better if he can (壓價 / 抬價: design/district.md 四).
        if (const DHouse* rv = districtRival())
            for (auto& o : s.bids)
                if (o.house == rv->shell && o.amount <= amount && o.level + 1 < levelCount() && rngDistrict_.chance(0.6)) {
                    const double more = std::round(s.base * levelMult(o.level + 1) / 5) * 5;
                    if (households_[rv->shell].money < more + 40) break;
                    o.amount = more;
                    ++o.level;
                    o.known = true;
                    o.heardFrom = "他聽說你家出了價，馬上加了碼";
                    log(1, "【地方】" + fullName(rv->head) + "聽說你家在爭" + s.name + "，把出價抬到了 " + pence(more) + "。");
                    break;
                }
        return;
    }
    // Whether the family hears of it: by a son in town, the family's posts out there, the village's talk.
    double hear = 0.4;
    std::string from = "市集上聽人說的";
    if (std::string son = sonInTown(); !son.empty()) hear += 0.4, from = son + "從鎮上捎話說的";
    for (const auto& o : stakes_)
        if ((o.kind == "post" || o.kind == "seat") && inPlayerHouse(o.holder)) hear += 0.2, from = "在" + postName(o.name) + "任上聽說的";
    if (!isDistrictShell(house)) hear += 0.3, from = "村裡都在傳";
    if (rngDistrict_.chance(hear)) {
        b->known = true;
        b->heardFrom = from;
        dHear(news, kNone);
    }
}

std::vector<std::pair<Id, std::string>> World::speakers(const Stake& s) const {
    std::vector<std::pair<Id, std::string>> out;
    const Id head = actingHead();
    if (head == kNone) return out;
    const Id dec = stakeDecider(s);
    auto add = [&](Id who, const std::string& how) {
        if (who == kNone || who == dec || who == head || !people_[who].alive()) return;
        for (const auto& o : out)
            if (o.first == who) return;
        out.push_back({who, how});
    };
    // A son at a clerk's desk or in town writes the letter, and knows whom to give it to.
    for (const auto& p : people_)
        if (p.alive() && isPlayerFamily(p) && ageYears(p) >= 14 && (p.station == "town_clerk" || p.station == "clerk" || p.occupation == "clerk")) add(p.id, "clerk");
    // A man of the family in a post out there.
    for (const auto& o : stakes_)
        if ((o.kind == "post" || o.kind == "seat") && inPlayerHouse(o.holder) && o.holderMan != kNone && o.key != s.key) add(o.holderMan, "post");
    // Kin out there: a daughter married into a house, the house a wife came from.
    for (const auto& d : dhouses_)
        if (!d.gone && d.head != kNone && districtKinHouse(d) && d.key != s.decider) add(d.head, "kin");
    // Anyone who owes the head a good turn and is someone the one who decides will listen to.
    std::vector<std::pair<double, Id>> owed;
    for (const auto& f : favours_)
        if (f.creditor == head && people_[f.debtor].alive() && favourNow(f) >= kFavourAsk && (dec == kNone || opinion(dec, f.debtor) >= 0 || dhouseOfPerson(f.debtor)))
            owed.push_back({-favourNow(f), f.debtor});
    std::sort(owed.begin(), owed.end());
    for (size_t i = 0; i < owed.size() && out.size() < 5; ++i) add(owed[i].second, "favour");
    // The lord himself, for a family he thinks well of (not for what he decides himself).
    if (lord_ != kNone && people_[lord_].alive() && s.decider != "lord" && opinion(lord_, head) >= 30) add(lord_, "lord");
    return out;
}

void World::speakFor(Stake& s, Id who) {
    const Id head = actingHead();
    DBid* mine = nullptr;
    for (auto& b : s.bids)
        if (inPlayerHouse(b.house)) mine = &b;
    if (!mine || head == kNone) return;
    std::string how;
    for (const auto& [p, h] : speakers(s))
        if (p == who) how = h;
    if (how.empty()) return;
    const double before = stakeOdds(s, playerHousehold_, mine->amount, mine->speak, true);
    std::string what;
    if (how == "favour") {
        const Favour* f = bestFavour(who, head);
        what = "還你家那份情（" + (f ? favourName(*f) : std::string("舊情")) + "）";
        spendFavour(who, head);
        ++counters_["district:speak_favour"];
    } else if (how == "lord") {
        what = "老爺寫了封信（老爺好感 -3：人情用一回少一回）";
        addOpinion(lord_, head, "asked_letter", -3, 4);
    } else {
        what = how == "clerk" ? "讀書人寫信說項" : how == "post" ? "在任上替家裡說話" : "姻親替你家說話";
        ++counters_["district:speak_" + how];
    }
    mine->speak = who;
    mine->speakHow = how;
    const double after = stakeOdds(s, playerHousehold_, mine->amount, who, true);
    log(1, "〔託人〕" + fullName(who) + "替你家去跟 " + (stakeDecider(s) != kNone ? fullName(stakeDecider(s)) : std::string("那邊")) + " 說了" + s.name + "的事：" + what +
               "。勝算約 " + odds(before) + " → " + odds(after) + "。");
}

// The card when bidding opens (or from the season's list): what it is, who decides and how they feel, the bids the
// family heard of, what each level costs and its odds, what losing means.
void World::bidCard(Stake& s, bool fromAgenda) {
    const Id head = actingHead();
    if (head == kNone) return;
    Household& h = households_[playerHousehold_];
    const int season = year() * 4 + month() / 3;
    // A human player who is not asked (the opening's cards are spent, one 地方 card a season) finds it on the panel.
    if (!fromAgenda && chooser_) {
        if (inOpening() || openingFull() || dist_.cardSeason == season) return;
    }
    if (!fromAgenda) dist_.cardSeason = season;
    s.carded = true;
    const Id dec = stakeDecider(s);
    std::vector<Id> men = stakeMen(s, playerHousehold_);
    const Id man = men.empty() ? kNone : men.front();
    std::string known;
    for (const auto& b : s.bids)
        if (b.known && !inPlayerHouse(b.house))
            known += (known.empty() ? "" : "；") + houseTag(b.house) + "出了 " + pence(b.amount) + (b.heardFrom.empty() ? "" : "（" + b.heardFrom + "）");
    std::string what = s.kind == "land"    ? "要賣，照價 " + pence(s.base) + "。買下來是你家的" + std::string(s.free ? "自由地" : "地") + "，租給那邊的人種，每年米迦勒節收租（壞年頭收得少）"
                       : s.kind == "lease" ? "要包出去：先送總管 " + pence(s.base) + "見面禮，往後每年米迦勒節交 " + pence(s.rent) + "地租，收成歸你家。壞年頭收成少，地租照付"
                       : s.kind == "post"  ? "要換人：照例要送 " + pence(s.base) + "的禮，當上了每年有 " + pence(s.fee) + "薪俸，也聽得到府裡的消息"
                       : s.kind == "place" ? "要收人：拜師錢 " + pence(s.base) + "。進了城，往後替家裡打聽價錢、牽線借貸"
                       : s.kind == "seat"  ? "出缺：送百戶長 " + pence(s.base) + "，當上了三年裡百戶法庭的官司都有你家一份說話"
                       : s.kind == "match" ? "要說人家：聘禮 " + pence(s.base) + "起，嫁妝是" + s.dowryName + "（" + num(s.dowryAcres) + " 畝自由地）"
                                           : "";
    std::string sit = cardText("district.bid.situation",
                               {{"place", defs_.district["places"].stringOr(s.place, "")},
                                {"name", s.name},
                                {"what", what},
                                {"for", man != kNone && s.kind != "land" && s.kind != "lease" ? "你家出面的是" + shortName(man) + "。" : ""},
                                {"decider", dec != kNone ? "拿主意的是" + fullName(dec) + "，對你家" + opinionWord(opinion(dec, houseHead(playerHousehold_))) + "（" +
                                                               num(opinion(dec, houseHead(playerHousehold_))) + "）。"
                                                         : ""},
                                {"bids", known.empty() ? "還沒聽說別家出價，" : "聽說的出價：" + known + "。"},
                                {"close", seasonOf(s.closeDay)},
                                {"money", pence(h.money)}});
    std::vector<std::string> opts, closed;
    std::vector<int> lv;
    for (int l = 0; l < levelCount(); ++l) {
        double amt = std::round(s.base * levelMult(l) / 5) * 5;
        std::string label = cardText("district.bid.option", {{"amount", pence(amt)}, {"level", levelName(l)}, {"odds", odds(stakeOdds(s, playerHousehold_, amt, kNone, true))}});
        if (h.money < amt) closed.push_back(label + "（錢不夠）");
        else opts.push_back(label), lv.push_back(l);
    }
    if (opts.empty()) return;
    opts.push_back(cardText("district.bid.pass"));
    int r = askPlayer("地方：" + s.name, sit, opts, closed);
    if (r < 0) {
        // The family's own ways: a gate is taken at the asking price when the purse can bear it and keep a reserve.
        const double reserve = std::max(200.0, h.money * 0.3);
        const double amt = std::round(s.base / 5) * 5;
        r = (s.gate || dist_.in) && h.money >= amt + reserve ? 0 : static_cast<int>(lv.size());
    }
    if (r >= static_cast<int>(lv.size())) {
        if (fromAgenda || chooser_) log(1, "〔地方：" + s.name + "〕這回不出價。（選擇：" + cardText("district.bid.pass") + "）");
        return;
    }
    if (fromAgenda && !spendTime(1)) {
        log(1, "〔地方：" + s.name + "〕這一季沒有空閒去出價了。");
        return;
    }
    placeBid(s, playerHousehold_, lv[static_cast<size_t>(r)], man);
    ++counters_["district:bid_ours"];
    log(1, "〔地方：" + s.name + "〕你家出了 " + pence(std::round(s.base * levelMult(lv[static_cast<size_t>(r)]) / 5) * 5) + "（" + levelName(lv[static_cast<size_t>(r)]) +
               "），" + seasonOf(s.closeDay) + "見分曉。輸了不花錢，贏了當場付清。（選擇：" + opts[static_cast<size_t>(r)] + "）");
}

void World::closeRound(Stake& s) {
    s.open = false;
    const Id dec = stakeDecider(s);
    // A bid the house can no longer pay, or whose man is gone, falls.
    std::vector<DBid> bids;
    for (const auto& b : s.bids) {
        const bool canPay = households_[b.house].money >= b.amount;
        const bool manOk = s.kind == "land" || s.kind == "lease" || (b.man != kNone && people_[b.man].alive());
        const bool houseOk = isDistrictShell(b.house) ? !dhouseOf(b.house)->gone : (!households_[b.house].extinct && households_[b.house].head != kNone);
        if (canPay && manOk && houseOk) bids.push_back(b);
        else if (inPlayerHouse(b.house)) log(1, "【地方】你家為" + s.name + "出的 " + pence(b.amount) + (canPay ? "作廢了：出面的人不在了。" : "拿不出來了，出價作廢。"));
    }
    if (bids.empty()) {
        s.nextOpen = year() + std::max(1, s.every > 0 ? s.every : 2);
        s.bids.clear();
        return;
    }
    std::vector<double> w;
    std::vector<std::string> opts;
    std::vector<Id> about;
    double total = 0;
    for (const auto& b : bids) total += std::pow(bidScore(s, b), 3);
    for (const auto& b : bids) {
        w.push_back(std::pow(bidScore(s, b), 3) / std::max(1e-9, total));
        opts.push_back(houseTag(b.house) + "出 " + pence(b.amount) + (b.speak != kNone ? "，" + fullName(b.speak) + "替他們說了話" : ""));
        about.push_back(houseHead(b.house));
    }
    int pick = -1;
    if (dec != kNone && bids.size() >= 2) pick = npcDecide(people_[dec], "誰拿到" + s.name, s.name + "有幾家在爭。", opts, w, about);
    if (pick < 0 || pick >= static_cast<int>(bids.size())) pick = rngDistrict_.weighted(w);
    if (pick < 0) pick = 0;
    s.bids = bids;
    const DBid win = bids[static_cast<size_t>(pick)];
    const double pWin = w[static_cast<size_t>(pick)];
    // The clash book: the family against each house it lost to or beat (never kin: fun-r12 A4).
    for (const auto& b : bids) {
        if (b.house == win.house) continue;
        Id ours = inPlayerHouse(win.house) ? win.house : inPlayerHouse(b.house) ? b.house : kNone;
        if (ours == kNone) continue;
        Id other = ours == win.house ? b.house : win.house;
        if (const DHouse* d = dhouseOf(other); d && districtKinHouse(*d)) continue;
        noteClash(ours, other, s.name + "：" + (ours == win.house ? "你家得手" : "他家得手"));
        ++counters_["district:clash"];
    }
    // What the family hears of how it went, and why.
    for (const auto& b : bids)
        if (inPlayerHouse(b.house) && b.house != win.house) {
            log(1, "【地方】" + s.name + "讓" + houseTag(win.house) + "拿走了（他家出 " + pence(win.amount) + (win.speak != kNone ? "，又託了 " + fullName(win.speak) : "") +
                       "；你家出 " + pence(b.amount) + "，原本勝算約 " + odds(stakeOdds(s, b.house, b.amount, b.speak, false)) + "）。");
            const DHouse* rv = districtRival();
            if (dist_.in && rv && win.house == rv->shell && b.level >= 1) districtBlow("宿敵搶走了" + s.name);
        }
    s.history.push_back(std::to_string(year()) + " 年 " + houseTag(win.house) + "拿到（" + pence(win.amount) + "）");
    (void)pWin;
    settleStake(s, static_cast<size_t>(pick));
    s.bids.clear();
    s.nextOpen = s.every > 0 ? year() + s.every : year() + 1;
}

void World::settleStake(Stake& s, size_t winner) {
    const DBid win = s.bids[winner];
    Household& wh = households_[win.house];
    DHouse* wd = dhouseOf(win.house);
    // Who the money goes to: the seller, the lord (the lease, the posts), the master, the hundredman, the bride's house.
    Id to = kNone;
    if (s.kind == "land") to = s.holder;
    else if (s.kind == "lease" || s.kind == "post") to = lord_ != kNone && people_[lord_].alive() ? people_[lord_].household : kNone;
    else if (const DHouse* d = dhouseByKey(s.kind == "place" ? s.master : s.kind == "match" ? s.house : s.decider)) to = d->shell;
    if (to == kNone) if (const DHouse* d = dhouseByKey(s.decider)) to = d->shell;
    const std::string item = s.kind == "land" ? "買外村的地" : s.kind == "lease" ? "包租的見面禮" : s.kind == "post" ? "謀差事的禮"
                             : s.kind == "place" ? "進城的拜師錢" : s.kind == "seat" ? "陪審席位的禮" : "下聘";
    wh.money -= win.amount;
    book(wh, item, -win.amount);
    if (to != kNone) {
        households_[to].money += win.amount;
        book(households_[to], s.kind == "land" ? "賣地" : "收的禮", win.amount);
    }
    const bool ours = inPlayerHouse(win.house);
    const Id man = win.man != kNone ? win.man : houseHead(win.house);
    std::string done;
    if (s.kind == "land") {
        if (DHouse* sd = dhouseOf(s.holder)) sd->acres = std::max(0.0, sd->acres - s.acres);
        if (wd) wd->acres += s.acres;
        else {
            wh.outAcres += s.acres;
            if (s.free) wh.outFree += s.acres;
        }
        s.holder = win.house;
        s.done = true;
        done = "買下了" + s.name;
    } else if (s.kind == "lease") {
        s.holder = win.house;
        s.holderMan = man;
        s.until = day_ + s.years * kDaysPerYear;
        done = "包下了" + s.name;
    } else if (s.kind == "post" || s.kind == "seat") {
        if (s.holderMan != kNone && people_[s.holderMan].alive() && s.holderMan != man && inPlayerHouse(s.holder))
            log(1, "【地方】" + fullName(s.holderMan) + "的" + postName(s.name) + "做到頭了。");
        s.holder = win.house;
        s.holderMan = man;
        s.until = s.years > 0 ? day_ + s.years * kDaysPerYear : -1;
        if (man != kNone) people_[man].flags.insert("district_post");
        done = (man != kNone ? shortName(man) : std::string("家裡")) + "當上了" + postName(s.name);
    } else if (s.kind == "place") {
        s.holderMan = man;
        if (man != kNone && !wd) {
            people_[man].flags.insert("district_place");
            setStation(man, s.station, s.years, "進了" + postName(s.name));
        }
        done = (man != kNone ? shortName(man) : std::string("孩子")) + "進了" + postName(s.name);
    } else if (s.kind == "match") {
        const Id bride = s.holderMan;
        DHouse* bh = dhouseByKey(s.house);
        if (bride != kNone && man != kNone && bh) {
            bh->members.erase(std::remove(bh->members.begin(), bh->members.end(), bride), bh->members.end());
            people_[bride].flags.insert("from:" + bh->key);
            bh->acres = std::max(0.0, bh->acres - s.dowryAcres);
            if (wd) {
                people_[bride].spouse = man;
                people_[man].spouse = bride;
                people_[bride].dynasty = wd->dynasty;
                wd->members.push_back(bride);
                wd->acres += s.dowryAcres;
            } else {
                marry(man, bride);
                wh.outAcres += s.dowryAcres;
                wh.outFree += s.dowryAcres;
            }
        }
        s.done = true;
        done = (man != kNone ? shortName(man) : std::string("兒子")) + "娶了" + (bride != kNone ? fullName(bride) : std::string("新娘")) + "，嫁妝是" + s.dowryName;
    }
    Id news = addNews("district:won", houseHead(win.house), kNone, win.house, kNone, s.name);
    if (ours) {
        ++counters_["district:won_ours"];
        log(1, "【地方】" + done + "（付了 " + pence(win.amount) + "）。");
        enterTier(win.house, done);
    } else {
        if (!wd) enterTier(win.house, done);
        for (const auto& b : s.bids)
            if (inPlayerHouse(b.house) || dist_.in) {
                dHear(news, kNone);
                break;
            }
        if (dist_.in || wd == nullptr) log(2, "【地方】" + houseTag(win.house) + done.substr(0) + "。");
    }
}

// ---- Into 地方, and out of it -------------------------------------------------------------------

void World::enterTier(Id house, const std::string& why) {
    if (house == kNone || isDistrictShell(house)) return;
    if (!inPlayerHouse(house)) {
        if (dist_.houses.emplace(house, why).second) {
            ++counters_["district:npc_in"];
            addNews("district:tier", households_[house].head, kNone, house, kNone, why);
        }
        return;
    }
    if (dist_.in) return;
    dist_.in = true;
    dist_.since = day_;
    dist_.gate = why;
    dist_.houses[house] = why;
    dist_.blows.clear(), dist_.blowWhy.clear();
    ++counters_["district:in"];
    const Household& h = households_[house];
    const Id head = h.head;
    log(1, "【地方】" + why + "：" + dynasties_[h.dynasty].surname + " 家從此在地方上有了名字。村子以外的人家、鎮上、領主府和百戶法庭，都要跟你家打交道了（「地方」那一欄打開了）。");
    // The village notices: those who were level with the family now see 「上面的人」 (design/district.md 四).
    std::string envious;
    int ne = 0;
    for (const auto& o : households_) {
        if (o.extinct || o.head == kNone || o.id == house || o.members.empty() || isDistrictShell(o.id)) continue;
        const Person& oh = people_[o.head];
        if (!oh.alive() || standing(o.id) < 0 || kinHouse(o.id)) continue;
        if (hasTrait(oh, "greedy") || hasTrait(oh, "ambitious") || opinion(oh.id, head) < -10 || o.id == rivalHouse()) {
            addOpinion(oh.id, head, "above_us", o.id == rivalHouse() ? -10 : -5, 8);
            if (ne++ < 3) envious += (envious.empty() ? "" : "、") + fullName(oh.id);
        }
    }
    if (!envious.empty()) log(1, "  " + envious + (ne > 3 ? " 等人" : "") + "如今把你家看成「上面的人」了（好感 -5，宿敵 -10）。");
    addNews("district:tier", head, kNone, house, kNone, why);
    if (const DHouse* rv = districtRival())
        log(1, "  在地方上，" + houseTag(rv->shell) + "（" + fullName(rv->head) + "）跟你家一樣在往上爬：外村的地、鄉紳家的親事、總管的位子，他都想要。");
}

void World::districtBlow(const std::string& why) {
    if (!dist_.in) return;
    dist_.blows.push_back(day_);
    dist_.blowWhy.push_back(why);
    ++counters_["district:blow"];
    const int need = static_cast<int>(defs_.district["tier"].numberOr("blows", 3));
    const int window = static_cast<int>(defs_.district["tier"].numberOr("blow_years", 4)) * kDaysPerYear;
    int n = 0;
    for (int d : dist_.blows)
        if (day_ - d < window) ++n;
    log(1, "【地方】挫折：" + why + "（" + std::to_string(window / kDaysPerYear) + " 年內第 " + std::to_string(n) + " 次；到第 " + std::to_string(need) + " 次，你家就在地方上站不住了）。");
    checkFall();
}

std::vector<std::string> World::footholds() const {
    std::vector<std::string> out;
    if (playerHousehold_ == kNone) return out;
    const Household& h = households_[playerHousehold_];
    if (h.outAcres > 0) out.push_back("外村有 " + num(h.outAcres) + " 畝地" + (h.outFree > 0 ? "（自由地 " + num(h.outFree) + " 畝）" : ""));
    for (const auto& s : stakes_) {
        if (s.kind == "lease" && inPlayerHouse(s.holder) && s.until > day_) out.push_back("包租著" + s.name.substr(0, s.name.find("（")) + "（到 " + std::to_string(cfg_.startYear + s.until / kDaysPerYear) + " 年）");
        if ((s.kind == "post" || s.kind == "seat") && inPlayerHouse(s.holder) && s.holderMan != kNone && people_[s.holderMan].alive())
            out.push_back(shortName(s.holderMan) + "當著" + postName(s.name));
    }
    for (const auto& p : people_)
        if (p.alive() && isPlayerFamily(p) && p.flags.count("district_place") && (p.station == "town_apprentice" || p.station == "town_clerk" || p.station == "town_journeyman" || p.station == "town_master"))
            out.push_back(p.firstName + stationName(p));
    for (const auto& d : dhouses_)
        if (!d.gone && districtKinHouse(d)) out.push_back("跟" + houseTag(d.shell) + "是親家");
    if (manorBond_.house == playerHousehold_ && manorBond_.lent >= defs_.district["tier"].numberOr("lord_gate", 400))
        out.push_back("領主欠你家 " + pence(manorBond_.owed));
    double lent = 0;
    for (const auto& l : dloans_)
        if (inPlayerHouse(l.lender) && l.state == 0 && !l.wool) lent += l.owed;
    if (lent >= 100) out.push_back("借給外頭的人 " + pence(lent));
    return out;
}

void World::checkFall() {
    if (!dist_.in) return;
    const int need = static_cast<int>(defs_.district["tier"].numberOr("blows", 3));
    const int window = static_cast<int>(defs_.district["tier"].numberOr("blow_years", 4)) * kDaysPerYear;
    std::vector<std::string> recent;
    for (size_t i = 0; i < dist_.blows.size(); ++i)
        if (day_ - dist_.blows[i] < window) recent.push_back(dist_.blowWhy[i]);
    std::string why;
    if (static_cast<int>(recent.size()) >= need) {
        for (const auto& r : recent) why += (why.empty() ? "" : "、") + r;
        why = "接連的挫折（" + why + "）";
    } else if (footholds().empty() && month() == 0) {
        why = "在地方上已經沒有一處立足的地方了";
    }
    if (why.empty()) return;
    dist_.in = false;
    dist_.houses.erase(playerHousehold_);
    dist_.blows.clear(), dist_.blowWhy.clear();
    ++counters_["district:fell"];
    log(1, "【地方】" + why + "：你家從地方上掉了下來，又只是" + villageName_ + "的一戶人家。（「地方」那一欄收起來了；買地、包租、讓兒子進城、跟外村結親或借錢給領主，都能再上去。）");
    if (Id ph = playerHead(); ph != kNone) addReputation(ph, -2, "在地方上跌了一跤");
}

void World::dHear(Id news, Id teller) {
    if (news == kNone || playerHousehold_ == kNone || news >= static_cast<Id>(news_.size())) return;
    learnNews(news_[static_cast<size_t>(news)], playerHousehold_, teller, 0.5);
}

// ---- Loans and wool -----------------------------------------------------------------------------

void World::seasonAsks() {
    dasks_.erase(std::remove_if(dasks_.begin(), dasks_.end(), [&](const DAsk& a) { return a.until <= day_; }), dasks_.end());
    const int until = day_ + 3 * kDaysPerMonth - 1;
    static const char* whys[] = {"春耕要買種子，犁牛又病了", "女兒要出嫁，嫁妝還差一截", "去年的租子還欠著領主府", "要付一場官司的錢", "房子塌了半邊要修"};
    for (auto& d : dhouses_) {
        if (d.gone || d.head == kNone || !d.borrow.isObject()) continue;
        bool owing = false;
        for (const auto& l : dloans_)
            if (l.borrower == d.shell && (l.state == 0 || l.state == 2 || l.state == 3)) owing = true;
        for (const auto& a : dasks_)
            if (a.house == d.shell) owing = true;
        if (owing || !rngDistrict_.chance(d.borrow.numberOr("chance", 0.2))) continue;
        DAsk a;
        a.id = dist_.nextAsk++;
        a.kind = "loan";
        a.house = d.shell;
        const Json& am = d.borrow["amount"];
        a.amount = std::round(rngDistrict_.range(am[0].asInt(), am[1].asInt()) / 10.0) * 10;
        a.rate = d.borrow.numberOr("rate", 0.15);
        a.years = static_cast<int>(d.borrow.numberOr("years", 1));
        a.pledge = std::min(d.borrow.numberOr("pledge", 0), std::floor(d.acres / 3));
        a.risk = d.borrow.numberOr("risk", 0.2);
        a.until = until;
        a.why = d.key == "wolman" ? "收毛的季節到了，要先付各村的現錢" : whys[rngDistrict_.range(0, 4)];
        dasks_.push_back(a);
    }
    // The wool merchant's share in the clip, every spring; the price it will fetch at Michaelmas is already in the
    // making (a son in town hears which way it goes).
    if (month() == static_cast<int>(defs_.district["wool"].numberOr("month", 3))) {
        const Json& W = defs_.district["wool"];
        const double spread = W.numberOr("spread", 0.3);
        dist_.woolNext = std::clamp(std::round(dist_.wool * (1 + (rngDistrict_.uniform() * 2 - 1) * spread)), W.numberOr("min", 32), W.numberOr("max", 110));
        const int truth = dist_.woolNext > dist_.wool * 1.03 ? 1 : dist_.woolNext < dist_.wool * 0.97 ? -1 : 0;
        dist_.woolHint = rngDistrict_.chance(W.numberOr("hint", 0.8)) ? truth : (truth == 0 ? (rngDistrict_.chance(0.5) ? 1 : -1) : -truth);
        if (const DHouse* m = dhouseByKey("wolman"); m && !m->gone) {
            DAsk a;
            a.id = dist_.nextAsk++;
            a.kind = "wool";
            a.house = m->shell;
            a.until = until;
            a.why = "今年剪的毛，米迦勒節照到時的價錢賣給佛蘭德斯人";
            dasks_.push_back(a);
        }
    }
}

void World::lendTo(int askId, int level) {
    auto it = std::find_if(dasks_.begin(), dasks_.end(), [&](const DAsk& a) { return a.id == askId; });
    if (it == dasks_.end() || playerHousehold_ == kNone) return;
    const DAsk a = *it;
    Household& h = households_[playerHousehold_];
    DLoan l;
    l.lender = playerHousehold_;
    l.borrower = a.house;
    l.since = day_;
    if (a.kind == "wool") {
        const Json& lv = defs_.district["wool"]["levels"];
        level = std::clamp(level, 0, static_cast<int>(lv.size()) - 1);
        l.lent = lv[static_cast<size_t>(level)].asNumber();
        l.wool = true;
        l.price = dist_.wool;
        l.due = year();
        l.owed = l.lent;
    } else {
        l.lent = a.amount;
        const bool pledged = level == 1 && a.pledge > 0;
        l.pledge = pledged ? a.pledge : 0;
        l.owed = std::round(a.amount * (1 + (a.rate - (pledged ? 0.05 : 0)) * a.years));
        l.due = year() + a.years;
    }
    if (h.money < l.lent) {
        log(1, "〔放貸〕手上的錢不夠借出 " + pence(l.lent) + "。");
        return;
    }
    h.money -= l.lent;
    book(h, l.wool ? "羊毛契約" : "放貸", -l.lent);
    households_[a.house].money += l.lent;
    dloans_.push_back(l);
    dasks_.erase(it);
    ++counters_[l.wool ? "district:wool" : "district:lent"];
    const Id bh = houseHead(a.house);
    if (l.wool) {
        log(1, "〔羊毛契約〕你家拿 " + pence(l.lent) + "入了 " + fullName(bh) + "今年的毛（現在的價錢 " + num(l.price) + "）。米迦勒節照到時的價錢分給你家，另加一成多的利；價錢跌了就賠。");
    } else {
        log(1, "〔放貸〕借給 " + fullName(bh) + "（" + houseTag(a.house) + "）" + pence(l.lent) + "，說好 " + std::to_string(l.due) + " 年米迦勒節還 " + pence(l.owed) +
                   (l.pledge > 0 ? "，拿 " + num(l.pledge) + " 畝地作抵押" : "，沒有抵押") + "。");
        if (bh != kNone && actingHead() != kNone) addOpinion(bh, actingHead(), "lent_us", 6, 4);
    }
}

void World::loansDue() {
    const bool bad = lastWeather_ < 0.85 || param("war_levy", 0) > 0 || param("plague_monthly", 0) > 0;
    const double margin = defs_.district["wool"].numberOr("margin", 1.12);
    for (size_t i = 0; i < dloans_.size(); ++i) {
        DLoan& l = dloans_[i];
        if (l.state != 0 || l.due > year()) continue;
        Household& lender = households_[l.lender];
        Household& borrower = households_[l.borrower];
        if (l.wool) {
            const double back = std::round(l.lent * dist_.woolNext / std::max(1.0, l.price) * margin);
            const double pay = std::min(back, borrower.money);
            borrower.money -= pay;
            lender.money += pay;
            book(lender, "羊毛契約", pay);
            l.state = 1;
            l.owed = pay;
            if (inPlayerHouse(l.lender)) {
                log(1, "【羊毛】今年的毛賣了，價錢從 " + num(l.price) + " " + (dist_.woolNext >= l.price ? "漲到" : "跌到") + " " + num(dist_.woolNext) + "：你家放進去 " + pence(l.lent) +
                           "，拿回 " + pence(pay) + "（" + (pay >= l.lent ? "賺了 " + pence(pay - l.lent) : "賠了 " + pence(l.lent - pay)) + "）。");
                if (l.lent - pay >= 120) districtBlow("羊毛價跌，賠了 " + pence(l.lent - pay));
            }
            continue;
        }
        double risk = 0.2;
        if (const DHouse* d = dhouseOf(l.borrower); d && d->borrow.isObject()) risk = d->borrow.numberOr("risk", 0.2);
        if (inPlayerHouse(l.borrower)) risk = 0;  // the family pays what it can, when it can
        const bool pays = borrower.money >= l.owed && rngDistrict_.chance(1 - risk * (bad ? 1.6 : 1));
        if (pays) {
            borrower.money -= l.owed;
            book(borrower, "還鎮上的債", -l.owed);
            lender.money += l.owed;
            book(lender, "收回的債", l.owed);
            l.state = 1;
            if (inPlayerHouse(l.lender)) log(1, "【放貸】" + fullName(houseHead(l.borrower)) + "按約還了 " + pence(l.owed) + "（借出 " + pence(l.lent) + "，賺了 " + pence(l.owed - l.lent) + "）。");
            if (inPlayerHouse(l.borrower)) log(1, "【地方】還了" + houseTag(l.lender) + " " + pence(l.owed) + "的債。");
            continue;
        }
        loanDefault(i);
    }
    // Wool settled: the price is the new price.
    dist_.wool = dist_.woolNext;
}

void World::loanDefault(size_t i) {
    DLoan& l = dloans_[i];
    const Id bh = houseHead(l.borrower);
    l.state = 2;
    if (inPlayerHouse(l.borrower)) {
        // The family cannot pay what it owes out there: the lender goes to the hundred court.
        log(1, "【地方】欠" + houseTag(l.lender) + "的 " + pence(l.owed) + "到期還不出來。" + fullName(houseHead(l.lender)) + "說要上百戶法庭告你家。");
        fileSuit("debt", l.lender, l.borrower, l.owed, 0.9, static_cast<int>(i), "欠債 " + pence(l.owed));
        return;
    }
    if (!inPlayerHouse(l.lender)) {
        l.state = 4;
        return;
    }
    Id news = addNews("district:default", bh, kNone, l.borrower);
    dHear(news, kNone);
    const double fee = defs_.district["court"].numberOr("fee", 6), att = defs_.district["court"].numberOr("attorney", 20);
    std::vector<std::string> opts;
    std::vector<int> kind;
    opts.push_back("上百戶法庭告他（規費 " + pence(fee) + "；請代訴人再加 " + pence(att) + "。陪審多半站在有借據的一邊，可也看人情）"), kind.push_back(0);
    if (!l.graced) opts.push_back("寬限一年，到時多收一成（" + pence(std::round(l.owed * 1.1)) + "）"), kind.push_back(1);
    if (l.pledge > 0) opts.push_back("收下抵押的 " + num(l.pledge) + " 畝地（他家會記恨）"), kind.push_back(2);
    opts.push_back("算了，這筆不要了（他家欠你家一份大人情）"), kind.push_back(3);
    const std::string sit = cardText("district.default.situation", {{"who", fullName(bh)}, {"house", houseTag(l.borrower)}, {"owed", pence(l.owed)}, {"lent", pence(l.lent)},
                                                                    {"year", std::to_string(cfg_.startYear + l.since / kDaysPerYear)}});
    int r = askPlayer("地方：債收不回來", sit, opts);
    int k;
    if (r >= 0 && r < static_cast<int>(kind.size())) k = kind[static_cast<size_t>(r)];
    else k = l.pledge > 0 ? 2 : l.owed >= 60 ? 0 : !l.graced ? 1 : 3;
    if (r >= 0) log(1, "〔地方：債收不回來〕" + fullName(bh) + "還不出 " + pence(l.owed) + "。（選擇：" + opts[static_cast<size_t>(r)] + "）");
    if (k == 0) {
        l.state = 3;
        fileSuit("debt", l.lender, l.borrower, l.owed, 0.85, static_cast<int>(i), "欠債 " + pence(l.owed));
    } else if (k == 1) {
        l.state = 0;
        l.graced = true;
        l.owed = std::round(l.owed * 1.1);
        ++l.due;
        log(1, "【放貸】給 " + fullName(bh) + "寬限一年：" + std::to_string(l.due) + " 年米迦勒節還 " + pence(l.owed) + "。");
    } else if (k == 2) {
        l.state = 5;
        Household& h = households_[playerHousehold_];
        DHouse* d = dhouseOf(l.borrower);
        const double acres = d ? std::min(l.pledge, d->acres) : 0;
        if (d) d->acres -= acres;
        h.outAcres += acres;
        h.outFree += acres;
        if (bh != kNone && actingHead() != kNone) addOpinion(bh, actingHead(), "took_pledge", -15, 10);
        log(1, "【放貸】" + fullName(bh) + "還不出錢，抵押的 " + num(acres) + " 畝地歸了你家（" + fullName(bh) + "對你 -15）。");
        enterTier(playerHousehold_, "收下了抵債的外村地");
    } else {
        l.state = 4;
        if (bh != kNone && actingHead() != kNone) favourLine(bh, actingHead(), 20, "免了欠你家的 " + pence(l.owed));
        log(1, "【放貸】借給 " + fullName(bh) + "的 " + pence(l.lent) + "不要了。");
        if (l.lent >= 150) districtBlow("一大筆債收不回來（" + pence(l.lent) + "）");
    }
}

// ---- The hundred court --------------------------------------------------------------------------

void World::fileSuit(const std::string& kind, Id plaintiff, Id defendant, double amount, double merit, int loan, const std::string& what) {
    DSuit s;
    s.kind = kind;
    s.plaintiff = plaintiff;
    s.defendant = defendant;
    s.amount = amount;
    s.merit = merit;
    s.loan = loan;
    s.filed = day_;
    s.what = what;
    dsuits_.push_back(s);
    ++counters_["district:suit"];
    if (inPlayerHouse(defendant)) log(1, "【百戶法庭】" + houseTag(plaintiff) + "（" + fullName(houseHead(plaintiff)) + "）為了「" + what + "」把你家告上了百戶法庭，下一個開庭日審。");
    else if (inPlayerHouse(plaintiff)) log(1, "【百戶法庭】你家為了「" + what + "」告了 " + fullName(houseHead(defendant)) + "，下一個開庭日審。");
}

void World::hundredCourt() {
    const Json& C = defs_.district["court"];
    const double fee = C.numberOr("fee", 6), att = C.numberOr("attorney", 20), acrePrice = C.numberOr("acre_price", 40);
    const Id hundredman = [&] {
        const DHouse* d = dhouseByKey("brun");
        return d && !d->gone ? d->head : kNone;
    }();
    for (size_t i = 0; i < dsuits_.size(); ++i) {
        if (dsuits_[i].done || dsuits_[i].filed >= day_) continue;
        DSuit s = dsuits_[i];
        const Id P = houseHead(s.plaintiff), D = houseHead(s.defendant);
        if (P == kNone || D == kNone || !people_[P].alive() || !people_[D].alive()) {
            dsuits_[i].done = true;
            dsuits_[i].result = "沒審成：有一邊的人家沒了";
            continue;
        }
        const bool ours = inPlayerHouse(s.plaintiff) || inPlayerHouse(s.defendant);
        // The family's side: plead itself, an attorney, or a son who can write and speak for it (託人).
        double edge = 0;
        if (ours) {
            Household& h = households_[playerHousehold_];
            Id clerk = kNone;
            for (const auto& [who, how] : speakers(Stake{}))
                if (how == "clerk" && clerk == kNone) clerk = who;
            std::vector<std::string> opts{"自己去說（不花錢）"};
            std::vector<int> kind{0};
            if (h.money >= att) opts.push_back("請代訴人（" + pence(att) + "，勝算多一成）"), kind.push_back(1);
            if (clerk != kNone) opts.push_back("讓" + shortName(clerk) + "替家裡說話（他識字懂規矩，勝算多一成半，不花錢）"), kind.push_back(2);
            const std::string role = inPlayerHouse(s.plaintiff) ? "你家告 " + fullName(D) : fullName(P) + "告你家";
            const std::string merit = (inPlayerHouse(s.plaintiff) ? "你家的理由站得住的把握大約 " : "對方的理由站得住的把握大約 ") + odds(s.merit);
            int r = askPlayer("百戶法庭", cardText("district.court.situation", {{"case", role}, {"what", s.what}, {"merit", merit}}), opts);
            int k = r >= 0 && r < static_cast<int>(kind.size()) ? kind[static_cast<size_t>(r)] : (clerk != kNone ? 2 : h.money >= att + 150 ? 1 : 0);
            if (k == 1) {
                h.money -= att;
                book(h, "代訴人", -att);
                if (const DHouse* sc = dhouseByKey("scrivener")) households_[sc->shell].money += att;
                edge += 0.1;
                dsuits_[i].attorney = true;
            } else if (k == 2) {
                edge += 0.15;
                dsuits_[i].speak = clerk;
                ++counters_["district:speak_court"];
            }
            for (const auto& st : stakes_)
                if (st.kind == "seat" && inPlayerHouse(st.holder)) edge += 0.05;
            if (inPlayerHouse(s.defendant)) edge = -edge;
            if (r >= 0) log(1, "〔百戶法庭〕" + role + "（選擇：" + opts[static_cast<size_t>(r)] + "）");
        }
        if (hundredman != kNone) edge += std::clamp((opinion(hundredman, P) - opinion(hundredman, D)) / 300, -0.1, 0.1);
        // The jurors: the juror's seat, the substantial men of both villages, not the parties.
        std::vector<Id> pool;
        for (const auto& st : stakes_)
            if (st.kind == "seat" && st.holderMan != kNone && people_[st.holderMan].alive() && st.holder != s.plaintiff && st.holder != s.defendant) pool.push_back(st.holderMan);
        // The hundred is many villages: the grown men of the houses out there sit beside the village's own, the
        // village's at most half the panel (else a quarrel with an outsider is judged by the family's neighbours).
        for (const auto& d : dhouses_)
            if (!d.gone && d.shell != s.plaintiff && d.shell != s.defendant)
                for (Id m : d.members)
                    if (people_[m].alive() && people_[m].sex == Sex::Male && ageYears(people_[m]) >= 21 && std::find(pool.begin(), pool.end(), m) == pool.end()) pool.push_back(m);
        std::vector<Id> village;
        for (const auto& o : households_) {
            if (o.extinct || o.head == kNone || o.members.empty() || o.id == s.plaintiff || o.id == s.defendant) continue;
            if (lord_ != kNone && o.id == people_[lord_].household) continue;
            // Not a man who plainly leans to one side (the hundred's bailiff picks those who will hear it out).
            if (standing(o.id) >= 1 && o.money >= 60 && people_[o.head].sex == Sex::Male && ageYears(people_[o.head]) >= 21 &&
                std::abs(opinion(o.head, P) - opinion(o.head, D)) < 20)
                village.push_back(o.head);
        }
        for (size_t k = village.size(); k > 1; --k) std::swap(village[k - 1], village[static_cast<size_t>(rngDistrict_.range(0, static_cast<int>(k) - 1))]);
        const size_t want = static_cast<size_t>(C.numberOr("jurors", 8));
        if (pool.size() > want / 2) pool.resize(want / 2);
        for (Id v : village)
            if (pool.size() < want && std::find(pool.begin(), pool.end(), v) == pool.end()) pool.push_back(v);
        std::vector<Id> ayes;
        int panel = 0;
        const std::string what = fullName(P) + " 為了「" + s.what + "」告 " + fullName(D);
        const int votes = neighbourJury(D, P, std::clamp(s.merit + edge, 0.05, 0.95), ayes, panel, what, &pool, &rngDistrict_);
        // The hundred goes by the writing and the bounds as the jurors find them: half the panel is enough (the manor
        // court asks two thirds of a kinsman suing his own).
        const bool won = panel <= 1 ? votes > 0 : votes * 2 >= panel;
        // Costs: the plaintiff's fee to the hundred; the loser pays the winner's.
        Household& ph = households_[s.plaintiff];
        Household& dh = households_[s.defendant];
        Id hundred = kNone;
        if (const DHouse* b = dhouseByKey("brun")) hundred = b->shell;
        const double f = std::min(fee, ph.money);
        ph.money -= f;
        book(ph, "百戶法庭的規費", -f);
        if (hundred != kNone) households_[hundred].money += f;
        Household& loser = won ? dh : ph;
        Household& winner = won ? ph : dh;
        const double costs = std::min(fee, loser.money);
        loser.money -= costs;
        book(loser, "賠對方的訟費", -costs);
        winner.money += costs;
        book(winner, "對方賠的訟費", costs);
        std::string result;
        if (s.kind == "debt") {
            DLoan* l = s.loan >= 0 && s.loan < static_cast<int>(dloans_.size()) ? &dloans_[static_cast<size_t>(s.loan)] : nullptr;
            if (won) {
                double owed = s.amount;
                const double pay = std::min(owed, dh.money);
                dh.money -= pay;
                book(dh, "法庭判還的債", -pay);
                ph.money += pay;
                book(ph, "法庭判還的債", pay);
                owed -= pay;
                double acres = 0;
                if (owed > 1) {
                    // What the purse cannot pay, land pays at the court's price.
                    if (DHouse* dd = dhouseOf(s.defendant)) acres = std::min(dd->acres, std::ceil(owed / acrePrice)), dd->acres -= acres;
                    else acres = std::min(dh.outAcres, std::ceil(owed / acrePrice)), dh.outAcres -= acres, dh.outFree = std::min(dh.outFree, dh.outAcres);
                    if (DHouse* pd = dhouseOf(s.plaintiff)) pd->acres += acres;
                    else ph.outAcres += acres, ph.outFree += acres;
                }
                result = "判" + fullName(P) + "有理：還了 " + pence(pay) + (acres > 0 ? "，不夠的拿 " + num(acres) + " 畝地抵" : "");
                if (l) l->state = 1;
                if (inPlayerHouse(s.plaintiff) && acres > 0) enterTier(playerHousehold_, "法庭判給你家抵債的外村地");
                if (inPlayerHouse(s.defendant)) districtBlow("百戶法庭判你家還債");
            } else {
                result = "判" + fullName(D) + "有理：借據說不清，債一筆勾銷";
                if (l) l->state = 4;
                if (inPlayerHouse(s.plaintiff) && s.amount >= 60) districtBlow("債的官司打輸了（" + pence(s.amount) + "）");
            }
        } else if (s.kind == "bounds") {
            if (won) {
                const double acres = std::min(s.amount, dh.outAcres);
                dh.outAcres -= acres;
                dh.outFree = std::min(dh.outFree, dh.outAcres);
                if (DHouse* pd = dhouseOf(s.plaintiff)) pd->acres += acres;
                result = "判" + fullName(P) + "有理：地界往你家這邊挪，" + num(acres) + " 畝劃給了他家";
                if (inPlayerHouse(s.defendant)) districtBlow("地界的官司輸了，丟了 " + num(acres) + " 畝");
            } else {
                result = "判" + fullName(D) + "有理：地界照舊";
            }
        } else {  // oppress: 仗勢欺人
            if (won) {
                const double pay = std::min(s.amount, dh.money);
                dh.money -= pay;
                book(dh, "賠人家的錢", -pay);
                ph.money += pay;
                book(ph, "法庭判賠的錢", pay);
                if (D != kNone) addReputation(D, -3, "百戶法庭判你家仗勢欺人");
                result = "判" + fullName(P) + "有理：你家賠 " + pence(pay);
                if (inPlayerHouse(s.defendant)) districtBlow("被判仗勢欺人");
            } else {
                result = "判" + fullName(D) + "有理：告不倒";
            }
        }
        dsuits_[i].done = true;
        dsuits_[i].result = result;
        ++counters_[won ? "district:suit_plaintiff" : "district:suit_defendant"];
        Id news = addNews("district:verdict", P, D, s.plaintiff, kNone, fullName(P) + "告" + fullName(D) + "：" + result);
        if (ours) {
            dHear(news, kNone);
            log(1, "【百戶法庭】" + what + "：陪審 " + std::to_string(panel) + " 人，" + std::to_string(votes) + " 人說原告有理。" + result + "。" + juryAside());
            Id other = inPlayerHouse(s.plaintiff) ? s.defendant : s.plaintiff;
            const bool weWon = inPlayerHouse(s.plaintiff) == won;
            const DHouse* od = dhouseOf(other);
            if (!(od && districtKinHouse(*od))) noteClash(playerHousehold_, other, "百戶法庭「" + s.what + "」：" + (weWon ? "你家贏" : "他家贏"));
        } else if (juryHeadSat_) {
            log(1, "【百戶法庭】" + what + "：" + result + "。" + juryAside());
        }
    }
}

// ---- The rival out there ------------------------------------------------------------------------

// Once a year the rising house in the neighbouring village decides what it will do besides bidding: go to law over
// the bounds of the land the family bought out there, speak against the family where it is bidding, lend to a house
// short of money against its land, or bide its time. The rules list them; the decider picks (logged for replay).
void World::rivalYear() {
    DHouse* rvp = nullptr;
    for (auto& d : dhouses_)
        if (d.rival && !d.gone && d.head != kNone) rvp = &d;
    if (!rvp || actingHead() == kNone) return;
    DHouse& rv = *rvp;
    // A match or a wedding made them kin: no longer the rival (fun-r12 A4).
    if (!dist_.rivalKin && districtKinHouse(rv)) {
        dist_.rivalKin = true;
        log(1, "【地方】" + houseTag(rv.shell) + "跟你家成了親家，從此不再是對頭。");
        return;
    }
    if (dist_.rivalKin) return;
    if (rv.head != dist_.rivalHead) {
        if (dist_.rivalHead != kNone && dist_.in) log(1, "【地方】" + houseTag(rv.shell) + "換了 " + fullName(rv.head) + "當家：他也想往上爬。");
        dist_.rivalHead = rv.head;
    }
    const Person& hp = people_[rv.head];
    const Id me = actingHead();
    std::vector<std::string> opts;
    std::vector<double> w;
    std::vector<int> act;
    const Stake* brook = nullptr;
    for (const auto& s : stakes_)
        if (s.kind == "land" && !s.bounds.empty() && s.bounds == rv.key && inPlayerHouse(s.holder)) brook = &s;
    bool suing = false;
    for (const auto& s : dsuits_)
        if (s.plaintiff == rv.shell && (!s.done || day_ - s.filed < 4 * kDaysPerYear)) suing = true;  // one suit over the bounds in four years
    if (brook && !suing && households_[playerHousehold_].outAcres >= 2 && households_[rv.shell].money >= 40)
        opts.push_back("為了" + brook->name + "的地界，到百戶法庭告" + houseTag(playerHousehold_)), w.push_back(hasTrait(hp, "wrathful") ? 2.5 : 1.2), act.push_back(0);
    const Stake* next = nullptr;
    for (const auto& s : stakes_)
        if (!s.done && !s.minor && stakeDecider(s) != kNone && stakeDecider(s) != rv.head && (s.open || s.nextOpen <= year() + 1) && (!next || s.nextOpen < next->nextOpen)) next = &s;
    if (dist_.in && next) opts.push_back("在 " + fullName(stakeDecider(*next)) + "面前說你家的不是（為了" + next->name + "）"), w.push_back(hasTrait(hp, "ambitious") ? 2 : 1), act.push_back(1);
    DHouse* hadley = dhouseByKey("hadley");
    if (hadley && !hadley->gone && hadley->acres >= 8 && households_[rv.shell].money >= 160 && households_[hadley->shell].money < 80)
        opts.push_back("借錢給" + houseTag(hadley->shell) + "，拿他家的地作抵"), w.push_back(hasTrait(hp, "greedy") ? 2 : 1), act.push_back(2);
    opts.push_back("今年按兵不動");
    w.push_back(1.5);
    act.push_back(-1);
    int r = npcDecide(hp, "這一年怎麼對付" + houseTag(playerHousehold_), "你家在布雷德利村往上爬，" + villageName_ + "的" + dynasties_[playerDynasty_].surname + "家也是。", opts, w,
                      {rv.head, me}, static_cast<int>(opts.size()) - 1);
    if (r < 0) r = rngDistrict_.weighted(w);
    const int a = r >= 0 && r < static_cast<int>(act.size()) ? act[static_cast<size_t>(r)] : -1;
    if (a == 0) {
        fileSuit("bounds", rv.shell, playerHousehold_, 2, 0.55 + 0.35 * rngDistrict_.uniform(), -1, brook->name + "的地界");
        noteClash(playerHousehold_, rv.shell, "告你家" + brook->name + "的地界");
    } else if (a == 1) {
        const Id dec = stakeDecider(*next);
        addOpinion(dec, me, "rival_slander", -8, 4);
        Id n = addNews("district:slander", rv.head, dec, rv.shell);
        const bool heard = !sonInTown().empty() || rngDistrict_.chance(0.5);
        if (heard) {
            dHear(n, kNone);
            log(1, "【地方】聽說 " + fullName(rv.head) + "在 " + fullName(dec) + "面前說了你家不少壞話（為了" + next->name + "；" + fullName(dec) + "對你 -8）。");
            noteClash(playerHousehold_, rv.shell, "在 " + fullName(dec) + "面前說你家的不是");
        }
    } else if (a == 2) {
        households_[rv.shell].money -= 100;
        households_[hadley->shell].money += 100;
        if (rngDistrict_.chance(0.5)) {
            hadley->acres -= 3, rv.acres += 3;
            if (dist_.in) log(2, "【地方】" + houseTag(hadley->shell) + "欠了布萊克伍德家的錢還不出，3 畝地抵給了他家。");
        }
    }
    // The village's old rival sees the family up among them now: he takes it to law as a bully (design/district.md 四).
    if (dist_.in)
        if (Id vr = rivalHouse(); vr != kNone && rngDistrict_.chance(0.15)) {
            bool open = false;
            for (const auto& s : dsuits_)
                if (s.plaintiff == vr && (!s.done || day_ - s.filed < 5 * kDaysPerYear)) open = true;
            if (!open && households_[vr].money >= 20) fileSuit("oppress", vr, playerHousehold_, 20, 0.35 + 0.4 * rngDistrict_.uniform(), -1, "仗勢欺人");
        }
}

// A house out there asks for a daughter of the family (a way into 地方 when she goes: design/district.md 五).
void World::proposals() {
    const Id head = actingHead();
    if (head == kNone) return;
    Household& h = households_[playerHousehold_];
    const Json& P = defs_.district["proposal"];
    if (!rngDistrict_.chance(P.numberOr("chance", 0.3))) return;
    Id girl = kNone;
    for (Id m : h.members) {
        const Person& p = people_[m];
        const double a = ageYears(p);
        if (p.sex == Sex::Female && m != head && a >= P.numberOr("min_age", 16) && a <= P.numberOr("max_age", 26) && marriageable(p) && p.home == kNone &&
            (girl == kNone || p.birthDay < people_[girl].birthDay))
            girl = m;
    }
    if (girl == kNone) return;
    DHouse* d = nullptr;
    Id groom = kNone;
    for (auto& o : dhouses_) {
        if (o.gone || o.place != "neighbour" || o.gentry) continue;
        for (Id m : o.members) {
            const Person& p = people_[m];
            if (p.alive() && p.sex == Sex::Male && ageYears(p) >= 18 && ageYears(p) <= 35 && (p.spouse == kNone || !people_[p.spouse].alive())) {
                d = &o, groom = m;
                break;
            }
        }
        if (d) break;
    }
    if (!d) return;
    const double dowry = std::round(std::clamp(h.money * P.numberOr("dowry", 0.12), 30.0, std::max(30.0, dowryCap(h))));
    const bool rival = d->rival && !dist_.rivalKin;
    std::string sit = cardText("district.propose.situation", {{"house", houseTag(d->shell)}, {"groom", fullName(groom)}, {"age", num(std::floor(ageYears(people_[groom])))},
                                                              {"girl", fullName(girl)}, {"dowry", pence(dowry)},
                                                              {"rival", rival ? cardText("district.propose.rival", {{"house", houseTag(d->shell)}}) : ""}});
    std::vector<std::string> opts{"答應（嫁妝 " + pence(dowry) + "；你家在地方上有了姻親）", "回絕"};
    std::vector<std::string> closed;
    if (h.money < dowry) {
        opts.erase(opts.begin());
        closed.push_back("答應（嫁妝 " + pence(dowry) + "，錢不夠）");
    }
    int r = askPlayer("外村來說親", sit, opts, closed);
    bool yes = h.money >= dowry && (r == 0 || (r < 0 && !rival && h.money >= dowry * 3 && rngDistrict_.chance(0.5)));
    if (h.money < dowry) yes = false;
    if (!yes) {
        log(1, "〔外村來說親〕" + houseTag(d->shell) + "託人來說，想替 " + fullName(groom) + "娶 " + fullName(girl) + "。家裡回絕了（" + fullName(d->head) + "對你 -5）。" +
                   (r >= 0 ? "（選擇：回絕）" : ""));
        addOpinion(d->head, head, "refused_match", -5, 5);
        return;
    }
    Person& w = people_[girl];
    h.money -= dowry;
    book(h, "嫁妝", -dowry);
    households_[d->shell].money += dowry;
    chargeMerchet(girl, true);
    h.members.erase(std::remove(h.members.begin(), h.members.end(), girl), h.members.end());
    w.household = kNone;
    w.station.clear();
    w.spouse = groom;
    people_[groom].spouse = girl;
    for (Person* x : {&w, &people_[groom]}) x->flags.insert("wed:" + std::to_string(day_));
    noteLife(girl, "嫁到了" + houseTag(d->shell));
    noteLife(groom, "娶了" + fullName(girl));
    d->members.push_back(girl);
    ++counters_["district:married_out"];
    log(1, "〔外村來說親〕" + fullName(girl) + "嫁給了" + houseTag(d->shell) + "的 " + fullName(groom) + "，嫁妝 " + pence(dowry) + "。" + (r >= 0 ? "（選擇：" + opts[0] + "）" : ""));
    enterTier(playerHousehold_, "女兒嫁進了" + houseTag(d->shell));
    if (rival) {
        dist_.rivalKin = true;
        log(1, "【地方】" + houseTag(d->shell) + "跟你家成了親家，從此不再是對頭。");
    }
}

// ---- Michaelmas out there ---------------------------------------------------------------------------

void World::districtMichaelmas() {
    if (!dist_.built || playerHousehold_ == kNone) return;
    const double weather = std::clamp(lastWeather_, 0.4, 1.3);
    Id manor = lord_ != kNone && people_[lord_].alive() ? people_[lord_].household : kNone;
    // The farms on lease: the harvest is the house's, the rent is the lord's whatever the summer (design/district.md 四).
    for (auto& s : stakes_) {
        if (s.kind != "lease" || s.holder == kNone || isDistrictShell(s.holder)) continue;
        Household& h = households_[s.holder];
        if (h.extinct || h.head == kNone) {
            s.holder = kNone, s.until = -1;
            continue;
        }
        const double crop = std::round(s.acres * s.yield * weather);
        h.money += crop;
        book(h, "包租地的收成", crop);
        double rent = s.rent;
        std::string line = "【包租】" + s.name.substr(0, s.name.find("（")) + "今年收成賣了 " + pence(crop) + "，地租 " + pence(rent);
        if (h.money < rent) {
            const double short_ = rent - h.money;
            if (inPlayerHouse(h.id)) {
                // The town's lender covers it, at his rate (design/district.md 四: 壞年頭地租照付).
                DHouse* lender = dhouseByKey(defs_.district["lender"].stringOr("house", "draper"));
                if (lender && !lender->gone && households_[lender->shell].money >= short_) {
                    const double owed = std::round(short_ * (1 + defs_.district["lender"].numberOr("rate", 0.25)));
                    households_[lender->shell].money -= short_;
                    h.money += short_;
                    book(h, "向鎮上借的錢", short_);
                    DLoan l;
                    l.lender = lender->shell, l.borrower = h.id, l.lent = short_, l.owed = owed, l.due = year() + 1, l.since = day_;
                    dloans_.push_back(l);
                    line += "；手上不夠，向" + houseTag(lender->shell) + "借了 " + pence(short_) + "（明年米迦勒節還 " + pence(owed) + "）";
                    ++counters_["district:borrowed"];
                } else {
                    rent = h.money;
                    line += "；只交得出 " + pence(rent) + "，總管把地收了回去";
                    s.until = day_;
                }
            } else {
                rent = h.money;
                s.until = day_;
            }
        }
        h.money -= rent;
        book(h, "包租的地租", -rent);
        if (manor != kNone) households_[manor].money += rent;
        if (inPlayerHouse(h.id)) log(1, line + "，算下來" + (crop >= rent ? "賺了 " + pence(crop - rent) : "賠了 " + pence(rent - crop)) + "。");
        if (inPlayerHouse(h.id) && crop + 40 < rent) districtBlow("包租的地遇上壞年頭，賠了 " + pence(rent - crop));
        if (s.until >= 0 && s.until <= day_ + kDaysPerMonth) {
            if (inPlayerHouse(h.id)) log(1, "【包租】" + s.name.substr(0, s.name.find("（")) + "的租約到期了。");
            s.holder = kNone, s.holderMan = kNone, s.until = -1;
            s.nextOpen = std::min(s.nextOpen, year() + 1);
        }
    }
    // Land out there: let to tenants for a money rent, less in a bad year.
    const double rentAcre = defs_.district.numberOr("out_rent", 9);
    for (auto& h : households_) {
        if (h.extinct || h.head == kNone || h.outAcres <= 0) continue;
        const double rent = std::round(h.outAcres * rentAcre * std::min(1.1, weather));
        h.money += rent;
        book(h, "外村地的租", rent);
        if (inPlayerHouse(h.id)) log(1, "【地方】外村那 " + num(h.outAcres) + " 畝地的佃戶交了租，" + pence(rent) + (weather < 0.85 ? "（年頭不好，收得少）" : "") + "。");
    }
    // The posts' fees, from the lord's purse.
    for (auto& s : stakes_) {
        if (s.kind != "post" || s.holder == kNone || isDistrictShell(s.holder) || s.holderMan == kNone || !people_[s.holderMan].alive()) continue;
        Household& h = households_[s.holder];
        if (h.extinct || manor == kNone) continue;
        const double fee = std::min(s.fee, households_[manor].money);
        households_[manor].money -= fee;
        book(households_[manor], "差事的薪俸", -fee);
        h.money += fee;
        book(h, "差事的薪俸", fee);
        if (inPlayerHouse(h.id)) log(1, "【地方】" + shortName(s.holderMan) + "當" + postName(s.name) + "，領了今年的薪俸 " + pence(fee) + (fee < s.fee ? "（府裡的錢一時不夠，欠著）" : "") + "。");
    }
    // Loans and the wool fall due.
    loansDue();
    // The houses out there live by their own fields and trade.
    for (auto& d : dhouses_) {
        if (d.gone) continue;
        double gain = d.income * weather * (0.85 + 0.3 * rngDistrict_.uniform()) * (1 - d.spend);
        if (d.key == "wolman") gain *= dist_.wool / std::max(1.0, defs_.district["wool"].numberOr("price", 60));
        households_[d.shell].money = std::max(0.0, households_[d.shell].money + std::round(gain) - (weather < 0.8 ? d.income * 0.15 : 0));
    }
}

// ---- What the family can do: the verbs and the season's list ------------------------------------------

void World::districtCommand(const Command& c) {
    if (!dist_.built || playerHousehold_ == kNone || actingHead() == kNone) return;
    std::vector<std::string> part;
    for (size_t at = 0, next; at <= c.plan.size(); at = next + 1) {
        next = c.plan.find('|', at);
        if (next == std::string::npos) next = c.plan.size();
        part.push_back(c.plan.substr(at, next - at));
    }
    if (part.size() < 3) return;
    const std::string& verb = part[0];
    const int arg = toIndex(part[2]);
    if (timeLeft() < 1) {
        log(1, "〔地方〕這一季沒有空閒了。");
        return;
    }
    if (verb == "lend") {
        const int id = toIndex(part[1]);
        auto it = std::find_if(dasks_.begin(), dasks_.end(), [&](const DAsk& a) { return a.id == id; });
        if (it == dasks_.end()) {
            log(1, "〔放貸〕這筆借錢的事已經過了。");
            return;
        }
        spendTime(1);
        lendTo(id, arg);
        return;
    }
    Stake* s = stakeByKey(part[1]);
    if (!s || !s->open) {
        log(1, "〔地方〕" + (s ? s->name : std::string("這件事")) + "眼下沒在爭。");
        return;
    }
    if (verb == "bid") {
        if (std::string bar = stakeBar(*s, playerHousehold_); !bar.empty()) {
            log(1, "〔出價〕" + s->name + "：" + bar + "。");
            return;
        }
        const double amt = std::round(s->base * levelMult(arg) / 5) * 5;
        if (households_[playerHousehold_].money < amt) {
            log(1, "〔出價〕" + s->name + "：錢不夠（要 " + pence(amt) + "）。");
            return;
        }
        spendTime(1);
        std::vector<Id> men = stakeMen(*s, playerHousehold_);
        placeBid(*s, playerHousehold_, arg, men.empty() ? kNone : men.front());
        ++counters_["district:bid_ours"];
        log(1, "〔出價〕你家為" + s->name + "出了 " + pence(amt) + "（" + levelName(arg) + "），" + seasonOf(s->closeDay) + "見分曉。勝算約 " +
                   odds(stakeOdds(*s, playerHousehold_, amt, kNone, true)) + "（只算聽說了的出價）。");
    } else if (verb == "speak") {
        bool bid = false;
        for (const auto& b : s->bids)
            if (inPlayerHouse(b.house)) bid = true;
        bool can = false;
        for (const auto& [p, h] : speakers(*s))
            if (p == arg) can = true;
        if (!bid || !can) {
            log(1, "〔託人〕" + std::string(!bid ? "要先出了價，才好託人說情。" : "這個人眼下說不上話。"));
            return;
        }
        spendTime(1);
        speakFor(*s, arg);
    }
}

void World::districtAgenda(std::vector<AgendaItem>& items) {
    if (!dist_.built || playerHousehold_ == kNone || actingHead() == kNone) return;
    const Household& h = households_[playerHousehold_];
    int n = 0;
    for (const auto& s : stakes_) {
        if (!s.open || n >= 2) continue;
        bool mine = false;
        for (const auto& b : s.bids)
            if (inPlayerHouse(b.house)) mine = true;
        if (mine || s.minor || !stakeBar(s, playerHousehold_).empty()) continue;
        if (!dist_.in && !s.gate) continue;
        if (h.money < s.base) continue;
        AgendaItem a;
        a.id = "district:" + s.key;
        a.title = (dist_.in ? "地方：爭" : "往上走：爭") + s.name;
        a.desc = "照價 " + pence(std::round(s.base / 5) * 5) + "，勝算約 " + odds(stakeOdds(s, playerHousehold_, s.base, kNone, true)) + "；" + seasonOf(s.closeDay) + "見分曉。" +
                 (dist_.in ? "" : "拿到了，你家就在地方上有了名字（「地方」那一欄）。");
        a.kind = "district";
        a.rank = dist_.in ? 55 : 70;
        items.push_back(a);
        ++n;
    }
}

void World::doDistrictAgenda(const AgendaItem& a) {
    if (a.id.rfind("district:", 0) != 0) return;
    Stake* s = stakeByKey(a.id.substr(9));
    if (!s || !s->open) {
        log(1, "〔地方〕這件事已經不在爭了。");
        return;
    }
    if (timeLeft() < 1) {
        log(1, "〔地方〕這一季沒有空閒了。");
        return;
    }
    bidCard(*s, true);
}

// ---- What the front ends show ---------------------------------------------------------------------

World::DistrictView World::districtView() const {
    DistrictView v;
    v.in = dist_.in;
    if (!dist_.built || playerHousehold_ == kNone) return v;
    const Household& h = households_[playerHousehold_];
    const Id head = actingHead();
    if (dist_.in) v.status = std::to_string(cfg_.startYear + dist_.since / kDaysPerYear) + " 年起：" + dist_.gate;
    v.footholds = footholds();
    {
        const int need = static_cast<int>(defs_.district["tier"].numberOr("blows", 3));
        const int window = static_cast<int>(defs_.district["tier"].numberOr("blow_years", 4)) * kDaysPerYear;
        int n = 0;
        for (int d : dist_.blows)
            if (day_ - d < window) ++n;
        v.fall = "再挨 " + std::to_string(std::max(1, need - n)) + " 次重挫（大筆的債收不回、官司打輸、宿敵搶走你家出了高價的東西、繼承人死了），或者立足的地方一處也不剩，就會掉回村子那一層。";
    }
    v.rival = districtRivalName();
    v.rivalAfter = districtRivalAfter();
    v.rivalClashes = districtRivalClashes();
    {
        std::string hint;
        if (std::string son = sonInTown(); !son.empty())
            hint = son + "從鎮上捎話：今年的毛" + std::string(dist_.woolHint > 0 ? "看漲" : dist_.woolHint < 0 ? "看跌" : "價錢大概持平") + "（鎮上的話多半準，也有走眼的時候）。";
        v.wool = "羊毛價 " + num(dist_.wool) + "（照佛蘭德斯商人收一袋的價錢算）。" + (hint.empty() ? "家裡沒人在鎮上，打聽不到價錢往哪邊走。" : hint);
    }
    auto timeWhy = [&](DistrictView::Opt& o) {
        if (timeLeft() < 1) o.open = false, o.why = "這一季沒有空了";
    };
    for (const auto& s : stakes_) {
        const bool ours = s.holder == playerHousehold_ && (s.kind == "land" || ((s.kind == "lease" || s.kind == "post" || s.kind == "seat") && s.holderMan != kNone));
        if (!s.open && !ours) continue;
        if (!s.open && s.kind == "land") continue;  // land bought is in the footholds
        DistrictView::Row r;
        r.key = s.key;
        r.title = s.name;
        r.place = defs_.district["places"].stringOr(s.place, "");
        const Id dec = stakeDecider(s);
        if (!s.open) {
            r.state = "在你家手上";
            r.desc = s.until >= 0 ? "到 " + std::to_string(cfg_.startYear + s.until / kDaysPerYear) + " 年" : "";
            v.stakes.push_back(r);
            continue;
        }
        r.state = "在爭，" + seasonOf(s.closeDay) + "見分曉";
        std::string known;
        double mine = 0;
        for (const auto& b : s.bids) {
            if (inPlayerHouse(b.house)) mine = b.amount;
            else if (b.known) known += (known.empty() ? "" : "；") + houseTag(b.house) + "出 " + pence(b.amount);
        }
        r.desc = (dec != kNone ? "拿主意的是 " + fullName(dec) + "（對你家 " + num(opinion(dec, head)) + "）。" : std::string()) +
                 (known.empty() ? "還沒聽說別家出價" : "聽說的出價：" + known) + "（可能還有別家沒傳出來）。" +
                 (mine > 0 ? "你家出了 " + pence(mine) + "，勝算約 " + odds(stakeOdds(s, playerHousehold_, mine, kNone, true)) + "。" : "");
        const std::string bar = stakeBar(s, playerHousehold_);
        for (int l = 0; l < levelCount(); ++l) {
            const double amt = std::round(s.base * levelMult(l) / 5) * 5;
            if (amt <= mine) continue;
            DistrictView::Opt o;
            o.verb = "bid", o.key = s.key, o.arg = std::to_string(l);
            o.label = "出價 " + pence(amt) + "（" + levelName(l) + "）：勝算約 " + odds(stakeOdds(s, playerHousehold_, amt, kNone, true)) + "，贏了當場付清，花一天";
            if (!bar.empty()) o.open = false, o.why = bar;
            else if (h.money < amt) o.open = false, o.why = "錢不夠";
            timeWhy(o);
            r.opts.push_back(o);
        }
        if (mine > 0) {
            bool spoke = false;
            for (const auto& b : s.bids)
                if (inPlayerHouse(b.house) && b.speak != kNone) spoke = true;
            for (const auto& [who, how] : speakers(s)) {
                if (spoke) break;
                DistrictView::Opt o;
                o.verb = "speak", o.key = s.key, o.arg = std::to_string(who);
                const std::string cost = how == "favour" ? "花掉他欠你家的人情" : how == "lord" ? "老爺好感 -3" : "不花錢";
                o.label = "託 " + fullName(who) + "說情（" + cost + "）：勝算約 " + odds(stakeOdds(s, playerHousehold_, mine, kNone, true)) + " → " +
                          odds(stakeOdds(s, playerHousehold_, mine, who, true)) + "，花一天";
                timeWhy(o);
                r.opts.push_back(o);
            }
        }
        v.stakes.push_back(r);
    }
    for (const auto& a : dasks_) {
        if (a.until <= day_) continue;
        DistrictView::Row r;
        r.key = std::to_string(a.id);
        const Id bh = houseHead(a.house);
        r.place = houseTag(a.house);
        if (a.kind == "wool") {
            r.title = "羊毛契約：入 " + fullName(bh) + "今年的毛";
            r.desc = a.why + "。照米迦勒節的價錢分給你家，另加一成多的利；價錢跌了就賠。現在的價錢 " + num(dist_.wool) + "。";
            const Json& lv = defs_.district["wool"]["levels"];
            for (size_t i = 0; i < lv.size(); ++i) {
                DistrictView::Opt o;
                o.verb = "lend", o.key = r.key, o.arg = std::to_string(i);
                o.label = "放進 " + pence(lv[i].asNumber()) + "，花一天";
                if (h.money < lv[i].asNumber()) o.open = false, o.why = "錢不夠";
                timeWhy(o);
                r.opts.push_back(o);
            }
        } else {
            r.title = fullName(bh) + "想借 " + pence(a.amount);
            const DHouse* d = dhouseOf(a.house);
            r.desc = a.why + "。" + (d ? d->note : "") + "。他家手上 " + pence(households_[a.house].money) + "；" + (a.risk >= 0.25 ? "看來不太靠得住" : a.risk >= 0.15 ? "多半還得上" : "向來還得上") + "。";
            DistrictView::Opt o;
            o.verb = "lend", o.key = r.key, o.arg = "0";
            o.label = "借 " + pence(a.amount) + "，" + std::to_string(year() + a.years) + " 年米迦勒節收 " + pence(std::round(a.amount * (1 + a.rate * a.years))) + "（沒有抵押），花一天";
            if (h.money < a.amount) o.open = false, o.why = "錢不夠";
            timeWhy(o);
            r.opts.push_back(o);
            if (a.pledge > 0) {
                DistrictView::Opt p = o;
                p.arg = "1";
                p.label = "借 " + pence(a.amount) + "，" + std::to_string(year() + a.years) + " 年收 " + pence(std::round(a.amount * (1 + (a.rate - 0.05) * a.years))) + "，拿他家 " + num(a.pledge) +
                          " 畝地作抵押（還不出地就歸你家），花一天";
                r.opts.push_back(p);
            }
        }
        if (dist_.in) v.asks.push_back(r);
    }
    for (const auto& l : dloans_) {
        if (l.state != 0 && l.state != 2 && l.state != 3) continue;
        if (inPlayerHouse(l.lender))
            v.loans.push_back(l.wool ? "羊毛契約 " + pence(l.lent) + "（入股時價錢 " + num(l.price) + "，" + std::to_string(l.due) + " 年米迦勒節結算）"
                                     : "借給 " + fullName(houseHead(l.borrower)) + " " + pence(l.lent) + "，" + std::to_string(l.due) + " 年米迦勒節收 " + pence(l.owed) +
                                           (l.pledge > 0 ? "（抵押 " + num(l.pledge) + " 畝）" : "") + (l.state == 3 ? "：告上了百戶法庭" : ""));
        else if (inPlayerHouse(l.borrower))
            v.loans.push_back("欠" + houseTag(l.lender) + " " + pence(l.owed) + "，" + std::to_string(l.due) + " 年米迦勒節還" + (l.state == 3 ? "：他告上了百戶法庭" : ""));
    }
    for (auto it = dsuits_.rbegin(); it != dsuits_.rend() && v.suits.size() < 5; ++it) {
        if (!inPlayerHouse(it->plaintiff) && !inPlayerHouse(it->defendant)) continue;
        const std::string who = inPlayerHouse(it->plaintiff) ? "你家告 " + fullName(houseHead(it->defendant)) : fullName(houseHead(it->plaintiff)) + "告你家";
        v.suits.push_back(std::to_string(cfg_.startYear + it->filed / kDaysPerYear) + " 年 " + who + "「" + it->what + "」：" + (it->done ? it->result : std::string("等開庭")));
    }
    for (const auto& d : dhouses_) {
        if (d.gone || d.head == kNone) continue;
        std::string line = houseTag(d.shell) + "：" + fullName(d.head) + "（" + num(std::floor(ageYears(people_[d.head]))) + " 歲，對你家 " + num(opinion(d.head, head)) + "）。" + d.note;
        if (d.rival && !dist_.rivalKin) line += "（宿敵）";
        if (districtKinHouse(d)) line += "（親家）";
        v.people.push_back(line);
    }
    for (const auto& [hid, why] : dist_.houses)
        if (!inPlayerHouse(hid) && !households_[hid].extinct && households_[hid].head != kNone) v.houses.push_back(houseTag(hid) + "：" + why);
    return v;
}

}  // namespace sim
