#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

/**
 * Gets gamepad input to a focused document ahead of whatever else wants it.
 *
 * Slate focus is not enough on its own. Input pre-processors run before any widget sees an event
 * and may consume it, and CommonUI registers one - FCommonAnalogCursor - whenever the project's
 * viewport client is a UCommonGameViewportClient, which is a line in DefaultEngine.ini rather than
 * anything a UI library gets asked about. The result is a document that navigates perfectly from
 * the keyboard and not at all from a pad, which reads as Folio being broken.
 *
 * So Folio registers earlier and takes gamepad events for itself, but only while a canvas actually
 * holds focus, and only gamepad ones: keyboard and mouse routing is left exactly as Slate does it,
 * and the moment focus leaves a document every event falls through untouched. A game whose viewport
 * client is not CommonUI's loses nothing by this being here.
 */
class FFolioInputPreprocessor final : public IInputProcessor
{
public:
	static void Register();
	static void Unregister();

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;

	virtual const TCHAR* GetDebugName() const override { return TEXT("Folio"); }
};
