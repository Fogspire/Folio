#!/usr/bin/env python3
"""Records and checks which sources Folio Studio's committed renderer was built from.

Studio draws with the real renderer compiled to WebAssembly, and that build is committed so that
building Studio needs Node and nothing else. The cost of committing a build is that it can fall behind
its sources without anything saying so: a fix to the text engine would be in the game and in uipreview
and silently absent from the editor.

So the build records a hash of everything it was compiled from, and a check compares that against the
sources as they stand. Line endings are normalised first, so a checkout that converts them agrees with
one that does not.

  source_hash.py --write     after building the engine
  source_hash.py --check     exits 1 when the sources have moved on from the build
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[3]
RECORD = pathlib.Path(__file__).resolve().parent / "dist" / "SOURCE_HASH"

# Everything the WebAssembly target compiles or is configured by. Dependencies are pinned by commit in
# Pins.cmake, so hashing the pins and the patches covers them without hashing their sources.
INPUTS = [
    "CMakeLists.txt",
    "cmake/Pins.cmake",
    "cmake/ApplyPatches.cmake",
    "patches",
    "Source/FolioCore",
    "Source/ThirdParty/SkiaSimplify",
    "Tools/uipreview/CMakeLists.txt",
    "Tools/uipreview/src",
    "Tools/studio/engine/CMakeLists.txt",
    "Tools/studio/engine/src",
]
EXTENSIONS = {".h", ".hpp", ".c", ".cc", ".cpp", ".cmake", ".txt", ".patch"}


def files() -> list[pathlib.Path]:
    found: list[pathlib.Path] = []
    for entry in INPUTS:
        path = REPO / entry
        if path.is_file():
            found.append(path)
        elif path.is_dir():
            found += [child for child in path.rglob("*") if child.is_file() and child.suffix.lower() in EXTENSIONS]
    return sorted(found, key=lambda path: path.relative_to(REPO).as_posix())


def compute() -> str:
    digest = hashlib.sha256()
    for path in files():
        digest.update(path.relative_to(REPO).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes().replace(b"\r\n", b"\n"))
        digest.update(b"\0")
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true")
    group.add_argument("--check", action="store_true")
    args = parser.parse_args()

    current = compute()

    if args.write:
        RECORD.parent.mkdir(parents=True, exist_ok=True)
        RECORD.write_text(current + "\n", encoding="utf-8")
        print(f"studio engine: recorded {current[:12]}")
        return 0

    if not RECORD.is_file() or not (RECORD.parent / "folio_engine.wasm").is_file():
        print("studio engine: no committed build. Run scripts/studio.sh engine", file=sys.stderr)
        return 1

    recorded = RECORD.read_text(encoding="utf-8").strip()
    if recorded != current:
        print("studio engine: the committed WebAssembly build is older than its sources.\n"
              f"  built from {recorded[:12]}, sources are {current[:12]}\n"
              "  Run scripts/studio.sh engine and commit Tools/studio/engine/dist.", file=sys.stderr)
        return 1

    print(f"studio engine: up to date ({current[:12]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
