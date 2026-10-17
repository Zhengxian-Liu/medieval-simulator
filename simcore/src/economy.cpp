// Harvest, rent, wages and buying food. Money is in pence.
#include <algorithm>
#include <cmath>
#include <set>

#include "simcore/world.h"

namespace sim {

namespace {
constexpr double kFarmGate = 0.6;  // what the corn dealer pays at the farm, of the market price he sells at
}

double World::consumption(const Household& h) const {
    double c = 0;
    for (Id m : h.members) c += ageYears(people_[m]) < 10 ? 0.6 : 1.0;
    return c;
}

void World::harvest() {
    double weather = 0.65 + rngEcon_.uniform() * 0.65;
    double mult = param("harvest_mult", 1);
    if (weather < 0.78) log(2, "今年夏天多雨，收成很差。");
    else if (weather > 1.2) log(2, "風調雨順，今年是豐收年。");

    for (auto& h : households_) {
        // Its own holding and any leased demesne; the manor tills what it has not let.
        const double tilled = h.acres + (h.leaseUntil > day_ ? h.leaseAcres : 0) - (h.house == "manor" ? leasedOut() : 0);
        if (h.extinct || tilled <= 0 || h.members.empty()) {
            h.focusYield = 0;  // no crop this year: last year's days don't carry into a holding taken later
            continue;
        }
        double workers = 0, skill = 0, count = 0;
        for (Id m : h.members) {
            const Person& p = people_[m];
            double age = ageYears(p);
            if (age < 10) continue;
            const OccupationDef* od = defs_.occupation(p.occupation);
            bool farmer = !od || od->farms || p.occupation == "housewife";
            if (!farmer) continue;
            double w = age < 14 ? 0.4 : p.sex == Sex::Male ? 1.0 : 0.6;
            if (p.health < 40) w *= 0.5;
            w *= p.effort;  // a grieving or despairing worker does less
            workers += w;
            skill += p.skills[Farming];
            ++count;
        }
        if (h.house == "manor") workers = tilled;  // the lord's demesne is worked by tenants' labour
        else workers += boardedHands(h.id) * 0.8;  // hands hired on for the year
        // Short of hands: hire harvest labourers, if the household can pay them.
        double needed = tilled / 12;
        if (workers < needed) {
            double cost = 120 * param("wage_mult", 1);  // a hired hand's wages and keep for the year
            double hired = std::min(needed - workers, h.money / cost);
            h.money = std::max(0.0, h.money - hired * cost);
            book(h, "雇人收割", -hired * cost);
            workers += hired;
        }
        double labour = std::min(1.0, workers * 12 / tilled);
        double avgSkill = count > 0 ? skill / count : 5;
        double headMod = h.head != kNone ? traitMod(people_[h.head], "harvest") : 0;
        // Week-work: villein households lose days to the lord's fields at the busiest time.
        if (h.head != kNone && !houseFree(h) && h.house != "manor") headMod -= 0.08;
        double yield = tilled * 120 * weather * mult * labour * (0.75 + avgSkill / 40) * (1 + headMod);
        if (h.noOxUntil > day_ && h.house != "manor") yield *= 0.88;  // ploughing with a borrowed beast
        if (h.boonUntil > day_) yield *= 0.85;                        // days owed on the demesne, not on their own strips
        yield *= 1 + h.workBonus;  // the head's spare days, spent in the fields
        h.workBonus = 0;
        yield *= 1 + std::clamp(h.focusYield, -0.15, 0.15);  // where the household's days went this year (年度重心, side roads)
        h.focusYield = 0;
        // Tithe: a tenth of the crop goes to the parish church.
        double tithe = h.house == "manor" || (h.head != kNone && people_[h.head].occupation == "priest") ? 0 : yield * 0.1;
        yield -= tithe;
        // The croft behind the house: peas and beans, a pig, hens, gleaning after the harvest. It
        // keeps a poor family going in a bad year, whatever the size of its holding.
        if (h.house != "manor" && workers > 0) yield += 220 * std::min(1.0, workers) * (0.8 + weather * 0.2);
        if (priest_ != kNone && people_[priest_].household != kNone)
            households_[people_[priest_].household].food += tithe;
        h.food += yield;

        // Kept back: a year's bread and two months more, so the family does not sell at harvest and buy back in July.
        double reserve = consumption(h) * (kDaysPerYear + 60);
        if (h.food > reserve) {
            double sold = (h.food - reserve) * 0.6;
            h.food -= sold;
            h.money += sold * param("grain_price", 0.5) * kFarmGate;  // the corn dealer takes his cut and the carting
            book(h, "賣餘糧", sold * param("grain_price", 0.5) * kFarmGate);
        }
        // The harvest is news when it falls short, when the year is unusual, and once a decade as a ledger.
        h.lastYield = yield / std::max(0.01, consumption(h));
        bool lean = h.food < consumption(h) * kDaysPerYear;  // what is in the barn now, not this crop alone
        bool news = lean != h.lean;  // a house that is always short does not hear it as news each year
        h.lean = lean;
        if (inPlayerHouse(h.id)) {
            double mult = param("harvest_mult", 1);
            if (news || mult < 0.85 || mult > 1.15 || (year() - cfg_.startYear) % 10 == 0)
                log(1, std::string(lean ? "秋收不夠一家吃到明年：" : news ? "秋收總算夠一家吃了：" : "秋收：") + "收了約 " + std::to_string(int(yield / 360)) +
                           " 人一年份的糧食，家裡存錢 " + std::to_string(int(h.money)) + " 便士。");
        }
    }
}

void World::gatherFirewood() {
    for (auto& h : households_) {
        if (h.extinct || h.members.empty()) continue;
        double workers = 0;
        for (Id m : h.members)
            if (ageYears(people_[m]) >= 10) workers += 1;
        h.firewood = std::min(120.0, h.firewood + workers * 30);
    }
}

void World::payRent() {
    double rentMult = param("rent_mult", 1);
    double collected = 0;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (auto& h : households_) {
        if (h.extinct || h.members.empty() || h.id == lordHouse) continue;
        if (h.head != kNone && people_[h.head].occupation == "priest") continue;
        // Freehold pays a small fixed quit-rent; the rest is customary rent.
        double rent = ((h.acres - h.freehold) * 5 + h.freehold + 10) * rentMult * h.rentFactor;
        // The demesne lease: its rent is set in the lease, paid with the rest at Michaelmas.
        if (h.leaseUntil > day_) rent += h.leaseAcres * 4 * rentMult;
        else if (h.leaseUntil >= 0) {
            if (inPlayerHouse(h.id)) log(1, "包租領主自營地的租約到期了，那 " + std::to_string(int(h.leaseAcres)) + " 畝地還給了莊園。");
            for (Id m : h.members) people_[m].flags.erase("demesne_farmer");  // free to lease again
            h.leaseUntil = -1;
            h.leaseAcres = 0;
        }
        if (h.millUntil > day_) {
            // The mill: every house grinds there and leaves a sixteenth of its corn; the farm of it goes to the lord.
            // The toll comes out of every grinding house (a poor one leaves what it has).
            double f = 0.7 + 0.6 * rngEcon_.uniform(), take = 0;
            for (auto& o : households_) {
                if (o.extinct || o.members.empty() || o.id == lordHouse || o.id == h.id) continue;
                double toll = std::min(std::round(3.5 * f), std::max(0.0, o.money));
                o.money -= toll;
                if (toll > 0) book(o, "磨坊的磨麥錢", -toll);
                take += toll;
            }
            h.money += take;
            book(h, "磨坊收的磨麥錢", take);
            rent += 45 * rentMult;
            // Nobody loves the miller: a house or two grumbles that the toll is taken heavy.
            if (h.head != kNone)
                for (int k = 0; k < 2; ++k) {
                    const Household& o = households_[rngEcon_.range(0, static_cast<int>(households_.size()) - 1)];
                    if (!o.extinct && o.head != kNone && o.id != h.id && o.id != lordHouse) addOpinion(o.head, h.head, "mill_toll", -4, 3);
                }
            if (inPlayerHouse(h.id)) log(2, "磨坊這一年收了 " + std::to_string(int(take)) + " 便士的磨麥錢，交給莊園 45 便士。");
        } else if (h.millUntil >= 0) {
            if (inPlayerHouse(h.id)) log(1, "包下磨坊的期滿了，磨坊還給了莊園。");
            for (Id m : h.members) people_[m].flags.erase("mill_farmer");
            h.millUntil = -1;
        }
        if (h.head != kNone && people_[h.head].flags.count("lord_favour")) rent *= 0.7;
        if (h.head != kNone && people_[h.head].flags.count("reeve")) rent *= 0.6;  // the reeve's service is his rent
        if (h.head != kNone && people_[h.head].flags.count("steward_man")) rent *= 0.8;  // the steward's man, likewise
        for (Id m : h.members)
            if (people_[m].flags.count("hayward")) rent *= 0.85;  // and the hayward's part of his
        // Villeins pay part of their rent in labour on the lord's demesne; free tenants pay it all in coin.
        if (h.head != kNone) rent *= houseFree(h) ? 1.15 : 0.8;
        // Short of coin, a family sells grain to the corn dealer to meet the rent, keeping enough to
        // reach the spring: rent first, hunger later.
        double daily = consumption(h);
        if (h.money < rent - 1 && h.food > daily * 300) {
            double price = param("grain_price", 0.5) * kFarmGate;
            double sell = std::min(h.food - daily * 300, (rent - h.money) / price);
            h.food -= sell;
            h.money += sell * price;
            book(h, "賣口糧湊租", sell * price);
            if (inPlayerHouse(h.id) && sell > daily * 20)
                log(1, "為了交租，賣了大約 " + std::to_string(int(sell / daily)) + " 天的口糧給糧販。");
        }
        h.lastRent = rent;
        h.lastRentPaid = std::min(rent, std::max(0.0, h.money));
        if (h.money >= rent - 1) {  // a penny short is paid in eggs
            book(h, "交租", -std::min(h.money, rent));
            h.money = std::max(0.0, h.money - rent);
            collected += rent;
        } else {
            double paid = std::max(0.0, h.money);
            book(h, "交租", -paid);
            collected += paid;
            h.money = 0;
            if (h.head != kNone) people_[h.head].reputation -= 2;
            // The steward carries at most about a year's rent on the books; beyond that he stops counting.
            auto owedLord = [&]() {
                return (h.debt > 0.5 && h.creditor == lord_ ? h.debt : 0.0) + (h.heldCreditor == lord_ ? h.heldDebt : 0.0);
            };
            double before = owedLord();
            addDebt(h, std::min(rent - paid, std::max(0.0, rent - before)), lord_);
            if (inPlayerHouse(h.id) && (before <= 0.5 || owedLord() - before >= 20))
                log(1, "地租 " + std::to_string(int(rent)) + " 便士只交得出 " + std::to_string(int(paid)) +
                           "，差的記在莊園的帳上（欠莊園共 " + std::to_string(int(owedLord())) + " 便士）。");
        }
    }
    // A family without a plough ox buys one at the autumn fair once the rent is paid and coin is left.
    for (auto& h : households_) {
        if (h.extinct || h.members.empty() || h.noOxUntil <= day_ || h.money < 110 || h.debt > 0.5) continue;
        h.money -= 60;
        book(h, "買耕牛", -60);
        h.noOxUntil = -1;
        if (inPlayerHouse(h.id)) log(1, "秋天的市集上，" + (h.head != kNone ? fullName(h.head) : std::string("家裡")) + " 花 60 便士又買了一頭耕牛。");
    }
    if (lordHouse != kNone && lordHouse >= 0) households_[lordHouse].money += collected;
}

void World::ensureFood(Household& h) {
    double daily = consumption(h);
    double price = param("grain_price", 0.5);
    if (h.food < daily * 60 && h.money > 0) {
        double want = daily * 90 - h.food;
        double buy = std::min(want, h.money / price);
        h.food += buy;
        h.money -= buy * price;
        book(h, "買糧", -buy * price);
    }
    if (isWinter() && h.firewood < 10 && h.money > 6) {
        h.firewood += 30;
        h.money -= 6;
        book(h, "買柴", -6);
    }
}

void World::monthlyEconomy() {
    double wage = param("wage_mult", 1);
    for (const auto& p : people_) {
        if (!p.alive() || p.household == kNone || ageYears(p) < 14) continue;
        const OccupationDef* od = defs_.occupation(p.occupation);
        if (!od || od->monthlyIncome <= 0) continue;
        double skill = p.skills[skillFromKey(od->skill)];
        double income = od->monthlyIncome * (0.5 + skill / 20) * wage * std::max(0.2, 1 + traitMod(p, "income"));
        if (const FocusDef* f = focusOf(p))  // against the default, so craftsmen left alone earn what they did
            if (const FocusDef* def = focusDefault(p)) income *= 1 + f->income - def->income;
        households_[p.household].money += income;
        book(households_[p.household], "手藝收入", income);
    }
    // A craft house remembers its trade. While no one at home can work it (the smith died and his
    // son is a child, or only the widow is left), the shop is let to a journeyman for a share.
    static const std::set<std::string> crafts{"smith", "carpenter", "miller", "weaver", "alewife"};
    for (auto& h : households_) {
        if (h.extinct || h.members.empty()) continue;
        if (h.head == kNone) continue;
        const Person& hd = people_[h.head];
        if (ageYears(hd) >= 14 && crafts.count(hd.occupation) && hd.occupation != "alewife") h.trade = hd.occupation;
        bool working = false;
        for (Id m : h.members)
            if (ageYears(people_[m]) >= 14 && people_[m].occupation == h.trade) working = true;
        if (working || h.trade.empty()) {
            if (working && h.tradeLet && inPlayerHouse(h.id))
                log(1, "鋪子收回來了，自家人又掌起了" + defs_.occupation(h.trade)->name + "的活。");
            h.tradeLet = false;
            continue;
        }
        if (!h.tradeLet && inPlayerHouse(h.id))
            log(1, "家裡沒人能掌" + defs_.occupation(h.trade)->name + "鋪，" + fullName(h.head) +
                       " 把鋪子租給了外村來的幫工，每月分一點錢，等孩子長大再收回來。");
        h.tradeLet = true;
        h.money += defs_.occupation(h.trade)->monthlyIncome * 0.35 * wage;
        book(h, "鋪子租金", defs_.occupation(h.trade)->monthlyIncome * 0.35 * wage);
    }
    for (auto& h : households_) {
        if (h.extinct || h.members.empty()) continue;
        // Hands the holding does not need hire themselves out as day labourers.
        double hands = 0;
        for (Id m : h.members) {
            const Person& p = people_[m];
            if (ageYears(p) < 14 || p.health < 40) continue;
            const OccupationDef* od = defs_.occupation(p.occupation);
            if (od && od->farms) hands += p.sex == Sex::Male ? 1.0 : 0.5;
        }
        double spare = std::max(0.0, hands - h.acres / 12);
        // A master who has died out, or whose own granary is down to two months, lets the hired hands go early.
        if (h.hiredUntil > day_ && (h.hiredBy == kNone || households_[h.hiredBy].extinct || households_[h.hiredBy].head == kNone ||
                                    households_[h.hiredBy].food < consumption(households_[h.hiredBy]) * 60)) {
            h.hiredUntil = -1;
            h.hiredBy = kNone;
            if (inPlayerHouse(h.id) && h.head != kNone) log(1, "東家那邊自己也揭不開鍋了，" + fullName(h.head) + " 家的長工提早散了。");
        }
        if (h.hiredUntil > day_) {
            // Hired on for the year: the grown eat at the master's table (a month's board here, the rest of the
            // household eats what they bring home) and the master pays a little of the wage each month.
            int n = hiredHands(h);
            Household& mh = households_[h.hiredBy];
            double board = std::min(n * 40.0, std::max(0.0, mh.food - consumption(mh) * 60));
            mh.food -= board;
            h.food += board;
            double pay = std::min(n * 4 * wage, std::max(0.0, mh.money));
            mh.money -= pay;
            book(mh, "雇長工", -pay);
            h.money += pay;
            book(h, "長工工錢", pay);
            spare = 0;
        } else if (h.hiredUntil >= 0) {
            h.hiredUntil = -1;
            if (inPlayerHouse(h.id) && h.head != kNone)
                log(1, "長工做完了，" + fullName(h.head) + " 從" + (h.hiredBy != kNone && households_[h.hiredBy].head != kNone ? " " + fullName(households_[h.hiredBy].head) + " 家" : "莊園") + "回來了。");
            h.hiredBy = kNone;
        }
        h.money += spare * 7 * wage;
        book(h, "打短工", spare * 7 * wage);
        h.money -= h.members.size() * 0.5;  // clothes, salt, tools
        book(h, "衣服、鹽、農具", -(h.members.size() * 0.5));
        // Better-off households spend on cloth, feasts and gifts: a share of what is put by, but a household that
        // saves hard can still save (at most 12 pence a month, enough for a feast day and a new coat).
        // A house is expected to live as its standing does: a yeoman keeps a servant, dresses his wife, feeds his guests.
        if (h.money > 150) {
            int rung = standing(h.id);
            double cap = rung >= 3 ? 40.0 : rung == 2 ? 22.0 : 12.0;
            double spend = std::min((h.money - 150) * 0.05, cap);
            book(h, "家用排場（衣料、宴客、送禮）", -spend);
            h.money -= spend;
        }
        if (h.money < 0) h.money = 0;
        ensureFood(h);
        // Other houses near the end of their grain do what the player's family is asked about: the grown hire on
        // with a bigger farm for the year, and failing that the parish and the neighbours keep the children alive.
        double daily = consumption(h);
        if (!inPlayerHouse(h.id) && h.head != kNone && h.food < daily * 20) askToFeed(h);
        if (!inPlayerHouse(h.id) && h.head != kNone && h.food < daily * 20) {
            Id master = h.acres < 12 && h.hiredUntil <= day_ ? hireMaster(h.id) : kNone;
            if (master != kNone) {
                h.hiredUntil = day_ + kDaysPerYear;
                h.hiredBy = master;
                log(2, fullName(h.head) + " 家沒了口糧，" + (households_[master].head != kNone && master != (lord_ != kNone ? people_[lord_].household : kNone)
                                                             ? "去 " + fullName(households_[master].head) + " 家" : "去莊園") + "當長工。");
            } else if (h.food < daily * 8) {
                h.food += daily * 20;
            }
        }
        h.money = std::max(0.0, h.money);  // float rounding after spending everything
    }
    monthlyDebts();
}

int World::hiredHands(const Household& h) const {
    int n = 0;
    for (Id m : h.members) {
        const Person& p = people_[m];
        if (ageYears(p) >= 14 && p.health >= 30 && p.home == kNone) ++n;
    }
    return n;
}

// Demesne acres let to a tenant on lease.
double World::leasedOut() const {
    double a = 0;
    for (const auto& o : households_)
        if (!o.extinct && o.leaseUntil > day_) a += o.leaseAcres;
    return a;
}

// Hands a house has boarded with it this year.
int World::boardedHands(Id master) const {
    int n = 0;
    for (const auto& o : households_)
        if (!o.extinct && o.hiredBy == master && o.hiredUntil > day_) n += hiredHands(o);
    return n;
}

// Takes on hired hands: the land wants them (a hand to 12 acres), and the granary can feed them for the year
// on top of the house's own two months' reserve.
bool World::canHire(const Household& m, int n) const {
    if (m.extinct || m.head == kNone) return false;
    const double tilled = m.acres + (m.leaseUntil > day_ ? m.leaseAcres : 0);
    int room = static_cast<int>(tilled / 12) - boardedHands(m.id);
    if (m.house != "manor") {
        int own = 0;
        for (Id x : m.members)
            if (ageYears(people_[x]) >= 14 && people_[x].home == kNone) ++own;
        room -= own;
    }
    return room >= n && m.food - consumption(m) * 60 >= (boardedHands(m.id) + n) * 40.0 * 12;
}

Id World::hireMaster(Id household) const {
    const int n = hiredHands(households_[household]);
    if (n == 0) return kNone;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id best = kNone;
    for (const auto& o : households_) {
        // Not the player's house: whom the family feeds and pays is the family's call, not the village's.
        if (o.extinct || o.id == household || o.id == lordHouse || o.head == kNone || o.acres < 25 || o.hiredUntil > day_ ||
            inPlayerHouse(o.id) || !canHire(o, n))
            continue;
        if (people_[o.head].occupation == "priest") continue;
        if (best == kNone || o.acres > households_[best].acres) best = o.id;
    }
    if (best != kNone) return best;
    return lordHouse != kNone && !inPlayerHouse(lordHouse) && canHire(households_[lordHouse], n) ? lordHouse : kNone;
}

// A vacant parcel taken from the lord with the entry fine on the manor's books, for a house with no coin to pay it.
std::string World::rentVacantOnCredit(Household& h, double minAcres) {
    if (lord_ == kNone || !people_[lord_].alive()) return "莊園眼下沒人做主";
    double owed = (h.debt > 0.5 && h.creditor == lord_ ? h.debt : 0.0) + (h.heldCreditor == lord_ ? h.heldDebt : 0.0);
    if (owed > 40) return "管家翻了翻帳本：「你家欠莊園的 " + std::to_string(int(owed)) + " 便士還沒還呢。」";
    if (h.head != kNone && opinion(lord_, h.head) < -25) return "管家說，老爺不想把地租給你家。";
    Id pick = kNone;
    for (const auto& pc : parcels_) {
        if (pc.gone || pc.holder != kNone || pc.acres < minAcres) continue;
        if (pick == kNone || pc.acres < parcels_[pick].acres) pick = pc.id;  // the smallest that will feed them
    }
    if (pick == kNone) return "村裡眼下沒有空著的地";
    double rate = worldFlags_.count("post_plague") ? 1.5 : 3;
    double fine = parcels_[pick].acres * rate;
    double paid = std::min(fine, std::max(0.0, h.money));
    std::string name = parcelName(parcels_[pick]);
    moveParcel(pick, h.id, "向領主租來");
    Id lordHouse = people_[lord_].household;
    h.money -= paid;
    book(h, "入地費", -paid);
    if (lordHouse != kNone) {
        households_[lordHouse].money += paid;
        book(households_[lordHouse], "收入地費", paid);
    }
    if (fine - paid > 0.5) addDebt(h, fine - paid, lord_);
    if (inPlayerHouse(h.id))
        log(1, "  " + name + "租給了你家，入地費 " + std::to_string(int(fine)) + " 便士" +
                   (fine - paid > 0.5 ? "，交不出的 " + std::to_string(int(fine - paid)) + " 便士記在莊園的帳上。" : "，當場交清。"));
    return "";
}

// A cottager's girl brings a cow's worth; a yeoman's daughter a few pounds, or the match is beneath them.
double World::dowryCap(const Household& h) const {
    int rung = standing(h.id);
    return rung >= 3 ? 480.0 : rung == 2 ? 240.0 : 100.0;
}

void World::askToFeed(Household& h) {
    Id ours = actingHead();
    if (ours == kNone || h.head == kNone) return;
    if (auto it = feedAskedUntil_.find(h.id); it != feedAskedUntil_.end() && it->second > day_) return;
    // One such card a month at most, whoever is hungry (kAnyHouse holds when the last one was).
    constexpr Id kAnyHouse = -2;
    if (auto it = feedAskedUntil_.find(kAnyHouse); it != feedAskedUntil_.end() && it->second > day_) return;
    // Whose house it is to the family: the nearest of kin in it, or a friend of the head's.
    Id kin = kNone;
    for (Id m : h.members)
        if (closeKin(ours, m) || !kinTie(ours, m).empty()) {
            if (kin == kNone || ageYears(people_[m]) > ageYears(people_[kin])) kin = m;
        }
    if (kin == kNone && opinion(ours, h.head) < 40) return;
    Household& ph = households_[playerHousehold_];
    double daily = consumption(h);
    double spare = ph.food - consumption(ph) * 120;
    double price = param("grain_price", 0.5);
    double coin = std::round(daily * 60 * price);
    std::vector<std::string> opts;
    std::vector<int> what;
    if (spare >= daily * 90) opts.push_back("從自家糧倉送去夠吃三個月的糧"), what.push_back(0);
    if (ph.money >= coin + 20) opts.push_back("送 " + std::to_string(int(coin)) + " 便士讓他們買糧"), what.push_back(1);
    if (h.debt > 0.5 && h.creditor != kNone && inPlayerHouse(people_[h.creditor].household)) opts.push_back("免了他們欠你家的債"), what.push_back(2);
    if (opts.empty()) return;
    opts.push_back("自家也不寬裕，先不管");
    what.push_back(3);
    feedAskedUntil_[h.id] = day_ + kDaysPerYear / 4;
    feedAskedUntil_[kAnyHouse] = day_ + kDaysPerMonth;
    int children = 0;
    for (Id m : h.members)
        if (ageYears(people_[m]) < 12) ++children;
    std::string who = kin != kNone ? fullName(kin) + "（" + kinWord(ours, kin) + "）住的 " + fullName(h.head) + " 家" : "好朋友 " + fullName(h.head) + " 家";
    std::string sit = who + "只剩不到二十天的糧了" + (children ? "，" + std::to_string(children) + " 個孩子已經餓瘦了" : "") + "。";
    int r = askPlayer("親友挨餓", sit, opts);
    // No one to ask: a well-off family feeds its own kin; otherwise it keeps the house's bread.
    if (r < 0) r = kin != kNone && (what.front() == 0 || ph.money >= 300) ? 0 : static_cast<int>(opts.size()) - 1;
    int w = what[r];
    if (w == 3) {
        log(1, "〔親友挨餓〕" + sit + "家裡沒有伸手。");
        if (kin != kNone) addOpinion(kin, ours, "left_us_hungry", -10, 10);
        feedAskedUntil_[h.id] = day_ + kDaysPerYear;  // asked and refused: not asked again for a year
        return;
    }
    if (w == 0) {
        double amount = daily * 90;
        ph.food -= amount;
        h.food += amount;
        log(1, "〔親友挨餓〕" + fullName(ours) + " 讓人背了幾袋麥子送到 " + fullName(h.head) + " 家，夠他們吃到下一季。");
    } else if (w == 1) {
        ph.money -= coin;
        book(ph, "接濟別人", -coin);
        h.money += coin;
        log(1, "〔親友挨餓〕" + fullName(ours) + " 送了 " + std::to_string(int(coin)) + " 便士到 " + fullName(h.head) + " 家買糧。");
    } else {
        log(1, "〔親友挨餓〕" + fullName(ours) + " 把 " + fullName(h.head) + " 家欠的債一筆勾銷，讓他們先顧著吃。");
        clearDebt(h);
    }
    ensureFood(h);
    recordDeed("kindness", ours, h.head);
    oweFavour(h.head, ours, 20, "挨餓時的接濟");
    if (kin != kNone && kin != h.head) addOpinion(kin, ours, "fed_us", 15, 20);
}

double World::taxDue(const Household& h) const {
    if (h.extinct || h.members.empty() || h.head == kNone) return 0;
    double grain = std::max(0.0, h.food - consumption(h) * kDaysPerYear) * param("grain_price", 0.5) * kFarmGate;
    double goods = h.money + grain + h.acres * 6;  // the beasts and gear, roughly, by the size of the holding
    if (goods < 120) return 0;                     // under ten shillings of goods: the poor were not taxed
    return std::round(goods / 15);
}

// The lay subsidy: a fifteenth of movable goods, granted by Parliament now and then, and most years once the war
// with France is on. Two of the better men of the village assess their neighbours.
void World::yearlyTax() {
    bool war = worldFlags_.count("war") > 0;
    if (!rngEcon_.chance(war ? 0.7 : year() >= 1332 ? 0.3 : 0.0)) return;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Id assessor = kNone;
    for (const auto& o : households_)
        if (!o.extinct && o.head != kNone && o.id != lordHouse && !inPlayerHouse(o.id) && people_[o.head].occupation != "priest" &&
            (assessor == kNone || o.acres > households_[people_[assessor].household].acres))
            assessor = o.head;
    log(2, std::string("國會准了國王一筆稅：鄉下人家照家當交十五分之一") + (war ? "，說是打法蘭西要用。" : "。"));
    for (auto& h : households_) {
        if (h.extinct || h.members.empty() || h.head == kNone || h.id == lordHouse || people_[h.head].occupation == "priest") continue;
        double tax = taxDue(h);
        if (tax <= 0) continue;
        if (inPlayerHouse(h.id) && actingHead() != kNone) {
            runEventById("lay_subsidy", actingHead(), assessor);
            continue;
        }
        double pay = std::min(tax, h.money);
        h.money -= pay;
        book(h, "國王的稅", -pay);
    }
}

}  // namespace sim
