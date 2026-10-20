#!/usr/bin/env python3
"""Runs the Folio verification harness.

Three things, in order of how much they catch:

Parity. The HLSL and GLSL field shaders must agree, or the editor and the preview tool disagree about
text in a way no screenshot comparison would reveal.

Layout. Every test and example document is rendered under --strict and its text metrics written out.
Given a baseline, the reports are compared numerically: a moved line break is the symptom of an advance
that changed, which is the regression this system is most likely to cause and the least likely to be
noticed by eye.

Images. Screenshots of the scale, weight and typography documents. These are for a person to look at -
the distance field case is about how text reads at 8x magnification and at 10dp, and neither is a number.

Nothing here asserts against a committed golden image. The two renderers' colour pipelines differ,
displays differ, and a harness that cries wolf gets ignored.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

# The dp ratio is pinned rather than derived from the window. A window larger than the display is
# silently clamped, so an unpinned run reports whatever ratio the desktop happened to allow and two
# machines would never agree.
# Spelled without a trailing .0 because that is what the harness has always written, and a
# baseline directory recorded before this was ported still has to compare against a new run.
RATIOS = ("1", "2")

SHOTS = (
    ("scale.rml", "1920x1080", "1"),
    ("weights.rml", "1920x1080", "1"),
    ("typography.rml", "1920x1080", "1"),
    ("typography.rml", "1920x1080", "2"),
    ("shaping.rml", "1280x820", "1"),
    ("borders.rml", "1000x620", "1"),
    ("effects.rml", "1920x1080", "1"),
    ("effect_edges.rml", "800x480", "1"),
    ("vector_stack.rml", "600x400", "1"),
    ("outside_blur.rml", "700x460", "1"),
    ("vector_strokes.rml", "620x700", "1"),
    ("textured_text.rml", "620x480", "1"),
    ("shader_decorator.rml", "640x620", "1"),
)

DEVICE_CASES = (("keyboard", "south"), ("mouse", "south"), ("gamepad", "south"), ("gamepad", "east"))


class Harness:
    def __init__(self, preview: pathlib.Path, out: pathlib.Path) -> None:
        self.preview = preview
        self.out = out
        self.failures: list[str] = []

    def fail(self, message: str) -> None:
        print(f"  FAIL {message}")
        self.failures.append(message)

    @staticmethod
    def ok(message: str) -> None:
        print(f"  ok   {message}")

    def run(self, arguments: list[str]) -> subprocess.CompletedProcess:
        # uipreview reports on stderr by design, so its output is captured rather than treated as a
        # failure signal. The exit code is what says whether it worked.
        return subprocess.run([str(self.preview), *arguments], capture_output=True, text=True)


def read_json(path: pathlib.Path):
    return json.loads(path.read_text(encoding="utf-8"))


def laid_out_text(report: pathlib.Path) -> list[str]:
    """Text of every element the report says was laid out.

    The metrics report only lists line boxes for text that was laid out, so an element hidden by a class
    rule or by data-if has none - which is what makes visibility checkable without comparing images.
    """
    parsed = read_json(report)
    return [entry["text"] for entry in parsed.get("text", []) if entry.get("lines")]


def all_lines(report_data) -> list[str]:
    return [line["text"] for entry in report_data.get("text", []) for line in entry.get("lines", [])]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", help="the Folio repository root")
    parser.add_argument("--out", help="where reports and screenshots are written")
    parser.add_argument("--baseline",
                        help="a previous --out directory; metrics reports are compared against it and "
                             "differences listed. Without it the run only records")
    parser.add_argument("--skip-shader-parity", action="store_true")
    parser.add_argument("--visual-tests", action="store_true",
                        help="also run RmlUi's own visual test suite. Off by default: 109 documents take "
                             "longer than the rest of the harness put together")
    args = parser.parse_args()

    repo = pathlib.Path(args.repo).resolve() if args.repo else pathlib.Path(__file__).resolve().parents[2]
    candidates = [repo / "bin/win64/uipreview.exe", repo / "bin/win64/uipreview", repo / "bin/linux/uipreview",
                  repo / "bin/mac/uipreview"]
    preview = next((path for path in candidates if path.is_file()), candidates[0])
    if not preview.is_file():
        print(f"verify: {preview} not found. Run scripts/build.sh first.", file=sys.stderr)
        return 1

    out = pathlib.Path(args.out).resolve() if args.out else repo / "build/verify"
    out.mkdir(parents=True, exist_ok=True)

    harness = Harness(preview, out)
    tests_dir = repo / "Tools/uipreview/tests"
    # The examples are documents a game copies, so a regression in one is inherited rather
    # than contained. They are checked alongside the fixtures for that reason.
    document_dirs = [tests_dir, repo / "examples"]

    # What a document needs beyond the defaults, and what it is supposed to do. missing_assets.rml
    # exists to be caught, so a clean exit from it would mean the sprite checks had stopped working.
    hud_json = str(tests_dir / "hud.json")
    setup: dict[str, dict] = {
        "data_model": {"args": ["--data", hud_json]},
        "hud": {"args": ["--data", hud_json]},
        "menu": {"args": ["--data", hud_json]},
        "missing_assets": {"expect": 5},
        "effect_edges": {"expect": 5},
    }

    # --- Shader parity ----------------------------------------------------------------------

    if not args.skip_shader_parity:
        print("Shader parity")
        parity = subprocess.run([sys.executable, str(repo / "Tools/shaderparity/check_parity.py"),
                                 "--repo", str(repo)], capture_output=True, text=True)
        if parity.returncode != 0:
            harness.fail("HLSL and GLSL field shaders disagree (run scripts/check_shader_parity.sh)")
        else:
            harness.ok("FolioMsdf.ush matches MsdfProgram.cpp")

    # --- Studio's committed renderer ---------------------------------------------------------
    #
    # Built from the same sources as the tool under test, and committed. Nothing else notices when it
    # falls behind them.
    print("Studio renderer")
    engine_hash = subprocess.run([sys.executable, str(repo / "Tools/studio/engine/source_hash.py"), "--check"],
                                 capture_output=True, text=True)
    if engine_hash.returncode != 0:
        harness.fail("Folio Studio's WebAssembly renderer is older than its sources (run scripts/studio.sh engine)")
    else:
        harness.ok("built from the current sources")

    # --- Documents --------------------------------------------------------------------------

    print("Layout")
    for document in sorted(d for directory in document_dirs for d in directory.glob("*.rml")):
        for ratio in RATIOS:
            name = f"{document.stem}@{ratio}"
            report = out / f"{name}.metrics.json"

            entry = setup.get(document.stem, {})
            extra = entry.get("args", [])
            expect = entry.get("expect", 0)

            result = harness.run([str(document), "--size", "1920x1080", "--dp-ratio", ratio,
                                  "--strict", "--no-assets-wanted", "--metrics-report", str(report),
                                  *extra])

            if result.returncode != expect:
                harness.fail(f"{name} exited {result.returncode}, expected {expect}")
                for line in (result.stdout + result.stderr).splitlines()[:5]:
                    print(f"    {line}")
                continue
            if not report.is_file():
                harness.fail(f"{name} wrote no metrics report")
                continue
            harness.ok(name)

    # --- Compare against a baseline ---------------------------------------------------------

    if args.baseline:
        print("Metrics diff")
        baseline = pathlib.Path(args.baseline)
        if not baseline.is_dir():
            harness.fail(f"baseline {baseline} does not exist")
        else:
            for current in sorted(out.glob("*.metrics.json")):
                before = baseline / current.name
                if not before.is_file():
                    print(f"  new  {current.stem}")
                    continue

                # Line text first. A box that resized is worth knowing about; a line that broke
                # somewhere new is what actually changes how a screen reads.
                lines_before = all_lines(read_json(before))
                lines_now = all_lines(read_json(current))

                if len(lines_before) != len(lines_now):
                    harness.fail(f"{current.stem}: {len(lines_before)} lines became {len(lines_now)}")
                    continue

                moved = 0
                for index, (was, now) in enumerate(zip(lines_before, lines_now)):
                    if was != now:
                        if moved < 5:
                            print(f"    line {index}")
                            print(f"      was: {was}")
                            print(f"      now: {now}")
                        moved += 1

                if moved:
                    if moved >= 5:
                        print(f"    ... and {moved - 5} more")
                    harness.fail(f"{current.stem}: {moved} lines wrapped differently")
                else:
                    harness.ok(f"{current.stem} unchanged")

    # --- Screenshots ------------------------------------------------------------------------

    print("Screenshots")
    for document_name, size, ratio in SHOTS:
        document = next((directory / document_name for directory in document_dirs
                         if (directory / document_name).is_file()), tests_dir / document_name)
        if not document.is_file():
            harness.fail(f"missing {document_name}")
            continue

        name = f"{document.stem}@{ratio}.png"
        png = out / name

        harness.run([str(document), "--size", size, "--dp-ratio", ratio,
                     "--no-assets-wanted", "--screenshot", str(png)])
        if png.is_file():
            harness.ok(name)
        else:
            harness.fail(f"no screenshot for {name}")

    # --- Input device -----------------------------------------------------------------------

    # Both routes the device reaches markup by, across every combination.
    print("Input device")
    device_doc = tests_dir / "input_device.rml"
    if not device_doc.is_file():
        harness.fail("missing input_device.rml")
    else:
        for device, confirm in DEVICE_CASES:
            label = f"{device}/{confirm}"
            report = out / f"input_device.{device}.{confirm}.json"

            result = harness.run([str(device_doc), "--strict", "--no-assets-wanted",
                                  "--input-device", device, "--confirm-button", confirm,
                                  "--metrics-report", str(report)])

            if result.returncode != 0:
                harness.fail(f"{label} exited {result.returncode}")
                continue
            if not report.is_file():
                harness.fail(f"{label} wrote no report")
                continue

            shown = laid_out_text(report)
            pad = device == "gamepad"
            expected = [
                (f"device={device}", True),
                (f"gamepad={'1' if pad else '0'}", True),
                (f"confirm={confirm}", True),
                (f"back={'south' if confirm == 'east' else 'east'}", True),
                ("the confirm button", pad),
                ("Enter", not pad),
                ("A / Cross", confirm == "south"),
                ("B / Circle", confirm == "east"),
                (f"a sprite name built from it: btn_{confirm}", pad),
            ]

            wrong = [f"'{text}' should{'' if want else ' not'} show"
                     for text, want in expected if (text in shown) != want]
            if wrong:
                harness.fail(f"{label} - {'; '.join(wrong)}")
            else:
                harness.ok(label)

    # --- Stylesheets -------------------------------------------------------------------------

    # @import, @font-face and :disabled come from RmlUi patches, so a pin moved without them would
    # parse cleanly and quietly lose all three.
    print("Stylesheets")
    sheets_doc = tests_dir / "stylesheets.rml"
    sheets_report = out / "stylesheets@1.metrics.json"
    if not sheets_doc.is_file() or not sheets_report.is_file():
        harness.fail("missing stylesheets.rml or its metrics report")
    else:
        entries = {entry["path"]: entry for entry in read_json(sheets_report)["text"]}
        widths = {name: entries.get(f"#{name}", {}).get("rect", [0, 0, 0, 0])[2] for name in ("w", "p", "n")}
        if widths != {"w": 300, "p": 200, "n": 100}:
            harness.fail(f"@import cascade order is wrong: widths {widths}, expected 300, 200, 100")
        else:
            harness.ok("@import, nested and in cascade order")

        narrow, wide = (entries.get(f"#{name}", {}).get("lines", [{}])[0].get("width") for name in ("narrow", "wide"))
        if narrow is None or narrow != wide:
            harness.fail(f"@font-face did not load a monospace face: 'iiii' is {narrow} wide and 'WWWW' {wide}")
        else:
            harness.ok("@font-face")

        result = harness.run([str(sheets_doc), "--no-assets-wanted", "--keys", "down"])
        if "focus-sequence: #last" not in result.stdout:
            harness.fail("down from #first did not skip the disabled element")
        else:
            harness.ok(":disabled on a generic element leaves navigation")

    # --- Accessibility tree -----------------------------------------------------------------

    # What a screen reader is read out of, against a committed transcript. The engine builds its
    # Slate accessibility tree from this same walk, so a change in what this prints is a change in
    # what a player using NVDA hears - and that is not a thing anyone notices by looking.
    print("Accessibility tree")
    tree_doc = tests_dir / "a11y_tree.rml"
    expected_path = tests_dir / "a11y_tree.expected.txt"

    # The second one is about what is *not* in the tree: a dialog's background is inert, and a reader
    # that can still walk into it tells a player about buttons they are not allowed to press.
    for document, transcript in ((tree_doc, expected_path),
                                 (tests_dir / "a11y_modal.rml",
                                  tests_dir / "a11y_modal.expected.txt")):
        if not document.is_file() or not transcript.is_file():
            harness.fail(f"missing {document.name} or its expected transcript")
            continue

        result = harness.run([str(document), "--strict", "--no-assets-wanted", "--a11y",
                              "--a11y-tree"])

        if result.returncode != 0:
            harness.fail(f"{document.name} exited {result.returncode} - the fixture must stay clean")
            for line in (result.stdout + result.stderr).splitlines()[:5]:
                print(f"    {line}")
            continue

        produced = result.stdout.splitlines()
        expected = transcript.read_text(encoding="utf-8").splitlines()
        if produced != expected:
            harness.fail(f"the accessibility tree of {document.name} changed")
            for index in range(max(len(produced), len(expected))):
                was = expected[index] if index < len(expected) else "<end>"
                now = produced[index] if index < len(produced) else "<end>"
                if was != now:
                    print(f"    line {index}")
                    print(f"      was: {was}")
                    print(f"      now: {now}")
        else:
            harness.ok(f"{document.name}, {len(expected)} nodes, unchanged")

    # --- Flashing ---------------------------------------------------------------------------

    # A check that never fires is a check nobody knows is broken, so this is measured against a
    # document built to be over the limit. Three flashes a second is where WCAG puts a seizure risk,
    # and it is a question on a platform submission form.
    print("Flashing")
    strobe = tests_dir / "a11y_flash.rml"
    if not strobe.is_file():
        harness.fail("missing a11y_flash.rml")
    else:
        result = harness.run([str(strobe), "--no-assets-wanted", "--flash-check",
                              "--filmstrip-frames", "90", "--size", "320x240"])

        if "W-A11Y-FLASH" not in result.stdout + result.stderr:
            harness.fail("the flash check did not catch a document flashing ten times a second")
        else:
            harness.ok("a strobe is caught")

        # And the other half: the documents that do animate must not read as flashing.
        for name in ("menu", "effects"):
            document = tests_dir / f"{name}.rml"
            if not document.is_file():
                continue

            quiet = harness.run([str(document), "--no-assets-wanted", "--flash-check",
                                 "--filmstrip-frames", "90", "--size", "320x240"])

            if "W-A11Y-FLASH" in quiet.stdout + quiet.stderr:
                harness.fail(f"{name}.rml reads as flashing, which it is not")
            else:
                harness.ok(f"{name} does not")

    # --- Presentation settings --------------------------------------------------------------

    # Larger text, less movement and a different colour vision change how a screen looks. None of
    # them may change what it means: the same controls, in the same order, with the same names. A
    # setting that quietly rewrote the accessibility tree would read differently to the one player
    # most likely to have turned it on.
    print("Presentation settings")
    if tree_doc.is_file() and expected_path.is_file():
        expected = expected_path.read_text(encoding="utf-8").splitlines()

        for label, flags in (
            ("text scale 1.5", ["--text-scale", "1.5"]),
            ("reduced motion", ["--reduced-motion"]),
            ("deuteranopia", ["--simulate", "deuteranopia"]),
        ):
            result = harness.run([str(tree_doc), "--no-assets-wanted", "--a11y-tree", *flags])
            if result.stdout.splitlines() != expected:
                harness.fail(f"{label} changed what the document means")
            else:
                harness.ok(label)

        # Text speed is its own setting rather than part of reduced motion, so it is checked on its
        # own terms: the same frame of the same line, typed at three speeds, has to differ.
        dialogue = repo / "rcss" / "recipes" / "dialogue.rml"
        if dialogue.is_file():
            shots = {}
            for label, extra in (("authored", []), ("fast", ["--text-speed", "4"]),
                                 ("instant", ["--instant-text"])):
                path = out / f"reveal.{label}.png"
                harness.run([str(dialogue), "--root", str(repo), "--frames", "12",
                             "--no-assets-wanted", "--screenshot", str(path), *extra])
                shots[label] = path.read_bytes() if path.is_file() else b""

            if not all(shots.values()):
                harness.fail("could not capture the reveal at three speeds")
            elif len(set(shots.values())) != 3:
                harness.fail("text speed changed nothing - the same frame typed at three speeds "
                             "should show three different amounts of the line")
            else:
                harness.ok("text speed and instant text differ from authored")

        # The screen effect layers have to actually leave. This is the one setting whose whole job is
        # that a marked element stops being drawn, so a theme that silently stopped matching would be
        # invisible until a player with photosensitivity found it.
        edges = repo / "examples" / "screen_edge.rml"
        if edges.is_file():
            drawn = out / "effects.on.png"
            hidden = out / "effects.off.png"
            for path, extra in ((drawn, []), (hidden, ["--no-screen-effects"])):
                harness.run([str(edges), "--root", str(repo), "--size", "640x400", "--dp-ratio", "1",
                             "--no-assets-wanted", "--screenshot", str(path), *extra])

            if not drawn.is_file() or not hidden.is_file():
                harness.fail("could not capture the screen effect pair")
            elif drawn.read_bytes() == hidden.read_bytes():
                harness.fail("--no-screen-effects left the marked layers on screen")
            else:
                harness.ok("screen effects leave when turned off")

        # And the simulation has to actually do something, or it is a flag that reads as a pass.
        plain = out / "simulate.normal.png"
        seen = out / "simulate.deuteranopia.png"
        harness.run([str(tests_dir / "typography.rml"), "--size", "640x480", "--dp-ratio", "1",
                     "--no-assets-wanted", "--screenshot", str(plain)])
        harness.run([str(tests_dir / "typography.rml"), "--size", "640x480", "--dp-ratio", "1",
                     "--no-assets-wanted", "--screenshot", str(seen),
                     "--simulate", "deuteranopia"])

        if not plain.is_file() or not seen.is_file():
            harness.fail("could not capture the simulation pair")
        elif plain.read_bytes() == seen.read_bytes():
            harness.fail("--simulate deuteranopia produced the same image as no simulation")
        else:
            harness.ok("--simulate redraws the capture")

    # --- Texture reload ---------------------------------------------------------------------

    # What a hot reload does, done twice in one process: render, throw every texture away, render again.
    # The two images must be identical. They are not, if something releases a texture it does not own -
    # which is easy to do, because a glyph atlas and a baked sprite sheet reach the render interface
    # through the same call and only one of them belongs to RmlUi.
    print("Texture reload")
    for name in ("typography", "effects", "menu"):
        document = tests_dir / f"{name}.rml"
        if not document.is_file():
            continue

        plain = out / f"{name}.reload-before.png"
        after = out / f"{name}.reload-after.png"
        extra = setup.get(name, {}).get("args", [])

        harness.run([str(document), "--size", "1920x1080", "--dp-ratio", "1", "--no-assets-wanted",
                     "--screenshot", str(plain), *extra])
        harness.run([str(document), "--size", "1920x1080", "--dp-ratio", "1", "--no-assets-wanted",
                     "--reload-textures", "--screenshot", str(after), *extra])

        if not plain.is_file() or not after.is_file():
            harness.fail(f"{name}: reload comparison produced no image")
            continue

        digest_before = hashlib.sha256(plain.read_bytes()).hexdigest()
        digest_after = hashlib.sha256(after.read_bytes()).hexdigest()
        if digest_before != digest_after:
            harness.fail(f"{name} renders differently after releasing its textures")
        else:
            harness.ok(f"{name} survives a texture reload")

    # --- RmlUi's visual test suite ----------------------------------------------------------

    if args.visual_tests:
        print("Visual tests")
        visual = subprocess.run([sys.executable, str(repo / "Tools/verify/visual_tests.py"),
                                 "--repo", str(repo), "--out", str(out / "visual")])
        if visual.returncode != 0:
            harness.fail("RmlUi visual tests regressed (run scripts/visual_tests.sh)")
        else:
            harness.ok("RmlUi visual tests match their baseline")

    # --- Summary ----------------------------------------------------------------------------

    print("")
    if not harness.failures:
        print(f"Verification passed. Output in {out}")
        return 0

    print(f"Verification failed: {len(harness.failures)} problem(s).", file=sys.stderr)
    for message in harness.failures:
        print(f"  {message}", file=sys.stderr)
    print(f"Output in {out}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
