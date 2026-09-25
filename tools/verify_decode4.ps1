# FS5c: the per-geometry decode gate, parameterized from TD5c's decode
# attempt #4. Decodes one committed v3 artifact with the machine's ffmpeg
# (libdav1d) and compares the decoded picture against the generator's
# fs2S_recon gate line - the content-1:1 check per geometry. Exits 0 on
# full success (decode + byte-exact content).
#
# CS5: the COLOR mode - decodes one committed color artifact to yuv420p
# raw, SPLITS the Y/U/V planes (4:2:0: Y = 64x64 = 4096 B at [0, 4096);
# U = 32x32 = 1024 B at [4096, 5120); V = 32x32 = 1024 B at [5120, 6144)),
# and compares each plane SEPARATELY against the gate's ecs4S_recon_y /
# recon_u / recon_v lines. Exits 0 on full success (decode + all three
# planes byte-exact).
#
# Usage:  pwsh tools/verify_decode4.ps1 [-Geometry 4|8|16|32|64]
#   (default: 32 = the committed 47-byte four-16x16-leaves artifact)
#         pwsh tools/verify_decode4.ps1 [-Color 4|8|16|32]
#   (the color mode: the committed color keyframe artifacts)
param(
    [int]$Geometry = -1,
    [int]$Color = -1
)

$ErrorActionPreference = "Continue"
$repo = Split-Path $PSScriptRoot -Parent
$gatePath = Join-Path $repo "tools\golden_gen\expected_primitives.txt"
$temp = Join-Path $env:TEMP "decode4"
New-Item -ItemType Directory $temp -Force | Out-Null

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { Write-Output "FAIL: ffmpeg not on PATH"; exit 2 }

if ($Color -ge 0) {
    if ($Color -notin 4, 8, 16, 32) {
        Write-Output ("FAIL: unknown color geometry " + $Color); exit 1
    }
    $artifactName = "structural_keyframe_color$Color.obu"
    $artifact = Join-Path $repo ("src\l8_bitstream\tests\goldens\" + $artifactName)
    $raw = Join-Path $temp ("decode4_color$Color.raw")
    Write-Output ("color c$Color : artifact " + $artifact + " (" + (Get-Item $artifact).Length + " bytes)")

    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $ffmpeg -hide_banner -loglevel error -y -i $artifact -f rawvideo -pix_fmt yuv420p $raw 2>$null
    $ffExit = $LASTEXITCODE
    $ErrorActionPreference = $previousEap
    if ($ffExit -ne 0) { Write-Output "FAIL: ffmpeg decode exit $ffExit"; exit 3 }
    Write-Output "ffmpeg decode exit 0 (yuv420p)"

    # the 4:2:0 split arithmetic: the raw frame is 64*64 (Y) + 32*32 (U) +
    # 32x32 (V) = 6144 bytes, planes contiguous in that order
    $planes = @(
        @{ name = "Y"; tag = "ecs4${Color}_recon_y"; off = 0;    pixels = 4096 },
        @{ name = "U"; tag = "ecs4${Color}_recon_u"; off = 4096; pixels = 1024 },
        @{ name = "V"; tag = "ecs4${Color}_recon_v"; off = 5120; pixels = 1024 })
    $got = [System.IO.File]::ReadAllBytes($raw)
    if ($got.Length -ne 6144) {
        Write-Output ("FAIL: decoded " + $got.Length + " bytes (want 6144)"); exit 5
    }
    $fail = 0
    foreach ($p in $planes) {
        $rec = (Get-Content $gatePath | Select-String ("^" + $p.tag + " ")).Line -split " " |
            Select-Object -Skip 1
        if ($rec.Count -ne $p.pixels) {
            Write-Output ("FAIL: gate " + $p.tag + " has " + $rec.Count + " values (want " +
                          $p.pixels + ")"); $fail = 1; break
        }
        $want = [byte[]][int[]]$rec
        $diff = 0
        $first = -1
        for ($i = 0; $i -lt $p.pixels; $i++) {
            if ($got[$p.off + $i] -ne $want[$i]) { if ($first -lt 0) { $first = $i }; $diff++ }
        }
        $fp = ($want[0..3] | ForEach-Object { $_.ToString("x2") }) -join " "
        Write-Output ("plane " + $p.name + " (" + $p.pixels + " B): expected fingerprint " + $fp)
        if ($diff -eq 0) {
            Write-Output ("  CONTENT 1:1: all " + $p.pixels + " decoded bytes match the generator's " +
                          $p.tag)
        } else {
            Write-Output ("  FAIL: plane " + $p.name + " diverges - $diff/" + $p.pixels +
                          " bytes differ, first at " + $first)
            $fail = 1
        }
    }
    if ($fail -eq 0) { Write-Output "COLOR CONTENT 1:1: Y, U, V all byte-exact"; exit 0 }
    exit 6
}

# the mono mode (the FS5c instrument, unchanged)
switch ($Geometry) {
    # FS5c: the 4x4 TU exists only as a partition leaf (the decoder aligns
    # frame dims to 8 px), so the d4 artifact is the 8x8 frame coded as
    # [part@8 SPLIT][4x 4x4-TU leaf walks] - the fs5g4 gate set (64 pixels).
    4 { $artifactName = "structural_keyframe_d4.obu"; $reconTag = "fs5g4_recon"; $pixels = 64 }
    8 { $artifactName = "structural_keyframe_d8.obu"; $reconTag = "fs28_recon"; $pixels = 64 }
    16 { $artifactName = "structural_keyframe_d16.obu"; $reconTag = "fs216_recon"; $pixels = 256 }
    32 { $artifactName = "structural_keyframe.obu"; $reconTag = "ecfrm_recon"; $pixels = 1024 }
    64 { $artifactName = "structural_keyframe_d64.obu"; $reconTag = "fs264_recon"; $pixels = 4096 }
    default { Write-Output ("FAIL: unknown geometry " + $Geometry); exit 1 }
}
$artifact = Join-Path $repo ("src\l8_bitstream\tests\goldens\" + $artifactName)
$raw = Join-Path $temp ("decode4_d" + $Geometry + ".raw")

Write-Output ("geometry d" + $Geometry + ": artifact " + $artifact + " (" + (Get-Item $artifact).Length + " bytes)")

$previousEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $ffmpeg -hide_banner -loglevel error -y -i $artifact -f rawvideo -pix_fmt gray $raw 2>$null
$ffExit = $LASTEXITCODE
$ErrorActionPreference = $previousEap
if ($ffExit -ne 0) { Write-Output "FAIL: ffmpeg decode exit $ffExit"; exit 3 }
Write-Output "ffmpeg decode exit 0"

# the expected recon = the gate's fs2S_recon line (the geometry's pixels)
$rec = (Get-Content $gatePath | Select-String ("^" + $reconTag + " ")).Line -split " " | Select-Object -Skip 1
if ($rec.Count -ne $pixels) {
    Write-Output ("FAIL: gate " + $reconTag + " has " + $rec.Count + " values (want " + $pixels + ")"); exit 4
}
$want = [byte[]][int[]]$rec
$got = [System.IO.File]::ReadAllBytes($raw)
if ($got.Length -ne $pixels) {
    Write-Output ("FAIL: decoded " + $got.Length + " bytes (want " + $pixels + ")"); exit 5
}

$diff = 0
$first = -1
for ($i = 0; $i -lt $pixels; $i++) {
    if ($got[$i] -ne $want[$i]) { if ($first -lt 0) { $first = $i }; $diff++ }
}
$fp = ($want[0..3] | ForEach-Object { $_.ToString("x2") }) -join " "
Write-Output ("expected fingerprint (" + $reconTag + " first pixels): " + $fp)
if ($diff -eq 0) {
    Write-Output ("CONTENT 1:1: all " + $pixels + " decoded bytes match the generator's " + $reconTag)
    exit 0
}
Write-Output ("FAIL: content diverges - $diff/$pixels bytes differ, first at $first")
exit 6