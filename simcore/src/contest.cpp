// The contested ledger (design/contested.md): the posts and places only so many can have, what each house
// is after, the rivalries where those meet, the favours that are the village's other currency, the entry
// fine a new holder owes the lord, and the head asking someone for something.
#include <algorithm>
#include <cmath>
#include <set>

#include "simcore/world.h"

namespace sim {

namespace {
const char* const kTrades[] = {"smith", "carpenter", "weaver", "miller"};
bool isTrade(const std::string& occ) {
    for (const char* t : kTrades)
        if (occ == t) return true;
    return false;
}
std::string pence(double v) { return std::to_string(static_cast<long>(std::lround(v))) + " 便士"; }
}  // namespace

// ---- Posts -------------------------------------------------------------------------------------

void World::setupOffices() {
    offices_.clear();
    for (auto [id, name] : {std::pair{"reeve", "莊頭"}, std::pair{"aletaster", "品酒官"}, std::pair{"hayward", "看籬人"}}) {
        Office o;
        o.id = id;
        o.name = name;
        offices_.push_back(o);
    }
    for (const auto& p : people_)
        if (p.alive() && p.flags.count("reeve")) {
            offices_[0].holder = p.id;
            offices_[0].until = nextReeveYear_;
            offices_[0].how = "選上的";
        }
}

const Office* World::office(const std::string& id) const {
    for (const auto& o : offices_)
        if (o.id == id) return &o;
    return nullptr;
}

void World::setOffice(const std::string& id, Id holder, const std::string& how) {
    // A man takes one post at a time: whatever else he held falls vacant (the reeve gives up the hayward's horn).
    if (holder != kNone)
        for (auto& o : offices_)
            if (o.id != id && o.holder == holder) {
                o.holder = kNone;
                people_[holder].flags.erase(o.id);
                if (inPlayerHouse(people_[holder].household))
                    log(1, "【差事】" + fullName(holder) + " 當了" + (office(id) ? office(id)->name : id) + "，" + o.name + "的差事就交出去了。");
            }
    for (auto& o : offices_) {
        if (o.id != id) continue;
        if (o.holder != kNone && o.holder != holder) people_[o.holder].flags.erase(id);
        if (holder != kNone && o.holder != holder) noteLife(holder, "當上了" + o.name);
        o.holder = holder;
        o.how = how;
        o.until = id == "reeve" ? nextReeveYear_ : year() + 1;
        if (holder != kNone) people_[holder].flags.insert(id);
    }
}

std::string World::officeBar(Id who, const std::string& id) const {
    for (const auto& g : officeGates(who, id))
        if (!g.ok) return g.detail;
    return "";
}

// Each condition of the post in turn. What bars a man from every post (the lord's household, the priest, a
// servant, a woman) is told only when it does, and alone: there is nothing there to work towards.
std::vector<OfficeGate> World::officeGates(Id who, const std::string& id) const {
    std::vector<OfficeGate> g;
    auto add = [&](const std::string& label, bool ok, const std::string& detail) { g.push_back({label, ok, detail}); };
    auto only = [&](const std::string& label, const std::string& detail) { return std::vector<OfficeGate>{{label, false, detail}}; };
    if (who == kNone) return only("家裡有當家的", "家裡沒有當家的");
    const Person& p = people_[who];
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    if (!p.alive() || p.household == kNone) return only("人在村裡", "人已經不在了");
    if (p.household == lordHouse || gentry(who)) return only("不是領主家的人", "領主家的人不當村裡的差事");
    if (p.occupation == "priest") return only("不是神父", "神父不當村裡的差事");
    if (p.home != kNone) return only("住在自己家", shortName(who) + "在別人家幫傭");
    if (p.sex != Sex::Male) return only("是男人", "村裡的差事只有男人能當，當家的" + shortName(who) + "是女人");
    const Household& h = households_[p.household];
    auto ages = [&](const std::string& post, int lo, int hi) {
        double a = ageYears(p);
        int y = static_cast<int>(a);
        std::string range = post + "要 " + std::to_string(lo) + " 到 " + std::to_string(hi) + " 歲";
        add("年紀 " + std::to_string(lo) + "–" + std::to_string(hi), y >= lo && a <= hi,
            y < lo   ? shortName(who) + "還要 " + std::to_string(std::max(1, static_cast<int>(std::ceil(lo - a)))) + " 年才滿 " +
                           std::to_string(lo) + " 歲（" + range + "）"
            : a > hi ? shortName(who) + "已經 " + std::to_string(y) + " 歲（" + range + "）"
                     : "現在 " + std::to_string(y) + " 歲");
    };
    if (id == "reeve") {
        add("一家之主", h.head == who, "莊頭得是一家之主");
        ages("莊頭", 25, 65);
        add("種地的人", p.occupation == "peasant", "莊頭得是種地的人，做手藝和買賣的不當");
        // All the house owes, counted as the header shows it (whole pence): one number on every page.
        const long owed = static_cast<long>(std::floor(owedAll(h.id) + 1e-9));
        add("欠債不到 20 便士", owed < 20, "欠債 " + std::to_string(owed) + "／上限 20（沒人放心讓欠債的人收租）");
        auto bound = cooldowns_.find({who, "lords_debtor"});
        bool beholden = bound != cooldowns_.end() && bound->second > day_;
        add("不欠領主免債的人情", !beholden,
            beholden ? "欠著領主免債的人情，到 " + std::to_string(cfg_.startYear + bound->second / kDaysPerYear) + " 年（村裡人不會選領主捏在手裡的人）" : "");
        return g;
    }
    add("不兼莊頭", !p.flags.count("reeve"), "已經是莊頭了，不能兼");
    // One minor post a man: the one he has he may stand for again, not a second.
    for (const auto& o : offices_)
        if (o.id != id && o.id != "reeve" && o.holder == who) add("沒兼別的差事", false, shortName(who) + "已經當著" + o.name + "了");
    if (id == "aletaster") {
        ages("品酒官", 25, 65);
        add("一家之主", h.head == who, "品酒官得是一家之主");
        bool alewife = false;
        for (Id m : h.members) alewife = alewife || people_[m].occupation == "alewife";
        add("家裡沒人釀酒賣", !alewife, "家裡有人釀酒賣，不能自己嚐自己的酒");
        return g;
    }
    ages("看籬人", 20, 60);
    return g;
}

std::string World::gatesText(const std::vector<OfficeGate>& gates) {
    std::string out;
    for (const auto& gt : gates) out += (out.empty() ? "" : "　") + std::string(gt.ok ? "✓ " + gt.label : "✗ " + gt.detail);
    return out;
}

int World::familyOffices(Id dynasty) const {
    int n = 0;
    for (const auto& o : offices_)
        n += dynasty != kNone && o.holder != kNone && people_[o.holder].alive() && people_[o.holder].dynasty == dynasty;
    return n;
}

int World::reeveTerms(Id dynasty) const {
    Id r = currentReeve();
    if (r == kNone || dynasty == kNone || people_[r].dynasty != dynasty) return 0;
    auto t = reeveTerms_.find(r);
    return t != reeveTerms_.end() ? std::max(1, t->second) : 1;
}

void World::officeVacated(Id person) {
    for (auto& o : offices_)
        if (o.holder == person) o.holder = kNone;
    for (const char* id : {"reeve", "aletaster", "hayward"}) people_[person].flags.erase(id);  // the dead hold no post
}

// ---- Elections ---------------------------------------------------------------------------------
// The reeve at Lammas (every three years, or when the post falls vacant), the ale-taster and the hayward at
// Michaelmas. The heads of household vote for whom they like best; a promise (開口) and a feud weigh heavily.

double World::officeMerit(const Person& p, const std::string& id) const {
    if (id == "reeve") return p.skills[Charm] + p.reputation / 4 + p.skills[Farming] / 3 + traitMod(p, "charm") * 3;
    if (id == "aletaster") return p.reputation / 4 + p.skills[Charm] / 2;
    return p.skills[Farming] / 3 + (hasTrait(p, "strong") ? 3 : 0) + (households_[p.household].acres < 10 ? 2 : 0);
}

Id World::currentReeve() const {
    for (const auto& p : people_)
        if (p.flags.count("reeve")) return p.id;
    return kNone;
}

Id World::familyOfficeMan(const std::string& id, Id preferred) const {
    auto ok = [&](Id m) { return m >= 0 && m < static_cast<Id>(people_.size()) && people_[m].alive() && inPlayerHouse(people_[m].household) && officeBar(m, id).empty(); };
    if (ok(preferred)) return preferred;
    Id head = playerHead();
    if (ok(head)) return head;
    if (playerHousehold_ != kNone)
        for (Id m : households_[playerHousehold_].members)
            if (ok(m)) return m;
    return kNone;
}

std::string World::familyOfficeBar(const std::string& id) const {
    return familyOfficeMan(id, kNone) != kNone ? "" : officeBar(playerHead(), id);
}

bool World::reeveDue() const {
    Id old = currentReeve();
    return old == kNone || !officeEligible(old, "reeve") || year() >= nextReeveYear_;
}

std::vector<Id> World::officeCandidates(const std::string& id, Id old, const std::function<double(double)>& noise) const {
    Id head = playerHead();
    std::vector<Id> cands;
    auto add = [&](Id c) {
        if (c != kNone && officeEligible(c, id) && std::find(cands.begin(), cands.end(), c) == cands.end()) cands.push_back(c);
    };
    auto wanter = [&](const Want& wt) { return wt.subject != kNone ? wt.subject : households_[wt.house].head; };
    std::vector<std::pair<double, Id>> ranked;
    if (id == "reeve") {
        // The likeliest three, those who let it be known they want it, and the family's man if he wants it or is
        // among the likeliest five.
        for (const auto& p : people_)
            if (officeEligible(p.id, id)) ranked.push_back({officeMerit(p, id) + noise(4) + (p.id == old ? 3 : 0), p.id});
        std::sort(ranked.rbegin(), ranked.rend());
        for (size_t i = 0; i < ranked.size() && cands.size() < 3; ++i) add(ranked[i].second);
        add(old);  // the sitting reeve stands again, if he still may
        for (const auto& wt : wants_)
            if (wt.open && wt.kind == "office" && wt.office == id && cands.size() < 4 && wanter(wt) != head) add(wanter(wt));
        bool runs = head != kNone && officeEligible(head, id) && std::find_if(wants_.begin(), wants_.end(), [&](const Want& wt) {
                        return wt.open && wt.house == playerHousehold_ && wt.kind == "office" && wt.office == id; }) != wants_.end();
        for (size_t i = 0; i < ranked.size() && i < 5; ++i) runs = runs || ranked[i].second == head;
        if (runs) add(head);
        return cands;
    }
    // Minor posts: those who want it, the man who has it, then the likeliest others until there are two.
    for (const auto& wt : wants_)
        if (wt.open && wt.kind == "office" && wt.office == id) add(wanter(wt));
    if (old != kNone && people_[old].alive()) add(old);  // the one who has it stands again, the family's own too
    for (const auto& p : people_)
        if (officeEligible(p.id, id) && !inPlayerHouse(p.household)) ranked.push_back({officeMerit(p, id) + noise(4), p.id});
    std::sort(ranked.rbegin(), ranked.rend());
    for (size_t i = 0; i < ranked.size() && cands.size() < 2; ++i) add(ranked[i].second);
    return cands;
}

World::Tally World::countVotes(const std::string& id, const std::vector<Id>& cands, Id old, const std::function<double(double)>& noise) const {
    Tally t;
    Id head = playerHead();
    for (Id c : cands)
        if (t.mine == kNone && inPlayerHouse(people_[c].household)) t.mine = c;
    bool familyRuns = t.mine != kNone;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (const auto& h : households_) {
        if (h.extinct || h.members.empty() || h.head == kNone || h.id == lordHouse) continue;
        const Person& voter = people_[h.head];
        if (voter.occupation == "priest" || ageYears(voter) < 21) continue;
        auto pr = promises_.find({voter.id, id});
        Id pick = kNone;
        double best = -1e9;
        for (Id c : cands) {
            double s = people_[c].household == h.id ? 1e6 : opinion(voter.id, c) + officeMerit(people_[c], id) * 2 + noise(5);
            bool promised = pr != promises_.end() && (pr->second == c || (c == t.mine && pr->second == head));
            if (promised && feudStage(voter.id, c) < 2) s += 90;  // gave his word (開口): kept, short of his own house or a feud
            if (people_[c].household != h.id && people_[c].household != kNone &&
                std::find_if(wants_.begin(), wants_.end(), [&](const Want& wt) {
                    return wt.open && wt.house == people_[c].household && wt.kind == "office" && wt.office == id; }) != wants_.end() &&
                opinion(voter.id, c) >= 20)
                s += 6;  // he asked around for it
            if (id == "reeve") {
                if (people_[c].flags.count("campaign_ale")) s += 10;
                // After two terms the village tires of the same man collecting its rents.
                auto terms = reeveTerms_.find(c);
                if (c == old && terms != reeveTerms_.end() && terms->second >= 2) s -= 6 * (terms->second - 1);
            }
            if (feudStage(voter.id, c) >= 2) s -= 40;
            if (s > best) best = s, pick = c;
        }
        if (pick == kNone) continue;
        ++t.votes[pick];
        if (!familyRuns || inPlayerHouse(h.id)) continue;
        Id m = t.mine;
        bool toUs = pr != promises_.end() && (pr->second == m || pr->second == head);
        if (pick == m && toUs) t.byWord.push_back(voter.id);
        if (pick != m && toUs && feudStage(voter.id, m) < 2) t.broke.push_back(voter.id);
        if (pick != m && people_[pick].household != h.id && feudStage(voter.id, m) >= 2) t.grudge.push_back(voter.id);
    }
    return t;
}

std::string World::tallyText(const std::vector<Id>& cands, const Tally& t, bool forecast) const {
    Id head = playerHead();
    std::vector<Id> order = cands;
    std::stable_sort(order.begin(), order.end(), [&](Id a, Id b) {
        auto va = t.votes.find(a), vb = t.votes.find(b);
        return (va != t.votes.end() ? va->second : 0) > (vb != t.votes.end() ? vb->second : 0);
    });
    std::string out;
    for (Id c : order) {
        auto v = t.votes.find(c);
        out += (out.empty() ? "" : "、") + fullName(c) + (c == head ? "（你）" : c == t.mine ? "（你家）" : "") + (forecast ? " 約 " : " ") +
               std::to_string(v != t.votes.end() ? v->second : 0) + " 票";
    }
    return out;
}

std::string World::voteWhy(const Tally& t, Id winner) const {
    auto houses = [&](const std::vector<Id>& ids) {
        std::vector<std::string> names;
        for (Id v : ids)
            if (people_[v].dynasty != kNone) names.push_back(dynasties_[people_[v].dynasty].surname + "家");
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        std::string out;
        for (const auto& n : names) out += (out.empty() ? "" : "、") + n;
        return out;
    };
    // The old feud is told only when it cost the family's man the office.
    Id m = t.mine;
    auto votesOf = [&](Id c) { auto it = t.votes.find(c); return it == t.votes.end() ? 0 : it->second; };
    bool decided = winner != kNone && m != kNone && winner != m && t.votes.count(m) &&
                   votesOf(m) + static_cast<int>(t.grudge.size()) >= votesOf(winner);
    std::string kept = houses(t.byWord), grudge = decided ? houses(t.grudge) : "", out;
    if (!kept.empty()) out += kept + "答應過你，說到做到";
    if (!grudge.empty()) out += (out.empty() ? "" : "；") + grudge + "記著舊仇，票投給了別人";
    std::string broke = houses(t.broke);
    if (!broke.empty()) out += (out.empty() ? "" : "；") + broke + "答應過你，臨了卻投了別人";
    return out.empty() ? out : "（" + out + "。）";
}

void World::electionNotice() {
    Id head = playerHead();
    if (head == kNone) return;
    std::vector<std::string> ids;
    if (month() == 6 && reeveDue()) ids.push_back("reeve");
    if (month() == 9) ids.push_back("aletaster"), ids.push_back("hayward");
    auto mid = [](double m) { return m / 2; };
    std::vector<std::string> names, parts, could, barred;
    bool loud = false;
    for (const auto& id : ids) {
        const Office* of = office(id);
        if (!of) continue;
        Id old = id == "reeve" ? currentReeve() : of->holder;
        std::vector<Id> cands = officeCandidates(id, old, mid);
        if (cands.empty()) continue;
        Tally t = countVotes(id, cands, old, mid);
        names.push_back(of->name);
        std::string part = (ids.size() > 1 ? of->name + "：" : "") + tallyText(cands, t, true);
        bool wanted = std::find_if(wants_.begin(), wants_.end(), [&](const Want& wt) {
                          return wt.open && wt.house == playerHousehold_ && wt.kind == "office" && wt.office == id; }) != wants_.end();
        std::string bar = familyOfficeBar(id);
        if (t.mine != kNone) {
            int ours = t.votes.count(t.mine) ? t.votes.at(t.mine) : 0, top = 0;
            for (const auto& [c, n] : t.votes)
                if (c != t.mine) top = std::max(top, n);
            std::string who = t.mine == head ? "你" : shortName(t.mine);
            part += cands.size() == 1 ? "，沒有別人出來爭"
                    : ours > top ? "，" + who + "暫時領先，可是還沒投票，人心會變"
                    : ours == top ? "，" + who + "跟人同票；同票時現任的連任，不然由老爺點名"
                                  : "，" + who + "落後 " + std::to_string(top - ours) + " 票，去找各家家主「開口」拉票，答應了的人多半會投你家";
            if (!t.grudge.empty()) part += "；有 " + std::to_string(t.grudge.size()) + " 家記著和你家的舊仇，不會投你";
            loud = true;
        } else if (bar.empty()) {
            could.push_back(of->name);
        } else {
            barred.push_back(of->name + "（" + bar + "）");
            loud = loud || wanted;
        }
        parts.push_back(part);
    }
    if (parts.empty()) return;
    auto join = [](const std::vector<std::string>& v, const char* sep) {
        std::string o;
        for (const auto& x : v) o += (o.empty() ? "" : sep) + x;
        return o;
    };
    std::string when = ids.front() == "reeve" ? "八月初要選莊頭（一任三年）" : "米迦勒節要選" + join(names, "和") + "（一任一年）";
    std::string tail;
    if (!could.empty()) tail += "你夠資格爭" + join(could, "、") + "：在「村裡在爭什麼」按「想當」，再去拉票。";
    if (!barred.empty()) tail += "你家這回爭不了" + join(barred, "、") + "。";
    log(loud || !could.empty() ? 1 : 2, "【選舉在即】這一季" + when + "。照現在看，" + join(parts, "；") + "。" + tail);
}

void World::chooseOfficers() {
    for (const char* oid : {"aletaster", "hayward"}) {
        const std::string id = oid;
        const Office* cur = office(id);
        if (!cur) continue;
        Id old = cur->holder;
        auto noise = [&](double m) { return static_cast<double>(rngContest_.range(0, static_cast<int>(m))); };
        std::vector<Id> cands = officeCandidates(id, old, noise);
        if (cands.empty()) continue;
        Tally t = countVotes(id, cands, old, noise);
        Id winner = kNone;
        for (Id c : cands)
            if (winner == kNone || t.votes[c] > t.votes[winner]) winner = c;
        // Even votes: the one who has it keeps it, else the one the lord likes better.
        std::string tie;
        int tied = 0;
        for (Id c : cands) tied += t.votes[c] == t.votes[winner];
        if (tied > 1) {
            Id pick = kNone;
            for (Id c : cands)
                if (t.votes[c] == t.votes[winner] &&
                    (pick == kNone || c == old || (pick != old && lord_ != kNone && opinion(lord_, c) > opinion(lord_, pick))))
                    pick = c;
            winner = pick;
            tie = pick == old ? "幾個人同票，照老規矩由原來的接著當。" : lord_ != kNone ? "幾個人同票，由老爺從中點了一個。" : "幾個人同票，抽籤定了一個。";
        }
        for (auto it = promises_.begin(); it != promises_.end();) it = it->first.second == id ? promises_.erase(it) : std::next(it);
        bool familyIn = false;
        for (Id c : cands) familyIn = familyIn || isPlayerFamily(people_[c]) || inPlayerHouse(people_[c].household);
        for (const auto& wt : wants_)
            if (wt.open && wt.kind == "office" && wt.office == id && inPlayerHouse(wt.house)) familyIn = true;
        const std::string& name = cur->name;
        setOffice(id, winner, "選上的");
        Household& wh = households_[people_[winner].household];
        if (id == "aletaster") wh.money += 8, book(wh, "品酒官的罰款分成", 8);  // a share of the fines he presents
        else wh.food += consumption(wh) * 30;  // a sheaf from every house at harvest
        if (winner != old) people_[winner].reputation += 2;
        if (familyIn && cands.size() > 1) {
            log(1, "【選" + name + "】" + tallyText(cands, t, false) + "。" + tie + voteWhy(t, winner) + fullName(winner) + " 當選，做到明年米迦勒節。");
        } else if (familyIn && t.mine != kNone) {
            log(1, "【選" + name + "】沒有別人出來爭，" + fullName(winner) + " 當選，做到明年米迦勒節。");
        } else if (std::any_of(wants_.begin(), wants_.end(), [&](const Want& wt) {
                       return wt.open && wt.kind == "office" && wt.office == id && inPlayerHouse(wt.house); })) {
            std::string bar = familyOfficeBar(id);
            log(1, "【選" + name + "】你家這回沒能出來選" + (bar.empty() ? "" : "：" + bar) + "。" + fullName(winner) + " 當選。");
        } else if (old != kNone && winner != old && people_[old].alive() && inPlayerHouse(people_[old].household)) {
            // The family's man had it and could not stand again: said, not left to be noticed.
            std::string bar = officeBar(old, id);
            log(1, "【選" + name + "】" + fullName(old) + " 這回沒能再出來選" + (bar.empty() ? "" : "（" + bar + "）") + "，" + fullName(winner) + " 當選。");
        } else if (winner != old) {
            log(isPlayerFamily(people_[winner]) ? 1 : 2, fullName(winner) + " 被選為" + name + "。");
        }
        if (winner != old) addNews("office:won", winner, kNone, wh.id, kNone, name);
        settleWants("office", kNone, id, wh.id, "當上了");
    }
}

// ---- Places ------------------------------------------------------------------------------------

// Each craftsman in the village takes one apprentice at a time; the manor keeps three servants and a house
// with a big holding or a trade one.
static std::vector<Place> placesOf(const std::vector<Household>& hs, const std::vector<Person>& ps, Id lordHouse) {
    std::vector<Place> out;
    for (const auto& h : hs) {
        if (h.extinct || h.members.empty() || h.head == kNone) continue;
        const std::string& occ = ps[h.head].occupation;
        std::string trade = isTrade(occ) ? occ : isTrade(h.trade) ? h.trade : "";
        if (!trade.empty() && !h.tradeLet) out.push_back({"apprentice", h.id, trade, 1});
        if (h.id == lordHouse) out.push_back({"service", h.id, "", 3});
        else if (h.acres >= 25 || !trade.empty() || h.money >= 200) out.push_back({"service", h.id, "", 1});
    }
    return out;
}

int World::placeTaken(const Place& pl) const {
    int n = 0;
    std::string flag = "apprentice_at_" + std::to_string(pl.master);
    for (const auto& p : people_) {
        if (!p.alive()) continue;
        if (pl.kind == "apprentice" ? p.flags.count(flag) > 0 : (p.home != kNone && p.household == pl.master)) ++n;
    }
    return n;
}

bool World::placeOpen(Id master, const std::string& kind) const {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (const auto& pl : placesOf(households_, people_, lordHouse))
        if (pl.master == master && pl.kind == kind) return placeTaken(pl) < pl.slots;
    return false;
}

bool World::takePlace(Id master, const std::string& kind, Id who) {
    if (!placeOpen(master, kind) || who == kNone) return false;
    Person& p = people_[who];
    if (!p.alive() || p.household == kNone || p.home != kNone) return false;  // dead since they asked, or already away
    if (households_[p.household].head == who || !p.station.empty()) return false;  // keeps a house now, or on another road
    p.focus.clear();  // a new life: the days go where a servant's or an apprentice's go (no more letters)
    if (kind == "apprentice") {
        Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
        std::string trade;
        for (const auto& pl : placesOf(households_, people_, lordHouse))
            if (pl.master == master && pl.kind == kind) trade = pl.trade;
        p.flags.insert("apprentice_at_" + std::to_string(master));
        p.flags.insert("apprentice_" + trade);
        p.flags.insert("trade_decided");
        if (households_[master].head != kNone) setStation(who, "apprentice", 0, "拜" + fullName(households_[master].head) + "為師");
    } else {
        Id mh = households_[master].head;
        if (mh == kNone) return false;
        sendToService(who, mh, 5);
        if (p.home == kNone) return false;
    }
    if (p.household != kNone || p.home != kNone) settleWants("place", master, kind, p.home != kNone ? p.home : p.household, "進去了");
    return true;
}

// Michaelmas too: a master with a place free takes on one of the children whose parents asked, the one whose
// family he likes best (and who can pay the fee).
void World::fillPlaces() {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (const auto& pl : placesOf(households_, people_, lordHouse)) {
        if (placeTaken(pl) >= pl.slots) continue;
        Id mh = households_[pl.master].head;
        if (mh == kNone) continue;
        const Want* best = nullptr;
        double bestScore = -1e9;
        for (const auto& wt : wants_) {
            if (!wt.open || wt.kind != "place" || wt.object != pl.master || wt.office != pl.kind || wt.subject == kNone) continue;
            if (inPlayerHouse(wt.house)) continue;  // the family asks for itself (開口)
            if (!people_[wt.subject].alive() || people_[wt.subject].home != kNone) continue;
            const Household& h = households_[wt.house];
            if (h.head == kNone || (pl.kind == "apprentice" && h.money < 24)) continue;
            double s = opinion(mh, h.head) + rngContest_.range(0, 12) + people_[wt.subject].skills[Charm];
            if (s > bestScore) bestScore = s, best = &wt;
        }
        if (!best) continue;
        Id house = best->house, who = best->subject;
        if (bestScore < -20) continue;
        bool known = familyKnows(*best);
        std::string what = pl.kind == "apprentice" ? "當學徒" : "幫傭";
        if (!takePlace(pl.master, pl.kind, who)) continue;
        if (pl.kind == "apprentice") {  // the fee, once the place is taken
            households_[house].money -= 24;
            book(households_[house], "學徒拜師費", -24);
            households_[pl.master].money += 24;
            book(households_[pl.master], "收學徒的拜師費", 24);
        }
        if (known) log(2, fullName(mh) + " 收了 " + fullName(who) + " 到家裡" + what + "。");
    }
}

Id World::openApprenticeship(const std::string& trade, Id except) const {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (const auto& pl : placesOf(households_, people_, lordHouse))
        if (pl.kind == "apprentice" && pl.trade == trade && pl.master != except && placeTaken(pl) < pl.slots) return pl.master;
    return kNone;
}

Id World::openPlace(const std::string& kind, Id except) const {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id best = kNone;
    for (const auto& pl : placesOf(households_, people_, lordHouse)) {
        if (pl.kind != kind || pl.master == except || placeTaken(pl) >= pl.slots) continue;
        if (pl.master == lordHouse) return pl.master;
        if (best == kNone) best = pl.master;
    }
    return best;
}

bool World::villageCraft(Id except) const {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (const auto& pl : placesOf(households_, people_, lordHouse))
        if (pl.kind == "apprentice" && pl.master != except) return true;
    return false;
}

// ---- Wants -------------------------------------------------------------------------------------

std::string World::wantThing(const Want& wt) const {
    auto house = [&](Id h) { return h == kNone || households_[h].dynasty == kNone ? std::string() : dynasties_[households_[h].dynasty].surname + "家"; };
    if (wt.kind == "parcel") return parcelName(parcels_[wt.object]);
    if (wt.kind == "office") {
        const Office* o = office(wt.office);
        return o ? o->name + "的位子" : wt.office;
    }
    if (wt.kind == "match") return house(people_[wt.object].household) + "的" + shortName(wt.object);
    if (wt.kind == "place") return house(wt.object) + (wt.office == "apprentice" ? "的學徒位子" : "的幫傭位子");
    return "";
}

bool World::familyKnows(const Want& wt) const {
    if (inPlayerHouse(wt.house)) return true;
    return wt.news != kNone && news_[wt.news].heard.count(playerHousehold_) > 0;
}

Want* World::wantOf(Id house, const std::string& kind, Id object, const std::string& office) {
    for (auto& wt : wants_)
        if (wt.open && wt.house == house && wt.kind == kind && (kind == "office" ? wt.office == office : wt.object == object) &&
            (kind != "place" || wt.office == office))
            return &wt;
    return nullptr;
}

std::vector<Id> World::rivalsFor(const std::string& kind, Id object, const std::string& office, Id except) const {
    std::vector<Id> out;
    for (const auto& wt : wants_)
        if (wt.open && wt.house != except && wt.kind == kind && (kind == "office" ? wt.office == office : wt.object == object) &&
            (kind != "place" || wt.office == office) && std::find(out.begin(), out.end(), wt.house) == out.end())
            out.push_back(wt.house);
    return out;
}

Id World::addWant(Id house, const std::string& kind, Id object, Id subject, const std::string& office, const std::string& why) {
    if (house == kNone || households_[house].extinct) return kNone;
    if (kind == "parcel" && object != kNone && parcels_[object].holder == house) return kNone;  // theirs already
    if (Want* old = wantOf(house, kind, object, office)) return old->id;
    Want wt;
    wt.id = static_cast<Id>(wants_.size());
    wt.house = house;
    wt.kind = kind;
    wt.object = object;
    wt.subject = subject;
    wt.office = office;
    wt.why = why;
    wt.since = day_;
    std::vector<Id> rivals = rivalsFor(kind, object, office, house);
    std::string thing = wantThing(wt);
    Id head = households_[house].head;
    // What they are after goes round the village like any news (the family's own included: others hear of it).
    Id about = kind == "place" || kind == "match" ? subject : kind == "office" && subject != kNone ? subject : head;
    std::string said = thing;
    if (kind == "office") if (const Office* o = this->office(office)) said = o->name;  // "想當看籬人", not "想當看籬人的位子"
    if (kind == "place") said = (households_[object].dynasty != kNone ? dynasties_[households_[object].dynasty].surname : "") + (office == "apprentice" ? "家當學徒" : "家幫傭");
    wt.news = addNews("want:" + kind, about, kind == "match" ? object : kNone, house, kNone, said);
    wants_.push_back(wt);
    ++counters_["want:" + kind];
    if (!rivals.empty()) ++counters_["contest:" + kind];
    // Two houses after the same thing: a rivalry the family hears of when it is one of them.
    for (Id r : rivals) {
        const Want* rw = nullptr;
        for (const auto& x : wants_)
            if (x.open && x.house == r && x.kind == kind && (kind == "office" ? x.office == office : x.object == object)) rw = &x;
        if (inPlayerHouse(house) && rw && familyKnows(*rw))
            log(1, "  （" + dynasties_[households_[r].dynasty].surname + " 家也在打" + thing + "的主意。）");
    }
    return wt.id;
}

void World::dropWant(Want& wt, const std::string& outcome) {
    if (!wt.open) return;
    wt.open = false;
    wt.outcome = outcome;
    wt.ended = day_;
}

// The thing went to winnerHouse (kNone: back to the lord). Its wanters are done with it: the winner glad, the
// rest left with a grudge against whoever got it.
void World::settleWants(const std::string& kind, Id object, const std::string& office, Id winnerHouse, const std::string& how) {
    std::vector<Want*> hit;
    for (auto& wt : wants_)
        if (wt.open && wt.kind == kind && (kind == "office" ? wt.office == office : wt.object == object) &&
            (kind != "place" || wt.office == office))
            hit.push_back(&wt);
    if (hit.empty()) return;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    std::string thing = wantThing(*hit.front());
    auto surname = [&](Id h) { return h != kNone && households_[h].dynasty != kNone ? dynasties_[households_[h].dynasty].surname : std::string(); };
    Id winnerHead = winnerHouse != kNone ? households_[winnerHouse].head : kNone;
    bool contested = hit.size() > 1 || (hit.size() == 1 && hit.front()->house != winnerHouse);
    for (Want* wt : hit) {
        if (wt->house == winnerHouse) {
            dropWant(*wt, how);
            if (inPlayerHouse(wt->house) && hit.size() > 1) log(1, "  （" + thing + "，你家搶在別人前頭拿到了。）");
            continue;
        }
        std::string lost = winnerHouse == kNone ? "回到領主手上" : "讓" + surname(winnerHouse) + "家" + (kind == "match" ? "訂走了" : "搶走了");
        // A post comes round again: the family still wants it until it lets it go (村裡在爭什麼 → 放下).
        bool again = kind == "office" && inPlayerHouse(wt->house);
        if (!again) dropWant(*wt, lost);
        Id loser = households_[wt->house].head;
        if (loser != kNone && winnerHead != kNone && winnerHouse != lordHouse) {
            addOpinion(loser, winnerHead, "took_our_want", -10, 8);
            addConcern(loser, "grievance", winnerHead, 12);
        }
        if (again) log(1, "  （" + thing + "這回" + lost + "。你家還想當的話，下回再選時接著爭；不想了，可以在「村裡在爭什麼」放下。）");
        else if (inPlayerHouse(wt->house)) log(1, "  （你家看中的" + thing + "，" + lost + "。）");
    }
    // An office already has its own "office:won" news.
    if (contested && kind != "office" && winnerHouse != kNone && winnerHouse != lordHouse)
        addNews("contest:won", winnerHead, kNone, winnerHouse, kNone, thing);
}

Id World::bestParcelWant(Id house) const {
    for (const auto& wt : wants_)
        if (wt.open && wt.house == house && wt.kind == "parcel") return wt.object;
    return kNone;
}

// Once a year, at the turn of it: each head lets go of what no longer makes sense and may set their heart on
// something new, out of how things stand with them: sons and too little land, a son of an age to learn a trade,
// a daughter or son to marry, a post to be had.
void World::yearlyWants() {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    // Stale wants first.
    for (auto& wt : wants_) {
        if (!wt.open) continue;
        const Household& h = households_[wt.house];
        if (h.extinct || h.head == kNone) {
            dropWant(wt, "家沒了");
            continue;
        }
        // A villager's want goes stale after six years; the family's own is the family's to give up (村裡在爭什麼), however
        // old (one carried over from before the hand-over was dropped on the first day, design/fun-r6.md D).
        bool stale = !inPlayerHouse(wt.house) && day_ - wt.since > 6 * kDaysPerYear;
        if (wt.kind == "parcel") {
            const Parcel& pc = parcels_[wt.object];
            if (pc.gone || pc.holder == wt.house) stale = true;
        } else if (wt.kind == "match") {
            const Person &o = people_[wt.object], &s = people_[wt.subject];
            auto married = [&](const Person& x) { return x.spouse != kNone && people_[x.spouse].alive(); };
            if (!o.alive() || !s.alive() || married(o) || married(s) || o.household == kNone) stale = true;
        } else if (wt.kind == "place") {
            const Person& s = people_[wt.subject];
            if (!s.alive() || s.flags.count("trade_decided") || ageYears(s) > (wt.office == "apprentice" ? 15 : 17) || s.home != kNone ||
                !s.station.empty() || s.household == kNone || households_[s.household].head == s.id)
                stale = true;  // placed, grown, gone, on another road, or keeping a house now
        } else if (wt.kind == "office") {
            const Office* o = office(wt.office);
            if (o && o->holder != kNone && people_[o->holder].household == wt.house) stale = true;
        }
        if (stale) dropWant(wt, inPlayerHouse(wt.house) ? "放下了" : "放下了");
    }
    // Apprentices are journeymen at twenty-one: the place is free again.
    for (auto& p : people_)
        if (p.alive() && ageYears(p) >= 21)
            for (auto it = p.flags.begin(); it != p.flags.end();) it = it->rfind("apprentice_at_", 0) == 0 ? p.flags.erase(it) : std::next(it);
    // Favours fade: the long-forgotten are dropped.
    favours_.erase(std::remove_if(favours_.begin(), favours_.end(), [&](const Favour& f) {
                       return !people_[f.debtor].alive() || !people_[f.creditor].alive() ||
                              f.weight * std::pow(0.5, (day_ - f.day) / double(kDaysPerYear) / 8) < 1;
                   }), favours_.end());

    for (auto& h : households_) {
        if (h.extinct || h.members.empty() || h.head == kNone || inPlayerHouse(h.id) || h.id == lordHouse) continue;
        Person& hp = people_[h.head];
        if (hp.occupation == "priest" || ageYears(hp) < 18) continue;
        int open = 0;
        for (const auto& wt : wants_) open += wt.open && wt.house == h.id;
        int cap = 2 + (hasTrait(hp, "ambitious") ? 1 : 0) - (hasTrait(hp, "content") ? 1 : 0);
        if (open >= cap) continue;
        struct Cand {
            double score;
            std::string kind;
            Id object, subject;
            std::string office, why;
        };
        std::vector<Cand> cands;
        double hands = 0;
        int sons = 0;
        for (Id m : h.members) {
            const Person& p = people_[m];
            double a = ageYears(p);
            if (p.sex == Sex::Male && a >= 14 && p.health >= 40) hands += 1;
            else if (p.sex == Sex::Male && a >= 9) hands += 0.5;
            if (p.sex == Sex::Male && a >= 6 && m != h.head && p.father == h.head) ++sons;
        }
        bool landAmbition = hp.ambition == "more_land" || hp.ambition == "big_landholder" || hp.ambition == "become_franklin" || hp.ambition == "first_house";
        // Land: sons and too little of it, or a mind to have more.
        double need = hands * 10 - h.acres;
        double hunger = need / 5 + (landAmbition ? 3 : 0) + (hasTrait(hp, "ambitious") ? 1 : 0) + (hasTrait(hp, "greedy") ? 1 : 0);
        if (hunger >= 1.5 && h.money >= 30 && h.debt < 1 && !wantOf(h.id, "parcel", bestParcelWant(h.id), "")) {
            std::set<int> mine;
            for (const auto& pc : parcels_)
                if (!pc.gone && pc.holder == h.id) mine.insert(pc.cells.begin(), pc.cells.end());
            Id pick = kNone;
            double best = 0;
            std::string reason;
            for (const auto& pc : parcels_) {
                if (pc.gone || pc.holder == h.id || pc.acres > 12 || (lordHouse != kNone && pc.holder == lordHouse)) continue;
                double s = 0;
                std::string why;
                if (pc.holder == kNone) {
                    s += 25;
                    why = "那塊地空著沒人租";
                } else {
                    const Household& oh = households_[pc.holder];
                    if (oh.head == kNone) continue;
                    const Person& op = people_[oh.head];
                    if (closeKin(hp.id, op.id)) s -= 25;
                    double o = opinion(hp.id, op.id);
                    if (o > 40) s -= 10;
                    if (o < -20) s += 6;
                    if (oh.debt > 0.5) s += 15, why = "他家欠著債";
                    else if (oh.lean) s += 8, why = "他家日子緊";
                    double ohHands = 0;
                    bool heir = false;
                    for (Id m : oh.members) {
                        const Person& q = people_[m];
                        if (q.sex == Sex::Male && ageYears(q) >= 14) ohHands += 1;
                        if (m != oh.head && q.sex == Sex::Male) heir = true;
                    }
                    if (!heir && ageYears(op) > 55) s += 12, why = "他家沒有兒子接手";
                    if (oh.acres > ohHands * 15 + 5) s += 8, why = why.empty() ? "他家地多人少" : why;
                    // A house set on being the biggest in the village is the one the land-hungry press (「村裡第一大戶」).
                    if (landAmbition && op.ambition == "first_house") s += 10, why = why.empty() ? "不能讓他家一家獨大" : why;
                    if (hasTrait(hp, "kind")) s -= 8;
                    if (why.empty()) continue;
                }
                bool next = false;
                for (int c : pc.cells)
                    for (int d : {1, -1, map_.width, -map_.width})
                        if (mine.count(c + d)) next = true;
                if (next) s += 10;
                s -= std::abs(pc.acres - std::clamp(need, 2.0, 8.0));
                s += rngContest_.range(0, 6);
                if (s > best) best = s, pick = pc.id, reason = why;
            }
            if (pick != kNone && best >= 8) {
                std::string why = landAmbition ? "想多種幾畝地" : sons >= 2 ? "兒子多，地不夠分" : "家裡人手多，地不夠種";
                cands.push_back({best + hunger * 3, "parcel", pick, kNone, "", why + "；" + reason});
            }
        }
        // A post.
        auto eligibleMan = [&](const Person& p, double lo, double hi) {
            return p.alive() && p.sex == Sex::Male && ageYears(p) >= lo && ageYears(p) <= hi && !p.flags.count("reeve");
        };
        if (eligibleMan(hp, 25, 65) && hp.occupation == "peasant" && year() + 1 >= nextReeveYear_ && std::floor(owedAll(h.id) + 1e-9) < 20) {
            double s = (hp.ambition == "become_reeve" ? 30 : 0) + (hasTrait(hp, "ambitious") && hp.reputation >= 5 ? 15 : 0);
            if (s > 0) cands.push_back({s + rngContest_.range(0, 6), "office", kNone, h.head, "reeve", "想當莊頭，在村裡說得上話"});
        }
        if ((h.acres < 10 || h.money < 25) && rngContest_.chance(0.3)) {
            Id man = kNone;
            for (Id m : h.members)
                if (eligibleMan(people_[m], 20, 60) && people_[m].home == kNone && (man == kNone || m == h.head)) man = m;
            if (man != kNone) cands.push_back({14.0 + rngContest_.range(0, 6), "office", kNone, man, "hayward", "家裡窮，看籬人每家給一束麥"});
        }
        if (eligibleMan(hp, 25, 65) && hp.reputation >= 8 && (hasTrait(hp, "gregarious") || hasTrait(hp, "ambitious")) && rngContest_.chance(0.25)) {
            bool alewife = false;
            for (Id m : h.members) alewife = alewife || people_[m].occupation == "alewife";
            if (!alewife) cands.push_back({12.0 + rngContest_.range(0, 6), "office", kNone, h.head, "aletaster", "想在村裡有點面子"});
        }
        // A match for a grown son or daughter: the best house that might have them.
        for (Id c : hp.children) {
            const Person& ch = people_[c];
            if (!ch.alive() || ch.household == kNone || (ch.spouse != kNone && people_[ch.spouse].alive())) continue;
            double a = ageYears(ch);
            bool son = ch.sex == Sex::Male;
            if (son ? (a < 18 || a > 30) : (a < 16 || a > 26)) continue;
            bool already = false;
            for (const auto& wt : wants_) already = already || (wt.open && wt.kind == "match" && wt.subject == c);
            if (already || !rngContest_.chance(0.5)) continue;
            double ownStanding = h.acres + h.money / 30;
            Id pick = kNone;
            double best = -1e9;
            for (const auto& q : people_) {
                if (!q.alive() || q.household == kNone || q.household == ch.household || q.sex == ch.sex) continue;
                if ((q.spouse != kNone && people_[q.spouse].alive()) || closeKin(q.id, c) || gentry(q.id) != gentry(c)) continue;
                if (q.occupation == "priest" || q.home != kNone) continue;
                double qa = ageYears(q);
                double gap = son ? a - qa : qa - a;  // the man older
                if ((son ? qa < 16 : qa < 18) || gap < -3 || gap > 12) continue;
                const Household& qh = households_[q.household];
                if (qh.head == kNone || qh.id == lordHouse) continue;
                double standing = qh.acres + qh.money / 30;
                double s = std::min(standing, ownStanding * 1.6) / 2 + opinion(hp.id, qh.head) / 4 + q.skills[Charm] / 4 + rngContest_.range(0, 8);
                if (standing < ownStanding * 0.6) s -= 10;
                if (s > best) best = s, pick = q.id;
            }
            if (pick != kNone)
                cands.push_back({10 + best / 2, "match", pick, c, "", son ? "替兒子物色媳婦" : "替女兒找婆家"});
            break;
        }
        // A trade for a younger son, or service for a child the house can hardly feed.
        Id heir = kNone;
        for (Id c : hp.children)
            if (people_[c].alive() && people_[c].sex == Sex::Male && (heir == kNone || people_[c].birthDay < people_[heir].birthDay)) heir = c;
        for (Id c : hp.children) {
            const Person& ch = people_[c];
            if (!ch.alive() || ch.household != h.id || ch.home != kNone || ch.flags.count("trade_decided")) continue;
            double a = ageYears(ch);
            bool apprentice = ch.sex == Sex::Male && c != heir && a >= 10 && a <= 13 && (h.acres < 15 || sons >= 3);
            bool service = a >= 10 && a <= 15 && (h.lean || h.food < consumption(h) * 120);
            if (!apprentice && !service) continue;
            bool already = false;
            for (const auto& wt : wants_) already = already || (wt.open && wt.subject == c);
            if (already) continue;
            Id master = kNone;
            double best = -1e9;
            for (const auto& pl : placesOf(households_, people_, lordHouse)) {
                if (pl.master == h.id || pl.kind != (apprentice ? "apprentice" : "service")) continue;
                double s = opinion(hp.id, households_[pl.master].head) + rngContest_.range(0, 10) + (placeTaken(pl) < pl.slots ? 10 : 0);
                if (s > best) best = s, master = pl.master;
            }
            if (master != kNone)
                cands.push_back({apprentice ? 16.0 : 12.0, "place", master, c, apprentice ? "apprentice" : "service",
                                 apprentice ? "家裡地少，想讓小兒子學門手藝" : "家裡養不起，想送孩子去幫傭"});
            break;
        }
        if (cands.empty()) continue;
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
        int take = open == 0 ? 2 : 1;
        for (const auto& c : cands) {
            if (take <= 0 || open >= cap) break;
            if (!rngContest_.chance(std::clamp(c.score / 40, 0.2, 0.9))) continue;
            addWant(h.id, c.kind, c.object, c.subject, c.office, c.why);
            --take;
            ++open;
        }
    }
}

// After harvest the steward lets the vacant parcels. Whoever has let it be known they want one puts in for it;
// he lets it to the one the lord likes best of those who can pay the fine for it.
void World::allotVacant() {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    double rate = worldFlags_.count("post_plague") ? 1.5 : 3;
    for (auto& pc : parcels_) {
        if (pc.gone || pc.holder != kNone) continue;
        Id pick = kNone;
        double best = -1e9;
        for (const auto& wt : wants_) {
            if (!wt.open || wt.kind != "parcel" || wt.object != pc.id) continue;
            const Household& h = households_[wt.house];
            if (h.extinct || h.head == kNone || h.money < pc.acres * rate || h.id == lordHouse) continue;
            double s = (lord_ != kNone ? opinion(lord_, h.head) : 0) + h.money / 15 + rngContest_.range(0, 10);
            if (s > best) best = s, pick = h.id;
        }
        if (pick == kNone) continue;
        double fine = pc.acres * rate;
        Id id = pc.id;
        bool family = inPlayerHouse(pick);
        std::string name = parcelName(pc);
        moveParcel(id, pick, "向領主租來");
        households_[pick].money -= fine;
        book(households_[pick], "入地費", -fine);
        if (lordHouse != kNone) households_[lordHouse].money += fine;
        if (family) log(1, "  管家把" + name + "租給了你家，入地費 " + pence(fine) + "。");
    }
}

// ---- Favours -----------------------------------------------------------------------------------

void World::oweFavour(Id debtor, Id creditor, double weight, const std::string& why) {
    if (debtor == kNone || creditor == kNone || debtor == creditor) return;
    for (auto& f : favours_)
        if (f.debtor == debtor && f.creditor == creditor) {
            f.weight = f.weight * std::pow(0.5, (day_ - f.day) / double(kDaysPerYear) / 8) + weight;
            f.day = day_;
            f.why = why;
            return;
        }
    favours_.push_back({debtor, creditor, weight, day_, why});
}

double World::favourOwed(Id debtor, Id creditor) const {
    double s = 0;
    for (const auto& f : favours_)
        if (f.debtor == debtor && f.creditor == creditor) s += f.weight * std::pow(0.5, (day_ - f.day) / double(kDaysPerYear) / 8);
    return s;
}

void World::callInFavour(Id debtor, Id creditor) {
    favours_.erase(std::remove_if(favours_.begin(), favours_.end(), [&](const Favour& f) { return f.debtor == debtor && f.creditor == creditor; }),
                   favours_.end());
}

// ---- The entry fine ----------------------------------------------------------------------------

// A new holder of customary land pays the lord a fine to be admitted to it, set by the lord: dearer when land
// is wanted, and dearer for an heir he does not like. Half a year to find it.
// A widow holds by free bench and pays nothing; a son or daughter of the dead pays the customary fine; anyone
// else (a brother, a nephew, a second husband) pays what the lord thinks the land will bear.
void World::chargeEntryFine(Household& h, Id heir, Id dead, double customary) {
    if (lord_ == kNone || people_[lord_].household == kNone || h.id == people_[lord_].household || heir == kNone) return;
    if (customary < 1) return;
    const Person& hp = people_[heir];
    if (dead != kNone && people_[dead].spouse == heir) return;
    bool child = dead != kNone && (hp.father == dead || hp.mother == dead);
    double rate = param("entry_fine_per_acre", worldFlags_.count("post_plague") ? 1.5 : 4) * (child ? 0.5 : 1);
    double factor = std::clamp(1 - opinion(lord_, heir) / 100, 0.6, 1.4);
    double amount = std::round(customary * rate * factor);
    if (amount < 2) return;
    fines_.push_back({h.id, heir, amount, day_ + 6 * kDaysPerMonth, false});
    if (inPlayerHouse(h.id))
        log(1, "  領主的管家說，" + fullName(heir) + " 要接下這 " + std::to_string(static_cast<int>(std::round(customary))) + " 畝地，得先交 " +
                   pence(amount) + "入地費，限半年內付清。");
}

void World::monthlyFines() {
    for (size_t i = 0; i < fines_.size();) {
        EntryFine& f = fines_[i];
        Household& h = households_[f.house];
        if (h.extinct || h.head == kNone) {
            fines_.erase(fines_.begin() + static_cast<long>(i));
            continue;
        }
        // Paid as soon as the money is there (a little kept back for bread); the family pays when it comes due.
        bool early = !inPlayerHouse(f.house) && h.money >= f.amount + 20;
        if (early || day_ >= f.due) {
            EntryFine copy = f;
            fines_.erase(fines_.begin() + static_cast<long>(i));
            resolveFine(copy);
            continue;
        }
        ++i;
    }
}

void World::resolveFine(EntryFine& f) {
    Household& h = households_[f.house];
    Id lordHouse = people_[lord_].household;
    bool family = inPlayerHouse(f.house);
    if (h.money >= f.amount) {
        h.money -= f.amount;
        households_[lordHouse].money += f.amount;
        book(h, "入地費", -f.amount);
        if (family) log(1, "交了 " + pence(f.amount) + "入地費給領主，地正式是" + shortName(f.heir != kNone && people_[f.heir].alive() ? f.heir : h.head) + "的了。");
        return;
    }
    if (family) {
        // The family decides: beg, borrow or give up some of the land (data/events/manor.json).
        fines_.push_back(f);
        fines_.back().due = day_ + kDaysPerMonth;  // until the choice is made
        log(1, "入地費 " + pence(f.amount) + "的期限到了，家裡只有 " + pence(h.money) + "。");
        runEventById("entry_fine_due", h.head, lord_);
        return;
    }
    // A neighbour may lend the rest; or the lord may give more time; or the land he cannot pay for goes.
    double short_ = f.amount - h.money;
    double owed = 0;
    Id lender = findLender(h) != kNone && opinion(findLender(h), h.head) >= 25 ? seekLender(h, short_, "交入地費", owed) : kNone;
    if (lender != kNone) {
        households_[people_[lender].household].money -= short_;
        book(households_[people_[lender].household], owed > 0 ? "借出" : "接濟別人", -short_);
        h.money += short_;
        book(h, "借錢", short_);
        if (owed > 0) addDebt(h, owed, lender);
        oweFavour(h.head, lender, 15, "借錢交入地費");
        h.money -= f.amount;
        book(h, "入地費", -f.amount);
        households_[lordHouse].money += f.amount;
        return;
    }
    if (!f.extended && rngContest_.chance(std::clamp(0.3 + opinion(lord_, h.head) / 100, 0.05, 0.8))) {
        f.extended = true;
        f.due = day_ + 6 * kDaysPerMonth;
        fines_.push_back(f);
        return;
    }
    forfeitForFine(h, std::clamp(short_ / f.amount, 0.0, 1.0));
    double paid = h.money;
    h.money = 0;
    book(h, "入地費", -paid);
    households_[lordHouse].money += paid;
}

EntryFine* World::fineOf(Id house) {
    for (auto& f : fines_)
        if (f.house == house) return &f;
    return nullptr;
}

Id World::fineLender(const Household& h, double amount) const {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id best = kNone;
    double bestOp = 20;
    for (const auto& o : households_) {
        if (o.extinct || o.members.empty() || o.id == h.id || o.id == lordHouse || o.head == kNone || o.money < amount + 40 || o.debt > 0) continue;
        if (people_[o.head].occupation == "priest" || h.head == kNone || !debtRoom(h, o.head)) continue;
        double op = opinion(o.head, h.head) + favourOwed(o.head, h.head) * 2;
        if (op > bestOp) bestOp = op, best = o.head;
    }
    return best;
}

void World::fineChoice(Household& h, const std::string& how) {
    EntryFine* fp = fineOf(h.id);
    if (!fp) return;
    EntryFine f = *fp;
    fines_.erase(fines_.begin() + (fp - fines_.data()));
    Id lordHouse = people_[lord_].household;
    double short_ = std::max(0.0, f.amount - h.money);
    if (how == "beg") {
        bool yes = !f.extended && rngContest_.chance(std::clamp(0.3 + opinion(lord_, h.head) / 100 + favourOwed(lord_, h.head) / 50, 0.05, 0.85));
        if (yes) {
            f.extended = true;
            f.due = day_ + 6 * kDaysPerMonth;
            fines_.push_back(f);
            log(1, "  領主答應再寬限半年。");
            return;
        }
        log(1, "  領主不肯再等。");
        addOpinion(h.head, lord_, "no_mercy", -6, 6);
    } else if (how == "borrow") {
        Id lender = fineLender(h, short_);
        if (lender != kNone) {
            households_[people_[lender].household].money -= short_;
            book(households_[people_[lender].household], "借出", -short_);
            h.money += short_;
            book(h, "借錢", short_);
            addDebt(h, short_, lender);
            oweFavour(h.head, lender, 15, "借錢交入地費");
            h.money -= f.amount;
            households_[lordHouse].money += f.amount;
            book(h, "入地費", -f.amount);
            log(1, "  " + fullName(lender) + " 借了 " + pence(short_) + "給你家補上入地費。這份人情，記下了。");
            return;
        }
    }
    forfeitForFine(h, std::clamp(short_ / f.amount, 0.0, 1.0));
    double paid = std::min(h.money, f.amount);
    h.money -= paid;
    households_[lordHouse].money += paid;
    book(h, "入地費", -paid);
}

// The lord takes back the share of the holding not paid for (at least a parcel, at most half: he wants a tenant
// who can pay, not an empty house) and lets it at once to whoever was after it and can pay, else it lies vacant.
// What is left owing is let go with the land.
void World::forfeitForFine(Household& h, double share) {
    double customary = h.acres - h.freehold;
    // One parcel, the one the steward has a taker for if he can: the family is shamed and poorer, not ruined.
    double lose = std::min(customary * share, 1.0);
    std::vector<Id> mine;
    for (const auto& pc : parcels_)
        if (!pc.gone && pc.holder == h.id && !pc.free) mine.push_back(pc.id);
    // The parcels someone else is after go first: that is who the steward has in mind.
    std::stable_sort(mine.begin(), mine.end(), [&](Id a, Id b) {
        bool wa = !rivalsFor("parcel", a, "", h.id).empty(), wb = !rivalsFor("parcel", b, "", h.id).empty();
        if (wa != wb) return wa;
        return parcels_[a].acres < parcels_[b].acres;
    });
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    double rate = worldFlags_.count("post_plague") ? 1.5 : 3;
    double gone = 0;
    std::string names;
    std::vector<std::string> to;
    for (Id id : mine) {
        if (gone >= lose - 0.5 || (gone > 0 && gone + parcels_[id].acres > lose + 2.5)) break;
        Id best = kNone;
        double bestScore = -1e9;
        for (Id r : rivalsFor("parcel", id, "", h.id)) {
            const Household& rh = households_[r];
            if (rh.extinct || rh.head == kNone || r == lordHouse || rh.money < parcels_[id].acres * rate) continue;
            double s = (lord_ != kNone ? opinion(lord_, rh.head) : 0) + rh.money / 15;
            if (s > bestScore) bestScore = s, best = r;
        }
        gone += parcels_[id].acres;
        std::string nm = parcelName(parcels_[id]);
        names += (names.empty() ? "" : "、") + nm;
        if (best != kNone) {
            double fine = parcels_[id].acres * rate;
            households_[best].money -= fine;
            book(households_[best], "入地費", -fine);
            if (lordHouse != kNone) households_[lordHouse].money += fine;
            moveParcel(id, best, "交不出入地費被收回，轉租來的");
            to.push_back(dynasties_[households_[best].dynasty].surname);
            if (h.head != kNone && households_[best].head != kNone) {
                addOpinion(h.head, households_[best].head, "took_our_land", -15, 15);
                addConcern(h.head, "grievance", households_[best].head, 20);
            }
        } else {
            moveParcel(id, kNone, "交不出入地費被收回");
        }
    }
    if (names.empty()) return;
    std::string whom;
    std::sort(to.begin(), to.end());
    to.erase(std::unique(to.begin(), to.end()), to.end());
    for (const auto& t : to) whom += (whom.empty() ? "" : "、") + t + " 家";
    addNews("fine:lost", h.head, kNone, h.id, kNone, names);
    if (inPlayerHouse(h.id))
        log(1, "【入地費】交不出入地費，領主收回了" + names + (whom.empty() ? "。" : "，轉手租給了" + whom + "。"));
    else if (h.head != kNone)
        log(2, fullName(h.head) + " 家交不出入地費，" + names + "被領主收回" + (whom.empty() ? "。" : "，租給了" + whom + "。"));
}

}  // namespace sim
