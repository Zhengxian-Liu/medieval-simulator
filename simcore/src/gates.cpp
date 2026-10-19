// What stands between the family and a plan (design/fun-r6.md A): each condition in turn, met (✓) or not (✗), in the
// player's words. Every gate is the very check that decides (EventRunner::evalKey on one key of the plan's JSON, the
// cooldown, the slots, the people availablePlans would offer), so what is shown and what blocks never disagree.
#include <algorithm>
#include <cmath>

#include "simcore/events.h"

namespace sim {

namespace {
std::string num(double v) { return std::to_string(static_cast<long>(std::lround(v))); }
std::string range(const Json& v) {
    if (v.isNumber()) return num(v.asNumber());
    return num(v[0].asNumber()) + " 到 " + num(v[1].asNumber());
}
const char* occName(const Defs& d, const std::string& id) {
    if (const OccupationDef* o = d.occupation(id)) return o->name.c_str();
    return id == "monk" ? "修士" : id == "nun" ? "修女" : id.c_str();
}
std::string occList(const Defs& d, const Json& v) {
    if (v.isString()) return occName(d, v.asString());
    std::string out;
    for (const auto& o : v.asArray()) out += (out.empty() ? "" : "、") + std::string(occName(d, o.asString()));
    return out;
}
}  // namespace

std::string World::seasonOf(int day) const {
    static const char* names[4] = {"冬末", "春", "夏收", "秋"};
    int m = (day % kDaysPerYear) / kDaysPerMonth;
    return std::to_string(cfg_.startYear + day / kDaysPerYear) + " 年" + names[(m / 3) % 4];
}

// One key of a condition, as a gate: what it asks (label), whether it holds (the same evalKey that decides), and how it
// stands for this person when it does not (detail). quiet: a condition met by nearly everyone (not the lord, not the
// priest), shown only when it is the one that bars.
OfficeGate EventRunner::gate(const std::string& k, const Json& v, const Person& p, Id target, bool* quiet) const {
    const World& w = w_;
    const Defs& d = w.defs_;
    OfficeGate g;
    g.ok = evalKey(k, v, p, target);
    if (quiet) *quiet = false;
    const std::string who = w.shortName(p.id);
    Id hid = p.household != kNone ? p.household : w.isAway(p) ? p.awayFrom : kNone;
    const Household* h = hid != kNone ? &w.households_[hid] : nullptr;
    auto yes = [&](const Json& x) { return !x.isBool() || x.asBool(); };
    auto set = [&](const std::string& label, const std::string& detail) {
        g.label = label;
        g.detail = g.ok ? label : detail;
    };
    auto hush = [&] { if (quiet) *quiet = true; };
    const double money = h ? h->money : 0;
    if (k == "age") {
        double a = w.ageYears(p);
        int y = static_cast<int>(a);
        double lo = v.isNumber() ? v.asNumber() : v[0].asNumber(), hi = v.isNumber() ? v.asNumber() : v[1].asNumber();
        set("年紀 " + range(v) + " 歲",
            a < lo ? who + " " + std::to_string(y) + " 歲，還要 " + std::to_string(std::max(1, static_cast<int>(std::ceil(lo - a)))) + " 年才滿 " + num(lo) + " 歲（要 " + range(v) + " 歲）"
                   : who + "已經 " + std::to_string(y) + " 歲（要 " + range(v) + " 歲）");
        if (a > hi && hi >= 60) g.detail = who + "已經 " + std::to_string(y) + " 歲，年紀太大了（要 " + range(v) + " 歲）";
    } else if (k == "married") {
        set(yes(v) ? "已婚" : "沒有成親", yes(v) ? who + "沒有成親" : who + "已經成親了");
    } else if (k == "is_head") {
        Id hh = h ? h->head : kNone;
        set(yes(v) ? "是一家之主" : "不是一家之主",
            yes(v) ? who + "不是一家之主" + (hh != kNone ? "（家主是 " + w.shortName(hh) + "）" : std::string()) : who + "自己當著家");
    } else if (k == "money_ge") {
        set("存錢 " + num(v.asNumber()) + " 便士以上", "存錢 " + num(money) + "／要 " + num(v.asNumber()) + " 便士");
    } else if (k == "money_lt") {
        set("存錢不到 " + num(v.asNumber()) + " 便士", "手上有 " + num(money) + " 便士，不缺這份錢（要不到 " + num(v.asNumber()) + "）");
    } else if (k == "acres_ge") {
        set("地 " + num(v.asNumber()) + " 畝以上", "地 " + num(h ? h->acres : 0) + "／要 " + num(v.asNumber()) + " 畝");
    } else if (k == "acres_lt") {
        set("田不到 " + num(v.asNumber()) + " 畝", "已經有 " + num(h ? h->acres : 0) + " 畝地了");
    } else if (k == "customary_ge") {
        set("佃來的地 " + num(v.asNumber()) + " 畝以上", "佃來的地 " + num(h ? h->acres - h->freehold : 0) + "／要 " + num(v.asNumber()) + " 畝");
    } else if (k == "freehold_ge") {
        set("自由地 " + num(v.asNumber()) + " 畝以上", "自由地 " + num(h ? h->freehold : 0) + "／要 " + num(v.asNumber()) + " 畝");
    } else if (k == "free") {
        set(yes(v) ? "自由身" : "農奴身分", yes(v) ? "你家還是農奴" : "你家已經是自由人了");
    } else if (k == "no_flag" || k == "flag") {
        const std::string f = v.asString();
        bool want = k == "flag";
        if (f == "lord_favour") set(want ? "得到了領主青睞" : "還沒得到領主青睞", want ? "領主還沒把你家當自己人" : "領主已經把你家當自己人了（「討好領主」成過）");
        else if (f == "study_failed") set("沒有試過認字又學不成", who + "試過跟神父認字，沒學成");
        else if (f == "demesne_farmer") set("沒包著領主的自營地", "你家已經包著領主的自營地");
        else if (f == "mill_farmer") set("沒包著磨坊", "你家已經包著磨坊");
        else if (f == "schooled") set("還沒讀過書", who + "已經讀過書了");
        else set(want ? f : "不是「" + f + "」", want ? who + "不是「" + f + "」" : who + "已經是「" + f + "」");
    } else if (k == "trait" || k == "no_trait") {
        const TraitDef* td = d.trait(v.asString());
        std::string tn = td ? td->name : v.asString();
        if (k == "trait") set(tn, who + "不" + (tn == "識字" ? "識字" : "是「" + tn + "」的人"));
        else set(tn == "識字" ? "不識字" : "不是「" + tn + "」", who + (tn == "識字" ? "已經識字了" : "是「" + tn + "」的人"));
    } else if (k == "sex") {
        set(v.asString() == "male" ? "男的" : "女的", who + "是" + (p.sex == Sex::Male ? "男的" : "女的"));
    } else if (k == "occupation") {
        set("是" + occList(d, v), who + "不是" + occList(d, v));
    } else if (k == "not") {
        // Negations: what one must not be. Spelled out for the usual ones; anything else is quiet.
        if (v.isObject() && v.has("occupation") && v.asObject().size() == 1) {
            set("不是" + occList(d, v["occupation"]), who + "是" + occName(d, p.occupation));
            hush();
        } else if (v.isObject() && v.has("trait") && v.asObject().size() == 1) {
            const TraitDef* td = d.trait(v["trait"].asString());
            std::string tn = td ? td->name : v["trait"].asString();
            set("不是「" + tn + "」", who + "是「" + tn + "」的人");
        } else if (v.isObject() && v.has("remembers")) {
            set("最近沒有對他家下過手", "最近才對這家下過手");
            hush();
        } else {
            set("另有條件", "不合條件");
            hush();
        }
    } else if (k == "any") {
        std::string alts;
        for (const auto& c : v.asArray()) {
            std::string one = describe(c, p, target);
            if (!one.empty()) alts += (alts.empty() ? "" : "，或") + one;
        }
        set(alts.empty() ? "合其中一樣" : alts, "這幾樣都不合（" + alts + "）");
    } else if (k == "service_place") {
        set(yes(v) ? "家裡還有幫傭的空位" : "家裡沒有空位", yes(v) ? "家裡的幫傭名額滿了" : "還有空位");
    } else if (k == "has_lover") {
        set(yes(v) ? "有相好" : "沒有相好", yes(v) ? who + "沒有相好" : who + "已經有相好了");
    } else if (k == "has_hook") {
        set("手上握有別人的把柄", "手上沒有誰的把柄（打聽、撞見了什麼才會有）");
    } else if (k == "hooked") {
        set("你手上有對方的把柄", "手上沒有對方的把柄");
    } else if (k == "sworn_friends_lt") {
        set("交心的朋友不到 " + num(v.asNumber()) + " 個", who + "已經有 " + std::to_string(w.swornFriends(p.id)) + " 個交心的朋友");
    } else if (k == "unsolved_secret") {
        set("家裡吃過還沒查清的暗虧", "家裡沒有還沒查清的暗虧");
    } else if (k == "gentry") {
        set(yes(v) ? "領主家的人" : "不是領主家的人", yes(v) ? who + "不是領主家的人" : who + "是領主家的人");
        if (!yes(v)) hush();
    } else if (k == "player_family") {
        set("是你家的人", "不是你家的人");
        hush();
    } else if (k == "standing_ge") {
        int now = h ? w.standing(h->id) : 0;
        set("家族地位到「" + std::string(World::standingName(static_cast<int>(v.asNumber()))) + "」", "家族現在是「" + std::string(now >= 0 ? World::standingName(now) : "領主家") + "」（要「" +
            World::standingName(static_cast<int>(v.asNumber())) + "」）");
    } else if (k == "lord_opinion_ge") {
        double op = w.lord_ != kNone && w.people_[w.lord_].alive() ? w.opinion(w.lord_, p.id) : 0;
        set("老爺對你家好感 " + num(v.asNumber()) + " 以上", w.lord_ == kNone ? "莊園眼下沒有領主" : "老爺對" + who + "好感 " + num(op) + "／要 " + num(v.asNumber()));
    } else if (k == "demesne_free") {
        set("領主的自營地沒人包著", "領主的自營地已經包給別人了");
    } else if (k == "mill_free") {
        set("磨坊沒有別人包着", "磨坊已經有人包了");
    } else if (k == "younger_son_wed") {
        set("家裡有成了親、不是長子的兒子", "家裡沒有成了親、又不是長子的兒子住在家裡");
    } else if (k == "in_service") {
        set(yes(v) ? "在別人家幫傭" : "不在別人家幫傭", yes(v) ? who + "沒在幫傭" : who + "在別人家幫傭");
    } else if (k == "heir_apparent") {
        set(yes(v) ? "是要接家的人" : "不是要接家的人", yes(v) ? who + "不是要接家的人" : who + "是要接家的人，不能出去");
    } else if (k == "same_household") {
        set(yes(v) ? "住在家裡" : "不住在家裡", yes(v) ? who + "不住在家裡" : who + "就住在家裡");
    } else if (k == "in_debt") {
        set(yes(v) ? "欠著債" : "沒欠債", yes(v) ? "沒欠債" : "欠著債");
    } else if (k == "opposite_sex") {
        set("異性", "同性");
    } else if (k == "not_kin") {
        set("不是近親", "是近親");
    } else if (k == "kin_tie") {
        set(yes(v) ? "是親戚" : "不是親戚", yes(v) ? "不是親戚" : "是親戚");
    } else if (k == "same_class") {
        set("身分相當", "身分不相當");
    } else if (k == "other_dynasty") {
        set("別家的人", "同一家的人");
    } else if (k == "feud_ge") {
        set(v.asNumber() >= 2 ? "跟你家結了怨" : "跟你家有嫌隙", "跟你家還沒到那一步");
    } else {
        set(k, k);
        hush();
    }
    return g;
}

std::vector<OfficeGate> EventRunner::explain(const Json& cond, const Person& p, Id target, bool all) const {
    std::vector<OfficeGate> out;
    if (cond.isNull()) return out;
    if (cond.isArray()) {
        for (const auto& c : cond.asArray()) {
            auto more = explain(c, p, target, all);
            out.insert(out.end(), more.begin(), more.end());
        }
        return out;
    }
    for (const auto& [k, v] : cond.asObject()) {
        bool quiet = false;
        OfficeGate g = gate(k, v, p, target, &quiet);
        if (all || !quiet || !g.ok) out.push_back(g);
    }
    return out;
}

// What one must be, as a short phrase: 「識字、未婚、14 到 25 歲的兒子」.
std::string EventRunner::describe(const Json& cond, const Person& p, Id target) const {
    std::string out;
    for (const auto& g : explain(cond, p, target, false)) {
        if (g.label.empty()) continue;
        out += (out.empty() ? "" : "、") + g.label;
    }
    return out;
}

std::vector<OfficeGate> World::planGates(Id actorId, const PlanDef& pd, bool withTime) {
    std::vector<OfficeGate> g;
    auto add = [&](const std::string& label, bool ok, const std::string& detail) { g.push_back({label, ok, ok ? label : detail}); };
    if (actorId == kNone) {
        add("家裡有當家的", false, "家裡沒有當家的");
        return g;
    }
    Person& actor = people_[actorId];
    if (!actor.alive() || actor.household == kNone) {
        add("人在村裡", false, "人已經不在了");
        return g;
    }
    const bool going = std::any_of(plans_.begin(), plans_.end(), [&](const ActivePlan& p) { return p.actor == actorId && p.plan == pd.id; });
    if (going) {
        add("沒有正在辦", false, "正在辦");
        return g;
    }
    const bool head = actorId == actingHead();
    add(head ? "手上的事不到兩件" : "手上沒有別的事", planRoom(actorId),
        head ? "手上已經有兩件事在辦（放棄一件才能開新的）" : "手上已經有一件事在辦");
    auto cd = cooldowns_.find({actorId, "plan:" + pd.id});
    bool cooling = cd != cooldowns_.end() && cd->second > day_;
    add("不在上回之後的等待期", !cooling, cooling ? "上回才辦過，要到 " + seasonOf(cd->second) + "才能再辦" : "");
    EventRunner ev(*this);
    for (auto& x : ev.explain(pd.actor, actor, kNone, false)) g.push_back(x);
    // Who it is for and with: the very lists availablePlans offers.
    bool actorOk = std::all_of(g.begin(), g.end(), [](const OfficeGate& x) { return x.ok; });
    if (!pd.subject.isNull()) {
        std::vector<Id> pool = planPool(actorId, pd);
        std::string what = ev.describe(pd.subject["conditions"], actor, actorId);
        bool any = false;
        std::vector<std::string> nots;
        for (Id c : pool) {
            const Person& cp = people_[c];
            if (!cp.alive() || cp.household == kNone) continue;
            std::string first;
            for (const auto& x : ev.explain(pd.subject["conditions"], cp, actorId, true))
                if (!x.ok) {
                    first = x.detail;
                    break;
                }
            if (!first.empty()) {
                nots.push_back(first);
                continue;
            }
            auto scd = cooldowns_.find({c, "plan:" + pd.id});
            if (scd != cooldowns_.end() && scd->second > day_) {
                nots.push_back(cp.firstName + " 上回的事才過，要到 " + seasonOf(scd->second) + "才好再辦");
                continue;
            }
            any = true;
        }
        std::string label = "家裡有" + (what.empty() ? std::string("合適的人") : what + "的人");
        std::string detail;
        if (nots.empty()) detail = pd.subject.stringOr("pick", "any_child") == "any_child" ? "沒有在世、在村裡的兒女" : "家裡沒有別人";
        else {
            detail = "家裡沒有合適的人：";
            for (size_t i = 0; i < nots.size() && i < 4; ++i) detail += (i ? "；" : "") + nots[i];
            if (nots.size() > 4) detail += "……";
        }
        add(label, any, detail);
        actorOk = actorOk && any;
    }
    // Whom to do it with: asked only once the rest holds (with nobody at home to do it for, there is nobody to ask).
    if (!pd.target.isNull() && actorOk) {
        std::string pick = pd.target.stringOr("pick", "villager");
        std::string what = ev.describe(pd.target["conditions"], actor, actorId);
        std::string who = pick == "lord" ? "領主" : pick == "rival" ? "心裡恨的人（好感 -30 以下，或兩家結了怨）" : pick == "friend" ? "要好的人（好感 30 以上）" : "村裡的人";
        std::string label = "有對象：" + who + (what.empty() ? "" : "，" + what);
        bool ok = true;
        std::string detail;
        if (actorOk) {
            std::vector<PlanOption> opts;
            planOptions(actorId, pd, opts);
            ok = !opts.empty();
            if (!ok) {
                if (pick == "lord") detail = lord_ == kNone || !people_[lord_].alive() ? "莊園眼下沒有領主" : "領主眼下不肯見（" + what + "）";
                else if (pd.id == "arrange_match" || pd.id == "court") detail = "村裡眼下沒有年紀相當、身分相配、還沒成親的人";
                else detail = "村裡眼下沒有這樣的人（要" + who + (what.empty() ? "" : "：" + what) + "）";
            }
        }
        add(label, ok, detail);
    }
    if (withTime && head) add("這一季還有空閒", timeLeft() > 0, "這一季沒有空閒了");
    return g;
}

std::string World::planBlock(Id actorId, const PlanDef& pd) {
    for (const auto& x : planGates(actorId, pd, false))
        if (!x.ok) return x.detail;
    return "";
}

}  // namespace sim
