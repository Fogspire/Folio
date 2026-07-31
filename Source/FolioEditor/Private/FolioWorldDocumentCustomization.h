#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UFolioWorldDocument;

/**
 * Adds the one thing Pixels Per Unit needs and a number field cannot give: a way to set it from the
 * view you are looking at.
 *
 * The value is a density, and the only thing that makes one density right rather than another is how
 * large the surface lands on screen at the distance it is meant to be read from. That is a fact about
 * the camera, not about the component, so no default and no formula over the component's own
 * properties can produce it.
 */
class FFolioWorldDocumentCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& Layout) override;

private:
	/** How tall the selected surfaces land in the active level viewport, and what that implies. */
	FText GetNote() const;
	bool CanApply() const;
	FReply OnApply();

	/** The measurement, or 0 when there is no viewport or nothing selected to measure. */
	int32 MeasureScreenHeight(const UFolioWorldDocument& Document) const;

	TArray<TWeakObjectPtr<UFolioWorldDocument>> Documents;
};
