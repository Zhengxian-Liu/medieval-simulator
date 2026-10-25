// Content definitions loaded from data/. Everything a designer tunes lives here.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "simcore/json.h"

namespace sim {

struct TraitDef {
    std::string id;
    std::string name;      // display name (zh-Hant)
    std::string category;  // personality | congenital | education
    std::string opposite;
    double inheritChance = 0;  // congenital: chance to pass to a child per parent
    double fromAge = 0;        // congenital but only shown (and in effect) from this age: 多子 is no trait of a newborn
    double weight = 1;         // random roll weight
    std::map<std::string, double> mods;  // fertility, mortality, skill_gain, charm, ...
    std::string against;  // what acting against this trait weighs on you, e.g. "一口嚥不下去的氣"
};

struct OccupationDef {
    std::string id;
    std::string name;
    std::string skill;       // farming | craft | learning | charm
    std::string sex;         // any | male | female
    double monthlyIncome = 0;  // pence at skill 10, before wage multiplier
    bool farms = false;        // works the household land
    std::string unmarriedName; // what one not yet married is called instead (主婦 -> 閨女); "" = the same
};

// 處境 (design/life-paths.md): where a person is in life. The forks between them are events (data/events/paths.json).
struct StationDef {
    std::string id;
    std::string name;
    std::string occupation;  // taken up on entering, if set
    bool away = false;       // lives outside the village: off the map, followed only by yearly "away" events
    bool lasting = false;    // away for good (settled in town, married there): nobody waits for them to come back
};

// 年度重心 (data/focus.json): where someone puts their days; a little each month.
struct FocusDef {
    std::string id, stage, name, desc;  // stage: child (5-13) | adult (14+)
    Json conditions;                    // on the person, as in events
    std::map<std::string, double> skills, needs;  // per month
    double work = 0, income = 0, reputation = 0, stress = 0, health = 0, opinionParents = 0;
    int knownPeers = 0, knownVillage = 0, priestKnown = 0;
};

struct OptionDef {
    std::string text;
    std::string log;  // chronicle line, may be empty
    std::vector<std::string> logs;  // when "log" is an array: variants, one picked at random
    Json effects;     // array
    Json aiWeight;    // { base, traits: { id: delta } }
    Json conditions;  // optional: the option is only open when these hold (for the chooser)
};

struct EventDef {
    std::string id;
    std::string title;
    std::string prompt;  // what the player is told when asked to choose; optional
    std::vector<std::string> prompts;  // other ways of telling it, picked at random (with prompt)
    Json promptIf;  // [{"cond": {...}, "prompt": "..."}]: the first whose conditions hold is told instead
    std::string quietLog;  // said instead when it comes again while still on cooldown (a second child buried)
    bool ask = true;     // false: routine, the AI chooses even for a human player
    bool parentDecides = false;  // the head chooses for their unmarried children living at home
    bool routine = false;        // an everyday moment: while a year is let run (World::setRoutineByRule) the AI answers it
    bool fold = false;           // a card that comes round every year or so (the king's tax, a child's schooling): asked
                                 // twice a generation, the second time with what was chosen the first; after that the
                                 // house answers it as it did last time (fun-r11 1), unless the rule says to ask each time
    std::string pulse;  // monthly | yearly
    double chance = 0;  // per pulse, after conditions pass
    std::string scope;  // anyone | player_family | village_head
    int cooldownYears = 0;
    bool householdCooldown = false;
    std::string budget;  // events sharing a budget fire at most twice a year per household (e.g. "feud")  // the cooldown covers everyone in the house, not just this person
    bool once = false;   // per person
    bool npcLog = false; // also write NPC outcomes to the chronicle (village news)
    Json conditions;
    Json target;  // { pick: random_villager|spouse|father|mother|lord|priest|eldest_child, conditions }
    std::vector<OptionDef> options;
    // Signal-driven events: { signal: deed|belief|death, deed_type, tag, as: target|actor|holder|widow,
    // after_months: [min, max] }. They never fire on the monthly pulse.
    Json on;
    // scope "interaction": something the head can go and do with a person (design/season-loop.md).
    // { who: family|household|outsider|anyone, time: free days it takes, money: pence it costs }.
    // The event is about the person sought out ({name}, who answers); the head is its {target}.
    Json interaction;
    int cooldownMonths = 0;
    // Repeats (design/fun-r5.md C): a story the family met is not met again within three years. "repeat": "person"
    // keys it by the person instead of the house (a second son's own turn at the same age), "free" for a rule that
    // answers a state as often as it comes (an empty larder). "institution": true for what comes round every year by
    // law or custom (the king's tax): never held back, but it must have at least three ways of telling it.
    std::string repeat = "household";
    bool institution = false;
};

// A long-running intention started by a person (Crusader Kings "scheme"): it gains progress
// each month and resolves into success or failure, with "beats" (small events) along the way.
struct PlanDef {
    std::string id;
    std::string name;
    std::string desc;
    std::string needs;  // for the player: what it takes before this plan can be started
    Json actor;    // conditions on whoever starts it
    Json subject;  // optional { pick: any_child, conditions } — the person the plan is for
    Json target;   // optional { pick: villager|rival|friend|lord, conditions }
    int maxMonths = 12;
    int retryYears = 0;  // after it ends, the same person won't start it again for this long
    double monthlyCost = 0;
    Json progress;  // { base, skill: {}, opinion, reputation, traits: {} }
    bool secret = false;
    double discovery = 0;  // monthly chance a secret plan is exposed
    bool npc = true;       // NPCs may start it
    bool notifyTarget = false;  // the target's family hears how it ended
    bool consent = false;       // aimed at the one keeping the player's house: they are asked before it starts
    bool personal = false;      // the actor's own (a courtship, their studies, a secret they hold): it dies with them
    std::string targetAsk;                  // when it targets the player's head: what they are asked (answers: yes = success, no = fail)
    std::vector<std::string> targetAnswers;
    std::string startLog, successLog, failLog, exposedLog, victimLog;
    std::string foreshadow;  // a hint the target's family may notice while a secret plan is under way
    Json success, fail, exposed;  // effect arrays
    Json aiWeight;
    std::vector<EventDef> beats;
};

EventDef parseEvent(const Json& e);

struct HistoryDef {
    int year = 0;
    int month = 0;
    std::string id;
    std::string text;
    Json effects;  // array of { param, value | mult, months }
};

struct AmbitionDef {
    std::string id;
    std::string name;
    std::string desc;
    Json offer;  // conditions on the head to be offered
    Json goal;   // conditions on the head that complete it
    Json aiWeight;
    std::string tradition;  // tradition granted on completion
    int maxYears = 25;
    struct Milestone {
        std::string name;
        Json goal;
        bool gate = false;  // a condition of the road (no debt over 20 for a reeve), not progress along it
        // fun-r8 2: a thing reached once (land, an office, a schooled child; unless "kept": false) stays reached.
        // Money never does by default: a sum a plan still needs shows as needed again once spent. "skip_if": the goal
        // itself has moved past the need for it (the 360 pence spent on land bought), so it counts as reached.
        bool kept = false;
        Json skip;
    };
    std::vector<Milestone> milestones;  // steps on the way, reached in order; the goal is the last step
    std::vector<std::string> plans;     // plans that move it forward: listed first and wanted more
    std::vector<std::string> next;      // the harder goals it opens up once done: offered first afterwards
    std::string final;                  // the last step in words (the goal itself)
    std::string hint;                   // how the player moves it on
    Json keep;                          // conditions it can still be done under; false: 【志向受挫】 (null: always)
    std::string lost;                   // why, when keep fails: said on the 【志向受挫】 line
    std::string office;                 // the post it is about: its conditions are shown with it, each ✓ or ✗
};

struct DeedDef {
    std::string id;
    std::string name;
    std::vector<std::string> tags;
    std::string visibility = "private";  // public | private | secret
    std::map<std::string, double> roles;  // actor | target | target_family | witness
    double halfLife = 10;                 // years; 0 = never fades
    bool inheritable = false;
    bool family = false;
    std::map<std::string, double> stress;  // actor | target | target_family: stress added
};

struct TraditionDef {
    std::string id;
    std::string name;
    std::string desc;
    std::map<std::string, double> mods;
};

struct ItemDef {
    std::string id;
    std::string name;
    double value = 0;
    bool dowry = false;  // can leave the house with a bride
};

// Something on a person's mind that an event left there: grief for the dead, a girl they want to marry,
// a grudge against their father. It colours their mood, fades with time, and opens things to do.
struct ConcernDef {
    std::string id;
    std::string name;   // 喪親之痛
    std::string text;   // how it reads: "{about}的死還壓在{pron}心上"
    std::string word;   // the mood it brings when it weighs most: 悲痛
    double mood = -30;  // effect on mood at full strength (100); positive for joys
    double fade = 5;    // strength lost per month
};

// What a member of the family wants from the head this season (data/wants.json): shown in the season's
// glance, met by seeking them out with one of `answeredBy` (an interaction id, or "plan:<id>" for a plan
// about them), or let go for `patience` seasons and resented.
struct WantDef {
    std::string id;
    std::string text;        // "希望你陪{pron}去{about:grief}的墳前"
    Json when;               // conditions on the person, the head as target
    std::vector<std::string> answeredBy;
    Json fulfil, neglect;    // effects on the person, the head as target
    std::string fulfilNote, neglectNote;  // for the season's summary
    int patience = 2;        // seasons
    double chance = 1;       // per season, when `when` holds
    bool oneAtATime = false; // only one member of the house holds it at a time
};

// What someone talks about when the head sits down with them (data/chatter.json): drawn from their state, so a
// talk with a grieving widow, a boy in love and a greedy neighbour do not read alike.
struct ChatterDef {
    std::string id;
    Json when;                       // conditions on the person, the head as target
    std::vector<std::string> lines;  // variants; {first}, {about:<concern>}, {target_first}…
    double weight = 1;
    std::vector<std::string> notIn;  // interactions it would contradict (no fishing story at the market)
};

// How a kind of news travels the village (data/news.json, design/news.md): how readily it is told (heat), how long
// it stays worth telling, who knows it at once, and what hearing it does.
struct NewsKindDef {
    std::string id;           // death | infant_death | birth | marriage | deed:<type>
    std::string text;         // "{house}家的{subject}因{cause}去世了"
    double heat = 0.5;        // chance a closely linked household passes it on in a month
    int fadeMonths = 3;       // after this it is old news and no longer told
    double instant = 2;       // households linked at least this closely know at once (2: nobody)
    std::string invite;       // the occasion close households are asked to: funeral | wedding
    double inviteLink = 0.35; // how close a household must be to be asked
    int inviteMinAge = 0;     // a child's burial is the family's own; the village is asked for the grown
    double logLink = 0.75;    // the family is told as it comes when it comes from this close (else: the glance)
    bool gathering = false;   // close households turn out for it (the dead's wake, a wedding)
    std::string attendMod;    // what the house it is about remembers of those who came
    bool hasWitness = false;  // overrides the deed's "witness" role for those who hear of it
    double witness = 0;
    bool wide = false;        // tell the player when most of the village has heard this about them
};

struct Defs {
    std::map<std::string, TraitDef> traits;
    std::vector<std::string> traitOrder;  // deterministic iteration
    std::map<std::string, OccupationDef> occupations;
    std::map<std::string, StationDef> stations;
    std::vector<FocusDef> foci;
    std::map<std::string, std::string> focusDefaults;  // child_male | child_female | adult | adult_craft -> focus id
    std::vector<EventDef> events;
    std::vector<HistoryDef> history;
    std::vector<AmbitionDef> ambitions;
    std::vector<PlanDef> plans;
    std::map<std::string, DeedDef> deeds;
    std::map<std::string, TraditionDef> traditions;
    std::map<std::string, ItemDef> items;
    std::map<std::string, ConcernDef> concerns;
    std::vector<WantDef> wants;
    std::vector<ChatterDef> chatter;
    std::map<std::string, NewsKindDef> news;
    std::map<std::string, std::string> whispers;  // secret deed type -> how a whisper of it is told
    std::vector<std::string> maleNames;
    std::vector<std::string> femaleNames;
    std::vector<std::string> surnames;
    Json village;  // starting village layout
    std::map<std::string, std::string> texts;  // "texts" in data/events/*.json: the words of the cards the code builds

    static Defs loadFromDir(const std::string& dataDir);

    const TraitDef* trait(const std::string& id) const;
    const OccupationDef* occupation(const std::string& id) const;
    const StationDef* station(const std::string& id) const;
    const FocusDef* focus(const std::string& id, const std::string& stage) const;
    const AmbitionDef* ambition(const std::string& id) const;
    const ItemDef* item(const std::string& id) const;
    const PlanDef* plan(const std::string& id) const;
    const DeedDef* deed(const std::string& id) const;
    const ConcernDef* concern(const std::string& id) const;
    const WantDef* want(const std::string& id) const;
    const NewsKindDef* newsKind(const std::string& id) const;
};

}  // namespace sim
