#pragma once

#include "Components/SceneComponent.h"
#include "FolioDrawList.h"
#include "CoreMinimal.h"

#include "FolioWorldDocument.generated.h"

class FFolioDocumentHost;

/**
 * How a world-space document reaches the screen.
 *
 * The choice is a genuine trade with no right answer, so it belongs to the game rather than to Folio.
 */
UENUM(BlueprintType)
enum class EFolioWorldDrawMode : uint8
{
	/**
	 * Drawn after the tonemapper, where temporal antialiasing can no longer touch it.
	 *
	 * The default, and the reason this exists. TAA and TSR blur and smear world-space UI, worst of all
	 * while the camera moves, and no amount of resolution fixes it. Drawing after the temporal resolve
	 * avoids it outright. Colour also lands post-tonemap, so authored UI colours arrive as authored, and
	 * under an upscaler the target is at display resolution rather than render resolution.
	 *
	 * The price: bloom, depth of field and motion blur never reach it, and the edges are ours to
	 * produce - which is what the distance fields already do analytically.
	 */
	AfterTonemap,

	/**
	 * Drawn into the scene as world-space triangles.
	 *
	 * Sharp at any distance because there is no render target and no resample, and it receives bloom,
	 * depth of field and colour grading like anything else in the world. Temporal antialiasing will
	 * smear it. For a diegetic screen that should sit *in* the world - a grimy monitor lit by the scene.
	 */
	InScene,
};

/**
 * A document on a surface in the world: a terminal, a sign, a screen you can walk up to.
 *
 * Sharpness at any distance is the requirement that decides the design, which is why this is not a
 * `UWidgetComponent`. That renders into a fixed-size render target and samples the quad at whatever
 * size it lands on screen, so every mismatch is a filtered resample. Distance fields make the
 * alternative possible: geometry drawn at the size it is seen, with no intermediate target.
 *
 * This component owns the document and reports where it is. It does not draw: the draw mode decides
 * which path picks it up, and both read the same recorded geometry.
 */
UCLASS(ClassGroup = (Folio), meta = (BlueprintSpawnableComponent, DisplayName = "Folio World Document"))
class FOLIO_API UFolioWorldDocument : public USceneComponent
{
	GENERATED_BODY()

public:
	UFolioWorldDocument();

	/**
	 * Document to load, relative to the project directory.
	 *
	 * A leading /folio/ resolves inside the plugin instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FString DocumentPath;

	/**
	 * The physical size of the surface in world units, before the component's scale.
	 *
	 * The document is laid out to this size times PixelsPerUnit, so a wider surface shows more of the
	 * document rather than the same content stretched.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FVector2D WorldSize = FVector2D(100.0, 60.0);

	/**
	 * Document pixels per world unit, which is what decides how much fits on the surface.
	 *
	 * Not a quality setting: glyphs and vector art are resolution-independent, so this does not affect
	 * sharpness at all. It is the authoring scale. At the default a 100x60 surface is a 1000x600
	 * document, so 16dp text reads at about the size it would on a screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "0.1"))
	float PixelsPerUnit = 10.0f;

	/** Whether temporal antialiasing may touch this document. See the enum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	EFolioWorldDrawMode DrawMode = EFolioWorldDrawMode::AfterTonemap;

	/** Loads a different document, replacing whatever is up. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void LoadDocument(const FString& InDocumentPath);

	/**
	 * Binds a UObject's properties as a named data model, so the document can read them.
	 *
	 * Same contract as the screen-space widget: callable before the component is registered, and the
	 * document's bindings resolve against whatever is bound when it loads.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	bool BindViewModel(FName ModelName, UObject* Source, bool bPollEveryFrame = true);

	/** Says the document changed, for game code that reaches into it directly. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void MarkDirty();

	/** Rebuilds from disk, putting focus and scroll back. What a markup change needs. */
	void ReloadDocument();

	/** Re-reads stylesheets, leaving the DOM, focus, scroll and data models alone. */
	void ReloadStyleSheets();

	/** The document's size in pixels, which is WorldSize times PixelsPerUnit. */
	FIntPoint GetDocumentSize() const;

	/**
	 * The surface in world space: origin at its centre, extents along its local X and Y.
	 *
	 * The document's own coordinates run from the top left, so the plane is expressed the way a
	 * renderer wants it rather than the way the component is authored.
	 */
	void GetSurface(FVector& OutCentre, FVector& OutRight, FVector& OutUp) const;

	FFolioDocumentHost* GetHost() const { return Host.Get(); }

	/**
	 * The document's geometry as of the last update, in document space.
	 *
	 * Recorded rather than drawn, because where it lands depends on which pass picks it up. Null until
	 * the first update produces something.
	 *
	 * Shared and immutable: a render pass reads this for as long as its frame is in flight, which
	 * outlasts the tick that handed it over. Re-recording publishes a new list rather than rewriting
	 * this one, so nothing is ever read while it is being built.
	 */
	TSharedPtr<const FFolioDrawList> GetDrawList() const { return Published; }

	/** Every live world document, so a render pass can find them without walking the world. */
	static const TArray<UFolioWorldDocument*>& GetLive();

	//~ Begin UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** One requested binding, kept so it survives a re-register the way the widget's do. */
	struct FPendingBinding
	{
		FName ModelName;
		TWeakObjectPtr<UObject> Source;
		bool bPollEveryFrame = true;
	};

	void ApplyBindings();

	/** Held by pointer so the host stays private to the module while this header is public. */
	TUniquePtr<FFolioDocumentHost> Host;

	TArray<FPendingBinding> Bindings;

	/**
	 * Re-recorded only when the document actually updated.
	 *
	 * A settled document keeps the recording it already has, which is the same reason the screen path
	 * lets Slate keep its cached draw elements.
	 */
	TSharedPtr<const FFolioDrawList> Published;

	/**
	 * Superseded lists a frame in flight may still be reading, dropped here once nothing else holds one.
	 *
	 * Released on the game thread deliberately: a recorded draw holds a TStrongObjectPtr, and letting the
	 * last reference go on a render worker would touch the garbage collector's reference set from the
	 * wrong thread.
	 */
	TArray<TSharedPtr<const FFolioDrawList>> Retired;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> BoundSources;
};
