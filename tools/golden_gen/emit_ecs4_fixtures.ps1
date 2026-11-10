# tools/golden_gen/emit_ecs4_fixtures.ps1
# Regenerates the committed CS4/CS5 test fixtures from the committed gate:
#   src/l6_pipeline/tests/ecs4_gate.inc   (the CS4 walk fixtures + CS2 cross-check)
#   src/l8_bitstream/tests/ecs5_gate.inc  (the color TU tiles + gate TUs)
# Every value comes from the named expected_primitives.txt line; no hand-typed
# values. The bytes lines (ecs4S_bytes, tu_colorS) carry the byte count as
# their second token - skipped for the arrays, checked against the value
# count loudly. The ecs4_gate.inc Gate table is carried over VERBATIM from
# the committed file (it is structural, never value-bearing).

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$gate = Get-Content (Join-Path $PSScriptRoot "expected_primitives.txt")

# IntArray name: all tokens after the name are the values.
function Gate-Values([string]$name) {
    $line = $gate | Where-Object { $_.StartsWith($name + " ") } | Select-Object -First 1
    if (-not $line) { throw "gate line not found: $name" }
    $toks = ($line.Trim() -split "\s+") | Where-Object { $_ -ne "" }
    return ,($toks[1..($toks.Count - 1)] | ForEach-Object { [int]$_ })
}

# ByteArray name: token 1 = the byte count (self-describing), tokens 2.. = the
# bytes. The count is checked against the parsed value count and thrown on if
# it disagrees (a regen that misparses must not silently shorten an array).
function Gate-Bytes([string]$name) {
    $line = $gate | Where-Object { $_.StartsWith($name + " ") } | Select-Object -First 1
    if (-not $line) { throw "gate line not found: $name" }
    $toks = ($line.Trim() -split "\s+") | Where-Object { $_ -ne "" }
    $count = [int]$toks[1]
    $vals = $toks[2..($toks.Count - 1)] | ForEach-Object { [Convert]::ToInt32($_, 16) }
    if ($vals.Count -ne $count) {
        throw "${name}: gate count $count disagrees with $($vals.Count) values"
    }
    return ,($vals)
}

function Emit-IntBlock([System.Text.StringBuilder]$sb, [string]$name, $vals) {
    [void]$sb.Append("static const int $name[$($vals.Count)] = {`n")
    for ($i = 0; $i -lt $vals.Count; $i += 16) {
        $end = [Math]::Min($i + 16, $vals.Count) - 1
        $chunk = $vals[$i..$end]
        $parts = @()
        foreach ($v in $chunk) { $parts += [string]$v }
        [void]$sb.Append("    " + ($parts -join ", "))
        if ($end -lt $vals.Count - 1) { [void]$sb.Append(",") }
        [void]$sb.Append("`n")
    }
    [void]$sb.Append("};`n`n")
}

function Emit-HexBlock([System.Text.StringBuilder]$sb, [string]$name, $vals, [string]$indent) {
    [void]$sb.Append("static const unsigned char $name[$($vals.Count)] = {`n")
    for ($i = 0; $i -lt $vals.Count; $i += 16) {
        $end = [Math]::Min($i + 16, $vals.Count) - 1
        $chunk = $vals[$i..$end]
        $parts = @()
        foreach ($v in $chunk) { $parts += ("0x{0:x2}" -f $v) }
        [void]$sb.Append($indent + ($parts -join ", "))
        if ($end -lt $vals.Count - 1) { [void]$sb.Append(",") }
        [void]$sb.Append("`n")
    }
    [void]$sb.Append("};`n`n")
}

# ---- ecs4_gate.inc ----------------------------------------------------------
$inc4 = Join-Path $repo "src\l6_pipeline\tests\ecs4_gate.inc"
$old4 = [IO.File]::ReadAllText($inc4)
$marker = "namespace ecs4 {"
$idx = $old4.IndexOf($marker)
if ($idx -lt 0) { throw "Gate block marker not found in ecs4_gate.inc" }
$gateBlock = $old4.Substring($idx)

$sb4 = New-Object System.Text.StringBuilder
[void]$sb4.Append("// CS4 test fixtures, mechanically emitted from tools/golden_gen/")
[void]$sb4.Append("`n// expected_primitives.txt by tools/golden_gen/emit_ecs4_fixtures.ps1;")
[void]$sb4.Append("`n// no hand-typed values. ecs4S_* = the walk-faithful CS4 drive lines;")
[void]$sb4.Append("`n// ecs2S_* = the CS2 quant-drive lines (the UV cross-check).")
[void]$sb4.Append("`n")

# int fixtures per geometry: each CS2 quant-drive pairs with its CS4 walk at
# the same UV block size (S=4: ecs24+ecs44, S=8: ecs28+ecs48, ...), the
# committed declaration order.
foreach ($pair in @(@("ecs24", "ecs44"), @("ecs28", "ecs48"),
                    @("ecs216", "ecs416"), @("ecs232", "ecs432"))) {
    $c2 = $pair[0]
    $c4 = $pair[1]
    Emit-IntBlock $sb4 "${c2}_modes" (Gate-Values "${c2}_modes")
    Emit-IntBlock $sb4 "${c2}_eobs" (Gate-Values "${c2}_eobs")
    Emit-IntBlock $sb4 "${c2}_coeffs" (Gate-Values "${c2}_coeffs")
    Emit-IntBlock $sb4 "${c2}_recon_u" (Gate-Values "${c2}_recon_u")
    Emit-IntBlock $sb4 "${c2}_recon_v" (Gate-Values "${c2}_recon_v")
    Emit-HexBlock $sb4 "${c4}_bytes" (Gate-Bytes "${c4}_bytes") "    "
    Emit-IntBlock $sb4 "${c4}_modes" (Gate-Values "${c4}_modes")
    Emit-IntBlock $sb4 "${c4}_eobs" (Gate-Values "${c4}_eobs")
    Emit-IntBlock $sb4 "${c4}_coeffs" (Gate-Values "${c4}_coeffs")
    Emit-IntBlock $sb4 "${c4}_recon_y" (Gate-Values "${c4}_recon_y")
    Emit-IntBlock $sb4 "${c4}_recon_u" (Gate-Values "${c4}_recon_u")
    Emit-IntBlock $sb4 "${c4}_recon_v" (Gate-Values "${c4}_recon_v")
}
[void]$sb4.Append($gateBlock)
[IO.File]::WriteAllText($inc4, $sb4.ToString(), (New-Object System.Text.UTF8Encoding $false))
Write-Host "ecs4_gate.inc written:" $sb4.Length "chars"

# ---- ecs5_gate.inc ----------------------------------------------------------
$inc5 = Join-Path $repo "src\l8_bitstream\tests\ecs5_gate.inc"
$sb5 = New-Object System.Text.StringBuilder
[void]$sb5.Append("// CS5 color-artifact test fixtures, mechanically emitted from the")
[void]$sb5.Append("`n// tools/golden_gen/expected_primitives.txt gate lines; no hand-typed values.")
[void]$sb5.Append("`n// tu_colorS = the composed color TU (TD + SPS_v3 + OBU_FRAME(v3 + the")
[void]$sb5.Append("`n// CS4 walk tile)); ecs4S_bytes = the CS4 walk tile (the assembled domain).")
[void]$sb5.Append("`n`n")
foreach ($pair in @(@("ecs44_tile", "ecs44_bytes"), @("tu_color4", "tu_color4"),
                    @("ecs48_tile", "ecs48_bytes"), @("tu_color8", "tu_color8"),
                    @("ecs416_tile", "ecs416_bytes"), @("tu_color16", "tu_color16"),
                    @("ecs432_tile", "ecs432_bytes"), @("tu_color32", "tu_color32"))) {
    Emit-HexBlock $sb5 $pair[0] (Gate-Bytes $pair[1]) ""
}
# the last block keeps the file's two-newline convention trimmed: the committed
# file ends right after the final "};"
$out5 = $sb5.ToString()
if ($out5.EndsWith("`n`n")) { $out5 = $out5.Substring(0, $out5.Length - 1) }
[IO.File]::WriteAllText($inc5, $out5, (New-Object System.Text.UTF8Encoding $false))
Write-Host "ecs5_gate.inc written:" $out5.Length "chars"
