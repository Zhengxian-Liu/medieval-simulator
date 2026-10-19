// "What my family knows": the player-facing view of the world. Only what the family itself knows
// or believes goes in here (beliefs, not facts), with the reasons behind every opinion.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

#include "simcore/world.h"

namespace sim {

namespace {

const std::map<std::string, std::string>& modNames() {
    static const std::map<std::string, std::string> names{
        {"accused_my_family", "告過我家的人"}, {"agitator", "帶頭鬧事"}, {"begged_pardon", "上門賠過罪"},
        {"betrayed", "背叛"}, {"found_out", "私情被挑明"}, {"bitter_words", "說過難聽的話"}, {"childhood_fight", "小時候打過架"},
        {"childhood_friend", "從小的玩伴"}, {"christmas_feast", "聖誕節的宴席"}, {"confided", "說過心裡話"},
        {"crush", "動過心"}, {"defiant", "頂撞過"}, {"dutiful", "肯出力"}, {"fat_pig", "送過肥豬"},
        {"firm_reeve", "收租不手軟"}, {"gift", "送過禮"}, {"good_times", "常一起說笑"}, {"good_work", "活做得好"},
        {"greased", "收過好處"}, {"heriot", "死後稅"}, {"honest_reeve", "帳目清楚"}, {"insolent", "目無尊長"},
        {"land_dispute", "爭過地"}, {"lashed_out", "對家裡人發過火"}, {"lovers", "私情"}, {"made_up", "和好過"},
        {"merchet", "婚姻稅"}, {"mortuary", "喪葬捐"}, {"obedient", "聽話"}, {"old_acquaintance", "老相識"},
        {"poacher", "在林子裡被逮過"}, {"promised_match", "說好的親事"}, {"quarrels", "常吵嘴"}, {"rebel", "造過反"},
        {"shamed_us", "讓家裡丟過臉"}, {"shirker", "推託勞役"}, {"soft_reeve", "收租太軟"}, {"tender", "疼愛"},
        {"tithe", "什一稅"}, {"tithe_quarrel", "吵過什一稅"}, {"trusted_tenant", "信得過的佃戶"},
        {"voted_against_me", "陪審時投了我有罪"}, {"voted_for_them", "陪審時偏袒對方"}, {"welched", "賴過帳"},
        {"worked_off_debt", "做工抵過債"}, {"envy", "眼紅你家"}, {"refused_kindness", "不准我幫朋友"}, {"respect", "敬重你家"}, {"worthy_tenant", "體面的佃戶"}, {"lords_man", "領主的人"}, {"event", "往來"},
        {"comforted", "陪過我熬過難關"}, {"talked_back", "當面頂撞過我"}, {"taught", "手把手教過我"}, {"took_to_market", "帶我去過市集"},
        {"swapped_work", "農忙時換過工"}, {"helped_kin", "日子難過時接濟過"}, {"praised", "誇過我"}, {"scolded", "管教過我"}, {"prayed_together", "一起上過教堂"},
        {"slighted", "給過我臉色"}, {"helped_me", "借糧給我"}, {"helped_them", "幫過他們"}, {"refused_help", "不肯幫忙"},
        {"condolence", "來弔唁過"}, {"took_our_want", "搶了我們看中的"}, {"mill_toll", "嫌他家磨坊抽得重"}, {"usury", "放債吃利"}, {"settled_son", "替小兒子置了產"}, {"took_our_land", "趁機拿走我家的地"},
        {"asked_too_much", "開口要得太過分"}, {"no_mercy", "不肯寬限"}, {"spoken_for", "有人替他說過話"}, {"granted_ask", "答應過我的請求"},
        {"refused_ask", "回絕過我的請求"}, {"pressed_with_secret", "拿把柄壓過我"}, {"haggled", "跟我討價還價過"}, {"helped_us", "替我家墊過租"}, {"lied_to_me", "瞞過我的帳"}, {"stood_aside", "讓過我一步"}, {"hard_words", "說過狠話"}, {"forbade_match", "不准我的親事"}, {"put_off", "推託過我的事"},
        {"cold_shoulder", "給過我冷臉"}, {"heeded", "把我的事放在心上"}, {"neglected", "冷落過我"},
        {"came_to_wake", "來守過靈"}, {"missed_funeral", "喪事沒來露面"}, {"came_to_wedding", "來喝過喜酒"},
        {"wedding_gift", "送過賀禮"}, {"helps_at_home", "在家很幫忙"}, {"for_the_dead", "看在先人份上"}, {"same_house", "一家人"}, {"helped_the_manor", "替莊園解過圍"}, {"held_back", "要錢時推託過"}, {"paid_up", "欠債按時還清"}, {"against_the_dead", "記著先人的舊帳"}, {"missed_wedding", "喜事沒來"}, {"sent_word", "紅白事託人帶話，沒來"},
        // design/fun-r5.md C: asks with terms, the king's tax, the season's deeds.
        {"turned_down_terms", "沒答應我開的條件"}, {"assessed_us", "估稅估得狠"}, {"spared_us", "估稅時手下留情"},
        {"cheated_the_tax", "估稅的自己少交"}, {"pushed_tax_on_us", "把稅推給別家"}, {"carried_our_share", "替窮人家擔了稅"},
        {"honest_assessor", "估稅公道"}, {"hired_me", "雇我幹活"}, {"took_us_in", "收留了我家的人"}, {"gave_land", "分了地給我家"},
        {"fed_us", "挨餓時送過糧"}, {"left_us_hungry", "挨餓時沒伸手"},
        // design/fun-r5.md B: family plans asked, affairs, illness.
        {"forbade_plan", "不准我的打算"}, {"rebuffed", "拒絕過我"}, {"struck_the_lord", "對我動過手"}, {"doubted_paternity", "疑心不是親生的"},
        {"cold_father", "對我冷淡"}, {"stood_by", "護著我"}, {"nursed", "病中照顧過我"}};
    return names;
}


std::string signedInt(double v) {
    int n = static_cast<int>(std::lround(v));
    return (n > 0 ? "+" : "") + std::to_string(n);
}

}  // namespace

const char* opinionWord(double op) {
    return op >= 60 ? "非常親近" : op >= 30 ? "有好感" : op >= 10 ? "還算友善" : op > -10 ? "沒什麼感覺" : op > -30 ? "不太喜歡" : op > -60 ? "厭惡" : "恨之入骨";
}

namespace {

// Decodes the UTF-8 code point starting at s[i]; returns its length in bytes.
int decodeAt(const std::string& s, size_t i, char32_t& cp) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (i + n > s.size()) n = 1;
    cp = n == 1 ? c : c & (0xFF >> (n + 1));
    for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    return n;
}

bool wide(char32_t cp) {
    return cp == 0xB7 || (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
           cp == 0x2014 || cp == 0x2026 || cp == 0x201C || cp == 0x201D;
}

}  // namespace

std::string tidyText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    char32_t prev = 0;
    size_t i = 0;
    while (i < text.size()) {
        char32_t cp;
        int n = decodeAt(text, i, cp);
        if (cp == ' ' && wide(prev)) {
            // A run of spaces between two wide characters goes.
            size_t j = i;
            while (j < text.size() && text[j] == ' ') ++j;
            char32_t next = 0;
            if (j < text.size()) decodeAt(text, j, next);
            if (wide(next)) {
                i = j;
                continue;
            }
        }
        out.append(text, i, n);
        prev = cp;
        i += n;
    }
    return out;
}

std::string World::opinionText(Id from, Id to, int maxReasons) const {
    double op = opinion(from, to);
    std::string s = std::string(opinionWord(op)) + " " + signedInt(op);
    std::string why;
    double shown = 0, faded = 0;
    std::string fadedNames;  // old reasons worn down to almost nothing: told as faded, not left in "temperament"
    int n = 0, nf = 0;
    for (const auto& [lab, v] : opinionBreakdown(from, to)) {
        if (std::abs(v) < 2) {
            faded += v;
            if (nf++ < 2) fadedNames += (fadedNames.empty() ? "" : "、") + opinionLabel(lab);
            continue;
        }
        if (n >= maxReasons) continue;
        why += (why.empty() ? "" : "、") + opinionLabel(lab) + " " + signedInt(v);
        shown += v;
        ++n;
    }
    if (std::abs(faded) >= 2) why += (why.empty() ? "" : "、") + fadedNames + "…淡了 " + signedInt(faded);
    else faded = 0;
    // Whatever the listed reasons leave over is temperament and small dealings, so the parts add up. Not when the
    // opinion is at its limit: reasons past it would leave a remainder of the wrong sign.
    double rest = op - shown - faded;
    bool capped = std::abs(op) >= 100 && rest * op < 0;
    if (std::abs(rest) >= 2 && !capped) why += (why.empty() ? "" : "、") + std::string(rest > 0 ? "個性合得來、平常的往來 " : "個性不合、平常的摩擦 ") + signedInt(rest);
    return why.empty() ? s : s + "（" + why + "）";
}

std::string opinionLabel(const std::string& raw) {
    // opinionBreakdown already turns deed types into their names; the rest are modifier ids.
    std::string base = raw, suffix;
    for (const char* s : {"（你做的）", "（上一代）", "（對他家人）"}) {
        auto at = base.find(s);
        if (at != std::string::npos) {
            suffix = base.substr(at);
            base = base.substr(0, at);
        }
    }
    auto it = modNames().find(base);
    return (it != modNames().end() ? it->second : base) + suffix;
}

Id World::actingHead() const {
    Id head = playerHead();
    if (head == kNone || ageYears(people_[head]) >= 14) return head;
    const Household& h = households_[playerHousehold_];
    Id mother = people_[head].mother;
    if (mother != kNone && people_[mother].alive() && inPlayerHouse(people_[mother].household)) return mother;
    Id best = head;
    for (Id m : h.members)
        if (ageYears(people_[m]) >= 16 && (best == head || people_[m].birthDay < people_[best].birthDay)) best = m;
    return best;
}

std::string World::ladderText() const {
    if (playerHousehold_ == kNone) return "";
    const Household& h = households_[playerHousehold_];
    int rung = standing(playerHousehold_);
    std::string o = standingName(rung);
    if (rung >= 0) o += "（第 " + std::to_string(rung + 1) + " 階，共 4 階）";
    if (h.freehold >= 1) o += "，" + std::to_string(int(h.acres)) + " 畝裡有 " + std::to_string(int(h.freehold)) + " 畝是自由地";
    o += "。";
    switch (rung) {
        case 0: o += "下一階「自由佃農」：替全家贖身（計畫「贖身」，存錢 140 便士以上）。"; break;
        case 1: o += "下一階「自耕農」：把 6 畝佃來的地買成自由地（計畫「買自由地」，存錢 200 便士以上）。"; break;
        case 2:
            o += "下一階「小地主」：40 畝地，其中 20 畝是自由地（現在 " + std::to_string(int(h.acres)) + " 畝，自由地 " + std::to_string(int(h.freehold)) + " 畝）。";
            break;
        case 3: o += "已經是村裡佃戶能到的最高一階。"; break;
        default: break;
    }
    return o;
}

std::string World::regretText() const {
    if (playerDynasty_ == kNone || playerHead() == kNone) return "";
    const std::string& r = dynasties_[playerDynasty_].regret;
    if (r.empty() || r == people_[playerHead()].ambition) return "";
    const AmbitionDef* a = defs_.ambition(r);
    return a ? a->name : "";
}

std::string World::familyReport() const {
    std::ostringstream o;
    Id head = playerHead();
    if (head == kNone) return "（家族已經斷絕）\n";
    const Person& hp = people_[head];
    const Household& h = households_[playerHousehold_];

    o << "【" << dynasties_[playerDynasty_].surname << " 家，" << year() << " 年】\n";
    std::string traits;
    for (const auto& t : hp.traits) traits += (traits.empty() ? "" : "、") + defs_.traits.at(t).name;
    o << "家主 " << fullName(head) << "，" << int(ageYears(hp)) << " 歲" << (traits.empty() ? "" : "，" + traits) << "。";
    if (hp.stress >= 40) o << "心裡壓著" << (hp.stressCause.empty() ? "很多事" : hp.stressCause) << "。";
    o << "\n家裡 " << h.members.size() << " 口人，" << int(h.acres) << " 畝地，存錢 " << int(h.money) << " 便士，糧食還夠吃 "
      << int(h.food / std::max(0.5, consumption(h))) << " 天。";
    if (h.debt > 0.5)
        o << "欠" << (h.creditor == lord_ || h.creditor == kNone ? std::string("莊園 ") : " " + fullName(h.creditor) + " ")
          << int(h.debt) << " 便士" << (h.debtYears > 0 ? "（拖了 " + std::to_string(h.debtYears) + " 年）" : h.debtYears < 0 ? "（明年秋收後到期）" : "（今年秋收後到期）") << "。";
    if (h.heldDebt > 0.5)
        o << "另外還欠" << (h.heldCreditor == lord_ ? std::string("莊園 ") : " " + fullName(h.heldCreditor) + " ") << int(h.heldDebt)
          << " 便士，等前一筆了結再算。";
    if (h.lastRent > 0)
        o << "\n去年秋收約夠全家吃 " << int(h.lastYield) << " 天（十分之一已交什一稅）；地租 " << int(h.lastRent) << " 便士，交了 "
          << int(h.lastRentPaid) << "。" << (h.noOxUntil > day_ ? "家裡沒有耕牛，借別人的牲口犁地，收成少一成多。" : "");
    if (hp.flags.count("reeve")) o << "現在是莊頭。";
    if (actingHead() != head) o << "家主還小，由 " << fullName(actingHead()) << " 代為持家。";
    o << "\n";

    // Everyone in the house, and those away in service, with what they are to the head.
    auto relation = [&](const Person& m) -> std::string {
        bool f = m.sex == Sex::Female;
        if (m.id == hp.spouse) return f ? "妻子" : "丈夫";
        if (m.father == head || m.mother == head) return f ? "女兒" : "兒子";
        if (m.id == hp.father) return "父親";
        if (m.id == hp.mother) return "母親";
        if (m.spouse != kNone && (people_[m.spouse].father == head || people_[m.spouse].mother == head)) return f ? "媳婦" : "女婿";
        for (Id par : {m.father, m.mother})
            if (par != kNone && (people_[par].father == head || people_[par].mother == head)) return f ? "孫女" : "孫子";
        if (m.father != kNone && m.father == hp.father) return f ? "姊妹" : "兄弟";
        return "家人";
    };
    o << "家人：";
    bool firstMember = true;
    for (const auto& m : people_) {
        bool away = isAway(m) && inPlayerHouse(m.awayFrom);
        if (!m.alive() || m.id == head || (!inPlayerHouse(m.household) && !inPlayerHouse(m.home) && !away)) continue;
        std::string t;
        for (const auto& tr : m.traits)
            if (defs_.traits.at(tr).category == "personality") t += (t.empty() ? "" : "、") + defs_.traits.at(tr).name;
        o << (firstMember ? "" : "；") << shortName(m.id) << "（" << relation(m) << "，" << int(ageYears(m)) << " 歲"
          << (t.empty() ? "" : "，" + t) << (away ? "，" + stationName(m) : !inPlayerHouse(m.household) ? "，在別家幫傭" : "")
          << (!away && focusOf(m) ? "，重心：" + focusOf(m)->name : "")
          << "）";
        firstMember = false;
    }
    if (firstMember) o << "（只剩家主一人）";
    o << "\n";

    // Where the family stands on the ladder, and what this generation is reaching for.
    {
        o << "家族地位：" << ladderText();
        std::string branch;
        if (kinInChurch(playerDynasty_)) branch += "家裡有人在教會";
        for (Id id : livingAway())
            if (people_[id].dynasty == playerDynasty_ && !inPlayerHouse(people_[id].awayFrom))  // the house's own are listed above
                branch += std::string(branch.empty() ? "" : "，") + people_[id].firstName + " " + stationName(people_[id]);
        if (!branch.empty()) o << branch << "。";
        o << "\n";
        Id acting = actingHead();
        const Person& ap = people_[acting];
        if (const AmbitionDef* a = defs_.ambition(hp.ambition)) {
            o << "這一代的志向：「" << a->name << "」，" << ambitionDesc(hp);
            int total = static_cast<int>(a->milestones.size()) + 1;
            int nx = nextMilestone(hp);
            if (nx >= 0)
                o << "（已做到 " << hp.ambitionStep << "/" << total << " 步；還差：" << milestoneName(hp, nx) << "）";
            else
                o << "（最後一步：" << ambitionFinal(hp) << "）";
            int left = a->maxYears - (day_ - hp.ambitionSince) / kDaysPerYear;
            if (left <= 5) o << "限到 " << ambitionDeadline(hp) << " 年底，還剩 " << std::max(0, left) << " 年。";
            std::string plans;
            for (const auto& pl : a->plans)
                if (const PlanDef* pd = defs_.plan(pl)) plans += (plans.empty() ? "" : "、") + pd->name;
            if (!plans.empty()) o << "能推進的計畫：" << plans << "。";
            o << "\n";
        }
        if (!dynasties_[playerDynasty_].regret.empty() && dynasties_[playerDynasty_].regret != hp.ambition)
            if (const AmbitionDef* r = defs_.ambition(dynasties_[playerDynasty_].regret))
                o << "上一代沒做完的心願：「" << r->name << "」。\n";
        (void)ap;
    }


    if (lord_ != kNone && people_[lord_].alive()) {
        o << "\n好感從 -100 到 +100：60 以上非常親近，30 有好感，10 還算友善，-10 到 10 沒什麼感覺，-30 不太喜歡，-60 厭惡，再低是恨之入骨。\n";
        o << "領主 " << fullName(lord_) << " 對你：" << opinionText(lord_, head) << "\n";
    }

    // The houses the head feels most strongly about, each with why.
    std::vector<std::pair<double, Id>> houses;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (const auto& oh : households_) {
        if (oh.extinct || oh.members.empty() || inPlayerHouse(oh.id) || oh.id == lordHouse || oh.head == kNone) continue;
        houses.push_back({opinion(head, oh.head), oh.head});
    }
    std::sort(houses.begin(), houses.end(), [](auto& a, auto& b) { return std::abs(a.first) > std::abs(b.first); });
    o << "\n你最在意的幾家（以家主為準）：\n";
    for (size_t i = 0; i < houses.size() && i < 6; ++i) {
        Id other = houses[i].second;
        o << "  " << fullName(other) << " 家";
        int st = feudStage(head, other);
        if (st > 0) o << "（" << (st == 3 ? "世仇" : st == 2 ? "結怨" : "嫌隙") << "）";
        o << "\n    你對他們：" << opinionText(head, other) << "\n    他們對你：" << opinionText(other, head) << "\n";
    }

    bool anyFeud = false;
    for (const auto& f : feuds_) {
        if (f.stage == 0 || (f.a != playerDynasty_ && f.b != playerDynasty_)) continue;
        if (!anyFeud) o << "\n恩怨：\n";
        anyFeud = true;
        Id other = f.a == playerDynasty_ ? f.b : f.a;
        o << "  " << dynasties_[other].surname << " 家：" << (f.stage == 3 ? "世仇" : f.stage == 2 ? "結怨" : "嫌隙") << "，從 "
          << (f.cause != kNone ? describeDeed(f.cause) : std::string("上一代")) << "起" << (f.truceUntil > day_ ? "（神父調解過，暫時休兵）" : "") << "\n";
    }

    // Secrets the family believes: wrongs done to them, and what they think they know about others.
    std::vector<std::string> wrongs, known;
    std::set<Id> seen;
    for (Id m : h.members) {
        for (const auto& b : beliefs_[m]) {
            if (b.confidence < 40 || seen.count(b.deed)) continue;
            const Deed& d = deeds_[b.deed];
            const DeedDef* def = defs_.deed(d.type);
            if (!def || def->visibility != "secret" || b.culprit == m) continue;
            seen.insert(b.deed);
            bool ours = d.target != kNone && (inPlayerHouse(people_[d.target].household) || people_[d.target].dynasty == playerDynasty_);
            std::string line = "  " + describeDeed(b.deed) + "：認定是 " + fullName(b.culprit) + "（把握 " +
                               std::to_string(int(b.confidence)) + (b.source != kNone && b.source != m && b.source != head ? "，聽 " + fullName(b.source) + " 說的" : "") + "）";
            if (b.used) line += "　已經拿來用過了";
            (ours ? wrongs : known).push_back(line);
        }
    }
    if (!wrongs.empty()) {
        o << "\n家裡吃過的暗虧：\n";
        for (const auto& l : wrongs) o << l << "\n";
    }
    if (!known.empty()) {
        o << "\n你們知道（或以為知道）的別人的秘密：\n";
        for (size_t i = 0; i < known.size() && i < 8; ++i) o << known[i] << "\n";
    }

    // What the one deciding has done in secret, and could be held to account for; what they have owned up to
    // (to a wife, at the confessional) is no longer theirs alone.
    std::vector<std::string> mine, owned;
    Id actor = actingHead();
    for (const auto& d : deeds_) {
        if (d.actor != actor || d.type == "affair") continue;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret") continue;
        std::string line = "  " + describeDeed(d.id) + (d.target != kNone ? "，對 " + fullName(d.target) : "");
        std::string toldTo;
        for (const auto& q : people_) {
            if (q.id == actor || !q.alive()) continue;
            const Belief* b = beliefAbout(q.id, d.id);
            if (b && b->culprit == actor && b->source == actor) toldTo += (toldTo.empty() ? "" : "、") + (q.id == priest_ ? "神父" : shortName(q.id));
        }
        if (toldTo.empty()) mine.push_back(line);
        else owned.push_back(line + "：已經跟" + toldTo + "說了");
    }
    if (!mine.empty()) {
        o << "\n只有你自己知道的事：\n";
        for (size_t i = mine.size() > 5 ? mine.size() - 5 : 0; i < mine.size(); ++i) o << mine[i] << "\n";
    }
    if (!owned.empty()) {
        o << "\n已經說出口的事：\n";
        for (size_t i = owned.size() > 5 ? owned.size() - 5 : 0; i < owned.size(); ++i) o << owned[i] << "\n";
    }

    bool anyInquiry = false;
    for (const auto& [key, q] : inquiries_) {
        if (!inPlayerHouse(key.first) || q.done || !q.started) continue;
        if (!anyInquiry) o << "\n還在追查的事：\n";
        anyInquiry = true;
        std::string s;
        for (Id hh : q.suspects)
            if (households_[hh].head != kNone) s += (s.empty() ? "" : "、") + fullName(households_[hh].head);
        o << "  " << describeDeed(key.second) << "，嫌疑還剩 " << (s.empty() ? "（沒有）" : s + " 家") << "\n";
    }

    // The people who matter to the family right now (the spotlight), each with what they are about.
    {
        std::vector<Id> note = peopleOfNote();
        bool any = false;
        for (Id id : note) {
            if (!seemsBusy(id)) continue;
            if (!any) o << "\n眼下跟你家有事的人（輸入 i 可以細看）：\n";
            any = true;
            o << "  " << fullName(id) << "：" << personLine(id) << "\n";
        }
    }

    if (hp.lover != kNone) o << "\n你心裡藏著一段私情：" << fullName(hp.lover) << "。\n";
    for (Id m : h.members)
        if (m != head && people_[m].lover != kNone && beliefAbout(head, people_[m].loverDeed))
            o << "你知道 " << fullName(m) << " 和 " << fullName(people_[m].lover) << " 的事。\n";
    return o.str();
}

// Kinship of b to a, in the family's words ("" if not close kin).
std::string World::kinWord(Id a, Id b) const {
    const Person& pa = people_[a];
    const Person& pb = people_[b];
    bool f = pb.sex == Sex::Female;
    const std::string t = kinTie(a, b);
    if (t == "spouse" || pa.spouse == b) return f ? "妻子" : "丈夫";
    if (t == "child") return f ? "女兒" : "兒子";
    if (t == "parent") return f ? "母親" : "父親";
    if (t == "sibling") return f ? "姊妹" : "兄弟";
    if (t == "grandchild") return f ? "孫女" : "孫子";
    if (t == "grandparent") return f ? "祖母" : "祖父";
    if (t == "step_parent") return f ? "繼母" : "繼父";
    if (t == "step_child") return f ? "繼女" : "繼子";
    if (t == "child_in_law") return f ? "媳婦" : "女婿";
    if (t == "parent_in_law") return pa.sex == Sex::Male ? (f ? "岳母" : "岳父") : (f ? "婆婆" : "公公");
    if (t == "sibling_in_law") {
        // A brother's wife, a sister's husband: older or younger than a goes by the sibling's age.
        if (pb.spouse != kNone && kinTie(a, pb.spouse) == "sibling") {
            bool older = people_[pb.spouse].birthDay < pa.birthDay;
            return f ? (older ? "嫂嫂" : "弟媳") : (older ? "姊夫" : "妹夫");
        }
        return pa.sex == Sex::Female ? (f ? "小姑" : "小叔") : (f ? "小姨" : "舅子");
    }
    if (t == "uncle") {
        bool fatherSide = pa.father != kNone && kinTie(pa.father, b) == "sibling";
        return f ? (fatherSide ? "姑姑" : "阿姨") : (fatherSide ? "叔伯" : "舅舅");
    }
    if (t == "nephew") {
        bool brothers = pb.father != kNone && kinTie(a, pb.father) == "sibling";  // a brother's child is 姪, a sister's 外甥
        return f ? (brothers ? "姪女" : "外甥女") : (brothers ? "姪子" : "外甥");
    }
    if (closeKin(a, b)) return "親戚";
    return "";
}

// kinWord with what a list needs to place the person: a married daughter under her husband's name says whose house
// she married into; a man who married into his wife's house says so; a woman who heads a house while her husband
// lives says why (the holding is hers: a widow's or an heiress's land, and he married in).
std::string World::kinLabel(Id a, Id b) const {
    std::string k = kinWord(a, b);
    if (k.empty() || a == kNone || b == kNone) return k;
    const Person& pb = people_[b];
    auto surname = [&](Id hh) {
        return hh != kNone && households_[hh].dynasty != kNone ? dynasties_[households_[hh].dynasty].surname : std::string();
    };
    std::string note;
    bool inLaw = k == "妻子" || k == "丈夫" || k == "媳婦" || k == "女婿" || k.find("繼") != std::string::npos;
    Id hh = pb.household;
    bool spouseHere = pb.spouse != kNone && people_[pb.spouse].alive() && people_[pb.spouse].household == hh;
    if (hh != kNone && households_[hh].head == b && spouseHere && pb.sex == Sex::Female)
        // Her husband married in only if the house is her own family's and not his; otherwise she simply keeps it.
        note = households_[hh].dynasty == pb.dynasty && people_[pb.spouse].dynasty != pb.dynasty
                   ? "她當家：地是她家的，" + shortName(pb.spouse) + "入贅"
                   : std::string("她當家");
    else if (!inLaw && hh != kNone && !inPlayerHouse(hh) && pb.spouse != kNone && surname(hh) != "" && surname(hh) != surname(people_[a].household))
        note = (pb.sex == Sex::Female && households_[hh].dynasty == people_[pb.spouse].dynasty ? "嫁到" : pb.sex == Sex::Male && households_[hh].dynasty != pb.dynasty ? "入贅到" : "在") +
               surname(hh) + "家";
    return note.empty() ? k : k + "（" + note + "）";
}

std::string World::seasonLabel(int day) const {
    static const char* names[4] = {"冬末", "春", "夏收", "秋"};
    return std::to_string(cfg_.startYear + day / kDaysPerYear) + " " + names[(day % kDaysPerYear) / kDaysPerMonth / 3];
}

// What the family can tell about a person's aims. Kin, friends and talkers let it be known what they
// want; a scheme is never told, though a target may sense something.
bool World::aimsKnown(const Person& p) const {
    Id head = playerHead();
    if (head == kNone) return true;
    if (inPlayerHouse(p.household) || closeKin(head, p.id)) return true;
    return opinion(p.id, head) >= 0 || hasTrait(p, "gregarious");
}

std::string World::personLine(Id id) const {
    const Person& p = people_[id];
    Id head = playerHead();
    std::string s = std::to_string(int(ageYears(p))) + " 歲，對你家 " + std::to_string(int(std::lround(opinion(id, head))));
    int st = head != kNone ? feudStage(id, head) : 0;
    if (st > 0) s += st == 3 ? "，世仇" : st == 2 ? "，結了怨" : "，有嫌隙";
    if (p.intent.kind == "none" || p.intent.why.empty()) return s;
    bool aimedAtUs = p.intent.target != kNone && inPlayerHouse(people_[p.intent.target].household);
    if (!p.intent.secret && aimsKnown(p)) s += "。" + p.intent.why;
    else if (p.intent.secret && aimedAtUs) s += "。最近看你家的眼神不太對";
    return s;
}

std::string World::personReport(Id id) const {
    std::ostringstream o;
    const Person& p = people_[id];
    Id head = playerHead();
    const char* pr = p.sex == Sex::Male ? "他" : "她";
    o << "【" << fullName(id) << "】" << int(ageYears(p)) << " 歲";
    if (std::string occ = occupationName(p); !occ.empty()) o << "，" << occ;
    if (head != kNone && id != head) {
        std::string k = kinWord(head, id);
        if (!k.empty()) o << "，是你的" << k;
    }
    if (!p.alive()) o << "，已經過世";
    o << "。\n";
    if (p.household != kNone) {
        // A servant is of their own family's house: the acres of the house they serve in are not theirs.
        const Household& h = households_[p.home != kNone ? p.home : p.household];
        o << (h.head == id ? "家主。" : (h.head != kNone ? fullName(h.head) + " 家的人。" : ""));
        if (p.home != kNone && households_[p.household].head != kNone) o << "在 " << fullName(households_[p.household].head) << " 家幫傭。";
        int rung = standing(h.id);
        o << standingName(rung) << "，" << int(h.acres) << " 畝地" << (h.freehold >= 1 ? "（自由地 " + std::to_string(int(h.freehold)) + " 畝）" : "");
        if (breadShort(h) > 0) o << "，糧不夠吃到秋收";
        if (h.debt > 0.5) o << "，欠著債";
        o << "。\n";
    }
    std::string t;
    for (const auto& tr : p.traits)
        if (defs_.traits.at(tr).category != "education" || tr == "literate") t += (t.empty() ? "" : "、") + defs_.traits.at(tr).name;
    if (!t.empty()) o << "為人：" << t << "。\n";
    if (p.stress >= 40) o << "心裡壓著" << (p.stressCause.empty() ? std::string("很多事") : p.stressCause) << "。\n";

    bool known = aimsKnown(p);
    if (const AmbitionDef* a = defs_.ambition(p.ambition)) {
        if (known) o << pr << "這輩子想要：「" << a->name << "」，" << ambitionDesc(p) << "\n";
        else o << pr << "這輩子想要什麼，你看不透。\n";
    }
    if (id != head && p.intent.kind != "none" && !p.intent.why.empty()) {
        bool aimedAtUs = p.intent.target != kNone && inPlayerHouse(people_[p.intent.target].household);
        if (!p.intent.secret && known) o << "今年的打算：" << p.intent.why << "。\n";
        else if (p.intent.secret && aimedAtUs) o << pr << "心裡在盤算什麼，你看不出來。只是最近" << pr << "看你家的眼神不太對。\n";
        else o << pr << "今年在忙什麼，你不太清楚。\n";
    }
    if (!p.lastReason.empty() && ringOf(id) == kSpotlight) o << pr << "上回做決定時心裡想的：「" << p.lastReason << "」\n";

    if (head != kNone && id != head) {
        o << pr << "對你：" << opinionText(id, head, 4) << "\n";
        o << "你對" << pr << "：" << opinionText(head, id) << "\n";
        int st = feudStage(id, head);
        if (st > 0) {
            const Feud* f = feudBetween(p.dynasty, people_[head].dynasty);
            o << "兩家" << (st == 3 ? "是世仇" : st == 2 ? "結了怨" : "有嫌隙");
            if (f && f->cause != kNone) o << "，從 " << describeDeed(f->cause) << "起";
            o << "。\n";
        }
        const Household& ph = households_[playerHousehold_];
        if ((ph.creditor == id && ph.debt > 0.5) || (ph.heldCreditor == id && ph.heldDebt > 0.5)) o << "你家欠" << pr << "家的錢。\n";
        if (p.household != kNone) {
            const Household& h = households_[p.household];
            if ((h.creditor == head && h.debt > 0.5) || (h.heldCreditor == head && h.heldDebt > 0.5)) o << pr << "家欠你家的錢。\n";
        }
    }
    return o.str();
}

}  // namespace sim
