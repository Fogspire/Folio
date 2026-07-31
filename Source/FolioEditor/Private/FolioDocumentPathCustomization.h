#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class SComboButton;

/** Draws FFolioDocumentPath as a tree picker rather than a text box. */
class FFolioDocumentPathCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Row,
		IPropertyTypeCustomizationUtils& Utils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Builder,
		IPropertyTypeCustomizationUtils& Utils) override;

private:
	TSharedRef<SWidget> MakeMenu();

	FString GetPath() const;
	void SetPath(const FString& Path);

	FText GetDisplayText() const;
	EVisibility GetWarningVisibility() const;
	FText GetWarningTooltip() const;

	/**
	 * Writes the document's declared size into the sibling property named by FolioSizeProperty.
	 *
	 * Only for a document that says what it wants. Nothing is guessed and nothing is overwritten
	 * otherwise, so a size someone deliberately set is never quietly replaced by a default.
	 */
	void ApplyDeclaredSize(const FString& Path, bool bOnlyIfDefault);

	/**
	 * What the reset arrow restores, which is the class default unless that names nothing.
	 *
	 * A default pointing at a document that has since been deleted or renamed is worse than no
	 * default: resetting puts back a path that cannot load, and the field looks deliberately set.
	 * Clearing is the honest outcome, and an empty path is legal - a component may be given its
	 * document from code.
	 */
	bool IsResetVisible() const;
	void OnReset();

	/** The default the archetype holds, or empty when it names no file that exists. */
	FString GetUsableDefault() const;

	/** What the current document declares, so the row can say whether there is anything to apply. */
	bool GetDeclaredSize(FVector2D& OutSize) const;

	FText GetSizeNote() const;
	bool CanApplyDeclaredSize() const;
	FReply OnApplyDeclaredSize();

	/** Whether this component has a size property to fill at all, from the metadata. */
	bool HasSizeProperty() const;

	/** The struct property this is drawing, which carries the metadata naming its siblings. */
	TSharedPtr<IPropertyHandle> StructHandle;

	TSharedPtr<IPropertyHandle> PathHandle;
	TSharedPtr<SComboButton> ComboButton;
};
