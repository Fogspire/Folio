#!/usr/bin/env bash
#
# Fails if the HLSL and GLSL distance field shaders have drifted apart.
#
# The work is in Tools/shaderparity/check_parity.py: it is text processing, which python does well and
# a shell does badly, and the same file runs on Linux.
#
#   ./scripts/check_shader_parity.sh
#   ./scripts/check_shader_parity.sh --show-canonical

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/folio_common.sh"

python_bin="$(folio_python)"
exec "$python_bin" "$script_dir/../Tools/shaderparity/check_parity.py" --repo "$(cd "$script_dir/.." && pwd)" "$@"
