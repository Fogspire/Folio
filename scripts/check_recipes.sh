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

for recipe in "$here"/rcss/recipes/*.rml; do
	[ -e "$recipe" ] || continue
	checked=$((checked + 1))

	# Rooted at the plugin, which is where a recipe's own mocks live. A game copying one supplies its
	# own under its UI/mock.
	if output="$("$uipreview" "$recipe" --root "$here" --strict --a11y 2>&1)"; then
		echo "ok   $(basename "$recipe")"
	else
		echo "FAIL $(basename "$recipe")"
		echo "$output" | grep -E "warning|error" | sed 's/^/  /'
		failed=$((failed + 1))
	fi
done

echo
echo "$checked recipe(s), $failed failing"
[ "$failed" -eq 0 ]
