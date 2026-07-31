#!/usr/bin/env python3
"""Checks that idle documents idle and busy documents do not.

Folio skips a document's update and paint when nothing has changed, which is worth a great deal and
whose failure mode is a **frozen document** - far worse than a slow one, and presenting as "the caret
stopped blinking" or "the hover state sticks" rather than as anything obvious.

So this asserts both directions, because only checking that idling works would pass a build where
everything idles forever:

  static   a document with nothing moving must update a handful of times and then stop
  animated a document with a running animation must update every frame
  caret    a document whose focused text field is blinking must update on the blink timer

It drives a real editor in -game mode with folio.Invalidation.Trace on and counts what the log says,
because the thing being measured only exists on the Unreal side: uipreview has its own frame loop and
would prove nothing about it.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

TRACE = re.compile(r"folio invalidation: updated because '([^']*)', next update in (.+)$")


class Case:
    def __init__(self, name: str, command: str, document: str, seconds: int,
                 min_updates: int, max_updates: int, expect_delay: str) -> None:
        self.name = name
        self.command = command
        self.document = document
        self.seconds = seconds
        self.min_updates = min_updates
        self.max_updates = max_updates
        self.expect_delay = expect_delay


# folio.Show does not take focus, which is what the static case needs; folio.NavTest does, which is what
# the caret needs, since the blink timer only runs while the field has focus.
CASES = (
    Case("static", "folio.Show", "UI/examples/panel.rml", 20,
         min_updates=1, max_updates=12, expect_delay="never"),
    Case("animated", "folio.Show", "/folio/examples/crt_terminal.rml", 20,
         min_updates=200, max_updates=10 ** 9, expect_delay="0.000s"),
    Case("caret", "folio.NavTest", "/folio/examples/crt_terminal.rml", 20,
         min_updates=10, max_updates=400, expect_delay="timer"),
)


def run_case(editor: pathlib.Path, uproject: pathlib.Path, case: Case,
             logs: pathlib.Path) -> tuple[bool, str]:
    log_name = f"FolioIdle_{case.name}.log"
    log_path = logs / log_name
    log_path.unlink(missing_ok=True)

    arguments = [
        str(editor), str(uproject), "-game", "-windowed", "-resx=800", "-resy=600",
        "-nosplash", "-unattended",
        "-ExecCmds=folio.Invalidation.Trace 1, "
        f"{case.command} {case.document}",
        f"-log={log_name}",
    ]

    try:
        subprocess.run(arguments, timeout=case.seconds, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        # Expected: nothing tells the game to quit, so the window is closed by the timeout and the log
        # holds however many frames fitted in the window.
        pass

    if not log_path.is_file():
        return False, f"no log at {log_path}"

    delays: list[str] = []
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = TRACE.search(line.rstrip())
        if match:
            delays.append(match.group(2).strip())

    count = len(delays)
    if count < case.min_updates:
        return False, (f"{count} update(s), expected at least {case.min_updates}. A document that "
                       f"should keep updating has gone idle, which is the frozen-document failure")
    if count > case.max_updates:
        return False, (f"{count} update(s), expected at most {case.max_updates}. A document that "
                       f"should be idle is updating anyway, so the dirty tracking is over-eager")

    last = delays[-1]
    if case.expect_delay == "never":
        if "never" not in last:
            return False, f"reported '{last}', expected 'never, until input'"
    elif case.expect_delay == "timer":
        if "never" in last or last.startswith("0.000"):
            return False, f"reported '{last}', expected a blink timer between 0 and 1 second"
    elif last != case.expect_delay:
        return False, f"reported '{last}', expected '{case.expect_delay}'"

    return True, f"{count} update(s) in {case.seconds}s, last reported '{last}'"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="path to UnrealEditor")
    parser.add_argument("--project", required=True, help="path to the .uproject")
    parser.add_argument("--filter", default="", help="run only cases whose name contains this")
    args = parser.parse_args()

    editor = pathlib.Path(args.editor)
    uproject = pathlib.Path(args.project).resolve()
    logs = uproject.parent / "Saved/Logs"
    logs.mkdir(parents=True, exist_ok=True)

    if not editor.is_file():
        print(f"check_invalidation: no editor at {editor}", file=sys.stderr)
        return 2
    if not uproject.is_file():
        print(f"check_invalidation: no project at {uproject}", file=sys.stderr)
        return 2

    failures = 0
    for case in CASES:
        if args.filter and args.filter not in case.name:
            continue

        print(f"  {case.name} ... ", end="", flush=True)
        passed, detail = run_case(editor, uproject, case, logs)
        print(("ok   " if passed else "FAIL ") + detail)
        failures += 0 if passed else 1

    print("")
    if failures:
        print(f"Invalidation check failed: {failures} case(s).", file=sys.stderr)
        return 1

    print("Invalidation check passed: idle documents idle, busy ones do not.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
