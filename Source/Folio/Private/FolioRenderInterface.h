#pragma once

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
	void BeginFrame(FSlateWindowElementList& InElementList, const FGeometry& InGeometry, int32 InLayerId);
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

private:
	struct FCompiledGeometry
	{
		/** Positions are RmlUi-local; the render transform is applied per draw. */
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
	};

	struct FTexture
	{
		TStrongObjectPtr<UTexture2D> Texture;
		FSlateBrush Brush;
		FSlateResourceHandle Handle;
	};

	FTexture* FindTexture(Rml::TextureHandle Handle);
	Rml::TextureHandle RegisterTexture(UTexture2D* Texture, FIntPoint Size);

	/** Builds a texture from premultiplied BGRA bytes. */
	static UTexture2D* CreateTexture(const void* Bgra, FIntPoint Size);

	void ApplyClipping();

	TMap<Rml::CompiledGeometryHandle, TUniquePtr<FCompiledGeometry>> Geometries;
	TMap<Rml::TextureHandle, TUniquePtr<FTexture>> Textures;

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
