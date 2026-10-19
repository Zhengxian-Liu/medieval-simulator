// Data-driven events: conditions, targets, options and effects from data/events/*.json.
#pragma once

#include <string>

#include "simcore/world.h"

namespace sim {

class EventRunner {
public:
    explicit EventRunner(World& w, ActivePlan* plan = nullptr) : w_(w), plan_(plan) {}

    // Runs every event whose pulse matches ("monthly" or "yearly").
    void pulse(const std::string& kind);

    // Condition tree evaluated on person p; `target` is the event's other party, if any.
    bool eval(const Json& cond, const Person& p, Id target) const;
    void apply(const Json& effects, Person& p, Id target);
    std::string format(const std::string& tmpl, const Person& p, Id target) const;
    std::string optionText(const OptionDef& o, const Person& p, Id target) const;
    double odds(const Json& o, const Person& p, Id target) const;
    // A condition as gates, one per key, each decided by the same check as eval (src/gates.cpp); `all` keeps the
    // quiet ones (not the lord, not the priest) even when met. describe: the met-or-not labels as one phrase.
    std::vector<OfficeGate> explain(const Json& cond, const Person& p, Id target, bool all = false) const;
    std::string describe(const Json& cond, const Person& p, Id target) const;
    OfficeGate gate(const std::string& key, const Json& v, const Person& p, Id target, bool* quiet = nullptr) const;

    // Picks the option an AI (or the auto-played head) would choose.
    int chooseOption(const EventDef& ev, const Person& p, Id target = kNone);
    double weight(const Json& ai, const Person& p, Id target = kNone, const Person* traitsOf = nullptr) const;  // traitsOf: whose temper weighs (a parent choosing)
    // The head goes to see `p` (an interaction event): p answers, the head's time is spent.
    void interact(const EventDef& ev, Person& p, Id head);
    void fireScheduled(const EventDef& ev, Person& p, Id target, Id deed);
    bool refire(const EventDef& ev, Person& p, Id target);  // a card held back at the opening, if it still fits
    // Picks an option, logs it at `importance` (0 = silent) and applies it.
    void runEvent(const EventDef& ev, Person& p, Id target, int importance);

private:
    World& w_;
    ActivePlan* plan_;  // set while running a plan's beats and outcomes
    Id pickTarget(const EventDef& ev, const Person& p);
    Id cause_ = kNone;  // deed being answered, for causality
    Id deed_ = kNone;   // the deed a signal-driven event is about
    bool pulsing_ = false;  // fire() called from pulse(): only such a card may wait out the opening (World::deferredCards_)
    double writtenOff_ = 0;  // what target_lord_debt last wrote off the manor book without payment
    bool eligible(const EventDef& ev, const Person& p, Id playerHead) const;
    bool playersToAnswer(const EventDef& ev, const Person& p) const;  // the player (not the AI) answers it
    // Where a story the family met is remembered so as not to come again soon (World::metStory_); first = kNone
    // when it is not held back (signals, institutions, rules that fire whenever their state holds, villagers).
    std::pair<Id, std::string> storyKey(const EventDef& ev, const Person& p) const;
    void fire(const EventDef& ev, Person& p, Id target);
    std::string choseLine(const Person& p, Id target, const std::string& option);
    const std::string& pickLog(const OptionDef& o, Id who);  // a variant this person was not told last time
    bool asked_ = false;   // the last chooseOption was the player's to make (the 「選擇」 is theirs)
    bool picked_ = false;  // ...and the player (not the AI standing in) picked it: a standing rule is set only then
    std::string bookAs_;   // what money the effects move is booked under: the event's title, or the plan's name
    void natureStress(const EventDef& ev, const OptionDef& chosen, Person& p, Id target);
    Id serviceChild(const Household& h) const;
    Id serviceMaster(const Household& h) const;
    std::pair<Id, Id> peaceMatch(const Person& p, Id target) const;
    bool remembers(const Json& v, const Person& p, Id target) const;
    bool evalKey(const std::string& key, const Json& v, const Person& p, Id target) const;
    // An option is open when its conditions hold and the household can pay what it costs outright.
    bool optionOpen(const OptionDef& o, const Person& p, Id target) const;
};

}  // namespace sim
