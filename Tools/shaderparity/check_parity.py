#!/usr/bin/env python3
"""Fails if the HLSL and GLSL distance field shaders have drifted apart.

Two renderers resolve the same field: Unreal through Shaders/Private/FolioMsdf.ush, the preview tool
through the GLSL in Tools/uipreview/src/MsdfProgram.cpp. If they disagree, text looks different in the
editor than it did in the tool it was authored in, and the difference is small enough to argue about
and slow to track down.

A golden image comparison cannot catch this. The two colour pipelines differ - Slate linearises vertex
colour and applies display gamma, the tool's GL backend does neither - so the images never match pixel
for pixel even when the maths does.

So compare the source instead. Both sides are reduced to a canonical token stream: comments and
declarations dropped, whitespace removed, and a fixed dialect map applied (float3 to vec3, saturate to
clamp, Texture.Sample to texture, and so on). What survives is the maths, and the two must be identical.

Python rather than a shell script because this is text processing, and the same file then runs on Linux.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# HLSL spelling on the left, GLSL on the right. Whole words only. Identifiers are included because the
# two sides name the same quantity differently: one takes parameters, the other reads uniforms and
# interpolants. Order is significant, so this stays a list.
TOKEN_MAP = [
    ("float2", "vec2"),
    ("float3", "vec3"),
    ("float4", "vec4"),
    ("lerp", "mix"),
    ("frac", "fract"),
    ("rsqrt", "inversesqrt"),
    ("atan2", "atan"),
    ("Atlas", "_atlas"),
    ("UV", "fragTexCoord"),
    ("UnitRange", "_unitRange"),
    ("Threshold", "_threshold"),
    ("Mode", "_mode"),
    ("Offset", "_offset"),
    ("Softness", "_softness"),
    ("Sharpen", "_sharpen"),
    ("Colour", "fragColor"),
    ("Fill", "_fill"),
    ("FillStrength", "_fillStrength"),
]


def remove_host_blocks(text: str) -> str:
    """A block the two renderers are expected to do differently.

    It is the host supplying an input rather than the field maths itself. Marked in the source so the
    exception is visible where it applies rather than only here.
    """
    return re.sub(r"//\s*FOLIO_MSDF_HOST_BEGIN.*?//\s*FOLIO_MSDF_HOST_END", "", text, flags=re.S)


def remove_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\r\n]*", "", text)


def remove_declarations(text: str) -> str:
    """Preprocessor lines, and the plumbing declarations that exist on one side only."""
    kept = []
    for line in re.split(r"\r?\n", text):
        if re.match(r"^\s*#", line):
            continue
        if re.match(r"^\s*(uniform|in|out|layout)\b", line):
            continue
        kept.append(line)
    return "\n".join(kept)


def convert_dialect(text: str) -> str:
    for hlsl, glsl in TOKEN_MAP:
        text = re.sub(rf"\b{re.escape(hlsl)}\b", glsl, text)
    return text


def convert_clamp_to_saturate(text: str) -> str:
    """Rewrites clamp(EXPR,0.0,1.0) to saturate(EXPR).

    A regex cannot do this: EXPR contains its own parentheses, so the closing one has to be found by
    counting.
    """
    index = 0
    while True:
        index = text.find("clamp(", index)
        if index < 0:
            return text

        open_at = index + len("clamp(")
        depth = 1
        end = -1
        for position in range(open_at, len(text)):
            if text[position] == "(":
                depth += 1
            elif text[position] == ")":
                depth -= 1
                if depth == 0:
                    end = position
                    break

        if end < 0:
            return text

        inner = text[open_at:end]
        match = re.match(r"^(.*),0\.0,1\.0$", inner)
        if match:
            text = text[:index] + "saturate(" + match.group(1) + ")" + text[end + 1:]
            index += len("saturate(")
        else:
            index = open_at


def canonical(text: str) -> str:
    text = remove_host_blocks(text)
    text = remove_comments(text)
    text = remove_declarations(text)
    text = convert_dialect(text)
    text = re.sub(r"\bconst\b", "", text)

    # Whitespace carries no meaning here, and the two files brace differently.
    text = re.sub(r"\s+", "", text)

    # The entry points differ by construction: one is a callable function, the other a main().
    text = re.sub(r"vec4FolioMsdfShade\([^)]*\)", "", text)
    text = text.replace("voidmain()", "")

    # A GL fragment shader assigns its output where HLSL returns it.
    text = re.sub(r"\bfinalColor=", "return", text)

    # Texture.Sample(sampler, uv) against texture(sampler, uv).
    text = re.sub(r"(\w+)\.Sample\(\w+,", r"texture(\1,", text)

    text = convert_clamp_to_saturate(text)

    # HLSL suffixes float literals, GLSL cannot.
    text = re.sub(r"(?<=\d)f(?![A-Za-z0-9_])", "", text)

    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", help="the Folio repository root (default: two levels above this file)")
    parser.add_argument("--show-canonical", action="store_true",
                        help="print both canonical forms whether or not they match, for diagnosing a failure")
    args = parser.parse_args()

    repo = pathlib.Path(args.repo).resolve() if args.repo else pathlib.Path(__file__).resolve().parents[2]
    hlsl_path = repo / "Shaders/Private/FolioMsdf.ush"
    glsl_path = repo / "Tools/uipreview/src/MsdfProgram.cpp"

    for path in (hlsl_path, glsl_path):
        if not path.is_file():
            print(f"check_parity: missing {path}", file=sys.stderr)
            return 1

    hlsl = hlsl_path.read_text(encoding="utf-8")
    cpp = glsl_path.read_text(encoding="utf-8")

    between = re.search(r"FOLIO_MSDF_GLSL_BEGIN(.*?)FOLIO_MSDF_GLSL_END", cpp, flags=re.S)
    if not between:
        print(f"check_parity: no FOLIO_MSDF_GLSL_BEGIN/END markers in {glsl_path}", file=sys.stderr)
        return 1

    # Only the shader source inside the raw string literal, not the C++ around it.
    literal = re.search(r'R"\((.*?)\)"', between.group(1), flags=re.S)
    if not literal:
        print(f"check_parity: no raw string literal between the markers in {glsl_path}", file=sys.stderr)
        return 1

    hlsl_canonical = canonical(hlsl)
    glsl_canonical = canonical(literal.group(1))

    if args.show_canonical:
        print(f"hlsl: {hlsl_canonical}")
        print(f"glsl: {glsl_canonical}")

    if hlsl_canonical == glsl_canonical:
        print(f"Shader parity OK ({len(hlsl_canonical)} tokens).")
        return 0

    # Point at the first difference rather than dumping two long strings and leaving the reader to
    # find it.
    limit = min(len(hlsl_canonical), len(glsl_canonical))
    at = limit
    for index in range(limit):
        if hlsl_canonical[index] != glsl_canonical[index]:
            at = index
            break

    start = max(0, at - 40)

    def window(text: str) -> str:
        if start >= len(text):
            return "<end of shader>"
        return text[start:start + 80]

    print("Shader parity FAILED. The two shaders resolve the field differently.", file=sys.stderr)
    print("", file=sys.stderr)
    print(f"  first difference at token {at}", file=sys.stderr)
    print(f"  ush : {window(hlsl_canonical)}", file=sys.stderr)
    print(f"  glsl: {window(glsl_canonical)}", file=sys.stderr)
    print("", file=sys.stderr)
    print("  Shaders/Private/FolioMsdf.ush is the source of truth. Update the GLSL between the",
          file=sys.stderr)
    print("  FOLIO_MSDF_GLSL markers in Tools/uipreview/src/MsdfProgram.cpp to match.", file=sys.stderr)
    print("  Re-run with --show-canonical to see both in full.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
