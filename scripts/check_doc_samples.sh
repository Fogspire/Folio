#!/usr/bin/env bash
# Fails if the checked-in doc samples are out of date with docs/.
#
# The compiling is the build's job - the generated file is a normal source file in FolioEditor, so a
# sample naming an API that has been renamed fails the editor build like any other call site. This
# only catches the other half: a sample edited without regenerating, which would leave the build
# checking the previous text.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=folio_common.sh
. "$here/scripts/folio_common.sh"

cd "$here"
"$(folio_python)" Tools/verify/extract_doc_samples.py --check
