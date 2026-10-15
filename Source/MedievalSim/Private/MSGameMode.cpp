#include "MSGameMode.h"

#include "MSPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	struct FShot
	{
		double Zoom;
		const TCHAR* Name;
	};
	const FShot Shots[] = { { 0.0, TEXT("far_iso") }, { 1.0, TEXT("near_third_person") } };
	// Lumen needs a few seconds to converge after the camera jumps.
	constexpr float SettleSeconds = 5.f;
}

AMSGameMode::AMSGameMode()
{
	DefaultPawnClass = AMSPlayerCharacter::StaticClass();
}

void AMSGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (FParse::Param(FCommandLine::Get(), TEXT("CaptureShots")))
	{
		GetWorldTimerManager().SetTimer(CaptureTimer, this, &AMSGameMode::CaptureStep, SettleSeconds, true, 1.f);
	}
}

// Step 2k sets up shot k, step 2k+1 grabs it; one step per timer tick.
void AMSGameMode::CaptureStep()
{
	const int32 ShotIndex = CaptureIndex / 2;
	if (ShotIndex >= UE_ARRAY_COUNT(Shots))
	{
		FPlatformMisc::RequestExit(false);
		return;
	}

	const FShot& Shot = Shots[ShotIndex];
	if (CaptureIndex % 2 == 0)
	{
		AMSPlayerCharacter* Player = Cast<AMSPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
		checkf(Player, TEXT("CaptureShots needs an AMSPlayerCharacter as player pawn"));
		Player->SetZoomImmediate(Shot.Zoom);
	}
	else
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / FString(Shot.Name) + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogTemp, Display, TEXT("CaptureShots: %s"), *Path);
	}
	++CaptureIndex;
}
