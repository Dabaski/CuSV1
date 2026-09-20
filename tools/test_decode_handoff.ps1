$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "decode_handoff.ps1"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("av1-decode-handoff-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null

try {
    $decoder = Join-Path $temp "fake_decoder.ps1"
    Set-Content -LiteralPath $decoder -Value '@("fake decoder output")' -Encoding ascii
    $report = Join-Path $temp "result.txt"

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script `
        -ReportPath $report `
        -Dav1dCommand $decoder -AomdecCommand $decoder -FfmpegCommand $decoder

    $contents = Get-Content -LiteralPath $report -Raw
    if ($contents -notmatch [regex]::Escape("[dav1d]")) {
        throw "decode handoff report did not record the dav1d command"
    }
}
finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output "PASS: decode handoff report records decoder commands"
