#pragma once

#include "FolioGradient.h"

#include "FolioCore/FolioShaderEffects.h"
#include "FolioDrawList.h"
#include "FolioLayerStack.h"
#include "FolioRmlUi.h"

#include "FolioCore/MsdfTextureSink.h"

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
class FFolioRenderInterface final : public Rml::RenderInterface, public Folio::IMsdfTextureSink
{
public:
	FFolioRenderInterface();
	virtual ~FFolioRenderInterface() override;

	/** Installs the Slate state that the following RmlUi render calls draw into. */
	void BeginFrame(FSlateWindowElementList& InElementList, const FGeometry& InGeometry, int32 InLayerId,
		FIntPoint InContextSize);

	/**
	 * Records the following render calls into a draw list instead of drawing them.
	 *
	 * What a world-space document needs: the same geometry, in the same neutral form the layer path
	 * already replays with RHI, but with no Slate to emit into and no screen to emit onto. The transform
	 * and the destination are the recorder's business, not this interface's.
	 *
	 * Layer effects are unavailable while recording. A layer is an offscreen target composited back into
	 * whatever is below it, and there is nothing below a recording - so box-shadow, filter and
	 * backdrop-filter are dropped rather than silently producing something wrong. That is the trade the
	 * direct paths make, and it is why a document needing them uses a render target instead.
	 */
	void BeginRecording(FFolioDrawList& InDrawList, FIntPoint InContextSize);

	void EndFrame();

	/** True while render calls are being recorded rather than drawn. */
	bool IsRecording() const { return DrawList != nullptr; }

	/** Highest Slate layer used this frame, so the caller can continue above the document. */
	int32 GetMaxLayerId() const { return MaxLayerId; }

	/**
	 * Drops cached textures whose file has changed since it was read, so a re-baked atlas or a
	 * repainted sprite appears without restarting the editor.
	 *
	 * RmlUi caches a texture by its source path and never looks at the file again, which is
	 * right for a game and wrong for authoring. Releasing through RmlUi rather than dropping our
	 * own entry is what matters: the document's decorators hold Rml::Texture references, and
	 * those re-request a handle on next use only if RmlUi's own cache let go of it.
	 *
	 * @param bForce Release everything, changed or not.
	 * @return How many were released.
	 */
	int32 ReloadChangedTextures(bool bForce = false);

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
	//
	// Distance-field atlases.
	//
	// The font engine builds its pages through here and hands the resulting handles to RmlUi, so
	// a page arrives back at RenderGeometry looking like any other texture. Recognising it is what
	// selects the field material without RmlUi having to know the difference.
	//
	virtual Folio::FMsdfHandle CreateAtlasPage(Folio::FMsdfPoint Size, const Folio::FMsdfAtlasParams& Params) override;
	virtual void UploadAtlasRegion(Folio::FMsdfHandle Page, Folio::FMsdfRect Region, const unsigned char* Rgba, int Stride) override;
	virtual Folio::FMsdfHandle CreateEffectAlias(Folio::FMsdfHandle Page, const Folio::FMsdfEffectParams& Effect) override;
	virtual void ReleaseAtlasHandle(Folio::FMsdfHandle Handle) override;

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
	// Slate has no offscreen target reachable from MakeCustomVerts, so a layer is a render target
	// of our own that draws are recorded into and replayed with RHI. That is what box-shadow,
	// filter and mask-image read back.
	//
	// backdrop-filter is the exception and cannot work that way: what it reads is the game, the
	// editor viewport and any UMG beneath us, none of which we own. It goes through Slate's
	// post-process element instead, which blurs the backbuffer in place.
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
		/** Only set for a saved layer, which has to stay a render target. */
		TStrongObjectPtr<UTexture> Texture;
		FSlateBrush Brush;
		FSlateResourceHandle Handle;
		/** Set instead of Texture for pixels we generated; owns its Slate resource. */
		TSharedPtr<struct FSlateDynamicImageBrush> DynamicBrush;
	};

	FTexture* FindTexture(Rml::TextureHandle Handle);
	Rml::TextureHandle RegisterTexture(UTexture* Texture, FIntPoint Size);

	/** Builds a brush over a Slate dynamic image resource, avoiding the UTexture path entirely. */
	static TSharedPtr<struct FSlateDynamicImageBrush> MakeDynamicBrush(const FString& Name, const void* Bgra,
		FIntPoint Size);

	/** Registers decoded bytes as a texture without creating a UObject for them. */
	Rml::TextureHandle RegisterPixels(const void* Bgra, FIntPoint Size);

	/** Registers a baked sheet as a field page if it has a sidecar. Zero if it is ordinary art. */
	Rml::TextureHandle LoadFieldAtlas(const Rml::String& Source, const TArray64<uint8>& Bgra, FIntPoint Size);

	/**
	 * Field pages that came from a file rather than from the font engine.
	 *
	 * A glyph atlas is owned by the font engine and released through ReleaseAtlasHandle. A baked
	 * vector sheet arrives through LoadTexture, so RmlUi owns it and releases it through
	 * ReleaseTexture - which has to know to look somewhere other than the ordinary texture map.
	 */
	TSet<Rml::TextureHandle> FieldAtlasesFromFile;

	/** The texture for a fill pattern path, loaded once. Null when it could not be used. */
	UTexture* ResolveFillTexture(const std::string& Path);
	TMap<FString, TStrongObjectPtr<UTexture>> FillTextures;

	/** Every source LoadTexture has read, against the file's timestamp when it read it. */
	TMap<FString, FDateTime> LoadedTextureTimes;

	/**
	 * When a texture source was last written, or a zero time if it cannot be told.
	 *
	 * A field atlas is two files, so the sidecar's time counts as the atlas's: re-baking writes
	 * both, and reading only the image would miss a change to the field parameters.
	 */
	static FDateTime TextureFileTime(const Rml::String& Source);

	/** Hands a saved layer to RmlUi as an ordinary texture. */
	Rml::TextureHandle RegisterRenderTarget(class UTextureRenderTarget2D* Target);

	/**
	 * Draws the bottom layer into Slate, which is what sits below it.
	 *
	 * RmlUi's stack always has one more layer than the backend pushes: the one it composites the
	 * first PushLayer into is the screen. Without this a filter at the top of a document resolves
	 * correctly and is then dropped, so the element and everything inside it renders as nothing.
	 */
	void CompositeLayerToSlate(float BlurSigma);

	/** backdrop-filter: blur, through Slate's post-process element, which reads the backbuffer. */
	void BlurBackdrop(float Sigma, bool bOutside);

	/** Carried between the two composites backdrop-filter issues, so the clipped one does the work. */
	float PendingBackdropSigma = 0.0f;
	/** Set when that pending blur is outside-blur, whose region is the viewport minus the element. */
	bool bPendingBackdropOutside = false;

	/** A resolved layer waiting for Slate to read it, which it does at the end of the frame. */
	struct FLayerComposite
	{
		TStrongObjectPtr<class UTextureRenderTarget2D> Target;
		FSlateBrush Brush;
		FSlateResourceHandle Handle;
	};

	/** Held for the frame, because Slate reads the textures after the last draw call is issued. */
	TArray<FLayerComposite> LayerComposites;

	/** The 1x1 white texture itself, for the layer path, which draws without Slate. */
	UTexture2D* GetWhiteTexture();

	/** Builds a texture from premultiplied BGRA bytes. */
	static UTexture2D* CreateTexture(const void* Bgra, FIntPoint Size);

	void ApplyClipping();

	/** Draws compiled geometry with an explicit resource, optionally renormalising UVs. */
	void DrawGeometry(const FCompiledGeometry& Compiled, Rml::Vector2f Translation,
		const FSlateResourceHandle& Resource, const FVector2f UvScale, bool bStraightAlpha = false);

	/** The texture for a gradient at this size, generated on first use and cached. */
	/** The baked gradient for a box size. Returns the record rather than the Slate handle alone, because
	 *  the recording path needs the UTexture and the Slate path needs the handle. */
	const FTexture* GetGradientTexture(Rml::CompiledShaderHandle Shader, FVector2f BoxSize);

	Rml::TextureHandle GetTransparentTexture();

	struct FShader
	{
		Folio::FGradient Gradient;
		/** Set instead of the gradient for `decorator: shader(...)`. */
		Folio::FShaderEffect Effect;
		/** The element box, in pixels, as RmlUi reported it when the shader was compiled. */
		FIntPoint Dimensions = FIntPoint(1, 1);
		/** One baked texture per box size the gradient is asked for. */
		TMap<FIntPoint, Rml::TextureHandle> Baked;
	};

	/** The texture for a shader effect, generated on first use. */
	const FTexture* GetEffectTexture(Rml::CompiledShaderHandle Shader);

	/**
	 * One distance-field page, and the material instance that resolves it.
	 *
	 * The instance carries the page's texture and its field parameters, so a draw is a resource
	 * handle swap rather than a different code path. Effect aliases share the page and differ only
	 * in the parameters, which is why the texture is shared rather than owned.
	 */
	struct FMsdfPage
	{
		TStrongObjectPtr<UTexture2D> Texture;
		Folio::FMsdfAtlasParams Params;
	};

	struct FMsdfBinding
	{
		TSharedPtr<FMsdfPage> Page;
		Folio::FMsdfEffectParams Effect;
		TStrongObjectPtr<class UMaterialInstanceDynamic> Material;
		FSlateBrush Brush;
		FSlateResourceHandle Handle;
	};

	/** The binding for a handle, with no material built. Null if the handle is not a field page. */
	FMsdfBinding* FindMsdfBinding(Rml::TextureHandle Handle);

	/** Builds the material instance for a binding on first draw, so a page that is never drawn
	    costs nothing. Null if the material asset is missing. */
	FMsdfBinding* ResolveMsdfBinding(Rml::TextureHandle Handle);

	/** Packs a binding into the layer shader's MsdfField and MsdfEffect. */
	static void PackMsdfParameters(const FMsdfBinding& Binding, FVector4f& OutField, FVector4f& OutEffect);

	TMap<Rml::TextureHandle, TUniquePtr<FMsdfBinding>> MsdfBindings;
	/** Loaded once on the first field page. */
	TStrongObjectPtr<class UMaterialInterface> MsdfMaterial;
	bool bMsdfMaterialChecked = false;

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
		/** Folio's outside-blur: the element's box is a hole, not the region blurred. */
		bool bOutside = false;
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

	TSharedPtr<struct FSlateDynamicImageBrush> WhiteBrush;
	/** The layer path draws with RHI and needs a real texture, not a Slate resource. */
	TStrongObjectPtr<UTexture2D> WhiteTexture;
	FSlateResourceHandle WhiteHandle;

	FSlateWindowElementList* ElementList = nullptr;

	/**
	 * Where render calls go when there is no Slate to draw into.
	 *
	 * Non-null exactly while recording, which is what every draw path tests to decide where it belongs.
	 */
	FFolioDrawList* DrawList = nullptr;
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
