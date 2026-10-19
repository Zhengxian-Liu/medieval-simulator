// Harvest, rent, wages and buying food. Money is in pence.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "simcore/world.h"

namespace sim {

namespace {
constexpr double kFarmGate = 0.6;  // what the corn dealer pays at the farm, of the market price he sells at
const std::set<std::string> kCrafts{"smith", "carpenter", "miller", "weaver", "alewife"};

std::string num(double v) { return std::to_string(static_cast<long>(std::lround(v))); }

// "×1.15", "×0.8": a rent factor as the steward writes it.
std::string times(double f) {
    char b[16];
    std::snprintf(b, sizeof b, "%.2f", f);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return "×" + s;
}

std::string join(const std::vector<std::string>& xs, const std::string& sep) {
    std::string s;
    for (const auto& x : xs) s += (s.empty() ? "" : sep) + x;
    return s;
}
}  // namespace

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

// The Michaelmas rent and how it is made up: customary acres at 5d, freehold at a penny, the croft, the lord's rise,
// the demesne lease and the mill, times what the times have done to rents (war, plague), the offices that excuse a
// share, and the villein's week-work against the free tenant's coin.
double World::rentOf(const Household& h, std::string* why, double lessAcres) const {
    const double rentMult = param("rent_mult", 1);
    const double customary = std::max(0.0, h.acres - h.freehold - lessAcres);
    std::vector<std::string> parts;
    if (customary > 0.05) parts.push_back("地 " + num(customary) + " 畝×5");
    if (h.freehold > 0.05) parts.push_back("自由地 " + num(h.freehold) + " 畝×1");
    parts.push_back("宅院 10");
    double rent = (customary * 5 + h.freehold + 10) * rentMult * h.rentFactor;
    if (std::abs(h.rentFactor - 1) > 0.005) parts.push_back(std::string(h.rentFactor > 1 ? "領主加的租 " : "領主減的租 ") + times(h.rentFactor));
    if (h.leaseUntil > day_) {
        rent += h.leaseAcres * 4 * rentMult;
        parts.push_back("包租的自營地 " + num(h.leaseAcres) + " 畝×4");
    }
    if (h.millUntil > day_ && !h.millFree) {
        rent += 45 * rentMult;
        parts.push_back("磨坊的包租 45");
    }
    if (std::abs(rentMult - 1) > 0.005) {
        // Which turns of the times did it (data/history.json, the "note" on a rent_mult effect).
        std::vector<std::string> notes;
        for (size_t i = 0; i < nextHistory_ && i < defs_.history.size(); ++i) {
            const Json& ef = defs_.history[i].effects;
            if (!ef.isArray()) continue;
            for (const auto& e : ef.asArray())
                if (e.has("param") && e["param"].asString() == "rent_mult" && e.has("note"))
                    notes.push_back(e["note"].asString() + " " + times(e.numberOr("mult", 1)));
        }
        parts.push_back("時勢 " + times(rentMult) + (notes.empty() ? "" : "（" + join(notes, "、") + "）"));
    }
    if (h.head != kNone) {
        const Person& hd = people_[h.head];
        if (hd.flags.count("lord_favour")) rent *= 0.7, parts.push_back("領主寵信 ×0.7");
        if (hd.flags.count("reeve")) rent *= 0.6, parts.push_back("當莊頭 ×0.6");  // the reeve's service is his rent
        if (hd.flags.count("steward_man")) rent *= 0.8, parts.push_back("管家的人 ×0.8");  // the steward's man, likewise
    }
    for (Id m : h.members)
        if (people_[m].flags.count("hayward")) rent *= 0.85, parts.push_back("看青人 ×0.85");  // and the hayward's part of his
    // Villeins pay part of their rent in labour on the lord's demesne; free tenants pay it all in coin.
    if (h.head != kNone) {
        bool free = houseFree(h);
        rent *= free ? 1.15 : 0.8;
        parts.push_back(free ? "自由佃戶全交現錢 ×1.15" : "農奴一部分用勞役抵 ×0.8");
    }
    if (why) *why = join(parts, "、");
    return rent;
}

// The rent moved since last Michaelmas for a reason the family did not choose (the times, its own freedom): say so.
void World::explainRent(Household& h, double rent) {
    const double mult = param("rent_mult", 1);
    const int free = h.head != kNone && houseFree(h) ? 1 : 0;
    if (h.lastRentMult > 0 && h.lastRent > 0) {
        std::vector<std::string> because;
        if (std::abs(mult - h.lastRentMult) > 0.005) {
            std::vector<std::string> notes;
            for (size_t i = 0; i < nextHistory_ && i < defs_.history.size(); ++i) {
                const HistoryDef& hd = defs_.history[i];
                int when = (hd.year - cfg_.startYear) * kDaysPerYear + hd.month * kDaysPerMonth;
                if (when <= day_ - kDaysPerYear || !hd.effects.isArray()) continue;
                for (const auto& e : hd.effects.asArray())
                    if (e.has("param") && e["param"].asString() == "rent_mult" && e.has("note"))
                        notes.push_back(e["note"].asString() + " " + times(e.numberOr("mult", 1)));
            }
            because.push_back("時勢" + (notes.empty() ? " " + times(mult / h.lastRentMult) : "：" + join(notes, "、")));
        }
        if (h.lastRentFree >= 0 && free != h.lastRentFree)
            because.push_back(free ? "贖了身，不再替領主服勞役，租子得全用現錢交（×1.15，當農奴時是 ×0.8）"
                                   : "成了農奴，一部分租子改用勞役抵（×0.8）");
        if (!because.empty() && std::abs(rent - h.lastRent) >= 3)
            log(1, "今年米迦勒節的地租是 " + num(rent) + " 便士，去年 " + num(h.lastRent) + "：" + join(because, "；") + "。");
    }
    h.lastRentMult = mult;
    h.lastRentFree = free;
}

// Pays the rent out of the purse; what it does not cover goes on the manor's books. With `respite` (the old arrears
// were paid or forgiven at this very Michaelmas) the steward lets the new shortfall wait a year instead of pressing.
void World::settleRent(Household& h, double rent, bool respite, bool begged) {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    h.lastRent = rent;
    bool full = h.money >= rent - 1;  // a penny short is paid in eggs
    // Short, the steward takes whole pennies: the fraction stays in the purse, so what was paid, what is owed and
    // the header's purse are the same whole numbers.
    double paid = full ? std::min(std::max(0.0, h.money), rent) : std::floor(std::max(0.0, h.money) + 1e-9);
    h.lastRentPaid = paid;
    h.money = full ? std::max(0.0, h.money - rent) : h.money - paid;
    book(h, "交租", -paid);
    if (lordHouse != kNone && lordHouse >= 0) households_[lordHouse].money += full ? rent : paid;
    // Paid in full there is nothing to reckon (no card): the chronicle still says the rent was met, and what it was.
    if (full && inPlayerHouse(h.id) && rent >= 1)
        log(1, "〔米迦勒節〕今年的地租 " + num(rent) + " 便士照數交清，存錢還剩 " + num(h.money) + " 便士。");
    if (full) return;
    if (h.head != kNone) people_[h.head].reputation -= 2;
    // The steward carries at most about a year's rent on the books; beyond that he stops counting.
    double before = owedLord(h);
    addDebt(h, std::min(rent - paid, std::max(0.0, rent - before)), lord_);
    if (respite && h.creditor == lord_ && h.debt > 0.5) h.debtYears = -1;
    if (!inPlayerHouse(h.id)) return;
    // In whole pence as the header shows the purse and the debt (rounded down), so the card, this line and the
    // header tell one sum.
    auto whole = [](double v) { return std::to_string(static_cast<long>(std::floor(v + 1e-9))); };
    if (respite)
        log(1, "地租 " + num(rent) + " 便士只交得出 " + whole(paid) + "。" + (begged ? "管家答應了，" : "舊帳才剛了清，管家說") + "差的 " +
                   whole(rent - paid) + " 便士" + (begged && before > 0.5 ? "連同舊帳" : "") + "寬限一年，後年米迦勒節再算（欠莊園共 " + whole(owedLord(h)) + " 便士）。");
    else if (before <= 0.5 || owedLord(h) - before >= 20)
        log(1, "地租 " + num(rent) + " 便士只交得出 " + whole(paid) + "，差的記在莊園的帳上（欠莊園共 " + whole(owedLord(h)) + " 便士）。");
}

// The player's house cannot pay at Michaelmas: one card with the whole reckoning (this year's rent and how it is made
// up, the arrears that fell due, the purse and the barn), and the levers a family had. The card comes back after a
// lever that covers only part of it, until the sum is met or the rest goes on the books.
void World::reckonRent(Household& h, double rent, const std::string& why0) {
    const double rent0 = rent;
    std::string why = why0;  // the breakdown shown; after land is given back, of the land still held
    const double daily = std::max(0.5, consumption(h));
    const double price = param("grain_price", 0.5) * kFarmGate;
    const int toHarvest = daysToHarvest();
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    bool oldDue = stewardOnCard_.count(h.id) > 0;  // the steward's call about old arrears was left for this card
    bool respite = lordCleared_.count(h.id) > 0;
    bool begged = false, begSaved = false, soldSafe = false;
    const std::string head = fullName(h.head);
    for (int round = 0; round < 6; ++round) {
        double old = oldDue ? owedLord(h) : 0;
        if (rent + old - h.money <= 1) break;
        // What is short, in the card's own numbers (the rent and arrears as shown, the purse as the header shows it),
        // so 租 − 手上 = 還差 on the card and every lever offers that same sum.
        const double cash = std::floor(std::max(0.0, h.money));
        double need = std::max(1.0, std::round(rent) + std::round(old) - cash);
        double owedNow = owedLord(h);
        std::string sit = "米迦勒節，管家來收租。今年的租 " + num(rent) + " 便士（" + why + "）";
        if (owedNow > 0.5) sit += "；欠莊園的舊帳 " + num(owedNow) + " 便士" + (oldDue ? "，已經拖了一年，今年到期"
                                                                                     : h.debtYears >= 1 && h.creditor == lord_ ? "，拖了 " + std::to_string(h.debtYears) + " 年，管家今年沒追"
                                                                                                                              : "，還沒到期");
        sit += "。手上 " + num(cash) + " 便士，還差 " + num(need) + "。倉裡的糧夠吃 " + num(h.food / daily) + " 天，離明年收成 " +
               std::to_string(toHarvest) + " 天；糧販收糧，一個月的口糧給 " + num(daily * 30 * price) + " 便士。";
        if (respite) sit += "舊帳剛了清，管家說今年差的可以寬限一年。";
        std::vector<std::string> opts;
        std::vector<int> tag;
        // Grain: first only what the house can spare and still reach the harvest, then (if that is not enough) down
        // to eight months' bread, to be bought back in summer at the market price.
        double floorDays = soldSafe || h.food / daily <= toHarvest + 25 ? 240 : toHarvest + 15;
        double sell = std::min(std::max(0.0, h.food - daily * floorDays), need / price);
        // A few days' grain is offered when that is all it takes (a sack for the last few pence), not only big sales.
        if (sell >= daily * 5 || (sell > 0 && sell * price >= need - 0.01)) {
            double left = (h.food - sell) / daily;
            opts.push_back("賣 " + num(std::max(1.0, sell / daily)) + " 天的糧給糧販（得 " + num(sell * price) + " 便士，倉裡剩 " + num(left) + " 天" +
                           (left < toHarvest ? "，撐不到收成，夏天得買回來" : "") + "）");
            tag.push_back(0);
        }
        // Land: a few customary acres back into the lord's hand; he lets them to someone else and the arrears go with them.
        double customary = h.acres - h.freehold;
        double acres = std::min(std::ceil(need / kSurrenderCredit), std::floor(customary * 0.25));
        if (customary >= 6 && acres >= 1) {
            opts.push_back("退回 " + num(acres) + " 畝地給領主（抵 " + num(std::min(need, acres * kSurrenderCredit)) + " 便士；往後每年少交 " +
                           num(rentOf(h) - rentOf(h, nullptr, acres)) + " 便士租，也少收那幾畝的糧）");
            tag.push_back(1);
        }
        if (h.noOxUntil <= day_) opts.push_back("賣掉耕牛（40 便士；沒有牛，收成少 12%，直到再買一頭）"), tag.push_back(2);
        Id lender = fineLender(h, need);
        double owe = 0;
        if (lender != kNone) {
            owe = opinion(lender, h.head) >= 50 || closeKin(lender, h.head) ? std::round(need) : std::round(need * 1.2);  // kin and close friends take no interest
            opts.push_back("向 " + fullName(lender) + " 借 " + num(need) + " 便士（明年米迦勒節還 " + num(owe) +
                           (owe <= std::round(need) ? "，說是不要利錢" : "，多還兩成") + "）");
            tag.push_back(3);
        }
        if (!begged && !respite && lord_ != kNone && people_[lord_].alive())
            opts.push_back("去求管家寬限一年（成不成，要看老爺對你家的臉色）"), tag.push_back(4);
        // What the books lead to: the arrears keep their years, and each Michaelmas unpaid the steward comes harder.
        // (the steward carries at most about a year's rent: as settleRent books it)
        double books = owedNow + std::min(std::max(0.0, rent - h.money), std::max(0.0, rent - owedNow));
        int next = (owedNow > 0.5 && h.creditor == lord_ ? std::max(0, h.debtYears) : 0) + 1;
        opts.push_back(respite ? "差的先欠著，寬限到後年米迦勒節（欠莊園會到 " + num(books) + " 便士）"
                               : "差的先記在莊園帳上（欠莊園會到 " + num(books) + " 便士；" +
                                     (next <= 1   ? "明年米迦勒節管家會上門來催）"
                                      : next == 2 ? "明年再交不出，管家要來牽牛抵債）"
                                                  : "明年再交不出，就要上莊園法庭算總帳）"));
        tag.push_back(5);
        // Nothing left but the books: no card for one answer (and not the no-chooser path, which asks the steward again).
        int r = opts.size() < 2 && chooser_ ? 0 : askPlayer("米迦勒節算帳", sit, opts);
        if (r < 0) {
            // No one to ask: what every house does (data/events/arcs.json for the old arrears, grain sold to 300 days).
            if (oldDue) {
                stewardOnCard_.erase(h.id);
                oldDue = false;
                runEventById("debt_steward", h.head, lord_);
            }
            if (h.money < rent - 1 && h.food > daily * 300) {
                double s = std::min(h.food - daily * 300, (rent - h.money) / price);
                h.food -= s;
                h.money += s * price;
                book(h, "賣口糧湊租", s * price);
                if (s > daily * 20) log(1, "為了交租，賣了大約 " + num(s / daily) + " 天的口糧給糧販。");
            }
            break;
        }
        int t = tag[r];
        if (t == 0) {
            h.food -= sell;
            h.money += sell * price;
            book(h, "賣口糧湊租", sell * price);
            if (floorDays > 240) soldSafe = true;
            log(1, "〔米迦勒節算帳〕" + head + " 把 " + num(std::max(1.0, sell / daily)) + " 天的口糧賣給糧販，得了 " + num(sell * price) + " 便士。");
        } else if (t == 1) {
            double moved = moveLand(h.id, kNone, acres, LandPick::CustomaryOnly, "交回領主");
            std::string names = movedText();
            double credit = std::min(need, moved * kSurrenderCredit);
            // Against this year's rent first, then against the arrears.
            double rentShort = std::max(0.0, rent - h.money);
            double onRent = std::min(credit, rentShort);
            rent -= onRent;
            reduceLordDebt(h, credit - onRent);
            rentOf(h, &why);
            if (onRent > 0.5) why += "；交回的地抵了 " + num(onRent);
            addStress(h.head, 8, "交回領主的地");
            log(1, "〔米迦勒節算帳〕" + head + " 把" + names + "交還給領主，抵了 " + num(credit) + " 便士。");
        } else if (t == 2) {
            h.noOxUntil = day_ + 40 * kDaysPerYear;  // until they can buy another
            h.money += 40;
            book(h, "賣耕牛", 40);
            log(1, "〔米迦勒節算帳〕" + head + " 把耕牛牽到秋市上賣了 40 便士，明年只能跟鄰居借牲口犁地。");
        } else if (t == 3) {
            Household& lh = households_[people_[lender].household];
            lh.money -= need;
            book(lh, "借出", -need);
            h.money += need;
            book(h, "借錢", need);
            bool fresh = h.debt <= 0.5;
            addDebt(h, owe, lender);
            if (fresh && h.creditor == lender) h.debtYears = 0;  // due at the next Michaelmas, as agreed
            oweFavour(h.head, lender, 15, "借錢交租");
            recordDeed("lent_money", lender, h.head);
            log(1, "〔米迦勒節算帳〕" + fullName(lender) + " 借了 " + num(need) + " 便士給你家交租，說好明年米迦勒節還 " + num(owe) + " 便士。");
        } else if (t == 4) {
            begged = true;
            double odds = std::clamp(0.35 + opinion(lord_, h.head) / 100 + favourOwed(lord_, h.head) / 50 - (oldDue ? 0.15 : 0), 0.05, 0.85);
            if (rngEcon_.chance(odds)) {
                log(1, "〔米迦勒節算帳〕" + head + " 去求管家寬限。管家答應了：差的" + (oldDue ? "和舊帳" : "") + "寬限一年，後年米迦勒節再算。");
                respite = true;
                begSaved = true;
                oldDue = false;
                break;
            }
            addOpinion(h.head, lord_, "no_mercy", -6, 6);
            log(1, "〔米迦勒節算帳〕" + head + " 去求管家寬限，管家把帳本一合：「規矩就是規矩。」");
        } else {
            log(1, "〔米迦勒節算帳〕" + head + (respite ? " 謝過管家，差的先欠著。" : " 交了手上有的，差的讓管家記在帳上。"));
            break;
        }
    }
    // Rent first, then whatever of the arrears that fell due the purse can still meet.
    settleRent(h, rent, respite, begSaved);
    h.lastRent = rent0;  // what the year's rent was, for next year's comparison (not what was left of it to pay)
    if (oldDue && h.money > 0.5 && owedLord(h) > 0.5) {
        double pay = reduceLordDebt(h, std::min(h.money, owedLord(h)));
        h.money -= pay;
        book(h, "還莊園的舊帳", -pay);
        if (lordHouse != kNone && lordHouse >= 0) households_[lordHouse].money += pay;
        log(1, owedLord(h) <= 0.5 ? "  欠莊園的舊帳也還清了。" : "  舊帳還了 " + num(pay) + " 便士，還欠莊園 " + num(owedLord(h)) + " 便士。");
    }
    stewardOnCard_.erase(h.id);
}

void World::payRent() {
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (auto& h : households_) {
        if (h.extinct || h.members.empty() || h.id == lordHouse) continue;
        if (h.head != kNone && people_[h.head].occupation == "priest") continue;
        // The demesne lease: its rent is set in the lease, paid with the rest at Michaelmas (rentOf).
        if (h.leaseUntil >= 0 && h.leaseUntil <= day_) {
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
            // Nobody loves the miller: a house or two grumbles that the toll is taken heavy.
            if (h.head != kNone)
                for (int k = 0; k < 2; ++k) {
                    const Household& o = households_[rngEcon_.range(0, static_cast<int>(households_.size()) - 1)];
                    if (!o.extinct && o.head != kNone && o.id != h.id && o.id != lordHouse) addOpinion(o.head, h.head, "mill_toll", -4, 3);
                }
            if (inPlayerHouse(h.id))
                log(2, "磨坊這一年收了 " + std::to_string(int(take)) + " 便士的磨麥錢，" + (h.millFree ? "抵著莊園的債，不交包租。" : "交給莊園 45 便士。"));
        } else if (h.millUntil >= 0) {
            if (inPlayerHouse(h.id)) log(1, "包下磨坊的期滿了，磨坊還給了莊園。");
            for (Id m : h.members) people_[m].flags.erase("mill_farmer");
            h.millUntil = -1;
            h.millFree = false;
        }
        std::string why;
        double rent = std::round(rentOf(h, &why));  // charged in whole pence
        if (inPlayerHouse(h.id)) explainRent(h, rent);
        // The player's house short of coin (or with old arrears left for this day): the family reckons it up.
        if (inPlayerHouse(h.id) && h.head != kNone && (h.money < rent - 1 || stewardOnCard_.count(h.id))) {
            reckonRent(h, rent, why);
            continue;
        }
        // Short of coin, a family sells grain to the corn dealer to meet the rent, keeping enough to
        // reach the spring: rent first, hunger later.
        double daily = consumption(h);
        if (h.money < rent - 1 && h.food > daily * 300) {
            double price = param("grain_price", 0.5) * kFarmGate;
            double sell = std::min(h.food - daily * 300, (rent - h.money) / price);
            h.food -= sell;
            h.money += sell * price;
            book(h, "賣口糧湊租", sell * price);
        }
        settleRent(h, rent, lordCleared_.count(h.id) > 0);
    }
    // A family without a plough ox buys one at the autumn fair once the rent is paid and coin is left.
    for (auto& h : households_) {
        if (h.extinct || h.members.empty() || h.noOxUntil <= day_) continue;
        if (inPlayerHouse(h.id)) {
            askOx(h);
            continue;
        }
        if (h.money < 110 || h.debt > 0.5) continue;
        h.money -= 60;
        book(h, "買耕牛", -60);
        h.noOxUntil = -1;
    }
}

// The player's house without an ox at the autumn fair, with the money for one: the family says.
void World::askOx(Household& h) {
    if (h.money < 60 || h.head == kNone) return;
    std::string sit = "秋天的市集上有一頭耕牛，要 60 便士。沒有牛，犁地得借鄰居的牲口，收成少 12%。交完租，手上還有 " + num(h.money) + " 便士" +
                      (owedAll(h.id) > 0.5 ? "；" + debtText(h.id) : "") + "。";
    int r = askPlayer("買耕牛", sit, {"買下這頭牛（60 便士）", "再借一年鄰居的牲口"});
    bool asked = r >= 0;
    if (r < 0) r = h.money >= 110 && h.debt <= 0.5 ? 0 : 1;  // no one to ask: as every house does
    if (r == 1) {
        if (asked) log(1, "〔買耕牛〕" + fullName(h.head) + " 在秋市上看了看牛，還是決定再借一年鄰居的牲口。");
        return;
    }
    h.money -= 60;
    book(h, "買耕牛", -60);
    h.noOxUntil = -1;
    log(1, "秋天的市集上，" + fullName(h.head) + " 花 60 便士又買了一頭耕牛。");
}

// Days to the next harvest. On harvest day itself the harvest has already been brought in (harvest() runs first): a
// year to the next.
int World::daysToHarvest() const {
    int d = (8 * kDaysPerMonth - day_ % kDaysPerYear + kDaysPerYear) % kDaysPerYear;
    return d == 0 ? kDaysPerYear : d;
}

int World::breadShort(const Household& h) const {
    double days = h.food / std::max(0.5, consumption(h));
    return std::max(0, static_cast<int>(std::ceil(daysToHarvest() - days - 1e-9)));
}

void World::ensureFood(Household& h) {
    double daily = consumption(h);
    double price = param("grain_price", 0.5);
    // Bread to the harvest and no further: grain bought in July is sold back at the farm gate in September.
    int toHarvest = daysToHarvest();
    if (daily > 0 && h.food < daily * std::min(60, toHarvest + 10) && h.money > 0 && !(inPlayerHouse(h.id) && askGrain(h))) {
        double want = daily * std::min(90, toHarvest + 15) - h.food;
        double buy = std::clamp(want, 0.0, h.money / price);
        h.food += buy;
        h.money -= buy * price;
        book(h, "買糧", -buy * price);
        if (inPlayerHouse(h.id) && buy * price >= 1)
            log(1, "家裡買了 " + num(buy / daily) + " 天的糧（" + num(buy * price) + " 便士），存糧夠吃 " + num(h.food / daily) + " 天，離秋收 " +
                       std::to_string(toHarvest) + " 天。");
    }
    if (isWinter() && h.firewood < 10 && h.money > 6) {
        h.firewood += 30;
        h.money -= 6;
        book(h, "買柴", -6);
    }
}

// The player's house running short of bread before the harvest: how to get through to the harvest is the family's to
// say, once a harvest year, and the card counts against the year's hunger budget like 米缸見底 and 斷糧.
// Returns false when no one was asked (the house then buys as every house does): no one to ask, no money to buy a
// fortnight's bread with (no market to choose between; the hunger cards carry the other ways), or hunger cards enough
// this year.
bool World::askGrain(Household& h) {
    if (h.head == kNone || actingHead() == kNone) return false;
    const double daily = consumption(h);
    const double price = param("grain_price", 0.5);
    const int toHarvest = daysToHarvest();
    const int season = (day_ + toHarvest) / kDaysPerYear;  // the harvest the bread has to last to
    const double days = h.food / daily;
    auto buy = [&](double upTo) {  // buys up to `upTo` days in the barn, as far as the purse goes; the days bought
        double b = std::clamp(daily * upTo - h.food, 0.0, h.money / price);
        h.food += b;
        h.money = std::max(0.0, h.money - b * price);
        book(h, "買糧", -b * price);
        return b;
    };
    if (h.grainAsked == season) {
        // Asked already this harvest year: what was decided holds (and before anyone starves, a month's bread is bought).
        if (h.grainChoice <= 1 || days < 15) {
            double b = buy(std::min<double>(days + 30, toHarvest + 15));
            if (b >= daily * 5)
                log(1, std::string(h.grainChoice <= 1 ? "照今年說好的，" : "眼看要斷糧，") + "家裡又買了 " + num(b / daily) + " 天的糧（" + num(b * price) + " 便士）。");
        }
        return true;
    }
    int& hungerCards = budgetUsed_[{h.id, "hunger", year()}];
    if (h.money < daily * 15 * price || hungerCards >= 2) return false;
    const double all = toHarvest + 15;
    const double costAll = std::max(0.0, daily * all - h.food) * price;
    const double monthDays = std::min(days + 30, all);
    const double costMonth = std::min(h.money, std::max(0.0, daily * monthDays - h.food) * price);
    const double borrowDays = std::min(90.0, all - days);
    const double borrowOwed = grainLoanOwed(h, borrowDays);
    std::vector<std::string> opts;
    std::vector<int> tag;
    if (h.money >= costAll) opts.push_back("買到收成（" + num(costAll) + " 便士）"), tag.push_back(0);
    else if (all - days > 45) opts.push_back("把錢都拿去買糧（" + num(h.money) + " 便士，夠吃 " + num(h.money / price / daily) + " 天）"), tag.push_back(0);
    if ((all - days > 45 || h.money < costAll) && costMonth >= daily * 25 * price)
        opts.push_back("隨吃隨買，每次買一個月（這回 " + num(costMonth) + " 便士）"), tag.push_back(1);
    opts.push_back("向鄰居借 " + num(borrowDays) + " 天的糧（秋收後還 " + num(borrowOwed) + " 便士）"), tag.push_back(2);
    opts.push_back("勒緊褲帶，一天只吃兩頓（省下半個月的糧；人會瘦，孩子最先撐不住）"), tag.push_back(3);
    std::string sit = "家裡存糧還夠 " + num(days) + " 天，離秋收還有 " + std::to_string(toHarvest) + " 天。市集上的糧，全家吃一個月要 " +
                      num(daily * 30 * price) + " 便士；手上 " + num(h.money) + " 便士" + (owedAll(h.id) > 0.5 ? "，" + debtText(h.id) : "") + "。";
    int r = askPlayer("買糧", sit, opts);
    if (r < 0) return false;
    ++hungerCards;
    h.grainAsked = season;
    h.grainChoice = tag[r];
    const std::string head = fullName(h.head);
    if (tag[r] <= 1) {
        double b = buy(tag[r] == 0 ? all : monthDays);
        log(1, "〔買糧〕" + head + " 去市集買了 " + num(b / daily) + " 天的糧（" + num(b * price) + " 便士），存糧夠吃 " + num(h.food / daily) + " 天。");
    } else if (tag[r] == 2) {
        log(1, "〔買糧〕" + head + " 拉下臉，去向鄰居借糧。");
        borrowGrain(h, h.head, borrowDays, kNone);
    } else {
        // Two meals a day: the barn lasts longer, the family grows thin, the children first.
        h.food += daily * 15;
        for (Id m : h.members) {
            Person& p = people_[m];
            p.health = std::clamp(p.health - (ageYears(p) < 10 ? 4 : 2), 0.0, 100.0);
            p.needs[Hunger] = std::clamp(p.needs[Hunger] - 30, 0.0, 100.0);
            p.needs[Spirit] = std::clamp(p.needs[Spirit] - 10, 0.0, 100.0);
        }
        addStress(h.head, 6, "勒緊褲帶過日子");
        log(1, "〔買糧〕" + head + " 家勒緊褲帶，一天只吃兩頓，省著吃到收成。");
    }
    return true;
}

// Grain lent in the lean months is paid back after the harvest at what the corn dealer pays then, and a quarter more
// for the favour: what `days` of the house's bread borrowed now will cost it at Michaelmas.
double World::grainLoanOwed(const Household& h, double days) const {
    return std::round(consumption(h) * days * param("grain_price", 0.5) * kFarmGate * 1.25);
}

// A neighbour who thinks well of the family lends grain against the next harvest (the borrow_grain effect and the
// grain card). The debt is due at the coming Michaelmas, after the harvest it was lent against.
Id World::borrowGrain(Household& h, Id asker, double days, Id cause) {
    const double debt = grainLoanOwed(h, days);
    double owed = 0;
    Id lender = seekLender(h, debt, "借糧度荒", owed);
    if (lender == kNone) {
        if (h.id == playerHousehold_) log(1, "  可是村裡沒有一家肯借，" + people_[asker].firstName + " 空著手回家。");
        return kNone;
    }
    h.food += consumption(h) * days;
    // The grain is the lender's whatever is asked back for it (given outright, it still came out of his store).
    Household& lh = households_[people_[lender].household];
    double cost = std::round(consumption(h) * days * param("grain_price", 0.5) * kFarmGate);
    lh.money -= cost;
    book(lh, owed > 0 ? "借糧給人" : "接濟別人", -cost);
    // Owed to the lender, behind the rent arrears if there are any (a third creditor's grain is let go as a kindness).
    const bool room = debtRoom(h, lender);
    if (owed > 0 && room) {
        bool fresh = h.debt <= 0.5;
        addDebt(h, owed, lender);
        if (fresh) h.debtYears = 0;
    }
    if (owed > 0) recordDeed("lent_money", lender, asker, 1, cause);  // a gift is recorded where it was given
    if (h.id == playerHousehold_)
        log(1, "  " + fullName(lender) + (owed > 0 && room ? " 借了一袋燕麥和豆子，說好秋收後還 " + num(owed) + " 便士。"
                                          : owed > 0       ? " 送來一袋燕麥和豆子：你家已經欠著別家的債，這袋就不記帳了。"
                                                           : " 送來一袋燕麥和豆子，說不用還。"));
    return lender;
}

void World::monthlyEconomy() {
    double wage = param("wage_mult", 1);
    // A trade's prices follow wages up only halfway: the smith's customers are the same village, no richer for the plague.
    double craftWage = 1 + 0.5 * (wage - 1);
    // The house's own trade earns its full rate; any other craft worked at home is a journeyman's half.
    std::map<Id, Id> master;  // household -> the one who works its main trade
    std::map<Id, double> best;
    for (const auto& p : people_) {
        if (!p.alive() || p.household == kNone || ageYears(p) < 14 || !kCrafts.count(p.occupation)) continue;
        const OccupationDef* od = defs_.occupation(p.occupation);
        if (!od || od->monthlyIncome <= 0) continue;
        double score = (p.occupation == households_[p.household].trade ? 1000 : 0) + od->monthlyIncome * (0.5 + p.skills[skillFromKey(od->skill)] / 20);
        if (!master.count(p.household) || score > best[p.household]) master[p.household] = p.id, best[p.household] = score;
    }
    for (const auto& p : people_) {
        if (!p.alive() || p.household == kNone || ageYears(p) < 14) continue;
        const OccupationDef* od = defs_.occupation(p.occupation);
        if (!od || od->monthlyIncome <= 0) continue;
        double skill = p.skills[skillFromKey(od->skill)];
        double income = od->monthlyIncome * (0.5 + skill / 20) * craftWage * std::max(0.2, 1 + traitMod(p, "income"));
        if (const FocusDef* f = focusOf(p))  // against the default, so craftsmen left alone earn what they did
            if (const FocusDef* def = focusDefault(p)) income *= 1 + f->income - def->income;
        bool craft = kCrafts.count(p.occupation) > 0;
        bool journeyman = craft && master[p.household] != p.id;
        if (journeyman) income *= 0.5;
        households_[p.household].money += income;
        if (inPlayerHouse(p.household))
            book(households_[p.household], std::string(craft ? "手藝收入" : "工錢") + "（" + shortName(p.id) + "・" + od->name + (journeyman ? "，幫工" : "") + "）", income);
    }
    // A craft house remembers its trade. While no one at home can work it (the smith died and his
    // son is a child, or only the widow is left), the shop is let to a journeyman for a share.
    for (auto& h : households_) {
        if (h.extinct || h.members.empty()) continue;
        if (h.head == kNone) continue;
        const Person& hd = people_[h.head];
        if (ageYears(hd) >= 14 && kCrafts.count(hd.occupation) && hd.occupation != "alewife") h.trade = hd.occupation;
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
        h.money += defs_.occupation(h.trade)->monthlyIncome * 0.35 * craftWage;
        book(h, "鋪子租金", defs_.occupation(h.trade)->monthlyIncome * 0.35 * craftWage);
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
        // What a month's living takes from the purse, never more than is in it (an empty purse goes without).
        auto spend = [&](double want, const std::string& why) {
            double paid = std::clamp(want, 0.0, std::max(0.0, h.money));
            h.money -= paid;
            book(h, why, -paid);
        };
        spend(h.members.size() * 0.5, "衣服、鹽、農具");
        // Better-off households spend on cloth, feasts and gifts: a share of what is put by, but a household that
        // saves hard can still save (at most 12 pence a month, enough for a feast day and a new coat).
        // A house is expected to live as its standing does: a yeoman keeps a servant, dresses his wife, feeds his guests.
        if (h.money > 150) {
            int rung = standing(h.id);
            double cap = rung >= 3 ? 40.0 : rung == 2 ? 22.0 : 12.0;
            double monthly = std::min((h.money - 150) * 0.05, cap);
            if (inPlayerHouse(h.id)) askHouseStyle(h, monthly, cap);
            if (!h.frugal) spend(monthly, "家用排場（衣料、宴客、送禮）");
        }
        // A house of standing keeps servants, and coin piled in a chest goes on the roof, the barn and the beasts:
        // money beyond what a family keeps by does not just pile up (the manor and the parsonage keep their own books).
        if (int rung = standing(h.id); rung >= 0) {
            double servants = rung >= 3 ? 3 * craftWage : 0, upkeep = std::max(0.0, h.money - 600) * 0.01;
            spend(servants, "僕人的工錢");
            spend(upkeep, rung >= 3 ? "房屋修繕" : "修房子、添農具牲口");
        }
        ensureFood(h);
        // Other houses near the end of their grain do what the player's family is asked about: the grown hire on
        // with a bigger farm for the year, and failing that the parish and the neighbours keep the children alive.
        double daily = consumption(h);
        if (!inPlayerHouse(h.id) && h.head != kNone) askToFeed(h);  // (it asks from 45 days of bread down)
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

// The house has money put by and would start living as its standing expects (or a new head finds it so, or five
// years have gone by since it was settled): the one keeping the house says whether it does, or saves instead.
void World::askHouseStyle(Household& h, double monthly, double cap) {
    Id head = actingHead();
    if (head == kNone) return;
    if (h.styleAsked >= 0 && h.styleHead == head && day_ - h.styleAsked < 5 * kDaysPerYear) return;
    bool again = h.styleAsked >= 0;
    h.styleAsked = day_;
    h.styleHead = head;
    std::string sit = "手頭攢下了 " + num(h.money) + " 便士。村裡殷實的人家，衣裳要體面，節日要請客，紅白事要送像樣的禮。照這樣過，" +
                      "每個月要花掉 150 便士以上那部分的二十分之一（眼下約 " + num(std::max(1.0, monthly)) + " 便士，最多 " + num(cap) + " 便士）。" +
                      "省著過，錢攢得快，可村裡會說你家小氣。" + (again ? "（" + std::string(h.frugal ? "這幾年家裡是省著過的。" : "這幾年家裡是照殷實人家的樣子過的。") + "）" : "");
    int r = askPlayer("家用排場", sit, {"照殷實人家的樣子過（每月約 " + num(std::max(1.0, monthly)) + " 便士）", "能省就省，錢先攢著"});
    bool frugal = r == 1;  // no one to ask: as every house of its standing does
    if (r >= 0 || !again) {
        log(1, "〔家用排場〕" + std::string(frugal ? fullName(head) + " 說錢要攢著，衣裳補一補再穿，節日也不請客。往後帳上沒有排場這一項，五年後或換了當家的再定。"
                                                    : fullName(head) + " 讓家裡照殷實人家的樣子過：衣料、宴客、送禮，存錢超過 150 便士的部分每月花掉二十分之一（最多 " +
                                                          num(cap) + " 便士），記在帳上「家用排場」一項。往後五年照這樣，換了當家的再定。") +
                   (r >= 0 ? "（選擇：" + std::string(frugal ? "能省就省" : "照殷實人家的樣子過") + "）" : ""));
    }
    if (frugal && !h.frugal) people_[head].reputation -= 2;  // 「他家攢著錢不花」
    h.frugal = frugal;
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
    double daily = consumption(h);
    // Asked while there is still time (a month and a half of bread left), not when the children are already wasting.
    if (h.food >= daily * 45) return;
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
    double spare = ph.food - consumption(ph) * 120;
    double price = param("grain_price", 0.5);
    double coin = std::round(daily * 60 * price);
    std::vector<std::string> opts;
    std::vector<int> what;
    // Helped once already and hungry again: the want is in the holding, not the season. Only a lasting way out is
    // offered from then on (the same as the season's list offers).
    bool again = feedHelped_[h.id] > 0;
    Id parcel = again ? spareParcel(h.id) : kNone;
    Id lodger = again && ph.food >= consumption(ph) * 120 && !ph.lean ? takeInWho(h.id) : kNone;
    bool hire = again && canHireKin(h.id);
    if (hire) opts.push_back("雇他家能幹活的人來當長工（管吃，每月每人 4 便士，做一年）"), what.push_back(4);
    if (parcel != kNone) opts.push_back("把" + parcelName(parcels_[parcel]) + "分給他家種（再送一個月的糧" + std::string(parcels_[parcel].free ? "；這是自由地" : "") + "）"), what.push_back(5);
    if (lodger != kNone) opts.push_back("把" + shortName(lodger) + "（" + std::to_string(int(ageYears(people_[lodger]))) + " 歲）接到家裡來住"), what.push_back(6);
    if (opts.empty()) {
        if (spare >= daily * 90) opts.push_back("從自家糧倉送去夠吃三個月的糧"), what.push_back(0);
        if (ph.money >= coin + 20) opts.push_back("送 " + std::to_string(int(coin)) + " 便士讓他們買糧"), what.push_back(1);
        if (h.debt > 0.5 && h.creditor != kNone && inPlayerHouse(people_[h.creditor].household)) opts.push_back("免了他們欠你家的債"), what.push_back(2);
    }
    if (opts.empty()) return;
    opts.push_back(again ? "這回先不管" : "自家也不寬裕，先不管");
    what.push_back(3);
    feedAskedUntil_[h.id] = day_ + kDaysPerYear / 4;
    feedAskedUntil_[kAnyHouse] = day_ + kDaysPerMonth;
    int children = 0;
    for (Id m : h.members)
        if (ageYears(people_[m]) < 12) ++children;
    std::string who = kin == h.head ? kinWord(ours, kin) + " " + fullName(h.head) + " 家"
                      : kin != kNone ? fullName(h.head) + " 家（你家的" + kinWord(ours, kin) + " " + shortName(kin) + " 在他家）" : "好朋友 " + fullName(h.head) + " 家";
    int days = static_cast<int>(h.food / std::max(1.0, daily));
    std::string sit = who + (again ? "又缺糧了：只剩 " : "只剩 ") + std::to_string(days) + " 天的糧" +
                      (children ? "，家裡有 " + std::to_string(children) + " 個孩子" : "") + "。" +
                      (again ? "上回送去的吃完了，他家的地本來就不夠一家人吃，送糧只撐得了一季。" : "");
    int r = askPlayer("親友挨餓", sit, opts);
    // No one to ask: a well-off family feeds its own kin; otherwise it keeps the house's bread.
    // Land and a new mouth at the table are the player's to give, never the rules'.
    if (r < 0) r = kin != kNone && (what.front() == 0 || what.front() == 4 || ph.money >= 300) && what.front() != 5 && what.front() != 6
                       ? 0 : static_cast<int>(opts.size()) - 1;
    int w = what[r];
    if (w == 3) {
        log(1, "〔親友挨餓〕" + sit + "家裡沒有伸手。");
        if (kin != kNone) addOpinion(kin, ours, "left_us_hungry", -10, 10);
        // Asked and refused: not asked again for a year; the lasting ways refused, not the same card for three.
        feedAskedUntil_[h.id] = day_ + (again ? kRepeatYears : 1) * kDaysPerYear;
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
    } else if (w == 2) {
        log(1, "〔親友挨餓〕" + fullName(ours) + " 把 " + fullName(h.head) + " 家欠的債一筆勾銷，讓他們先顧著吃。");
        clearDebt(h);
    } else if (w == 4) {
        log(1, "〔親友挨餓〕" + sit + "（選擇：雇他家的人當長工）" + fullName(h.head) + " 家能幹活的人搬來你家做一年，吃住都在你家。");
        hireKin(h.id, playerHousehold_);
    } else if (w == 5) {
        log(1, "〔親友挨餓〕" + sit + "（選擇：分地）" + fullName(ours) + " 把" + parcelName(parcels_[parcel]) + "分給了 " + fullName(h.head) + " 家，往後他們自己種糧。");
        giveParcelTo(parcel, h.id, ours);
    } else if (w == 6) {
        log(1, "〔親友挨餓〕" + sit + "（選擇：接進門）" + fullName(lodger) + " 搬到你家來住了。");
        takeIn(lodger, playerHousehold_, ours);
    }
    if (w <= 2) {
        ensureFood(h);
        ++feedHelped_[h.id];
    }
    // Help that came in time: nobody in the house goes to bed hungry this month (the month's health is reckoned today).
    feedHouse(h);
    if (w != 5) recordDeed("kindness", ours, h.head);  // (giving land records its own)
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
    // The two biggest holders assess the rest; the family is one of them when it holds the most land.
    Id assessor = kNone, second = kNone;
    for (const auto& o : households_) {
        if (o.extinct || o.head == kNone || o.id == lordHouse || people_[o.head].occupation == "priest") continue;
        if (assessor == kNone || o.acres > households_[people_[assessor].household].acres) second = assessor, assessor = o.head;
        else if (second == kNone || o.acres > households_[people_[second].household].acres) second = o.head;
    }
    log(2, std::string("國會准了國王一筆稅：鄉下人家照家當交十五分之一") + (war ? "，說是打法蘭西要用。" : "。"));
    for (auto& h : households_) {
        if (h.extinct || h.members.empty() || h.head == kNone || h.id == lordHouse || people_[h.head].occupation == "priest") continue;
        double tax = taxDue(h);
        if (tax <= 0) continue;
        if (inPlayerHouse(h.id) && actingHead() != kNone) {
            // A yearly institution, told by the situation the family is in this time (data/events/market.json).
            Id head = actingHead();
            bool assesses = assessor != kNone && inPlayerHouse(people_[assessor].household);
            Id other = assesses ? second : assessor;
            std::string ev = assesses ? "lay_subsidy_assessor"
                             : people_[head].flags.count("hid_tax") ? "lay_subsidy_watched"
                             : year() >= 1334 && rngEcon_.chance(0.6) ? "lay_subsidy_quota"
                                                                      : "lay_subsidy";
            runEventById(ev, head, other);
            continue;
        }
        double pay = std::min(tax, h.money);
        h.money -= pay;
        book(h, "國王的稅", -pay);
    }
}

}  // namespace sim
