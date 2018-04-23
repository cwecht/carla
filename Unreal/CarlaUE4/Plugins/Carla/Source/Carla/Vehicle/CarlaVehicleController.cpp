// Copyright (c) 2017 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla.h"
#include "CarlaVehicleController.h"

#include "Sensor/Lidar.h"
#include "Sensor/SceneCaptureCamera.h"

#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "WheeledVehicle.h"
#include "WheeledVehicleMovementComponent.h"

// =============================================================================
// -- Constructor and destructor -----------------------------------------------
// =============================================================================

ACarlaVehicleController::ACarlaVehicleController(const FObjectInitializer& ObjectInitializer) :
  Super(ObjectInitializer)
{
  PrimaryActorTick.bCanEverTick = true;
}

ACarlaVehicleController::~ACarlaVehicleController() {}

// =============================================================================
// -- APlayerController --------------------------------------------------------
// =============================================================================

void ACarlaVehicleController::Possess(APawn *aPawn)
{
  Super::Possess(aPawn);

  if (IsPossessingAVehicle()) {
    // Bind hit events.
    aPawn->OnActorHit.AddDynamic(this, &ACarlaVehicleController::OnCollisionEvent);
    // Get custom player state.
    CarlaPlayerState = Cast<ACarlaPlayerState>(PlayerState);
    check(CarlaPlayerState != nullptr);
    // We can set the bounding box already as it's not going to change.
    CarlaPlayerState->BoundingBoxTransform = GetPossessedVehicle()->GetVehicleBoundingBoxTransform();
    CarlaPlayerState->BoundingBoxExtent = GetPossessedVehicle()->GetVehicleBoundingBoxExtent();
    // Set HUD input.
    CarlaHUD = Cast<ACarlaHUD>(GetHUD());
    if (CarlaHUD != nullptr) {
      InputComponent->BindAction("ToggleHUD", IE_Pressed, CarlaHUD, &ACarlaHUD::ToggleHUDView);
    } else {
      UE_LOG(LogCarla, Warning, TEXT("Current HUD is not a ACarlaHUD"));
    }
  }
}

// =============================================================================
// -- AActor -------------------------------------------------------------------
// =============================================================================

#include <iostream>

void ACarlaVehicleController::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);

  if (IsPossessingAVehicle()) {
    auto Vehicle = GetPossessedVehicle();
    CarlaPlayerState->UpdateTimeStamp(DeltaTime);
    CarlaPlayerState->ForwardSpeed = Vehicle->GetVehicleForwardSpeed();
    // Get velocity by numeric differenciation
    const FVector PreviousSpeed = CarlaPlayerState->Velocity;
    CarlaPlayerState->Velocity = Vehicle->GetVehicleVelocity();
    const FVector CurrentSpeed = CarlaPlayerState->Velocity;
    // TODO: This probably needs to be in local coordinates
    CarlaPlayerState->Acceleration = (CurrentSpeed - PreviousSpeed) / DeltaTime;
    CarlaPlayerState->Transform = Vehicle->GetVehicleTransform();
    FTransform CurrentOrientation = CarlaPlayerState->Transform;
    CarlaPlayerState->AngularRate = Vehicle->GetVehicleAngularVelocity();

    FVector av = Vehicle->GetVehicleAngularVelocity();
    std::cout << av.X << " " << av.Y << " " << av.Z << std::endl;

    const auto &AutopilotControl = GetAutopilotControl();
    CarlaPlayerState->Steer = AutopilotControl.Steer;
    CarlaPlayerState->Throttle = AutopilotControl.Throttle;
    CarlaPlayerState->Brake = AutopilotControl.Brake;
    CarlaPlayerState->bHandBrake = AutopilotControl.bHandBrake;
    CarlaPlayerState->CurrentGear = Vehicle->GetVehicleCurrentGear();
    CarlaPlayerState->SpeedLimit = GetSpeedLimit();
    CarlaPlayerState->TrafficLightState = GetTrafficLightState();
    IntersectPlayerWithRoadMap();
  }
}

// =============================================================================
// -- Events -------------------------------------------------------------------
// =============================================================================

void ACarlaVehicleController::OnCollisionEvent(
    AActor* Actor,
    AActor* OtherActor,
    FVector NormalImpulse,
    const FHitResult& Hit)
{
  // Register collision only if we are moving faster than 1 km/h.
  check(IsPossessingAVehicle());
  if (FMath::Abs(GetPossessedVehicle()->GetVehicleForwardSpeed() * 0.036f) > 1.0f) {
    CarlaPlayerState->RegisterCollision(Actor, OtherActor, NormalImpulse, Hit);
  }
}

// =============================================================================
// -- Other --------------------------------------------------------------------
// =============================================================================

void ACarlaVehicleController::IntersectPlayerWithRoadMap()
{
  auto RoadMap = GetRoadMap();
  if (RoadMap == nullptr) {
    UE_LOG(LogCarla, Error, TEXT("Controller doesn't have a road map!"));
    return;
  }

  check(IsPossessingAVehicle());
  auto Vehicle = GetPossessedVehicle();
  constexpr float ChecksPerCentimeter = 0.1f;
  const auto *BoundingBox = Vehicle->GetVehicleBoundingBox();
  check(BoundingBox != nullptr);
  auto Result = RoadMap->Intersect(
      BoundingBox->GetComponentTransform(),
      BoundingBox->GetUnscaledBoxExtent(),
      ChecksPerCentimeter);

  CarlaPlayerState->OffRoadIntersectionFactor = Result.OffRoad;
  CarlaPlayerState->OtherLaneIntersectionFactor = Result.OppositeLane;
}
