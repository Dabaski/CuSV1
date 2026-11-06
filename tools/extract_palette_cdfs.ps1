# SY1 mechanical extract: palette + seg default CDF tables from the pinned
# vendored cabac_context_model.c into src/l7_entropy/*.inc initializers.
# Output text is the reference's own initializer body, byte-identical modulo
# the strip of the declaration lines. UTF-8 no BOM, LF endings.
$src = "third_party\SVT-AV1\Source\Lib\Codec\cabac_context_model.c"
$lines = Get-Content $src
# 1-based inclusive spans: [name, start, end] where start = the `static const`
# line, end = the closing `};` line.
$spans = @(
    @("palette_y_size_cdfs_default.inc",          477, 485),
    @("palette_uv_size_cdfs_default.inc",         487, 495),
    @("palette_y_mode_cdfs_default.inc",          497, 505),
    @("palette_uv_mode_cdfs_default.inc",         507, 509),
    @("palette_y_color_index_cdfs_default.inc",   511, 547),
    @("palette_uv_color_index_cdfs_default.inc",  549, 585),
    @("seg_tree_cdfs_default.inc",                652, 654),
    @("segment_pred_cdfs_default.inc",            656, 658),
    @("spatial_pred_seg_tree_cdfs_default.inc",   660, 664)
)
foreach ($s in $spans) {
    $name = $s[0]; $a = $s[1]; $b = $s[2]
    # Body = lines after the declaration, before the closing `};`.
    $body = $lines[$a..($b - 2)]
    $joined = ($body -join "`n") + "`n"
    $decl = $lines[$a - 1]
    $hdr = "// mechanical extract: $decl`n" +
           "// (cabac_context_model.c lines $a-$b, pinned vendored SVT-AV1;`n" +
           "// SY1 - no hand-transcribed values)`n"
    $path = "src\l7_entropy\$name"
    [IO.File]::WriteAllText($path, $hdr + $joined, (New-Object Text.UTF8Encoding $false))
    Write-Output "$name <- lines $a-$b"
}
