// 開口 (design/contested.md 3): the head goes to someone and asks for something they hold or can decide: land,
// a vote, a loan, a match, a place for a son, a word with the lord, to stand aside. They answer by what they feel
// for the family, what they owe it, kinship, their nature, and what they are after themselves: yes, yes on a
// condition, no with the reason, or offence at being asked. A no can be pressed once more with a lever: a gift,
// a favour called in, a secret, or meeting the condition.
#include <algorithm>
#include <cmath>

#include "simcore/world.h"

namespace sim {

namespace {
std::string pence(double v) { return std::to_string(static_cast<long>(std::lround(v))) + " 便士"; }
constexpr double kAcrePrice = 64;
AskOption opt(const std::string& ask, const std::string& label, Id object = kNone, Id subject = kNone) {
    AskOption o;
    o.ask = ask;
    o.label = label;
    o.object = object;
    o.subject = subject;
    return o;
}  // what a customary acre fetched between neighbours (data/plans.json acquire_land)
}  // namespace

std::vector<AskOption> World::asksFor(Id target) {
    std::vector<AskOption> out;
    Id head = playerHead();
    if (head == kNone || target == kNone || target == head) return out;
    const Person& hp = people_[head];
    const Person& t = people_[target];
    if (!t.alive() || t.household == kNone || inPlayerHouse(t.household) || ageYears(t) < 16 || ageYears(hp) < 16) return out;
    const Household& th = households_[t.household];
    const Household& ph = households_[playerHousehold_];
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    bool isHead = th.head == target;
    auto push = [&](AskOption o) {
        auto cd = asked_.find({target, o.ask, o.object});
        if (o.open && cd != asked_.end() && cd->second > day_) o.open = false, o.why = "這一季剛問過";
        if (o.open && timeLeft() < 1) o.open = false, o.why = "這一季沒空了";
        out.push_back(std::move(o));
    };
    auto surname = [&](Id h) { return h != kNone && households_[h].dynasty != kNone ? dynasties_[households_[h].dynasty].surname : std::string(); };
    double fineRate = worldFlags_.count("post_plague") ? 1.5 : 3;

    // Land: from a neighbour's holding, or a vacant parcel from the lord.
    if (target == lord_) {
        for (const auto& pc : parcels_) {
            if (pc.gone || pc.holder != kNone) continue;
            double fine = pc.acres * fineRate;
            AskOption o = opt("rent_vacant", "請領主把" + parcelName(pc) + "租給你（入地費 " + pence(fine) + "）", pc.id);
            if (ph.money < fine) o.open = false, o.why = "錢不夠";
            push(o);
        }
    } else if (isHead && th.id != lordHouse) {
        for (const auto& pc : parcels_) {
            if (pc.gone || pc.holder != th.id) continue;
            double price = std::round(pc.acres * kAcrePrice);
            AskOption o = opt("sell_parcel", "請他把" + parcelName(pc) + "讓給你（" + pence(price) + "）", pc.id);
            if (ph.money < price) o.open = false, o.why = "錢不夠（要 " + pence(price) + "）";
            push(o);
        }
    }
    // A vote: for the posts the head could stand for.
    if (isHead && target != lord_ && t.occupation != "priest" && ageYears(t) >= 21 && hp.sex == Sex::Male) {
        for (const auto& of : offices_) {
            bool can = officeEligible(head, of.id) && (of.id != "reeve" || year() + 1 >= nextReeveYear_);
            if (!can || of.holder == head) continue;
            AskOption o = opt("vote", "請他選" + of.name + "時投你一票");
            o.office = of.id;
            auto pr = promises_.find({target, of.id});
            if (pr != promises_.end() && pr->second == head) o.open = false, o.why = "已經答應你了";
            push(o);
        }
    }
    // A loan.
    if (isHead && th.money >= 100 && th.debt <= 0.5 && target != lord_ && t.occupation != "priest") {
        double amount = th.money >= 300 ? 60 : 40;
        AskOption o = opt("lend", "向他借 " + pence(amount));
        o.object = static_cast<Id>(amount);
        if (ph.debt > 0.5) o.open = false, o.why = "你家還欠著債";
        push(o);
    }
    // A match between one of theirs and one of ours.
    auto single = [&](const Person& x) { return x.alive() && !(x.spouse != kNone && people_[x.spouse].alive()) && x.home == kNone; };
    for (Id x : t.children) {
        const Person& xp = people_[x];
        if (!single(xp) || xp.household != t.household || xp.occupation == "priest") continue;
        double xa = ageYears(xp);
        if (xa < 16) continue;
        for (Id y : ph.members) {
            const Person& yp = people_[y];
            if (!single(yp) || yp.sex == xp.sex || y == head || closeKin(x, y) || gentry(x) != gentry(y)) continue;
            double ya = ageYears(yp);
            if (ya < 16 || (yp.father != head && yp.mother != head)) continue;
            double gap = yp.sex == Sex::Male ? ya - xa : xa - ya;
            if (gap < -4 || gap > 14) continue;
            push(opt("match", "替" + shortName(y) + "向" + shortName(x) + "提親", x, y));
        }
    }
    // A place for a son with this master.
    if (isHead && placeOpen(th.id, "apprentice"))
        for (Id c : hp.children) {
            const Person& cp = people_[c];
            if (!cp.alive() || !inPlayerHouse(cp.household) || cp.sex != Sex::Male || cp.flags.count("trade_decided")) continue;
            double a = ageYears(cp);
            if (a < 11 || a > 14) continue;
            AskOption o = opt("apprentice", "請他收" + shortName(c) + "當學徒（拜師錢 24 便士）", th.id, c);
            if (ph.money < 24) o.open = false, o.why = "錢不夠";
            push(o);
        }
    // A word with the lord, from the reeve or the priest.
    if (target != lord_ && (t.flags.count("reeve") || target == priest_) && lord_ != kNone && people_[lord_].alive())
        push(opt("speak", "請他在領主面前替你說句話"));
    // To stand aside from something both houses are after.
    for (const auto& wt : wants_) {
        if (!wt.open || wt.house != th.id || !isHead) continue;
        bool both = false;
        for (const auto& mine : wants_)
            if (mine.open && inPlayerHouse(mine.house) && mine.kind == wt.kind &&
                (wt.kind == "office" ? mine.office == wt.office : mine.object == wt.object))
                both = true;
        if (!both) continue;
        AskOption o = opt("stand_aside", "請他別跟你搶" + wantThing(wt), wt.id);
        push(o);
    }
    (void)surname;
    return out;
}

void World::ask(Id who, const std::string& what, Id object, Id subject, const std::string& officeId, const std::string& lever) {
    Id head = playerHead();
    if (head == kNone || who == kNone) return;
    Person& t = people_[who];
    std::vector<AskOption> opts = asksFor(who);
    const AskOption* opt = nullptr;
    for (const auto& o : opts)
        if (o.ask == what && o.object == object && o.subject == subject && o.office == officeId) opt = &o;
    bool again = !lever.empty();
    if (again) {
        // Pressing the answer just given: in the same season, with a lever offered, and another free day
        // (taken below, once the lever is known to work).
        if (lastAsk_.who != who || lastAsk_.ask != what || lastAsk_.object != object || lastAsk_.subject != subject ||
            lastAsk_.season != year() * 4 + month() / 3 ||
            std::find(lastAsk_.levers.begin(), lastAsk_.levers.end(), lever) == lastAsk_.levers.end())
            return;
        if (!opt) return;
    } else {
        if (!opt || !opt->open || !spendTime(1)) return;
    }
    AskOption o = *opt;
    Household& ph = households_[playerHousehold_];
    Household& th = households_[t.household];
    const Id thId = th.id;  // marry() below can add a household and move households_
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    auto surname = [&](Id h) { return h != kNone && households_[h].dynasty != kNone ? dynasties_[households_[h].dynasty].surname : std::string(); };
    double fineRate = worldFlags_.count("post_plague") ? 1.5 : 3;

    // What the family is asking for, it is now known to want: word of it goes round.
    if (!again) {
        if (what == "sell_parcel" || what == "rent_vacant") addWant(playerHousehold_, "parcel", object, kNone, "", "開口要了");
        else if (what == "vote") addWant(playerHousehold_, "office", kNone, head, officeId, "開口拉票");
        else if (what == "match") addWant(playerHousehold_, "match", object, subject, "", "開口提親");
        else if (what == "apprentice") addWant(playerHousehold_, "place", object, subject, "apprentice", "開口求師");
    }

    // ---- How they weigh it.
    struct Factor {
        double v;
        std::string text;
    };
    std::vector<Factor> fs;
    double op = opinion(who, head);
    fs.push_back({op * 0.6, op >= 20 ? "看在兩家的交情上" : op <= -15 ? "我們兩家沒那個交情" : ""});
    double owed = favourOwed(who, head);
    if (owed >= 3) fs.push_back({owed, "上回你幫過我"});
    double weOwe = favourOwed(head, who);
    if (weOwe >= 10) fs.push_back({-weOwe * 0.5, "上回的人情你家還沒還"});
    if (closeKin(who, head)) fs.push_back({15, "都是自家人"});
    if (feudStage(who, head) >= 2) fs.push_back({-40, "我們兩家的仇還沒了"});
    if (hasTrait(t, "generous")) fs.push_back({8, ""});
    if (hasTrait(t, "kind")) fs.push_back({4, ""});
    if (hasTrait(t, "greedy")) fs.push_back({-6, ""});
    if (hasTrait(t, "cynical")) fs.push_back({-4, ""});
    bool hardNo = false;
    std::string hardWhy;
    double big = 0;  // how much is being asked, for taking offence
    std::string demand;
    double demandMoney = 0;
    Id demandParcel = kNone;
    std::string demandOffice;
    // A rival for the same thing whom they like better.
    auto rivalFavoured = [&](const std::string& kind, Id obj, const std::string& off) -> std::string {
        for (Id r : rivalsFor(kind, obj, off, playerHousehold_)) {
            if (r == th.id || households_[r].head == kNone) continue;
            if (opinion(who, households_[r].head) > op + 10) return surname(r);
        }
        return "";
    };
    double price = 0;
    if (what == "sell_parcel") {
        const Parcel& pc = parcels_[object];
        price = std::round(pc.acres * kAcrePrice);
        double share = th.acres > 0 ? pc.acres / th.acres : 1;
        int sons = 0;
        for (Id m : th.members) sons += people_[m].sex == Sex::Male && people_[m].father == who;
        fs.push_back({-(30 + share * 80), share > 0.4 ? "這塊地是我家一大半的口糧" : ""});
        if (sons >= 2) fs.push_back({-15, "兒子們還指望這塊地"});
        if (pc.how == "祖傳") fs.push_back({-10, "這是祖上傳下來的地"});
        if (th.debt > 0.5) fs.push_back({25, "家裡正缺錢還債"});
        else if (th.lean) fs.push_back({10, "今年手頭緊"});
        std::string r = rivalFavoured("parcel", object, "");
        if (!r.empty()) fs.push_back({-20, "我已經答應" + r + "家了"});
        big = 40;
    } else if (what == "rent_vacant") {
        const Parcel& pc = parcels_[object];
        price = pc.acres * fineRate;
        fs.push_back({-5, ""});
        fs.push_back({ph.money / 20, ""});
        std::string r = rivalFavoured("parcel", object, "");
        if (!r.empty()) fs.push_back({-25, "這塊地我已經答應租給" + r + "家了"});
        big = 10;
    } else if (what == "vote") {
        fs.push_back({-10, ""});
        if (wantOf(th.id, "office", kNone, officeId)) hardNo = true, hardWhy = "我家自己也要選";
        auto pr = promises_.find({who, officeId});
        if (pr != promises_.end() && pr->second != head) fs.push_back({-30, "我已經答應投給" + fullName(pr->second) + "了"});
        std::string r = rivalFavoured("office", kNone, officeId);
        if (!r.empty()) fs.push_back({-15, r + "家也找過我"});
        big = 5;
    } else if (what == "lend") {
        double amount = object;
        fs.push_back({-(15 + amount / 4 - th.money / 25), "手頭也不寬裕"});
        if (ph.debt > 0.5 || ph.lean) fs.push_back({-10, "怕你家還不起"});
        big = 20;
    } else if (what == "match") {
        const Household& oh = households_[playerHousehold_];
        double theirs = th.acres + th.money / 30 + t.reputation, ours = oh.acres + oh.money / 30 + people_[head].reputation;
        fs.push_back({-15 - std::max(0.0, theirs - ours) / 3, theirs > ours * 1.4 ? "我家的孩子不愁嫁娶" : ""});
        fs.push_back({opinion(who, subject) * 0.3, ""});
        fs.push_back({opinion(object, subject) * 0.3, opinion(object, subject) >= 30 ? "孩子自己也願意" : ""});
        for (const auto& wt : wants_)
            if (wt.open && wt.house == th.id && wt.kind == "match" && wt.subject == object) {
                if (wt.object == subject) fs.push_back({40, "正合我意"});
                else fs.push_back({-20, "已經在跟" + surname(people_[wt.object].household) + "家談了"});
            }
        std::string r = rivalFavoured("match", object, "");
        if (!r.empty()) fs.push_back({-15, r + "家也來提過親"});
        big = 30;
    } else if (what == "apprentice") {
        price = 24;
        fs.push_back({-10, ""});
        fs.push_back({people_[subject].skills[Craft] * 2 + people_[subject].skills[Charm], ""});
        std::string r = rivalFavoured("place", object, "apprentice");
        if (!r.empty()) fs.push_back({-15, "我已經答應收" + r + "家的孩子了"});
        big = 10;
    } else if (what == "speak") {
        fs.push_back({-20, "領主那裡不好開口"});
        big = 15;
    } else if (what == "stand_aside") {
        const Want& wt = wants_[object];
        fs.push_back({-30.0 - (hasTrait(t, "ambitious") ? 10 : 0) + (hasTrait(t, "content") ? 10 : 0), wt.why.empty() ? "我家也等著要" : "我家" + wt.why});
        big = 30;
    }
    // What they would take in return: something they are after that the family holds, a vote, or money.
    for (const auto& wt : wants_) {
        if (!wt.open || wt.house != th.id || !demand.empty()) continue;
        if (wt.kind == "parcel" && inPlayerHouse(parcels_[wt.object].holder) && wt.object != object && what != "rent_vacant") {
            demand = "那你把" + parcelName(parcels_[wt.object]) + "讓給我";
            demandParcel = wt.object;
        } else if (wt.kind == "office" && what != "vote" && households_[playerHousehold_].head != kNone) {
            const Office* of = office(wt.office);
            if (of) demand = "選" + of->name + "時你家得投我", demandOffice = wt.office;
        }
    }
    if (demand.empty() && (what == "sell_parcel" || what == "lend" || what == "match" || what == "apprentice")) {
        demandMoney = what == "sell_parcel" ? std::round(price * 0.25) : what == "lend" ? std::round(object * 0.2) : what == "match" ? 30 : 12;
        demand = what == "lend" ? "到時候多還 " + pence(demandMoney) : what == "match" ? "嫁妝（聘禮）得給 " + pence(demandMoney) : "再加 " + pence(demandMoney);
        if (what != "lend" && ph.money < price + demandMoney) demand.clear(), demandMoney = 0;
    }
    if (what == "rent_vacant" && ph.money >= price + 10) {
        // The steward's tea money replaces whatever else they were going to ask for.
        demand = "再給管家 " + pence(10) + "茶水錢", demandMoney = 10;
        demandParcel = kNone;
        demandOffice.clear();
    }

    // The lever, if pressing a first answer.
    std::string leverNote;
    // What a yes costs the family outright, so a gift never spends the money the deal itself needs.
    const double dealCost = what == "sell_parcel" || what == "rent_vacant" || what == "apprentice" ? price : 0;
    const double giftSum = std::max(12.0, std::round(price * 0.1));
    // What pressing with this lever takes from the family if it works: the deal, the gift, and a met condition's
    // money (a loan's condition is paid later, when it is repaid). A press the family cannot pay for is never made,
    // so it never takes the day only to end in "等你家湊夠".
    const double pressCost = dealCost + (lever == "gift" ? giftSum : 0) +
                             (lever == "meet" && what != "lend" ? lastAsk_.demandMoney : 0);
    auto canMeet = [&] {
        return lastAsk_.answer == 2 &&
               (lastAsk_.demandParcel == kNone || inPlayerHouse(parcels_[lastAsk_.demandParcel].holder));
    };
    if (again) {
        // Every way a press can fail is checked before it takes the day.
        bool ok = leverOpen(lever) && ph.money + 0.01 >= pressCost && (lever != "meet" || canMeet());
        if (!ok || !spendTime(1)) return;
    }
    if (lever == "gift") {
        double g = giftSum;
        if (ph.money + 0.01 < pressCost) return;
        ph.money -= g;
        th.money += g;
        book(ph, "送禮", -g);
        fs.push_back({18, "收了你的禮"});
        addOpinion(who, head, "gift", 3, 3);
        leverNote = "送上 " + pence(g) + "的禮，";
    } else if (lever == "favour") {
        fs.push_back({favourOwed(who, head) * 2 + 15, "欠你的人情總要還"});
        callInFavour(who, head);
        leverNote = "提起上回幫過的忙，";
    } else if (lever == "hook") {
        Id deed = hookDeed(head, who);
        if (deed == kNone) return;
        for (auto& b : beliefs_[head])
            if (b.deed == deed && b.culprit == who) b.used = true;
        fs.push_back({80, ""});
        addOpinion(who, head, "pressed_with_secret", -25, 10);
        hardNo = false;
        leverNote = "低聲提起那件見不得人的事，";
    } else if (lever == "meet") {
        if (!canMeet() || ph.money + 0.01 < pressCost) return;
        demand = lastAsk_.demand;
        demandMoney = lastAsk_.demandMoney;
        demandParcel = lastAsk_.demandParcel;
        demandOffice = lastAsk_.demandOffice;
        fs.push_back({100, ""});
        leverNote = "答應了" + shortName(who) + "的條件，";
    }

    double s = 0;
    for (const auto& f : fs) s += f.v;
    int answer;
    if (hardNo && lever != "hook") answer = 3;
    else if (s >= 15) answer = 1;
    else if (s >= -12 && !demand.empty() && lever != "meet") answer = 2;
    else if (s < -45 && op < 0 && (big >= 30 || hasTrait(t, "wrathful"))) answer = 4;
    else answer = 3;
    // The person answers for themselves when the model is deciding for them: among what the rules allow.
    if (lever.empty() && !hardNo && answer != 4) {
        std::vector<std::string> options{"答應", "拒絕"};
        std::vector<double> weights{std::max(0.05, (s + 30) / 45), std::max(0.05, (15 - s) / 45)};
        if (!demand.empty()) options.push_back("開條件：" + demand), weights.push_back(0.6);
        int fb = answer == 1 ? 0 : answer == 2 ? 2 : 1;
        int r = npcDecide(t, "有人開口", fullName(head) + " 來找" + t.firstName + "：" + o.label + "。", options, weights, {head}, fb);
        if (r == 0) answer = 1;
        else if (r == 1) answer = 3;
        else if (r == 2) answer = 2;
    }
    std::sort(fs.begin(), fs.end(), [](const Factor& a, const Factor& b) { return a.v < b.v; });
    std::string why, plus;
    for (const auto& f : fs)
        if (!f.text.empty() && f.v < 0) {
            why = f.text;
            break;
        }
    for (auto it = fs.rbegin(); it != fs.rend(); ++it)
        if (!it->text.empty() && it->v > 0) {
            plus = it->text;
            break;
        }
    if (hardNo) why = hardWhy;
    if (why.empty()) {
        static const std::map<std::string, std::string> plain{
            {"rent_vacant", "那塊地我還沒想好租給誰"}, {"vote", "我還沒想好要投誰"}, {"lend", "我家的錢另有用處"},
            {"match", "孩子的親事我還想再看看"}, {"apprentice", "我這裡眼下不缺人手"}, {"speak", "領主那裡我不好開口"},
            {"sell_parcel", "這塊地我不想賣"}, {"stand_aside", "我家也等著要"}};
        auto it = plain.find(what);
        if (it != plain.end()) why = it->second;
    }

    // A yes the family cannot pay for is no deal: never spend money it does not have.
    const double payNow = dealCost + (lever == "meet" && what != "lend" ? demandMoney : 0);
    if (answer == 1 && payNow > households_[playerHousehold_].money + 0.01) {
        answer = 3;
        why = "等你家湊夠 " + pence(payNow) + "再說";
    }

    AskResult res;
    res.day = day_;
    res.season = year() * 4 + month() / 3;
    res.who = who;
    res.ask = what;
    res.office = officeId;
    res.object = object;
    res.subject = subject;
    res.answer = answer;
    std::string name = shortName(who);
    if (answer == 1) {
        res.text = name + "點頭了：「" + (plus.empty() ? "好吧" : plus + "，好吧") + "。」";
        if (what == "sell_parcel") {
            double pay = price + (lever == "meet" ? demandMoney : 0);
            ph.money -= pay;
            th.money += pay;
            book(ph, "買地", -pay);
            res.text += "你家用 " + pence(pay) + "買下了" + parcelName(parcels_[object]) + "。";
            moveParcel(object, playerHousehold_, "買來");
            oweFavour(head, who, 6, "讓地給你家");
        } else if (what == "rent_vacant") {
            double pay = price + (lever == "meet" ? demandMoney : 0);
            ph.money -= pay;
            if (lordHouse != kNone) households_[lordHouse].money += pay;
            book(ph, "入地費", -pay);
            res.text += parcelName(parcels_[object]) + "租給了你家。";
            moveParcel(object, playerHousehold_, "向領主租來");
        } else if (what == "vote") {
            promises_[{who, officeId}] = head;
            const Office* of = office(officeId);
            res.text += "選" + (of ? of->name : officeId) + "時會投你。";
            oweFavour(head, who, 6, "答應投你一票");
        } else if (what == "lend") {
            double amount = object;
            th.money -= amount;
            ph.money += amount;
            book(ph, "借錢", amount);
            addDebt(ph, amount + (lever == "meet" ? demandMoney : 0), who);
            oweFavour(head, who, 10, "借錢給你家");
            res.text += "借了你家 " + pence(amount) + "。";
        } else if (what == "match") {
            if (lever == "meet" && demandMoney > 0) {
                ph.money -= demandMoney;
                th.money += demandMoney;
                book(ph, "聘禮嫁妝", -demandMoney);
            }
            Id man = people_[object].sex == Sex::Male ? object : subject;
            Id woman = man == object ? subject : object;
            res.text += shortName(subject) + "和" + shortName(object) + "的親事說定了。";
            log(1, "〔開口〕" + res.text);
            marry(man, woman);
            oweFavour(head, who, 8, "答應了親事");
        } else if (what == "apprentice") {
            double pay = 24 + (lever == "meet" ? demandMoney : 0);
            ph.money -= pay;
            th.money += pay;
            book(ph, "拜師錢", -pay);
            takePlace(object, "apprentice", subject);
            res.text += shortName(subject) + "跟著" + name + "學手藝去了。";
            oweFavour(head, who, 8, "收你兒子當學徒");
        } else if (what == "speak") {
            if (EntryFine* f = fineOf(playerHousehold_)) {
                f->amount = std::round(f->amount * 0.7);
                res.text += "領主看在" + name + "的面子上，入地費減到 " + pence(f->amount) + "。";
            } else {
                addOpinion(lord_, head, "spoken_for", 8, 6);
                res.text += name + "在領主面前替你家說了好話。";
            }
            oweFavour(head, who, 10, "在領主面前替你說話");
        } else if (what == "stand_aside") {
            Want& wt = wants_[object];
            std::string thing = wantThing(wt);
            dropWant(wt, "讓給了你家");
            res.text += name + "家不跟你搶" + thing + "了。";
            oweFavour(head, who, 20, "讓你一步");
            addOpinion(head, who, "stood_aside", 6, 8);
        }
        if (lever == "meet") {
            if (demandParcel != kNone) {
                std::string pn = parcelName(parcels_[demandParcel]);
                // marry() above may have merged their house into the family's: then the parcel already sits there.
                Id to = !households_[thId].extinct ? thId : people_[who].household;
                if (to != kNone && !inPlayerHouse(to)) moveParcel(demandParcel, to, "換來");
                res.text += "你家把" + pn + "讓了出去。";
            }
            if (!demandOffice.empty()) promises_[{head, demandOffice}] = who;
        }
        addOpinion(head, who, "granted_ask", 5, 6);
        asked_[{who, what, object}] = day_ + 90;
    } else if (answer == 2) {
        res.text = name + "想了想：「行是行，" + demand + "。」";
        res.demand = demand;
        res.demandMoney = demandMoney;
        res.demandParcel = demandParcel;
        res.demandOffice = demandOffice;
        res.dealCost = dealCost;
        if (lever.empty()) res.levers.push_back("meet");  // a press is the one more try: its condition is not pressed again
    } else if (answer == 3) {
        res.text = name + "搖了搖頭：「" + why + "。」";
        asked_[{who, what, object}] = day_ + 90;
    } else {
        res.text = name + "沉下臉來：「你也太看得起自己了！」";
        addOpinion(who, head, "asked_too_much", -8, 4);
        asked_[{who, what, object}] = day_ + 180;
    }
    if (!leverNote.empty()) res.text = "你" + leverNote + res.text;
    // A first no or a condition can be pressed once more, by whatever the family has to press with.
    if (lever.empty() && (answer == 2 || answer == 3)) {
        res.dealCost = dealCost;
        res.giftSum = giftSum;
        if (households_[playerHousehold_].money >= giftSum + dealCost) res.levers.push_back("gift");
        if (favourOwed(who, head) >= 5) res.levers.push_back("favour");
        if (hookDeed(head, who) != kNone) res.levers.push_back("hook");
    }
    if (!(answer == 1 && what == "match")) log(1, "〔開口〕" + o.label.substr(0, o.label.find("（")) + "。" + res.text);
    if (!pendingReason_.empty()) {
        log(1, "  （" + pendingReason_ + "）");
        pendingReason_.clear();
    }
    seasonNotes_.push_back("你找" + name + "開口：" + res.text);
    t.soughtDay = day_;
    befriend(who, head, 6);
    lastAsk_ = res;
    ++counters_["ask:" + what + ":" + std::to_string(answer)];
}

bool World::leverOpen(const std::string& lever) const {
    const AskResult& a = lastAsk_;
    if (a.who == kNone || playerHousehold_ == kNone || timeLeft() <= 0 || a.season != year() * 4 + month() / 3) return false;
    if (std::find(a.levers.begin(), a.levers.end(), lever) == a.levers.end()) return false;
    double cost = a.dealCost + (lever == "gift" ? a.giftSum : 0) + (lever == "meet" && a.ask != "lend" ? a.demandMoney : 0);
    if (households_[playerHousehold_].money + 0.01 < cost) return false;
    if (lever == "hook") return hookDeed(playerHead(), a.who) != kNone;
    if (lever == "meet") return a.answer == 2 && (a.demandParcel == kNone || inPlayerHouse(parcels_[a.demandParcel].holder));
    return true;
}

}  // namespace sim
