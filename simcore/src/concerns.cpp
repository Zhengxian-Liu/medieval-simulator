// Mood, what is on people's minds, and the head's season (design/season-loop.md). Big events leave
// concerns behind (grief, a lost match, a grudge against one's father); concerns and needs and stress
// make up a mood the player can read in words, and the mood shows in the work and the body. The head
// has a few free days a season to spend on people (data/events/interactions.json) or on plans; days
// left over go into the fields.
#include <algorithm>
#include <cmath>

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
    Id head = playerHead();
    if (head == kNone) return;
    int season = year() * 4 + month() / 3;
    // A post the family wanted but its head can no longer stand for (a widow holds the house now, debt, age):
    // said at once, not left to end as "taken by another house".
    for (auto& wt : wants_)
        if (wt.open && wt.kind == "office" && wt.house == playerHousehold_) {
            std::string bar = officeBar(head, wt.office);
            if (bar.empty()) continue;
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
            if (!er.eval(w->when, p, head)) {
                p.want.clear();  // it passed of itself
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
    Id head = playerHead();
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
    Id head = playerHead();
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
    Id head = playerHead();
    if (head == kNone || target == kNone || target == head) return out;
    Person& tp = people_[target];
    if (!tp.alive() || tp.household == kNone) return out;
    bool home = inPlayerHouse(tp.household);
    bool kin = home || isPlayerFamily(tp) || closeKin(head, target);
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
        double money = -ev.interaction.numberOr("money", 0);
        if (cd != cooldowns_.end() && cd->second > day_) in.why = "最近才" + ev.title + "過";
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
    s.head = playerHead();
    if (s.head == kNone) return s;
    const Household& h = households_[playerHousehold_];
    for (Id m : h.members)
        if (people_[m].alive()) {
            s.people[m] = {moodView(people_[m]).word, m == s.head ? 0 : opinion(m, s.head)};
            if (m != s.head)
                for (const auto& [lab, v] : opinionBreakdown(m, s.head)) s.why[m][lab] += v;
        }
    s.money = h.money;
    s.food = h.food / std::max(1.0, consumption(h));
    s.acres = h.acres;
    s.ledger = h.ledger;
    return s;
}

std::string World::seasonChanges(const FamilySnapshot& before) const {
    Id head = playerHead();
    if (head == kNone || before.head == kNone) return "";
    const Household& h = households_[playerHousehold_];
    std::string o;
    for (const auto& n : lastSeasonNotes_) o += "  " + n + "\n";
    lastSeasonNotes_.clear();
    for (const auto& [id, was] : before.people) {
        const Person& p = people_[id];
        if (!p.alive() || !inPlayerHouse(p.household)) continue;
        std::string who = id == head ? "你" : shortName(id);
        MoodView mv = moodView(p);
        if (mv.word != was.first) o += "  " + who + "從" + was.first + "變得" + mv.word + (mv.cause.empty() ? "" : "：" + mv.cause) + "。\n";
        if (id != head && head == before.head) {
            double now = opinion(id, head), d = now - was.second;
            if (std::abs(d) >= 3) {
                // Name the reasons that moved, so the numbers add up: new ones, grown, faded.
                std::map<std::string, double> diff;
                auto bw = before.why.find(id);
                if (bw != before.why.end())
                    for (const auto& [lab, v] : bw->second) diff[lab] -= v;
                for (const auto& [lab, v] : opinionBreakdown(id, head)) diff[lab] += v;
                std::vector<std::pair<double, std::string>> moved;
                double named = 0;
                for (const auto& [lab, v] : diff)
                    if (std::abs(v) >= 1) moved.push_back({v, lab});
                std::sort(moved.begin(), moved.end(), [](auto& a, auto& b) { return std::abs(a.first) > std::abs(b.first); });
                std::string why;
                for (size_t i = 0; i < moved.size() && i < 3; ++i) {
                    why += (why.empty() ? "" : "、") + opinionLabel(moved[i].second) + " " + signedText(moved[i].first);
                    named += moved[i].first;
                }
                if (std::abs(d - named) >= 2) why += (why.empty() ? "" : "、") + std::string("平常的往來 ") + signedText(d - named);
                std::string words = opinionWord(was.second) == std::string(opinionWord(now))
                                        ? std::string(opinionWord(now))
                                        : std::string(opinionWord(was.second)) + " → " + opinionWord(now);
                o += "  " + who + "對你" + (d > 0 ? "更親近了" : "疏遠了") + "：" + signedText(was.second) + " → " + signedText(now) +
                     "，" + words + (why.empty() ? "" : "（" + why + "）") + "。\n";
            }
        }
    }
    double food = h.food / std::max(1.0, consumption(h));
    if (std::abs(h.money - before.money) >= 5) {
        // Where it went: the biggest items of the season's account, the rest as other.
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
        o += "  存錢 " + std::to_string(int(before.money)) + " → " + std::to_string(int(h.money)) + " 便士" + (why.empty() ? "" : "（" + why + "）") + "。\n";
    }
    if (food - before.food >= 45 || (food < 60 && before.food >= 60)) o += "  存糧 " + std::to_string(int(before.food)) + " → " + std::to_string(int(food)) + " 天。\n";
    if (std::abs(h.acres - before.acres) >= 0.5) o += "  田地 " + std::to_string(int(before.acres)) + " → " + std::to_string(int(h.acres)) + " 畝。\n";
    return o.empty() ? "" : "這一季的變化：\n" + o;
}

}  // namespace sim
