// Play the sim as the head of the family, in the terminal. The world runs a season at a time;
// whenever an event asks the head to choose, it stops and asks you. Between seasons you can look
// at what your family knows, or set a plan in motion.
//
//   play [--data data] [--seed N] [--start 1340] [--origin peasant|artisan] [--gentle] [--years 100] [--out log.md] [--stop]
//        [--npc-model "python3 tools/npc_decider.py"] [--npc-log npc.txt] [--npc-scope spotlight|near|all]
//
// Villagers in the spotlight (those with business with your family right now) can have a model choose
// among the options the rules allow: --npc-model runs a command per choice (JSON on stdin, "index<TAB>
// reason" on stdout). --npc-log records every such answer, and replays them on the next run, so a game
// played with a model replays exactly without calling it again. --npc-scope says who the model decides
// for: every villager (default), the near ring and spotlight, or the spotlight only.
//
// Input can be piped (one answer per line); when input runs out, the AI plays on to the end, or with
// --stop the program exits there. The same seed and answers always replay the same game, so a
// script (or an agent) can play by re-running with one more answer each time.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>

#include "simcore/autoplayer.h"
#include "simcore/events.h"
#include "simcore/world.h"
#include "npc_bridge.h"

using namespace sim;

namespace {

// Everything printed goes through tidyText a line at a time, so names read as Chinese ("彼得·阿特伍德開始")
// though the sim writes spaces around them.
class TidyBuf : public std::streambuf {
public:
    explicit TidyBuf(std::streambuf* to) : to_(to) {}
    ~TidyBuf() override { sync(); }

protected:
    int overflow(int c) override {
        if (c == EOF) return 0;
        line_ += static_cast<char>(c);
        if (c == '\n') sync();
        return c;
    }
    int sync() override {
        if (line_.empty()) return to_->pubsync();
        std::string t = tidyText(line_);
        line_.clear();
        to_->sputn(t.data(), static_cast<std::streamsize>(t.size()));
        return to_->pubsync();
    }

private:
    std::streambuf* to_;
    std::string line_;
};

const char* seasonName(int month) {
    static const char* names[4] = {"冬末（1–3 月）", "春（4–6 月）", "夏收（7–9 月）", "秋（10–12 月）"};
    return names[(month / 3) % 4];
}

bool eof = false;
bool stopAtEof = false;  // --stop: when input runs out, stop instead of letting the AI play on
struct StopPlay {};      // thrown to unwind out of the world when --stop input runs out

std::string ask(const std::string& prompt) {
    if (eof) return "";
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        eof = true;
        if (stopAtEof) {
            std::cout << "\n（輸入結束，停在這裡）\n";
            throw StopPlay{};
        }
        std::cout << "\n（輸入結束，接下來由 AI 代玩到最後）\n";
        return "";
    }
    return line;
}

int number(const std::string& s) {
    try {
        size_t used = 0;
        int n = std::stoi(s, &used);
        return used == s.size() ? n : -1;
    } catch (...) {
        return -1;
    }
}

// The people who matter to the family right now; pick one to see what they want and why.
void inspectMenu(World& w) {
    std::vector<Id> note = w.peopleOfNote();
    if (note.size() > 15) note.resize(15);
    if (note.empty()) {
        std::cout << "眼下沒有誰跟你家特別有關係。\n";
        return;
    }
    for (size_t i = 0; i < note.size(); ++i)
        std::cout << "  " << i + 1 << ". " << (w.seemsBusy(note[i]) ? "★ " : "　 ") << w.fullName(note[i]) << "："
                  << w.personLine(note[i]) << "\n";
    int n = number(ask("看誰（直接 Enter 取消）："));
    if (n < 1 || n > static_cast<int>(note.size())) return;
    std::cout << w.personReport(note[n - 1]);
}

// 找人: pick someone, then what to do with them; they answer in their own way (design/season-loop.md).
// Returns true when something was done, so the caller can show what came of it.
bool seeMenu(World& w, const std::function<void()>& flush) {
    Id head = w.playerHead();
    if (head == kNone) return false;
    if (w.timeLeft() <= 0) {
        std::cout << "這一季已經沒有空閒了。\n";
        return false;
    }
    std::vector<Id> people = w.peopleToSee();
    if (people.empty()) {
        std::cout << "身邊沒有能去找的人。\n";
        return false;
    }
    for (size_t i = 0; i < people.size(); ++i) {
        Id id = people[i];
        const Person& p = w.people()[id];
        std::string kin = w.kinWord(head, id);
        World::MoodView mv = w.moodView(p);
        std::string want = w.wantText(p);
        std::cout << "  " << i + 1 << ". " << w.fullName(id) << (kin.empty() ? "" : "（" + kin + "）") << "：" << mv.word
                  << "；對你" << opinionWord(w.opinion(id, head)) << (want.empty() ? "" : "；" + want) << "\n";
    }
    int n = number(ask("找誰（直接 Enter 取消）："));
    if (n < 1 || n > static_cast<int>(people.size())) return false;
    Id who = people[n - 1];
    std::vector<World::Interaction> ins = w.interactionsWith(who);
    std::vector<const EventDef*> open;
    EventRunner er(w);
    const Person& wp = w.people()[who];
    const WantDef* want = w.defs().want(wp.want);
    for (const auto& in : ins) {
        std::string desc = er.format(in.ev->interaction.stringOr("desc", ""), wp, head);
        bool wanted = want && std::find(want->answeredBy.begin(), want->answeredBy.end(), in.ev->id) != want->answeredBy.end();
        if (in.open) {
            open.push_back(in.ev);
            std::cout << "  " << open.size() << ". " << in.ev->title << (desc.empty() ? "" : "：" + desc)
                      << (wanted ? "　← 正是" + std::string(wp.sex == Sex::Male ? "他" : "她") + "想要的" : "") << "\n";
        } else {
            std::cout << "  　 " << in.ev->title << "（" << in.why << "）\n";
        }
    }
    // 開口: ask for something they hold or decide (design/contested.md 3), numbered after the interactions.
    std::vector<AskOption> asks;
    for (const auto& a : w.asksFor(who)) {
        if (a.open) {
            asks.push_back(a);
            std::cout << "  " << open.size() + asks.size() << ". 開口：" << a.label << "\n";
        } else {
            std::cout << "  　 開口：" << a.label << "（" << a.why << "）\n";
        }
    }
    if (open.empty() && asks.empty()) {
        std::cout << "現在跟" << w.shortName(wp.id) << "沒什麼能做的。\n";
        return false;
    }
    int k = number(ask("做什麼（直接 Enter 取消）："));
    if (k < 1 || k > static_cast<int>(open.size() + asks.size())) return false;
    if (k <= static_cast<int>(open.size())) {
        w.issue({w.today(), "interact", head, open[k - 1]->id, who, kNone});
        w.runCommands();
        return true;
    }
    const AskOption& a = asks[static_cast<size_t>(k) - open.size() - 1];
    auto askWith = [&](const std::string& lever) {
        w.issue({w.today(), "ask", head, a.ask + "|" + a.office + "|" + lever + "|" + (a.subject == kNone ? "" : std::to_string(a.subject)), who, a.object});
        w.runCommands();
        flush();  // the answer, as the log has it
    };
    askWith("");
    // Not a yes: the levers the family has on them, if any; each costs another free day.
    const AskResult& r = w.lastAsk();
    if (r.answer >= 2 && !r.levers.empty() && w.timeLeft() > 0) {
        static const std::map<std::string, std::string> name = {{"gift", "送點禮再說"}, {"favour", "提他欠的人情"}, {"hook", "拿把柄壓他"}, {"meet", "答應他的條件"}};
        std::vector<std::string> lv = r.levers;
        for (size_t i = 0; i < lv.size(); ++i) std::cout << "  " << i + 1 << ". " << (name.count(lv[i]) ? name.at(lv[i]) : lv[i]) << "\n";
        int l = number(ask("再試一次（直接 Enter 算了）："));
        if (l >= 1 && l <= static_cast<int>(lv.size())) askWith(lv[static_cast<size_t>(l) - 1]);
    }
    return true;
}

void planMenu(World& w) {
    Id head = w.actingHead();
    if (head == kNone) return;
    if (const ActivePlan* ap = w.activePlan(head)) {
        const PlanDef* pd = w.defs().plan(ap->plan);
        std::cout << "正在進行「" << pd->name << "」" << (ap->target != kNone ? "，對象 " + w.fullName(ap->target) : "")
                  << "（進度 " << int(ap->progress) << "/100）。輸入 c 可以放棄。\n";
        return;
    }
    std::vector<PlanOption> opts = w.availablePlans(head);
    // Every plan keeps its number from game to game; the ones out of reach say what they need.
    const auto& all = w.defs().plans;
    bool any = false;
    // Plans that move this generation's ambition forward come first, marked ★.
    const AmbitionDef* amb = w.defs().ambition(w.people()[w.playerHead()].ambition);
    auto forAmbition = [&](const std::string& id) {
        return amb && std::find(amb->plans.begin(), amb->plans.end(), id) != amb->plans.end();
    };
    if (amb) std::cout << "這一代的志向：「" << amb->name << "」\n";
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < all.size(); ++i) {
            if (forAmbition(all[i].id) != (pass == 0)) continue;
            bool open = std::any_of(opts.begin(), opts.end(), [&](const PlanOption& o) { return o.plan == all[i].id; });
            any = any || open;
            std::string mark = pass == 0 ? "★" : "";
            if (open) std::cout << "  " << i + 1 << ". " << mark << all[i].name << "：" << all[i].desc << "\n";
            else if (!all[i].needs.empty()) std::cout << "  　 " << mark << all[i].name << "（現在不行。要：" << all[i].needs << "，而且有合適的對象）\n";
        }
    if (!any) {
        std::cout << "現在沒有什麼能做的計畫。\n";
        return;
    }
    int p = number(ask("選哪個計畫（直接 Enter 取消）："));
    if (p < 1 || p > static_cast<int>(all.size())) return;
    std::string chosen = all[p - 1].id;
    if (std::none_of(opts.begin(), opts.end(), [&](const PlanOption& o) { return o.plan == chosen; })) {
        std::cout << "這個計畫現在做不到。\n";
        return;
    }
    std::vector<PlanOption> picks;
    for (const auto& o : opts)
        if (o.plan == chosen) picks.push_back(o);
    std::sort(picks.begin(), picks.end(), [](auto& a, auto& b) { return a.targetWeight > b.targetWeight; });
    if (picks.size() > 12) picks.resize(12);
    int t = 1;
    if (picks.size() > 1 || picks[0].target != kNone) {
        for (size_t i = 0; i < picks.size(); ++i) {
            const auto& o = picks[i];
            std::cout << "  " << i + 1 << ". ";
            if (o.subject != kNone) std::cout << "替 " << w.fullName(o.subject) << " ";
            if (o.target != kNone) {
                const Person& tp = w.people()[o.target];
                std::cout << w.fullName(o.target) << "（" << int(w.ageYears(tp)) << " 歲；你對他們 " << int(w.opinion(head, o.target))
                          << "，他們對你 " << int(w.opinion(o.target, head));
                if (tp.household != kNone) {
                    const Household& th = w.households()[tp.household];
                    std::cout << "；" << int(th.acres) << " 畝" << (th.lean ? "，今年缺糧" : "") << (th.debt > 0.5 ? "，欠著債" : "");
                }
                int st = w.feudStage(head, o.target);
                if (st > 0) std::cout << "；" << (st == 3 ? "世仇" : st == 2 ? "結怨" : "嫌隙");
                std::cout << "）";
            }
            std::cout << "\n";
        }
        t = number(ask("對誰（直接 Enter 取消）："));
        if (t < 1 || t > static_cast<int>(picks.size())) return;
    }
    const auto& o = picks[t - 1];
    w.issue({w.today(), "start_plan", head, o.plan, o.target, o.subject});
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataDir = "data", out, npcModel, npcLogPath, npcScope = "all";
    Config cfg;
    cfg.prerun = 25;
    int years = 100;
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
        else if (a == "--out") out = next();
        else if (a == "--stop") stopAtEof = true;
        else if (a == "--npc-model") npcModel = next();
        else if (a == "--npc-scope") npcScope = next();
        else if (a == "--npc-log") npcLogPath = next();
        else {
            std::cerr << "unknown argument " << a << "\n";
            return 2;
        }
    }
    TidyBuf tidy(std::cout.rdbuf());
    std::streambuf* plain = std::cout.rdbuf(&tidy);
    struct Restore {
        std::streambuf* b;
        ~Restore() { std::cout.rdbuf(b); }
    } restore{plain};
    Defs defs;
    try {
        defs = Defs::loadFromDir(dataDir);
    } catch (const std::exception& e) {
        std::cerr << "data error: " << e.what() << "\n";
        return 1;
    }
    World w(defs, cfg);
    w.setChooser([&](const World::ChoicePrompt& c) {
        if (eof) return -1;
        std::cout << "\n〔" << c.title << "〕" << c.situation << "\n";
        for (size_t i = 0; i < c.options.size(); ++i) std::cout << "  " << i + 1 << ". " << c.options[i] << "\n";
        for (const auto& u : c.unavailable) std::cout << "  　 " << u << "\n";
        while (true) {
            std::string a = ask("你怎麼做（數字；a = 讓 AI 決定；k = 先看家裡知道什麼；i = 看一個人）：");
            if (eof || a == "a") return -1;
            if (a == "k") {
                std::cout << w.familyReport();
                continue;
            }
            if (a == "i") {
                inspectMenu(w);
                continue;
            }
            int n = number(a);
            if (n >= 1 && n <= static_cast<int>(c.options.size())) return n - 1;
            std::cout << "請輸入 1 到 " << c.options.size() << " 的數字。\n";
        }
    });
    // Villagers' choices: replayed from the log first, then asked of the model, else the rules.
    NpcBridge npc;
    npc.attach(w, npcModel, npcLogPath, npcScope);
    AutoPlayer ai(cfg.seed);

    std::cout << "== " << w.dynasties()[w.playerDynasty()].surname << " 家，" << cfg.startYear << " 年 ==\n";
    std::cout << w.familyReport();
    // The village's years before the hand-over (Config::prerun) are not the family's story: start at the hand-over.
    size_t shown = 0;
    while (shown < w.chronicle().size() && w.chronicle()[shown].day < w.handedOver()) ++shown;
    auto flush = [&]() {
        const auto& log = w.chronicle();
        for (; shown < log.size(); ++shown)
            if (log[shown].importance != 2) {
                std::string t = log[shown].text;
                t.erase(0, t.find_first_not_of(' '));
                std::cout << (log[shown].importance == 3 ? "【史事】" : "- ") << t << "\n";
            }
    };
    int skipUntil = 0;
    World::FamilySnapshot before = w.snapshot();
    bool stopped = false;
    try {
    for (int d = 0; d < years * kDaysPerYear && !w.gameOver(); ++d) {
        if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0 && w.today() >= skipUntil) {
            flush();
            // A season is a turn: what changed, who is at home and how they are, then what to spend the free days on.
            w.startSeason();
            std::string changes = w.seasonChanges(before);
            if (!changes.empty() && !eof) std::cout << "\n" << changes;
            if (!eof) std::cout << "\n—— " << w.year() << " 年 " << seasonName(w.month()) << " ——\n" << w.familyGlance();
            // From here: what the player does this season counts toward the season's changes.
            before = w.snapshot();
            while (!eof) {
                std::string a = ask("Enter 過這一季　t 找人　p 定計畫　k 家裡知道什麼　i 看一個人　c 放棄計畫　y 過一年　q 結束：");
                if (a == "t") {
                    if (seeMenu(w, flush)) {
                        flush();
                        std::cout << "（這一季還有 " << w.timeLeft() << " 段空閒）\n";
                    }
                } else if (a == "k") {
                    std::cout << w.familyReport();
                } else if (a == "i") {
                    inspectMenu(w);
                } else if (a == "p") {
                    planMenu(w);
                } else if (a == "c") {
                    w.issue({w.today(), "cancel_plan", w.actingHead(), "", kNone, kNone});
                } else if (a == "y") {
                    skipUntil = w.today() + kDaysPerYear;
                    break;
                } else if (a == "q") {
                    d = years * kDaysPerYear;
                    break;
                } else {
                    break;
                }
            }
        }
        if (d >= years * kDaysPerYear) break;
        if (eof) ai.act(w);
        w.stepDay();
        flush();
    }
    } catch (const StopPlay&) {
        stopped = true;
    }
    if (!stopped) {
        flush();
        std::cout << "\n== 結束：" << w.year() << " 年" << (w.gameOver() ? "，家族斷絕了" : "") << " ==\n";
    }
    if (!out.empty()) {
        std::ofstream f(out);
        for (const auto& e : w.chronicle())
            if (e.day >= w.handedOver() && e.importance != 2) f << "- " << w.config().startYear + e.day / kDaysPerYear << " 年　" << tidyText(e.text) << "\n";
    }
    return 0;
}
