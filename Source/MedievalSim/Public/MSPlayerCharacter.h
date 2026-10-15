#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MSPlayerCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

// Player body plus the iso-to-shoulder zoom camera. Mouse wheel zooms, WASD moves relative to the iso yaw.
UCLASS()
class MEDIEVALSIM_API AMSPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMSPlayerCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// 0 = isometric overview, 1 = over the shoulder. Jumps without easing.
	void SetZoomImmediate(double T);

private:
	void ApplyZoom();
	void ZoomIn() { ZoomTarget = FMath::Clamp(ZoomTarget + ZoomStep, 0.0, 1.0); }
	void ZoomOut() { ZoomTarget = FMath::Clamp(ZoomTarget - ZoomStep, 0.0, 1.0); }
	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveBack(float Value) { MoveForward(-Value); }
	void MoveLeft(float Value) { MoveRight(-Value); }

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> CameraArm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;

	static constexpr double ZoomStep = 0.08;
	static constexpr double ZoomSpeed = 6.0;
	double Zoom = 0.0;
	double ZoomTarget = 0.0;
};
