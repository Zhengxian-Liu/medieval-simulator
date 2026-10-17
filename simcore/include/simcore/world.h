// SimCore world state and the coarse (one game day) step.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
#include <set>
#include <string>
#include <vector>

#include "simcore/defs.h"
#include "simcore/rng.h"

namespace sim {

using Id = int32_t;
constexpr Id kNone = -1;

// Calendar: 12 months of 30 days. Day 0 is 1 January of the start year.
constexpr int kDaysPerMonth = 30;
constexpr int kDaysPerYear = 360;

enum class Sex : uint8_t { Male, Female };
enum Skill { Farming, Craft, Learning, Charm, kSkillCount };
enum Need { Hunger, Rest, Warmth, Social, Spirit, kNeedCount };

const char* skillKey(int s);
const char* needKey(int n);
int skillFromKey(const std::string& k);
int needFromKey(const std::string& k);

enum class Harshness { Historical, Gentle };

struct Config {
    uint64_t seed = 1;
    int startYear = 1330;
    Harshness harshness = Harshness::Historical;
    std::string origin = "peasant";  // which starting household the player takes
    // Years the village lives on its own before the family is handed over, so its quarrels, debts and
    // matches are already there (design/contested.md 5). The world then starts that many years before
    // startYear (config().startYear is moved back), and play begins in startYear. 0: start at once, on one
    // of the opening situations (data/events/openings.json) instead.
    int prerun = 0;
};

// What a villager means to do this year, and why (the NPC "backbone"). Chosen from their ambition,
// grudges, friendships and hardship; it raises the odds of the matching plan or action.
struct Intention {
    std::string kind;   // plan | help | none
    std::string plan;   // for kind == plan
    Id target = kNone;
    Id subject = kNone; // the child a match is for
    std::string why;    // in words, for the player who looks at this person
    bool secret = false;  // a scheme others cannot see
    int since = -1;
};

// One thing on a person's mind (data/concerns.json): what it is, who it is about, how strongly.
struct Concern {
    std::string kind;
    Id about = kNone;
    double strength = 0;  // 0–100; fades monthly
    int since = 0;
};

// One step on a person's road through life (design/life-paths.md): the day they came to a station, and why.
struct RoadStep {
    int day = 0;
    std::string station;
    std::string note;  // what happened, in a few words: "考上了"、"受了傷回家"
};

struct Person {
    Id id = kNone;
    std::string firstName;
    Id dynasty = kNone;
    Sex sex = Sex::Male;
    int birthDay = 0;
    int deathDay = -1;
    std::string deathCause;
    Id father = kNone, mother = kNone, spouse = kNone;
    Id household = kNone;
    std::string occupation;
    std::vector<std::string> traits;
    std::array<double, kSkillCount> skills{};
    std::array<double, kNeedCount> needs{};
    double health = 90;
    double reputation = 0;
    std::set<std::string> flags;
    int pregnantDue = -1;
    int lastBirthDay = -100000;
    std::vector<Id> children;
    std::string ambition;
    int ambitionSince = 0;
    int ambitionStep = 0;       // milestones of the ambition reached so far
    std::string lastAmbition;   // the last one completed: its harder sequels are offered first
    uint32_t ambitionDone = 0;  // which of them (bit i: milestone i), in whatever order they came
    Intention intent;
    std::string lastReason;     // why they chose what they last chose, when a model decided for them
    bool free = true;           // false = villein (unfree), bound to the manor
    Id home = kNone;            // while in service: the household the servant belongs to
    int serviceEnd = -1;        // day the service term ends
    double stress = 0;          // CK-style stress: grief, hardship, and acting against one's nature
    std::string stressCause;    // the biggest weight on their mind, for the chronicle
    double stressCauseValue = 0;
    int stressLevel = 0;        // 0–3; crossing a level brings a breaking point
    int lastBreakDay = -100000; // breaking points come at most once a year
    int breaks = 0;
    std::string lastBreakCause;
    Id lover = kNone;           // a standing affair: one secret, and a yearly chance it comes out
    int loverSince = 0;
    Id loverDeed = kNone;       // this person's betrayal of their spouse
    std::vector<Concern> concerns;  // what is on their mind
    std::string want;               // what they want from the head this season (data/wants.json), if anything
    int wantSince = -1;             // season it took hold
    std::string lastWant;           // the last one met, which does not come back at once
    int lastWantMet = -100;         // season it was met
    int soughtDay = -1;             // the last day the head sought them out
    double effort = 1;          // how much of themselves they put into the work, from their mood over recent months
    std::map<Id, float> known;  // 熟識 (design/news.md): how well they know each other villager beyond kin, 0–100
    // 處境 and 人生路 (design/life-paths.md). An empty station is "home": living with the family in the village.
    std::string station;
    int stationSince = 0;
    int stationUntil = -1;      // a term (school, a campaign, years in service in town) is up on this day
    Id awayFrom = kNone;        // living outside the village: the household they belong to and would come back to
    std::vector<RoadStep> road;
    std::string focus;          // 年度重心 (data/focus.json) the family chose; empty = what a family like theirs would do

    bool alive() const { return deathDay < 0; }
};

struct Household {
    Id id = kNone;
    Id dynasty = kNone;
    Id head = kNone;
    std::vector<Id> members;
    double acres = 0;
    double freehold = 0;  // of those acres, held by charter: a small fixed rent, and the lord cannot take them back
    double money = 0;     // pence
    double food = 0;      // person-days of food
    double firewood = 0;  // household-days of winter fuel
    std::string house = "cottage";  // cottage | timber | stone | manor
    std::vector<Id> items;
    bool extinct = false;
    // Debt (story arc): arrears to the lord, or a loan from a neighbour, escalating each year unpaid.
    double debt = 0;
    // A second debt to someone else waits behind the first (e.g. rent arrears while owing the miller).
    double heldDebt = 0;
    Id heldCreditor = kNone;
    Id creditor = kNone;   // person owed; the lord for rent arrears
    int debtYears = 0;     // rent days passed with the debt unpaid
    int noOxUntil = -1;    // the plough ox was sold or seized: poorer harvests until then
    double rentFactor = 1; // the lord raises the rent of households that do well
    double leaseAcres = 0; // the lord's demesne farmed on a lease (包租), with its own rent, until leaseUntil
    int leaseUntil = -1;
    int hiredUntil = -1;   // the grown hands are hired on for the year by another house (bed and board, a wage)
    Id hiredBy = kNone;
    int boonUntil = -1;    // working off a debt on the lord's demesne: fewer days on their own land
    int lodgerUntil = -1;  // a lodger pays for the corner by the hearth
    std::string trade;     // the craft this house lives by, even while no one here can work it
    bool tradeLet = false; // the shop is let to a journeyman until a son grows into it
    bool lean = false;     // last harvest fell short of the household's needs
    double lastYield = 0;  // last harvest, in days of the household's food
    double lastRent = 0, lastRentPaid = 0;
    // The head's time this season (design/season-loop.md): spent on people and plans, or else on the work.
    std::map<std::string, double> ledger;  // the player's money, by what it came from or went on (running totals)
    int timeSeason = -1;   // year * 4 + season the budget belongs to
    int timeLeft = 0;
    double workBonus = 0;  // seasons spent bent over the work: a slightly better harvest
    double focusYield = 0; // this year's 年度重心 and side roads against the defaults (more hands in the field, a pack to the fair)
    Id ward = kNone;       // the heir under 16 while a widow or kinsman holds the land for them (寡婦產 / 代管)
    Id wardLine = kNone;   // the head whose heir the ward is: should the ward be lost, the land goes to the next of that line
};

// A quarrel between two families that grows by stages (Crusader Kings-style story arc).
// It belongs to the families, so it outlives the people who started it.
struct Feud {
    Id a = kNone, b = kNone;  // dynasties, a < b
    double heat = 0;
    int stage = 0;            // 0 none, 1 嫌隙, 2 結怨, 3 世仇
    int since = 0;
    Id cause = kNone;         // the deed that started it
    Id last = kNone;          // the latest deed that fed it
    int truceUntil = -1;      // a priest's peace holds once a generation; a second one means less
};

struct Dynasty {
    Id id = kNone;
    std::string surname;
    std::vector<std::string> traditions;
    std::vector<Id> heads;  // for the player dynasty: every head in order
    std::vector<int> headSince;  // day each of them became head
    std::map<size_t, Id> heldFor;  // by index into heads: the child that head only held the house for (a regent, not a generation)
    double prestige = 0;
    std::string regret;          // the ambition the last head died without reaching; the next one may take it up
    Id townKin = kNone;          // a son sent to town to learn a trade (the town branch of the ladder)
    int townSince = -1;
};

// A named piece of land (design/contested.md). Every acre of the village's fields is in exactly one parcel;
// a household's acres and freehold are the sums of the parcels it holds, kept in step by land.cpp.
struct Parcel {
    Id id = kNone;
    std::string place;       // where it lies: "北田坡上", "東田河灣"
    double acres = 0;
    bool free = false;       // held by charter
    Id holder = kNone;       // household; kNone = in the lord's hands, waiting for a tenant
    int field = 0;           // index into VillageMap::fields
    std::vector<int> cells;  // map cells, y * width + x
    int since = 0;           // day it came to its holder
    std::string how;         // how it came to them: 祖傳, 買來, 抵債收來...
    bool gone = false;       // merged away
};

// The village drawn as blocks (data/village.json "map"): fields cut into parcels, the other ground as zones.
struct MapArea {
    std::string kind, name;  // field | river | meadow | wood | common | village
    int x = 0, y = 0, w = 0, h = 0;
};
struct MapPoi {
    std::string kind, name;  // church | manor | mill | alehouse | green | well | ...
    int x = 0, y = 0;
};
struct VillageMap {
    int width = 0, height = 0;
    std::vector<MapArea> fields, zones;
    std::vector<MapPoi> pois;
    MapArea houses;               // where the cottages stand
    std::map<Id, int> houseCell;  // household -> cell
};

enum class LandPick { CustomaryOnly, CustomaryFirst, FreeFirst };

// The contested ledger (design/contested.md): what only so many can have, what each house is after,
// and who owes whom.
// A village post: the reeve (elected every three years), the ale-taster and the hayward (every Michaelmas).
struct Office {
    std::string id, name;
    Id holder = kNone;
    int until = 0;       // year the term ends
    std::string how;     // "選上的", "領主指派的"
};
// A place someone can be taken into: a craftsman's apprentice (one at a time) or a servant in a bigger house.
struct Place {
    std::string kind;    // apprentice | service
    Id master = kNone;   // household
    std::string trade;   // apprentice: the craft taught
    int slots = 1;
};
// Something a household is after. kind: parcel (object = parcel id), office (office = its id),
// match (object = the person they want for subject), place (object = master household, subject = who goes).
struct Want {
    Id id = kNone;
    Id house = kNone;
    std::string kind;
    Id object = kNone;
    Id subject = kNone;
    std::string office;
    std::string why;     // "兒子多，地不夠分"
    int since = 0;
    Id news = kNone;     // the talk of it going round the village
    bool open = true;
    std::string outcome; // how it ended: "到手了", "讓里夫家搶走了", "放下了"
    int ended = -1;
};
// A good turn not yet repaid: the debtor owes the creditor. Fades over the years; spent when called in.
struct Favour {
    Id debtor = kNone, creditor = kNone;  // people
    double weight = 0;
    int day = 0;
    std::string why;
};
// The entry fine a new holder owes the lord for taking up the land.
struct EntryFine {
    Id house = kNone;
    Id heir = kNone;
    double amount = 0;
    int due = 0;
    bool extended = false;  // the lord already gave more time once
};
// What the head asked someone for, and how they answered (design/contested.md 3).
struct AskOption {
    std::string ask;     // sell_parcel | vote | lend | match | apprentice | speak | stand_aside
    std::string label;   // "請他把北田坡上那 3 畝賣給你（192 便士）"
    Id object = kNone, subject = kNone;
    std::string office;
    bool open = true;
    std::string why;     // when closed
};
struct AskResult {
    int day = -1;
    int season = -1;     // year * 4 + quarter: a no can be pressed only in the season it was given
    Id who = kNone;
    std::string ask, office;
    Id object = kNone, subject = kNone;
    int answer = 0;      // 1 yes, 2 on a condition, 3 no, 4 offended
    std::string text;    // what they said
    std::string demand;  // on a condition: what they want for it
    double demandMoney = 0;
    Id demandParcel = kNone;
    std::string demandOffice;  // on a condition: the office they want the family's vote for
    double dealCost = 0;       // what a yes costs the family outright (a parcel's price, a fee)
    double giftSum = 0;        // what the gift lever would hand over
    std::vector<std::string> levers;  // what the head can still try: gift | favour | hook | meet
};

struct Item {
    Id id = kNone;
    std::string type;
    Id maker = kNone;
    int madeDay = 0;
    Id household = kNone;
    std::vector<Id> owners;  // heads who held it, in order
    bool lost = false;
};

// Something a person did, as it happened. Memories and (later) beliefs point at deeds.
struct Deed {
    Id id = kNone;
    std::string type;
    int day = 0;
    Id actor = kNone;
    Id target = kNone;
    double scale = 1;
    Id cause = kNone;  // the deed this one answered, if any
    Id with = kNone;   // the other party when it took two (an affair's lover)
};

// How one person remembers someone. Opinion is the sum of a person's memories.
struct Memory {
    Id deed = kNone;          // kNone for small everyday impressions
    Id about = kNone;         // the person remembered
    Id aboutDynasty = kNone;  // set when the memory colours the whole family
    std::string label;        // deed type or impression id
    double value = 0;         // opinion contribution when fresh
    int sinceDay = 0;
    double halfLife = 0;      // years; 0 = does not fade
    int expiresDay = -1;
    bool inherited = false;
};

// What a person believes about a deed: who did it, and how sure they are. Beliefs can be wrong.
struct Belief {
    Id deed = kNone;
    Id culprit = kNone;     // who they think did it (may not be the real doer)
    double confidence = 0;  // 0–100
    Id source = kNone;      // who told them; kNone = saw it or did it
    int day = 0;
    bool actedOn = false;   // already turned into a grudge (or an accusation)
    int quietUntil = -1;    // paid (or frightened) to keep quiet until this day
    bool used = false;      // already spent as leverage (blackmail, exposure)
};

// A typed state change other rules can listen to.
struct Signal {
    std::string type;  // deed | belief | death
    Id deed = kNone;
    Id a = kNone;      // deed: doer; belief: holder; death: the dead
    Id b = kNone;      // deed: victim; belief: believed culprit; death: surviving spouse
    bool flag = false; // death: the dead was head of a household
    double value = 0;  // stress: the new level
};

// A signal-driven event waiting for its day.
struct Scheduled {
    int day = 0;
    std::string event;
    Id person = kNone;
    Id target = kNone;
    Id deed = kNone;
    uint64_t seq = 0;
};

// Something that happened, as it goes round the village (design/news.md): it starts with the houses it
// happened to and those close to them, and travels the web of acquaintance by its heat and chance.
struct News {
    Id id = kNone;
    std::string kind;        // a data/news.json kind: death, birth, marriage, deed:<type>…
    Id subject = kNone, other = kNone, deed = kNone;
    Id house = kNone;        // the household it is about
    int day = 0;
    std::string cause;       // for a death
    int age = 0;
    std::string subjectName, otherName, subjectFirst, otherFirst;
    std::string thing;       // what it is about, said as a phrase: "北田坡上那 3 畝" (wants, contests, posts)
    bool again = false;      // the same thing has happened to them before (a mother losing another baby)  // as they were called that day (no "老"/"已故的" for the newly dead)
    std::set<Id> heard;      // households that know
    std::vector<Id> came;    // who turned out for it (a wake, a wedding)
    bool wideNoted = false;
};

// Something the player's family heard, as they heard it.
struct Heard {
    int day = 0;
    Id news = kNone;         // kNone: a whisper about a secret (a belief, not news)
    Id teller = kNone;
    std::string text;
    double weight = 0;       // how much it matters to the family: closeness to them, and how much it is talked of
    bool logged = false;     // already told in the chronicle as it came
};

struct ChronicleEntry {
    int day = 0;
    int importance = 1;  // 1 family, 2 village, 3 history
    std::string text;
};

// A player (or AI-player) instruction. The sim changes only through commands and its own rules,
// so a run is fully reproducible from the start config plus the command log.
struct Command {
    int day = 0;
    std::string type;  // start_plan | cancel_plan | interact (plan = the interaction's event id)
    Id actor = kNone;
    std::string plan;
    Id target = kNone;
    Id subject = kNone;
};

struct ActivePlan {
    Id actor = kNone, target = kNone, subject = kNone;
    std::string plan;
    double progress = 0;
    int startDay = 0;
    std::vector<std::string> beatsSeen;  // each beat happens at most once per plan
};

struct PlanOption {
    std::string plan;
    Id target = kNone;
    Id subject = kNone;
    double weight = 0;      // how much the person wants this plan at all
    double targetWeight = 1;  // preference among targets of the same plan
};

// A memory or modifier id as the player reads it ("refused_proposal" -> "拒婚之辱").
std::string opinionLabel(const std::string& raw);
// How a feeling reads in words: 非常親近 (60+), 有好感, 還算友善, 沒什麼感覺, 不太喜歡, 厭惡, 恨之入骨 (-60 and below).
const char* opinionWord(double op);
// For the reader: drops the spaces the text puts around names ("彼得·阿特伍德 開始") where both sides are
// Chinese, keeping them next to numbers and Latin letters ("第 1 階", "好感 +23").
std::string tidyText(const std::string& text);

// Picks a plan first by its weight, then a target within it, so plans with many possible
// targets are not chosen more often just because of that.
int choosePlanOption(const std::vector<PlanOption>& opts, Rng& rng);

struct ActiveEffect {
    std::string param;
    double value = 0;  // multiplier or absolute, see param
    int untilDay = 0;
};

class World {
public:
    World(const Defs& defs, Config cfg);

    void stepDay();
    void runYears(int years);

    // --- queries
    int today() const { return day_; }
    int year() const { return cfg_.startYear + day_ / kDaysPerYear; }
    int month() const { return (day_ % kDaysPerYear) / kDaysPerMonth; }
    int dayOfMonth() const { return day_ % kDaysPerMonth; }
    bool isWinter() const { int m = month(); return m == 11 || m == 0 || m == 1; }
    double ageYears(const Person& p) const { return (day_ - p.birthDay) / double(kDaysPerYear); }

    const Defs& defs() const { return defs_; }
    const Config& config() const { return cfg_; }
    const std::vector<Person>& people() const { return people_; }
    const std::vector<Household>& households() const { return households_; }
    const std::vector<Dynasty>& dynasties() const { return dynasties_; }
    const std::vector<Item>& items() const { return items_; }
    const std::vector<ChronicleEntry>& chronicle() const { return chronicle_; }
    const Person& person(Id id) const { return people_.at(id); }
    const Household& household(Id id) const { return households_.at(id); }
    Id playerDynasty() const { return playerDynasty_; }
    Id playerHousehold() const { return playerHousehold_; }
    // Whether h is the family's house. Nobody's is before the family is handed over (Config::prerun).
    bool inPlayerHouse(Id h) const { return h != kNone && h == playerHousehold_; }
    // The day the family was handed over to the player (0 without a pre-run); before it, nobody played them.
    int handedOver() const { return handedOver_; }
    Id lordId() const { return lord_; }
    bool gameOver() const { return gameOver_; }

    std::string fullName(Id id) const;
    std::string shortName(Id id) const;  // first name, with "小" when fullName gives one (younger namesake)

    // Human play: when set, the player's head makes their own choices in events. The chooser gets
    // the open options and returns the position of the one picked, or -1 to let the AI decide.
    struct ChoicePrompt {
        std::string title, situation;
        std::vector<std::string> options;
        std::vector<std::string> unavailable;  // shown but not choosable, with why (e.g. "請老婦人來看（錢不夠）")
    };
    using Chooser = std::function<int(const ChoicePrompt&)>;
    void setChooser(Chooser c) { chooser_ = std::move(c); }
    // Puts a decision the code makes (not an event) to the human player, if there is one: the
    // option index they chose, or -1 to let the AI decide.
    int askPlayer(const std::string& title, const std::string& situation, const std::vector<std::string>& options);
    // What the family knows: opinions with their reasons, feuds, debts, believed secrets, inquiries.
    std::string familyReport() const;
    // One villager as the family sees them: what they want, what they mean to do, what they think of you and why.
    std::string personReport(Id id) const;
    // One line on a person: age, how they feel about the family, and what they are about (if known).
    std::string personLine(Id id) const;
    std::string kinWord(Id a, Id b) const;  // "兒子", "姊妹", "親戚"…: what b is to a
    bool aimsKnown(const Person& p) const;

    // The family ladder: 0 villein, 1 free tenant, 2 freeholder, 3 small landholder (-1: the lord's house).
    int standing(Id household) const;
    static const char* standingName(int rung);
    // Branches off the ladder: a churchman in the family, a son in town.
    bool kinInChurch(Id dynasty) const;
    bool kinInTown(Id dynasty) const;

    // Simulation level of detail: how closely each villager is simulated, by how close they are to
    // the player's family in the web of relations. Recomputed every season.
    enum Ring : uint8_t { kFar = 0, kNear = 1, kSpotlight = 2, kHome = 3 };
    int ringOf(Id id) const { return id >= 0 && id < static_cast<Id>(ring_.size()) ? int(ring_[id]) : int(kFar); }
    // People worth looking at, closest first (spotlight, then near).
    std::vector<Id> peopleOfNote() const;
    bool seemsBusy(Id id) const;  // the family can see this person has business with it right now

    // A spotlight villager's choice can be handed to a model: the rules list the options, the decider
    // picks one (or -1 for the rules to decide) and says why. Replays log its answers like the player's.
    struct NpcChoice {
        Id who = kNone;
        std::string title, situation, mind;
        std::string feelings;              // how they feel about each person the options touch
        std::vector<std::string> options;
        std::vector<double> weights;       // what the rules alone would weigh each option at
        int fallback = -1;                 // the do-nothing option, which neither side may rule out
    };
    using NpcDecider = std::function<int(const NpcChoice&, std::string& reason)>;
    void setNpcDecider(NpcDecider d) { npcDecider_ = std::move(d); }
    // Optional: answers many choices at once (the whole village's yearly intentions), in the order given,
    // so a model can be asked in parallel. Answers it leaves out go to the one-at-a-time decider.
    using NpcBatchDecider = std::function<std::vector<std::pair<int, std::string>>(const std::vector<NpcChoice>&)>;
    void setNpcBatchDecider(NpcBatchDecider d) { npcBatchDecider_ = std::move(d); }
    // Who the decider is asked for: kSpotlight (default), kNear, or kFar for every villager.
    void setNpcScope(Ring r) { npcScope_ = r; }
    bool npcInScope(Id id) const {
        int r = ringOf(id);
        return npcDecider_ && r != kHome && r >= npcScope_;
    }
    const std::vector<Deed>& deeds() const { return deeds_; }
    const std::vector<Memory>& memoriesOf(Id id) const { return memories_.at(id); }
    double memoryValue(const Memory& m) const;
    // Every contribution to `from`'s opinion of `to`, largest first: (label, value).
    std::vector<std::pair<std::string, double>> opinionBreakdown(Id from, Id to) const;
    double kinBase(Id from, Id to) const;
    std::string kinBaseLabel(Id from, Id to) const;
    // "有好感 +23（常一起說笑 +15、個性合得來 +8）": the word, the number, and what it adds up from.
    std::string opinionText(Id from, Id to, int maxReasons = 3) const;
    Id recordDeed(const std::string& type, Id actor, Id target, double scale = 1, Id cause = kNone);
    const std::vector<Belief>& beliefsOf(Id id) const { return beliefs_.at(id); }
    const Belief* beliefAbout(Id holder, Id deed) const;
    const std::vector<Feud>& feuds() const { return feuds_; }
    const Feud* feudBetween(Id dynA, Id dynB) const;
    int feudStage(Id personA, Id personB) const;  // between the two people's families
    std::string describeDeed(Id deed) const;      // "1414 年的「拒婚之辱」"
    double opinion(Id from, Id to) const;
    bool hasTrait(const Person& p, const std::string& t) const;
    double traitMod(const Person& p, const std::string& key) const;  // sum over traits + traditions
    double param(const std::string& name, double fallback) const;
    double mood(const Person& p) const;  // 0–100: needs, stress and what is on their mind
    // The mood in words, with its main cause: {"悲痛", "兒子威廉的死還壓在她心上"}.
    struct MoodView {
        double value = 50;
        std::string word, cause;
    };
    MoodView moodView(const Person& p) const;
    void addConcern(Id holder, const std::string& kind, Id about, double strength);
    // Lessens a concern (about = kNone: whoever it is about); returns how much it eased.
    double easeConcern(Id holder, const std::string& kind, double by, Id about = kNone);
    const Concern* concernOf(const Person& p, const std::string& kind, Id about = kNone) const;
    std::string concernText(const Person& p, const Concern& c) const;
    // The head's free time this season, and the people the head can go and see with what.
    static int seasonBudget(int month);
    int timeLeft() const;
    struct Interaction {
        const EventDef* ev = nullptr;
        bool open = false;     // conditions hold and there is time (and money) for it
        std::string why;       // when closed: why not
    };
    std::vector<Interaction> interactionsWith(Id target);
    // 處境 and 人生路 (design/life-paths.md).
    std::string stationName(const Person& p) const;
    bool isAway(const Person& p) const { return p.alive() && p.awayFrom != kNone && p.household == kNone; }
    std::vector<Id> livingAway() const;  // alive and living outside the village, in id order
    std::string lifeLine(Id id) const;   // the road walked, as one line: "拜…為師，出師當鐵匠，娶了…，當了家"
    std::vector<Id> peopleToSee() const;  // family first, then those the head knows
    void runCommands() { processCommands(); }
    void setMoneyForTest(Id house, double money) { households_[house].money = money; }  // tests only
    void startSeason() { rollSeason(); }  // on a season's first day, before it runs: free time and the family's wants  // carry out what the player just asked for, before the day runs
    // The season at a glance, and what changed since a snapshot taken at its start.
    std::string familyGlance() const;
    std::string wantText(const Person& p) const;  // "希望你陪她去威廉的墳前", or "" when they want nothing of you
    // The head did something for them (an interaction id, or "plan:<id>"): meets their want if that was it.
    // Returns whether it did (and wrote what came of it into the season's summary).
    bool answerWant(Id who, const std::string& by);
    struct FamilySnapshot {
        std::map<Id, std::pair<std::string, double>> people;  // mood word, opinion of the head
        std::map<Id, std::map<std::string, double>> why;       // the reasons behind that opinion
        double money = 0, food = 0, acres = 0;
        Id head = kNone;
        std::map<std::string, double> ledger;
        int day = 0;
    };
    FamilySnapshot snapshot() const;
    std::string seasonChanges(const FamilySnapshot& before) const;
    std::string moneySince(const FamilySnapshot& before) const;
    bool isPlayerFamily(const Person& p) const;
    bool closeKin(Id a, Id b) const;
    bool gentry(Id id) const;  // the lord's family: they do not marry tenants
    int swornFriends(Id id) const;  // living people this person has sworn friendship with
    int livingCount() const;
    // The web of acquaintance (design/news.md): how close two people are, 0–100 (kin, then 熟識), and two
    // households, 0–1 (their closest pair of members).
    double closeness(Id a, Id b) const;
    double houseLink(Id ha, Id hb) const;
    const std::vector<News>& news() const { return news_; }
    // land.cpp: the village's parcels and its map
    const std::vector<Parcel>& parcels() const { return parcels_; }
    const VillageMap& villageMap() const { return map_; }
    std::string parcelName(const Parcel& pc) const;  // "北田坡上那 3 畝"
    double vacantAcres() const;
    const std::vector<Heard>& heard() const { return heard_; }
    std::string newsText(const News& n) const;
    std::string villageTalk() const;  // what the family heard last season, for the glance
    std::string tellerName(Id teller, Id news = kNone) const;  // "兒子威廉" for the family's own, the full name for others
    std::set<std::pair<Id, Id>> namesakeTeller_;  // (news, teller) where the teller shares the name of the one it is about
    // contest.cpp: the contested ledger
    const std::vector<Office>& offices() const { return offices_; }
    const Office* office(const std::string& id) const;
    const std::vector<Place>& places() const { return places_; }
    int placeTaken(const Place& pl) const;  // how many of its slots are filled
    bool placeOpen(Id master, const std::string& kind) const;
    bool officeEligible(Id who, const std::string& id) const { return officeBar(who, id).empty(); }
    std::string officeBar(Id who, const std::string& id) const;  // why they cannot stand for it: "" when they can
    const std::vector<Want>& wants() const { return wants_; }
    std::string wantThing(const Want& wt) const;   // "北田坡上那 3 畝", "莊頭的位子", "替威廉娶艾瑪"
    bool familyKnows(const Want& wt) const;        // the family's own, or the talk of it reached them
    double favourOwed(Id debtor, Id creditor) const;
    const std::vector<EntryFine>& entryFines() const { return fines_; }
    std::vector<AskOption> asksFor(Id target);
    const AskResult& lastAsk() const { return lastAsk_; }
    // Whether the last answer can be pressed with this lever now: same season, a free day, the money it takes,
    // the secret still unused, the parcel asked for still the family's. The front ends show only these.
    bool leverOpen(const std::string& lever) const;
    // 年度重心 (data/focus.json): the foci open to someone now, the one they follow, and the family choosing one.
    std::vector<const FocusDef*> focusChoices(Id who) const;
    const FocusDef* focusOf(const Person& p) const;
    const FocusDef* focusDefault(const Person& p) const;  // what a family like theirs would do
    bool steersFocus(Id who) const;  // the family decides where this person's days go: its own, at home or in service
    void setFocus(Id who, const std::string& id);

    // --- player interface
    void issue(const Command& c);
    std::vector<PlanOption> availablePlans(Id actor);
    const ActivePlan* activePlan(Id actor) const;          // the first plan under way, if any
    std::vector<const ActivePlan*> plansOf(Id actor) const;
    bool planRoom(Id actor) const;
    std::string planBlock(Id actor, const PlanDef& pd);  // why it cannot start now ("上回才辦過，還要 2 季…")  // the one keeping the player's house can have two plans going, anyone else one
    const std::vector<Command>& commandLog() const { return commandLog_; }
    Id playerHead() const { return playerHousehold_ == kNone ? kNone : households_[playerHousehold_].head; }
    // Who decides for the family: the head, or while the head is a child, whoever keeps the house
    // for them (their mother if she is home, else the oldest grown member).
    Id actingHead() const;

    // Deterministic digest of the whole state, for replay tests.
    uint64_t digest() const;

    // Instrumentation for the stats tool (not part of the digest): event fires, plan outcomes.
    const std::map<std::string, int>& counters() const { return counters_; }

private:
    friend class EventRunner;

    const Defs& defs_;
    Config cfg_;
    int day_ = 0;
    bool gameOver_ = false;

    std::vector<Person> people_;
    std::vector<Household> households_;
    std::vector<Dynasty> dynasties_;
    std::vector<Item> items_;
    std::vector<Deed> deeds_;
    std::vector<std::vector<Memory>> memories_;  // by person id
    std::vector<std::vector<Belief>> beliefs_;   // by person id
    std::vector<Scheduled> scheduled_;
    std::set<std::pair<Id, Id>> toldPlayer_;
    std::vector<Feud> feuds_;
    std::set<std::pair<Id, std::string>> deedNotes_;  // (deed, key): this deed already led to that event
    std::set<std::pair<Id, Id>> feudCounted_;  // (deed, dynasty) already added to a feud  // (deed, culprit) stories already in the chronicle
    uint64_t scheduleSeq_ = 0;
    std::map<std::pair<Id, std::string>, int> cooldowns_;  // (person, event) -> day allowed again
    std::map<std::pair<Id, std::string>, int> lastLog_;    // (person, option text) -> the variant last told
    std::map<std::pair<Id, std::string>, int> chatterSaid_;  // (person, chatter id) -> day they last talked of it
    std::set<std::string> worldFlags_;
    std::vector<ActiveEffect> effects_;
    std::map<std::string, double> baseParams_;
    std::vector<ChronicleEntry> chronicle_;
    std::map<std::string, int> counters_;
    std::vector<Parcel> parcels_;
    VillageMap map_;
    std::vector<Id> lastMoved_;
    std::vector<Office> offices_;
    std::vector<Place> places_;
    std::vector<Want> wants_;
    std::vector<Favour> favours_;
    std::vector<EntryFine> fines_;
    std::map<std::pair<Id, std::string>, Id> promises_;  // (voter, office) -> the man they promised their vote
    std::map<std::tuple<Id, std::string, Id>, int> asked_;  // (who, ask, object) -> day it may be asked again
    AskResult lastAsk_;
    Rng rngContest_;
    Id playerDynasty_ = kNone;
    Id playerHousehold_ = kNone;
    Id lord_ = kNone;
    Id priest_ = kNone;
    int nextReeveYear_ = 0;
    Chooser chooser_;
    NpcDecider npcDecider_;
    NpcBatchDecider npcBatchDecider_;
    Ring npcScope_ = kSpotlight;
    std::vector<uint8_t> ring_;          // by person id
    std::vector<float> ringScore_;       // how much each villager has to do with the family right now
    std::vector<float> shownScore_;      // the part of it the family can see
    std::string pendingReason_;          // a model's reason for the choice being logged
    std::map<std::pair<Id, std::string>, int> reacted_;  // (person, deed type) -> day the family last saw them take it in
    std::map<Id, int> helpedUntil_;      // household -> day before which no one sends them help again
    std::map<Id, int> noticed_;          // far villagers drawn closer for a while (until day)
    std::map<Id, int> lastStanding_;     // household -> rung last seen
    bool pendingAmbition_ = false;       // the first head picks an ambition on the first day
    int handedOver_ = 0;                 // day the player took the family over (Config::prerun)
    Rng rngMinds_;
    std::string pendingOpening_;  // the opening situation, run on the first day
    int lastForeshadow_ = -100000;  // the family senses a plot against it at most once every few years
    std::map<Id, int> reeveTerms_;  // consecutive terms served
    Id lordOverrode_ = kNone;       // the lord who last overrode the vote
    std::map<Id, int> matchQuietUntil_;  // a match the player turned down or put off: no matchmaker until this day
    mutable std::map<Id, std::string> bynames_;
    std::vector<std::string> seasonNotes_;            // what the head did this season and what came of it
    mutable std::vector<std::string> lastSeasonNotes_;  // the ended season's, until the summary shows them
    Rng rngWants_;
    Rng rngNews_;
    Rng rngLand_;
    std::vector<News> news_;
    std::vector<Heard> heard_;
    std::set<Id> whispered_;  // deeds whose whisper the family has already been told
    int lastWideDay_ = -100000;  // the last time the family heard what the whole village says of it
    std::map<std::tuple<Id, std::string, int>, int> budgetUsed_;  // (household, budget, year) -> events fired  // "（Rose 的丈夫）": fixed the first time it is needed  // the reeve serves three years unless the post falls vacant
    size_t nextHistory_ = 0;

    std::vector<Command> pending_, commandLog_;
    std::vector<ActivePlan> plans_;

    Rng rngLife_, rngEcon_, rngSocial_, rngEvents_, rngNames_, rngPlans_, rngKnow_;

    // plans.cpp
    void processCommands();
    bool startPlan(Id actor, const std::string& plan, Id target, Id subject);
    void monthlyPlans();
    void npcStartPlans();
    void endPlan(size_t index, const std::string& outcome);

    // setup.cpp
    void setupVillage();
    void prerun();
    void handOver(Id house, Id dynasty, int playYear);
    std::string villageName_;
    Id addPerson(const std::string& name, Id dynasty, Sex sex, int birthDay, Id household);
    Id addHousehold(Id dynasty, double acres, double money);
    Id addDynasty(const std::string& surname);
    Id addItem(const std::string& type, Id maker, Id household);
    void rollPersonality(Person& p, int count);
    void rollCongenital(Person& p);

    // life.cpp
    void dailyNeeds();
    void monthlyHealth();
    void monthlyBirths();
    void yearlyMarriages();
    void yearlyAdulthood();
    void giveBirth(Person& mother);
    void kill(Id id, const std::string& cause);
    // What a head left behind (before an heir moved in with their own), for the lord's dues and the 【分家產】 line.
    struct Estate {
        double acres = 0, freehold = 0, money = 0;
        std::string house;
        size_t items = 0;
        bool regent = false;  // the dead only held it for a child
    };
    Estate onHeadDied(Household& h, Id deadHead);
    bool wardHeld(const Household& h) const;  // the ward is alive and still this house's: at home, in service, or away and coming back
    void handOver(Household& h, Id to, const std::string& why);
    void obituary(Id id);
    void followSurvivingBranch(bool lineEnded = false);
    void mergeHousehold(Household& from, Household& to, const std::string& how);  // the land notes: how it came  // members, land, money, food and goods move in; `from` ends
    void inheritRegard(Id dead, Id heir);  // the village's regard for the dead, half of it, passes to the one taking over
    void monthlyWards();                   // an heir held for turns 16: the land is handed over
    void marry(Id a, Id b);
    std::string pickName(Sex sex, Id father, Id mother);
    void growSkills();

    // economy.cpp
    void monthlyEconomy();
    void harvest();
    double taxDue(const Household& h) const;
    double dowryCap(const Household& h) const;  // what a house of its standing is expected to give with a daughter  // the king's fifteenth on what the house has: coin, spare grain, beasts
    void yearlyTax();
    void askToFeed(Household& h);           // kin or a close friend near the end of their bread: the player is asked
    std::map<Id, int> feedAskedUntil_;
    std::map<Id, Id> lastMatchSubject_;     // head -> the one whose match last fell through (for "另說一門親")      // household -> no asking again before this day
    void gatherFirewood();
    void payRent();
    double consumption(const Household& h) const;
    void ensureFood(Household& h);

    // social.cpp
    void monthlySocial();
    void addOpinion(Id from, Id to, const std::string& modId, double value, int years);
    void addMemory(Id holder, const Memory& m);
    void inheritMemories(Id dead);
    void pruneOpinions();
    void electReeve();

    // manor.cpp: serfdom, manorial dues and life-cycle service
    void moveTo(Id person, Id household);
    void chargeMerchet(Id woman, bool outsideManor);
    void chargeHeriot(Household& h, Id dead, double acres, double money);  // on what the dead held
    void recallServants(Id household);
    void yearlyService();
    // Moves a person to another station: leaves the village for an away one, comes back for a home one.
    void setStation(Id who, const std::string& station, int years, const std::string& note);
    void comeHome(Id who);
    void noteLife(Id who, const std::string& note);  // a turn on the road that is not a change of station
    void yearlyAway();
    void monthlyAway();
    void monthlyFocus();
    void rehomeAway(Id from, Id to);
    Id leaving_ = kNone;              // the head walking out right now (setStation): not one of the house's people away
    void settleAsHead(Id who);        // someone takes up a house: a youth's station (labourer, veteran, apprentice) ends  // the house the people away belong to is gone: re-point them
    void sendToService(Id servant, Id master, int years);
    void setFree(Id person);

    // knowledge.cpp: beliefs, rumours, investigation, accusation
    void addBelief(Id holder, Id deed, Id culprit, double confidence, Id source);
    void knowAtDeed(const Deed& d, const DeedDef& def);
    void monthlyRumours();
    void actOnBelief(Id holder, Belief& b);
    Id investigate(Id asker, Id informant);  // returns the deed learned about, or kNone
    Id unsolvedDeed(Id asker) const;         // the secret wrong this person's family most wants explained
    struct Inquiry {
        std::vector<Id> suspects;  // households with a reason to have done it, narrowed as people talk
        bool started = false, done = false;
    };
    std::map<std::pair<Id, Id>, Inquiry> inquiries_;  // (household, deed)
    void accuse(Id accuser, Id accused, Id deed);
    bool deedTried(Id deed) const;
    // The manor court's jury: neighbours who vote by what they believe and whom they like.
    int juryVerdict(Id accuser, Id accused, Id deed, std::vector<Id>& guiltyVotes, int& panel);
    int juryVote_ = -1;  // set by the jury_duty event: 1 guilty, 0 not guilty

    // news.cpp: acquaintance and how news travels
    void befriend(Id a, Id b, double by);
    Id pickAcquaintance(Id who, Rng& rng) const;  // someone they know, the better known the likelier
    void seedAcquaintance();
    void fadeAcquaintance();
    Id addNews(const std::string& kind, Id subject, Id other, Id house, Id deed = kNone, const std::string& thing = "");
    void monthlyNews();
    void learnNews(News& n, Id household, Id teller, double link);
    void whisper(Id holder, Id deed, Id culprit, Id source);
    void attendOccasion(Id person, Id about, bool came, double opinionValue, const std::string& mod);
    // 紅白事: the head answers the season's first invitation once, for the season (all / close / none); the
    // rest that season follow that answer without asking again.
    int occasionSeason_ = -1;
    std::string occasionPolicy_;
    bool occasionByPolicy(const std::string& invite, Id subject, double link);
    std::vector<double> houseLinks(Id hh) const;  // link of every household to this one
    std::string formatNews(const std::string& tmpl, const News& n) const;

    // stress.cpp: grief, hardship and acting against one's nature, and the breaking points
    void addStress(Id id, double amount, const std::string& cause);
    void monthlyStress();
    std::vector<Id> confessSecrets(Id id, Id to);  // the secrets newly told
    Id knownFlame(Id who, Id member) const;      // the lover `who` believes `member` had, if any
    bool hasUntoldSecret(Id id, Id to) const;
    std::string secretWords(Id deed, Id to) const;  // a secret as the one who did it would tell it to `to`

    // hooks.cpp: secrets as leverage, and the truth coming out at the end of a life
    Id hireMaster(Id household) const;          // who would take this house's hands on for a year: the biggest farm, else the demesne
    int hiredHands(const Household& h) const;
    double leasedOut() const;                   // demesne acres let on lease
    int boardedHands(Id master) const;          // hands other houses have hired on with this one
    bool canHire(const Household& m, int n) const;
    std::string rentVacantOnCredit(Household& h, double minAcres);  // "" when done, else why not
    Id boyForMaster(const Person& master) const;  // a boy of 11-16 at the master's old home with no trade yet
    int nextMilestone(const Person& head) const;  // the first step of the ambition not yet reached, -1 if none
    void secretSurfaces();
    bool deedSpoken(Id deed) const;
    std::string sellReason(const Household& h) const;  // why a house would sell a few acres, "" if it would not
    void carryHouseRegard(Id prev, Id next);
    std::string kinTie(Id from, Id to) const;   // how `to` stands to `from`: parent, sibling, step_parent, sibling_in_law, uncle...
    Id lineHead(const Household& h) const;      // the heir a regent holds for when not of the same blood, else the head
    bool houseFree(const Household& h) const;
    double owedLord(const Household& h) const;         // rent arrears and loans the house owes the manor
    double reduceLordDebt(Household& h, double amount);                     // once a season: a secret the head keeps may come up
    Id hookDeed(Id holder, Id culprit) const;  // a secret holder believes culprit did, usable as leverage
    bool hasHook(Id holder) const;
    void deathbedConfession(Id dying);

    // arcs.cpp: story arcs that run for years: family feuds and debts
    void feudHeat(Id dynA, Id dynB, double delta, Id deed);
    void feudOnDeed(const Deed& d, const DeedDef& def, Id culprit);
    void endFeud(Id dynA, Id dynB, const std::string& how);
    void yearlyFeuds();
    Id heirOf(const Household& h, Id dead) const;
    void takeLover(Id a, Id b);
    void endLiaison(Id a);
    void yearlyLiaisons();
    void addDebt(Household& h, double amount, Id creditor);
    void monthlyDebts();
    void yearlyDebts();
    double forfeitLand(Household& h, bool lordIsCreditor);  // acres taken; 0 if the holding is too small

    // land.cpp: parcels change hands only through these, so acres and freehold always match the parcels.
    // from/to are households, kNone for the lord's untenanted land. Returns the acres moved.
    void setupLand();
    double moveLand(Id from, Id to, double acres, LandPick pick, const std::string& how);
    void makeFree(Household& h, double acres);  // turn customary acres into freehold
    void syncLand(Id household);
    Id splitParcel(Id parcel, double acres);    // cuts off a new parcel of `acres`; returns it
    void placeHouse(Id household);
    std::string movedText() const;              // the parcels the last moveLand moved, by name
    // What a sale of `acres` for `price` between two houses would come to: the parcel the buyer is after when the
    // seller holds it (at the same price an acre), else acres off the seller's holding. ok: the buyer can pay.
    struct LandDeal { Id parcel = kNone; double acres = 0, price = 0; bool ok = false; std::string what; };
    LandDeal landDeal(const Household& buyer, const Household& seller, double acres, double price, bool wanted) const;
    void clearDebt(Household& h);
    void promoteHeldDebt(Household& h);
    void runEventById(const std::string& id, Id person, Id target, Id deed = kNone);
    Id findLender(const Household& h) const;
    void reletVacant();

    void moveParcel(Id parcel, Id to, const std::string& how);  // one named parcel, whole
    // contest.cpp
    void setupOffices();
    void chooseOfficers();       // Michaelmas: the ale-taster and the hayward
    // Elections (contest.cpp): who stands and how the heads of household vote. noise(max) is a random draw
    // in [0, max] for a real vote and max / 2 for the forecast the family hears beforehand.
    struct Tally {
        std::map<Id, int> votes;
        std::vector<Id> byWord;  // heads who promised the family's man their vote and kept it
        std::vector<Id> grudge;  // heads an old feud with the family turned against him
        std::vector<Id> broke;   // heads who promised him their vote and gave it to someone else
    };
    double officeMerit(const Person& p, const std::string& id) const;
    std::vector<Id> officeCandidates(const std::string& id, Id old, const std::function<double(double)>& noise) const;
    Tally countVotes(const std::string& id, const std::vector<Id>& cands, Id old, const std::function<double(double)>& noise) const;
    std::string tallyText(const std::vector<Id>& cands, const Tally& t, bool forecast) const;
    std::string voteWhy(const Tally& t, Id winner) const;  // "（艾爾家、柯克家答應過你，說到做到；…家記著舊仇…）"
    Id currentReeve() const;
    bool reeveDue() const;
    void electionNotice();       // the season of a vote, before it: who stands, how it looks, whether the family can
    void setOffice(const std::string& id, Id holder, const std::string& how);
    void officeVacated(Id person);
    bool takePlace(Id master, const std::string& kind, Id who);
    void yearlyWants();          // each head's wants: let go of the stale, take up new ones
    Id addWant(Id house, const std::string& kind, Id object, Id subject, const std::string& office, const std::string& why);
    void settleWants(const std::string& kind, Id object, const std::string& office, Id winnerHouse, const std::string& how);
    void dropWant(Want& wt, const std::string& outcome);
    Want* wantOf(Id house, const std::string& kind, Id object, const std::string& office);
    std::vector<Id> rivalsFor(const std::string& kind, Id object, const std::string& office, Id except) const;
    void allotVacant();          // the steward lets vacant parcels to those after them
    void fillPlaces();           // masters take on whom they like from those asking
    void oweFavour(Id debtor, Id creditor, double weight, const std::string& why);
    // Who lends `amount` to h, richest willing neighbour first; when that is the player's house, the player is asked.
    // owed: what h will owe (more with interest, 0 when the player gives it outright).
    Id seekLender(Household& h, double amount, const std::string& why, double& owed);
    void callInFavour(Id debtor, Id creditor);
    void chargeEntryFine(Household& h, Id heir, Id dead, double customary);
    void monthlyFines();
    void resolveFine(EntryFine& f);
    void forfeitForFine(Household& h, double share);
    void ask(Id who, const std::string& what, Id object, Id subject, const std::string& office, const std::string& lever);
    Id bestParcelWant(Id house) const;  // the parcel this house is most after, kNone if none
    Id openApprenticeship(const std::string& trade, Id except) const;  // a master of that trade with a place free
    // Any place of that kind free outside the household `except`: the lord's first for service, then in village order.
    Id openPlace(const std::string& kind, Id except) const;
    bool villageCraft(Id except) const;  // a craftsman outside that house keeps a shop (taken place or not)
    EntryFine* fineOf(Id house);
    Id fineLender(const Household& h, double amount) const;
    void fineChoice(Household& h, const std::string& how);  // beg | borrow | forfeit (data/events/manor.json)
    // signals.cpp: typed signals and the delayed-event queue
    void emit(const Signal& s);
    void monthlyConcerns();
    void book(Household& h, const std::string& why, double amount) {
        if (h.id == playerHousehold_ && amount != 0) h.ledger[why] += amount;
    }
    void seasonWants();         // the family's wants: kept, let go and resented, or newly formed
    void rollSeason();          // starts the head's season budget, banking unspent days as work
    bool spendTime(int n);
    void runScheduled();

    // ambitions.cpp
    void chooseAmbition(Person& head);
    void yearlyAmbitions();
    void checkMilestones(Person& head, bool quiet);
    void checkStandings();
    void yearlyTown();

    // minds.cpp: rings, yearly intentions, and acting on them
    void computeRings();
    void yearlyIntentions();
    void chooseIntention(Person& p);
    // What a villager might do this year, best first, and the question for the decider if it is asked.
    struct IntentDraft {
        std::vector<Intention> options;
        std::vector<double> scores;
        bool ask = false;
        NpcChoice choice;
    };
    bool draftIntention(Person& p, IntentDraft& d);  // false: nothing to choose (a plan is under way)
    void settleIntention(Person& p, const IntentDraft& d, int pick);  // pick < 0: the rules draw
    bool npcChoiceFor(const Person& p, const std::string& title, const std::string& situation, const std::vector<std::string>& options,
                      const std::vector<double>& weights, const std::vector<Id>& about, int fallback, NpcChoice& c) const;
    int npcAnswer(const NpcChoice& c, int r, const std::string& reason);
    void actOnIntentions();
    void helpFriend(Person& p);
    std::string mindOf(const Person& p) const;
    std::string feelingOf(const Person& p, Id about) const;
    int npcDecide(const Person& p, const std::string& title, const std::string& situation, const std::vector<std::string>& options,
                  const std::vector<double>& weights, const std::vector<Id>& about, int fallback = -1);

    // history / effects
    void applyHistory();
    void applyHistoryEffects(const Json& effects);

    void log(int importance, const std::string& text);
    std::vector<Id> livingInVillage() const;
};

}  // namespace sim
