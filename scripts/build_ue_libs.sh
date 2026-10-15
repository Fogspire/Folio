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
#
# On a Mac it needs Xcode, CMake and Ninja, and builds a universal library for both architectures. On
# Linux it needs CMake, Ninja and the engine's own toolchain, found through LINUX_MULTIARCH_ROOT or
# under the engine's Extras, because the engine links libc++ and a library built against anything else
# does not link with it.
#
# A platform other than the host needs a CMake toolchain file, and stages under its own lib/
# directory. Anything else the toolchain needs is passed straight through as -D:
#
#   ./scripts/build_ue_libs.sh --platform <Name> --toolchain <file.cmake> -DFOLIO_FREETYPE_LIB=<path>

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/vsenv.sh"

repo_root="$(cd "$script_dir/.." && pwd)"
source_dir="$repo_root/cmake/ue"
stage_dir="$repo_root/Source/ThirdParty"

config=RelWithDebInfo
toolset=latest
engine_dir=""
clean=0
case "$(uname -s)" in
	Darwin) platform=Mac ;;
	Linux) platform=Linux ;;
	*) platform=Win64 ;;
esac
toolchain=""
extra_defines=()

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
		--platform)   platform="$2"; shift 2 ;;
		--toolchain)  toolchain="$2"; shift 2 ;;
		-D*)          extra_defines+=("$1"); shift ;;
		--clean)      clean=1; shift ;;
		-h|--help)    sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) folio_die "unknown argument: $1" ;;
	esac
done

build_dir="$repo_root/build/ue-$platform-$config"

if [[ -z "$engine_dir" ]]; then
	# The plugin sits at <project>/Plugins/Folio.
	engine_dir="$(folio_resolve_engine "$(cd "$repo_root/../.." && pwd)")"
fi
echo "Engine: $engine_dir"

host_defines=()
if folio_is_windows; then
	# Visual Studio is where cmake and ninja come from even when it is not the compiler. A toolchain
	# file names its own, so the MSVC search paths are dropped or its headers win over the sysroot's.
	folio_import_msvc "$toolset"
	if [[ -n "$toolchain" ]]; then
		unset INCLUDE LIB LIBPATH
	fi
else
	folio_import_unix_tools
	if [[ -z "$toolchain" && "$platform" == Mac ]]; then
		host_defines+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0")
	elif [[ -z "$toolchain" && "$platform" == Linux ]]; then
		triple=x86_64-unknown-linux-gnu
		[[ "$(uname -m)" == aarch64 ]] && triple=aarch64-unknown-linux-gnueabi
		sdk="${LINUX_MULTIARCH_ROOT:-}"
		if [[ -z "$sdk" ]]; then
			sdk="$(ls -d "$engine_dir/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64"/*/ 2>/dev/null | sort -V | tail -1)"
		fi
		sdk="${sdk%/}/$triple"
		[[ -x "$sdk/bin/clang++" ]] || folio_die "the engine's Linux toolchain was not found at '$sdk'. Run the engine's Setup.sh, or set LINUX_MULTIARCH_ROOT."
		export CC="$sdk/bin/clang" CXX="$sdk/bin/clang++"
		export CFLAGS="--sysroot=$sdk" CXXFLAGS="--sysroot=$sdk -stdlib=libc++"
		echo "Toolchain: $sdk"
	fi
fi

if [[ $clean -eq 1 && -d "$build_dir" ]]; then
	echo "Removing $build_dir"
	rm -rf "$build_dir"
fi

folio_discard_moved_build "$build_dir"

configure_args=(
	"-DCMAKE_MAKE_PROGRAM=$(folio_tool_path "$FOLIO_NINJA")"
	"-DCMAKE_BUILD_TYPE=$config"
	"-DFOLIO_ENGINE_DIR=$(folio_tool_path "$engine_dir")"
	"-DFOLIO_UE_PLATFORM=$platform"
)
if [[ -n "$toolchain" ]]; then
	configure_args+=("-DCMAKE_TOOLCHAIN_FILE=$(folio_tool_path "$toolchain")")
fi
configure_args+=("${host_defines[@]+"${host_defines[@]}"}")
configure_args+=("${extra_defines[@]+"${extra_defines[@]}"}")

echo "Configuring ($platform, $config)..."
"$FOLIO_CMAKE" -G Ninja \
	"${configure_args[@]}" \
	-S "$(folio_tool_path "$source_dir")" \
	-B "$(folio_tool_path "$build_dir")" \
	|| folio_die "CMake configure failed."

echo "Building ($platform, $config)..."
"$FOLIO_CMAKE" --build "$(folio_tool_path "$build_dir")" --parallel || folio_die "build failed."

for define in ${extra_defines[@]+"${extra_defines[@]}"}; do
	case "$define" in
		-DFOLIO_STAGE_ROOT=*) stage_dir="${define#-DFOLIO_STAGE_ROOT=}" ;;
	esac
done

echo "Done. Staged into $stage_dir"
