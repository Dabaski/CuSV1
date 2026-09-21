# TD5c decode attempt #4: decode the committed 47-byte idx-2 artifact with
# the machine's ffmpeg (libdav1d) and compare the decoded picture against
# the generator's ecfrm_recon gate line - the content-1:1 check that closes
# the TS-series. Exits 0 on full success (decode + byte-exact content).
$ErrorActionPreference = "Continue"
$repo = Split-Path $PSScriptRoot -Parent
$artifact = Join-Path $repo "src\l8_bitstream\tests\goldens\structural_keyframe.obu"
$gatePath = Join-Path $repo "tools\golden_gen\expected_primitives.txt"
$temp = Join-Path $env:TEMP "decode4"
New-Item -ItemType Directory $temp -Force | Out-Null
$raw = Join-Path $temp "decode4.raw"

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { Write-Output "FAIL: ffmpeg not on PATH"; exit 2 }

Write-Output ("artifact: " + $artifact + " (" + (Get-Item $artifact).Length + " bytes)")

$previousEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $ffmpeg -hide_banner -loglevel error -i $artifact -f rawvideo -pix_fmt gray $raw 2>$null
$ffExit = $LASTEXITCODE
$ErrorActionPreference = $previousEap
if ($ffExit -ne 0) { Write-Output "FAIL: ffmpeg decode exit $ffExit"; exit 3 }
Write-Output "ffmpeg decode exit 0"

# the expected 32x32 recon = the gate's ecfrm_recon line (1024 values)
$rec = (Get-Content $gatePath | Select-String "^ecfrm_recon ").Line -split " " | Select-Object -Skip 1
if ($rec.Count -ne 1024) { Write-Output ("FAIL: gate ecfrm_recon has " + $rec.Count + " values (want 1024)"); exit 4 }
$want = [byte[]][int[]]$rec
$got = [System.IO.File]::ReadAllBytes($raw)
if ($got.Length -ne 1024) { Write-Output ("FAIL: decoded " + $got.Length + " bytes (want 1024)"); exit 5 }

$diff = 0
$first = -1
for ($i = 0; $i -lt 1024; $i++) {
    if ($got[$i] -ne $want[$i]) { if ($first -lt 0) { $first = $i }; $diff++ }
}
$fp = ($want[0..3] | ForEach-Object { $_.ToString("x2") }) -join " "
Write-Output ("expected fingerprint (ecfrm_recon first pixels): " + $fp)
if ($diff -eq 0) {
    Write-Output "CONTENT 1:1: all 1024 decoded bytes match the generator's ecfrm_recon"
    Write-Output "TS-SERIES CLOSED: decode-acceptance AND content-1:1"
    exit 0
}
Write-Output ("FAIL: content diverges - $diff/1024 bytes differ, first at $first")
exit 6