#include "FolioDocumentHost.h"

#include "FolioDataModel.h"
#include "FolioDocumentPath.h"
#include "FolioInsights.h"
#include "FolioInvalidation.h"
#include "FolioLocalization.h"
#include "FolioModule.h"
#include "FolioObjectModel.h"
#include "FolioReload.h"
#include "FolioRenderInterface.h"
#include "FolioRmlUi.h"

#include "Misc/FileHelper.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/MeshUtilities.h>

namespace
{
	/**
	 * Every live host, and the themes that are on.
	 *
	 * A theme is per-context in RmlUi, so switching one means walking every context; and a context
	 * created afterwards has to be told too, or a document loaded after the switch comes up in the
	 * wrong theme. Keeping both here is what makes the setting global rather than per-document.
	 */
	TArray<FFolioDocumentHost*> LiveHosts;
	TSet<FName> ActiveThemes;
}

FFolioDocumentHost::FFolioDocumentHost() = default;

FFolioDocumentHost::~FFolioDocumentHost()
{
	Destroy();
}

bool FFolioDocumentHost::Create(const TCHAR* Label, const FIntPoint InSize)
{
	if (Context || !FFolioModule::Get().IsRmlInitialised())
	{
		return false;
	}

	Size = FIntPoint(FMath::Max(1, InSize.X), FMath::Max(1, InSize.Y));

	static int32 ContextCounter = 0;
	const Rml::String Name = Folio::ToRmlString(
		FString::Printf(TEXT("folio_%s_%d"), Label, ContextCounter++));

	Context = Rml::CreateContext(Name, Rml::Vector2i(Size.X, Size.Y));
	if (!Context)
	{
		UE_LOG(LogFolio, Error, TEXT("could not create an RmlUi context"));
		return false;
	}

	// Applied before anything loads, so a document never renders a frame in the wrong theme.
	for (const FName Theme : GetActiveThemes())
	{
		Context->ActivateTheme(Folio::ToRmlString(Theme.ToString()), true);
	}

	LiveHosts.AddUnique(this);

	InputModel.OnChanged = [this] { MarkDirty(TEXT("input device or confirm button")); };
	InputModel.Create(Context);
	return true;
}

void FFolioDocumentHost::SetTheme(const FName Theme, const bool bActive)
{
	if (bActive)
	{
		ActiveThemes.Add(Theme);
	}
	else
	{
		ActiveThemes.Remove(Theme);
	}

	const Rml::String Name = Folio::ToRmlString(Theme.ToString());
	for (FFolioDocumentHost* Host : LiveHosts)
	{
		if (Host->Context)
		{
			Host->Context->ActivateTheme(Name, bActive);

			// A media query changing is not something RmlUi reports as a change, so without this a
			// settled document keeps the look it already had until something else disturbs it.
			Host->MarkDirty(TEXT("theme"));
		}
	}
}

bool FFolioDocumentHost::IsThemeActive(const FName Theme)
{
	return ActiveThemes.Contains(Theme);
}

const TSet<FName>& FFolioDocumentHost::GetActiveThemes()
{
	return ActiveThemes;
}

void FFolioDocumentHost::Destroy()
{
	if (!Context)
	{
		return;
	}

	// A host can outlive the module. Once Rml::Shutdown has run, every context is already destroyed
	// and its name is no longer in RmlUi's map, so looking one up walks freed memory - which surfaces
	// as a crash inside robin_hood's hash table on editor close.
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		// Safe with RmlUi already down: the document is observed, so this drops a null rather than
		// unregistering from a freed element.
		Behaviours.Detach();

		InputModel.Release();
		ObjectModels.Empty();
		Context = nullptr;
		Document = nullptr;
		return;
	}

	if (Document && OnDocumentClosing)
	{
		OnDocumentClosing();
	}

	Behaviours.Detach();

	// Closing a document is deferred until the next Update, so the context has to outlive the close.
	// Removing the context destroys everything in it outright, which is what we want.
	InputModel.Release();
	ObjectModels.Empty();
	Document = nullptr;
	LiveHosts.RemoveSingleSwap(this);
	Rml::RemoveContext(Context->GetName());
	Context = nullptr;
	DataModel.Reset();
}

void FFolioDocumentHost::SetSize(const FIntPoint InSize)
{
	const FIntPoint Clamped(FMath::Max(1, InSize.X), FMath::Max(1, InSize.Y));
	if (Clamped == Size || !Context)
	{
		return;
	}

	Size = Clamped;
	Context->SetDimensions(Rml::Vector2i(Size.X, Size.Y));

	// A resize re-evaluates @media and every vw/vh unit, so it is a change even though nothing in the
	// document moved on its own.
	MarkDirty(TEXT("resized"));
}

void FFolioDocumentHost::LoadDocument(const FString& RelativePath)
{
	if (!Context)
	{
		return;
	}

	if (Document)
	{
		// Before the close, not after: whatever is listening on this document has to be told while its
		// elements still exist.
		if (OnDocumentClosing)
		{
			OnDocumentClosing();
		}

		Behaviours.Detach();
		Document->Close();
		// Close is deferred to the next update; without this the old document is still alive when the
		// new one loads and both would render.
		Context->Update();
		Document = nullptr;
	}

	DocumentPath = RelativePath;
	if (RelativePath.IsEmpty())
	{
		return;
	}

	// Reopening a document is what anyone does after editing its art, so this is the moment to notice
	// that a file on disk has moved on. RmlUi caches a texture by path forever otherwise, and a
	// re-baked atlas keeps rendering as it was until the editor restarts.
	if (FFolioModule::IsAvailable())
	{
		if (FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface())
		{
			if (const int32 Reloaded = Renderer->ReloadChangedTextures())
			{
				UE_LOG(LogFolio, Log, TEXT("reloaded %d changed texture(s)"), Reloaded);
			}
		}
	}

	MountDataModel(RelativePath);

	// Checked before the load rather than after it, so "the file is not there" is told apart from "the
	// file is there and did not parse". Both render nothing, and guessing which is the whole problem.
	if (!UFolioDocumentPathLibrary::DocumentExists(RelativePath))
	{
		UFolioDocumentPathLibrary::ReportMissingDocument(RelativePath, TEXT("Folio"));
		return;
	}

	Document = Context->LoadDocument(Folio::ToRmlString(RelativePath));
	if (!Document)
	{
		UE_LOG(LogFolio, Error, TEXT("could not load document '%s': it exists but did not parse"), *RelativePath);
		return;
	}

	// Before the first update, so translated text is laid out rather than reflowed a frame later. Data
	// views already exist by now - they are built as the markup is parsed - which is what lets the pass
	// recognise a bound element and leave it alone.
	Folio::Loc::ApplyToDocument(Document, RelativePath);

	Document->Show();
	InputModel.SetDocument(Document);
	Behaviours.Attach(Document);

	if (OnDocumentLoaded)
	{
		OnDocumentLoaded(Document);
	}

	MarkDirty(TEXT("document loaded"));
	UE_LOG(LogFolio, Log, TEXT("loaded '%s'"), *RelativePath);
}

void FFolioDocumentHost::ReloadDocument()
{
	if (!Context || DocumentPath.IsEmpty())
	{
		return;
	}

	const Folio::Reload::FDocumentState State = Folio::Reload::CaptureDocumentState(Context, Document);

	// Without clearing these, an edited stylesheet reached through @import is served from cache and the
	// reload appears to do nothing at all.
	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	Rml::ReleaseTextures();
	Rml::ReleaseCompiledGeometry();

	LoadDocument(DocumentPath);
	if (!Document)
	{
		return;
	}

	// Layout has to exist before focus and scroll can be put back, and the restore itself changes both,
	// so the state lands one update later.
	Context->Update();
	Folio::Reload::RestoreDocumentState(Document, State);
	Context->Update();
}

void FFolioDocumentHost::ReloadStyleSheets()
{
	Folio::Reload::ReloadStyleSheets(Context);
	MarkDirty(TEXT("stylesheets reloaded"));
}

bool FFolioDocumentHost::HasObjectModel(const FString& ModelName) const
{
	return ObjectModels.ContainsByPredicate(
		[&ModelName](const TUniquePtr<FFolioObjectModel>& Model)
		{
			return Model.IsValid() && Model->GetModelName() == ModelName;
		});
}

bool FFolioDocumentHost::BindObjectModel(const FString& ModelName, UObject* Source,
	const bool bPollEveryFrame)
{
	if (!Context)
	{
		return false;
	}

	TUniquePtr<FFolioObjectModel> Model = MakeUnique<FFolioObjectModel>();
	if (!Model->Mount(Context, ModelName, Source, bPollEveryFrame))
	{
		return false;
	}

	ObjectModels.Add(MoveTemp(Model));
	MarkDirty(TEXT("model bound"));
	return true;
}

void FFolioDocumentHost::MountDataModel(const FString& RelativePath)
{
	// Mock data is a design-time stand-in and has no business in a retail build. Left in, a screen whose
	// model the game forgot to bind would silently render invented values instead of failing visibly,
	// and the mock would occupy the name the real view model wanted.
#if !UE_BUILD_SHIPPING
	if (!Folio::MockDataEnabled() || DataModel.IsValid())
	{
		return;
	}

	const FString& ContentRoot = FFolioModule::Get().GetContentRoot();

	FString Markup;
	if (!FFileHelper::LoadFileToString(Markup, *(ContentRoot / RelativePath)))
	{
		return;
	}

	const FString ModelName = FFolioDataModel::DiscoverModelName(Markup);
	if (ModelName.IsEmpty())
	{
		return;
	}

	// The reserved model is mounted with the context and is not a game model, so it has no mock file and
	// nothing is missing when there isn't one.
	if (ModelName == UTF8_TO_TCHAR(Folio::InputModel::Name()))
	{
		return;
	}

	// Game code got there first, and it wins. RmlUi refuses a second model under the same name, so
	// mounting a mock here is what would make BindObjectModel fail rather than the other way round.
	if (HasObjectModel(ModelName))
	{
		return;
	}

	const FString MockPath = FFolioDataModel::FindMockFile(ContentRoot, ModelName);
	if (MockPath.IsEmpty())
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' declares data-model=\"%s\" and nothing is bound, so its bindings render as literal ")
			TEXT("{{ }}. Provide the model from game code, or put mock values in ui/mock/%s.json."),
			*RelativePath, *ModelName, *ModelName);
		return;
	}

	DataModel = MakeUnique<FFolioDataModel>();
	if (!DataModel->Mount(Context, ModelName, MockPath))
	{
		DataModel.Reset();
	}
#endif
}

bool FFolioDocumentHost::IsTakingText() const
{
	if (!Context)
	{
		return false;
	}

	const Rml::Element* Focus = Context->GetFocusElement();
	if (Focus == nullptr)
	{
		return false;
	}

	const Rml::String FocusTag = Focus->GetTagName();
	if (FocusTag == "textarea")
	{
		return true;
	}

	if (FocusTag != "input")
	{
		return false;
	}

	// The tag alone is not enough: a checkbox, radio or range slider is an input that takes no text, and
	// treating it as one blocks WASD and IJKL for as long as it has focus. RmlUi defaults a type-less
	// input to text.
	const Rml::String Type = Focus->GetAttribute<Rml::String>("type", "text");
	return Type == "text" || Type == "password";
}

void FFolioDocumentHost::MarkDirty(const TCHAR* Reason)
{
	// First reason of the frame wins, since it is the one that actually caused the update.
	if (!bDirty)
	{
		DirtyReason = Reason;
	}

	bDirty = true;

	if (OnDirty)
	{
		OnDirty();
	}
}

void FFolioDocumentHost::TickDataModels()
{
	// Always, even when the rest of the frame is skipped: polling is how a source without change
	// notification is noticed at all, so skipping it would freeze exactly the bindings that have no
	// other way to report themselves.
	for (const TUniquePtr<FFolioObjectModel>& Model : ObjectModels)
	{
		if (Model->Tick())
		{
			MarkDirty(TEXT("data model"));
		}
	}
}

bool FFolioDocumentHost::Update(const float DeltaSeconds, const float DpRatio, const float RenderScale)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_Update);
	SCOPE_CYCLE_COUNTER(STAT_FolioUpdate);

	if (!Context)
	{
		return false;
	}

	// Nothing on our side changed and RmlUi asked not to be updated yet, so the frame is skipped
	// entirely. The clock still runs, because what RmlUi asked for is a delay rather than a signal.
	if (Folio::InvalidationEnabled() && !bDirty && NextUpdateIn > 0.0)
	{
		NextUpdateIn -= DeltaSeconds;
		if (NextUpdateIn > 0.0)
		{
			return false;
		}
	}

	// The host's scaling expressed the way RmlUi wants it: dp units resolve against this ratio.
	Context->SetDensityIndependentPixelRatio(DpRatio);

	// Backgrounds carry their own antialiasing because there is no multisampling to soften a rounded
	// corner. The generator works in document units, so it needs one rendered pixel expressed in them.
	Rml::MeshUtilities::SetBackgroundAntialiasWidth(
		RenderScale > KINDA_SMALL_NUMBER ? 1.0f / RenderScale : 1.0f);

	const TCHAR* const Reason = bDirty ? DirtyReason : TEXT("RmlUi asked");

	Context->Update();

	// Read after the update, not before: RmlUi resets the request to infinity as Update starts and fills
	// it in as elements ask, so before the call it says nothing about this frame.
	NextUpdateIn = Context->GetNextUpdateDelay();
	bDirty = false;

	if (Folio::InvalidationTracing())
	{
		UE_LOG(LogFolio, Display, TEXT("folio invalidation: updated because '%s', next update in %s"),
			Reason,
			FMath::IsFinite(NextUpdateIn)
				? *FString::Printf(TEXT("%.3fs"), NextUpdateIn)
				: TEXT("never, until input"));
	}

	return true;
}
