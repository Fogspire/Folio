#include "FolioRenderInterface.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FolioBlurShader.h"
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
	const int32 InLayerId, const FIntPoint InContextSize)
{
	ElementList = &InElementList;
	Geometry = InGeometry;
	LayerId = InLayerId;
	ContextSize = InContextSize;
	MaxLayerId = InLayerId;

	bScissorEnabled = false;
	bClipPushed = false;
	bHasTransform = false;
	ClipMaskRect.Reset();
	LayerDepth = 0;
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

	// A document that fails mid-pass can leave a layer open, which would otherwise send the
	// next frame's geometry into an orphaned render target instead of to the screen.
	while (!Layers.IsEmpty())
	{
		Layers.Pop();
	}
	LayerDepth = 0;

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

	const FTexture* Bound = FindTexture(Texture);

	// While a layer is open every draw belongs to it, not to Slate: the layer is what
	// box-shadow and the filters read back, and geometry that leaked past it would appear
	// twice, once inside the effect and once beside it.
	if (!Layers.IsEmpty())
	{
		UTexture* LayerTexture = Bound && Bound->Texture.IsValid()
			? static_cast<UTexture*>(Bound->Texture.Get())
			: GetWhiteTexture();

		Layers.DrawTriangles(Compiled.Vertices, Compiled.Indices, LayerTexture,
			FVector2f(Translation.x - LayerOrigin.X, Translation.y - LayerOrigin.Y));
		return;
	}

	const FSlateResourceHandle& Resource = Bound ? Bound->Handle : GetWhiteHandle();

	ApplyClipping();
	DrawGeometry(Compiled, Translation, Resource, FVector2f(1.0f, 1.0f));
}

void FFolioRenderInterface::DrawGeometry(const FCompiledGeometry& Compiled, const Rml::Vector2f Translation,
	const FSlateResourceHandle& Resource, const FVector2f UvScale)
{
	// Slate dereferences the resource handle's proxy without checking it, so an invalid one
	// is a crash rather than a missing texture. Untextured geometry is most of a document -
	// every background and border arrives with none - so this is not an edge case.
	if (!ElementList || !Resource.IsValid())
	{
		return;
	}

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

		// A shader's geometry carries pixel offsets inside the element box rather than UVs,
		// so the gradient path normalises them against the box it baked the texture for.
		Out.TexCoords[0] = Source.TexCoords[0] * UvScale.X;
		Out.TexCoords[1] = Source.TexCoords[1] * UvScale.Y;
		Out.MaterialTexCoords = FVector2f(Out.TexCoords[0], Out.TexCoords[1]);
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

UTexture2D* FFolioRenderInterface::GetWhiteTexture()
{
	// Shares the lazy construction with the Slate path: the handle is what needs Slate, the
	// texture itself does not.
	GetWhiteHandle();
	return WhiteTexture.Get();
}

Rml::TextureHandle FFolioRenderInterface::RegisterRenderTarget(UTextureRenderTarget2D* Target)
{
	if (!Target)
	{
		return 0;
	}

	return RegisterTexture(Target, FIntPoint(Target->SizeX, Target->SizeY));
}

Rml::TextureHandle FFolioRenderInterface::RegisterTexture(UTexture* Texture, const FIntPoint Size)
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

	const FSlateRenderTransform& ToWindow = Geometry.GetAccumulatedRenderTransform();

	TOptional<FSlateRect> Clip;

	if (bScissorEnabled)
	{
		// RmlUi's scissor is in window coordinates with a top-left origin and is deliberately
		// unaffected by SetTransform, so it composes with the widget's geometry only.
		const FVector2f Position(static_cast<float>(ScissorRegion.Left()), static_cast<float>(ScissorRegion.Top()));
		const FVector2f Size(static_cast<float>(ScissorRegion.Width()), static_cast<float>(ScissorRegion.Height()));

		const FVector2f TopLeft(TransformPoint(ToWindow, Position));
		const FVector2f BottomRight(TransformPoint(ToWindow, Position + Size));
		Clip = FSlateRect(TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y);
	}

	if (ClipMaskRect.IsSet())
	{
		// The mask is in document space and the scissor is already in window space, so the
		// mask is transformed and then intersected: both are clips, and Slate has one stack.
		const FVector2f TopLeft(TransformPoint(ToWindow, FVector2f(ClipMaskRect->Left, ClipMaskRect->Top)));
		const FVector2f BottomRight(TransformPoint(ToWindow, FVector2f(ClipMaskRect->Right, ClipMaskRect->Bottom)));
		const FSlateRect MaskInWindow(TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y);

		Clip = Clip.IsSet() ? Clip.GetValue().IntersectionWith(MaskInWindow) : MaskInWindow;
	}

	if (!Clip.IsSet())
	{
		return;
	}

	ElementList->PushClip(FSlateClippingZone(Clip.GetValue()));
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

//
// Clip masks
//

void FFolioRenderInterface::EnableClipMask(const bool bEnable)
{
	if (!bEnable)
	{
		ClipMaskRect.Reset();
	}
}

void FFolioRenderInterface::RenderToClipMask(const Rml::ClipMaskOperation Operation,
	const Rml::CompiledGeometryHandle GeometryHandle, const Rml::Vector2f Translation)
{
	const TUniquePtr<FCompiledGeometry>* Found = Geometries.Find(GeometryHandle);
	if (!Found || !Found->IsValid() || (*Found)->Vertices.IsEmpty())
	{
		return;
	}

	// Slate clips with a quad, not a stencil, so the mask becomes the bounds of the geometry
	// that drew it. Exact for a rectangle; a rounded rectangle clips to its square corners.
	FVector2f Minimum((*Found)->Vertices[0].Position);
	FVector2f Maximum = Minimum;
	for (const FSlateVertex& Vertex : (*Found)->Vertices)
	{
		Minimum = FVector2f::Min(Minimum, FVector2f(Vertex.Position));
		Maximum = FVector2f::Max(Maximum, FVector2f(Vertex.Position));
	}

	const FVector2f Offset(Translation.x, Translation.y);
	const FSlateRect Bounds(Minimum.X + Offset.X, Minimum.Y + Offset.Y, Maximum.X + Offset.X, Maximum.Y + Offset.Y);

	switch (Operation)
	{
	case Rml::ClipMaskOperation::Set:
	case Rml::ClipMaskOperation::SetInverse:
		// An inverse mask cannot be expressed as a quad at all. Setting the bounds rather
		// than inverting them keeps the element visible, which is the lesser wrong.
		ClipMaskRect = Bounds;
		break;

	case Rml::ClipMaskOperation::Intersect:
		ClipMaskRect = ClipMaskRect.IsSet() ? ClipMaskRect.GetValue().IntersectionWith(Bounds) : Bounds;
		break;
	}
}

//
// Layers
//

Rml::LayerHandle FFolioRenderInterface::PushLayer()
{
	// A layer covers the current scissor region, which is what RmlUi has already clamped to
	// the window. Allocating the whole context instead would make a small shadow cost a
	// full-screen target.
	FIntPoint Size = ContextSize;
	FIntPoint Origin = FIntPoint::ZeroValue;

	if (bScissorEnabled && ScissorRegion.Width() > 0 && ScissorRegion.Height() > 0)
	{
		Size = FIntPoint(ScissorRegion.Width(), ScissorRegion.Height());
		Origin = FIntPoint(ScissorRegion.Left(), ScissorRegion.Top());
	}

	if (Layers.IsEmpty())
	{
		LayerOrigin = Origin;
	}

	if (!Layers.Push(Size))
	{
		UE_LOG(LogFolio, Warning, TEXT("could not allocate a %dx%d layer"), Size.X, Size.Y);
	}

	return static_cast<Rml::LayerHandle>(++LayerDepth);
}

void FFolioRenderInterface::CompositeLayers(Rml::LayerHandle, Rml::LayerHandle, const Rml::BlendMode BlendMode,
	const Rml::Span<const Rml::CompiledFilterHandle> AppliedFilters)
{
	UTextureRenderTarget2D* Source = Layers.GetCurrent();
	if (!Source)
	{
		return;
	}

	// Filters apply to the layer before it is composited, which is the order that makes a
	// blurred shadow a blurred shadow rather than a blurred everything.
	for (const Rml::CompiledFilterHandle Handle : AppliedFilters)
	{
		if (const FFilter* Filter = Filters.Find(Handle); Filter && Filter->Sigma > 0.0f)
		{
			Folio::BlurRenderTarget(Source, Filter->Sigma);
		}
	}

	// The destination is the layer below, and Blend is the only mode RmlUi asks for here that
	// a canvas tile can express directly.
	Layers.CompositeDown(BlendMode == Rml::BlendMode::Blend);
}

void FFolioRenderInterface::PopLayer()
{
	Layers.Pop();
	LayerDepth = FMath::Max(0, LayerDepth - 1);
}

Rml::TextureHandle FFolioRenderInterface::GetTransparentTexture()
{
	if (TransparentTexture != 0)
	{
		return TransparentTexture;
	}

	const uint8 Clear[4] = { 0, 0, 0, 0 };
	TransparentTexture = RegisterTexture(CreateTexture(Clear, FIntPoint(1, 1)), FIntPoint(1, 1));
	return TransparentTexture;
}

Rml::TextureHandle FFolioRenderInterface::SaveLayerAsTexture()
{
	UTextureRenderTarget2D* Detached = Layers.DetachCurrent();
	if (!Detached)
	{
		// No layer to read back. Transparent rather than zero: zero also means "no texture"
		// on the geometry path, so the caller's quad would paint white over its own element.
		return GetTransparentTexture();
	}

	return RegisterRenderTarget(Detached);
}

Rml::CompiledFilterHandle FFolioRenderInterface::CompileFilter(const Rml::String& Name,
	const Rml::Dictionary& Parameters)
{
	FFilter Filter;
	Filter.Name = Name;

	if (Name == "blur")
	{
		// RmlUi's blur radius is a standard deviation already, in pixels.
		const auto It = Parameters.find("sigma");
		if (It != Parameters.end())
		{
			It->second.GetInto(Filter.Sigma);
		}
	}
	else
	{
		// std::string has no GetTypeHash, so the set is of FString.
		static TSet<FString> Reported;
		const FString Reported_Name = Folio::ToFString(Name);
		if (!Reported.Contains(Reported_Name))
		{
			Reported.Add(Reported_Name);
			UE_LOG(LogFolio, Warning, TEXT("filter '%s' is not implemented and renders as nothing"),
				*Reported_Name);
		}
	}

	const Rml::CompiledFilterHandle Handle = NextHandle++;
	Filters.Add(Handle, MoveTemp(Filter));
	return Handle;
}

void FFolioRenderInterface::ReleaseFilter(const Rml::CompiledFilterHandle Filter)
{
	Filters.Remove(Filter);
}

//
// Shaders
//

Rml::CompiledShaderHandle FFolioRenderInterface::CompileShader(const Rml::String& Name,
	const Rml::Dictionary& Parameters)
{
	Folio::FGradient Gradient;
	if (!Folio::ParseGradient(Name, Parameters, Gradient))
	{
		UE_LOG(LogFolio, Warning, TEXT("unsupported shader '%s'"), *Folio::ToFString(Name));
		return 0;
	}

	TUniquePtr<FShader> Shader = MakeUnique<FShader>();
	Shader->Gradient = MoveTemp(Gradient);

	const Rml::CompiledShaderHandle Handle = NextHandle++;
	Shaders.Add(Handle, MoveTemp(Shader));
	return Handle;
}

const FSlateResourceHandle* FFolioRenderInterface::GetGradientResource(const Rml::CompiledShaderHandle Handle,
	const FVector2f BoxSize)
{
	const TUniquePtr<FShader>* Found = Shaders.Find(Handle);
	if (!Found || !Found->IsValid())
	{
		return nullptr;
	}

	FShader& Shader = **Found;

	// A gradient is smooth, so the texture does not have to match the element pixel for
	// pixel. Capping it keeps a full-screen background from allocating a full-screen texture
	// while staying well inside what bilinear filtering hides.
	static constexpr int32 MaxSize = 256;
	const FIntPoint TextureSize(
		FMath::Clamp(FMath::CeilToInt(BoxSize.X), 1, MaxSize),
		FMath::Clamp(FMath::CeilToInt(BoxSize.Y), 1, MaxSize));

	if (const Rml::TextureHandle* Cached = Shader.Baked.Find(TextureSize))
	{
		const FTexture* Texture = FindTexture(*Cached);
		return Texture ? &Texture->Handle : nullptr;
	}

	TArray<uint8> Bgra;
	Folio::RasterizeGradient(Shader.Gradient, BoxSize, TextureSize, Bgra);

	const Rml::TextureHandle Baked = RegisterTexture(CreateTexture(Bgra.GetData(), TextureSize), TextureSize);
	if (Baked == 0)
	{
		return nullptr;
	}

	Shader.Baked.Add(TextureSize, Baked);

	const FTexture* Texture = FindTexture(Baked);
	return Texture ? &Texture->Handle : nullptr;
}

void FFolioRenderInterface::RenderShader(const Rml::CompiledShaderHandle Shader,
	const Rml::CompiledGeometryHandle GeometryHandle, const Rml::Vector2f Translation, Rml::TextureHandle)
{
	const TUniquePtr<FCompiledGeometry>* Found = Geometries.Find(GeometryHandle);
	if (!ElementList || !Found || !Found->IsValid() || (*Found)->Vertices.IsEmpty())
	{
		return;
	}

	const FCompiledGeometry& Compiled = **Found;

	// The gradient's parameters are expressed in the element box's pixel space, and so are
	// the geometry's texture coordinates. The box size is therefore the extent of those
	// coordinates, not of the positions, which differ once a border radius trims the corners.
	FVector2f Extent(Compiled.Vertices[0].TexCoords[0], Compiled.Vertices[0].TexCoords[1]);
	for (const FSlateVertex& Vertex : Compiled.Vertices)
	{
		Extent = FVector2f::Max(Extent, FVector2f(Vertex.TexCoords[0], Vertex.TexCoords[1]));
	}

	const FVector2f BoxSize(FMath::Max(Extent.X, 1.0f), FMath::Max(Extent.Y, 1.0f));

	const FSlateResourceHandle* Resource = GetGradientResource(Shader, BoxSize);
	if (!Resource)
	{
		return;
	}

	ApplyClipping();
	DrawGeometry(Compiled, Translation, *Resource, FVector2f(1.0f / BoxSize.X, 1.0f / BoxSize.Y));
}

void FFolioRenderInterface::ReleaseShader(const Rml::CompiledShaderHandle Shader)
{
	if (const TUniquePtr<FShader>* Found = Shaders.Find(Shader); Found && Found->IsValid())
	{
		for (const TPair<FIntPoint, Rml::TextureHandle>& Baked : (*Found)->Baked)
		{
			Textures.Remove(Baked.Value);
		}
	}

	Shaders.Remove(Shader);
}
