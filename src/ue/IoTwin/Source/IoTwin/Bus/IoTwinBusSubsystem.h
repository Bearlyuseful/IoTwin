// IoTwin - game-wide access to the virtual CAN bus.

#pragma once

#include "CoreMinimal.h"
#include "Bus/CanFrame.h"
#include "Bus/IoTwinBusConnection.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IoTwinBusSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCanFrameReceived, const FCanFrame&, Frame);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBusConnectionChanged, bool, bConnected);

/**
 * Connects the game to iotwin-bus and makes the CAN traffic available on the
 * game thread. Configure in DefaultGame.ini ([/Script/IoTwin.IoTwinBusSubsystem])
 * or per run with -IoTwinBusHost=, -IoTwinBusPort=, -IoTwinBusDisabled.
 */
UCLASS(Config = Game)
class IOTWIN_API UIoTwinBusSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Convenience accessor, returns nullptr outside of a game world. */
	static UIoTwinBusSubsystem* Get(const UObject* WorldContextObject);

	/** Sends a frame to all other nodes. Returns false if invalid or not connected. */
	UFUNCTION(BlueprintCallable, Category = "IoTwin|Bus")
	bool SendFrame(const FCanFrame& Frame);

	UFUNCTION(BlueprintPure, Category = "IoTwin|Bus")
	bool IsConnected() const;

	/** "host:port" of the bus. */
	UFUNCTION(BlueprintPure, Category = "IoTwin|Bus")
	FString GetEndpoint() const;

	/** Last frame received with this identifier (current value of a cyclic signal). */
	UFUNCTION(BlueprintCallable, Category = "IoTwin|Bus")
	bool GetLastFrame(int32 Id, bool bExtended, FCanFrame& OutFrame) const;

	/** Most recent traffic, newest last ("RX 100#01", "TX 200#00"). */
	const TArray<FString>& GetRecentTraffic() const { return RecentTraffic; }

	/** Fired on the game thread for every frame received from the bus. */
	UPROPERTY(BlueprintAssignable, Category = "IoTwin|Bus")
	FOnCanFrameReceived OnFrameReceived;

	/** Fired when the connection to the bus is established or lost. */
	UPROPERTY(BlueprintAssignable, Category = "IoTwin|Bus")
	FOnBusConnectionChanged OnConnectionChanged;

	UPROPERTY(Config, EditAnywhere, Category = "IoTwin|Bus")
	FString Host = TEXT("127.0.0.1");

	UPROPERTY(Config, EditAnywhere, Category = "IoTwin|Bus")
	int32 Port = 8080;

	UPROPERTY(Config, EditAnywhere, Category = "IoTwin|Bus")
	bool bEnabled = true;

private:
	bool Tick(float DeltaSeconds);
	void AddTraffic(const TCHAR* Direction, const FString& Line);

	static int64 MakeKey(int32 Id, bool bExtended)
	{
		return (static_cast<int64>(bExtended) << 32) | static_cast<uint32>(Id);
	}

	static constexpr int32 MaxRecentTraffic = 10;
	static constexpr int32 MaxFramesPerTick = 1024;

	TUniquePtr<FIoTwinBusConnection> Connection;
	FTSTicker::FDelegateHandle TickHandle;
	TMap<int64, FCanFrame> LastFrames;
	TArray<FString> RecentTraffic;
	bool bWasConnected = false;
};
