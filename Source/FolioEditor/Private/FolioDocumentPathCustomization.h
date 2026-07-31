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

	TSharedPtr<IPropertyHandle> PathHandle;
	TSharedPtr<SComboButton> ComboButton;
};
