# FS5c: the per-geometry decode gate, parameterized from TD5c's decode
# attempt #4. Decodes one committed v3 artifact with the machine's ffmpeg
# (libdav1d) and compares the decoded picture against the generator's
# fs2S_recon gate line - the content-1:1 check per geometry. Exits 0 on
# full success (decode + byte-exact content).
#
# Usage:  pwsh tools/verify_decode4.ps1 [-Geometry 4|8|16|32|64]
#   (default: 32 = the committed 47-byte four-16x16-leaves artifact)
param([int]$Geometry = 32)

$ErrorActionPreference = "Continue"
$repo = Split-Path $PSScriptRoot -Parent
$gatePath = Join-Path $repo "tools\golden_gen\expected_primitives.txt"
$temp = Join-Path $env:TEMP "decode4"
New-Item -ItemType Directory $temp -Force | Out-Null
$raw = Join-Path $temp ("decode4_d" + $Geometry + ".raw")

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

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { Write-Output "FAIL: ffmpeg not on PATH"; exit 2 }

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