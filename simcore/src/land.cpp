// Land as named parcels on a block map (design/contested.md). At the start every household's acres are cut into
// three to five pieces scattered over the open fields, as strips were; from then on land changes hands only
// through moveLand, which moves whole parcels where it can and cuts one when it must, so a household's acres
// and freehold are always the sums of what it holds and the map always shows who holds what.
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "simcore/world.h"

namespace sim {

namespace {

std::string acresText(double a) {
    double r = std::round(a * 2) / 2;
    if (std::abs(r - std::round(r)) < 1e-9) return std::to_string(static_cast<int>(std::round(r)));
    return std::to_string(static_cast<int>(r)) + ".5";
}

// Cuts `total` acres into `n` whole-acre pieces of uneven size, none under one acre.
std::vector<double> cut(double total, int n, Rng& rng) {
    std::vector<double> out;
    int whole = static_cast<int>(std::floor(total + 1e-9));
    if (whole < 1) return {total};
    double rest = total - whole;
    n = std::max(1, std::min(n, std::max(1, whole)));
    std::vector<int> sizes(n, 1);
    for (int left = whole - n; left > 0; --left) sizes[rng.range(0, n - 1)] += 1;
    for (int s : sizes) out.push_back(s);
    if (rest > 1e-9) out.back() += rest;
    return out;
}

}  // namespace

std::string World::parcelName(const Parcel& pc) const {
    return pc.place + "那 " + acresText(pc.acres) + " 畝";
}

double World::vacantAcres() const {
    double a = 0;
    for (const auto& pc : parcels_)
        if (!pc.gone && pc.holder == kNone) a += pc.acres;
    return a;
}

void World::syncLand(Id household) {
    if (household == kNone) return;
    Household& h = households_[household];
    h.acres = h.freehold = 0;
    for (const auto& pc : parcels_)
        if (!pc.gone && pc.holder == household) {
            h.acres += pc.acres;
            if (pc.free) h.freehold += pc.acres;
        }
}

void World::placeHouse(Id household) {
    if (map_.width == 0 || map_.houseCell.count(household)) return;
    std::set<int> taken;
    for (const auto& [h, c] : map_.houseCell)
        if (!households_[h].extinct) taken.insert(c);
    std::set<int> nearPoi;  // a cottage right against the church or the well crowds it
    for (const auto& poi : map_.pois) {
        taken.insert(poi.y * map_.width + poi.x);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) nearPoi.insert((poi.y + dy) * map_.width + poi.x + dx);
    }
    const MapArea& a = map_.houses;
    // Every other cell away from the landmarks, so the cottages stand apart; then the gaps.
    for (int pass = 0; pass < 3; ++pass)
        for (int y = a.y; y < a.y + a.h; ++y)
            for (int x = a.x; x < a.x + a.w; ++x) {
                int c = y * map_.width + x;
                if (pass < 2 && (x + y) % 2 != 0) continue;
                if (pass == 0 && nearPoi.count(c)) continue;
                if (taken.count(c)) continue;
                map_.houseCell[household] = c;
                return;
            }
}

void World::setupLand() {
    const Json& m = defs_.village["map"];
    map_ = VillageMap{};
    map_.width = m["width"].asInt();
    map_.height = m["height"].asInt();
    auto area = [](const Json& j) {
        MapArea a;
        a.kind = j.stringOr("kind", "field");
        a.name = j.stringOr("name", "");
        a.x = j["x"].asInt();
        a.y = j["y"].asInt();
        a.w = j["w"].asInt();
        a.h = j["h"].asInt();
        return a;
    };
    std::vector<std::vector<std::string>> places;
    for (const auto& f : m["fields"].asArray()) {
        map_.fields.push_back(area(f));
        std::vector<std::string> ps;
        for (const auto& p : f["places"].asArray()) ps.push_back(p.asString());
        places.push_back(ps);
    }
    for (const auto& z : m["zones"].asArray()) map_.zones.push_back(area(z));
    map_.houses = area(m["houses"]);
    for (const auto& p : m["pois"].asArray()) map_.pois.push_back({p["kind"].asString(), p["name"].asString(), p["x"].asInt(), p["y"].asInt()});

    // The pieces: each household's customary and free acres, the lord's demesne in great blocks, the vacant land.
    struct Piece {
        Id holder;
        double acres;
        bool free;
        bool demesne;
    };
    std::vector<Piece> pieces;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    for (const auto& h : households_) {
        if (h.acres <= 0) continue;
        if (h.id == lordHouse) {
            for (double a : cut(h.acres, std::max(1, static_cast<int>(std::round(h.acres / 45))), rngLand_)) pieces.push_back({h.id, a, false, true});
            continue;
        }
        int n = std::clamp(static_cast<int>(std::round(h.acres / 4)), 1, 5);
        double fh = std::min(h.freehold, h.acres);
        int nFree = fh > 0 ? std::max(1, static_cast<int>(std::round(n * fh / h.acres))) : 0;
        if (nFree >= n && h.acres - fh > 0) nFree = n - 1;
        if (fh > 0)
            for (double a : cut(fh, nFree, rngLand_)) pieces.push_back({h.id, a, true, false});
        if (h.acres - fh > 0)
            for (double a : cut(h.acres - fh, std::max(1, n - nFree), rngLand_)) pieces.push_back({h.id, a, false, false});
    }
    for (double a : cut(defs_.village.numberOr("vacant_acres", 10), 3, rngLand_)) pieces.push_back({kNone, a, false, false});

    // Spread each holder's pieces over the fields; big pieces first so they find room.
    std::vector<int> room;
    for (const auto& f : map_.fields) room.push_back(f.w * f.h);
    std::vector<std::vector<size_t>> inField(map_.fields.size());
    std::vector<size_t> order(pieces.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return pieces[a].acres > pieces[b].acres; });
    std::map<Id, int> nextField;
    for (size_t i : order) {
        const Piece& pc = pieces[i];
        int need = std::max(1, static_cast<int>(std::round(pc.acres)));
        int start = nextField.count(pc.holder) ? nextField[pc.holder] : rngLand_.range(0, static_cast<int>(room.size()) - 1);
        int chosen = -1;
        for (size_t k = 0; k < room.size(); ++k) {
            int f = static_cast<int>((start + k) % room.size());
            if (room[f] >= need) {
                chosen = f;
                break;
            }
        }
        if (chosen < 0) throw std::runtime_error("village.json: the fields are too small for the village's land");
        room[chosen] -= need;
        nextField[pc.holder] = (chosen + 1) % static_cast<int>(room.size());
        inField[chosen].push_back(i);
    }

    // Within a field the pieces lie in random order, cut as strips across its short side.
    parcels_.clear();
    std::vector<size_t> usedPlace(map_.fields.size(), 0);
    for (size_t f = 0; f < map_.fields.size(); ++f) {
        auto& list = inField[f];
        for (size_t i = list.size(); i > 1; --i) std::swap(list[i - 1], list[rngLand_.range(0, static_cast<int>(i) - 1)]);
        const MapArea& a = map_.fields[f];
        std::vector<int> cells;
        bool across = a.w >= a.h;  // strips run across the short side
        int lines = across ? a.w : a.h, len = across ? a.h : a.w;
        for (int i = 0; i < lines; ++i)
            for (int j = 0; j < len; ++j) {
                int k = i % 2 ? len - 1 - j : j;
                int x = across ? a.x + i : a.x + k, y = across ? a.y + k : a.y + i;
                cells.push_back(y * map_.width + x);
            }
        size_t at = 0;
        // Shuffle the field's place names so two games do not name the same strips alike.
        auto& ps = places[f];
        for (size_t i = ps.size(); i > 1; --i) std::swap(ps[i - 1], ps[rngLand_.range(0, static_cast<int>(i) - 1)]);
        for (size_t i : list) {
            const Piece& pc = pieces[i];
            Parcel p;
            p.id = static_cast<Id>(parcels_.size());
            p.acres = pc.acres;
            p.free = pc.free;
            p.holder = pc.holder;
            p.field = static_cast<int>(f);
            p.how = pc.holder == kNone ? "沒人租" : pc.demesne ? "領主自營" : "祖傳";
            if (pc.demesne) p.place = "領主在" + a.name + "的大塊地";
            else if (usedPlace[f] < ps.size()) p.place = a.name + ps[usedPlace[f]++];
            else p.place = a.name + "第 " + std::to_string(++usedPlace[f]) + " 條";
            int need = std::max(1, static_cast<int>(std::round(pc.acres)));
            for (int c = 0; c < need && at < cells.size(); ++c) p.cells.push_back(cells[at++]);
            parcels_.push_back(p);
        }
    }
    for (const auto& h : households_) {
        syncLand(h.id);
        if (!h.extinct) placeHouse(h.id);
    }
}

Id World::splitParcel(Id parcel, double acres) {
    Parcel& old = parcels_[parcel];
    if (acres <= 0 || acres >= old.acres) return parcel;
    static const char* ends[] = {"東頭", "西頭", "那一角", "南半", "北半"};
    Parcel p = old;
    p.id = static_cast<Id>(parcels_.size());
    int k = 0;
    for (const auto& q : parcels_)
        if (q.place.rfind(old.place, 0) == 0) ++k;
    p.place = old.place + ends[(k - 1) % 5];
    p.acres = acres;
    size_t take = static_cast<size_t>(std::round(old.cells.size() * acres / old.acres));
    take = std::min(std::max<size_t>(take, old.cells.size() > 1 ? 1 : 0), old.cells.size() > 1 ? old.cells.size() - 1 : 0);
    p.cells.assign(old.cells.end() - static_cast<long>(take), old.cells.end());
    old.cells.resize(old.cells.size() - take);
    old.acres -= acres;
    parcels_.push_back(p);
    return p.id;
}

double World::moveLand(Id from, Id to, double acres, LandPick pick, const std::string& how) {
    lastMoved_.clear();
    if (acres <= 1e-6 || from == to) return 0;
    std::vector<Id> mine;
    for (const auto& pc : parcels_)
        if (!pc.gone && pc.holder == from && !(pick == LandPick::CustomaryOnly && pc.free)) mine.push_back(pc.id);
    // Customary land goes first unless freehold is wanted; within that, small parcels first so whole ones move.
    std::stable_sort(mine.begin(), mine.end(), [&](Id a, Id b) {
        const Parcel &pa = parcels_[a], &pb = parcels_[b];
        bool fa = pick == LandPick::FreeFirst ? !pa.free : pa.free, fb = pick == LandPick::FreeFirst ? !pb.free : pb.free;
        if (fa != fb) return !fa;
        return pa.acres < pb.acres;
    });
    double left = acres;
    for (Id id : mine) {
        if (left <= 1e-6) break;
        Id take = id;
        if (parcels_[id].acres > left + 1e-6) take = splitParcel(id, left);
        Parcel& pc = parcels_[take];
        pc.holder = to;
        pc.since = day_;
        pc.how = how;
        if (to == kNone) pc.free = false;  // land that falls back to the lord is his to grant again on his terms
        left -= pc.acres;
        lastMoved_.push_back(take);
    }
    syncLand(from);
    syncLand(to);
    if (to != kNone && households_[to].extinct == false) placeHouse(to);
    // Whoever else was after these parcels has lost them (land falling back to the lord is still to be had).
    if (to != kNone) {
        std::vector<Id> moved = lastMoved_;
        for (Id id : moved) settleWants("parcel", id, "", to, "到手了");
        lastMoved_ = moved;
    }
    double moved = acres - std::max(0.0, left);
    if (moved > 1e-6 && (inPlayerHouse(from) || inPlayerHouse(to)) && playerHousehold_ != kNone) {
        auto who = [&](Id h) { return h == kNone ? std::string("領主手上") : dynasties_[households_[h].dynasty].surname + " 家"; };
        log(2, "  （" + movedText() + "從" + who(from) + "到了" + who(to) + "。）");
    }
    return moved;
}

void World::moveParcel(Id parcel, Id to, const std::string& how) {
    Parcel& pc = parcels_[parcel];
    Id from = pc.holder;
    if (pc.gone || from == to) return;
    pc.holder = to;
    pc.since = day_;
    pc.how = how;
    if (to != kNone && (how == "買來" || how == "向領主租來") && households_[to].head != kNone)
        noteLife(households_[to].head, (how == "買來" ? "買下了" : "租下了") + parcelName(pc));
    if (to == kNone) pc.free = false;
    lastMoved_ = {parcel};
    syncLand(from);
    syncLand(to);
    if (to != kNone && !households_[to].extinct) placeHouse(to);
    if (to != kNone) settleWants("parcel", parcel, "", to, "到手了");
    lastMoved_ = {parcel};
    if ((inPlayerHouse(from) || inPlayerHouse(to)) && playerHousehold_ != kNone) {
        auto who = [&](Id h) { return h == kNone ? std::string("領主手上") : dynasties_[households_[h].dynasty].surname + " 家"; };
        log(2, "  （" + movedText() + "從" + who(from) + "到了" + who(to) + "。）");
    }
}

void World::makeFree(Household& h, double acres) {
    double left = acres;
    std::vector<Id> mine;
    for (const auto& pc : parcels_)
        if (!pc.gone && pc.holder == h.id && !pc.free) mine.push_back(pc.id);
    std::stable_sort(mine.begin(), mine.end(), [&](Id a, Id b) { return parcels_[a].acres < parcels_[b].acres; });
    for (Id id : mine) {
        if (left <= 1e-6) break;
        Id take = parcels_[id].acres > left + 1e-6 ? splitParcel(id, left) : id;
        parcels_[take].free = true;
        left -= parcels_[take].acres;
    }
    syncLand(h.id);
}

std::string World::movedText() const {
    std::string s;
    for (size_t i = 0; i < lastMoved_.size(); ++i) s += (i ? "、" : "") + parcelName(parcels_[lastMoved_[i]]);
    return s;
}

// Why a house would let a few acres go, in its neighbours' words ("" if it would not): debts, an old man with
// no son to take the plough, or more land than hands (after a pestilence, most of all).
std::string World::sellReason(const Household& h) const {
    if (h.extinct || h.head == kNone || h.acres < 8 || inPlayerHouse(h.id)) return "";
    double hands = 0;
    bool son = false;
    for (Id m : h.members) {
        const Person& q = people_[m];
        if (q.sex == Sex::Male && ageYears(q) >= 14 && q.health >= 40) hands += 1;
        if (m != h.head && q.sex == Sex::Male && ageYears(q) >= 14 && (q.father == h.head || q.mother == h.head)) son = true;
    }
    for (const auto& q : people_)  // a son away in service comes home to it
        if (!son && q.alive() && q.home == h.id && q.sex == Sex::Male && ageYears(q) >= 14 && (q.father == h.head || q.mother == h.head)) son = true;
    if (h.debt >= 20 && h.creditor != playerHead()) return "欠著債，等錢用";
    if (!son && ageYears(people_[h.head]) > 55) return "沒有兒子接手，老人家種不動了";
    if (h.acres > hands * 12 + 4) return worldFlags_.count("post_plague") ? "瘟疫以後人手不夠，地荒著" : "地多人少，種不過來";
    return "";
}

World::LandDeal World::landDeal(const Household& buyer, const Household& seller, double acres, double price, bool wanted, double extra) const {
    LandDeal d;
    if (acres <= 0) return d;
    if (wanted)
        for (const auto& wt : wants_)
            if (wt.open && wt.house == buyer.id && wt.kind == "parcel" && parcels_[wt.object].holder == seller.id) d.parcel = wt.object;
    if (d.parcel != kNone) {
        d.acres = parcels_[d.parcel].acres;
        d.price = std::round(d.acres * price / acres);
        d.what = parcelName(parcels_[d.parcel]);  // "北田坡上那 3 畝": the acres are in the name already
    } else {
        d.acres = std::min(acres, seller.acres);
        d.price = std::round(price * d.acres / acres);
        d.what = " " + std::to_string(static_cast<int>(std::lround(d.acres))) + " 畝地";  // spaced like other numbers in text
    }
    if (d.acres > 0) d.price += extra;  // raised in the haggling, on top of what the land is worth
    d.ok = d.acres > 0 && buyer.money >= d.price;
    return d;
}

}  // namespace sim
