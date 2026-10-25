// Play the sim through the browser (tools/ui): the same game as `play`, but every turn is sent as one line of
// JSON (the whole situation: family, people, houses, village talk, plans, what is asked), and every answer comes
// back as one line of JSON. tools/ui/server.py runs this and serves the page.
//
//   webplay [--data data] [--seed N] [--start 1340] [--origin peasant|artisan] [--gentle] [--years 100] [--out log.md]
//           [--npc-model "python3 tools/npc_decider.py"] [--npc-log npc.txt] [--npc-scope spotlight|near|all]
//
// Out:  {"type":"state",...} then {"type":"ask","kind":"season"|"choice",...}; {"type":"person"|"report","text":...}
//       answers a look; {"type":"end",...} when the game is over.
// In:   {"do":"pass"} {"do":"year"} {"do":"quit"} {"do":"interact","who":12,"event":"talk_comfort"}
//       {"do":"ask","who":12,"ask":"sell_parcel","object":30,"subject":-1,"office":"","lever":""}
//       {"do":"want","what":"parcel"|"office:reeve"|"drop","object":30}
//       {"do":"plan","plan":"arrange_match","target":3,"subject":5} {"do":"cancel","plan":"befriend"}
//       {"do":"choose","index":0} {"do":"ai"} {"do":"inspect","who":12} {"do":"report"}
//       {"do":"focus","who":12,"focus":"letters"}   (年度重心; free, any time in the season)
//       {"do":"agenda","id":"sell_grain"}            (這一季的事: one of the state's "agenda" items)
//       {"do":"interact","who":12,"event":"keep_company"}   (陪家人: the company visit this person needs most)
//       {"do":"rule","rule":"occasions","value":"close"}     (家裡的規矩: one of the state's "rules", set to one of its choices;
//                                                             occasions = all|close|none|ask, grain = harvest|monthly|ask)
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

#include "simcore/autoplayer.h"
#include "simcore/events.h"
#include "simcore/world.h"
#include "npc_bridge.h"

using namespace sim;

namespace {

const char* seasonName(int month) {
    static const char* names[4] = {"冬末（1–3 月）", "春（4–6 月）", "夏收（7–9 月）", "秋（10–12 月）"};
    return names[(month / 3) % 4];
}

std::string str(const std::string& s) { return "\"" + jsonEscape(tidyText(s)) + "\""; }
std::string num(double v) {
    if (!std::isfinite(v)) return "0";
    if (std::abs(v - std::round(v)) < 1e-9) return std::to_string(static_cast<long long>(std::llround(v)));
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}
std::string boolean(bool b) { return b ? "true" : "false"; }

// A JSON object built one field at a time.
struct Obj {
    std::string s = "{";
    Obj& raw(const char* k, const std::string& v) {
        if (s.size() > 1) s += ",";
        s += "\"" + std::string(k) + "\":" + v;
        return *this;
    }
    Obj& text(const char* k, const std::string& v) { return raw(k, str(v)); }
    Obj& n(const char* k, double v) { return raw(k, num(v)); }
    Obj& b(const char* k, bool v) { return raw(k, boolean(v)); }
    std::string done() const { return s + "}"; }
};
std::string arr(const std::vector<std::string>& items) {
    std::string s = "[";
    for (size_t i = 0; i < items.size(); ++i) s += (i ? "," : "") + items[i];
    return s + "]";
}

// Reads the few kinds of input lines: a flat object of strings and numbers.
std::map<std::string, std::string> readLine(const std::string& line) {
    std::map<std::string, std::string> out;
    try {
        Json j = Json::parse(line);
        if (!j.isObject()) return out;
        for (const auto& [k, v] : j.asObject()) {
            if (v.isString()) out[k] = v.asString();
            else if (v.isNumber()) out[k] = std::to_string(v.asInt());
            else if (v.isBool()) out[k] = v.asBool() ? "1" : "0";
        }
    } catch (...) {
    }
    return out;
}
int toInt(const std::map<std::string, std::string>& m, const std::string& k, int fallback = kNone) {
    auto it = m.find(k);
    return it == m.end() ? fallback : parseIndex(it->second);
}

// What a season action must and may carry; "" when it is well formed, else what is wrong with it, in words.
std::string badAction(const std::map<std::string, std::string>& m) {
    static const std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> keys{
        {"interact", {{"who", "event"}, {}}},
        {"ask", {{"who", "ask"}, {"object", "subject", "office", "lever"}}},
        {"want", {{"what"}, {"object"}}},
        {"focus", {{"who", "focus"}, {}}},
        {"agenda", {{"id"}, {}}},
        {"plan", {{"plan"}, {"target", "subject"}}},
        {"cancel", {{}, {"plan"}}},
        {"rule", {{"rule", "value"}, {}}},
        {"pass", {{}, {}}}, {"year", {{}, {}}}, {"regency", {{}, {}}}, {"quit", {{}, {}}}};
    auto it = m.find("do");
    if (it == m.end()) return "動作沒有 \"do\"（例：{\"do\":\"pass\"}）";
    auto k = keys.find(it->second);
    if (k == keys.end()) {
        std::string all;
        for (const auto& [d, _] : keys) all += (all.empty() ? "" : "、") + d;
        return "不認得的動作 \"" + it->second + "\"（能用的：" + all + "）";
    }
    const auto& [need, may] = k->second;
    std::string missing, unknown;
    for (const auto& n : need)
        if (!m.count(n)) missing += (missing.empty() ? "" : "、") + ("\"" + n + "\"");
    for (const auto& [key, _] : m)
        if (key != "do" && std::find(need.begin(), need.end(), key) == need.end() && std::find(may.begin(), may.end(), key) == may.end())
            unknown += (unknown.empty() ? "" : "、") + ("\"" + key + "\"");
    if (missing.empty() && unknown.empty()) return "";
    std::string usage;
    for (const auto& n : need) usage += (usage.empty() ? "" : "、") + n;
    for (const auto& n : may) usage += (usage.empty() ? "" : "、") + n + "（可省）";
    return "\"" + it->second + "\" 動作" + (missing.empty() ? "" : "少了 " + missing) + (missing.empty() || unknown.empty() ? "" : "，") +
           (unknown.empty() ? "" : "不認得 " + unknown) + "（要寫的是：" + usage + "）。這一下什麼都沒做。";
}

const char* familiarity(double c) {
    return c >= 60 ? "很熟" : c >= 30 ? "熟識" : c >= 10 ? "認識" : "點頭之交";
}

class Web {
public:
    Web(World& w) : w_(w) {}

    size_t shown = 0;           // chronicle lines already sent
    std::string summary;        // what the last season came to, shown through this season
    World::FamilySnapshot seasonStart;  // the family at the start of this season, for "這一季到現在"
    World::FamilySnapshot lastSeason;   // ...and of the season before it: how each one's feeling for the head moved since
    bool quit = false;
    std::string notice;         // an action refused as malformed: sent once with the next state

    void send(const std::string& line) { std::cout << line << "\n" << std::flush; }

    // 人生路: each turn of a person's road, with its year.
    std::string road(const Person& p) const {
        std::vector<std::string> v;
        for (const auto& r : p.road)
            if (!r.note.empty() && r.note.rfind("接著", 0) != 0) v.push_back(Obj().n("year", w_.config().startYear + r.day / kDaysPerYear).text("text", r.note).done());
        return arr(v);
    }

    // What the family can do with someone this season (empty while a choice is being asked).
    std::string actionsFor(Id who, const Person& head) {
        std::vector<std::string> out;
        std::vector<World::Interaction> ins = w_.interactionsWith(who);
        EventRunner er(w_);
        const Person& p = w_.people()[who];
        const WantDef* want = w_.defs().want(p.want);
        // 談心, 一起上教堂, 誇獎 and 帶去市集 are one button, 陪伴: the engine picks the one this person needs (design/fun-r5.md C).
        static const std::set<std::string> company{"talk_heart", "talk_church", "talk_praise", "talk_market"};
        bool companyShown = false;
        std::string pick = w_.companyEvent(who);
        for (const auto& in : ins) {
            if (company.count(in.ev->id)) {
                if (companyShown) continue;
                companyShown = true;
                const World::Interaction* use = &in;
                for (const auto& x : ins)
                    if (x.ev->id == pick) use = &x;
                // A visit that cannot be made now is not listed (fun-r9 0: no 「最近才陪過」 button).
                if (pick.empty() || !use->open) continue;
                bool wanted = want && std::find(want->answeredBy.begin(), want->answeredBy.end(), pick) != want->answeredBy.end();
                out.push_back(Obj()
                                  .text("event", "keep_company")
                                  .text("title", pick.empty() ? std::string("陪伴") : "陪伴：" + use->ev->title)
                                  .text("desc", er.format(use->ev->interaction.stringOr("desc", ""), p, head.id))
                                  .b("open", !pick.empty() && use->open)
                                  .text("why", pick.empty() ? (use->why.empty() ? "最近才陪過" : use->why) : "")
                                  .b("wanted", wanted && use->open)
                                  .n("time", use->ev->interaction.numberOr("time", 1))
                                  .n("money", use->ev->interaction.numberOr("money", 0))
                                  .done());
                continue;
            }
            bool wanted = want && std::find(want->answeredBy.begin(), want->answeredBy.end(), in.ev->id) != want->answeredBy.end();
            out.push_back(Obj()
                              .text("event", in.ev->id)
                              .text("title", in.ev->title)
                              .text("desc", er.format(in.ev->interaction.stringOr("desc", ""), p, head.id))
                              .b("open", in.open)
                              .text("why", in.why)
                              .b("wanted", wanted && in.open && w_.timeLeft() >= in.ev->interaction.numberOr("time", 1))
                              .n("time", in.ev->interaction.numberOr("time", 1))
                              .n("money", in.ev->interaction.numberOr("money", 0))
                              .done());
        }
        return arr(out);
    }

    // The houses the family knows to be after this parcel.
    std::string wantedBy(Id parcel) {
        std::vector<std::string> out;
        Id hh = w_.playerHousehold();
        for (const auto& wt : w_.wants())
            if (wt.open && wt.kind == "parcel" && wt.object == parcel && w_.familyKnows(wt)) {
                Id d = w_.households()[wt.house].dynasty;
                out.push_back(str(wt.house == hh ? "你家" : (d != kNone ? w_.dynasties()[d].surname : "") + "家"));
            }
        return arr(out);
    }

    // 開口: what the head could ask this person for (design/contested.md).
    std::string asksFor(Id who) {
        std::vector<std::string> out;
        for (const auto& a : w_.asksFor(who))
            out.push_back(Obj().text("ask", a.ask).text("label", a.label).n("object", a.object).n("subject", a.subject)
                              .text("office", a.office).b("open", a.open).text("why", a.why).done());
        return arr(out);
    }

    std::string reasons(Id from, Id to) {
        std::vector<std::string> out;
        auto br = w_.opinionBreakdown(from, to);
        // A reason worn down to almost nothing reads as faded, not as a small push one way or the other.
        for (size_t i = 0; i < br.size() && i < 6; ++i)
            out.push_back("[" + str(opinionLabel(br[i].first) + (std::abs(br[i].second) < 2 ? "…淡了" : "")) + "," + num(std::lround(br[i].second)) + "]");
        return arr(out);
    }

    // ---- The chronicle by season (design/ui-text.md) ----
    // Each new chronicle line becomes an entry: its season, which block it goes in (big = 大事 in bold, fam = the
    // family's smaller news under them, news = village talk, sub = a line that belongs to the entry above it), the
    // text in the house style (full name at the first mention in a season, then the given name; only 〔〕 for a card
    // the player answered and 【消息】 for what was heard), and for a late outcome the season it came from.
    static int seasonKeyOf(int year, int month) { return year * 4 + month / 3; }
    int seasonKey(int day) const {
        return seasonKeyOf(w_.config().startYear + day / kDaysPerYear, (day % kDaysPerYear) / kDaysPerMonth);
    }
    static std::string seasonLabel(int key) {
        static const char* names[4] = {"冬末", "春", "夏收", "秋"};
        return std::to_string(key / 4) + " " + names[key % 4];
    }
    // 【欠債】 → 「欠債：」, 【風聲】 → 【消息】; ASCII minus before a number → −.
    static std::string houseStyle(std::string t) {
        std::string out;
        size_t i = 0;
        while (i < t.size()) {
            size_t a = t.find("【", i);
            if (a == std::string::npos) break;
            size_t b = t.find("】", a);
            if (b == std::string::npos) break;
            std::string tag = t.substr(a + 3, b - a - 3);
            out += t.substr(i, a - i);
            if (tag == "消息" || tag == "風聲" || tag == "傳聞") out += "【消息】";
            else if (!tag.empty()) out += tag + "：";
            i = b + 3;
        }
        out += t.substr(i);
        std::string r;
        for (size_t k = 0; k < out.size(); ++k) {
            unsigned char prev = k ? static_cast<unsigned char>(out[k - 1]) : ' ';
            bool numAfter = k + 1 < out.size() && out[k + 1] >= '0' && out[k + 1] <= '9';
            if (out[k] == '-' && numAfter && (prev == ' ' || prev >= 0x80 || prev == '(' || prev == ','))
                r += "−";
            else
                r += out[k];
        }
        return r;
    }
    struct NameRef { std::string full, given; };
    std::vector<NameRef> names_;
    size_t namesFor_ = static_cast<size_t>(-1);
    std::map<int, std::set<std::string>> namesSeen_;  // season -> full names already written in it
    void refreshNames() {
        const World& w = w_;
        if (namesFor_ == static_cast<size_t>(w.today())) return;
        namesFor_ = static_cast<size_t>(w.today());
        names_.clear();
        std::map<std::string, int> givenCount;
        std::vector<NameRef> all;
        for (const auto& p : w.people()) {
            if (!p.alive() && p.deathDay < w.today() - kDaysPerYear) continue;
            std::string full = w.fullName(p.id), given = w.shortName(p.id);
            if (full == given || given.empty()) continue;
            ++givenCount[given];
            all.push_back({full, given});
        }
        for (const auto& n : all)
            if (givenCount[n.given] == 1) names_.push_back(n);  // a given name two people answer to stays full
        std::sort(names_.begin(), names_.end(), [](const NameRef& a, const NameRef& b) { return a.full.size() > b.full.size(); });
    }
    std::string shortenNames(std::string t, int key) {
        auto& seen = namesSeen_[key];
        // Longest names first, each mention swapped for a marker as it is found, so 「奧斯伯特·阿特伍德」 is never
        // found inside 「小奧斯伯特·阿特伍德」 (whole names only).
        std::vector<std::string> said;
        for (const auto& n : names_) {
            size_t at = t.find(n.full);
            if (at == std::string::npos) continue;
            bool first = !seen.count(n.full);  // the first mention in the season stays whole; later ones the given name
            seen.insert(n.full);
            for (; at != std::string::npos; at = t.find(n.full, at)) {
                std::string mark = "\x01" + std::to_string(said.size()) + "\x02";
                said.push_back(first ? n.full : n.given);
                first = false;
                t.replace(at, n.full.size(), mark);
                at += mark.size();
            }
        }
        for (size_t a = t.find('\x01'); a != std::string::npos; a = t.find('\x01', a)) {
            size_t b = t.find('\x02', a);
            const std::string& name = said[std::stoul(t.substr(a + 1, b - a - 1))];
            t.replace(a, b - a + 1, name);
            a += name.size();
        }
        return t;
    }
    // Who and what count as touching the family: kin, the household, anyone with a strong feeling either way (好感
    // 40 or more, a grudge of −30 or a feud), and the things the family is after.
    std::vector<std::string> nearNames_, nearThings_;
    void refreshNear() {
        const World& w = w_;
        nearNames_.clear();
        nearThings_.clear();
        Id head = w.playerHead(), hh = w.playerHousehold();
        if (head == kNone) return;
        for (const auto& p : w.people()) {
            if (!p.alive() || p.id == head) continue;
            bool near = p.household == hh || !w.kinWord(head, p.id).empty() || w.opinion(p.id, head) >= 40 ||
                        w.opinion(head, p.id) >= 40 || w.opinion(head, p.id) <= -30 || w.feudStage(head, p.id) > 0;
            if (near) nearNames_.push_back(w.fullName(p.id));
        }
        nearNames_.push_back(w.fullName(head));
        nearNames_.push_back(w.dynasties()[w.playerDynasty()].surname + "家");
        for (const auto& wt : w.wants())
            if (wt.open && wt.house == hh) nearThings_.push_back(w.wantThing(wt));
    }
    bool touches(const std::string& t) const {
        // A whole name: not the tail of a longer one (「小威廉·庫柏」 is not 「威廉·庫柏」).
        auto whole = [&](const std::string& n) {
            for (size_t at = t.find(n); at != std::string::npos; at = t.find(n, at + 1)) {
                std::string before = at >= 3 ? t.substr(at - 3, 3) : "";
                if (before != "小" && before != "二" && before != "三" && before != "老" && before != "大") return true;
            }
            return false;
        };
        for (const auto& n : nearNames_)
            if (whole(n)) return true;
        for (const auto& n : nearThings_)
            if (!n.empty() && t.find(n) != std::string::npos) return true;
        return false;
    }
    // The card the player just answered, so the line it leaves becomes 〔卡名〕選擇 → 結果 even when the sim
    // writes the outcome without a （選擇：…） mark.
    std::string answeredTitle, answeredChoice;
    int answeredDay = -1;
    // A sub-line (indented) belongs to the entry before it: none when that line was dropped or folded (the road's
    // conditions, a grain buy), and it stands alone when that entry is in another season.
    bool dropSubs_ = false;
    int parentKey_ = -1;
    static double numberBefore(const std::string& t, const std::string& mark) {
        size_t at = t.find(mark);
        if (at == std::string::npos) return 0;
        size_t b = at;
        while (b > 0 && t[b - 1] == ' ') --b;
        size_t a = b;
        while (a > 0 && ((t[a - 1] >= '0' && t[a - 1] <= '9') || t[a - 1] == '.')) --a;
        return a < b ? std::atof(t.substr(a, b - a).c_str()) : 0;
    }
    static bool bigLine(const std::string& t) {
        // The family's turning points: births, deaths, marriages, the ambition and posts, contests, debts, standing.
        for (const char* k : {"出生", "生了", "去世", "過世", "夭折", "死了", "死於", "娶了", "嫁給", "嫁到", "入贅", "成了親", "訂了親", "立志", "志向",
                              "達成", "當上", "選上", "落選", "連任", "爭到", "拿到了", "讓給", "輸給", "贏了", "欠債", "借了", "借來", "還清", "地位",
                              "繼承", "當了家", "接下", "贖身", "自由人", "法庭", "判了", "罰", "世仇", "結了怨", "和好", "開局"})
            if (t.find(k) != std::string::npos) return true;
        return false;
    }
    // fun-r9 C: what goes to the top of the view, above the decision: a post won or lost, the heriot or the steward
    // taking the ox, the debt moving, someone of the family coming or going.
    static bool topLine(const std::string& t) {
        for (const char* k : {"當上了", "當選", "選上", "落選", "連任", "【死後稅】", "牽走", "【欠債】", "【抵押】", "【租約】", "還清", "債清了", "劃掉了",
                              "欠債 ", "借了", "【宿敵】", "回到家裡", "回村", "回來了", "離家", "幫傭去了", "打了個小包袱", "出發", "入贅", "嫁到", "嫁給",
                              "娶了", "出生", "生下了", "去世", "夭折", "當了家"})
            if (t.find(k) != std::string::npos) return true;
        return false;
    }
    std::vector<std::string> entries_;  // built since the last state
    int lastBooksDay_ = -1;
    void takeLine(const ChronicleEntry& e) {
        std::string raw = e.text;
        bool indented = !raw.empty() && raw[0] == ' ';
        std::string t = raw;
        t.erase(0, t.find_first_not_of(' '));
        if (t.empty()) return;
        // The road's conditions are the top bar's to show (its expanded part), never the chronicle's.
        if (t.rfind("路怎麼走", 0) == 0 || (t.find("的條件：") != std::string::npos && (t.find("✓") != std::string::npos || t.find("✗") != std::string::npos))) {
            if (!indented) dropSubs_ = true;
            return;
        }
        int key = seasonKey(e.day);
        // Only an indented line is another's sub-line: a bare 「→ …」 or 「（…）」 is logged on its own (sometimes before
        // the line it follows from) and stands as its own entry.
        bool sub = indented;
        if (sub && dropSubs_) return;
        if (sub && parentKey_ != key) sub = false;
        if (!sub) dropSubs_ = false;
        std::string cls = sub ? "sub" : "fam";
        bool near = false;
        if (t.rfind("家裡買了 ", 0) == 0 && t.find("天的糧") != std::string::npos) {
            // (merged into the season's books: World counts every buy, by card, rule or custom)
            if (!indented) dropSubs_ = true;
            return;
        }
        if (!sub && (t.rfind("【消息】", 0) == 0 || t.rfind("【風聲】", 0) == 0 || t.rfind("【傳聞】", 0) == 0)) {
            cls = "news";
            // Who told it is not what it is about: 「聽女兒露西說，…」 touches the family only by what follows.
            size_t said = t.find("說，");
            near = touches(said != std::string::npos && said < 80 ? t.substr(said) : t);
        } else if (!sub && t.rfind("〔", 0) == 0) {
            size_t close = t.find("〕");
            std::string title = close == std::string::npos ? "" : t.substr(3, close - 3);
            std::string rest = close == std::string::npos ? t : t.substr(close + 3);
            const std::string mark = "（選擇：";
            size_t ch = rest.rfind(mark);
            size_t chEnd = std::string::npos;  // the 「）」 that closes the mark (the choice may hold its own （…）)
            for (size_t i = ch == std::string::npos ? rest.size() : ch + mark.size(), depth = 1; i + 3 <= rest.size(); ++i) {
                if (rest.compare(i, 3, "（") == 0) ++depth;
                else if (rest.compare(i, 3, "）") == 0 && --depth == 0) {
                    chEnd = i;
                    break;
                }
            }
            bool mine = !title.empty() && title == answeredTitle && e.day == answeredDay;
            if (chEnd != std::string::npos || mine) {
                // The choice and what came of it, one entry: 〔卡名〕選擇 → 結果. The mark may close the line (the
                // outcome before it) or sit in the middle (the situation before it, the outcome after).
                std::string choice = answeredChoice, outcome = rest;
                if (chEnd != std::string::npos) {
                    choice = rest.substr(ch + mark.size(), chEnd - ch - mark.size());
                    std::string after = rest.substr(chEnd + 3);
                    outcome = after.empty() ? rest.substr(0, ch) : after;
                }
                t = "〔" + title + "〕" + choice + " → " + outcome;
                cls = "big";
                if (mine) answeredTitle.clear();
            } else {
                // Not the player's answer: the card's name is a plain label, not a 〔〕.
                t = title.empty() ? rest : title + "：" + rest;
                cls = bigLine(rest) ? "big" : "fam";
            }
        } else if (!sub) {
            cls = e.importance == 3 || bigLine(t) ? "big" : "fam";
        }
        std::string origin;
        if (size_t at = t.find("借的）"); at != std::string::npos) {
            // 「（1330 春借的）」: the season the loan was taken, so the clients can show it under that entry too.
            size_t open = t.rfind("（", at);
            if (open != std::string::npos) origin = t.substr(open + 3, at - open - 3);
        }
        if (!sub) parentKey_ = key;
        t = shortenNames(houseStyle(t), key);
        Obj o;
        o.n("skey", key).text("season", seasonLabel(key)).n("year", key / 4).text("cls", cls).text("text", t);
        if (near) o.b("near", true);
        if (!sub && cls != "news" && topLine(t)) o.b("top", true);
        if (!origin.empty()) o.text("origin", origin);
        entries_.push_back(o.done());
    }
    // A season's books as the clients show them: from → to, each item signed and rounded to add up (as booksText),
    // and the grain bought merged into one line.
    // food: the barn over the same days, in words (World::foodSince), for the season still running.
    // gTimes/gDays: the grain bought in the season (World counts every buy); its pence are the books' 買糧.
    std::string booksJson(int key, double start, double end, const std::map<std::string, double>& items, const std::string& food = "",
                          int gTimes = 0, double gDays = 0) {
        long from = static_cast<long>(std::floor(start + 1e-9)), to = static_cast<long>(std::floor(end + 1e-9));
        std::vector<std::pair<std::string, std::pair<double, long>>> xs;
        long sum = 0;
        for (const auto& [why, d] : items) xs.push_back({why, {d, std::lround(d)}}), sum += std::lround(d);
        long off = (to - from) - sum;
        while (off != 0 && !xs.empty()) {
            int dir = off > 0 ? 1 : -1;
            size_t pick = 0;
            double worst = -1e9;
            for (size_t i = 0; i < xs.size(); ++i) {
                double cut = (xs[i].second.first - xs[i].second.second) * dir;
                if (cut > worst) worst = cut, pick = i;
            }
            xs[pick].second.second += dir;
            off -= dir;
        }
        std::sort(xs.begin(), xs.end(), [](auto& a, auto& b) { return std::abs(a.second.first) > std::abs(b.second.first); });
        std::vector<std::string> ins, outs;
        for (const auto& [why, v] : xs)
            if (v.second != 0) (v.second > 0 ? ins : outs).push_back("[" + str(why) + "," + std::to_string(v.second) + "]");
        Obj o;
        o.n("skey", key).text("season", seasonLabel(key)).text("cls", "ledger").n("from", from).n("to", to).raw("ins", arr(ins)).raw("outs", arr(outs));
        if (gTimes > 0) {
            auto bought = items.find("買糧");
            o.raw("grain", Obj().n("times", gTimes).n("days", std::lround(gDays)).n("pence", bought == items.end() ? 0 : std::lround(-bought->second)).done());
        }
        if (!food.empty()) o.text("food", food);
        return o.done();
    }

    // 村裡在說 (design/ui-text.md): one entry for each thing houses are after (who wants it, and who got it), what
    // touches the family before the rest, strangers' births, weddings and deaths folded into one line a year.
    // Six years back at most; the clients show the last three and fold the rest.
    std::string talk() {
        const World& w = w_;
        refreshNear();
        Id hh = w.playerHousehold();
        auto surname = [&](Id h) { return h != kNone && w.households()[h].dynasty != kNone ? w.dynasties()[w.households()[h].dynasty].surname : std::string(); };
        struct Thing { std::string thing; std::vector<std::string> houses; std::string outcome; int year = 0; bool near = false; };
        std::vector<Thing> things;
        struct Fold { int births = 0, weddings = 0, deaths = 0; std::vector<std::string> texts; };
        std::map<int, Fold> folds;
        std::vector<std::string> near, other;
        const auto& hd = w.heard();
        for (size_t i = hd.size(); i-- > 0;) {
            const Heard& x = hd[i];
            int year = w.config().startYear + x.day / kDaysPerYear;
            if (x.day < w.today() - 6 * kDaysPerYear) break;
            std::string text = houseStyle(x.text);
            bool touch = touches(x.text);
            if (x.news != kNone && x.news < static_cast<Id>(w.news().size())) {
                const News& n = w.news()[x.news];
                bool want = n.kind.rfind("want:", 0) == 0, won = n.kind == "contest:won" || n.kind == "office:won";
                if ((want || won) && !n.thing.empty()) {
                    auto it = std::find_if(things.begin(), things.end(), [&](const Thing& t) { return t.thing == n.thing; });
                    if (it == things.end()) things.push_back({n.thing, {}, "", year, false}), it = things.end() - 1;
                    std::string house = n.house == hh ? std::string("你家") : surname(n.house) + "家";
                    if (want && n.house != kNone && std::find(it->houses.begin(), it->houses.end(), house) == it->houses.end()) it->houses.push_back(house);
                    if (won && it->outcome.empty()) it->outcome = std::to_string(year) + " 年讓" + house + "拿到了";
                    it->near = it->near || touch || n.house == hh;
                    continue;
                }
                bool life = n.kind == "birth" || n.kind == "marriage" || n.kind == "death" || n.kind == "child_death" || n.kind == "infant_death";
                if (life && !touch) {
                    Fold& f = folds[year];
                    (n.kind == "birth" ? f.births : n.kind == "marriage" ? f.weddings : f.deaths)++;
                    f.texts.push_back(text);
                    continue;
                }
            }
            Obj q;
            q.n("year", year).text("text", text).b("whisper", x.news == kNone)
                .text("teller", x.teller != kNone ? w.tellerName(x.teller, x.news) : "");
            (touch ? near : other).push_back(q.done());
        }
        std::vector<std::string> out;
        std::stable_partition(things.begin(), things.end(), [](const Thing& t) { return t.near; });
        for (const auto& t : things) {
            std::string others;
            bool mine = false;
            for (const auto& h : t.houses) {
                if (h == "你家") mine = true;
                else others += (others.empty() ? "" : "、") + h.substr(0, h.size() - 3);  // 「卡特、米勒家」
            }
            std::string hs = std::string(mine ? "你家" : "") + (mine && !others.empty() ? "和" : "") + (others.empty() ? "" : others + "家");
            std::string line = t.thing + "：" + (hs.empty() ? "" : hs + (t.houses.size() > 1 ? "都想要" : "想要")) +
                               (t.outcome.empty() ? "" : (hs.empty() ? "" : " → ") + t.outcome);
            out.push_back(Obj().text("kind", "thing").n("year", t.year).b("near", t.near).text("text", line).done());
        }
        for (const auto& s : near) out.push_back("{\"kind\":\"near\"," + s.substr(1));
        for (auto it = folds.rbegin(); it != folds.rend(); ++it) {
            const Fold& f = it->second;
            std::string line = std::to_string(it->first) + " 年村裡";
            std::vector<std::string> parts;
            if (f.births) parts.push_back("生了 " + std::to_string(f.births) + " 個孩子");
            if (f.weddings) parts.push_back(std::to_string(f.weddings) + " 對結婚");
            if (f.deaths) parts.push_back("走了 " + std::to_string(f.deaths) + " 個人");
            for (size_t k = 0; k < parts.size(); ++k) line += (k ? "、" : "") + parts[k];
            std::vector<std::string> texts;
            for (const auto& t : f.texts) texts.push_back(str(t));
            out.push_back(Obj().text("kind", "fold").n("year", it->first).text("text", line).raw("items", arr(texts)).done());
        }
        for (const auto& s : other) out.push_back("{\"kind\":\"other\"," + s.substr(1));
        return arr(out);
    }

    // What happened since the last state, as the chronicle tells it (also on the last state, after the line ends).
    void chronicleInto(Obj& o) {
        const World& w = w_;
        std::vector<std::string> lines;
        const auto& log = w.chronicle();
        if (shown < log.size()) {
            refreshNames();
            refreshNear();
        }
        for (; shown < log.size(); ++shown) {
            if (log[shown].importance == 2) continue;
            std::string t = log[shown].text;
            t.erase(0, t.find_first_not_of(' '));
            lines.push_back(Obj().n("year", w.config().startYear + log[shown].day / kDaysPerYear).n("kind", log[shown].importance).text("text", t).done());
            takeLine(log[shown]);
        }
        answeredTitle.clear();  // its outcome, if any, has been told by now
        o.raw("chronicle", arr(lines));  // (the old flat list, kept until every client reads "entries")
        // Each season's books once it has closed, as an entry of that season (design/ui-text.md 這一季的帳).
        for (const auto& b : w.seasonBooks())
            if (b.day > lastBooksDay_) {
                lastBooksDay_ = b.day;
                if (b.day >= w.handedOver()) entries_.push_back(booksJson(seasonKey(b.day), b.start, b.end, b.items, "", b.grainTimes, b.grainDays));
            }
        o.raw("entries", arr(entries_));
        entries_.clear();
    }

    std::string state(bool withActions) {
        const World& w = w_;
        Id head = w.playerHead();
        Id hh = w.playerHousehold();
        Obj o;
        o.text("type", "state");
        o.raw("date", Obj().n("year", w.year()).n("month", w.month() + 1).text("season", seasonName(w.month())).done());
        if (head == kNone || hh == kNone) {
            chronicleInto(o);  // the death that ends the line is told too
            return o.done();
        }
        const Household& h = w.households()[hh];
        const Person& hp = w.people()[head];
        int rung = w.standing(hh);
        o.raw("house", Obj()
                           .text("surname", w.dynasties()[w.playerDynasty()].surname)
                           .n("money", std::floor(h.money))
                           .n("food_days", std::floor(w.foodDays(h)))
                           .n("acres", h.acres)
                           .n("freehold", h.freehold)
                           .n("debt", std::floor(w.owedAll(hh)))  // all the house owes, as the cards count it
                           .text("debt_text", w.debtText(hh))     // to whom, and when it falls due
                           .text("home", h.house)
                           .text("standing", rung >= 0 ? World::standingName(rung) : "領主家")
                           // The barn against the days to the harvest, today, in the numbers the 買糧 card uses; 「！」
                           // (lean) only when the purse cannot buy what is short.
                           .n("to_harvest", w.harvestIn())
                           .n("short_days", w.breadShort(h))
                           .b("lean", w.breadAtRisk(h))
                           .done());
        // What only the report used to say (design/fun-r6.md A): the family's name with what moved it and how to mend
        // it, the ladder with its next rung, the last generation's unfinished wish.
        {
            World::RepView rv = w.reputationView(head);
            std::vector<std::string> recent;
            for (const auto& r : rv.recent) recent.push_back(str(r));
            o.raw("reputation", Obj().n("value", rv.value).text("word", rv.word).text("effect", rv.effect).raw("recent", arr(recent))
                                    .text("mend", rv.mend).done());
            o.text("ladder", w.ladderText()).text("regret", w.regretText());
        }
        o.n("time_left", w.timeLeft());
        // fun-r9 D: this generation's rival house, what it contends with the family for now, and where they have met.
        if (Id rh = w.rivalHouse(); rh != kNone) {
            std::vector<std::string> clashes;
            for (const auto& c : w.rivalClashes()) clashes.push_back(str(c));
            o.raw("rival", Obj().text("name", w.rivalName()).n("house", rh).n("head", w.households()[rh].head).text("after", w.rivalAfter())
                               .raw("clashes", arr(clashes)).b("struck", w.rival().struck).done());
        }
        // 這一季的事: the few things worth a free day now, each a button ({"do":"agenda","id":...}).
        if (withActions) {
            // What pushes the ambition first (design/ui-text.md), then the rest in the game's order.
            std::vector<World::AgendaItem> list = w_.seasonAgenda();
            std::stable_partition(list.begin(), list.end(), [](const World::AgendaItem& a) { return a.id.rfind("step:", 0) == 0; });
            std::vector<std::string> items;
            for (const auto& a : list)
                items.push_back(Obj().text("id", a.id).text("title", a.title).text("desc", a.desc).b("open", a.open).text("why", a.why)
                                    .b("amb", a.id.rfind("step:", 0) == 0).done());
            o.raw("agenda", arr(items));
        }
        // 家裡的規矩: standing rules, each with what it is now and what it could be ({"do":"rule","rule":..,"value":..}).
        {
            std::vector<std::string> choices;
            for (const char* v : {"close", "all", "none", "ask"})
                choices.push_back(Obj().text("value", v).text("text", World::occasionRuleText(v)).done());
            const std::string& now = w.occasionRule();
            std::vector<std::string> grain;
            for (const char* v : {"harvest", "monthly", "ask"})
                grain.push_back(Obj().text("value", v).text("text", World::grainRuleText(v)).done());
            const std::string& g = w.grainRule();
            o.raw("rules", arr({Obj().text("id", "occasions").text("name", "紅白事").text("value", now)
                                    .text("text", now.empty() ? "還沒定（下次有人來請時問你；讓 AI 過一年時照常例：親近的人家才去）" : World::occasionRuleText(now))
                                    .raw("choices", arr(choices)).done(),
                                Obj().text("id", "grain").text("name", "買糧").text("value", g)
                                    .text("text", g.empty() ? "還沒定（頭一回缺糧時問你；讓 AI 過一年時每月買一個月的）" : World::grainRuleText(g))
                                    .raw("choices", arr(grain)).done()}));
        }
        // Holding the house for a child: the head may hand the years to the steward and answer only what is asked.
        if (const Household& ph = w.households()[w.playerHousehold()]; ph.ward != kNone && w.people()[ph.ward].alive()) {
            double left = 16 - w.ageYears(w.people()[ph.ward]);
            if (left > 1) o.raw("regency", Obj().text("ward", w.shortName(ph.ward)).n("years_left", std::ceil(left)).done());
        }
        o.n("head", head);
        if (const AmbitionDef* a = w.defs().ambition(hp.ambition)) {
            // Each step as it stands now (a sum saved and then spent is no longer "done"), then the goal itself.
            std::vector<std::string> steps;
            std::vector<int> st = w.milestoneState(head);
            for (size_t i = 0; i < a->milestones.size(); ++i)
                steps.push_back(Obj().text("name", w.milestoneName(hp, i)).b("done", st[i] == 1).b("slipped", st[i] == 2)
                                    .b("gate", a->milestones[i].gate).done());
            steps.push_back(Obj().text("name", w.ambitionFinal(hp)).b("done", false).b("final", true).done());
            int left = w.ambitionDeadline(hp) - w.year() + 1;  // counting this year: 1 = the last year
            std::vector<std::string> helps;
            for (const auto& pid : a->plans)
                if (const PlanDef* pd = w.defs().plan(pid)) helps.push_back(str(pd->name));
            // The post it is about, each of its conditions ✓ or ✗; who stands in the way; the loan to the manor.
            std::string gates;
            if (const Office* of = w.office(a->office)) gates = of->name + "的條件：" + World::gatesText(w.officeGates(head, a->office));
            std::string note;
            const ManorBond& bond = w.manorBond();
            if (bond.house == w.playerHousehold())
                note = "莊園欠你家 " + std::to_string(long(bond.owed)) + " 便士（借出 " + std::to_string(long(bond.lent)) + "），" +
                       std::to_string(bond.due) + " 年米迦勒節到期。";
            // The one thing in the way now (the top bar's second line): the first step not reached, and for a post the
            // first of its conditions that fails, with the value and the limit.
            std::string blocker;
            for (size_t i = 0; i < a->milestones.size() && blocker.empty(); ++i)
                if (st[i] != 1) {
                    if (a->milestones[i].gate && !a->office.empty())
                        for (const auto& g : w.officeGates(head, a->office))
                            if (!g.ok && blocker.empty()) blocker = "✗ " + g.detail;
                    if (blocker.empty()) {
                        // The step's own condition as it stands, with the value and the limit.
                        std::string why;
                        EventRunner er(w_);
                        for (const auto& g : er.explain(a->milestones[i].goal, hp, kNone))
                            if (!g.ok && why.empty() && g.detail != "不合條件") why = g.detail;  // (a bare 「不合條件」 says less than the step name)
                        std::string name = w.milestoneName(hp, i);
                        if (size_t cut = name.find("（"); cut != std::string::npos && !why.empty()) name = name.substr(0, cut);
                        blocker = std::string(st[i] == 2 ? "△ " : "○ ") + name + (why.empty() ? "" : "：" + why);
                    }
                }
            if (blocker.empty()) blocker = "◎ " + w.ambitionFinal(hp);
            o.raw("ambition", Obj().text("name", a->name).text("race", w.ambitionRace(hp)).text("desc", w.ambitionDesc(hp)).raw("steps", arr(steps)).text("hint", a->hint)
                                   .text("blocker", houseStyle(blocker))
                                   .raw("plans", arr(helps)).n("years_left", std::max(0, left)).n("deadline", w.ambitionDeadline(hp))
                                   .text("gates", gates).text("rival", w.rivalLine(hp)).text("note", note).done());
        }

        // 這一代的線 (design/fun-r6.md D): what this generation is living through, and what of it has come to an end.
        {
            std::vector<std::string> live, ended;
            for (const auto& t : w.threads())
                live.push_back(Obj().text("kind", t.kind).text("title", t.title).text("text", t.text).text("ends", t.ends).n("since", t.since).done());
            const auto& ends = w.threadEnds();
            int since = w.dynasties()[w.playerDynasty()].headSince.empty() ? 0 : w.dynasties()[w.playerDynasty()].headSince.back();
            for (auto it = ends.rbegin(); it != ends.rend() && ended.size() < 6; ++it)
                if (it->day >= since) ended.push_back(Obj().text("kind", it->kind).text("text", it->text).n("year", w.config().startYear + it->day / kDaysPerYear).done());
            o.raw("threads", arr(live)).raw("threads_ended", arr(ended));
        }

        // The family at home.
        std::vector<std::string> fam;
        std::vector<Id> order{head};
        for (Id m : h.members)
            if (m != head && w.people()[m].alive()) order.push_back(m);
        for (Id m : order) {
            const Person& p = w.people()[m];
            World::MoodView mv = w.moodView(p);
            std::vector<std::string> needs;
            for (int k = 0; k < kNeedCount; ++k) needs.push_back(num(std::lround(p.needs[k])));
            std::vector<std::string> traits;
            for (const auto& t : p.traits)
                if (const TraitDef* td = w.defs().trait(t)) traits.push_back(str(td->name));
            Obj f;
            f.n("id", m).text("name", w.shortName(m)).text("full", w.fullName(m)).text("kin", m == head ? "你" : w.kinWord(head, m).empty() ? "家裡人" : w.kinWord(head, m))
                .n("age", std::floor(w.ageYears(p))).b("female", p.sex == Sex::Female).text("mood", mv.word).text("mood_cause", mv.cause)
                .n("mood_value", std::lround(mv.value)).n("health", std::lround(p.health)).raw("needs", arr(needs)).raw("traits", arr(traits));
            f.text("station", p.station.empty() ? "" : w.stationName(p)).raw("road", road(p));
            // 年度重心: where their days go, and what else is open to them now.
            if (const FocusDef* fd = w.focusOf(p)) {
                f.text("focus", fd->id).text("focus_name", fd->name);
                std::vector<std::string> foci;
                if (withActions && w.steersFocus(m))
                    for (const FocusDef* o : w.focusChoices(m)) foci.push_back(Obj().text("id", o->id).text("name", o->name).text("desc", o->desc).done());
                f.raw("foci", arr(foci));
            }
            // The need that has fallen lowest, when it is in the yellow or the red (the only bar a name tag shows).
            {
                static const char* needNames[kNeedCount] = {"吃飽", "歇息", "暖和", "人情", "心靈"};
                int low = -1;
                for (int k = 0; k < kNeedCount; ++k)
                    if (p.needs[k] < 55 && (low < 0 || p.needs[k] < p.needs[low])) low = k;
                if (low >= 0) f.raw("need_low", Obj().text("name", needNames[low]).n("value", std::lround(p.needs[low])).done());
            }
            if (m != head) {
                double op = w.opinion(m, head);
                f.n("opinion", std::lround(op)).text("opinion_word", opinionWord(op)).raw("reasons", reasons(m, head)).text("want", w.wantText(p));
                if (auto was = lastSeason.people.find(m); lastSeason.head == head && was != lastSeason.people.end())
                    f.n("opinion_was", std::lround(was->second.second)).text("mood_was", was->second.first);
                if (withActions) f.raw("actions", actionsFor(m, hp));
            }
            fam.push_back(f.done());
        }
        o.raw("family", arr(fam));
        // The family's people living outside the village (design/life-paths.md): where they are, the road there.
        std::vector<std::string> away;
        for (Id m : w.livingAway()) {
            const Person& p = w.people()[m];
            if (p.awayFrom != hh) continue;
            away.push_back(Obj().n("id", m).text("name", w.shortName(m)).text("kin", w.kinWord(head, m)).n("age", std::floor(w.ageYears(p)))
                               .text("station", w.stationName(p)).raw("road", road(p)).done());
        }
        o.raw("away", arr(away));

        // Everyone else the family deals with: those it can go and see, and those with business with it.
        std::vector<Id> people;
        std::set<Id> seen(order.begin(), order.end());
        std::set<Id> canSee;
        for (Id id : w.peopleToSee())
            if (!seen.count(id)) {
                people.push_back(id);
                canSee.insert(id);
                seen.insert(id);
            }
        for (Id id : w.peopleOfNote())
            if (seen.insert(id).second) people.push_back(id);
        std::vector<std::string> ppl;
        for (Id id : people) {
            const Person& p = w.people()[id];
            if (!p.alive()) continue;
            double close = w.closeness(head, id);
            std::string kin = w.kinLabel(head, id);
            Obj q;
            q.n("id", id).text("name", w.fullName(id)).text("kin", kin).n("age", std::floor(w.ageYears(p)))
                .text("house", p.household != kNone && w.households()[p.household].dynasty != kNone
                                   ? w.dynasties()[w.households()[p.household].dynasty].surname : "")
                .n("household", p.household).n("opinion", std::lround(w.opinion(id, head))).text("opinion_word", opinionWord(w.opinion(id, head)))
                .n("yours", std::lround(w.opinion(head, id))).n("closeness", std::lround(close))
                .text("familiarity", kin.empty() ? familiarity(close) : kin).n("feud", w.feudStage(head, id))
                .b("busy", w.seemsBusy(id)).text("line", w.personLine(id)).raw("reasons", reasons(id, head));
            if (withActions && canSee.count(id)) q.raw("actions", actionsFor(id, hp));
            if (withActions && (canSee.count(id) || id == w.lordId())) q.raw("asks", asksFor(id));
            q.n("owes", std::lround(w.favourOwed(id, head))).n("owed", std::lround(w.favourOwed(head, id)));
            ppl.push_back(q.done());
        }
        o.raw("people", arr(ppl));

        // The village as houses, by how close each is to yours, and how.
        std::vector<std::string> houses;
        for (const auto& oh : w.households()) {
            if (oh.extinct || oh.members.empty() || oh.id == hh) continue;
            double link = w.houseLink(hh, oh.id);
            double sum = 0;
            int adults = 0;
            std::vector<std::string> mem;
            // The closest tie between the two houses, said in words.
            double best = -1;
            std::string tie;
            for (Id m : oh.members) {
                const Person& p = w.people()[m];
                if (!p.alive()) continue;
                if (w.ageYears(p) >= 14) sum += w.opinion(m, head), ++adults;
                mem.push_back(Obj().n("id", m).text("name", w.shortName(m)).n("age", std::floor(w.ageYears(p)))
                                  .b("head", oh.head == m).n("opinion", std::lround(w.opinion(m, head))).done());
                for (Id x : h.members) {
                    double c = w.closeness(x, m);
                    if (c <= best) continue;
                    best = c;
                    std::string k = w.kinWord(x, m);
                    std::string who = x == head ? "你" : w.shortName(x);
                    tie = !k.empty() ? w.shortName(m) + "是" + who + "的" + k : who + "和" + w.shortName(m) + familiarity(c);
                }
            }
            houses.push_back(Obj()
                                 .n("id", oh.id)
                                 .text("surname", oh.dynasty != kNone ? w.dynasties()[oh.dynasty].surname : "")
                                 .n("link", std::round(link * 100) / 100)
                                 .n("attitude", adults ? std::lround(sum / adults) : 0)
                                 .n("feud", oh.head != kNone ? w.feudStage(head, oh.head) : 0)
                                 .n("acres", std::floor(oh.acres))
                                 .text("tie", tie)
                                 .raw("members", arr(mem))
                                 .done());
        }
        o.raw("houses", arr(houses));

        // The village map (design/contested.md): who holds which parcel, the ground nobody holds alone, landmarks.
        {
            const VillageMap& vm = w.villageMap();
            Id lordHouse = w.lordId() != kNone ? w.people()[w.lordId()].household : kNone;
            std::vector<std::string> areas, pois, homes, parcels;
            for (const auto& a : vm.fields) areas.push_back(Obj().text("kind", "field").text("name", a.name).n("x", a.x).n("y", a.y).n("w", a.w).n("h", a.h).done());
            for (const auto& a : vm.zones) areas.push_back(Obj().text("kind", a.kind).text("name", a.name).n("x", a.x).n("y", a.y).n("w", a.w).n("h", a.h).done());
            for (const auto& pt : vm.pois) pois.push_back(Obj().text("kind", pt.kind).text("name", pt.name).n("x", pt.x).n("y", pt.y).done());
            for (const auto& [hid, cell] : vm.houseCell) {
                const Household& x = w.households()[hid];
                if (x.extinct || x.members.empty()) continue;
                homes.push_back(Obj().n("house", hid).text("name", x.dynasty != kNone ? w.dynasties()[x.dynasty].surname : "")
                                    .n("x", cell % vm.width).n("y", cell / vm.width).b("mine", hid == hh).b("lord", hid == lordHouse).done());
            }
            std::vector<int> cells(static_cast<size_t>(vm.width * vm.height), -1);
            for (const auto& pc : w.parcels()) {
                if (pc.gone) continue;
                int idx = static_cast<int>(parcels.size());
                for (int c : pc.cells) cells[static_cast<size_t>(c)] = idx;
                std::string holder = pc.holder == kNone ? "" : w.households()[pc.holder].dynasty != kNone ? w.dynasties()[w.households()[pc.holder].dynasty].surname : "";
                parcels.push_back(Obj().n("id", pc.id).text("name", w.parcelName(pc)).n("acres", pc.acres).b("free", pc.free)
                                      .n("house", pc.holder == kNone ? -1 : pc.holder).text("holder", holder).text("how", pc.how)
                                      .n("since", w.config().startYear + pc.since / kDaysPerYear).b("mine", pc.holder == hh)
                                      .b("lord", pc.holder != kNone && pc.holder == lordHouse).raw("wanted", wantedBy(pc.id)).done());
            }
            std::string cs = "[";
            for (size_t i = 0; i < cells.size(); ++i) cs += (i ? "," : "") + std::to_string(cells[i]);
            cs += "]";
            o.raw("map", Obj().n("w", vm.width).n("h", vm.height).raw("areas", arr(areas)).raw("pois", arr(pois))
                             .raw("homes", arr(homes)).raw("parcels", arr(parcels)).raw("cells", cs).done());
        }

        // What the village is contending over, as far as the family knows (design/contested.md 4).
        {
            auto surname = [&](Id h) { return h != kNone && w.households()[h].dynasty != kNone ? w.dynasties()[w.households()[h].dynasty].surname : std::string(); };
            std::vector<std::string> keys;
            std::map<std::string, std::vector<const Want*>> by;
            for (const auto& wt : w.wants()) {
                if (!wt.open || !w.familyKnows(wt)) continue;
                std::string k = wt.kind + "|" + (wt.kind == "office" ? wt.office : std::to_string(wt.object)) + "|" + (wt.kind == "place" ? wt.office : "");
                if (!by.count(k)) keys.push_back(k);
                by[k].push_back(&wt);
            }
            // The family's own things others are after count even when it wants nothing itself.
            std::vector<std::string> items;
            for (const auto& k : keys) {
                const auto& ws = by[k];
                const Want& first = *ws.front();
                std::string holder;
                bool ours = false;
                Id parcel = kNone;
                if (first.kind == "parcel") {
                    const Parcel& pc = w.parcels()[first.object];
                    parcel = pc.id;
                    ours = pc.holder == hh;
                    holder = pc.holder == kNone ? "領主手上，沒人租" : ours ? "你家的" : surname(pc.holder) + "家的";
                } else if (first.kind == "office") {
                    const Office* of = w.office(first.office);
                    ours = of && of->holder != kNone && w.people()[of->holder].household == hh;
                    holder = of && of->holder != kNone ? "現在是" + w.fullName(of->holder) : "空著";
                } else if (first.kind == "match") {
                    ours = w.people()[first.object].household == hh;
                    holder = w.ageYears(w.people()[first.object]) >= 0 ? std::to_string(static_cast<int>(w.ageYears(w.people()[first.object]))) + " 歲，還沒說親" : "";
                } else {
                    ours = first.object == hh;
                    holder = "名額 " + std::string(w.placeOpen(first.object, first.office) ? "還空著" : "已經滿了");
                }
                std::vector<std::string> who;
                bool mine = false;
                Id myWant = kNone;
                for (const Want* wt : ws) {
                    bool fam = wt->house == hh;
                    mine = mine || fam;
                    if (fam) myWant = wt->id;
                    std::string sub = wt->subject != kNone && (first.kind != "parcel") ? w.shortName(wt->subject) : "";
                    who.push_back(Obj().text("house", fam ? "你家" : surname(wt->house) + "家").text("why", fam ? wt->why : "")
                                      .text("for", sub).b("mine", fam).n("since", w.config().startYear + wt->since / kDaysPerYear).done());
                }
                items.push_back(Obj().text("kind", first.kind).text("thing", w.wantThing(first)).text("holder", holder).b("ours", ours)
                                    .b("contested", ws.size() >= 2)
                                    .n("parcel", parcel).n("mine", myWant).raw("wanters", arr(who)).done());
            }
            std::vector<std::string> past;
            for (auto it = w.wants().rbegin(); it != w.wants().rend() && past.size() < 8; ++it)
                if (!it->open && it->ended >= w.today() - 3 * kDaysPerYear && it->house == hh && it->outcome != "放下了" && it->outcome != "家沒了")
                    past.push_back(Obj().text("thing", w.wantThing(*it)).text("outcome", it->outcome).n("year", w.config().startYear + it->ended / kDaysPerYear).done());
            std::vector<std::string> offs;
            for (const auto& of : w.offices())
                offs.push_back(Obj().text("id", of.id).text("name", of.name).text("holder", of.holder != kNone ? w.fullName(of.holder) : "")
                                   .b("mine", of.holder != kNone && w.people()[of.holder].household == hh).n("until", of.until)
                                   .b("can", w.familyOfficeMan(of.id, head) != kNone).text("why", w.familyOfficeBar(of.id))
                                   .raw("gates", [&] {
                                       // Every condition for the man who would stand (else the head), met or not.
                                       Id man = w.familyOfficeMan(of.id, head);
                                       std::vector<std::string> v;
                                       for (const auto& g : w.officeGates(man != kNone ? man : head, of.id))
                                           v.push_back(Obj().text("label", g.label).b("ok", g.ok).text("detail", g.detail).done());
                                       return arr(v); }())
                                   .n("want", [&] { for (const auto& x : w.wants()) if (x.open && x.house == hh && x.kind == "office" && x.office == of.id) return x.id; return kNone; }())
                                   .text("runner", [&] { for (const auto& x : w.wants()) if (x.open && x.house == hh && x.kind == "office" && x.office == of.id && x.subject != kNone) return w.shortName(x.subject); return std::string(); }())
                                   .raw("men", [&] {
                                       // Every man of the house who could stand for it (the head first).
                                       std::vector<std::string> v;
                                       if (hh != kNone)
                                           for (Id m : w.households()[hh].members)
                                               if (w.officeBar(m, of.id).empty()) v.push_back(Obj().n("id", m).text("name", w.shortName(m)).b("head", m == head).done());
                                       return arr(v); }())
                                   .b("wanted", w.wants().end() != std::find_if(w.wants().begin(), w.wants().end(), [&](const Want& x) {
                                       return x.open && x.house == hh && x.kind == "office" && x.office == of.id; }))
                                   .raw("voters", [&] {
                                       // While the family is after it: each voting head's lean and what stands in the way,
                                       // with the 開口 for his vote (design/fun-r6.md A 選舉).
                                       std::vector<std::string> v;
                                       bool wanted = false;
                                       for (const auto& x : w.wants()) wanted = wanted || (x.open && x.house == hh && x.kind == "office" && x.office == of.id);
                                       if (!wanted || !withActions) return arr(v);
                                       for (const auto& vv : w_.electionView(of.id)) {
                                           Obj q;
                                           q.n("id", vv.voter).text("name", w.fullName(vv.voter)).text("lean", vv.lean != kNone ? (vv.lean == head ? "你" : w.inPlayerHouse(w.person(vv.lean).household) ? w.shortName(vv.lean) : w.fullName(vv.lean)) : "")
                                               .b("ours", vv.lean != kNone && w.people()[vv.lean].household == hh)
                                               .text("promised", vv.promised != kNone ? w.shortName(vv.promised) : "").text("note", vv.note);
                                           if (!vv.ask.ask.empty())
                                               q.raw("ask", Obj().text("ask", vv.ask.ask).text("label", vv.ask.label).n("object", vv.ask.object).n("subject", vv.ask.subject)
                                                                .text("office", vv.ask.office).b("open", vv.ask.open).text("why", vv.ask.why).done());
                                           v.push_back(q.done());
                                       }
                                       return arr(v); }())
                                   .done());
            Obj c;
            c.raw("items", arr(items)).raw("past", arr(past)).raw("offices", arr(offs));
            for (const auto& f : w.entryFines())
                if (f.house == hh)
                    c.raw("fine", Obj().n("amount", f.amount).n("year", w.config().startYear + f.due / kDaysPerYear)
                                     .n("month", (f.due % kDaysPerYear) / kDaysPerMonth + 1).b("extended", f.extended).done());
            o.raw("contest", c.done());
            const AskResult& la = w.lastAsk();
            // Only the levers that can work now: the season it was given and the money it takes (a press is part of the same talk).
            if (la.who != kNone && la.day >= w.today() - 2 && w.people()[la.who].alive())
                o.raw("last_ask", Obj().n("who", la.who).text("name", w.fullName(la.who)).text("ask", la.ask).text("office", la.office)
                                      .n("object", la.object).n("subject", la.subject).n("answer", la.answer).text("text", la.text)
                                      .raw("levers", [&] { std::vector<std::string> v; for (const auto& l : la.levers) if (w.leverOpen(l)) v.push_back(str(l)); return arr(v); }()).done());
        }

        // What the family has heard, newest first.
        std::vector<std::string> heard;
        const auto& hd = w.heard();
        for (size_t i = hd.size(); i-- > 0 && heard.size() < 30;) {
            const Heard& x = hd[i];
            heard.push_back(Obj()
                                .n("year", w.config().startYear + x.day / kDaysPerYear)
                                .text("teller", x.teller != kNone ? w.tellerName(x.teller, x.news) : "")
                                .text("text", x.text)
                                .b("whisper", x.news == kNone)
                                .b("recent", w.today() - x.day <= 3 * kDaysPerMonth)
                                .done());
        }
        o.raw("heard", arr(heard));  // (the old flat list, kept until every client reads "talk")
        o.raw("talk", talk());

        // Plans: the one under way, or those that can be started and with whom.
        Id acting = w.actingHead();
        {
            std::vector<std::string> going;
            for (const ActivePlan* ap : w.plansOf(acting)) {
                const PlanDef* pd = w.defs().plan(ap->plan);
                going.push_back(Obj().text("id", ap->plan).text("name", pd ? pd->name : ap->plan)
                                    .text("target", ap->target != kNone ? w.fullName(ap->target) : "").n("progress", std::floor(ap->progress)).done());
            }
            o.raw("plans_going", arr(going));
        }
        // Listed even with both plans going, each saying why it cannot start (give one up to start another).
        if (withActions && acting != kNone) {
            std::vector<PlanOption> opts = w_.availablePlans(acting);
            const AmbitionDef* amb = w.defs().ambition(hp.ambition);
            std::vector<std::string> plans;
            for (const auto& pd : w.defs().plans) {
                std::vector<PlanOption> picks;
                for (const auto& op : opts)
                    if (op.plan == pd.id) picks.push_back(op);
                bool forAmb = amb && std::find(amb->plans.begin(), amb->plans.end(), pd.id) != amb->plans.end();
                if (picks.empty() && pd.needs.empty()) continue;
                std::sort(picks.begin(), picks.end(), [](auto& a, auto& b) { return a.targetWeight > b.targetWeight; });
                if (picks.size() > 12) picks.resize(12);
                std::vector<std::string> targets;
                for (const auto& op : picks) {
                    std::string label;
                    if (op.subject != kNone) label += "替" + w.shortName(op.subject) + "：";
                    if (op.target != kNone) {
                        const Person& tp = w.people()[op.target];
                        label += w.fullName(op.target) + "（" + std::to_string(int(w.ageYears(tp))) + " 歲，對你" + opinionWord(w.opinion(op.target, head));
                        // A servant is of their own family's house, not the one they serve in (whose acres are not theirs).
                        Id own = tp.home != kNone ? tp.home : tp.household;
                        if (own != kNone) {
                            const Household& th = w.households()[own];
                            label += "，家裡 " + std::to_string(int(th.acres)) + " 畝" + (w.breadShort(th) > 0 ? "，糧不夠吃到秋收" : "") + (th.debt > 0.5 ? "，欠著債" : "");
                        }
                        if (tp.home != kNone && tp.household != kNone && w.households()[tp.household].dynasty != kNone)
                            label += "，在" + w.dynasties()[w.households()[tp.household].dynasty].surname + "家幫傭";
                        int st = w.feudStage(head, op.target);
                        if (st > 0) label += st == 3 ? "，世仇" : st == 2 ? "，結怨" : "，嫌隙";
                        label += "）";
                    }
                    if (label.empty()) label = "就這麼辦";
                    targets.push_back(Obj().n("target", op.target).n("subject", op.subject).text("label", label).done());
                }
                // Each condition met or not (design/fun-r6.md A), the first unmet one as why.
                std::vector<std::string> gates;
                std::string why;
                for (const auto& g : w_.planGates(acting, pd)) {
                    gates.push_back(Obj().text("label", g.label).b("ok", g.ok).text("detail", g.detail).done());
                    if (!g.ok && why.empty()) why = g.detail;
                }
                if (why.empty() && picks.empty()) why = "眼下找不到合適的對象";
                plans.push_back(Obj().text("id", pd.id).text("name", pd.name).text("desc", pd.desc + "（用一段空閒）").text("needs", pd.needs).text("why", why)
                                    .b("open", !picks.empty() && w.timeLeft() > 0).b("ambition", forAmb).raw("targets", arr(targets))
                                    .raw("gates", arr(gates)).done());
            }
            o.raw("plans", arr(plans));
        }

        chronicleInto(o);
        // The season so far, the same way.
        if (seasonStart.head != kNone && hh != kNone) {
            std::map<std::string, double> items;
            for (const auto& [why, total] : w.ledger()) {
                auto it = seasonStart.ledger.find(why);
                double d = total - (it == seasonStart.ledger.end() ? 0 : it->second);
                if (std::abs(d) >= 0.005) items[why] = d;
            }
            // ...and the barn (fun-r8 4): between two questions neither the purse nor the bread moves without a line.
            o.raw("ledger_now", booksJson(seasonKey(w.today()), seasonStart.money, w.households()[hh].money, items, w.foodSince(seasonStart),
                                                   w.grainTimes() - seasonStart.grainTimes, w.grainDays() - seasonStart.grainDays));
        }
        // What the last season came to, until the next one; and what has moved since, so the header's money adds up.
        std::string sum = summary;
        // seasonChanges says 這一季 / 這 N 季 while the season is running; shown afterwards it is the one just past.
        if (sum.rfind("這一季", 0) == 0) sum = "上一季" + sum.substr(std::string("這一季").size());
        else if (sum.rfind("這 ", 0) == 0) sum = "過去 " + sum.substr(std::string("這 ").size());
        else if (!sum.empty()) sum = "【上一季】\n" + sum;
        // Shown whenever the purse has moved since the season began, on a question as on the season's page: every
        // penny the header shows has its line.
        if (w.playerHead() != kNone && seasonStart.head != kNone &&
            std::floor(w.households()[w.playerHousehold()].money) != std::floor(seasonStart.money))
            sum += "【這一季到現在】" + w.moneySince(seasonStart) + "\n";
        o.text("summary", sum);
        if (!notice.empty()) o.text("notice", notice), notice.clear();
        return o.done();
    }

    // Waits for an answer; looks (a person, the family's knowledge) are answered on the spot.
    std::map<std::string, std::string> next() {
        std::string line;
        while (std::getline(std::cin, line)) {
            auto m = readLine(line);
            std::string what = m["do"];
            if (what == "inspect") {
                Id who = toInt(m, "who");
                if (who >= 0 && who < static_cast<Id>(w_.people().size()))
                    send(Obj().text("type", "person").n("id", who).text("text", w_.personReport(who)).done());
                continue;
            }
            if (what == "report") {
                send(Obj().text("type", "report").text("text", w_.familyReport()).done());
                continue;
            }
            if (!what.empty()) return m;
        }
        quit = true;
        return {{"do", "quit"}};
    }

private:
    World& w_;
};

}  // namespace

int main(int argc, char** argv) {
    std::string dataDir = "data", out, npcModel, npcLogPath, npcScope = "all";
    Config cfg;
    cfg.prerun = 25;
    int years = 100;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nextArg = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") dataDir = nextArg();
        else if (a == "--seed") cfg.seed = std::stoull(nextArg());
        else if (a == "--start") cfg.startYear = std::stoi(nextArg());
        else if (a == "--prerun") cfg.prerun = std::stoi(nextArg());
        else if (a == "--years") years = std::stoi(nextArg());
        else if (a == "--gentle") cfg.harshness = Harshness::Gentle;
        else if (a == "--origin") cfg.origin = nextArg();
        else if (a == "--out") out = nextArg();
        else if (a == "--npc-model") npcModel = nextArg();
        else if (a == "--npc-scope") npcScope = nextArg();
        else if (a == "--npc-log") npcLogPath = nextArg();
        else {
            std::cerr << "unknown argument " << a << "\n";
            return 2;
        }
    }
    Defs defs;
    try {
        defs = Defs::loadFromDir(dataDir);
    } catch (const std::exception& e) {
        std::cerr << "data error: " << e.what() << "\n";
        return 1;
    }
    World w(defs, cfg);
    Web web(w);
    // The village's years before the hand-over (Config::prerun) are not the family's story: start at the hand-over.
    while (web.shown < w.chronicle().size() && w.chronicle()[web.shown].day < w.handedOver()) ++web.shown;
    auto saveLog = [&]() {
        if (out.empty()) return;
        std::ofstream f(out);
        for (const auto& e : w.chronicle())
            if (e.day >= w.handedOver() && e.importance != 2) f << "- " << w.config().startYear + e.day / kDaysPerYear << " 年　" << tidyText(e.text) << "\n";
    };
    bool aiPlays = false;  // after quit: nobody is asked any more
    w.setChooser([&](const World::ChoicePrompt& c) {
        if (aiPlays || web.quit) return -1;
        web.send(web.state(false));
        std::vector<std::string> opts, closed;
        for (const auto& s : c.options) opts.push_back(str(s));
        for (const auto& s : c.unavailable) closed.push_back(str(s));
        web.send(Obj().text("type", "ask").text("kind", "choice").text("title", c.title).text("situation", c.situation)
                     .raw("options", arr(opts)).raw("unavailable", arr(closed)).done());
        while (true) {
            auto m = web.next();
            std::string what = m["do"];
            if (what == "quit") {
                aiPlays = true;
                return -1;
            }
            if (what == "ai") {
                web.answeredTitle.clear();  // the AI's answer is not shown as the player's
                return -1;
            }
            int k = toInt(m, "index", -1);
            if (what == "choose" && k >= 0 && k < static_cast<int>(c.options.size())) {
                // Remembered for the chronicle (Web::takeLine); a long option keeps only what comes before its （…）.
                std::string o = tidyText(c.options[k]);
                size_t paren = o.find("（");
                web.answeredTitle = c.title;
                web.answeredDay = w.today();
                web.answeredChoice = paren != std::string::npos && paren > 18 ? o.substr(0, paren) : o;
                return k;
            }
        }
    });
    NpcBridge npc;
    npc.attach(w, npcModel, npcLogPath, npcScope);

    int skipUntil = 0;
    bool routine = false;  // a year (or a regency) let run: routine questions go by the family's rules (World::setRoutineByRule)
    Id regencyFor = kNone;  // the child a regency skip is for: if they die or the house passes, the skip ends
    World::FamilySnapshot before = w.snapshot();
    for (int d = 0; d < years * kDaysPerYear && !w.gameOver() && !aiPlays; ++d) {
        if (regencyFor != kNone && w.households()[w.playerHousehold()].ward != regencyFor) {
            regencyFor = kNone;
            skipUntil = 0;
        }
        // A season passed over (a year let pass): what a question on the way shows of the money starts from it.
        if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0 && w.today() < skipUntil) web.seasonStart = w.snapshot();
        if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0 && w.today() >= skipUntil) {
            if (routine) {
                w.issue({w.today(), "rule", w.actingHead(), "routine:0", kNone, kNone});
                w.runCommands();
                routine = false;
            }
            w.startSeason();
            web.summary = w.seasonChanges(before);
            web.lastSeason = before;
            before = w.snapshot();
            web.seasonStart = before;
            saveLog();
            bool leave = false;
            while (!leave) {
                web.send(web.state(true));
                web.send(Obj().text("type", "ask").text("kind", "season").done());
                auto m = web.next();
                std::string what = m["do"];
                Id head = w.actingHead();  // the one keeping the house, a widow or stepfather for a child head
                // An action that does not say what it means is refused with what it lacks, not taken as something
                // else (a mistyped key was a visit to nobody, an unknown action passed the season).
                if (std::string bad = badAction(m); !bad.empty()) {
                    web.notice = bad;
                    continue;
                }
                if (what == "interact") {
                    w.issue({w.today(), "interact", head, m["event"], toInt(m, "who"), kNone});
                    w.runCommands();
                } else if (what == "ask") {
                    w.issue({w.today(), "ask", head, m["ask"] + "|" + m["office"] + "|" + m["lever"] + "|" + (toInt(m, "subject") == kNone ? "" : m["subject"]),
                             toInt(m, "who"), toInt(m, "object")});
                    w.runCommands();
                } else if (what == "want") {
                    w.issue({w.today(), "want", head, m["what"], kNone, toInt(m, "object")});
                    w.runCommands();
                } else if (what == "focus") {
                    w.issue({w.today(), "focus", head, m["focus"], toInt(m, "who"), kNone});
                    w.runCommands();
                } else if (what == "agenda") {
                    w.issue({w.today(), "agenda", head, m["id"], kNone, kNone});
                    w.runCommands();
                } else if (what == "plan") {
                    w.issue({w.today(), "start_plan", w.actingHead(), m["plan"], toInt(m, "target"), toInt(m, "subject")});
                    w.runCommands();
                } else if (what == "cancel") {
                    w.issue({w.today(), "cancel_plan", w.actingHead(), m["plan"], kNone, kNone});
                    w.runCommands();
                } else if (what == "rule") {
                    // 家裡的規矩: {"do":"rule","rule":"occasions","value":"close"}
                    w.issue({w.today(), "rule", head, m["rule"] + ":" + m["value"], kNone, kNone});
                    w.runCommands();
                } else if (what == "year") {
                    skipUntil = w.today() + kDaysPerYear;
                    regencyFor = kNone;
                    w.issue({w.today(), "rule", head, "routine:1", kNone, kNone});
                    w.runCommands();
                    routine = true;
                    leave = true;
                } else if (what == "regency") {
                    // Until the child the house is held for comes of age; questions are still asked on the way.
                    const Household& ph = w.households()[w.playerHousehold()];
                    if (ph.ward != kNone && w.people()[ph.ward].alive()) {
                        skipUntil = std::max(w.today() + 1, w.people()[ph.ward].birthDay + 16 * kDaysPerYear);
                        regencyFor = ph.ward;
                        w.issue({w.today(), "rule", head, "routine:1", kNone, kNone});
                        w.runCommands();
                        routine = true;
                    }
                    leave = true;
                } else if (what == "quit") {
                    aiPlays = true;
                    leave = true;
                } else {
                    leave = true;  // pass: on with the season
                }
            }
            if (aiPlays) break;
        }
        w.stepDay();
    }
    web.send(web.state(false));
    web.send(Obj().text("type", "end").n("year", w.year()).b("line_ended", w.gameOver()).done());
    saveLog();
    return 0;
}
