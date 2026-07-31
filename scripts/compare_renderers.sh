#!/usr/bin/env bash
#
# Holds the preview tool and the engine to each other, on where text actually landed.
#
# The preview's whole value is showing what Unreal will show, and the two have diverged before
# without anyone noticing. Not an image diff: the colour pipelines differ by design, so only where
# text landed is compared. Slow: it runs both renderers once per document.
#
#   ./scripts/compare_renderers.sh
#   ./scripts/compare_renderers.sh --filter settings

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/folio_common.sh"
source "$script_dir/vsenv.sh"

repo_root="$(cd "$script_dir/.." && pwd)"
project_root="$(cd "$repo_root/../.." && pwd)"

uproject="$(find "$project_root" -maxdepth 1 -name '*.uproject' -print -quit)"
[[ -n "$uproject" ]] || folio_die "no .uproject in $project_root"

engine="$(folio_resolve_engine "$project_root")"

for candidate in "$engine/Binaries/Win64/UnrealEditor.exe" "$engine/Binaries/Linux/UnrealEditor" \
	"$engine/Binaries/Mac/UnrealEditor"; do
	if [[ -x "$candidate" ]]; then
		editor="$candidate"
		break
	fi
done
[[ -n "${editor:-}" ]] || folio_die "no UnrealEditor under $engine/Binaries"

python_bin="$(folio_python)"
exec "$python_bin" "$repo_root/Tools/verify/compare_renderers.py" \
	--editor "$editor" --project "$uproject" 	--uipreview "$repo_root/bin/win64/uipreview.exe" "$@"
