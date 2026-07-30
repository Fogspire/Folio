#!/usr/bin/env python3
"""Runs RmlUi's own visual test suite against Folio's renderer.

RmlUi ships ~109 documents covering layout, floats, flexbox, tables, transforms, filters, clip masks,
box-shadow and text. They are a far broader exercise of the render interface than anything hand-written
here, which is exactly why the brief names them as the correctness harness for Phase 3.

They are not our documents and they are not written to our conventions. Several use `<link>` tags with
no `type` (metadata for RmlUi's own test app), some name features Folio bans, and a few need input to
show anything. So the bar is not "zero diagnostics": it is that a document renders without crashing,
and that its diagnostic count has not gone up since the recorded baseline. A regression in the renderer
moves those numbers; a document we never handled stays where it was and is visible in the file.

Screenshots are written for every test so a person can look, which is the only way to catch "renders,
but wrong".
"""

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys

# 0 ok, 1 document errors, 2 usage, 3 init, 4 runtime, 5 strict warnings. Anything else is a crash: a
# segfault surfaces here as something else entirely.
MAX_DOCUMENTED_EXIT = 5

LATO_FACES = ("LatoLatin-Regular", "LatoLatin-Bold", "LatoLatin-Italic", "LatoLatin-BoldItalic")


def fail(message: str) -> int:
    print(f"visual_tests: {message}", file=sys.stderr)
    return 1


def find_rmlui_tree(repo: pathlib.Path) -> pathlib.Path | None:
    """The suite lives in the fetched RmlUi tree, so it follows the pin without being vendored."""
    for config in ("win64-RelWithDebInfo", "win64-Debug", "win64-Release"):
        candidate = repo / "build" / config / "_deps/rmlui-src"
        if (candidate / "Tests/Data/VisualTests").is_dir():
            return candidate
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", help="the Folio repository root")
    parser.add_argument("--out", help="where reports and screenshots are written")
    parser.add_argument("--filter", default="", help="substring match on the document name")
    parser.add_argument("--update-baseline", action="store_true",
                        help="record what the suite does now as the new expectation; read the diff before committing it")
    args = parser.parse_args()

    repo = pathlib.Path(args.repo).resolve() if args.repo else pathlib.Path(__file__).resolve().parents[2]
    preview = repo / "bin/win64/uipreview.exe"
    if not preview.is_file():
        preview = repo / "bin/win64/uipreview"
    if not preview.is_file():
        return fail(f"{preview} not found. Run scripts/build.sh first.")

    baseline_path = repo / "Tools/uipreview/tests/visual_baseline.json"
    out = pathlib.Path(args.out).resolve() if args.out else repo / "build/visual"

    rmlui_root = find_rmlui_tree(repo)
    if rmlui_root is None:
        return fail("no fetched RmlUi tree with Tests/Data/VisualTests. Run scripts/build.sh first.")

    tests_data = rmlui_root / "Tests/Data"
    assets = rmlui_root / "Samples/assets"

    # The suite's own stylesheet names LatoLatin and nothing else. Folio bundles no Lato, and
    # substituting a different face would change every line break in the suite.
    fonts: list[str] = []
    for face in LATO_FACES:
        path = assets / f"{face}.ttf"
        if path.is_file():
            fonts += ["--font", str(path)]
    if not fonts:
        return fail(f"no LatoLatin faces under {assets}")

    out.mkdir(parents=True, exist_ok=True)

    # Several tests name art as `assets/invader.tga`, which RmlUi's own harness finds because it adds
    # Samples/assets as a second search path. uipreview has one root by design, so the files are copied
    # under it instead. This is the fetched build tree, so a clean rebuild undoes it and the copy happens
    # again.
    assets_under_root = tests_data / "assets"
    assets_under_root.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.tga", "*.png"):
        for art in assets.rglob(pattern):
            target = assets_under_root / art.name
            if not target.exists():
                shutil.copy2(art, target)

    documents = sorted((tests_data / "VisualTests").glob("*.rml"))
    if args.filter:
        documents = [d for d in documents if args.filter in d.stem]

    baseline: dict[str, dict] = {}
    if baseline_path.is_file():
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))

    results: dict[str, dict] = {}
    failures: list[str] = []
    crashes = 0

    print(f"Running {len(documents)} visual tests")

    for document in documents:
        name = document.stem
        report = out / f"{name}.json"
        shot = out / f"{name}.png"

        completed = subprocess.run(
            [str(preview), str(document), "--root", str(tests_data), *fonts,
             "--size", "1024x768", "--dp-ratio", "1", "--no-assets-wanted",
             "--json-report", str(report), "--screenshot", str(shot)],
            capture_output=True, text=True)
        code = completed.returncode

        errors = -1
        warnings = -1
        if report.is_file():
            try:
                parsed = json.loads(report.read_text(encoding="utf-8"))
                errors = int(parsed["result"]["errors"])
                warnings = int(parsed["result"]["warnings"])
            except (json.JSONDecodeError, KeyError, TypeError, ValueError):
                pass

        results[name] = {"exit": code, "errors": errors, "warnings": warnings}

        if code < 0 or code > MAX_DOCUMENTED_EXIT:
            crashes += 1
            failures.append(f"{name} crashed (exit {code})")
            print(f"  CRASH {name} (exit {code})")
            continue

        if not shot.is_file():
            failures.append(f"{name} produced no image")
            print(f"  FAIL  {name} produced no image")
            continue

        if not args.update_baseline and name in baseline:
            was = baseline[name]
            was_errors = int(was.get("errors", -1))
            was_warnings = int(was.get("warnings", -1))

            if errors > was_errors:
                failures.append(f"{name}: {was_errors} errors became {errors}")
                print(f"  FAIL  {name} errors {was_errors} -> {errors}")
                continue
            if warnings > was_warnings:
                failures.append(f"{name}: {was_warnings} warnings became {warnings}")
                print(f"  FAIL  {name} warnings {was_warnings} -> {warnings}")
                continue
            if errors < was_errors or warnings < was_warnings:
                print(f"  better {name} {was_errors}/{was_warnings} -> {errors}/{warnings}")
                continue
        elif not args.update_baseline:
            print(f"  new   {name} ({errors} errors, {warnings} warnings)")
            continue

        print(f"  ok    {name}")

    if args.update_baseline:
        # No byte order mark, and a trailing newline: this file is read by python here and was written
        # by PowerShell before, and a BOM stops most other readers parsing it.
        baseline_path.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"Baseline written to {baseline_path}")
        return 0

    print("")

    # Most of the suite reports something, and almost all of it is the same two things: `<link>` tags
    # carrying metadata rather than a stylesheet, which RmlUi's parser rejects, and seventeen documents
    # whose stylesheet path is wrong upstream. Counting documents that say nothing at all would
    # therefore measure the suite, not the renderer. What matters is that every one produced an image
    # and none of them moved.
    drawn = sum(1 for r in results.values() if 0 <= r["exit"] <= MAX_DOCUMENTED_EXIT)
    print(f"{drawn} of {len(results)} rendered.")

    if not failures:
        print(f"Visual tests passed. Images in {out}")
        return 0

    print(f"Visual tests failed: {len(failures)} problem(s), {crashes} crash(es).", file=sys.stderr)
    for message in failures:
        print(f"  {message}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
