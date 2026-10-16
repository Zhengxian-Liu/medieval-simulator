// 處境 and 人生路 (design/life-paths.md): where each person is in life, and the road they took to get there.
// The forks themselves are ordinary events (data/events/paths.json); this file moves people between stations,
// sends them out of the village and brings them back, and follows the ones away with a yearly reckoning.
#include <algorithm>
#include <stdexcept>

#include "simcore/world.h"

namespace sim {

std::string World::stationName(const Person& p) const {
    const StationDef* sd = defs_.station(p.station.empty() ? "home" : p.station);
    return sd ? sd->name : p.station;
}

std::vector<Id> World::livingAway() const {
    std::vector<Id> v;
    for (const auto& p : people_)
        if (isAway(p)) v.push_back(p.id);
    return v;
}

void World::setStation(Id who, const std::string& station, int years, const std::string& note) {
    Person& p = people_[who];
    if (!p.alive()) return;
    const StationDef* sd = defs_.station(station);
    if (!sd) throw std::runtime_error("unknown station " + station);
    bool wasAway = isAway(p);
    if (sd->away && !wasAway && p.household == kNone) return;  // nowhere to leave from
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
        if (head) onHeadDied(households_[hid], who);
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

void World::noteLife(Id who, const std::string& note) {
    if (who == kNone) return;
    Person& p = people_[who];
    p.road.push_back({day_, p.station.empty() ? "home" : p.station, note});
}

std::string World::lifeLine(Id id) const {
    const Person& p = people_[id];
    std::string out;
    std::string last;
    for (const auto& r : p.road) {
        if (r.note.empty() || r.note == last) continue;
        out += (out.empty() ? "" : "，") + r.note;
        last = r.note;
    }
    return out;
}

// Once a year, for everyone living away: they age and may die out there, and a villein who has lived a year
// and a day in a town is free. What else befalls them comes from the "away" events.
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
        bool town = p.station.rfind("town_", 0) == 0 || p.station == "fled";
        if (town && !p.free && day_ - p.stationSince >= kDaysPerYear + 1) {
            p.free = true;
            if (inPlayerHouse(p.awayFrom)) log(1, "從鎮上捎來消息：" + fullName(id) + " 在城裡住滿了一年零一天，從此是自由人。");
        }
    }
}

}  // namespace sim
