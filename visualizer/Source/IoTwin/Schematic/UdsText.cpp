// IoTwin - short text for CAN frames carrying UDS over ISO-TP.

#include "Schematic/UdsText.h"

namespace
{
	const TCHAR* RequestName(uint8 Sid)
	{
		switch (Sid)
		{
		case 0x10: return TEXT("DiagnosticSessionControl");
		case 0x11: return TEXT("ECUReset");
		case 0x14: return TEXT("ClearDiagnosticInformation");
		case 0x19: return TEXT("ReadDTCInformation");
		case 0x22: return TEXT("ReadDataByIdentifier");
		case 0x27: return TEXT("SecurityAccess");
		case 0x2E: return TEXT("WriteDataByIdentifier");
		case 0x31: return TEXT("RoutineControl");
		case 0x3E: return TEXT("TesterPresent");
		case 0x85: return TEXT("ControlDTCSetting");
		default: return nullptr;
		}
	}

	FString SessionName(uint8 Session)
	{
		switch (Session & 0x7F)
		{
		case 0x01: return TEXT("default");
		case 0x02: return TEXT("programming");
		case 0x03: return TEXT("extended");
		default: return FString::Printf(TEXT("%02X"), Session);
		}
	}

	/** Service text for the UDS message starting at Payload[0] (Length bytes available in this frame). */
	FString ServiceText(const uint8* Payload, int32 Length)
	{
		if (Length < 1)
		{
			return FString();
		}
		const uint8 Sid = Payload[0];
		if (Sid == 0x7F)
		{
			if (Length < 3)
			{
				return TEXT("NRC");
			}
			return FString::Printf(TEXT("NRC %s: %s"), *FUdsText::ServiceName(Payload[1]), *FUdsText::NrcName(Payload[2]));
		}

		const bool bPositive = RequestName(Sid) == nullptr && (Sid & 0x40) != 0 && RequestName(static_cast<uint8>(Sid - 0x40)) != nullptr;
		const uint8 Request = bPositive ? static_cast<uint8>(Sid - 0x40) : Sid;
		FString Text = (bPositive ? TEXT("+") : TEXT("")) + FUdsText::ServiceName(Sid);
		switch (Request)
		{
		case 0x10:
			if (Length >= 2)
			{
				Text += TEXT(" ") + SessionName(Payload[1]);
			}
			break;
		case 0x11:
			if (Length >= 2)
			{
				Text += (Payload[1] & 0x7F) == 0x01 ? TEXT(" hard") : (Payload[1] & 0x7F) == 0x03 ? TEXT(" soft") : TEXT("");
			}
			break;
		case 0x19:
			if (Length >= 2)
			{
				Text += FString::Printf(TEXT(" %02X"), Payload[1]);
			}
			break;
		case 0x22:
		case 0x2E:
			if (Length >= 3)
			{
				Text += FString::Printf(TEXT(" %02X%02X"), Payload[1], Payload[2]);
			}
			break;
		default:
			break;
		}
		return Text;
	}
}

FString FUdsText::ServiceName(uint8 Sid)
{
	if (const TCHAR* Name = RequestName(Sid))
	{
		return Name;
	}
	if (Sid == 0x7F)
	{
		return TEXT("NegativeResponse");
	}
	if ((Sid & 0x40) != 0)
	{
		if (const TCHAR* Name = RequestName(static_cast<uint8>(Sid - 0x40)))
		{
			return Name;
		}
	}
	return FString::Printf(TEXT("service %02X"), Sid);
}

FString FUdsText::NrcName(uint8 Nrc)
{
	switch (Nrc)
	{
	case 0x10: return TEXT("generalReject");
	case 0x11: return TEXT("serviceNotSupported");
	case 0x12: return TEXT("subFunctionNotSupported");
	case 0x13: return TEXT("incorrectMessageLengthOrInvalidFormat");
	case 0x14: return TEXT("responseTooLong");
	case 0x21: return TEXT("busyRepeatRequest");
	case 0x22: return TEXT("conditionsNotCorrect");
	case 0x31: return TEXT("requestOutOfRange");
	case 0x33: return TEXT("securityAccessDenied");
	case 0x78: return TEXT("responsePending");
	case 0x7E: return TEXT("subFunctionNotSupportedInActiveSession");
	case 0x7F: return TEXT("serviceNotSupportedInActiveSession");
	default: return FString::Printf(TEXT("NRC %02X"), Nrc);
	}
}

FString FUdsText::Describe(const TArray<uint8>& Data)
{
	if (Data.Num() == 0)
	{
		return FString();
	}
	const uint8 Pci = Data[0] >> 4;
	switch (Pci)
	{
	case 0: // single frame
	{
		const int32 Length = Data[0] & 0x0F;
		if (Length == 0 || Length + 1 > Data.Num())
		{
			return TEXT("SF (invalid length)");
		}
		return TEXT("SF ") + ServiceText(&Data[1], Length);
	}
	case 1: // first frame
	{
		if (Data.Num() < 3)
		{
			return TEXT("FF (too short)");
		}
		const int32 Total = ((Data[0] & 0x0F) << 8) | Data[1];
		return FString::Printf(TEXT("FF %d bytes "), Total) + ServiceText(&Data[2], Data.Num() - 2);
	}
	case 2:
		return FString::Printf(TEXT("CF %d"), Data[0] & 0x0F);
	case 3:
	{
		const int32 Status = Data[0] & 0x0F;
		return Status == 0 ? TEXT("FC continue") : Status == 1 ? TEXT("FC wait") : TEXT("FC overflow");
	}
	default:
		return FString::Printf(TEXT("PCI %X"), Pci);
	}
}
