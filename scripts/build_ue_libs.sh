#!/usr/bin/env bash
#
# Builds RmlUi and msdfgen as static libraries for the Unreal ThirdParty modules.
#
# Separate from build.sh because the engine build wants different settings to the preview tool: the
# engine's FreeType rather than our own, the DLL runtime, no RTTI, and output staged into
# Source/ThirdParty rather than bin/.
#
# Run this after changing a pin or a patch. The staged libraries are committed, so a normal Unreal
# build needs neither CMake nor this script.
#
#   ./scripts/build_ue_libs.sh
#   ./scripts/build_ue_libs.sh --engine-dir "Z:/Epic Games/Src/5.8/Engine"
#   ./scripts/build_ue_libs.sh --clean

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/vsenv.sh"

repo_root="$(cd "$script_dir/.." && pwd)"
source_dir="$repo_root/cmake/ue"
stage_dir="$repo_root/Source/ThirdParty/RmlUiLibrary"

config=RelWithDebInfo
toolset=latest
engine_dir=""
clean=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		--config)
			config="$2"
			case "$config" in
				Debug|RelWithDebInfo|Release) ;;
				*) folio_die "--config must be Debug, RelWithDebInfo or Release" ;;
			esac
			shift 2 ;;
		--toolset)    toolset="$2"; shift 2 ;;
		--engine-dir) engine_dir="$2"; shift 2 ;;
		--clean)      clean=1; shift ;;
		-h|--help)    sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) folio_die "unknown argument: $1" ;;
	esac
done

build_dir="$repo_root/build/ue-$config"

if [[ -z "$engine_dir" ]]; then
	# The plugin sits at <project>/Plugins/Folio.
	engine_dir="$(folio_resolve_engine "$(cd "$repo_root/../.." && pwd)")"
fi
echo "Engine: $engine_dir"

folio_import_msvc "$toolset"

if [[ $clean -eq 1 && -d "$build_dir" ]]; then
	echo "Removing $build_dir"
	rm -rf "$build_dir"
fi

echo "Configuring ($config)..."
"$FOLIO_CMAKE" -G Ninja \
	"-DCMAKE_MAKE_PROGRAM=$(cygpath -m "$FOLIO_NINJA")" \
	"-DCMAKE_BUILD_TYPE=$config" \
	"-DFOLIO_ENGINE_DIR=$(cygpath -m "$engine_dir")" \
	-S "$(cygpath -m "$source_dir")" \
	-B "$(cygpath -m "$build_dir")" \
	|| folio_die "CMake configure failed."

echo "Building ($config)..."
"$FOLIO_CMAKE" --build "$(cygpath -m "$build_dir")" --parallel || folio_die "build failed."

echo "Done. Staged into $stage_dir"
