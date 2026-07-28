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

    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'VsEnv.ps1')

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot "build\win64-$Config"

function Fail([string] $Message) {
    Write-Host "build.ps1: $Message" -ForegroundColor Red
    exit 1
}

try { $tools = Import-FolioMsvcEnvironment -Toolset $Toolset } catch { Fail $_ }
$CMakeExe = $tools.CMake
$NinjaExe = $tools.Ninja

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
