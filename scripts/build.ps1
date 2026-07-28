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

    # Matches the toolset UE 5.8 selects, so the static libraries built here can link into the
    # Phase 3 UE ThirdParty module without an ABI mismatch in the C++ standard library.
    [string] $Toolset = '14.38.33130',

    [string] $Target,

    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build\win64-$Config"

function Fail([string] $Message) {
    Write-Host "build.ps1: $Message" -ForegroundColor Red
    exit 1
}

# --- Locate Visual Studio -------------------------------------------------------------------

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $VsWhere)) {
    Fail "vswhere.exe not found at '$VsWhere'. Install Visual Studio 2022 with the C++ desktop workload."
}

$VsPath = & $VsWhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $VsPath) {
    Fail 'No Visual Studio installation with the C++ desktop workload was found.'
}

$CMakeExe = Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$NinjaExe = Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$VsDevCmd = Join-Path $VsPath 'Common7\Tools\VsDevCmd.bat'

foreach ($tool in @($CMakeExe, $NinjaExe, $VsDevCmd)) {
    if (-not (Test-Path $tool)) {
        Fail "Missing '$tool'. Add the 'C++ CMake tools for Windows' component to the Visual Studio install."
    }
}

# --- Import the MSVC environment ------------------------------------------------------------

# Ninja invokes cl.exe directly, so INCLUDE, LIB and LIBPATH must be present in this process.
Write-Host "Importing MSVC environment (toolset $Toolset)..." -ForegroundColor DarkGray
$envLines = & cmd.exe /c "`"$VsDevCmd`" -arch=amd64 -host_arch=amd64 -vcvars_ver=$Toolset -no_logo && set"
if ($LASTEXITCODE -ne 0) {
    Fail "VsDevCmd.bat failed. Is MSVC toolset $Toolset installed? Override with -Toolset."
}
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
    }
}
if (-not $env:INCLUDE) {
    Fail 'VsDevCmd.bat did not set INCLUDE. The MSVC environment was not imported.'
}

# --- Configure and build --------------------------------------------------------------------

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Removing $BuildDir" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "Configuring ($Config)..." -ForegroundColor Cyan
& $CMakeExe -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$NinjaExe" `
    "-DCMAKE_BUILD_TYPE=$Config" `
    -S $RepoRoot `
    -B $BuildDir
if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed.' }

Write-Host "Building ($Config)..." -ForegroundColor Cyan
$buildArgs = @('--build', $BuildDir, '--parallel')
if ($Target) { $buildArgs += @('--target', $Target) }
& $CMakeExe @buildArgs
if ($LASTEXITCODE -ne 0) { Fail 'Build failed.' }

Write-Host "Done. Output in $(Join-Path $RepoRoot 'bin\win64')" -ForegroundColor Green
