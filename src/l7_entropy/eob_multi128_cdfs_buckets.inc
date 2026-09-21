// TD5b: mechanically extracted from the committed verbatim extract
// (tools/golden_gen/svt_gen.c, the vendored cabac_context_model.c
// tables, provenance rows there) - the four TOKEN_CDF_Q_CTXS buckets,
// split verbatim; bucket[idx] selected by get_q_ctx(base_q_idx)
// (cabac_context_model.c:1907-1918; the spec's init_coeff_cdfs,
// 07.bitstream.semantics.md:1800-1820). No value transcribed by hand.
{{{AOM_CDF8(  219,   482,  1140,  2091,  3680,  6028, 12586)},
      {AOM_CDF8(  371,   699,  1254,  4830,  9479, 12562, 17497)}},
     {{AOM_CDF8( 5245,  7456, 12880, 15852, 20033, 23932, 27608)},
      {AOM_CDF8( 2054,  3472,  5869, 14232, 18242, 20590, 26752)}}}
,
{{{AOM_CDF8(  685,   933,  1488,  2714,  4766,  8562, 19254)},
      {AOM_CDF8(  217,   352,   618,  2303,  5261,  9969, 17472)}},
     {{AOM_CDF8( 8045, 11200, 15497, 19595, 23948, 27408, 30938)},
      {AOM_CDF8( 2310,  4160,  7471, 14997, 17931, 20768, 30240)}}}
,
{{{AOM_CDF8( 1366,  1738,  2527,  5016,  9355, 15797, 24643)},
      {AOM_CDF8(  354,   558,   944,  2760,  7287, 14037, 21779)}},
     {{AOM_CDF8(13627, 16246, 20173, 24429, 27948, 30415, 31863)},
      {AOM_CDF8( 6275,  9889, 14769, 23164, 27988, 30493, 32272)}}}
,
{{{AOM_CDF8( 3472,  4885,  7489, 12481, 18517, 24536, 29635)},
      {AOM_CDF8(  886,  1731,  3271,  8469, 15569, 22126, 28383)}},
     {{AOM_CDF8(24313, 26062, 28385, 30107, 31217, 31898, 32345)},
      {AOM_CDF8( 9165, 13282, 21150, 30286, 31894, 32571, 32712)}}}
