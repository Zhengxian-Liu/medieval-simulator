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
//       {"do":"plan","plan":"arrange_match","target":3,"subject":5} {"do":"cancel"}
//       {"do":"choose","index":0} {"do":"ai"} {"do":"inspect","who":12} {"do":"report"}
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

const char* familiarity(double c) {
    return c >= 60 ? "很熟" : c >= 30 ? "熟識" : c >= 10 ? "認識" : "點頭之交";
}

class Web {
public:
    Web(World& w) : w_(w) {}

    size_t shown = 0;           // chronicle lines already sent
    std::string summary;        // what the last season came to, shown through this season
    bool quit = false;

    void send(const std::string& line) { std::cout << line << "\n" << std::flush; }

    // What the family can do with someone this season (empty while a choice is being asked).
    std::string actionsFor(Id who, const Person& head) {
        std::vector<std::string> out;
        std::vector<World::Interaction> ins = w_.interactionsWith(who);
        EventRunner er(w_);
        const Person& p = w_.people()[who];
        const WantDef* want = w_.defs().want(p.want);
        for (const auto& in : ins) {
            bool wanted = want && std::find(want->answeredBy.begin(), want->answeredBy.end(), in.ev->id) != want->answeredBy.end();
            out.push_back(Obj()
                              .text("event", in.ev->id)
                              .text("title", in.ev->title)
                              .text("desc", er.format(in.ev->interaction.stringOr("desc", ""), p, head.id))
                              .b("open", in.open)
                              .text("why", in.why)
                              .b("wanted", wanted)
                              .n("time", in.ev->interaction.numberOr("time", 1))
                              .n("money", in.ev->interaction.numberOr("money", 0))
                              .done());
        }
        return arr(out);
    }

    std::string reasons(Id from, Id to) {
        std::vector<std::string> out;
        auto br = w_.opinionBreakdown(from, to);
        for (size_t i = 0; i < br.size() && i < 6; ++i)
            out.push_back("[" + str(opinionLabel(br[i].first)) + "," + num(std::lround(br[i].second)) + "]");
        return arr(out);
    }

    std::string state(bool withActions) {
        const World& w = w_;
        Id head = w.playerHead();
        Id hh = w.playerHousehold();
        Obj o;
        o.text("type", "state");
        o.raw("date", Obj().n("year", w.year()).n("month", w.month() + 1).text("season", seasonName(w.month())).done());
        if (head == kNone || hh == kNone) return o.done();
        const Household& h = w.households()[hh];
        const Person& hp = w.people()[head];
        double perDay = 0;  // as the economy counts it: a child under ten eats a little over half
        for (Id m : h.members) perDay += w.ageYears(w.people()[m]) < 10 ? 0.6 : 1.0;
        int rung = w.standing(hh);
        o.raw("house", Obj()
                           .text("surname", w.dynasties()[w.playerDynasty()].surname)
                           .n("money", std::floor(h.money))
                           .n("food_days", std::floor(h.food / std::max(1.0, perDay)))
                           .n("acres", h.acres)
                           .n("freehold", h.freehold)
                           .n("debt", std::floor(h.debt))
                           .text("home", h.house)
                           .text("standing", rung >= 0 ? World::standingName(rung) : "領主家")
                           .b("lean", h.lean)
                           .done());
        o.n("time_left", w.timeLeft());
        o.n("head", head);
        if (const AmbitionDef* a = w.defs().ambition(hp.ambition)) {
            std::vector<std::string> steps;
            for (size_t i = 0; i < a->milestones.size(); ++i)
                steps.push_back(Obj().text("name", a->milestones[i].name).b("done", static_cast<int>(i) < hp.ambitionStep).done());
            o.raw("ambition", Obj().text("name", a->name).text("desc", a->desc).raw("steps", arr(steps)).done());
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
            f.n("id", m).text("name", w.shortName(m)).text("full", w.fullName(m)).text("kin", m == head ? "你" : w.kinWord(head, m))
                .n("age", std::floor(w.ageYears(p))).b("female", p.sex == Sex::Female).text("mood", mv.word).text("mood_cause", mv.cause)
                .n("mood_value", std::lround(mv.value)).n("health", std::lround(p.health)).raw("needs", arr(needs)).raw("traits", arr(traits));
            if (m != head) {
                double op = w.opinion(m, head);
                f.n("opinion", std::lround(op)).text("opinion_word", opinionWord(op)).raw("reasons", reasons(m, head)).text("want", w.wantText(p));
                if (withActions) f.raw("actions", actionsFor(m, hp));
            }
            fam.push_back(f.done());
        }
        o.raw("family", arr(fam));

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
            std::string kin = w.kinWord(head, id);
            Obj q;
            q.n("id", id).text("name", w.fullName(id)).text("kin", kin).n("age", std::floor(w.ageYears(p)))
                .text("house", p.household != kNone && w.households()[p.household].dynasty != kNone
                                   ? w.dynasties()[w.households()[p.household].dynasty].surname : "")
                .n("household", p.household).n("opinion", std::lround(w.opinion(id, head))).text("opinion_word", opinionWord(w.opinion(id, head)))
                .n("yours", std::lround(w.opinion(head, id))).n("closeness", std::lround(close))
                .text("familiarity", kin.empty() ? familiarity(close) : kin).n("feud", w.feudStage(head, id))
                .b("busy", w.seemsBusy(id)).text("line", w.personLine(id)).raw("reasons", reasons(id, head));
            if (withActions && canSee.count(id)) q.raw("actions", actionsFor(id, hp));
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
                                      .b("lord", pc.holder != kNone && pc.holder == lordHouse).done());
            }
            std::string cs = "[";
            for (size_t i = 0; i < cells.size(); ++i) cs += (i ? "," : "") + std::to_string(cells[i]);
            cs += "]";
            o.raw("map", Obj().n("w", vm.width).n("h", vm.height).raw("areas", arr(areas)).raw("pois", arr(pois))
                             .raw("homes", arr(homes)).raw("parcels", arr(parcels)).raw("cells", cs).done());
        }

        // What the family has heard, newest first.
        std::vector<std::string> heard;
        const auto& hd = w.heard();
        for (size_t i = hd.size(); i-- > 0 && heard.size() < 30;) {
            const Heard& x = hd[i];
            heard.push_back(Obj()
                                .n("year", w.config().startYear + x.day / kDaysPerYear)
                                .text("teller", x.teller != kNone ? w.tellerName(x.teller) : "")
                                .text("text", x.text)
                                .b("whisper", x.news == kNone)
                                .b("recent", w.today() - x.day <= 3 * kDaysPerMonth)
                                .done());
        }
        o.raw("heard", arr(heard));

        // Plans: the one under way, or those that can be started and with whom.
        Id acting = w.actingHead();
        if (const ActivePlan* ap = acting != kNone ? w.activePlan(acting) : nullptr) {
            const PlanDef* pd = w.defs().plan(ap->plan);
            o.raw("plan", Obj().text("name", pd ? pd->name : ap->plan).text("target", ap->target != kNone ? w.fullName(ap->target) : "")
                              .n("progress", std::floor(ap->progress)).done());
        }
        if (withActions && acting != kNone && !w.activePlan(acting)) {
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
                        if (tp.household != kNone) {
                            const Household& th = w.households()[tp.household];
                            label += "，" + std::to_string(int(th.acres)) + " 畝" + (th.lean ? "，今年缺糧" : "") + (th.debt > 0.5 ? "，欠著債" : "");
                        }
                        int st = w.feudStage(head, op.target);
                        if (st > 0) label += st == 3 ? "，世仇" : st == 2 ? "，結怨" : "，嫌隙";
                        label += "）";
                    }
                    if (label.empty()) label = "就這麼辦";
                    targets.push_back(Obj().n("target", op.target).n("subject", op.subject).text("label", label).done());
                }
                plans.push_back(Obj().text("id", pd.id).text("name", pd.name).text("desc", pd.desc).text("needs", pd.needs)
                                    .b("open", !picks.empty()).b("ambition", forAmb).raw("targets", arr(targets)).done());
            }
            o.raw("plans", arr(plans));
        }

        // What happened since the last state, as the chronicle tells it.
        std::vector<std::string> lines;
        const auto& log = w.chronicle();
        for (; shown < log.size(); ++shown) {
            if (log[shown].importance == 2) continue;
            std::string t = log[shown].text;
            t.erase(0, t.find_first_not_of(' '));
            lines.push_back(Obj().n("year", w.config().startYear + log[shown].day / kDaysPerYear).n("kind", log[shown].importance).text("text", t).done());
        }
        o.raw("chronicle", arr(lines));
        o.text("summary", summary);  // what the last season came to, until the next one
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
    int years = 100;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nextArg = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") dataDir = nextArg();
        else if (a == "--seed") cfg.seed = std::stoull(nextArg());
        else if (a == "--start") cfg.startYear = std::stoi(nextArg());
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
    auto saveLog = [&]() {
        if (out.empty()) return;
        std::ofstream f(out);
        for (const auto& e : w.chronicle())
            if (e.importance != 2) f << "- " << cfg.startYear + e.day / kDaysPerYear << " 年　" << tidyText(e.text) << "\n";
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
            if (what == "ai") return -1;
            int k = toInt(m, "index", -1);
            if (what == "choose" && k >= 0 && k < static_cast<int>(c.options.size())) return k;
        }
    });
    NpcBridge npc;
    npc.attach(w, npcModel, npcLogPath, npcScope);

    int skipUntil = 0;
    World::FamilySnapshot before = w.snapshot();
    for (int d = 0; d < years * kDaysPerYear && !w.gameOver() && !aiPlays; ++d) {
        if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0 && w.today() >= skipUntil) {
            w.startSeason();
            web.summary = w.seasonChanges(before);
            before = w.snapshot();
            saveLog();
            bool leave = false;
            while (!leave) {
                web.send(web.state(true));
                web.send(Obj().text("type", "ask").text("kind", "season").done());
                auto m = web.next();
                std::string what = m["do"];
                Id head = w.playerHead();
                if (what == "interact") {
                    w.issue({w.today(), "interact", head, m["event"], toInt(m, "who"), kNone});
                    w.runCommands();
                } else if (what == "plan") {
                    w.issue({w.today(), "start_plan", w.actingHead(), m["plan"], toInt(m, "target"), toInt(m, "subject")});
                } else if (what == "cancel") {
                    w.issue({w.today(), "cancel_plan", w.actingHead(), "", kNone, kNone});
                } else if (what == "year") {
                    skipUntil = w.today() + kDaysPerYear;
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
