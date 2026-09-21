[CmdletBinding()]
param(
    [string]$FfmpegCommand = "ffmpeg"
)

# TD0 bisect ladder runner. Parses the golden_primitives.exe gate output for
# the td0_rung* lines, materializes each rung TU (.obu) + expected picture
# (.raw), decodes each with the local conformant decoder (ffmpeg - the AV1
# decode path inside it is libdav1d/libaom), and reports the FIRST rung whose
# decoded picture diverges from the generator's expected recon.

$ErrorActionPreference = "Stop"
$exe = Join-Path $PSScriptRoot "..\build\golden_gen\Release\golden_primitives.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Output "SKIP: golden_primitives.exe not built (cmake --build build/golden_gen --config Release)"
    exit 2
}
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("av1-td0-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null

# The gate exe writes CK: progress to stderr; PowerShell 5.1 turns stderr
# lines into errors under Stop - run with Continue and separate the streams.
$previousEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$rawOut = & $exe 2>$null
$ErrorActionPreference = $previousEap
if ($LASTEXITCODE -ne 0) { throw "gate exe failed with exit $LASTEXITCODE" }
$firstDivergent = -1
$summary = New-Object System.Collections.Generic.List[string]
try {
    for ($r = 0; $r -lt 7; ++$r) {
        $name = @("a", "b", "c", "d", "e", "f", "g")[$r]
        $tuLine = $rawOut | Select-String -SimpleMatch ("td0_rung" + $name + "_tu ")
        $picLine = $rawOut | Select-String -SimpleMatch ("td0_rung" + $name + "_pic ")
        if (-not $tuLine -or -not $picLine) { throw "gate output missing rung $name" }
        $tuParts = ($tuLine.Line -split " ")
        $tuSize = [int]$tuParts[1]
        $tuBytes = $tuParts[2..($tuSize + 1)] | ForEach-Object { [byte]("0x" + $_) }
        $obuPath = Join-Path $temp ("rung" + $name + ".obu")
        [System.IO.File]::WriteAllBytes($obuPath, [byte[]]$tuBytes)
        $picParts = ($picLine.Line -split " ") | Select-Object -Skip 1
        $expected = New-Object System.Collections.Generic.List[byte]
        foreach ($v in $picParts) { $expected.Add([byte][Math]::Min(255, [Math]::Max(0, [int]$v))) }
        $expectedPath = Join-Path $temp ("rung" + $name + "_expected.raw")
        [System.IO.File]::WriteAllBytes($expectedPath, $expected.ToArray())
        $decPath = Join-Path $temp ("rung" + $name + "_dec.raw")
        $previousEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $FfmpegCommand -hide_banner -loglevel error -i $obuPath -f rawvideo -pix_fmt gray $decPath 2>$null
        $ffExit = $LASTEXITCODE
        $ErrorActionPreference = $previousEap
        if ($ffExit -ne 0) {
            $summary.Add("rung $name : ffmpeg FAILED exit $ffExit")
            continue
        }
        $decoded = [System.IO.File]::ReadAllBytes($decPath)
        $diffs = 0
        $firstDiff = -1
        $firstExp = -1
        $firstGot = -1
        for ($i = 0; $i -lt 256; ++$i) {
            if ($i -ge $decoded.Length -or $expected[$i] -ne $decoded[$i]) {
                $diffs++
                if ($firstDiff -lt 0) {
                    $firstDiff = $i
                    $firstExp = $expected[$i]
                    $firstGot = if ($i -ge $decoded.Length) { -1 } else { $decoded[$i] }
                }
            }
        }
        if ($diffs -eq 0) {
            $summary.Add("rung $name : PASS (byte-exact, ffmpeg exit $ffExit)")
        } else {
            $summary.Add("rung $name : DIVERGES - $diffs/256 bytes differ, first at pixel $firstDiff (expected $($expected[$firstDiff]), decoded $firstGot)")
            if ($firstDivergent -lt 0) { $firstDivergent = $r }
        }
    }
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Output "TD0 bisect ladder results:"
$summary | ForEach-Object { Write-Output $_ }
if ($firstDivergent -ge 0) {
    Write-Output ("FIRST DIVERGENT RUNG: " + @("a", "b", "c", "d", "e", "f", "g")[$firstDivergent])
} else {
    Write-Output "FIRST DIVERGENT RUNG: none (all rungs byte-exact)"
}
exit 0