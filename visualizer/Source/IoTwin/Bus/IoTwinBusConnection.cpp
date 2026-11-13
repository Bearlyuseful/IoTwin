// IoTwin - background TCP connection to iotwin-bus.

#include "Bus/IoTwinBusConnection.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "IoTwin.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

FIoTwinBusConnection::FIoTwinBusConnection(FString InHost, int32 InPort)
	: Host(MoveTemp(InHost))
	, Port(InPort)
{
}

FIoTwinBusConnection::~FIoTwinBusConnection()
{
	Shutdown();
}

bool FIoTwinBusConnection::Start()
{
	if (Thread != nullptr)
	{
		return true;
	}
	bStopRequested = false;
	Thread = FRunnableThread::Create(this, TEXT("IoTwinBusConnection"), 0, TPri_Normal);
	return Thread != nullptr;
}

void FIoTwinBusConnection::Shutdown()
{
	if (Thread != nullptr)
	{
		Stop();
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	Disconnect();
}

void FIoTwinBusConnection::Stop()
{
	bStopRequested = true;
}

bool FIoTwinBusConnection::EnqueueLine(const FString& Line)
{
	if (!bConnected)
	{
		return false;
	}
	Outgoing.Enqueue(Line);
	return true;
}

bool FIoTwinBusConnection::DequeueLine(FString& OutLine)
{
	return Incoming.Dequeue(OutLine);
}

uint32 FIoTwinBusConnection::Run()
{
	while (!bStopRequested)
	{
		if (Socket == nullptr)
		{
			if (!Connect())
			{
				SleepInterruptible(ReconnectDelaySeconds);
			}
			continue;
		}
		if (!SendPending() || !ReceiveAvailable())
		{
			UE_LOG(LogIoTwinBus, Warning, TEXT("Connection to iotwin-bus %s:%d lost, reconnecting"), *Host, Port);
			Disconnect();
			SleepInterruptible(ReconnectDelaySeconds);
		}
	}
	Disconnect();
	return 0;
}

bool FIoTwinBusConnection::Connect()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		return false;
	}

	FAddressInfoResult AddressInfo = SocketSubsystem->GetAddressInfo(
		*Host, nullptr, EAddressInfoFlags::Default, NAME_None, ESocketType::SOCKTYPE_Streaming);
	if (AddressInfo.ReturnCode != SE_NO_ERROR || AddressInfo.Results.Num() == 0)
	{
		if (!bReportedUnreachable)
		{
			UE_LOG(LogIoTwinBus, Warning, TEXT("Cannot resolve bus host '%s'"), *Host);
			bReportedUnreachable = true;
		}
		return false;
	}
	TSharedRef<FInternetAddr> Address = AddressInfo.Results[0].Address;
	Address->SetPort(Port);

	FSocket* NewSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("IoTwinBus"), Address->GetProtocolType());
	if (NewSocket == nullptr)
	{
		return false;
	}
	NewSocket->SetNoDelay(true);

	// Non-blocking connect with timeout so Stop() is never delayed by the OS connect timeout.
	NewSocket->SetNonBlocking(true);
	NewSocket->Connect(*Address);
	bool bEstablished = false;
	const double Deadline = FPlatformTime::Seconds() + ConnectTimeoutSeconds;
	while (!bStopRequested && FPlatformTime::Seconds() < Deadline)
	{
		if (NewSocket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(100)))
		{
			// Writable also signals a failed connect on some platforms: check for a peer.
			TSharedRef<FInternetAddr> Peer = SocketSubsystem->CreateInternetAddr();
			bEstablished = NewSocket->GetPeerAddress(*Peer);
			break;
		}
	}

	if (!bEstablished)
	{
		NewSocket->Close();
		SocketSubsystem->DestroySocket(NewSocket);
		if (!bReportedUnreachable)
		{
			UE_LOG(LogIoTwinBus, Warning, TEXT("iotwin-bus not reachable at %s:%d - retrying every %.0f s"),
				*Host, Port, ReconnectDelaySeconds);
			bReportedUnreachable = true;
		}
		return false;
	}

	NewSocket->SetNonBlocking(false);
	Socket = NewSocket;
	PartialLine.Reset();
	bDiscardingLine = false;
	bReportedUnreachable = false;
	bConnected = true;
	UE_LOG(LogIoTwinBus, Log, TEXT("Connected to iotwin-bus at %s:%d"), *Host, Port);
	return true;
}

void FIoTwinBusConnection::Disconnect()
{
	bConnected = false;
	if (Socket != nullptr)
	{
		Socket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	Outgoing.Empty();
}

bool FIoTwinBusConnection::SendPending()
{
	FString Line;
	while (Outgoing.Dequeue(Line))
	{
		Line.AppendChar(TEXT('\n'));
		const FTCHARToUTF8 Utf8(*Line);
		const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
		int32 Remaining = Utf8.Length();
		while (Remaining > 0)
		{
			int32 Sent = 0;
			if (!Socket->Send(Bytes, Remaining, Sent))
			{
				return false;
			}
			Bytes += Sent;
			Remaining -= Sent;
		}
	}
	return true;
}

bool FIoTwinBusConnection::ReceiveAvailable()
{
	if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(20)))
	{
		return true; // nothing to read
	}

	uint8 Buffer[2048];
	int32 BytesRead = 0;
	if (!Socket->Recv(Buffer, sizeof(Buffer), BytesRead))
	{
		return false; // closed by the peer or socket error
	}

	for (int32 Index = 0; Index < BytesRead; ++Index)
	{
		const ANSICHAR Char = static_cast<ANSICHAR>(Buffer[Index]);
		if (Char == '\n')
		{
			if (!bDiscardingLine)
			{
				if (PartialLine.Num() > 0 && PartialLine.Last() == '\r')
				{
					PartialLine.Pop();
				}
				if (PartialLine.Num() > 0)
				{
					PartialLine.Add('\0');
					Incoming.Enqueue(FString(UTF8_TO_TCHAR(PartialLine.GetData())));
				}
			}
			PartialLine.Reset();
			bDiscardingLine = false;
		}
		else if (!bDiscardingLine)
		{
			if (PartialLine.Num() < MaxLineLength)
			{
				PartialLine.Add(Char);
			}
			else
			{
				PartialLine.Reset();
				bDiscardingLine = true;
			}
		}
	}
	return true;
}

void FIoTwinBusConnection::SleepInterruptible(double Seconds) const
{
	const double Deadline = FPlatformTime::Seconds() + Seconds;
	while (!bStopRequested && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.05f);
	}
}
