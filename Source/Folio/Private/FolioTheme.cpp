#include "FolioTheme.h"

#include "FolioDocumentHost.h"

void UFolioThemeLibrary::SetTheme(const FName Theme, const bool bActive)
{
	FFolioDocumentHost::SetTheme(Theme, bActive);
}

bool UFolioThemeLibrary::IsThemeActive(const FName Theme)
{
	return FFolioDocumentHost::IsThemeActive(Theme);
}

TArray<FName> UFolioThemeLibrary::GetActiveThemes()
{
	return FFolioDocumentHost::GetActiveThemes().Array();
}
