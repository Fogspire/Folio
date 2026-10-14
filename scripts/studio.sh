#!/usr/bin/env bash
#
# Builds and runs Folio Studio, the editor for Folio documents.
#
# Needs Node 20 or newer and nothing else: the renderer is committed as WebAssembly, so there is no
# compiler to install. Runs the same on Windows (Git Bash), macOS and Linux.
#
#   ./scripts/studio.sh                  build, then run
#   ./scripts/studio.sh run [project]    the same, opening a project folder
#   ./scripts/studio.sh build            build only
#   ./scripts/studio.sh test             unit tests, then the scripted tests against the running app
#   ./scripts/studio.sh package          a runnable copy for this machine, in Tools/studio/release
#   ./scripts/studio.sh engine           rebuild the WebAssembly renderer. Only needed after changing
#                                        C++ under Tools/uipreview, Tools/studio/engine or FolioCore.
#                                        Fetches Emscripten into build/emsdk on first use.
#   ./scripts/studio.sh parity           diff the WebAssembly renderer against native uipreview

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/folio_common.sh"
source "$script_dir/vsenv.sh"

repo_root="$(cd "$script_dir/.." && pwd)"
studio="$repo_root/Tools/studio"
emsdk_version=4.0.10

command -v node >/dev/null 2>&1 || folio_die "Node is not installed. Folio Studio needs Node 20 or newer: https://nodejs.org"

# A path a native Windows program can read. Everywhere else it already is one.
native_path() {
	if folio_is_windows; then cygpath -m "$1"; else echo "$1"; fi
}

# The plugin's version is the one version there is. Studio's package takes it rather than keeping its
# own, so the number in the title bar and the one compared against release tags cannot disagree.
sync_version() {
	(cd "$studio" && node -e '
		const fs = require("fs");
		const plugin = JSON.parse(fs.readFileSync("../../Folio.uplugin", "utf8"));
		const manifest = JSON.parse(fs.readFileSync("package.json", "utf8"));
		if (plugin.VersionName && manifest.version !== plugin.VersionName) {
			manifest.version = plugin.VersionName;
			fs.writeFileSync("package.json", JSON.stringify(manifest, null, "\t") + "\n");
			console.log("Folio Studio is now version " + plugin.VersionName);
		}')
}

install_dependencies() {
	# Reinstalled when the lockfile has moved on from what is installed, which is what a pull does.
	if [[ ! -d "$studio/node_modules" || "$studio/package-lock.json" -nt "$studio/node_modules/.package-lock.json" ]]; then
		echo "Installing dependencies..."
		(cd "$studio" && npm ci --no-audit --no-fund) || folio_die "npm ci failed."
	fi
}

build() {
	sync_version
	install_dependencies
	[[ -f "$studio/engine/dist/folio_engine.wasm" ]] || folio_die "the renderer is missing from Tools/studio/engine/dist. Run ./scripts/studio.sh engine"
	echo "Building..."
	(cd "$studio" && npm run --silent build) || folio_die "build failed."
}

electron() {
	(cd "$studio" && ./node_modules/.bin/electron . "$@")
}

# CMake and Ninja from PATH where they are there, and from Visual Studio where they are not.
find_build_tools() {
	FOLIO_CMAKE="$(command -v cmake || true)"
	FOLIO_NINJA="$(command -v ninja || true)"
	if [[ -n "$FOLIO_CMAKE" && -n "$FOLIO_NINJA" ]]; then
		return
	fi
	folio_is_windows || folio_die "cmake and ninja are needed to build the renderer. Install both and try again."

	local vswhere install
	vswhere="$(folio_program_files_x86)/Microsoft Visual Studio/Installer/vswhere.exe"
	[[ -f "$vswhere" ]] || folio_die "cmake and ninja were not found, and neither was Visual Studio, which bundles them."
	install="$(cygpath -u "$("$vswhere" -latest -products '*' -property installationPath | tr -d '\r')")"
	FOLIO_CMAKE="$install/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
	FOLIO_NINJA="$install/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
	[[ -f "$FOLIO_CMAKE" && -f "$FOLIO_NINJA" ]] || folio_die "Visual Studio is installed without its 'C++ CMake tools for Windows' component."
}

build_engine() {
	local emsdk="$repo_root/build/emsdk"
	find_build_tools

	if [[ ! -d "$emsdk" ]]; then
		echo "Fetching Emscripten $emsdk_version..."
		git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$emsdk" || folio_die "could not fetch emsdk."
	fi

	local tool="./emsdk"
	folio_is_windows && tool="./emsdk.bat"
	if [[ ! -f "$emsdk/upstream/emscripten/emcc" && ! -f "$emsdk/upstream/emscripten/emcc.bat" ]] \
		|| ! grep -q "$emsdk_version" "$emsdk/upstream/emscripten/emscripten-version.txt" 2>/dev/null; then
		(cd "$emsdk" && "$tool" install "$emsdk_version" && "$tool" activate "$emsdk_version") || folio_die "could not install Emscripten."
	fi

	# shellcheck disable=SC1091
	source "$emsdk/emsdk_env.sh" >/dev/null 2>&1
	PATH="$(dirname "$FOLIO_CMAKE"):$(dirname "$FOLIO_NINJA"):$PATH"

	local build_dir="$repo_root/build/wasm-Release"
	echo "Configuring..."
	"$FOLIO_CMAKE" -G Ninja \
		"-DCMAKE_MAKE_PROGRAM=$(native_path "$FOLIO_NINJA")" \
		"-DCMAKE_TOOLCHAIN_FILE=$(native_path "$emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")" \
		-DCMAKE_BUILD_TYPE=Release \
		-S "$(native_path "$repo_root")" -B "$(native_path "$build_dir")" >/dev/null || folio_die "CMake configure failed."

	echo "Building the renderer..."
	"$FOLIO_CMAKE" --build "$(native_path "$build_dir")" --parallel || folio_die "build failed."

	"$(folio_python)" "$studio/engine/source_hash.py" --write
	echo "Done. Commit Tools/studio/engine/dist."
}

# Renders every recipe and example through both renderers and compares them. A difference of one
# level in a few dozen pixels is the two GPUs rounding differently; anything more is a real divergence.
parity() {
	local native="$repo_root/bin/win64/uipreview.exe"
	[[ -x "$native" ]] || native="$repo_root/bin/linux/uipreview"
	[[ -x "$native" ]] || folio_die "native uipreview is not built, so there is nothing to compare against. Run ./scripts/build.sh"
	[[ -f "$repo_root/bin/wasm/uipreview.js" ]] || folio_die "the WebAssembly uipreview is not built. Run ./scripts/studio.sh engine"
	install_dependencies

	local out="$repo_root/build/studio-parity"
	local python
	python="$(folio_python)"
	mkdir -p "$out/native" "$out/wasm"

	local -a documents=()
	local document
	for document in "$repo_root"/rcss/recipes/*.rml "$repo_root"/examples/*.rml; do
		documents+=("$(native_path "$document")")
	done

	(cd "$studio" && ./node_modules/.bin/electron parity/main.js "$(native_path "$repo_root")" "$(native_path "$out/wasm")" 1 "${documents[@]}" 2>&1 | grep -E "^FAIL" || true)

	# WebGL cannot clamp a texture to a transparent border, so a blur that samples past the edge of
	# the screen reads the edge pixel where desktop GL reads nothing.
	local known_differences="screen_edge_baker"
	local failed=0 name report
	for document in "${documents[@]}"; do
		name="$(basename "$document" .rml)"
		"$native" "$document" --root "$(native_path "$repo_root")" --dp-ratio 1 --screenshot "$(native_path "$out/native/$name.png")" >/dev/null 2>&1 || true
		if report="$("$python" "$repo_root/Tools/verify/compare_images.py" "$out/native/$name.png" "$out/wasm/$name.png" --gate rgba --tolerance "${FOLIO_PARITY_TOLERANCE:-2}" --allow 0.05 2>&1)"; then
			echo "ok   $name"
		elif [[ "$report" == *"colour type"* ]]; then
			# A render with few enough colours is written with a palette, which the comparison does not read.
			echo "skip $name (palette image)"
		elif [[ " $known_differences " == *" $name "* ]]; then
			echo "know $name (a blur that reaches the edge of the screen: see docs/known_gaps.md)"
		else
			failed=1
			echo "FAIL $name"
			echo "$report" | sed 's/^/  /'
		fi
	done
	return $failed
}

run_tests() {
	build
	(cd "$studio" && npm run --silent test) || folio_die "unit tests failed."

	# Each scripted test edits files, so each gets a throwaway copy of what it works on.
	local scratch
	scratch="$(mktemp -d)"
	mkdir -p "$scratch/edit/UI/screens" "$scratch/shell/UI/screens" "$scratch/data"
	cp "$repo_root/rcss/recipes/main_menu.rml" "$scratch/edit/UI/screens/test.rml"
	cp "$repo_root/rcss/recipes/main_menu.rml" "$scratch/shell/UI/screens/test.rml"
	cp -r "$studio/test/fixtures/data/UI" "$scratch/data/UI"

	local failed=0
	run_scripted() {
		local name="$1" project="$2" document="$3" output
		output="$(electron --project "$(native_path "$project")" --open "$document" --shots "$(native_path "$scratch")" --test "test/$name.js" 2>&1)" || failed=1
		echo "$output" | grep -E "^ok|^     |TEST FAILED|^    at " || true
	}
	echo "Editing"
	run_scripted editing "$scratch/edit" UI/screens/test.rml
	echo "Menus, keys and panels"
	run_scripted shell "$scratch/shell" UI/screens/test.rml
	echo "Data"
	run_scripted data "$scratch/data" UI/screens/pause.rml

	rm -rf "$scratch"
	[[ $failed -eq 0 ]] || folio_die "scripted tests failed."
	echo "All tests passed."
}

package() {
	build
	echo "Packaging..."
	(cd "$studio" && npx --yes electron-builder@26 --dir) || folio_die "packaging failed."
	echo "Done. Output in Tools/studio/release"
}

command="${1:-run}"
[[ $# -gt 0 ]] && shift

case "$command" in
	run)
		build
		if [[ $# -gt 0 ]]; then
			electron --project "$(native_path "$(cd "$1" && pwd)")"
		else
			electron
		fi
		;;
	build)   build ;;
	test)    run_tests ;;
	package) package ;;
	engine)  build_engine ;;
	parity)  parity ;;
	-h|--help|help) sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' ;;
	*) folio_die "unknown command: $command. Try ./scripts/studio.sh --help" ;;
esac
