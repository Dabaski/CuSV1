// TD5b: mechanically extracted from the committed verbatim extract
// (tools/golden_gen/svt_gen.c, the vendored cabac_context_model.c
// tables, provenance rows there) - the four TOKEN_CDF_Q_CTXS buckets,
// split verbatim; bucket[idx] selected by get_q_ctx(base_q_idx)
// (cabac_context_model.c:1907-1918; the spec's init_coeff_cdfs,
// 07.bitstream.semantics.md:1800-1820). No value transcribed by hand.
{{{AOM_CDF5(  840,  1039,  1980,  4895)}, {AOM_CDF5(  370,   671,  1883,  4471)}},
     {{AOM_CDF5( 3247,  4950,  9688, 14563)}, {AOM_CDF5( 1904,  3354,  7763, 14647)}}}
,
{{{AOM_CDF5( 2125,  2551,  5165,  8946)}, {AOM_CDF5(  513,   765,  1859, 6339)}},
     {{AOM_CDF5( 7637,  9498, 14259, 19108)}, {AOM_CDF5( 2497,  4096,  8866, 16993)}}}
,
{{{AOM_CDF5( 4016,  4897,  8881, 14968)}, {AOM_CDF5(  716,  1105,  2646, 10056)}},
     {{AOM_CDF5(11139, 13270, 18241, 23566)}, {AOM_CDF5( 3192,  5032, 10297, 19755)}}}
,
{{{AOM_CDF5( 6708,  8958, 14746, 22133)}, {AOM_CDF5( 1222,  2074,  4783, 15410)}},
     {{AOM_CDF5(19575, 21766, 26044, 29709)}, {AOM_CDF5( 7297, 10767, 19273, 28194)}}}
