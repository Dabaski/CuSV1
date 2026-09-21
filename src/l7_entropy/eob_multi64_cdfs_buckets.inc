// TD5b: mechanically extracted from the committed verbatim extract
// (tools/golden_gen/svt_gen.c, the vendored cabac_context_model.c
// tables, provenance rows there) - the four TOKEN_CDF_Q_CTXS buckets,
// split verbatim; bucket[idx] selected by get_q_ctx(base_q_idx)
// (cabac_context_model.c:1907-1918; the spec's init_coeff_cdfs,
// 07.bitstream.semantics.md:1800-1820). No value transcribed by hand.
{{{AOM_CDF7(  329,   498,  1101,  1784,  3265,  7758)}, {AOM_CDF7(  335,   730,  1459,  5494,  8755, 12997)}},
     {{AOM_CDF7( 3505,  5304, 10086, 13814, 17684, 23370)}, {AOM_CDF7( 1563,  2700,  4876, 10911, 14706, 22480)}}}
,
{{{AOM_CDF7( 1260,  1446,  2253,  3712,  6652, 13369)}, {AOM_CDF7(  401,   605,  1029,  2563,  5845, 12626)}},
     {{AOM_CDF7( 8609, 10612, 14624, 18714, 22614, 29024)}, {AOM_CDF7( 1923,  3127,  5867,  9703, 14277, 27100)}}}
,
{{{AOM_CDF7( 2374,  2772,  4583,  7276, 12288, 19706)}, {AOM_CDF7(  497,   810,  1315,  3000,  7004, 15641)}},
     {{AOM_CDF7(15050, 17126, 21410, 24886, 28156, 30726)}, {AOM_CDF7( 4034,  6290, 10235, 14982, 21214, 28491)}}}
,
{{{AOM_CDF7( 6307,  7541, 12060, 16358, 22553, 27865)}, {AOM_CDF7( 1289,  2320,  3971,  7926, 14153, 24291)}},
     {{AOM_CDF7(24212, 25708, 28268, 30035, 31307, 32049)}, {AOM_CDF7( 8726, 12378, 19409, 26450, 30038, 32462)}}}
