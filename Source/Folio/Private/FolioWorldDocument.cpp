#include "FolioWorldDocument.h"

#include "FolioDocumentHost.h"
#include "FolioModule.h"

namespace
{
	TArray<UFolioWorldDocument*> LiveDocuments;
}

const TArray<UFolioWorldDocument*>& UFolioWorldDocument::GetLive()
{
	return LiveDocuments;
}

UFolioWorldDocument::UFolioWorldDocument()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// The document updates itself, which has nothing to do with whether the surface is on screen. A
	// document that stopped updating while looked away from would arrive stale, and the invalidation
	// path already makes an idle one nearly free.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

FIntPoint UFolioWorldDocument::GetDocumentSize() const
{
	return FIntPoint(
		FMath::Max(1, FMath::RoundToInt(WorldSize.X * PixelsPerUnit)),
		FMath::Max(1, FMath::RoundToInt(WorldSize.Y * PixelsPerUnit)));
}

void UFolioWorldDocument::GetSurface(FVector& OutCentre, FVector& OutRight, FVector& OutUp) const
{
	const FTransform& Transform = GetComponentTransform();

	OutCentre = Transform.GetLocation();

	// The surface lies in the component's local XY, facing along local Z, so it faces the way an actor
	// arrow does and can be aimed by pointing the component at the viewer.
	//
	// Half extents rather than full: the document's own space runs from a corner, and a renderer needs
	// the centre and the two axes to get from one to the other.
	OutRight = Transform.GetUnitAxis(EAxis::X) * (WorldSize.X * 0.5) * Transform.GetScale3D().X;
	OutUp = Transform.GetUnitAxis(EAxis::Y) * (WorldSize.Y * 0.5) * Transform.GetScale3D().Y;
}

void UFolioWorldDocument::OnRegister()
{
	Super::OnRegister();

	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		return;
	}

	// A component is registered in the editor as well as in play, and a document that renders in the
	// level viewport is the point of authoring one, so this is not gated on a game world.
	Host = MakeUnique<FFolioDocumentHost>();
	if (!Host->Create(TEXT("world"), GetDocumentSize()))
	{
		Host.Reset();
		return;
	}

	LiveDocuments.AddUnique(this);

	// Bound before the document loads, because a document resolves its bindings as it loads and RmlUi
	// gives no way to attach a model afterwards.
	ApplyBindings();

	if (!DocumentPath.IsEmpty())
	{
		Host->LoadDocument(DocumentPath);
	}
}

void UFolioWorldDocument::OnUnregister()
{
	LiveDocuments.RemoveSingleSwap(this);
	Host.Reset();

	Super::OnUnregister();
}

void UFolioWorldDocument::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Host.IsValid())
	{
		return;
	}

	Host->TickDataModels();

	// A world document has no host scaling to inherit: `dp` resolves against 1, so a dp is a document
	// pixel and PixelsPerUnit is the only thing deciding how large text is on the surface.
	//
	// The render scale is one document pixel expressed in document units, which is 1 here for the same
	// reason. It only affects background antialiasing width, and the fields carry their own edges.
	Host->Update(DeltaTime, 1.0f, 1.0f);
}

void UFolioWorldDocument::LoadDocument(const FString& InDocumentPath)
{
	DocumentPath = InDocumentPath;

	if (Host.IsValid())
	{
		Host->LoadDocument(InDocumentPath);
	}
}

bool UFolioWorldDocument::BindViewModel(const FName ModelName, UObject* Source, const bool bPollEveryFrame)
{
	if (ModelName.IsNone() || !Source)
	{
		return false;
	}

	// Recorded rather than only applied, so a re-register - which moving the component in the editor
	// causes - puts the same models back.
	const int32 Existing = Bindings.IndexOfByPredicate(
		[ModelName](const FPendingBinding& Binding) { return Binding.ModelName == ModelName; });

	FPendingBinding Binding;
	Binding.ModelName = ModelName;
	Binding.Source = Source;
	Binding.bPollEveryFrame = bPollEveryFrame;

	if (Existing != INDEX_NONE)
	{
		Bindings[Existing] = Binding;
	}
	else
	{
		Bindings.Add(Binding);
	}

	// Strong, so a view model referenced nowhere else is not collected out from under the document.
	BoundSources.AddUnique(Source);

	if (!Host.IsValid())
	{
		return true;
	}

	const bool bDocumentAlreadyUp = Host->GetDocument() != nullptr;
	ApplyBindings();

	if (bDocumentAlreadyUp)
	{
		// The document resolved its bindings without this model and RmlUi cannot attach one afterwards,
		// so rebuilding is what makes it visible.
		UE_LOG(LogFolio, Log,
			TEXT("BindViewModel('%s') arrived after '%s' had loaded, so the document is being rebuilt. ")
			TEXT("Bind before the document loads to avoid it."),
			*ModelName.ToString(), *DocumentPath);

		Host->ReloadDocument();
	}

	return true;
}

void UFolioWorldDocument::ApplyBindings()
{
	if (!Host.IsValid())
	{
		return;
	}

	for (const FPendingBinding& Binding : Bindings)
	{
		UObject* Source = Binding.Source.Get();
		if (Source && !Host->HasObjectModel(Binding.ModelName.ToString()))
		{
			Host->BindObjectModel(Binding.ModelName.ToString(), Source, Binding.bPollEveryFrame);
		}
	}
}

void UFolioWorldDocument::MarkDirty()
{
	if (Host.IsValid())
	{
		Host->MarkDirty(TEXT("game code"));
	}
}

#if WITH_EDITOR
void UFolioWorldDocument::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!Host.IsValid())
	{
		return;
	}

	const FName Property = PropertyChangedEvent.GetPropertyName();

	if (Property == GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, DocumentPath))
	{
		Host->LoadDocument(DocumentPath);
	}
	else if (Property == GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, WorldSize)
		|| Property == GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, PixelsPerUnit))
	{
		// Resizing relays out the document rather than scaling it, which is the point of expressing the
		// surface in world units.
		Host->SetSize(GetDocumentSize());
	}
}
#endif
