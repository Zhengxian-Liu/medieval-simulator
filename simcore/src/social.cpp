// Friendships, quarrels and the yearly reeve election.
#include <algorithm>
#include <map>

#include "simcore/world.h"

namespace sim {

void World::monthlySocial() {
    std::vector<Id> all = livingInVillage();
    if (all.size() < 2) return;
    for (Id a : all) {
        Person& pa = people_[a];
        double age = ageYears(pa);
        if (age < 6) continue;
        int meetings = 2 + (traitMod(pa, "sociable") > 0 ? 2 : 0);
        for (int k = 0; k < meetings; ++k) {
            // Mostly the people they already know (design/news.md), the better known the oftener; now and then
            // someone new of about their age.
            Id b = rngSocial_.chance(0.65) ? pickAcquaintance(a, rngSocial_) : kNone;
            bool stranger = b == kNone;
            if (stranger) b = all[rngSocial_.range(0, static_cast<int>(all.size()) - 1)];
            if (b == a || people_[b].household == pa.household) continue;
            Person& pb = people_[b];
            double ageB = ageYears(pb);
            if (ageB < 6 || (stranger && std::abs(ageB - age) > 15)) continue;
            befriend(a, b, 4);
            double before = opinion(a, b);
            double friendly = 0.7 + before / 300 + traitMod(pa, "sociable") * 0.1 - traitMod(pa, "quarrel") * 0.15;
            if (rngSocial_.chance(std::clamp(friendly, 0.1, 0.95))) {
                addOpinion(a, b, "good_times", 3, 6);
                addOpinion(b, a, "good_times", 3, 6);
                pa.needs[Social] = std::min(100.0, pa.needs[Social] + 12);
                pb.needs[Social] = std::min(100.0, pb.needs[Social] + 12);
            } else {
                addOpinion(a, b, "quarrels", -4, 4);
                addOpinion(b, a, "quarrels", -4, 4);
            }
            double after = opinion(a, b);
            bool family = pa.household == playerHousehold_ && ageYears(pa) >= 12;
            if (!family) continue;
            std::string friendFlag = "friend_" + std::to_string(b);
            std::string rivalFlag = "rival_" + std::to_string(b);
            if (after >= 50 && before < 50 && !pa.flags.count(friendFlag)) {
                pa.flags.insert(friendFlag);
                log(1, fullName(a) + " 和 " + fullName(b) + " 成了好朋友。");
            } else if (after <= -45 && before > -45 && !pa.flags.count(rivalFlag)) {
                pa.flags.insert(rivalFlag);
                log(1, fullName(a) + " 和 " + fullName(b) + " 結下了梁子。");
            }
        }
    }
}

// The reeve is chosen at Lammas by the heads of household from the likeliest few; each votes for
// whom they like best. He serves three years unless he dies, stops heading his house or falls into
// arrears. The lord can overrule the vote, but a man he forces on the village is resented for it.
void World::electReeve() {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    auto eligible = [&](const Person& p) {
        if (!p.alive() || p.household == kNone || p.household == lordHouse) return false;
        if (p.sex != Sex::Male || ageYears(p) < 25 || ageYears(p) > 65 || p.occupation != "peasant") return false;
        const Household& h = households_[p.household];
        auto bound = cooldowns_.find({p.id, "lords_debtor"});
        if (bound != cooldowns_.end() && bound->second > day_) return false;
        return h.head == p.id && h.debt < 20;
    };
    Id old = kNone;
    for (auto& p : people_)
        if (p.flags.count("reeve")) old = p.id;
    bool vacant = old == kNone || !eligible(people_[old]);
    if (!vacant && year() < nextReeveYear_) return;
    if (old != kNone) people_[old].flags.erase("reeve");
    nextReeveYear_ = year() + 3;

    auto merit = [&](const Person& p) {
        return p.skills[Charm] + p.reputation / 4 + p.skills[Farming] / 3 + traitMod(p, "charm") * 3;
    };
    std::vector<std::pair<double, Id>> ranked;
    for (auto& p : people_)
        if (eligible(p)) ranked.push_back({merit(p) + rngSocial_.range(0, 4) + (p.id == old ? 3 : 0), p.id});
    if (ranked.empty()) return;
    std::sort(ranked.rbegin(), ranked.rend());
    std::vector<Id> cands;
    for (size_t i = 0; i < ranked.size() && cands.size() < 3; ++i) cands.push_back(ranked[i].second);
    // Those who let it be known they want it stand too (contest.cpp).
    for (const auto& wt : wants_) {
        if (!wt.open || wt.kind != "office" || wt.office != "reeve" || cands.size() >= 4) continue;
        Id c = wt.subject != kNone ? wt.subject : households_[wt.house].head;
        if (c != kNone && eligible(people_[c]) && std::find(cands.begin(), cands.end(), c) == cands.end() && c != playerHead()) cands.push_back(c);
    }
    Id head = playerHead();
    bool playerRuns = head != kNone && eligible(people_[head]) && wantOf(playerHousehold_, "office", kNone, "reeve");
    for (size_t i = 0; i < ranked.size() && i < 5; ++i)
        if (ranked[i].second == head) playerRuns = true;
    if (playerRuns && std::find(cands.begin(), cands.end(), head) == cands.end()) cands.push_back(head);
    if (playerRuns) {
        people_[head].flags.erase("campaign_ale");
        people_[head].flags.erase("campaign_lord");
        runEventById("reeve_campaign", head, lord_);
    }

    std::map<Id, int> votes;
    std::vector<std::string> grudgeHouses;  // houses whose feud with the player decided their vote
    for (const auto& h : households_) {
        if (h.extinct || h.members.empty() || h.head == kNone || h.id == lordHouse) continue;
        const Person& voter = people_[h.head];
        if (voter.occupation == "priest" || ageYears(voter) < 21) continue;
        Id pick = kNone;
        double best = -1e9;
        for (Id c : cands) {
            double s = c == voter.id ? 1e6 : opinion(voter.id, c) + merit(people_[c]) * 2 + rngSocial_.range(0, 5);
            if (people_[c].flags.count("campaign_ale")) s += 10;
            auto pr = promises_.find({voter.id, "reeve"});
            if (pr != promises_.end() && pr->second == c) s += 40;  // gave his word (開口)
            if (c != voter.id && people_[c].household != kNone && wantOf(people_[c].household, "office", kNone, "reeve") && opinion(voter.id, c) >= 20) s += 6;
            // After two terms the village tires of the same man collecting its rents.
            auto t = reeveTerms_.find(c);
            if (c == old && t != reeveTerms_.end() && t->second >= 2) s -= 6 * (t->second - 1);
            if (feudStage(voter.id, c) >= 2) s -= 40;
            if (s > best) {
                best = s;
                pick = c;
            }
        }
        if (pick == kNone) continue;
        ++votes[pick];
        if (playerRuns && pick != head && feudStage(voter.id, head) >= 2 && voter.dynasty != kNone)
            grudgeHouses.push_back(dynasties_[voter.dynasty].surname);
    }
    Id winner = kNone;
    for (Id c : cands)
        if (winner == kNone || votes[c] > votes[winner]) winner = c;
    // The lord's word is final, but the village remembers a reeve it did not choose.
    std::string how;
    bool imposed = false;
    if (lord_ != kNone && people_[lord_].alive()) {
        Id fav = winner;
        // A lord overrides the vote once in his time, unless the man he picks is already his favourite.
        bool canOverride = lordOverrode_ != lord_ || people_[head].flags.count("lord_favour");
        if (playerRuns && canOverride && people_[head].flags.count("campaign_lord") && opinion(lord_, head) >= 20 &&
            rngSocial_.chance(people_[head].flags.count("lord_favour") ? 0.7 : 0.35))
            fav = head;
        else if (lordOverrode_ != lord_ && opinion(lord_, winner) < -15)
            for (Id c : cands)
                if (opinion(lord_, c) > opinion(lord_, fav)) fav = c;
        if (fav != winner) {
            how = "領主不管票數，硬是指派了 " + fullName(fav) + "。";
            imposed = true;
            lordOverrode_ = lord_;
            Id passedOver = winner;
            winner = fav;
            recordDeed("imposed_reeve", winner, passedOver);
        }
    }
    Person& r = people_[winner];
    setOffice("reeve", winner, imposed ? "領主指派的" : "選上的");
    for (auto it = promises_.begin(); it != promises_.end();) it = it->first.second == "reeve" ? promises_.erase(it) : std::next(it);
    if (playerRuns && cands.size() == 1 && winner == head) {
        log(winner == old ? 2 : 1, "【選莊頭】沒有人出來爭，" + fullName(head) + (winner == old ? " 又當了三年莊頭。" : " 當上了莊頭，這一任三年。"));
    } else if (playerRuns) {
        std::string tally;
        for (Id c : cands) tally += (tally.empty() ? "" : "、") + fullName(c) + " " + std::to_string(votes[c]) + " 票";
        std::string grudge;
        std::sort(grudgeHouses.begin(), grudgeHouses.end());
        grudgeHouses.erase(std::unique(grudgeHouses.begin(), grudgeHouses.end()), grudgeHouses.end());
        for (const auto& g : grudgeHouses) grudge += (grudge.empty() ? "" : "、") + g;
        // Only worth saying when those votes would have changed the result.
        bool decided = winner != head && votes[head] + static_cast<int>(grudgeHouses.size()) >= votes[winner];
        if (!grudge.empty() && decided) grudge = "（" + grudge + " 家記著舊仇，票都投給了別人。）";
        else grudge.clear();
        log(1, "【選莊頭】" + tally + "。" + grudge + how +
                   (winner == head ? fullName(head) + (old == head ? " 連任莊頭，再做三年。" : " 當上了莊頭，這一任三年。") : fullName(winner) + " 當選。"));
        if (winner != head) addStress(head, 6, "落選的難堪");
    } else if (winner != old) {
        log(isPlayerFamily(r) ? 1 : 2, fullName(winner) + " 被選為莊頭（reeve），替領主管理村裡的農事。" + how);
    } else if (imposed) {
        log(2, how);
    }
    if (winner != old) r.reputation += 5;
    reeveTerms_[winner] = winner == old ? reeveTerms_[winner] + 1 : 1;
    settleWants("office", kNone, "reeve", r.household, "當上了");
}

}  // namespace sim
