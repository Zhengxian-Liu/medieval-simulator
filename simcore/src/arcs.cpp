// Story arcs: things that are not over when they happen.
//
// Feud: two families' quarrel grows by stages (嫌隙 → 結怨 → 世仇) as hostile deeds pile up,
// cools with time, and ends with a reconciliation (a marriage, the priest's handshake). It belongs
// to the families, so grandchildren inherit it. Secret deeds feed the feud only once the victims
// believe they know who did it, so a wrong belief starts a feud with the wrong family.
//
// Debt: unpaid rent becomes arrears; each rent day it stays unpaid the steward comes harder:
// a warning with choices, then distraint (the plough ox, an heirloom), then the land itself.
#include <algorithm>
#include <set>
#include <cmath>

#include "simcore/events.h"

namespace sim {

namespace {

constexpr double kStageHeat[] = {0, 20, 50, 90};

std::pair<Id, Id> ordered(Id a, Id b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }

}  // namespace

std::string World::describeDeed(Id deed) const {
    if (deed == kNone) return "";
    const Deed& d = deeds_[deed];
    const DeedDef* def = defs_.deed(d.type);
    return std::to_string(cfg_.startYear + d.day / kDaysPerYear) + " 年的「" + (def ? def->name : d.type) + "」";
}

const Feud* World::feudBetween(Id dynA, Id dynB) const {
    if (dynA == kNone || dynB == kNone || dynA == dynB) return nullptr;
    auto [a, b] = ordered(dynA, dynB);
    for (const auto& f : feuds_)
        if (f.a == a && f.b == b) return &f;
    return nullptr;
}

int World::feudStage(Id personA, Id personB) const {
    if (personA == kNone || personB == kNone) return 0;
    const Feud* f = feudBetween(people_[personA].dynasty, people_[personB].dynasty);
    return f ? f->stage : 0;
}

void World::feudHeat(Id dynA, Id dynB, double delta, Id deed, bool truce) {
    if (dynA == kNone || dynB == kNone || dynA == dynB || delta == 0) return;
    auto [a, b] = ordered(dynA, dynB);
    Feud* f = nullptr;
    for (auto& x : feuds_)
        if (x.a == a && x.b == b) f = &x;
    if (!f) {
        if (delta < 0) return;
        feuds_.push_back({a, b, 0, 0, day_, deed, deed, -1});
        f = &feuds_.back();
    }
    if (f->stage == 0 && f->heat <= 0) {
        // A quarrel begun afresh: its cause, whom it is with and who headed the family are this one's, not an old one's.
        f->since = day_;
        f->cause = deed;
        f->face = f->ourFace = kNone;
        f->told = false;
    }
    f->heat = std::max(0.0, f->heat + delta);
    if (delta > 0 && f->cause == kNone) f->cause = deed;  // heat with no deed behind it (a debt, an old quarrel) first
    if (delta > 0 && deed != kNone) f->last = deed;
    bool player = a == playerDynasty_ || b == playerDynasty_;
    std::string names = dynasties_[a == playerDynasty_ ? a : b].surname + " 家和 " +
                        dynasties_[a == playerDynasty_ ? b : a].surname + " 家";
    std::string why = f->cause != kNone ? describeDeed(f->cause) : "積了多年的舊怨";
    if (f->last != f->cause && f->last != kNone) why += "，又有 " + describeDeed(f->last);
    while (f->stage < 3 && f->heat >= kStageHeat[f->stage + 1]) {
        ++f->stage;
        if (player && f->stage >= 2) {  // a coolness between houses is everyday life; a grudge is news
            std::string what = f->stage == 2 ? "結了怨" : "成了世仇";
            log(1, std::string(f->stage == 3 ? "【世仇】" : "【恩怨】") + names + what + "（" + why + "）。");
            if (!f->told) f->ourFace = actingHead();  // the head it is told to is the one it began under
            f->told = true;
        }
        emit({"feud", f->last, a, b, false, static_cast<double>(f->stage)});
    }
    // A coolness with a second wrong in it is already a story (design/fun-r6.md D): the family is told it has begun, and
    // what can end it, so that its end can be told too.
    if (player && !f->told && f->stage >= 1 && delta > 0 && f->last != kNone && f->last != f->cause) {
        f->told = true;
        f->ourFace = actingHead();
        log(1, "【恩怨】" + names + "有了嫌隙（" + why + "）。兩家的帳不會自己了結：神父調解、上門賠罪、兩家結親、上莊園法庭，"
                                                 "或是等到有人死了、換了一代。");
    }
    // Years with nothing new between them: it is over, and said so; and it is over, so a wrong after it starts a new
    // quarrel with a cause of its own (not the old one back from the dead).
    // Quiet since the priest's handshake (no wrong after it): said as the truce having held.
    const int truceDay = f->truceUntil - 25 * kDaysPerYear;
    const bool held = f->truceUntil > day_ && (f->last == kNone || deeds_[f->last].day <= truceDay);
    auto fade = [&](bool afterTruce) {
        noteThreadEnd("feud", "和" + dynasties_[a == playerDynasty_ ? b : a].surname + "家的恩怨：" +
                                  (afterTruce ? "神父調解之後再沒起事，過去了" : "這些年沒再起事，淡了"));
        log(1, "【恩怨了結】" + names + "從 " + (f->cause != kNone ? describeDeed(f->cause) : std::string("上一代")) + "起的嫌隙，" +
                   (afterTruce ? "神父調解之後兩家再沒起事，這筆帳也就過去了。" : "這些年沒再添新帳，慢慢也就過去了。"));
        f->told = false;
        f->face = f->ourFace = kNone;
        f->heat = 0;
        f->stage = 0;
    };
    while (f->stage > 0 && f->heat < kStageHeat[f->stage] * 0.6) {
        --f->stage;
        // A truce says what it is itself (〔了結恩怨〕: 暫時休兵); the cooling it brings is not told as a fading too.
        if (truce) continue;
        if (player && f->stage == 1) log(1, "【恩怨】" + names + "的怨氣慢慢淡了，見了面還是不說話。");
        if (player && f->stage == 0 && f->told) fade(held);
    }
    // A quarrel the priest cooled to nothing stays the family's (休兵, not 了結) until two quiet years have gone by.
    if (!truce && player && f->stage == 0 && f->told && delta < 0 && f->truceUntil > day_ && day_ >= truceDay + 2 * kDaysPerYear)
        fade(held);
}

// Called for a public deed (culprit = the doer) and, for a secret one, when the victims come to
// believe who did it (culprit = whom they believe).
void World::feudOnDeed(const Deed& d, const DeedDef& def, Id culprit) {
    if (culprit == kNone || d.target == kNone) return;
    Id dc = people_[culprit].dynasty, dv = people_[d.target].dynasty;
    if (dc == kNone || dv == kNone || dc == dv) return;
    // Tenants resent the lord, but they do not feud with him as with an equal.
    Id lordDyn = lord_ != kNone ? people_[lord_].dynasty : kNone;
    if (dc == lordDyn || dv == lordDyn) return;
    if (!feudCounted_.insert({d.id, dc}).second) return;
    if (std::find(def.tags.begin(), def.tags.end(), "peace") != def.tags.end()) {
        endFeud(dc, dv, def.name);
        return;
    }
    // A truce (a priest's handshake, compensation paid) cools a feud but does not end it, and a
    // second truce within a generation hardly counts: everyone remembers how the last one went.
    if (std::find(def.tags.begin(), def.tags.end(), "truce") != def.tags.end()) {
        auto [a, b] = ordered(dc, dv);
        for (auto& f : feuds_)
            if (f.a == a && f.b == b && f.heat > 0) {
                bool fresh = f.truceUntil < day_;
                f.truceUntil = day_ + 25 * kDaysPerYear;
                feudHeat(dc, dv, fresh ? -f.heat * 0.6 : -8, d.id, true);  // (truceUntil is set first: feudHeat reads it)
            }
        return;
    }
    auto role = [&](const char* r) {
        auto it = def.roles.find(r);
        return it == def.roles.end() ? 0.0 : it->second;
    };
    double t = role("target"), tf = role("target_family");
    if (t < 0 || tf < 0) {
        feudHeat(dc, dv, (std::abs(std::min(t, 0.0)) * 0.6 + std::abs(std::min(tf, 0.0)) * 0.4) * d.scale, d.id);
        // Whom the quarrel is with, seen from the family: the other side's one in this wrong, and the family's head now.
        if (dc == playerDynasty_ || dv == playerDynasty_) {
            auto [a, b] = ordered(dc, dv);
            for (auto& f : feuds_)
                if (f.a == a && f.b == b) {
                    // A child in it (a daughter refused) is not whom the quarrel is with: the head of that child's house is.
                    Id face = dc == playerDynasty_ ? d.target : culprit;
                    if (face != kNone && ageYears(people_[face]) < 16 && people_[face].household != kNone &&
                        households_[people_[face].household].head != kNone)
                        face = households_[people_[face].household].head;
                    f.face = face;  // (who headed the family is set when the quarrel is told: feudHeat)
                }
        }
    } else if (t > 0) {
        feudHeat(dc, dv, -t * 0.3 * d.scale, d.id);
    }
}

void World::endFeud(Id dynA, Id dynB, const std::string& how) {
    if (dynA == kNone || dynB == kNone || dynA == dynB) return;
    auto [a, b] = ordered(dynA, dynB);
    for (auto& f : feuds_) {
        if (f.a != a || f.b != b || (f.stage == 0 && !f.told)) continue;
        if ((a == playerDynasty_ || b == playerDynasty_) && (f.stage >= 2 || f.told)) {
            std::string other = dynasties_[a == playerDynasty_ ? b : a].surname;
            log(1, "【恩怨了結】" + dynasties_[a == playerDynasty_ ? a : b].surname + " 家和 " + other + " 家化解了從 " +
                       (f.cause != kNone ? describeDeed(f.cause) : std::string("上一代")) + "起的恩怨（" + how + "）。");
            noteThreadEnd("feud", "和" + other + "家的恩怨：" + how);
        }
        f.heat = 0;
        f.stage = 0;
        f.told = false;
        f.face = f.ourFace = kNone;
    }
}

void World::yearlyFeuds() {
    for (auto& f : feuds_) {
        if (f.heat <= 0) continue;
        bool aliveA = false, aliveB = false;
        for (const auto& h : households_) {
            if (h.extinct || h.members.empty()) continue;
            aliveA = aliveA || h.dynasty == f.a;
            aliveB = aliveB || h.dynasty == f.b;
        }
        if (!aliveA || !aliveB) {  // one family is gone from the village: nobody left to hate
            if (f.told && (f.a == playerDynasty_ || f.b == playerDynasty_) && aliveA != aliveB) {
                Id gone = aliveA ? f.b : f.a;
                if (gone != playerDynasty_) {
                    log(1, "【恩怨了結】" + dynasties_[gone].surname + " 家在村裡已經沒有人了。從 " +
                               (f.cause != kNone ? describeDeed(f.cause) : std::string("上一代")) + "起的那筆帳，也就沒人再記了。");
                    noteThreadEnd("feud", "和" + dynasties_[gone].surname + "家的恩怨：他家在村裡沒人了");
                }
            }
            f.told = false;
            f.face = f.ourFace = kNone;
            f.heat = 0;
            f.stage = 0;
            continue;
        }
        // Grudges cool, slower the deeper they run.
        double keep = f.stage >= 3 ? 0.94 : f.stage == 2 ? 0.9 : 0.85;
        feudHeat(f.a, f.b, -f.heat * (1 - keep), kNone);
        // A coolness never told and long gone quiet is forgotten: a wrong after it is a quarrel of its own.
        if (f.stage == 0 && !f.told && f.heat < 2) f.heat = 0;
    }
}

// --- the feud as the family's thread: its turns and its ends (design/fun-r6.md D)

void World::noteThreadEnd(const std::string& kind, const std::string& text) {
    threadEnds_.push_back({day_, actingHead(), kind, text});
}

void World::yearlyFeudTurns() {
    if (playerHousehold_ == kNone || actingHead() == kNone || handedOver_ > day_) return;
    // One a year at most: the hottest quarrel whose turn it is and that has something to ask (one with nothing new
    // in it does not keep the others from their turn).
    size_t pick = feuds_.size();
    for (size_t i = 0; i < feuds_.size(); ++i) {
        const Feud& f = feuds_[i];
        if (!f.told || f.heat <= 0 || (f.a != playerDynasty_ && f.b != playerDynasty_) || f.nextTurn > year()) continue;
        if (!feudTurnDue(f)) continue;
        if (pick == feuds_.size() || f.heat > feuds_[pick].heat) pick = i;
    }
    if (pick < feuds_.size()) feudTurn(pick);
}

// Who speaks for the other side now: the house of the one it is with, else the biggest house of that name.
Id World::feudTheirHouse(const Feud& f) const {
    const Id other = f.a == playerDynasty_ ? f.b : f.a;
    const Id face = f.face;
    Id theirHouse = face != kNone && people_[face].alive() && people_[face].household != kNone &&
                            households_[people_[face].household].dynasty == other
                        ? people_[face].household
                        : kNone;
    if (theirHouse == kNone)
        for (const auto& o : households_)
            if (!o.extinct && !o.members.empty() && o.head != kNone && o.dynasty == other &&
                (theirHouse == kNone || o.acres > households_[theirHouse].acres))
                theirHouse = o.id;
    return theirHouse;
}

// The turns feudTurn has, in its order: the one it was with died, a new head, or a wrong in the last three years.
bool World::feudTurnDue(const Feud& f) const {
    Id theirHouse = feudTheirHouse(f);
    if (theirHouse == kNone) return false;
    Id them = households_[theirHouse].head;
    if (f.face != kNone && !people_[f.face].alive() && f.face != them) return true;
    if (f.ourFace != kNone && f.ourFace != actingHead()) return true;
    return f.last != kNone && day_ - deeds_[f.last].day <= 3 * kDaysPerYear;
}

void World::feudTurn(size_t i) {
    const Id other = feuds_[i].a == playerDynasty_ ? feuds_[i].b : feuds_[i].a;
    const Id ours = actingHead();
    Id face = feuds_[i].face;
    Id theirHouse = feudTheirHouse(feuds_[i]);
    if (theirHouse == kNone) {
        feuds_[i].nextTurn = year() + 1;
        return;
    }
    const Id them = households_[theirHouse].head;
    const std::string sur = dynasties_[other].surname, us = dynasties_[playerDynasty_].surname;
    const Person& op = people_[ours];
    auto since = [&] { return feuds_[i].cause != kNone ? describeDeed(feuds_[i].cause) : std::string("上一代"); };
    const char* word = feuds_[i].stage >= 3 ? "世仇" : feuds_[i].stage == 2 ? "怨" : "嫌隙";
    bool forgiving = hasTrait(op, "forgiving") || hasTrait(op, "kind") || hasTrait(op, "pious");
    bool vengeful = hasTrait(op, "vengeful") || hasTrait(op, "wrathful");

    // The one it was with is dead: does the quarrel go into the grave with them, or to their house?
    if (face != kNone && !people_[face].alive() && face != them) {
        feuds_[i].nextTurn = year() + 2;
        // The wake is in the days after the death; later on, it is a visit to the grave.
        const bool fresh = day_ - people_[face].deathDay <= 3 * kDaysPerMonth;
        const std::string pron = people_[face].sex == Sex::Male ? "他" : "她";
        const int diedIn = cfg_.startYear + people_[face].deathDay / kDaysPerYear;
        std::string sit = fullName(face) + (fresh ? " 不在了（" : " " + std::to_string(diedIn) + " 年就不在了（") + people_[face].deathCause + "）。" + us + " 家和 " + sur +
                          " 家從 " + since() + "起的" + word + "，多半是跟" + pron + "結下的。如今 " + sur + " 家由 " + fullName(them) + " 當家。";
        int r = askPlayer("人死帳了？", sit,
                          {fresh ? "去守靈，讓舊帳跟著下葬" : "到" + pron + "墳前上一回香，讓舊帳跟著入土", "不去：這筆帳，" + sur + " 家的人一樣要還"});
        if (r < 0) r = vengeful || opinion(ours, them) < -25 ? 1 : 0;
        if (r == 0) {
            addOpinion(them, ours, "came_to_wake", 10, 10);
            endFeud(playerDynasty_, other, fullName(face) + " 死了，" + op.firstName + (fresh ? " 去守了靈，舊帳跟著下葬" : " 到墳前上了香，舊帳跟著入土"));
        } else {
            feuds_[i].face = them;
            log(1, "〔人死帳了？〕" + op.firstName + (fresh ? " 沒有去守靈。" : " 沒有去上墳。") + sur + " 家記住了：兩家的帳，往後記在 " + fullName(them) + " 身上。");
            feudHeat(playerDynasty_, other, 6, kNone);
        }
        return;
    }
    // A new head of the family: the other house asks whether the old quarrel is the new head's to carry.
    if (feuds_[i].ourFace != kNone && feuds_[i].ourFace != ours) {
        feuds_[i].nextTurn = year() + 2;
        std::string sit = fullName(them) + " 託人帶話：兩家從 " + since() + "起的" + word + "，是上一代結下的。如今換你當家了，那些帳，到此為止？";
        int r = askPlayer("上一代的帳", sit, {"答應：上一代的帳，不再算了", "不答應：這筆帳，我接下了"});
        if (r < 0) r = vengeful || opinion(ours, them) < -20 ? 1 : 0;
        if (r == 0) {
            addOpinion(them, ours, "made_peace", 8, 10);
            addOpinion(ours, them, "made_peace", 8, 10);
            endFeud(playerDynasty_, other, "換了一代，" + op.firstName + " 答應不再算上一代的帳");
        } else {
            feuds_[i].ourFace = ours;
            log(1, "〔上一代的帳〕" + op.firstName + " 回絕了。" + sur + " 家知道了：上一代的帳，" + op.firstName + " 接下了。");
            feudHeat(playerDynasty_, other, 8, kNone);
        }
        return;
    }
    // Still going (a wrong in the last three years): the family says what it will do about it.
    if (feuds_[i].last == kNone || day_ - deeds_[feuds_[i].last].day > 3 * kDaysPerYear) {
        feuds_[i].nextTurn = year() + 1;  // nothing to ask this year
        return;
    }
    feuds_[i].nextTurn = year() + 3;
    Household& h = households_[playerHousehold_];
    Household& th = households_[theirHouse];
    std::string sit = us + " 家和 " + sur + " 家的" + word + "：從 " + since() + "起，最近一回是 " + describeDeed(feuds_[i].last) + "（" +
                      sur + " 家現在由 " + fullName(them) + " 當家，" + fullName(them) + " 對你 " + std::to_string(int(opinion(them, ours))) + "）。這筆帳，要怎麼了結？";
    std::vector<std::string> opts;
    std::vector<int> kind;  // 0 amends, 1 priest, 2 court, 3 wait
    if (h.money >= 10) opts.push_back("上門賠罪，帶一份 10 便士的禮"), kind.push_back(0);
    if (feuds_[i].truceUntil < day_) opts.push_back("請神父出面調解（兩家暫時休兵）"), kind.push_back(1);
    opts.push_back("把這些年的帳告上莊園法庭，讓陪審評理（輸的一方賠 12 便士）"), kind.push_back(2);
    opts.push_back("先記著，總有一天討回來"), kind.push_back(3);
    int r = askPlayer("了結恩怨", sit, opts);
    auto has = [&](int x) { return std::find(kind.begin(), kind.end(), x) != kind.end(); };
    // The rules: a vengeful head bides his time, a forgiving one makes amends, others go to the priest.
    int k = r >= 0 && r < static_cast<int>(kind.size()) ? kind[r] : vengeful ? 3 : forgiving && has(0) ? 0 : has(1) ? 1 : 3;
    if (k == 0) {
        h.money -= 10, book(h, "賠罪的禮", -10);
        th.money += 10;
        if (opinion(them, ours) < -45 && rngKnow_.chance(0.5)) {
            log(1, "〔了結恩怨〕" + op.firstName + " 提著禮上 " + sur + " 家的門，" + fullName(them) + " 收了禮，門卻沒開。兩家的帳還在。");
            return;
        }
        endFeud(playerDynasty_, other, op.firstName + " 上門賠了罪，" + fullName(them) + " 讓進了門");
        recordDeed("made_amends", ours, them);
    } else if (k == 1) {
        log(1, "〔了結恩怨〕" + op.firstName + " 請神父出面。神父把兩家家主叫到教堂門口，兩人握了手：暫時休兵，往後再起事，還是會翻舊帳。");
        recordDeed("mediated", ours, them);
    } else if (k == 2) {
        // The jury: the other heads of the village, by whom they like better, and who began it.
        double support = 0;
        int n = 0;
        Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
        for (const auto& o : households_) {
            if (o.extinct || o.head == kNone || o.members.empty() || o.id == h.id || o.id == theirHouse || o.id == lordHouse) continue;
            support += opinion(o.head, ours) - opinion(o.head, them);
            ++n;
        }
        if (n > 0) support /= n;
        Id began = feuds_[i].cause != kNone ? deeds_[feuds_[i].cause].actor : kNone;
        double started = began == kNone ? 0 : people_[began].dynasty == other ? 12 : people_[began].dynasty == playerDynasty_ ? -12 : 0;
        bool won = rngKnow_.chance(std::clamp(0.5 + support / 80 + started / 100, 0.15, 0.85));
        Household& payer = won ? th : h;
        Household& paid = won ? h : th;
        double fine = std::min(12.0, std::max(0.0, std::floor(payer.money)));
        payer.money -= fine, book(payer, "莊園法庭的判決", -fine);
        paid.money += fine, book(paid, "莊園法庭的判決", fine);
        if (won) {
            people_[ours].reputation += 2;
            addOpinion(them, ours, "lost_in_court", -10, 8);
            log(1, "【莊園法庭】陪審聽完兩家這些年的帳，判 " + sur + " 家理虧，賠你家 " + std::to_string(int(fine)) + " 便士。這口氣，算是討回來了。");
            endFeud(playerDynasty_, other, "莊園法庭判 " + sur + " 家理虧，討回了公道");
        } else {
            people_[ours].reputation -= 1;
            addStress(ours, 8, "法庭上輸給仇家");
            log(1, "【莊園法庭】陪審偏向 " + sur + " 家，判你家理虧，賠了 " + std::to_string(int(fine)) + " 便士。" + op.firstName + " 低著頭認了。");
            endFeud(playerDynasty_, other, "莊園法庭判你家理虧，" + op.firstName + " 認了");
        }
    } else {
        log(1, "〔了結恩怨〕" + op.firstName + " 沒有動作：和 " + sur + " 家的帳，先記著。");
    }
}

// What this generation is living through, for the page (這一代的線).
std::vector<World::Thread> World::threads() const {
    std::vector<Thread> out;
    if (playerHousehold_ == kNone) return out;
    const Household& h = households_[playerHousehold_];
    Id head = actingHead();
    if (head == kNone) return out;
    const Person& hp = people_[head];
    auto yearOf = [&](int d) { return cfg_.startYear + d / kDaysPerYear; };
    for (const auto& f : feuds_) {
        if (!f.told || f.heat <= 0 || (f.a != playerDynasty_ && f.b != playerDynasty_)) continue;
        Id other = f.a == playerDynasty_ ? f.b : f.a;
        std::string t = std::string(f.stage >= 3 ? "世仇" : f.stage == 2 ? "結了怨" : "有嫌隙") + "：從 " +
                        (f.cause != kNone ? describeDeed(f.cause) : std::string("上一代")) + "起";
        if (f.last != kNone && f.last != f.cause) t += "，最近一回是 " + describeDeed(f.last);
        if (f.face != kNone) t += "（對方是 " + fullName(f.face) + (people_[f.face].alive() ? "" : "，已經不在了") + "）";
        if (f.truceUntil > day_) t += "；神父調解過，暫時休兵";
        out.push_back({"feud", "和" + dynasties_[other].surname + "家的恩怨", t,
                       "了結的路：上門賠罪、神父調解、兩家結親、告上莊園法庭；那人死了或換了一代，也會再問你一次。",
                       f.cause != kNone ? yearOf(deeds_[f.cause].day) : f.since >= 0 ? yearOf(f.since) : -1});
    }
    if (const AmbitionDef* a = defs_.ambition(hp.ambition))
        out.push_back({"ambition", "志向「" + a->name + "」",
                       "走到第 " + std::to_string(hp.ambitionStep) + " 步，共 " + std::to_string(a->milestones.size() + 1) + " 步",
                       "限到 " + std::to_string(ambitionDeadline(hp)) + " 年底；沒做成就留給下一代", yearOf(hp.ambitionSince)});
    const Dynasty& dyn = dynasties_[playerDynasty_];
    if (const AmbitionDef* a = defs_.ambition(dyn.regret); a && a->id != hp.ambition)
        out.push_back({"wish", "上一代沒做完的心願", "「" + a->name + "」", "立志時選它，做成了家族聲望加倍",
                       dyn.headSince.empty() ? -1 : yearOf(dyn.headSince.back())});
    // The family's people on a road that ends somewhere: school, a master, service, the wars, a clerk's desk.
    for (const auto& p : people_) {
        if (!p.alive() || !onRoad(p.station) || p.id == head) continue;
        bool ours = (p.household == h.id && p.home == kNone) || p.home == h.id || (isAway(p) && p.awayFrom == h.id);
        if (!ours) continue;
        std::string t = p.firstName + " " + stationName(p) + "（" + std::to_string(yearOf(p.stationSince)) + " 年起";
        const bool term = p.stationUntil >= 0 || p.serviceEnd >= 0;
        if (p.stationUntil >= 0) t += "，約 " + std::to_string(yearOf(p.stationUntil)) + " 年期滿";
        else if (p.serviceEnd >= 0) t += "，約 " + std::to_string(yearOf(p.serviceEnd)) + " 年期滿";
        t += "）";
        std::string ends = p.station == "scholar" ? "期滿考核：考上了當書記（往後可受神職），沒考上識了字回家；交不起學費就得回家"
                           : p.station == "town_apprentice" ? "學滿七年出師：當伙計、入行會當師傅，或回村"
                           : p.station == "apprentice" ? "學滿出師，從此吃這門手藝"
                           : p.station == "clerk" ? "往後是受神職、留下替領主管帳，或還俗成家"
                           : p.station == "archer" ? "期滿回村、負傷回家，或死在外頭"
                           : term                       ? "期滿回家"
                                                        : "沒有說定做到哪一年：家裡叫回來、成了家，或一直做下去";
        out.push_back({"path", fullName(p.id), t, ends, yearOf(p.stationSince)});
    }
    if (owedAll(h.id) > 0.5) out.push_back({"debt", "欠債", debtText(h.id), "還清，或到期時拿地、牛、勞役抵", -1});  // -1: no start shown (a debt rolls over)
    return out;
}

// --- debt

void World::addDebt(Household& h, double amount, Id creditor) {
    if (amount <= 0) return;
    if (creditor == kNone) creditor = lord_;
    if (h.debt <= 0.5) {
        h.creditor = creditor;
        h.debtSince = day_;
        // A neighbour's loan made after the winter is due at the Michaelmas after next ("明年米迦勒節"), not in weeks.
        h.debtYears = creditor != lord_ && month() >= 2 ? -1 : 0;
    } else if (creditor != h.creditor) {
        // Owed to someone else: it waits its turn rather than being added to the wrong account.
        if (h.heldDebt <= 0.5 || h.heldCreditor == creditor) {
            if (h.heldDebt <= 0.5) h.heldSince = day_;
            h.heldDebt += amount;
            h.heldCreditor = creditor;
            return;
        }
        // A third creditor: lumped with the first, as when households merge (life.cpp); the books are not so fine.
    }
    h.debt += amount;
}

// Whether a new loan from `creditor` has a place on the house's books (two creditors at most). A neighbour never takes
// the last place while the manor holds none: Michaelmas arrears must always have somewhere to go.
bool World::debtRoom(const Household& h, Id creditor) const {
    if (creditor == kNone) creditor = lord_;
    const bool inMain = h.debt > 0.5, inHeld = h.heldDebt > 0.5;
    if ((inMain && h.creditor == creditor) || (inHeld && h.heldCreditor == creditor)) return true;
    if (inMain && inHeld) return false;
    if (creditor != lord_ && lord_ != kNone && (inMain || inHeld) && owedLord(h) <= 0.5) return false;
    return true;
}

double World::owedLord(const Household& h) const {
    if (lord_ == kNone) return 0;
    return (h.debt > 0.5 && h.creditor == lord_ ? h.debt : 0.0) + (h.heldCreditor == lord_ ? h.heldDebt : 0.0);
}

double World::owedAll(Id household) const {
    const Household& h = households_[household];
    return (h.debt > 0.5 ? h.debt : 0.0) + (h.heldDebt > 0.5 ? h.heldDebt : 0.0);
}

// "欠莊園 62 便士（拖了一年）、欠 艾倫·米勒 30 便士（明年米迦勒節到期）": what is owed, to whom, and when.
std::string World::debtText(Id household) const {
    const Household& h = households_[household];
    auto whom = [&](Id c) { return c == kNone || c == lord_ ? std::string("欠莊園 ") : "欠 " + fullName(c) + " "; };
    std::vector<std::string> parts;
    if (h.debt > 0.5) {
        bool lord = h.creditor == kNone || h.creditor == lord_;
        std::string when;
        if (h.debtYears >= 1) when = lord ? "（拖了 " + std::to_string(h.debtYears) + " 年）" : "（約定的日子已經過了）";
        else {
            // Due at the Michaelmas after (−debtYears) more of them; rent day is the end of the ninth month.
            int ahead = 1 - h.debtYears;  // Michaelmases from now until it is due
            bool past = month() > 8 || (month() == 8 && dayOfMonth() == 29);  // this year's rent day is behind us
            int y = year() + (past ? 1 : 0) + ahead - 1;
            int dy = y - year();
            std::string at = dy == 0 ? "今年" : dy == 1 ? "明年" : dy == 2 ? "後年" : std::to_string(y) + " 年";
            when = lord ? (ahead >= 2 ? "（寬限到" + at + "米迦勒節）" : "（" + at + "米迦勒節管家來收）") : "（" + at + "米迦勒節到期）";
        }
        parts.push_back(whom(h.creditor) + std::to_string(static_cast<long>(h.debt)) + " 便士" + when);
    }
    if (h.heldDebt > 0.5) parts.push_back(whom(h.heldCreditor) + std::to_string(static_cast<long>(h.heldDebt)) + " 便士（排在前一筆後面）");
    std::string s;
    for (const auto& x : parts) s += (s.empty() ? "" : "、") + x;
    return s;
}

// Takes up to `amount` off what the house owes the manor; returns how much came off.
double World::reduceLordDebt(Household& h, double amount) {
    double done = 0;
    if (lord_ == kNone || amount <= 0) return 0;
    if (h.debt > 0.5 && h.creditor == lord_) {
        double d = std::min(h.debt, amount);
        h.debt -= d;
        done += d;
        if (h.debt <= 0.5) h.debt = 0;
    }
    if (done < amount && h.heldCreditor == lord_ && h.heldDebt > 0.5) {
        double d = std::min(h.heldDebt, amount - done);
        h.heldDebt -= d;
        done += d;
        if (h.heldDebt <= 0.5) h.heldDebt = 0, h.heldCreditor = kNone;
    }
    promoteHeldDebt(h);
    return done;
}

void World::promoteHeldDebt(Household& h) {
    if (h.debt > 0.5 || h.heldDebt <= 0.5) return;
    h.debt = h.heldDebt;
    h.creditor = h.heldCreditor;
    h.debtSince = h.heldSince;
    h.debtYears = 0;
    h.heldDebt = 0;
    h.heldCreditor = kNone;
}

Id World::seekLender(Household& h, double amount, const std::string& why, double& owed) {
    owed = amount;
    std::set<Id> asked;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (int tries = 0; tries < 8; ++tries) {
        Id best = kNone;
        double bestMoney = 0;
        for (const auto& o : households_) {
            if (o.extinct || o.members.empty() || o.id == h.id || o.id == lordHouse || o.head == kNone || asked.count(o.id)) continue;
            if (people_[o.head].occupation == "priest" || !debtRoom(h, o.head)) continue;
            if (o.money < amount + 60 || o.debt > 0) continue;
            if (h.head != kNone && opinion(o.head, h.head) < 5) continue;
            if (o.money > bestMoney) bestMoney = o.money, best = o.head;
        }
        if (best == kNone) return kNone;
        Id lh = people_[best].household;
        if (!inPlayerHouse(lh) || inPlayerHouse(h.id) || actingHead() == kNone || h.head == kNone) return best;
        // The player's purse: the player says.
        Id ours = actingHead();
        std::string sit = fullName(h.head) + " 家來借 " + std::to_string(int(amount)) + " 便士（" + why + "）。他們家存糧約 " +
                          std::to_string(int(h.food / std::max(0.5, consumption(h)))) + " 天，手上 " + std::to_string(int(h.money)) +
                          " 便士；你對他們 " + std::to_string(int(opinion(ours, h.head))) + "。";
        int r = askPlayer("來借錢", sit, {"借給他們", "借，但秋後要多還兩成", "白送給他們", "不借"});
        if (r < 0) r = opinion(ours, h.head) >= 30 ? 0 : 3;  // no one to ask: a friend is not turned away
        if (r == 3) {
            asked.insert(lh);
            addOpinion(h.head, ours, "refused_loan", -6, 5);
            log(1, "〔來借錢〕" + fullName(h.head) + " 家來借錢，" + fullName(ours) + " 沒有借。");
            continue;
        }
        if (r == 1) owed = std::round(amount * 1.2);
        if (r == 2) {
            owed = 0;
            recordDeed("kindness", ours, h.head);
            oweFavour(h.head, ours, 20, "白送的錢");
        }
        bool grain = why == "借糧度荒";
        log(1, "〔來借錢〕" + fullName(ours) + (r == 2 ? " 白送了 " : " 借了 ") + (grain ? "值 " : "") + std::to_string(int(amount)) + " 便士" +
                   (grain ? "的糧" : "") + "給 " + fullName(h.head) + " 家" + (r == 1 ? "，說好秋後還 " + std::to_string(int(owed)) + " 便士。" : "。"));
        return best;
    }
    return kNone;
}

Id World::findLender(const Household& h) const {
    Id best = kNone;
    double bestMoney = 0;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (const auto& o : households_) {
        if (o.extinct || o.members.empty() || o.id == h.id || o.id == lordHouse || o.head == kNone) continue;
        if (people_[o.head].occupation == "priest") continue;
        if (o.money < h.debt + 60 || o.debt > 0) continue;
        if (h.head != kNone && opinion(o.head, h.head) < 5) continue;
        if (o.money > bestMoney) {
            bestMoney = o.money;
            best = o.head;
        }
    }
    return best;
}

void World::monthlyDebts() {
    for (auto& h : households_) {
        if (h.extinct || h.members.empty() || h.debt <= 0) continue;
        // The manor pays what it borrowed after the Michaelmas rents, half a year's loan at a time, as the steward wrote it.
        bool manor = lord_ != kNone && people_[lord_].household == h.id;
        if (manor && month() != 9) continue;
        if (manor && (h.creditor == kNone || people_[h.creditor].household == kNone || households_[people_[h.creditor].household].extinct)) {
            h.debt = 0, h.debtYears = 0, h.creditor = kNone;  // the house it owed is gone: nobody left to pay
            continue;
        }
        if (h.head != kNone && !manor && month() % 3 == 0) addStress(h.head, 2, "還不清的債");
        // The harvest month's coin goes to the rent first (rent day settles the arrears with it), and a neighbour's
        // loan is paid at the Michaelmas agreed, not a little each month; only once overdue is it dunned monthly.
        bool toLord = h.creditor == kNone || h.creditor == lord_;
        if (!manor && (month() == 8 || (!toLord && h.debtYears < 1))) continue;
        // Bread first: only money beyond a few months' food goes to the creditor.
        double need = consumption(h) * 360 - h.food;
        double spare = h.money - 40 - std::max(0.0, need) * param("grain_price", 0.5);
        if (spare <= 0) continue;
        double pay = manor ? std::min({h.debt, spare, std::max(110.0, std::round(h.debt * 0.5))}) : std::min(h.debt, spare * 0.4);
        h.debt -= pay;
        h.money -= pay;
        book(h, "還債", -pay);
        if (h.creditor != kNone && people_[h.creditor].household != kNone) {
            households_[people_[h.creditor].household].money += pay;
            book(households_[people_[h.creditor].household], "收回借款", pay);
            if (manor && inPlayerHouse(people_[h.creditor].household) && h.debt > 0.5)
                log(1, "莊園按帳還了 " + std::to_string(static_cast<long>(pay)) + " 便士，還欠 " + std::to_string(static_cast<long>(h.debt)) + " 便士。");
        }
        if (h.debt <= 0.5) {
            h.debt = 0;
            h.debtYears = 0;
            if (inPlayerHouse(h.id))
                log(1, (h.creditor == lord_ ? std::string("總算還清了欠領主的債") : "總算還清了欠 " + fullName(h.creditor) + " 的債") +
                           (h.debtSince >= 0 ? "（" + seasonLabel(h.debtSince) + "借的）" : "") + "。");
            else if (manor && h.creditor != kNone && people_[h.creditor].household != kNone && inPlayerHouse(people_[h.creditor].household))
                log(1, "莊園把借你家的錢連本帶利還清了。");
            if (h.head != kNone) addStress(h.head, -15, "");
            h.creditor = kNone;
        }
    }
}

// On rent day, before this year's rent: debts left from earlier years come due, harder each time.
void World::yearlyDebts() {
    // In famine and plague years the manor court does not press anyone.
    bool hardTimes = param("harvest_mult", 1) < 0.8 || param("plague_monthly", 0) > 0;
    for (auto& h : households_)
        if (!h.extinct && h.lodgerUntil > day_) h.money += 14, book(h, "房客的租錢", 14);
    // Whose arrears get settled today (paid, forgiven, worked off): this year's shortfall then waits a year (payRent).
    lordCleared_.clear();
    stewardOnCard_.clear();
    std::map<Id, double> owedBefore;
    for (const auto& h : households_)
        if (!h.extinct && owedLord(h) > 0.5) owedBefore[h.id] = owedLord(h);
    for (auto& h : households_) {
        promoteHeldDebt(h);
        if (h.extinct || h.members.empty() || h.debt <= 0.5 || h.head == kNone) continue;
        if (lord_ != kNone && people_[lord_].household == h.id) continue;  // the manor pays on its own terms (monthlyDebts)
        if (hardTimes) continue;
        bool lordIsCreditor = h.creditor == lord_ || h.creditor == kNone ||
                              people_[h.creditor].household == kNone || !people_[h.creditor].alive();
        if (!lordIsCreditor && people_[h.creditor].household == h.id) {  // owed to their own house now
            h.debt = 0;
            continue;
        }
        bool home = inPlayerHouse(h.id);
        if (lordIsCreditor) {
            if (h.debt < 20) continue;  // small arrears just stay on the books
            if (++h.debtYears <= 0) continue;  // not due yet
        } else {
            // A neighbour's loan falls due at the Michaelmas agreed, small or large, and is paid then if the purse allows
            // (the player's house answers its creditor itself, unless the sum is a trifle).
            if (++h.debtYears <= 0) continue;
            if ((!home || h.debt < 20) && h.money - (h.debt < 20 ? 0 : 20) >= h.debt) {
                Household& ch = households_[people_[h.creditor].household];
                h.money -= h.debt;
                book(h, "還債", -h.debt);
                ch.money += h.debt;
                book(ch, "收回借款", h.debt);
                if (home) log(1, "米迦勒節到了，照約定還了欠 " + fullName(h.creditor) + " 的 " + std::to_string(int(h.debt)) + " 便士。");
                else if (inPlayerHouse(ch.id))
                    log(1, fullName(h.head) + " 家照約定，在米迦勒節還了欠你家的 " + std::to_string(int(h.debt)) + " 便士。");
                clearDebt(h);
                continue;
            }
            if (h.debt < 20) continue;  // a trifle: dunned month by month from now on (monthlyDebts)
        }
        // Owed to the player's house: whether to press, wait or let it go is the player's to say.
        if (!lordIsCreditor && !home && inPlayerHouse(people_[h.creditor].household) && actingHead() != kNone) {
            Id ours = actingHead();
            bool court = h.debtYears >= 2;
            std::string sit = fullName(h.head) + " 家欠你家 " + std::to_string(int(h.debt)) + " 便士，說好的日子" + (court ? "早就過了，拖了兩年。" : "已經過了。") +
                              "他們家存糧約 " + std::to_string(int(h.food / std::max(0.5, consumption(h)))) + " 天，手上 " + std::to_string(int(h.money)) +
                              " 便士；你對他們 " + std::to_string(int(opinion(ours, h.head))) + "。";
            std::vector<std::string> opts = {court ? "告上莊園法庭" : "上門去討", "再寬限一年", "免了這筆債"};
            int r = askPlayer("欠你家的債", sit, opts);
            if (r == 1) {
                h.debtYears = court ? 1 : 0;
                addOpinion(h.head, ours, "gave_time", 8, 5);
                log(1, "〔欠你家的債〕" + fullName(ours) + " 讓 " + fullName(h.head) + " 家再緩一年。");
                continue;
            }
            if (r == 2) {
                log(1, "〔欠你家的債〕" + fullName(ours) + " 把 " + fullName(h.head) + " 家欠的 " + std::to_string(int(h.debt)) + " 便士一筆勾銷了。");
                recordDeed("kindness", ours, h.head);
                oweFavour(h.head, ours, 20, "免了欠債");
                clearDebt(h);
                continue;
            }
        }
        if (h.debtYears == 1) {
            // Old arrears and a rent the purse cannot meet either: one reckoning on the Michaelmas card (payRent).
            if (lordIsCreditor && home && chooser_ && h.money < rentOf(h) - 1) {
                stewardOnCard_.insert(h.id);
                continue;
            }
            runEventById(lordIsCreditor ? "debt_steward" : "debt_neighbour", h.head, lordIsCreditor ? lord_ : h.creditor);
            continue;
        }
        if (h.debtYears == 2 && lordIsCreditor) {
            // Distraint: the steward takes the plough ox, or failing that the best thing in the house.
            if (h.noOxUntil < day_) {
                h.noOxUntil = day_ + 40 * kDaysPerYear;  // until they can buy another
                h.debt = std::max(0.0, h.debt - 40);
                if (home) log(1, "【欠債】管家帶人來牽走了家裡的耕牛抵債，明年犁地只能跟鄰居借牲口。");
            } else {
                Id taken = kNone;
                for (Id it : h.items)
                    if (!items_[it].lost) taken = it;
                if (taken != kNone) {
                    items_[taken].lost = true;
                    h.debt = std::max(0.0, h.debt - 60);
                    if (home) log(1, "【欠債】管家拿走了家裡的" + defs_.items.at(items_[taken].type).name + "抵債。");
                } else {
                    h.debtYears = 3;
                }
            }
            if (h.debtYears == 2) {
                if (lord_ != kNone) recordDeed("distrained", lord_, h.head);
                addStress(h.head, 20, "管家牽走的牛");
                continue;
            }
        }
        if (lordIsCreditor) {
            // The third rent day: a reckoning, and the family chooses how to pay for it.
            runEventById("debt_reckoning", h.head, lord_);
            continue;
        }
        // Owed to a neighbour: the manor court gives them land, or the family defaults and the
        // neighbour's house never forgets it.
        // The player's family hears the claim in court and answers it.
        if (home) {
            double could = std::min({std::ceil(h.debt / 40), std::floor(h.acres * 0.25), std::max(0.0, h.acres - 3)});
            bool canPay = h.money >= h.debt;
            std::vector<std::string> opts;
            if (could >= 1 && h.acres >= 12) opts.push_back("交出 " + std::to_string(int(could)) + " 畝地抵債");
            if (canPay) opts.push_back("咬牙把 " + std::to_string(int(h.debt)) + " 便士一次還清");
            opts.push_back("在法庭上咬定還過了（賴帳）");
            int r = askPlayer("莊園法庭", fullName(h.creditor) + " 把欠了三年的 " + std::to_string(int(h.debt)) +
                                             " 便士告上莊園法庭，要 " + fullName(h.head) + " 家今天給個了斷。",
                              opts);
            if (r >= 0 && opts[r].rfind("咬牙", 0) == 0) {
                h.money -= h.debt;
                book(h, "還債", -h.debt);
                households_[people_[h.creditor].household].money += h.debt;
                book(households_[people_[h.creditor].household], "收回借款", h.debt);
                log(1, "【欠債】" + fullName(h.head) + " 在莊園法庭上把欠 " + fullName(h.creditor) + " 的 " + std::to_string(int(h.debt)) +
                           " 便士一次還清了" + (h.debtSince >= 0 ? "（" + seasonLabel(h.debtSince) + "借的）" : "") + "。");
                clearDebt(h);
                continue;
            }
            if (r >= 0 && opts[r].rfind("在法庭", 0) == 0) {
                recordDeed("defaulted", h.head, h.creditor);
                if (h.head != kNone) addReputation(h.head, -4, "欠債賴帳，被告上法庭");
                log(1, "【欠債】" + fullName(h.head) + " 在莊園法庭上咬定早就還過了。陪審的鄰居面面相覷，" + fullName(h.creditor) +
                           " 家這筆錢算是要不回來了。");
                clearDebt(h);
                continue;
            }
        }
        double acres = forfeitLand(h, false);
        if (acres > 0) {
            if (home)
                log(1, "【欠債】" + fullName(h.creditor) + " 告上莊園法庭，法庭判把 " + std::to_string(int(acres)) + " 畝地抵給對方。");
            addStress(h.head, 30, "抵債失去的地");
        } else {
            recordDeed("defaulted", h.head, h.creditor);
            if (home || isPlayerFamily(people_[h.creditor]))
                log(1, "【欠債】" + fullName(h.head) + " 家實在還不出 " + std::to_string(int(h.debt)) + " 便士，" + fullName(h.creditor) +
                           " 家這筆錢算是要不回來了。");
        }
        clearDebt(h);
    }
    for (const auto& [id, owed] : owedBefore)
        if (owedLord(households_[id]) <= 0.5) lordCleared_.insert(id);
}

void World::clearDebt(Household& h) {
    h.debt = 0;
    h.debtYears = 0;
    h.creditor = kNone;
    promoteHeldDebt(h);
}

// Forfeiture: land goes to whoever is owed. A lord wants tenants more than land, so the family
// keeps at least three quarters of its holding and its croft.
double World::forfeitLand(Household& h, bool lordIsCreditor) {
    double acres = std::min({std::ceil(h.debt / 40), std::floor(h.acres * 0.25), std::max(0.0, h.acres - 3)});
    // The lord's court cannot take back what he has granted by charter.
    if (lordIsCreditor) acres = std::min(acres, std::floor(h.acres - h.freehold));
    if (acres < 1 || h.acres < 12) return 0;
    if (lordIsCreditor) {
        acres = moveLand(h.id, kNone, acres, LandPick::CustomaryOnly, "抵債被收回");
        if (lord_ != kNone && acres > 0) recordDeed("land_seized", lord_, h.head, acres / 4);
    } else {
        acres = moveLand(h.id, people_[h.creditor].household, acres, LandPick::CustomaryFirst, "抵債收來");
        if (acres > 0) recordDeed("land_seized", h.creditor, h.head, acres / 4);
    }
    return acres;
}

// After harvest the steward lets vacant land to households with hands to spare (the player's
// household decides for itself through events). Before the plague land is scarce and is taken up at
// once; afterwards nobody is short of land and much of it lies empty.
void World::reletVacant() {
    bool landHungry = !worldFlags_.count("post_plague");
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    // Those already after a vacant parcel put in for it first (contest.cpp); then houses with hands to spare
    // ask for some.
    allotVacant();
    for (auto& h : households_) {
        if (vacantAcres() < 3) return;
        if (h.extinct || h.members.empty() || inPlayerHouse(h.id) || h.id == lordHouse || h.head == kNone) continue;
        if (people_[h.head].occupation == "priest" || h.debt > 0) continue;
        double hands = 0;
        for (Id m : h.members) {
            const Person& p = people_[m];
            if (ageYears(p) >= 14 && p.health >= 40) hands += p.sex == Sex::Male ? 1.0 : 0.5;
        }
        double spare = hands * 12 - h.acres;
        if (spare < 5 || h.money < 15) continue;
        if (!rngEcon_.chance(landHungry ? 0.6 : 0.2)) continue;
        // They put in for the vacant parcel that suits them best; the steward lets each to one of those asking.
        Id pick = kNone;
        double best = 1e9;
        for (const auto& pc : parcels_)
            if (!pc.gone && pc.holder == kNone && std::abs(pc.acres - std::min(5.0, std::floor(spare))) < best)
                best = std::abs(pc.acres - std::min(5.0, std::floor(spare))), pick = pc.id;
        if (pick != kNone) addWant(h.id, "parcel", pick, kNone, "", "家裡人手多，地不夠種");
    }
    allotVacant();
}

void World::runEventById(const std::string& id, Id person, Id target, Id deed) {
    if (person == kNone) return;
    for (const auto& ev : defs_.events) {
        if (ev.id != id) continue;
        EventRunner runner(*this);
        runner.fireScheduled(ev, people_[person], target, deed);
        return;
    }
}

// ---- Lovers -------------------------------------------------------------------------------
// An affair is one standing secret per pair, not a fresh scandal every month. Each year it may
// cool, a spouse may find out, or the whole parish may.

void World::takeLover(Id a, Id b) {
    for (Id x : {a, b}) {
        Person& p = people_[x];
        if (p.lover != kNone) endLiaison(x);
    }
    for (auto [x, y] : {std::pair{a, b}, std::pair{b, a}}) {
        Person& p = people_[x];
        p.lover = y;
        p.loverSince = day_;
        p.loverDeed = p.spouse != kNone && people_[p.spouse].alive() ? recordDeed("affair", x, p.spouse) : kNone;
        if (p.loverDeed != kNone) deeds_[p.loverDeed].with = y;
    }
}

void World::exposeAffair(Id a, Id b) {
    if (a == kNone || b == kNone) return;
    bool family = isPlayerFamily(people_[a]) || isPlayerFamily(people_[b]);
    log(family ? 1 : 2, "【醜聞】" + fullName(a) + " 和 " + fullName(b) + " 的姦情敗露，兩人被教會法庭罰款，還得披著白布在主日彌撒上當眾悔罪。");
    for (Id x : {a, b}) {
        Person& p = people_[x];
        addReputation(x, -15, "姦情敗露，當眾悔罪");
        p.flags.insert("adulterer");
        noteLife(x, "和 " + fullName(x == a ? b : a) + " 的私情敗露，在教堂當眾悔罪");
        if (p.household != kNone) {
            Household& ph = households_[p.household];
            book(ph, "教會法庭罰款", -std::min(20.0, std::max(0.0, ph.money)));
            ph.money = std::max(0.0, ph.money - 20);
        }
        if (p.loverDeed != kNone && p.spouse != kNone && people_[p.spouse].alive())
            addBelief(p.spouse, p.loverDeed, x, 100, kNone);
        if (p.loverDeed != kNone) deedNotes_.insert({p.loverDeed, "spoken"});
    }
    recordDeed("affair_exposed", a, b);
    recordDeed("affair_exposed", b, a);
    if (people_[a].lover == b) endLiaison(a);
}

void World::endLiaison(Id a) {
    Id b = people_[a].lover;
    people_[a].lover = kNone;
    if (b != kNone && people_[b].lover == a) people_[b].lover = kNone;
}

void World::yearlyLiaisons() {
    for (auto& pa : people_) {
        Id a = pa.id, b = pa.lover;
        if (b == kNone || b < a) continue;  // each pair once (the other side has the smaller id)
        Person& pb = people_[b];
        bool family = isPlayerFamily(pa) || isPlayerFamily(pb);
        int imp = family ? 1 : 2;
        auto gone = [&](const Person& p) { return !p.alive() || p.household == kNone; };
        if (gone(pa) || gone(pb)) {
            Person& left = gone(pa) ? pb : pa;
            const Person& lost = gone(pa) ? pa : pb;
            if (!gone(left) && !lost.alive()) {
                addStress(left.id, 15, "不能說出口的傷心");
                if (isPlayerFamily(left)) log(1, fullName(left.id) + " 在 " + fullName(lost.id) + " 的葬禮上站得遠遠的，一滴眼淚也不敢掉。");
            }
            endLiaison(gone(pa) ? b : a);
            continue;
        }
        bool aWed = pa.spouse != kNone && people_[pa.spouse].alive();
        bool bWed = pb.spouse != kNone && people_[pb.spouse].alive();
        if (!aWed && !bWed) {
            endLiaison(a);
            continue;
        }
        int years = (day_ - pa.loverSince) / kDaysPerYear;
        if (years >= 1 && rngKnow_.chance(0.12 + years * 0.02)) {
            // Told only when the family knows of it: an affair the head never found out about ends as it went on,
            // unseen (not a line out of nowhere about the head's own wife).
            Id head = playerHead();
            auto known = [&](const Person& x) {
                const Belief* bl = x.loverDeed != kNone && head != kNone ? beliefAbout(head, x.loverDeed) : nullptr;
                return bl && bl->culprit == x.id && bl->confidence >= 60;
            };
            bool told = !family || a == head || b == head || known(pa) || known(pb);
            if (told)
                log(imp, fullName(a) + " 和 " + fullName(b) + " 的私情，" + (years >= 5 ? "斷斷續續好了 " + std::to_string(years) + " 年，" : "") + "漸漸淡了。");
            endLiaison(a);
            continue;
        }
        if (rngKnow_.chance(0.04)) {
            exposeAffair(a, b);
            continue;
        }
        if (rngKnow_.chance(0.08)) {
            // One betrayed spouse starts to see it. In the player's house that is a story told by stages instead
            // (hooks.cpp familyAffairs), unless it is the head who strayed.
            Id x = aWed && (!bWed || rngKnow_.chance(0.5)) ? a : b;
            const Person& px = people_[x];
            if (px.loverDeed == kNone) continue;
            if (inPlayerHouse(px.household) && x != playerHead() && x != actingHead()) continue;
            const Belief* already = beliefAbout(px.spouse, px.loverDeed);
            if (already && already->culprit == x && already->confidence >= 80) continue;
            addBelief(px.spouse, px.loverDeed, x, 85, kNone);
        }
    }
}

}  // namespace sim
