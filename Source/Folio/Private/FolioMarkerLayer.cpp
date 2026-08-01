#include "FolioMarkerLayer.h"

#include "FolioDocumentHost.h"
#include "FolioMarkerDocument.h"
#include "FolioModule.h"
#include "FolioRenderInterface.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

#include <RmlUi/Core/Context.h>

TSharedPtr<SFolioMarkerLayer> FFolioMarkerLayer::Layer;
TWeakObjectPtr<UWorld> FFolioMarkerLayer::AttachedTo;

void SFolioMarkerLayer::Construct(const FArguments&)
{
	// Nothing here takes input: a marker is a label on the world, and letting one swallow a click
	// under the cursor would be a bug that only appears when a nameplate happens to be in the way.
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);
}

int32 SFolioMarkerLayer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	const UWorld* Painting = World.Get();
	if (!Painting)
	{
		return LayerId;
	}

	FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface();
	if (!Renderer)
	{
		return LayerId;
	}

	// Gathered rather than painted straight from the live list, because the paint order is the sort
	// order and registration order is arbitrary.
	TArray<const UFolioMarkerDocument::FPlacement*> Visible;
	Visible.Reserve(UFolioMarkerDocument::GetLive().Num());

	for (const UFolioMarkerDocument* Marker : UFolioMarkerDocument::GetLive())
	{
		if (!Marker || Marker->GetWorld() != Painting)
		{
			continue;
		}

		if (const UFolioMarkerDocument::FPlacement* Placement = Marker->GetPlacement())
		{
			if (Placement->Host && Placement->Host->GetContext())
			{
				Visible.Add(Placement);
			}
		}
	}

	Visible.StableSort([](const UFolioMarkerDocument::FPlacement& A, const UFolioMarkerDocument::FPlacement& B)
	{
		return A.SortOrder < B.SortOrder;
	});

	int32 Highest = LayerId;

	for (const UFolioMarkerDocument::FPlacement* Placement : Visible)
	{
		// The document is laid out at its authored size and placed by a transform, rather than laid
		// out at its scaled size. A scaled layout would reflow the text at every distance, so a
		// nameplate would rewrap as the player walked towards it.
		const FGeometry Marker = AllottedGeometry.MakeChild(
			FVector2f(Placement->Size),
			FSlateLayoutTransform(Placement->Scale, FVector2f(Placement->Position)));

		const FIntPoint ContextSize(
			FMath::Max(1, FMath::RoundToInt(Placement->Size.X)),
			FMath::Max(1, FMath::RoundToInt(Placement->Size.Y)));

		Renderer->BeginFrame(OutDrawElements, Marker, Highest, ContextSize, Placement->Opacity);
		Placement->Host->GetContext()->Render();
		Renderer->EndFrame();

		// One above the last, so the next marker cannot interleave with this one. Sharing a layer
		// would let two overlapping plates draw through each other in geometry order.
		Highest = FMath::Max(Highest, Renderer->GetMaxLayerId()) + 1;
	}

	return Highest;
}

void FFolioMarkerLayer::EnsureAttached(const UWorld* World)
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	UGameViewportClient* Viewport = World->GetGameViewport();
	if (!Viewport)
	{
		return;
	}

	if (Layer.IsValid() && AttachedTo.Get() == World)
	{
		return;
	}

	Detach();

	Layer = SNew(SFolioMarkerLayer);
	Layer->SetWorld(const_cast<UWorld*>(World));
	AttachedTo = const_cast<UWorld*>(World);

	// Order zero, so anything the game adds to the viewport later sits above it. A marker belongs
	// under the HUD, not over the pause menu.
	Viewport->AddViewportWidgetContent(Layer.ToSharedRef(), 0);
}

void FFolioMarkerLayer::Detach()
{
	if (!Layer.IsValid())
	{
		return;
	}

	if (const UWorld* World = AttachedTo.Get())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(Layer.ToSharedRef());
		}
	}

	Layer.Reset();
	AttachedTo.Reset();
}
