#include "FolioWorldDocumentCustomization.h"

#include "FolioWorldDocument.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FolioEditor"

namespace
{
	/** The viewport the author is looking through, which is the one the measurement has to use. */
	const FLevelEditorViewportClient* ActiveViewport()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		for (const FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
		{
			if (Client && Client->Viewport && Client->IsPerspective())
			{
				// The focused one when there is one, since a four-up layout has several and only one is
				// arranged the way the author means.
				if (Client == GCurrentLevelEditingViewportClient)
				{
					return Client;
				}
			}
		}

		for (const FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
		{
			if (Client && Client->Viewport && Client->IsPerspective())
			{
				return Client;
			}
		}

		return nullptr;
	}
}

TSharedRef<IDetailCustomization> FFolioWorldDocumentCustomization::MakeInstance()
{
	return MakeShared<FFolioWorldDocumentCustomization>();
}

int32 FFolioWorldDocumentCustomization::MeasureScreenHeight(const UFolioWorldDocument& Document) const
{
	const FLevelEditorViewportClient* Client = ActiveViewport();
	if (!Client || !Client->Viewport)
	{
		return 0;
	}

	const FIntPoint Size = Client->Viewport->GetSizeXY();

	return Document.ComputeScreenHeightPixels(Client->GetViewLocation(),
		Client->ViewFOV, Size.Y);
}

FText FFolioWorldDocumentCustomization::GetNote() const
{
	const UFolioWorldDocument* Document = Documents.IsEmpty() ? nullptr : Documents[0].Get();
	if (!Document)
	{
		return FText::GetEmpty();
	}

	const int32 Height = MeasureScreenHeight(*Document);
	if (Height <= 0)
	{
		return LOCTEXT("NoViewport",
			"No perspective viewport to measure against. Open one and place the camera where the "
			"surface is meant to be read from.");
	}

	const int32 Current = Document->GetDocumentSize().Y;

	FNumberFormattingOptions TwoPlaces;
	TwoPlaces.MaximumFractionalDigits = 2;

	// The ratio is the whole story, so it is what the line leads with. Anything much above 1 is a
	// document being minified, which is what makes text unreadable long before resolution does.
	const float Ratio = float(Current) / float(Height);
	const float Suggested = float(Height) / float(FMath::Max(0.01, Document->WorldSize.Y));

	return FText::Format(
		LOCTEXT("PpuNote",
			"Covers {0} px tall from this viewport; laid out at {1}. That is {2}x. Applying sets "
			"Pixels Per Unit to {3}."),
		FText::AsNumber(Height),
		FText::AsNumber(Current),
		FText::AsNumber(Ratio, &TwoPlaces),
		FText::AsNumber(Suggested, &TwoPlaces));
}

bool FFolioWorldDocumentCustomization::CanApply() const
{
	const UFolioWorldDocument* Document = Documents.IsEmpty() ? nullptr : Documents[0].Get();
	return Document && MeasureScreenHeight(*Document) > 0;
}

FReply FFolioWorldDocumentCustomization::OnApply()
{
	FScopedTransaction Transaction(LOCTEXT("SetPpu", "Set Pixels Per Unit From Viewport"));
	bool bChanged = false;

	for (const TWeakObjectPtr<UFolioWorldDocument>& Weak : Documents)
	{
		UFolioWorldDocument* Document = Weak.Get();
		if (!Document)
		{
			continue;
		}

		const int32 Height = MeasureScreenHeight(*Document);

		Document->Modify();
		if (Document->SetPixelsPerUnitForScreenHeight(Height))
		{
			FProperty* Property = UFolioWorldDocument::StaticClass()->FindPropertyByName(
				GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, PixelsPerUnit));

			FPropertyChangedEvent Changed(Property, EPropertyChangeType::ValueSet);
			Document->PostEditChangeProperty(Changed);
			bChanged = true;
		}
	}

	if (!bChanged)
	{
		Transaction.Cancel();
	}

	return FReply::Handled();
}

void FFolioWorldDocumentCustomization::CustomizeDetails(IDetailLayoutBuilder& Layout)
{
	for (const TWeakObjectPtr<UObject>& Object : Layout.GetSelectedObjects())
	{
		if (UFolioWorldDocument* Document = Cast<UFolioWorldDocument>(Object.Get()))
		{
			Documents.Add(Document);
		}
	}

	if (Documents.IsEmpty())
	{
		return;
	}

	IDetailCategoryBuilder& Category = Layout.EditCategory(TEXT("Folio"));

	Category.AddCustomRow(LOCTEXT("Density", "Density"))
	.NameContent()
	[
		SNew(STextBlock)
			.Text(LOCTEXT("DensityName", "Density"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(320.0f)
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
					.Text(LOCTEXT("SetPpu", "Set Pixels Per Unit From Viewport"))
					.ToolTipText(LOCTEXT("SetPpuTip",
						"Lays the document out at about one document pixel per screen pixel, as the "
						"surface appears in the active viewport. Put the camera where a player would "
						"read it from, then click."))
					.IsEnabled(this, &FFolioWorldDocumentCustomization::CanApply)
					.OnClicked(this, &FFolioWorldDocumentCustomization::OnApply)
			]

		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
					.Text(this, &FFolioWorldDocumentCustomization::GetNote)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.AutoWrapText(true)
			]
	];
}

#undef LOCTEXT_NAMESPACE
