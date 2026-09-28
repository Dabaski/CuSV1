# doc_audit.ps1 - an ADVISORY audit of numeric claims in doc prose.
#
# WHAT THIS IS FOR
#   Four defects in this project's recent arc were wrong NUMBERS in prose:
#   an artifact byte size, a per-geometry table row, a bit width in a
#   comment, and a uvB read taken out of a bad table. None of them were
#   caught by the test suite, because the suite tests CODE, and a wrong
#   number in a document is not a code path. tools/cite_check.ps1 catches
#   wrong CITATIONS (file:line pointing at the wrong bytes). Nothing caught
#   wrong COUNTS, so this is the third instrument: it extracts candidate
#   numbers from the tracked markdown and prints each one beside the value
#   the instrument actually measures, for a human to adjudicate.
#
# ADVISORY ONLY. BY DESIGN. THIS IS NOT A GATE.
#   - It always exits 0. It cannot fail a build or a commit.
#   - It reports CANDIDATES, not verdicts. A regex over prose produces false
#     positives; a human decides which candidates are real claims.
#   - It is deliberately NOT wired into ctest, the GATE, or any pre-commit
#     hook. An instrument that cries wolf gets switched off, which is the
#     same fate that befalls git diff --check on the CRLF .inc files in this
#     tree. A switched-off instrument is worse than no instrument, because it
#     still looks like coverage.
#
# USAGE
#   powershell -File tools\doc_audit.ps1
#   powershell -File tools\doc_audit.ps1 -Root .
#   powershell -File tools\doc_audit.ps1 -Docs docs\*.md,README.md
#
# CATEGORIES
#   size      a byte count attributed to a committed .obu artifact
#   gate      a line count or byte count attributed to expected_primitives.txt
#   coverage  a portable-C coverage percentage
#   nonascii  a non-ASCII byte in a tracked text file (ASCII-only authoring)

[CmdletBinding()]
param(
    [string] $Root = '.',
    [string] $Docs = '',
    [int]    $MaxReport = 200
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $Root

# ---------------------------------------------------------------------------
# The measured values. Everything here is MEASURED AT RUN TIME, never
# transcribed. A doc-audit that hardcodes its own reference values has
# reproduced the exact defect class it exists to catch.
# ---------------------------------------------------------------------------

$findings = New-Object System.Collections.Generic.List[object]

function Add-Finding {
    param([string] $Category, [string] $File, [int] $Line, [string] $Text, [string] $Claim, [string] $Actual)
    $script:findings.Add([pscustomobject]@{
        Category = $Category
        File     = $File
        Line     = $Line
        Text     = $Text.Trim()
        Claim    = $Claim
        Actual   = $Actual
        Verdict  = if ($Claim -eq $Actual) { 'ok' } else { 'MISMATCH' }
    })
}

# --- artifacts: measure every committed .obu on disk ------------------------
$artifacts = @{}
Get-ChildItem -Recurse -Filter '*.obu' -File | ForEach-Object {
    $name = $_.BaseName                      # e.g. structural_keyframe_color4
    $artifacts[$name] = $_.Length
    # shorthand alias -> the long name, so "color4" and
    # "structural_keyframe_color4.obu" both resolve
    if ($name -match '_([a-z0-9]+)$') { $artifacts[$Matches[1]] = $_.Length }
    # the geometry alias: structural_keyframe_d16 -> d16
    if ($name -match '_(d[0-9]+)$') { $artifacts[$Matches[1]] = $_.Length }
}

# --- the gate: lines and bytes ----------------------------------------------
$gatePath = 'tools/golden_gen/expected_primitives.txt'
$gateLines = 0
$gateBytes = 0
if (Test-Path -LiteralPath $gatePath) {
    $gateBytes = ([IO.File]::ReadAllBytes($gatePath)).Length
    $gateLines = [IO.File]::ReadAllLines($gatePath).Count
}

# --- coverage: the advisory snapshot, if it has been saved ------------------
$coverage = $null
$covPath = 'tools/coverage_baseline.txt'
if (Test-Path -LiteralPath $covPath) {
    $m = Select-String -Path $covPath -Pattern '^\s*TOTAL\b.*?([0-9]+\.[0-9]+)\s*%' -ErrorAction SilentlyContinue |
         Select-Object -First 1
    if (-not $m) {
        $m = Select-String -Path $covPath -Pattern '([0-9]+\.[0-9]+)\s*%' -ErrorAction SilentlyContinue |
             Select-Object -First 1
    }
    if ($m -and $m.Matches.Count -gt 0) { $coverage = $m.Matches[0].Groups[1].Value }
}

# ---------------------------------------------------------------------------
# Which docs to scan. Default: every tracked markdown.
# ---------------------------------------------------------------------------
$docList = @()
if ($Docs) {
    $docList = $Docs -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
} else {
    $docList = & git ls-files '*.md' 2>$null
    if (-not $docList) {
        $docList = Get-ChildItem -Recurse -Filter '*.md' -File |
                   ForEach-Object { $_.FullName.Substring((Get-Location).Path.Length + 1) }
    }
}
# The reference tree is not ours to audit, and its numbers are not claims.
$docList = $docList | Where-Object { $_ -notmatch '^third_party[\\/]' }

Write-Host ''
Write-Host 'doc_audit.ps1 - ADVISORY ONLY. Always exits 0. Never a gate.' -ForegroundColor Cyan
Write-Host ("scanning {0} markdown file(s); third_party excluded" -f @($docList).Count)
Write-Host ''

# ---------------------------------------------------------------------------
# CATEGORY: size - a byte count attributed to a committed artifact
# ---------------------------------------------------------------------------
# Matches either order:
#   "color4 201B"   "d4 30B"   "d64 441B"
#   "201B color4"   "44 B d16"
# 'B' must be followed by a boundary so "165B" does not match inside a token.
$aliasPattern = [regex]::Escape(($artifacts.Keys | Sort-Object -Unique) -join '|')
$sizeFwd = [regex]"($aliasPattern)\D{0,12}?(\d{1,6})\s*B\b"
$sizeRev = [regex]"(\d{1,6})\s*B\b\D{0,12}?($aliasPattern)\b"

foreach ($doc in $docList) {
    if (-not (Test-Path -LiteralPath $doc)) { continue }
    $lines = [IO.File]::ReadAllLines($doc)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $text = $lines[$i]
        if ($text -match '^\s*(#|//|\*)' -and $text -notmatch '\S') { continue }
        foreach ($re in @($sizeFwd, $sizeRev)) {
            foreach ($m in $re.Matches($text)) {
                $alias = $m.Groups[$re.GetGroup(1) -eq 'x'].Value
                # recover which side is the alias by checking both groups
                $g1 = $m.Groups[1].Value; $g2 = $m.Groups[2].Value
                $alias = if ($artifacts.ContainsKey($g1)) { $g1 } else { $g2 }
                $num   = if ($artifacts.ContainsKey($g1)) { $g2 } else { $g1 }
                if ($artifacts.ContainsKey($alias)) {
                    Add-Finding 'size' $doc ($i + 1) $text $num ([string]$artifacts[$alias])
                }
            }
        }
    }
}

# ---------------------------------------------------------------------------
# CATEGORY: gate - lines / bytes attributed to expected_primitives.txt
# ---------------------------------------------------------------------------
# NARROWED DELIBERATELY, and the reason is the instrument's own output.
#
# The first version of this check matched a bare number near any word like
# "gate", and on the tree as it stands that produced SIX candidates and ZERO
# real defects. Four of the six were legible on inspection:
#   - "The pre-RT10b value was 884961 B"  a CORRECT historical value, and the
#     most dangerous kind of false positive, because a reader who "fixed" it
#     would have destroyed a true statement about the past.
#   - "a moved c16 line"                  "c16" + "line", not a count at all.
#   - "(27 lines, 362-line gate)"         a REAL number about a historical
#                                          per-set gate, not expected_primitives.
# A category that flags correct history is worse than no category: it trains
# the reader to skim, and a skimmed instrument is a switched-off instrument.
#
# So the gate check now matches exactly ONE unambiguous form: N/N immediately
# followed by "lines", which is how the gate count is actually written
# ("421/421 lines"). Requiring the trailing "lines" is what kills the second
# round of false positives, where a bare N/N on a line containing the word
# "gate" turned out to be a TRANSFORM SIZE - "16/32" and "32/64" are TX
# dimensions, and "32/13" is an alphabet count, and none of them is a gate.
# Eleven of them on the tree as it stands, zero real defects.
#
# The gate BYTE count is matched only on a line that also names the file
# itself, and only for a value above 100000, because a five-digit number
# followed by "B" on a line that says "primitives" is the gate and nothing
# else in this project's prose is.
#
# The blind spots are listed in BLIND SPOTS below rather than papered over
# with a wider regex.

$gateCtx = 'gate|expected_primitives|primitives file|golden-gate|golden gate'
# N/N immediately followed by "lines". Both halves captured.
$slashRe = [regex]"(\d{2,4})\s*/\s*(\d{2,4})\s*lines"
# a large byte count on a line that names the gate file by name
$gateByteRe = [regex]"(\d{1,3}(?:,\d{3}){2,3}|\d{6,9})\s*B\b"

foreach ($doc in $docList) {
    if (-not (Test-Path -LiteralPath $doc)) { continue }
    $lines = [IO.File]::ReadAllLines($doc)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $text = $lines[$i]
        if ($text -notmatch $gateCtx) { continue }
        foreach ($m in $slashRe.Matches($text)) {
            $a = $m.Groups[1].Value
            $b = $m.Groups[2].Value
            # Only the N/N form, and only when BOTH halves equal the measured
            # count do we call it confirmed; if the halves disagree with each
            # other the line is internally inconsistent and is a candidate.
            if ($a -eq $b) {
                Add-Finding 'gate' $doc ($i + 1) $text $a ([string]$gateLines)
            } else {
                $findings.Add([pscustomobject]@{
                    Category = 'gate'; File = $doc; Line = $i + 1
                    Text = $text.Trim(); Claim = "$a/$b (halves disagree)"
                    Actual = [string]$gateLines; Verdict = 'MISMATCH'
                })
            }
        }
        if ($text -match '(?i)expected_primitives|primitives file|golden-gate|golden gate') {
            foreach ($m in $gateByteRe.Matches($text)) {
                $raw = $m.Groups[1].Value
                $norm = $raw.Replace(',', '')
                if ([int64]$norm -ge 100000) {
                    Add-Finding 'gate' $doc ($i + 1) $text $norm ([string]$gateBytes)
                }
            }
        }
    }
}

# ---------------------------------------------------------------------------
# CATEGORY: coverage - a portable-C percentage
# ---------------------------------------------------------------------------
if ($coverage) {
    $covRe = [regex]"(\d{1,2}\.\d)\s*%"
    foreach ($doc in $docList) {
        if (-not (Test-Path -LiteralPath $doc)) { continue }
        $lines = [IO.File]::ReadAllLines($doc)
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $text = $lines[$i]
            if ($text -notmatch '(?i)coverage|portable-C|portable C') { continue }
            foreach ($m in $covRe.Matches($text)) {
                Add-Finding 'coverage' $doc ($i + 1) $text $m.Groups[1].Value $coverage
            }
        }
    }
}

# ---------------------------------------------------------------------------
# CATEGORY: nonascii - the ASCII-only authoring rule, measured not asserted
# ---------------------------------------------------------------------------
$textExt = @('.md', '.c', '.h', '.cpp', '.hpp', '.ps1', '.txt', '.inc', '.py', '.cmake')
$tracked = & git ls-files 2>$null
if ($tracked) {
    foreach ($f in $tracked) {
        $ext = [IO.Path]::GetExtension($f)
        if ($textExt -notcontains $ext) { continue }
        # third_party is the VENDORED reference, pinned and not ours to
        # author. The first version of this check scanned it and produced 45+
        # non-ASCII findings in SVT-AV1's own headers and appendices. Those
        # files are not defects and not ours; reporting them would have made
        # this instrument cry wolf on its very first run.
        if ($f -match '^third_party[\\/]') { continue }
        if (-not (Test-Path -LiteralPath $f)) { continue }
        $bytes = [IO.File]::ReadAllBytes($f)
        $bad = 0
        foreach ($b in $bytes) { if ($b -gt 127) { $bad++ } }
        if ($bad -gt 0) {
            $findings.Add([pscustomobject]@{
                Category = 'nonascii'; File = $f; Line = 0
                Text = "$bad non-ASCII byte(s)"; Claim = '0'; Actual = "$bad"
                Verdict = 'MISMATCH'
            })
        }
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
$mismatch = $findings | Where-Object { $_.Verdict -eq 'MISMATCH' }
$ok       = $findings | Where-Object { $_.Verdict -eq 'ok' }

Write-Host '--- CANDIDATES WITH A MISMATCH (adjudicate by hand) ---'
if ($mismatch) {
    $mismatch | Sort-Object Category, File, Line | Select-Object -First $MaxReport |
        Format-Table -AutoSize -Wrap Category, File, Line, Claim, Actual
} else {
    Write-Host '  (none)'
}

Write-Host ''
Write-Host '--- CONFIRMED CANDIDATES (claim == instrument) ---'
$ok | Group-Object Category | Sort-Object Name | ForEach-Object {
    "  {0,-9} {1} confirmed / {2} in category" -f $_.Name, $_.Count, $_.Count
}

Write-Host ''
Write-Host ("candidates: {0}   mismatches: {1}   confirmed: {2}" -f $findings.Count, @($mismatch).Count, @($ok).Count)
if (@($mismatch).Count -gt 0) {
    Write-Host ''
    Write-Host 'A mismatch is a CANDIDATE, not a verdict. Confirm the claim is about'
    Write-Host 'the instrument named on that line before editing anything: prose'
    Write-Host 'legitimately carries sizes, counts and percentages that no instrument'
    Write-Host 'here measures. Do not chase the regex.'
    Write-Host ''
    Write-Host 'This instrument is ADVISORY. It exits 0 by design and must never be'
    Write-Host 'promoted to a gate.'
}
Write-Host ''

exit 0
