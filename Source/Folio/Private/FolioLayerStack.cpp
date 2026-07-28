#include "FolioLayerStack.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FolioModule.h"
#include "Rendering/RenderingCommon.h"
#include "TextureResource.h"

FFolioLayerStack::FFolioLayerStack() = default;

FFolioLayerStack::~FFolioLayerStack()
{
	Reset();
}

void FFolioLayerStack::Reset()
{
	EndCanvas();
	Stack.Empty();
	Pool.Empty();
}

UTextureRenderTarget2D* FFolioLayerStack::Acquire(const FIntPoint Size, const bool bClear)
{
	// TMultiMap has no RemoveAndCopyValue: take one of the values for this size and drop it
	// from the pool by hand.
	TArray<TStrongObjectPtr<UTextureRenderTarget2D>> Available;
	Pool.MultiFind(Size, Available);

	if (Available.Num() > 0 && Available[0].IsValid())
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Found = Available[0];
		Pool.RemoveSingle(Size, Found);

		if (bClear)
		{
			// A pooled target still holds the last layer that used it. Recreating the
			// resource clears it to ClearColor without needing a world to hand to
			// UKismetRenderingLibrary.
			Found->UpdateResourceImmediate(true);
		}
		Stack.Add({ Found, Size });
		return Found.Get();
	}

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
	Target->RenderTargetFormat = RTF_RGBA8;

	// Everything downstream is premultiplied, so the clear colour has to be premultiplied
	// transparent rather than opaque black, or every layer arrives with a black fringe.
	Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	Target->bAutoGenerateMips = false;
	Target->bForceLinearGamma = false;
	Target->InitAutoFormat(Size.X, Size.Y);
	Target->UpdateResourceImmediate(true);

	Stack.Add({ TStrongObjectPtr<UTextureRenderTarget2D>(Target), Size });
	return Target;
}

void FFolioLayerStack::Release(UTextureRenderTarget2D* Target)
{
	if (Target)
	{
		Pool.Add(FIntPoint(Target->SizeX, Target->SizeY), TStrongObjectPtr<UTextureRenderTarget2D>(Target));
	}
}

bool FFolioLayerStack::Push(const FIntPoint Size)
{
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}

	// A canvas is bound to one target, so it has to close before the layer changes.
	EndCanvas();

	return Acquire(Size, true) != nullptr;
}

void FFolioLayerStack::Pop()
{
	if (Stack.IsEmpty())
	{
		return;
	}

	EndCanvas();

	const FLayer Layer = Stack.Pop();
	Release(Layer.Target.Get());
}

UTextureRenderTarget2D* FFolioLayerStack::GetCurrent() const
{
	return Stack.IsEmpty() ? nullptr : Stack.Last().Target.Get();
}

UTextureRenderTarget2D* FFolioLayerStack::GetBelow() const
{
	return Stack.Num() >= 2 ? Stack[Stack.Num() - 2].Target.Get() : nullptr;
}

UTextureRenderTarget2D* FFolioLayerStack::DetachCurrent()
{
	if (Stack.IsEmpty())
	{
		return nullptr;
	}

	EndCanvas();

	FLayer& Layer = Stack.Last();
	UTextureRenderTarget2D* Detached = Layer.Target.Get();

	// The caller owns the detached target from here, so the layer needs a fresh one to keep
	// drawing into. Not cleared: RmlUi saves a layer and then carries on with it intact.
	UTextureRenderTarget2D* Replacement = NewObject<UTextureRenderTarget2D>();
	Replacement->RenderTargetFormat = RTF_RGBA8;
	Replacement->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	Replacement->bAutoGenerateMips = false;
	Replacement->InitAutoFormat(Layer.Size.X, Layer.Size.Y);
	Replacement->UpdateResourceImmediate(true);

	Layer.Target = TStrongObjectPtr<UTextureRenderTarget2D>(Replacement);
	return Detached;
}

FCanvas* FFolioLayerStack::BeginCanvas()
{
	UTextureRenderTarget2D* Target = GetCurrent();
	if (!Target)
	{
		return nullptr;
	}

	if (Canvas && CanvasTarget == Target)
	{
		return Canvas;
	}

	EndCanvas();

	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return nullptr;
	}

	Canvas = new FCanvas(Resource, nullptr, FGameTime(), GMaxRHIFeatureLevel);
	CanvasTarget = Target;
	return Canvas;
}

void FFolioLayerStack::EndCanvas()
{
	if (!Canvas)
	{
		return;
	}

	// Flushing is what actually enqueues the draws, and it has to happen before anything
	// reads the target.
	Canvas->Flush_GameThread();
	delete Canvas;
	Canvas = nullptr;
	CanvasTarget = nullptr;
}

void FFolioLayerStack::DrawTriangles(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
	UTexture* Texture, const FVector2f Offset)
{
	FCanvas* Target = BeginCanvas();
	if (!Target || Indices.Num() < 3 || !Texture || !Texture->GetResource())
	{
		return;
	}

	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Indices.Num() / 3);

	for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
	{
		const FSlateVertex& A = Vertices[Indices[Index + 0]];
		const FSlateVertex& B = Vertices[Indices[Index + 1]];
		const FSlateVertex& C = Vertices[Indices[Index + 2]];

		FCanvasUVTri& Tri = Triangles.AddDefaulted_GetRef();
		Tri.V0_Pos = FVector2D(A.Position.X + Offset.X, A.Position.Y + Offset.Y);
		Tri.V1_Pos = FVector2D(B.Position.X + Offset.X, B.Position.Y + Offset.Y);
		Tri.V2_Pos = FVector2D(C.Position.X + Offset.X, C.Position.Y + Offset.Y);

		Tri.V0_UV = FVector2D(A.TexCoords[0], A.TexCoords[1]);
		Tri.V1_UV = FVector2D(B.TexCoords[0], B.TexCoords[1]);
		Tri.V2_UV = FVector2D(C.TexCoords[0], C.TexCoords[1]);

		// The target is sRGB, so the canvas re-encodes on write. Passing the byte values
		// through as linear would encode them a second time, which is what turns a dark
		// panel into light grey. FLinearColor's FColor constructor decodes first, so the
		// round trip lands back on the original value.
		Tri.V0_Color = FLinearColor(A.Color);
		Tri.V1_Color = FLinearColor(B.Color);
		Tri.V2_Color = FLinearColor(C.Color);
	}

	FCanvasTriangleItem Item(Triangles, Texture->GetResource());

	// AlphaComposite, not AlphaBlend. Everything from RmlUi is already multiplied by its own
	// alpha, and AlphaBlend multiplies again, which lightens a surface every time it passes
	// through a layer.
	Item.BlendMode = SE_BLEND_AlphaComposite;
	Target->DrawItem(Item);
}

void FFolioLayerStack::CompositeDown(const bool bBlend)
{
	UTextureRenderTarget2D* Source = GetCurrent();
	UTextureRenderTarget2D* Destination = GetBelow();
	if (!Source || !Destination || !Source->GetResource())
	{
		return;
	}

	// The canvas is open on the source, and it has to be flushed before the source can be
	// read as a texture.
	EndCanvas();

	FTextureRenderTargetResource* Resource = Destination->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}

	FCanvas Composite(Resource, nullptr, FGameTime(), GMaxRHIFeatureLevel);

	FCanvasTileItem Item(FVector2D::ZeroVector, Source->GetResource(),
		FVector2D(Source->SizeX, Source->SizeY), FLinearColor::White);
	Item.BlendMode = bBlend ? SE_BLEND_AlphaComposite : SE_BLEND_Opaque;
	Composite.DrawItem(Item);
	Composite.Flush_GameThread();
}
