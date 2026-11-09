// RT21 chroma ABOVE-RIGHT PRE-FIX baseline (the cascade-immunity reference).
//
// CAPTURED AT COMMIT 69a8a1a, PRE-FIX (the RT18 docs commit; the working tree is
// clean there). ONE geometry, S=4 (the c4 artifact), the only one whose committed
// UV mode set contains a leaf whose above-right owner walks later in Z-order AND
// whose decided mode reads the gathered stretch (leaf 41, UV_D45_PRED). Full 32x32
// recon for BOTH planes: 1024 bytes per plane, 2 arrays, 2048 bytes total, held
// here as 4096 hex characters (2 per byte, 64 per line).
//
// NEVER REGENERATE. THIS IS THE PRE-FIX REFERENCE. Regenerating it from a
// post-fix build makes the prefix assertion VACUOUS: "unchanged" and "compared
// against something that moved with it" are indistinguishable from inside the
// test. If the baseline must move, that is a COURT decision and a named commit,
// never a side effect of running the generator. A regen cannot clobber it:
// ecs4_gate.inc is only ever #included by test_pipeline.cpp and nothing in the
// generator or in CMake writes this file (same property as
// cs7_prefix_baseline.inc, the direct precedent).
//
// c8 IS ABSENT, deliberately: two structural above-right violations exist at c8
// (leaves 5 and 13 of the 4x4 grid) but the committed UV modes there
// (UV_SMOOTH_H_PRED and UV_DC_PRED) never read the stretch, so the fix is inert
// at c8 end to end - proven by the regen diff (ecs48_* unchanged) and the color8
// artifact byte-comparison, stronger checks than a fixture. c16 and c32 are
// absent because grid <= 2 admits NO above-right violation at all - structural,
// not fixture-luck.
//
// Values are hex, two characters per byte, 64 characters (32 bytes) per line.

// ecs44_recon_u : S=4 (c4) U, 1024 bytes
static const char* const rt21_recon_u_hex =
    "050608090c0f11141214181b1c1d1f2024272b2c2c2c2d2f34373b3c3c3c3d3f"
    "05070b0d1013141514171b1e2021222327292b2c2e2f313337393b3c3e3f4143"
    "06090e1112141415181b1f212324242427292d303233343437393d4042434444"
    "05080d11121414151b1e2124242424242a2e3234343434343a3e424444444444"
    "080d111216191b1c1d2023242123272a2a2e31333133373a3a3e41434143474a"
    "0d11121417191c1d1e21242423262a2d2d30323333363a3d3d40424343464a4d"
    "11121414181a1c1d1f222424272a2e3030323333373a3e4040424343474a4e50"
    "12141415181a1c1d202324242a2d3033323333343a3d4043424343444a4d5053"
    "1717181b1d1f20232324242a2b2e3234333335393a404345434345494a505355"
    "17181b1d1f20232624242a2d2e3234353335393d3e4344464345494d4e535456"
    "181b1d1f20232627242a2d303234363735393d404144454645494d5051545556"
    "1b1d1f20232627272a2d303334353738393d404243454647494d505253555657"
    "1d1f2023242525252c2f323437383a3b3d4042434548494a4d5052534f52575a"
    "1f202326272828282d303334393a3c3d4042434547494a4b5052535553565a5d"
    "20232627292b2d2d2e3134343b3c3e3f42434546484a4a4b52535556575a5e60"
    "232627272a2d30312f3234343c3d3f4043454647484a4a4b535556575a5d6063"
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
    "0000000000000000000000000000000000000000000000000000000000000000"
;

// rt21_recon_v : S=4 (c4) V, 1024 bytes
static const char* const rt21_recon_v_hex =
    "f9f8f6f5f2f0edeaefede9e6e4e2dfdddbd9d6d5d2d0cdcbc9c7c4c3c2c1bfbe"
    "f9f7f3f1eeeceae9edeae6e3e1dfdcdad8d7d6d5d1cecac8c6c5c4c3c1bfbdbc"
    "f8f5f0edecebeae9e9e6e2e0dddbdad9d8d7d5d2cdcac8c7c6c5c3c1bfbdbcbb"
    "f9f6f1edecebeae9e6e3e0dddcdbdad9d6d3d0cfcccac8c7c5c3c0bfbebdbcbb"
    "f6f1edece9e6e4e3e4e1dddbdddbd7d4d4d1cececdcbc7c4c3c1bebebfbdb9b6"
    "f1edecebe8e5e2e1e2dfdbdadbd8d4d2d1cecccccbc8c4c2c0bebcbcbdbab6b4"
    "edecebeae6e4e2e1dfdcd9d9d7d4d0cecdcbcacac7c4c0bebdbbbabab9b6b2b0"
    "ecebeae9e6e4e2e1dddad8d8d4d2cecbcacac9c8c4c2bebbbabab9b8b6b4b0ad"
    "ebeae9e6e4e2e1dddad8d8d4d4d1cecccac9c7c5c5c1bebebab9b8b6b5b0adab"
    "eae9e6e4e2e1dddad8d8d4d2d1cfcdccc9c7c5c2c2bfbdbdb9b8b6b4b3afacab"
    "e9e6e4e2e1dddad8d8d4d2cececdcccbc7c5c2bebebdbcbcb8b6b4b0afadabaa"
    "e6e4e2e1dddad8d8d4d2cecbcccccbcbc5c2bebcbcbcbbbbb6b4b0aeadacaaa9"
    "e4e2e1dddad8d6d5d3d0cccbc9c9c8c8c2bebcbcbbbbbbbab4b0aeadb2aea9a6"
    "e2e1dddad8d6d5d4d2cfcbccc7c7c6c6bebcbcbcbab8b7b6b0aeadacaeaaa6a3"
    "e1dddad8d7d6d5d4d1cdcbccc5c5c4c4bcbcbcbbb8b4b2b1aeadacaaa9a6a2a0"
    "dddad8d8d7d6d5d4cfcccbccc4c4c3c3bcbcbbbbb6b2afadadacaaa9a6a3a09d"
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
    "fefefefeffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
;
