#pragma once

#include "CoreMinimal.h"

// Plain C++ simulation core: no UObject, no Actor. Game time in minutes since day 0.
class MEDIEVALSIM_API FSimClock
{
public:
	static constexpr int32 MinutesPerDay = 24 * 60;

	void Advance(int32 Minutes) { TotalMinutes += Minutes; }
	int32 Day() const { return TotalMinutes / MinutesPerDay; }
	int32 MinuteOfDay() const { return TotalMinutes % MinutesPerDay; }

private:
	int64 TotalMinutes = 0;
};
