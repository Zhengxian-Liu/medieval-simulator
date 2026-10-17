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
    World::FamilySnapshot seasonStart;  // the family at the start of this season, for "這一季到現在"
    bool quit = false;

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
        for (const auto& in : ins) {
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
                steps.push_back(Obj().text("name", a->milestones[i].name).b("done", st[i] == 1).b("slipped", st[i] == 2).done());
            steps.push_back(Obj().text("name", a->final).b("done", false).b("final", true).done());
            int left = a->maxYears - (w.today() - hp.ambitionSince) / kDaysPerYear;
            std::vector<std::string> helps;
            for (const auto& pid : a->plans)
                if (const PlanDef* pd = w.defs().plan(pid)) helps.push_back(str(pd->name));
            o.raw("ambition", Obj().text("name", a->name).text("desc", a->desc).raw("steps", arr(steps)).text("hint", a->hint)
                                   .raw("plans", arr(helps)).n("years_left", std::max(0, left)).done());
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
            if (m != head) {
                double op = w.opinion(m, head);
                f.n("opinion", std::lround(op)).text("opinion_word", opinionWord(op)).raw("reasons", reasons(m, head)).text("want", w.wantText(p));
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
                                       return x.open && x.house == hh && x.kind == "office" && x.office == of.id; })).done());
            Obj c;
            c.raw("items", arr(items)).raw("past", arr(past)).raw("offices", arr(offs));
            for (const auto& f : w.entryFines())
                if (f.house == hh)
                    c.raw("fine", Obj().n("amount", f.amount).n("year", w.config().startYear + f.due / kDaysPerYear)
                                     .n("month", (f.due % kDaysPerYear) / kDaysPerMonth + 1).b("extended", f.extended).done());
            o.raw("contest", c.done());
            const AskResult& la = w.lastAsk();
            // Only the levers that can work now: the season it was given and the money it takes (a press is part of the same talk).
            if (la.who != kNone && la.day >= w.today() - 2)
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
        o.raw("heard", arr(heard));

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
        if (withActions && acting != kNone && w.planRoom(acting)) {
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
                std::string why = picks.empty() ? w_.planBlock(acting, pd) : w.timeLeft() > 0 ? "" : "這一季沒有空閒了";
                plans.push_back(Obj().text("id", pd.id).text("name", pd.name).text("desc", pd.desc).text("needs", pd.needs).text("why", why)
                                    .b("open", !picks.empty() && w.timeLeft() > 0).b("ambition", forAmb).raw("targets", arr(targets)).done());
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
        // What the last season came to, until the next one; and what has moved since, so the header's money adds up.
        std::string sum = summary;
        // seasonChanges says 這一季 / 這 N 季 while the season is running; shown afterwards it is the one just past.
        if (sum.rfind("這一季", 0) == 0) sum = "上一季" + sum.substr(std::string("這一季").size());
        else if (sum.rfind("這 ", 0) == 0) sum = "過去 " + sum.substr(std::string("這 ").size());
        else if (!sum.empty()) sum = "【上一季】\n" + sum;
        if (withActions && w.playerHead() != kNone && seasonStart.head != kNone &&
            std::abs(w.households()[w.playerHousehold()].money - seasonStart.money) >= 1)
            sum += "【這一季到現在】" + w.moneySince(seasonStart) + "\n";
        o.text("summary", sum);
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
            if (what == "ai") return -1;
            int k = toInt(m, "index", -1);
            if (what == "choose" && k >= 0 && k < static_cast<int>(c.options.size())) return k;
        }
    });
    NpcBridge npc;
    npc.attach(w, npcModel, npcLogPath, npcScope);

    int skipUntil = 0;
    Id regencyFor = kNone;  // the child a regency skip is for: if they die or the house passes, the skip ends
    World::FamilySnapshot before = w.snapshot();
    for (int d = 0; d < years * kDaysPerYear && !w.gameOver() && !aiPlays; ++d) {
        if (regencyFor != kNone && w.households()[w.playerHousehold()].ward != regencyFor) {
            regencyFor = kNone;
            skipUntil = 0;
        }
        if (d > 0 && w.dayOfMonth() == 0 && w.month() % 3 == 0 && w.today() >= skipUntil) {
            w.startSeason();
            web.summary = w.seasonChanges(before);
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
                } else if (what == "plan") {
                    w.issue({w.today(), "start_plan", w.actingHead(), m["plan"], toInt(m, "target"), toInt(m, "subject")});
                    w.runCommands();
                } else if (what == "cancel") {
                    w.issue({w.today(), "cancel_plan", w.actingHead(), m["plan"], kNone, kNone});
                    w.runCommands();
                } else if (what == "year") {
                    skipUntil = w.today() + kDaysPerYear;
                    regencyFor = kNone;
                    leave = true;
                } else if (what == "regency") {
                    // Until the child the house is held for comes of age; questions are still asked on the way.
                    const Household& ph = w.households()[w.playerHousehold()];
                    if (ph.ward != kNone && w.people()[ph.ward].alive()) {
                        skipUntil = std::max(w.today() + 1, w.people()[ph.ward].birthDay + 16 * kDaysPerYear);
                        regencyFor = ph.ward;
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
