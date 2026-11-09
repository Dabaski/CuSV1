// CS7 chroma bottom-left PRE-FIX baseline (the cascade-immunity reference).
//
// CAPTURED AT COMMIT d2a9322, PRE-FIX. Two geometries, S=4 (the c4 artifact)
// and S=8 (the c8 artifact), the two the CS7 map says must change. Full
// 32x32 recon for BOTH planes: 1024 bytes per plane, 4 arrays, 4096 bytes
// total, held here as 8192 hex characters (2 per byte, 64 per line).
//
// NEVER REGENERATE. THIS IS THE PRE-FIX REFERENCE. Regenerating it from a
// post-fix build makes the prefix assertion VACUOUS: 'unchanged' and
// 'compared against something that moved with it' are indistinguishable
// from inside the test. A post-change artifact can never serve as its own
// baseline. This file is a frozen, manually-refreshed fixture with direct
// precedent in ecs4_gate.inc (also only #included by test_pipeline.cpp;
// nothing in the generator or in CMake writes it), so a regen cannot
// clobber it. If the baseline must move, that is a COURT decision and a
// named commit, never a side effect of running the generator.
//
// S is the UV (chroma) block size, so S=4 and S=8 are c4 and c8. c16 and c32
// are deliberately ABSENT: their non-divergence is already proven
// end-to-end by the regen diff and the artifact byte-comparison, which is a
// stronger check than a fixture, so duplicating it would only bloat this file.
//
// Values are hex, two characters per byte, 64 characters (32 bytes) per line.

// ecs44_recon_u : S=4 (c4) U, 1024 bytes
static const char* const ecs44_recon_u_hex =
    "050608090c0f11141214181b1c1d1f2022232526282b2d30333537383a3d3f42"
    "05070b0d1013141514171b1e20212223262729292c30323538393b3b3e424447"
    "06090e1112141415181b1f2123242424282b2d2e313435363a3d3f4043464748"
    "05080d11121414151b1e212424242424292d3032333535363b3f424445474748"
    "080d111216191b1c1d2023242123272a2d303233373a3c3d3d4143454143484b"
    "0d11121417191c1d1e21242423262a2d30323335383a3d3e3e42444544474b4e"
    "11121414181a1c1d1f222424272a2e3032333535393b3d3e40434546484b4f51"
    "12141415181a1c1d202324242a2d303333353536393b3d3e414345464b4e5154"
    "1717181b1d1f20232324242a2b2e3234353537393b3d3e414345464b4b4e5153"
    "17181b1d1f20232624242a2d2e3234353537393b3d3e414345464b4e4e515253"
    "181b1d1f20232627242a2d303234363737393b3d3e414345464b4e5151525354"
    "1b1d1f20232627272a2d303334353738393b3d3e414345464b4e515453535454"
    "1d1f2023242525252c2f323437383a3b3b3d3e4145484b4c4d5053545053575a"
    "1f202326272828282d303334393a3c3d3d3e4143474a4d4e4e51545353565a5d"
    "20232627292b2d2d2e3134343b3c3e3f3e414345494b4d4e4f525453575a5e60"
    "232627272a2d30312f3234343c3d3f4041434546494b4d4e505354535a5d6063"
    "0102000401010100000000000000000000000000000000000000000000000000"
    "0102000403020100000000000000000000000000000000000000000000000000"
    "0102000403020100000000000000000000000000000000000000000000000000"
    "0102000403020100000000000000000000000000000000000000000000000000"
    "0101010102010000000000000000000000000000000000000000000000000000"
    "0101010101000000000000000000000000000000000000000000000000000000"
    "0101010100000000000000000000000000000000000000000000000000000000"
    "0101010100000000000000000000000000000000000000000000000000000000"
    "0101010000000000000000000000000000000000000000000000000000000000"
    "0101000000000000000000000000000000000000000000000000000000000000"
    "0100000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000";

// ecs44_recon_v : S=4 (c4) V, 1024 bytes
static const char* const ecs44_recon_v_hex =
    "f9f8f6f5f2f0edeaefede9e6e4e2dfdddbdad8d7d5d3d0cdcccac9c9c7c5c2bf"
    "f9f7f3f1eeeceae9edeae6e3e1dfdcdad7d6d5d4d1cecbc8c7c6c6c6c3c0bdba"
    "f8f5f0edecebeae9e9e6e2e0dddbdad9d6d2d0cfcccac8c7c5c3c1c1bebcbab9"
    "f9f6f1edecebeae9e6e3e0dddcdbdad9d4d0cdcbcac9c8c7c4c1bebdbcbbbab9"
    "f6f1edece9e6e4e3e4e1dddbdddbd7d4d3d0cecdcac7c5c4c3bfbdbdc1beb9b6"
    "f1edecebe8e5e2e1e2dfdbdadbd8d4d2d0cecdccc9c6c3c2c2bebdbcbebab6b3"
    "edecebeae6e4e2e1dfdcd9d9d7d4d0cececdcccbc7c5c3c2c0bdbdbcb9b6b2b0"
    "ecebeae9e6e4e2e1dddad8d8d4d2cecbcdcccbcac7c5c3c2bfbdbdbcb6b3b0ad"
    "ebeae9e6e4e2e1dddad8d8d4d4d1ceccc9c8c7c4c2c0bfbcbabab9b3b4b2afae"
    "eae9e6e4e2e1dddad8d8d4d2d1cfcdccc8c7c4c2c0bfbcbabab9b3b0b0afaead"
    "e9e6e4e2e1dddad8d8d4d2cececdcccbc7c4c2c0bfbcbabab9b3b0adadadacac"
    "e6e4e2e1dddad8d8d4d2cecbcccccbcbc4c2c0bfbcbabab9b3b0adaaacacacac"
    "e4e2e1dddad8d6d5d3d0cccbc9c9c8c8c2c0bfbcb8b5b3b2b1afababafada9a6"
    "e2e1dddad8d6d5d4d2cfcbccc7c7c6c6c0bfbcbab7b5b3b2b0aeaaacadaaa6a4"
    "e1dddad8d7d6d5d4d1cdcbccc5c5c4c4bfbcbabab7b4b2b1afacabaca9a6a2a0"
    "dddad8d8d7d6d5d4cfcccbccc4c4c3c3bcbabab9b6b4b2b1aeababaca6a4a09d"
    "fffefffcffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fffefffcfdffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fffefffcfdffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fffefffcfdffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fffffffffffffffefefefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffffffffef8fefefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffffffefdfffefefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffffef8fffdfefefefeffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fffffffbffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fffffbffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fffbffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fefefefeffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fefefefeffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fefefefeffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "fefefefeffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

// ecs48_recon_u : S=8 (c8) U, 1024 bytes
static const char* const ecs48_recon_u_hex =
    "090a0a0b0b0c0d0d11161a1e2022232325282b2d2e2e2e2f3133373a3c3f4042"
    "0a0a0c0d0f11121316191c1e1f21222527292a2c2c2e313235383b3d40424243"
    "0a0b0d1012151617181a1b1d2023262626272a2d2f313436393c3f4142434444"
    "08090c0e11141617181a1e212424242426292d30323538393b3e404244464747"
    "0a0c0d10131517181c1f222324252627292c303436383a3a3c3f41444648494a"
    "11121416181a1c1d1e2022242728292a2d30343638393a3a3c3f4346484a4c4c"
    "14141617191b1c1c1e2225282a2c2d2e3033353738393a3a3c4044474a4c4d4e"
    "161718191a1c1d1d1f2326292c2e2f303133353738393a3a3c4044484a4d4e4f"
    "141517191d1f212323262a2c2d2e2f2f3335383a3d3f41433e4247494d4e5050"
    "1517181b1e20222626292b2e2f3031313538393b3e4042464247494d4f505151"
    "17181b1d1e212528282b2d303132333338393b3d3e41454947494d4f51525354"
    "191b1d1e2023272a2b2d3032333536363a3b3d3e4043484c494d4f5253555657"
    "1d1e1e202326282b2c2f3234363738393d3e3e4043474a4c4d4f51535657595a"
    "1f20212326282a2c2d313436383a3b3c3f404143474a4b4e4e505255575a5c5d"
    "21222527282a2c2c2d3135373a3b3c3e414245484a4b4e4e50515356595c5e5f"
    "2326282a2b2c2c2d2f3235393b3c3e3f4346494c4c4e4e4f505154575a5d5f60"
    "0000000101000000000000000000000000000000000000000000000000000000"
    "0000000101000000000000000000000000000000000000000000000000000000"
    "0000000101000000000000000000000000000000000000000000000000000000"
    "0000000101000000000000000000000000000000000000000000000000000000"
    "0000000101000000000000000000000000000000000000000000000000000000"
    "0000000101000000000000000000000000000000000000000000000000000000"
    "0000000101000000000000000000000000000000000000000000000000000000"
    "0000000101000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000";

// ecs48_recon_v : S=8 (c8) V, 1024 bytes
static const char* const ecs48_recon_v_hex =
    "f5f5f4f4f3f2f2f2eeeae7e3e1e0e0dfdddad8d4d2d0cfcdcccac8c6c4c1bfbd"
    "f5f4f3f1efeeecece9e6e4e2e1e0dedddad7d4d2d0cecdcdcbc8c6c2c0bebdbd"
    "f5f4f2f0edeae9e8e7e5e3e1dfdcd9d9d7d4d1cfcecdcbcbc8c5c1bfbebdbbbb"
    "f7f6f4f1eeebe9e8e7e4e1dddad9d8d7d5d3d1cfcccbc9c7c5c3c1bfbcbbb9b7"
    "f5f4f2f0edeae8e7e4e0dddbd9d8d7d6d5d1cfcdcac8c7c6c5c1bfbdbab8b7b6"
    "efeeeceae8e6e4e3e2dfdcdad8d6d5d5d4d0cccac8c6c5c5c4c0bcbab8b6b5b5"
    "ececeae9e7e5e4e3e2dfdcd9d7d6d4d4d2cfccc9c7c6c4c4c2bfbcb9b7b6b4b4"
    "eaeae8e7e6e4e3e3e1dedcd9d7d5d4d3d1ceccc9c7c5c4c3c1bebcb9b7b5b4b3"
    "e7e5e4e3e1e0e0deded9d6d5d2d0d0cfcbc9c6c4c2c1c0bebdbbb7b5b2b1afaf"
    "e5e4e3e1e0e0dedbdad7d5d2d0cfcecec9c6c4c2c1c0bebbbbb8b5b2b0afaeae"
    "e4e3e1e0e0dedbd9d8d6d2d0cfcdccccc6c4c2c1c0bebbb9b7b5b2b0aeadacac"
    "e3e1e0e0dedbd9d6d5d2d0cdcccac9c8c4c2c1c0bebbb9b6b5b2b0aeacaaa9a9"
    "e1e0e0dedbd9d6d4d3d0cecac8c7c6c5c2c1c0bebbb9b6b4b2b1aeacaaa8a6a5"
    "e0e0dedbd9d6d4d2d0cdcac9c7c5c4c3c1c0bebbb9b6b4b2b1afadaaa8a5a3a2"
    "e0dedbd9d6d4d2d1cfccc9c7c4c3c2c1c0bebbb9b6b4b2b1afaeaca9a6a3a1a0"
    "dedbd9d6d4d2d1d0cecbc8c6c3c1c1c0bebbb9b6b4b2b1b0afaeaca9a5a2a09f"
    "fffffffefdfefffdfdfefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffefdfefffdfdfefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffefdfefffdfdfefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffefdfefffdfdfefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffefdfefffdfdfefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffefdfefffdfdfefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffefdfefffdfdfefefeffffffffffffffffffffffffffffffffffffffff"
    "fffffffefdfefffdfdfefefeffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

