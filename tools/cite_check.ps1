# cite_check.ps1 - mechanical verification of the docs' file:line citations.
#
# PURPOSE (AGENTS.md citation rule): a file:line citation in a tracked doc is
# a claim that a specific line of a specific file says a specific thing. This
# script extracts every `file.ext:NNN` / `file.ext:NNN-MMM` citation from the
# tracked markdown, resolves the file, and prints the ACTUAL bytes at that
# line. It does not judge the claim - it makes the claim auditable, so a
# reviewer can diff claim-vs-content in one pass instead of 296 separate reads.
#
# USAGE (from the repo root):
#   powershell -File tools\cite_check.ps1                 # summary per citation
#   powershell -File tools\cite_check.ps1 -Context 2      # wider context
#   powershell -File tools\cite_check.ps1 -Doc AGENTS.md  # one document
#   powershell -File tools\cite_check.ps1 -Unresolved     # only unresolvable
#
# RESOLUTION ORDER for a cited basename (first hit wins):
#   1. docs/<name>                        (doc cross-references)
#   2. src/<layer>/<name>                 (our port; layer = owning dir)
#   3. src/**/<name>                      (our port, incl. tests/)
#   4. tools/golden_gen/<name>            (the committed generator)
#   5. tools/<name>                       (the instrument scripts)
#   6. third_party/SVT-AV1/Source/Lib/Codec/<name>       (pinned reference)
#   7. third_party/SVT-AV1/Source/Lib/C_DEFAULT/<name>
#   8. third_party/SVT-AV1/Source/Lib/Globals/<name>
#   9. third_party/SVT-AV1/Source/API/<name>
#  10. third_party/SVT-AV1/<name>          (top level: LICENSE, NOTICE, ...)
#  11. third_party/aom/... , third_party/aom_dsp/...    (vendored aom headers)
# Anything unresolved is reported as EXTERNAL - the aom/dav1d decoder sources
# are deliberately NOT vendored (see the RT7-a commit: the throwaway dav1d is
# built outside the repo), so an aom/dav1d citation cannot be checked here and
# must be checked against the external checkout by hand. That is stated, not
# silently passed.
#
# STATUS: standalone script, not a CMake target and not a GATE input. The
# GATE is bit-exactness; this is a documentation audit.

[CmdletBinding()]
param(
    [int]$Context = 0,
    [string]$Doc = '',
    [switch]$Unresolved,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# ---------------------------------------------------------------- doc set
$docs = @()
if ($Doc) {
    $docs = @(Get-Item -LiteralPath (Join-Path $root $Doc))
} else {
    $docs = @(Get-Item -LiteralPath (Join-Path $root 'AGENTS.md'))
    $docs += @(Get-Item -LiteralPath (Join-Path $root 'README.md'))
    $docs += @(Get-ChildItem -LiteralPath (Join-Path $root 'docs') -File -Filter '*.md')
    $gg = Join-Path $root 'tools\golden_gen\README.md'
    if (Test-Path -LiteralPath $gg) { $docs += Get-Item -LiteralPath $gg }
}

# ------------------------------------------------------- resolution roots
$searchRoots = @(
    'docs',
    'src',
    'tools\golden_gen',
    'tools',
    'third_party\SVT-AV1\Source\Lib\Codec',
    'third_party\SVT-AV1\Source\Lib\C_DEFAULT',
    'third_party\SVT-AV1\Source\Lib\Globals',
    'third_party\SVT-AV1\Source\API',
    'third_party\SVT-AV1',
    'third_party\aom',
    'third_party\aom_dsp'
)

# Files that live OUTSIDE the repo by design (aom + dav1d decoder internals).
# Listed so the report can name them instead of calling them missing.
$externalRoots = @('third_party\aom', 'third_party\aom_dsp', 'third_party\dav1d')

# Candidate forms for a cited name, in priority order:
#   a) the path taken literally relative to the repo root (docs cite
#      `src/l6_pipeline/tests/test_pipeline.cpp` and `Codec/av1_structs.h`)
#   b) joined onto each search root
#   c) with a leading `Codec/` or `C_DEFAULT/` stripped, joined again
#      (the search roots already sit inside Source/Lib)
#   d) a recursive basename search under src/ and third_party/ - the port's
#      layer dirs and the vendored aom_dsp subdirs are not flat
function Resolve-Cited([string]$name) {
    $p = Join-Path $root $name
    if (Test-Path -LiteralPath $p -PathType Leaf) { return $p }
    foreach ($r in $searchRoots) {
        $p = Join-Path $root (Join-Path $r $name)
        if (Test-Path -LiteralPath $p -PathType Leaf) { return $p }
    }
    $stripped = $name -replace '^(Codec|C_DEFAULT|Globals)[\\/]', ''
    if ($stripped -ne $name) {
        foreach ($r in $searchRoots) {
            $p = Join-Path $root (Join-Path $r $stripped)
            if (Test-Path -LiteralPath $p -PathType Leaf) { return $p }
        }
    }
    # SVT-AV1's own root is searched before third_party\aom so that a cited
    # name resolves against the PINNED vendored tree (REFERENCE PINNING), not
    # against libaom's copy. The two overlap: SVT-AV1 carries
    # third_party/aom_dsp/src/bitreader.c and libaom carries
    # aom_dsp/bitreader.c, and the docs cite the `src/` path, i.e. SVT's.
    foreach ($r in @('src', 'third_party\SVT-AV1\Source', 'third_party\SVT-AV1', 'third_party\aom_dsp', 'third_party\aom')) {
        $base = Join-Path $root $r
        if (-not (Test-Path -LiteralPath $base)) { continue }
        # A cited name may carry a partial directory prefix (e.g.
        # `SVT-AV1/CMakeLists.txt`, `Codec/av1_structs.h`). Match on the path
        # SUFFIX, not the bare basename - a basename-only match would resolve
        # `SVT-AV1/CMakeLists.txt` to whichever src-layer CMakeLists.txt the
        # glob happened to visit first, which is a false OK.
        $suffix = $stripped -replace '\\', '/'
        $hit = Get-ChildItem -LiteralPath $base -Recurse -File -ErrorAction SilentlyContinue |
               Where-Object {
                   $rel = $_.FullName.Substring($root.Length + 1) -replace '\\', '/'
                   $rel.EndsWith('/' + $suffix) -or $rel -eq $suffix
               } |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
        # Last resort: a name with no directory component at all.
        $leaf = [IO.Path]::GetFileName($stripped)
        if ($suffix -eq $leaf) {
            $hit2 = Get-ChildItem -LiteralPath $base -Recurse -File -Filter $leaf -ErrorAction SilentlyContinue |
                    Select-Object -First 1
            if ($hit2) { return $hit2.FullName }
        }
    }
    return $null
}

function Get-Lines([string]$path) {
    # [IO.File]::ReadAllLines keeps empty lines (Measure-Object/Get-Content
    # round-trips are avoided so line numbers are physical, 1-based).
    return [IO.File]::ReadAllLines($path)
}

# ------------------------------------------------------------------ main
$citeRe = [regex]'([A-Za-z0-9_./\\-]+\.(?:c|h|cpp|cc|md|ps1|txt|inc|asm))\s*:\s*(\d+)(?:\s*-\s*(\d+))?'
$total = 0; $ok = 0; $oob = 0; $unres = 0
$unresolvedNames = New-Object 'System.Collections.Generic.HashSet[string]'
$oobList = New-Object System.Collections.ArrayList

foreach ($d in $docs) {
    $relDoc = $d.FullName.Substring($root.Length + 1)
    $lines = Get-Lines $d.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $text = $lines[$i]
        foreach ($m in $citeRe.Matches($text)) {
            $total++
            $name = $m.Groups[1].Value
            $n1 = [int]$m.Groups[2].Value
            $n2 = if ($m.Groups[3].Success -and $m.Groups[3].Value -ne '') { [int]$m.Groups[3].Value } else { $n1 }
            $path = Resolve-Cited $name

            if (-not $path) {
                $unres++
                [void]$unresolvedNames.Add($name)
                if (-not $Quiet -and -not $Unresolved) {
                    '{0}:{1}: UNRESOLVED {2}:{3}' -f $relDoc, ($i + 1), $name, $n1
                }
                continue
            }

            $tgt = Get-Lines $path
            $rel = $path.Substring($root.Length + 1)
            if ($n1 -lt 1 -or $n1 -gt $tgt.Count) {
                $oob++
                [void]$oobList.Add(('{0}:{1} -> {2}:{3} (file has {4} lines)' -f $relDoc, ($i + 1), $rel, $n1, $tgt.Count))
                if (-not $Quiet -and -not $Unresolved) {
                    '{0}:{1}: OUT-OF-RANGE {2}:{3} (file has {4} lines)' -f $relDoc, ($i + 1), $rel, $n1, $tgt.Count
                }
                continue
            }
            $ok++

            if (-not $Quiet -and -not $Unresolved) {
                ''
                '{0}:{1}  ==>  {2}:{3}{4}' -f $relDoc, ($i + 1), $rel, $n1, $(if ($n2 -ne $n1) { "-$n2" } else { '' })
                '    claim: {0}' -f $text.Trim()
                $lo = [Math]::Max(0, $n1 - 1 - $Context)
                $hi = [Math]::Min($tgt.Count - 1, $n2 - 1 + $Context)
                for ($k = $lo; $k -le $hi; $k++) {
                    '    {0,6} | {1}{2}' -f ($k + 1), $(if ($k + 1 -eq $n1 -or ($k + 1 -ge $n1 -and $k + 1 -le $n2)) { '>' } else { ' ' }), $tgt[$k]
                }
            }
        }
    }
}

''
'=' * 72
'SCANNED DOC SET - a citation count is only meaningful against the exact set'
'(any doc edit changes it; the set is part of the measurement, not context)'
'=' * 72
$docBytes = 0
foreach ($d in $docs) {
    $relDoc = $d.FullName.Substring($root.Length + 1)
    $lc = ([IO.File]::ReadAllLines($d.FullName)).Count
    $bc = $d.Length
    $docBytes += $bc
    '{0,-34} {1,6} lines  {2,8} B' -f $relDoc, $lc, $bc
}
# A content hash of the whole set, so two runs can be compared for identity
# rather than for a bare number that may or may not have moved.
$sha = [Security.Cryptography.SHA256]::Create()
$setBytes = ''
foreach ($d in $docs) { $setBytes += $d.FullName.Substring($root.Length + 1) + "`n" + [IO.File]::ReadAllText($d.FullName) }
$setHash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($setBytes)))).Replace('-', '').Substring(0, 16)
'doc set: {0} files, {1} B, sha256:{2}' -f $docs.Count, $docBytes, $setHash
''
'=' * 72
'CITATIONS: {0} total | {1} resolved in-tree | {2} out-of-range | {3} unresolved' -f $total, $ok, $oob, $unres
if ($oobList.Count) {
    ''
    'OUT-OF-RANGE ({0}):' -f $oobList.Count
    $oobList | ForEach-Object { '  ' + $_ }
}
if ($unresolvedNames.Count) {
    ''
    'UNRESOLVED ({0} distinct) - external decoder internals are NOT vendored;' -f $unresolvedNames.Count
    'check these against the external aom / dav1d checkouts by hand:'
    $unresolvedNames | Sort-Object | ForEach-Object { '  ' + $_ }
}
