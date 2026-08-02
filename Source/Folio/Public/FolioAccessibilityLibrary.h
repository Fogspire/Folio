#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FolioAccessibilityLibrary.generated.h"

/**
 * How much the reader says about a control beyond its name.
 *
 * Not a nicety. Someone who has learned a screen wants "Master volume, 60" and someone meeting it
 * wants to be told it is a slider and what it does, and each finds the other setting close to
 * unusable. Every real screen reader has this; a game that speaks for itself needs it too.
 */
UENUM(BlueprintType)
enum class EFolioReaderVerbosity : uint8
{
	/** The name, the state and the value. Nothing else. */
	Low,
	/** Adds what kind of control it is and the panel it sits in. The default. */
	Normal,
	/** Adds the `folio-description`, which is the consequence a name cannot carry. */
	High,
};

/**
 * How much of the punctuation in a field is named as it is typed.
 *
 * A separate axis from verbosity, and it has to be: someone entering a password or a file path wants
 * every symbol named, and someone entering their own name wants none of them.
 */
UENUM(BlueprintType)
enum class EFolioPunctuation : uint8
{
	/** None of it. A voice handed a bare comma says nothing, and that is what was asked for. */
	None,
	/** A single keystroke is named; a run is read as words. The default. */
	Some,
	/** Every symbol, wherever it appears. What a password or a command line needs. */
	All,
};

/**
 * What a game drives, on top of markup that is already annotated.
 *
 * A document annotated the way docs/accessibility.md describes is walked into a Slate accessibility
 * tree whenever a screen reader is listening, with no game code at all. What a game still owns is the
 * setting that turns its own reader on, and the things no element can say by existing.
 */
UCLASS(MinimalAPI)
class UFolioAccessibilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Turns the game's own screen reader on or off, and speaks the focused control from then on.
	 *
	 * This is the setting a player finds in an options menu. It is separate from a player running
	 * NVDA or a console narrator, who needs nothing turned on: that path reads the same tree through
	 * the platform. Off by default, always - a menu that starts talking unasked is a bug.
	 *
	 * Save it with the rest of your settings and apply it on load, before the first menu opens.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void SetScreenReader(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API bool IsScreenReaderEnabled();

	/**
	 * How fast it talks: -10 to 10, zero being the voice's own speed.
	 *
	 * Worth putting beside the toggle. Someone who relies on a screen reader usually runs it far
	 * faster than it sounds reasonable to anyone who does not.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void SetSpeechRate(int32 Rate);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API int32 GetSpeechRate();

	/** 0 to 100, against the system voice's own volume. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void SetSpeechVolume(int32 Volume);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API int32 GetSpeechVolume();

	/** Whether this platform can speak at all. False hides the setting rather than offering a dead one. */
	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API bool IsSpeechAvailable();
	/**
	 * Says something out loud, wherever the reader happens to be.
	 *
	 * For what no element can carry: a save completing, a round starting, a warning that is gone
	 * before anyone could navigate to it. A screen reader only reads what it is standing on.
	 *
	 * Best effort by nature - two announcements close together can stomp one another on some
	 * platforms - so never make this the only way a player can learn something.
	 *
	 * Markup asks for the same thing with `folio-announce` on a line that rewrites itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void Announce(const FString& Text);

	/**
	 * Whether anything is listening, by either route.
	 *
	 * True once the platform reports a screen reader has asked for the application's accessibility
	 * tree, and true while the game's own reader is on. Worth reading for the things that are only
	 * worth doing for that player - an audio cue on a state a sighted player can see, a slower default
	 * timer - and not for hiding UI from them.
	 */
	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API bool IsScreenReaderActive();

	/**
	 * Reads the next thing on screen without moving focus, and returns what was said.
	 *
	 * Focus reaches the controls. It does not reach a heading, a paragraph, a warning, a score or the
	 * label above the field you are standing in, and a player who can only hear what focus lands on
	 * hears a menu with all of its words removed. Every screen reader has a second cursor for this;
	 * a game that speaks for itself has to be the one that provides it.
	 *
	 * Bind these to something reachable one-handed. The set that matters is next, previous, repeat
	 * and read-all - the rest are conveniences.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReviewNext();

	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReviewPrevious();

	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReviewFirst();

	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReviewLast();

	/** Says the last thing said again, whichever part of the reader said it. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReviewRepeat();

	/**
	 * Jumps to the next or previous heading, skipping everything between.
	 *
	 * How a long screen is read without walking every line of it. Needs the document to use `<h1>` to
	 * `<h6>` or `folio-role="heading"`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReviewNextHeading();

	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReviewPreviousHeading();

	/**
	 * Reads from the cursor to the end of the screen.
	 *
	 * Queued rather than interrupting, so it is one reading. Stop it with `ReviewStop`, which is what
	 * any other key a player presses should also do.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReviewAll();

	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API void ReviewStop();

	/** Presses what the cursor is on, without focus having to be there first. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API bool ReviewActivate();

	/** Moves the document's own focus to the cursor, which is how a player rejoins the screen. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API bool ReviewMoveFocusHere();

	/**
	 * How much bigger than authored the type is, 1 being as authored. Clamped to 0.5 - 3.
	 *
	 * Reaches every rule written in `rem`, which is what the shipped theme uses, and no rule written
	 * in `dp`. Size your own type in `rem` if you want this setting to reach it, and check the result
	 * with `uipreview --text-scale 1.5`, which reports both the text it could not move and the text
	 * that no longer fits.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API void SetTextScale(float Scale);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API float GetTextScale();

	/**
	 * Turns transitions and animations off across every document.
	 *
	 * Not a taste setting: vestibular disorders make large movement genuinely unpleasant, and menus
	 * that slide, scale and parallax are the usual trigger. Anything moved by a stylesheet stops.
	 * Anything moved by code - a HUD spring, a shader, a video - has to read `IsReducedMotionOn` and
	 * snap, because nothing else can reach it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API void SetReducedMotion(bool bReduce);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API bool IsReducedMotionOn();

	/**
	 * The high-contrast theme, which the shipped stylesheet carries.
	 *
	 * Named rather than left as a theme string so a game does not hard-code one, and so a project that
	 * writes its own high-contrast rules has an obvious place to put them: `@media (theme:
	 * high-contrast)` in its own stylesheet.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API void SetHighContrast(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API bool IsHighContrastOn();

	/**
	 * How fast `<text>` types. 1 is as authored, 2 is twice as fast. Clamped to 0.1 - 10.
	 *
	 * Its own setting rather than part of reduced motion, because players look for it separately and
	 * because the two wants come apart: someone can need menus still and dialogue paced, or instant
	 * text and every transition intact.
	 *
	 * The multiplier divides the whole schedule, so `reveal-rate`, an authored `{delay:0.2}` and the
	 * punctuation beats all move together and the writer's pacing survives.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API void SetTextSpeed(float Multiplier);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API float GetTextSpeed();

	/** Every line arrives whole. The row a player finds as "instant text". */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API void SetInstantText(bool bInstant);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility|Presentation")
	static FOLIO_API bool IsInstantTextOn();

	/** How much the reader says beyond a control's name. Worth a row beside the speech rate. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void SetReaderVerbosity(EFolioReaderVerbosity Verbosity);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API EFolioReaderVerbosity GetReaderVerbosity();

	/** How much of a field's punctuation is named as it is typed. Its own row, beside verbosity. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void SetPunctuation(EFolioPunctuation Level);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API EFolioPunctuation GetPunctuation();

	/**
	 * Reads the word or the line the caret is in, or spells the word out.
	 *
	 * Typing is echoed as it happens; this is the other half, and it is what a player needs the moment
	 * they stop typing and start correcting. Nothing moves - the caret is where it was afterwards.
	 * Worth binding beside the review keys.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReadEditWord();

	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString ReadEditLine();

	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Review")
	static FOLIO_API FString SpellEditWord();

	/**
	 * Every line on screen, in reading order, as a screen reader would say it.
	 *
	 * Needs nothing turned on and costs one walk per open document, so call it when something asks
	 * rather than every frame. What it is good for beyond accessibility: driving a UI test by name
	 * instead of by selector, telling a tutorial which control the player has never found, checking a
	 * translated screen against the original, and handing a screen to something that has to reason
	 * about it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Reading")
	static FOLIO_API TArray<FString> DescribeScreen();

	/**
	 * The control the player is standing on, as a screen reader would say it. Empty when nothing is.
	 *
	 * Cheap, and needs nothing turned on. This is what an NPC reacting to someone hovering *Quit* for
	 * eight seconds reads, and what telemetry counts.
	 */
	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility|Reading")
	static FOLIO_API FString GetFocusedControl();

	/**
	 * Keeps the Slate accessibility tree built while something other than a screen reader needs it.
	 *
	 * Only for the *platform* tree - the widgets NVDA, a console narrator or a UI Automation harness
	 * walk. Reading a document from game code needs none of this: `DescribeScreen` and
	 * `GetFocusedControl` work off the document itself and are always available.
	 *
	 * Counted by reason, so two systems asking do not cancel one another and a request nobody released
	 * can be named. Balance every add with a remove; `folio.Accessibility.Dump` reports what is
	 * holding it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Reading")
	static FOLIO_API void AddDocumentTreeRequest(FName Reason);

	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility|Reading")
	static FOLIO_API void RemoveDocumentTreeRequest(FName Reason);

	/** Whether the Slate tree is being built at all, by any route. */
	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility|Reading")
	static FOLIO_API bool IsDocumentTreeBuilt();
};

/**
 * A tree request that releases itself.
 *
 * What native code should use rather than the two calls: the failure mode of the pair is an early
 * return that leaves the tree built for the rest of the session, which nothing visibly goes wrong
 * about.
 */
struct FFolioDocumentTreeRequest
{
	FOLIO_API explicit FFolioDocumentTreeRequest(FName InReason);
	FOLIO_API ~FFolioDocumentTreeRequest();

	FFolioDocumentTreeRequest(const FFolioDocumentTreeRequest&) = delete;
	FFolioDocumentTreeRequest& operator=(const FFolioDocumentTreeRequest&) = delete;

private:
	FName Reason;
};
