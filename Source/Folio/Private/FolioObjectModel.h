#pragma once

#include "CoreMinimal.h"
#include "FolioRmlUi.h"
#include "UObject/WeakObjectPtr.h"

#include <RmlUi/Core/DataModelHandle.h>

namespace Rml
{
	class Context;
}

/**
 * Binds a UObject's properties into an RmlUi data model.
 *
 * This is the view model bridge, and it deliberately knows nothing about MVVM. A
 * `UMVVMViewModelBase` is a UObject whose properties are UPROPERTYs and which implements
 * `INotifyFieldValueChanged` - both of which are reflection and an engine-core interface
 * (`FieldNotification`), not the ModelViewViewModel plugin. So Folio binds view models without
 * depending on MVVM, a project that never uses MVVM pays nothing, and the same code binds any
 * UObject: a game state, a settings object, a plain UObject holding four fields.
 *
 * Values are mirrored rather than bound in place. RmlUi keeps the address of what it is given, and
 * a UPROPERTY cannot always supply a stable one of the right type - a bitfield bool has no `bool*`,
 * an FText is not a string. Mirroring makes every property type work the same way and costs one
 * copy per bound field per refresh.
 *
 * Updates arrive by notification when the source supports it, which is the whole point of a view
 * model. Sources that do not are polled once a frame instead, which is correct but does work
 * proportional to the number of fields, so it is opt-in.
 */
class FFolioObjectModel
{
public:
	~FFolioObjectModel();

	/** Creates the model and binds every property the source exposes. */
	bool Mount(Rml::Context* Context, const FString& ModelName, UObject* Source, bool bPollEveryFrame);

	/** Re-reads polled fields. Does nothing for a source that reports its own changes. */
	void Tick();

	/** True while the source is alive. A garbage collected source stops updating rather than crashing. */
	bool IsValid() const { return Source.IsValid(); }

	const FString& GetModelName() const { return ModelName; }

	/** One bound property, and the copy of it RmlUi holds the address of. */
	struct FField
	{
		FProperty* Property = nullptr;
		Rml::String Name;

		// Only one of these is bound, chosen from the property's type.
		bool bValue = false;
		int IntValue = 0;
		float FloatValue = 0.0f;
		Rml::String StringValue;

		enum class EKind : uint8 { Bool, Int, Float, String } Kind = EKind::String;

		FDelegateHandle Notification;
	};

private:

	/** Reads the property into the mirror. Returns true when the value actually moved. */
	bool Refresh(FField& Field);

	void Unsubscribe();

	TWeakObjectPtr<UObject> Source;
	FString ModelName;
	Rml::DataModelHandle Handle;

	// Held by pointer so the mirrors keep their addresses as the array grows.
	TArray<TUniquePtr<FField>> Fields;

	bool bPolling = false;
};
