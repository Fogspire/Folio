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
 * Binds a UObject's properties and commands into an RmlUi data model.
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
 *
 * Commands go the other way. Every BlueprintCallable function with no return value and parameters a
 * data expression can supply becomes an event callback, so `data-event-click="Confirm"` reaches game
 * code from markup. That is what makes the Blueprint authoring path work at all, and it is the same
 * mechanism a C++ view model uses.
 */
class FFolioObjectModel
{
public:
	FFolioObjectModel();
	~FFolioObjectModel();

	/** Creates the model and binds every property the source exposes. */
	bool Mount(Rml::Context* Context, const FString& ModelName, UObject* Source, bool bPollEveryFrame);

	/**
	 * Re-reads polled fields. Does nothing for a source that reports its own changes.
	 *
	 * Returns whether any value moved, so the canvas knows to update. A polled source is the case that
	 * would otherwise freeze: it has no notification to raise a dirty flag with, so the only evidence
	 * that it changed is this sweep finding a difference.
	 */
	bool Tick();

	/** True while the source is alive. A garbage collected source stops updating rather than crashing. */
	bool IsValid() const { return Source.IsValid(); }

	const FString& GetModelName() const { return ModelName; }

	/** One mirrored value, and the kind that decides which of the four slots carries it. */
	struct FValue
	{
		enum class EKind : uint8 { Bool, Int, Float, String } Kind = EKind::String;

		bool bValue = false;
		int IntValue = 0;
		float FloatValue = 0.0f;
		Rml::String StringValue;
	};

	/** One bound property, and the copy of it RmlUi holds the address of. */
	struct FField
	{
		FProperty* Property = nullptr;
		Rml::String Name;
		FValue Value;

		FDelegateHandle Notification;
		/** Kept so a write can announce itself, not only so the subscription can be removed. */
		UE::FieldNotification::FFieldId FieldId;

		/** Whose property this is. RmlUi hands Set the field, and the write needs the object. */
		FFolioObjectModel* Owner = nullptr;

		/** False for a property the author marked read-only, so markup cannot write it. */
		bool bWritable = false;
		bool bWarnedReadOnly = false;
	};

	/** One value inside one element of a bound array. Read only: markup cannot write into a TArray. */
	struct FLeaf
	{
		FProperty* Property = nullptr;
		Rml::String Name;
		FValue Value;

		/**
		 * The members of a struct value, so `{{ Row.Glyph.Cap }}` reaches inside it. Empty for anything
		 * else. The struct as a whole still reads as its exported text.
		 */
		TArray<TUniquePtr<FLeaf>> Members;
	};

	/** One element of a bound array: a bag of members, or a single value. */
	struct FRow
	{
		TArray<TUniquePtr<FLeaf>> Members;
		TUniquePtr<FLeaf> Scalar;
	};

	/**
	 * One bound TArray, mirrored element by element.
	 *
	 * The member list is resolved once from the inner property's type rather than per element, which
	 * is what makes `{{ Row.Name }}` resolve without a registered struct. For an array of objects that
	 * means the declared class: a derived element's extra properties are not visible.
	 */
	struct FArrayField
	{
		FArrayProperty* Property = nullptr;
		Rml::String Name;

		enum class EElement : uint8 { Scalar, Struct, Object } Element = EElement::Scalar;

		/** Empty for a scalar element. */
		TArray<FProperty*> MemberProperties;
		FValue::EKind ScalarKind = FValue::EKind::String;

		TArray<TUniquePtr<FRow>> Rows;

		FDelegateHandle Notification;
		UE::FieldNotification::FFieldId FieldId;
	};

private:

	/**
	 * Binds a BlueprintCallable function as a data event callback, so markup can invoke it.
	 *
	 * `data-event-click="Confirm"` and `data-event-change="SetVolume(1.5)"` both come through here.
	 * Rejected rather than half-bound if the signature is not callable from a data expression, so a
	 * mistake is a warning at mount rather than a crash on click.
	 */
	bool BindCommand(Rml::DataModelConstructor& Constructor, UFunction* Function);

	/**
	 * Binds a TArray as an RmlUi array, so `data-for` can iterate it.
	 *
	 * Returns false for an element type there is nothing to bind through - an array of arrays, or of
	 * maps - which is reported with the other omissions rather than failing the mount.
	 */
	bool BindArray(Rml::DataModelConstructor& Constructor, FArrayProperty& Property);

	/** Calls the function with whatever the expression supplied, defaulting anything it did not. */
	void Invoke(UFunction* Function, const Rml::VariantList& Arguments) const;

	/** Reads the property into the mirror. Returns true when the value actually moved. */
	bool Refresh(FField& Field);

	/**
	 * Re-reads a bound array: its length, then every mirrored value in it.
	 *
	 * Returns true when anything moved, including a change of length, which is what makes `data-for`
	 * rebuild its children rather than reuse them against stale values.
	 */
	bool Refresh(FArrayField& Field);

	/** Builds the mirrors for one element, from the member list the array resolved at mount. */
	TUniquePtr<FRow> MakeRow(const FArrayField& Field) const;

	/**
	 * Writes a value from markup back through the property to the source object.
	 *
	 * What `data-value` on an input needs. Writing the mirror alone would look like it worked and then
	 * spring back on the next refresh, which is worse than not supporting it.
	 */
	bool Write(FField& Field, const Rml::Variant& Value);

	void Unsubscribe();

	TWeakObjectPtr<UObject> Source;
	FString ModelName;
	Rml::DataModelHandle Handle;

	struct FDefinitions;

	// Held by pointer so the mirrors keep their addresses as the array grows.
	TArray<TUniquePtr<FField>> Fields;
	TArray<TUniquePtr<FArrayField>> ArrayFields;
	TUniquePtr<FDefinitions> Definitions;

	int32 Commands = 0;
	bool bPolling = false;

	/** Set by the notification path, read and cleared by Tick, so a notified change reaches the canvas. */
	bool bNotified = false;
};
