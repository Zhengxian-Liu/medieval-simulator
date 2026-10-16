#include "simcore/defs.h"

#include <algorithm>
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
        d.occupations[od.id] = od;
    }

    std::vector<fs::path> eventFiles;
    for (const auto& entry : fs::directory_iterator(dir + "/events"))
        if (entry.path().extension() == ".json") eventFiles.push_back(entry.path());
    std::sort(eventFiles.begin(), eventFiles.end());
    for (const auto& p : eventFiles) loadEvents(Json::parseFile(p.string()), d.events);

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
            for (const auto& m : a["milestones"].asArray()) ad.milestones.push_back({m["name"].asString(), m["goal"]});
        if (a["plans"].isArray())
            for (const auto& pl : a["plans"].asArray()) ad.plans.push_back(pl.asString());
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
        pd.startLog = p.stringOr("start_log", "");
        pd.successLog = p.stringOr("success_log", "");
        pd.failLog = p.stringOr("fail_log", "");
        pd.exposedLog = p.stringOr("exposed_log", "");
        pd.victimLog = p.stringOr("victim_log", "");
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
        cd.wish = c.stringOr("wish", "");
        cd.mood = c.numberOr("mood", -30);
        cd.fade = c.numberOr("fade", 5);
        d.concerns[cd.id] = cd;
    }

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
const AmbitionDef* Defs::ambition(const std::string& id) const {
    for (const auto& a : ambitions)
        if (a.id == id) return &a;
    return nullptr;
}
const ConcernDef* Defs::concern(const std::string& id) const {
    auto it = concerns.find(id);
    return it == concerns.end() ? nullptr : &it->second;
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
