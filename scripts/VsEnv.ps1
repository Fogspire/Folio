#Requires -Version 5.1
<#
.SYNOPSIS
    Locates Visual Studio's bundled CMake and Ninja and imports the MSVC environment.

.DESCRIPTION
    Dot-sourced by the build scripts. Nothing has to be installed globally and neither cmake
    nor ninja has to be on PATH.

    Returns a hashtable with CMake, Ninja and VsPath.
#>

function Import-FolioMsvcEnvironment {
    [CmdletBinding()]
    param(
        # 'latest' picks the highest toolset installed, which is what UnrealBuildTool also
        # defaults to. Static libraries that will be linked into an Unreal module must be
        # built with the same one: check UBT's "Toolchain Information" line and pass the
        # version explicitly if it disagrees.
        [string] $Toolset = 'latest'
    )

    $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $VsWhere)) {
        throw "vswhere.exe not found at '$VsWhere'. Install Visual Studio 2022 with the C++ desktop workload."
    }

    # Pick the install that actually carries the requested toolset rather than simply the
    # newest one: a BuildTools install may ship neither the toolset nor CMake.
    $Installs = @(& $VsWhere -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath)
    if (-not $Installs) {
        throw 'No Visual Studio installation with the C++ desktop workload was found.'
    }

    if ($Toolset -eq 'latest') {
        $newest = $Installs |
            ForEach-Object {
                Get-ChildItem (Join-Path $_ 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
                    ForEach-Object { [pscustomobject]@{ Install = $_.Parent.Parent.Parent.FullName; Version = $_.Name } }
            } |
            Sort-Object { [version]($_.Version) } -Descending |
            Select-Object -First 1
        if (-not $newest) { throw 'No MSVC toolset found in any Visual Studio install.' }
        $Toolset = $newest.Version
    }

    $VsPath = $Installs | Where-Object { Test-Path (Join-Path $_ "VC\Tools\MSVC\$Toolset") } | Select-Object -First 1
    if (-not $VsPath) {
        Write-Host "No Visual Studio install provides MSVC toolset ${Toolset}:" -ForegroundColor Red
        foreach ($install in $Installs) {
            $available = Get-ChildItem (Join-Path $install 'VC\Tools\MSVC') -Name -ErrorAction SilentlyContinue
            Write-Host "  $install -> $($available -join ', ')"
        }
        throw 'Install the required toolset, or select an available one with -Toolset.'
    }

    $CMakeExe = Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $NinjaExe = Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    $VsDevCmd = Join-Path $VsPath 'Common7\Tools\VsDevCmd.bat'

    foreach ($tool in @($CMakeExe, $NinjaExe, $VsDevCmd)) {
        if (-not (Test-Path $tool)) {
            throw "Missing '$tool'. Add the 'C++ CMake tools for Windows' component to the Visual Studio install."
        }
    }

    Write-Host "Importing MSVC environment (toolset $Toolset)..." -ForegroundColor DarkGray

    # Some of VsDevCmd's helper scripts invoke vswhere without a path.
    $env:PATH = "$(Split-Path -Parent $VsWhere);$env:PATH"

    # /s stops cmd from mangling the quoting around a path containing spaces.
    $envLines = & $env:COMSPEC /s /c "`"$VsDevCmd`" -arch=amd64 -host_arch=amd64 -vcvars_ver=$Toolset -no_logo && set"
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed. Is MSVC toolset $Toolset installed? Override with -Toolset."
    }
    foreach ($line in $envLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }
    if (-not $env:INCLUDE) {
        throw 'VsDevCmd.bat did not set INCLUDE. The MSVC environment was not imported.'
    }

    return @{ CMake = $CMakeExe; Ninja = $NinjaExe; VsPath = $VsPath }
}

<#
.SYNOPSIS
    Resolves the Unreal engine directory for the project this plugin sits in.

.DESCRIPTION
    Reads EngineAssociation from the .uproject and looks it up in the registry, which is where
    source builds register themselves. A launcher install stores a version string instead, so
    that case falls back to the standard install location.
#>
function Resolve-FolioEngineDirectory {
    [CmdletBinding()]
    param([string] $ProjectRoot)

    $uproject = Get-ChildItem -Path $ProjectRoot -Filter '*.uproject' -File | Select-Object -First 1
    if (-not $uproject) {
        throw "No .uproject found in '$ProjectRoot'. Pass -EngineDir explicitly."
    }

    $association = (Get-Content $uproject.FullName -Raw | ConvertFrom-Json).EngineAssociation
    if (-not $association) {
        throw "$($uproject.Name) has no EngineAssociation. Pass -EngineDir explicitly."
    }

    foreach ($key in 'HKCU:\Software\Epic Games\Unreal Engine\Builds', 'HKLM:\Software\Epic Games\Unreal Engine\Builds') {
        if (-not (Test-Path $key)) { continue }
        $value = (Get-ItemProperty $key).$association
        if ($value) { return (Join-Path $value 'Engine') }
    }

    # A launcher install associates by version number rather than by GUID.
    if ($association -match '^\d+\.\d+$') {
        $installRoot = (Get-ItemProperty 'HKLM:\Software\EpicGames\Unreal Engine' -ErrorAction SilentlyContinue).INSTALLDIR
        if ($installRoot) {
            $candidate = Join-Path $installRoot "UE_$association\Engine"
            if (Test-Path $candidate) { return $candidate }
        }
    }

    throw "Could not resolve engine '$association' from the registry. Pass -EngineDir explicitly."
}
