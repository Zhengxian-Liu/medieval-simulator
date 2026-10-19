// 處境 and 人生路 (design/life-paths.md): where each person is in life, and the road they took to get there.
// The forks themselves are ordinary events (data/events/paths.json); this file moves people between stations,
// sends them out of the village and brings them back, and follows the ones away with a yearly reckoning.
#include <algorithm>
#include <set>
#include <stdexcept>

#include "simcore/world.h"

namespace sim {

std::string World::stationName(const Person& p) const {
    // A servant is in someone else's house, seen from her own; under the player's roof she is serving the family.
    if (p.station == "servant" && p.home != kNone && p.household != kNone && households_[p.household].dynasty != kNone)
        return inPlayerHouse(p.household) ? "在你家幫傭" : "在" + dynasties_[households_[p.household].dynasty].surname + "家幫傭";
    const StationDef* sd = defs_.station(p.station.empty() ? "home" : p.station);
    return sd ? sd->name : p.station;
}

std::string World::occupationName(const Person& p) const {
    const OccupationDef* od = defs_.occupation(p.occupation);
    if (!od) return "";
    return p.spouse == kNone && !od->unmarriedName.empty() ? od->unmarriedName : od->name;
}

std::vector<Id> World::livingAway() const {
    std::vector<Id> v;
    for (const auto& p : people_)
        if (isAway(p)) v.push_back(p.id);
    return v;
}

bool World::onRoad(const std::string& station) {
    static const std::set<std::string> roads = {"scholar", "town_apprentice", "town_journeyman", "apprentice", "servant", "town_servant",
                                                "archer", "labourer", "town_labourer", "clerk", "chapman", "retainer", "fled"};
    return roads.count(station) > 0;
}

void World::setStation(Id who, const std::string& station, int years, const std::string& note) {
    Person& p = people_[who];
    if (!p.alive()) return;
    // The family's own leaving a road: how it ended is kept for the generation's account (design/fun-r6.md D).
    if (onRoad(p.station) && p.station != station && !note.empty() && isPlayerFamily(p) && note.rfind("接著", 0) != 0)
        noteThreadEnd("path", p.firstName + "：" + note);
    const StationDef* sd = defs_.station(station);
    if (!sd) throw std::runtime_error("unknown station " + station);
    bool wasAway = isAway(p);
    if (sd->away && !wasAway && p.household == kNone) return;  // nowhere to leave from
    // The manor's lord and his lady do not walk off to a master in town or to the wars: the manor would be left
    // with no one in it. Their children may go.
    if (sd->away && !wasAway && lord_ != kNone && (p.id == lord_ || people_[lord_].spouse == p.id)) return;
    std::string id = station;
    std::string why = note;
    // Coming back to a village with no house of theirs left in it: they stay where they are.
    if (!sd->away && wasAway) {
        Id dest = p.awayFrom;
        if (dest == kNone || households_[dest].extinct || households_[dest].head == kNone) {
            dest = kNone;
            for (const auto& h : households_)
                if (dest == kNone && !h.extinct && h.head != kNone && p.dynasty != kNone && h.dynasty == p.dynasty) dest = h.id;
        }
        if (dest == kNone) {
            id = "elsewhere";
            why = "家裡已經沒人了，留在外地";
            sd = defs_.station(id);
            if (!sd) throw std::runtime_error("data/paths.json needs the station elsewhere");
        } else {
            p.awayFrom = dest;
        }
    }
    p.station = id == "home" ? "" : id;
    p.stationSince = day_;
    p.stationUntil = years > 0 ? day_ + years * kDaysPerYear : -1;
    p.road.push_back({day_, id, why});
    if (!sd->occupation.empty() && defs_.occupation(sd->occupation)) p.occupation = sd->occupation;

    if (sd->away && !wasAway) {
        // Off the map: out of the household (and out of service), but still the family's.
        Id from = p.home != kNone ? p.home : p.household;
        p.home = kNone;
        p.serviceEnd = -1;
        p.flags.erase("in_service");
        Id hid = p.household;
        bool head = households_[hid].head == who;
        moveTo(who, kNone);
        p.awayFrom = from;
        if (head) {
            leaving_ = who;
            onHeadDied(households_[hid], who);
            leaving_ = kNone;
            // Walked out of a house that ended with him: he belongs to another house of his blood, if any.
            if (households_[hid].extinct)
                for (const auto& h : households_)
                    if (!h.extinct && h.head != kNone && p.dynasty != kNone && h.dynasty == p.dynasty) {
                        p.awayFrom = h.id;
                        break;
                    }
        }
    } else if (!sd->away && wasAway) {
        comeHome(who);
    }
    // Settled in town for good: the family has someone there (design/GDD.md 階層流動).
    if (sd->lasting && id.rfind("town_", 0) == 0 && p.dynasty != kNone) {
        dynasties_[p.dynasty].townKin = who;
        dynasties_[p.dynasty].townSince = day_;
    }
}

void World::comeHome(Id who) {
    Person& p = people_[who];
    Id to = p.awayFrom;
    p.awayFrom = kNone;
    if (p.dynasty != kNone && dynasties_[p.dynasty].townKin == who) dynasties_[p.dynasty].townKin = kNone;
    moveTo(who, to);
}

// A house the family's people away belong to is gone (died out, or merged into another at a wedding): they
// belong to where it went, else to another house of their blood, else nowhere and they stay where they are.
void World::rehomeAway(Id from, Id to) {
    for (auto& p : people_) {
        if (!isAway(p) || p.awayFrom != from || p.id == leaving_) continue;
        Id dest = to;
        if (dest == kNone)
            for (const auto& h : households_)
                if (dest == kNone && h.id != from && !h.extinct && h.head != kNone && p.dynasty != kNone && h.dynasty == p.dynasty) dest = h.id;
        if (dest != kNone) {
            p.awayFrom = dest;
        } else if (p.station != "elsewhere") {
            const StationDef* sd = defs_.station(p.station);
            if (!(sd && sd->lasting)) setStation(p.id, "elsewhere", 0, "家裡已經沒人了，留在外地");
        }
    }
}

// A head works his own land: hired labour, service in another house, the walk home from the wars and a village
// apprenticeship end when someone takes up a house.
void World::settleAsHead(Id who) {
    if (who == kNone) return;
    const std::string& st = people_[who].station;
    if (st == "labourer" || st == "veteran" || st == "apprentice" || (st == "servant" && people_[who].home == kNone)) setStation(who, "home", 0, "");
}

Id World::boyForMaster(const Person& master) const {
    Id home = master.awayFrom;
    if (home == kNone || households_[home].extinct) return kNone;
    Id best = kNone;
    for (Id m : households_[home].members) {
        const Person& q = people_[m];
        double a = ageYears(q);
        if (!q.alive() || q.sex != Sex::Male || a < 11 || a > 16 || q.home != kNone || q.flags.count("trade_decided") ||
            households_[home].head == m || (households_[home].head != kNone && heirOf(households_[home], households_[home].head) == m))
            continue;
        if (best == kNone || q.birthDay < people_[best].birthDay) best = m;
    }
    return best;
}

void World::noteLife(Id who, const std::string& note) {
    if (who == kNone) return;
    Person& p = people_[who];
    p.road.push_back({day_, p.station.empty() ? "home" : p.station, note});
}

std::string World::lifeLine(Id id) const {
    const Person& p = people_[id];
    std::string out;
    std::vector<std::string> said;  // the same turn twice (elected hayward again) is told once
    for (const auto& r : p.road) {
        // "接著做…" only says the road went on: it is not a turn.
        if (r.note.empty() || r.note.rfind("接著", 0) == 0 || std::find(said.begin(), said.end(), r.note) != said.end()) continue;
        out += (out.empty() ? "" : "，") + r.note;
        said.push_back(r.note);
    }
    return out;
}

std::string World::lifeSummary(Id id) const {
    const Person& p = people_[id];
    std::string out = lifeLine(id);
    // A life with no turns written down (a wife married in before the family was handed over, a stepfather) is still
    // a life: what they did, whom they married, the children they had.
    if (out.empty()) {
        if (std::string occ = occupationName(p); !occ.empty()) out = "做了一輩子" + occ;
        if (p.spouse != kNone) out += (out.empty() ? "" : "，") + std::string(p.sex == Sex::Male ? "娶了 " : "嫁給 ") + fullName(p.spouse);
    }
    int kids = 0;
    for (Id c : p.children) kids += people_[c].alive() ? 1 : 0;
    if (!p.children.empty())
        out += (out.empty() ? "" : "，") + std::string("有過 ") + std::to_string(p.children.size()) + " 個孩子" +
               (kids == 0 ? "，一個也沒留住" : kids < static_cast<int>(p.children.size()) ? "，活下來 " + std::to_string(kids) + " 個" : "");
    return out.empty() ? "活了 " + std::to_string(int(ageYears(p))) + " 年" : out;
}

// A villein who has lived a year and a day in a town is free: checked every month, so it is a year and a day.
void World::monthlyAway() {
    for (Id id : livingAway()) {
        Person& p = people_[id];
        bool town = p.station.rfind("town_", 0) == 0 || p.station == "fled";
        if (town && !p.free && day_ - p.stationSince >= kDaysPerYear + 1) {
            p.free = true;
            if (inPlayerHouse(p.awayFrom)) log(1, "從鎮上捎來消息：" + fullName(id) + " 在城裡住滿了一年零一天，從此是自由人。");
        }
    }
}

// Once a year, for everyone living away: they age and may die out there. What else befalls them comes from
// the "away" events.
void World::yearlyAway() {
    for (Id id : livingAway()) {
        Person& p = people_[id];
        double age = ageYears(p);
        double annual = age < 15 ? 0.012 : age < 40 ? 0.018 : age < 50 ? 0.03 : age < 60 ? 0.05 : age < 70 ? 0.1 : 0.22;
        annual *= std::max(0.2, 1 + traitMod(p, "mortality"));
        if (rngLife_.chance(annual)) {
            kill(id, age >= 60 ? "年老" : "病");
            continue;
        }
    }
}

}  // namespace sim
