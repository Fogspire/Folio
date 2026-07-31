#!/usr/bin/env bash
# Every document under a directory, checked for the things a screen reader cannot fix afterwards.
#
# Exits non-zero if anything is found, so it can gate a build. Pass --report <dir> to also keep the
# JSON per document.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=folio_common.sh
. "$here/scripts/folio_common.sh"

root="${1:-$here/..}"
report_dir=""

if [ "${2:-}" = "--report" ]; then
	report_dir="${3:-}"
	mkdir -p "$report_dir"
fi

uipreview="$here/bin/win64/uipreview.exe"
[ -x "$uipreview" ] || uipreview="$here/bin/linux/uipreview"

if [ ! -x "$uipreview" ]; then
	echo "uipreview is not built. Run ./scripts/build.sh" >&2
	exit 2
fi

failed=0
checked=0

while IFS= read -r document; do
	checked=$((checked + 1))
	args=("$document" "--root" "$root" "--a11y")

	if [ -n "$report_dir" ]; then
		args+=("--a11y-report" "$report_dir/$(basename "${document%.rml}").json")
	fi

	output="$("$uipreview" "${args[@]}" 2>&1 | grep "A11Y" || true)"

	if [ -n "$output" ]; then
		echo "$document"
		echo "$output" | sed 's/^/  /'
	fi

	# Only warnings fail. I-A11Y-NOBACKDROP is a statement about what could not be measured, which is
	# the normal and correct state for anything drawn over the world.
	if echo "$output" | grep -q "W-A11Y"; then
		failed=$((failed + 1))
	fi
done < <(find "$root" -name "*.rml" -not -path "*/build/*" -not -path "*/node_modules/*" | sort)

echo
echo "$checked document(s), $failed with findings"
[ "$failed" -eq 0 ]
