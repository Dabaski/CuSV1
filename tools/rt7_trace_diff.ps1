param(
    [int]$S = 8
)
# RT7-a cross-decoder symbol diff: our read twin vs the throwaway dav1d
# instrument (TD4S lines). Compares the decoded VALUE per symbol, which is
# the structural question (does the real decoder read what we wrote?); the cdf
# row is reported only as supporting detail, since the trailing count field
# legitimately differs when the adaptation history differs.
# exit 0 = every symbol agrees; 1 = a value/row divergence; 2 = instrument
# problem (empty trace / no alignment).
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
$rows = Join-Path $env:TEMP 'opencode\rt7_rows.txt'
$dav = Join-Path $env:TEMP 'opencode\rt7_dav1d.txt'
$artifact = Join-Path $repo "src\l8_bitstream\tests\goldens\structural_keyframe_color$S.obu"
$davExe = Join-Path $env:TEMP 'dav1d_td4\build_td4\tools\dav1d.exe'

# --- our side ---
$env:RT7_S = "$S"
cmd /c "build\tools\golden_gen\Release\golden_primitives.exe > NUL 2> `"$rows`"" | Out-Null
$ours = @()
foreach ($l in Get-Content $rows) {
    if ($l -notlike 'RS *') { continue }
    $t = $l -split '\s+'
    $ns = [int]($t[4] -replace 'ns=', '')
    $v = [int]($t[5] -replace 'v=', '')
    $mi = [int]($t[2] -replace 'mi=', '')
    $pl = ($t[3] -replace 'pl=', '')
    $ours += , @(($ns), ($v), ($mi), ($pl), (($t[7..(6 + $ns)]) -join ' '))
}

# --- decoder side ---
cmd /c "`"$davExe`" -q --muxer null -o NUL -i `"$artifact`" 2> `"$dav`"" | Out-Null
$theirs = @()
foreach ($l in Get-Content -LiteralPath $dav) {
    if ($l -notlike 'TD4S*') { continue }
    $t = $l -split '\s+'
    $ns = [int]($t[2] -replace 'ns=', '') + 1
    $v = [int]($t[3] -replace 'got=', '')
    $theirs += , @(($ns), ($v), (-1), ('-'), (($t[5..(4 + $ns)]) -join ' '))
}

Write-Host "S=$S ours=$($ours.Count) dav1d=$($theirs.Count)"
if ($ours.Count -eq 0 -or $theirs.Count -eq 0) { Write-Host 'FAIL: empty trace'; exit 2 }

# align: the first (ns,v) that appears in dav1d
$off = -1
for ($o = 0; $o -lt $theirs.Count; $o++) {
    if ($theirs[$o][0] -eq $ours[0][0] -and $theirs[$o][1] -eq $ours[0][1]) { $off = $o; break }
}
if ($off -lt 0) { Write-Host "FAIL: no alignment; our first (ns=$($ours[0][0]) v=$($ours[0][1]))"; exit 2 }
Write-Host "tile offset=$off"

$rowOnly = 0
for ($i = 0; $i -lt $ours.Count; $i++) {
    $j = $off + $i
    if ($j -ge $theirs.Count) {
        Write-Host "RUN-OUT: decoder exhausted at our seq=$i (we write MORE than it reads)"
        exit 1
    }
    if ($theirs[$j][0] -ne $ours[$i][0] -or $theirs[$j][1] -ne $ours[$i][1]) {
        Write-Host "VALUE DIVERGE at our seq=$i (dav1d seq=$j)"
        Write-Host "  ours   ns=$($ours[$i][0]) v=$($ours[$i][1]) at mi=$($ours[$i][2]) plane=$($ours[$i][3])"
        Write-Host "  dav1d  ns=$($theirs[$j][0]) v=$($theirs[$j][1])"
        exit 1
    }
    if ($theirs[$j][2] -ne $ours[$i][2]) { $rowOnly++ }
}
if ($ours.Count -lt $theirs.Count - $off) {
    Write-Host "SHORT WRITE: decoder reads $($theirs.Count - $off) symbols, we write $($ours.Count)"
    exit 1
}
Write-Host "MATCH: all $($ours.Count) values agree (offset=$off); row-only diffs=$rowOnly"
exit 0
