#!/usr/bin/env bash
# Every shipped recipe, under --strict --a11y.
#
# A recipe is what someone copies to start a screen, so it is the one document set where a finding is
# inherited by everyone who uses it rather than affecting one game. Nothing checked these until one
# drifted: nameplate.rml bound a model with no mock beside it, so it failed --strict while the docs
# claimed all four were clean.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=folio_common.sh
. "$here/scripts/folio_common.sh"

uipreview="$here/bin/win64/uipreview.exe"
[ -x "$uipreview" ] || uipreview="$here/bin/linux/uipreview"

if [ ! -x "$uipreview" ]; then
	echo "uipreview is not built. Run ./scripts/build.sh" >&2
	exit 2
fi

failed=0
checked=0

# Each pass is a setting a player can turn on, and a recipe has to survive all of them: a starting
# point that only works at the defaults teaches the defaults.
#
#   default          as authored
#   high contrast    the shipped theme, held to 7:1 rather than 4.5:1
#   text scale       150%, where a fixed-height row stops fitting its own text
#   deuteranopia     the commonest colour vision difference, which red-on-green fails
run_pass() {
	local recipe="$1"
	local label="$2"
	shift 2

	# Rooted at the plugin, which is where a recipe's own mocks live. A game copying one supplies its
	# own under its UI/mock.
	if output="$("$uipreview" "$recipe" --root "$here" --strict --a11y "$@" 2>&1)"; then
		return 0
	fi

	echo "FAIL $(basename "$recipe") [$label]"
	echo "$output" | grep -E "warning|error" | sed 's/^/  /'
	return 1
}

for recipe in "$here"/rcss/recipes/*.rml; do
	[ -e "$recipe" ] || continue
	checked=$((checked + 1))

	recipe_failed=0
	run_pass "$recipe" default || recipe_failed=1
	run_pass "$recipe" high-contrast --theme high-contrast --a11y-contrast 7 || recipe_failed=1
	run_pass "$recipe" text-scale --text-scale 1.5 || recipe_failed=1
	run_pass "$recipe" deuteranopia --simulate deuteranopia || recipe_failed=1

	if [ "$recipe_failed" -eq 0 ]; then
		echo "ok   $(basename "$recipe")"
	else
		failed=$((failed + 1))
	fi
done

echo
echo "$checked recipe(s), $failed failing"
[ "$failed" -eq 0 ]
