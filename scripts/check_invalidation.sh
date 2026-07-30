#!/usr/bin/env bash
#
# Checks that idle documents idle and busy documents do not.
#
# Separate from verify.sh because it needs a real editor: what it measures only exists on the Unreal
# side, so uipreview would prove nothing about it. Slow by nature - it runs the editor once per case -
# so it is the check to run after touching the invalidation path rather than after every edit.
#
#   ./scripts/check_invalidation.sh
#   ./scripts/check_invalidation.sh --filter caret

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
exec "$python_bin" "$repo_root/Tools/verify/check_invalidation.py" \
	--editor "$editor" --project "$uproject" "$@"
