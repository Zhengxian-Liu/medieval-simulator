// The web of acquaintance and how news travels it (design/news.md). Everyone knows their kin; beyond that each
// villager knows some people well and most a little, from meeting them (熟識, Person::known). Whatever happens
// starts with the houses it happened in and spreads from house to house along those ties: a death in a house the
// family is close to reaches it the same day, with a call to the wake; a scandal far off comes round in a month
// or two, or not at all.
#include <algorithm>
#include <cmath>

#include "simcore/world.h"

namespace sim {

namespace {

constexpr double kFloorLink = 0.03;  // church, the alehouse, the market: every house hears something of every other

}  // namespace

void World::befriend(Id a, Id b, double by) {
    if (a == kNone || b == kNone || a == b) return;
    auto grow = [&](Id x, Id y) {
        float& v = people_[x].known[y];
        v = static_cast<float>(std::clamp(v + by, 0.0, 100.0));
    };
    grow(a, b);
    grow(b, a);
}

void World::seedAcquaintance() {
    // A village that has lived together for generations: everyone already knows a handful of people of their age
    // well, and the heads of houses know each other from the manor court and the fields.
    std::vector<Id> all = livingInVillage();
    if (all.size() < 2) return;
    for (Id a : all) {
        double age = ageYears(people_[a]);
        if (age < 6) continue;
        for (int k = 0; k < 6; ++k) {
            Id b = all[rngNews_.range(0, static_cast<int>(all.size()) - 1)];
            if (b == a || people_[b].household == people_[a].household) continue;
            if (ageYears(people_[b]) < 6 || std::abs(ageYears(people_[b]) - age) > 12) continue;
            befriend(a, b, rngNews_.range(10, 35));
        }
    }
    std::vector<Id> heads;
    for (const auto& h : households_)
        if (!h.extinct && h.head != kNone && people_[h.head].alive()) heads.push_back(h.head);
    for (Id a : heads)
        for (int k = 0; k < 3 && heads.size() > 1; ++k) befriend(a, heads[rngNews_.range(0, static_cast<int>(heads.size()) - 1)], 20);
}

void World::fadeAcquaintance() {
    for (auto& p : people_) {
        if (p.known.empty()) continue;
        if (!p.alive()) {
            p.known.clear();
            continue;
        }
        for (auto it = p.known.begin(); it != p.known.end();) {
            it->second *= 0.95f;
            if (it->second < 2 || !people_[it->first].alive()) it = p.known.erase(it);
            else ++it;
        }
    }
}

Id World::pickAcquaintance(Id who, Rng& rng) const {
    const Person& p = people_[who];
    double total = 0;
    for (const auto& [id, v] : p.known)
        if (people_[id].alive() && people_[id].household != kNone) total += v;
    if (total <= 0) return kNone;
    double r = rng.uniform() * total;
    for (const auto& [id, v] : p.known) {
        if (!people_[id].alive() || people_[id].household == kNone) continue;
        if (r < v) return id;
        r -= v;
    }
    return kNone;
}

namespace {

// Kin, closest first, with how close the tie is (0–100).
template <class F>
void forKin(const std::vector<Person>& people, const Person& x, F&& f) {
    // The dead count too (who mourns them); callers that want the living check for themselves.
    auto each = [&](Id i, double v) {
        if (i != kNone && i != x.id) f(i, v);
    };
    each(x.spouse, 90);
    for (Id par : {x.father, x.mother}) {
        if (par == kNone) continue;
        each(par, 90);
        for (Id s : people[par].children) each(s, 85);  // siblings and half-siblings
        for (Id g : {people[par].father, people[par].mother}) {
            if (g == kNone) continue;
            each(g, 60);
            for (Id u : people[g].children)
                if (u != par) each(u, 55);  // aunts and uncles
        }
    }
    for (Id c : x.children) {
        each(c, 90);
        if (c != kNone) {
            each(people[c].spouse, 50);
            for (Id gc : people[c].children) each(gc, 60);
        }
    }
    if (x.spouse != kNone)
        for (Id par : {people[x.spouse].father, people[x.spouse].mother}) {
            if (par == kNone) continue;
            each(par, 50);
            for (Id s : people[par].children)
                if (s != x.spouse) each(s, 45);
        }
}

}  // namespace

double World::closeness(Id a, Id b) const {
    if (a == kNone || b == kNone) return 0;
    if (a == b) return 100;
    const Person& x = people_[a];
    const Person& y = people_[b];
    if (x.household != kNone && x.household == y.household) return 100;
    double best = 0;
    forKin(people_, x, [&](Id k, double v) {
        if (k == b) best = std::max(best, v);
    });
    auto it = x.known.find(b);
    if (it != x.known.end()) best = std::max(best, double(it->second));
    return best;
}

std::vector<double> World::houseLinks(Id hh) const {
    std::vector<double> row(households_.size(), 0.0);
    if (hh == kNone) return row;
    row[hh] = 1;
    // Children's friendships tie two houses less than their parents' do. (A member who has just died still
    // counts here: whoever knew them hears first.)
    auto weight = [&](const Person& x) { return ageYears(x) < 14 ? 0.5 : 1.0; };
    for (Id m : households_[hh].members) {
        const Person& x = people_[m];
        double wx = weight(x);
        auto touch = [&](Id other, double v) {
            if (!people_[other].alive()) return;
            Id oh = people_[other].household;
            if (oh != kNone && oh != hh) row[oh] = std::max(row[oh], v / 100 * std::min(wx, weight(people_[other])));
        };
        forKin(people_, x, touch);
        for (const auto& [id, v] : x.known)
            if (people_[id].alive()) touch(id, v);
    }
    return row;
}

double World::houseLink(Id ha, Id hb) const {
    if (ha == kNone || hb == kNone) return 0;
    if (ha == hb) return 1;
    return houseLinks(ha)[hb];
}

std::string World::formatNews(const std::string& tmpl, const News& n) const {
    std::string out;
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] != '{') {
            out += tmpl[i++];
            continue;
        }
        size_t end = tmpl.find('}', i);
        std::string key = tmpl.substr(i + 1, end - i - 1);
        std::string val;
        // The family's own people go by the names the family calls them.
        auto name = [&](Id id) { return id == kNone ? std::string("某人") : isPlayerFamily(people_[id]) ? shortName(id) : fullName(id); };
        if (key == "subject") val = n.subjectName.empty() ? name(n.subject) : n.subjectName;
        else if (key == "other") val = n.otherName.empty() ? name(n.other) : n.otherName;
        else if (key == "subject_first") val = !n.subjectFirst.empty() ? n.subjectFirst : n.subject != kNone ? shortName(n.subject) : "";
        else if (key == "house") {
            Id dyn = n.house != kNone ? households_[n.house].dynasty : kNone;
            val = dyn != kNone ? dynasties_[dyn].surname : "";
        } else if (key == "cause") val = n.cause;
        else if (key == "again") val = n.again ? "又" : "";
        else if (key == "age") val = std::to_string(n.age);
        else if (key == "sexword") val = n.subject != kNone && people_[n.subject].sex == Sex::Male ? "兒子" : "女兒";
        else val = "{" + key + "}";
        out += val;
        i = end + 1;
    }
    return out;
}

std::string World::tellerName(Id teller) const {
    if (teller == kNone) return "";
    Id head = playerHead();
    if (head != kNone && isPlayerFamily(people_[teller]) && teller != head) {
        std::string kin = kinWord(head, teller);
        return (kin == "親戚" ? "" : kin) + shortName(teller);
    }
    return fullName(teller);
}

std::string World::newsText(const News& n) const {
    const NewsKindDef* k = defs_.newsKind(n.kind);
    return k ? formatNews(k->text, n) : n.kind;
}

Id World::addNews(const std::string& kind, Id subject, Id other, Id house, Id deed) {
    const NewsKindDef* k = defs_.newsKind(kind);
    if (!k || house == kNone || households_[house].extinct) return kNone;
    // The same thing told twice (both lovers' side of one affair) is one piece of news, and the same help given
    // again while the last time is still talked of is not news again.
    for (auto it = news_.rbegin(); it != news_.rend() && day_ - it->day < k->fadeMonths * kDaysPerMonth; ++it)
        if (it->kind == kind && (it->day == day_ || deed != kNone) &&
            ((it->subject == subject && it->other == other) || (it->subject == other && it->other == subject)))
            return it->id;
    News n;
    n.id = static_cast<Id>(news_.size());
    n.kind = kind;
    n.subject = subject;
    n.other = other;
    n.deed = deed;
    n.house = house;
    n.day = day_;
    if (subject != kNone) {
        n.cause = people_[subject].deathCause;
        n.age = static_cast<int>(ageYears(people_[subject]));
    }
    auto name = [&](Id id) { return id == kNone ? std::string() : isPlayerFamily(people_[id]) ? shortName(id) : fullName(id); };
    n.subjectName = name(subject);
    n.otherName = name(other);
    n.subjectFirst = subject != kNone ? shortName(subject) : "";
    for (const auto& o : news_)
        if (o.kind == kind && other != kNone && o.other == other && o.subject != subject) n.again = true;
    // The houses it happened in know it; the houses close to them know it at once.
    std::vector<Id> origin{house};
    for (Id who : {subject, other})
        if (who != kNone && people_[who].household != kNone && people_[who].household != house) origin.push_back(people_[who].household);
    std::vector<double> link(households_.size(), 0.0);
    for (Id o : origin) {
        n.heard.insert(o);
        auto row = houseLinks(o);
        for (size_t i = 0; i < row.size(); ++i) link[i] = std::max(link[i], row[i]);
    }
    // Who knew the dead counts as much as who knows the house they leave.
    if (subject != kNone && !people_[subject].alive())
        for (Id h = 0; h < static_cast<Id>(households_.size()); ++h)
            for (Id m : households_[h].members)
                if (m != subject) link[h] = std::max(link[h], closeness(m, subject) / 100);
    news_.push_back(n);
    Id idx = n.id;
    std::vector<Id> close;
    for (Id h = 0; h < static_cast<Id>(households_.size()); ++h) {
        if (households_[h].extinct || households_[h].members.empty() || news_[idx].heard.count(h)) continue;
        if (link[h] >= k->instant) close.push_back(h);
    }
    for (Id h : close) {
        // The teller: whoever in the houses it happened in is closest to someone here.
        Id teller = kNone;
        double best = -1;
        for (Id o : origin)
            for (Id t : households_[o].members) {
                if (!people_[t].alive() || ageYears(people_[t]) < 10) continue;
                for (Id m : households_[h].members) {
                    // Someone else tells it, if anyone can: the one it is about is not the one who spreads it.
                    double c = closeness(t, m) - (t == subject || t == other ? 200 : 0);
                    if (c > best) best = c, teller = t;
                }
            }
        if (teller == subject || teller == other) teller = kNone;
        learnNews(news_[idx], h, teller, link[h]);
    }
    // A wake or a wedding: the houses close to it turn out, and the people who come meet one another.
    if (k->gathering) {
        for (Id h : close) {
            if (h == playerHousehold_) continue;  // the family is asked, and decides (data/events/occasions.json)
            Id head = households_[h].head;
            if (head == kNone || !people_[head].alive() || ageYears(people_[head]) < 16) continue;
            if (feudStage(head, subject) >= 2) continue;  // not to the wake of an enemy's house
            if (!rngNews_.chance(0.4 + 0.6 * link[h])) continue;
            attendOccasion(head, subject, true, 4, k->attendMod);
        }
    }
    return idx;
}

// Someone came to the house's wake or wedding (or, came = false, stayed away when they should have been there).
void World::attendOccasion(Id person, Id about, bool came, double value, const std::string& mod) {
    if (person == kNone || about == kNone) return;
    News* n = nullptr;
    for (auto it = news_.rbegin(); it != news_.rend(); ++it)
        if ((it->kind == "death" || it->kind == "marriage") && it->subject == about) {
            n = &*it;
            break;
        }
    if (!n) return;
    std::vector<Id> hosts;
    for (Id who : {n->subject, n->other})
        if (who != kNone && people_[who].household != kNone)
            for (Id m : households_[people_[who].household].members)
                // The guest's own people living there (a son in service) are not the hosts.
                if (m != person && people_[m].alive() && ageYears(people_[m]) >= 10 &&
                    people_[m].home != people_[person].household &&
                    std::find(hosts.begin(), hosts.end(), m) == hosts.end())
                    hosts.push_back(m);
    for (Id m : hosts) {
        // Staying away is noticed by those who expected you: kin and the people who know you.
        if (!came && closeness(m, person) < 30) continue;
        addOpinion(m, person, mod, value, came ? 8 : 6);
        if (came) befriend(person, m, 8);
    }
    if (!came) return;
    // Sitting up with the dead, or at the wedding ale: the people there get to know one another.
    std::vector<Id> others;
    for (Id c : n->came)
        if (c != person && people_[c].alive()) others.push_back(c);
    int met = 0;
    std::string names;
    for (size_t i = 0; i < others.size() && met < 2; ++i) {
        Id o = others[rngNews_.range(0, static_cast<int>(others.size()) - 1)];
        befriend(person, o, 6);
        if (person == playerHead() && names.find(shortName(o)) == std::string::npos) {
            names += (names.empty() ? "" : "、") + fullName(o);
            ++met;
        }
    }
    n->came.push_back(person);
    if (person == playerHead() && !names.empty())
        seasonNotes_.push_back(std::string(n->kind == "death" ? "守靈那晚" : "喜酒席上") + "，你和" + names + "多說了好些話。");
}

void World::learnNews(News& n, Id household, Id teller, double link) {
    if (household == kNone || !n.heard.insert(household).second) return;
    const NewsKindDef* k = defs_.newsKind(n.kind);
    if (!k) return;
    // Hearing of a deed is how the village comes to think well or ill of the one who did it.
    if (n.deed != kNone) {
        const Deed& d = deeds_[n.deed];
        const DeedDef* def = defs_.deed(d.type);
        double w = k->hasWitness ? k->witness : 0;
        if (!k->hasWitness && def) {
            auto it = def->roles.find("witness");
            if (it != def->roles.end()) w = it->second;
        }
        w *= d.scale;
        if (w != 0 && d.actor != kNone && def)
            for (Id m : households_[household].members) {
                if (m == d.actor || m == d.target || !people_[m].alive() || ageYears(people_[m]) < 10) continue;
                Memory mem;
                mem.deed = d.id;
                mem.about = d.actor;
                mem.aboutDynasty = def->family && people_[m].dynasty != people_[d.actor].dynasty ? people_[d.actor].dynasty : kNone;
                mem.label = d.type;
                mem.value = w;
                mem.sinceDay = day_;
                mem.halfLife = def->halfLife;
                addMemory(m, mem);
            }
    }
    bool aboutFamily = n.house == playerHousehold_ || (n.subject != kNone && people_[n.subject].household == playerHousehold_) ||
                       (n.other != kNone && people_[n.other].household == playerHousehold_);
    // The family's own births, deaths and weddings elsewhere are told in the chronicle already.
    bool ownKin = n.deed == kNone && ((n.subject != kNone && isPlayerFamily(people_[n.subject])) ||
                                      (n.other != kNone && isPlayerFamily(people_[n.other])));
    if (household == playerHousehold_ && !aboutFamily && !ownKin) {
        Heard h;
        h.day = day_;
        h.news = n.id;
        h.teller = teller;
        h.text = newsText(n);
        // What comes from people the family is close to, or is the talk of the village, is told as it comes.
        double weight = 0;
        if (n.deed != kNone)
            if (const DeedDef* def = defs_.deed(deeds_[n.deed].type)) {
                auto it = def->roles.find("witness");
                weight = it != def->roles.end() ? std::abs(it->second) : 0;
            }
        h.logged = link >= k->logLink || weight >= 4;
        h.weight = link + k->heat + weight / 4;
        if (h.logged)
            log(1, "【消息】" + (teller != kNone ? "聽" + tellerName(teller) + "說，" : std::string()) + h.text + "。");
        heard_.push_back(h);
        // A death in a house the family is close to: they are asked to the wake. A wedding: to the ale.
        Id head = actingHead();
        bool mourner = head != kNone && n.subject != kNone && closeness(head, n.subject) >= 85;  // their own grief, not a call
        if (!k->invite.empty() && link >= k->inviteLink && n.age >= k->inviteMinAge && head != kNone && !mourner)
            emit({"news:" + k->invite, n.deed, head, n.subject, false, link});
    }
    // What the village says of the family: once most of it has heard, the family hears that it has.
    if (aboutFamily && k->wide && !n.wideNoted && household != playerHousehold_) {
        int live = 0, knew = 0;
        for (const auto& h : households_)
            if (!h.extinct && !h.members.empty()) {
                ++live;
                knew += n.heard.count(h.id) ? 1 : 0;
            }
        if (live > 0 && knew * 10 >= live * 6) {
            n.wideNoted = true;
            // The village's talk of the family comes up once a season at most: the latest is what people say.
            if (day_ - lastWideDay_ < 3 * kDaysPerMonth) return;
            lastWideDay_ = day_;
            log(1, "【風聲】" + newsText(n) + "——這事村裡大半的人家都知道了。");
        }
    }
}

void World::monthlyNews() {
    fadeAcquaintance();
    std::vector<Id> live;
    for (const auto& h : households_)
        if (!h.extinct && !h.members.empty()) live.push_back(h.id);
    std::vector<Id> active;
    for (const auto& n : news_) {
        const NewsKindDef* k = defs_.newsKind(n.kind);
        if (k && day_ - n.day < k->fadeMonths * kDaysPerMonth && n.heard.size() < live.size()) active.push_back(n.id);
    }
    if (active.empty()) return;
    std::map<Id, std::vector<double>> rows;
    for (Id h : live) rows[h] = houseLinks(h);
    for (Id idx : active) {
        const NewsKindDef* k = defs_.newsKind(news_[idx].kind);
        double fresh = 1 - (day_ - news_[idx].day) / double(k->fadeMonths * kDaysPerMonth);
        std::vector<std::pair<Id, Id>> learned;  // (household, from household)
        for (Id u : live) {
            if (news_[idx].heard.count(u)) continue;
            for (Id from : news_[idx].heard) {
                auto row = rows.find(from);
                if (row == rows.end()) continue;
                double l = std::max(kFloorLink, row->second[u]);
                if (rngNews_.chance(k->heat * l * fresh)) {
                    learned.push_back({u, from});
                    break;
                }
            }
        }
        for (auto [u, from] : learned) {
            // Whoever there knows someone here best tells it.
            Id teller = kNone;
            double best = -1e9;
            const News& n = news_[idx];
            for (Id t : households_[from].members) {
                if (!people_[t].alive() || ageYears(people_[t]) < 10) continue;
                for (Id m : households_[u].members) {
                    double c = closeness(t, m) - (t == n.subject || t == n.other ? 200 : 0);
                    if (c > best) best = c, teller = t;
                }
            }
            if (teller == news_[idx].subject || teller == news_[idx].other) teller = kNone;
            learnNews(news_[idx], u, teller, rows[from][u]);
        }
    }
}

// A belief about a secret reached the family by word of mouth: the family hears the whisper, true or not.
void World::whisper(Id holder, Id deed, Id culprit, Id source) {
    if (!whispered_.insert(deed).second) return;
    const Deed& d = deeds_[deed];
    auto it = defs_.whispers.find(d.type);
    News n;
    n.subject = culprit;
    n.other = d.target;
    const DeedDef* def = defs_.deed(d.type);
    std::string text = it != defs_.whispers.end() ? formatNews(it->second, n)
                                                   : formatNews("{subject}背地裡做了「" + (def ? def->name : d.type) + "」的事", n);
    Heard h;
    h.day = day_;
    h.teller = source;
    h.text = text;
    h.weight = 3;  // a secret is worth more than any ordinary news
    // The head hears it straight; what the others hear comes to the head through them.
    if (holder != playerHead()) h.text += "（" + shortName(holder) + "聽來的）";
    heard_.push_back(h);
}

std::string World::villageTalk() const {
    // What the family heard last season and was not already told as it came.
    std::vector<const Heard*> recent;
    for (auto it = heard_.rbegin(); it != heard_.rend() && day_ - it->day <= 3 * kDaysPerMonth; ++it)
        if (!it->logged) recent.push_back(&*it);
    if (recent.empty()) return "";
    // What matters most to the family first: news from close by, the talk of the village, a whisper.
    std::stable_sort(recent.begin(), recent.end(), [](const Heard* a, const Heard* b) { return a->weight > b->weight; });
    std::string o = "村裡在說：\n";
    for (size_t i = 0; i < recent.size() && i < 4; ++i) {
        const Heard& h = *recent[i];
        std::string who = h.teller != kNone ? tellerName(h.teller) : "有人";
        o += "  " + (h.news == kNone ? who + "悄悄說，" + h.text + "，不知真假。" : "聽" + who + "說，" + h.text + "。") + "\n";
    }
    if (recent.size() > 4) o += "  （還有 " + std::to_string(recent.size() - 4) + " 件小事。）\n";
    return o;
}

}  // namespace sim
