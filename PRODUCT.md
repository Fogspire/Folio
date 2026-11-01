# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Docs site: static HTML generated from the repository's markdown by a small dependency-light Node build script, run by a GitHub Actions workflow and deployed to GitHub Pages from the same repository. The markdown stays the source of truth and must keep rendering correctly on github.com.

## Users

Unreal Engine 5 game developers (solo to AAA) evaluating or integrating Folio as their game's UI layer: gameplay/UI programmers, technical designers, and UI designers who author in Folio Studio. Also AI agents handed AGENTS.md. They arrive either deciding whether to adopt Folio, or mid-task looking up a property, a trap, or a tutorial step.

## Product Purpose

Folio is a UI rendering framework for UE5: HTML/CSS-style markup (RmlUi RML/RCSS) instead of UMG/Slate widget graphs. The docs site makes the existing markdown documentation browsable: a landing page that makes the case, then navigable, searchable docs. Success: a new developer gets a document on screen via Hello world, and a working developer finds the page that answers their question in seconds.

## Positioning

Distance-field text and vector art sharp at any scale; world UI drawn after the tonemapper so TAA/TSR and motion blur never smear it; surface-projected UI with input; Folio Studio, a cross-platform WYSIWYG editor drawing with the same renderer as the game; text files that diff and merge instead of binary .uasset.

## Operating Context

Developers read docs beside Unreal, Folio Studio, or an editor, often on a second monitor. The repo is public at github.com/Fogspire/Folio. Pages link to source files, UI_CONVENTIONS.md, and AGENTS.md.

## Capabilities and Constraints

- Source docs: docs/overview.md (landing page copy), CHANGELOG.md, docs/*.md, docs/tutorials/*.md, UI_CONVENTIONS.md, AGENTS.md. README.md is a minimal pointer to the site.
- Relative links between .md files must resolve on both github.com and the site.
- RCSS is not CSS: the docs repeatedly warn about this; code samples are RML/RCSS, C++, ini, bash.
- Latest version 1.5.1, beta flag in .uplugin. Requires UE 5.8+.
- License: MIT with the Commons Clause.

## Brand Commitments

- Name "Folio", made by Fogspire. Plugin icon: Resources/Icon256.png (a window frame with the word FOLIO).
- Voice: terse, direct, honest about limits ("Honest limits", known gaps page).

## Evidence on Hand

- Showcase videos and screenshots hosted as github user-attachments (URLs in docs/overview.md).
- docs/images/*.png|gif and docs/tutorials/images.
- No testimonials, adopters, benchmarks, or download counts exist; do not invent them.

## Product Principles

- Get it working first: Hello world is always one click away.
- Truth over pitch: limits and traps are surfaced, not hidden.
- Markdown in the repo is the single source; the site never forks content.
- Fast to find: search and stable deep links to headings.
