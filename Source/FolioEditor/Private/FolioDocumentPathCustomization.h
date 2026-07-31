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
	void ApplyDeclaredSize(const FString& Path);

	/** The struct property this is drawing, which carries the metadata naming its siblings. */
	TSharedPtr<IPropertyHandle> StructHandle;

	TSharedPtr<IPropertyHandle> PathHandle;
	TSharedPtr<SComboButton> ComboButton;
};
