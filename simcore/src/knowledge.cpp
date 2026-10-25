// Knowledge: who believes what about secret deeds. The deed itself is the fact; a belief is
// what one person thinks happened, and it can name the wrong culprit. Rumours spread beliefs,
// getting less certain and sometimes garbled with every retelling.
#include <algorithm>
#include <cmath>

#include "simcore/world.h"

namespace sim {

namespace {

bool isCrime(const DeedDef& d) { return std::find(d.tags.begin(), d.tags.end(), "crime") != d.tags.end(); }

}  // namespace

const Belief* World::beliefAbout(Id holder, Id deed) const {
    for (const auto& b : beliefs_[holder])
        if (b.deed == deed) return &b;
    return nullptr;
}

// Who knows about a secret deed the moment it happens: the doer, sometimes a witness,
// and the priest if a pious doer goes to confession.
void World::knowAtDeed(const Deed& d, const DeedDef& def) {
    addBelief(d.actor, d.id, d.actor, 100, kNone);
    std::vector<Id> pool;
    for (Id id : livingInVillage()) {
        const Person& p = people_[id];
        if (id == d.actor || id == d.target || ageYears(p) < 10) continue;
        if (p.household == people_[d.actor].household) continue;
        pool.push_back(id);
    }
    if (!pool.empty() && rngKnow_.chance(0.25)) {
        Id witness = pool[rngKnow_.range(0, static_cast<int>(pool.size()) - 1)];
        Id seen = d.actor;
        // In the dark, a witness may take someone else from the same house for the doer.
        if (rngKnow_.chance(0.15)) {
            std::vector<Id> lookalikes;
            for (Id m : households_[people_[d.actor].household].members)
                if (m != d.actor && people_[m].sex == people_[d.actor].sex && ageYears(people_[m]) >= 14 && people_[m].alive())
                    lookalikes.push_back(m);
            if (!lookalikes.empty()) seen = lookalikes[rngKnow_.range(0, static_cast<int>(lookalikes.size()) - 1)];
        }
        addBelief(witness, d.id, seen, 80, kNone);
    }
    if (priest_ != kNone && hasTrait(people_[d.actor], "pious") && rngKnow_.chance(0.5))
        addBelief(priest_, d.id, d.actor, 95, d.actor);
}

void World::addBelief(Id holder, Id deed, Id culprit, double confidence, Id source) {
    if (holder == kNone || culprit == kNone || !people_[holder].alive()) return;
    // Nobody believes a child did it, or someone not yet born: a garbled tale lands on someone grown at the time.
    if (deed != kNone && (deeds_[deed].day - people_[culprit].birthDay) < 12 * kDaysPerYear) return;
    confidence = std::clamp(confidence, 0.0, 100.0);
    auto& list = beliefs_[holder];
    Belief* b = nullptr;
    for (auto& x : list)
        if (x.deed == deed) b = &x;
    if (!b) {
        list.push_back({deed, culprit, confidence, source, day_, false});
        b = &list.back();
        // Word of a secret reaching the family: the head hears the whisper (unless it is said outright, below).
        Id head = playerHead();
        if (source != kNone && inPlayerHouse(people_[holder].household) && playerHousehold_ != kNone &&
            ageYears(people_[holder]) >= 12 && deeds_[deed].actor != holder && !(holder == head && confidence >= 50))
            whisper(holder, deed, culprit, source);
    } else if (b->culprit == culprit) {
        b->confidence = std::min(100.0, std::max(b->confidence, confidence) + 10);  // corroborated
    } else if (confidence > b->confidence) {
        // A more convincing story replaces the old one.
        b->culprit = culprit;
        b->confidence = confidence;
        b->source = source;
        b->day = day_;
        b->actedOn = false;
    } else {
        b->confidence = std::max(0.0, b->confidence - 10);  // conflicting stories breed doubt
    }
    if (b->confidence >= 50 && !b->actedOn && holder != deeds_[deed].actor) actOnBelief(holder, *b);
}

// A belief strong enough to act on: the victim's family now holds a grudge against whoever
// they think did it, right or wrong; anyone else thinks less of the supposed culprit.
void World::actOnBelief(Id holder, Belief& b) {
    b.actedOn = true;
    const Deed& d = deeds_[b.deed];
    const DeedDef* def = defs_.deed(d.type);
    if (!def) return;
    Id victim = d.target;
    bool victimSide = holder == victim || (victim != kNone && people_[victim].household != kNone &&
                                           people_[holder].household == people_[victim].household);
    double scale = d.scale * b.confidence / 100;
    Memory m;
    m.deed = d.id;
    m.about = b.culprit;
    m.label = d.type;
    m.sinceDay = day_;
    m.halfLife = def->halfLife;
    if (victimSide) {
        auto it = def->roles.find(holder == victim ? "target" : "target_family");
        m.value = (it != def->roles.end() ? it->second : -10) * scale;
        if (def->family && people_[holder].dynasty != people_[b.culprit].dynasty)
            m.aboutDynasty = people_[b.culprit].dynasty;
    } else {
        m.value = (isCrime(*def) ? -6 : -3) * scale;
    }
    if (holder != b.culprit) addMemory(holder, m);
    if (victimSide && holder != b.culprit) feudOnDeed(d, *def, b.culprit);

    // The chronicle only tells what the player's family knows.
    if (inPlayerHouse(people_[holder].household) && holder != b.culprit &&
        toldPlayer_.insert({d.id, b.culprit}).second) {
        std::string year = std::to_string(cfg_.startYear + d.day / kDaysPerYear);
        std::string how = b.source == kNone ? " 親眼看見" : " 聽 " + fullName(b.source) + " 說";
        if (victimSide)
            log(1, "【秘密】" + fullName(holder) + how + "，" + year + " 年那次「" + def->name + "」是 " +
                       fullName(b.culprit) + " 幹的。");
        else if (holder == households_[playerHousehold_].head)
            log(1, "【傳聞】" + fullName(holder) + how + "，" + year + " 年 " + fullName(victim) + " 家那次「" +
                       def->name + "」是 " + fullName(b.culprit) + " 幹的。");
    }
    emit({"belief", d.id, holder, b.culprit});
}

void World::monthlyRumours() {
    auto village = livingInVillage();
    if (village.size() < 2) return;
    for (Id id : village) {
        Person& p = people_[id];
        if (ageYears(p) < 12 || p.occupation == "priest") continue;  // the seal of confession
        double talk = 0.05;
        if (hasTrait(p, "gregarious")) talk += 0.05;
        if (hasTrait(p, "drunkard")) talk += 0.05;
        if (hasTrait(p, "cruel")) talk += 0.03;
        if (hasTrait(p, "shy")) talk -= 0.03;
        // Copy: telling someone can add beliefs to this list through corroboration loops.
        std::vector<Belief> mine = beliefs_[id];
        for (const auto& b : mine) {
            if (b.culprit == id || deeds_[b.deed].actor == id) continue;  // nobody gossips about themselves
            if (b.confidence < 25 || day_ < b.quietUntil) continue;
            double years = (day_ - b.day) / double(kDaysPerYear);
            if (!rngKnow_.chance(talk * (years > 10 ? 0.3 : 1.0))) continue;

            // Said at home, or to someone they know (design/news.md); now and then to whoever is at the alehouse.
            Id hearer = kNone;
            const Household& home = households_[p.household];
            double r = rngKnow_.uniform();
            if (r < 0.35 && home.members.size() > 1) {
                hearer = home.members[rngKnow_.range(0, static_cast<int>(home.members.size()) - 1)];
            } else if (r < 0.85) {
                hearer = pickAcquaintance(id, rngKnow_);
            }
            if (hearer == kNone) hearer = village[rngKnow_.range(0, static_cast<int>(village.size()) - 1)];
            if (hearer == id || ageYears(people_[hearer]) < 10) continue;
            if (people_[hearer].household == people_[b.culprit].household) continue;  // not said to their faces

            Id told = b.culprit;
            if (rngKnow_.chance(0.08)) {
                // Garbled in the retelling: someone else from that house, or the teller's own enemy. Only someone who
                // could have done it then: grown (12 or more) on the day of the deed.
                const int deedDay = deeds_[b.deed].day;
                auto couldHave = [&](Id o) { return deedDay - people_[o].birthDay >= 12 * kDaysPerYear && ageYears(people_[o]) >= 14; };
                std::vector<Id> others;
                if (people_[b.culprit].household != kNone)
                    for (Id m : households_[people_[b.culprit].household].members)
                        if (m != b.culprit && couldHave(m)) others.push_back(m);
                if (hasTrait(p, "cruel") || others.empty()) {
                    double worst = 0;
                    for (Id o : village)
                        if (o != id && o != hearer && couldHave(o) && opinion(id, o) < worst) {
                            worst = opinion(id, o);
                            told = o;
                        }
                } else {
                    told = others[rngKnow_.range(0, static_cast<int>(others.size()) - 1)];
                }
            }
            addBelief(hearer, b.deed, told, b.confidence * 0.75, id);
        }
    }
}

// The secret wrong done to this person's household in the last 12 years that they still have no
// firm idea about and have not given up on. Affairs are a matter for the spouse, not an inquiry.
Id World::unsolvedDeed(Id asker) const {
    const Person& a = people_[asker];
    if (a.household == kNone) return kNone;
    for (size_t i = deeds_.size(); i-- > 0;) {
        const Deed& d = deeds_[i];
        if (day_ - d.day > 12 * kDaysPerYear) break;
        const DeedDef* def = defs_.deed(d.type);
        if (!def || def->visibility != "secret" || d.type == "affair" || d.target == kNone || d.actor == asker) continue;
        const Person& v = people_[d.target];
        if (d.target != asker && v.household != a.household && !(v.dynasty == a.dynasty && !v.alive())) continue;
        auto q = inquiries_.find({a.household, d.id});
        if (q != inquiries_.end() && q->second.done) continue;
        const Belief* known = beliefAbout(asker, d.id);
        if (known && known->confidence >= 50) continue;
        return d.id;
    }
    return kNone;
}

// Asking around about a secret done to one's family. It is an inquiry that runs across many
// conversations: the family starts with the few houses that had a reason, and each talk either
// names someone or rules a house out. People lie for their own house, and alibis can be wrong.
Id World::investigate(Id asker, Id informant) {
    const Person& a = people_[asker];
    Id deed = unsolvedDeed(asker);
    bool player = inPlayerHouse(a.household);
    if (deed == kNone) return kNone;
    const Deed& d = deeds_[deed];
    Id victim = d.target;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    Inquiry& q = inquiries_[{a.household, deed}];
    auto houseName = [&](Id hh) {
        Id head = households_[hh].head;
        return head != kNone ? fullName(head) : std::string("某");
    };
    auto listHouses = [&]() {
        std::string s;
        for (Id hh : q.suspects) s += (s.empty() ? "" : "、") + houseName(hh);
        return s + " 家";
    };
    if (!q.started) {
        q.started = true;
        std::vector<std::pair<double, Id>> motive;
        for (const auto& h : households_) {
            if (h.extinct || h.members.empty() || h.id == a.household || h.id == lordHouse) continue;
            if (people_[d.actor].household == h.id) continue;
            double worst = 0;
            for (Id m : h.members)
                if (ageYears(people_[m]) >= 14) worst = std::min(worst, opinion(m, victim) - 20 * feudStage(m, asker));
            if (worst < -5) motive.push_back({worst, h.id});
        }
        std::sort(motive.begin(), motive.end());
        for (size_t i = 0; i < motive.size() && i < 3; ++i) q.suspects.push_back(motive[i].second);
        // The guilty house is usually among those with a reason, but not always: some wrongs come
        // from where nobody thought to look.
        Id guilty = people_[d.actor].household;
        if (guilty != kNone && guilty != a.household && !households_[guilty].extinct &&
            std::find(q.suspects.begin(), q.suspects.end(), guilty) == q.suspects.end() && rngKnow_.chance(0.75))
            q.suspects.push_back(guilty);
        for (size_t i = q.suspects.size(); i > 1; --i)
            std::swap(q.suspects[i - 1], q.suspects[rngKnow_.range(0, static_cast<int>(i) - 1)]);
        if (q.suspects.empty()) {
            q.done = true;
            if (player) log(1, "  " + fullName(asker) + " 想來想去，想不出誰會對 " + people_[victim].firstName + " 下這種手。");
            return kNone;
        }
        if (player) log(1, "  " + fullName(asker) + " 要查清楚 " + describeDeed(deed) + "是誰幹的。心裡懷疑的有 " + listHouses() + "。");
    }
    auto drop = [&](Id hh) { q.suspects.erase(std::remove(q.suspects.begin(), q.suspects.end(), hh), q.suspects.end()); };
    auto conclude = [&]() {
        if (q.suspects.size() > 1) {
            if (player) log(1, "  還剩 " + listHouses() + " 脫不了嫌疑。");
            return kNone;
        }
        q.done = true;
        if (q.suspects.empty()) {
            if (player) log(1, "  每一家都有人作證，線索全斷了。");
            return kNone;
        }
        // One house left: the family settles on whoever there bears the victim the most ill will.
        Id hh = q.suspects[0], culprit = kNone;
        double worst = 1e9;
        for (Id m : households_[hh].members)
            if (ageYears(people_[m]) >= 14 && opinion(m, victim) < worst) {
                worst = opinion(m, victim);
                culprit = m;
            }
        if (culprit == kNone) return kNone;
        if (player) log(1, "  這麼一來，只剩 " + houseName(hh) + " 家了。" + people_[asker].firstName + " 認定是 " + fullName(culprit) + " 幹的。");
        addBelief(asker, deed, culprit, 65, kNone);
        return deed;
    };

    Id infHouse = people_[informant].household;
    const Belief* theirs = beliefAbout(informant, deed);
    if (std::find(q.suspects.begin(), q.suspects.end(), infHouse) != q.suspects.end()) {
        // Asked about their own house, they vouch for it, guilty or not.
        if (player) log(1, "  " + fullName(informant) + " 拍著胸脯說，那陣子 " + houseName(infHouse) + " 家的人一個也沒出過門。");
        drop(infHouse);
        return conclude();
    }
    if (theirs && theirs->confidence >= 25 && theirs->culprit != asker) {
        Id culprit = theirs->culprit;
        double conf = std::min(100.0, theirs->confidence * 0.9 + 10);
        if (player) log(1, "  " + fullName(informant) + " 壓低聲音說，是 " + fullName(culprit) + " 幹的。");
        addBelief(asker, deed, culprit, conf, informant);
        if (conf >= 50) q.done = true;
        return deed;
    }
    if (q.suspects.empty() || rngKnow_.chance(0.3)) {
        // Knows nothing, but will not admit it: names whoever they like least.
        Id guess = kNone;
        double worst = -10;
        for (Id o : livingInVillage())
            if (o != informant && o != asker && people_[o].household != a.household && ageYears(people_[o]) >= 14 &&
                opinion(informant, o) < worst) {
                worst = opinion(informant, o);
                guess = o;
            }
        if (guess == kNone) {
            if (player) log(1, "  " + fullName(informant) + " 搖搖頭，什麼也不知道。");
            return kNone;
        }
        if (player) log(1, "  " + fullName(informant) + " 想了想，說八成是 " + fullName(guess) + " 幹的。");
        addBelief(asker, deed, guess, 55, informant);
        q.done = true;
        return deed;
    }
    // Rules a house out. Usually rightly; sometimes a mistaken alibi clears the guilty.
    Id guilty = people_[d.actor].household;
    std::vector<Id> innocent;
    for (Id hh : q.suspects)
        if (hh != guilty) innocent.push_back(hh);
    Id cleared = !innocent.empty() && (rngKnow_.chance(0.75) || innocent.size() == q.suspects.size())
                     ? innocent[rngKnow_.range(0, static_cast<int>(innocent.size()) - 1)]
                     : guilty;
    static const char* alibis[] = {"那幾天在鎮上趕集", "整夜在守著生病的老人", "那晚在酒館喝到天亮", "那陣子全家在幫領主收割"};
    if (player)
        log(1, "  " + fullName(informant) + " 說 " + houseName(cleared) + " 家的人" + alibis[rngKnow_.range(0, 3)] + "，不會是他們。");
    drop(cleared);
    return conclude();
}

// The manor court. It gets the truth right more often than not, but not always.
bool World::deedTried(Id deedId) const {
    for (size_t i = deedId + 1; i < deeds_.size(); ++i) {
        const Deed& x = deeds_[i];
        if (x.cause == deedId && (x.type == "convicted" || x.type == "wrongly_convicted" ||
                                  x.type == "accused_unproven" || x.type == "false_accusation"))
            return true;
    }
    return false;
}

// Six (or as many as can be found) heads of household sit on the jury, none from the two houses.
// Each votes by the evidence (the system knows the truth: a guilty man leaves more traces), by what
// they believe themselves, and by whom they like; a family feud weighs heavily. If the player's
// head is empanelled, the player decides that vote.
int World::juryVerdict(Id accuser, Id accused, Id deedId, std::vector<Id>& ayes, int& panel) {
    const Deed& d = deeds_[deedId];
    bool guilty = d.actor == accused;
    std::vector<Id> pool;
    Id lordHouse = lord_ != kNone ? people_[lord_].household : kNone;
    // Heads first; grown sons make up the numbers when there are not six.
    std::vector<Id> sons;
    for (const auto& h : households_) {
        if (h.extinct || h.members.empty() || h.head == kNone || h.id == lordHouse) continue;
        if (h.id == people_[accuser].household || h.id == people_[accused].household) continue;
        for (Id m : h.members) {
            const Person& j = people_[m];
            if (j.sex != Sex::Male || ageYears(j) < 21 || ageYears(j) > 70 || j.occupation == "priest") continue;
            (m == h.head ? pool : sons).push_back(m);
        }
    }
    for (size_t i = pool.size(); i > 1; --i) std::swap(pool[i - 1], pool[rngKnow_.range(0, static_cast<int>(i) - 1)]);
    for (size_t i = sons.size(); i > 1; --i) std::swap(sons[i - 1], sons[rngKnow_.range(0, static_cast<int>(i) - 1)]);
    for (size_t i = 0; pool.size() < 6 && i < sons.size(); ++i) pool.push_back(sons[i]);
    if (pool.size() > 6) pool.resize(6);
    panel = static_cast<int>(pool.size());
    if (panel < 3) {  // too few neighbours to sit: the steward decides
        panel = 1;
        return rngKnow_.chance(guilty ? 0.6 : 0.2) ? 1 : 0;
    }
    int votes = 0;
    for (Id j : pool) {
        people_[j].flags.insert("juror");
        bool aye;
        if (j == playerHead()) {
            juryVote_ = -1;
            runEventById("jury_duty", j, accused, deedId);
            aye = juryVote_ == 1 || (juryVote_ < 0 && rngKnow_.chance(guilty ? 0.5 : 0.2));
        } else {
            double pr = guilty ? 0.6 : 0.25;
            if (const Belief* b = beliefAbout(j, deedId))
                pr += b->culprit == accused ? b->confidence / 250 : -0.2;
            pr += std::clamp((opinion(j, accuser) - opinion(j, accused)) / 200, -0.3, 0.3);
            pr += 0.12 * feudStage(j, accused) - 0.12 * feudStage(j, accuser);
            if (people_[accused].flags.count("lord_favour")) pr -= 0.1;
            aye = rngKnow_.chance(std::clamp(pr, 0.02, 0.95));
        }
        if (aye) {
            ++votes;
            ayes.push_back(j);
        }
    }
    return votes;
}

void World::accuse(Id accuser, Id accused, Id deedId) {
    if (deedTried(deedId)) return;  // the court will not hear the same case twice
    const Deed& d = deeds_[deedId];
    const DeedDef* def = defs_.deed(d.type);
    bool guilty = d.actor == accused;
    bool family = isPlayerFamily(people_[accuser]) || isPlayerFamily(people_[accused]);
    int imp = inPlayerHouse(people_[accuser].household) || inPlayerHouse(people_[accused].household) ? 1 : 2;
    std::string what = def ? def->name : d.type;
    Household* ah = people_[accused].household != kNone ? &households_[people_[accused].household] : nullptr;
    Household* uh = people_[accuser].household != kNone ? &households_[people_[accuser].household] : nullptr;

    // The manor court is the lord's court: nobody convicts his own family there.
    bool lordFamily = lord_ != kNone && people_[accused].household == people_[lord_].household;
    if (lordFamily) {
        recordDeed("accused_unproven", accuser, accused, 1, deedId);
        if (lord_ != accused) addOpinion(lord_, accuser, "accused_my_family", -15, 15);
        if (family)
            log(imp, "【法庭】" + fullName(accuser) + " 想在莊園法庭告 " + fullName(accused) + "「" + what +
                         "」，可這是領主的法庭，沒有一個陪審的鄰居敢開口。案子不了了之，領主從此記恨 " +
                         fullName(accuser) + "。");
        return;
    }
    // A suit between two houses is a clash between them (the rival is the house that clashed most: fun-r12 A4); not a
    // quarrel inside one family (a theft at home, a share), which is the family's own business (H1). The names are
    // put in when the clash is read, by what they are to the head then (L11).
    const bool kinCase = def && std::find(def->tags.begin(), def->tags.end(), "kin") != def->tags.end();
    if (!kinCase && !kinTo(accuser, accused))
        noteClash(people_[accuser].household, people_[accused].household, "莊園法庭：{p0} 告 {p1}「" + what + "」", 1.5, accuser, accused);
    std::vector<Id> ayes;
    int panel = 0;
    int votes = juryVerdict(accuser, accused, deedId, ayes, panel);
    bool convicted = votes * 3 >= panel * 2;
    // The family's head sat on the jury: the verdict is the family's to hear, whoever stood in the dock (fun-r12 A1).
    const Id me = playerHead();
    const bool sat = me != kNone && panel >= 3 && people_[me].flags.count("juror");
    std::string myVote;
    if (sat) {
        myVote = std::find(ayes.begin(), ayes.end(), me) != ayes.end() ? "（你說有罪）" : "（你說證據不夠）";
        family = true;
        imp = 1;
    }
    // Who voted which way is known in a village: it is remembered on both sides.
    std::string jury;
    if (panel >= 3) {
        std::set<std::string> houses;
        for (Id j : ayes)
            if (people_[j].dynasty != kNone) houses.insert(dynasties_[people_[j].dynasty].surname);
        std::string names;
        for (const auto& s : houses) names += (names.empty() ? "" : "、") + s;
        jury = "陪審的 " + std::to_string(panel) + " 個鄰居裡，" +
               (votes == 0 ? std::string("沒有一個點頭") : std::to_string(votes) + " 個點了頭（" + names + " 家的人）");
        for (Id j : ayes) addOpinion(accused, j, "voted_against_me", -10, 15);
        for (Id j : livingInVillage())
            if (people_[j].flags.count("juror") && std::find(ayes.begin(), ayes.end(), j) == ayes.end())
                addOpinion(accuser, j, "voted_for_them", -4, 8);
        for (auto& p : people_) p.flags.erase("juror");
    }
    if (convicted) {
        recordDeed(guilty ? "convicted" : "wrongly_convicted", accuser, accused, 1, deedId);
        if (ah) book(*ah, "莊園法庭罰款", -std::min(20.0, std::max(0.0, ah->money))), ah->money = std::max(0.0, ah->money - 20);
        addReputation(accused, -10, "在莊園法庭被定了罪");
        // The verdict is public: the whole village now believes the accused did it.
        for (Id v : livingInVillage())
            if (ageYears(people_[v]) >= 12) addBelief(v, deedId, accused, 90, accuser);
        if (family)
            log(imp, "【法庭】" + fullName(accuser) + " 在莊園法庭告 " + fullName(accused) + "「" + what + "」。" + (jury.empty() ? "" : jury + myVote + "，") +
                         "判" + (people_[accused].sex == Sex::Male ? "他" : "她") + "有罪，罰了 20 便士。");
    } else if (guilty || votes * 3 >= panel) {
        recordDeed("accused_unproven", accuser, accused, 1, deedId);
        if (family)
            log(imp, "【法庭】" + fullName(accuser) + " 在莊園法庭告 " + fullName(accused) + "「" + what + "」。" +
                         (jury.empty() ? "" : jury + myVote + "，") + "不夠定罪，查無實據。");
    } else {
        recordDeed("false_accusation", accuser, accused, 1, deedId);
        if (uh) book(*uh, "誣告罰款", -std::min(6.0, std::max(0.0, uh->money))), uh->money = std::max(0.0, uh->money - 6);
        addReputation(accuser, -5, "在莊園法庭被判誣告");
        for (auto& b : beliefs_[accuser])
            if (b.deed == deedId) b.confidence = std::min(b.confidence, 30.0);
        if (family)
            log(imp, "【法庭】" + fullName(accuser) + " 在莊園法庭告 " + fullName(accused) + "「" + what + "」。" +
                         (jury.empty() ? "" : jury + myVote + "，") + "鄰居們都替" + (people_[accused].sex == Sex::Male ? "他" : "她") +
                         "作證。法庭判 " + fullName(accuser) + " 誣告，罰了 6 便士。");
    }
}

}  // namespace sim
