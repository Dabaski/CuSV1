[CmdletBinding()]
param(
    [string]$InputPath = "",
    [string]$ReportPath = "",
    [string]$Dav1dCommand = "dav1d",
    [string]$AomdecCommand = "aomdec",
    [string]$FfmpegCommand = "ffmpeg"
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($InputPath)) {
    $InputPath = Join-Path $PSScriptRoot "..\src\l8_bitstream\tests\goldens\structural_keyframe.obu"
}
if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $PSScriptRoot "..\decode_handoff_results.txt"
}
$InputPath = (Resolve-Path -LiteralPath $InputPath).Path
$ReportPath = [System.IO.Path]::GetFullPath($ReportPath)
$reportDirectory = Split-Path -Parent $ReportPath
if (-not (Test-Path -LiteralPath $reportDirectory)) {
    New-Item -ItemType Directory -Path $reportDirectory | Out-Null
}

$workDirectory = Split-Path -Parent $ReportPath
$dav1dOutput = Join-Path $workDirectory "ts4.y4m"
$aomdecOutput = Join-Path $workDirectory "ts4_aom.y4m"
$results = New-Object System.Collections.Generic.List[string]
$failures = 0

function Invoke-DecodeCommand([string]$Name, [string]$Command, [string[]]$Arguments) {
    $displayArguments = ($Arguments | ForEach-Object {
        if ($_ -match "\s") { '"' + $_ + '"' } else { $_ }
    }) -join " "
    $script:results.Add("[$Name]")
    $script:results.Add("command: $Command $displayArguments")
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = (& $Command @Arguments 2>&1 | Out-String).TrimEnd()
        $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
        if ($output.Length -gt 0) {
            $script:results.Add("output:")
            $script:results.Add($output)
        } else {
            $script:results.Add("output: <empty>")
        }
    } catch {
        $output = $_.Exception.Message
        $exitCode = 9009
        $script:results.Add("output:")
        $script:results.Add($output)
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $script:results.Add("exit_code: $exitCode")
    $script:results.Add("")
    if ($exitCode -ne 0) { $script:failures++ }
}

$results.Add("AV1 TS4 decode handoff")
$results.Add("input: $InputPath")
$results.Add("started: $(Get-Date -Format o)")
$results.Add("")

Invoke-DecodeCommand "dav1d" $Dav1dCommand @("-i", $InputPath, "-o", $dav1dOutput)
Invoke-DecodeCommand "aomdec" $AomdecCommand @("-o", $aomdecOutput, $InputPath)
# ffmpeg requires an output target: the bare "-i input" form exits 1 with
# "At least one output file must be specified". Decode fully to the null
# muxer so the runner still measures a real full decode.
Invoke-DecodeCommand "ffmpeg" $FfmpegCommand @("-hide_banner", "-i", $InputPath, "-f", "null", "-")

$results.Add("summary: $failures decoder command(s) failed")
$results.Add("finished: $(Get-Date -Format o)")
Set-Content -LiteralPath $ReportPath -Value $results -Encoding utf8

if ($failures -ne 0) { exit 1 }
exit 0
