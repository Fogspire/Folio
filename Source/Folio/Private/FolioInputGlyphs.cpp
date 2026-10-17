#include "FolioInputGlyphs.h"

#include "FolioModule.h"
#include "FolioTextures.h"

#include "CommonInputBaseTypes.h"
#include "CommonInputSubsystem.h"
#include "Engine/Texture.h"

namespace
{
	/** Key to row, flattened as tables are registered so a lookup is not a walk per prompt. */
	TMap<FKey, FFolioKeyGlyph> GGlyphs;

	EFolioPadScheme GPadScheme = EFolioPadScheme::Xbox;

	/**
	 * What a pad's buttons are called, per scheme.
	 *
	 * The glyph table still wins: this is the answer when a project has not said otherwise, and it is
	 * a better one than "Gamepad Face Button Bottom".
	 */
	const TCHAR* PadLabel(const FKey& Key)
	{
		const bool bSony = GPadScheme == EFolioPadScheme::PlayStation;

		if (Key == EKeys::Gamepad_FaceButton_Bottom) { return bSony ? TEXT("X") : TEXT("A"); }
		if (Key == EKeys::Gamepad_FaceButton_Right)  { return bSony ? TEXT("O") : TEXT("B"); }
		if (Key == EKeys::Gamepad_FaceButton_Left)   { return bSony ? TEXT("SQUARE") : TEXT("X"); }
		if (Key == EKeys::Gamepad_FaceButton_Top)    { return bSony ? TEXT("TRIANGLE") : TEXT("Y"); }

		if (Key == EKeys::Gamepad_LeftShoulder)  { return bSony ? TEXT("L1") : TEXT("LB"); }
		if (Key == EKeys::Gamepad_RightShoulder) { return bSony ? TEXT("R1") : TEXT("RB"); }
		if (Key == EKeys::Gamepad_LeftTrigger)   { return bSony ? TEXT("L2") : TEXT("LT"); }
		if (Key == EKeys::Gamepad_RightTrigger)  { return bSony ? TEXT("R2") : TEXT("RT"); }

		if (Key == EKeys::Gamepad_LeftThumbstick)  { return bSony ? TEXT("L3") : TEXT("LS"); }
		if (Key == EKeys::Gamepad_RightThumbstick) { return bSony ? TEXT("R3") : TEXT("RS"); }

		if (Key == EKeys::Gamepad_Special_Left)  { return bSony ? TEXT("CREATE") : TEXT("VIEW"); }
		if (Key == EKeys::Gamepad_Special_Right) { return bSony ? TEXT("OPTIONS") : TEXT("MENU"); }

		if (Key == EKeys::Gamepad_DPad_Up)    { return TEXT("D-UP"); }
		if (Key == EKeys::Gamepad_DPad_Down)  { return TEXT("D-DOWN"); }
		if (Key == EKeys::Gamepad_DPad_Left)  { return TEXT("D-LEFT"); }
		if (Key == EKeys::Gamepad_DPad_Right) { return TEXT("D-RIGHT"); }

		if (Key == EKeys::Gamepad_LeftStick_Up)    { return TEXT("LS UP"); }
		if (Key == EKeys::Gamepad_LeftStick_Down)  { return TEXT("LS DOWN"); }
		if (Key == EKeys::Gamepad_LeftStick_Left)  { return TEXT("LS LEFT"); }
		if (Key == EKeys::Gamepad_LeftStick_Right) { return TEXT("LS RIGHT"); }

		if (Key == EKeys::Gamepad_RightStick_Up)    { return TEXT("RS UP"); }
		if (Key == EKeys::Gamepad_RightStick_Down)  { return TEXT("RS DOWN"); }
		if (Key == EKeys::Gamepad_RightStick_Left)  { return TEXT("RS LEFT"); }
		if (Key == EKeys::Gamepad_RightStick_Right) { return TEXT("RS RIGHT"); }

		return nullptr;
	}

	TArray<EFolioGlyphSource> GKeyboardOrder = { EFolioGlyphSource::Drawn };
	TArray<EFolioGlyphSource> GGamepadOrder = { EFolioGlyphSource::Drawn };

	/**
	 * CommonInput's art for a key, registered as a Folio texture so a document can name it.
	 *
	 * Registered under the key and the device, so the same key asked for twice is the same texture and
	 * the cache does the work. The device is part of the name because a face button has one picture per
	 * pad: under the key alone, picking up a second pad would hand every document the new art under the
	 * old name. Empty when CommonInput has nothing, which is the ordinary case for a project that has
	 * not shipped controller art.
	 */
	FString ImageFor(const FKey& Key, const ULocalPlayer* LocalPlayer)
	{
		const UCommonInputSubsystem* Input = UCommonInputSubsystem::Get(LocalPlayer);
		const UCommonInputPlatformSettings* Settings = UCommonInputPlatformSettings::Get();
		if (!Input || !Settings)
		{
			return FString();
		}

		const ECommonInputType Device = Input->GetCurrentInputType();
		const FName Gamepad = Input->GetCurrentGamepadName();

		FSlateBrush Brush;
		if (!Settings->TryGetInputBrush(Brush, Key, Device, Gamepad))
		{
			return FString();
		}

		UTexture* Texture = Cast<UTexture>(Brush.GetResourceObject());
		if (!Texture)
		{
			return FString();
		}

		const FName Name(*FString::Printf(TEXT("key/%s/%s"),
			Device == ECommonInputType::Gamepad ? *Gamepad.ToString() : TEXT("kbm"),
			*Key.ToString()));
		Folio::RegisterTexture(Name, Texture);

		return FString::Printf(TEXT("%s%s"), Folio::TextureScheme, *Name.ToString());
	}
}

void Folio::RegisterGlyphTable(const UDataTable* Table)
{
	if (!Table)
	{
		return;
	}

	if (Table->GetRowStruct() != FFolioKeyGlyph::StaticStruct())
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' is not a table of FFolioKeyGlyph rows, so no prompt will find a glyph in it."),
			*Table->GetName());
		return;
	}

	int32 Added = 0;
	Table->ForeachRow<FFolioKeyGlyph>(TEXT("Folio glyphs"),
		[&Added](const FName&, const FFolioKeyGlyph& Row)
		{
			if (Row.Key.IsValid())
			{
				GGlyphs.Add(Row.Key, Row);
				++Added;
			}
		});

	UE_LOG(LogFolio, Log, TEXT("registered %d key glyph(s) from '%s'"), Added, *Table->GetName());
}

void Folio::ClearGlyphTables()
{
	GGlyphs.Empty();
}

FName Folio::GlyphFor(const FKey& Key)
{
	const FFolioKeyGlyph* Row = GGlyphs.Find(Key);
	return Row ? Row->Sprite : NAME_None;
}

FText Folio::LabelFor(const FKey& Key)
{
	if (!Key.IsValid())
	{
		return FText::GetEmpty();
	}

	if (const FFolioKeyGlyph* Row = GGlyphs.Find(Key); Row && !Row->Label.IsEmpty())
	{
		return Row->Label;
	}

	if (const TCHAR* Pad = PadLabel(Key))
	{
		return FText::FromString(Pad);
	}

	return Key.GetDisplayName(/*bLongDisplayName*/ false);
}

void Folio::SetGlyphSources(const TArray<EFolioGlyphSource>& Keyboard,
	const TArray<EFolioGlyphSource>& Gamepad)
{
	GKeyboardOrder = Keyboard.IsEmpty() ? TArray<EFolioGlyphSource>{ EFolioGlyphSource::Drawn } : Keyboard;
	GGamepadOrder = Gamepad.IsEmpty() ? TArray<EFolioGlyphSource>{ EFolioGlyphSource::Drawn } : Gamepad;
}

FString Folio::ShapeFor(const FKey& Key)
{
	if (Key == EKeys::LeftMouseButton) { return TEXT("mouse left"); }
	if (Key == EKeys::RightMouseButton) { return TEXT("mouse right"); }
	if (Key == EKeys::MiddleMouseButton || Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown) { return TEXT("mouse middle"); }
	if (Key == EKeys::ThumbMouseButton) { return TEXT("mouse side-one"); }
	if (Key == EKeys::ThumbMouseButton2) { return TEXT("mouse side-two"); }
	if (!Key.IsGamepadKey()) { return TEXT("key"); }

	if (Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Gamepad_FaceButton_Right
		|| Key == EKeys::Gamepad_FaceButton_Left || Key == EKeys::Gamepad_FaceButton_Top)
	{
		return TEXT("face");
	}
	if (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder) { return TEXT("bumper"); }
	if (Key == EKeys::Gamepad_LeftTrigger || Key == EKeys::Gamepad_RightTrigger
		|| Key == EKeys::Gamepad_LeftTriggerAxis || Key == EKeys::Gamepad_RightTriggerAxis)
	{
		return TEXT("trigger");
	}
	if (Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down
		|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right)
	{
		return TEXT("dpad");
	}
	if (Key == EKeys::Gamepad_LeftThumbstick || Key == EKeys::Gamepad_RightThumbstick
		|| Key == EKeys::Gamepad_Left2D || Key == EKeys::Gamepad_Right2D
		|| Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY
		|| Key == EKeys::Gamepad_RightX || Key == EKeys::Gamepad_RightY)
	{
		return TEXT("stick");
	}
	return TEXT("key");
}

FFolioGlyph Folio::ResolveGlyph(const FKey& Key, const ULocalPlayer* LocalPlayer)
{
	FFolioGlyph Glyph;
	if (!Key.IsValid())
	{
		return Glyph;
	}

	// Always, whichever source wins: a drawn cap is the fallback if art turns out to be missing at
	// runtime, and it costs a string.
	Glyph.Kind = TEXT("drawn");
	Glyph.Cap = LabelFor(Key).ToString();
	Glyph.Shape = ShapeFor(Key);

	const TArray<EFolioGlyphSource>& Order = Key.IsGamepadKey() ? GGamepadOrder : GKeyboardOrder;

	for (const EFolioGlyphSource Source : Order)
	{
		if (Source == EFolioGlyphSource::Sprite)
		{
			if (const FName Sprite = GlyphFor(Key); !Sprite.IsNone())
			{
				Glyph.Kind = TEXT("sprite");
				Glyph.Sprite = Sprite.ToString();
				return Glyph;
			}
		}
		else if (Source == EFolioGlyphSource::Image)
		{
			if (FString Image = ImageFor(Key, LocalPlayer); !Image.IsEmpty())
			{
				Glyph.Kind = TEXT("image");
				Glyph.Image = MoveTemp(Image);
				return Glyph;
			}
		}
		else
		{
			return Glyph;
		}
	}

	return Glyph;
}

void Folio::SetPadScheme(const EFolioPadScheme Scheme)
{
	GPadScheme = Scheme;
}

EFolioPadScheme Folio::GetPadScheme()
{
	return GPadScheme;
}
