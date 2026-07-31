#!/usr/bin/env python3
"""Fails if a module's public header includes one of its private headers.

The build does not catch this. A module's own translation units have Private on their include path,
so everything compiles here and only breaks for whoever consumes the plugin - and the error names the
public header, which is the one file that looked fine.

Folio carried this for a while and its own editor module worked around it by putting Folio/Private on
its include path. That is what this exists to stop coming back.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def modules(root: pathlib.Path) -> list[pathlib.Path]:
    return [path.parent for path in sorted(root.glob("*/Public")) if path.is_dir()]


def check(module: pathlib.Path) -> list[str]:
    private = module / "Private"
    public = module / "Public"

    if not private.is_dir():
        return []

    def relative(directory: pathlib.Path) -> set[str]:
        return {path.relative_to(directory).as_posix() for path in directory.rglob("*.h")}

    private_paths = relative(private)
    public_paths = relative(public)

    # A bare name resolves against Private directly, so it counts; a qualified one has to match the
    # path under Private. Without that second rule `FolioCore/FolioInputState.h` - another module's
    # public header - is flagged by any module whose Private holds a `FolioInputState.h` of its own.
    def is_private(included: str) -> bool:
        if included in public_paths:
            return False
        if "/" in included:
            return included in private_paths
        return included in {pathlib.PurePosixPath(path).name for path in private_paths}

    problems = []
    for header in sorted(public.rglob("*.h")):
        for included in INCLUDE.findall(header.read_text(encoding="utf-8", errors="replace")):
            if is_private(included):
                problems.append(f"{header.relative_to(module.parent).as_posix()} includes {included}")

    return problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default="Source")
    arguments = parser.parse_args()

    root = pathlib.Path(arguments.source)
    if not root.is_dir():
        print(f"error: no such directory: {root}", file=sys.stderr)
        return 2

    found = []
    checked = 0

    for module in modules(root):
        checked += 1
        found += check(module)

    if found:
        print("public headers reaching into Private:", file=sys.stderr)
        for problem in found:
            print(f"  {problem}", file=sys.stderr)
        print("\nMove the header to Public, or forward declare the type.", file=sys.stderr)
        return 1

    print(f"{checked} module(s), no public header reaches into Private")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
