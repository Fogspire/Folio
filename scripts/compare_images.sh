#!/usr/bin/env bash
# Compares two PNGs numerically. Run --help for what the gates mean.
#
# Alpha and colour are reported separately on purpose: RGB will never match across the two
# renderers, and coverage always must.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=folio_common.sh
. "$here/scripts/folio_common.sh"

exec "$(folio_python)" "$here/Tools/verify/compare_images.py" "$@"
