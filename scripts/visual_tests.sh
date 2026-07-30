#!/usr/bin/env bash
#
# Wrapper. The work is in Tools/verify/visual_tests.py: the harness reads JSON reports and compares them, which
# python does well and a shell does badly, and the same file runs on Linux.
#
# Run --help for the arguments.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/folio_common.sh"

python_bin="$(folio_python)"
exec "$python_bin" "$script_dir/../Tools/verify/visual_tests.py" --repo "$(cd "$script_dir/.." && pwd)" "$@"
