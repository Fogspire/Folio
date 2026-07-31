#!/usr/bin/env bash
#
# Checks that a world-space document still responds to the properties it is drawn with.
#
# Separate from verify.sh because it needs a real editor: the recording path only exists on the Unreal
# side, and uipreview does not share it - which is exactly how a property came to be applied on one
# renderer and not the other without anything noticing. Slow: it runs the editor once per document.
#
#   ./scripts/check_world_path.sh
#   ./scripts/check_world_path.sh --filter crt

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
exec "$python_bin" "$repo_root/Tools/verify/check_world_path.py" \
	--editor "$editor" --project "$uproject" "$@"
