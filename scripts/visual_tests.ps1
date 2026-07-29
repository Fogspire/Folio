#Requires -Version 5.1
<#
.SYNOPSIS
    Runs RmlUi's own visual test suite against Folio's renderer.

.DESCRIPTION
    RmlUi ships ~109 documents covering layout, floats, flexbox, tables, transforms, filters,
    clip masks, box-shadow and text. They are a far broader exercise of the render interface than
    anything hand-written here, which is exactly why the brief names them as the correctness
    harness for Phase 3.

    They are not our documents and they are not written to our conventions. Several use `<link>`
    tags with no `type` (metadata for RmlUi's own test app), some name features Folio bans, and a
    few need input to show anything. So the bar is not "zero diagnostics": it is that a document
    renders without crashing, and that its diagnostic count has not gone up since the recorded
    baseline. A regression in the renderer moves those numbers; a document we never handled stays
    where it was and is visible in the file.

    Screenshots are written for every test so a person can look, which is the only way to catch
    "renders, but wrong".

.EXAMPLE
    .\scripts\visual_tests.ps1
    .\scripts\visual_tests.ps1 -UpdateBaseline
    .\scripts\visual_tests.ps1 -Filter flex
#>
[CmdletBinding()]
param(
    [string] $Out,

    # Substring match on the document name, for working on one area.
    [string] $Filter,

    # Record what the suite does now as the new expectation. Read the diff before committing it.
    [switch] $UpdateBaseline
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Preview = Join-Path $RepoRoot 'bin\win64\uipreview.exe'
$BaselinePath = Join-Path $RepoRoot 'Tools\uipreview\tests\visual_baseline.json'
if (-not $Out) { $Out = Join-Path $RepoRoot 'build\visual' }

function Fail([string] $Message) {
    Write-Host "visual_tests.ps1: $Message" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $Preview)) { Fail "$Preview not found. Run scripts\build.ps1 first." }

# The suite lives in the fetched RmlUi tree, so it follows the pin without being vendored.
$RmlUiRoot = $null
foreach ($config in @('win64-RelWithDebInfo', 'win64-Debug', 'win64-Release')) {
    $candidate = Join-Path $RepoRoot "build\$config\_deps\rmlui-src"
    if (Test-Path (Join-Path $candidate 'Tests\Data\VisualTests')) { $RmlUiRoot = $candidate; break }
}

if (-not $RmlUiRoot) {
    Fail 'no fetched RmlUi tree with Tests/Data/VisualTests. Run scripts\build.ps1 first.'
}

$TestsData = Join-Path $RmlUiRoot 'Tests\Data'
$Assets = Join-Path $RmlUiRoot 'Samples\assets'

# The suite's own stylesheet names LatoLatin and nothing else. Folio bundles no Lato, and
# substituting a different face would change every line break in the suite.
$Fonts = @()
foreach ($face in @('LatoLatin-Regular', 'LatoLatin-Bold', 'LatoLatin-Italic', 'LatoLatin-BoldItalic')) {
    $path = Join-Path $Assets "$face.ttf"
    if (Test-Path $path) { $Fonts += @('--font', $path) }
}
if (-not $Fonts) { Fail "no LatoLatin faces under $Assets" }

New-Item -ItemType Directory -Force -Path $Out | Out-Null

# Several tests name art as `assets/invader.tga`, which RmlUi's own harness finds because it adds
# Samples/assets as a second search path. uipreview has one root by design, so the files are
# copied under it instead. This is the fetched build tree, so a clean rebuild undoes it and the
# copy happens again.
$AssetsUnderRoot = Join-Path $TestsData 'assets'
if (-not (Test-Path $AssetsUnderRoot)) {
    New-Item -ItemType Directory -Force -Path $AssetsUnderRoot | Out-Null
}
foreach ($art in Get-ChildItem -Path $Assets -File -Include *.tga, *.png -Recurse) {
    $target = Join-Path $AssetsUnderRoot $art.Name
    if (-not (Test-Path $target)) { Copy-Item -LiteralPath $art.FullName -Destination $target }
}

$documents = Get-ChildItem -Path (Join-Path $TestsData 'VisualTests') -Filter *.rml -File | Sort-Object Name
if ($Filter) { $documents = $documents | Where-Object { $_.BaseName -like "*$Filter*" } }

$baseline = @{}
if (Test-Path $BaselinePath) {
    $loaded = Get-Content -Raw -LiteralPath $BaselinePath | ConvertFrom-Json
    foreach ($property in $loaded.PSObject.Properties) { $baseline[$property.Name] = $property.Value }
}

$results = [ordered] @{}
$failures = New-Object System.Collections.Generic.List[string]
$crashes = 0

Write-Host "Running $($documents.Count) visual tests" -ForegroundColor Cyan

foreach ($doc in $documents) {
    $name = $doc.BaseName
    $report = Join-Path $Out "$name.json"
    $shot = Join-Path $Out "$name.png"

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $null = & $Preview $doc.FullName --root $TestsData @Fonts --size 1024x768 --dp-ratio 1 `
        --no-assets-wanted --json-report $report --screenshot $shot 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous

    $errors = -1
    $warnings = -1
    if (Test-Path $report) {
        $parsed = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
        $errors = [int] $parsed.result.errors
        $warnings = [int] $parsed.result.warnings
    }

    $results[$name] = [ordered] @{ exit = $code; errors = $errors; warnings = $warnings }

    # Anything past the documented exit codes is a crash: 0 ok, 1 document errors, 2 usage,
    # 3 init, 4 runtime, 5 strict warnings. A segfault surfaces here as something else entirely.
    if ($code -lt 0 -or $code -gt 5) {

        $crashes++
        $failures.Add("$name crashed (exit $code)")
        Write-Host "  CRASH $name (exit $code)" -ForegroundColor Red
        continue
    }

    if (-not (Test-Path $shot)) {
        $failures.Add("$name produced no image")
        Write-Host "  FAIL  $name produced no image" -ForegroundColor Red
        continue
    }

    if (-not $UpdateBaseline -and $baseline.ContainsKey($name)) {
        $was = $baseline[$name]
        if ($errors -gt [int] $was.errors) {
            $failures.Add("${name}: $([int] $was.errors) errors became $errors")
            Write-Host "  FAIL  $name errors $([int] $was.errors) -> $errors" -ForegroundColor Red
            continue
        }
        if ($warnings -gt [int] $was.warnings) {
            $failures.Add("${name}: $([int] $was.warnings) warnings became $warnings")
            Write-Host "  FAIL  $name warnings $([int] $was.warnings) -> $warnings" -ForegroundColor Red
            continue
        }
        if ($errors -lt [int] $was.errors -or $warnings -lt [int] $was.warnings) {
            Write-Host "  better $name $([int] $was.errors)/$([int] $was.warnings) -> $errors/$warnings" -ForegroundColor Green
            continue
        }
    }
    elseif (-not $UpdateBaseline) {
        Write-Host "  new   $name ($errors errors, $warnings warnings)" -ForegroundColor Yellow
        continue
    }

    Write-Host "  ok    $name" -ForegroundColor DarkGray
}

if ($UpdateBaseline) {
    # No byte order mark: PowerShell 5.1 writes one with -Encoding UTF8 and most other readers
    # treat it as content, so the file stops being parseable by anything else.
    $json = $results | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText($BaselinePath, $json, (New-Object Text.UTF8Encoding $false))
    Write-Host "Baseline written to $BaselinePath" -ForegroundColor Green
    exit 0
}

Write-Host ''

# Most of the suite reports something, and almost all of it is the same two things: `<link>` tags
# carrying metadata rather than a stylesheet, which RmlUi's parser rejects, and seventeen
# documents whose stylesheet path is wrong upstream. Counting documents that say nothing at all
# would therefore measure the suite, not the renderer. What matters is that every one produced an
# image and none of them moved.
$drawn = ($results.Values | Where-Object { $_.exit -ge 0 -and $_.exit -le 5 }).Count
Write-Host "$drawn of $($results.Count) rendered." -ForegroundColor DarkGray

if ($failures.Count -eq 0) {
    Write-Host "Visual tests passed. Images in $Out" -ForegroundColor Green
    exit 0
}

Write-Host "Visual tests failed: $($failures.Count) problem(s), $crashes crash(es)." -ForegroundColor Red
foreach ($f in $failures) { Write-Host "  $f" -ForegroundColor Red }
exit 1
