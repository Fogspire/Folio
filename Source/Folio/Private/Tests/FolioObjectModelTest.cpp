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

	/*
	 * An FText binds as what it says rather than as how it was declared.
	 *
	 * Read through ExportTextItem - which is how everything that is not a number is read - a text
	 * carrying a namespace and a key exports as the whole NSLOCTEXT("ns", "key", "source")
	 * expression, and a document rendered that instead of the words. Two texts with the same
	 * contents under different keys are the same string to a reader, so nothing changed.
	 */
	Plain->Title = NSLOCTEXT("FolioTest", "Ready_A", "Ready");
	TestTrue(TEXT("a localised string is a change"), Bridge.Tick());

	Plain->Title = NSLOCTEXT("FolioTest", "Ready_B", "Ready");
	TestFalse(TEXT("an FText binds as its text, not as its declaration"), Bridge.Tick());

	Plain->Title = NSLOCTEXT("FolioTest", "Ready_B", "Steady");
	TestTrue(TEXT("and changing what it says is a change"), Bridge.Tick());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioArrayModelTest, "Folio.ViewModel.Arrays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioArrayModelTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no data model can be created."));
		return false;
	}

	FScopedContext Scope("FolioTest.Arrays");
	if (!TestNotNull(TEXT("context"), Scope.Context))
	{
		return false;
	}

	TStrongObjectPtr<UFolioTestPlainModel> Plain(NewObject<UFolioTestPlainModel>());
	Plain->Party.Add({ TEXT("Ripley"), 1.0f });

	// A map alongside the arrays, because the mount has to keep binding the rest rather than give up
	// on a source that holds one.
	Plain->Unbindable.Add(TEXT("mode"), 2);

	FFolioObjectModel Bridge;
	if (!TestTrue(TEXT("mount"), Bridge.Mount(Scope.Context, TEXT("party"), Plain.Get(), true)))
	{
		return false;
	}

	TestFalse(TEXT("a settled model reports no change"), Bridge.Tick());

	// Length, which is what makes data-for rebuild rather than reuse rows against stale values.
	Plain->Party.Add({ TEXT("Hicks"), 0.5f });
	TestTrue(TEXT("an appended element is a change"), Bridge.Tick());
	TestFalse(TEXT("and settles"), Bridge.Tick());

	// A value inside an element, with the length unchanged. Mirroring per element is the only reason
	// this is visible at all.
	Plain->Party[1].Health = 0.25f;
	TestTrue(TEXT("a member of an element is a change"), Bridge.Tick());
	TestFalse(TEXT("and settles"), Bridge.Tick());

	Plain->Party[1].Health = 0.25f;
	TestFalse(TEXT("writing the same value is not a change"), Bridge.Tick());

	Plain->Party.RemoveAt(0);
	TestTrue(TEXT("a removed element is a change"), Bridge.Tick());

	// A shortened array leaves the removed rows' mirrors behind if they are not dropped, and the
	// following element then reads the value of the one that used to be after it.
	Plain->Party.Empty();
	TestTrue(TEXT("emptying is a change"), Bridge.Tick());
	TestFalse(TEXT("and an empty array settles"), Bridge.Tick());

	// An array of scalars has no members to address, and binds as the values themselves.
	Plain->Tags.Add(TEXT("payload"));
	TestTrue(TEXT("an array of strings is bound too"), Bridge.Tick());

	Plain->Tags[0] = TEXT("koth");
	TestTrue(TEXT("and its elements are mirrored"), Bridge.Tick());
	TestFalse(TEXT("and settle"), Bridge.Tick());

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
