#include "simcore/defs.h"

#include <algorithm>
#include <functional>
#include <set>
#include <filesystem>

namespace sim {

namespace {

std::map<std::string, double> readMods(const Json& j) {
    std::map<std::string, double> out;
    if (j.isObject())
        for (const auto& [k, v] : j.asObject()) out[k] = v.asNumber();
    return out;
}

std::vector<std::string> readStrings(const Json& j) {
    std::vector<std::string> out;
    if (j.isArray())
        for (const auto& v : j.asArray()) out.push_back(v.asString());
    return out;
}

void loadEvents(const Json& root, std::vector<EventDef>& out) {
    for (const auto& e : root["events"].asArray()) out.push_back(parseEvent(e));
}

}  // namespace

EventDef parseEvent(const Json& e) {
    EventDef d;
    d.id = e["id"].asString();
    d.title = e.stringOr("title", d.id);
    d.prompt = e.stringOr("prompt", "");
    d.prompts = readStrings(e["prompts"]);
    d.promptIf = e["prompt_if"];
    d.quietLog = e.stringOr("quiet_log", "");
    d.ask = !e["ask"].isBool() || e["ask"].asBool();
    d.parentDecides = e["parent_decides"].isBool() && e["parent_decides"].asBool();
    d.pulse = e.stringOr("pulse", "monthly");
    d.chance = e.numberOr("chance", 0.01);
    d.scope = e.stringOr("scope", "anyone");
    d.cooldownYears = static_cast<int>(e.numberOr("cooldown_years", 0));
    d.householdCooldown = e.stringOr("cooldown_scope", "person") == "household";
    d.budget = e.stringOr("budget", "");
    d.once = e["once"].isBool() && e["once"].asBool();
    d.npcLog = e["npc_log"].isBool() && e["npc_log"].asBool();
    d.conditions = e["conditions"];
    d.target = e["target"];
    d.on = e["on"];
    if (!d.on.isNull()) d.pulse = "signal";
    d.interaction = e["interaction"];
    if (d.scope == "interaction") d.pulse = "interaction";
    d.cooldownMonths = static_cast<int>(e.numberOr("cooldown_months", 0));
    d.repeat = e.stringOr("repeat", "household");
    if (d.repeat != "household" && d.repeat != "person" && d.repeat != "free") throw JsonError("event " + d.id + ": unknown repeat " + d.repeat);
    d.institution = e["institution"].isBool() && e["institution"].asBool();
    if (d.institution) {
        std::set<std::string> told(d.prompts.begin(), d.prompts.end());
        if (!d.prompt.empty()) told.insert(d.prompt);
        if (told.size() < 3) throw JsonError("event " + d.id + ": an institution needs at least 3 prompt variants (has " + std::to_string(told.size()) + ")");
    }
    for (const auto& o : e["options"].asArray()) {
        OptionDef od;
        od.text = o.stringOr("text", "");
        if (o["log"].isArray()) {
            for (const auto& l : o["log"].asArray()) od.logs.push_back(l.asString());
            if (!od.logs.empty()) od.log = od.logs[0];
        } else {
            od.log = o.stringOr("log", "");
        }
        od.effects = o["effects"];
        od.aiWeight = o["ai"];
        od.conditions = o["conditions"];
        d.options.push_back(od);
    }
    if (d.options.empty()) throw JsonError("event " + d.id + " has no options");
    return d;
}

Defs Defs::loadFromDir(const std::string& dir) {
    namespace fs = std::filesystem;
    Defs d;

    Json traitsRoot = Json::parseFile(dir + "/traits.json");
    for (const auto& t : traitsRoot["traits"].asArray()) {
        TraitDef td;
        td.id = t["id"].asString();
        td.name = t.stringOr("name", td.id);
        td.category = t.stringOr("category", "personality");
        td.opposite = t.stringOr("opposite", "");
        td.inheritChance = t.numberOr("inherit", 0);
        td.weight = t.numberOr("weight", 1);
        td.mods = readMods(t["mods"]);
        td.against = t.stringOr("against", "");
        d.traitOrder.push_back(td.id);
        d.traits[td.id] = td;
    }

    Json occupationsRoot = Json::parseFile(dir + "/occupations.json");
    for (const auto& o : occupationsRoot["occupations"].asArray()) {
        OccupationDef od;
        od.id = o["id"].asString();
        od.name = o.stringOr("name", od.id);
        od.skill = o.stringOr("skill", "farming");
        od.sex = o.stringOr("sex", "any");
        od.monthlyIncome = o.numberOr("monthly_income", 0);
        od.farms = o["farms"].isBool() && o["farms"].asBool();
        od.unmarriedName = o.stringOr("unmarried_name", "");
        d.occupations[od.id] = od;
    }

    Json pathsRoot = Json::parseFile(dir + "/paths.json");
    for (const auto& o : pathsRoot["stations"].asArray()) {
        StationDef sd;
        sd.id = o["id"].asString();
        sd.name = o.stringOr("name", sd.id);
        sd.occupation = o.stringOr("occupation", "");
        sd.away = o["away"].isBool() && o["away"].asBool();
        sd.lasting = o["lasting"].isBool() && o["lasting"].asBool();
        if (!sd.occupation.empty() && !d.occupations.count(sd.occupation))
            throw JsonError("station " + sd.id + ": unknown occupation " + sd.occupation);
        d.stations[sd.id] = sd;
    }

    Json focusRoot = Json::parseFile(dir + "/focus.json");
    for (const auto& [k, v] : focusRoot["defaults"].asObject()) d.focusDefaults[k] = v.asString();
    for (const auto& o : focusRoot["foci"].asArray()) {
        FocusDef f;
        f.id = o["id"].asString();
        f.stage = o["stage"].asString();
        if (f.stage != "child" && f.stage != "adult") throw JsonError("focus " + f.id + ": stage must be child or adult");
        f.name = o["name"].asString();
        f.desc = o.stringOr("desc", "");
        f.conditions = o["conditions"];
        if (o["skills"].isObject())
            for (const auto& [k, v] : o["skills"].asObject()) f.skills[k] = v.asNumber();
        if (o["need"].isObject())
            for (const auto& [k, v] : o["need"].asObject()) f.needs[k] = v.asNumber();
        f.work = o.numberOr("work", 0);
        f.income = o.numberOr("income", 0);
        f.reputation = o.numberOr("reputation", 0);
        f.stress = o.numberOr("stress", 0);
        f.health = o.numberOr("health", 0);
        f.opinionParents = o.numberOr("opinion_parents", 0);
        f.knownPeers = static_cast<int>(o.numberOr("known_peers", 0));
        f.knownVillage = static_cast<int>(o.numberOr("known_village", 0));
        f.priestKnown = static_cast<int>(o.numberOr("priest_known", 0));
        d.foci.push_back(f);
    }
    for (const char* k : {"child_male", "child_female", "adult", "adult_craft"})
        if (!d.focusDefaults.count(k)) throw JsonError(std::string("focus.json: defaults has no ") + k);
    for (const char* id : {"errand", "letters"})  // named in focus.cpp (apprentices, schooled children)
        if (!d.focus(id, "child")) throw JsonError(std::string("focus.json: no child focus ") + id);
    for (const auto& [k, id] : d.focusDefaults)
        if (!d.focus(id, k.rfind("child", 0) == 0 ? "child" : "adult")) throw JsonError("focus default " + k + ": unknown focus " + id);

    std::vector<fs::path> eventFiles;
    for (const auto& entry : fs::directory_iterator(dir + "/events"))
        if (entry.path().extension() == ".json") eventFiles.push_back(entry.path());
    std::sort(eventFiles.begin(), eventFiles.end());
    // Every station an event names must exist; a typo would otherwise surface only when the event fires.
    std::function<void(const Json&, const std::string&)> checkStations = [&](const Json& j, const std::string& file) {
        auto known = [&](const std::string& id) {
            if (!d.stations.count(id)) throw JsonError(file + ": unknown station " + id);
        };
        if (j.isArray()) {
            for (const auto& x : j.asArray()) checkStations(x, file);
        } else if (j.isObject()) {
            for (const auto& [k, v] : j.asObject()) {
                if (k == "station") {
                    if (v.isString()) known(v.asString());
                    else if (v.isArray()) for (const auto& x : v.asArray()) known(x.asString());
                    else if (v.isObject()) known(v["id"].asString());
                } else {
                    checkStations(v, file);
                }
            }
        }
    };
    for (const auto& p : eventFiles) {
        Json root = Json::parseFile(p.string());
        checkStations(root, p.filename().string());
        loadEvents(root, d.events);
    }

    Json historyRoot = Json::parseFile(dir + "/history.json");
    for (const auto& h : historyRoot["history"].asArray()) {
        HistoryDef hd;
        hd.year = h["year"].asInt();
        hd.month = static_cast<int>(h.numberOr("month", 0));
        hd.id = h["id"].asString();
        hd.text = h.stringOr("text", "");
        hd.effects = h["effects"];
        d.history.push_back(hd);
    }
    std::stable_sort(d.history.begin(), d.history.end(), [](const HistoryDef& a, const HistoryDef& b) {
        return a.year != b.year ? a.year < b.year : a.month < b.month;
    });

    Json ambitionsRoot = Json::parseFile(dir + "/ambitions.json");
    for (const auto& a : ambitionsRoot["ambitions"].asArray()) {
        AmbitionDef ad;
        ad.id = a["id"].asString();
        ad.name = a.stringOr("name", ad.id);
        ad.desc = a.stringOr("desc", "");
        ad.offer = a["offer"];
        ad.goal = a["goal"];
        ad.aiWeight = a["ai"];
        ad.tradition = a.stringOr("tradition", "");
        ad.maxYears = static_cast<int>(a.numberOr("max_years", 25));
        if (a["milestones"].isArray())
            for (const auto& m : a["milestones"].asArray())
                ad.milestones.push_back({m["name"].asString(), m["goal"], m["gate"].isBool() && m["gate"].asBool()});
        if (a["plans"].isArray())
            for (const auto& pl : a["plans"].asArray()) ad.plans.push_back(pl.asString());
        ad.next = readStrings(a["next"]);
        ad.final = a.stringOr("final", ad.name);
        ad.hint = a.stringOr("hint", "");
        ad.keep = a["keep"];
        ad.lost = a.stringOr("lost", "");
        ad.office = a.stringOr("office", "");
        d.ambitions.push_back(ad);
    }

    Json traditionsRoot = Json::parseFile(dir + "/traditions.json");
    for (const auto& t : traditionsRoot["traditions"].asArray()) {
        TraditionDef td;
        td.id = t["id"].asString();
        td.name = t.stringOr("name", td.id);
        td.desc = t.stringOr("desc", "");
        td.mods = readMods(t["mods"]);
        d.traditions[td.id] = td;
    }

    Json itemsRoot = Json::parseFile(dir + "/items.json");
    for (const auto& i : itemsRoot["items"].asArray()) {
        ItemDef id;
        id.id = i["id"].asString();
        id.name = i.stringOr("name", id.id);
        id.value = i.numberOr("value", 0);
        id.dowry = i["dowry"].isBool() && i["dowry"].asBool();
        d.items[id.id] = id;
    }

    Json deedsRoot = Json::parseFile(dir + "/deeds.json");
    for (const auto& j : deedsRoot["deeds"].asArray()) {
        DeedDef dd;
        dd.id = j["id"].asString();
        dd.name = j.stringOr("name", dd.id);
        dd.tags = readStrings(j["tags"]);
        dd.visibility = j.stringOr("visibility", "private");
        dd.roles = readMods(j["roles"]);
        dd.stress = readMods(j["stress"]);
        dd.halfLife = j.numberOr("half_life", 10);
        dd.inheritable = j["inheritable"].isBool() && j["inheritable"].asBool();
        dd.family = j["family"].isBool() && j["family"].asBool();
        d.deeds[dd.id] = dd;
    }

    Json plansRoot = Json::parseFile(dir + "/plans.json");
    for (const auto& p : plansRoot["plans"].asArray()) {
        PlanDef pd;
        pd.id = p["id"].asString();
        pd.name = p.stringOr("name", pd.id);
        pd.desc = p.stringOr("desc", "");
        pd.needs = p.stringOr("requires", "");
        pd.actor = p["actor"];
        pd.subject = p["subject"];
        pd.target = p["target"];
        pd.maxMonths = static_cast<int>(p.numberOr("max_months", 12));
        pd.monthlyCost = p.numberOr("monthly_cost", 0);
        pd.progress = p["progress"];
        pd.secret = p["secret"].isBool() && p["secret"].asBool();
        pd.discovery = p.numberOr("discovery", 0);
        pd.npc = !p["npc"].isBool() || p["npc"].asBool();
        pd.notifyTarget = p["notify_target"].isBool() && p["notify_target"].asBool();
        pd.consent = p["consent"].isBool() && p["consent"].asBool();
        pd.personal = p["personal"].isBool() && p["personal"].asBool();
        pd.startLog = p.stringOr("start_log", "");
        pd.successLog = p.stringOr("success_log", "");
        pd.failLog = p.stringOr("fail_log", "");
        pd.exposedLog = p.stringOr("exposed_log", "");
        pd.victimLog = p.stringOr("victim_log", "");
        pd.targetAsk = p.stringOr("target_ask", "");
        if (p["target_answers"].isArray())
            for (const auto& a : p["target_answers"].asArray()) pd.targetAnswers.push_back(a.asString());
        pd.foreshadow = p.stringOr("foreshadow", "");
        pd.retryYears = static_cast<int>(p.numberOr("retry_years", 0));
        pd.success = p["success"];
        pd.fail = p["fail"];
        pd.exposed = p["exposed"];
        pd.aiWeight = p["ai"];
        for (const auto& b : p["beats"].asArray()) {
            pd.beats.push_back(parseEvent(b));
            if (!b.has("title")) pd.beats.back().title = pd.name;
        }
        d.plans.push_back(std::move(pd));
    }

    Json names = Json::parseFile(dir + "/names.json");
    d.maleNames = readStrings(names["male"]);
    d.femaleNames = readStrings(names["female"]);
    d.surnames = readStrings(names["surnames"]);

    Json concernsRoot = Json::parseFile(dir + "/concerns.json");
    for (const auto& c : concernsRoot["concerns"].asArray()) {
        ConcernDef cd;
        cd.id = c["id"].asString();
        cd.name = c.stringOr("name", cd.id);
        cd.text = c.stringOr("text", cd.name);
        cd.word = c.stringOr("word", "");
        cd.mood = c.numberOr("mood", -30);
        cd.fade = c.numberOr("fade", 5);
        d.concerns[cd.id] = cd;
    }

    Json wantsRoot = Json::parseFile(dir + "/wants.json");
    for (const auto& w : wantsRoot["wants"].asArray()) {
        WantDef wd;
        wd.id = w["id"].asString();
        wd.text = w["text"].asString();
        wd.when = w["when"];
        wd.answeredBy = readStrings(w["answered_by"]);
        wd.fulfil = w["fulfil"];
        wd.neglect = w["neglect"];
        wd.fulfilNote = w.stringOr("fulfil_note", "");
        wd.neglectNote = w.stringOr("neglect_note", "");
        wd.patience = static_cast<int>(w.numberOr("patience", 2));
        wd.chance = w.numberOr("chance", 1);
        wd.oneAtATime = w["one_at_a_time"].isBool() && w["one_at_a_time"].asBool();
        d.wants.push_back(std::move(wd));
    }

    Json chatterRoot = Json::parseFile(dir + "/chatter.json");
    for (const auto& c : chatterRoot["chatter"].asArray()) {
        ChatterDef cd;
        cd.id = c["id"].asString();
        cd.when = c["when"];
        cd.lines = readStrings(c["lines"]);
        cd.weight = c.numberOr("weight", 1);
        cd.notIn = readStrings(c["not_in"]);
        d.chatter.push_back(std::move(cd));
    }

    Json newsRoot = Json::parseFile(dir + "/news.json");
    for (const auto& n : newsRoot["kinds"].asArray()) {
        NewsKindDef nd;
        nd.id = n["id"].asString();
        nd.text = n["text"].asString();
        nd.heat = n.numberOr("heat", 0.5);
        nd.fadeMonths = static_cast<int>(n.numberOr("fade_months", 3));
        nd.instant = n.numberOr("instant", 2);
        nd.invite = n.stringOr("invite", "");
        nd.inviteLink = n.numberOr("invite_link", 0.35);
        nd.inviteMinAge = static_cast<int>(n.numberOr("invite_min_age", 0));
        nd.logLink = n.numberOr("log_link", 0.75);
        nd.gathering = n["gathering"].isBool() && n["gathering"].asBool();
        nd.attendMod = n.stringOr("attend_mod", "");
        nd.hasWitness = n.has("witness");
        nd.witness = n.numberOr("witness", 0);
        nd.wide = n["wide"].isBool() && n["wide"].asBool();
        d.news[nd.id] = nd;
    }
    for (const auto& [k, v] : newsRoot["whispers"].asObject()) d.whispers[k] = v.asString();

    d.village = Json::parseFile(dir + "/village.json");
    return d;
}

const TraitDef* Defs::trait(const std::string& id) const {
    auto it = traits.find(id);
    return it == traits.end() ? nullptr : &it->second;
}
const OccupationDef* Defs::occupation(const std::string& id) const {
    auto it = occupations.find(id);
    return it == occupations.end() ? nullptr : &it->second;
}
const FocusDef* Defs::focus(const std::string& id, const std::string& stage) const {
    for (const auto& f : foci)
        if (f.id == id && f.stage == stage) return &f;
    return nullptr;
}

const StationDef* Defs::station(const std::string& id) const {
    auto it = stations.find(id);
    return it == stations.end() ? nullptr : &it->second;
}
const AmbitionDef* Defs::ambition(const std::string& id) const {
    for (const auto& a : ambitions)
        if (a.id == id) return &a;
    return nullptr;
}
const ConcernDef* Defs::concern(const std::string& id) const {
    auto it = concerns.find(id);
    return it == concerns.end() ? nullptr : &it->second;
}
const NewsKindDef* Defs::newsKind(const std::string& id) const {
    auto it = news.find(id);
    return it == news.end() ? nullptr : &it->second;
}

const WantDef* Defs::want(const std::string& id) const {
    for (const auto& w : wants)
        if (w.id == id) return &w;
    return nullptr;
}
const DeedDef* Defs::deed(const std::string& id) const {
    auto it = deeds.find(id);
    return it == deeds.end() ? nullptr : &it->second;
}
const PlanDef* Defs::plan(const std::string& id) const {
    for (const auto& p : plans)
        if (p.id == id) return &p;
    return nullptr;
}
const ItemDef* Defs::item(const std::string& id) const {
    auto it = items.find(id);
    return it == items.end() ? nullptr : &it->second;
}

}  // namespace sim
