// IoTwin - draws the 2D schematic view onto a UCanvas.

#include "Schematic/SchematicRenderer.h"

#include "Bus/IoTwinBusSubsystem.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasTypes.h"
#include "Math/ScaleMatrix.h"
#include "Misc/Paths.h"
#include "Schematic/IoTwinSchematicSubsystem.h"

const FName FSchematicRenderer::BackgroundBox(TEXT("Schematic.Background"));
const FName FSchematicRenderer::View3DBox(TEXT("Schematic.View3D"));
const TCHAR* FSchematicRenderer::NodeBoxPrefix = TEXT("Schematic.Node:");

namespace
{
	/** sRGB hex colour as used in design tools. */
	FLinearColor Hex(uint32 Rgb, float Alpha = 1.0f)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(
			FColor(static_cast<uint8>(Rgb >> 16), static_cast<uint8>(Rgb >> 8), static_cast<uint8>(Rgb)));
		Color.A = Alpha;
		return Color;
	}

	namespace Palette
	{
		constexpr uint32 Background = 0x11151B;
		constexpr uint32 Grid = 0x171D25;
		constexpr uint32 Panel = 0x1A2029;
		constexpr uint32 Border = 0x2B3441;
		constexpr uint32 Text = 0xE6EAF0;
		constexpr uint32 Dim = 0x8A94A3;
		constexpr uint32 Virtual = 0x4FA3FF;
		constexpr uint32 Physical = 0xFFB547;
		constexpr uint32 BusIdle = 0x5C6773;
		constexpr uint32 Live = 0x3DDC84;
		constexpr uint32 Recent = 0xFFD166;
		constexpr uint32 Error = 0xFF6B5B;
		constexpr uint32 Link = 0xB48CFF;
	}

	constexpr float Margin = 16.0f;
	constexpr float TopBarHeight = 48.0f;
	constexpr float NodeHeight = 124.0f;
	constexpr float NodeGap = 44.0f;
	constexpr float LinkGap = 150.0f;
	constexpr float BusOffset = 62.0f;
	constexpr float RowGap = 110.0f;
	constexpr float GridStep = 32.0f;
	/** The engine's medium font is small; all text scales below are relative to this. */
	constexpr float TextBase = 1.35f;

	FString FormatAge(double Seconds)
	{
		if (Seconds < 60.0)
		{
			return FString::Printf(TEXT("%.1f s"), Seconds);
		}
		if (Seconds < 3600.0)
		{
			return FString::Printf(TEXT("%d min"), static_cast<int32>(Seconds / 60.0));
		}
		return FString::Printf(TEXT("%d h"), static_cast<int32>(Seconds / 3600.0));
	}

	FString FrameCount(int32 Count)
	{
		return Count == 1 ? FString(TEXT("1 frame")) : FString::Printf(TEXT("%d frames"), Count);
	}

	bool AreLinked(const FSystemDescription& System, const FString& A, const FString& B)
	{
		return System.Links.ContainsByPredicate([&A, &B](const FSystemLink& Link)
		{
			const FString From = FSystemLink::NodeOf(Link.From);
			const FString To = FSystemLink::NodeOf(Link.To);
			return (From == A && To == B) || (From == B && To == A);
		});
	}
}

FSchematicRenderer::FSchematicRenderer(UCanvas* InCanvas, const UIoTwinSchematicSubsystem& InSchematic,
	const UIoTwinBusSubsystem* InBus, double InNow)
	: Canvas(InCanvas)
	, Schematic(InSchematic)
	, Bus(InBus)
	, Now(InNow)
	, Font(GEngine ? GEngine->GetMediumFont() : nullptr)
{
}

// --- Primitives --------------------------------------------------------------

void FSchematicRenderer::Rect(const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color) const
{
	FCanvasTileItem Item(Position, Size, Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void FSchematicRenderer::Outline(const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color,
	float Thickness) const
{
	FCanvasBoxItem Item(Position, Size);
	Item.SetColor(Color);
	Item.LineThickness = Thickness;
	Canvas->DrawItem(Item);
}

void FSchematicRenderer::Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness) const
{
	FCanvasLineItem Item(A, B);
	Item.SetColor(Color);
	Item.LineThickness = Thickness;
	Canvas->DrawItem(Item);
}

void FSchematicRenderer::DashedLine(const FVector2D& A, const FVector2D& B, const FLinearColor& Color,
	float Thickness) const
{
	const FVector2D Delta = B - A;
	const double Length = Delta.Size();
	if (Length < 1.0)
	{
		return;
	}
	const FVector2D Direction = Delta / Length;
	for (double Start = 0.0; Start < Length; Start += 12.0)
	{
		Line(A + Direction * Start, A + Direction * FMath::Min(Start + 7.0, Length), Color, Thickness);
	}
}

void FSchematicRenderer::Dot(const FVector2D& Center, float Radius, const FLinearColor& Color) const
{
	FCanvasNGonItem Item(Center, FVector2D(Radius, Radius), 18, Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void FSchematicRenderer::Text(const FString& Value, const FVector2D& Position, const FLinearColor& Color,
	float Scale) const
{
	FCanvasTextItem Item(Position, FText::FromString(Value), Font, Color);
	Item.Scale = FVector2D(Scale * TextBase, Scale * TextBase);
	Canvas->DrawItem(Item);
}

float FSchematicRenderer::TextWidth(const FString& Value, float Scale) const
{
	float Width = 0.0f;
	float Height = 0.0f;
	Canvas->TextSize(Font, Value, Width, Height, Scale * TextBase, Scale * TextBase);
	return Width;
}

float FSchematicRenderer::LineHeight(float Scale) const
{
	float Width = 0.0f;
	float Height = 0.0f;
	Canvas->TextSize(Font, TEXT("Ag"), Width, Height, Scale * TextBase, Scale * TextBase);
	return Height * 1.08f;
}

FString FSchematicRenderer::Fit(const FString& Value, float MaxWidth, float Scale) const
{
	if (TextWidth(Value, Scale) <= MaxWidth)
	{
		return Value;
	}
	FString Result = Value;
	while (Result.Len() > 1 && TextWidth(Result + TEXT("..."), Scale) > MaxWidth)
	{
		Result.LeftChopInline(1);
	}
	return Result.TrimEnd() + TEXT("...");
}

float FSchematicRenderer::Paragraph(const FString& Value, const FVector2D& Position, float Width,
	const FLinearColor& Color, float Scale, float MaxY) const
{
	TArray<FString> Words;
	Value.ParseIntoArrayWS(Words);
	const float Step = LineHeight(Scale);
	float Y = Position.Y;
	FString Current;
	for (const FString& Word : Words)
	{
		const FString Candidate = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
		if (!Current.IsEmpty() && TextWidth(Candidate, Scale) > Width)
		{
			if (Y + 2.0f * Step > MaxY)
			{
				// Last line that fits: show that the text goes on.
				Text(Fit(Candidate + TEXT(" ..."), Width, Scale), FVector2D(Position.X, Y), Color, Scale);
				return Y + Step;
			}
			Text(Current, FVector2D(Position.X, Y), Color, Scale);
			Y += Step;
			Current = Word;
		}
		else
		{
			Current = Candidate;
		}
	}
	if (!Current.IsEmpty() && Y + Step <= MaxY)
	{
		Text(Fit(Current, Width, Scale), FVector2D(Position.X, Y), Color, Scale);
		Y += Step;
	}
	return Y;
}

// --- Helpers -----------------------------------------------------------------

FLinearColor FSchematicRenderer::AccentOf(const FString& NodeId) const
{
	const FSystemNode* Node = Schematic.GetSystem().FindNode(NodeId);
	if (Node == nullptr)
	{
		return Hex(Palette::Dim);
	}
	return Node->Kind == TEXT("physical") ? Hex(Palette::Physical) : Hex(Palette::Virtual);
}

FLinearColor FSchematicRenderer::ActivityColor(const FString& NodeId) const
{
	const FSchematicNodeActivity* Activity = Schematic.FindActivity(NodeId);
	if (Activity == nullptr)
	{
		return Hex(Palette::BusIdle);
	}
	const double Age = Now - Activity->LastSeen;
	return Age < 3.0 ? Hex(Palette::Live) : (Age < 10.0 ? Hex(Palette::Recent) : Hex(Palette::BusIdle));
}

FString FSchematicRenderer::NodeLabel(const FString& NodeId) const
{
	const FSystemNode* Node = Schematic.GetSystem().FindNode(NodeId);
	return (Node && !Node->Label.IsEmpty()) ? Node->Label : NodeId;
}

double FSchematicRenderer::SecondsSinceLastFrame() const
{
	const TArray<FSchematicLogEntry>& Log = Schematic.GetLog();
	return Log.Num() > 0 ? Now - Log.Last().Time : 1.0e9;
}

// --- Frame -------------------------------------------------------------------

void FSchematicRenderer::Draw(TArray<FSchematicHitBox>& OutHitBoxes)
{
	if (Canvas == nullptr || Canvas->Canvas == nullptr || Font == nullptr)
	{
		return;
	}
	// Draw in layout units and let the canvas scale them to the viewport.
	const float UiScale = FMath::Clamp(Canvas->ClipY / ReferenceHeight, 0.6f, 4.0f);
	ViewWidth = Canvas->ClipX / UiScale;
	ViewHeight = Canvas->ClipY / UiScale;
	Canvas->Canvas->PushRelativeTransform(FScaleMatrix(FVector(UiScale, UiScale, 1.0f)));

	const int32 FirstHitBox = OutHitBoxes.Num();
	DrawContents(OutHitBoxes);

	Canvas->Canvas->PopTransform();
	for (int32 Index = FirstHitBox; Index < OutHitBoxes.Num(); ++Index)
	{
		OutHitBoxes[Index].Position *= UiScale;
		OutHitBoxes[Index].Size *= UiScale;
	}
}

void FSchematicRenderer::DrawContents(TArray<FSchematicHitBox>& OutHitBoxes)
{
	const float Width = ViewWidth;
	const float Height = ViewHeight;
	const float PanelWidth = FMath::Clamp(Width * 0.3f, 300.0f, 440.0f);
	const FBox2D Diagram(FVector2D(Margin, TopBarHeight + Margin), FVector2D(Width - PanelWidth - 2.0f * Margin, Height - Margin));
	const FBox2D Side(FVector2D(Width - PanelWidth - Margin, TopBarHeight + Margin), FVector2D(Width - Margin, Height - Margin));

	FSchematicHitBox& Background = OutHitBoxes.AddDefaulted_GetRef();
	Background.Name = BackgroundBox;
	Background.Size = FVector2D(Width, Height);

	DrawBackground(Diagram);
	DrawTopBar(OutHitBoxes);

	if (!Schematic.HasSystem())
	{
		DrawMessage(FString::Printf(TEXT("No system description loaded: %s.  Expected data/systems/light-demo.json next to ")
			TEXT("the visualizer, or start with -IoTwinSystem=<file>."), *Schematic.GetLoadError()));
		return;
	}

	LayoutNodes(Diagram);
	DrawBuses();
	DrawLinks();
	DrawNodes(OutHitBoxes);
	DrawPulses();

	const float DetailsBottom = Side.Min.Y + Side.GetSize().Y * 0.56f;
	DrawDetails(FBox2D(Side.Min, FVector2D(Side.Max.X, DetailsBottom)));
	DrawLog(FBox2D(FVector2D(Side.Min.X, DetailsBottom + 12.0f), Side.Max));
}

void FSchematicRenderer::DrawBackground(const FBox2D& Diagram) const
{
	Rect(FVector2D::ZeroVector, FVector2D(ViewWidth, ViewHeight), Hex(Palette::Background));
	const FLinearColor GridColor = Hex(Palette::Grid);
	for (double X = Diagram.Min.X; X <= Diagram.Max.X; X += GridStep)
	{
		Line(FVector2D(X, Diagram.Min.Y), FVector2D(X, Diagram.Max.Y), GridColor, 1.0f);
	}
	for (double Y = Diagram.Min.Y; Y <= Diagram.Max.Y; Y += GridStep)
	{
		Line(FVector2D(Diagram.Min.X, Y), FVector2D(Diagram.Max.X, Y), GridColor, 1.0f);
	}
}

void FSchematicRenderer::DrawTopBar(TArray<FSchematicHitBox>& OutHitBoxes) const
{
	const float Width = ViewWidth;
	Rect(FVector2D::ZeroVector, FVector2D(Width, TopBarHeight), Hex(Palette::Panel));
	Line(FVector2D(0.0f, TopBarHeight), FVector2D(Width, TopBarHeight), Hex(Palette::Border), 1.0f);

	auto CenteredY = [this](float Scale) { return (TopBarHeight - LineHeight(Scale)) * 0.5f + 1.0f; };

	float X = Margin;
	Text(TEXT("IoTwin"), FVector2D(X, CenteredY(1.0f)), Hex(Palette::Text), 1.0f);
	X += TextWidth(TEXT("IoTwin"), 1.0f) + 10.0f;
	Text(TEXT("Schematic"), FVector2D(X, CenteredY(0.85f)), Hex(Palette::Virtual), 0.85f);
	X += TextWidth(TEXT("Schematic"), 0.85f) + 18.0f;
	if (Schematic.HasSystem())
	{
		Text(Schematic.GetSystem().Name, FVector2D(X, CenteredY(0.72f)), Hex(Palette::Dim), 0.72f);
	}

	// Button: back to the 3D scene.
	const FString ButtonText = TEXT("3D scene    [Tab]");
	const FVector2D ButtonSize(TextWidth(ButtonText, 0.72f) + 28.0f, 30.0f);
	const FVector2D ButtonPosition(Width - Margin - ButtonSize.X, (TopBarHeight - ButtonSize.Y) * 0.5f);
	Rect(ButtonPosition, ButtonSize, Hex(Palette::Border));
	Outline(ButtonPosition, ButtonSize, Hex(Palette::Virtual), 1.0f);
	Text(ButtonText, ButtonPosition + FVector2D(14.0f, (ButtonSize.Y - LineHeight(0.72f)) * 0.5f + 1.0f), Hex(Palette::Text), 0.72f);
	FSchematicHitBox& Button = OutHitBoxes.AddDefaulted_GetRef();
	Button.Name = View3DBox;
	Button.Position = ButtonPosition;
	Button.Size = ButtonSize;
	Button.Priority = 20;

	// Bus status left of the button.
	FString Status;
	FLinearColor StatusColor;
	if (Bus == nullptr || !Bus->bEnabled)
	{
		Status = TEXT("bus disabled");
		StatusColor = Hex(Palette::Dim);
	}
	else if (Bus->IsConnected())
	{
		Status = FString::Printf(TEXT("iotwin-bus %s    %.1f frames/s    %d frames"), *Bus->GetEndpoint(),
			Schematic.GetFramesPerSecond(Now), Schematic.GetTotalFrames());
		StatusColor = Hex(Palette::Live);
	}
	else
	{
		Status = FString::Printf(TEXT("waiting for iotwin-bus at %s"), *Bus->GetEndpoint());
		StatusColor = Hex(Palette::Error);
	}
	const float StatusX = ButtonPosition.X - 28.0f - TextWidth(Status, 0.68f);
	if (StatusX > X + TextWidth(Schematic.GetSystem().Name, 0.72f) + 24.0f)
	{
		Dot(FVector2D(StatusX - 12.0f, TopBarHeight * 0.5f), 5.0f, StatusColor);
		Text(Status, FVector2D(StatusX, CenteredY(0.68f)), StatusColor, 0.68f);
	}
}

void FSchematicRenderer::LayoutNodes(const FBox2D& Area)
{
	const FSystemDescription& System = Schematic.GetSystem();
	Slots.Reset();
	BusSlots.Reset();

	// One row per bus with its members; other nodes go next to a node they are linked to.
	TArray<TArray<FString>> Rows;
	TArray<const FSystemBus*> RowBuses;
	TSet<FString> Placed;
	for (const FSystemBus& SystemBus : System.Buses)
	{
		TArray<FString> Row;
		for (const FString& Member : SystemBus.Members)
		{
			if (System.FindNode(Member) && !Placed.Contains(Member))
			{
				Row.Add(Member);
				Placed.Add(Member);
			}
		}
		if (Row.Num() > 0)
		{
			Rows.Add(MoveTemp(Row));
			RowBuses.Add(&SystemBus);
		}
	}
	for (const FSystemNode& Node : System.Nodes)
	{
		if (Placed.Contains(Node.Id))
		{
			continue;
		}
		bool bInserted = false;
		for (int32 RowIndex = 0; RowIndex < Rows.Num() && !bInserted; ++RowIndex)
		{
			for (int32 Index = 0; Index < Rows[RowIndex].Num(); ++Index)
			{
				if (AreLinked(System, Rows[RowIndex][Index], Node.Id))
				{
					Rows[RowIndex].Insert(Node.Id, Index + 1);
					bInserted = true;
					break;
				}
			}
		}
		if (!bInserted)
		{
			if (Rows.Num() == 0)
			{
				Rows.AddDefaulted();
				RowBuses.Add(nullptr);
			}
			Rows.Last().Add(Node.Id);
		}
		Placed.Add(Node.Id);
	}

	const FVector2D AreaSize = Area.GetSize();
	const float RowHeight = NodeHeight + BusOffset;
	const float TotalHeight = Rows.Num() * RowHeight + FMath::Max(0, Rows.Num() - 1) * RowGap;
	float Y = Area.Min.Y + FMath::Max(24.0f, static_cast<float>(AreaSize.Y - TotalHeight) * 0.4f);

	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const TArray<FString>& Row = Rows[RowIndex];
		const FSystemBus* RowBus = RowBuses[RowIndex];

		TArray<float> Gaps;
		float GapSum = 0.0f;
		for (int32 Index = 0; Index + 1 < Row.Num(); ++Index)
		{
			const bool bBothOnBus = RowBus && RowBus->Members.Contains(Row[Index]) && RowBus->Members.Contains(Row[Index + 1]);
			const float Gap = (!bBothOnBus && AreLinked(System, Row[Index], Row[Index + 1])) ? LinkGap : NodeGap;
			Gaps.Add(Gap);
			GapSum += Gap;
		}
		const float BoxWidth = FMath::Clamp(static_cast<float>(AreaSize.X - 40.0 - GapSum) / FMath::Max(1, Row.Num()), 120.0f, 230.0f);
		const float RowWidth = BoxWidth * Row.Num() + GapSum;
		float X = Area.Min.X + FMath::Max(0.0f, static_cast<float>(AreaSize.X - RowWidth) * 0.5f);

		int32 BusIndex = INDEX_NONE;
		if (RowBus)
		{
			BusIndex = BusSlots.AddDefaulted();
			BusSlots[BusIndex].BusId = RowBus->Id;
			BusSlots[BusIndex].Y = Y + NodeHeight + BusOffset;
			BusSlots[BusIndex].X0 = TNumericLimits<float>::Max();
			BusSlots[BusIndex].X1 = TNumericLimits<float>::Lowest();
		}
		for (int32 Index = 0; Index < Row.Num(); ++Index)
		{
			FNodeSlot Slot;
			Slot.Position = FVector2D(X, Y);
			Slot.Size = FVector2D(BoxWidth, NodeHeight);
			if (RowBus && RowBus->Members.Contains(Row[Index]))
			{
				Slot.Bus = BusIndex;
				const float CenterX = static_cast<float>(Slot.Center().X);
				BusSlots[BusIndex].X0 = FMath::Min(BusSlots[BusIndex].X0, CenterX - 46.0f);
				BusSlots[BusIndex].X1 = FMath::Max(BusSlots[BusIndex].X1, CenterX + 46.0f);
			}
			Slots.Add(Row[Index], Slot);
			X += BoxWidth + (Index < Gaps.Num() ? Gaps[Index] : 0.0f);
		}
		Y += RowHeight + RowGap;
	}
}

void FSchematicRenderer::DrawBuses() const
{
	const FSystemBus* LiveBus = Schematic.GetLiveBus();
	const bool bConnected = Bus && Bus->IsConnected();
	const bool bRecentFrame = SecondsSinceLastFrame() < 0.15;

	for (int32 Index = 0; Index < BusSlots.Num(); ++Index)
	{
		const FBusSlot& Slot = BusSlots[Index];
		const FSystemBus* Definition = Schematic.GetSystem().Buses.FindByPredicate(
			[&Slot](const FSystemBus& Candidate) { return Candidate.Id == Slot.BusId; });
		if (Definition == nullptr)
		{
			continue;
		}
		const bool bLive = Definition == LiveBus && bConnected;
		const FLinearColor Color = bLive ? Hex(Palette::Live) : Hex(Palette::BusIdle);

		for (const TPair<FString, FNodeSlot>& Pair : Slots)
		{
			if (Pair.Value.Bus == Index)
			{
				const double X = Pair.Value.Center().X;
				Line(FVector2D(X, Pair.Value.Bottom()), FVector2D(X, Slot.Y), Color, 2.0f);
				Dot(FVector2D(X, Slot.Y), 5.0f, Color);
			}
		}
		Line(FVector2D(Slot.X0, Slot.Y), FVector2D(Slot.X1, Slot.Y), Color, (bLive && bRecentFrame) ? 6.0f : 4.0f);
		Line(FVector2D(Slot.X0, Slot.Y - 9.0f), FVector2D(Slot.X0, Slot.Y + 9.0f), Color, 3.0f);
		Line(FVector2D(Slot.X1, Slot.Y - 9.0f), FVector2D(Slot.X1, Slot.Y + 9.0f), Color, 3.0f);

		FString Title = FString::Printf(TEXT("%s    %s"), *Definition->Id, *Definition->Type.ToUpper());
		if (Definition->Bitrate > 0)
		{
			Title += FString::Printf(TEXT("  %d kbit/s"), Definition->Bitrate / 1000);
		}
		if (!Definition->Dbc.IsEmpty())
		{
			Title += TEXT("    DBC ") + FPaths::GetCleanFilename(Definition->Dbc);
		}
		const float LabelY = Slot.Y + 14.0f;
		Text(Title, FVector2D(Slot.X0, LabelY), Hex(Palette::Text), 0.72f);

		FString Transport = Definition->Transport;
		if (Definition == LiveBus && Bus)
		{
			// The configured transport may differ from the endpoint this run actually uses.
			Transport = FString::Printf(TEXT("iotwin-bus %s    %s"), *Bus->GetEndpoint(),
				bConnected ? TEXT("live") : TEXT("offline - start iotwin-bus"));
		}
		Text(Transport, FVector2D(Slot.X0, LabelY + LineHeight(0.72f)), bLive ? Hex(Palette::Dim) : Hex(Palette::Error, 0.85f), 0.62f);
	}
}

void FSchematicRenderer::DrawLinks() const
{
	const FLinearColor Color = Hex(Palette::Link);
	for (const FSystemLink& Link : Schematic.GetSystem().Links)
	{
		const FNodeSlot* A = Slots.Find(FSystemLink::NodeOf(Link.From));
		const FNodeSlot* B = Slots.Find(FSystemLink::NodeOf(Link.To));
		if (A == nullptr || B == nullptr)
		{
			continue;
		}
		FVector2D Start;
		FVector2D End;
		if (FMath::IsNearlyEqual(A->Position.Y, B->Position.Y))
		{
			const FNodeSlot* LeftSlot = A->Position.X < B->Position.X ? A : B;
			const FNodeSlot* RightSlot = LeftSlot == A ? B : A;
			const double Y = LeftSlot->Position.Y + LeftSlot->Size.Y * 0.5;
			Start = FVector2D(LeftSlot->Position.X + LeftSlot->Size.X, Y);
			End = FVector2D(RightSlot->Position.X, Y);
		}
		else
		{
			const FNodeSlot* Upper = A->Position.Y < B->Position.Y ? A : B;
			const FNodeSlot* Lower = Upper == A ? B : A;
			Start = FVector2D(Upper->Center().X, Upper->Bottom());
			End = FVector2D(Lower->Center().X, Lower->Position.Y);
		}
		DashedLine(Start, End, Color, 2.0f);
		Dot(Start, 4.0f, Color);
		Dot(End, 4.0f, Color);

		const FVector2D Middle = (Start + End) * 0.5;
		const float Room = FMath::Max(60.0f, static_cast<float>(FMath::Abs(End.X - Start.X)) - 12.0f);
		const FString Type = Link.Type.ToUpper();
		Text(Type, FVector2D(Middle.X - TextWidth(Type, 0.72f) * 0.5f, Middle.Y - LineHeight(0.72f) - 4.0f), Color, 0.72f);
		FString Port;
		if (Link.From.Split(TEXT(":"), nullptr, &Port))
		{
			const FString Shown = Fit(Port, Room, 0.6f);
			Text(Shown, FVector2D(Middle.X - TextWidth(Shown, 0.6f) * 0.5f, Middle.Y + 6.0f), Hex(Palette::Dim), 0.6f);
		}
	}
}

void FSchematicRenderer::DrawNodes(TArray<FSchematicHitBox>& OutHitBoxes) const
{
	const FSystemDescription& System = Schematic.GetSystem();
	for (const FSystemNode& Node : System.Nodes)
	{
		const FNodeSlot* Slot = Slots.Find(Node.Id);
		if (Slot == nullptr)
		{
			continue;
		}
		const FVector2D Position = Slot->Position;
		const FVector2D Size = Slot->Size;
		const FLinearColor Accent = AccentOf(Node.Id);
		const FSchematicNodeActivity* Activity = Schematic.FindActivity(Node.Id);
		const double Age = Activity ? Now - Activity->LastSeen : 1.0e9;

		Rect(Position, Size, Hex(Palette::Panel));
		Rect(Position, FVector2D(Size.X, 4.0f), Accent);
		if (Node.Id == Schematic.SelectedNode)
		{
			Outline(Position, Size, Hex(Palette::Text), 2.0f);
		}
		else if (Age < 0.35)
		{
			Outline(Position, Size, Accent, 2.0f);
		}
		else
		{
			Outline(Position, Size, Hex(Palette::Border), 1.0f);
		}

		const float Inner = static_cast<float>(Size.X) - 24.0f;
		const float TextLeft = static_cast<float>(Position.X) + 12.0f;
		const float MaxY = static_cast<float>(Position.Y + Size.Y) - 6.0f;
		float Y = static_cast<float>(Position.Y) + 12.0f;

		Text(Fit(Node.Label.IsEmpty() ? Node.Id : Node.Label, Inner - 16.0f, 0.82f), FVector2D(TextLeft, Y), Hex(Palette::Text), 0.82f);
		if (Slot->Bus != INDEX_NONE)
		{
			Dot(FVector2D(Position.X + Size.X - 16.0f, Y + LineHeight(0.82f) * 0.5f), 5.0f, ActivityColor(Node.Id));
		}
		Y += LineHeight(0.82f) + 4.0f;

		const FComponentInfo* Component = Node.Component.IsEmpty() ? nullptr : Schematic.GetComponents().Find(Node.Component);
		const FString What = Component ? Component->Name : (Node.Component.IsEmpty() ? Node.Program : Node.Component);
		const FString Subtitle = What.IsEmpty() ? Node.Kind : Node.Kind + TEXT("  -  ") + What;
		Text(Fit(Subtitle, Inner, 0.62f), FVector2D(TextLeft, Y), Hex(Palette::Dim), 0.62f);
		Y += LineHeight(0.62f) + 10.0f;

		if (Slot->Bus == INDEX_NONE)
		{
			Paragraph(Node.Description, FVector2D(TextLeft, Y), Inner, Hex(Palette::Dim), 0.6f, MaxY);
		}
		else if (Activity == nullptr)
		{
			Text(TEXT("no frames sent yet"), FVector2D(TextLeft, Y), Hex(Palette::Dim), 0.62f);
		}
		else
		{
			Text(Fit(Activity->LastMessage, Inner, 0.68f), FVector2D(TextLeft, Y), Hex(Palette::Text), 0.68f);
			Y += LineHeight(0.68f) + 4.0f;
			Text(Fit(FString::Printf(TEXT("%s  -  last %s ago"), *FrameCount(Activity->FrameCount), *FormatAge(Age)), Inner, 0.6f),
				FVector2D(TextLeft, Y), Hex(Palette::Dim), 0.6f);
		}

		FSchematicHitBox& HitBox = OutHitBoxes.AddDefaulted_GetRef();
		HitBox.Name = FName(*(FString(NodeBoxPrefix) + Node.Id));
		HitBox.Position = Position;
		HitBox.Size = Size;
		HitBox.Priority = 10;
	}
}

void FSchematicRenderer::DrawPulses() const
{
	const double Duration = UIoTwinSchematicSubsystem::PulseDuration;
	for (const FSchematicPulse& Pulse : Schematic.GetPulses())
	{
		const double Age = Now - Pulse.Time;
		const FNodeSlot* From = Slots.Find(Pulse.From);
		if (Age < 0.0 || Age > Duration || From == nullptr || From->Bus == INDEX_NONE)
		{
			continue;
		}
		const double T = Age / Duration;
		FLinearColor Color = AccentOf(Pulse.From);
		Color.A = T > 0.85 ? static_cast<float>((1.0 - T) / 0.15) : 1.0f;

		const double BusY = BusSlots[From->Bus].Y;
		const FVector2D Start(From->Center().X, From->Bottom());
		const FVector2D OnBus(From->Center().X, BusY);
		if (T < 0.3)
		{
			Dot(FMath::Lerp(Start, OnBus, T / 0.3), 6.0f, Color);
			continue;
		}
		for (const FString& To : Pulse.To)
		{
			const FNodeSlot* Target = Slots.Find(To);
			if (Target == nullptr || Target->Bus != From->Bus)
			{
				continue;
			}
			const FVector2D Arrive(Target->Center().X, BusY);
			const FVector2D End(Target->Center().X, Target->Bottom());
			const FVector2D Position = T < 0.75 ? FMath::Lerp(OnBus, Arrive, (T - 0.3) / 0.45)
			                                    : FMath::Lerp(Arrive, End, (T - 0.75) / 0.25);
			Dot(Position, 6.0f, Color);
		}
	}
}

// --- Side panel --------------------------------------------------------------

void FSchematicRenderer::DrawDetails(const FBox2D& Area) const
{
	Rect(Area.Min, Area.GetSize(), Hex(Palette::Panel));
	Outline(Area.Min, Area.GetSize(), Hex(Palette::Border), 1.0f);
	const float X = static_cast<float>(Area.Min.X) + 16.0f;
	const float Width = static_cast<float>(Area.GetSize().X) - 32.0f;
	const float Y = static_cast<float>(Area.Min.Y) + 12.0f;
	const float MaxY = static_cast<float>(Area.Max.Y) - 8.0f;

	const FSystemNode* Node = Schematic.SelectedNode.IsEmpty() ? nullptr : Schematic.GetSystem().FindNode(Schematic.SelectedNode);
	if (Node)
	{
		DrawNodeDetails(*Node, X, Y, Width, MaxY);
	}
	else
	{
		DrawSystemOverview(X, Y, Width, MaxY);
	}
}

void FSchematicRenderer::DrawSystemOverview(float X, float Y, float Width, float MaxY) const
{
	const FSystemDescription& System = Schematic.GetSystem();
	Text(TEXT("SYSTEM"), FVector2D(X, Y), Hex(Palette::Dim), 0.58f);
	Y += LineHeight(0.58f) + 2.0f;
	Text(Fit(System.Name, Width, 0.9f), FVector2D(X, Y), Hex(Palette::Text), 0.9f);
	Y += LineHeight(0.9f) + 4.0f;
	Y = Paragraph(System.Description, FVector2D(X, Y), Width, Hex(Palette::Text), 0.64f, MaxY) + 8.0f;

	const FString Counts = FString::Printf(TEXT("%d nodes  -  %d buses  -  %d links  -  %d messages"),
		System.Nodes.Num(), System.Buses.Num(), System.Links.Num(), System.Messages.Num());
	if (Y + LineHeight(0.62f) > MaxY)
	{
		return;
	}
	Text(Fit(Counts, Width, 0.62f), FVector2D(X, Y), Hex(Palette::Dim), 0.62f);
	Y += LineHeight(0.62f);
	Text(Fit(FPaths::GetCleanFilename(Schematic.GetSystemPath()) + FString::Printf(TEXT("  -  %d components in the database"),
		Schematic.GetComponents().Num()), Width, 0.6f), FVector2D(X, Y), Hex(Palette::Dim), 0.6f);
	Y += LineHeight(0.6f) + 12.0f;

	if (System.Messages.Num() > 0 && Y + 2.0f * LineHeight(0.62f) <= MaxY)
	{
		Text(TEXT("MESSAGES"), FVector2D(X, Y), Hex(Palette::Dim), 0.58f);
		Y += LineHeight(0.58f) + 2.0f;
		for (const FSystemMessage& Message : System.Messages)
		{
			if (Y + LineHeight(0.62f) > MaxY)
			{
				return;
			}
			const FString Entry = FString::Printf(TEXT("%s:  %s  ->  %s"), *Message.Name,
				*FString::Join(Message.Producers, TEXT(", ")), *FString::Join(Message.Consumers, TEXT(", ")));
			Text(Fit(Entry, Width, 0.62f), FVector2D(X, Y), Hex(Palette::Text), 0.62f);
			Y += LineHeight(0.62f);
		}
		Y += 12.0f;
	}
	if (Y + LineHeight(0.62f) <= MaxY)
	{
		Text(TEXT("Click a node for details.  [Tab] switches to the 3D scene."), FVector2D(X, Y), Hex(Palette::Dim), 0.6f);
	}
}

void FSchematicRenderer::DrawNodeDetails(const FSystemNode& Node, float X, float Y, float Width, float MaxY) const
{
	const FSystemDescription& System = Schematic.GetSystem();
	const FLinearColor PrimaryColor = Hex(Palette::Text);
	const FLinearColor SecondaryColor = Hex(Palette::Dim);
	const float Small = 0.62f;
	const float Step = LineHeight(Small);

	Text(TEXT("NODE"), FVector2D(X, Y), SecondaryColor, 0.58f);
	Y += LineHeight(0.58f) + 2.0f;
	Text(Fit(Node.Label.IsEmpty() ? Node.Id : Node.Label, Width, 0.9f), FVector2D(X, Y), AccentOf(Node.Id), 0.9f);
	Y += LineHeight(0.9f) + 2.0f;
	Text(Fit(FString::Printf(TEXT("id %s  -  %s"), *Node.Id, *Node.Kind), Width, Small), FVector2D(X, Y), SecondaryColor, Small);
	Y += Step;
	if (!Node.Program.IsEmpty())
	{
		Text(Fit(TEXT("Program: ") + Node.Program, Width, Small), FVector2D(X, Y), PrimaryColor, Small);
		Y += Step;
	}
	Y = Paragraph(Node.Description, FVector2D(X, Y + 2.0f), Width, PrimaryColor, 0.64f, MaxY) + 6.0f;

	if (const FSchematicNodeActivity* Activity = Schematic.FindActivity(Node.Id); Activity && Y + 2.0f * Step <= MaxY)
	{
		Text(Fit(FString::Printf(TEXT("%s sent, last %s ago"), *FrameCount(Activity->FrameCount),
			*FormatAge(Now - Activity->LastSeen)), Width, Small), FVector2D(X, Y), ActivityColor(Node.Id), Small);
		Y += Step;
		Text(Fit(TEXT("Last: ") + Activity->LastMessage, Width, Small), FVector2D(X, Y), PrimaryColor, Small);
		Y += Step;
	}

	TArray<FString> Sends;
	TArray<FString> Receives;
	for (const FSystemMessage& Message : System.Messages)
	{
		if (Message.Producers.Contains(Node.Id))
		{
			Sends.Add(Message.Name);
		}
		if (Message.Consumers.Contains(Node.Id))
		{
			Receives.Add(Message.Name);
		}
	}
	if (Sends.Num() > 0 && Y + Step <= MaxY)
	{
		Text(Fit(TEXT("Sends: ") + FString::Join(Sends, TEXT(", ")), Width, Small), FVector2D(X, Y), PrimaryColor, Small);
		Y += Step;
	}
	if (Receives.Num() > 0 && Y + Step <= MaxY)
	{
		Text(Fit(TEXT("Receives: ") + FString::Join(Receives, TEXT(", ")), Width, Small), FVector2D(X, Y), PrimaryColor, Small);
		Y += Step;
	}

	for (const FSystemLink& Link : System.Links)
	{
		const FString From = FSystemLink::NodeOf(Link.From);
		const FString To = FSystemLink::NodeOf(Link.To);
		if (From != Node.Id && To != Node.Id)
		{
			continue;
		}
		const FString Other = From == Node.Id ? To : From;
		const FString Description = FString::Printf(TEXT("%s to %s: %s"), *Link.Type.ToUpper(), *NodeLabel(Other), *Link.Carries);
		Y = Paragraph(Description, FVector2D(X, Y), Width, Hex(Palette::Link), Small, MaxY);
	}

	if (!Node.Component.IsEmpty())
	{
		DrawComponent(Node.Component, TEXT("COMPONENT"), X, Y + 8.0f, Width, MaxY, 0);
	}
}

float FSchematicRenderer::DrawComponent(const FString& ComponentId, const FString& Heading, float X, float Y, float Width,
	float MaxY, int32 Depth) const
{
	const float Small = 0.6f;
	const float Step = LineHeight(Small);
	if (Y + LineHeight(0.58f) + Step > MaxY)
	{
		return Y;
	}
	Text(Heading, FVector2D(X, Y), Hex(Palette::Dim), 0.58f);
	Y += LineHeight(0.58f) + 2.0f;

	const FComponentInfo* Component = Schematic.GetComponents().Find(ComponentId);
	if (Component == nullptr)
	{
		Text(Fit(TEXT("not in the component database: ") + ComponentId, Width, Small), FVector2D(X, Y), Hex(Palette::Error), Small);
		return Y + Step;
	}
	Text(Fit(FString::Printf(TEXT("%s  (%s)"), *Component->Name, *Component->Manufacturer), Width, 0.68f), FVector2D(X, Y),
		Hex(Palette::Text), 0.68f);
	Y += LineHeight(0.68f) + 2.0f;

	bool bAnyUnverified = false;
	const float ValueX = X + Width * 0.5f;
	for (const FComponentParameter& Parameter : Component->Parameters)
	{
		if (Y + Step > MaxY)
		{
			return Y;
		}
		Text(Fit(Parameter.Name.Replace(TEXT("_"), TEXT(" ")), Width * 0.5f - 8.0f, Small), FVector2D(X, Y), Hex(Palette::Dim), Small);
		const FString Value = Parameter.bVerified ? Parameter.Display : Parameter.Display + TEXT(" *");
		Text(Fit(Value, Width * 0.5f, Small), FVector2D(ValueX, Y), Parameter.bVerified ? Hex(Palette::Text) : Hex(Palette::Recent), Small);
		Y += Step;
		bAnyUnverified |= !Parameter.bVerified;
	}
	if (bAnyUnverified && Y + Step <= MaxY)
	{
		Text(TEXT("* not verified yet"), FVector2D(X, Y), Hex(Palette::Recent), 0.55f);
		Y += Step;
	}

	if (Depth == 0)
	{
		for (const FComponentPart& Part : Component->Contains)
		{
			Y = DrawComponent(Part.ComponentId, Part.Instance.ToUpper() + TEXT("  (on board)"), X, Y + 8.0f, Width, MaxY, Depth + 1);
		}
	}
	return Y;
}

void FSchematicRenderer::DrawLog(const FBox2D& Area) const
{
	Rect(Area.Min, Area.GetSize(), Hex(Palette::Panel));
	Outline(Area.Min, Area.GetSize(), Hex(Palette::Border), 1.0f);
	const float X = static_cast<float>(Area.Min.X) + 16.0f;
	const float Width = static_cast<float>(Area.GetSize().X) - 32.0f;
	const float MaxY = static_cast<float>(Area.Max.Y) - 8.0f;
	float Y = static_cast<float>(Area.Min.Y) + 12.0f;

	Text(TEXT("BUS MESSAGES"), FVector2D(X, Y), Hex(Palette::Dim), 0.58f);
	const FString Total = FString::Printf(TEXT("%d total"), Schematic.GetTotalFrames());
	Text(Total, FVector2D(X + Width - TextWidth(Total, 0.58f), Y), Hex(Palette::Dim), 0.58f);
	Y += LineHeight(0.58f) + 6.0f;

	const TArray<FSchematicLogEntry>& Log = Schematic.GetLog();
	if (Log.Num() == 0)
	{
		Paragraph(TEXT("No frames yet. Start iotwin-bus and can_sim, or press SPACE to toggle the wall switch."),
			FVector2D(X, Y), Width, Hex(Palette::Dim), 0.62f, MaxY);
		return;
	}

	const float Scale = 0.6f;
	const float Step = LineHeight(Scale) + 3.0f;
	const float AgeWidth = 52.0f;
	const float NodeWidth = 78.0f;
	for (int32 Index = Log.Num() - 1; Index >= 0 && Y + Step <= MaxY; --Index)
	{
		const FSchematicLogEntry& Entry = Log[Index];
		Text(FormatAge(Now - Entry.Time), FVector2D(X, Y), Hex(Palette::Dim), 0.56f);
		Text(Fit(Entry.NodeId.IsEmpty() ? TEXT("?") : Entry.NodeId, NodeWidth - 6.0f, Scale), FVector2D(X + AgeWidth, Y),
			AccentOf(Entry.NodeId), Scale);
		// Raw frame on the right, shortened for long (e.g. diagnostic) frames so the decoded text stays readable.
		const FString Raw = Fit(Entry.Raw, Width * 0.22f, 0.56f);
		const float RawWidth = TextWidth(Raw, 0.56f);
		Text(Raw, FVector2D(X + Width - RawWidth, Y), Hex(Palette::Dim), 0.56f);
		const FString Decoded = Entry.Decoded.IsEmpty() ? TEXT("(not in DBC)") : Entry.Decoded;
		Text(Fit(Decoded, Width - AgeWidth - NodeWidth - RawWidth - 10.0f, Scale), FVector2D(X + AgeWidth + NodeWidth, Y),
			Entry.Decoded.IsEmpty() ? Hex(Palette::Dim) : Hex(Palette::Text), Scale);
		Y += Step;
	}
}

void FSchematicRenderer::DrawMessage(const FString& Value) const
{
	const float Width = FMath::Min(ViewWidth - 2.0f * Margin, 720.0f);
	const FVector2D Position((ViewWidth - Width) * 0.5f, ViewHeight * 0.4f);
	Paragraph(Value, Position, Width, Hex(Palette::Error), 0.8f, ViewHeight - Margin);
}
