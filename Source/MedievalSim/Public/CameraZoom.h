#pragma once

#include "CoreMinimal.h"

// Camera pose for one zoom level. T=0: near-orthographic isometric overview; T=1: over-the-shoulder.
struct FCameraZoomPose
{
	double ArmLength = 0;      // cm from pivot to camera
	double FovDeg = 0;
	double PitchDeg = 0;       // negative looks down
	double YawDeg = 0;
	double ShoulderOffset = 0; // cm to the right of the pivot
};

struct FCameraZoomTuning
{
	static constexpr double FarFov = 10.0;
	static constexpr double NearFov = 65.0;
	static constexpr double FarFramedWidth = 4000.0; // cm of ground across the screen at the overview
	static constexpr double NearFramedWidth = 350.0;
	static constexpr double FarPitchDeg = -30.0;     // classic 2:1 isometric look
	static constexpr double NearPitchDeg = -12.0;
	static constexpr double IsoYawDeg = 45.0;
	static constexpr double NearShoulderOffset = 50.0;
};

// Distance is derived from FOV so the framed area shrinks smoothly while the FOV opens (no pop).
inline FCameraZoomPose EvaluateCameraZoom(double T)
{
	using Tune = FCameraZoomTuning;
	T = FMath::Clamp(T, 0.0, 1.0);
	const double Ease = T * T * (3.0 - 2.0 * T);

	FCameraZoomPose Pose;
	Pose.FovDeg = FMath::Lerp(Tune::FarFov, Tune::NearFov, Ease);
	// Geometric interpolation: each wheel notch zooms by the same ratio.
	const double Width = Tune::FarFramedWidth * FMath::Pow(Tune::NearFramedWidth / Tune::FarFramedWidth, T);
	Pose.ArmLength = Width / (2.0 * FMath::Tan(FMath::DegreesToRadians(Pose.FovDeg) * 0.5));
	Pose.PitchDeg = FMath::Lerp(Tune::FarPitchDeg, Tune::NearPitchDeg, Ease);
	Pose.YawDeg = Tune::IsoYawDeg;
	Pose.ShoulderOffset = Tune::NearShoulderOffset * Ease * Ease;
	return Pose;
}
