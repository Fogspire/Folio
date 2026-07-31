#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioModule.h"
#include "FolioObjectModel.h"
#include "FolioTestViewModel.h"

#include "UObject/StrongObjectPtr.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>

namespace
{
	/**
	 * A context to mount a model into, removed however the test leaves.
	 *
	 * RmlUi keeps contexts in a global registry, so one left behind by a failing test is still there
	 * for the next one and the second mount of the same model name fails for no visible reason.
	 */
	struct FScopedContext
	{
		explicit FScopedContext(const Rml::String& InName)
			: Name(InName)
			, Context(Rml::CreateContext(InName, Rml::Vector2i(320, 240)))
		{
		}

		~FScopedContext()
		{
			if (Context)
			{
				Rml::RemoveContext(Name);
			}
		}

		Rml::String Name;
		Rml::Context* Context = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioNotifiedModelTest, "Folio.ViewModel.Notification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioNotifiedModelTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no data model can be created."));
		return false;
	}

	FScopedContext Scope("FolioTest.Notification");
	if (!TestNotNull(TEXT("context"), Scope.Context))
	{
		return false;
	}

	TStrongObjectPtr<UFolioTestViewModel> Model(NewObject<UFolioTestViewModel>());

	FFolioObjectModel Bridge;
	if (!TestTrue(TEXT("mount"), Bridge.Mount(Scope.Context, TEXT("test"), Model.Get(), false)))
	{
		return false;
	}

	TestFalse(TEXT("a model nothing has touched reports no change"), Bridge.Tick());

	// The path under test. Polling is off, so nothing but the subscription can make this true.
	Model->SetScore(7);
	TestTrue(TEXT("a broadcast reaches the model"), Bridge.Tick());
	TestFalse(TEXT("and is consumed by the tick that saw it"), Bridge.Tick());

	Model->SetPlayerName(TEXT("Ripley"));
	TestTrue(TEXT("every bound field is subscribed, not only the first"), Bridge.Tick());

	// Writing the same value again broadcasts nothing, and a broadcast whose value did not move must
	// not report a change either - a document that redraws on every unchanged notification is the
	// bug this half of the check exists for.
	Model->SetScore(7);
	TestFalse(TEXT("an unchanged value is not a change"), Bridge.Tick());

	Model->BroadcastFieldValueChanged(UFolioTestViewModel::FFieldNotificationClassDescriptor::Score);
	TestFalse(TEXT("a broadcast with no value behind it is not a change"), Bridge.Tick());

	// A source that does not announce itself is invisible without polling. This is the distinction
	// that makes bPollEveryFrame worth having, so it is worth asserting rather than assuming.
	Model->SetScoreSilently(9);
	TestFalse(TEXT("a silent write is not seen without polling"), Bridge.Tick());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioPolledModelTest, "Folio.ViewModel.Polling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioPolledModelTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no data model can be created."));
		return false;
	}

	FScopedContext Scope("FolioTest.Polling");
	if (!TestNotNull(TEXT("context"), Scope.Context))
	{
		return false;
	}

	TStrongObjectPtr<UFolioTestPlainModel> Plain(NewObject<UFolioTestPlainModel>());

	FFolioObjectModel Bridge;
	if (!TestTrue(TEXT("mount"), Bridge.Mount(Scope.Context, TEXT("plain"), Plain.Get(), true)))
	{
		return false;
	}

	// The sweep runs once at mount, so the first tick is already settled.
	TestFalse(TEXT("a settled model reports no change"), Bridge.Tick());

	Plain->Score = 9;
	TestTrue(TEXT("polling sees a write that announced nothing"), Bridge.Tick());
	TestFalse(TEXT("and settles again"), Bridge.Tick());

	Plain->PlayerName = TEXT("Ripley");
	TestTrue(TEXT("the sweep covers every field, not only the first"), Bridge.Tick());

	// Polling is a fallback, not an addition: a source that reports its own changes is never swept,
	// whatever the caller asked for. Otherwise every view model would pay for both.
	TStrongObjectPtr<UFolioTestViewModel> Notifying(NewObject<UFolioTestViewModel>());

	FFolioObjectModel Second;
	if (!TestTrue(TEXT("mount"), Second.Mount(Scope.Context, TEXT("notifying"), Notifying.Get(), true)))
	{
		return false;
	}

	Notifying->SetScoreSilently(9);
	TestFalse(TEXT("a notifying source is not polled even when asked"), Second.Tick());

	Notifying->SetScore(11);
	TestTrue(TEXT("and still reports what it announces"), Second.Tick());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioModelLifetimeTest, "Folio.ViewModel.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioModelLifetimeTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no data model can be created."));
		return false;
	}

	FScopedContext Scope("FolioTest.Lifetime");
	if (!TestNotNull(TEXT("context"), Scope.Context))
	{
		return false;
	}

	TStrongObjectPtr<UFolioTestViewModel> Model(NewObject<UFolioTestViewModel>());

	{
		FFolioObjectModel Bridge;
		if (!TestTrue(TEXT("mount"), Bridge.Mount(Scope.Context, TEXT("test"), Model.Get(), false)))
		{
			return false;
		}
	}

	// The subscription outliving the model would call a lambda holding a dangling `this`. It is the
	// failure this whole path is most likely to produce and the one least likely to be noticed, since
	// the freed memory usually still reads as something plausible.
	Model->SetScore(11);
	TestEqual(TEXT("the source is still writable after its model is gone"), Model->Score, 11);

	return true;
}

#endif
