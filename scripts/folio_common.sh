#!/usr/bin/env bash
#
# Shared helpers. Sourced, never run.

folio_repo_root() {
	cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

folio_python() {
	if [[ -n "${PYTHON:-}" ]]; then
		echo "$PYTHON"
		return
	fi
	local candidate
	for candidate in python3 python; do
		if command -v "$candidate" >/dev/null 2>&1; then
			echo "$candidate"
			return
		fi
	done
	echo "error: no python found; set PYTHON to one" >&2
	return 1
}
