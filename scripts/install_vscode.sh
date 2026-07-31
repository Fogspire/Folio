#!/usr/bin/env bash
# Links the VSCode extension into place, replacing a copy if one is there.
#
# A copy goes stale the moment the plugin updates, and the symptom is a fix that appears not to have
# worked rather than an error - so this refuses to leave one behind.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source="$here/Tools/vscode-folio"

case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*)
		target="$(cygpath "$USERPROFILE")/.vscode/extensions/folio-preview"
		;;
	*)
		target="$HOME/.vscode/extensions/folio-preview"
		;;
esac

if [ -e "$target" ] || [ -L "$target" ]; then
	echo "removing $target"
	rm -rf "$target"
fi

mkdir -p "$(dirname "$target")"

case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*)
		# A junction rather than a symlink: it needs no elevation and no developer mode.
		cmd //c mklink //J "$(cygpath -w "$target")" "$(cygpath -w "$source")" >/dev/null
		;;
	*)
		ln -s "$source" "$target"
		;;
esac

echo "linked $target -> $source"
echo "now reload the VSCode window: ctrl+shift+p, 'Developer: Reload Window'"
