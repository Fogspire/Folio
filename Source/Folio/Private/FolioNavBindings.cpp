#include "FolioNavBindings.h"

#include "FolioModule.h"
#include "FolioNavigation.h"

namespace
{
	TMap<FKey, EFolioNavAction> Bindings;
	bool bReplaceDefaults = false;
}

namespace Folio
{
	EFolioNavAction FindBoundAction(const FKey& Key)
	{
		const EFolioNavAction* Found = Bindings.Find(Key);
		return Found ? *Found : EFolioNavAction::None;
	}

	bool DefaultsReplaced()
	{
		return bReplaceDefaults;
	}
}

void UFolioNavLibrary::LoadNavBindings(const UDataTable* Table, const bool bInReplaceDefaults)
{
	if (!Table)
	{
		UE_LOG(LogFolio, Warning, TEXT("LoadNavBindings was given no table"));
		return;
	}

	if (Table->GetRowStruct() != FFolioNavBinding::StaticStruct())
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' is not a table of FFolioNavBinding rows, so no binding was loaded"),
			*Table->GetName());
		return;
	}

	int32 Loaded = 0;
	Table->ForeachRow<FFolioNavBinding>(TEXT("LoadNavBindings"),
		[&Loaded](const FName&, const FFolioNavBinding& Row)
		{
			// A row with no action is how a table says "leave this key alone", which is worth
			// allowing so one table can describe a scheme without listing exclusions elsewhere.
			if (Row.Key.IsValid() && Row.Action != EFolioNavAction::None)
			{
				Bindings.Add(Row.Key, Row.Action);
				++Loaded;
			}
		});

	// Only once the table turned out to be usable. Set beforehand, a bad table would leave a
	// document with no navigation at all and nothing bound to replace it.
	bReplaceDefaults = bInReplaceDefaults;

	UE_LOG(LogFolio, Log, TEXT("loaded %d navigation binding(s) from '%s'%s"),
		Loaded, *Table->GetName(),
		bReplaceDefaults ? TEXT(", replacing the built-in schemes") : TEXT(""));
}

void UFolioNavLibrary::SetNavBinding(const FKey Key, const EFolioNavAction Action)
{
	if (!Key.IsValid())
	{
		return;
	}

	if (Action == EFolioNavAction::None)
	{
		Bindings.Remove(Key);
		return;
	}

	Bindings.Add(Key, Action);
}

void UFolioNavLibrary::ClearNavBinding(const FKey Key)
{
	Bindings.Remove(Key);
}

void UFolioNavLibrary::ResetNavBindings()
{
	Bindings.Empty();
	bReplaceDefaults = false;
}

EFolioNavAction UFolioNavLibrary::GetNavAction(const FKey Key)
{
	return Folio::ActionForKey(Key);
}

TArray<FKey> UFolioNavLibrary::GetKeysForNavAction(const EFolioNavAction Action)
{
	TArray<FKey> Keys;
	if (Action == EFolioNavAction::None)
	{
		return Keys;
	}

	for (const TPair<FKey, EFolioNavAction>& Binding : Bindings)
	{
		if (Binding.Value == Action)
		{
			Keys.Add(Binding.Key);
		}
	}

	// The built-in schemes are bindings too as far as a settings screen is concerned: a player
	// looking at "Up" should see the arrow key that works, not only what someone rebound.
	if (!bReplaceDefaults)
	{
		for (const FKey& Key : Folio::DefaultKeysForAction(Action))
		{
			Keys.AddUnique(Key);
		}
	}

	return Keys;
}
