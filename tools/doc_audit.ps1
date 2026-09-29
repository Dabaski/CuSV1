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
    [int]    $MaxReport = 200,
    [switch] $SelfTest
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
# a byte count on a line that names the gate file BY NAME.
#
# FIXED HERE, TWO INDEPENDENT CAUSES, and neither was the one the record
# guessed. The recorded diagnosis was a two-condition conjunction - the line
# must name the gate file AND the unit must be the abbreviation B - and
# relaxing the unit alone would have fixed nothing, because the number
# pattern could not match a six-digit comma-grouped value at all:
#   {2,3}  required TWO or more ",ddd" groups, so "884,980" (884 + ONE
#          ",980") never matched. Every gate figure in this project's prose
#          is written with one comma group, so the branch was dead on
#          arrival. Now {1,3}.
#   B\b     requires a word boundary after B, and in "bytes" the B is
#          followed by "y" - both word characters, so no boundary, no match.
#          "bytes" is accepted now, as the record recommended.
#
# Measured consequence of the fix: the check had NEVER fired, on any file, for
# any figure - and it reported "0 mismatches" doing so, which is
# indistinguishable in the output from a clean run. A check that cannot fail
# is worse than a check that fails narrowly, because it reports success.
#
# AND IT FOUND NOTHING WHEN FIXED, WHICH IS THE HONEST RESULT, not a
# disappointment. I expected the revived check to light up docs/todo.md:540 and
# AGENTS.md:199, both of which carry 884,953 while I believed the gate to be
# 884,980. Re-measuring showed the gate IS 884,953 and those two lines are
# CORRECT: commit 5ce0224 landed the CS7 fix and its regen moved the gate from
# 884,980 to 884,953, so my 884,980 was the stale figure, not theirs. The
# near-miss is the whole lesson of this instrument class: a number carried
# forward from an earlier commit reads exactly like a number measured at this
# one, and only re-measuring separated them. Recorded here because the false
# claim was written first and the measurement corrected it, and because "the
# revived check found 2 defects" would have been a better story than the truth.
#
# The self-test below is therefore the real evidence, not the tree scan: it
# proves the pattern detects what it must, independent of whether the tree
# currently happens to contain a wrong figure.
$gateByteRe = [regex]"(\d{1,3}(?:,\d{3}){1,3}|\d{6,9})\s*(?:B\b|bytes?\b)"

function Get-GateByteClaims {
    # Returns every gate byte claim this pattern detects in a block of lines.
    # Extracted so -SelfTest exercises the SAME code path the real scan uses,
    # rather than a copy of the pattern that could drift from it.
    param([string[]] $Lines, [string] $CtxRe, [regex] $NumRe)
    $out = @()
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        $t = $Lines[$i]
        if ($t -notmatch $CtxRe) { continue }
        foreach ($m in $NumRe.Matches($t)) {
            $out += [pscustomobject]@{
                Line = $i + 1
                Num  = $m.Groups[1].Value.Replace(',', '')
                Text = $t.Trim()
            }
        }
    }
    return $out
}

if ($SelfTest) {
    # THE RED FOR THE DEAD BYTE CHECK, as a durable self-check rather than a
    # throwaway: it asserts DETECTION COUNT, not a verdict, because the defect
    # was that the claim was never detected at all - the tool reported "0
    # mismatches" over a tree with stale gate figures, which looks exactly
    # like a clean run.
    #
    # Case D is the one that must stay at 0. It is the whole reason condition
    # (i) stays on the SAME LINE: that line is about out.txt and the UTF-16
    # redirect, and it names no gate file. Widening the context to the whole
    # document would light it up, and would also light up AGENTS.md's
    # historical 884,953 / 884,961 figures and its 1,770,750 B redirect
    # measurement - five correct claims about the past and about a different
    # file, which is the cries-wolf failure this instrument is built to avoid.
    $cases = @(
        @{ name = 'A  gate file named + word "bytes" + wrong number'
           lines = @('The committed expected_primitives.txt is 884,961 bytes at this commit.')
           expect = 1 }
        @{ name = 'B  gate file named + abbreviation B + wrong number'
           lines = @('The committed expected_primitives.txt is 884,961 B at this commit.')
           expect = 1 }
        @{ name = 'C  gate file named + word "bytes" + correct number'
           lines = @('The committed expected_primitives.txt is 884,980 bytes at this commit.')
           expect = 1 }
        @{ name = 'D  out.txt / UTF-16 redirect, NO gate file named'
           lines = @('out.txt measures 1,770,750 B against a committed 884,953 B file.')
           expect = 0 }
    )
    $fail = 0
    Write-Host ''
    Write-Host 'doc_audit.ps1 -SelfTest: gate byte-claim detection' -ForegroundColor Cyan
    foreach ($c in $cases) {
        $got = @(Get-GateByteClaims -Lines $c.lines -CtxRe $gateCtx -NumRe $gateByteRe)
        $ok = ($got.Count -eq $c.expect)
        if (-not $ok) { $fail++ }
        $detail = if ($got.Count) { $got[0].Num } else { '(none)' }
        Write-Host ('  {0}  expect={1} got={2} [{3}]  {4}' -f
            $(if ($ok) { 'PASS' } else { 'FAIL' }), $c.expect, $got.Count, $detail, $c.name)
    }
    Write-Host ''
    if ($fail -gt 0) {
        Write-Host ("SELF-TEST FAILED: {0} of {1} case(s). The gate byte check is not" -f $fail, $cases.Count) -ForegroundColor Red
        Write-Host 'detecting claims it is supposed to detect. Reported as RED, not fixed here.' -ForegroundColor Red
        Write-Host ''
        # still exit 0: this tool is advisory and fails nothing, including
        # its own self-test. A self-test that can fail a build would be a
        # gate, and this is deliberately not one.
        exit 0
    }
    Write-Host 'SELF-TEST PASSED.' -ForegroundColor Green
    Write-Host ''
    exit 0
}

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
        # the byte claims go through the same helper -SelfTest exercises, so
        # the tested pattern and the running pattern cannot drift apart
        foreach ($c in (Get-GateByteClaims -Lines @($text) -CtxRe '(?i)expected_primitives|primitives file|golden-gate|golden gate' -NumRe $gateByteRe)) {
            if ([int64]$c.Num -ge 100000) {
                Add-Finding 'gate' $doc ($i + 1) $text $c.Num ([string]$gateBytes)
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
