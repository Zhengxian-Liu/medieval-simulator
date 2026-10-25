// Friendships, quarrels and the yearly reeve election.
#include <algorithm>
#include <map>
#include <set>

#include "simcore/world.h"

namespace sim {

void World::monthlySocial() {
    std::vector<Id> all = livingInVillage();
    if (all.size() < 2) return;
    // Houses after the same thing (a parcel, a post, a match) rub each other the wrong way when they meet.
    std::set<std::pair<Id, Id>> competing;
    for (const auto& x : wants_)
        if (x.open)
            for (const auto& y : wants_)
                if (y.open && y.house != x.house && y.kind == x.kind &&
                    (x.kind == "office" ? x.office == y.office : x.object == y.object && (x.kind != "place" || x.office == y.office)))
                    competing.insert({x.house, y.house});
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
            // How a meeting goes is down to the two natures and what lies between the houses, not to how well they
            // already get on (that made every acquaintance a friend in the end).
            int alike = 0;
            for (const auto& t : pa.traits) {
                const TraitDef* td = defs_.trait(t);
                if (!td || td->category != "personality") continue;
                if (hasTrait(pb, t)) ++alike;
                if (!td->opposite.empty() && hasTrait(pb, td->opposite)) --alike;
            }
            double friendly = 0.7 + alike * 0.05 + traitMod(pa, "sociable") * 0.1 - traitMod(pa, "quarrel") * 0.15;
            if (competing.count({pa.household, pb.household})) friendly -= 0.2;
            friendly -= 0.08 * feudStage(a, b);
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
            bool family = inPlayerHouse(pa.household) && ageYears(pa) >= 12;
            if (!family) continue;
            std::string friendFlag = "friend_" + std::to_string(b);
            std::string rivalFlag = "rival_" + std::to_string(b);
            // Fondness for a father or a sister-in-law in another house is kinship, not a new friend.
            bool kin = closeKin(a, b) || !kinTie(a, b).empty();
            if (after >= 50 && before < 50 && !pa.flags.count(friendFlag) && !kin) {
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
    Id old = currentReeve();
    if (!reeveDue()) return;
    if (old != kNone) people_[old].flags.erase("reeve");
    nextReeveYear_ = year() + 3;

    auto noise = [&](double m) { return static_cast<double>(rngSocial_.range(0, static_cast<int>(m))); };
    std::vector<Id> cands = officeCandidates("reeve", old, noise);
    if (cands.empty()) return;
    Id head = playerHead();
    bool playerRuns = head != kNone && std::find(cands.begin(), cands.end(), head) != cands.end();
    if (playerRuns) {
        people_[head].flags.erase("campaign_ale");
        people_[head].flags.erase("campaign_lord");
        // The sitting reeve standing again is asked as one (its prompt_if): the flag is his until the count.
        if (old == head) people_[head].flags.insert("reeve");
        runEventById("reeve_campaign", head, lord_);
        if (old == head) people_[head].flags.erase("reeve");
    }
    decideFamilyBallot("reeve", cands);
    Tally tally = countVotes("reeve", cands, old, noise);
    familyBallot_.erase("reeve");
    std::map<Id, int>& votes = tally.votes;
    Id winner = kNone;
    for (Id c : cands)
        if (winner == kNone || votes[c] > votes[winner]) winner = c;
    // Even votes: the sitting reeve keeps it, else the one the lord likes better.
    std::string tie;
    int tied = 0;
    for (Id c : cands)
        if (votes[c] == votes[winner]) ++tied;
    if (tied > 1) {
        Id pick = kNone;
        for (Id c : cands)
            if (votes[c] == votes[winner] && (pick == kNone || c == old || (pick != old && lord_ != kNone && opinion(lord_, c) > opinion(lord_, pick)))) pick = c;
        winner = pick;
        tie = pick == old ? "幾家同票，照老規矩由現任的連任。" : (lord_ != kNone ? "幾家同票，由老爺從中點了一個。" : "幾家同票，抽籤定了一個。");
    }
    // The lord's word is final, but the village remembers a reeve it did not choose.
    std::string how;
    bool imposed = false;
    if (lord_ != kNone && people_[lord_].alive()) {
        Id fav = winner;
        // A lord overrides the vote once in his time, unless the man he picks is already his favourite.
        bool canOverride = lordOverrode_ != lord_ || (head != kNone && people_[head].flags.count("lord_favour"));
        bool asked = playerRuns && people_[head].flags.count("campaign_lord");
        if (asked && canOverride && opinion(lord_, head) >= 20 && rngSocial_.chance(people_[head].flags.count("lord_favour") ? 0.7 : 0.35))
            fav = head;
        else if (lordOverrode_ != lord_ && opinion(lord_, winner) < -15)
            for (Id c : cands)
                if (opinion(lord_, c) > opinion(lord_, fav)) fav = c;
        if (asked && fav != head)
            how = opinion(lord_, head) < 20 ? "（你求過領主點名，可老爺跟你家還沒那麼熟，沒有開口。）"
                  : !canOverride            ? "（你求過領主點名，可老爺這一任已經插手過一回，這次不便再管。）"
                  : fav == winner           ? "（你求過領主點名，老爺想了想，還是讓村裡自己選。）"
                                            : "（你求過領主點名，老爺卻另有屬意的人。）";  // followed by whom he named
        if (fav != winner) {
            how += "領主不管票數，硬是指派了 " + fullName(fav) + "。";
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
        log(1, "【選莊頭】" + tallyText(cands, tally, false) + "。" + tie + voteWhy(tally, winner) + how +
                   (winner == head ? fullName(head) + (old == head ? " 連任莊頭，再做三年。" : " 當上了莊頭，這一任三年。") : fullName(winner) + " 當選。"));
        log(1, "  開票時各家當眾說了投誰：" + ballotText(tally) + "。");
        if (winner != head) addStress(head, 6, "落選的難堪");
    } else if (old != kNone && winner != old && people_[old].alive() && inPlayerHouse(people_[old].household)) {
        std::string bar = officeBar(old, "reeve");
        log(1, "【選莊頭】" + fullName(old) + " 這回沒能再出來選" + (bar.empty() ? "" : "（" + bar + "）") + "，" + fullName(winner) + " 當選。" + how);
    } else if (winner != old) {
        log(isPlayerFamily(r) ? 1 : 2, fullName(winner) + " 被選為莊頭（reeve），替領主管理村裡的農事。" + how);
    } else if (imposed) {
        log(2, how);
    }
    if (winner != old) addReputation(winner, 5, "當選莊頭");
    for (Id c : cands)
        if (c != winner && people_[c].household != kNone && r.household != kNone)
            noteClash(people_[c].household, r.household, "莊頭：{w0}選上", 1, winner);
    // Terms on end of one blood: a son after his father carries the count on (「十年莊頭」 is the family's).
    const bool sameBlood = old != kNone && winner != old && r.dynasty != kNone && people_[old].dynasty == r.dynasty;
    reeveTerms_[winner] = winner == old ? reeveTerms_[winner] + 1 : sameBlood ? reeveTerms_[old] + 1 : 1;
    settleWants("office", kNone, "reeve", r.household, "當上了");
}

}  // namespace sim
