#Requires -Version 5.1
<#
.SYNOPSIS
    Builds RmlUi as static libraries for the Unreal ThirdParty module.

.DESCRIPTION
    Separate from build.ps1 because the engine build wants different settings to the preview
    tool: the engine's FreeType rather than our own, the DLL runtime, no RTTI, and output
    staged into Source/ThirdParty/RmlUiLibrary rather than bin/.

    Run this after changing the RmlUi pin or a patch. The staged libraries are committed, so
    a normal Unreal build needs neither CMake nor this script.

.EXAMPLE
    .\scripts\build_ue_libs.ps1
    .\scripts\build_ue_libs.ps1 -EngineDir "Z:\Epic Games\Src\5.8\Engine"
    .\scripts\build_ue_libs.ps1 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string] $Config = 'RelWithDebInfo',

    [string] $Toolset = 'latest',

    [string] $EngineDir,

    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'VsEnv.ps1')

$RepoRoot = Split-Path -Parent $PSScriptRoot
$SourceDir = Join-Path $RepoRoot 'cmake\ue'
$BuildDir = Join-Path $RepoRoot "build\ue-$Config"
$StageDir = Join-Path $RepoRoot 'Source\ThirdParty\RmlUiLibrary'

try {
    if (-not $EngineDir) {
        # The plugin sits at <project>/Plugins/Folio.
        $ProjectRoot = Split-Path -Parent (Split-Path -Parent $RepoRoot)
        $EngineDir = Resolve-FolioEngineDirectory -ProjectRoot $ProjectRoot
    }
    Write-Host "Engine: $EngineDir" -ForegroundColor DarkGray

    $tools = Import-FolioMsvcEnvironment -Toolset $Toolset

    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host "Removing $BuildDir" -ForegroundColor DarkGray
        Remove-Item -Recurse -Force $BuildDir
    }

    Write-Host "Configuring ($Config)..." -ForegroundColor Cyan
    & $tools.CMake -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)" `
        "-DCMAKE_BUILD_TYPE=$Config" `
        "-DFOLIO_ENGINE_DIR=$($EngineDir -replace '\\', '/')" `
        -S $SourceDir `
        -B $BuildDir
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    Write-Host "Building ($Config)..." -ForegroundColor Cyan
    & $tools.CMake --build $BuildDir --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

    Write-Host "Done. Staged into $StageDir" -ForegroundColor Green
}
catch {
    Write-Host "build_ue_libs.ps1: $_" -ForegroundColor Red
    exit 1
}
