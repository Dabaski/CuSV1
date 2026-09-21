// TD5b: mechanically extracted from the committed verbatim extract
// (tools/golden_gen/svt_gen.c, the vendored cabac_context_model.c
// tables, provenance rows there) - the four TOKEN_CDF_Q_CTXS buckets,
// split verbatim; bucket[idx] selected by get_q_ctx(base_q_idx)
// (cabac_context_model.c:1907-1918; the spec's init_coeff_cdfs,
// 07.bitstream.semantics.md:1800-1820). No value transcribed by hand.
{{{AOM_CDF6(  400,   520,   977,  2102,  6542)}, {AOM_CDF6(  210,   405,  1315,  3326, 7537)}},
     {{AOM_CDF6( 2636,  4273,  7588, 11794, 20401)}, {AOM_CDF6( 1786,  3179,  6902, 11357, 19054)}}}
,
{{{AOM_CDF6(  989,  1249,  2019,  4151, 10785)}, {AOM_CDF6(  313,   441,  1099,  2917, 8562)}},
     {{AOM_CDF6( 8394, 10352, 13932, 18855, 26014)}, {AOM_CDF6( 2578,  4124,  8181, 13670, 24234)}}}
,
{{{AOM_CDF6( 2515,  3003,  4452,  8162, 16041)}, {AOM_CDF6(  574,   821,  1836,  5089, 13128)}},
     {{AOM_CDF6(13468, 16303, 20361, 25105, 29281)}, {AOM_CDF6( 3542,  5502, 10415, 16760, 25644)}}}
,
{{{AOM_CDF6( 4617,  5709,  8446, 13584, 23135)}, {AOM_CDF6( 1156,  1702,  3675,  9274, 20539)}},
     {{AOM_CDF6(22086, 24282, 27010, 29770, 31743)}, {AOM_CDF6( 7699, 10897, 20891, 26926, 31628)}}}
