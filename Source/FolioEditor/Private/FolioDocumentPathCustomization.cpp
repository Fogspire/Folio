#include "FolioDocumentPathCustomization.h"

#include "FolioDocumentPath.h"
#include "SFolioDocumentTree.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FolioEditor"

TSharedRef<IPropertyTypeCustomization> FFolioDocumentPathCustomization::MakeInstance()
{
	return MakeShared<FFolioDocumentPathCustomization>();
}

FString FFolioDocumentPathCustomization::GetPath() const
{
	FString Value;
	if (PathHandle.IsValid())
	{
		PathHandle->GetValue(Value);
	}
	return Value;
}

void FFolioDocumentPathCustomization::SetPath(const FString& Path)
{
	if (PathHandle.IsValid())
	{
		// Through the handle rather than by writing the struct, so it lands on every object being
		// edited at once, marks the package dirty and goes on the transaction stack.
		PathHandle->SetValue(UFolioDocumentPathLibrary::MakeDocumentPathPortable(Path));
	}

	ApplyDeclaredSize(Path);

	if (ComboButton.IsValid())
	{
		ComboButton->SetIsOpen(false);
	}
}

void FFolioDocumentPathCustomization::ApplyDeclaredSize(const FString& Path)
{
	if (!StructHandle.IsValid() || Path.IsEmpty())
	{
		return;
	}

	const FString SizeProperty = StructHandle->GetMetaData(TEXT("FolioSizeProperty"));
	if (SizeProperty.IsEmpty())
	{
		return;
	}

	FVector2D Declared;
	if (!UFolioDocumentPathLibrary::GetDeclaredDocumentSize(Path, Declared))
	{
		return;
	}

	TArray<UObject*> Objects;
	StructHandle->GetOuterObjects(Objects);

	if (Objects.IsEmpty())
	{
		return;
	}

	// Named rather than assumed, because a world document's size is in world units and everything
	// else's is in pixels.
	const FString DivisorProperty = StructHandle->GetMetaData(TEXT("FolioSizeDivisor"));

	FScopedTransaction Transaction(LOCTEXT("SetDeclaredSize", "Set Folio Document Size"));
	bool bChanged = false;

	for (UObject* Object : Objects)
	{
		if (!Object)
		{
			continue;
		}

		FStructProperty* Target = CastField<FStructProperty>(
			Object->GetClass()->FindPropertyByName(FName(*SizeProperty)));

		if (!Target || Target->Struct != TBaseStructure<FVector2D>::Get())
		{
			continue;
		}

		FVector2D Size = Declared;

		// Read as it stands, so changing the divisor afterwards does not retroactively rescale a size
		// that was already written.
		if (!DivisorProperty.IsEmpty())
		{
			if (const FFloatProperty* Divisor = CastField<FFloatProperty>(
				Object->GetClass()->FindPropertyByName(FName(*DivisorProperty))))
			{
				const float Scale = Divisor->GetPropertyValue_InContainer(Object);
				if (Scale > 0.0f)
				{
					Size /= Scale;
				}
			}
		}

		Object->Modify();
		*Target->ContainerPtrToValuePtr<FVector2D>(Object) = Size;

		FPropertyChangedEvent Changed(Target, EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(Changed);
		bChanged = true;
	}

	if (!bChanged)
	{
		Transaction.Cancel();
	}
}

FText FFolioDocumentPathCustomization::GetDisplayText() const
{
	const FString Path = GetPath();
	return Path.IsEmpty() ? LOCTEXT("NoDocument", "None") : FText::FromString(Path);
}

EVisibility FFolioDocumentPathCustomization::GetWarningVisibility() const
{
	const FString Path = GetPath();

	// An empty field is not an error - a component may be given its document from code - so only a
	// path that names something absent is worth marking.
	return !Path.IsEmpty() && !UFolioDocumentPathLibrary::DocumentExists(Path)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FText FFolioDocumentPathCustomization::GetWarningTooltip() const
{
	const FString Path = GetPath();
	return FText::Format(
		LOCTEXT("MissingDocument", "'{0}' does not exist. It resolves to '{1}'."),
		FText::FromString(Path),
		FText::FromString(UFolioDocumentPathLibrary::ResolveDocumentPath(Path)));
}

void FFolioDocumentPathCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> Handle,
	FDetailWidgetRow& Row, IPropertyTypeCustomizationUtils&)
{
	StructHandle = Handle;
	PathHandle = Handle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FFolioDocumentPath, Path));

	Row
	.NameContent()
	[
		Handle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(280.0f)
	[
		SNew(SHorizontalBox)

		// Marked rather than left to be discovered at runtime: a path that resolves to nothing renders
		// nothing, which is indistinguishable from a document that loaded and is laid out wrong.
		+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Warning")))
					.Visibility(this, &FFolioDocumentPathCustomization::GetWarningVisibility)
					.ToolTipText(this, &FFolioDocumentPathCustomization::GetWarningTooltip)
			]

		+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(ComboButton, SComboButton)
					.OnGetMenuContent(this, &FFolioDocumentPathCustomization::MakeMenu)
					.ContentPadding(FMargin(4.0f, 2.0f))
					.ButtonContent()
					[
						SNew(STextBlock)
							.Text(this, &FFolioDocumentPathCustomization::GetDisplayText)
							.Font(IDetailLayoutBuilder::GetDetailFont())
					]
			]
	];
}

TSharedRef<SWidget> FFolioDocumentPathCustomization::MakeMenu()
{
	TSharedRef<SFolioDocumentTree> Picker = SNew(SFolioDocumentTree)
		.CurrentPath(GetPath())
		.OnDocumentPicked(SFolioDocumentTree::FOnDocumentPicked::CreateSP(
			this, &FFolioDocumentPathCustomization::SetPath));

	ComboButton->SetMenuContentWidgetToFocus(Picker->GetWidgetToFocusOnOpen());
	return Picker;
}

void FFolioDocumentPathCustomization::CustomizeChildren(TSharedRef<IPropertyHandle>,
	IDetailChildrenBuilder&, IPropertyTypeCustomizationUtils&)
{
	// Nothing. The header is the whole control, and showing the raw string underneath it would offer a
	// second way to set the value that skips the portability rewrite.
}

#undef LOCTEXT_NAMESPACE
