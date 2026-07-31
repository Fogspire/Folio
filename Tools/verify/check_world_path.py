#!/usr/bin/env python3
"""Checks that a world-space document still responds to the properties it is drawn with.

A world document is *recorded* rather than drawn, and a recording carries only what the recording path
bothers to apply. `transform` was composed on the Slate path and nowhere else for a while, so every
world document ignored it silently: an animation moved its opacity and stayed exactly where it was.
Nothing caught that, because the only render coverage is a screenshot of the screen path.

So this drives a real editor and runs `folio.World.Verify`, which records the same document twice -
once plain, once with a property set on its body - and asserts the geometry moved the way the property
says it should. Nothing here is a golden image: there is no reference to keep in step with an edited
test document, and a failure names the property that was dropped rather than reporting that some
pixels differ.

Run it against the same documents the game ships, not a special one. A property that only the test
document uses is a property nobody notices losing.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

CHECK = re.compile(r"folio world verify: (\S+) (pass|FAIL) - (.*)$")
SUMMARY = re.compile(r"folio world verify: (passed|FAILED) on '([^']*)', "
                     r"(\d+) draw\(s\), (\d+) vertices, (\d+) failure\(s\)")

# Documents rather than cases: the command decides what to assert, so adding a check does not mean
# touching this file. Any document with geometry will do; these are the ones that exercise text,
# vector art and a background between them.
DOCUMENTS = (
    "/folio/examples/nav_test.rml",
    "/folio/examples/crt_terminal.rml",
)

SECONDS = 40


def run_case(editor: pathlib.Path, uproject: pathlib.Path, document: str,
             logs: pathlib.Path) -> tuple[bool, str]:
    name = re.sub(r"\W+", "_", document).strip("_")
    log_name = f"FolioWorldPath_{name}.log"
    log_path = logs / log_name
    log_path.unlink(missing_ok=True)

    arguments = [
        str(editor), str(uproject), "-game", "-windowed", "-resx=800", "-resy=600",
        "-nosplash", "-unattended",
        f"-ExecCmds=folio.World.Verify {document}",
        f"-log={log_name}",
    ]

    try:
        subprocess.run(arguments, timeout=SECONDS, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        # Expected: nothing tells the game to quit, so the window is closed by the timeout. The command
        # has run and logged long before that.
        pass

    if not log_path.is_file():
        return False, f"no log at {log_path}"

    checks: list[tuple[str, str, str]] = []
    summary = None
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.rstrip()
        if match := CHECK.search(stripped):
            checks.append((match.group(1), match.group(2), match.group(3)))
        if found := SUMMARY.search(stripped):
            summary = found

    if summary is None:
        return False, ("the command never reported. Either it did not run, or the document recorded "
                       "nothing at all")

    failed = [f"{name} ({detail})" for name, result, detail in checks if result != "pass"]
    if failed:
        return False, "; ".join(failed)

    if not checks:
        return False, "reported a summary but ran no checks"

    return True, f"{len(checks)} check(s), {summary.group(3)} draw(s), {summary.group(4)} vertices"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="path to UnrealEditor")
    parser.add_argument("--project", required=True, help="path to the .uproject")
    parser.add_argument("--filter", default="", help="run only documents whose path contains this")
    args = parser.parse_args()

    editor = pathlib.Path(args.editor)
    uproject = pathlib.Path(args.project).resolve()
    logs = uproject.parent / "Saved/Logs"
    logs.mkdir(parents=True, exist_ok=True)

    if not editor.is_file():
        print(f"check_world_path: no editor at {editor}", file=sys.stderr)
        return 2
    if not uproject.is_file():
        print(f"check_world_path: no project at {uproject}", file=sys.stderr)
        return 2

    failures = 0
    for document in DOCUMENTS:
        if args.filter and args.filter not in document:
            continue

        print(f"  {document} ... ", end="", flush=True)
        passed, detail = run_case(editor, uproject, document, logs)
        print(("ok   " if passed else "FAIL ") + detail)
        failures += 0 if passed else 1

    print("")
    if failures:
        print(f"World path check failed: {failures} document(s).", file=sys.stderr)
        return 1

    print("World path check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
