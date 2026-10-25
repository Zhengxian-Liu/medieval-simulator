// Mood, what is on people's minds, and the head's season (design/season-loop.md). Big events leave
// concerns behind (grief, a lost match, a grudge against one's father); concerns and needs and stress
// make up a mood the player can read in words, and the mood shows in the work and the body. The head
// has a few free days a season to spend on people (data/events/interactions.json) or on plans; days
// left over go into the fields.
#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>

#include "simcore/events.h"

namespace sim {

namespace {
std::string signedText(double v) {
    int n = static_cast<int>(std::lround(v));
    return (n > 0 ? "+" : "") + std::to_string(n);
}
}  // namespace

double World::mood(const Person& p) const {
    double needs = 0;
    for (double n : p.needs) needs += n;
    needs /= static_cast<double>(kNeedCount);
    double m = 40 + needs * 0.35 - p.stress * 0.3;
    for (const auto& c : p.concerns)
        if (const ConcernDef* d = defs_.concern(c.kind)) m += d->mood * c.strength / 100;
    return std::clamp(m, 0.0, 100.0);
}

World::MoodView World::moodView(const Person& p) const {
    MoodView v;
    v.value = mood(p);
    v.word = v.value >= 75 ? "高興" : v.value >= 55 ? "平靜" : v.value >= 40 ? "悶悶不樂" : v.value >= 25 ? "憂愁" : "絕望";
    // The heaviest thing on their mind names the mood when it is what drags it down (or lifts it).
    const Concern* top = nullptr;
    double topWeight = 0;
    for (const auto& c : p.concerns)
        if (const ConcernDef* d = defs_.concern(c.kind)) {
            double w = d->mood * c.strength / 100;
            if (std::abs(w) > std::abs(topWeight)) {
                topWeight = w;
                top = &c;
            }
        }
    if (top && topWeight <= -6 && v.value < 60) {
        const ConcernDef* d = defs_.concern(top->kind);
        if (!d->word.empty() && v.value >= 25) v.word = d->word;
        v.cause = concernText(p, *top);
    } else if (top && topWeight >= 6 && v.value >= 55) {
        v.cause = concernText(p, *top);
    } else if (p.stress >= 40 && !p.stressCause.empty()) {
        v.cause = "心裡壓著" + p.stressCause;
    } else if (v.value < 55) {
        // Nothing in particular: say which need is wanting most.
        int low = 0;
        for (int n = 1; n < kNeedCount; ++n)
            if (p.needs[n] < p.needs[low]) low = n;
        if (p.needs[low] < 40) {
            static const char* why[kNeedCount] = {"餓著肚子", "累壞了", "冷得受不了", "悶得慌，好久沒跟人說說話", "好久沒上教堂，心裡空空的"};
            v.cause = why[low];
        }
    }
    return v;
}

std::string World::concernText(const Person& p, const Concern& c) const {
    const ConcernDef* d = defs_.concern(c.kind);
    if (!d) return "";
    std::string t = d->text;
    auto put = [&](const std::string& key, const std::string& val) {
        for (size_t at; (at = t.find(key)) != std::string::npos;) t.replace(at, key.size(), val);
    };
    std::string about = c.about == kNone ? "那件事" : isPlayerFamily(people_[c.about]) ? shortName(c.about) : fullName(c.about);
    // The player reads this as the head: the head is "你", in his own line and in others' lines about him.
    bool self = p.id == playerHead();
    if (c.about != kNone && c.about == playerHead() && !self) about = "你";
    put("{about}", about);
    put("{pron}", self ? "你" : p.sex == Sex::Male ? "他" : "她");
    return t;
}

const Concern* World::concernOf(const Person& p, const std::string& kind, Id about) const {
    // About anyone: the one that weighs most (two deaths in a plague year, the worse loss).
    const Concern* best = nullptr;
    for (const auto& c : p.concerns)
        if (c.kind == kind && (about == kNone || c.about == about) && (!best || c.strength > best->strength)) best = &c;
    return best;
}

void World::addConcern(Id holder, const std::string& kind, Id about, double strength) {
    if (holder == kNone || strength <= 0) return;
    Person& p = people_[holder];
    if (!p.alive() || p.household == kNone || ageYears(p) < 6) return;
    if (!defs_.concern(kind)) throw JsonError("unknown concern " + kind);
    for (auto& c : p.concerns)
        if (c.kind == kind && c.about == about) {
            // The same sorrow again deepens it; it does not start a second one.
            c.strength = std::min(100.0, std::max(c.strength, strength) + std::min(c.strength, strength) * 0.3);
            return;
        }
    p.concerns.push_back({kind, about, std::min(100.0, strength), day_});
    ++counters_["concern:" + kind];
    // Events can answer it: "concern:grief" fires for the holder, or with "as": "head" for the head of their house.
    emit({"concern:" + kind, kNone, holder, about, false, strength});
}

double World::easeConcern(Id holder, const std::string& kind, double by, Id about) {
    if (holder == kNone) return 0;
    Person& p = people_[holder];
    double eased = 0;
    for (auto& c : p.concerns)
        if (c.kind == kind && (about == kNone || c.about == about)) {
            double d = std::min(c.strength, by);
            c.strength -= d;
            eased += d;
        }
    p.concerns.erase(std::remove_if(p.concerns.begin(), p.concerns.end(), [](const Concern& c) { return c.strength <= 0.5; }),
                     p.concerns.end());
    return eased;
}

void World::monthlyConcerns() {
    for (auto& p : people_) {
        if (!p.alive() || p.household == kNone) continue;
        for (auto& c : p.concerns)
            if (const ConcernDef* d = defs_.concern(c.kind)) c.strength -= d->fade;
        // A longing ends with the wedding, or when the one longed for is gone or wed to another.
        for (auto& c : p.concerns)
            if (c.kind == "longing" && c.about != kNone) {
                const Person& o = people_[c.about];
                if (!o.alive() || o.household == kNone || p.spouse == c.about || (o.spouse != kNone && people_[o.spouse].alive()))
                    c.strength = 0;
            }
        p.concerns.erase(std::remove_if(p.concerns.begin(), p.concerns.end(), [](const Concern& c) { return c.strength <= 0.5; }),
                         p.concerns.end());
        // Effort follows mood, slowly: a month of grief does not empty the barn, a season of it shows.
        double m = mood(p);
        double target = m < 25 ? 0.65 : m < 40 ? 0.82 : m < 55 ? 0.95 : m >= 75 ? 1.05 : 1.0;
        p.effort = p.effort * 0.6 + target * 0.4;
    }
    // A house deep in debt or short of bread worries those who keep it.
    for (const auto& h : households_) {
        if (h.extinct || h.head == kNone || h.members.empty()) continue;
        bool hard = h.debt > 0.5 || (h.lean && h.food < consumption(h) * 60);
        if (!hard) continue;
        for (Id m : {h.head, people_[h.head].spouse})
            if (m != kNone && people_[m].alive() && people_[m].household == h.id && !concernOf(people_[m], "worry"))
                addConcern(m, "worry", kNone, 45);
    }
}

int World::seasonBudget(int month) {
    static const int budget[4] = {3, 2, 1, 2};  // winter evenings are long; at harvest there is no time at all
    return budget[std::clamp(month / 3, 0, 3)];
}

void World::rollSeason() {
    if (playerHousehold_ == kNone) return;
    Household& h = households_[playerHousehold_];
    int key = year() * 4 + month() / 3;
    if (h.timeSeason == key) return;
    // Days not spent on people or plans went into the fields.
    if (h.timeSeason >= 0) h.workBonus = std::min(0.12, h.workBonus + 0.015 * h.timeLeft);
    h.timeSeason = key;
    h.timeLeft = seasonBudget(month());
    // What the head did last season waits for the summary (which the text game prints and clears).
    settleAsk();
    lastSeasonNotes_.insert(lastSeasonNotes_.end(), seasonNotes_.begin(), seasonNotes_.end());
    seasonNotes_.clear();
    seasonWants();
    electionNotice();
    secretSurfaces();
    if (lastSeasonNotes_.size() > 40) lastSeasonNotes_.erase(lastSeasonNotes_.begin(), lastSeasonNotes_.end() - 40);
    ++counters_["season"];
    bool concern = false, wish = false;
    for (Id m : h.members) {
        if (m != h.head && !people_[m].want.empty()) wish = true;
        for (const auto& c : people_[m].concerns)
            if (c.strength >= 15) concern = true;
    }
    if (concern) ++counters_["season_concern"];
    if (wish) ++counters_["season_wish"];
}

void World::seasonWants() {
    Id head = actingHead();  // the one keeping the house hears what the family wants
    if (head == kNone) return;
    int season = year() * 4 + month() / 3;
    // A post the family wanted but its head can no longer stand for (a widow holds the house now, debt, age):
    // said at once, not left to end as "taken by another house".
    for (auto& wt : wants_)
        if (wt.open && wt.kind == "office" && wt.house == playerHousehold_) {
            Id man = familyOfficeMan(wt.office, wt.subject);
            if (man != kNone) {
                wt.subject = man;
                continue;
            }
            std::string bar = familyOfficeBar(wt.office);
            dropWant(wt, "當不了");
            log(1, "【差事】你家只好放下" + wantThing(wt) + "：" + bar + "。");
        }
    EventRunner er(*this);
    std::vector<Id> members = households_[playerHousehold_].members;
    std::set<std::string> held;
    for (Id m : members)
        if (!people_[m].want.empty()) held.insert(people_[m].want);
    int formed = 0;
    for (Id m : members) {
        Person& p = people_[m];
        if (m == head || !p.alive() || !inPlayerHouse(p.household) || ageYears(p) < 5) {
            p.want.clear();
            continue;
        }
        if (const WantDef* w = defs_.want(p.want)) {
            // A want only a plan answers, and that plan cannot be started for them yet (no one suitable for a match, too
            // little land or coin to set a son up), is not the head's neglect: the clock waits.
            bool matchBlocked = false;
            std::vector<std::string> byPlan;
            for (const auto& by : w->answeredBy)
                if (by.rfind("plan:", 0) == 0) byPlan.push_back(by.substr(5));
            if (!byPlan.empty() && byPlan.size() == w->answeredBy.size()) {
                matchBlocked = true;
                for (const auto& op : availablePlans(head))
                    if (std::find(byPlan.begin(), byPlan.end(), op.plan) != byPlan.end() && op.subject == m) matchBlocked = false;
            }
            if (!er.eval(w->when, p, head)) {
                p.want.clear();  // it passed of itself
            } else if (matchBlocked) {
                p.wantSince = std::min(season, p.wantSince + 1);  // the clock waits
                continue;
            } else if (season - p.wantSince >= w->patience) {
                // Waited and was not heard: it is let go, and remembered.
                if (!w->neglectNote.empty()) lastSeasonNotes_.push_back(er.format(w->neglectNote, p, head));
                er.apply(w->neglect, p, head);
                ++counters_["want_neglected"];
                p.want.clear();
                p.wantSince = season;  // a while before the next one
                continue;
            } else {
                continue;
            }
        } else {
            p.want.clear();
        }
        if (p.wantSince == season || formed >= 2) continue;  // not everyone at once: a family asks a thing or two a season
        for (const auto& w : defs_.wants)
            if ((!w.oneAtATime || !held.count(w.id)) && !(w.id == p.lastWant && season - p.lastWantMet < 3) &&
                er.eval(w.when, p, head) && rngWants_.chance(w.chance)) {
                held.insert(w.id);
                ++formed;
                p.want = w.id;
                p.wantSince = season;
                ++counters_["want:" + w.id];
                break;
            }
    }
}

std::string World::wantText(const Person& p) const {
    const WantDef* w = defs_.want(p.want);
    if (!w) return "";
    return EventRunner(const_cast<World&>(*this)).format(w->text, p, playerHead());
}

bool World::answerWant(Id who, const std::string& by) {
    if (who == kNone) return false;
    Person& p = people_[who];
    const WantDef* w = defs_.want(p.want);
    if (!w || std::find(w->answeredBy.begin(), w->answeredBy.end(), by) == w->answeredBy.end()) return false;
    EventRunner er(*this);
    Id head = actingHead();
    std::string note = w->fulfilNote.empty() ? "" : er.format(w->fulfilNote, p, head);
    if (!note.empty()) seasonNotes_.push_back(note);
    er.apply(w->fulfil, p, head);
    ++counters_["want_met"];
    p.lastWant = p.want;
    p.lastWantMet = year() * 4 + month() / 3;
    p.want.clear();
    return !note.empty();
}

int World::timeLeft() const {
    if (playerHousehold_ == kNone) return 0;
    const Household& h = households_[playerHousehold_];
    return h.timeSeason == year() * 4 + month() / 3 ? h.timeLeft : seasonBudget(month());
}

bool World::spendTime(int n) {
    rollSeason();
    if (playerHousehold_ == kNone) return false;
    Household& h = households_[playerHousehold_];
    if (h.timeLeft < n) return false;
    h.timeLeft -= n;
    return true;
}

std::vector<Id> World::peopleToSee() const {
    std::vector<Id> out;
    Id head = actingHead();
    if (head == kNone) return out;
    const Household& ph = households_[playerHousehold_];
    for (Id m : ph.members)
        if (m != head && people_[m].alive()) out.push_back(m);
    // Family living elsewhere (children in service, married daughters), then the people who matter most to the head.
    std::vector<std::pair<double, Id>> others;
    for (const auto& p : people_) {
        if (!p.alive() || p.household == kNone || inPlayerHouse(p.household) || ageYears(p) < 12) continue;
        double s = std::abs(opinion(head, p.id)) + std::abs(opinion(p.id, head)) +
                   (p.id < static_cast<Id>(ringScore_.size()) ? ringScore_[p.id] * 0.5 : 0);
        if (isPlayerFamily(p) || closeKin(head, p.id)) s += 100;
        if (auto k = people_[head].known.find(p.id); k != people_[head].known.end()) s += k->second;  // the people the head knows
        if (p.id == lord_ || p.id == priest_) s += 30;
        if (s >= 20) others.push_back({s, p.id});
    }
    std::sort(others.begin(), others.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    for (size_t i = 0; i < others.size() && i < 14; ++i) out.push_back(others[i].second);
    return out;
}

std::vector<World::Interaction> World::interactionsWith(Id target) {
    std::vector<Interaction> out;
    Id head = actingHead();
    if (head == kNone || target == kNone || target == head) return out;
    Person& tp = people_[target];
    if (!tp.alive() || tp.household == kNone) return out;
    bool home = inPlayerHouse(tp.household);
    bool kin = home || isPlayerFamily(tp) || closeKin(head, target) || !kinTie(head, target).empty();
    EventRunner er(*this);
    int left = timeLeft();
    for (const auto& ev : defs_.events) {
        if (ev.scope != "interaction") continue;
        std::string who = ev.interaction.stringOr("who", "anyone");
        if ((who == "family" && !kin) || (who == "household" && !home) || (who == "outsider" && kin)) continue;
        if (!er.eval(ev.conditions, tp, head)) continue;
        Interaction in;
        in.ev = &ev;
        int cost = static_cast<int>(ev.interaction.numberOr("time", 1));
        auto cd = cooldowns_.find({target, ev.id});
        double money = ev.interaction.numberOr("money", 0);  // what it costs
        if (!home && people_[head].flags.count("plague_shelter")) in.why = "全家躲在林子裡避瘟疫，不和外人來往";
        else if (cd != cooldowns_.end() && cd->second > day_) in.why = "最近才" + ev.title + "過";
        else if (left < cost) in.why = "這一季沒有空了";
        else if (money > households_[playerHousehold_].money + 0.5) in.why = "錢不夠";
        else in.open = true;
        out.push_back(in);
    }
    return out;
}

}  // namespace sim

namespace sim {

std::string World::familyGlance() const {
    Id head = playerHead();
    if (head == kNone) return "";
    const Household& h = households_[playerHousehold_];
    std::string o = "家裡的人：\n";
    auto line = [&](Id id) {
        const Person& p = people_[id];
        MoodView mv = moodView(p);
        std::string s = "  " + (id == head ? "你（" + shortName(id) + "，" : shortName(id) + "（" + kinWord(head, id) + "，") +
                        std::to_string(int(ageYears(p))) + " 歲）：" + mv.word + (mv.cause.empty() ? "" : "，" + mv.cause) + "。";
        if (p.health < 40) s += "身體很差。";
        if (id != head && ageYears(p) >= 6) s += "對你" + std::string(opinionWord(opinion(id, head))) + "。";
        // What they want from you this season (data/wants.json).
        std::string want = id == head ? "" : wantText(p);
        if (!want.empty()) s += want + "。";
        return s + "\n";
    };
    o += line(head);
    // Those with something on their mind each get a line; the rest are named together.
    std::string fine;
    for (Id m : h.members) {
        const Person& p = people_[m];
        if (m == head || !p.alive()) continue;
        MoodView mv = moodView(p);
        bool wants = !p.want.empty();
        if (mv.word == "平靜" && mv.cause.empty() && !wants && p.health >= 40 && std::abs(opinion(m, head)) < 30)
            fine += (fine.empty() ? "" : "、") + shortName(m) + (kinWord(head, m).empty() ? std::string() : "（" + kinWord(head, m) + "）");
        else
            o += line(m);
    }
    if (!fine.empty()) o += "  " + fine + "：都還好。\n";
    o += villageTalk();
    int left = timeLeft();
    o += "這一季還有 " + std::to_string(left) + " 段空閒" +
         (left > 0 ? "（找人、定計畫各用一段；沒用完的拿去下田，收成會好一點）" : "（都用完了，這一季就埋頭幹活）") + "。\n";
    return o;
}

World::FamilySnapshot World::snapshot() const {
    FamilySnapshot s;
    s.day = day_;
    s.head = playerHead();
    if (s.head == kNone) return s;
    const Household& h = households_[playerHousehold_];
    for (Id m : h.members)
        if (people_[m].alive()) {
            s.people[m] = {moodView(people_[m]).word, m == s.head ? 0 : opinion(m, s.head)};
            if (m != s.head)
                for (const auto& [lab, v] : opinionBreakdown(m, s.head)) s.why[m][lab] += v;
        }
    // Near kin in other houses (a married daughter, a brother with his own holding): only their feeling for the head.
    for (const auto& p : people_)
        if (p.alive() && p.household != kNone && !inPlayerHouse(p.household) && ageYears(p) >= 12 && !kinTie(s.head, p.id).empty()) {
            s.kinAway[p.id] = opinion(p.id, s.head);
            for (const auto& [lab, v] : opinionBreakdown(p.id, s.head)) s.why[p.id][lab] += v;
        }
    s.money = h.money;
    s.food = h.food / std::max(0.5, consumption(h));
    s.acres = h.acres;
    s.ledger = ledger_;
    s.foodLedger = foodLedger_;
    s.bread = h.food;
    s.grainTimes = grainTimes_, s.grainDays = grainDays_;
    return s;
}

void World::openBooks() {
    ledger_.clear();
    seasonLedger_.clear();
    seasonBooks_.clear();
    unbooked_ = 0;
    foodLedger_.clear();
    foodOpen_ = playerHousehold_ != kNone ? households_[playerHousehold_].food : 0;
    ledgerOpen_ = seasonMoney_ = playerHousehold_ != kNone ? households_[playerHousehold_].money : 0;
    seasonFrom_ = day_;
}

double World::settleBooks(const std::string& why) {
    if (playerHousehold_ == kNone) return 0;
    double booked = ledgerOpen_;
    for (const auto& [k, v] : ledger_) booked += v;
    double gap = households_[playerHousehold_].money - booked;
    // The barn too: what moved it without a line of its own is 其他.
    double food = foodOpen_;
    for (const auto& [k, v] : foodLedger_) food += v;
    if (double fgap = households_[playerHousehold_].food - food; std::abs(fgap) > 0.01) foodLedger_["其他（送來送去、事件裡的）"] += fgap;
    if (std::abs(gap) < 0.005) return 0;
    ledger_[why] += gap;
    return gap;
}

void World::closeSeasonBooks() {
    if (playerHousehold_ == kNone) return;
    SeasonBooks b;
    b.day = seasonFrom_;
    b.start = seasonMoney_;
    b.end = households_[playerHousehold_].money;
    for (const auto& [k, v] : ledger_) {
        auto it = seasonLedger_.find(k);
        double d = v - (it == seasonLedger_.end() ? 0 : it->second);
        if (std::abs(d) >= 0.005) b.items[k] = d;
    }
    b.grainTimes = grainTimes_ - seasonGrainTimes_, b.grainDays = grainDays_ - seasonGrainDays_;
    seasonGrainTimes_ = grainTimes_, seasonGrainDays_ = grainDays_;
    seasonBooks_.push_back(b);
    if (seasonBooks_.size() > 16) seasonBooks_.erase(seasonBooks_.begin());
    seasonMoney_ = b.end;
    seasonLedger_ = ledger_;
    seasonFrom_ = day_;
}

std::string World::booksText(double start, double end, const std::map<std::string, double>& items) {
    // As the header shows the purse (whole pence, rounded down), and each item rounded so that they add up to it:
    // the nearest whole penny, then the few pence the rounding lost or gained go to the items it cut most.
    long from = static_cast<long>(std::floor(start + 1e-9)), to = static_cast<long>(std::floor(end + 1e-9));
    struct Item { std::string why; double exact; long shown; };
    std::vector<Item> xs;
    long sum = 0;
    for (const auto& [why, d] : items) {
        xs.push_back({why, d, std::lround(d)});
        sum += xs.back().shown;
    }
    long off = (to - from) - sum;
    while (off != 0 && !xs.empty()) {
        int dir = off > 0 ? 1 : -1;
        // The item whose rounding went furthest the other way takes the penny.
        size_t pick = 0;
        double worst = -1e9;
        for (size_t i = 0; i < xs.size(); ++i) {
            double cut = (xs[i].exact - xs[i].shown) * dir;
            if (cut > worst) worst = cut, pick = i;
        }
        xs[pick].shown += dir;
        off -= dir;
    }
    std::vector<Item> in, out;
    for (const auto& x : xs) (x.shown > 0 ? in : out).push_back(x);
    out.erase(std::remove_if(out.begin(), out.end(), [](const Item& x) { return x.shown == 0; }), out.end());
    auto bySize = [](const Item& a, const Item& b) { return std::abs(a.exact) > std::abs(b.exact); };
    std::sort(in.begin(), in.end(), bySize);
    std::sort(out.begin(), out.end(), bySize);
    auto list = [](const std::vector<Item>& v) {
        std::string s;
        for (const auto& x : v) s += (s.empty() ? "" : "、") + x.why + " " + (x.shown > 0 ? "+" + std::to_string(x.shown) : "−" + std::to_string(-x.shown));
        return s;
    };
    std::string why;
    if (!in.empty()) why += "進：" + list(in);
    if (!out.empty()) why += (why.empty() ? "" : "；") + std::string("出：") + list(out);
    if (off != 0) why += (why.empty() ? "" : "；") + std::string("零頭 ") + (off > 0 ? "+" : "") + std::to_string(off);
    if (from == to && why.empty()) return "存錢 " + std::to_string(from) + " 便士，沒有進出。";
    return "存錢 " + std::to_string(from) + " → " + std::to_string(to) + " 便士" + (why.empty() ? "" : "（" + why + "）") + "。";
}

std::string World::seasonBooksText(const SeasonBooks& b) const {
    static const char* kSeason[4] = {"冬末", "春", "夏收", "秋"};
    return std::to_string(cfg_.startYear + b.day / kDaysPerYear) + " 年" + kSeason[(b.day % kDaysPerYear) / (3 * kDaysPerMonth)] + "：" +
           booksText(b.start, b.end, b.items);
}

// "存錢 120 → 98 便士（進：賣餘糧 +18、手藝收入（馬修・織工）+33；出：交租 -40、買糧 -12）。": the family's money since
// a snapshot, every item, what came in apart from what went out (booksText).
std::string World::moneySince(const FamilySnapshot& before) const {
    std::map<std::string, double> items;
    for (const auto& [why, total] : ledger_) {
        auto it = before.ledger.find(why);
        double d = total - (it == before.ledger.end() ? 0 : it->second);
        if (std::abs(d) >= 0.005) items[why] = d;
    }
    return booksText(before.money, households_[playerHousehold_].money, items);
}

std::string World::foodSince(const FamilySnapshot& before) const {
    if (playerHousehold_ == kNone || before.head == kNone) return "";
    const Household& h = households_[playerHousehold_];
    const double daily = std::max(0.5, consumption(h));
    // In the house's days as it is now (the header's number), the items rounded the way the purse's are.
    const double from = before.bread / daily, to = h.food / daily;
    if (std::abs(std::floor(to) - std::floor(from)) < 1) return "";
    std::vector<std::pair<double, std::string>> items;
    for (const auto& [why, total] : foodLedger_) {
        auto it = before.foodLedger.find(why);
        double d = (total - (it == before.foodLedger.end() ? 0 : it->second)) / daily;
        if (std::abs(d) >= 0.5) items.push_back({d, why});
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return std::abs(a.first) > std::abs(b.first); });
    std::string o = "存糧 " + std::to_string(static_cast<long>(std::floor(from + 1e-9))) + " → " + std::to_string(static_cast<long>(std::floor(to + 1e-9))) + " 天";
    std::string parts;
    for (const auto& [d, why] : items) {
        long v = std::lround(d);
        if (v == 0) continue;
        parts += (parts.empty() ? "：" : "、") + why + " " + (v > 0 ? "+" : "−") + std::to_string(std::labs(v));
    }
    return o + parts;
}

std::string World::seasonChanges(const FamilySnapshot& before) const {
    Id head = playerHead();
    if (head == kNone || before.head == kNone) return "";
    const Household& h = households_[playerHousehold_];
    std::string o;
    for (const auto& n : lastSeasonNotes_) o += "  " + n + "\n";
    lastSeasonNotes_.clear();
    // "威廉對你更親近了：+40 → +46，有好感（一起喝酒 +4、「冷落過我」的事淡了 +2）。": the reasons that moved, so the
    // numbers add up. A new or grown reason is named as it is; one that is wearing off (its change against its own
    // sign) or gone is said to have faded, so a grudge easing never reads as a fresh grudge with a plus sign.
    auto opinionLine = [&](Id id, const std::string& who, double was, double minChange) {
        double now = opinion(id, head), d = now - was;
        if (std::abs(d) < minChange) return;
        std::map<std::string, double> diff, nowBy;
        auto bw = before.why.find(id);
        if (bw != before.why.end())
            for (const auto& [lab, v] : bw->second) diff[lab] -= v;
        for (const auto& [lab, v] : opinionBreakdown(id, head)) diff[lab] += v, nowBy[lab] += v;
        std::vector<std::pair<double, std::string>> moved;
        double named = 0;
        for (const auto& [lab, v] : diff)
            if (std::abs(v) >= 1) moved.push_back({v, lab});
        std::sort(moved.begin(), moved.end(), [](auto& a, auto& b) { return std::abs(a.first) > std::abs(b.first); });
        std::string why;
        for (size_t i = 0; i < moved.size() && i < 3; ++i) {
            const std::string& lab = moved[i].second;
            double own = nowBy.count(lab) && std::abs(nowBy[lab]) >= 0.5 ? nowBy[lab] : 0;
            bool fading = own == 0 || own * moved[i].first < 0;
            std::string name = opinionLabel(lab);
            why += (why.empty() ? "" : "、") + (fading ? "「" + name + "」的事淡了 " : name + " ") + signedText(moved[i].first);
            named += moved[i].first;
        }
        if (std::abs(d - named) >= 2) why += (why.empty() ? "" : "、") + std::string("平常的往來 ") + signedText(d - named);
        std::string words = opinionWord(was) == std::string(opinionWord(now)) ? std::string(opinionWord(now))
                                                                             : std::string(opinionWord(was)) + " → " + opinionWord(now);
        o += "  " + who + "對你" + (d > 0 ? "更親近了" : "疏遠了") + "：" + signedText(was) + " → " + signedText(now) + "，" + words +
             (why.empty() ? "" : "（" + why + "）") + "。\n";
    };
    for (const auto& [id, was] : before.people) {
        const Person& p = people_[id];
        if (!p.alive() || !inPlayerHouse(p.household)) continue;
        std::string who = id == head ? "你" : shortName(id);
        MoodView mv = moodView(p);
        if (mv.word != was.first) o += "  " + who + "從" + was.first + "變得" + mv.word + (mv.cause.empty() ? "" : "：" + mv.cause) + "。\n";
        if (id != head && head == before.head) opinionLine(id, who, was.second, 3);
    }
    // Kin in other houses: only a big swing is news.
    if (head == before.head)
        for (const auto& [id, was] : before.kinAway) {
            const Person& p = people_[id];
            if (!p.alive() || p.household == kNone || inPlayerHouse(p.household)) continue;
            std::string kw = kinWord(head, id);
            opinionLine(id, (kw.empty() ? "" : kw) + fullName(id), was, 15);
        }
    double food = h.food / std::max(0.5, consumption(h));
    // The money, every season, item by item. Over several seasons let pass at once, each season on its own line.
    std::vector<const SeasonBooks*> past;
    for (const auto& b : seasonBooks_)
        if (b.day >= before.day && b.day < day_) past.push_back(&b);
    o += "  " + moneySince(before) + "\n";
    if (past.size() > 1)
        for (const SeasonBooks* b : past) o += "    " + seasonBooksText(*b) + "\n";
    if (food - before.food >= 45 || (food < 60 && before.food >= 60)) o += "  存糧 " + std::to_string(int(before.food)) + " → " + std::to_string(int(food)) + " 天。\n";
    if (std::abs(h.acres - before.acres) >= 0.5) o += "  田地 " + std::to_string(int(before.acres)) + " → " + std::to_string(int(h.acres)) + " 畝。\n";
    // Over a year let pass at once, say so: the totals are for all of it.
    int seasons = static_cast<int>(std::lround((day_ - before.day) / (kDaysPerYear / 4.0)));
    std::string span = seasons <= 1 ? "這一季" : "這 " + std::to_string(seasons) + " 季";
    return o.empty() ? "" : span + "的變化：\n" + o;
}

}  // namespace sim

// 這一季的事 (design/fun-r5.md C): instead of six chats that read alike, a few things that need doing this season,
// each a single button. Built afresh from the family's state whenever it is asked for, so a button pressed after the
// situation changed is checked again (doAgenda) and says why it no longer can be done.
namespace sim {

namespace {
const char* const kCompany[] = {"talk_heart", "talk_church", "talk_praise", "talk_market"};
bool isCompany(const std::string& id) {
    return std::find(std::begin(kCompany), std::end(kCompany), id) != std::end(kCompany);
}
std::string pence(double v) { return std::to_string(static_cast<long>(std::lround(v))) + " 便士"; }
}  // namespace

std::string World::companyEvent(Id who) {
    if (who == kNone) return "";
    const Person& p = people_[who];
    const WantDef* want = defs_.want(p.want);
    std::string best;
    double bestScore = -1e9;
    for (const auto& in : interactionsWith(who)) {
        if (!in.open || !isCompany(in.ev->id)) continue;
        const std::string& id = in.ev->id;
        if (want && std::find(want->answeredBy.begin(), want->answeredBy.end(), id) != want->answeredBy.end()) return id;
        double s = 0;
        if (id == "talk_heart") s = 100 - p.needs[Social] + (opinion(who, actingHead()) < 20 ? 15 : 0);
        else if (id == "talk_church") {
            s = 100 - p.needs[Spirit];
            for (const auto& c : p.concerns)
                if (c.kind == "grief" || c.kind == "shame") s += c.strength * 0.5;
        } else if (id == "talk_praise") s = 40 + p.stress * 0.4 + std::max(0.0, 30 - opinion(who, actingHead()));
        else if (id == "talk_market") s = 35 + (100 - p.needs[Social]) * 0.5;
        if (s > bestScore) bestScore = s, best = id;
    }
    return best;
}

Id World::spareParcel(Id toHouse) const {
    if (playerHousehold_ == kNone || toHouse == kNone) return kNone;
    const Household& ph = households_[playerHousehold_];
    // What the family must keep to feed itself (a hundredweight of grain an acre, about, and the croft).
    double need = consumption(ph) * kDaysPerYear;
    Id best = kNone;
    for (const auto& pc : parcels_) {
        if (pc.gone || pc.holder != playerHousehold_ || pc.acres < 1.5 || pc.acres > 6) continue;
        if (keepsHouse(ph.acres - pc.acres) < need * 1.1) continue;
        // Customary (rented) land first: giving away charter land can cost the family its standing.
        if (best == kNone || (parcels_[best].free && !pc.free) || (parcels_[best].free == pc.free && pc.acres < parcels_[best].acres))
            best = pc.id;
    }
    (void)toHouse;
    return best;
}

Id World::takeInWho(Id house) const {
    if (house == kNone || playerHousehold_ == kNone) return kNone;
    const Household& h = households_[house];
    Id ours = actingHead();
    Id best = kNone;
    for (Id m : h.members) {
        const Person& p = people_[m];
        double a = ageYears(p);
        if (m == h.head || (h.head != kNone && people_[h.head].spouse == m) || p.home != kNone || a < 4 || a >= 60 || h.ward == m) continue;
        if (p.spouse != kNone && people_[p.spouse].alive()) continue;
        // The nearest of kin to the head first, then the youngest (the one hunger takes first).
        bool kin = ours != kNone && (closeKin(ours, m) || !kinTie(ours, m).empty());
        if (best == kNone) {
            best = m;
            continue;
        }
        bool bestKin = ours != kNone && (closeKin(ours, best) || !kinTie(ours, best).empty());
        if ((kin && !bestKin) || (kin == bestKin && p.birthDay > people_[best].birthDay)) best = m;
    }
    return best;
}

bool World::canHireKin(Id house) const {
    if (house == kNone || playerHousehold_ == kNone) return false;
    const Household& h = households_[house];
    int n = hiredHands(h);
    return n > 0 && n <= 2 && h.hiredUntil <= day_ && canHire(households_[playerHousehold_], n);
}

Id World::harvestHand() const {
    Id best = kNone;
    double bestAcres = 1e9;
    for (const auto& o : households_) {
        if (o.extinct || o.head == kNone || inPlayerHouse(o.id) || o.acres >= 8 || o.hiredUntil > day_ || o.house == "manor") continue;
        for (Id m : o.members) {
            const Person& p = people_[m];
            double a = ageYears(p);
            if (p.sex != Sex::Male || a < 16 || a > 55 || p.health < 40 || p.home != kNone || p.occupation == "priest") continue;
            bool kin = actingHead() != kNone && !kinTie(actingHead(), m).empty();
            double score = o.acres - (kin ? 5 : 0);
            if (score < bestAcres) bestAcres = score, best = m;
            break;
        }
    }
    return best;
}

void World::feedHouse(Household& h) {
    for (Id m : h.members) people_[m].needs[Hunger] = std::max(people_[m].needs[Hunger], 60.0);
}

double World::sellGrainAtMarket(Household& h, double days) {
    double keep = consumption(h) * (daysToHarvest() + 90);
    double amount = std::min(consumption(h) * days, std::max(0.0, h.food - keep));
    if (amount <= 0) return 0;
    // At the town market the family gets the price the corn dealer sells at, less the carting.
    double got = std::round(amount * param("grain_price", 0.5) * 0.9);
    h.food -= amount;
    h.money += got;
    book(h, "去市集賣糧", got);
    return got;
}

void World::hireHand(Household& h, Id hand, double pay, double bonus) {
    if (hand == kNone || people_[hand].household == kNone) return;
    pay = std::min(pay, h.money);
    Household& theirs = households_[people_[hand].household];
    h.money -= pay;
    book(h, "雇短工收割", -pay);
    theirs.money += pay;
    book(theirs, "給人收割的工錢", pay);
    h.workBonus = std::min(0.12, h.workBonus + bonus);
    cooldowns_[{h.id, "hire_hand"}] = day_ + daysToHarvest() + 1;
    if (h.head != kNone) addOpinion(hand, h.head, "hired_me", 5, 4);
}

void World::giveParcelTo(Id parcel, Id house, Id giver) {
    if (parcel == kNone || house == kNone || parcels_[parcel].gone) return;
    moveParcel(parcel, house, "親戚分給的");
    Household& h = households_[house];
    // A sack or two to see them to the harvest the land will bring.
    Household& ph = households_[playerHousehold_];
    double bridge = std::min(consumption(h) * 30, std::max(0.0, ph.food - consumption(ph) * 120));
    if (bridge < std::max(0.5, consumption(h)) * 7) bridge = 0;  // not a few days' crumbs (the card says none is sent)
    ph.food -= bridge;
    h.food += bridge;
    feedHouse(h);
    if (h.head != kNone && giver != kNone) {
        recordDeed("kindness", giver, h.head);
        addOpinion(h.head, giver, "gave_land", 20, 30);
        oweFavour(h.head, giver, 25, "分了地給他家");
    }
    ++feedHelped_[house];
}

void World::takeIn(Id who, Id house, Id giver) {
    if (who == kNone || house == kNone) return;
    Person& p = people_[who];
    Id from = p.household;
    if (from == kNone || from == house) return;
    moveTo(who, house);
    p.home = kNone;
    p.flags.erase("in_service");
    p.needs[Hunger] = std::max(p.needs[Hunger], 60.0);
    noteLife(who, "搬到" + (households_[house].head != kNone ? fullName(households_[house].head) : std::string("親戚")) + "家住");
    const Household& fh = households_[from];
    if (fh.head != kNone && giver != kNone) {
        addOpinion(fh.head, giver, "took_us_in", 15, 20);
        oweFavour(fh.head, giver, 15, "收留了他家的人");
    }
    ++feedHelped_[from];
}

void World::hireKin(Id house, Id master) {
    if (house == kNone || master == kNone) return;
    Household& h = households_[house];
    h.hiredUntil = day_ + kDaysPerYear;
    h.hiredBy = master;
    feedHouse(h);
    if (h.head != kNone && households_[master].head != kNone) addOpinion(h.head, households_[master].head, "hired_me", 10, 6);
    ++feedHelped_[house];
}

std::vector<World::AgendaItem> World::spareWorkItems() {
    std::vector<AgendaItem> items;
    Id head = actingHead();
    if (head == kNone || playerHousehold_ == kNone) return items;
    Household& ph = households_[playerHousehold_];
    const Person& hp = people_[head];
    if (hp.flags.count("plague_shelter") || hp.occupation == "priest" || gentry(head)) return items;
    // Who goes out to work: the head if fit, else the fittest grown member of the house (a sick head's husband).
    Id hand = ageYears(hp) >= 14 && hp.health >= 40 ? head : kNone;
    for (Id m : ph.members)
        if (m != head && hand != head && ageYears(people_[m]) >= 14 && people_[m].health >= 40 &&
            (hand == kNone || people_[m].health > people_[hand].health))
            hand = m;
    const double daily = std::max(0.5, consumption(ph));
    const double days = ph.food / daily;
    const bool shortCoin = ph.money < 40 || owedAll(ph.id) > ph.money + 0.5;
    const bool shortBread = days < daysToHarvest() + 20;
    if (!shortCoin && !shortBread) return items;
    const int season = month() / 3;  // 0 winter's end, 1 spring, 2 harvest, 3 autumn
    const double wage = param("wage_mult", 1);
    auto ready = [&](const std::string& kind) {
        auto cd = cooldowns_.find({ph.id, "spare_" + kind});
        return cd == cooldowns_.end() || cd->second <= day_;
    };
    const std::string me = fullName(head);
    // A day's work for whoever is short of hands for their land and can pay (the manor last). Not while every hand of
    // the house is already out hiring by the month (打短工): those days are paid once. Once a season.
    if (hand != kNone && ready("day") && spareHands(ph) < farmHands(ph) - 1e-9) {
        const double pay = std::round((season == 2 ? 9 : 6) * wage);
        Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
        Id best = kNone;
        double bestNeed = -1e9;
        for (const auto& o : households_) {
            if (o.extinct || o.head == kNone || inPlayerHouse(o.id) || o.id == lordHouse || o.acres < 10 || o.money < pay + 10) continue;
            if (people_[o.head].occupation == "priest" || feudStage(head, o.head) >= 2) continue;
            double hands = 0;
            for (Id m : o.members)
                if (ageYears(people_[m]) >= 14 && people_[m].health >= 40) hands += people_[m].sex == Sex::Male ? 1.0 : 0.5;
            double need = o.acres / 12 - hands;
            if (need >= 0.5 && need > bestNeed) bestNeed = need, best = o.id;  // really short of hands, or not asked
        }
        if (best == kNone && lordHouse != kNone && households_[lordHouse].money >= pay + 10) best = lordHouse;
        if (best != kNone) {
            const Household& o = households_[best];
            const bool manor = best == lordHouse;
            static const char* const kWork[4] = {"打穀、劈柴", "鋤地、趕羊", "割麥", "犁冬麥地"};
            const std::string where = manor ? std::string("莊園") : fullName(o.head) + " 家";
            AgendaItem a;
            a.id = "spare:day";
            a.title = "去" + where + "打零工（" + kWork[season] + "，" + pence(pay) + "）";
            std::string state = !shortCoin                         ? "存糧只夠 " + std::to_string(int(days)) + " 天"
                                : owedAll(ph.id) > ph.money + 0.5 ? "欠著 " + pence(owedAll(ph.id)) + "的債，手上只有 " + pence(ph.money)
                                                                   : "手上只有 " + pence(ph.money);
            a.desc = state + "。" +
                     (manor ? "莊園的管家在找人手" : where + "地多人少，正缺人手") + "：幹幾天活，當場拿工錢，還管飯。（用一段空閒）";
            a.kind = "effects", a.target = manor ? lord_ : o.head;
            a.effects = "[{\"spare_work\": {\"kind\": \"day\", \"pay\": " + std::to_string(int(pay)) + ", \"meals\": 6, \"worker\": " +
                        std::to_string(hand) + "}}]";
            a.done = fullName(hand) + " 去" + where + kWork[season] + "，幹了六天，拿到 " + pence(pay) + "，晌午還在那邊吃了飯。";
            a.rank = 78;
            items.push_back(a);
        }
    }
    // After the harvest, the stubble is the poor's: the women and children glean (once a year).
    if (season == 3 && ready("glean")) {
        int gleaners = 0;
        for (Id m : ph.members)
            if (ageYears(people_[m]) >= 6 && people_[m].health >= 30) ++gleaners;
        const double food = std::min(60.0, 12.0 * gleaners);
        if (food >= 12) {
            AgendaItem a;
            a.id = "spare:glean";
            a.title = "帶家裡人去麥茬地撿麥穗（約 " + std::to_string(int(food / daily)) + " 天的糧）";
            a.desc = "麥子收完了，地還沒翻。照規矩，麥茬地裡掉的穗子是窮人家的。（用一段空閒，一年一回）";
            a.kind = "effects";
            a.effects = "[{\"spare_work\": {\"kind\": \"glean\", \"food\": " + std::to_string(int(food)) + "}}]";
            a.done = me + " 帶著家裡人在麥茬地裡彎腰撿了幾天麥穗，揹回來約 " + std::to_string(int(food / daily)) + " 天的糧。";
            a.rank = 77;
            items.push_back(a);
        }
    }
    // A craft piece for the market on long evenings: baskets, rope, hurdles.
    if ((season == 0 || season == 3) && shortCoin && ready("craft")) {
        const double pay = std::round(5 * (1 + 0.5 * (wage - 1)));
        AgendaItem a;
        a.id = "spare:craft";
        a.title = std::string(season == 0 ? "編籃子、搓繩子" : "紮柵欄") + "拿去市集賣（約 " + pence(pay) + "）";
        a.desc = "晚上在灶邊做點手上的活，趕下一個市集賣掉。（用一段空閒，一季一回）";
        a.kind = "effects";
        a.effects = "[{\"spare_work\": {\"kind\": \"craft\", \"pay\": " + std::to_string(int(pay)) + "}}]";
        a.done = me + (season == 0 ? " 編了幾個籃子、搓了兩捆繩子" : " 紮了幾片柵欄") + "，在市集上賣了 " + pence(pay) + "。";
        a.rank = 76;
        items.push_back(a);
    }
    // The parish's alms, for a house nearly out of bread: a sack of beans, and the neighbours see you in the line.
    if (days < 30 && priest_ != kNone && people_[priest_].alive() && ready("alms")) {
        AgendaItem a;
        a.id = "spare:alms";
        a.title = "去教堂領救濟（約 15 天的糧；濟貧箱 −1 名聲）";
        a.desc = "存糧只夠 " + std::to_string(int(days)) + " 天。神父的濟貧箱是給揭不開鍋的人家的；排在寡婦和乞丐後面，村裡的人都看得見。（用一段空閒，一季一回）";
        a.kind = "effects", a.target = priest_;
        a.effects = "[{\"spare_work\": {\"kind\": \"alms\", \"days\": 15}}, {\"stress\": 3}]";  // the −1 is spareWork's, named
        a.done = me + " 排在教堂門口領了救濟：一袋豆子和兩條黑麵包，夠全家吃半個月。";
        a.rank = 77;
        items.push_back(a);
    }
    // Two at most: the season's list has other things on it.
    std::stable_sort(items.begin(), items.end(), [](const AgendaItem& x, const AgendaItem& y) { return x.rank > y.rank; });
    if (items.size() > 2) items.resize(2);
    return items;
}

void World::spareWork(Household& h, Id worker, const Json& what, Id target) {
    const std::string kind = what.stringOr("kind", "");
    // Until the next season (gleaning: the next harvest), the same piece of work is not offered again.
    const int nextSeason = (day_ / (kDaysPerMonth * 3) + 1) * kDaysPerMonth * 3;
    if (kind == "day") {
        Id payer = target != kNone ? people_[target].household : kNone;
        if (payer == kNone) return;
        Household& o = households_[payer];
        double pay = std::min(what.numberOr("pay", 6), std::max(0.0, o.money));
        o.money -= pay;
        book(o, "雇人打零工", -pay);
        h.money += pay;
        book(h, "打零工（" + (o.head != kNone && o.house != "manor" ? shortName(o.head) + "家" : std::string("莊園")) + "）", pay);
        // The worker ate at the master's table those days: out of the master's bread, not from nowhere.
        const double meals = std::min(what.numberOr("meals", 0), std::max(0.0, o.food));
        o.food -= meals;
        h.food += meals;
        cooldowns_[{h.id, "spare_day"}] = nextSeason;
        const Id hand = static_cast<Id>(what.numberOr("worker", static_cast<double>(worker)));
        if (target != kNone && hand != kNone) addOpinion(target, hand, "good_work", 3, 3);
    } else if (kind == "glean") {
        h.food += what.numberOr("food", 0);
        cooldowns_[{h.id, "spare_glean"}] = day_ + daysToHarvest() + 1;
    } else if (kind == "craft") {
        double pay = what.numberOr("pay", 5);
        h.money += pay;
        book(h, "做手藝賣錢", pay);
        cooldowns_[{h.id, "spare_craft"}] = nextSeason;
    } else if (kind == "alms") {
        h.food += consumption(h) * what.numberOr("days", 15);
        cooldowns_[{h.id, "spare_alms"}] = nextSeason;
        // In the line at the church door the whole village sees: a public act, and the only thing here that moves the name.
        if (h.head != kNone) addReputation(h.head, -1, "去濟貧箱領救濟");
    }
}

std::vector<World::AgendaItem> World::seasonAgenda(bool all) {

    std::vector<AgendaItem> items;
    Id head = actingHead();
    if (head == kNone || playerHousehold_ == kNone) return items;
    Household& ph = households_[playerHousehold_];
    const Person& hp = people_[head];
    bool hiding = hp.flags.count("plague_shelter") > 0;
    int left = timeLeft();
    auto inter = [&](Id who, const std::string& id) -> const EventDef* {
        for (const auto& in : interactionsWith(who))
            if (in.open && in.ev->id == id) return in.ev;
        return nullptr;
    };
    std::set<Id> named;  // one thing per person

    // Kin or a close friend who cannot feed themselves: a lasting way out, not another sack of grain.
    if (!hiding) {
        std::vector<std::pair<double, Id>> hungry;
        for (const auto& h : households_) {
            if (h.extinct || h.head == kNone || inPlayerHouse(h.id) || h.members.empty() || h.house == "manor") continue;
            double days = h.food / std::max(0.5, consumption(h));
            if (days >= 45 && !(h.lean && days < 120)) continue;
            bool kin = false;
            for (Id m : h.members) kin = kin || closeKin(head, m) || !kinTie(head, m).empty();
            if (!kin && opinion(head, h.head) < 40) continue;
            // Asked about lately (helped, or left be): not on the list again before its time.
            if (auto at = feedAskedUntil_.find(h.id); at != feedAskedUntil_.end() && at->second > day_) continue;
            hungry.push_back({days, h.id});
        }
        std::sort(hungry.begin(), hungry.end());
        // One hungry house a season, card or list (fun-r11 2): the hungriest, unless the month's card already asked
        // about one this season. Every way to help is on the card it opens.
        if (!hungry.empty() && hungerSeason_ != day_ / (3 * kDaysPerMonth)) {
            const Household& h = households_[hungry.front().second];
            std::string who = fullName(h.head) + " 家";
            std::string state = "只剩 " + std::to_string(int(hungry.front().first)) + " 天的糧" + (h.lean ? "，今年的收成也不夠" : "");
            AgendaItem a;
            a.id = "help_feed:" + std::to_string(h.id);
            a.title = "接濟" + who;
            a.desc = who + state + "。" + hungerWhy(h.id) + "借糧、送糧、替他家找活、分地或接一個孩子來住，挑一樣（每樣的代價都寫在上頭；佔一段空閒）。";
            a.kind = "feed", a.target = h.head;
            a.rank = 92;
            items.push_back(a);
        }
    }

    // Someone at home carrying something heavy: sit with them.
    {
        Id worst = kNone;
        const Concern* heavy = nullptr;
        double w = 20;
        for (Id m : ph.members) {
            if (m == head || !people_[m].alive()) continue;
            for (const auto& c : people_[m].concerns)
                if ((c.kind == "grief" || c.kind == "heartbreak" || c.kind == "shame" || c.kind == "worry") && c.strength > w && inter(m, "talk_comfort"))
                    w = c.strength, worst = m, heavy = &c;
        }
        if (worst != kNone) {
            AgendaItem a;
            a.id = "comfort:" + std::to_string(worst);
            a.title = "安慰" + shortName(worst);
            std::string why = heavy ? concernText(people_[worst], *heavy) : "";
            a.desc = shortName(worst) + "（" + (kinWord(head, worst).empty() ? std::string("住在家裡") : kinWord(head, worst)) + "）" + (why.empty() ? "心裡不好受" : why) + "。";
            a.kind = "interact", a.who = worst, a.event = "talk_comfort";
            a.rank = 80;
            items.push_back(a);
            named.insert(worst);
        }
    }

    // What someone at home asked of the head (not company: that is the 陪家人 below).
    for (Id m : ph.members) {
        if (m == head || named.count(m)) continue;
        const WantDef* wd = defs_.want(people_[m].want);
        if (!wd) continue;
        for (const auto& by : wd->answeredBy) {
            if (isCompany(by)) break;
            if (by.rfind("plan:", 0) == 0) {
                std::string plan = by.substr(5);
                const PlanOption* pick = nullptr;
                std::vector<PlanOption> opts = availablePlans(head);
                for (const auto& op : opts)
                    if (op.plan == plan && (op.subject == m || op.subject == kNone) && (!pick || op.targetWeight > pick->targetWeight)) pick = &op;
                if (!pick) continue;
                const PlanDef* pd = defs_.plan(plan);
                AgendaItem a;
                a.id = "want:" + std::to_string(m);
                a.title = (pd ? pd->name : plan) + (pick->target != kNone ? "：" + fullName(pick->target) : "");
                a.desc = shortName(m) + wantText(people_[m]) + "。（定下這件事，用一段空閒）";
                a.kind = "plan", a.event = plan, a.target = pick->target, a.subject = pick->subject;
                a.rank = 75;
                items.push_back(a);
                named.insert(m);
                break;
            }
            if (const EventDef* ev = inter(m, by)) {
                AgendaItem a;
                a.id = "want:" + std::to_string(m);
                a.title = ev->title + "（" + shortName(m) + "）";
                a.desc = shortName(m) + wantText(people_[m]) + "。";
                a.kind = "interact", a.who = m, a.event = by;
                a.rank = 75;
                items.push_back(a);
                named.insert(m);
                break;
            }
        }
    }

    // Grain to spare: carted to the town market it fetches half again what the corn dealer pays at the door.
    if (!hiding) {
        double c = std::max(0.5, consumption(ph));
        double spare = ph.food / c - daysToHarvest() - 90;
        if (spare >= 15) {  // a standing action (fun-r9 0): whenever there is grain beyond what must be kept
            double days = std::min(spare, 180.0);
            double got = std::round(c * days * param("grain_price", 0.5) * 0.9);
            AgendaItem a;
            a.id = "sell_grain";
            a.title = "去市集賣糧（約 " + pence(got) + "）";
            a.desc = "糧倉裡存著 " + std::to_string(int(ph.food / c)) + " 天的糧，離收成還有 " + std::to_string(daysToHarvest()) +
                     " 天。拉 " + std::to_string(int(days)) + " 天份去鎮上賣，留足到收成後三個月的。";
            a.kind = "effects", a.effects = "[{\"sell_grain\": " + std::to_string(int(days)) + "}]";
            a.done = fullName(head) + " 套上車，把多出來的糧拉到鎮上的市集賣了。";
            a.rank = 55;
            items.push_back(a);
        }
    }
    // 拿錢還債 (fun-r9 0, standing): what the purse holds against what the house owes, one creditor at a time.
    if (!hiding) {
        auto payItem = [&](Id cred, double owed) {
            if (owed <= 0.5) return;
            const double cash = std::floor(std::max(0.0, ph.money));
            const double pay = std::min(std::ceil(owed - 1e-9), cash);
            if (pay < 5) return;
            const bool lord = cred == kNone || cred == lord_;
            const std::string whom = lord ? std::string("莊園") : fullName(cred);
            AgendaItem a;
            a.id = "pay_debt:" + std::to_string(lord ? -1 : cred);
            a.title = pay >= owed - 0.5 ? "拿錢還債：還清欠" + whom + "的 " + pence(pay) : "拿錢還債：先還" + whom + " " + pence(pay) + "（還欠 " + pence(owed - pay) + "）";
            a.desc = "手上 " + pence(cash) + "，欠" + whom + " " + pence(owed) + "。還了就少一筆債，" + (lord ? std::string("管家不會再上門催。") : whom + "也會記得你家說話算話。") +
                     "（不用空閒）";
            a.kind = "pay", a.who = lord ? kNone : cred, a.target = static_cast<Id>(pay);
            a.done = fullName(head) + " 拿錢去還了" + whom + " " + pence(pay) + "。";
            a.rank = 54;
            items.push_back(a);
        };
        payItem(ph.creditor, ph.debt);
        payItem(ph.heldCreditor, ph.heldDebt);
    }

    // The lord's waste land for rent (design/fun-r6.md A): a family whose acres do not feed it is told of the strips
    // nobody holds, and can ask for one from the list (開口 the lord: the entry fine, then rent like any holding).
    if (!hiding && lord_ != kNone && people_[lord_].alive() && head != lord_) {
        double need = consumption(ph) * kDaysPerYear;
        bool short_ = ph.lean || keepsHouse(ph.acres) < need * 1.05;
        int vacant = 0;
        const AskOption* best = nullptr;
        std::vector<AskOption> asks = short_ ? asksFor(lord_) : std::vector<AskOption>{};
        for (const auto& o : asks) {
            if (o.ask != "rent_vacant") continue;
            ++vacant;
            const Parcel& pc = parcels_[o.object];
            if (!best || (o.open && !best->open) || (o.open == best->open && pc.acres > parcels_[best->object].acres)) best = &o;
        }
        if (best) {
            const Parcel& pc = parcels_[best->object];
            AgendaItem a;
            a.id = "rent_land:" + std::to_string(pc.id);
            a.title = best->label.substr(0, best->label.find("（")) + "（" + std::to_string(int(pc.acres)) + " 畝）";
            a.desc = "領主手上有 " + std::to_string(vacant) + " 塊空著沒人種的地。你家 " + std::to_string(int(ph.acres)) + " 畝地養 " +
                     std::to_string(ph.members.size()) + " 口人不太夠。" + best->label.substr(best->label.find("（")) + "，往後跟別的佃地一樣交租。";
            a.kind = "ask", a.who = lord_, a.event = "rent_vacant", a.target = pc.id;
            a.rank = 85;
            if (!best->open) a.open = false, a.why = best->why;
            items.push_back(a);
        }
    }

    // A hand for the harvest: spring and summer, on a holding big enough to want one.
    if (!hiding && month() >= 3 && month() < 8 && ph.acres >= 12 && ph.workBonus < 0.08) {
        auto cd = cooldowns_.find({ph.id, "hire_hand"});
        double pay = std::round(24 * param("wage_mult", 1));
        Id hand = harvestHand();
        if ((cd == cooldowns_.end() || cd->second <= day_) && hand != kNone && ph.money >= pay + 30) {
            double bonus = ph.acres >= 24 ? 0.07 : 0.05;
            AgendaItem a;
            a.id = "hire_hand:" + std::to_string(hand);  // the man shown is the man hired: pressed later, it is still him or nothing
            a.title = "雇 " + fullName(hand) + " 幫忙收割（" + pence(pay) + "）";
            a.desc = fullName(hand) + " 家地少，正缺工錢。雇他做到收完麥子，今年收成約多 " +
                     std::to_string(int(bonus * 100 + 0.5)) + "%。";
            a.kind = "effects", a.target = hand,
            a.effects = "[{\"hire_hand\": {\"pay\": " + std::to_string(int(pay)) + ", \"bonus\": " + std::to_string(bonus) + "}}]";
            a.done = fullName(head) + " 雇了 " + fullName(hand) + " 來幫忙收割，先付了 " + pence(pay) + "，請他做到收完麥子。";
            a.rank = 60;
            items.push_back(a);
        }
    }

    // Short of coin or of bread to the harvest: a free day turned into pay at once, not a plan to start (fun-r6 C).
    for (auto& a : spareWorkItems()) items.push_back(a);

    // The next step of this generation's ambition, and wage work when the purse is empty and money is owed.
    {
        std::vector<PlanOption> opts = availablePlans(head);
        auto best = [&](const std::string& plan) -> const PlanOption* {
            const PlanOption* pick = nullptr;
            for (const auto& op : opts)
                if (op.plan == plan && !named.count(op.subject) && (!pick || op.targetWeight > pick->targetWeight)) pick = &op;
            return pick;
        };
        auto planItem = [&](const PlanOption& op, const std::string& why, int rank) {
            const PlanDef* pd = defs_.plan(op.plan);
            AgendaItem a;
            a.id = "step:" + op.plan;
            a.title = (pd ? pd->name : op.plan) + (op.target != kNone ? "：" + fullName(op.target) : op.subject != kNone ? "（" + shortName(op.subject) + "）" : "");
            a.desc = why + (pd ? pd->desc : "") + "（用一段空閒）";
            a.kind = "plan", a.event = op.plan, a.target = op.target, a.subject = op.subject;
            a.rank = rank;
            items.push_back(a);
            if (op.subject != kNone) named.insert(op.subject);
        };
        std::string stepPlan;
        if (const AmbitionDef* amb = defs_.ambition(hp.ambition)) {
            for (const auto& pid : amb->plans)
                if (const PlanOption* op = best(pid)) {
                    planItem(*op, "為了「" + amb->name + "」：", 70);
                    stepPlan = pid;
                    break;
                }
        }
        if (stepPlan != "hire_out" && owedAll(ph.id) > 0.5 && ph.money < owedAll(ph.id))
            if (const PlanOption* op = best("hire_out"))
                planItem(*op, debtText(ph.id) + "，手上只有 " + pence(ph.money) + "。", 65);
    }

    // A match to make, when there is exactly one in the house of an age for it.
    {
        std::vector<PlanOption> opts = availablePlans(head);
        std::set<Id> subjects;
        const PlanOption* pick = nullptr;
        for (const auto& op : opts)
            if (op.plan == "arrange_match") {
                subjects.insert(op.subject);
                if (!pick || op.targetWeight > pick->targetWeight) pick = &op;
            }
        if (subjects.size() == 1 && pick && !named.count(pick->subject)) {
            AgendaItem a;
            a.id = "match:" + std::to_string(pick->subject) + ":" + std::to_string(pick->target);
            a.title = "替" + shortName(pick->subject) + "向" + fullName(pick->target) + "說親";
            a.desc = shortName(pick->subject) + "（" + std::to_string(int(ageYears(people_[pick->subject]))) + " 歲）到了成親的年紀，" +
                     fullName(pick->target) + "（" + std::to_string(int(ageYears(people_[pick->target]))) + " 歲）年紀身分都相當。託媒人去說（用一段空閒）。" + dowerNote(pick->subject);
            a.kind = "plan", a.event = "arrange_match", a.target = pick->target, a.subject = pick->subject;
            a.rank = 50;
            items.push_back(a);
            named.insert(pick->subject);
        }
        const PlanOption* court = nullptr;
        for (const auto& op : opts)
            if (op.plan == "court" && (!court || op.targetWeight > court->targetWeight)) court = &op;
        if (court) {
            AgendaItem a;
            a.id = "court:" + std::to_string(court->target);
            a.title = "去追求 " + fullName(court->target);
            a.desc = "你還沒成家，" + fullName(court->target) + "（" + std::to_string(int(ageYears(people_[court->target]))) + " 歲）對你" +
                     opinionWord(opinion(court->target, head)) + "。（用一段空閒）";
            a.kind = "plan", a.event = "court", a.target = court->target;
            a.rank = 52;
            items.push_back(a);
        }
        // Letters for a child: taught at the hearth by a head who reads, else sent to the priest.
        Id pupil = kNone;
        for (Id m : ph.members)
            if (!named.count(m) && inter(m, "talk_teach_letters") && (pupil == kNone || people_[m].skills[Learning] > people_[pupil].skills[Learning])) pupil = m;
        if (pupil != kNone) {
            AgendaItem a;
            a.id = "teach:" + std::to_string(pupil);
            a.title = "教" + shortName(pupil) + "認字";
            a.desc = "晚上在灶邊教" + shortName(pupil) + "（" + std::to_string(int(ageYears(people_[pupil]))) + " 歲）認字；學會了就是識字的人，能當書記、進教會。";
            a.kind = "interact", a.who = pupil, a.event = "talk_teach_letters";
            a.rank = 45;
            items.push_back(a);
            named.insert(pupil);
        } else {
            for (const auto& op : opts)
                if (op.plan == "school_child" && !named.count(op.subject)) {
                    AgendaItem a;
                    a.id = "school:" + std::to_string(op.subject);
                    a.title = "送" + shortName(op.subject) + "去神父那裡讀書";
                    a.desc = "每月兩便士和一些雞蛋，請神父教" + shortName(op.subject) + "認字（用一段空閒）。";
                    a.kind = "plan", a.event = "school_child", a.target = op.target, a.subject = op.subject;
                    a.rank = 44;
                    items.push_back(a);
                    named.insert(op.subject);
                    break;
                }
        }
    }

    // 陪家人: the one at home who most wants or needs the head's company, in the way they need it.
    {
        Id best = kNone;
        std::string bestEv;
        double bestScore = -1e9;
        for (Id m : ph.members) {
            if (m == head || named.count(m) || !people_[m].alive()) continue;
            std::string ev = companyEvent(m);
            if (ev.empty()) continue;
            const Person& p = people_[m];
            double s = 100 - p.needs[Social] * 0.5 - p.needs[Spirit] * 0.3 - opinion(m, head) * 0.3;
            if (const WantDef* wd = defs_.want(p.want); wd && std::find(wd->answeredBy.begin(), wd->answeredBy.end(), ev) != wd->answeredBy.end()) s += 100;
            for (const auto& c : p.concerns) s += c.strength * 0.3;
            if (s > bestScore) bestScore = s, best = m, bestEv = ev;
        }
        if (best != kNone) {
            const EventDef* ev = inter(best, bestEv);
            AgendaItem a;
            a.id = "company:" + std::to_string(best);
            a.title = "陪家人：" + (ev ? ev->title : bestEv) + "（" + shortName(best) + "）";
            std::string want = wantText(people_[best]);
            MoodView mv = moodView(people_[best]);
            a.desc = shortName(best) + "（" + (kinWord(head, best).empty() ? std::string("家裡人") : kinWord(head, best)) + "）" +
                     (!want.empty() ? want : !mv.cause.empty() ? mv.cause : "對你" + std::string(opinionWord(opinion(best, head)))) + "。";
            a.kind = "interact", a.who = best, a.event = bestEv;
            a.rank = 40;
            items.push_back(a);
        }
    }

    // The family's own drive (src/kin.cpp): what is brewing at home, how the holding will pass, the ladder in the head's hand.
    if (!hiding) kinAgenda(items);
    std::stable_sort(items.begin(), items.end(), [](const AgendaItem& x, const AgendaItem& y) { return x.rank > y.rank; });
    // One plan once: the ambition's step may be the match or the courting found below it.
    // And every item once: two hungry houses can name the same child to take in (help_take:<child>).
    std::set<std::tuple<std::string, Id, Id>> plansNamed;
    std::set<std::string> ids;
    items.erase(std::remove_if(items.begin(), items.end(), [&](const AgendaItem& x) {
                    return !ids.insert(x.id).second || (x.kind == "plan" && !plansNamed.insert({x.event, x.target, x.subject}).second);
                }), items.end());
    // Standing actions (sell spare grain, pay a debt) are always there, beside the four.
    std::vector<AgendaItem> standing;
    if (!all) {
        auto isStanding = [](const AgendaItem& x) { return x.id == "sell_grain" || x.id.rfind("pay_debt:", 0) == 0 || x.kind == "kin"; };
        std::copy_if(items.begin(), items.end(), std::back_inserter(standing), isStanding);
        items.erase(std::remove_if(items.begin(), items.end(), isStanding), items.end());
    }
    // At most four, and the company kept among them when there is room: the season's list, not the village's.
    if (items.size() > 4 && !all) {
        auto company = std::find_if(items.begin(), items.end(), [](const AgendaItem& x) { return x.id.rfind("company:", 0) == 0; });
        AgendaItem keep;
        bool had = company != items.end() && company - items.begin() >= 4;
        if (had) keep = *company;
        items.resize(had ? 3 : 4);
        if (had) items.push_back(keep);
    }
    for (auto& a : items) {
        double money = 0;
        if (a.kind == "kin") continue;  // a card of its own, which says what it costs (time included)
        if (a.kind == "interact")
            for (const auto& ev : defs_.events)
                if (ev.id == a.event) money = ev.interaction.numberOr("money", 0);
        if (left < 1) a.open = false, a.why = "這一季沒有空了";
        else if (money > ph.money + 0.5) a.open = false, a.why = "錢不夠";
    }
    // A company visit that cannot be made is not offered (fun-r8: 「最近才陪過」 on a button that was there).
    items.erase(std::remove_if(items.begin(), items.end(), [](const AgendaItem& x) { return !x.open && x.id.rfind("company:", 0) == 0; }), items.end());
    items.insert(items.end(), standing.begin(), standing.end());
    if (!all) {
        // Everything shown this season, not only the last list: a button pressed after its item was done or
        // dropped off is still known, and told why.
        int key = year() * 4 + month() / 3;
        if (key != lastAgendaSeason_) lastAgenda_.clear(), agendaDone_.clear(), lastAgendaSeason_ = key;
        for (const auto& a : items) {
            auto old = std::find_if(lastAgenda_.begin(), lastAgenda_.end(), [&](const AgendaItem& x) { return x.id == a.id; });
            if (old != lastAgenda_.end()) *old = a;
            else lastAgenda_.push_back(a);
        }
    }
    return items;
}

// Why a thing that was on the season's list can no longer be done: it was offered for a reason, and the reason that
// went away is said (a visit's own check, the grain gone below what must be kept, the hungry house fed again).
std::string World::agendaGoneWhy(const AgendaItem& a) {
    Id head = actingHead();
    const Household& ph = households_[playerHousehold_];
    auto houseOf = [&](Id who) { return who != kNone && people_[who].household != kNone ? fullName(households_[people_[who].household].head) + " 家" : std::string("那一家"); };
    if (a.kind == "interact") {
        if (a.who == kNone || !people_[a.who].alive()) return "人已經不在了";
        for (const auto& in : interactionsWith(a.who))
            if (in.ev->id == a.event && !in.open) return in.why.empty() ? "最近才去過" : in.why;
        return "最近才陪過" + shortName(a.who);
    }
    const std::string kind = a.id.substr(0, a.id.find(':'));
    if (kind == "pay_debt") return "這筆債已經還了，或手上的錢不夠了";
    if (kind == "sell_grain") {
        double c = std::max(0.5, consumption(ph));
        return "存糧只剩 " + std::to_string(int(ph.food / c)) + " 天，要留到收成後三個月（" + std::to_string(daysToHarvest() + 90) + " 天），沒有多的可賣了";
    }
    if (kind == "hire_hand") return a.target != kNone && people_[a.target].household != kNone && households_[people_[a.target].household].hiredUntil > day_
                                       ? shortName(a.target) + "已經被別家雇走了" : ph.money < 54 ? "錢不夠了（雇工錢之外還得留 30 便士）" : "今年已經雇過幫手了";
    if (kind == "help_hire" || kind == "help_land" || kind == "help_take" || kind == "help_feed") {
        Id hid = a.target != kNone ? people_[a.target].household : kNone;
        if (hid == kNone) return "那一家已經不在了";
        const Household& h = households_[hid];
        double days = h.food / std::max(0.5, consumption(h));
        if (days >= 45 && !(h.lean && days < 120)) return houseOf(a.target) + "的糧接上了（有 " + std::to_string(int(days)) + " 天）";
        if (kind == "help_take") return "你家的糧撐不起多一張嘴了（要 120 天以上、今年不缺糧）";
        if (kind == "help_land") return "你家沒有分得出去、自己又吃得飽的那幾畝地了";
        if (kind == "help_feed") return "這一季已經為挨餓的人家拿過主意了";
        return "他家的人眼下雇不了";
    }
    if (kind == "spare") {
        double c = std::max(0.5, consumption(ph));
        return "手頭和存糧都不算緊了（手上 " + pence(ph.money) + "，存糧夠 " + std::to_string(int(ph.food / c)) + " 天），或這一季已經做過";
    }
    (void)head;
    return "情形變了";
}

void World::doAgenda(const std::string& id) {
    Id head = actingHead();
    if (head == kNone) return;
    // The whole list as it stands now (not cut to four), then the one shown: a visit or a plan that was offered is
    // still done if its own check holds; anything else that dropped off says what changed.
    std::vector<AgendaItem> items = seasonAgenda(true);
    auto it = std::find_if(items.begin(), items.end(), [&](const AgendaItem& a) { return a.id == id; });
    AgendaItem a;
    if (it != items.end()) {
        a = *it;
    } else {
        auto done = agendaDone_.find(id);
        if (done != agendaDone_.end() && lastAgendaSeason_ == year() * 4 + month() / 3) {
            log(1, "〔這一季的事〕" + done->second + "：這一季已經辦過了。");
            return;
        }
        auto shown = std::find_if(lastAgenda_.begin(), lastAgenda_.end(), [&](const AgendaItem& x) { return x.id == id; });
        if (shown == lastAgenda_.end()) {
            log(1, "〔這一季的事〕這一季的單子上沒有這件事（" + id + "）。");
            return;
        }
        a = *shown;
        bool still = false;
        if (a.kind == "interact" && a.who != kNone && people_[a.who].alive()) {
            if (a.id.rfind("company:", 0) == 0) a.event = companyEvent(a.who);  // the visit they need most now
            for (const auto& in : interactionsWith(a.who))
                if (in.ev->id == a.event && in.open) still = true;
        }
        if (a.kind == "plan" || a.kind == "ask") still = true;  // startPlan / ask check it and say why not
        if (!still) {
            log(1, "〔這一季的事〕" + a.title + "辦不成了：" + agendaGoneWhy(a) + "。");
            return;
        }
        a.open = timeLeft() >= 1;
        a.why = a.open ? "" : "這一季沒有空了";
    }
    if (a.kind == "pay") {
        payCreditor(households_[playerHousehold_], a.who, static_cast<double>(a.target));
        log(1, "〔這一季的事〕" + a.done);
        return;
    }
    if (a.id == "sell_grain") {
        log(1, "〔這一季的事〕" + a.done);
        EventRunner er(*this);
        er.apply(Json::parse(a.effects), people_[head], a.target);
        return;
    }
    if (a.kind == "kin") {
        doKinAgenda(a);
        return;
    }
    if (!a.open) {
        log(1, "〔這一季的事〕" + a.title + "：" + a.why + "。");
        return;
    }
    ++counters_["agenda:" + a.id.substr(0, a.id.find(':'))];
    // Done once it took the time it costs (a plan or an ask that was refused stays on the list, with its reason).
    const int before = timeLeft();
    struct MarkDone {
        World& w;
        const AgendaItem& a;
        int before;
        ~MarkDone() {
            if (w.timeLeft() < before) w.agendaDone_[a.id] = a.title;
        }
    } mark{*this, a, before};
    if (a.kind == "interact") {
        for (const auto& ev : defs_.events)
            if (ev.id == a.event && ev.scope == "interaction") {
                EventRunner er(*this);
                er.interact(ev, people_[a.who], head);
                return;
            }
    } else if (a.kind == "plan") {
        const PlanDef* pd = defs_.plan(a.event);
        std::string why = startPlan(head, a.event, a.target, a.subject);
        if (why.empty()) spendTime(1);
        else log(1, "〔計畫〕「" + (pd ? pd->name : a.event) + "」眼下辦不成：" + why + "。");
    } else if (a.kind == "ask") {
        ask(a.who, a.event, a.target, kNone, "", "");  // takes the day itself, and says why not when it cannot
    } else if (a.kind == "effects") {
        if (!spendTime(1)) return;
        log(1, "〔這一季的事〕" + a.done);
        EventRunner er(*this);
        er.apply(Json::parse(a.effects), people_[head], a.target);
        seasonNotes_.push_back(a.done);
    } else if (a.kind == "feed") {
        // The hungry house's card, every way out on it; going to see them takes the day, whatever the family then does.
        Id hid = a.target != kNone && people_[a.target].alive() ? people_[a.target].household : kNone;
        if (hid == kNone) {
            log(1, "〔這一季的事〕" + a.title + "辦不成了：那一家已經不在了。");
            return;
        }
        if (feedMenu(households_[hid], true)) spendTime(1);
        else log(1, "〔這一季的事〕" + a.title + "辦不成了：你家眼下沒有拿得出手的接濟（糧、錢、活、地都不夠）。");
    }
}

}  // namespace sim
