#Requires -Version 5.1
<#
.SYNOPSIS
    Fails if the HLSL and GLSL distance field shaders have drifted apart.

.DESCRIPTION
    Two renderers resolve the same field: Unreal through Shaders/Private/FolioMsdf.ush, the
    preview tool through the GLSL in Tools/uipreview/src/MsdfProgram.cpp. If they disagree, text
    looks different in the editor than it did in the tool it was authored in, and the difference
    is small enough to argue about and slow to track down.

    A golden image comparison cannot catch this. The two colour pipelines differ - Slate
    linearises vertex colour and applies display gamma, the tool's GL backend does neither - so
    the images never match pixel for pixel even when the maths does.

    So compare the source instead. Both sides are reduced to a canonical token stream: comments
    and declarations dropped, whitespace removed, and a fixed dialect map applied (float3 to vec3,
    saturate to clamp, Texture.Sample to texture, and so on). What survives is the maths, and the
    two must be identical.

.EXAMPLE
    .\scripts\check_shader_parity.ps1
    .\scripts\check_shader_parity.ps1 -ShowCanonical
#>
[CmdletBinding()]
param(
    # Print both canonical forms, whether or not they match. For diagnosing a failure.
    [switch] $ShowCanonical
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$HlslPath = Join-Path $RepoRoot 'Shaders\Private\FolioMsdf.ush'
$GlslPath = Join-Path $RepoRoot 'Tools\uipreview\src\MsdfProgram.cpp'

function Fail([string] $Message) {
    Write-Host "check_shader_parity.ps1: $Message" -ForegroundColor Red
    exit 1
}

foreach ($path in @($HlslPath, $GlslPath)) {
    if (-not (Test-Path $path)) { Fail "missing $path" }
}

# --- Canonicalisation -----------------------------------------------------------------------

# HLSL spelling on the left, GLSL on the right. Whole words only. Identifiers are included
# because the two sides name the same quantity differently: one takes parameters, the other
# reads uniforms and interpolants.
$TokenMap = [ordered] @{
    'float2'     = 'vec2'
    'float3'     = 'vec3'
    'float4'     = 'vec4'
    'lerp'       = 'mix'
    'frac'       = 'fract'
    'rsqrt'      = 'inversesqrt'
    'atan2'      = 'atan'
    'Atlas'      = '_atlas'
    'UV'         = 'fragTexCoord'
    'UnitRange'  = '_unitRange'
    'Threshold'  = '_threshold'
    'Mode'       = '_mode'
    'Offset'     = '_offset'
    'Softness'   = '_softness'
    'Sharpen'    = '_sharpen'
    'Colour'     = 'fragColor'
}

function Remove-Comments([string] $Text) {
    $Text = [regex]::Replace($Text, '/\*.*?\*/', '', 'Singleline')
    $Text = [regex]::Replace($Text, '//[^\r\n]*', '')
    return $Text
}

# Preprocessor lines, and the plumbing declarations that exist on one side only.
function Remove-Declarations([string] $Text) {
    $kept = foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*#') { continue }
        if ($line -match '^\s*(uniform|in|out|layout)\b') { continue }
        $line
    }
    return ($kept -join "`n")
}

function Convert-Dialect([string] $Text) {
    foreach ($entry in $TokenMap.GetEnumerator()) {
        $Text = [regex]::Replace($Text, "\b$([regex]::Escape($entry.Key))\b", $entry.Value)
    }
    return $Text
}

# Rewrites clamp(EXPR, 0.0, 1.0) to saturate(EXPR). Regex cannot do this: EXPR contains its own
# parentheses, so the closing one has to be found by counting.
function Convert-ClampToSaturate([string] $Text) {
    for ($i = 0; ($i = $Text.IndexOf('clamp(', $i)) -ge 0; ) {
        $open = $i + 'clamp('.Length
        $depth = 1
        $end = -1
        for ($j = $open; $j -lt $Text.Length; $j++) {
            if ($Text[$j] -eq '(') { $depth++ }
            elseif ($Text[$j] -eq ')') {
                $depth--
                if ($depth -eq 0) { $end = $j; break }
            }
        }
        if ($end -lt 0) { break }

        $inner = $Text.Substring($open, $end - $open)
        if ($inner -match '^(.*),0\.0,1\.0$') {
            $Text = $Text.Substring(0, $i) + 'saturate(' + $Matches[1] + ')' + $Text.Substring($end + 1)
            $i += 'saturate('.Length
        }
        else {
            $i = $open
        }
    }
    return $Text
}

function Get-Canonical([string] $Text) {
    $Text = Remove-Comments $Text
    $Text = Remove-Declarations $Text
    $Text = Convert-Dialect $Text
    $Text = [regex]::Replace($Text, '\bconst\b', '')

    # Whitespace carries no meaning here, and the two files brace differently.
    $Text = [regex]::Replace($Text, '\s+', '')

    # The entry points differ by construction: one is a callable function, the other a main().
    $Text = [regex]::Replace($Text, 'vec4FolioMsdfShade\([^)]*\)', '')
    $Text = $Text.Replace('voidmain()', '')

    # A GL fragment shader assigns its output where HLSL returns it.
    $Text = [regex]::Replace($Text, '\bfinalColor=', 'return')

    # Texture.Sample(sampler, uv) against texture(sampler, uv).
    $Text = [regex]::Replace($Text, '(\w+)\.Sample\(\w+,', 'texture($1,')

    $Text = Convert-ClampToSaturate $Text

    # HLSL suffixes float literals, GLSL cannot.
    $Text = [regex]::Replace($Text, '(?<=\d)f(?![A-Za-z0-9_])', '')

    return $Text
}

# --- Extract and compare --------------------------------------------------------------------

$hlsl = Get-Content -Raw -LiteralPath $HlslPath

$cpp = Get-Content -Raw -LiteralPath $GlslPath
$match = [regex]::Match($cpp, 'FOLIO_MSDF_GLSL_BEGIN(.*?)FOLIO_MSDF_GLSL_END', 'Singleline')
if (-not $match.Success) {
    Fail "no FOLIO_MSDF_GLSL_BEGIN/END markers in $GlslPath"
}

# Only the shader source inside the raw string literal, not the C++ around it.
$literal = [regex]::Match($match.Groups[1].Value, 'R"\((.*?)\)"', 'Singleline')
if (-not $literal.Success) {
    Fail "no raw string literal between the markers in $GlslPath"
}

$hlslCanonical = Get-Canonical $hlsl
$glslCanonical = Get-Canonical $literal.Groups[1].Value

if ($ShowCanonical) {
    Write-Host "hlsl: $hlslCanonical" -ForegroundColor DarkGray
    Write-Host "glsl: $glslCanonical" -ForegroundColor DarkGray
}

if ($hlslCanonical -eq $glslCanonical) {
    Write-Host "Shader parity OK ($($hlslCanonical.Length) tokens)." -ForegroundColor Green
    exit 0
}

# Point at the first difference rather than dumping two long strings and leaving the reader to
# find it.
$limit = [Math]::Min($hlslCanonical.Length, $glslCanonical.Length)
$at = $limit
for ($k = 0; $k -lt $limit; $k++) {
    if ($hlslCanonical[$k] -ne $glslCanonical[$k]) { $at = $k; break }
}

$from = [Math]::Max(0, $at - 40)
$Show = {
    param([string] $Text)
    $len = [Math]::Min(80, $Text.Length - $from)
    if ($len -le 0) { return '<end of shader>' }
    return $Text.Substring($from, $len)
}

Write-Host 'Shader parity FAILED. The two shaders resolve the field differently.' -ForegroundColor Red
Write-Host ''
Write-Host "  first difference at token $at" -ForegroundColor Yellow
Write-Host "  ush : $(& $Show $hlslCanonical)"
Write-Host "  glsl: $(& $Show $glslCanonical)"
Write-Host ''
Write-Host "  Shaders/Private/FolioMsdf.ush is the source of truth. Update the GLSL between the"
Write-Host "  FOLIO_MSDF_GLSL markers in Tools/uipreview/src/MsdfProgram.cpp to match."
Write-Host "  Re-run with -ShowCanonical to see both in full."
exit 1
