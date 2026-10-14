# Subtitles and closed captions

A stack of spoken lines at the foot of the frame, newest at the bottom, each on its own clock.

The shape is a long-standing one in shooters, and there is a reason for that: it is the only arrangement
that survives two people talking over each other, which is what happens the moment a game gets loud.
A few lines at a time, the newest bright and the ones above it dimmed, and each one leaving when it
has had its say rather than when the next one arrives.

`rcss/recipes/subtitles.rml` is a complete one. Copy it into your `UI/screens/` and change the
palette and the type.

## The document draws; the queue is yours

This is the split that matters, and the queue is most of the feature. The document reads a list and
knows nothing about time, sound or speakers. Everything below lives in your game.

```cpp
USTRUCT()
struct FMySubtitleLine
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FString Speaker;
    UPROPERTY(BlueprintReadOnly) bool    bHasSpeaker = false;
    UPROPERTY(BlueprintReadOnly) FString Text;

    /** Which speaker colour, as an index the stylesheet turns into a colour. */
    UPROPERTY(BlueprintReadOnly) int32   Voice = 0;

    /** The most recent line. Everything above it is dimmed. */
    UPROPERTY(BlueprintReadOnly) bool    bFresh = false;

    /** Past its time and playing its way off. See "Leaving" below - this one is not optional. */
    UPROPERTY(BlueprintReadOnly) bool    bLeaving = false;

    /** A sound rather than speech, which a closed caption sets apart. */
    UPROPERTY(BlueprintReadOnly) bool    bCue = false;
};
```

The model itself holds `TArray<FMySubtitleLine> Lines`, a `bool bAny`, and a `float Offset` that
moves the stack up the frame as a fraction of the viewport.

## How long a line holds

**Off the written length, not off the sound.** This is the mistake worth naming: a voice line's audio
has a breath in front of it, a tail behind it and often a second of room tone, so a five word line
arrives with six seconds of sound attached. Reading it takes as long as reading it takes.

```cpp
float ReadingTime(const FString& Text)
{
    // A floor because the eye needs longer to notice a caption appear than to read a short one once
    // noticed, and a ceiling because a caption that outstays its moment stops being read and starts
    // being scenery.
    return FMath::Clamp(1.4f + Text.Len() * 0.045f, 1.8f, 7.0f);
}
```

Per character rather than per word, because a Japanese or Chinese line has no spaces in it and
counting words in one gives a caption that is gone before it has been seen.

**Drop the oldest rather than refusing the newest** when the stack is full. What was just said is
always the thing the player most needs, and a queue that fills up and stops is a queue that stops
during a fight.

## Leaving

**A row taken out of a data array is destroyed on the spot, so a line cannot animate its own removal.**
This is the one that catches everybody. Removing the entry the frame its time runs out gives you no
exit at all - the element is simply gone, and what you see is a single frame of something and then
nothing.

Hold it in the list instead:

```cpp
for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
{
    FLine& Line = Lines[Index];
    Line.Left -= DeltaSeconds;

    if (Line.Left > 0.f)
    {
        continue;
    }

    if (!Line.bLeaving)
    {
        Line.bLeaving = true;     // the document hangs its exit on this
        Line.Left = ExitSeconds;  // and it has to agree with the RCSS
        continue;
    }

    Lines.RemoveAt(Index);
}
```

Two consequences worth having:

- **A leaving line should stop counting toward the maximum.** Otherwise a new line cuts a departing
  one's exit short and replaces it with nothing, which is the bug you were fixing.
- **A leaving line is nobody's newest**, even when it is the only one left, or it holds the bright
  styling all the way out.

## Do not dim or fade with opacity

Opacity is inherited in RCSS, unlike CSS, and multiplied into premultiplied vertex colours rather
than compositing the subtree as one image. A caption is a dark slab with light type over it, so a
fading one blows out to white where the two overlap and is correct at both ends.

| | |
|---|---|
| Ageing a line | A quieter ground and quieter type. `.sub.old { background-color: ... }` |
| Taking one away | A transform. The recipe wipes with `scaleX`, from the edge the tick is on |

`filter` would composite it properly and costs a render target every frame. A caption stack is not
where to spend one.

## Colour is not identity

**Always write the speaker's name.** A palette that four people can tell apart at a glance is not one
that everybody can, and captions are read by exactly the players least served by leaning on hue.
Colour reinforces the name; it never replaces it.

That also settles what to do when a game has more voices than colours: wrap the palette. Two
characters sharing a colour is fine when both are named.

**Claim a voice's colour when it comes into the world, not when it speaks.** A colour handed out per
line is given back the moment a caption expires, so a character who speaks twice with a pause between
is two different people as far as the player can tell. A round robin over the palette, wrapping, held
for the lifetime of whatever owns the voice.

## What the recipe does not do

Two things are worth adding and are deliberately not in it, because they are not what a caption is:

- **A direction arrow**, pointing at the speaker. Genuinely useful in a game where hearing someone you
  cannot see is the point. Rotate it by the flat bearing from the *player's body* to the speaker,
  taken against the camera's yaw - measuring from the camera puts it several metres behind the player
  in third person, which is wrong most exactly at close range where it matters.

  Build the arrowhead from solid bars. The usual trick of collapsing a box to nothing and letting its
  borders mitre into a triangle draws correctly at every quarter turn and upside down at every bearing
  between them.

- **A marker over the speaker's head**, in the line's colour. See
  [markers](markers.md); the colour shared between the caption and the head is a link a player reads
  without being told there is one.

## Where to go next

- **[Data binding](data_binding.md)** — driving the list from gameplay.
- **[Markers](markers.md)** — the mark over the speaker's head.
- **[Accessibility](accessibility.md)** — the contrast and text-scale checks a caption has to pass.
