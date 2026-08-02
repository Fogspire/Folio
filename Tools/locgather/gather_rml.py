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

# The namespace every term lands in. One table across the whole game, because a term exists so that
# one name is written once.
TERMS_NAMESPACE = "Folio.Terms"

DIRECTIVES = {"tag", "delay", "pause", "br"}


def read_terms(ui_root: pathlib.Path) -> tuple[dict[str, str], set[str], list["Finding"]]:
    """Reads every UI/terms/*.terms. Mirrors FolioTerms.cpp; the two must agree.

    Returns tag -> text including `Tag:form` entries under their full key, and the set of tags that
    are `@external`. Those still have to resolve - a document naming one is correct - they just are
    not gathered here, because the game already gathers them where the text really lives.
    """
    terms: dict[str, tuple[str, str, int]] = {}
    external: set[str] = set()
    findings: list[Finding] = []

    for path in sorted((ui_root / "terms").glob("*.terms")):
        display = path.name
        # An @external file mirrors names the game already holds and already translates. Its text
        # is here so the preview can resolve a tag, and gathering it would translate them twice.
        is_external = False

        for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("@"):
                is_external = is_external or line[1:].split()[0:1] == ["external"]
                continue

            if "=" not in line:
                findings.append(Finding(display, number, "E-TERM-SYNTAX",
                                        "expected 'Tag = Text'"))
                continue

            tag, _, text = line.partition("=")
            tag, text = tag.strip(), text.strip()

            if not tag or not text:
                findings.append(Finding(display, number, "E-TERM-EMPTY",
                                        f"'{tag}' has no tag or no text"))
                continue

            if tag in terms:
                _, other_file, other_line = terms[tag]
                findings.append(Finding(display, number, "E-TERM-DUPLICATE",
                                        f"'{tag}' is already defined at {other_file}:{other_line}"))
                continue

            terms[tag] = (text, display, number)
            if is_external:
                external.add(tag)

    return {tag: value[0] for tag, value in terms.items()}, external, findings


class Markup:
    """One parsed `<text>` body: what it says, and what it asks for.

    A deliberate mirror of Folio::TextMarkup::Parse. The two are checked against each other by
    scripts/check_markup.sh, because a rule that only one of them enforces is a rule that fails
    either in the game or in the gather but never in both.
    """

    def __init__(self) -> None:
        self.plain = ""
        self.styles: list[str] = []
        self.terms: list[str] = []
        self.errors: list[str] = []


def parse_markup(markup: str) -> Markup:
    out = Markup()
    styles: list[str] = []
    directives: list[str] = []
    index = 0

    while index < len(markup):
        character = markup[index]

        if character == "\\" and index + 1 < len(markup) and markup[index + 1] in "<>{}\\":
            out.plain += markup[index + 1]
            index += 2
            continue

        if character == "<":
            close = markup.find(">", index)
            if close < 0:
                out.errors.append(r"'<' with no closing '>'. Write \< for a literal one")
                out.plain += character
                index += 1
                continue

            inner = markup[index + 1:close]
            index = close + 1

            if inner.startswith("/"):
                named = inner[1:]
                if not styles:
                    out.errors.append(f"'</{named}>' closes a style that was never opened")
                elif named and named != styles[-1]:
                    out.errors.append(f"'</{named}>' closes '{styles[-1]}', which is not what is open")
                    styles.pop()
                else:
                    styles.pop()
            elif inner.replace("-", "").replace("_", "").isalnum() and inner[:1].isalpha():
                styles.append(inner)
                if inner not in out.styles:
                    out.styles.append(inner)
            else:
                out.errors.append(f"'<{inner}>' is not a style name")
            continue

        if character == "{":
            close = markup.find("}", index)
            if close < 0:
                out.errors.append(r"'{' with no closing '}'. Write \{ for a literal one")
                out.plain += character
                index += 1
                continue

            inner = markup[index + 1:close]
            literal = markup[index:close + 1]
            index = close + 1

            if inner == "/":
                if not directives:
                    out.errors.append("'{/}' closes a directive that was never opened")
                else:
                    directives.pop()
                continue

            name, _, value = inner.partition(":")
            if name == "br":
                out.plain += " "
                continue

            if not value:
                out.errors.append(f"'{literal}' needs a value, as {{{name}:...}}")
                continue

            if name == "tag":
                if value not in out.terms:
                    out.terms.append(value)
                continue

            if name not in DIRECTIVES:
                out.errors.append(f"'{literal}' is not a directive. There are {{delay:}}, "
                                  f"{{pause:}}, {{br}} and {{tag:}}")
                continue

            try:
                if float(value) < 0:
                    raise ValueError
            except ValueError:
                out.errors.append(f"'{literal}' wants a number of seconds")
                continue

            if name == "delay":
                directives.append(name)
            continue

        out.plain += character
        index += 1

    for style in styles:
        out.errors.append(f"'<{style}>' was never closed. Close it with </>")
    if directives:
        out.errors.append("a '{delay:}' was never closed. Close it with {/}")

    out.plain = " ".join(out.plain.split())
    return out


def namespace_for(relative: pathlib.PurePosixPath) -> str:
    """UI/screens/main_menu.rml -> Folio.MainMenu; anything under UI/lib/ -> Folio.Common.

    Must stay in step with Folio::Loc::NamespaceForDocument in FolioLocalization.cpp.
    """
    parts = relative.parts
    if "lib" in parts[:-1]:
        return "Folio.Common"

    stem = relative.stem
    pascal = "".join(word[:1].upper() + word[1:] for word in stem.replace("-", "_").split("_") if word)
    return f"Folio.{pascal}"


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

    # A <text> holds Folio's own markup rather than RML: `<angry>` is a style and `{tag:}` is a
    # term, neither of which is an element. Read as elements they would trip the "key on an element
    # containing other elements" rule on every line of dialogue in the game.
    CDATA_CONTENT_ELEMENTS = ("script", "style", "text")

    def __init__(self, path: str, terms: dict[str, str]) -> None:
        super().__init__(convert_charrefs=True)
        self.path = path
        self.terms = terms
        self.entries: list[tuple[str, str, int, Markup | None]] = []
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

        if frame["tag"] == "text":
            self.finish_markup(frame, key, text)
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

        self.entries.append((key, text, frame["line"], None))

    def finish_markup(self, frame: dict, key: str, text: str) -> None:
        """A <text>, whose source string is its markup and has to survive translation intact."""
        if not text:
            self.findings.append(Finding(self.path, frame["line"], "E-LOC-EMPTY",
                                         f"'{key}' has no text content, so nothing can be gathered"))
            return

        markup = parse_markup(text)
        for error in markup.errors:
            self.findings.append(Finding(self.path, frame["line"], "E-TEXT-MARKUP",
                                         f"'{key}': {error}"))

        # A term with no entry renders as its own tag, in every language at once. Caught here
        # rather than at runtime, where it is one line of dialogue nobody replayed.
        for tag in markup.terms:
            if tag not in self.terms and tag.split(":", 1)[0] not in self.terms:
                self.findings.append(Finding(self.path, frame["line"], "E-TEXT-TERM",
                                             f"'{key}': no term '{tag}' in any table"))

        self.entries.append((key, text, frame["line"], markup))


def write_notes(path: pathlib.Path, gathered: dict, terms: dict[str, str],
                external: set[str] | None = None) -> None:
    """Writes the brief that goes to a translator with the PO.

    Unreal's PO export carries no room for a note against a string, so what a translator needs to
    know about `<angry>` and `{tag:Place.X}` has to travel beside it. Generated rather than written,
    so it cannot fall out of step with the strings it explains.
    """
    lines = [
        "# Translator notes",
        "",
        "Generated by gather_rml.py. Send it with Folio.po.",
        "",
        "## The two things in these strings that are not words",
        "",
        "**`<name>word</>`** marks how something is said, and the game styles it. Keep the tags,",
        "translate what is between them, and move them to whichever word carries the emphasis in",
        "your language. `</>` closes the nearest open tag; `</name>` also works.",
        "",
        "**`{tag:Some.Thing}`** is a name the game substitutes: a place, a person, an item. Do not",
        "translate the tag itself and do not change its spelling. Every string that mentions that",
        "name uses the same tag, so the name is translated once, in the terms section below, and",
        "every line agrees. What each tag prints in English is listed there.",
        "",
        "If a sentence cannot be built around a substituted name because your language inflects it,",
        "either ask for the form you need or write the word out in full instead of using the tag.",
        "Writing it out loses the automatic consistency, and is the right call when grammar demands",
        "it.",
        "",
        "**`{delay:0.2}`, `{pause:0.5}`, `{br}`** control how fast the text types and where it",
        "breaks. Keep them, put them where they read correctly, and change nothing else about them.",
        "",
    ]

    external = external or set()
    if terms:
        lines += ["## Terms", "",
                  "What each tag prints in English. The ones marked elsewhere are translated with",
                  "the rest of the game's text rather than in this PO - they are listed so you can",
                  "read the sentence, not so you can translate them here.",
                  "", "| Tag | English | Translated |", "|---|---|---|"]
        for tag in sorted(terms):
            where = "elsewhere" if tag in external else "`Folio.Terms`"
            lines.append("| `" + tag + "` | " + terms[tag] + " | " + where + " |")
        lines.append("")

    rows = []
    for namespace in sorted(gathered):
        for key in sorted(gathered[namespace]):
            markup = gathered[namespace][key][3]
            if markup is None or not (markup.styles or markup.terms):
                continue
            used = ", ".join("`<" + style + ">`" for style in markup.styles)
            named = ", ".join("`" + tag + "` = "
                              + terms.get(tag, terms.get(tag.split(":", 1)[0], "?"))
                              for tag in markup.terms)
            rows.append("| `" + namespace + "` | `" + key + "` | "
                        + (used or "-") + " | " + (named or "-") + " |")

    if rows:
        lines += ["## Strings that carry tags", "",
                  "| Namespace | Key | Styles | Names it prints |", "|---|---|---|---|"]
        lines += rows
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def unquote_po(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        value = value[1:-1]
    return (value.replace('\\"', '"').replace("\\n", "\n")
                 .replace("\\t", "\t").replace("\\\\", "\\"))


def read_po(path: pathlib.Path) -> list[tuple[int, str, str]]:
    """(line, msgid, msgstr) for every translated entry. As much of PO as checking one needs."""
    entries: list[tuple[int, str, str]] = []
    msgid: list[str] = []
    msgstr: list[str] = []
    field: list[str] | None = None
    start = 0

    def flush() -> None:
        if msgid and any(part for part in msgstr):
            entries.append((start, "".join(msgid), "".join(msgstr)))
        msgid.clear()
        msgstr.clear()

    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()

        if line.startswith("msgid "):
            flush()
            field, start = msgid, number
            field.append(unquote_po(line[len("msgid "):]))
        elif line.startswith("msgstr "):
            field = msgstr
            field.append(unquote_po(line[len("msgstr "):]))
        elif line.startswith('"') and field is not None:
            field.append(unquote_po(line))
        elif not line:
            flush()
            field = None

    flush()
    return entries


def check_po(path: pathlib.Path, terms: dict[str, str]) -> list[Finding]:
    """Checks that every translation still carries markup the game can read.

    This is what makes handing a translator the raw tags safe. The failure it catches is the
    ordinary one: someone retypes `{tag:Place.Mountain.Temple}` slightly wrong, and it ships as
    literal braces in the middle of a sentence, in one language, which nobody on the team reads.
    """
    findings: list[Finding] = []
    display = path.name

    def base(tag: str) -> str:
        return tag.split(":", 1)[0]

    for line, source, translation in read_po(path):
        if "<" not in source and "{" not in source:
            continue

        original = parse_markup(source)
        translated = parse_markup(translation)

        for error in translated.errors:
            findings.append(Finding(display, line, "E-PO-MARKUP", error))

        for tag in translated.terms:
            if tag not in terms and base(tag) not in terms:
                findings.append(Finding(display, line, "E-PO-TERM",
                                        "the translation asks for a term that does not exist: '"
                                        + tag + "'"))
            elif base(tag) not in {base(t) for t in original.terms}:
                findings.append(Finding(display, line, "E-PO-TERM",
                                        "the translation names '" + tag
                                        + "', which the source does not"))

        for style in translated.styles:
            if style not in original.styles:
                findings.append(Finding(display, line, "E-PO-STYLE",
                                        "the translation uses a style the source does not: '<"
                                        + style + ">'"))

        # Dropping one is a judgement a translator is allowed to make: a name that cannot be
        # inflected gets written out, and emphasis moves or disappears between languages.
        for tag in original.terms:
            if base(tag) not in {base(t) for t in translated.terms}:
                findings.append(Finding(display, line, "W-PO-TERM",
                                        "the translation does not use '" + tag
                                        + "'; that name will not follow the table"))
        for style in original.styles:
            if style not in translated.styles:
                findings.append(Finding(display, line, "W-PO-STYLE",
                                        "the translation drops '<" + style + ">'"))

    return findings


def escape_cpp(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ").replace("\t", " ")


def identifier(text: str) -> str:
    """The declaration around a key has to compile, and a term's key is a dotted tag."""
    return "".join(character if character.isalnum() else "_" for character in text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", required=True, help="project directory holding the ui tree")
    parser.add_argument("--ui", default="UI", help="UI tree, relative to root (default: UI)")
    parser.add_argument("--out", required=True, help="generated header to write")
    parser.add_argument("--exclude", action="append", default=[],
                        help="path fragment to skip, repeatable (e.g. examples)")
    parser.add_argument("--notes", help="translator notes to write (default: beside --out)")
    parser.add_argument("--check-po", help="validate the markup in a translated .po and exit")
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    ui_root = root / args.ui
    if not ui_root.is_dir():
        print(f"error: no ui tree at {ui_root}", file=sys.stderr)
        return 2

    if args.check_po:
        terms, _, findings = read_terms(ui_root)
        findings += check_po(pathlib.Path(args.check_po), terms)
        for finding in findings:
            print(str(finding), file=sys.stderr if finding.code.startswith("E-") else sys.stdout)
        errors = [f for f in findings if f.code.startswith("E-")]
        print(f"gather_rml: {len(errors)} error(s) in {args.check_po}",
              file=sys.stderr if errors else sys.stdout)
        return 1 if errors else 0

    documents = sorted(p for p in ui_root.rglob("*.rml"))

    # Before the documents, because a `{tag:}` in one of them is checked against these.
    terms, external_terms, findings = read_terms(ui_root)

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
        if namespace in owners and owners[namespace] != display and namespace != "Folio.Common":
            findings.append(Finding(display, 1, "E-LOC-NAMESPACE",
                                    f"'{namespace}' is already used by {owners[namespace]}; "
                                    f"rename one of the files"))
            continue
        owners.setdefault(namespace, display)

        parser_instance = DocumentParser(display, terms)
        parser_instance.feed(document.read_text(encoding="utf-8"))
        parser_instance.close()
        findings.extend(parser_instance.findings)

        bucket = gathered.setdefault(namespace, {})
        for key, text, line, markup in parser_instance.entries:
            if key in bucket:
                existing_text, existing_path, existing_line, _ = bucket[key]
                if existing_text != text:
                    findings.append(Finding(display, line, "E-LOC-DUPLICATE",
                                            f"'{key}' in {namespace} already means "
                                            f"\"{existing_text}\" at {existing_path}:{existing_line}"))
                continue
            bucket[key] = (text, display, line, markup)

    # Terms are their own namespace: a name belongs to the game rather than to the screen that
    # happens to mention it, and every screen that mentions it must get the same translation.
    gatherable = {tag: text for tag, text in terms.items() if tag not in external_terms}
    if gatherable:
        bucket = gathered.setdefault(TERMS_NAMESPACE, {})
        owners.setdefault(TERMS_NAMESPACE, f"{args.ui}/terms")
        for tag in sorted(gatherable):
            bucket[tag] = (gatherable[tag], f"{args.ui}/terms", 0, None)

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
        lines.append(f"// {owners[namespace]}" if namespace != "Folio.Common" else "// shared components")
        for key in sorted(gathered[namespace]):
            text, _, _, _ = gathered[namespace][key]
            lines.append(f'static const FText FolioText_{identifier(namespace)}_{identifier(key)} = '
                         f'NSLOCTEXT("{namespace}", "{key}", "{escape_cpp(text)}");')
            total += 1
        lines.append("")

    out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")

    notes = pathlib.Path(args.notes) if args.notes else out.with_suffix(".notes.md")
    write_notes(notes, gathered, terms, external_terms)

    print(f"gather_rml: {total} string(s) in {len(gathered)} namespace(s) -> "
          f"{os.path.relpath(out, root)}")
    print(f"gather_rml: translator notes -> {os.path.relpath(notes, root)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
