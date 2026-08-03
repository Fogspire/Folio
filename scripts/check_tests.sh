#!/usr/bin/env bash
# Runs Folio's engine-side automation tests headlessly.
#
# These are the checks that need a real UObject and a real reflection system, so they cannot live in
# uipreview with the rest of the harness. The view model bridge is the whole of it today: the
# subscription path has no other coverage, and a regression there is silent - a document simply stops
# updating, which reads as a game bug rather than a plugin one.
#
#   ./scripts/check_tests.sh                       # discovers the engine from the .uproject
#   ./scripts/check_tests.sh --filter Folio.ViewModel
#   UE_ROOT=/path/to/UnrealEngine ./scripts/check_tests.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=folio_common.sh
. "$here/scripts/folio_common.sh"

filter="Folio."
project="${FOLIO_UPROJECT:-}"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--filter) filter="$2"; shift 2 ;;
		--project) project="$2"; shift 2 ;;
		-h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "error: unknown argument '$1'" >&2; exit 2 ;;
	esac
done

# The plugin sits under <project>/Plugins/Folio, so the project is two levels up. Overridable, since
# the plugin can be dropped into any project.
if [[ -z "$project" ]]; then
	project="$(ls "$here/../../"*.uproject 2>/dev/null | head -1 || true)"
fi

if [[ ! -f "$project" ]]; then
	echo "error: no .uproject found. Pass --project or set FOLIO_UPROJECT." >&2
	exit 1
fi

find_engine() {
	if [[ -n "${UE_ROOT:-}" ]]; then
		echo "$UE_ROOT"
		return
	fi

	# The association is a registered build's GUID on Windows, resolved through the registry the
	# launcher writes. A source build has no version number to guess from.
	local guid
	guid="$("$(folio_python)" -c "import json,sys; print(json.load(open(sys.argv[1])).get('EngineAssociation',''))" "$project")"

	if [[ -z "$guid" ]]; then
		echo "error: the project has no EngineAssociation. Set UE_ROOT." >&2
		return 1
	fi

	local root
	root="$(powershell.exe -NoProfile -Command \
		"(Get-ItemProperty 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds')."'"'"$guid"'"' 2>/dev/null | tr -d '\r')"

	if [[ -z "$root" ]]; then
		echo "error: engine '$guid' is not registered. Set UE_ROOT." >&2
		return 1
	fi

	echo "$root"
}

engine="$(find_engine)"
editor="$engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"

if [[ ! -f "$editor" ]]; then
	echo "error: no editor at $editor" >&2
	exit 1
fi

echo "running '$filter' against $project"

logs="$(dirname "$project")/Saved/Logs"
name="$(basename "$project" .uproject)"

# -nullrhi because none of these draw. TestExit is what makes the exit code mean something: without
# it the editor quits successfully whatever the tests did.
"$editor" "$project" \
	-unattended -nopause -nosplash -nullrhi \
	-ExecCmds="Automation RunTests $filter; Quit" \
	-TestExit="Automation Test Queue Empty" \
	-log > /dev/null 2>&1 || status=$?

# The newest non-backup log, not "<Project>.log": an editor already holding that name sends this run
# to <Project>_2.log, and reading the wrong file reports nothing at all while still exiting 0.
#
# Globbed with a nullglob rather than passed to ls: an unmatched pattern reaches ls as a literal, and
# its exit code then takes the whole script down through pipefail before anything is reported.
shopt -s nullglob
candidates=("$logs/$name.log" "$logs/${name}_"[0-9]*.log)
shopt -u nullglob

log=""
if [[ ${#candidates[@]} -gt 0 ]]; then
	log="$(ls -t "${candidates[@]}" 2>/dev/null | grep -v -- '-backup-' | head -1 || true)"
fi

if [[ ! -f "$log" ]]; then
	echo "error: no log found under $logs" >&2
	exit 1
fi

results="$(grep -E "LogAutomationController: (Error|Display: Test Completed)" "$log" | tail -40)"

if [[ -n "${status:-}" ]]; then
	echo "tests failed:" >&2
	echo "$results" >&2
	exit 1
fi

if [[ -z "$results" ]]; then
	echo "error: the run produced no test results. Read $log" >&2
	exit 1
fi

echo "$results"
