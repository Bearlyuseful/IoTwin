// IoTwin - CAN frame model shared with iotwin-bus (see docs/dev/protocol.md).

#include "Bus/CanFrame.h"

namespace
{
	int32 HexValue(TCHAR C)
	{
		if (C >= TEXT('0') && C <= TEXT('9')) { return C - TEXT('0'); }
		if (C >= TEXT('a') && C <= TEXT('f')) { return C - TEXT('a') + 10; }
		if (C >= TEXT('A') && C <= TEXT('F')) { return C - TEXT('A') + 10; }
		return -1;
	}
}

bool FCanFrame::IsValid() const
{
	const uint32 Mask = bExtended ? ExtendedIdMask : StandardIdMask;
	return Id >= 0 && (static_cast<uint32>(Id) & ~Mask) == 0 && Data.Num() <= MaxDataLength;
}

FString FCanFrame::ToString() const
{
	if (!IsValid())
	{
		return FString();
	}
	FString Text = bExtended
		? FString::Printf(TEXT("%08X#"), static_cast<uint32>(Id))
		: FString::Printf(TEXT("%03X#"), static_cast<uint32>(Id));
	if (bRemote)
	{
		Text += TEXT("R");
		if (Data.Num() > 0)
		{
			Text.AppendInt(Data.Num());
		}
		return Text;
	}
	for (const uint8 Byte : Data)
	{
		Text += FString::Printf(TEXT("%02X"), Byte);
	}
	return Text;
}

bool FCanFrame::GetBit(int32 ByteIndex, int32 BitIndex) const
{
	if (bRemote || !Data.IsValidIndex(ByteIndex) || BitIndex < 0 || BitIndex > 7)
	{
		return false;
	}
	return (Data[ByteIndex] >> BitIndex) & 1;
}

bool FCanFrame::Parse(const FString& InText, FCanFrame& OutFrame)
{
	const FString Text = InText.TrimStartAndEnd();
	const int32 Length = Text.Len();

	int32 Separator = INDEX_NONE;
	if (!Text.FindChar(TEXT('#'), Separator) || (Separator != 3 && Separator != 8))
	{
		return false;
	}

	FCanFrame Frame;
	Frame.bExtended = Separator == 8;
	uint32 ParsedId = 0;
	for (int32 Index = 0; Index < Separator; ++Index)
	{
		const int32 Value = HexValue(Text[Index]);
		if (Value < 0)
		{
			return false;
		}
		ParsedId = (ParsedId << 4) | static_cast<uint32>(Value);
	}
	if (ParsedId > (Frame.bExtended ? ExtendedIdMask : StandardIdMask))
	{
		return false;
	}
	Frame.Id = static_cast<int32>(ParsedId);

	int32 Pos = Separator + 1;
	if (Pos < Length && Text[Pos] == TEXT('#'))
	{
		return false; // CAN FD is not supported
	}

	if (Pos < Length && (Text[Pos] == TEXT('R') || Text[Pos] == TEXT('r')))
	{
		Frame.bRemote = true;
		++Pos;
		if (Pos < Length)
		{
			if (Length - Pos != 1 || Text[Pos] < TEXT('0') || Text[Pos] > TEXT('8'))
			{
				return false;
			}
			Frame.Data.SetNumZeroed(Text[Pos] - TEXT('0'));
		}
		OutFrame = MoveTemp(Frame);
		return true;
	}

	while (Pos < Length)
	{
		if (Text[Pos] == TEXT('.'))
		{
			if (Frame.Data.Num() == 0 || Pos + 1 >= Length || Text[Pos + 1] == TEXT('.'))
			{
				return false;
			}
			++Pos;
			continue;
		}
		if (Pos + 1 >= Length || Frame.Data.Num() >= MaxDataLength)
		{
			return false;
		}
		const int32 High = HexValue(Text[Pos]);
		const int32 Low = HexValue(Text[Pos + 1]);
		if (High < 0 || Low < 0)
		{
			return false;
		}
		Frame.Data.Add(static_cast<uint8>((High << 4) | Low));
		Pos += 2;
	}
	OutFrame = MoveTemp(Frame);
	return true;
}

bool FCanFrame::ParseId(const FString& HexText, bool bExtended, int32& OutId)
{
	FString Text = HexText.TrimStartAndEnd();
	if (Text.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
	{
		Text.RightChopInline(2);
	}
	if (Text.IsEmpty() || Text.Len() > 8)
	{
		return false;
	}
	uint32 Value = 0;
	for (const TCHAR C : Text)
	{
		const int32 Digit = HexValue(C);
		if (Digit < 0)
		{
			return false;
		}
		Value = (Value << 4) | static_cast<uint32>(Digit);
	}
	if (Value > (bExtended ? ExtendedIdMask : StandardIdMask))
	{
		return false;
	}
	OutId = static_cast<int32>(Value);
	return true;
}

FCanFrame FCanFrame::MakeData(int32 Id, TArray<uint8> Data, bool bExtended)
{
	FCanFrame Frame;
	Frame.Id = Id;
	Frame.bExtended = bExtended;
	Frame.Data = MoveTemp(Data);
	return Frame;
}

FString UCanFrameLibrary::CanFrameToString(const FCanFrame& Frame)
{
	return Frame.ToString();
}

bool UCanFrameLibrary::ParseCanFrame(const FString& Text, FCanFrame& Frame)
{
	return FCanFrame::Parse(Text, Frame);
}

bool UCanFrameLibrary::GetCanFrameBit(const FCanFrame& Frame, int32 ByteIndex, int32 BitIndex)
{
	return Frame.GetBit(ByteIndex, BitIndex);
}
