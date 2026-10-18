// Secrets as currency (Crusader Kings "hooks"): believing you know someone's secret gives you
// leverage over them, whether or not you are right. And the truth has ways of coming out: a
// dying man wants to go to God with a clean conscience.
#include <algorithm>

#include "simcore/events.h"

namespace sim {

// The secret `holder` is most sure `culprit` did, if it is still worth anything: believed firmly,
// never tried in court, not yet spent, and not too old to matter.
Id World::hookDeed(Id holder, Id culprit) const {
    if (holder == kNone || culprit == kNone || holder == culprit) return kNone;
    Id best = kNone;
    double bestConf = 0;
    for (const auto& b : beliefs_[holder]) {
        if (b.culprit != culprit || b.confidence < 60 || b.used) continue;
        const Deed& d = deeds_[b.deed];
        if (d.actor == holder || day_ - d.day > 30 * kDaysPerYear) continue;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret" || deedTried(d.id)) continue;
        if (b.confidence > bestConf) {
            bestConf = b.confidence;
            best = d.id;
        }
    }
    return best;
}

bool World::hasHook(Id holder) const {
    for (const auto& b : beliefs_[holder]) {
        if (b.confidence < 60 || b.used || b.culprit == holder) continue;
        const Person& c = people_[b.culprit];
        if (!c.alive() || c.household == kNone || c.household == people_[holder].household) continue;
        if (hookDeed(holder, b.culprit) != kNone) return true;
    }
    return false;
}

// A secret that is no longer one between the people in it: said out loud, or tried in a court.
bool World::deedSpoken(Id deed) const { return deedNotes_.count({deed, "spoken"}) > 0 || deedTried(deed); }

// What the head knows and has never said comes up when the people in it cross paths: a wife's old lover at the
// church door, the man who fired the ricks greeting you at the mill. Now and then, once for each secret.
void World::secretSurfaces() {
    familyAffairs();
    Id head = playerHead();
    if (head == kNone || ageYears(people_[head]) < 16 || playerHousehold_ == kNone) return;
    if (!rngKnow_.chance(0.12)) return;
    const Household& home = households_[playerHousehold_];
    std::vector<std::pair<Id, Id>> found;  // (deed, the other party)
    for (const auto& b : beliefs_[head]) {
        // Not what has been said out loud already (shouted in the street, taken to court, thrown in someone's face),
        // nor what was turned into money once.
        if (b.confidence < 60 || b.used || deedSpoken(b.deed) || deedNotes_.count({b.deed, "surfaced"})) continue;
        const Deed& d = deeds_[b.deed];
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret" || day_ - d.day > 30 * kDaysPerYear || d.actor == head) continue;
        const Person& a = people_[d.actor];
        if (!a.alive() || b.culprit != d.actor) continue;
        if (d.type == "affair") {
            // One of the family with someone outside it.
            if (a.household != home.id || d.with == kNone || d.with == head || a.flags.count("adulterer")) continue;
            const Person& o = people_[d.with];
            if (!o.alive() || o.household == kNone || o.household == home.id) continue;
            found.push_back({d.id, d.with});
        } else {
            // A wrong done to this house by someone outside it, never tried.
            if (a.household == kNone || a.household == home.id || d.target == kNone || deedTried(d.id)) continue;
            const Person& v = people_[d.target];
            if (v.household != home.id && !(playerDynasty_ != kNone && v.dynasty == playerDynasty_)) continue;
            found.push_back({d.id, d.actor});
        }
    }
    if (found.empty()) return;
    auto [deed, other] = found[rngKnow_.range(0, static_cast<int>(found.size()) - 1)];
    deedNotes_.insert({deed, "surfaced"});
    runEventById(deeds_[deed].type == "affair" ? "old_flame" : "old_wrong", head, other, deed);
}

// A family member's affair does not stay hidden for years and then fade in one line: it reaches the one keeping the
// house by stages, each a season or more apart. First talk (a 【風聲】 a neighbour lets drop), then a neighbour's hint
// the head can press, ignore or answer by watching, then the two are seen together and the head decides what to do.
// The head's own affair is the head's choice and the head's secret: that one the spouse may find out by the yearly roll.
void World::familyAffairs() {
    Id knower = actingHead();
    if (knower == kNone || playerHousehold_ == kNone || ageYears(people_[knower]) < 16) return;
    const int season = kDaysPerYear / 4;
    const Household& home = households_[playerHousehold_];
    for (Id x : std::vector<Id>(home.members)) {
        Person& px = people_[x];
        if (!px.alive() || x == knower || x == playerHead() || px.lover == kNone || px.loverDeed == kNone || px.lover == knower) continue;
        const Id deed = px.loverDeed, lover = px.lover;
        const Deed& d = deeds_[deed];
        if (d.target == kNone || !people_[d.target].alive() || !people_[lover].alive() || people_[lover].household == kNone) continue;
        if (deedSpoken(deed) || deedNotes_.count({deed, "caught"})) continue;
        const Belief* known = beliefAbout(knower, deed);
        if (known && known->culprit == x && known->confidence >= 60) {
            deedNotes_.insert({deed, "caught"});  // found out another way: nothing left to surface
            continue;
        }
        auto next = affairNext_.find(deed);
        if (next == affairNext_.end()) next = affairNext_.emplace(deed, px.loverSince + rngKnow_.range(3, 9) * kDaysPerMonth).first;
        if (day_ < next->second) continue;
        // Who lets it drop: someone the head knows, not of either house.
        auto outsider = [&](Id t) {
            return t != kNone && people_[t].alive() && people_[t].household != kNone && people_[t].household != home.id && t != lover &&
                   people_[t].household != people_[lover].household && ageYears(people_[t]) >= 16;
        };
        Id teller = pickAcquaintance(knower, rngKnow_);
        if (!outsider(teller)) {
            teller = kNone;
            for (int tries = 0; tries < 8 && teller == kNone; ++tries) {
                std::vector<Id> village = livingInVillage();
                Id t = village.empty() ? kNone : village[rngKnow_.range(0, static_cast<int>(village.size()) - 1)];
                if (outsider(t)) teller = t;
            }
        }
        if (teller == kNone) continue;
        if (!deedNotes_.count({deed, "rumour"})) {
            // 1. Talk: the head half-believes it (not enough to act on), and hears it said.
            deedNotes_.insert({deed, "rumour"});
            whispered_.insert(deed);  // told here, as the head hears it
            addBelief(knower, deed, x, 30, teller);
            News n;
            n.subject = x;
            n.other = d.target;
            n.subjectName = shortName(x);
            if (d.target == knower) n.otherName = "你";
            auto it = defs_.whispers.find("affair");
            std::string text = formatNews(it != defs_.whispers.end() ? it->second : "{subject}背著{other}在外頭有人", n);
            Heard hd;
            hd.day = day_;
            hd.teller = teller;
            hd.text = text;
            hd.weight = 3;
            hd.logged = true;
            heard_.push_back(hd);
            log(1, "【風聲】" + tellerName(teller) + "悄悄說，" + text + "，不知真假。");
            next->second = day_ + season * rngKnow_.range(1, 2);
        } else if (!deedNotes_.count({deed, "hint"})) {
            // 2. A neighbour's hint, plainer: the head chooses what to make of it.
            if (!rngKnow_.chance(0.6)) {
                next->second = day_ + season;
                continue;
            }
            runEventById("affair_hint", knower, teller, deed);
            deedNotes_.insert({deed, "hint"});
            next->second = day_ + season * (deedNotes_.count({deed, "watched"}) ? 1 : 2);
        } else {
            // 3. Seen together. Watching her finds it soonest; a hint let go may never come to more.
            double c = deedNotes_.count({deed, "watched"}) ? 0.6 : deedNotes_.count({deed, "asked"}) ? 0.4 : 0.15;
            if (!rngKnow_.chance(c)) {
                next->second = day_ + season;
                continue;
            }
            deedNotes_.insert({deed, "caught"});
            static const char* kSeen[] = {"天快亮的時候，{x}從 {lover} 家後頭的小路溜回來，鞋上全是露水。",
                                          "收麥那天，{x}和 {lover} 一前一後鑽進了磨坊後面的樹林，半天沒出來。",
                                          "夜裡醒來，{x}不在床上。後門虛掩著，泥地上有兩個人的腳印，一直通到 {lover} 家那邊。"};
            std::string seen = kSeen[rngKnow_.range(0, 2)];
            for (auto [key, val] : {std::pair<std::string, std::string>{"{x}", shortName(x)}, {"{lover}", fullName(lover)}})
                for (size_t at; (at = seen.find(key)) != std::string::npos;) seen.replace(at, key.size(), val);
            toldPlayer_.insert({deed, x});  // said just now, not again as a 【秘密】 line
            addBelief(knower, deed, x, 90, kNone);
            if (d.target == knower) {
                log(1, "〔撞見〕" + seen);
                runEventById("spouse_learns_affair", knower, x, deed);
            } else {
                // Someone else's husband or wife is wronged (a son's wife and a neighbour): the head decides whether
                // they hear it.
                // By full name: the wronged one's first name is often the lover's, or the head of the house it went to.
                const std::string bn = fullName(d.target), xn = people_[x].firstName == people_[d.target].firstName ? fullName(x) : shortName(x);
                std::string sit = seen + bn + "還什麼都不知道。";
                std::vector<std::string> opts{"告訴" + bn, "私下叫" + xn + "斷了", "當作沒看見"};
                int r = askPlayer("撞見", sit, opts);
                bool asked = r >= 0;
                if (!asked) r = 0;
                log(1, "〔撞見〕" + sit + (asked ? "（選擇：" + opts[r] + "）" : "你告訴了" + bn + "。"));
                if (r == 0) {
                    addBelief(d.target, deed, x, 90, knower);
                } else if (r == 1) {
                    endLiaison(x);
                    addOpinion(x, knower, "hard_words", -15, 10);
                    deedNotes_.insert({deed, "spoken"});
                } else {
                    addStress(knower, 8, "撞見了家裡的醜事");
                }
            }
        }
    }
    // A child born while the mother had a lover: as the years pass the head may come to see a stranger's face in it.
    if (!rngKnow_.chance(0.25)) return;
    for (Id c : std::vector<Id>(home.members)) {
        Person& cp = people_[c];
        if (!cp.alive() || !cp.flags.count("paternity_doubt") || cp.flags.count("paternity_told")) continue;
        double age = ageYears(cp);
        if (age < 3 || age > 14 || cp.father == kNone || (cp.father != knower && cp.father != playerHead())) continue;
        const Person& f = people_[cp.father];
        if (!f.alive() || f.household != home.id || ageYears(f) < 16) continue;
        cp.flags.insert("paternity_told");
        runEventById("not_like_you", cp.father, c);
        break;
    }
}

// Before dying, someone who did a secret wrong may send for the one they wronged. Families who
// spent years blaming the wrong house learn it here.
void World::deathbedConfession(Id dying) {
    const Person& p = people_[dying];
    if (ageYears(p) < 30) return;
    double chance = 0.2 + (hasTrait(p, "pious") ? 0.3 : 0) + p.stress / 200 - (hasTrait(p, "cruel") ? 0.15 : 0);
    if (!rngKnow_.chance(std::clamp(chance, 0.0, 0.8))) return;
    int told = 0;
    for (size_t i = deeds_.size(); i-- > 0 && told < 2;) {
        const Deed& d = deeds_[i];
        if (d.actor != dying) continue;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret" || d.type == "affair") continue;
        // Tell the victim, or whoever heads the victim's house now.
        Id victim = d.target;
        if (victim == kNone) continue;
        if (!people_[victim].alive() || people_[victim].household == kNone) {
            Id hh = people_[victim].household;
            if (hh == kNone || households_[hh].extinct) continue;
            victim = households_[hh].head;
        }
        if (victim == kNone || !people_[victim].alive()) continue;
        const Belief* before = beliefAbout(victim, d.id);
        if (before && before->culprit == dying && before->confidence >= 90) continue;
        ++told;
        Id wrong = before && before->culprit != dying && before->confidence >= 50 ? before->culprit : kNone;
        if (inPlayerHouse(people_[victim].household)) {
            std::string line = "【臨終】" + fullName(dying) + " 臨死前派人把 " + fullName(victim) + " 叫到床前，承認 " +
                               describeDeed(d.id) + "是" + (p.sex == Sex::Male ? "他" : "她") + "幹的。";
            if (wrong != kNone) line += "原來這些年家裡一直錯怪了 " + fullName(wrong) + "。";
            log(1, line);
            toldPlayer_.insert({d.id, dying});
        }
        addBelief(victim, d.id, dying, 100, dying);
        // Everyone in the victim's house hears it from them.
        for (Id m : households_[people_[victim].household].members)
            if (m != victim) addBelief(m, d.id, dying, 95, victim);
        if (wrong != kNone && people_[wrong].alive() && people_[wrong].household != kNone)
            runEventById("make_amends", victim, wrong);
    }
}

}  // namespace sim
