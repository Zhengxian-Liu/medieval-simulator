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

void World::feudHeat(Id dynA, Id dynB, double delta, Id deed) {
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
        f->since = day_;
        f->cause = deed;
    }
    f->heat = std::max(0.0, f->heat + delta);
    if (delta > 0 && f->cause == kNone) f->cause = deed;  // heat with no deed behind it (a debt, an old quarrel) first
    if (delta > 0 && deed != kNone) f->last = deed;
    bool player = a == playerDynasty_ || b == playerDynasty_;
    std::string names = dynasties_[a == playerDynasty_ ? a : b].surname + " 家和 " +
                        dynasties_[a == playerDynasty_ ? b : a].surname + " 家";
    while (f->stage < 3 && f->heat >= kStageHeat[f->stage + 1]) {
        ++f->stage;
        if (player && f->stage >= 2) {  // a coolness between houses is everyday life; a grudge is news
            std::string why = f->cause != kNone ? describeDeed(f->cause) : "積了多年的舊怨";
            if (f->last != f->cause && f->last != kNone) why += "，又有 " + describeDeed(f->last);
            std::string what = f->stage == 1 ? "有了嫌隙" : f->stage == 2 ? "結了怨" : "成了世仇";
            log(1, std::string(f->stage == 3 ? "【世仇】" : "【恩怨】") + names + what + "（" + why + "）。");
        }
        emit({"feud", f->last, a, b, false, static_cast<double>(f->stage)});
    }
    while (f->stage > 0 && f->heat < kStageHeat[f->stage] * 0.6) {
        --f->stage;
        if (player && f->stage == 1) log(1, "【恩怨】" + names + "的怨氣慢慢淡了，見了面還是不說話。");
    }
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
                feudHeat(dc, dv, fresh ? -f.heat * 0.6 : -8, d.id);
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
    } else if (t > 0) {
        feudHeat(dc, dv, -t * 0.3 * d.scale, d.id);
    }
}

void World::endFeud(Id dynA, Id dynB, const std::string& how) {
    if (dynA == kNone || dynB == kNone || dynA == dynB) return;
    auto [a, b] = ordered(dynA, dynB);
    for (auto& f : feuds_) {
        if (f.a != a || f.b != b || f.stage == 0) continue;
        if ((a == playerDynasty_ || b == playerDynasty_) && f.stage >= 2)
            log(1, "【和解】" + dynasties_[a == playerDynasty_ ? a : b].surname + " 家和 " +
                       dynasties_[a == playerDynasty_ ? b : a].surname + " 家化解了從 " + describeDeed(f.cause) +
                       "起的恩怨（" + how + "）。");
        f.heat = 0;
        f.stage = 0;
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
            f.heat = 0;
            f.stage = 0;
            continue;
        }
        // Grudges cool, slower the deeper they run.
        double keep = f.stage >= 3 ? 0.94 : f.stage == 2 ? 0.9 : 0.85;
        feudHeat(f.a, f.b, -f.heat * (1 - keep), kNone);
    }
}

// --- debt

void World::addDebt(Household& h, double amount, Id creditor) {
    if (amount <= 0) return;
    if (creditor == kNone) creditor = lord_;
    if (h.debt <= 0.5) {
        h.creditor = creditor;
        h.debtYears = 0;
    } else if (creditor != h.creditor) {
        // Owed to someone else: it waits its turn rather than being added to the wrong account.
        h.heldDebt += amount;
        h.heldCreditor = creditor;
        return;
    }
    h.debt += amount;
}

void World::promoteHeldDebt(Household& h) {
    if (h.debt > 0.5 || h.heldDebt <= 0.5) return;
    h.debt = h.heldDebt;
    h.creditor = h.heldCreditor;
    h.debtYears = 0;
    h.heldDebt = 0;
    h.heldCreditor = kNone;
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
        if (h.head != kNone && month() % 3 == 0) addStress(h.head, 2, "還不清的債");
        // Bread first: only money beyond a few months' food goes to the creditor.
        double need = consumption(h) * 360 - h.food;
        double spare = h.money - 40 - std::max(0.0, need) * param("grain_price", 0.5);
        if (spare <= 0) continue;
        double pay = std::min(h.debt, spare * 0.4);
        h.debt -= pay;
        h.money -= pay;
        book(h, "還債", -pay);
        if (h.creditor != kNone && people_[h.creditor].household != kNone) {
            households_[people_[h.creditor].household].money += pay;
            book(households_[people_[h.creditor].household], "收回借款", pay);
        }
        if (h.debt <= 0.5) {
            h.debt = 0;
            h.debtYears = 0;
            if (inPlayerHouse(h.id))
                log(1, h.creditor == lord_ ? std::string("總算還清了欠領主的債。") : "總算還清了欠 " + fullName(h.creditor) + " 的債。");
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
    for (auto& h : households_) {
        promoteHeldDebt(h);
        if (h.extinct || h.members.empty() || h.debt <= 0.5 || h.head == kNone) continue;
        if (hardTimes || h.debt < 20) continue;  // small arrears just stay on the books
        ++h.debtYears;
        bool lordIsCreditor = h.creditor == lord_ || h.creditor == kNone ||
                              people_[h.creditor].household == kNone || !people_[h.creditor].alive();
        if (!lordIsCreditor && people_[h.creditor].household == h.id) {  // owed to their own house now
            h.debt = 0;
            continue;
        }
        bool home = inPlayerHouse(h.id);
        if (h.debtYears == 1) {
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
                addStress(h.head, 20, "被管家牽走的牛");
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
                           " 便士一次還清了。");
                clearDebt(h);
                continue;
            }
            if (r >= 0 && opts[r].rfind("在法庭", 0) == 0) {
                recordDeed("defaulted", h.head, h.creditor);
                if (h.head != kNone) people_[h.head].reputation -= 4;
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
            log(imp, fullName(a) + " 和 " + fullName(b) + " 的私情，" + (years >= 5 ? "斷斷續續好了 " + std::to_string(years) + " 年，" : "") + "漸漸淡了。");
            endLiaison(a);
            continue;
        }
        if (rngKnow_.chance(0.04)) {
            log(imp, "【醜聞】" + fullName(a) + " 和 " + fullName(b) + " 的姦情敗露，兩人被教會法庭罰款，還得披著白布在主日彌撒上當眾悔罪。");
            for (Id x : {a, b}) {
                Person& p = people_[x];
                p.reputation -= 15;
                p.flags.insert("adulterer");
                Household& ph = households_[p.household];
                book(ph, "教會法庭罰款", -std::min(20.0, std::max(0.0, ph.money)));
                ph.money = std::max(0.0, ph.money - 20);
                if (p.loverDeed != kNone && p.spouse != kNone && people_[p.spouse].alive())
                    addBelief(p.spouse, p.loverDeed, x, 100, kNone);
            }
            recordDeed("affair_exposed", a, b);
            recordDeed("affair_exposed", b, a);
            endLiaison(a);
            continue;
        }
        if (rngKnow_.chance(0.08)) {
            // One betrayed spouse starts to see it.
            Id x = aWed && (!bWed || rngKnow_.chance(0.5)) ? a : b;
            const Person& px = people_[x];
            if (px.loverDeed == kNone) continue;
            const Belief* already = beliefAbout(px.spouse, px.loverDeed);
            if (already && already->culprit == x && already->confidence >= 80) continue;
            addBelief(px.spouse, px.loverDeed, x, 85, kNone);
        }
    }
}

}  // namespace sim
