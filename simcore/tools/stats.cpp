// Runs many seeds and prints aggregate statistics, to see whether a rule change moved the numbers.
//
//   stats --data data --seeds 200 [--first 1] --start 1340 --years 100 [--origin peasant] [--gentle]
//         [--threads 8] [--out stats.md]
//
// Reports family survival, wealth, pacing (chronicle lines per year), how often each event and
// plan happens, and events that never fire.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "simcore/autoplayer.h"
#include "simcore/world.h"

using namespace sim;

namespace {

struct RunResult {
    bool survived = false;
    int endYear = 0;
    int heads = 0;
    double acres = 0, money = 0;
    std::string house;
    double headLifespan = 0;
    int familyLines = 0;   // chronicle entries about the family
    int choiceLines = 0;   // of which the player family made a choice
    int quietYears = 0;    // years with no family entry at all
    int busyYears = 0;     // years with 12+ family entries
    int familyDeeds = 0;
    int wrongBeliefs = 0, beliefs = 0;
    std::map<std::string, int> counters;
    std::map<std::string, int> deedTypes;
    int standing = 0;        // the family's rung on the ladder at the end
    bool church = false, town = false;
    std::string lastCause;  // how the last head died, when the line ended
    int seed = 0;
};

RunResult runOne(const Defs& defs, Config cfg, int years) {
    World w(defs, cfg);
    AutoPlayer player(cfg.seed);
    for (int d = 0; d < years * kDaysPerYear && !w.gameOver(); ++d) {
        player.act(w);
        w.stepDay();
    }
    RunResult r;
    r.survived = !w.gameOver();
    r.endYear = w.year();
    r.seed = static_cast<int>(cfg.seed);
    const Dynasty& dyn = w.dynasties()[w.playerDynasty()];
    if (!r.survived && !dyn.heads.empty()) r.lastCause = w.person(dyn.heads.back()).deathCause;
    r.heads = static_cast<int>(dyn.heads.size());
    double life = 0;
    int dead = 0;
    for (Id h : dyn.heads) {
        const Person& p = w.person(h);
        if (!p.alive()) {
            life += (p.deathDay - p.birthDay) / double(kDaysPerYear);
            ++dead;
        }
    }
    r.headLifespan = dead ? life / dead : 0;
    if (w.playerHousehold() != kNone) {
        const Household& h = w.household(w.playerHousehold());
        r.acres = h.acres;
        r.money = h.money;
        r.house = h.house;
        r.standing = w.standing(w.playerHousehold());
        r.church = w.kinInChurch(w.playerDynasty());
        r.town = w.kinInTown(w.playerDynasty());
    }
    std::map<int, int> perYear;
    for (const auto& e : w.chronicle()) {
        if (e.importance != 1) continue;
        ++r.familyLines;
        if (e.text.find("（選擇：") != std::string::npos) ++r.choiceLines;
        ++perYear[e.day / kDaysPerYear];
    }
    int lastYear = (w.today() - 1) / kDaysPerYear;
    for (int y = 0; y <= lastYear; ++y) {
        int n = perYear.count(y) ? perYear[y] : 0;
        if (n == 0) ++r.quietYears;
        if (n >= 12) ++r.busyYears;
    }
    for (const auto& d : w.deeds()) {
        ++r.deedTypes[d.type];
        for (Id x : {d.actor, d.target})
            if (x != kNone && w.person(x).dynasty == w.playerDynasty()) {
                ++r.familyDeeds;
                break;
            }
    }
    for (const auto& p : w.people()) {
        if (p.dynasty != w.playerDynasty() || !p.alive()) continue;
        for (const auto& b : w.beliefsOf(p.id)) {
            if (b.confidence < 50) continue;
            // Whodunits only: secret wrongs (not affairs, which the spouse learns from the source).
            const Deed& d = w.deeds()[b.deed];
            const DeedDef* def = w.defs().deed(d.type);
            if (!def || def->visibility != "secret" || d.type == "affair" || d.actor == p.id) continue;
            ++r.beliefs;
            if (b.culprit != w.deeds()[b.deed].actor) ++r.wrongBeliefs;
        }
    }
    r.counters = w.counters();
    return r;
}

double pct(int a, int b) { return b ? 100.0 * a / b : 0; }

}  // namespace

int main(int argc, char** argv) {
    std::string dataDir = "data", out;
    Config base;
    base.startYear = 1340;
    int years = 100, seeds = 100, first = 1;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") dataDir = next();
        else if (a == "--seeds") seeds = std::stoi(next());
        else if (a == "--first") first = std::stoi(next());
        else if (a == "--start") base.startYear = std::stoi(next());
        else if (a == "--years") years = std::stoi(next());
        else if (a == "--gentle") base.harshness = Harshness::Gentle;
        else if (a == "--origin") base.origin = next();
        else if (a == "--threads") threads = static_cast<unsigned>(std::stoi(next()));
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

    std::vector<RunResult> results(seeds);
    std::atomic<int> nextSeed{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (int i; (i = nextSeed++) < seeds;) {
                Config cfg = base;
                cfg.seed = static_cast<uint64_t>(first + i);
                results[i] = runOne(defs, cfg, years);
            }
        });
    for (auto& t : pool) t.join();

    int survived = 0, heads = 0, lines = 0, choices = 0, quiet = 0, busy = 0, famDeeds = 0, wrong = 0, bel = 0;
    double acres = 0, money = 0, life = 0;
    std::map<std::string, int> houses, counters, deedTypes;
    std::vector<int> extinctYears;
    std::map<std::string, int> endCauses;
    std::string endSeeds;
    std::vector<double> acreList;
    std::map<int, int> rungs;
    int church = 0, town = 0;
    for (const auto& r : results) {
        if (r.survived) {
            ++survived;
            ++rungs[r.standing];
            church += r.church;
            town += r.town;
            acres += r.acres;
            money += r.money;
            ++houses[r.house];
            acreList.push_back(r.acres);
        } else {
            extinctYears.push_back(r.endYear);
            ++endCauses[r.lastCause.empty() ? "?" : r.lastCause];
            endSeeds += (endSeeds.empty() ? "" : " ") + std::to_string(r.seed);
        }
        heads += r.heads;
        life += r.headLifespan;
        lines += r.familyLines;
        choices += r.choiceLines;
        quiet += r.quietYears;
        busy += r.busyYears;
        famDeeds += r.familyDeeds;
        wrong += r.wrongBeliefs;
        bel += r.beliefs;
        for (const auto& [k, v] : r.counters) counters[k] += v;
        for (const auto& [k, v] : r.deedTypes) deedTypes[k] += v;
    }
    std::sort(acreList.begin(), acreList.end());
    std::sort(extinctYears.begin(), extinctYears.end());
    double n = seeds;
    double yearsRun = 0;
    for (const auto& r : results) yearsRun += r.endYear - base.startYear;

    std::ostringstream md;
    md << std::fixed << std::setprecision(1);
    md << "# SimCore 統計：" << seeds << " 個種子（" << first << "–" << first + seeds - 1 << "），" << base.startYear
       << " 年開局，" << years << " 年，出身 " << base.origin << "，"
       << (base.harshness == Harshness::Gentle ? "溫和" : "史實") << "模式\n\n";
    md << "## 家族\n\n";
    md << "- 撐到最後：" << pct(survived, seeds) << "%";
    if (!extinctYears.empty()) md << "；斷絕的中位年份 " << extinctYears[extinctYears.size() / 2];
    if (!endCauses.empty()) {
        md << "（最後一任家主死於：";
        for (const auto& [k, v] : endCauses) md << k << " " << v << "　";
        md << "；種子 " << endSeeds << "）";
    }
    md << "\n- 平均家主數 " << heads / n << "，家主平均壽命 " << life / n << " 歲\n";
    if (survived) {
        md << "- 存活家族的田地：平均 " << acres / survived << " 畝，中位 " << acreList[acreList.size() / 2]
           << "，最多 " << acreList.back() << "；平均存錢 " << money / survived << " 便士\n";
        md << "- 最後的家族地位：";
        for (const auto& [k, v] : rungs) md << World::standingName(k) << " " << pct(v, survived) << "%　";
        md << "；家裡有人在教會 " << pct(church, survived) << "%，有人在城裡 " << pct(town, survived) << "%\n";
        md << "- 房子：";
        for (const auto& [k, v] : houses) md << k << " " << pct(v, survived) << "%　";
        md << "\n";
    }
    md << "\n## 節奏（玩家家族的編年史條目）\n\n";
    md << "- 每年 " << lines / yearsRun << " 條，其中有選擇的 " << choices / yearsRun << " 條\n";
    md << "- 完全沒事的年份 " << pct(quiet, static_cast<int>(yearsRun)) << "%，12 條以上的年份 "
       << pct(busy, static_cast<int>(yearsRun)) << "%\n";
    md << "- 和家族有關的行為紀錄每局 " << famDeeds / n << " 筆；家人深信（≥50）的秘密（不含私通） " << bel / n << " 件，其中認錯人 "
       << pct(wrong, bel) << "%\n";

    md << "\n## 事件（每局平均次數：全村 / 玩家家族）\n\n| 事件 | 全村 | 家族 |\n|---|---|---|\n";
    std::vector<std::string> never;
    for (const auto& ev : defs.events) {
        int all = counters.count("event:" + ev.id) ? counters["event:" + ev.id] : 0;
        int fam = counters.count("event_family:" + ev.id) ? counters["event_family:" + ev.id] : 0;
        if (all == 0) {
            never.push_back(ev.id);
            continue;
        }
        md << "| " << ev.id << " | " << std::setprecision(2) << all / n << " | " << fam / n << " |\n";
    }
    md << std::setprecision(1);
    md << "\n從沒觸發過：" << (never.empty() ? "（無）" : "");
    for (size_t i = 0; i < never.size(); ++i) md << (i ? "、" : "") << never[i];
    md << "\n\n## 計畫（每局平均：開始 / 家族開始 / 成功 / 失敗 / 敗露）\n\n| 計畫 | 開始 | 家族 | 成功 | 失敗 | 敗露 |\n|---|---|---|---|---|---|\n";
    md << std::setprecision(2);
    for (const auto& pd : defs.plans) {
        auto c = [&](const std::string& k) { return counters.count(k) ? counters[k] / n : 0.0; };
        std::string p = "plan:" + pd.id + ":";
        md << "| " << pd.id << " | " << c(p + "start") << " | " << c("plan_family:" + pd.id + ":start") << " | "
           << c(p + "success") << " | " << c(p + "fail") << " | " << c(p + "exposed") << " |\n";
    }
    md << std::setprecision(2);
    auto cnt = [&](const std::string& k) { return counters.count(k) ? counters[k] / n : 0.0; };
    md << "\n## 志向（每局平均：立下 / 完成 / 放下；玩家家族 | 全村其他人家）\n\n| 志向 | 家族立 | 家族成 | 家族放 | 村立 | 村成 | 村放 |\n|---|---|---|---|---|---|---|\n";
    for (const auto& a : defs.ambitions) {
        std::string f = "ambition_family:" + a.id + ":", v = "ambition:" + a.id + ":";
        md << "| " << a.id << " | " << cnt(f + "chosen") << " | " << cnt(f + "done") << " | " << cnt(f + "dropped") << " | "
           << cnt(v + "chosen") << " | " << cnt(v + "done") << " | " << cnt(v + "dropped") << " |\n";
    }
    md << "\n往上爬一階（每局平均，家族 / 全村其他）：";
    for (int k = 1; k <= 3; ++k)
        md << World::standingName(k) << " " << cnt("rise_family:" + std::to_string(k)) << " / " << cnt("rise:" + std::to_string(k)) << "　";
    md << "\n\n## 人心（每局平均）\n\n";
    md << "- 幫朋友 " << cnt("help_given") << " 次，其中幫到你家 " << cnt("help_to_player") << " 次\n";
    md << "- 決定者：問了 " << cnt("npc_decider:asked") << " 次，回答 " << cnt("npc_decider:answered") << " 次\n";
    md << "- 年度打算：";
    std::vector<std::pair<int, std::string>> intents;
    for (const auto& [k, v] : counters)
        if (k.rfind("intent:", 0) == 0) intents.push_back({v, k.substr(7)});
    std::sort(intents.rbegin(), intents.rend());
    for (const auto& [v, k] : intents) md << k << " " << v / n << "　";
    md << "\n- 打算化成計畫：";
    for (const auto& [k, v] : counters)
        if (k.rfind("intent_acted:", 0) == 0) md << k.substr(13) << " " << v / n << "　";
    // The contested ledger (design/contested.md): what houses set their hearts on, and how often two wanted the same.
    md << "\n- 想要（其中跟別家撞上）：";
    for (const char* k : {"parcel", "office", "match", "place"})
        md << k << " " << cnt(std::string("want:") + k) << "（" << cnt(std::string("contest:") + k) << "）　";
    md << "\n- 開口：";
    for (const auto& [k, v] : counters)
        if (k.rfind("ask:", 0) == 0) md << k.substr(4) << " " << v / n << "　";
    // The season turn (design/season-loop.md): what the head did with the free days, what was on people's minds.
    md << "\n\n## 季回合（每局平均）\n\n";
    double seasons = cnt("season");
    md << "- 季數 " << seasons << "，家裡有人有心事的季 " << (seasons > 0 ? 100 * cnt("season_concern") / seasons : 0)
       << "%，有人想要你做什麼的季 " << (seasons > 0 ? 100 * cnt("season_wish") / seasons : 0) << "%\n";
    md << "- 家人的願望：做到 " << cnt("want_met") << "，冷落 " << cnt("want_neglected") << "；";
    for (const auto& [k, v] : counters)
        if (k.rfind("want:", 0) == 0) md << k.substr(5) << " " << v / n << "　";
    md << "\n";
    md << "- 找人：";
    std::vector<std::pair<int, std::string>> talks;
    for (const auto& [k, v] : counters)
        if (k.rfind("interaction:", 0) == 0) talks.push_back({v, k.substr(12)});
    std::sort(talks.rbegin(), talks.rend());
    for (const auto& [v, k] : talks) md << k << " " << v / n << "　";
    md << "\n- 心事（全村新增）：";
    for (const auto& [k, v] : counters)
        if (k.rfind("concern:", 0) == 0) md << k.substr(8) << " " << v / n << "　";
    md << std::setprecision(1);
    md << "\n\n## 行為紀錄類型（每局平均）\n\n";
    std::vector<std::pair<int, std::string>> dt;
    for (const auto& [k, v] : deedTypes) dt.push_back({v, k});
    std::sort(dt.rbegin(), dt.rend());
    for (const auto& [v, k] : dt) md << k << " " << v / n << "　";
    md << "\n";

    if (out.empty()) {
        std::cout << md.str();
    } else {
        std::ofstream(out) << md.str();
        std::cerr << "wrote " << out << "\n";
    }
    return 0;
}
