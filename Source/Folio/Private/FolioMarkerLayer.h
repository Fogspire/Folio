#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UWorld;

/**
 * One widget over the viewport that paints every marker.
 *
 * One widget rather than one per marker, because a marker is not a Slate participant: it takes no
 * input, has no layout, and its position comes from a projection rather than from a slot. A hundred
 * nameplates are a hundred draw calls either way, and this way they are not a hundred widgets Slate
 * has to arrange, hit test and invalidate.
 *
 * It sits under everything a game adds to the viewport itself, so a HUD, a menu or a loading screen
 * covers markers rather than fighting them.
 */
class SFolioMarkerLayer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFolioMarkerLayer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetWorld(TWeakObjectPtr<UWorld> InWorld) { World = InWorld; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakObjectPtr<UWorld> World;
};

/** Owns the one layer per game viewport, and puts it there the first time a marker needs it. */
class FFolioMarkerLayer
{
public:
	/** Adds the layer to the world's viewport if it is not already there. Cheap to call repeatedly. */
	static void EnsureAttached(const UWorld* World);

	/** Takes it down. Called when the module shuts down; a world tearing down does it for us. */
	static void Detach();

private:
	static TSharedPtr<SFolioMarkerLayer> Layer;
	static TWeakObjectPtr<UWorld> AttachedTo;
};
