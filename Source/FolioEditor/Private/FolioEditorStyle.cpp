#include "FolioEditorStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

namespace
{
	TSharedPtr<FSlateStyleSet> Style;
}

FName FFolioEditorStyle::GetStyleSetName()
{
	static const FName Name(TEXT("FolioEditorStyle"));
	return Name;
}

void FFolioEditorStyle::Register()
{
	if (Style.IsValid())
	{
		return;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Folio"));
	if (!Plugin)
	{
		return;
	}

	Style = MakeShared<FSlateStyleSet>(GetStyleSetName());
	Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	// Authored at the size it is drawn at, so nothing is resampled.
	Style->Set(GetToolbarIconName(), new FSlateImageBrush(
		Style->RootToContentDir(TEXT("Icon20"), TEXT(".png")), FVector2D(20.0, 20.0)));

	FSlateStyleRegistry::RegisterSlateStyle(*Style);
}

void FFolioEditorStyle::Unregister()
{
	if (!Style.IsValid())
	{
		return;
	}

	FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
	Style.Reset();
}
