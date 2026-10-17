// Runs the village for N years and prints the player family's chronicle.
//
//   chronicle --data data --seed 7 --start 1330 --years 100 [--gentle] [--origin artisan]
//             [--village] [--out chronicle.md]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "simcore/autoplayer.h"
#include "simcore/world.h"

using namespace sim;

namespace {

std::string lifespan(const World& w, const Person& p) {
    int born = w.config().startYear + static_cast<int>(std::floor(p.birthDay / double(kDaysPerYear)));
    std::string s = std::to_string(born) + "–";
    if (!p.alive()) {
        int died = w.config().startYear + p.deathDay / kDaysPerYear;
        s += std::to_string(died) + "，" + p.deathCause;
    } else if (p.household == kNone) {
        s += "？（離開了村子）";
    } else {
        s += "在世";
    }
    return s;
}

std::string houseName(const std::string& h) {
    if (h == "cottage") return "茅屋";
    if (h == "timber") return "木屋";
    if (h == "stone") return "石屋";
    if (h == "manor") return "莊園宅邸";
    return h;
}

// A byname for a head, from what they did and suffered while head (Crusader Kings-style nickname).
// The rarest thing wins: a plague nurse or a wronged man is remembered for that, not for the one
// rick they burned. `reeveTerms` comes from the chapter's lines.
std::string nickname(const World& w, Id head, int from, int to, int reeveTerms = 0) {
    std::map<std::string, int> did, suffered;
    for (const auto& d : w.deeds()) {
        if (d.day < from || d.day >= to) continue;
        if (d.actor == head) ++did[d.type];
        if (d.target == head) ++suffered[d.type];
    }
    const Person& p = w.person(head);
    auto has = [&](const std::string& t) { return std::find(p.traits.begin(), p.traits.end(), t) != p.traits.end(); };
    if (suffered["wrongly_convicted"]) return "被冤枉的";
    if (did["helped_in_plague"]) return "瘟疫裡救人的";
    if (did["blackmailed"]) return "捏著把柄的";
    if (did["arson"] >= 2) return "放火的";
    if (did["embezzled"] >= 1) return "做假帳的";
    if (did["imposed_reeve"] >= 1) return "領主的人";
    if (suffered["convicted"]) return "吃過官司的";
    if (reeveTerms >= 3) return "十年莊頭";
    if (did["reconciled"] + did["mediated"] + did["made_amends"] >= 1) return "和事佬";
    if (suffered["land_seized"]) return "丟了地的";
    if (suffered["forgiven_by_lord"] >= 1) return "欠領主人情的";
    if (did["defaulted"] >= 1) return "賴帳的";
    if (did["affair"] >= 1) return "風流的";
    if (did["court_case"] >= 3) return "愛告狀的";
    if (did["land_bought"] >= 2) return "置地的";
    if (p.sex == Sex::Female && p.spouse != kNone && !w.person(p.spouse).alive()) return "守寡持家的";
    if (suffered["distrained"]) return "被牽了牛的";
    if (has("drunkard")) return "酒鬼";
    if (reeveTerms >= 1) return "當莊頭的";
    if (did["brawl"] >= 2) return "愛打架的";
    if (suffered["public_insult"] >= 2) return "受氣的";
    if (did["arson"] >= 1) return "心狠的";
    if (did["sworn_friend"] + suffered["sworn_friend"] >= 3) return "朋友多的";
    if (has("literate")) return "識字的";
    for (const auto& t : p.traits) {
        const TraitDef& td = w.defs().traits.at(t);
        if (td.category == "personality") return td.name + "的";
    }
    return "";
}

// A one-line look back at a head's time: children born and kin buried, debt, the reeve's office,
// and the house the family had the worst of it with.
std::string recap(const World& w, Id dyn, Id head, int from, int to, const std::vector<std::string>& lines) {
    // Close kin of the head: parents, spouse, siblings, children and grandchildren.
    const Person& hd = w.person(head);
    auto child = [&](const Person& p, Id of) { return of != kNone && (p.father == of || p.mother == of); };
    auto kin = [&](const Person& p) {
        if (p.id == head || p.id == hd.father || p.id == hd.mother || p.spouse == head || child(p, head)) return true;
        if (hd.father != kNone && p.father == hd.father) return true;
        for (Id par : {p.father, p.mother})
            if (par != kNone && child(w.person(par), head)) return true;
        return false;
    };
    int born = 0, buried = 0;
    for (const auto& p : w.people()) {
        if (!kin(p)) continue;
        if (p.birthDay >= from && p.birthDay < to) ++born;
        if (!p.alive() && p.deathDay >= from && p.deathDay < to) ++buried;
    }
    int debt = 0, reeve = 0;
    for (const auto& l : lines) {
        if (l.find("【欠債】") != std::string::npos || l.find("〔算總帳〕") != std::string::npos) ++debt;
        if (l.find("當上了莊頭") != std::string::npos || l.find("連任莊頭") != std::string::npos ||
            l.find("又當了三年莊頭") != std::string::npos)
            ++reeve;
    }
    std::map<Id, double> grudge;
    for (const auto& d : w.deeds()) {
        if (d.day < from || d.day >= to || d.target == kNone || d.actor == kNone) continue;
        const Person& t = w.person(d.target);
        const Person& a = w.person(d.actor);
        if (t.dynasty != dyn || a.dynasty == dyn || a.dynasty == kNone) continue;
        const DeedDef* def = w.defs().deed(d.type);
        if (!def || def->visibility == "secret") continue;  // the family only counts what it saw
        auto it = def->roles.find("target");
        if (it != def->roles.end() && it->second < 0) grudge[a.dynasty] -= it->second;
    }
    Id worst = kNone;
    for (const auto& [k, v] : grudge)
        if (v >= 30 && (worst == kNone || v > grudge[worst])) worst = k;
    std::vector<std::string> parts;
    if (born || buried)
        parts.push_back((born ? "兒孫添了 " + std::to_string(born) + " 口" : std::string("沒添丁")) +
                        (buried ? "，送走了 " + std::to_string(buried) + " 個親人" : ""));
    if (debt) parts.push_back("為欠債發愁了 " + std::to_string(debt) + " 回");
    if (reeve) parts.push_back("當了 " + std::to_string(reeve) + " 任莊頭");
    if (worst != kNone) parts.push_back("吃 " + w.dynasties()[worst].surname + " 家的虧最多");
    if (parts.empty()) return "";
    std::string s = "> 這一代：";
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? "；" : "") + parts[i];
    return s + "。\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataDir = "data", out;
    Config cfg;
    cfg.prerun = 25;
    int years = 100;
    bool village = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") dataDir = next();
        else if (a == "--seed") cfg.seed = std::stoull(next());
        else if (a == "--start") cfg.startYear = std::stoi(next());
        else if (a == "--prerun") cfg.prerun = std::stoi(next());
        else if (a == "--years") years = std::stoi(next());
        else if (a == "--gentle") cfg.harshness = Harshness::Gentle;
        else if (a == "--origin") cfg.origin = next();
        else if (a == "--village") village = true;
        else if (a == "--out") out = next();
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

    AutoPlayer player(cfg.seed);
    std::map<int, int> population;
    for (int d = 0; d < years * kDaysPerYear && !w.gameOver(); ++d) {
        if (w.today() % kDaysPerYear == 0) population[w.year()] = w.livingCount();
        player.act(w);
        w.stepDay();
    }

    std::ostringstream md;
    const Dynasty& dyn = w.dynasties()[w.playerDynasty()];
    md << "# " << dyn.surname << " 家族編年史\n\n";
    md << "種子 " << cfg.seed << "，" << cfg.startYear << " 年開局，模擬 " << years << " 年，"
       << (cfg.harshness == Harshness::Gentle ? "溫和" : "史實") << "模式，出身 " << cfg.origin << "。\n\n";

    // One chapter per head of the family, titled with a byname earned in their time.
    std::vector<std::pair<int, Id>> chapters;
    std::vector<Id> chapterWard;  // a regent's chapter: the child they held the house for
    for (size_t i = 0; i < dyn.heads.size(); ++i) {
        int since = i < dyn.headSince.size() ? dyn.headSince[i] : 0;
        auto held = dyn.heldFor.find(i);
        Id ward = held != dyn.heldFor.end() ? held->second : kNone;
        if (!chapters.empty() && chapters.back().first == since) chapters.back().second = dyn.heads[i], chapterWard.back() = ward;
        else chapters.push_back({since, dyn.heads[i]}), chapterWard.push_back(ward);
    }
    size_t chapter = 0;
    std::vector<int> reeveTerms(chapters.size(), 0);
    for (const auto& e : w.chronicle()) {
        size_t c = 0;
        while (c + 1 < chapters.size() && e.day >= chapters[c + 1].first) ++c;
        if (c < chapters.size() && (e.text.find("當上了莊頭") != std::string::npos || e.text.find("連任莊頭") != std::string::npos ||
                                    e.text.find("又當了三年莊頭") != std::string::npos))
            ++reeveTerms[c];
    }
    auto chapterTitle = [&](size_t c) {
        Id head = chapters[c].second;
        int from = chapters[c].first;
        int to = c + 1 < chapters.size() ? chapters[c + 1].first : w.today() + 1;
        std::string nick = nickname(w, head, from, to, reeveTerms[c]);
        std::string years = std::to_string(w.config().startYear + from / kDaysPerYear) + "–" +
                            (c + 1 < chapters.size() || w.gameOver() ? std::to_string(w.config().startYear + to / kDaysPerYear) : "");
        int generation = 0;
        for (size_t k = 0; k <= c; ++k) generation += chapterWard[k] == kNone ? 1 : 0;
        if (chapterWard[c] != kNone)  // a regency is not a generation of its own
            return "## 代管　" + std::string(nick.empty() ? "" : "「" + nick + "」") + w.fullName(head) + "（替 " + w.fullName(chapterWard[c]) +
                   " 持家 " + years + "）";
        return "## 第 " + std::to_string(generation) + " 代　" + (nick.empty() ? "" : "「" + nick + "」") + w.fullName(head) +
               "（家主 " + years + "）";
    };
    if (!chapters.empty()) md << chapterTitle(0) << "\n";
    int lastYear = -1;
    std::vector<std::string> chapterLines;
    auto closeChapter = [&](size_t c) {
        if (c >= chapters.size()) return;
        int to = c + 1 < chapters.size() ? chapters[c + 1].first : w.today() + 1;
        std::string r = recap(w, w.playerDynasty(), chapters[c].second, chapters[c].first, to, chapterLines);
        if (!r.empty()) md << "\n" << r;
        chapterLines.clear();
    };
    for (const auto& e : w.chronicle()) {
        if (e.importance == 2 && !village) continue;
        while (chapter + 1 < chapters.size() && e.day >= chapters[chapter + 1].first) {
            closeChapter(chapter);
            ++chapter;
            md << "\n" << chapterTitle(chapter) << "\n";
            lastYear = -1;
        }
        chapterLines.push_back(e.text);
        int y = w.config().startYear + e.day / kDaysPerYear;
        if (y != lastYear) {
            md << "\n### " << y << " 年\n\n";
            lastYear = y;
        }
        const char* prefix = e.importance == 3 ? "**史事**　" : e.importance == 2 ? "村裡　" : "";
        md << "- " << prefix << e.text << "\n";
    }

    closeChapter(chapter);
    md << "\n## 歷代家主\n\n";
    int gen = 1;
    for (size_t c = 0; c < chapters.size(); ++c) {
        Id h = chapters[c].second;
        const Person& p = w.person(h);
        int to = c + 1 < chapters.size() ? chapters[c + 1].first : w.today() + 1;
        std::string nick = nickname(w, h, chapters[c].first, to, reeveTerms[c]);
        // One who held the house for a child is not counted among the heads.
        if (chapterWard[c] != kNone) md << "   （代管）";
        else md << gen++ << ". ";
        md << (nick.empty() ? "" : "「" + nick + "」") << w.fullName(h) << "（" << lifespan(w, p) << "）";
        std::string traits;
        for (const auto& t : p.traits) traits += (traits.empty() ? "" : "、") + defs.traits.at(t).name;
        if (!traits.empty()) md << "：" << traits;
        md << "\n";
    }

    md << "\n## 家族傳統\n\n";
    if (dyn.traditions.empty()) md << "（還沒有）\n";
    for (const auto& t : dyn.traditions) md << "- " << defs.traditions.at(t).name << "：" << defs.traditions.at(t).desc << "\n";

    md << "\n## 傳家之物\n\n";
    bool anyItem = false;
    for (const auto& it : w.items()) {
        if (it.lost || it.household != w.playerHousehold()) continue;
        anyItem = true;
        md << "- " << defs.items.at(it.type).name;
        if (it.maker != kNone) md << "，" << w.fullName(it.maker)
                                  << (it.type == "pilgrim_badge" || it.type == "longbow" ? " 帶回" : " 所製");
        md << "，經手 " << it.owners.size() << " 代家主\n";
    }
    if (!anyItem) md << "（沒有）\n";

    md << "\n## 和別家的恩怨\n\n";
    int feudsShown = 0;
    for (const auto& f : w.feuds()) {
        if (f.stage < 1 || (f.a != w.playerDynasty() && f.b != w.playerDynasty())) continue;
        Id other = f.a == w.playerDynasty() ? f.b : f.a;
        const char* stage = f.stage >= 3 ? "世仇" : f.stage == 2 ? "結怨" : "嫌隙";
        md << "- " << w.dynasties()[other].surname << " 家：" << stage << "（從" << (f.cause != kNone ? w.describeDeed(f.cause) : std::string("上一代"));
        if (f.last != kNone && f.last != f.cause) md << "起，最近一次是" << w.describeDeed(f.last);
        else md << "起";
        md << "）\n";
        ++feudsShown;
    }
    if (feudsShown == 0) md << "（目前沒有）\n";

    // Developer view: the true chains of cause and effect, secrets included.
    md << "\n## 恩怨的來龍去脈（開發用：系統知道的事實，包含秘密）\n\n";
    const auto& deeds = w.deeds();
    std::vector<int> depth(deeds.size(), 1);
    for (size_t i = 0; i < deeds.size(); ++i)
        if (deeds[i].cause != kNone) depth[i] = depth[deeds[i].cause] + 1;
    auto involvesPlayer = [&](const Deed& d) {
        for (Id x : {d.actor, d.target})
            if (x != kNone && w.person(x).dynasty == w.playerDynasty()) return true;
        return false;
    };
    std::vector<size_t> ends;
    for (size_t i = 0; i < deeds.size(); ++i)
        if (depth[i] >= 3 && involvesPlayer(deeds[i])) ends.push_back(i);
    std::stable_sort(ends.begin(), ends.end(), [&](size_t a, size_t b) { return depth[a] > depth[b]; });
    std::vector<Id> usedRoots;
    int shown = 0;
    for (size_t e : ends) {
        std::vector<Id> chain;
        for (Id k = static_cast<Id>(e); k != kNone; k = deeds[k].cause) chain.push_back(k);
        if (std::find(usedRoots.begin(), usedRoots.end(), chain.back()) != usedRoots.end()) continue;
        usedRoots.push_back(chain.back());
        md << "- ";
        for (size_t k = chain.size(); k-- > 0;) {
            const Deed& d = deeds[chain[k]];
            const DeedDef* def = defs.deed(d.type);
            md << (w.config().startYear + d.day / kDaysPerYear) << " 年 " << w.fullName(d.actor);
            if (d.target != kNone) md << " → " << w.fullName(d.target);
            md << "：" << (def ? def->name : d.type);
            if (def && def->visibility == "secret") md << "（秘密）";
            if (k > 0) md << "　⟶　";
        }
        md << "\n";
        if (++shown == 6) break;
    }
    if (shown == 0) md << "（沒有超過兩步的連鎖）\n";

    // Developer view: what the player's family believes about secrets, against the facts.
    md << "\n## 家裡相信的秘密，對照事實（開發用）\n\n";
    std::set<Id> shownDeeds;
    int beliefsShown = 0;
    if (!w.gameOver())
        for (Id m : w.household(w.playerHousehold()).members)
            for (const auto& b : w.beliefsOf(m)) {
                const Deed& d = w.deeds()[b.deed];
                if (b.confidence < 50 || d.actor == m || !shownDeeds.insert(b.deed).second) continue;
                const DeedDef* def = defs.deed(d.type);
                md << "- " << (w.config().startYear + d.day / kDaysPerYear) << " 年 " << w.fullName(d.target) << " 遭「"
                   << (def ? def->name : d.type) << "」：家裡相信是 " << w.fullName(b.culprit) << "（可信度 "
                   << int(b.confidence) << "）。事實：" << w.fullName(d.actor)
                   << (b.culprit == d.actor ? "，沒錯。" : "，認錯人了。") << "\n";
                ++beliefsShown;
            }
    if (beliefsShown == 0) md << "（沒有）\n";

    md << "\n## 結局\n\n";
    if (w.gameOver()) {
        md << "家族在 " << w.year() << " 年斷絕。\n";
    } else {
        const Household& h = w.household(w.playerHousehold());
        md << w.year() << " 年，家主 " << w.fullName(h.head) << "，家中 " << h.members.size() << " 人，"
           << int(h.acres) << " 畝地，" << int(h.money) << " 便士，住" << houseName(h.house) << "。家族聲望 "
           << int(dyn.prestige) << "。";
        if (h.debt > 0.5) md << "還欠著 " << int(h.debt) << " 便士的債。";
        md << "\n";
    }
    md << "\n村莊人口：";
    int k = 0;
    for (const auto& [y, n] : population)
        if (k++ % 10 == 0) md << y << " 年 " << n << " 人　";
    md << "\n";

    if (out.empty()) std::cout << md.str();
    else std::ofstream(out) << md.str();
    return 0;
}
