#!/usr/bin/env python3
"""Generates the view model base class a document's bindings already describe.

A document declares everything its binding surface needs: `data-model="hud"` names the model,
`{{ Health }}` names a value, `data-event-click="Reload"` names a command. Writing the matching C++ by
hand is transcription, and transcription is worth generating.

**Types come from the mock, names come from the markup.** `UI/mock/hud.json` already holds typed values
because someone wrote it to see the screen, so `"Health": 0.75` is a float and `"bAlive": true` is a
bool - with no inference from expressions and no type annotations polluting the markup. That also makes
the two a cross-check: a name used in markup with no mock entry is exactly the error to report, and so
is a mock entry nothing uses.

**A base class, never the class itself.** `UHudViewModelBase` holds the fields and the command stubs;
`UHudViewModel : UHudViewModelBase` is hand-written and holds the logic. Regenerating the base is then
always safe. Generating one file a person is expected to own is the version that eventually overwrites
somebody's work.

Reads the document as text rather than through RmlUi, deliberately: `{{ }}` is rewritten into a data
view as the document loads, so a loaded document no longer knows what was written. The limitation is
that bindings introduced by a linked template are not seen; those surface as unused mock entries rather
than being silently dropped.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

MODEL = re.compile(r'data-model\s*=\s*"([^"]+)"')
INTERPOLATION = re.compile(r"\{\{(.+?)\}\}", re.S)
EVENT = re.compile(r'data-event-([a-z]+)\s*=\s*"([^"]*)"')

# Every data attribute that carries an expression. data-loc is Folio's own and names a translation key
# rather than a binding, and data-model names the model, so neither appears here.
EXPRESSION_ATTRIBUTES = re.compile(
    r'data-(?:text|value|checked|if|visible|for|rml|attr-[a-z-]+|attrif-[a-z-]+'
    r'|class-[a-z-]+|style-[a-z-]+)\s*=\s*"([^"]*)"')

IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
STRING = re.compile(r"'[^']*'|\"[^\"]*\"")

# Names an expression may contain that are not bindings: RmlUi's own, and the transform functions.
RESERVED = {
    "true", "false", "it", "it_index", "size",
    "format", "round", "ceil", "floor", "abs", "min", "max", "clamp", "to_lower", "to_upper",
}

CPP_TYPES = {
    bool: ("bool", "false"),
    int: ("int32", "0"),
    float: ("float", "0.0f"),
    str: ("FString", "FString()"),
}


def identifiers(expression: str) -> list[str]:
    """The bindings an expression reads, with literals, calls and reserved names removed."""
    without_strings = STRING.sub(" ", expression)

    found: list[str] = []
    for match in IDENTIFIER.finditer(without_strings):
        name = match.group(0)
        if name in RESERVED:
            continue

        # A name followed by ( is a call, and RmlUi expressions have no user-defined ones.
        if without_strings[match.end():].lstrip().startswith("("):
            continue

        # A member reference reads the whole path; the root is what the model exposes.
        if without_strings[:match.start()].rstrip().endswith("."):
            continue

        found.append(name)

    return found


def scan(document: pathlib.Path) -> tuple[str | None, set[str], dict[str, list[str]]]:
    text = document.read_text(encoding="utf-8")

    model_match = MODEL.search(text)
    model = model_match.group(1) if model_match else None

    values: set[str] = set()
    for expression in INTERPOLATION.findall(text):
        values.update(identifiers(expression))
    for expression in EXPRESSION_ATTRIBUTES.findall(text):
        values.update(identifiers(expression))

    # A command takes its signature from the call site, so arguments are kept rather than counted.
    commands: dict[str, list[str]] = {}
    for _event, call in EVENT.findall(text):
        call = call.strip()
        if not call:
            continue

        if "(" in call:
            name, _, rest = call.partition("(")
            inner = rest.rsplit(")", 1)[0].strip()
            arguments = [part.strip() for part in inner.split(",")] if inner else []
        else:
            name, arguments = call, []

        name = name.strip()
        if not IDENTIFIER.fullmatch(name):
            continue

        commands[name] = arguments

        # An argument that is a bound value rather than a literal is part of the surface too.
        for argument in arguments:
            values.update(identifiers(argument))

    return model, values, commands


def argument_type(literal: str, mock: dict) -> str | None:
    """A literal types itself; a bound name types from the mock. Anything else is reported."""
    literal = literal.strip()
    if not literal:
        return None

    if STRING.fullmatch(literal):
        return "const FString&"
    if literal in ("true", "false"):
        return "bool"

    for parse, cpp in ((int, "int32"), (float, "float")):
        try:
            parse(literal)
            return cpp
        except ValueError:
            pass

    if literal in mock:
        return CPP_TYPES[type(mock[literal])][0]

    return None


def pascal(name: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in re.split(r"[_\-\s]+", name) if part)


def field_name(name: str) -> str:
    """A bool keeps its b prefix, which Unreal's own conventions want; anything else is PascalCase."""
    if name.startswith("b") and len(name) > 1 and name[1].isupper():
        return name
    return pascal(name)


def generate(model: str, values: set[str], commands: dict[str, list[str]], mock: dict,
             api: str) -> tuple[str, list[str]]:
    problems: list[str] = []

    for name in sorted(values):
        if name not in mock:
            problems.append(
                f"'{name}' is bound in the markup but is not in the mock, so it has no type. Add it "
                f"to the mock with a value of the type it should be.")

    for name in sorted(set(mock) - values):
        problems.append(
            f"'{name}' is in the mock but nothing in this document binds it. Expected when a mock is "
            f"shared between documents; a typo in the markup otherwise.")

    known = [name for name in sorted(values) if name in mock]
    class_name = f"U{pascal(model)}ViewModelBase"
    category = pascal(model)
    prefix = f"{api} " if api else ""

    out: list[str] = [
        "// Generated from the document's bindings. Do not edit.",
        "//",
        "// Regenerate with Tools/codegen/viewmodel.py. Subclass this and put the logic in the",
        "// subclass: this file is rewritten whenever the document's bindings change, so anything",
        "// added here is lost.",
        "",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        '#include "MVVMViewModelBase.h"',
        "",
        f'#include "{class_name[1:]}.generated.h"',
        "",
        "/**",
        f" * The binding surface of the '{model}' model, as the document declares it.",
        " *",
        " * Every field notifies, so a document updates as one changes rather than being polled for it.",
        " */",
        "UCLASS(Abstract, BlueprintType)",
        f"class {prefix}{class_name} : public UMVVMViewModelBase",
        "{",
        "\tGENERATED_BODY()",
        "",
        "public:",
    ]

    for name in known:
        cpp, _default = CPP_TYPES[type(mock[name])]
        field = field_name(name)
        out += [
            f'\tUFUNCTION(BlueprintPure, Category = "{category}")',
            f"\t{cpp} Get{pascal(name)}() const {{ return {field}; }}",
            "",
            f'\tUFUNCTION(BlueprintCallable, Category = "{category}")',
            f"\tvoid Set{pascal(name)}(const {cpp} InValue)",
            "\t{",
            f"\t\tUE_MVVM_SET_PROPERTY_VALUE({field}, InValue);",
            "\t}",
            "",
        ]

    for name, arguments in sorted(commands.items()):
        types = [argument_type(argument, mock) for argument in arguments]
        if any(entry is None for entry in types):
            unknown = [a for a, t in zip(arguments, types) if t is None]
            problems.append(
                f"command '{name}' has argument(s) that are neither a literal nor a bound value, so "
                f"they cannot be typed: {', '.join(unknown)}")
            continue

        signature = ", ".join(f"{cpp} InArg{index}" for index, cpp in enumerate(types))
        called = f"{name}({', '.join(arguments)})" if arguments else name

        out += [
            f'\t/** Called by data-event-*="{called}" in the document. */',
            f'\tUFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "{category}")',
            f"\tvoid {name}({signature});",
            "",
        ]

    out.append("protected:")
    for name in known:
        cpp, default = CPP_TYPES[type(mock[name])]
        out += [
            f'\tUPROPERTY(BlueprintReadWrite, FieldNotify, Setter, Getter, Category = "{category}")',
            f"\t{cpp} {field_name(name)} = {default};",
            "",
        ]

    out.append("};")
    return "\n".join(out) + "\n", problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("document", help="the .rml to read the binding surface from")
    parser.add_argument("--mock", help="the mock JSON. Defaults to UI/mock/<model>.json above the document")
    parser.add_argument("--out", help="where to write the header. Prints to stdout without it")
    parser.add_argument("--api", default="", help="the module's API macro, for a module that exports")
    parser.add_argument("--strict", action="store_true",
                        help="treat an untyped name, or a mock entry nothing uses, as an error")
    args = parser.parse_args()

    document = pathlib.Path(args.document)
    if not document.is_file():
        print(f"viewmodel: no document at {document}", file=sys.stderr)
        return 2

    model, values, commands = scan(document)
    if not model:
        print(f"viewmodel: {document} declares no data-model, so it has no binding surface",
              file=sys.stderr)
        return 2

    mock_path = pathlib.Path(args.mock) if args.mock else None
    if mock_path is None:
        for parent in document.parents:
            candidate = next((parent / name / "mock" / f"{model}.json" for name in ("UI", "ui")
                              if (parent / name / "mock").is_dir()), parent / "UI" / "mock" / f"{model}.json")
            if candidate.is_file():
                mock_path = candidate
                break

    if mock_path is None or not mock_path.is_file():
        print(f"viewmodel: no mock for model '{model}'. Types come from it, so there is nothing to "
              f"generate without one. Write UI/mock/{model}.json with a value of the right type for "
              f"each bound name.", file=sys.stderr)
        return 2

    mock = json.loads(mock_path.read_text(encoding="utf-8"))
    source, problems = generate(model, values, commands, mock, args.api)

    for problem in problems:
        print(f"  {problem}", file=sys.stderr)

    if args.out:
        out = pathlib.Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(source, encoding="utf-8")
        print(f"viewmodel: {document.name} -> {out}, "
              f"{len([v for v in values if v in mock])} field(s), {len(commands)} command(s)")
    else:
        print(source)

    return 1 if problems and args.strict else 0


if __name__ == "__main__":
    sys.exit(main())
