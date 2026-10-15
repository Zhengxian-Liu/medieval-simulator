#include "CameraZoom.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	double FramedWidth(const FCameraZoomPose& Pose)
	{
		return 2.0 * Pose.ArmLength * FMath::Tan(FMath::DegreesToRadians(Pose.FovDeg) * 0.5);
	}
}

// Failure modes:
// 1. Out-of-range input produces a pose outside the far..near envelope.
// 2. Far end loses the near-orthographic isometric look (wide FOV / wrong pitch / wrong framing).
// 3. Near end is not a third-person view (narrow FOV / camera far from the character).
// 4. Zooming in moves the camera away or widens the framed area at some step (visible pop).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraZoomClampsInput, "MedievalSim.Sim.CameraZoom.ClampsInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraZoomClampsInput::RunTest(const FString&)
{
	const FCameraZoomPose Below = EvaluateCameraZoom(-3.0);
	const FCameraZoomPose Far = EvaluateCameraZoom(0.0);
	const FCameraZoomPose Above = EvaluateCameraZoom(7.0);
	const FCameraZoomPose Near = EvaluateCameraZoom(1.0);
	TestEqual(TEXT("below 0 == far"), Below.ArmLength, Far.ArmLength);
	TestEqual(TEXT("above 1 == near"), Above.ArmLength, Near.ArmLength);
	TestEqual(TEXT("above 1 fov == near"), Above.FovDeg, Near.FovDeg);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraZoomFarIsIsometric, "MedievalSim.Sim.CameraZoom.FarIsIsometric",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraZoomFarIsIsometric::RunTest(const FString&)
{
	const FCameraZoomPose Far = EvaluateCameraZoom(0.0);
	TestTrue(TEXT("near-orthographic FOV"), Far.FovDeg <= 12.0);
	TestEqual(TEXT("classic iso pitch"), Far.PitchDeg, FCameraZoomTuning::FarPitchDeg);
	TestEqual(TEXT("fixed iso yaw"), Far.YawDeg, FCameraZoomTuning::IsoYawDeg);
	TestEqual(TEXT("framed width"), FramedWidth(Far), FCameraZoomTuning::FarFramedWidth, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraZoomNearIsThirdPerson, "MedievalSim.Sim.CameraZoom.NearIsThirdPerson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraZoomNearIsThirdPerson::RunTest(const FString&)
{
	const FCameraZoomPose Near = EvaluateCameraZoom(1.0);
	TestTrue(TEXT("perspective FOV"), Near.FovDeg >= 60.0);
	TestTrue(TEXT("camera close to character"), Near.ArmLength <= 400.0);
	TestTrue(TEXT("over the shoulder"), Near.ShoulderOffset > 0.0);
	TestTrue(TEXT("shallow pitch"), Near.PitchDeg > FCameraZoomTuning::FarPitchDeg);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraZoomIsMonotonic, "MedievalSim.Sim.CameraZoom.IsMonotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraZoomIsMonotonic::RunTest(const FString&)
{
	FCameraZoomPose Prev = EvaluateCameraZoom(0.0);
	for (int32 Step = 1; Step <= 200; ++Step)
	{
		const FCameraZoomPose Cur = EvaluateCameraZoom(Step / 200.0);
		if (!TestTrue(FString::Printf(TEXT("arm shrinks at step %d"), Step), Cur.ArmLength <= Prev.ArmLength)
			|| !TestTrue(FString::Printf(TEXT("framing shrinks at step %d"), Step), FramedWidth(Cur) <= FramedWidth(Prev)))
		{
			return false;
		}
		Prev = Cur;
	}
	return true;
}

#endif
