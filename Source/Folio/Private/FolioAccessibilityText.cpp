#include "FolioAccessibilityText.h"

#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "FolioAccessibility"

namespace Folio::Spoken
{
	FText Role(const ESemanticRole InRole)
	{
		switch (InRole)
		{
		case ESemanticRole::Image: return LOCTEXT("RoleImage", "image");
		case ESemanticRole::Button: return LOCTEXT("RoleButton", "button");
		case ESemanticRole::Link: return LOCTEXT("RoleLink", "link");
		case ESemanticRole::CheckBox: return LOCTEXT("RoleCheckBox", "check box");
		case ESemanticRole::Radio: return LOCTEXT("RoleRadio", "radio button");
		case ESemanticRole::ComboBox: return LOCTEXT("RoleComboBox", "combo box");
		case ESemanticRole::Slider: return LOCTEXT("RoleSlider", "slider");
		case ESemanticRole::ProgressBar: return LOCTEXT("RoleProgress", "progress");
		case ESemanticRole::TextEdit: return LOCTEXT("RoleTextEdit", "edit");
		case ESemanticRole::List: return LOCTEXT("RoleList", "list");
		case ESemanticRole::ListItem: return LOCTEXT("RoleListItem", "item");

		// Text says itself, and a group is a place rather than a thing to hear the kind of.
		default: return FText::GetEmpty();
		}
	}

	FText Checked(const bool bChecked)
	{
		return bChecked ? LOCTEXT("StateChecked", "checked") : LOCTEXT("StateNotChecked", "not checked");
	}

	FText Unavailable()
	{
		return LOCTEXT("StateUnavailable", "unavailable");
	}

	FText ReadOnly()
	{
		return LOCTEXT("StateReadOnly", "read only");
	}

	FText ReaderOn()
	{
		return LOCTEXT("ReaderOn", "Screen reader on");
	}

	FText Blank()
	{
		return LOCTEXT("FieldBlank", "blank");
	}

	FText Deleted(const FText& What)
	{
		return FText::Format(LOCTEXT("EditDeleted", "deleted {0}"), What);
	}

	FText Selected(const FText& What)
	{
		return FText::Format(LOCTEXT("EditSelected", "selected {0}"), What);
	}

	FText TopOfScreen()
	{
		return LOCTEXT("ReviewTop", "top");
	}

	FText EndOfScreen()
	{
		return LOCTEXT("ReviewEnd", "end of screen");
	}

	FText NothingOnScreen()
	{
		return LOCTEXT("ReviewEmpty", "nothing on screen");
	}

	FText Character(const TCHAR InCharacter)
	{
		switch (InCharacter)
		{
		case TEXT(' '): return LOCTEXT("CharSpace", "space");
		case TEXT('\t'): return LOCTEXT("CharTab", "tab");
		case TEXT('\n'): return LOCTEXT("CharNewLine", "new line");
		case TEXT('.'): return LOCTEXT("CharPeriod", "period");
		case TEXT(','): return LOCTEXT("CharComma", "comma");
		case TEXT(';'): return LOCTEXT("CharSemicolon", "semicolon");
		case TEXT(':'): return LOCTEXT("CharColon", "colon");
		case TEXT('\''): return LOCTEXT("CharApostrophe", "apostrophe");
		case TEXT('"'): return LOCTEXT("CharQuote", "quote");
		case TEXT('-'): return LOCTEXT("CharDash", "dash");
		case TEXT('_'): return LOCTEXT("CharUnderscore", "underscore");
		case TEXT('/'): return LOCTEXT("CharSlash", "slash");
		case TEXT('\\'): return LOCTEXT("CharBackslash", "backslash");
		case TEXT('!'): return LOCTEXT("CharExclamation", "exclamation");
		case TEXT('?'): return LOCTEXT("CharQuestion", "question");
		case TEXT('@'): return LOCTEXT("CharAt", "at");
		case TEXT('#'): return LOCTEXT("CharHash", "hash");
		case TEXT('$'): return LOCTEXT("CharDollar", "dollar");
		case TEXT('%'): return LOCTEXT("CharPercent", "percent");
		case TEXT('&'): return LOCTEXT("CharAmpersand", "and");
		case TEXT('*'): return LOCTEXT("CharAsterisk", "star");
		case TEXT('+'): return LOCTEXT("CharPlus", "plus");
		case TEXT('='): return LOCTEXT("CharEquals", "equals");
		case TEXT('('): return LOCTEXT("CharOpenParen", "left bracket");
		case TEXT(')'): return LOCTEXT("CharCloseParen", "right bracket");
		case TEXT('['): return LOCTEXT("CharOpenSquare", "left square bracket");
		case TEXT(']'): return LOCTEXT("CharCloseSquare", "right square bracket");
		case TEXT('{'): return LOCTEXT("CharOpenBrace", "left brace");
		case TEXT('}'): return LOCTEXT("CharCloseBrace", "right brace");
		case TEXT('<'): return LOCTEXT("CharLess", "less than");
		case TEXT('>'): return LOCTEXT("CharGreater", "greater than");
		case TEXT('|'): return LOCTEXT("CharPipe", "pipe");
		case TEXT('~'): return LOCTEXT("CharTilde", "tilde");
		case TEXT('^'): return LOCTEXT("CharCaret", "caret");
		case TEXT('`'): return LOCTEXT("CharBacktick", "backtick");
		default: return FText::FromString(FString::Chr(InCharacter));
		}
	}

	FText Typed(const FString& Text)
	{
		// One character is named, because that is a keystroke and a bare comma is silence. A run of
		// them is read as words: a pasted address said one symbol at a time is unusable.
		if (Text.Len() == 1)
		{
			return Character(Text[0]);
		}

		return FText::FromString(Text);
	}

	FString Join(const TArray<FString>& Parts)
	{
		return FString::Join(Parts, *LOCTEXT("UtteranceSeparator", ", ").ToString());
	}

	FText Number(const FString& Digits)
	{
		if (Digits.IsNumeric() || Digits.Contains(TEXT(".")))
		{
			double Value = 0.0;
			if (LexTryParseString(Value, *Digits))
			{
				return FText::AsNumber(Value);
			}
		}

		return FText::FromString(Digits);
	}
}

#undef LOCTEXT_NAMESPACE
