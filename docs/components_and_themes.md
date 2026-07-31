# Components, themes and recipes

Folio ships a component library, a default theme and a set of complete screens to copy. Three
directories, and the split between them is the one design decision everything else follows from.

| | |
|---|---|
| `/folio/lib/*.rcss` | **Structure.** Sizes, spacing, flex bases, scrollbar geometry, `tab-index` and `nav`. **No colour, anywhere.** |
| `/folio/themes/default.rcss` | **The tokens, and a skin for every library class.** Every colour lives here. |
| your game's stylesheet | Anything it disagrees with, and anything naming the game. |

Load them in that order. A game's rules land last and win by being later.

## Why a component carries no colour

RCSS has **no custom properties and no `var()`**. A stylesheet cannot hold a variable that a theme
replaces.

So a theme is not a swapped value - it is an **override block**, and the later rule wins. Which means
a colour written inside a component's own rule can only be overridden by restating that rule in full,
and the first game that disagrees with one colour forks the library.

Keeping colour out of `lib/` is what makes the library usable by a game that does not share its
palette. It looks arbitrary until you have tried the other way.

## Getting the library

RCSS has **no `@import`**. A stylesheet cannot pull in another one. There are exactly two ways to get
rules into a document:

```html
<link type="text/rcss" href="/ui/lib/button.rcss"/>   <!-- one per stylesheet -->
<style> /* document-local rules */ </style>
```

Listing eight component stylesheets in every document is how this becomes unmaintainable, so use the
one aggregation mechanism RmlUi has: **a template's head is merged into any document that links it.**

```html
<link type="text/template" href="/folio/lib/lib.rml"/>
```

One line, the whole library. The document does not need to *use* the template - merely linking it
merges the head.

Order: template heads merge first, then the document's own head, so a document's `<style>` block
always wins over the library. Within the library, later `<link>`s win at equal specificity.

## What is in it

Nine components, each structure-only:

`button` · `panel` · `list` · `modal` · `tabbar` · `toggle` (checkbox, radio, segmented) ·
`slider` · `field` · `tokens`

Compose these rather than starting from a bare `div`. They already have the `box-sizing`, the flex
bases and the scrollbar geometry right - and each of those is a documented trap in
**[layout](layout.md)**. They also carry their own `tab-index: auto` and `nav`, which is the thing
most hand-written components forget.

The utility classes in `tokens` are the vocabulary the recipes are written in:

| | |
|---|---|
| `.t-title` `.t-caption` `.t-numeric` | type scale |
| `.c-text` `.c-muted` `.c-dim` `.c-accent` `.c-danger` | colour |
| `.s-pad-*` `.s-gap-*` `.s-mb-*` | spacing |
| `.x-row` `.x-col` `.x-fill` | layout shorthands |
| `.focusable` | the focus ring, and the `tab-index` that makes it real |

## Recipes

`/folio/rcss/recipes/` holds complete, working screens - a main menu, a pause menu, a confirmation
dialog, a nameplate. They are **meant to be copied into your own `ui/`, not linked**.

That is deliberate. A recipe is a starting point you own and edit; a linked one is a dependency you
cannot change. Nothing in a recipe names Folio's directory except the library link.

All four pass `--strict --a11y` with no findings, and `./scripts/check_recipes.sh` keeps it that way.

**A recipe that binds a model needs a mock beside it.** `nameplate.rml` reads `Name` and `Health`, and
previews against `ui/mock/nameplate.json` in the plugin. Copy it into your own `ui/mock/` along with
the document, or the preview reports the binding as a missing variable.

Every recipe passes `--strict` and `--a11y` with no findings, so what you copy starts clean.

## Themes

A theme is `@media (theme: name)` in a stylesheet:

```css
@media (theme: high-contrast)
{
	.focusable:focus-visible { border-width: 3dp; border-color: #ffffff; }
	.is-disabled             { color: #9a9aa8; opacity: 1; }
}
```

Turn it on with `folio.Theme <name>`, **Set Theme** from Blueprint, or `uipreview --theme <name>`.

**Several themes can be on at once.** They are independent switches, not a list to pick from, so
`colour-blind` and `large-text` compose without either stylesheet knowing about the other.

**A theme is global, not per document.** RmlUi tracks it per context, so Folio walks every context
when one changes and remembers it for contexts created later - a screen opened after the switch comes
up in the new look rather than the old one.

`high-contrast` ships in the default theme. It is an accessibility setting rather than a taste one,
so it widens the focus ring as well as brightening it, and stops disabled controls relying on opacity
alone.

## Overriding the shipped look

Your stylesheet loads last, so a plain restatement wins:

```css
/* ui/theme.rcss - loaded after the plugin's default */
.btn         { background-color: #2a1f14; border-color: #6b4a25; }
.btn:hover   { background-color: #3a2a1c; }
.c-accent    { color: #d9a441; }
```

Two things worth doing while you are there:

**Keep the token classes and retune them** rather than inventing your own. Every component and every
recipe is written against `.c-accent` and `.s-pad-m`; changing what those mean recolours the whole
library at once.

**Re-run `--a11y` after retuning colours.** The shipped theme passes contrast on every surface it
puts text on; your palette has not been checked. `./scripts/check_a11y.sh ui/` does the sweep.

## Templates for markup

The same template mechanism carries markup, not just stylesheet links, which is how a shared chrome -
a screen frame, a header bar - is written once:

```html
<template name="screen" content="content">
<head>
	<link type="text/rcss" href="/ui/theme.rcss"/>
</head>
<body class="screen">
	<div class="topbar">…</div>
	<div id="content"/>
</body>
</template>
```

A document naming that template has its own body substituted into `content`.

Never write an angle-bracketed `head`, `body` or `template` tag inside a comment in a template file.
The loader finds those three by raw text search before any XML parsing, so a mention inside a comment
is indistinguishable from the real thing and truncates the file at the wrong place.

## Where to go next

- **[Layout and panels](layout.md)** — what the components are solving for you.
- **[Accessibility](accessibility.md)** — the guarantee the default theme gives, and how to keep it.
- **[Localisation](localization.md)** — `data-loc` on everything the player reads.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §10 and §13 — composition and themes in full.
