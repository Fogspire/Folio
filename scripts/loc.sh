#!/usr/bin/env bash
#
# Gathers Folio's markup strings and runs Unreal's localisation pipeline.
#
# Folio's UI text is its own localization target, so what a translator receives is one Folio.po holding
# the screens and nothing else. Two steps, in this order, because the second cannot see the first's
# input:
#
#   1. gather_rml.py reads every data-loc key in the project's .rml documents and writes a generated
#      header full of NSLOCTEXT. Nothing includes it; it exists for the gatherer to read.
#   2. GatherText runs the ordinary pipeline, which picks that header up as source code.
#
# Running GatherText without step 1 produces a manifest with no markup strings in it and says nothing
# about the omission, which is the whole reason this is a script rather than a note in a readme.
#
# Bash rather than PowerShell so it runs on Linux as well as in git bash on Windows. Set UE_ROOT to
# skip engine discovery entirely.
#
#   ./scripts/loc.sh                        # gather, from the project directory
#   ./scripts/loc.sh --script-only          # regenerate the header alone, the fast check
#   ./scripts/loc.sh --gather-only          # skip the header, for a C++-only change
#   ./scripts/loc.sh --check-po <file>      # check what a translator sent back, and stop

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project="$(cd "$script_dir/../../.." && pwd)"
config="Config/Localization/Folio.ini"
generated="Source/Generated/FolioText.h"
excludes=(examples)
script_only=0
gather_only=0
check_po=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--project)   project="$(cd "$2" && pwd)"; shift 2 ;;
		--config)    config="$2"; shift 2 ;;
		--generated) generated="$2"; shift 2 ;;
		--exclude)   excludes+=("$2"); shift 2 ;;
		--script-only) script_only=1; shift ;;
		--gather-only) gather_only=1; shift ;;
		--check-po)  check_po="$2"; shift 2 ;;
		-h|--help)   sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done

uproject="$(find "$project" -maxdepth 1 -name '*.uproject' -print -quit)"
if [[ -z "$uproject" ]]; then
	echo "error: no .uproject in $project" >&2
	exit 2
fi

if [[ $gather_only -eq 0 ]]; then
	python_bin="${PYTHON:-}"
	if [[ -z "$python_bin" ]]; then
		for candidate in python3 python; do
			if command -v "$candidate" >/dev/null 2>&1; then python_bin="$candidate"; break; fi
		done
	fi
	if [[ -z "$python_bin" ]]; then
		echo "error: no python found; set PYTHON to one" >&2
		exit 2
	fi

	if [[ -n "$check_po" ]]; then
		# Markup travels inside the strings, so a translation can be a perfectly good string and
		# still be unreadable text. This is the gate for that, and it is what makes handing a
		# translator the raw tags safe.
		"$python_bin" "$script_dir/../Tools/locgather/gather_rml.py" --root "$project" --out /dev/null --check-po "$check_po"
		exit $?
	fi

	arguments=(--root "$project" --out "$project/$generated")
	for fragment in "${excludes[@]}"; do arguments+=(--exclude "$fragment"); done

	"$python_bin" "$script_dir/../Tools/locgather/gather_rml.py" "${arguments[@]}"
fi

if [[ $script_only -eq 1 ]]; then
	exit 0
fi

# The engine that built this project, rather than a guess at where one lives. Windows keeps the
# mapping in the registry, Linux in a config file, and UE_ROOT overrides both.
find_engine() {
	if [[ -n "${UE_ROOT:-}" ]]; then
		echo "$UE_ROOT"
		return
	fi

	local association
	association="$(sed -n 's/.*"EngineAssociation"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$uproject" | head -1)"
	if [[ -z "$association" ]]; then
		return
	fi

	# A relative association is a path from the project, which is how a source build beside it is named.
	if [[ "$association" != \{* ]]; then
		if [[ -d "$project/$association" ]]; then
			(cd "$project/$association" && pwd)
			return
		fi
		echo "$association"
		return
	fi

	if command -v reg >/dev/null 2>&1; then
		# Everything after REG_SZ, not the last field: an engine path routinely contains spaces.
		# reg also emits CRLF, and a trailing carriage return turns a valid path into a missing one.
		reg query 'HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds' 2>/dev/null \
			| grep -F "$association" \
			| sed -e 's/\r$//' -e 's/.*REG_SZ[[:space:]]*//' \
			| head -1
		return
	fi

	local install="${XDG_CONFIG_HOME:-$HOME/.config}/Epic/UnrealEngine/Install.ini"
	if [[ -f "$install" ]]; then
		sed -n "s|^${association}=||p" "$install" | head -1
	fi
}

engine="$(find_engine || true)"
if [[ -z "$engine" || ! -d "$engine" ]]; then
	echo "error: could not resolve the engine for $(basename "$uproject")." >&2
	echo "       set UE_ROOT to the engine directory, or pass --script-only and gather yourself." >&2
	exit 2
fi

if [[ -x "$engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ]]; then
	editor="$engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
elif [[ -x "$engine/Engine/Binaries/Linux/UnrealEditor-Cmd" ]]; then
	editor="$engine/Engine/Binaries/Linux/UnrealEditor-Cmd"
elif [[ -x "$engine/Engine/Binaries/Mac/UnrealEditor-Cmd" ]]; then
	editor="$engine/Engine/Binaries/Mac/UnrealEditor-Cmd"
else
	echo "error: no UnrealEditor-Cmd under $engine/Engine/Binaries" >&2
	exit 2
fi

"$editor" "$uproject" -run=GatherText -config="$config" -unattended -nopause -nosplash

echo "Localisation gathered. Folio.po and Folio.locres are under Content/Localization/Folio."
