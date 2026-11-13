// IoTwin - background TCP connection to iotwin-bus.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"

#include <atomic>

class FRunnableThread;
class FSocket;

/**
 * Worker thread that keeps a TCP connection to iotwin-bus alive and
 * exchanges newline-terminated text lines with it.
 *
 * - Reconnects automatically while the bus is not reachable.
 * - Incoming lines are queued for the game thread (DequeueLine).
 * - Outgoing lines are queued by the game thread (EnqueueLine) and dropped
 *   while disconnected - like frames sent on an unconnected CAN node.
 */
class FIoTwinBusConnection final : public FRunnable
{
public:
	FIoTwinBusConnection(FString InHost, int32 InPort);
	virtual ~FIoTwinBusConnection() override;

	/** Starts the worker thread. */
	bool Start();

	/** Stops the worker thread and waits for it. */
	void Shutdown();

	bool IsConnected() const { return bConnected.load(); }

	/** Queues a line (without newline) for sending. Returns false while disconnected. */
	bool EnqueueLine(const FString& Line);

	/** Takes the next received line. Game thread only. */
	bool DequeueLine(FString& OutLine);

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	bool Connect();
	void Disconnect();
	bool SendPending();
	bool ReceiveAvailable();
	void SleepInterruptible(double Seconds) const;

	static constexpr double ReconnectDelaySeconds = 1.0;
	static constexpr double ConnectTimeoutSeconds = 2.0;
	static constexpr int32 MaxLineLength = 256;

	const FString Host;
	const int32 Port;

	FRunnableThread* Thread = nullptr;
	FSocket* Socket = nullptr; // worker thread only

	std::atomic<bool> bStopRequested{false};
	std::atomic<bool> bConnected{false};

	TQueue<FString, EQueueMode::Mpsc> Outgoing;
	TQueue<FString, EQueueMode::Spsc> Incoming;

	TArray<ANSICHAR> PartialLine; // worker thread only
	bool bDiscardingLine = false;
	bool bReportedUnreachable = false;
};
