param(
    [int]$Color = 8,
    [ValidateSet("Y", "U", "V")][string]$Plane = "Y"
)
# RT8 / CS7: the BLOCK-GRANULAR divergence map, per plane. The standing check
# reports a plane total and a first-divergent byte; this reports WHICH LEAVES
# diverge, bucketed by leaf index, so the pattern reads as either compounding
# recon evolution (a contiguous run in decode order) or an independent
# per-block defect (scattered). Diagnostic only - it asserts nothing and
# changes no product code.
#
# Leaf geometry mirrors the CS4 walk: lumaB = 2*S for 4:2:0, grid = 64/lumaB,
# bidx = by*grid + bx, and the Z-order visit sequence is the Morton curve of
# the leaf grid (the RT6 tree order). CS7 step 1 extends this to U and V: the
# chroma leaf grid is 32/S per side, and the first divergent CHROMA leaf is
# found BY THE MAP - never assumed to be luma's bidx 2, since that assumption
# is exactly the error class this session has logged three times.
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

# the plane under test
$lumaB = 2 * $Color          # 4 -> 8, 8 -> 16, 16 -> 32, 32 -> 64
$isChroma = ($Plane -ne "Y")
$blk = if ($isChroma) { $Color } else { $lumaB }
$dim = if ($isChroma) { 32 } else { 64 }
$g = if ($isChroma) { 32 / $Color } else { 64 / $lumaB }
$pixels = $dim * $dim
$off = if ($Plane -eq "U") { 4096 } elseif ($Plane -eq "V") { 5120 } else { 0 }
$tag = "ecs4${Color}_recon_" + $Plane.ToLower()

$rec = (Get-Content $gatePath | Select-String ("^" + $tag + " ")).Line -split " " | Select-Object -Skip 1
if ($rec.Count -ne $pixels) { Write-Output ("FAIL: gate $tag has " + $rec.Count + " values (want " + $pixels + ")"); exit 4 }
$want = [byte[]][int[]]$rec

# the Z-order (Morton) visit sequence over THIS plane's leaf grid
$zord = New-Object System.Collections.Generic.List[int]
function Add-Morton([int]$x0, [int]$y0, [int]$n) {
    if ($n -eq 1) { $script:zord.Add($y0 * $g + $x0); return }
    $h = $n / 2
    Add-Morton $x0 $y0 $h                        # TL
    Add-Morton ($x0 + $h) $y0 $h                 # TR
    Add-Morton $x0 ($y0 + $h) $h                 # BL
    Add-Morton ($x0 + $h) ($y0 + $h) $h          # BR
}
Add-Morton 0 0 $g
$zpos = @{}
for ($i = 0; $i -lt $zord.Count; $i++) { $zpos[$zord[$i]] = $i }

$leafDiff = @{}
$leafTotal = @{}
$firstAt = -1
for ($i = 0; $i -lt $pixels; $i++) {
    # [math]::Floor, NOT [int] - PowerShell's [int] cast ROUNDS to nearest
    # rather than truncating, which silently mis-buckets every index.
    $r = [int][math]::Floor($i / $dim); $c = $i % $dim
    $by = [int][math]::Floor($r / $blk); $bx = [int][math]::Floor($c / $blk)
    $bidx = $by * $g + $bx
    if (-not $leafTotal.ContainsKey($bidx)) { $leafTotal[$bidx] = 0; $leafDiff[$bidx] = 0 }
    $leafTotal[$bidx]++
    if ($got[$off + $i] -ne $want[$i]) {
        $leafDiff[$bidx]++
        if ($firstAt -lt 0) {
            $firstAt = $i
            Write-Output ("plane $Plane first divergence: byte $i = row $r col $c -> bidx $bidx (by=$by bx=$bx, zpos " + $zpos[$bidx] + ")")
        }
    }
}

Write-Output ""
Write-Output ("plane=$Plane blk=$blk grid=$g leaves=" + $g * $g + " pixels=$pixels gate=$tag")
Write-Output ("bidx  zpos  by bx  diff/total")
$diffSum = 0
foreach ($b in ($zord | Sort-Object { $zpos[$_] })) {
    $d = $leafDiff[$b]
    $diffSum += $d
    if ($d -gt 0) {
        $bb = [int][math]::Floor($b / $g)
        Write-Output ("{0,4}  {1,4}  {2,2} {3,2}  {4}/{5}" -f $b, $zpos[$b], $bb, ($b % $g), $d, $leafTotal[$b])
    }
}
Write-Output ""
Write-Output ("total divergent bytes: $diffSum / $pixels")
$zDiv = @($zord | Sort-Object { $zpos[$_] } | Where-Object { $leafDiff[$_] -gt 0 } | ForEach-Object { $zpos[$_] })
$zMatch = @($zord | Sort-Object { $zpos[$_] } | Where-Object { $leafDiff[$_] -eq 0 } | ForEach-Object { $zpos[$_] })
if ($zDiv.Count -eq 0) {
    Write-Output "PLANE CONFORMS: 0 divergent leaves"
} else {
    $firstDivZ = $zDiv[0]
    Write-Output ("divergent leaves: " + $zDiv.Count + " of " + $g * $g + "; first divergent zpos: " + $firstDivZ +
                  " (bidx " + $zord[$firstDivZ] + "); matching zpos: " + ($zMatch -join ","))
    $isPrefixClean = ($zMatch.Count -eq 0) -or (($zMatch | Measure-Object -Maximum).Maximum -lt $firstDivZ)
    if ($isPrefixClean) {
        Write-Output ("PATTERN: CASCADE - zpos 0.." + ($firstDivZ - 1) + " match, divergence from zpos " + $firstDivZ + " onward")
    } else {
        Write-Output "PATTERN: SCATTERED (divergent leaves are not a decode-order suffix)"
    }
}
