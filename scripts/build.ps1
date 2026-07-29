#Requires -Version 5.1
<#
.SYNOPSIS
    Builds the Folio native tools.

.DESCRIPTION
    Locates the CMake and Ninja that ship inside Visual Studio, imports the MSVC environment,
    then configures and builds. Nothing has to be installed globally and neither cmake nor
    ninja has to be on PATH.

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Config Debug
    .\scripts\build.ps1 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string] $Config = 'RelWithDebInfo',

    [string] $Toolset = 'latest',

    [string] $Target,

    [switch] $Clean,

    # Escape hatch for editing one side of the distance field shader before the other.
    [switch] $SkipShaderParity,

    # Build with AddressSanitizer, into its own directory. Slower, and the only practical way to
    # get a real answer out of the intermittent crashes this codebase can produce: lifetime bugs
    # here usually read freed memory that is still intact, so they survive for weeks and then
    # fail on someone else's machine.
    [switch] $Asan
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'VsEnv.ps1')

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = if ($Asan) { Join-Path $RepoRoot 'build\win64-asan' } else { Join-Path $RepoRoot "build\win64-$Config" }

function Fail([string] $Message) {
    Write-Host "build.ps1: $Message" -ForegroundColor Red
    exit 1
}

try { $tools = Import-FolioMsvcEnvironment -Toolset $Toolset } catch { Fail $_ }
$CMakeExe = $tools.CMake
$NinjaExe = $tools.Ninja

# --- Shader parity --------------------------------------------------------------------------

# Before the compile, because it takes a moment and catches a class of bug that survives every
# later check: the tool and the engine resolving the same field differently.
if (-not $SkipShaderParity) {
    & (Join-Path $PSScriptRoot 'check_shader_parity.ps1')
    if ($LASTEXITCODE -ne 0) { Fail 'Shader parity check failed. Pass -SkipShaderParity to build anyway.' }
}

# --- Configure and build --------------------------------------------------------------------

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Removing $BuildDir" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $BuildDir
}

$configureArgs = @(
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$NinjaExe",
    "-DCMAKE_BUILD_TYPE=$Config",
    '-S', $RepoRoot,
    '-B', $BuildDir
)

if ($Asan) {
    # /Zi and a non-incremental link, or the report is addresses with no symbols.
    $configureArgs += @(
        '-DCMAKE_C_FLAGS=/fsanitize=address /Zi',
        '-DCMAKE_CXX_FLAGS=/fsanitize=address /Zi',
        '-DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO /DEBUG'
    )
}

Write-Host "Configuring ($Config$(if ($Asan) { ', asan' }))..." -ForegroundColor Cyan
& $CMakeExe @configureArgs
if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed.' }

Write-Host "Building ($Config)..." -ForegroundColor Cyan
$buildArgs = @('--build', $BuildDir, '--parallel')
if ($Target) { $buildArgs += @('--target', $Target) }
& $CMakeExe @buildArgs
if ($LASTEXITCODE -ne 0) { Fail 'Build failed.' }

Write-Host "Done. Output in $(Join-Path $RepoRoot 'bin\win64')" -ForegroundColor Green

# Every configuration writes its executables to the same place, so whichever built last is the
# one that runs. An instrumented binary left there quietly makes everything slower.
if ($Asan) {
    Write-Host 'Instrumented binaries are in bin\win64. Re-run without -Asan to replace them.' -ForegroundColor Yellow
    Write-Host 'They need the MSVC environment on PATH for the sanitizer runtime.' -ForegroundColor Yellow
}
