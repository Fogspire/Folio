#include "FolioRenderInterface.h"

#include "Engine/Texture2D.h"
#include "FolioModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "TextureResource.h"

namespace
{
	FColor ToSlateColor(const Rml::ColourbPremultiplied& Colour)
	{
		// Both sides are premultiplied, so this is a channel swap and nothing else.
		return FColor(Colour.red, Colour.green, Colour.blue, Colour.alpha);
	}

	FVector2f ToVector2f(const Rml::Vector2f& Value)
	{
		return FVector2f(Value.x, Value.y);
	}
}

FFolioRenderInterface::FFolioRenderInterface() = default;

const FSlateResourceHandle& FFolioRenderInterface::GetWhiteHandle()
{
	// Built on demand rather than in the constructor: the module starts before Slate's
	// renderer exists, and a handle taken then is permanently invalid.
	if (WhiteHandle.IsValid())
	{
		return WhiteHandle;
	}

	if (!WhiteTexture.IsValid())
	{
		const uint8 Opaque[4] = { 255, 255, 255, 255 };
		WhiteTexture.Reset(CreateTexture(Opaque, FIntPoint(1, 1)));
		if (!WhiteTexture.IsValid())
		{
			return WhiteHandle;
		}

		WhiteBrush.SetResourceObject(WhiteTexture.Get());
		WhiteBrush.ImageSize = FVector2f(1.0f, 1.0f);
		WhiteBrush.DrawAs = ESlateBrushDrawType::Image;
	}

	if (FSlateApplication::IsInitialized())
	{
		WhiteHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(WhiteBrush);
	}

	return WhiteHandle;
}

FFolioRenderInterface::~FFolioRenderInterface()
{
	Geometries.Empty();
	Textures.Empty();
}

void FFolioRenderInterface::BeginFrame(FSlateWindowElementList& InElementList, const FGeometry& InGeometry,
	const int32 InLayerId)
{
	ElementList = &InElementList;
	Geometry = InGeometry;
	LayerId = InLayerId;
	MaxLayerId = InLayerId;

	bScissorEnabled = false;
	bClipPushed = false;
	bHasTransform = false;
	RmlTransform = FSlateRenderTransform();
}

void FFolioRenderInterface::EndFrame()
{
	// A document that leaves clipping enabled would otherwise leak its clip zone into
	// whatever Slate draws after it.
	if (bClipPushed && ElementList)
	{
		ElementList->PopClip();
		bClipPushed = false;
	}

	ElementList = nullptr;
}

//
// Geometry
//

Rml::CompiledGeometryHandle FFolioRenderInterface::CompileGeometry(const Rml::Span<const Rml::Vertex> Vertices,
	const Rml::Span<const int> Indices)
{
	TUniquePtr<FCompiledGeometry> Compiled = MakeUnique<FCompiledGeometry>();

	Compiled->Vertices.Reserve(Vertices.size());
	for (const Rml::Vertex& Vertex : Vertices)
	{
		FSlateVertex& Out = Compiled->Vertices.AddDefaulted_GetRef();
		Out.Position = ToVector2f(Vertex.position);
		Out.TexCoords[0] = Vertex.tex_coord.x;
		Out.TexCoords[1] = Vertex.tex_coord.y;
		Out.TexCoords[2] = 1.0f;
		Out.TexCoords[3] = 1.0f;
		Out.MaterialTexCoords = FVector2f(Vertex.tex_coord.x, Vertex.tex_coord.y);
		Out.Color = ToSlateColor(Vertex.colour);
	}

	Compiled->Indices.Reserve(Indices.size());
	for (const int Index : Indices)
	{
		Compiled->Indices.Add(static_cast<SlateIndex>(Index));
	}

	const Rml::CompiledGeometryHandle Handle = NextHandle++;
	Geometries.Add(Handle, MoveTemp(Compiled));
	return Handle;
}

void FFolioRenderInterface::RenderGeometry(const Rml::CompiledGeometryHandle Handle,
	const Rml::Vector2f Translation, const Rml::TextureHandle Texture)
{
	if (!ElementList)
	{
		return;
	}

	const TUniquePtr<FCompiledGeometry>* Found = Geometries.Find(Handle);
	if (!Found || !Found->IsValid())
	{
		return;
	}

	const FCompiledGeometry& Compiled = **Found;
	if (Compiled.Vertices.IsEmpty() || Compiled.Indices.IsEmpty())
	{
		return;
	}

	ApplyClipping();

	// Slate wants window space, RmlUi works in document space, and the two are separated by
	// the widget's own geometry plus whatever transform RCSS has applied. Composing them here
	// rather than baking them at compile time is what lets one compiled geometry be drawn at
	// several positions, which is exactly what RmlUi does with a translated draw.
	FSlateRenderTransform Transform = Geometry.GetAccumulatedRenderTransform();
	if (bHasTransform)
	{
		Transform = Concatenate(RmlTransform, Transform);
	}
	Transform = Concatenate(FSlateRenderTransform(FVector2f(Translation.x, Translation.y)), Transform);

	ScratchVertices.Reset(Compiled.Vertices.Num());
	for (const FSlateVertex& Source : Compiled.Vertices)
	{
		FSlateVertex& Out = ScratchVertices.Add_GetRef(Source);
		Out.Position = FVector2f(TransformPoint(Transform, FVector2f(Source.Position)));
	}

	const FTexture* Bound = FindTexture(Texture);
	const FSlateResourceHandle& Resource = Bound ? Bound->Handle : GetWhiteHandle();

	// Slate dereferences the resource handle's proxy without checking it, so an invalid one
	// is a crash rather than a missing texture. Untextured geometry is most of a document -
	// every background, border and gradient - so this is not an edge case.
	if (!Resource.IsValid())
	{
		return;
	}

	// RmlUi 6.x supplies vertex colours and generated texture data premultiplied and expects
	// the blend mode to match. Straight-alpha blending here is what produces the dark fringing
	// around text and translucent edges that reads as a filtering problem.
	FSlateDrawElement::MakeCustomVerts(
		*ElementList,
		LayerId,
		Resource,
		ScratchVertices,
		Compiled.Indices,
		nullptr, 0, 0,
		ESlateDrawEffect::PreMultipliedAlpha);

	MaxLayerId = FMath::Max(MaxLayerId, LayerId);
}

void FFolioRenderInterface::ReleaseGeometry(const Rml::CompiledGeometryHandle Handle)
{
	Geometries.Remove(Handle);
}

//
// Textures
//

FFolioRenderInterface::FTexture* FFolioRenderInterface::FindTexture(const Rml::TextureHandle Handle)
{
	const TUniquePtr<FTexture>* Found = Textures.Find(Handle);
	return Found && Found->IsValid() ? Found->Get() : nullptr;
}

UTexture2D* FFolioRenderInterface::CreateTexture(const void* Bgra, const FIntPoint Size)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	// UI textures are authored at their display size and atlased; filtering and mips would
	// only soften them.
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->NeverStream = true;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Bgra, static_cast<SIZE_T>(Size.X) * Size.Y * 4);
	Mip.BulkData.Unlock();

	Texture->UpdateResource();
	return Texture;
}

Rml::TextureHandle FFolioRenderInterface::RegisterTexture(UTexture2D* Texture, const FIntPoint Size)
{
	if (!Texture)
	{
		return 0;
	}

	TUniquePtr<FTexture> Entry = MakeUnique<FTexture>();
	Entry->Texture.Reset(Texture);
	Entry->Brush.SetResourceObject(Texture);
	Entry->Brush.ImageSize = FVector2f(Size.X, Size.Y);
	Entry->Brush.DrawAs = ESlateBrushDrawType::Image;

	if (FSlateApplication::IsInitialized())
	{
		Entry->Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Entry->Brush);
	}

	const Rml::TextureHandle Handle = NextHandle++;
	Textures.Add(Handle, MoveTemp(Entry));
	return Handle;
}

Rml::TextureHandle FFolioRenderInterface::LoadTexture(Rml::Vector2i& OutDimensions, const Rml::String& Source)
{
	Rml::FileInterface* Files = Rml::GetFileInterface();
	const Rml::FileHandle File = Files ? Files->Open(Source) : Rml::FileHandle();
	if (!File)
	{
		UE_LOG(LogFolio, Warning, TEXT("could not open texture '%s'"), *Folio::ToFString(Source));
		return 0;
	}

	const size_t Length = Files->Length(File);
	TArray<uint8> Encoded;
	Encoded.SetNumUninitialized(static_cast<int32>(Length));
	Files->Read(Encoded.GetData(), Length, File);
	Files->Close(File);

	// RmlUi's own loader reads uncompressed TGA and nothing else, which no real atlas is.
	// The engine's image wrappers cover PNG, TGA, JPEG and the rest, so the format question
	// stops being ours.
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	const EImageFormat Format = ImageWrapperModule.DetectImageFormat(Encoded.GetData(), Encoded.Num());
	const TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);

	TArray64<uint8> Raw;
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Encoded.GetData(), Encoded.Num()) ||
		!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		UE_LOG(LogFolio, Warning, TEXT("could not decode texture '%s'"), *Folio::ToFString(Source));
		return 0;
	}

	const FIntPoint Size(Wrapper->GetWidth(), Wrapper->GetHeight());

	// Everything downstream assumes premultiplied alpha; decoded images are straight.
	for (int64 Index = 0; Index + 3 < Raw.Num(); Index += 4)
	{
		const uint32 Alpha = Raw[Index + 3];
		Raw[Index + 0] = static_cast<uint8>((Raw[Index + 0] * Alpha + 127) / 255);
		Raw[Index + 1] = static_cast<uint8>((Raw[Index + 1] * Alpha + 127) / 255);
		Raw[Index + 2] = static_cast<uint8>((Raw[Index + 2] * Alpha + 127) / 255);
	}

	OutDimensions = Rml::Vector2i(Size.X, Size.Y);
	return RegisterTexture(CreateTexture(Raw.GetData(), Size), Size);
}

Rml::TextureHandle FFolioRenderInterface::GenerateTexture(const Rml::Span<const Rml::byte> Source,
	const Rml::Vector2i Dimensions)
{
	const FIntPoint Size(Dimensions.x, Dimensions.y);

	// RmlUi generates RGBA and already premultiplied; the engine wants BGRA.
	TArray<uint8> Bgra;
	Bgra.SetNumUninitialized(Size.X * Size.Y * 4);
	for (int32 Index = 0; Index + 3 < Bgra.Num(); Index += 4)
	{
		Bgra[Index + 0] = Source[Index + 2];
		Bgra[Index + 1] = Source[Index + 1];
		Bgra[Index + 2] = Source[Index + 0];
		Bgra[Index + 3] = Source[Index + 3];
	}

	return RegisterTexture(CreateTexture(Bgra.GetData(), Size), Size);
}

void FFolioRenderInterface::ReleaseTexture(const Rml::TextureHandle Handle)
{
	Textures.Remove(Handle);
}

//
// Clipping and transforms
//

void FFolioRenderInterface::EnableScissorRegion(const bool bEnable)
{
	bScissorEnabled = bEnable;
}

void FFolioRenderInterface::SetScissorRegion(const Rml::Rectanglei Region)
{
	ScissorRegion = Region;
}

void FFolioRenderInterface::ApplyClipping()
{
	if (!ElementList)
	{
		return;
	}

	if (bClipPushed)
	{
		ElementList->PopClip();
		bClipPushed = false;
	}

	if (!bScissorEnabled)
	{
		return;
	}

	// RmlUi's scissor is in window coordinates with a top-left origin and is deliberately
	// unaffected by SetTransform, so it composes with the widget's geometry only.
	const FVector2f Position(static_cast<float>(ScissorRegion.Left()), static_cast<float>(ScissorRegion.Top()));
	const FVector2f Size(static_cast<float>(ScissorRegion.Width()), static_cast<float>(ScissorRegion.Height()));

	const FSlateRenderTransform& ToWindow = Geometry.GetAccumulatedRenderTransform();
	const FVector2f TopLeft(TransformPoint(ToWindow, Position));
	const FVector2f BottomRight(TransformPoint(ToWindow, Position + Size));

	ElementList->PushClip(FSlateClippingZone(FSlateRect(TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y)));
	bClipPushed = true;
}

void FFolioRenderInterface::SetTransform(const Rml::Matrix4f* Transform)
{
	if (!Transform)
	{
		bHasTransform = false;
		RmlTransform = FSlateRenderTransform();
		return;
	}

	// Slate's render transform is 2D. RCSS 3D transforms are out of scope by decision, so the
	// upper-left 2x2 plus the translation column is the whole of what can be expressed; a
	// perspective or rotateX would silently flatten, which is why they are banned in the
	// conventions rather than merely discouraged.
	const Rml::Matrix4f& M = *Transform;
	const FMatrix2x2 Linear(
		M[0][0], M[0][1],
		M[1][0], M[1][1]);
	const FVector2f Offset(M[3][0], M[3][1]);

	RmlTransform = FSlateRenderTransform(Linear, Offset);
	bHasTransform = true;
}
