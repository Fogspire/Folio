#!/usr/bin/env python3
"""Turn the data-loc keys in .rml documents into NSLOCTEXT declarations Unreal can gather.

Unreal's GatherTextFromSource step scans C++ for NSLOCTEXT. It knows nothing about markup, so the
strings in a Folio document are invisible to it. This walks the documents, reads every data-loc key
with the source text beside it, and writes one generated header full of NSLOCTEXT for the gatherer to
find. Nothing includes that header; it exists to be read, not compiled.

Run it before -run=GatherText. The namespace is derived from the file path and must match
Folio::Loc::NamespaceForDocument exactly, or every runtime lookup misses and every string silently
falls back to English.

    python gather_rml.py --root <project> --out <project>/Source/Generated/FolioText.h

Exits non-zero on anything that would produce untranslatable or ambiguous text, because a
localisation problem that only warns is a localisation problem that ships.
"""

from __future__ import annotations

import argparse
import html.parser
import os
import pathlib
import sys

# Elements whose content is never user-visible text.
SKIP_TAGS = {"style", "script", "head", "title"}


def namespace_for(relative: pathlib.PurePosixPath) -> str:
    """ui/screens/main_menu.rml -> UI.MainMenu; anything under ui/lib/ -> UI.Common.

    Must stay in step with Folio::Loc::NamespaceForDocument in FolioLocalization.cpp.
    """
    parts = relative.parts
    if "lib" in parts[:-1]:
        return "UI.Common"

    stem = relative.stem
    pascal = "".join(word[:1].upper() + word[1:] for word in stem.replace("-", "_").split("_") if word)
    return f"UI.{pascal}"


class Finding:
    def __init__(self, path: str, line: int, code: str, message: str) -> None:
        self.path, self.line, self.code, self.message = path, line, code, message

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.code}: {self.message}"


class DocumentParser(html.parser.HTMLParser):
    """Collects (key, text, line) for every element carrying data-loc.

    RML is XML-like but not strictly XML, so this uses the tolerant HTML parser rather than an XML
    one. It only needs to see attributes and the text immediately inside an element, which is all the
    convention allows a data-loc element to contain.
    """

    def __init__(self, path: str) -> None:
        super().__init__(convert_charrefs=True)
        self.path = path
        self.entries: list[tuple[str, str, int]] = []
        self.findings: list[Finding] = []

        # Stack of open elements, each a dict recording whether it is collecting and what it has.
        self.stack: list[dict] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = {name: (value or "") for name, value in attrs}
        frame = {
            "tag": tag,
            "line": self.getpos()[0],
            "key": values.get("data-loc"),
            "text": [],
            "child_elements": 0,
            # A value arriving through a binding is game state, not a translatable string.
            "bound": "data-text" in values,
        }

        if self.stack:
            self.stack[-1]["child_elements"] += 1

        self.stack.append(frame)

    def handle_endtag(self, tag: str) -> None:
        # Tolerate unbalanced markup rather than desynchronising on it.
        for index in range(len(self.stack) - 1, -1, -1):
            if self.stack[index]["tag"] == tag:
                for frame in self.stack[index:]:
                    self.finish(frame)
                del self.stack[index:]
                return

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self.handle_starttag(tag, attrs)
        self.handle_endtag(tag)

    def handle_data(self, data: str) -> None:
        if self.stack and self.stack[-1]["tag"] not in SKIP_TAGS:
            self.stack[-1]["text"].append(data)

    def close(self) -> None:  # noqa: D102
        super().close()
        for frame in reversed(self.stack):
            self.finish(frame)
        self.stack.clear()

    def finish(self, frame: dict) -> None:
        key = frame["key"]
        if key is None:
            return

        text = "".join(frame["text"]).strip()

        if not key:
            self.findings.append(Finding(self.path, frame["line"], "E-LOC-NOKEY",
                                        "data-loc with an empty key"))
            return

        if frame["bound"]:
            self.findings.append(Finding(self.path, frame["line"], "E-LOC-BOUND",
                                        f"'{key}' is on an element whose text comes from a data "
                                        f"binding, so there is no source string to translate"))
            return

        if frame["child_elements"]:
            self.findings.append(Finding(self.path, frame["line"], "E-LOC-MARKUP",
                                         f"'{key}' is on an element containing other elements; "
                                         f"translation replaces all of its content, so the key "
                                         f"belongs on an inner text-only element"))
            return

        if not text:
            self.findings.append(Finding(self.path, frame["line"], "E-LOC-EMPTY",
                                         f"'{key}' has no text content, so nothing can be gathered"))
            return

        self.entries.append((key, text, frame["line"]))


def escape_cpp(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ").replace("\t", " ")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", required=True, help="project directory holding the ui tree")
    parser.add_argument("--ui", default="ui", help="ui tree, relative to root (default: ui)")
    parser.add_argument("--out", required=True, help="generated header to write")
    parser.add_argument("--exclude", action="append", default=[],
                        help="path fragment to skip, repeatable (e.g. examples)")
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    ui_root = root / args.ui
    if not ui_root.is_dir():
        print(f"error: no ui tree at {ui_root}", file=sys.stderr)
        return 2

    documents = sorted(p for p in ui_root.rglob("*.rml"))
    findings: list[Finding] = []

    # namespace -> key -> (text, path, line), so a duplicate names both sides.
    gathered: dict[str, dict[str, tuple[str, str, int]]] = {}
    # namespace -> first document that produced it, so a collision is reported rather than merged.
    owners: dict[str, str] = {}

    for document in documents:
        relative = pathlib.PurePosixPath(document.relative_to(root).as_posix())
        if any(fragment in str(relative) for fragment in args.exclude):
            continue

        namespace = namespace_for(relative)
        display = str(relative)

        # Two documents deriving one namespace would silently share a key space, and the collision
        # would surface as one screen showing another's text.
        if namespace in owners and owners[namespace] != display and namespace != "UI.Common":
            findings.append(Finding(display, 1, "E-LOC-NAMESPACE",
                                    f"'{namespace}' is already used by {owners[namespace]}; "
                                    f"rename one of the files"))
            continue
        owners.setdefault(namespace, display)

        parser_instance = DocumentParser(display)
        parser_instance.feed(document.read_text(encoding="utf-8"))
        parser_instance.close()
        findings.extend(parser_instance.findings)

        bucket = gathered.setdefault(namespace, {})
        for key, text, line in parser_instance.entries:
            if key in bucket:
                existing_text, existing_path, existing_line = bucket[key]
                if existing_text != text:
                    findings.append(Finding(display, line, "E-LOC-DUPLICATE",
                                            f"'{key}' in {namespace} already means "
                                            f"\"{existing_text}\" at {existing_path}:{existing_line}"))
                continue
            bucket[key] = (text, display, line)

    errors = [f for f in findings if f.code.startswith("E-")]
    for finding in findings:
        print(str(finding), file=sys.stderr if finding.code.startswith("E-") else sys.stdout)

    if errors:
        print(f"gather_rml: {len(errors)} error(s); nothing written", file=sys.stderr)
        return 1

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "// Generated by Plugins/Folio/Tools/locgather/gather_rml.py. Do not edit.",
        "//",
        "// Nothing includes this file. It exists so GatherTextFromSource can see the strings that live",
        "// in .rml documents, which it would otherwise never find. Re-run the script and gather again",
        "// after changing any user-visible text.",
        "",
        "#pragma once",
        "",
        "#include \"Internationalization/Text.h\"",
        "",
    ]

    total = 0
    for namespace in sorted(gathered):
        if not gathered[namespace]:
            continue
        lines.append(f"// {owners[namespace]}" if namespace != "UI.Common" else "// shared components")
        for key in sorted(gathered[namespace]):
            text, _, _ = gathered[namespace][key]
            lines.append(f'static const FText FolioText_{namespace.replace(".", "_")}_{key} = '
                         f'NSLOCTEXT("{namespace}", "{key}", "{escape_cpp(text)}");')
            total += 1
        lines.append("")

    out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"gather_rml: {total} string(s) in {len(gathered)} namespace(s) -> "
          f"{os.path.relpath(out, root)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
