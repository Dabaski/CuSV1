# coverage.ps1 - the portable-C coverage metric (AGENTS.md north star #1).
#
# WHAT IT MEASURES: what fraction of SVT-AV1's PORTABLE C algorithm surface
# this CUDA port has ported. The denominator is
#     third_party/SVT-AV1/Source/Lib/Codec   (the 239-file algorithm dir)
#   + third_party/SVT-AV1/Source/Lib/C_DEFAULT (the portable reference kernels)
# and explicitly EXCLUDES:
#   - all 11 ASM_* dirs (217,092 nonblank lines of x86/ARM SIMD). Our CUDA
#     kernels are the analog of those, not a separate algorithm, so counting
#     them would measure SVT's optimisation work, not our coverage.
#   - Source/Lib/Globals, Source/API, Source/App - the library shell, the
#     public API surface and the CLI, none of which is a 1:1 algorithm target.
#   - the four *_rtcd.* dispatch files inside Codec, which are generated
#     function-pointer tables plus inline intrinsic stubs (see A13's note).
#   - third_party/ in its entirety (aom, aom_dsp, googletest, fastfeat, ...).
#
# LINE-COUNT METRIC - and its one trap. Counts are NON-BLANK lines, via
# [IO.File]::ReadAllLines filtered on $_.Trim(). Measure-Object -Line also
# reports non-blank, so the two agree; the trap is that Get-Content piped
# into Measure-Object looks like it counts physical lines and does not.
# Verified on the pinned tree: entropy_coding.c is 4,898 non-blank vs 5,551
# physical (653 blank). Both sides of the ratio are measured identically, so
# the percentage is sound; an absolute line count quoted without saying which
# metric it used is not.
#
# THE NUMBER IS TWO NUMBERS. A single LOC percentage can be moved by
# transcribing a table and cannot be moved by fixing an algorithm, so the
# metric is reported PAIRED with a decoder-gated conformance count, and a
# slice only counts as progress if it moves both. See AGENTS.md.
#
# USAGE (from the repo root):
#   powershell -File tools\coverage.ps1            # the table + the percentage
#   powershell -File tools\coverage.ps1 -Baseline  # diff vs the committed
#                                                  # coverage_baseline.txt
#   powershell -File tools\coverage.ps1 -Save      # rewrite the baseline
#
# STATUS: standalone script, not a CMake target and NOT a GATE input. The
# GATE (tools/golden_gen vs expected_primitives.txt) is bit-exactness; this
# is a progress metric. A coverage number must never gate a commit.

[CmdletBinding()]
param(
    [switch]$Baseline,
    [switch]$Save
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lib = Join-Path $root 'third_party\SVT-AV1\Source\Lib'
$areasFile = Join-Path $PSScriptRoot 'coverage_areas.txt'
$baselineFile = Join-Path $PSScriptRoot 'coverage_baseline.txt'

# Non-blank line count. ReadAllLines keeps blank lines, so filter explicitly.
function Count-NonBlank([string]$path) {
    $n = 0
    foreach ($l in [IO.File]::ReadAllLines($path)) { if ($l.Trim().Length -gt 0) { $n++ } }
    return $n
}

if (-not (Test-Path -LiteralPath $lib)) {
    throw "pinned reference not found: $lib"
}
if (-not (Test-Path -LiteralPath $areasFile)) {
    throw "area classification not found: $areasFile"
}

# ------------------------------------------------------- parse the areas file
$areas = New-Object System.Collections.ArrayList
foreach ($raw in [IO.File]::ReadAllLines($areasFile)) {
    $line = $raw.Trim()
    if ($line.Length -eq 0 -or $line.StartsWith('#')) { continue }
    $p = $line.Split('|')
    if ($p.Count -lt 5) {
        throw "malformed area line (need 5 pipe-separated fields): $raw"
    }
    $globs = @()
    foreach ($g in $p[4].Split(';')) { if ($g.Trim().Length -gt 0) { $globs += $g.Trim() } }
    [void]$areas.Add([PSCustomObject]@{
        Id      = $p[0].Trim()
        Name    = $p[1].Trim()
        Covered = [double]$p[2].Trim()
        Why     = $p[3].Trim()
        Globs   = $globs
        Files   = New-Object System.Collections.ArrayList
    })
}

# ------------------------------------------------- walk the reference surface
# Every .c/.h under Codec and C_DEFAULT. A file matched by two areas is a
# classification bug; a file matched by none is reported as UNASSIGNED and
# still counted, so the rows always reconcile to the directory total.
$surface = New-Object System.Collections.ArrayList
foreach ($d in @('Codec', 'C_DEFAULT')) {
    $dir = Join-Path $lib $d
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Extension -in '.c', '.h' } | ForEach-Object {
        [void]$surface.Add([PSCustomObject]@{
            Rel  = "$d/$($_.Name)"
            Full = $_.FullName
        })
    }
}

$owner = @{}
$dupes = New-Object System.Collections.ArrayList
foreach ($f in $surface) {
    $hits = @($areas | Where-Object { $_.Globs -contains $f.Rel })
    if ($hits.Count -gt 1) { [void]$dupes.Add($f.Rel) }
    if ($hits.Count -ge 1) { [void]$hits[0].Files.Add($f); $owner[$f.Rel] = $hits[0].Id }
}

# rtcd exclusion: generated dispatch tables, not portable algorithm. These are
# deliberately absent from every glob in coverage_areas.txt, so they fall out as
# unassigned; they are reclassified here as an explicit excluded row rather than
# being charged against a real area's denominator.
$rtcd = @($surface | Where-Object { $_.Rel -match '_rtcd\.[ch]$' })
$rtcdLines = 0
foreach ($r in $rtcd) { $rtcdLines += Count-NonBlank $r.Full }

# --------------------------------------------------------------- per-area LOC
$rows = New-Object System.Collections.ArrayList
$assignedLines = 0
foreach ($a in $areas) {
    $loc = 0
    foreach ($f in $a.Files) { $loc += Count-NonBlank $f.Full }
    $assignedLines += $loc
    [void]$rows.Add([PSCustomObject]@{
        Id = $a.Id; Name = $a.Name; Files = $a.Files.Count; Lines = $loc
        Covered = $a.Covered; Points = [math]::Round($loc * $a.Covered, 0)
    })
}

# unassigned reconciliation row - computed, never asserted. Everything in the
# surface that no area claimed and that is not an rtcd file.
$unassignedFiles = @($surface | Where-Object {
    -not $owner.ContainsKey($_.Rel) -and ($_.Rel -notmatch '_rtcd\.[ch]$')
})
$unassignedLines = 0
foreach ($f in $unassignedFiles) { $unassignedLines += Count-NonBlank $f.Full }
$totalLines = 0
foreach ($f in $surface) { $totalLines += Count-NonBlank $f.Full }

[void]$rows.Add([PSCustomObject]@{
    Id = '--'; Name = 'unassigned (drift detector)'; Files = $unassignedFiles.Count
    Lines = $unassignedLines; Covered = 0.0; Points = 0.0
})
[void]$rows.Add([PSCustomObject]@{
    Id = '--'; Name = 'excluded: *_rtcd.* dispatch'; Files = $rtcd.Count
    Lines = $rtcdLines; Covered = 0.0; Points = 0.0
})

$coveredTotal = ($rows | Measure-Object -Property Points -Sum).Sum
$pct = if ($totalLines -gt 0) { 100.0 * $coveredTotal / $totalLines } else { 0 }

# ------------------------------------------------------------------- report
$bar = '=============================================================================='
$bar
'PORTABLE-C COVERAGE - SVT-AV1 -> CUDA 12.2 port'
$bar
'--- INPUTS (a coverage number is meaningless without these) ---'
$areasBytes = (Get-Item -LiteralPath $areasFile).Length
$sha = [Security.Cryptography.SHA256]::Create()
$areasHash = ([BitConverter]::ToString(
    $sha.ComputeHash([IO.File]::ReadAllBytes($areasFile)))).Replace('-', '').Substring(0, 16)
# The pinned reference is the denominator's identity. Quote the pin from the
# tree itself rather than from memory: if the snapshot is ever swapped, this
# line changes and the number stops being comparable.
$pin = '(unreadable)'
$chg = Join-Path $root 'third_party\SVT-AV1\CHANGELOG.md'
if (Test-Path -LiteralPath $chg) {
    $m = Select-String -LiteralPath $chg -Pattern '^##\s*\[?([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1
    if ($m) { $pin = $m.Matches[0].Groups[1].Value }
}
'areas file   : {0} ({1} B, sha256:{2})' -f (Split-Path $areasFile -Leaf), $areasBytes, $areasHash
'reference    : third_party/SVT-AV1 (pinned snapshot, CHANGELOG {0})' -f $pin
'denominator  : Source/Lib/{Codec,C_DEFAULT} (*.c,*.h)'
'metric       : NON-BLANK lines (blank lines excluded on both sides)'
'exclusions   : all 11 ASM_* dirs, Globals/API/App, third_party/, and the'
'               four *_rtcd.* dispatch files inside Codec (reported below)'
''
$surfaceFiles = 0
$surfaceLines = 0
foreach ($f in $surface) { $surfaceLines += Count-NonBlank $f.Full; $surfaceFiles++ }
'surface      : {0} files, {1} non-blank lines' -f $surfaceFiles, $surfaceLines
''
'{0,-5} {1,-42} {2,6} {3,7} {4,8} {5,8}' -f 'id', 'area', 'files', 'lines', 'cov%', 'covered'
$bar.Substring(0, 78)
foreach ($r in $rows) {
    '{0,-5} {1,-42} {2,6} {3,7} {4,8} {5,8}' -f $r.Id, $r.Name, $r.Files, $r.Lines,
        ('{0:P0}' -f $r.Covered), $r.Points
}
$bar.Substring(0, 78)
'{0,-5} {1,-42} {2,6} {3,7} {4,8} {5,8}' -f '', 'TOTAL', $surface.Count, $totalLines, '', $coveredTotal
$bar
''
'PORTABLE-C COVERAGE: {0:N1}%  ({1:N0} / {2:N0} non-blank lines)' -f $pct, $coveredTotal, $totalLines
"+1.0 point = {0:N0} non-blank reference lines" -f ($totalLines / 100)
''
if ($dupes.Count) {
    'CLASSIFICATION BUG - a file is claimed by more than one area:'
    $dupes | Sort-Object | ForEach-Object { '  ' + $_ }
    ''
}
if ($unassignedFiles.Count) {
    'UNASSIGNED ({0} files, {1} non-blank lines) - a file in the pinned reference' -f $unassignedFiles.Count, $unassignedLines
    'that no area claims. Either add it to coverage_areas.txt or explain why it is'
    'excluded. It is counted in the denominator, so leaving it unassigned is safe'
    'but silently lowers the percentage:'
    $unassignedFiles | Sort-Object Rel | Select-Object -First 30 | ForEach-Object { '  ' + $_.Rel }
    ''
}

# ------------------------------------------------------------- our own size
$srcRoot = Join-Path $root 'src'
$ourProd = 0; $ourProdFiles = 0; $ourGpu = 0
if (Test-Path -LiteralPath $srcRoot) {
    Get-ChildItem -LiteralPath $srcRoot -Recurse -File |
        Where-Object { $_.Extension -in '.cpp', '.h', '.inc' -and $_.FullName -notmatch '\\tests\\' } |
        ForEach-Object {
            $ourProd += Count-NonBlank $_.FullName
            $ourProdFiles++
        }
    # GPU source lives inside the .cpp files as CUDA raw strings; the two
    # transforms/intra files carry the bulk of it. Counted separately so the
    # host/GPU split is visible rather than assumed.
    foreach ($f in @('src\l3_transforms\transform.cpp', 'src\l4_intra\intra.cpp',
            'src\l5_motion\motion.cpp', 'src\l6_pipeline\pipeline.cpp')) {
        $p = Join-Path $root $f
        if (Test-Path -LiteralPath $p) { $ourGpu += Count-NonBlank $p }
    }
}
'OURS: src/ production {0} non-blank lines in {1} files' -f $ourProd, $ourProdFiles
'      (the l3/l4/l5/l6 .cpp files - host + CUDA strings - are {0} non-blank lines)' -f $ourGpu
''
'PAIRED METRIC - report this number too:'
'  decoder-gated conformance = committed artifacts x decoders verified'
'  content-1:1. See docs/decode_conformance.md for the per-geometry matrix.'
'A slice counts as progress only if it moves BOTH numbers. A LOC percentage'
'alone can be moved by transcribing a table; the conformance count alone does'
'not scale with coverage.'

# ------------------------------------------------------------------ baseline
if ($Save) {
    # LF explicitly: the repo is LF-only (every tracked file measures 0 CR),
    # and [IO.File]::WriteAllLines would emit the platform CRLF instead.
    $out = @(
        '# coverage_baseline.txt - A REGENERABLE ADVISORY SNAPSHOT, not a gate',
        '# and not a source of truth.',
        '#',
        '# Written only by: powershell -File tools\coverage.ps1 -Save',
        '# Regenerate freely. NEVER hand-edit, and never treat a diff here as a',
        '# failure: this file exists so that a change in the per-area table is',
        '# visible in one command (-Baseline), NOT so that it can be enforced.',
        '# An enforced coverage number would be a gate on LOC, which rewards',
        '# transcribing tables and punishes fixing algorithms. The real gates are',
        '# bit-exactness (tools/golden_gen vs expected_primitives.txt) and',
        '# decoder conformance (docs/decode_conformance.md).',
        '#',
        '# The number itself is also printed by every plain run, together with the',
        '# inputs that produced it, so this file is a convenience and never the',
        '# only place the figure exists.',
        ("generated  : {0}" -f (Get-Date -Format 'yyyy-MM-dd')),
        ("reference  : third_party/SVT-AV1 pinned snapshot, CHANGELOG {0}" -f $pin),
        ("areas file : {0} B, sha256:{1}" -f $areasBytes, $areasHash),
        ("surface    : {0} files, {1} non-blank lines" -f $surfaceFiles, $surfaceLines),
        ("coverage   : {0:N1}%  ({1:N0} covered, {2:N0} denominator)" -f $pct, $coveredTotal, $surfaceLines),
        ''
    )
    foreach ($r in $rows) {
        $out += ('{0,-5} {1,-42} {2,6} {3,7} {4,8} {5,8}' -f $r.Id, $r.Name, $r.Files, $r.Lines,
            ('{0:P0}' -f $r.Covered), $r.Points)
    }
    # LF explicitly: the repo is LF-only (every tracked file measures 0 CR),
    # and [IO.File]::WriteAllLines would emit the platform CRLF instead.
    $lf = ($out -join "`n") + "`n"
    [IO.File]::WriteAllText($baselineFile, $lf, (New-Object Text.UTF8Encoding $false))
    ''
    "baseline written: $baselineFile"
}

if ($Baseline) {
    if (-not (Test-Path -LiteralPath $baselineFile)) {
        throw "no baseline to diff: $baselineFile (run with -Save to create it)"
    }
    # Parse the saved rows back into fields and REBUILD them with the same
    # format used above, so the comparison is field-vs-field. Comparing the
    # saved text lines against differently-formatted current lines reports a
    # diff for every row, which is a false alarm rather than a finding.
    $old = New-Object System.Collections.ArrayList
    foreach ($l in [IO.File]::ReadAllLines($baselineFile)) {
        if ($l -notmatch '^(A\d+|--)\s') { continue }
        $p = $l -split '\s{2,}'
        # id, name, files, lines, cov%, covered
        if ($p.Count -lt 6) { continue }
        [void]$old.Add(('{0,-5} {1,-42} {2,6} {3,7} {4,8} {5,8}' -f
            $p[0], $p[1], $p[2], $p[3], $p[4], $p[5]).TrimEnd())
    }
    $now = New-Object System.Collections.ArrayList
    foreach ($r in $rows) {
        [void]$now.Add(('{0,-5} {1,-42} {2,6} {3,7} {4,8} {5,8}' -f
            $r.Id, $r.Name, $r.Files, $r.Lines, ('{0:P0}' -f $r.Covered), $r.Points).TrimEnd())
    }
    $d = Compare-Object $old $now
    if ($d) {
        ''
        'BASELINE DIFF (<= baseline, => current):'
        $d | ForEach-Object { '  {0} {1}' -f $_.SideIndicator, $_.InputObject }
    } else {
        ''
        'BASELINE DIFF: none - the current table matches coverage_baseline.txt.'
    }
}
