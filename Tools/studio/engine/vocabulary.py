#!/usr/bin/env python3
"""Writes the RCSS vocabulary Folio Studio completes and checks against.

Read out of the sources that register it, so the editor offers exactly what the renderer parses: every
property RmlUi's StyleSheetSpecification registers with its default and keywords, the properties
FolioCore adds, and the names of every decorator, filter and font effect either one instances.

  vocabulary.py                    finds RmlUi under build/*/_deps/rmlui-src
  vocabulary.py --rmlui <dir>      a checkout of RmlUi to read instead
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[3]
OUTPUT = REPO / "Tools" / "studio" / "app" / "src" / "lang" / "vocabulary.json"

STRING = r'"((?:[^"\\]|\\.)*)"'
PROPERTY = re.compile(r"RegisterProperty\(\s*(?:PropertyId::\w+\s*,\s*)?" + STRING + r"\s*,\s*" + STRING + r"\s*(?:,\s*(true|false))?")
SHORTHAND = re.compile(r"RegisterShorthand\(\s*(?:ShorthandId::\w+\s*,\s*)?" + STRING + r"\s*,\s*" + STRING)
PARSER = re.compile(r"\.AddParser\(\s*" + STRING + r"(?:\s*,\s*" + STRING + r")?\s*\)")


def find_rmlui(explicit: str | None) -> pathlib.Path:
    if explicit:
        return pathlib.Path(explicit)
    # The WebAssembly build first, since that is the RmlUi Studio itself runs.
    candidates = sorted((REPO / "build").glob("*/_deps/rmlui-src"), key=lambda path: "wasm" not in path.parent.parent.name)
    if not candidates:
        sys.exit("RmlUi's source is not under build/. Build Folio once, or pass --rmlui.")
    return candidates[0]


def statements(text: str) -> list[str]:
    """The source split at semicolons, comments removed, so a call chained over several lines is one."""
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
    return text.split(";")


def read_properties(text: str, qualified_only: bool) -> tuple[dict, dict]:
    properties: dict[str, dict] = {}
    shorthands: dict[str, list[str]] = {}
    for statement in statements(text):
        # Inside an instancer, RegisterProperty describes a decorator's own arguments, not a property.
        if qualified_only and "StyleSheetSpecification::Register" not in statement:
            continue
        match = PROPERTY.search(statement)
        if match:
            keywords: list[str] = []
            parsers: list[str] = []
            for parser in PARSER.finditer(statement[match.end():]):
                parsers.append(parser.group(1))
                if parser.group(1) == "keyword" and parser.group(2):
                    keywords += [word.split("=")[0].strip() for word in parser.group(2).split(",") if word.strip()]
            properties[match.group(1)] = {
                "default": match.group(2),
                "inherited": match.group(3) == "true",
                "keywords": keywords,
                "parsers": [parser for parser in parsers if parser != "keyword"],
            }
            continue
        match = SHORTHAND.search(statement)
        if match:
            shorthands[match.group(1)] = [name.strip() for name in match.group(2).split(",") if name.strip()]
    return properties, shorthands


def instanced(text: str, kind: str) -> list[str]:
    return re.findall(r"Register" + kind + r"Instancer\(\s*" + STRING, text)


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    arguments.add_argument("--rmlui", help="an RmlUi checkout to read")
    options = arguments.parse_args()

    rmlui = find_rmlui(options.rmlui)
    specification = (rmlui / "Source" / "Core" / "StyleSheetSpecification.cpp").read_text(encoding="utf-8")
    factory = (rmlui / "Source" / "Core" / "Factory.cpp").read_text(encoding="utf-8")
    animation = (rmlui / "Source" / "Core" / "PropertyParserAnimation.cpp").read_text(encoding="utf-8")
    core = REPO / "Source" / "FolioCore" / "Public" / "FolioCore"
    decorators = (core / "FolioDecorators.h").read_text(encoding="utf-8")
    filters = (core / "FolioFilters.h").read_text(encoding="utf-8")
    effects = (core / "FolioFontEffects.h").read_text(encoding="utf-8")

    properties, shorthands = read_properties(specification, qualified_only=False)
    for header in sorted(core.glob("*.h")):
        extra, extra_shorthands = read_properties(header.read_text(encoding="utf-8"), qualified_only=True)
        properties.update(extra)
        shorthands.update(extra_shorthands)

    # Folio registers several names through one loop or helper, so those are read from the literals it passes.
    folio_decorators = [name for block in re.findall(r"for \(const char\* Name : \{([^}]*)\}", decorators) for name in re.findall(STRING, block)]
    folio_effects = re.findall(r"\bAdd\(\s*" + STRING, effects)

    vocabulary = {
        "properties": dict(sorted(properties.items())),
        "shorthands": dict(sorted(shorthands.items())),
        "decorators": sorted(set(instanced(factory, "Decorator") + folio_decorators)),
        "filters": sorted(set(instanced(factory, "Filter") + instanced(filters, "Filter"))),
        "fontEffects": sorted(set(instanced(factory, "FontEffect") + folio_effects)),
        # Shared by transition and animation, which parse through their own keyword table.
        "animationKeywords": re.findall(r"\{\s*" + STRING + r"\s*,\s*\{(?:KeywordType|Tween)", animation),
        "elements": sorted(name for name in set(instanced(factory, "Element") + instanced("\n".join(path.read_text(encoding="utf-8") for path in core.glob("*.h")), "Element")) if name[0].isalpha()),
    }

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(vocabulary, indent="\t") + "\n", encoding="utf-8", newline="\n")
    print(f"{len(properties)} properties, {len(shorthands)} shorthands -> {OUTPUT.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
