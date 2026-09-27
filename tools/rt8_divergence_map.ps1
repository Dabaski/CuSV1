param(
    [int]$Color = 8
)
# RT8: the BLOCK-GRANULAR divergence map. The standing check reports a plane
# total and a first-divergent byte; this reports WHICH LEAVES diverge, bucketed
# by leaf index, so the pattern can be read as either compounding recon
# evolution (a contiguous run in decode order) or an independent per-block
# defect (scattered). Diagnostic only - it asserts nothing and changes no
# product code.
#
# Leaf geometry mirrors the CS4 walk: lumaB = 128/S, grid = 64/lumaB,
# bidx = by*grid + bx, and the Z-order visit sequence is the Morton curve of
# the leaf grid (the RT6 tree order). "scan order" below is Z-order position.
$ErrorActionPreference = "Continue"
$repo = Split-Path $PSScriptRoot -Parent
$gatePath = Join-Path $repo "tools\golden_gen\expected_primitives.txt"
$temp = Join-Path $env:TEMP "rt8map"
New-Item -ItemType Directory $temp -Force | Out-Null

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { Write-Output "FAIL: ffmpeg not on PATH"; exit 2 }
if ($Color -notin 4, 8, 16, 32) { Write-Output "FAIL: bad geometry"; exit 1 }

$artifact = Join-Path $repo "src\l8_bitstream\tests\goldens\structural_keyframe_color$Color.obu"
$raw = Join-Path $temp "c$Color.raw"
& $ffmpeg -hide_banner -loglevel error -y -i $artifact -f rawvideo -pix_fmt yuv420p $raw 2>$null
if ($LASTEXITCODE -ne 0) { Write-Output "FAIL: ffmpeg decode exit $LASTEXITCODE"; exit 3 }
$got = [System.IO.File]::ReadAllBytes($raw)

$lumaB = 128 / $Color          # 4 -> 32, 8 -> 16, 16 -> 8? no: see note
# lumaB is the LUMA block edge; the color walk's S is the CHROMA edge and
# lumaB = 2*S for 4:2:0. The CS4 sizes are S = 4/8/16/32 with lumaB = 8/16/32/64.
$lumaB = 2 * $Color
$grid = 64 / $lumaB

function Get-GateVec($tag, $count) {
    $rec = (Get-Content $gatePath | Select-String ("^" + $tag + " ")).Line -split " " |
        Select-Object -Skip 1
    if ($rec.Count -ne $count) { throw "gate $tag has $($rec.Count) values (want $count)" }
    return [byte[]][int[]]$rec
}

# the Z-order (Morton) visit sequence over the leaf grid - the RT6 tree order
$zord = New-Object System.Collections.Generic.List[int]
function Add-Morton([int]$x0, [int]$y0, [int]$n) {
    if ($n -eq 1) { $script:zord.Add($y0 * $grid + $x0); return }
    $h = $n / 2
    Add-Morton $x0 $y0 $h            # TL
    Add-Morton ($x0 + $h) $y0 $h     # TR
    Add-Morton $x0 ($y0 + $h) $h     # BL
    Add-Morton ($x0 + $h) ($y0 + $h) $h  # BR
}
Add-Morton 0 0 $grid
$zpos = @{}
for ($i = 0; $i -lt $zord.Count; $i++) { $zpos[$zord[$i]] = $i }

# ---- luma map ----
$wantY = Get-GateVec "ecs4${Color}_recon_y" 4096
$leafDiff = @{}
$leafTotal = @{}
$firstAt = -1
for ($i = 0; $i -lt 4096; $i++) {
    # [math]::Floor, NOT [int] - PowerShell's [int] cast ROUNDS to nearest
    # rather than truncating, which silently mis-buckets every index.
    $r = [int][math]::Floor($i / 64); $c = $i % 64
    $by = [int][math]::Floor($r / $lumaB); $bx = [int][math]::Floor($c / $lumaB)
    $bidx = $by * $grid + $bx
    if (-not $leafTotal.ContainsKey($bidx)) { $leafTotal[$bidx] = 0; $leafDiff[$bidx] = 0 }
    $leafTotal[$bidx]++
    if ($got[$i] -ne $wantY[$i]) {
        $leafDiff[$bidx]++
        if ($firstAt -lt 0) {
            $firstAt = $i
            Write-Output ("first luma divergence: byte $i = row " + $r + " col " + $c +
                          " -> bidx $bidx (by=$by bx=$bx, zpos " + $zpos[$bidx] + ")")
        }
    }
}

Write-Output ""
Write-Output ("lumaB=$lumaB grid=$grid leaves=" + $grid * $grid)
Write-Output ("bidx  zpos  by bx  diff/total")
$zposOrder = $zord | Sort-Object { $zpos[$_] }
$prevZ = -2; $runs = 0; $maxRun = 0; $run = 0
foreach ($b in $zposOrder) {
    $d = $leafDiff[$b]
    if ($d -gt 0) {
        $z = $zpos[$b]
        $bb = [int][math]::Floor($b / $grid); $bx2 = $b % $grid
        Write-Output ("{0,4}  {1,4}  {2,2} {3,2}  {4}/{5}" -f $b, $z, $bb, $bx2, $d, $leafTotal[$b])
        if ($z -eq $prevZ + 1) { $run++ } else { if ($run -gt 0) { $runs++ }; $run = 1 }
        if ($run -gt $maxRun) { $maxRun = $run }
        $prevZ = $z
    }
}
if ($run -gt 0) { $runs++ }
$nDiv = @($leafDiff.Keys | Where-Object { $leafDiff[$_] -gt 0 }).Count
Write-Output ""
Write-Output ("divergent leaves: $nDiv of " + $grid * $grid +
              "; contiguous z-order runs: $runs; longest run: $maxRun")
$diffSum = 0; foreach ($k in $leafDiff.Keys) { $diffSum += $leafDiff[$k] }
Write-Output ("total divergent luma bytes: $diffSum")
# The shape that matters is WHERE the cascade starts, not how many runs: a
# prefix of decode order matching followed by everything diverging is a
# compounding cascade rooted at the first divergent leaf.
$zDiv = @($zord | Sort-Object { $zpos[$_] } | Where-Object { $leafDiff[$_] -gt 0 } | ForEach-Object { $zpos[$_] })
$zMatch = @($zord | Sort-Object { $zpos[$_] } | Where-Object { $leafDiff[$_] -eq 0 } | ForEach-Object { $zpos[$_] })
$firstDivZ = if ($zDiv.Count) { $zDiv[0] } else { -1 }
$isPrefixClean = ($zMatch.Count -eq 0) -or (($zMatch | Measure-Object -Maximum).Maximum -lt $firstDivZ)
$isSuffixAll = $true
foreach ($z in $zDiv) { if ($z -lt $firstDivZ) { $isSuffixAll = $false } }
Write-Output ("first divergent zpos: $firstDivZ (bidx " + $zord[$firstDivZ] +
              "); matching zpos: " + ($zMatch -join ","))
if ($isPrefixClean -and $firstDivZ -ge 0) {
    Write-Output ("PATTERN: CASCADE - zpos 0.." + ($firstDivZ - 1) + " match, then divergence from zpos " +
                  "$firstDivZ onward (compounding recon evolution rooted at that leaf)")
} else {
    Write-Output "PATTERN: SCATTERED (divergent leaves are not a decode-order suffix)"
}
