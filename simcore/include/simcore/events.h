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

    // Picks the option an AI (or the auto-played head) would choose.
    int chooseOption(const EventDef& ev, const Person& p, Id target = kNone);
    double weight(const Json& ai, const Person& p, Id target = kNone, const Person* traitsOf = nullptr) const;  // traitsOf: whose temper weighs (a parent choosing)
    // The head goes to see `p` (an interaction event): p answers, the head's time is spent.
    void interact(const EventDef& ev, Person& p, Id head);
    void fireScheduled(const EventDef& ev, Person& p, Id target, Id deed);
    // Picks an option, logs it at `importance` (0 = silent) and applies it.
    void runEvent(const EventDef& ev, Person& p, Id target, int importance);

private:
    World& w_;
    ActivePlan* plan_;  // set while running a plan's beats and outcomes
    Id pickTarget(const EventDef& ev, const Person& p);
    Id cause_ = kNone;  // deed being answered, for causality
    Id deed_ = kNone;   // the deed a signal-driven event is about
    bool eligible(const EventDef& ev, const Person& p, Id playerHead) const;
    void fire(const EventDef& ev, Person& p, Id target);
    const std::string& pickLog(const OptionDef& o, Id who);  // a variant this person was not told last time
    bool asked_ = false;   // the last chooseOption was the player's to make (the 「選擇」 is theirs)
    std::string bookAs_;   // what money the effects move is booked under: the event's title, or the plan's name
    void natureStress(const EventDef& ev, const OptionDef& chosen, Person& p, Id target);
    Id serviceChild(const Household& h) const;
    std::pair<Id, Id> peaceMatch(const Person& p, Id target) const;
    bool remembers(const Json& v, const Person& p, Id target) const;
    bool evalKey(const std::string& key, const Json& v, const Person& p, Id target) const;
    // An option is open when its conditions hold and the household can pay what it costs outright.
    bool optionOpen(const OptionDef& o, const Person& p, Id target) const;
};

}  // namespace sim
