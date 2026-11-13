// IoTwin - draws the 2D schematic view onto a UCanvas.

#pragma once

#include "CoreMinimal.h"

class UCanvas;
class UFont;
class UIoTwinBusSubsystem;
class UIoTwinSchematicSubsystem;
struct FSystemNode;

/** A clickable area of the schematic, registered by the HUD as hit box. */
struct FSchematicHitBox
{
	FName Name;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	int32 Priority = 0;
};

/**
 * Paints one frame of the schematic: nodes of the system description, buses with
 * their members, links (USB, ...), frames travelling from producer to consumers,
 * a detail panel for the selected node and the decoded message log.
 */
class IOTWIN_API FSchematicRenderer
{
public:
	/** Hit box names. Node boxes are NodeBoxPrefix + node id. */
	static const FName BackgroundBox;
	static const FName View3DBox;
	static const TCHAR* NodeBoxPrefix;

	FSchematicRenderer(UCanvas* InCanvas, const UIoTwinSchematicSubsystem& InSchematic, const UIoTwinBusSubsystem* InBus,
		double InNow);

	/** Draws the whole view. Hit boxes are returned in screen pixels. */
	void Draw(TArray<FSchematicHitBox>& OutHitBoxes);

	/** Layout is designed for 900 px height and scaled to the viewport. */
	static constexpr float ReferenceHeight = 900.0f;

private:
	struct FNodeSlot
	{
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		int32 Bus = INDEX_NONE; // index into BusSlots, if the node is a member of the row's bus

		FVector2D Center() const { return Position + Size * 0.5; }
		double Bottom() const { return Position.Y + Size.Y; }
	};

	struct FBusSlot
	{
		FString BusId;
		float Y = 0.0f;
		float X0 = 0.0f;
		float X1 = 0.0f;
	};

	// Primitives
	void Rect(const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color) const;
	void Outline(const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color, float Thickness) const;
	void Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness) const;
	void DashedLine(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness) const;
	void Dot(const FVector2D& Center, float Radius, const FLinearColor& Color) const;
	void Text(const FString& Value, const FVector2D& Position, const FLinearColor& Color, float Scale) const;
	float TextWidth(const FString& Value, float Scale) const;
	float LineHeight(float Scale) const;
	FString Fit(const FString& Value, float MaxWidth, float Scale) const;
	/** Word-wrapped text; returns the y below the last line. Stops before MaxY. */
	float Paragraph(const FString& Value, const FVector2D& Position, float Width, const FLinearColor& Color, float Scale,
		float MaxY) const;

	// Parts
	void DrawContents(TArray<FSchematicHitBox>& OutHitBoxes);
	void DrawBackground(const FBox2D& Diagram) const;
	void DrawTopBar(TArray<FSchematicHitBox>& OutHitBoxes) const;
	void LayoutNodes(const FBox2D& Area);
	void DrawBuses() const;
	void DrawLinks() const;
	void DrawNodes(TArray<FSchematicHitBox>& OutHitBoxes) const;
	void DrawPulses() const;
	void DrawDetails(const FBox2D& Area) const;
	void DrawNodeDetails(const FSystemNode& Node, float X, float Y, float Width, float MaxY) const;
	void DrawSystemOverview(float X, float Y, float Width, float MaxY) const;
	float DrawComponent(const FString& ComponentId, const FString& Heading, float X, float Y, float Width, float MaxY,
		int32 Depth) const;
	void DrawLog(const FBox2D& Area) const;
	void DrawMessage(const FString& Value) const;

	FLinearColor AccentOf(const FString& NodeId) const;
	FLinearColor ActivityColor(const FString& NodeId) const;
	FString NodeLabel(const FString& NodeId) const;
	double SecondsSinceLastFrame() const;

	UCanvas* Canvas;
	const UIoTwinSchematicSubsystem& Schematic;
	const UIoTwinBusSubsystem* Bus;
	double Now;
	UFont* Font;
	float ViewWidth = 0.0f;  // in layout units (screen pixels / UiScale)
	float ViewHeight = 0.0f;

	TMap<FString, FNodeSlot> Slots;
	TArray<FBusSlot> BusSlots;
};
