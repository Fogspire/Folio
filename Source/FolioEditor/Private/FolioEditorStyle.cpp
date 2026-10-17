#include "FolioEditorStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace
{
	TSharedPtr<FSlateStyleSet> StyleSet;
}

FName FFolioEditorStyle::GetStyleSetName()
{
	static const FName Name(TEXT("FolioEditorStyle"));
	return Name;
}

void FFolioEditorStyle::Register()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Folio"));
	if (!Plugin)
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	StyleSet->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	// Authored at the size it is drawn at, so nothing is resampled.
	StyleSet->Set(GetToolbarIconName(), new FSlateImageBrush(
		StyleSet->RootToContentDir(TEXT("Icon20"), TEXT(".png")), FVector2D(20.0, 20.0)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FFolioEditorStyle::Unregister()
{
	if (!StyleSet.IsValid())
	{
		return;
	}

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
	StyleSet.Reset();
}
