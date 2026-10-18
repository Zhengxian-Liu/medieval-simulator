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
            // A match nobody could make yet (no one suitable, or the last one just fell through) is not the head's neglect.
            bool matchBlocked = false;
            if (std::find(w->answeredBy.begin(), w->answeredBy.end(), "plan:arrange_match") != w->answeredBy.end()) {
                matchBlocked = true;
                for (const auto& op : availablePlans(head))
                    if (op.plan == "arrange_match" && op.subject == m) matchBlocked = false;
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
            fine += (fine.empty() ? "" : "、") + shortName(m) + "（" + kinWord(head, m) + "）";
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
    s.food = h.food / std::max(1.0, consumption(h));
    s.acres = h.acres;
    s.ledger = h.ledger;
    return s;
}

// "存錢 120 → 98 便士（交租 -40、賣餘糧 +18）。": the family's money since a snapshot, item by item.
std::string World::moneySince(const FamilySnapshot& before) const {
    const Household& h = households_[playerHousehold_];
    std::vector<std::pair<double, std::string>> items;
    double listed = 0;
    for (const auto& [why, total] : h.ledger) {
        auto it = before.ledger.find(why);
        double d = total - (it == before.ledger.end() ? 0 : it->second);
        if (std::abs(d) >= 1) items.push_back({d, why});
    }
    std::sort(items.begin(), items.end(), [](auto& a, auto& b) { return std::abs(a.first) > std::abs(b.first); });
    std::string why;
    for (size_t i = 0; i < items.size() && i < 4; ++i) {
        why += (why.empty() ? "" : "、") + items[i].second + " " + signedText(items[i].first);
        listed += items[i].first;
    }
    double rest = (h.money - before.money) - listed;
    if (std::abs(rest) >= 2) why += (why.empty() ? "" : "、") + std::string("其他 ") + signedText(rest);
    return "存錢 " + std::to_string(int(before.money)) + " → " + std::to_string(int(h.money)) + " 便士" + (why.empty() ? "" : "（" + why + "）") + "。";
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
    double food = h.food / std::max(1.0, consumption(h));
    if (std::abs(h.money - before.money) >= 5) o += "  " + moneySince(before) + "\n";
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

int World::daysToHarvest() const {
    int m = month() < 8 ? 8 - month() : 20 - month();
    return m * kDaysPerMonth - dayOfMonth();
}

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
        if ((ph.acres - pc.acres) * 100 + 220 < need * 1.1) continue;
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
        oweFavour(fh.head, giver, 15, "收留了家裡的人");
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

std::vector<World::AgendaItem> World::seasonAgenda() {
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
            double days = h.food / std::max(1.0, consumption(h));
            if (days >= 45 && !(h.lean && days < 120)) continue;
            bool kin = false;
            for (Id m : h.members) kin = kin || closeKin(head, m) || !kinTie(head, m).empty();
            if (!kin && opinion(head, h.head) < 40) continue;
            hungry.push_back({days, h.id});
        }
        std::sort(hungry.begin(), hungry.end());
        for (size_t k = 0; k < hungry.size() && k < 2; ++k) {
            const Household& h = households_[hungry[k].second];
            std::string who = fullName(h.head) + " 家";
            std::string state = "只剩 " + std::to_string(int(hungry[k].first)) + " 天的糧" + (h.lean ? "，今年的收成也不夠" : "");
            if (canHireKin(h.id)) {
                AgendaItem a;
                a.id = "help_hire:" + std::to_string(h.id);
                a.title = "雇" + who + "的人當長工";
                a.desc = who + state + "。讓他家能幹活的人來你家做一年，管吃，每月每人 4 便士：他家有了口糧，你家多了人手。";
                a.kind = "effects", a.target = h.head, a.effects = "[{\"hire_kin\": true}]";
                a.done = fullName(head) + " 雇了" + who + "的人來家裡當長工，管吃管住，做到明年這時候。";
                a.rank = 92;
                items.push_back(a);
            }
            if (Id pc = spareParcel(h.id); pc != kNone) {
                AgendaItem a;
                a.id = "help_land:" + std::to_string(h.id);
                a.title = "分" + parcelName(parcels_[pc]) + "給" + who;
                a.desc = who + state + "。把這塊地讓給他們種（再送一個月的糧撐到收成），往後他們自己就吃得上飯；你家少了這幾畝" +
                         std::string(parcels_[pc].free ? "，而且這是你家的自由地，送出去自家的身分可能跟著降一等。" : "。");
                a.kind = "effects", a.target = h.head, a.effects = "[{\"give_parcel\": " + std::to_string(pc) + "}]";
                a.done = fullName(head) + " 把" + parcelName(parcels_[pc]) + "分給了" + who + "，讓他們往後自己種糧。";
                a.rank = 90;
                items.push_back(a);
            }
            // Another mouth at the table only when the family's own bread will stretch to it.
            bool room = ph.food >= consumption(ph) * 120 && !ph.lean;
            if (Id t = room ? takeInWho(h.id) : kNone; t != kNone) {
                AgendaItem a;
                a.id = "help_take:" + std::to_string(t);
                a.title = "把" + shortName(t) + "接到家裡來住";
                a.desc = who + state + "。" + shortName(t) + "（" + std::to_string(int(ageYears(people_[t]))) + " 歲）搬來你家，吃你家的飯；他家少一張嘴。";
                a.kind = "effects", a.target = t, a.effects = "[{\"take_in\": true}]";
                a.done = fullName(head) + " 把 " + fullName(t) + " 從" + who + "接到家裡來住，往後吃住都在你家。";
                a.rank = 88;
                items.push_back(a);
            }
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
            a.desc = shortName(worst) + "（" + kinWord(head, worst) + "）" + (why.empty() ? "心裡不好受" : why) + "。";
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
        double c = std::max(1.0, consumption(ph));
        double spare = ph.food / c - daysToHarvest() - 90;
        if (spare >= 60) {
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

    // A hand for the harvest: spring and summer, on a holding big enough to want one.
    if (!hiding && month() >= 3 && month() < 8 && ph.acres >= 12 && ph.workBonus < 0.08) {
        auto cd = cooldowns_.find({ph.id, "hire_hand"});
        double pay = std::round(24 * param("wage_mult", 1));
        Id hand = harvestHand();
        if ((cd == cooldowns_.end() || cd->second <= day_) && hand != kNone && ph.money >= pay + 30) {
            double bonus = ph.acres >= 24 ? 0.07 : 0.05;
            AgendaItem a;
            a.id = "hire_hand";
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
                     fullName(pick->target) + "（" + std::to_string(int(ageYears(people_[pick->target]))) + " 歲）年紀身分都相當。託媒人去說（用一段空閒）。";
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
            a.desc = shortName(best) + "（" + kinWord(head, best) + "）" +
                     (!want.empty() ? want : !mv.cause.empty() ? mv.cause : "對你" + std::string(opinionWord(opinion(best, head)))) + "。";
            a.kind = "interact", a.who = best, a.event = bestEv;
            a.rank = 40;
            items.push_back(a);
        }
    }

    std::stable_sort(items.begin(), items.end(), [](const AgendaItem& x, const AgendaItem& y) { return x.rank > y.rank; });
    // At most four, and the company kept among them when there is room: the season's list, not the village's.
    if (items.size() > 4) {
        auto company = std::find_if(items.begin(), items.end(), [](const AgendaItem& x) { return x.id.rfind("company:", 0) == 0; });
        AgendaItem keep;
        bool had = company != items.end() && company - items.begin() >= 4;
        if (had) keep = *company;
        items.resize(had ? 3 : 4);
        if (had) items.push_back(keep);
    }
    for (auto& a : items) {
        double money = 0;
        if (a.kind == "interact")
            for (const auto& ev : defs_.events)
                if (ev.id == a.event) money = ev.interaction.numberOr("money", 0);
        if (left < 1) a.open = false, a.why = "這一季沒有空了";
        else if (money > ph.money + 0.5) a.open = false, a.why = "錢不夠";
    }
    return items;
}

void World::doAgenda(const std::string& id) {
    Id head = actingHead();
    if (head == kNone) return;
    std::vector<AgendaItem> items = seasonAgenda();
    auto it = std::find_if(items.begin(), items.end(), [&](const AgendaItem& a) { return a.id == id; });
    if (it == items.end()) {
        log(1, "〔這一季的事〕情形變了，這件事眼下辦不成了。");
        return;
    }
    AgendaItem a = *it;
    if (!a.open) {
        log(1, "〔這一季的事〕" + a.title + "：" + a.why + "。");
        return;
    }
    ++counters_["agenda:" + a.id.substr(0, a.id.find(':'))];
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
    } else if (a.kind == "effects") {
        if (!spendTime(1)) return;
        log(1, "〔這一季的事〕" + a.done);
        EventRunner er(*this);
        er.apply(Json::parse(a.effects), people_[head], a.target);
        seasonNotes_.push_back(a.done);
    }
}

}  // namespace sim
