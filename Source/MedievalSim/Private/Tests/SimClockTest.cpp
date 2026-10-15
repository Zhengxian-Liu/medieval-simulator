#include "SimClock.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimClockRollsOverDay, "MedievalSim.Sim.Clock.RollsOverDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimClockRollsOverDay::RunTest(const FString& Parameters)
{
	FSimClock Clock;
	Clock.Advance(FSimClock::MinutesPerDay + 30);
	TestEqual(TEXT("Day"), Clock.Day(), 1);
	TestEqual(TEXT("MinuteOfDay"), Clock.MinuteOfDay(), 30);
	return true;
}

#endif
