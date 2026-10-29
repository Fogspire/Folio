#!/usr/bin/env bash
#
# Locates Visual Studio's bundled CMake and Ninja and imports the MSVC environment.
#
# Sourced by the build scripts. Nothing has to be installed globally and neither cmake nor ninja has
# to be on PATH.
#
# Bash rather than PowerShell even though this half is Windows-only: a .sh can grow a Linux branch
# later, and a .ps1 never can. When Folio builds on Linux, folio_import_toolchain gains a case here
# rather than a second script somewhere else.
#
# Sets FOLIO_CMAKE, FOLIO_NINJA, FOLIO_VS_PATH and FOLIO_MSVC_TOOLSET.

folio_is_windows() {
	case "$(uname -s)" in
		MINGW*|MSYS*|CYGWIN*) return 0 ;;
		*) return 1 ;;
	esac
}

folio_die() {
	echo "error: $*" >&2
	exit 1
}

# Windows puts a parenthesis in the name, which is not a legal shell identifier, so it cannot be
# read as $ProgramFiles(x86) and has to come out of the environment by name.
folio_program_files_x86() {
	local value
	value="$(printenv 'ProgramFiles(x86)' 2>/dev/null || true)"
	if [[ -n "$value" ]]; then
		cygpath -u "$value"
	else
		echo "/c/Program Files (x86)"
	fi
}

# The entries of a string array in one of the engine's SDK JSON files.
folio_sdk_json_array() {
	sed -n "/\"$2\"[[:space:]]*:[[:space:]]*\[/,/\]/p" "$1" | grep -o '"[0-9][^"]*"' | tr -d '"'
}

# Whether version $1 lies in the inclusive range $2, written as "min-max".
folio_version_in_range() {
	local min="${2%-*}" max="${2#*-}"
	[[ "$(printf '%s\n%s\n' "$min" "$1" | sort -V | head -1)" == "$min" ]] &&
		[[ "$(printf '%s\n%s\n' "$1" "$max" | sort -V | head -1)" == "$1" ]]
}

#
# The MSVC toolset UnrealBuildTool picks for the given engine, out of the given Visual Studio installs.
#
# Follows UBT's own ordering: banned and too-old toolsets are out, then the earliest family in
# PreferredVisualCppVersions wins, then the highest version. The ban list is matched against cl.exe's
# product version, as UBT does, since the folder name can sit inside a banned range when the compiler
# does not. A CompilerVersion set in BuildConfiguration.xml is not seen; pass --toolset for that.
#
folio_unreal_msvc_toolset() {
	local sdk_json="$1/Config/Windows/Windows_SDK.json"
	shift
	[[ -f "$sdk_json" ]] || return 1

	local -a preferred banned
	mapfile -t preferred < <(folio_sdk_json_array "$sdk_json" PreferredVisualCppVersions | tr -d '\r')
	mapfile -t banned < <(folio_sdk_json_array "$sdk_json" BannedVisualCppVersions | tr -d '\r')
	local minimum
	minimum="$(sed -n 's/.*"MinimumVisualCppVersion"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$sdk_json" | head -1)"

	local install dir family cl version range rank
	for install in "$@"; do
		for dir in "$install"/VC/Tools/MSVC/*/; do
			family="$(basename "$dir")"
			cl="$dir/bin/Hostx64/x64/cl.exe"
			[[ -f "$cl" ]] || continue

			version="$(powershell.exe -NoProfile -Command "\$v = (Get-Item -LiteralPath '$(cygpath -w "$cl")').VersionInfo; '{0}.{1}.{2}' -f \$v.ProductMajorPart, \$v.ProductMinorPart, \$v.ProductBuildPart" | tr -d '\r')"
			[[ "$version" == 0.* || -z "$version" ]] && version="$family"

			[[ -n "$minimum" ]] && ! folio_version_in_range "$version" "$minimum-99999" && continue
			for range in "${banned[@]}"; do
				folio_version_in_range "$version" "$range" && continue 2
			done

			rank=0
			for range in "${preferred[@]}"; do
				folio_version_in_range "$family" "$range" && break
				rank=$((rank + 1))
			done
			printf '%s\t%s\t%s\n' "$rank" "$version" "$family"
		done
	done | sort -t$'\t' -k1,1n -k2,2Vr | head -1 | cut -f3
}

#
# Imports the MSVC environment for the given toolset into this shell.
#
# 'latest' picks the highest toolset installed. 'unreal' picks the one UnrealBuildTool uses for the
# engine passed as the second argument, which static libraries linked into an Unreal module must be
# built with: a newer STL emits calls into runtime helpers the older one's libraries do not have.
#
folio_import_msvc() {
	local toolset="${1:-latest}"
	local engine_dir="${2:-}"

	if ! folio_is_windows; then
		folio_die "this build is Windows only. Folio's ThirdParty libraries are MSVC static libraries; there is no Linux toolchain path yet."
	fi

	local vswhere
	vswhere="$(folio_program_files_x86)/Microsoft Visual Studio/Installer/vswhere.exe"
	if [[ ! -f "$vswhere" ]]; then
		folio_die "vswhere.exe not found at '$vswhere'. Install Visual Studio 2022 with the C++ desktop workload."
	fi

	# Installs carrying the C++ toolset, rather than simply the newest: a BuildTools install may
	# ship neither the toolset nor CMake.
	local -a installs=()
	while IFS= read -r line; do
		line="${line%$'\r'}"
		[[ -n "$line" ]] && installs+=("$(cygpath -u "$line")")
	done < <("$vswhere" -products '*' \
		-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
		-property installationPath 2>/dev/null)

	if [[ ${#installs[@]} -eq 0 ]]; then
		folio_die "no Visual Studio installation with the C++ desktop workload was found."
	fi

	if [[ "$toolset" == "unreal" ]]; then
		toolset="$(folio_unreal_msvc_toolset "$engine_dir" "${installs[@]}")" ||
			folio_die "could not read '$engine_dir/Config/Windows/Windows_SDK.json'. Pass --toolset explicitly."
		[[ -n "$toolset" ]] || folio_die "no installed MSVC toolset is one UnrealBuildTool accepts. Check $engine_dir/Config/Windows/Windows_SDK.json."
	elif [[ "$toolset" == "latest" ]]; then
		# Sorted as versions, not as strings, or 14.9 beats 14.50.
		toolset="$(
			for install in "${installs[@]}"; do
				[[ -d "$install/VC/Tools/MSVC" ]] || continue
				ls -1 "$install/VC/Tools/MSVC" 2>/dev/null
			done | sort -V | tail -1
		)"
		[[ -n "$toolset" ]] || folio_die "no MSVC toolset found in any Visual Studio install."
	fi

	FOLIO_VS_PATH=""
	for install in "${installs[@]}"; do
		if [[ -d "$install/VC/Tools/MSVC/$toolset" ]]; then
			FOLIO_VS_PATH="$install"
			break
		fi
	done

	if [[ -z "$FOLIO_VS_PATH" ]]; then
		echo "No Visual Studio install provides MSVC toolset $toolset:" >&2
		for install in "${installs[@]}"; do
			echo "  $install -> $(ls -1 "$install/VC/Tools/MSVC" 2>/dev/null | tr '\n' ' ')" >&2
		done
		folio_die "install the required toolset, or select an available one with --toolset."
	fi

	FOLIO_CMAKE="$FOLIO_VS_PATH/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
	FOLIO_NINJA="$FOLIO_VS_PATH/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
	local vsdevcmd="$FOLIO_VS_PATH/Common7/Tools/VsDevCmd.bat"

	local tool
	for tool in "$FOLIO_CMAKE" "$FOLIO_NINJA" "$vsdevcmd"; do
		[[ -f "$tool" ]] || folio_die "missing '$tool'. Add the 'C++ CMake tools for Windows' component to the Visual Studio install."
	done

	FOLIO_MSVC_TOOLSET="$toolset"
	echo "Importing MSVC environment (toolset $toolset)..." >&2

	# Some of VsDevCmd's helper scripts invoke vswhere without a path.
	PATH="$(dirname "$vswhere"):$PATH"
	export PATH

	# The environment lives in a cmd process, so it has to be dumped and imported.
	#
	# Through a generated batch file rather than `cmd //c "..."`, because a quoted path handed
	# straight from bash to cmd does not survive: bash's escaping and MSYS's path mangling fight over
	# it and cmd ends up seeing a literal \" in the command name. A wrapper does its own quoting on
	# the cmd side, where the rules are consistent.
	local wrapper
	wrapper="$(mktemp --suffix=.bat)"
	printf '@echo off\r\ncall "%s" -arch=amd64 -host_arch=amd64 -vcvars_ver=%s -no_logo\r\nif errorlevel 1 exit /b 1\r\nset\r\n' \
		"$(cygpath -w "$vsdevcmd")" "$toolset" > "$wrapper"

	local dumped status=0
	dumped="$(cmd //c "$(cygpath -w "$wrapper")" 2>&1)" || status=$?
	rm -f "$wrapper"

	if [[ $status -ne 0 ]]; then
		echo "$dumped" >&2
		folio_die "VsDevCmd.bat failed. Is MSVC toolset $toolset installed? Override with --toolset."
	fi

	local name value
	while IFS= read -r line; do
		line="${line%$'\r'}"
		[[ "$line" == *=* ]] || continue
		name="${line%%=*}"
		value="${line#*=}"

		# A name with anything but word characters is not a shell identifier - cmd exports several -
		# and exporting one is a syntax error rather than a warning.
		[[ "$name" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || continue

		if [[ "$name" == "PATH" || "$name" == "Path" ]]; then
			# The only variable that has to change form: bash resolves executables through a POSIX
			# path list, while the compiler reads INCLUDE and LIB as Windows paths and must not be
			# converted.
			PATH="$(cygpath -up "$value")"
			export PATH
		else
			export "$name=$value"
		fi
	done <<< "$dumped"

	[[ -n "${INCLUDE:-}" ]] || folio_die "VsDevCmd.bat did not set INCLUDE. The MSVC environment was not imported."
}

#
# Finds cmake and ninja on PATH, which is where a Mac or Linux developer keeps them. Sets FOLIO_CMAKE
# and FOLIO_NINJA, as folio_import_msvc does on Windows.
folio_import_unix_tools() {
	FOLIO_CMAKE="$(command -v cmake || true)"
	FOLIO_NINJA="$(command -v ninja || true)"
	[[ -n "$FOLIO_CMAKE" ]] || folio_die "cmake was not found. Install it with Homebrew or your package manager."
	[[ -n "$FOLIO_NINJA" ]] || folio_die "ninja was not found. Install it with Homebrew or your package manager."
}

# A path as the tools on this host want it: mixed-style on Windows, unchanged everywhere else.
folio_tool_path() {
	if folio_is_windows; then cygpath -m "$1"; else echo "$1"; fi
}

# Removes a CMake build directory that was configured somewhere else, as one copied along with the
# plugin from another project is. CMake refuses to reuse it, and nothing in it is worth keeping.
folio_discard_moved_build() {
	local cache="$1/CMakeCache.txt"
	[[ -f "$cache" ]] || return 0
	local made_in
	made_in="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$cache" | tr -d '\r')"
	local here
	here="$(folio_tool_path "$1")"
	local compared="$made_in"
	# Windows paths differ in drive letter case alone, as x: and X: do.
	if folio_is_windows; then
		compared="$(echo "$made_in" | tr '[:upper:]' '[:lower:]')"
		here="$(echo "$here" | tr '[:upper:]' '[:lower:]')"
	fi
	if [[ -n "$made_in" && "$compared" != "$here" ]]; then
		echo "Removing $1, which was configured at $made_in"
		rm -rf "$1"
	fi
}

# Removes a CMake build directory whose cached compiler is not the imported MSVC toolset. CMake keeps
# the compiler from its first configure while INCLUDE follows the toolset, and the two must agree.
folio_discard_other_toolset_build() {
	local cache="$1/CMakeCache.txt"
	[[ -f "$cache" && -n "${FOLIO_MSVC_TOOLSET:-}" ]] || return 0
	local compiler
	compiler="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$cache" | tr -d '\r')"
	if [[ "$compiler" == */MSVC/* && "$compiler" != */MSVC/"$FOLIO_MSVC_TOOLSET"/* ]]; then
		echo "Removing $1, which was configured with $compiler"
		rm -rf "$1"
	fi
}

# Resolves the Unreal engine directory for the project this plugin sits in.
#
# Reads EngineAssociation from the .uproject and looks it up in the registry, which is where source
# builds register themselves. A launcher install stores a version string instead, so that case falls
# back to the standard install location. An association may also be a path to the engine, in which
# case nothing is looked up.
#
folio_resolve_engine() {
	local project_root="$1"

	local uproject
	uproject="$(find "$project_root" -maxdepth 1 -name '*.uproject' -print -quit)"
	[[ -n "$uproject" ]] || folio_die "no .uproject found in '$project_root'. Pass --engine-dir explicitly."

	local association
	association="$(sed -n 's/.*"EngineAssociation"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$uproject" | head -1)"
	[[ -n "$association" ]] || folio_die "$(basename "$uproject") has no EngineAssociation. Pass --engine-dir explicitly."

	# A path association is relative to the project unless it is rooted or drive-qualified.
	if [[ "$association" == */* || "$association" == *\\* ]]; then
		local candidate="${association//\\//}"
		[[ "$candidate" == /* || "$candidate" =~ ^[A-Za-z]: ]] || candidate="$project_root/$candidate"
		[[ -d "$candidate/Engine" ]] || folio_die "'$candidate/Engine' does not exist. Pass --engine-dir explicitly."
		( cd "$candidate/Engine" && pwd )
		return
	fi

	local root=""

	if folio_is_windows && command -v reg >/dev/null 2>&1; then
		local key
		for key in 'HKCU\Software\Epic Games\Unreal Engine\Builds' 'HKLM\Software\Epic Games\Unreal Engine\Builds'; do
			# Everything after REG_SZ, not the last field: an engine path routinely contains spaces.
			# reg also emits CRLF, and a trailing carriage return turns a valid path into a missing one.
			root="$(reg query "$key" 2>/dev/null \
				| grep -F "$association" \
				| sed -e 's/\r$//' -e 's/.*REG_SZ[[:space:]]*//' \
				| head -1)"
			[[ -n "$root" ]] && break
		done

		# A launcher install associates by version number rather than by GUID.
		if [[ -z "$root" && "$association" =~ ^[0-9]+\.[0-9]+$ ]]; then
			local install_root
			install_root="$(reg query 'HKLM\Software\EpicGames\Unreal Engine' //v INSTALLDIR 2>/dev/null \
				| sed -e 's/\r$//' -e 's/.*REG_SZ[[:space:]]*//' | head -1)"
			[[ -n "$install_root" ]] && root="$install_root/UE_$association"
		fi
	else
		local install="${XDG_CONFIG_HOME:-$HOME/.config}/Epic/UnrealEngine/Install.ini"
		[[ "$(uname -s)" == Darwin ]] && install="$HOME/Library/Application Support/Epic/UnrealEngine/Install.ini"
		[[ -f "$install" ]] && root="$(sed -n "s|^${association}=||p" "$install" | head -1)"

		# A launcher install on a Mac associates by version and lives in a fixed place.
		if [[ -z "$root" && "$association" =~ ^[0-9]+\.[0-9]+$ && -d "/Users/Shared/Epic Games/UE_$association" ]]; then
			root="/Users/Shared/Epic Games/UE_$association"
		fi
	fi

	[[ -n "$root" ]] || folio_die "could not resolve engine '$association'. Pass --engine-dir explicitly."

	local engine="$root/Engine"
	[[ -d "$engine" ]] || folio_die "'$engine' does not exist. Pass --engine-dir explicitly."

	echo "$engine"
}
