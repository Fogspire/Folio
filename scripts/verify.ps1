#Requires -Version 5.1
<#
.SYNOPSIS
    Runs the Folio verification harness.

.DESCRIPTION
    Three things, in order of how much they catch:

    Parity. The HLSL and GLSL field shaders must agree, or the editor and the preview tool
    disagree about text in a way no screenshot comparison would reveal.

    Layout. Every test and example document is rendered under --strict and its text metrics
    written out. Given a baseline, the reports are compared numerically: a moved line break is
    the symptom of an advance that changed, which is the regression this system is most likely
    to cause and the least likely to be noticed by eye.

    Images. Screenshots of the scale, weight and typography documents. These are for a person to
    look at - the distance field case is about how text reads at 8x magnification and at 10dp,
    and neither is a number.

    Nothing here asserts against a committed golden image. The two renderers' colour pipelines
    differ, displays differ, and a harness that cries wolf gets ignored.

.EXAMPLE
    .\scripts\verify.ps1
    .\scripts\verify.ps1 -Baseline build\verify-before
    .\scripts\verify.ps1 -Out build\verify-before
#>
[CmdletBinding()]
param(
    # Where reports and screenshots are written.
    [string] $Out,

    # A previous -Out directory. Metrics reports are compared against it and differences are
    # listed; without it the run only records.
    [string] $Baseline,

    [switch] $SkipShaderParity,

    # Also run RmlUi's own visual test suite. Off by default: 109 documents take longer than the
    # rest of the harness put together, and it is the sweep you run before a commit that touches
    # the render interface rather than after every edit.
    [switch] $VisualTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Preview = Join-Path $RepoRoot 'bin\win64\uipreview.exe'
if (-not $Out) { $Out = Join-Path $RepoRoot 'build\verify' }

$failures = New-Object System.Collections.Generic.List[string]

function Fail([string] $Message) {
    Write-Host "  FAIL $Message" -ForegroundColor Red
    $failures.Add($Message)
}

function Pass([string] $Message) {
    Write-Host "  ok   $Message" -ForegroundColor DarkGray
}

# uipreview reports on stderr by design. Under $ErrorActionPreference = 'Stop' PowerShell turns
# every one of those lines into a terminating NativeCommandError, so the tool working as intended
# would abort the harness. Run native commands with that off and read the exit code instead.
function Invoke-Preview {
    param([string[]] $Arguments)

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $Preview @Arguments 2>&1
        return [pscustomobject] @{ ExitCode = $LASTEXITCODE; Output = $output }
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

if (-not (Test-Path $Preview)) {
    Write-Host "verify.ps1: $Preview not found. Run scripts\build.ps1 first." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null

# --- Shader parity --------------------------------------------------------------------------

if (-not $SkipShaderParity) {
    Write-Host 'Shader parity' -ForegroundColor Cyan
    $null = & (Join-Path $PSScriptRoot 'check_shader_parity.ps1') 2>&1
    if ($LASTEXITCODE -ne 0) { Fail 'HLSL and GLSL field shaders disagree (run check_shader_parity.ps1)' }
    else { Pass 'FolioMsdf.ush matches MsdfProgram.cpp' }
}

# --- Documents ------------------------------------------------------------------------------

$TestsDir = Join-Path $RepoRoot 'Tools\uipreview\tests'
$documents = Get-ChildItem -Path $TestsDir -Filter *.rml -File | Sort-Object Name

# What a document needs beyond the defaults, and what it is supposed to do. missing_assets.rml
# exists to be caught, so a clean exit from it would mean the sprite checks had stopped working.
$DocumentSetup = @{
    'data_model'     = @{ Args = @('--data', (Join-Path $TestsDir 'hud.json')) }
    'hud'            = @{ Args = @('--data', (Join-Path $TestsDir 'hud.json')) }
    'menu'           = @{ Args = @('--data', (Join-Path $TestsDir 'hud.json')) }
    'missing_assets' = @{ Expect = 5 }
    'effect_edges'   = @{ Expect = 5 }
}

# The dp ratio is pinned rather than derived from the window. A window larger than the display is
# silently clamped, so an unpinned run reports whatever ratio the desktop happened to allow and
# two machines would never agree.
$Ratios = @(1.0, 2.0)

Write-Host 'Layout' -ForegroundColor Cyan
foreach ($doc in $documents) {
    foreach ($ratio in $Ratios) {
        $name = "$($doc.BaseName)@$ratio"
        $report = Join-Path $Out "$name.metrics.json"

        $setup = $DocumentSetup[$doc.BaseName]
        $extra = if ($setup -and $setup.ContainsKey('Args')) { $setup.Args } else { @() }
        $expect = if ($setup -and $setup.ContainsKey('Expect')) { $setup.Expect } else { 0 }

        $run = Invoke-Preview (@($doc.FullName, '--size', '1920x1080', '--dp-ratio', $ratio,
            '--strict', '--no-assets-wanted', '--metrics-report', $report) + $extra)

        if ($run.ExitCode -ne $expect) {
            Fail "$name exited $($run.ExitCode), expected $expect"
            $run.Output | Select-Object -First 5 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkYellow }
            continue
        }
        if (-not (Test-Path $report)) { Fail "$name wrote no metrics report" ; continue }
        Pass $name
    }
}

# --- Compare against a baseline -------------------------------------------------------------

if ($Baseline) {
    Write-Host 'Metrics diff' -ForegroundColor Cyan
    if (-not (Test-Path $Baseline)) {
        Fail "baseline $Baseline does not exist"
    }
    else {
        foreach ($current in Get-ChildItem -Path $Out -Filter *.metrics.json -File) {
            $before = Join-Path $Baseline $current.Name
            if (-not (Test-Path $before)) {
                Write-Host "  new  $($current.BaseName)" -ForegroundColor Yellow
                continue
            }

            $a = Get-Content -Raw -LiteralPath $before | ConvertFrom-Json
            $b = Get-Content -Raw -LiteralPath $current.FullName | ConvertFrom-Json

            # Line text first. A box that resized is worth knowing about; a line that broke
            # somewhere new is what actually changes how a screen reads.
            $linesA = @($a.text | ForEach-Object { $_.lines } | ForEach-Object { $_.text })
            $linesB = @($b.text | ForEach-Object { $_.lines } | ForEach-Object { $_.text })

            if ($linesA.Count -ne $linesB.Count) {
                Fail "$($current.BaseName): $($linesA.Count) lines became $($linesB.Count)"
                continue
            }

            $moved = 0
            for ($i = 0; $i -lt $linesA.Count; $i++) {
                if ($linesA[$i] -ne $linesB[$i]) {
                    if ($moved -lt 5) {
                        Write-Host "    line $i" -ForegroundColor Yellow
                        Write-Host "      was: $($linesA[$i])"
                        Write-Host "      now: $($linesB[$i])"
                    }
                    $moved++
                }
            }

            if ($moved -gt 0) {
                if ($moved -ge 5) { Write-Host "    ... and $($moved - 5) more" -ForegroundColor Yellow }
                Fail "$($current.BaseName): $moved lines wrapped differently"
            }
            else {
                Pass "$($current.BaseName) unchanged"
            }
        }
    }
}

# --- Screenshots ----------------------------------------------------------------------------

Write-Host 'Screenshots' -ForegroundColor Cyan
$Shots = @(
    @{ Doc = 'scale.rml';      Size = '1920x1080'; Ratio = 1.0 },
    @{ Doc = 'weights.rml';    Size = '1920x1080'; Ratio = 1.0 },
    @{ Doc = 'typography.rml'; Size = '1920x1080'; Ratio = 1.0 },
    @{ Doc = 'typography.rml'; Size = '1920x1080'; Ratio = 2.0 },
    @{ Doc = 'effects.rml';      Size = '1920x1080'; Ratio = 1.0 },
    @{ Doc = 'effect_edges.rml'; Size = '800x480';   Ratio = 1.0 },
    @{ Doc = 'vector_stack.rml'; Size = '600x400';   Ratio = 1.0 },
    @{ Doc = 'outside_blur.rml'; Size = '700x460';   Ratio = 1.0 },
    @{ Doc = 'vector_strokes.rml'; Size = '620x700'; Ratio = 1.0 },
    @{ Doc = 'textured_text.rml'; Size = '620x480';  Ratio = 1.0 },
    @{ Doc = 'shader_decorator.rml'; Size = '640x620'; Ratio = 1.0 }
)

foreach ($shot in $Shots) {
    $doc = Join-Path $TestsDir $shot.Doc
    if (-not (Test-Path $doc)) { Fail "missing $($shot.Doc)" ; continue }

    $name = "$([IO.Path]::GetFileNameWithoutExtension($shot.Doc))@$($shot.Ratio).png"
    $png = Join-Path $Out $name

    $null = Invoke-Preview @($doc, '--size', $shot.Size, '--dp-ratio', $shot.Ratio,
        '--no-assets-wanted', '--screenshot', $png)
    if (-not (Test-Path $png)) { Fail "no screenshot for $name" } else { Pass $name }
}

# --- Texture reload -------------------------------------------------------------------------

# What a hot reload does, done twice in one process: render, throw every texture away, render
# again. The two images must be identical. They are not, if something releases a texture it does
# not own - which is easy to do, because a glyph atlas and a baked sprite sheet reach the render
# interface through the same call and only one of them belongs to RmlUi.
Write-Host 'Texture reload' -ForegroundColor Cyan
foreach ($name in @('typography', 'effects', 'menu')) {
    $doc = Join-Path $TestsDir "$name.rml"
    if (-not (Test-Path $doc)) { continue }

    $plain = Join-Path $Out "$name.reload-before.png"
    $after = Join-Path $Out "$name.reload-after.png"

    $extra = @()
    $setup = $DocumentSetup[$name]
    if ($setup -and $setup.ContainsKey('Args')) { $extra = $setup.Args }

    $null = Invoke-Preview (@($doc, '--size', '1920x1080', '--dp-ratio', 1.0, '--no-assets-wanted',
        '--screenshot', $plain) + $extra)
    $null = Invoke-Preview (@($doc, '--size', '1920x1080', '--dp-ratio', 1.0, '--no-assets-wanted',
        '--reload-textures', '--screenshot', $after) + $extra)

    if (-not (Test-Path $plain) -or -not (Test-Path $after)) {
        Fail "${name}: reload comparison produced no image"
        continue
    }

    $a = (Get-FileHash -Algorithm SHA256 $plain).Hash
    $b = (Get-FileHash -Algorithm SHA256 $after).Hash
    if ($a -ne $b) { Fail "$name renders differently after releasing its textures" }
    else { Pass "$name survives a texture reload" }
}

# --- RmlUi's visual test suite --------------------------------------------------------------

if ($VisualTests) {
    Write-Host 'Visual tests' -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'visual_tests.ps1') -Out (Join-Path $Out 'visual')
    if ($LASTEXITCODE -ne 0) { Fail 'RmlUi visual tests regressed (run visual_tests.ps1)' }
    else { Pass 'RmlUi visual tests match their baseline' }
}

# --- Summary --------------------------------------------------------------------------------

Write-Host ''
if ($failures.Count -eq 0) {
    Write-Host "Verification passed. Output in $Out" -ForegroundColor Green
    exit 0
}

Write-Host "Verification failed: $($failures.Count) problem(s)." -ForegroundColor Red
foreach ($f in $failures) { Write-Host "  $f" -ForegroundColor Red }
Write-Host "Output in $Out"
exit 1
