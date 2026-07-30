<#
.SYNOPSIS
Gathers Folio's markup strings and runs Unreal's localisation pipeline.

.DESCRIPTION
Two steps, in this order, because the second cannot see the first's input:

  1. gather_rml.py reads every data-loc key in the project's .rml documents and writes a generated
     header full of NSLOCTEXT. Nothing includes it; it exists for the gatherer to read.
  2. GatherText runs the ordinary pipeline, which picks that header up as source code.

Running GatherText without step 1 produces a manifest with no markup strings in it and says nothing
about the omission, which is the whole reason this script exists rather than a note in a readme.
#>
param(
	[string]$Project = (Resolve-Path "$PSScriptRoot/../../.."),
	[string]$Config = "Config/Localization/Game.ini",
	[string]$Generated = "Source/Generated/FolioText.h",
	[string[]]$Exclude = @("examples"),
	[switch]$GatherOnly,
	[switch]$ScriptOnly
)

$ErrorActionPreference = "Stop"
$project = (Resolve-Path $Project).Path

if (-not $GatherOnly) {
	$script = Join-Path $PSScriptRoot "../Tools/locgather/gather_rml.py"
	$arguments = @("--root", $project, "--out", (Join-Path $project $Generated))
	foreach ($fragment in $Exclude) { $arguments += @("--exclude", $fragment) }

	& python $script @arguments
	if ($LASTEXITCODE -ne 0) { throw "gather_rml failed; nothing was gathered" }
}

if ($ScriptOnly) { return }

# The editor binary that built this project, rather than a guess at where the engine lives.
$engine = (Get-Content (Join-Path $project "*.uproject") -Raw | ConvertFrom-Json).EngineAssociation
$builds = Get-ItemProperty "HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds" -ErrorAction SilentlyContinue
$root = $builds.$engine
if (-not $root) { throw "could not resolve engine '$engine' from the registry; pass -GatherOnly and run GatherText yourself" }

$editor = Join-Path $root "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
$uproject = (Get-Item (Join-Path $project "*.uproject")).FullName

& $editor $uproject -run=GatherText -config="$Config" -unattended -nopause -nosplash
if ($LASTEXITCODE -ne 0) { throw "GatherText failed" }

Write-Host "Localisation gathered. Manifest and archives are under Content/Localization." -ForegroundColor Green
