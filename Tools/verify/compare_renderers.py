#!/usr/bin/env python3
"""Holds the preview tool and the engine to each other, on where text actually landed.

The preview's entire value is showing what Unreal will show, so the two renderers agreeing is not a
nicety - it is the thing the preview *is*. And they have diverged before without anyone noticing:
`transform` was applied on one path and not the other for weeks, and every gradient decorator was a
white rectangle in the engine while the preview drew it correctly.

**Not an image diff.** The colour pipelines differ by design: the preview does no sRGB conversion
where Slate linearises vertex colour and applies display gamma, and the engine writes premultiplied
colour on a transparent ground. An image comparison therefore reports a difference on every document
and means nothing. Where a glyph was laid out does not differ by design, so that is what is compared,
and it diffs to nothing when the two agree.

Both sides emit the same schema: `uipreview --metrics-report` and `folio.Metrics.Report`. The dp ratio
is pinned on both, because the preview's follows its viewport by default and an unmatched ratio makes
every length differ - which reads as a rendering fault rather than a settings one.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

# Documents the game ships, not a special one. A divergence that only a test document provokes is a
# divergence nobody meets.
DOCUMENTS = (
    "ui/examples/panel.rml",
    "ui/examples/button.rml",
    "ui/examples/list.rml",
    "ui/examples/modal.rml",
    "ui/screens/settings.rml",
    # Layer effects. Only the text in them is compared - nothing here checks what a box-shadow or a
    # backdrop-filter looks like, because the preview draws those in OpenGL and the engine replays
    # them through RHI. Layout under an effect agreeing is worth having; it is not the whole claim.
    "ui/examples/effects.rml",
    "ui/examples/backdrop.rml",
)

SIZE = (1280, 720)
DP_RATIO = 1
SECONDS = 90


def run_preview(uipreview: pathlib.Path, root: pathlib.Path, document: str,
                out: pathlib.Path) -> str | None:
    arguments = [
        str(uipreview), document, "--root", str(root),
        "--size", f"{SIZE[0]}x{SIZE[1]}", "--dp-ratio", str(DP_RATIO),
        "--metrics-report", str(out),
    ]
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=SECONDS)
    if not out.is_file():
        return f"uipreview wrote no report: {result.stdout.strip() or result.stderr.strip()}"
    return None


def run_engine(editor: pathlib.Path, uproject: pathlib.Path, document: str,
               out: pathlib.Path) -> str | None:
    # Relative to the project, because that is what the console command resolves against.
    relative = out.relative_to(uproject.parent) if out.is_absolute() else out
    arguments = [
        str(editor), str(uproject), "-game", "-windowed", "-resx=400", "-resy=300",
        "-nosplash", "-unattended",
        f"-ExecCmds=folio.Metrics.Report {document} {relative.as_posix()} "
        f"{SIZE[0]} {SIZE[1]} {DP_RATIO}",
        "-log=FolioCompare.log",
    ]

    try:
        subprocess.run(arguments, timeout=SECONDS, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        # Expected: nothing tells the game to quit, so the window is closed by the timeout. The report
        # is written long before that.
        pass

    if not out.is_file():
        return "the engine wrote no report"
    return None


def compare(engine: dict, preview: dict) -> list[str]:
    problems: list[str] = []

    by_path_engine = {entry["path"]: entry for entry in engine["text"]}
    by_path_preview = {entry["path"]: entry for entry in preview["text"]}

    missing = sorted(set(by_path_preview) - set(by_path_engine))
    extra = sorted(set(by_path_engine) - set(by_path_preview))
    for path in missing[:5]:
        problems.append(f"the engine has no text at {path}")
    for path in extra[:5]:
        problems.append(f"the preview has no text at {path}")

    for path in sorted(set(by_path_engine) & set(by_path_preview)):
        a, b = by_path_engine[path], by_path_preview[path]

        if a["rect"] != b["rect"]:
            problems.append(f"{path}: box {a['rect']} against {b['rect']}")

        if a["font"]["size"] != b["font"]["size"]:
            problems.append(
                f"{path}: font size {a['font']['size']} against {b['font']['size']}")

        if len(a["lines"]) != len(b["lines"]):
            problems.append(
                f"{path}: {len(a['lines'])} line(s) against {len(b['lines'])}")
            continue

        for index, (line_a, line_b) in enumerate(zip(a["lines"], b["lines"])):
            if line_a["text"] != line_b["text"]:
                problems.append(f"{path} line {index}: {line_a['text']!r} against {line_b['text']!r}")
            elif (line_a["x"], line_a["y"]) != (line_b["x"], line_b["y"]):
                problems.append(
                    f"{path} line {index}: at ({line_a['x']}, {line_a['y']}) against "
                    f"({line_b['x']}, {line_b['y']})")

    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="path to UnrealEditor")
    parser.add_argument("--project", required=True, help="path to the .uproject")
    parser.add_argument("--uipreview", required=True, help="path to uipreview")
    parser.add_argument("--filter", default="", help="run only documents whose path contains this")
    args = parser.parse_args()

    editor = pathlib.Path(args.editor)
    uproject = pathlib.Path(args.project).resolve()
    uipreview = pathlib.Path(args.uipreview)
    root = uproject.parent

    for name, path in (("editor", editor), ("project", uproject), ("uipreview", uipreview)):
        if not path.is_file():
            print(f"compare_renderers: no {name} at {path}", file=sys.stderr)
            return 2

    reports = root / "Saved" / "FolioCompare"
    reports.mkdir(parents=True, exist_ok=True)

    failures = 0
    for document in DOCUMENTS:
        if args.filter and args.filter not in document:
            continue

        stem = document.replace("/", "_").replace(".rml", "")
        engine_path = reports / f"{stem}.engine.json"
        preview_path = reports / f"{stem}.preview.json"
        engine_path.unlink(missing_ok=True)
        preview_path.unlink(missing_ok=True)

        print(f"  {document} ... ", end="", flush=True)

        if error := run_preview(uipreview, root, document, preview_path):
            print(f"FAIL {error}")
            failures += 1
            continue

        if error := run_engine(editor, uproject, document, engine_path):
            print(f"FAIL {error}")
            failures += 1
            continue

        engine = json.loads(engine_path.read_text(encoding="utf-8"))
        preview = json.loads(preview_path.read_text(encoding="utf-8"))

        problems = compare(engine, preview)
        if problems:
            print(f"FAIL {len(problems)} difference(s)")
            for problem in problems[:8]:
                print(f"      {problem}")
            failures += 1
        else:
            print(f"ok   {engine['totals']['text_elements']} text element(s), "
                  f"{engine['totals']['lines']} line(s) agree")

    print("")
    if failures:
        print(f"Renderer comparison failed: {failures} document(s).", file=sys.stderr)
        return 1

    print("Renderer comparison passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
