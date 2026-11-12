# T4 model_rd span extractor + renamer. Reads the three model_rd spans from
# enc_inter_prediction.c, applies the port renames, and emits the splice body
# to t4_splice.txt (UTF-8, LF). The four small wedge/sse bodies are hand-
# spliced separately; only the table-bearing spans go through here.
$enc = [IO.File]::ReadAllLines("third_party/SVT-AV1/Source/Lib/Codec/enc_inter_prediction.c")
$spans = @(
    @{ a = 1883; b = 1937; name = "modelRdNorm" },
    @{ a = 1939; b = 1958; name = "modelRdFromVarLapndz" },
    @{ a = 1960; b = 1981; name = "modelRdFromSse" }
)
$pairs = [ordered]@{
    'svt_av1_model_rd_from_var_lapndz' = 'modelRdFromVarLapndz'
    'model_rd_norm'                    = 'modelRdNorm'
    'model_rd_from_sse'                = 'modelRdFromSse'
    'static void'                      = 'void'
    'BlockSize bsize'                  = 'std::int32_t numPelsLog2'
    'eb_num_pels_log2_lookup\[bsize\]' = 'numPelsLog2'
    'xsq_q10_64'                       = 'xsqQ10_64'
    'xsq_iq_q10'                       = 'xsqIqQ10'
    'rate_tab_q10'                     = 'rateTabQ10'
    'dist_tab_q10'                     = 'distTabQ10'
    'MAX_XSQ_Q10'                      = 'kMaxXsqQ10'
    'xsq_q10'                          = 'xsqQ10'
    'r_q10'                            = 'rQ10'
    'd_q10'                            = 'dQ10'
    'one_q10'                          = 'oneQ10'
    'a_q10'                            = 'aQ10'
    'b_q10'                            = 'bQ10'
    'n_log2'                           = 'nLog2'
    'bit_depth'                        = 'bitDepth'
    'simple_model_rd_from_var'         = 'simpleModelRdFromVar'
    'dequant_shift'                    = 'dequantShift'
    'square_error'                     = 'squareError'
    'get_msb'                          = 'getMsb'
    '\bMIN\('                          = 'std::min<std::uint64_t>('
    'ROUND_POWER_OF_TWO'               = 'roundPowerOfTwo'
    'AV1_PROB_COST_SHIFT'              = 'kAv1ProbCostShift'
    '\bint16_t\b'                      = 'std::int16_t'
    '\bint32_t\b'                      = 'std::int32_t'
    '\bint64_t\b'                      = 'std::int64_t'
    '\buint8_t\b'                      = 'std::uint8_t'
    '\buint32_t\b'                     = 'std::uint32_t'
    '\buint64_t\b'                     = 'std::uint64_t'
}
$out = New-Object System.Collections.Generic.List[string]
foreach ($s in $spans) {
    $out.Add("// span enc $($s.a)-$($s.b) ($($s.name))")
    for ($i = $s.a; $i -le $s.b; $i++) {
        $t = $enc[$i - 1]
        if ($t.Trim() -eq "") { continue }
        foreach ($k in $pairs.Keys) { $t = [regex]::Replace($t, $k, $pairs[$k]) }
        $out.Add($t)
    }
    $out.Add("")
}
[IO.File]::WriteAllLines("src/l17_inter/t4_splice.txt", $out)
"lines=$($out.Count)"
