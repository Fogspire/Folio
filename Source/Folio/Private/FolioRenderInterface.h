#pragma once

#include "FolioGradient.h"
#include "FolioLayerStack.h"
#include "FolioRmlUi.h"

#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"

class UTexture2D;

/**
 * RmlUi's renderer, expressed as Slate draw elements.
 *
 * RmlUi emits a vertex array, an index array and a texture handle, which maps almost exactly
 * onto FSlateDrawElement::MakeCustomVerts. Going through Slate rather than raw RHI means the
 * UI composites correctly with everything else Slate draws, needs no fullscreen render target,
 * and keeps text crisp.
 *
 * The interface is stateful in a way Slate is not: RmlUi calls it during its own render pass
 * and expects scissor, transform and layer state to persist between calls. BeginFrame installs
 * the element list and geometry that the following calls draw into, and EndFrame clears them.
 */
class FFolioRenderInterface final : public Rml::RenderInterface
{
public:
	FFolioRenderInterface();
	virtual ~FFolioRenderInterface() override;

	/** Installs the Slate state that the following RmlUi render calls draw into. */
	void BeginFrame(FSlateWindowElementList& InElementList, const FGeometry& InGeometry, int32 InLayerId,
		FIntPoint InContextSize);
	void EndFrame();

	/** Highest Slate layer used this frame, so the caller can continue above the document. */
	int32 GetMaxLayerId() const { return MaxLayerId; }

	//
	// Geometry.
	//
	// Compiled geometry is not optional in 6.x and is the whole performance argument over
	// UMG: RmlUi tessellates once and hands back a handle, and re-uploading that every frame
	// throws the advantage away. Vertex and index data stay valid until ReleaseGeometry, so
	// the conversion into Slate's vertex layout is done once here rather than per frame.
	//
	virtual Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> Vertices,
		Rml::Span<const int> Indices) override;
	virtual void RenderGeometry(Rml::CompiledGeometryHandle Handle, Rml::Vector2f Translation,
		Rml::TextureHandle Texture) override;
	virtual void ReleaseGeometry(Rml::CompiledGeometryHandle Handle) override;

	//
	// Textures.
	//
	virtual Rml::TextureHandle LoadTexture(Rml::Vector2i& OutDimensions, const Rml::String& Source) override;
	virtual Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i Dimensions) override;
	virtual void ReleaseTexture(Rml::TextureHandle Handle) override;

	//
	// Clipping and transforms.
	//
	virtual void EnableScissorRegion(bool bEnable) override;
	virtual void SetScissorRegion(Rml::Rectanglei Region) override;
	virtual void SetTransform(const Rml::Matrix4f* Transform) override;

	//
	// Clip masks.
	//
	// Slate clips with a quad rather than a stencil, so a mask is reduced to the bounds of
	// the geometry that defined it. That is exact for the rectangles this is used for, and an
	// over-estimate for a rounded one: a border-radius corner clips square. Recorded in
	// NOTES.md rather than hidden, because it is visible on a rotated rounded element.
	//
	virtual void EnableClipMask(bool bEnable) override;
	virtual void RenderToClipMask(Rml::ClipMaskOperation Operation, Rml::CompiledGeometryHandle Geometry,
		Rml::Vector2f Translation) override;

	//
	// Layers.
	//
	// Slate has no offscreen target reachable from MakeCustomVerts, so a layer is not a real
	// framebuffer: pushing one keeps drawing into the same element list, and compositing is a
	// no-op. Everything that only needs draw ordering is therefore correct, and everything
	// that needs to read the layer back - box-shadow, filter, backdrop-filter, mask-image -
	// is not. SaveLayerAsTexture returns a fully transparent texture so those degrade to
	// nothing rather than to a solid rectangle.
	//
	virtual Rml::LayerHandle PushLayer() override;
	virtual void CompositeLayers(Rml::LayerHandle Source, Rml::LayerHandle Destination, Rml::BlendMode BlendMode,
		Rml::Span<const Rml::CompiledFilterHandle> Filters) override;
	virtual void PopLayer() override;
	virtual Rml::TextureHandle SaveLayerAsTexture() override;

	virtual Rml::CompiledFilterHandle CompileFilter(const Rml::String& Name,
		const Rml::Dictionary& Parameters) override;
	virtual void ReleaseFilter(Rml::CompiledFilterHandle Filter) override;

	//
	// Shaders: the gradient decorators, and nothing else so far.
	//
	virtual Rml::CompiledShaderHandle CompileShader(const Rml::String& Name,
		const Rml::Dictionary& Parameters) override;
	virtual void RenderShader(Rml::CompiledShaderHandle Shader, Rml::CompiledGeometryHandle Geometry,
		Rml::Vector2f Translation, Rml::TextureHandle Texture) override;
	virtual void ReleaseShader(Rml::CompiledShaderHandle Shader) override;

private:
	struct FCompiledGeometry
	{
		/** Positions are RmlUi-local; the render transform is applied per draw. */
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
	};

	struct FTexture
	{
		/** UTexture rather than UTexture2D: a saved layer is a render target. */
		TStrongObjectPtr<UTexture> Texture;
		FSlateBrush Brush;
		FSlateResourceHandle Handle;
	};

	FTexture* FindTexture(Rml::TextureHandle Handle);
	Rml::TextureHandle RegisterTexture(UTexture* Texture, FIntPoint Size);

	/** Hands a saved layer to RmlUi as an ordinary texture. */
	Rml::TextureHandle RegisterRenderTarget(class UTextureRenderTarget2D* Target);

	/** The 1x1 white texture itself, for the layer path, which draws without Slate. */
	UTexture2D* GetWhiteTexture();

	/** Builds a texture from premultiplied BGRA bytes. */
	static UTexture2D* CreateTexture(const void* Bgra, FIntPoint Size);

	void ApplyClipping();

	/** Draws compiled geometry with an explicit resource, optionally renormalising UVs. */
	void DrawGeometry(const FCompiledGeometry& Compiled, Rml::Vector2f Translation,
		const FSlateResourceHandle& Resource, const FVector2f UvScale);

	/** The texture for a gradient at this size, generated on first use and cached. */
	const FSlateResourceHandle* GetGradientResource(Rml::CompiledShaderHandle Shader, FVector2f BoxSize);

	Rml::TextureHandle GetTransparentTexture();

	struct FShader
	{
		Folio::FGradient Gradient;
		/** One baked texture per box size the gradient is asked for. */
		TMap<FIntPoint, Rml::TextureHandle> Baked;
	};

	TMap<Rml::CompiledGeometryHandle, TUniquePtr<FCompiledGeometry>> Geometries;
	TMap<Rml::TextureHandle, TUniquePtr<FTexture>> Textures;
	TMap<Rml::CompiledShaderHandle, TUniquePtr<FShader>> Shaders;

	/** Scissor state saved and restored around a clip mask, which shares the clip stack. */
	TOptional<FSlateRect> ClipMaskRect;

	/** Only the blur is implemented; the rest are recorded so the handles stay paired. */
	struct FFilter
	{
		Rml::String Name;
		float Sigma = 0.0f;
	};

	TMap<Rml::CompiledFilterHandle, FFilter> Filters;

	FFolioLayerStack Layers;

	/**
	 * The size a layer is allocated at, and the origin its contents are offset by.
	 *
	 * RmlUi renders into a layer in document coordinates, and the layer only covers the
	 * scissor region, so geometry has to be shifted by the region's top-left to land inside
	 * the target.
	 */
	FIntPoint LayerOrigin = FIntPoint::ZeroValue;

	int32 LayerDepth = 0;
	Rml::TextureHandle TransparentTexture = 0;

	/** Reused every draw call so a frame of UI does not allocate per element. */
	TArray<FSlateVertex> ScratchVertices;

	/**
	 * A 1x1 white texture, for the geometry RmlUi draws with no texture of its own, which is
	 * most of a document: backgrounds, borders and gradients all arrive untextured.
	 */
	const FSlateResourceHandle& GetWhiteHandle();

	TStrongObjectPtr<UTexture2D> WhiteTexture;
	FSlateBrush WhiteBrush;
	FSlateResourceHandle WhiteHandle;

	FSlateWindowElementList* ElementList = nullptr;
	FGeometry Geometry;

	/** Needed to size a layer when nothing has set a scissor region. */
	FIntPoint ContextSize = FIntPoint(1, 1);
	int32 LayerId = 0;
	int32 MaxLayerId = 0;

	bool bScissorEnabled = false;
	bool bClipPushed = false;
	Rml::Rectanglei ScissorRegion;

	/** RmlUi's current transform, already reduced to the 2D affine part Slate can express. */
	FSlateRenderTransform RmlTransform;
	bool bHasTransform = false;

	uint64 NextHandle = 1;
};
