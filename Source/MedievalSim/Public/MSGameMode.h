#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MSGameMode.generated.h"

// With -CaptureShots on the command line, takes the far-iso and close-third-person screenshots
// into Saved/Screenshots/ and quits. This is the visual E2E check for the camera and lighting.
UCLASS()
class MEDIEVALSIM_API AMSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMSGameMode();

protected:
	virtual void BeginPlay() override;

private:
	void CaptureStep();

	int32 CaptureIndex = 0;
	FTimerHandle CaptureTimer;
};
