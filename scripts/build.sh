#!/usr/bin/env bash
#
# Builds the Folio native tools.
#
# Locates the CMake and Ninja that ship inside Visual Studio, imports the MSVC environment, then
# configures and builds. Nothing has to be installed globally and neither cmake nor ninja has to be on
# PATH.
#
#   ./scripts/build.sh
#   ./scripts/build.sh --config Debug
#   ./scripts/build.sh --clean
#   ./scripts/build.sh --asan
#   ./scripts/build.sh --target uipreview

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/vsenv.sh"

repo_root="$(cd "$script_dir/.." && pwd)"

config=RelWithDebInfo
toolset=latest
target=""
clean=0
skip_parity=0
asan=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		--config)
			config="$2"
			case "$config" in
				Debug|RelWithDebInfo|Release) ;;
				*) folio_die "--config must be Debug, RelWithDebInfo or Release" ;;
			esac
			shift 2 ;;
		--toolset) toolset="$2"; shift 2 ;;
		--target)  target="$2"; shift 2 ;;
		--clean)   clean=1; shift ;;
		# Escape hatch for editing one side of the distance field shader before the other.
		--skip-shader-parity) skip_parity=1; shift ;;
		# Build with AddressSanitizer, into its own directory. Slower, and the only practical way to
		# get a real answer out of the intermittent crashes this codebase can produce: lifetime bugs
		# here usually read freed memory that is still intact, so they survive for weeks and then fail
		# on someone else's machine.
		--asan)    asan=1; shift ;;
		-h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) folio_die "unknown argument: $1" ;;
	esac
done

if [[ $asan -eq 1 ]]; then
	build_dir="$repo_root/build/win64-asan"
else
	build_dir="$repo_root/build/win64-$config"
fi

folio_import_msvc "$toolset"

# Before the compile, because it takes a moment and catches a class of bug that survives every later
# check: the tool and the engine resolving the same field differently.
if [[ $skip_parity -eq 0 ]]; then
	if ! "$script_dir/check_shader_parity.sh"; then
		folio_die "shader parity check failed. Pass --skip-shader-parity to build anyway."
	fi
fi

if [[ $clean -eq 1 && -d "$build_dir" ]]; then
	echo "Removing $build_dir"
	rm -rf "$build_dir"
fi

folio_discard_moved_build "$build_dir"

configure_args=(
	-G Ninja
	"-DCMAKE_MAKE_PROGRAM=$(cygpath -m "$FOLIO_NINJA")"
	"-DCMAKE_BUILD_TYPE=$config"
	-S "$(cygpath -m "$repo_root")"
	-B "$(cygpath -m "$build_dir")"
)

if [[ $asan -eq 1 ]]; then
	# /Zi and a non-incremental link, or the report is addresses with no symbols.
	configure_args+=(
		"-DCMAKE_C_FLAGS=/fsanitize=address /Zi"
		"-DCMAKE_CXX_FLAGS=/fsanitize=address /Zi"
		"-DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO /DEBUG"
	)
fi

if [[ $asan -eq 1 ]]; then
	echo "Configuring ($config, asan)..."
else
	echo "Configuring ($config)..."
fi
"$FOLIO_CMAKE" "${configure_args[@]}" || folio_die "CMake configure failed."

echo "Building ($config)..."
build_args=(--build "$(cygpath -m "$build_dir")" --parallel)
[[ -n "$target" ]] && build_args+=(--target "$target")
"$FOLIO_CMAKE" "${build_args[@]}" || folio_die "build failed."

echo "Done. Output in $repo_root/bin/win64"

# Every configuration writes its executables to the same place, so whichever built last is the one
# that runs. An instrumented binary left there quietly makes everything slower.
if [[ $asan -eq 1 ]]; then
	echo "Instrumented binaries are in bin/win64. Re-run without --asan to replace them."
	echo "They need the MSVC environment on PATH for the sanitizer runtime."
fi
