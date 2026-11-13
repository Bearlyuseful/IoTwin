// IoTwin - game mode of the visualizer.

#include "Core/GM_Simulation.h"

#include "Bus/CanFrame.h"
#include "Bus/CanSignalComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/IoTwinHUD.h"
#include "Core/IoTwinPlayerController.h"
#include "Components/PointLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "IoTwin.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Systems/ElectricalCableActor.h"
#include "Systems/LampActor.h"
#include "Systems/PowerSourceActor.h"
#include "Systems/WallSwitchActor.h"
#include "UObject/ConstructorHelpers.h"

const FName AGM_Simulation::DemoTag(TEXT("IoTwinDemo"));

AGM_Simulation::AGM_Simulation()
{
	PlayerControllerClass = AIoTwinPlayerController::StaticClass();
	HUDClass = AIoTwinHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object;
	PlaneMesh = Plane.Object;
	ColorMaterial = Material.Object;
}

void AGM_Simulation::StartPlay()
{
	if (bSpawnDemoScene)
	{
		SpawnDemoScene();
	}
	Super::StartPlay();

	if (DemoCamera)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PlayerController = It->Get())
			{
				PlayerController->SetViewTarget(DemoCamera);
			}
		}
	}

	// Smoke-test hooks: -IoTwinScreenshot=<s> saves Saved/Screenshots/IoTwinDemo.png,
	// -IoTwinQuitAfter=<s> closes the game.
	float ScreenshotDelay = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("IoTwinScreenshot="), ScreenshotDelay) && ScreenshotDelay > 0.0f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([]()
		{
			const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("IoTwinDemo.png"));
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			UE_LOG(LogIoTwin, Log, TEXT("Screenshot requested: %s"), *File);
		}), ScreenshotDelay, false);
	}
	float QuitDelay = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("IoTwinQuitAfter="), QuitDelay) && QuitDelay > 0.0f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([]()
		{
			UE_LOG(LogIoTwin, Log, TEXT("Quitting (-IoTwinQuitAfter)"));
			FPlatformMisc::RequestExit(false);
		}), QuitDelay, false);
	}
}

void AGM_Simulation::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	if (DemoCamera && NewPlayer)
	{
		NewPlayer->SetViewTarget(DemoCamera);
	}
}

AActor* AGM_Simulation::SpawnBlock(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color)
{
	AStaticMeshActor* Block = GetWorld()->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
	if (Block == nullptr)
	{
		return nullptr;
	}
	UStaticMeshComponent* MeshComponent = Block->GetStaticMeshComponent();
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetStaticMesh(Mesh);
	Block->SetActorScale3D(Scale);
	Block->Tags.Add(DemoTag);
	if (ColorMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ColorMaterial, Block);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		MeshComponent->SetMaterial(0, Material);
	}
	return Block;
}

void AGM_Simulation::AddLabel(AActor* LabelOwner, const FString& Text, const FVector& WorldLocation, float Size)
{
	if (LabelOwner == nullptr || LabelOwner->GetRootComponent() == nullptr)
	{
		return;
	}
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(LabelOwner);
	Label->SetupAttachment(LabelOwner->GetRootComponent());
	Label->RegisterComponent();
	Label->SetText(FText::FromString(Text));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(Size);
	Label->SetTextRenderColor(FColor(235, 235, 235));
	// Readable from the camera, which looks along +X.
	Label->SetWorldLocationAndRotation(WorldLocation, FRotator(0.0f, 180.0f, 0.0f));
	Label->SetWorldScale3D(FVector::OneVector);

	// Dark sign behind the text keeps it readable whether the wall is lit or not.
	if (CubeMesh)
	{
		UStaticMeshComponent* Sign = NewObject<UStaticMeshComponent>(LabelOwner);
		Sign->SetupAttachment(LabelOwner->GetRootComponent());
		Sign->SetMobility(EComponentMobility::Movable);
		Sign->SetStaticMesh(CubeMesh);
		Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sign->SetCastShadow(false);
		Sign->RegisterComponent();
		const float Width = Text.Len() * Size * 0.6f + 14.0f;
		const float Height = Size * 1.8f;
		Sign->SetWorldLocationAndRotation(WorldLocation + FVector(1.5f, 0.0f, 0.0f), FRotator::ZeroRotator);
		Sign->SetWorldScale3D(FVector(0.01f, Width / 100.0f, Height / 100.0f));
		if (ColorMaterial)
		{
			UMaterialInstanceDynamic* SignMaterial = UMaterialInstanceDynamic::Create(ColorMaterial, Sign);
			SignMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.01f, 0.012f, 0.015f));
			Sign->SetMaterial(0, SignMaterial);
		}
	}
}

void AGM_Simulation::SpawnDemoScene()
{
	UWorld* World = GetWorld();
	if (World == nullptr || CubeMesh == nullptr || PlaneMesh == nullptr)
	{
		UE_LOG(LogIoTwin, Warning, TEXT("Demo scene not created: engine basic shapes missing"));
		return;
	}

	int32 RelayId = 0x100;
	int32 SwitchId = 0x200;
	if (!FCanFrame::ParseId(RelayCanId, false, RelayId) || !FCanFrame::ParseId(SwitchCanId, false, SwitchId))
	{
		UE_LOG(LogIoTwin, Warning, TEXT("Invalid demo CAN ids '%s' / '%s', using 100 / 200"), *RelayCanId, *SwitchCanId);
		RelayId = 0x100;
		SwitchId = 0x200;
	}

	const FVector O = DemoOrigin;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Room: floor and back wall (the wall faces -X, towards the camera).
	SpawnBlock(PlaneMesh, O, FVector(12.0f, 9.0f, 1.0f), FLinearColor(0.05f, 0.05f, 0.055f));
	SpawnBlock(CubeMesh, O + FVector(310.0f, 0.0f, 150.0f), FVector(0.2f, 9.0f, 3.0f), FLinearColor(0.32f, 0.32f, 0.34f));

	// Power source with CAN controlled relay, mounted on the wall.
	const FTransform SourceTransform(FRotator::ZeroRotator, O + FVector(280.0f, -260.0f, 150.0f));
	APowerSourceActor* Source = World->SpawnActorDeferred<APowerSourceActor>(
		APowerSourceActor::StaticClass(), SourceTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Source)
	{
		Source->bControlledByCan = true;
		Source->RelayControl->Configure(ECanSignalDirection::Receive, RelayId);
		Source->Tags.Add(DemoTag);
		Source->FinishSpawning(SourceTransform);
		AddLabel(Source, FString::Printf(TEXT("Relay  (CAN 0x%03X)"), RelayId), O + FVector(255.0f, -260.0f, 200.0f));
	}

	// Lamp hanging in front of the wall.
	ALampActor* Lamp = World->SpawnActor<ALampActor>(ALampActor::StaticClass(), O + FVector(120.0f, 0.0f, 250.0f), FRotator::ZeroRotator, Params);
	if (Lamp)
	{
		Lamp->Tags.Add(DemoTag);
		AddLabel(Lamp, TEXT("Lamp"), O + FVector(120.0f, 0.0f, 285.0f));
	}

	// Cable from the relay output to the lamp.
	if (Source && Lamp)
	{
		AElectricalCableActor* Cable = World->SpawnActor<AElectricalCableActor>(
			AElectricalCableActor::StaticClass(), Source->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (Cable)
		{
			Cable->Tags.Add(DemoTag);
			Cable->ConnectBetween(Source->Port, Lamp->Port);
		}
	}

	// Wall switch, reports its position to the light ECU.
	const FTransform SwitchTransform(FRotator::ZeroRotator, O + FVector(296.0f, 220.0f, 120.0f));
	AWallSwitchActor* Switch = World->SpawnActorDeferred<AWallSwitchActor>(
		AWallSwitchActor::StaticClass(), SwitchTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Switch)
	{
		Switch->Signal->Configure(ECanSignalDirection::Transmit, SwitchId);
		Switch->Tags.Add(DemoTag);
		Switch->FinishSpawning(SwitchTransform);
		AddLabel(Switch, FString::Printf(TEXT("Switch  (CAN 0x%03X)"), SwitchId), O + FVector(280.0f, 220.0f, 160.0f));
	}

	// Some ambient light if the map has none.
	bool bHasSun = false;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		bHasSun = true;
		break;
	}
	if (!bHasSun)
	{
		if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(O, FRotator(-50.0f, 30.0f, 0.0f), Params))
		{
			Sun->GetLightComponent()->SetIntensity(2.0f);
		}
	}

	// Weak fill light so the room stays visible while the lamp is off.
	if (APointLight* Fill = World->SpawnActor<APointLight>(O + FVector(-250.0f, 0.0f, 320.0f), FRotator::ZeroRotator, Params))
	{
		Fill->Tags.Add(DemoTag);
		Fill->PointLightComponent->SetIntensity(1200.0f);
		Fill->PointLightComponent->SetAttenuationRadius(2000.0f);
		Fill->PointLightComponent->SetCastShadows(false);
	}

	// Fixed camera looking at the wall.
	const FVector CameraLocation = O + FVector(-480.0f, 0.0f, 230.0f);
	const FVector LookAt = O + FVector(250.0f, 0.0f, 140.0f);
	DemoCamera = World->SpawnActor<ACameraActor>(CameraLocation, (LookAt - CameraLocation).Rotation(), Params);
	if (DemoCamera)
	{
		DemoCamera->GetCameraComponent()->SetFieldOfView(80.0f);
		DemoCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	}

	UE_LOG(LogIoTwin, Log, TEXT("Light demo created: relay on CAN 0x%03X, switch on CAN 0x%03X"), RelayId, SwitchId);
}
