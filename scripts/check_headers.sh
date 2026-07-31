#!/usr/bin/env bash
# Fails if a module's public header includes one of its private headers.
#
# Invisible to the build, because a module's own sources have Private on their include path. It only
# breaks for whoever consumes the plugin.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=folio_common.sh
. "$here/scripts/folio_common.sh"

cd "$here"
"$(folio_python)" Tools/verify/check_headers.py "$@"
