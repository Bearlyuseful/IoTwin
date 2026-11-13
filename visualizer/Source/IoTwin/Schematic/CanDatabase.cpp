// IoTwin - DBC reader for the visualizer (messages, signals, value tables).

#include "Schematic/CanDatabase.h"

#include "Misc/FileHelper.h"

namespace
{
	constexpr uint32 ExtendedFlag = 0x80000000u;

	/** Splits off the next whitespace separated token. */
	bool NextToken(const FString& Line, int32& Pos, FString& OutToken)
	{
		while (Pos < Line.Len() && FChar::IsWhitespace(Line[Pos]))
		{
			++Pos;
		}
		const int32 Start = Pos;
		while (Pos < Line.Len() && !FChar::IsWhitespace(Line[Pos]))
		{
			++Pos;
		}
		OutToken = Line.Mid(Start, Pos - Start);
		return !OutToken.IsEmpty();
	}

	/** Reads a "quoted string" starting at Pos (after optional whitespace). */
	bool QuotedString(const FString& Line, int32& Pos, FString& OutText)
	{
		while (Pos < Line.Len() && FChar::IsWhitespace(Line[Pos]))
		{
			++Pos;
		}
		if (Pos >= Line.Len() || Line[Pos] != TEXT('"'))
		{
			return false;
		}
		const int32 Start = ++Pos;
		while (Pos < Line.Len() && Line[Pos] != TEXT('"'))
		{
			++Pos;
		}
		OutText = Line.Mid(Start, Pos - Start);
		if (Pos < Line.Len())
		{
			++Pos;
		}
		return true;
	}

	uint32 MotorolaNext(uint32 Position)
	{
		return (Position % 8u == 0u) ? Position + 15u : Position - 1u;
	}

	bool GetBit(const uint8 Data[8], uint32 Position)
	{
		return Position < 64u && ((Data[Position / 8u] >> (Position % 8u)) & 1u);
	}

	/** " SG_ Name [M|mN] : start|len@order sign (factor,offset) [min|max] "unit" receivers" */
	bool ParseSignal(const FString& Line, FCanSignalDef& Out)
	{
		int32 Pos = Line.Find(TEXT("SG_")) + 3;
		if (!NextToken(Line, Pos, Out.Name))
		{
			return false;
		}
		const int32 Colon = Line.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
		if (Colon == INDEX_NONE)
		{
			return false;
		}
		Pos = Colon + 1;
		FString Layout;
		if (!NextToken(Line, Pos, Layout))
		{
			return false;
		}
		FString StartText, Rest, LengthText, Flags;
		if (!Layout.Split(TEXT("|"), &StartText, &Rest) || !Rest.Split(TEXT("@"), &LengthText, &Flags) || Flags.Len() < 2)
		{
			return false;
		}
		Out.StartBit = FCString::Atoi(*StartText);
		Out.Length = FCString::Atoi(*LengthText);
		Out.bIntel = Flags[0] == TEXT('1');
		Out.bSigned = Flags[1] == TEXT('-');
		if (Out.Length < 1 || Out.Length > 64 || Out.StartBit < 0 || Out.StartBit > 63)
		{
			return false;
		}

		const int32 Open = Line.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
		const int32 Close = Line.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
		if (Open == INDEX_NONE || Close == INDEX_NONE)
		{
			return false;
		}
		FString FactorText, OffsetText;
		if (!Line.Mid(Open + 1, Close - Open - 1).Split(TEXT(","), &FactorText, &OffsetText))
		{
			return false;
		}
		Out.Factor = FCString::Atod(*FactorText);
		Out.Offset = FCString::Atod(*OffsetText);

		int32 UnitPos = Line.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Close);
		if (UnitPos != INDEX_NONE)
		{
			++UnitPos;
			QuotedString(Line, UnitPos, Out.Unit);
		}
		return true;
	}
}

bool FCanDatabase::LoadFromFile(const FString& Path, FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}
	return LoadFromString(Text, OutError);
}

bool FCanDatabase::LoadFromString(const FString& Text, FString& OutError)
{
	Messages.Reset();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*CullEmpty=*/false);

	bool bInString = false; // inside a multi-line quoted string of a skipped statement
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString Line = Lines[Index].TrimStart();
		if (bInString)
		{
			for (const TCHAR C : Line)
			{
				bInString = (C == TEXT('"')) ? !bInString : bInString;
			}
			continue;
		}

		if (Line.StartsWith(TEXT("BO_ ")))
		{
			int32 Pos = 3;
			FString IdText, NameText, DlcText, Transmitter;
			NextToken(Line, Pos, IdText);
			NextToken(Line, Pos, NameText);
			NextToken(Line, Pos, DlcText);
			NextToken(Line, Pos, Transmitter);
			NameText.RemoveFromEnd(TEXT(":"));
			const uint64 RawId = FCString::Strtoui64(*IdText, nullptr, 10);
			FCanMessageDef& Message = Messages.AddDefaulted_GetRef();
			Message.bExtended = (RawId & ExtendedFlag) != 0;
			Message.Id = static_cast<uint32>(RawId & FCanFrame::ExtendedIdMask);
			Message.Name = NameText;
			Message.Transmitter = Transmitter;
		}
		else if (Line.StartsWith(TEXT("SG_ ")))
		{
			FCanSignalDef Signal;
			if (Messages.Num() == 0 || !ParseSignal(Line, Signal))
			{
				OutError = FString::Printf(TEXT("line %d: invalid signal"), Index + 1);
				Messages.Reset();
				return false;
			}
			Messages.Last().Signals.Add(MoveTemp(Signal));
		}
		else if (Line.StartsWith(TEXT("VAL_ ")))
		{
			int32 Pos = 4;
			FString IdText, SignalName;
			NextToken(Line, Pos, IdText);
			NextToken(Line, Pos, SignalName);
			const uint64 RawId = FCString::Strtoui64(*IdText, nullptr, 10);
			const FCanMessageDef* Message =
				FindMessage(static_cast<int32>(RawId & FCanFrame::ExtendedIdMask), (RawId & ExtendedFlag) != 0);
			FCanSignalDef* Signal = Message
				? const_cast<FCanMessageDef*>(Message)->Signals.FindByPredicate(
					[&SignalName](const FCanSignalDef& S) { return S.Name == SignalName; })
				: nullptr;
			FString ValueText, Description;
			while (NextToken(Line, Pos, ValueText) && ValueText != TEXT(";") && QuotedString(Line, Pos, Description))
			{
				if (Signal)
				{
					Signal->ValueNames.Add(FCString::Atoi64(*ValueText), Description);
				}
			}
		}
		else
		{
			for (const TCHAR C : Line)
			{
				bInString = (C == TEXT('"')) ? !bInString : bInString;
			}
		}
	}
	return true;
}

const FCanMessageDef* FCanDatabase::FindMessage(int32 Id, bool bExtended) const
{
	return Messages.FindByPredicate([Id, bExtended](const FCanMessageDef& M)
	{
		return M.Id == static_cast<uint32>(Id) && M.bExtended == bExtended;
	});
}

const FCanMessageDef* FCanDatabase::FindMessageByName(const FString& Name) const
{
	return Messages.FindByPredicate([&Name](const FCanMessageDef& M) { return M.Name == Name; });
}

uint64 FCanDatabase::ExtractRaw(const FCanSignalDef& Signal, const uint8 Data[8])
{
	uint64 Raw = 0;
	if (Signal.bIntel)
	{
		for (int32 Bit = 0; Bit < Signal.Length; ++Bit)
		{
			if (GetBit(Data, static_cast<uint32>(Signal.StartBit + Bit)))
			{
				Raw |= uint64(1) << Bit;
			}
		}
		return Raw;
	}
	uint32 Position = static_cast<uint32>(Signal.StartBit);
	for (int32 Bit = Signal.Length - 1; Bit >= 0; --Bit)
	{
		if (GetBit(Data, Position))
		{
			Raw |= uint64(1) << Bit;
		}
		Position = MotorolaNext(Position);
	}
	return Raw;
}

double FCanDatabase::Decode(const FCanSignalDef& Signal, const uint8 Data[8], int64* OutRawSigned)
{
	const uint64 Raw = ExtractRaw(Signal, Data);
	int64 Value = static_cast<int64>(Raw);
	if (Signal.bSigned && Signal.Length < 64 && (Raw & (uint64(1) << (Signal.Length - 1))))
	{
		Value = static_cast<int64>(Raw | ~((uint64(1) << Signal.Length) - 1));
	}
	if (OutRawSigned)
	{
		*OutRawSigned = Value;
	}
	return static_cast<double>(Value) * Signal.Factor + Signal.Offset;
}

FString FCanDatabase::Describe(const FCanFrame& Frame) const
{
	const FCanMessageDef* Message = FindMessage(Frame.Id, Frame.bExtended);
	if (Message == nullptr)
	{
		return FString();
	}
	FString Text = Message->Name;
	if (Frame.bRemote)
	{
		return Text + TEXT(" (remote request)");
	}
	uint8 Data[8] = {};
	for (int32 Index = 0; Index < Frame.Data.Num() && Index < 8; ++Index)
	{
		Data[Index] = Frame.Data[Index];
	}
	for (const FCanSignalDef& Signal : Message->Signals)
	{
		int64 Raw = 0;
		const double Value = Decode(Signal, Data, &Raw);
		Text += FString::Printf(TEXT(" %s=%s"), *Signal.Name, *FString::SanitizeFloat(Value, 0));
		if (!Signal.Unit.IsEmpty())
		{
			Text += TEXT(" ") + Signal.Unit;
		}
		if (const FString* Name = Signal.ValueNames.Find(Raw))
		{
			Text += FString::Printf(TEXT(" (%s)"), **Name);
		}
	}
	return Text;
}
