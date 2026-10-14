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

**Or from a schema, which carries the types itself.** `--schema UI/schema/hud.json` generates from the
typed description Folio Studio keeps instead of from a document and a mock. A schema knows what a mock
cannot say - that a string is an FText, that a number is an enum, what a row of an array holds, what a
command's parameters are - and serves every document that binds the model rather than one. The mock is
then an output too: `--mock-out` writes the one the schema describes.
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

    if literal in mock and type(mock[literal]) in CPP_TYPES:
        return CPP_TYPES[type(mock[literal])][0]

    return None


# What each schema type is in C++, and how it is passed to a command.
SCHEMA_TYPES = {
    "bool": ("bool", "bool"),
    "int": ("int32", "int32"),
    "float": ("float", "float"),
    "string": ("FString", "const FString&"),
    "text": ("FText", "const FText&"),
    "name": ("FName", "FName"),
}

LOOP = re.compile(r'data-for\s*=\s*"\s*([A-Za-z_]\w*)\s*(?:,\s*([A-Za-z_]\w*)\s*)?:')


def schema_type(field: dict, problems: list[str], where: str) -> str:
    kind = field.get("type", "string")
    if kind == "enum":
        if not field.get("enum"):
            problems.append(f"'{where}' is an enum with no enum named, so it is generated as an int32.")
            return "int32"
        return field["enum"]
    if kind == "array":
        inner = field.get("of", "string")
        return f"TArray<{SCHEMA_TYPES[inner][0] if inner in SCHEMA_TYPES else inner}>"
    if kind not in SCHEMA_TYPES:
        problems.append(f"'{where}' has unknown type '{kind}', so it is generated as an FString.")
        return "FString"
    return SCHEMA_TYPES[kind][0]


def schema_default(field: dict) -> str:
    """The initialiser a field is declared with, or nothing for a type that default-constructs."""
    kind = field.get("type", "string")
    value = field.get("default")

    if kind == "bool":
        return " = true" if value is True else " = false"
    if kind == "int":
        return f" = {int(value) if isinstance(value, (int, float)) else 0}"
    if kind == "float":
        return f" = {float(value) if isinstance(value, (int, float)) else 0.0}f"
    if kind == "enum" and isinstance(value, str) and value:
        return f" = {value}"
    if kind in ("string", "name") and isinstance(value, str) and value:
        escaped = value.replace("\\", "\\\\").replace('"', '\\"')
        return f' = TEXT("{escaped}")'
    return ""


def schema_parameter(declaration: str, problems: list[str], command: str) -> str:
    """`int Index` in a schema is `int32 Index` in the signature; an unknown type is passed through."""
    parts = declaration.split()
    if len(parts) != 2:
        problems.append(f"command '{command}' has parameter '{declaration}', which is not 'type Name'.")
        return declaration
    kind, name = parts
    return f"{SCHEMA_TYPES[kind][1] if kind in SCHEMA_TYPES else kind} {name}"


def documented(lines: list[str], doc: str | None, indent: str) -> None:
    if doc:
        lines.append(f"{indent}/** {doc} */")


def generate_from_schema(schema: dict, schema_name: str, api: str) -> tuple[str, list[str]]:
    problems: list[str] = []
    model = schema["model"]
    class_name = schema.get("class") or f"U{pascal(model)}ViewModel"
    base_name = f"{class_name}Base"
    category = schema.get("category") or pascal(model)
    prefix = f"{api or schema.get('api', '')} ".lstrip()

    out: list[str] = [
        f"// Generated from {schema_name}. Do not edit.",
        "//",
        "// Regenerate with Tools/codegen/viewmodel.py --schema. Derive the model's class from this one",
        "// and put the logic there: this file is rewritten whenever the schema changes, so anything",
        "// added here is lost.",
        "",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        '#include "UObject/Object.h"',
    ]
    out += [f'#include "{include}"' for include in schema.get("includes", [])]
    out += ["", f'#include "{base_name[1:]}.generated.h"', ""]

    for struct_name, struct in schema.get("structs", {}).items():
        out += [
            f"/** One row of an array in the '{model}' model. */",
            "USTRUCT(BlueprintType)",
            f"struct {prefix}{struct_name}",
            "{",
            "\tGENERATED_BODY()",
            "",
        ]
        for name, field in struct.get("fields", {}).items():
            documented(out, field.get("doc"), "\t")
            out += [
                f'\tUPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "{category}")',
                f"\t{schema_type(field, problems, f'{struct_name}.{name}')} {name}{schema_default(field)};",
                "",
            ]
        out[-1] = "};"
        out.append("")

    out += [
        "/**",
        f" * The binding surface of the '{model}' model, as its schema declares it.",
        " *",
        " * Folio binds an object by reflection, so these properties are the binding: give an instance of the",
        " * derived class to BindViewModel under ModelName() and the document reads them.",
        " */",
        "UCLASS(Abstract, BlueprintType)",
        f"class {prefix}{base_name} : public UObject",
        "{",
        "\tGENERATED_BODY()",
        "",
        "public:",
        "\t/** The name a document binds this model under. */",
        f'\tstatic const TCHAR* ModelName() {{ return TEXT("{model}"); }}',
        "",
    ]

    for name, field in schema.get("fields", {}).items():
        documented(out, field.get("doc"), "\t")
        access = "BlueprintReadWrite" if field.get("writable") else "BlueprintReadOnly"
        out += [
            f'\tUPROPERTY({access}, Category = "{category}")',
            f"\t{schema_type(field, problems, name)} {name}{schema_default(field)};",
            "",
        ]

    for name, parameters in schema.get("commands", {}).items():
        signature = ", ".join(schema_parameter(parameter, problems, name) for parameter in parameters)
        out += [
            f'\t/** Called by data-event-*="{name}" in a document. */',
            f'\tUFUNCTION(BlueprintCallable, Category = "{category}")',
            f"\tvirtual void {name}({signature}) PURE_VIRTUAL({base_name}::{name}, );",
            "",
        ]

    out[-1] = "};"
    return "\n".join(out) + "\n", problems


def check_documents(schema: dict, root: pathlib.Path) -> list[str]:
    """What the documents a schema names bind that it lacks, and what it declares that none of them bind."""
    problems: list[str] = []
    bound: set[str] = set()
    called: dict[str, list[str]] = {}

    for relative in schema.get("documents", []):
        document = root / relative
        if not document.is_file():
            problems.append(f"the schema names {relative}, which does not exist.")
            continue

        _model, values, commands = scan(document)
        # A loop variable is the document's own name for a row, not something the model supplies.
        for match in LOOP.finditer(document.read_text(encoding="utf-8")):
            values -= {name for name in match.groups() if name}
        bound |= values
        called.update(commands)

    if not schema.get("documents"):
        return problems

    fields = schema.get("fields", {})
    for name in sorted(bound - set(fields)):
        problems.append(f"'{name}' is bound in the markup but the schema has no such field.")
    for name in sorted(set(fields) - bound):
        problems.append(f"'{name}' is in the schema but none of its documents bind it.")
    for name, arguments in sorted(called.items()):
        declared = schema.get("commands", {}).get(name)
        if declared is None:
            problems.append(f"command '{name}' is called by the markup but the schema does not declare it.")
        elif len(declared) != len(arguments):
            problems.append(f"command '{name}' is called with {len(arguments)} argument(s) but declares {len(declared)}.")
    return problems


def mock_from_schema(schema: dict) -> dict:
    zero = {"bool": False, "int": 0, "float": 0, "enum": 0, "array": []}
    return {name: field.get("sample", zero.get(field.get("type", "string"), ""))
            for name, field in schema.get("fields", {}).items()}


def run_schema(args: argparse.Namespace) -> int:
    schema_path = pathlib.Path(args.schema)
    if not schema_path.is_file():
        print(f"viewmodel: no schema at {schema_path}", file=sys.stderr)
        return 2

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    if "model" not in schema or "fields" not in schema:
        print(f"viewmodel: {schema_path} has no 'model' or no 'fields', so it is not a schema", file=sys.stderr)
        return 2

    # UI/schema/<model>.json, so the content root the documents are relative to is two levels above.
    root = schema_path.resolve().parents[2]
    source, problems = generate_from_schema(schema, f"UI/schema/{schema_path.name}", args.api)
    problems += check_documents(schema, root)

    for problem in problems:
        print(f"  {problem}", file=sys.stderr)

    if args.mock_out:
        mock_path = pathlib.Path(args.mock_out)
        mock_path.parent.mkdir(parents=True, exist_ok=True)
        mock_path.write_text(json.dumps(mock_from_schema(schema), indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")

    if args.out:
        out = pathlib.Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(source, encoding="utf-8")
        print(f"viewmodel: {schema_path.name} -> {out}, {len(schema['fields'])} field(s), "
              f"{len(schema.get('commands', {}))} command(s)")
    else:
        print(source)

    return 1 if problems and args.strict else 0


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

    # An array or an object has no type a value alone can give: what a row holds is exactly what a
    # mock cannot say. A schema can, so those are left to it rather than guessed at.
    for name in sorted(values):
        if name in mock and type(mock[name]) not in CPP_TYPES:
            problems.append(
                f"'{name}' is an array or an object in the mock, which this mode cannot type. Describe "
                f"the model in UI/schema/{model}.json and generate with --schema.")

    known = [name for name in sorted(values) if name in mock and type(mock[name]) in CPP_TYPES]
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
    parser.add_argument("document", nargs="?", help="the .rml to read the binding surface from")
    parser.add_argument("--schema", help="generate from a typed schema, UI/schema/<model>.json, instead of from a "
                                         "document and its mock")
    parser.add_argument("--mock-out", help="with --schema: also write the mock the schema describes")
    parser.add_argument("--mock", help="the mock JSON. Defaults to UI/mock/<model>.json above the document")
    parser.add_argument("--out", help="where to write the header. Prints to stdout without it")
    parser.add_argument("--api", default="", help="the module's API macro, for a module that exports")
    parser.add_argument("--strict", action="store_true",
                        help="treat an untyped name, or a mock entry nothing uses, as an error")
    args = parser.parse_args()

    if args.schema:
        return run_schema(args)
    if not args.document:
        parser.error("give a document, or --schema")

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
