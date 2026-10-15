#include "MSPlayerCharacter.h"

#include "CameraZoom.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"

AMSPlayerCharacter::AMSPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(30.f, 90.f);

	// Placeholder body until a real character mesh exists.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetStaticMesh(Cylinder.Object);
	Body->SetRelativeScale3D(FVector(0.5, 0.5, 1.8));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(GetCapsuleComponent());
	CameraArm->SetRelativeLocation(FVector(0, 0, 60));
	CameraArm->SetUsingAbsoluteRotation(true);
	CameraArm->bUsePawnControlRotation = false;
	CameraArm->bDoCollisionTest = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);

	bUseControllerRotationYaw = false;
	ApplyZoom();
}

void AMSPlayerCharacter::SetZoomImmediate(double T)
{
	Zoom = ZoomTarget = FMath::Clamp(T, 0.0, 1.0);
	ApplyZoom();
}

void AMSPlayerCharacter::ApplyZoom()
{
	const FCameraZoomPose Pose = EvaluateCameraZoom(Zoom);
	CameraArm->TargetArmLength = Pose.ArmLength;
	CameraArm->SetWorldRotation(FRotator(Pose.PitchDeg, Pose.YawDeg, 0));
	CameraArm->SocketOffset = FVector(0, Pose.ShoulderOffset, 0);
	Camera->SetFieldOfView(Pose.FovDeg);
}

void AMSPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Zoom != ZoomTarget)
	{
		Zoom = FMath::FInterpTo(Zoom, ZoomTarget, DeltaSeconds, ZoomSpeed);
		ApplyZoom();
	}
}

void AMSPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AMSPlayerCharacter::ZoomIn);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AMSPlayerCharacter::ZoomOut);
	PlayerInputComponent->BindAxisKey(EKeys::W, this, &AMSPlayerCharacter::MoveForward);
	PlayerInputComponent->BindAxisKey(EKeys::D, this, &AMSPlayerCharacter::MoveRight);
	PlayerInputComponent->BindAxisKey(EKeys::S, this, &AMSPlayerCharacter::MoveBack);
	PlayerInputComponent->BindAxisKey(EKeys::A, this, &AMSPlayerCharacter::MoveLeft);
}

void AMSPlayerCharacter::MoveForward(float Value)
{
	AddMovementInput(FRotator(0, FCameraZoomTuning::IsoYawDeg, 0).Vector(), Value);
}

void AMSPlayerCharacter::MoveRight(float Value)
{
	AddMovementInput(FRotator(0, FCameraZoomTuning::IsoYawDeg + 90.0, 0).Vector(), Value);
}
