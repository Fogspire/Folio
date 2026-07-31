#!/usr/bin/env bash
#
# Generates the view model base class a document's bindings describe.
#
# Names come from the markup, types from the mock, so the two are a cross-check: a bound name with no
# mock entry has no type and is reported, and so is a mock entry nothing binds.
#
# It writes a *base* class. Subclass it and put the logic there; this file is rewritten whenever the
# document's bindings change.
#
#   ./scripts/viewmodel.sh ui/screens/settings.rml --out Source/MyGame/Public/SettingsViewModelBase.h
#   ./scripts/viewmodel.sh ui/screens/hud.rml --strict

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/folio_common.sh"

python_bin="$(folio_python)"
exec "$python_bin" "$script_dir/../Tools/codegen/viewmodel.py" "$@"
