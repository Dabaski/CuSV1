// l16_rdo - EMC1 slice 1: the standalone arithmetic of the A4
// mode-config surface.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/enc_mode_config.c.
// Every span is cited at its site; deviations are named where the wall
// threads as a parameter (the allowPalette / RE8A DeblockContext
// precedents).
#include "mode_config.h"

#include <algorithm>
#include <cmath>

namespace l16_rdo {
namespace mode_config {

// ---------------------------------------------------------------------------
// The :20-:24 statics.
// ---------------------------------------------------------------------------
// enc_mode_config.c:21, verbatim (the cdef primary/secondary
// filter-strength ladder, consumed by set_cdef_search_controls
// :919-:1171).
const std::uint8_t pfGi[16] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60};
// enc_mode_config.c:23, verbatim (consumed by set_intra_ctrls :6627+ and
// sig_deriv_pre_analysis_scs :2873).
const std::uint8_t angularPredLevel[kMaxIntraLevel] = {0, 1, 2, 2, 3, 4, 4, 4, 4, 0};

// svt_aom_get_qp_based_th_scaling_factors (enc_mode_config.c:26-54).
void getQpBasedThScalingFactors(bool enableQpBasedThScaling, std::uint32_t* retQWeight,
                                std::uint32_t* retQWeightDenom, std::uint32_t qp) {
    if (enableQpBasedThScaling) {
        // :30-31 - limit scaling for low QPs to 10/63 (10 chosen
        // arbitrarily); MAX_QP_VALUE = 63 (definitions.h:1646).
        std::uint32_t qWeight       = std::max(10u, qp);
        std::uint32_t qWeightDenom  = kMaxQpValue;
        // :38-46 - the QP 46 boundary: Scaling(45) = 0.714 linear vs
        // Scaling(46) = 0.717 exponential, chosen so there is no large
        // jump (the reference's own comment). The MAX folds (double)qp.
        if (qp >= 46) {
            double ex          = -(std::max(40.0, (double)qp) - 35) / 10;
            double qWeightInt  = std::exp(ex);
            qWeightInt         = 1.05 - qWeightInt;
            qWeightInt        *= 10000;
            qWeight            = (std::uint32_t)qWeightInt;
            qWeightDenom       = 10000;
        }
        *retQWeight       = qWeight;
        *retQWeightDenom  = qWeightDenom;
    } else {
        *retQWeight       = 1;
        *retQWeightDenom  = 1;
        return;
    }
}

// svt_aom_get_wn_filter_level_default (enc_mode_config.c:1365-1381).
std::uint8_t getWnFilterLevelDefault(EncMode encMode, std::uint8_t inputResolution, bool isNotLastLayer) {
    std::uint8_t wnFilterLvl = 0;
    if (encMode <= ENC_M3) {
        wnFilterLvl = isNotLastLayer ? 4 : 0;
    } else if (encMode <= ENC_M8) {
        wnFilterLvl = isNotLastLayer ? 5 : 0;
    } else {
        wnFilterLvl = 0;
    }
    // Disable wiener restoration for resolutions 8K and above (memory
    // constraints), :1375-1378.
    if (inputResolution >= INPUT_SIZE_8K_RANGE) {
        wnFilterLvl = 0;
    }
    return wnFilterLvl;
}

// svt_aom_get_wn_filter_level_rtc (enc_mode_config.c:1383-1392).
std::uint8_t getWnFilterLevelRtc(std::uint8_t inputResolution) {
    std::uint8_t wnFilterLvl = 0;
    wnFilterLvl              = 0;
    // The 8K gate is a no-op at 0 (:1386-1389); kept verbatim.
    if (inputResolution >= INPUT_SIZE_8K_RANGE) {
        wnFilterLvl = 0;
    }
    return wnFilterLvl;
}

// svt_aom_get_wn_filter_level_allintra (enc_mode_config.c:1394-1407).
std::uint8_t getWnFilterLevelAllIntra(EncMode encMode) {
    std::uint8_t wnFilterLvl;
    if (encMode <= ENC_M0) {
        wnFilterLvl = 3;
    } else if (encMode <= ENC_M3) {
        wnFilterLvl = 3;
    } else if (encMode <= ENC_M6) {
        wnFilterLvl = 4;
    } else {
        wnFilterLvl = 0;
    }
    return wnFilterLvl;
}

// svt_aom_get_sg_filter_level_default (enc_mode_config.c:1410-1426).
std::uint8_t getSgFilterLevelDefault(EncMode encMode, std::uint8_t inputResolution, std::uint8_t fastDecode) {
    std::uint8_t sgFilterLvl;
    if (encMode <= ENC_MR) {
        sgFilterLvl = 1;
    } else if (encMode <= ENC_M3) {
        sgFilterLvl = 3;
    } else {
        sgFilterLvl = 0;
    }
    // Disable self-guided restoration for resolutions 8K and above, and
    // for fast-decode above 360p (:1420-1423).
    if ((inputResolution >= INPUT_SIZE_8K_RANGE) || (fastDecode && !(inputResolution <= INPUT_SIZE_360p_RANGE))) {
        sgFilterLvl = 0;
    }
    return sgFilterLvl;
}

// svt_aom_get_sg_filter_level_rtc (enc_mode_config.c:1428-1437).
std::uint8_t getSgFilterLevelRtc(std::uint8_t inputResolution, std::uint8_t fastDecode) {
    std::uint8_t sgFilterLvl;
    sgFilterLvl = 0;
    // The gate is a no-op at 0 (:1431-1434); kept verbatim.
    if ((inputResolution >= INPUT_SIZE_8K_RANGE) || (fastDecode && !(inputResolution <= INPUT_SIZE_360p_RANGE))) {
        sgFilterLvl = 0;
    }
    return sgFilterLvl;
}

// svt_aom_get_sg_filter_level_allintra (enc_mode_config.c:1439-1448).
std::uint8_t getSgFilterLevelAllIntra(EncMode encMode) {
    std::uint8_t sgFilterLvl;
    if (encMode <= ENC_MR) {
        sgFilterLvl = 1;
    } else {
        sgFilterLvl = 0;
    }
    return sgFilterLvl;
}

// dlf_level_modulation (enc_mode_config.c:1450-1472).
void dlfLevelModulation(const DlfPcs& pcs, std::uint8_t* defaultDlfLevel, std::uint8_t modulationMode) {
    std::uint8_t dlfLevel = *defaultDlfLevel;

    if (modulationMode == 1 || modulationMode == 2) {
        if (pcs.refSkipPercentage < 25) {
            dlfLevel = dlfLevel == 0 ? 6 : dlfLevel > 5 ? (std::uint8_t)std::max(5, (int)dlfLevel - 2) : dlfLevel;
        } else if (pcs.refSkipPercentage < 50) {
            dlfLevel = dlfLevel == 0 ? 7 : dlfLevel > 5 ? (std::uint8_t)(dlfLevel - 1) : dlfLevel;
        }
    }

    if (modulationMode == 2 || modulationMode == 3) {
        if (dlfLevel > 4) {
            if (pcs.refSkipPercentage > 95) {
                dlfLevel = dlfLevel >= 6 ? 0 : (std::uint8_t)(dlfLevel + 2);
            } else if (pcs.refSkipPercentage > 75) {
                dlfLevel = dlfLevel == 7 ? 0 : (std::uint8_t)(dlfLevel + 1);
            }
        }
    }

    *defaultDlfLevel = dlfLevel;
}

// get_dlf_level_default (enc_mode_config.c:1474-1523).
std::uint8_t getDlfLevelDefault(const DlfPcs& pcs, EncMode encMode, std::uint8_t isNotLastLayer,
                                std::uint8_t fastDecode, ResolutionRange resolution, int isBase) {
    std::uint8_t dlfLevel       = 0;
    std::uint8_t modulationMode = 0;  // 0: off, 1: towards bd-rate, 2: both sides, 3: towards speed

    if (fastDecode <= 1 || resolution <= INPUT_SIZE_360p_RANGE) {  // fast-decode 0/1
        if (encMode <= ENC_M0) {
            dlfLevel = 1;
        } else if (encMode <= ENC_M3) {
            dlfLevel = 2;
        } else if (encMode <= ENC_M6) {
            dlfLevel = isNotLastLayer ? 3 : 6;
        } else if (encMode <= ENC_M7) {
            dlfLevel       = isNotLastLayer ? 3 : 6;
            modulationMode = 3;
        } else if (encMode <= ENC_M9) {
            dlfLevel       = isNotLastLayer ? 6 : 0;
            modulationMode = 3;
        } else if (encMode <= ENC_M11) {
            if (pcs.coeffLvl == HIGH_LVL) {
                dlfLevel = isBase ? 6 : 0;
            } else {
                dlfLevel = isBase ? 6 : isNotLastLayer ? 7 : 0;
            }
            modulationMode = 3;
        } else {
            dlfLevel       = 0;
            modulationMode = 3;
        }
    } else {  // fast-decode 2
        if (encMode <= ENC_M6) {
            dlfLevel = 4;
        } else if (encMode <= ENC_M7) {
            dlfLevel       = 6;
            modulationMode = 3;
        } else if (encMode <= ENC_M10) {
            dlfLevel       = isNotLastLayer ? 6 : 0;
            modulationMode = 3;
        } else {
            dlfLevel       = isNotLastLayer ? 7 : 0;
            modulationMode = 3;
        }
    }

    if (!isBase) {
        dlfLevelModulation(pcs, &dlfLevel, modulationMode);
    }

    return dlfLevel;
}

// get_dlf_level_rtc (enc_mode_config.c:1525-1546).
std::uint8_t getDlfLevelRtc(const DlfPcs& pcs, EncMode encMode, int isBase) {
    std::uint8_t dlfLevel       = 0;
    std::uint8_t modulationMode = 0;
    if (encMode <= ENC_M7) {
        dlfLevel       = 3;
        modulationMode = 1;
    } else if (encMode <= ENC_M9) {
        dlfLevel       = 6;
        modulationMode = 3;
    } else if (encMode <= ENC_M10) {
        dlfLevel       = 7;
        modulationMode = 3;
    } else {
        dlfLevel       = 0;
        modulationMode = 3;
    }
    if (!isBase) {
        dlfLevelModulation(pcs, &dlfLevel, modulationMode);
    }
    return dlfLevel;
}

// get_dlf_level_allintra (enc_mode_config.c:1548-1567).
std::uint8_t getDlfLevelAllIntra(EncMode encMode, std::uint8_t fastDecode, ResolutionRange resolution) {
    std::uint8_t dlfLevel = 0;
    if (fastDecode <= 1 || resolution <= INPUT_SIZE_360p_RANGE) {
        if (encMode <= ENC_M3) {
            dlfLevel = 1;
        } else if (encMode <= ENC_M5) {
            dlfLevel = 2;
        } else {
            dlfLevel = 5;
        }
    } else {
        if (encMode <= ENC_M7) {
            dlfLevel = 0;
        } else {
            dlfLevel = 5;
        }
    }
    return dlfLevel;
}

// The :2589-2591 correspondence fold, factored for testability (the
// cdefSign precedent). Cases 3/4 inline the same expression over
// pcs->input_resolution.
std::uint8_t getGmCorrespondence(std::uint8_t inputResolution) {
    return inputResolution <= INPUT_SIZE_480p_RANGE ? (std::uint8_t)MV_8x8
         : inputResolution <= INPUT_SIZE_1080p_RANGE ? (std::uint8_t)MV_16x16
                                                     : (std::uint8_t)MV_32x32;
}

// svt_aom_set_gm_controls (enc_mode_config.c:2523-2625). NAMED DEVIATION:
// CONFIG_ENABLE_GLOBAL_MOTION threads as the runtime parameter - the
// pinned/generator build configures neither RTC_BUILD nor MINIMAL_BUILD
// (tools/golden_gen/extract.ps1:378-380) so the macro resolves to 1
// there (EbConfigMacros.h:182-183); the 0 default is the RTC+MINIMAL
// arm (:61-63). Both arms are carried (the allowPalette precedent). The
// switch's default case assert(0)s in the reference and writes
// nothing; the port leaves the struct untouched. Precondition
// (:2621-2623): when gm_level != 0 the case bodies set match_sz to 13 or
// 7, both odd - the reference asserts it.
void setGmControls(GmPpcs& pcs, std::uint8_t gmLevel, int configEnableGlobalMotion) {
    GmControls* gmCtrls = &pcs.gmCtrls;
    if (!configEnableGlobalMotion) {
        (void)gmLevel;
        gmCtrls->enabled    = 0;
        gmCtrls->ppEnabled  = false;
        return;
    }
    switch (gmLevel) {
    case 0:
        gmCtrls->enabled    = 0;
        gmCtrls->ppEnabled  = false;
        break;

    case 1:
        gmCtrls->enabled                 = 1;
        gmCtrls->identiyExit             = 0;
        gmCtrls->searchStartModel        = TRANSLATION;
        gmCtrls->searchEndModel          = AFFINE;
        gmCtrls->skipIdentity            = 0;
        gmCtrls->bypassBasedOnMe         = 0;
        gmCtrls->paramsRefinementSteps   = 5;
        gmCtrls->downsampleLevel         = GM_FULL;

        gmCtrls->corners                 = 4;
        gmCtrls->chessRfn                = 0;
        gmCtrls->matchSz                 = 13;
        gmCtrls->injPsqGlb               = false;
        gmCtrls->ppEnabled               = false;
        gmCtrls->refIdx0Only             = false;
        gmCtrls->rfnEarlyExit            = 0;
        gmCtrls->correspondenceMethod    = CORNERS;
        break;
    case 2:
        gmCtrls->enabled                 = 1;
        gmCtrls->identiyExit             = 1;
        gmCtrls->searchStartModel        = TRANSLATION;
        gmCtrls->searchEndModel          = ROTZOOM;
        gmCtrls->skipIdentity            = 0;
        gmCtrls->bypassBasedOnMe         = 0;
        gmCtrls->paramsRefinementSteps   = 5;
        gmCtrls->downsampleLevel         = GM_FULL;
        gmCtrls->corners                 = 2;
        gmCtrls->chessRfn                = 0;
        gmCtrls->matchSz                 = 7;
        gmCtrls->injPsqGlb               = false;
        gmCtrls->ppEnabled               = true;
        gmCtrls->refIdx0Only             = false;
        gmCtrls->rfnEarlyExit            = 0;
        gmCtrls->correspondenceMethod    = CORNERS;
        break;
    case 3:
        gmCtrls->enabled                 = 1;
        gmCtrls->identiyExit             = 1;
        gmCtrls->searchStartModel        = TRANSLATION;
        gmCtrls->searchEndModel          = ROTZOOM;
        gmCtrls->skipIdentity            = 0;
        gmCtrls->bypassBasedOnMe         = 1;
        gmCtrls->paramsRefinementSteps   = 5;
        gmCtrls->downsampleLevel         = GM_FULL;
        gmCtrls->corners                 = 2;
        gmCtrls->chessRfn                = 1;
        gmCtrls->matchSz                 = 7;
        gmCtrls->injPsqGlb               = true;
        gmCtrls->ppEnabled               = false;
        gmCtrls->refIdx0Only             = true;
        gmCtrls->rfnEarlyExit            = 1;
        gmCtrls->correspondenceMethod    = getGmCorrespondence(pcs.inputResolution);
        break;
    case 4:
        gmCtrls->enabled                 = 1;
        gmCtrls->identiyExit             = 1;
        gmCtrls->searchStartModel        = TRANSLATION;
        gmCtrls->searchEndModel          = ROTZOOM;
        gmCtrls->skipIdentity            = 1;
        gmCtrls->bypassBasedOnMe         = 1;
        gmCtrls->paramsRefinementSteps   = 5;
        gmCtrls->downsampleLevel         = GM_FULL;
        gmCtrls->corners                 = 2;
        gmCtrls->chessRfn                = 1;
        gmCtrls->matchSz                 = 7;
        gmCtrls->injPsqGlb               = true;
        gmCtrls->ppEnabled               = false;
        gmCtrls->refIdx0Only             = true;
        gmCtrls->rfnEarlyExit            = 1;
        gmCtrls->correspondenceMethod    = getGmCorrespondence(pcs.inputResolution);
        break;
    default:
        // The reference assert(0)s (:2613-2615) and writes nothing.
        break;
    }
    if (gmCtrls->correspondenceMethod < CORNERS) {
        // MV-based correspondence methods rely on ME info, unavailable in
        // the pre-processor stage (:2617-2620).
        gmCtrls->ppEnabled = false;
    }
}

// ---------------------------------------------------------------------------
// EMC1 slice 2, batch A: the ME/HME setter families
// (enc_mode_config.c:223-:897).
// ---------------------------------------------------------------------------

// DIVIDE_AND_ROUND (utility.h, verbatim).
#define DIVIDE_AND_ROUND(x, y) (((x) + ((y) >> 1)) / (y))

// SUB_SAD_SEARCH / FULL_SAD_SEARCH (definitions.h:1805-1806) and
// BLOCK_SIZE_64 (definitions.h:2017), verbatim.
#define SUB_SAD_SEARCH 0
#define FULL_SAD_SEARCH 1
#define BLOCK_SIZE_64 64u

// set_hme_search_params (enc_mode_config.c:223-283). NAMED DEVIATION: the
// reference takes PictureParentControlSet* and chains to pcs->scs; the
// port folds the consumed wall fields into MeModeCfg (the carrier's field
// comments carry the paths). The rtc/qp/scaling reads are the SCS surface
// (:224/:263-266), the enc-mode reads the PCS surface (:225/:276).
void setHmeSearchParams(const MeModeCfg& scs, const MeModeCfg& pcs, MeSearchParams& meCtx,
                        ResolutionRange inputResolution) {
    const bool    rtcTune = scs.rtcTune;
    const EncMode encMode = pcs.encMode;
    // Set number of HME level 0 search regions to use (:227-228).
    meCtx.numHmeSaW = 2;
    meCtx.numHmeSaH = 2;
    // Set HME level 0 min and max search areas (:230-260).
    if (encMode <= ENC_M1) {
        if (inputResolution < INPUT_SIZE_4K_RANGE) {
            meCtx.hmeL0Sa.saMin = SearchArea{32, 32};
            meCtx.hmeL0Sa.saMax = SearchArea{192, 192};
        } else {
            meCtx.hmeL0Sa.saMin = SearchArea{240, 240};
            meCtx.hmeL0Sa.saMax = SearchArea{480, 480};
        }
    } else if (encMode <= ENC_M5) {
        meCtx.hmeL0Sa.saMin = SearchArea{32, 32};
        meCtx.hmeL0Sa.saMax = SearchArea{192, 192};
    } else if (!rtcTune && encMode <= ENC_M7) {
        if (inputResolution >= INPUT_SIZE_4K_RANGE) {
            meCtx.hmeL0Sa.saMin = SearchArea{32, 32};
            meCtx.hmeL0Sa.saMax = SearchArea{192, 192};
        } else {
            meCtx.hmeL0Sa.saMin = SearchArea{16, 16};
            meCtx.hmeL0Sa.saMax = SearchArea{192, 192};
        }
    } else if (encMode <= ENC_M8) {
        meCtx.hmeL0Sa.saMin = SearchArea{16, 16};
        meCtx.hmeL0Sa.saMax = SearchArea{192, 192};
    } else {
        if (inputResolution < INPUT_SIZE_4K_RANGE) {
            meCtx.hmeL0Sa.saMin = SearchArea{8, 8};
            meCtx.hmeL0Sa.saMax = SearchArea{96, 96};
        } else {
            meCtx.hmeL0Sa.saMin = SearchArea{16, 16};
            meCtx.hmeL0Sa.saMax = SearchArea{96, 96};
        }
    }
    // Modulate the HME search-area using qp (:261-274).
    std::uint32_t qWeight, qWeightDenom;
    getQpBasedThScalingFactors(scs.hmeQpBasedThScaling, &qWeight, &qWeightDenom, scs.qp);
    meCtx.hmeL0Sa.saMin.width  = (std::uint16_t)std::max<std::uint32_t>(
        8u, DIVIDE_AND_ROUND(meCtx.hmeL0Sa.saMin.width * qWeight, qWeightDenom));
    meCtx.hmeL0Sa.saMin.height = (std::uint16_t)std::max<std::uint32_t>(
        8u, DIVIDE_AND_ROUND(meCtx.hmeL0Sa.saMin.height * qWeight, qWeightDenom));
    meCtx.hmeL0Sa.saMax.width  = (std::uint16_t)std::max<std::uint32_t>(
        96u, DIVIDE_AND_ROUND(meCtx.hmeL0Sa.saMax.width * qWeight, qWeightDenom));
    meCtx.hmeL0Sa.saMax.height = (std::uint16_t)std::max<std::uint32_t>(
        96u, DIVIDE_AND_ROUND(meCtx.hmeL0Sa.saMax.height * qWeight, qWeightDenom));
    // Set the HME Level 1 and Level 2 refinement areas (:276-282).
    if (encMode <= ENC_MR) {
        meCtx.hmeL1Sa = SearchArea{16, 16};
        meCtx.hmeL2Sa = SearchArea{16, 16};
    } else {
        meCtx.hmeL1Sa = SearchArea{8, 3};
        meCtx.hmeL2Sa = SearchArea{8, 3};
    }
}

// set_me_search_params (enc_mode_config.c:288-351). The reference reads
// enc_mode + hierarchical_levels from PCS (:290/:295) and rtc + the
// qp-scaling pair from SCS (:291/:344-346).
void setMeSearchParams(const MeModeCfg& scs, const MeModeCfg& pcs, MeSearchParams& meCtx,
                       ResolutionRange inputResolution) {
    const EncMode encMode   = pcs.encMode;
    const bool    rtcTune   = scs.rtcTune;

    // Set the min and max ME search area (:294-343).
    if (rtcTune) {
        const bool useFlatIpp = pcs.hierarchicalLevels == 0;
        if (encMode <= ENC_M10 || (encMode == ENC_M11 && !useFlatIpp)) {
            if (inputResolution < INPUT_SIZE_1080p_RANGE) {
                meCtx.meSa.saMin = SearchArea{24, 16};
                meCtx.meSa.saMax = SearchArea{32, 16};
            } else {
                meCtx.meSa.saMin = SearchArea{16, 9};
                meCtx.meSa.saMax = SearchArea{16, 9};
            }
        } else {
            meCtx.meSa.saMin = SearchArea{8, 1};
            meCtx.meSa.saMax = SearchArea{8, 1};
        }
    } else if (encMode <= ENC_MR) {
        meCtx.meSa.saMin = SearchArea{96, 96};
        meCtx.meSa.saMax = SearchArea{256, 256};
    } else if (encMode <= ENC_M1) {
        meCtx.meSa.saMin = SearchArea{84, 84};
        meCtx.meSa.saMax = SearchArea{224, 224};
    } else if (encMode <= ENC_M3) {
        meCtx.meSa.saMin = SearchArea{24, 24};
        meCtx.meSa.saMax = SearchArea{88, 88};
    } else if (encMode <= ENC_M4) {
        meCtx.meSa.saMin = SearchArea{24, 12};
        meCtx.meSa.saMax = SearchArea{48, 32};
    } else if (encMode <= ENC_M7) {
        meCtx.meSa.saMin = SearchArea{24, 9};
        meCtx.meSa.saMax = SearchArea{24, 12};
    } else if (encMode <= ENC_M8) {
        meCtx.meSa.saMin = SearchArea{16, 6};
        meCtx.meSa.saMax = SearchArea{24, 12};
    } else if (encMode <= ENC_M11) {
        if (inputResolution < INPUT_SIZE_720p_RANGE) {
            meCtx.meSa.saMin = SearchArea{12, 4};
            meCtx.meSa.saMax = SearchArea{16, 9};
        } else if (inputResolution < INPUT_SIZE_1080p_RANGE) {
            meCtx.meSa.saMin = SearchArea{12, 1};
            meCtx.meSa.saMax = SearchArea{16, 7};
        } else if (inputResolution < INPUT_SIZE_4K_RANGE) {
            meCtx.meSa.saMin = SearchArea{8, 1};
            meCtx.meSa.saMax = SearchArea{8, 7};
        } else {
            meCtx.meSa.saMin = SearchArea{8, 1};
            meCtx.meSa.saMax = SearchArea{8, 1};
        }
    } else {
        meCtx.meSa.saMin = SearchArea{8, 1};
        meCtx.meSa.saMax = SearchArea{8, 1};
    }
    std::uint32_t qWeight, qWeightDenom;
    getQpBasedThScalingFactors(scs.meQpBasedThScaling, &qWeight, &qWeightDenom, scs.qp);
    meCtx.meSa.saMin.width  = (std::uint16_t)std::max<std::uint32_t>(
        8u, DIVIDE_AND_ROUND(meCtx.meSa.saMin.width * qWeight, qWeightDenom));
    meCtx.meSa.saMin.height = (std::uint16_t)std::max<std::uint32_t>(
        3u, DIVIDE_AND_ROUND(meCtx.meSa.saMin.height * qWeight, qWeightDenom));
    meCtx.meSa.saMax.width  = (std::uint16_t)std::max<std::uint32_t>(
        8u, DIVIDE_AND_ROUND(meCtx.meSa.saMax.width * qWeight, qWeightDenom));
    meCtx.meSa.saMax.height = (std::uint16_t)std::max<std::uint32_t>(
        3u, DIVIDE_AND_ROUND(meCtx.meSa.saMax.height * qWeight, qWeightDenom));
}

// svt_aom_set_me_hme_ref_prune_ctrls (enc_mode_config.c:353-434).
void setMeHmeRefPruneCtrls(MeSearchParams& meCtx, std::uint8_t pruneLevel) {
    MeHmeRefPruneCtrls* meHmePruneCtrls = &meCtx.meHmePruneCtrls;

    switch (pruneLevel) {
    case 0:
        meHmePruneCtrls->enableMeHmeRefPruning               = false;
        meHmePruneCtrls->pruneRefIfHmeSadDevBiggerThanTh     = (std::uint16_t)~0;
        meHmePruneCtrls->pruneRefIfMeSadDevBiggerThanTh      = (std::uint16_t)~0;

        meHmePruneCtrls->zzSadTh                             = 0;
        meHmePruneCtrls->zzSadPct                            = 0;
        meHmePruneCtrls->phmeSadTh                           = 0;
        meHmePruneCtrls->phmeSadPct                          = 0;
        break;
    case 1:
        meHmePruneCtrls->enableMeHmeRefPruning               = true;
        meHmePruneCtrls->pruneRefIfHmeSadDevBiggerThanTh     = 80;
        meHmePruneCtrls->pruneRefIfMeSadDevBiggerThanTh      = (std::uint16_t)~0;

        meHmePruneCtrls->zzSadTh                             = 0;
        meHmePruneCtrls->zzSadPct                            = 0;
        meHmePruneCtrls->phmeSadTh                           = 0;
        meHmePruneCtrls->phmeSadPct                          = 0;
        break;
    case 2:
        meHmePruneCtrls->enableMeHmeRefPruning               = true;
        meHmePruneCtrls->pruneRefIfHmeSadDevBiggerThanTh     = 50;
        meHmePruneCtrls->pruneRefIfMeSadDevBiggerThanTh      = (std::uint16_t)~0;

        meHmePruneCtrls->zzSadTh                             = 0;
        meHmePruneCtrls->zzSadPct                            = 0;
        meHmePruneCtrls->phmeSadTh                           = 0;
        meHmePruneCtrls->phmeSadPct                          = 0;
        break;

    case 3:
        meHmePruneCtrls->enableMeHmeRefPruning               = true;
        meHmePruneCtrls->pruneRefIfHmeSadDevBiggerThanTh     = 30;
        meHmePruneCtrls->pruneRefIfMeSadDevBiggerThanTh      = (std::uint16_t)~0;

        meHmePruneCtrls->zzSadTh                             = 0;
        meHmePruneCtrls->zzSadPct                            = 0;
        meHmePruneCtrls->phmeSadTh                           = 0;
        meHmePruneCtrls->phmeSadPct                          = 0;
        break;
    case 4:
        meHmePruneCtrls->enableMeHmeRefPruning               = true;
        meHmePruneCtrls->pruneRefIfHmeSadDevBiggerThanTh     = 15;
        meHmePruneCtrls->pruneRefIfMeSadDevBiggerThanTh      = 60;

        meHmePruneCtrls->zzSadTh                             = 0;
        meHmePruneCtrls->zzSadPct                            = 0;
        meHmePruneCtrls->phmeSadTh                           = 0;
        meHmePruneCtrls->phmeSadPct                          = 0;
        break;

    case 5:
        meHmePruneCtrls->enableMeHmeRefPruning               = true;
        meHmePruneCtrls->pruneRefIfHmeSadDevBiggerThanTh     = 5;
        meHmePruneCtrls->pruneRefIfMeSadDevBiggerThanTh      = 60;

        meHmePruneCtrls->zzSadTh                             = 0;
        meHmePruneCtrls->zzSadPct                            = 0;
        meHmePruneCtrls->phmeSadTh                           = 0;
        meHmePruneCtrls->phmeSadPct                          = 0;
        break;
    case 6:
        meHmePruneCtrls->enableMeHmeRefPruning               = true;
        meHmePruneCtrls->pruneRefIfHmeSadDevBiggerThanTh     = 5;
        meHmePruneCtrls->pruneRefIfMeSadDevBiggerThanTh      = 60;

        meHmePruneCtrls->zzSadTh                             = 20 * 64 * 64;
        meHmePruneCtrls->zzSadPct                            = 5;
        meHmePruneCtrls->phmeSadTh                           = 10 * 64 * 64;
        meHmePruneCtrls->phmeSadPct                          = 5;
        break;
    default:
        // The reference assert(0)s (:430-432) and writes nothing.
        break;
    }
}

// svt_aom_set_mv_based_sa_ctrls (enc_mode_config.c:436-459).
void setMvBasedSaCtrls(MeSearchParams& meCtx, std::uint8_t mvSaAdjLevel) {
    MvBasedSearchAdj* mvSaAdjCtrls = &meCtx.mvBasedSaAdj;

    switch (mvSaAdjLevel) {
    case 0:
        mvSaAdjCtrls->enabled = false;
        break;
    case 1:
        mvSaAdjCtrls->enabled          = true;
        mvSaAdjCtrls->nearestRefOnly   = false;
        mvSaAdjCtrls->mvSizeTh         = 25;
        mvSaAdjCtrls->saMultiplier     = 2;
        break;
    case 2:
        mvSaAdjCtrls->enabled          = true;
        mvSaAdjCtrls->nearestRefOnly   = true;
        mvSaAdjCtrls->mvSizeTh         = 25;
        mvSaAdjCtrls->saMultiplier     = 2;
        break;
    default:
        // The reference assert(0)s (:455-457) and writes nothing.
        break;
    }
}

// svt_aom_set_me_sr_adjustment_ctrls (enc_mode_config.c:461-532).
void setMeSrAdjustmentCtrls(MeSearchParams& meCtx, std::uint8_t srAdjustmentLevel) {
    MeSrCtrls* meSrAdjustmentCtrls = &meCtx.meSrAdjustmentCtrls;

    switch (srAdjustmentLevel) {
    case 0:
        meSrAdjustmentCtrls->enableMeSrAdjustment = 0;
        break;
    case 1:
        meSrAdjustmentCtrls->enableMeSrAdjustment              = 1;
        meSrAdjustmentCtrls->reduceMeSrBasedOnMvLengthTh       = 4;
        meSrAdjustmentCtrls->stationaryHmeSadAbsTh             = 12000;
        meSrAdjustmentCtrls->stationaryMeSrDivisor             = 8;
        meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh      = 6000;
        meSrAdjustmentCtrls->meSrDivisorForLowHmeSad           = 8;
        meSrAdjustmentCtrls->distanceBasedHmeResizing          = 0;
        break;
    case 2:
        meSrAdjustmentCtrls->enableMeSrAdjustment              = 1;
        meSrAdjustmentCtrls->reduceMeSrBasedOnMvLengthTh       = 4;
        meSrAdjustmentCtrls->stationaryHmeSadAbsTh             = 12000;
        meSrAdjustmentCtrls->stationaryMeSrDivisor             = 8;
        meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh      = 6000;
        meSrAdjustmentCtrls->meSrDivisorForLowHmeSad           = 8;
        meSrAdjustmentCtrls->distanceBasedHmeResizing          = 1;
        break;
    case 3:
        meSrAdjustmentCtrls->enableMeSrAdjustment              = 1;
        meSrAdjustmentCtrls->reduceMeSrBasedOnMvLengthTh       = 4;
        meSrAdjustmentCtrls->stationaryHmeSadAbsTh             = 12000;
        meSrAdjustmentCtrls->stationaryMeSrDivisor             = 8;
        meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh      = 12000;
        meSrAdjustmentCtrls->meSrDivisorForLowHmeSad           = 8;
        meSrAdjustmentCtrls->distanceBasedHmeResizing          = 1;
        break;
    case 4:
        meSrAdjustmentCtrls->enableMeSrAdjustment              = 2;
        meSrAdjustmentCtrls->reduceMeSrBasedOnMvLengthTh       = 16;
        meSrAdjustmentCtrls->stationaryHmeSadAbsTh             = 20000;
        meSrAdjustmentCtrls->stationaryMeSrDivisor             = 8;
        meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh      = 20000;
        meSrAdjustmentCtrls->meSrDivisorForLowHmeSad           = 8;
        meSrAdjustmentCtrls->distanceBasedHmeResizing          = 1;
        break;

    case 5:
        meSrAdjustmentCtrls->enableMeSrAdjustment              = 2;
        meSrAdjustmentCtrls->reduceMeSrBasedOnMvLengthTh       = 20;
        meSrAdjustmentCtrls->stationaryHmeSadAbsTh             = 24000;
        meSrAdjustmentCtrls->stationaryMeSrDivisor             = 8;
        meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh      = 24000;
        meSrAdjustmentCtrls->meSrDivisorForLowHmeSad           = 8;
        meSrAdjustmentCtrls->distanceBasedHmeResizing          = 1;
        break;

    default:
        // The reference assert(0)s (:517-519) and writes nothing.
        break;
    }
    if (meCtx.enableHmeLevel2Flag == false) {
        if (meCtx.enableHmeLevel1Flag == true) {
            meSrAdjustmentCtrls->stationaryHmeSadAbsTh =
                (std::uint16_t)(meSrAdjustmentCtrls->stationaryHmeSadAbsTh / 4);
            meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh =
                (std::uint16_t)(meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh / 4);
        } else {
            meSrAdjustmentCtrls->stationaryHmeSadAbsTh =
                (std::uint16_t)(meSrAdjustmentCtrls->stationaryHmeSadAbsTh / 16);
            meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh =
                (std::uint16_t)(meSrAdjustmentCtrls->reduceMeSrBasedOnHmeSadAbsTh / 16);
        }
    }
}

// svt_aom_set_me_8x8_var_ctrls (enc_mode_config.c:534-556).
void setMe8x8VarCtrls(MeSearchParams& meCtx, std::uint8_t level) {
    Me8x8VarCtrls* me8x8VarCtrls = &meCtx.me8x8VarCtrls;

    switch (level) {
    case 0:
        me8x8VarCtrls->enabled = 0;
        break;
    case 1:
        me8x8VarCtrls->enabled        = 1;
        me8x8VarCtrls->meSrDiv4Th     = 0;
        me8x8VarCtrls->meSrDiv2Th     = 0;
        me8x8VarCtrls->meSrMult2Th    = 900000;
        break;
    case 2:
        me8x8VarCtrls->enabled        = 1;
        me8x8VarCtrls->meSrDiv4Th     = 80000;
        me8x8VarCtrls->meSrDiv2Th     = 150000;
        me8x8VarCtrls->meSrMult2Th    = (std::uint32_t)~0;
        break;
    default:
        // The reference assert(0)s (no break, :553-555) and writes nothing.
        break;
    }
}

// svt_aom_set_prehme_ctrls (enc_mode_config.c:559-614).
void setPrehmeCtrls(MeSearchParams& meCtx, std::uint8_t level) {
    PreHmeCtrls* ctrl = &meCtx.prehmeCtrl;

    switch (level) {
    case 0:
        ctrl->enable = 0;
        break;
    case 1:
        ctrl->enable = 1;
        // vertical shape search region
        ctrl->prehmeSaCfg[0].saMin = SearchArea{8, 144};
        ctrl->prehmeSaCfg[0].saMax = SearchArea{8, 496};
        // horizontal shape search region
        ctrl->prehmeSaCfg[1].saMin = SearchArea{144, 3};
        ctrl->prehmeSaCfg[1].saMax = SearchArea{496, 3};
        ctrl->skipSearchLine       = 0;
        ctrl->l1EarlyExit          = 0;
        break;
    case 2:
        ctrl->enable = 1;
        // vertical shape search region
        ctrl->prehmeSaCfg[0].saMin = SearchArea{8, 100};
        ctrl->prehmeSaCfg[0].saMax = SearchArea{8, 400};
        // horizontal shape search region
        ctrl->prehmeSaCfg[1].saMin = SearchArea{96, 3};
        ctrl->prehmeSaCfg[1].saMax = SearchArea{384, 3};
        ctrl->skipSearchLine       = 0;
        ctrl->l1EarlyExit          = 0;
        break;
    case 3:
        ctrl->enable = 1;
        // vertical shape search region
        ctrl->prehmeSaCfg[0].saMin = SearchArea{8, 100};
        ctrl->prehmeSaCfg[0].saMax = SearchArea{8, 350};
        // horizontal shape search region
        ctrl->prehmeSaCfg[1].saMin = SearchArea{32, 7};
        ctrl->prehmeSaCfg[1].saMax = SearchArea{200, 7};
        ctrl->skipSearchLine       = 1;
        ctrl->l1EarlyExit          = 0;
        break;
    case 4:
        ctrl->enable = 1;
        // vertical shape search region
        ctrl->prehmeSaCfg[0].saMin = SearchArea{8, 100};
        ctrl->prehmeSaCfg[0].saMax = SearchArea{8, 350};
        // horizontal shape search region
        ctrl->prehmeSaCfg[1].saMin = SearchArea{32, 7};
        ctrl->prehmeSaCfg[1].saMax = SearchArea{128, 7};
        ctrl->skipSearchLine       = 1;
        ctrl->l1EarlyExit          = 1;
        break;
    default:
        // The reference assert(0)s (:610-612) and writes nothing.
        break;
    }
}

// tf_set_me_hme_params_oq (enc_mode_config.c:619-701).
void tfSetMeHmeParamsOq(MeSearchParams& meCtx, const MeModeCfg& pcs) {
    switch (pcs.tfHmeMeLevel) {
    case 0:
        meCtx.numHmeSaW                     = 2;
        meCtx.numHmeSaH                     = 2;
        meCtx.hmeL0SaDefaultTf.saMin        = SearchArea{30, 30};
        meCtx.hmeL0SaDefaultTf.saMax        = SearchArea{60, 60};
        meCtx.hmeL1Sa                       = SearchArea{16, 16};
        meCtx.hmeL2Sa                       = SearchArea{16, 16};
        meCtx.meSa.saMin                    = SearchArea{60, 60};
        meCtx.meSa.saMax                    = SearchArea{120, 120};
        break;

    case 1:
        meCtx.numHmeSaW                     = 2;
        meCtx.numHmeSaH                     = 2;
        meCtx.hmeL0SaDefaultTf.saMin        = SearchArea{16, 16};
        meCtx.hmeL0SaDefaultTf.saMax        = SearchArea{32, 32};
        meCtx.hmeL1Sa                       = SearchArea{16, 16};
        meCtx.hmeL2Sa                       = SearchArea{16, 16};
        meCtx.meSa.saMin                    = SearchArea{16, 16};
        meCtx.meSa.saMax                    = SearchArea{32, 32};
        break;

    case 2:
        meCtx.numHmeSaW = 2;
        meCtx.numHmeSaH = 2;
        if (pcs.inputResolution <= INPUT_SIZE_360p_RANGE) {
            meCtx.hmeL0SaDefaultTf.saMin    = SearchArea{8, 8};
            meCtx.hmeL0SaDefaultTf.saMax    = SearchArea{8, 8};
            meCtx.hmeL1Sa                   = SearchArea{8, 8};
        } else if (pcs.inputResolution <= INPUT_SIZE_480p_RANGE) {
            meCtx.hmeL0SaDefaultTf.saMin    = SearchArea{8, 8};
            meCtx.hmeL0SaDefaultTf.saMax    = SearchArea{16, 16};
            meCtx.hmeL1Sa                   = SearchArea{8, 8};
        } else {
            meCtx.hmeL0SaDefaultTf.saMin    = SearchArea{16, 16};
            meCtx.hmeL0SaDefaultTf.saMax    = SearchArea{32, 32};
            meCtx.hmeL1Sa                   = SearchArea{16, 16};
        }
        meCtx.hmeL2Sa                       = SearchArea{16, 16};
        meCtx.meSa.saMin                    = SearchArea{8, 8};
        meCtx.meSa.saMax                    = SearchArea{8, 8};
        break;
    case 3:
        meCtx.numHmeSaW                     = 2;
        meCtx.numHmeSaH                     = 2;
        meCtx.hmeL0SaDefaultTf.saMin        = SearchArea{8, 8};
        meCtx.hmeL0SaDefaultTf.saMax        = SearchArea{8, 8};
        meCtx.hmeL1Sa                       = SearchArea{8, 8};
        meCtx.hmeL2Sa                       = SearchArea{8, 8};
        meCtx.meSa.saMin                    = SearchArea{8, 8};
        meCtx.meSa.saMax                    = SearchArea{8, 8};
        break;
    case 4:
        meCtx.numHmeSaW                     = 2;
        meCtx.numHmeSaH                     = 2;
        meCtx.hmeL0SaDefaultTf.saMin        = SearchArea{4, 4};
        meCtx.hmeL0SaDefaultTf.saMax        = SearchArea{4, 4};
        meCtx.hmeL1Sa                       = SearchArea{8, 8};
        meCtx.hmeL2Sa                       = SearchArea{8, 8};
        meCtx.meSa.saMin                    = SearchArea{8, 8};
        meCtx.meSa.saMax                    = SearchArea{8, 8};
        break;

    default:
        // The reference assert(0)s (:684-686) and writes nothing.
        break;
    }

    // Modulate the ME search-area using qp (:690-700).
    if (pcs.tfQpOpt) {
        std::uint32_t qWeight, qWeightDenom;
        getQpBasedThScalingFactors(pcs.tfMeQpBasedThScaling, &qWeight, &qWeightDenom, pcs.qp);
        meCtx.meSa.saMin.width  = (std::uint16_t)std::max<std::uint32_t>(
            8u, DIVIDE_AND_ROUND(meCtx.meSa.saMin.width * qWeight, qWeightDenom));
        meCtx.meSa.saMin.height = (std::uint16_t)std::max<std::uint32_t>(
            8u, DIVIDE_AND_ROUND(meCtx.meSa.saMin.height * qWeight, qWeightDenom));
        meCtx.meSa.saMax.width  = (std::uint16_t)std::max<std::uint32_t>(
            8u, DIVIDE_AND_ROUND(meCtx.meSa.saMax.width * qWeight, qWeightDenom));
        meCtx.meSa.saMax.height = (std::uint16_t)std::max<std::uint32_t>(
            8u, DIVIDE_AND_ROUND(meCtx.meSa.saMax.height * qWeight, qWeightDenom));
    }
}

// svt_aom_sig_deriv_me (enc_mode_config.c:708-849). NAMED DEVIATION: the
// scs and pcs walls both fold into MeModeCfg (disjoint field sets; the
// carrier field comments carry the paths); frame_is_boosted
// (enc_mode_config.h:100-110, the frame_is_kf_gf_arf disjunction) threads
// as pcs.frameIsBoosted.
void sigDerivMe(const MeModeCfg& scs, const MeModeCfg& pcs, MeSearchParams& meCtx) {
    EncMode         encMode         = pcs.encMode;
    const std::uint8_t scClass5     = pcs.scClass5;
    ResolutionRange inputResolution = scs.inputResolution;
    const bool      rtcTune         = scs.rtcTune;
    const bool      isBase          = pcs.frameIsBoosted;
    const bool      useFlatIpp      = rtcTune && pcs.hierarchicalLevels == 0;
    // Set ME search area (:716)
    setMeSearchParams(scs, pcs, meCtx, inputResolution);

    // Set HME search area (:719)
    setHmeSearchParams(scs, pcs, meCtx, inputResolution);

    // Set HME flags (:722-725)
    meCtx.enableHmeFlag        = pcs.enableHmeFlag;
    meCtx.enableHmeLevel0Flag  = pcs.enableHmeLevel0Flag;
    meCtx.enableHmeLevel1Flag  = pcs.enableHmeLevel1Flag;
    meCtx.enableHmeLevel2Flag  = pcs.enableHmeLevel2Flag;
    // HME Search Method (:727-728)
    meCtx.hmeSearchMethod      = SUB_SAD_SEARCH;
    meCtx.meSearchMethod       = SUB_SAD_SEARCH;

    meCtx.reduceHmeL0SrThMin   = 0;
    meCtx.reduceHmeL0SrThMax   = 0;
    // Set pre-hme level (0-2) (:733-753)
    std::uint8_t prehmeLevel = 0;
    if (rtcTune) {
        if (encMode <= ENC_M8) {
            prehmeLevel = 2;
        } else if (encMode <= ENC_M10 || (encMode == ENC_M11 && !useFlatIpp)) {
            prehmeLevel = 4;
        } else {
            prehmeLevel = 0;
        }
    } else {
        if (encMode <= ENC_M7) {
            prehmeLevel = 2;
        } else if (encMode <= ENC_M11) {
            prehmeLevel = 4;
        } else {
            prehmeLevel = 0;
        }
    }
    if (pcs.enableHmeLevel1Flag == false) {
        prehmeLevel = 0;
    }

    setPrehmeCtrls(meCtx, prehmeLevel);

    // Set hme/me based reference pruning level (0-4) (:759-783)
    std::uint8_t meRefPruneLevel = 0;

    if (rtcTune) {
        if (useFlatIpp) {
            meRefPruneLevel = 6;
        } else {
            if (encMode <= ENC_M7) {
                meRefPruneLevel = isBase ? 1 : 6;
            } else {
                meRefPruneLevel = 6;
            }
        }
    } else {
        if (encMode <= ENC_MR) {
            meRefPruneLevel = 0;
        } else if (encMode <= ENC_M0) {
            meRefPruneLevel = isBase ? 1 : 4;
        } else if (encMode <= ENC_M4) {
            meRefPruneLevel = isBase ? 1 : 5;
        } else if (encMode <= ENC_M8) {
            meRefPruneLevel = isBase ? 1 : 6;
        } else {
            meRefPruneLevel = 6;
        }
    }

    setMeHmeRefPruneCtrls(meCtx, meRefPruneLevel);

    // Set hme-based me sr adjustment level (:788-794)
    std::uint8_t meSrAdjLvl = 0;
    if (encMode <= ENC_M0) {
        meSrAdjLvl = 0;
    } else {
        meSrAdjLvl = 3;
    }
    setMeSrAdjustmentCtrls(meCtx, meSrAdjLvl);

    std::uint8_t mvSaAdjLevel = 0;
    if (encMode <= ENC_M0) {
        mvSaAdjLevel = 2;
    } else {
        mvSaAdjLevel = 0;
    }
    setMvBasedSaCtrls(meCtx, mvSaAdjLevel);

    std::uint8_t me8x8VarLvl = 2;
    setMe8x8VarCtrls(meCtx, me8x8VarLvl);
    if (encMode <= ENC_M1) {
        meCtx.pruneMeCandidatesTh = 0;
    } else {
        meCtx.pruneMeCandidatesTh = 65;
    }

    // Applies to sc-class1 & sc-class4 scenes (:813-823)
    if (scClass5) {
        if (encMode <= ENC_M2) {
            meCtx.scClassMeBoost = 1;
        } else if (encMode <= ENC_M5) {
            meCtx.scClassMeBoost = 2;
        } else {
            meCtx.scClassMeBoost = 0;
        }
    } else {
        meCtx.scClassMeBoost = 0;
    }
    // Set signal at picture level b/c may check signal in MD (:825)
    meCtx.useBestUnipredCandOnly = pcs.useBestMeUnipredCandOnly;
    if (rtcTune) {
        meCtx.meEarlyExitTh = BLOCK_SIZE_64 * BLOCK_SIZE_64 * 8;
    } else {
        if (encMode <= ENC_M2) {
            meCtx.meEarlyExitTh = 0;
        } else if (encMode <= ENC_M9) {
            meCtx.meEarlyExitTh = BLOCK_SIZE_64 * BLOCK_SIZE_64 * 8;
        } else {
            meCtx.meEarlyExitTh = BLOCK_SIZE_64 * BLOCK_SIZE_64 * 12;
        }
    }
    // Complete ME bypass for static 64x64 blocks (:840-844)
    if (rtcTune) {
        meCtx.meStaticB64Th = BLOCK_SIZE_64 * BLOCK_SIZE_64;
    } else {
        meCtx.meStaticB64Th = 0;
    }

    meCtx.meSafeLimitZzTh = scs.mrpSafeLimitNref == 1 ? scs.mrpSafeLimitZzTh : 0;

    meCtx.prevMeStageBasedExitTh = 0;
}

// svt_aom_sig_deriv_me_tf (enc_mode_config.c:856-897).
void sigDerivMeTf(const MeModeCfg& pcs, MeSearchParams& meCtx) {
    // Set ME/HME search regions (:858)
    tfSetMeHmeParamsOq(meCtx, pcs);
    // Set HME flags (:860-863)
    meCtx.enableHmeFlag        = pcs.tfEnableHmeFlag;
    meCtx.enableHmeLevel0Flag  = pcs.tfEnableHmeLevel0Flag;
    meCtx.enableHmeLevel1Flag  = pcs.tfEnableHmeLevel1Flag;
    meCtx.enableHmeLevel2Flag  = pcs.tfEnableHmeLevel2Flag;
    if (pcs.tfHmeMeLevel <= 2) {
        // HME Search Method (:866)
        meCtx.hmeSearchMethod = FULL_SAD_SEARCH;
        // ME Search Method (:868)
        meCtx.meSearchMethod  = FULL_SAD_SEARCH;
    } else {
        // HME Search Method (:871)
        meCtx.hmeSearchMethod = SUB_SAD_SEARCH;
        // ME Search Method (:873)
        meCtx.meSearchMethod  = SUB_SAD_SEARCH;
    }

    std::uint8_t prehmeLevel = 0;
    setPrehmeCtrls(meCtx, prehmeLevel);

    // Ref pruning is disallowed for TF in motion_estimate_sb() (:881)
    setMeHmeRefPruneCtrls(meCtx, 0);

    // ME SR adjustment is disallowed for TF in motion_estimate_sb() (:885)
    setMeSrAdjustmentCtrls(meCtx, 0);

    setMvBasedSaCtrls(meCtx, 0);

    setMe8x8VarCtrls(meCtx, 0);

    meCtx.scClassMeBoost           = 0;
    meCtx.meEarlyExitTh            = pcs.tfHmeMeLevel <= 1 ? 0 : BLOCK_SIZE_64 * BLOCK_SIZE_64 * 4;
    meCtx.meSafeLimitZzTh          = 0;
    meCtx.reduceHmeL0SrThMin       = 0;
    meCtx.reduceHmeL0SrThMax       = 0;
    meCtx.prevMeStageBasedExitTh   = pcs.tfHmeMeLevel <= 1 ? 0 : BLOCK_SIZE_64 * BLOCK_SIZE_64 * 4;
}

// ---------------------------------------------------------------------------
// Batch B: the cdef search/recon setter family.
// ---------------------------------------------------------------------------

// set_cdef_search_controls (enc_mode_config.c:899-1206), 1:1. The walls
// are threaded: isBase = frame_is_boosted(pcs) (:901), isNotHighestLayer
// = !frame_is_leaf(pcs) (:902).
void setCdefSearchCtrls(CdefSearchCtrls& ctrls, std::uint8_t cdefSearchLevel, bool isBase,
                        bool isNotHighestLayer) {
    const bool isBase_             = isBase;
    const bool isNotHighestLayer_  = isNotHighestLayer;
    int secondPassFsNum;
    switch (cdefSearchLevel) {
    // OFF
    case 0:
        ctrls.enabled               = 0;
        ctrls.useReferenceCdefFs    = 0;
        ctrls.skipTh                = 0;
        ctrls.uvFromY               = false;
        break;
    case 1: {
        // pf_set {0,1,..,15}
        // sf_set {0,1,2,3}
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 16;
        secondPassFsNum                  = 3;
        ctrls.defaultSecondPassFsNum     = (std::uint8_t)(ctrls.firstPassFsNum * secondPassFsNum);
        for (int i = 0; i < 16; ++i) ctrls.defaultFirstPassFs[i] = pfGi[i];
        int sfIdx = 0;
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            int pfIdx = ctrls.defaultFirstPassFs[i];
            for (int j = 1; j < 4; j++) {
                ctrls.defaultSecondPassFs[sfIdx] = (std::uint8_t)(pfIdx + j);
                sfIdx++;
            }
        }
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            ctrls.defaultFirstPassFsUv[i] = (std::int8_t)ctrls.defaultFirstPassFs[i];
        }
        for (int i = 0; i < ctrls.defaultSecondPassFsNum; i++) {
            ctrls.defaultSecondPassFsUv[i] = (std::int8_t)ctrls.defaultSecondPassFs[i];
        }
        ctrls.useReferenceCdefFs = 0;
        ctrls.searchBestRefFs    = 0;
        ctrls.subsamplingFactor  = 1;
        ctrls.skipTh             = 0;
        ctrls.uvFromY            = false;
        ctrls.useQpStrength      = false;
        break;
    }
    case 2: {
        // pf_set {0,1,2,4,5,6,8,9,10,12,13,14}
        // sf_set {0,1,..,3}
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 12;
        secondPassFsNum                  = 3;
        ctrls.defaultSecondPassFsNum     = (std::uint8_t)(ctrls.firstPassFsNum * secondPassFsNum);
        const int sel[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        for (int i = 0; i < 12; ++i) ctrls.defaultFirstPassFs[i] = pfGi[sel[i]];
        int sfIdx = 0;
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            int pfIdx = ctrls.defaultFirstPassFs[i];
            for (int j = 1; j < 4; j++) {
                ctrls.defaultSecondPassFs[sfIdx] = (std::uint8_t)(pfIdx + j);
                sfIdx++;
            }
        }
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            ctrls.defaultFirstPassFsUv[i] = (std::int8_t)ctrls.defaultFirstPassFs[i];
        }
        for (int i = 0; i < ctrls.defaultSecondPassFsNum; i++) {
            ctrls.defaultSecondPassFsUv[i] = -1;
        }
        ctrls.useReferenceCdefFs = 0;
        ctrls.searchBestRefFs    = 0;
        ctrls.subsamplingFactor  = 1;
        ctrls.skipTh             = 0;
        ctrls.uvFromY            = false;
        ctrls.useQpStrength      = false;
        break;
    }
    case 3: {
        // pf_set {0,4,8,12,15}
        // sf_set {0,1,..,3}
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 5;
        secondPassFsNum                  = 3;
        ctrls.defaultSecondPassFsNum     = (std::uint8_t)(ctrls.firstPassFsNum * secondPassFsNum);
        const int sel[5] = {0, 4, 8, 12, 15};
        for (int i = 0; i < 5; ++i) ctrls.defaultFirstPassFs[i] = pfGi[sel[i]];
        int sfIdx = 0;
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            int pfIdx = ctrls.defaultFirstPassFs[i];
            for (int j = 1; j < 4; j++) {
                ctrls.defaultSecondPassFs[sfIdx] = (std::uint8_t)(pfIdx + j);
                sfIdx++;
            }
        }
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            ctrls.defaultFirstPassFsUv[i] = (std::int8_t)ctrls.defaultFirstPassFs[i];
        }
        for (int i = 0; i < ctrls.defaultSecondPassFsNum; i++) {
            ctrls.defaultSecondPassFsUv[i] = -1;
        }
        ctrls.useReferenceCdefFs = 0;
        ctrls.searchBestRefFs    = 0;
        ctrls.subsamplingFactor  = 1;
        ctrls.skipTh             = 0;
        ctrls.uvFromY            = false;
        ctrls.useQpStrength      = false;
        break;
    }
    case 4: {
        // pf_set {0,7,15}
        // sf_set {0,1,..,3}
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 3;
        secondPassFsNum                  = 3;
        ctrls.defaultSecondPassFsNum     = (std::uint8_t)(ctrls.firstPassFsNum * secondPassFsNum);
        const int sel[3] = {0, 7, 15};
        for (int i = 0; i < 3; ++i) ctrls.defaultFirstPassFs[i] = pfGi[sel[i]];
        int sfIdx = 0;
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            int pfIdx = ctrls.defaultFirstPassFs[i];
            for (int j = 1; j < 4; j++) {
                ctrls.defaultSecondPassFs[sfIdx] = (std::uint8_t)(pfIdx + j);
                sfIdx++;
            }
        }
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            ctrls.defaultFirstPassFsUv[i] = (std::int8_t)ctrls.defaultFirstPassFs[i];
        }
        for (int i = 0; i < ctrls.defaultSecondPassFsNum; i++) {
            ctrls.defaultSecondPassFsUv[i] = -1;
        }
        ctrls.useReferenceCdefFs = 0;
        ctrls.searchBestRefFs    = 0;
        ctrls.subsamplingFactor  = 1;
        ctrls.skipTh             = 0;
        ctrls.uvFromY            = false;
        ctrls.useQpStrength      = false;
        break;
    }
    case 5: {
        // pf_set {0,7,15}
        // sf_set {0,2}
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 3;
        secondPassFsNum                  = 1;
        ctrls.defaultSecondPassFsNum     = (std::uint8_t)(ctrls.firstPassFsNum * secondPassFsNum);
        ctrls.defaultFirstPassFs[0]      = pfGi[0];
        ctrls.defaultFirstPassFs[1]      = pfGi[7];
        ctrls.defaultFirstPassFs[2]      = pfGi[15];
        ctrls.defaultSecondPassFs[0]     = (std::uint8_t)(pfGi[0] + 2);
        ctrls.defaultSecondPassFs[1]     = (std::uint8_t)(pfGi[7] + 2);
        ctrls.defaultSecondPassFs[2]     = (std::uint8_t)(pfGi[15] + 2);
        for (int i = 0; i < ctrls.firstPassFsNum; i++) {
            ctrls.defaultFirstPassFsUv[i] = (std::int8_t)ctrls.defaultFirstPassFs[i];
        }
        for (int i = 0; i < ctrls.defaultSecondPassFsNum; i++) {
            ctrls.defaultSecondPassFsUv[i] = -1;
        }
        ctrls.useReferenceCdefFs = 0;
        ctrls.searchBestRefFs    = isNotHighestLayer_ ? 0 : 1;
        ctrls.subsamplingFactor  = 1;
        ctrls.skipTh             = 0;
        ctrls.uvFromY            = false;
        ctrls.useQpStrength      = false;
        break;
    }
    case 6: {
        // pf_set {0,15}
        // sf_set {0,2}
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 2;
        secondPassFsNum                  = 1;
        ctrls.defaultSecondPassFsNum     = (std::uint8_t)(ctrls.firstPassFsNum * secondPassFsNum);
        ctrls.defaultFirstPassFs[0]      = pfGi[0];
        ctrls.defaultFirstPassFs[1]      = pfGi[15];
        ctrls.defaultSecondPassFs[0]     = (std::uint8_t)(pfGi[0] + 2);
        ctrls.defaultSecondPassFs[1]     = (std::uint8_t)(pfGi[15] + 2);
        ctrls.defaultFirstPassFsUv[0]    = (std::int8_t)ctrls.defaultFirstPassFs[0];
        ctrls.defaultFirstPassFsUv[1]    = (std::int8_t)ctrls.defaultFirstPassFs[1];
        ctrls.defaultFirstPassFsUv[2]    = -1;  // when using search_best_ref_fs, set at least 3 filters
        ctrls.defaultSecondPassFsUv[0]   = -1;
        ctrls.defaultSecondPassFsUv[1]   = -1;
        ctrls.useReferenceCdefFs = 0;
        ctrls.searchBestRefFs    = isNotHighestLayer_ ? 0 : 1;
        ctrls.subsamplingFactor  = 4;
        ctrls.skipTh             = 0;
        ctrls.uvFromY            = false;
        ctrls.useQpStrength      = false;
        break;
    }
    case 7: {
        // pf_set {0,15}
        // sf_set {0,2}
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 2;
        secondPassFsNum                  = 1;
        ctrls.defaultSecondPassFsNum     = (std::uint8_t)(ctrls.firstPassFsNum * secondPassFsNum);
        ctrls.defaultFirstPassFs[0]      = pfGi[0];
        ctrls.defaultFirstPassFs[1]      = pfGi[15];
        ctrls.defaultSecondPassFs[0]     = (std::uint8_t)(pfGi[0] + 2);
        ctrls.defaultSecondPassFs[1]     = (std::uint8_t)(pfGi[15] + 2);
        ctrls.defaultFirstPassFsUv[0]    = (std::int8_t)ctrls.defaultFirstPassFs[0];
        ctrls.defaultFirstPassFsUv[1]    = (std::int8_t)ctrls.defaultFirstPassFs[1];
        ctrls.defaultFirstPassFsUv[2]    = -1;  // if using search_best_ref_fs, set at least 3 filters
        ctrls.defaultSecondPassFsUv[0]   = -1;
        ctrls.defaultSecondPassFsUv[1]   = -1;
        ctrls.useReferenceCdefFs = isNotHighestLayer_ ? 0 : 1;
        ctrls.searchBestRefFs    = isBase_ ? 0 : 1;
        ctrls.subsamplingFactor  = 4;
        ctrls.skipTh             = isBase_ ? 0 : 80;
        ctrls.uvFromY            = false;
        ctrls.useQpStrength      = false;
        break;
    }
    case 8: {
        // pf_set {0,15}
        // sf_set {0,2}
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 2;
        secondPassFsNum                  = 1;
        ctrls.defaultSecondPassFsNum     = (std::uint8_t)(ctrls.firstPassFsNum * secondPassFsNum);
        ctrls.defaultFirstPassFs[0]      = pfGi[0];
        ctrls.defaultFirstPassFs[1]      = pfGi[15];
        ctrls.defaultSecondPassFs[0]     = (std::uint8_t)(pfGi[0] + 2);
        ctrls.defaultSecondPassFs[1]     = (std::uint8_t)(pfGi[15] + 2);
        ctrls.defaultFirstPassFsUv[0]    = -1;
        ctrls.defaultFirstPassFsUv[1]    = -1;
        ctrls.defaultFirstPassFsUv[2]    = -1;  // if using search_best_ref_fs, set at least 3 filters
        ctrls.defaultSecondPassFsUv[0]   = -1;
        ctrls.defaultSecondPassFsUv[1]   = -1;
        ctrls.useReferenceCdefFs = isBase_ ? 0 : 1;
        ctrls.searchBestRefFs    = isBase_ ? 0 : 1;
        ctrls.subsamplingFactor  = 4;
        ctrls.skipTh             = isBase_ ? 0 : 80;
        ctrls.uvFromY            = true;
        ctrls.useQpStrength      = false;
        break;
    }
    case 9: {
        // Primary-only: no secondary strength candidates
        ctrls.enabled                    = 1;
        ctrls.firstPassFsNum             = 2;
        secondPassFsNum                  = 0;
        ctrls.defaultSecondPassFsNum     = 0;
        ctrls.defaultFirstPassFs[0]      = pfGi[0];
        ctrls.defaultFirstPassFs[1]      = pfGi[15];
        ctrls.defaultFirstPassFsUv[0]    = -1;
        ctrls.defaultFirstPassFsUv[1]    = -1;
        ctrls.defaultFirstPassFsUv[2]    = -1;
        ctrls.defaultSecondPassFsUv[0]   = -1;
        ctrls.defaultSecondPassFsUv[1]   = -1;
        ctrls.useReferenceCdefFs = isBase_ ? 0 : 1;
        ctrls.searchBestRefFs    = isBase_ ? 0 : 1;
        ctrls.subsamplingFactor  = 4;
        ctrls.skipTh             = isBase_ ? 0 : 80;
        ctrls.uvFromY            = true;
        ctrls.useQpStrength      = false;
        break;
    }
    case 10:
        ctrls.enabled               = 1;
        ctrls.useReferenceCdefFs    = 0;
        ctrls.useQpStrength         = true;
        ctrls.skipTh                = isBase_ ? 0 : 80;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
    // If chroma filters will be copied from luma, set chroma filters to -1 to avoid testing
    if (ctrls.uvFromY && !ctrls.useQpStrength) {
        for (int fsIdx = 0; fsIdx < ctrls.firstPassFsNum; fsIdx++) {
            ctrls.defaultFirstPassFsUv[fsIdx] = -1;
        }
        for (int fsIdx = 0; fsIdx < ctrls.defaultSecondPassFsNum; fsIdx++) {
            ctrls.defaultSecondPassFsUv[fsIdx] = -1;
        }
    }
}

// set_cdef_recon_controls (enc_mode_config.c:1208-1240), 1:1.
void setCdefReconCtrls(CdefReconCtrls& ctrls, std::uint8_t cdefReconLevel) {
    switch (cdefReconLevel) {
    case 0:  // OFF
        ctrls.zeroFsCostBias        = 0;
        ctrls.zeroFilterStrengthLvl = 0;
        ctrls.prevCdefDistTh        = 0;
        break;
    case 1:
        ctrls.zeroFsCostBias        = 61;
        ctrls.zeroFilterStrengthLvl = 2;
        ctrls.prevCdefDistTh        = 10;
        break;
    case 2:
        ctrls.zeroFsCostBias        = 61;
        ctrls.zeroFilterStrengthLvl = 3;
        ctrls.prevCdefDistTh        = 10;
        break;
    case 3:  // old level 4
        ctrls.zeroFsCostBias        = 60;
        ctrls.zeroFilterStrengthLvl = 3;
        ctrls.prevCdefDistTh        = 10;
        break;
    case 4:  // old level 5
        ctrls.zeroFsCostBias        = 58;
        ctrls.zeroFilterStrengthLvl = 3;
        ctrls.prevCdefDistTh        = 10;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
}

// svt_aom_set_dlf_controls (enc_mode_config.c:1569-1665), 1:1.
void setDlfCtrls(DlfCtrls& ctrls, std::uint8_t dlfLevel) {
    switch (dlfLevel) {
    case 0:
        ctrls.enabled                  = 0;
        ctrls.sbBasedDlf               = 0;
        ctrls.pickMethod               = LPF_PICK_FROM_FULL_IMAGE;
        ctrls.dlfAvg                   = false;
        ctrls.useRefAvgY               = false;
        ctrls.useRefAvgUv              = false;
        ctrls.earlyExitConvergence     = 0;
        ctrls.zeroFilterStrengthLvl    = 0;
        ctrls.prevDlfDistTh            = 0;
        break;
    case 1:
        ctrls.enabled                  = 1;
        ctrls.sbBasedDlf               = 0;
        ctrls.pickMethod               = LPF_PICK_FROM_FULL_IMAGE;
        ctrls.dlfAvg                   = false;
        ctrls.useRefAvgY               = false;
        ctrls.useRefAvgUv              = false;
        ctrls.earlyExitConvergence     = 0;
        ctrls.zeroFilterStrengthLvl    = 0;
        ctrls.prevDlfDistTh            = 0;
        break;
    case 2:
        ctrls.enabled                  = 1;
        ctrls.sbBasedDlf               = 0;
        ctrls.pickMethod               = LPF_PICK_FROM_FULL_IMAGE;
        ctrls.dlfAvg                   = true;
        ctrls.useRefAvgY               = false;
        ctrls.useRefAvgUv              = true;
        ctrls.earlyExitConvergence     = 1;
        ctrls.zeroFilterStrengthLvl    = 0;
        ctrls.prevDlfDistTh            = 0;
        break;
    case 3:
        ctrls.enabled                  = 1;
        ctrls.sbBasedDlf               = 0;
        ctrls.pickMethod               = LPF_PICK_FROM_FULL_IMAGE;
        ctrls.dlfAvg                   = true;
        ctrls.useRefAvgY               = true;
        ctrls.useRefAvgUv              = true;
        ctrls.earlyExitConvergence     = 1;
        ctrls.zeroFilterStrengthLvl    = 0;
        ctrls.prevDlfDistTh            = 0;
        break;
    case 4:
        ctrls.enabled                  = 1;
        ctrls.sbBasedDlf               = 0;
        ctrls.pickMethod               = LPF_PICK_FROM_FULL_IMAGE;
        ctrls.dlfAvg                   = true;
        ctrls.useRefAvgY               = true;
        ctrls.useRefAvgUv              = true;
        ctrls.earlyExitConvergence     = 1;
        ctrls.zeroFilterStrengthLvl    = 2;
        ctrls.prevDlfDistTh            = 10;
        break;
    case 5:
        ctrls.enabled                  = 1;
        ctrls.sbBasedDlf               = 1;
        ctrls.pickMethod               = LPF_PICK_FROM_Q;
        ctrls.dlfAvg                   = false;
        ctrls.useRefAvgY               = false;
        ctrls.useRefAvgUv              = false;
        ctrls.earlyExitConvergence     = 0;
        ctrls.zeroFilterStrengthLvl    = 1;
        ctrls.prevDlfDistTh            = 0;
        break;
    case 6:
        ctrls.enabled                  = 1;
        ctrls.sbBasedDlf               = 1;
        ctrls.pickMethod               = LPF_PICK_FROM_Q;
        ctrls.dlfAvg                   = false;
        ctrls.useRefAvgY               = false;
        ctrls.useRefAvgUv              = false;
        ctrls.earlyExitConvergence     = 0;
        ctrls.zeroFilterStrengthLvl    = 2;
        ctrls.prevDlfDistTh            = 0;
        break;
    case 7:
        ctrls.enabled                  = 1;
        ctrls.sbBasedDlf               = 1;
        ctrls.pickMethod               = LPF_PICK_FROM_Q;
        ctrls.dlfAvg                   = false;
        ctrls.useRefAvgY               = false;
        ctrls.useRefAvgUv              = false;
        ctrls.earlyExitConvergence     = 0;
        ctrls.zeroFilterStrengthLvl    = 3;
        ctrls.prevDlfDistTh            = 0;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
}

// set_intrabc_level (enc_mode_config.c:1672-1851), 1:1. MAX_INTRABC_LEVEL
// is 7 (:1670), so case MAX_INTRABC_LEVEL is case 7.
void setIntrabcLevel(IntrabcCtrls& ctrls, std::uint8_t ibcLevel) {
    switch (ibcLevel) {
    case 0:
        ctrls.enabled = false;
        break;
    case 1:
        ctrls.enabled = true;

        ctrls.paletteHint      = false;
        ctrls.nsqParentGating  = false;
        ctrls.b4ParentGating   = false;

        // Hash search settings
        ctrls.maxBlockSizeHash  = 64;
        ctrls.maxCandPerBucket  = 256;

        // Mesh search settings
        ctrls.exhaustiveMeshThresh        = (1 << 20);
        ctrls.meshSearchMvDiffThreshold   = -1;
        ctrls.meshPatterns[0]             = MeshPattern{256, 1};
        ctrls.meshPatterns[1]             = MeshPattern{256, 1};
        ctrls.meshPatterns[2]             = MeshPattern{0, 0};
        ctrls.meshPatterns[3]             = MeshPattern{0, 0};
        ctrls.meshQpScaling               = false;

        // Search direction(s)
        ctrls.searchDir = 0;
        break;
    case 2:
        ctrls.enabled = true;

        ctrls.paletteHint      = true;
        ctrls.nsqParentGating  = false;
        ctrls.b4ParentGating   = false;

        // Hash search settings
        ctrls.maxBlockSizeHash  = 64;
        ctrls.maxCandPerBucket  = 256;

        // Mesh search settings
        ctrls.exhaustiveMeshThresh        = (1 << 20);
        ctrls.meshSearchMvDiffThreshold   = -1;
        ctrls.meshPatterns[0]             = MeshPattern{256, 8};
        ctrls.meshPatterns[1]             = MeshPattern{64, 1};
        ctrls.meshPatterns[2]             = MeshPattern{0, 0};
        ctrls.meshPatterns[3]             = MeshPattern{0, 0};
        ctrls.meshQpScaling               = false;

        // Search direction(s)
        ctrls.searchDir = 0;
        break;
    case 3:
        ctrls.enabled = true;

        ctrls.paletteHint      = true;
        ctrls.nsqParentGating  = true;
        ctrls.b4ParentGating   = false;

        // Hash search settings
        ctrls.maxBlockSizeHash  = 64;
        ctrls.maxCandPerBucket  = 256;

        // Mesh search settings
        ctrls.exhaustiveMeshThresh        = (1 << 20);
        ctrls.meshSearchMvDiffThreshold   = 0;
        ctrls.meshPatterns[0]             = MeshPattern{256, 8};
        ctrls.meshPatterns[1]             = MeshPattern{64, 1};
        ctrls.meshPatterns[2]             = MeshPattern{0, 0};
        ctrls.meshPatterns[3]             = MeshPattern{0, 0};
        ctrls.meshQpScaling               = true;

        // Search direction(s)
        ctrls.searchDir = 0;
        break;
    case 4:
        ctrls.enabled = true;

        ctrls.paletteHint      = true;
        ctrls.nsqParentGating  = true;
        ctrls.b4ParentGating   = false;

        // Hash search settings
        ctrls.maxBlockSizeHash  = 64;
        ctrls.maxCandPerBucket  = 64;

        // Mesh search settings
        ctrls.exhaustiveMeshThresh        = (1 << 24);
        ctrls.meshSearchMvDiffThreshold   = 0;
        ctrls.meshPatterns[0]             = MeshPattern{256, 8};
        ctrls.meshPatterns[1]             = MeshPattern{32, 1};
        ctrls.meshPatterns[2]             = MeshPattern{0, 0};
        ctrls.meshPatterns[3]             = MeshPattern{0, 0};
        ctrls.meshQpScaling               = true;

        // Search direction(s)
        ctrls.searchDir = 0;
        break;
    case 5:
        ctrls.enabled = true;

        ctrls.paletteHint      = true;
        ctrls.nsqParentGating  = true;
        ctrls.b4ParentGating   = false;

        // Hash search settings
        ctrls.maxBlockSizeHash  = 8;
        ctrls.maxCandPerBucket  = 64;

        // Mesh search settings
        ctrls.exhaustiveMeshThresh        = (1 << 24);
        ctrls.meshSearchMvDiffThreshold   = 0;
        ctrls.meshPatterns[0]             = MeshPattern{256, 8};
        ctrls.meshPatterns[1]             = MeshPattern{32, 1};
        ctrls.meshPatterns[2]             = MeshPattern{0, 0};
        ctrls.meshPatterns[3]             = MeshPattern{0, 0};
        ctrls.meshQpScaling               = true;

        // Search direction(s)
        ctrls.searchDir = 0;
        break;
    case 6:
        ctrls.enabled = true;

        ctrls.paletteHint      = true;
        ctrls.nsqParentGating  = true;
        ctrls.b4ParentGating   = false;

        // Hash search settings
        ctrls.maxBlockSizeHash  = 8;
        ctrls.maxCandPerBucket  = 32;

        // Mesh search settings
        ctrls.exhaustiveMeshThresh = (std::uint64_t)~0;

        // Search direction(s)
        ctrls.searchDir = 0;
        break;
    case kMaxIntrabcLevel:
        ctrls.enabled = true;

        ctrls.paletteHint      = true;
        ctrls.nsqParentGating  = true;
        ctrls.b4ParentGating   = false;

        // Hash search settings
        ctrls.maxBlockSizeHash  = 8;
        ctrls.maxCandPerBucket  = 32;

        // Mesh search settings
        ctrls.exhaustiveMeshThresh = (std::uint64_t)~0;

        // Search direction(s)
        ctrls.searchDir = 1;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
}

// set_palette_level (enc_mode_config.c:1856-1930), 1:1.
void setPaletteLevel(PaletteCtrls& ctrls, std::uint8_t paletteLevel) {
    switch (paletteLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = 1;
        ctrls.kmeanColorStep      = 1;
        ctrls.centroidRefinement  = true;
        ctrls.kMeansMaxItr        = 50;
        break;
    case 2:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = 2;
        ctrls.kmeanColorStep      = 1;
        ctrls.centroidRefinement  = false;
        ctrls.kMeansMaxItr        = 2;
        break;
    case 3:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = (std::uint8_t)~0;
        ctrls.kmeanColorStep      = 1;
        ctrls.centroidRefinement  = false;
        ctrls.kMeansMaxItr        = 2;
        break;
    case 4:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = (std::uint8_t)~0;
        ctrls.kmeanColorStep      = 2;
        ctrls.centroidRefinement  = false;
        ctrls.kMeansMaxItr        = 2;
        break;
    case 5:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = (std::uint8_t)~0;
        ctrls.kmeanColorStep      = 3;
        ctrls.centroidRefinement  = false;
        ctrls.kMeansMaxItr        = 2;
        break;
    case 6:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = (std::uint8_t)~0;
        ctrls.kmeanColorStep      = 5;
        ctrls.centroidRefinement  = false;
        ctrls.kMeansMaxItr        = 2;
        break;
    case 7:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = (std::uint8_t)~0;
        ctrls.kmeanColorStep      = 5;
        ctrls.centroidRefinement  = false;
        ctrls.kMeansMaxItr        = 1;
        break;
    case 8:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = (std::uint8_t)~0;
        ctrls.kmeanColorStep      = 6;
        ctrls.centroidRefinement  = false;
        ctrls.kMeansMaxItr        = 1;
        break;
    case 9:
        ctrls.enabled             = 1;
        ctrls.dominantColorStep   = (std::uint8_t)~0;
        ctrls.kmeanColorStep      = 50;
        ctrls.centroidRefinement  = false;
        ctrls.kMeansMaxItr        = 1;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
}

// svt_aom_set_wn_filter_ctrls (enc_mode_config.c:1242-1301), 1:1. The
// Av1Common wall folds into the carrier.
void setWnFilterCtrls(WnFilterCtrls& ctrls, std::uint8_t wnFilterLvl) {
    switch (wnFilterLvl) {
    case 0:
        ctrls.enabled = false;
        break;
    case 1:
        ctrls.enabled               = true;
        ctrls.useChroma             = true;
        ctrls.filterTapLvl          = 1;
        ctrls.useRefinement         = true;
        ctrls.maxOneRefinementStep  = false;
        ctrls.usePrevFrameCoeffs    = false;
        break;
    case 2:
        ctrls.enabled               = true;
        ctrls.useChroma             = true;
        ctrls.filterTapLvl          = 1;
        ctrls.useRefinement         = true;
        ctrls.maxOneRefinementStep  = true;
        ctrls.usePrevFrameCoeffs    = false;
        break;
    case 3:
        ctrls.enabled               = true;
        ctrls.useChroma             = true;
        ctrls.filterTapLvl          = 2;
        ctrls.useRefinement         = true;
        ctrls.maxOneRefinementStep  = true;
        ctrls.usePrevFrameCoeffs    = false;
        break;
    case 4:
        ctrls.enabled               = true;
        ctrls.useChroma             = true;
        ctrls.filterTapLvl          = 2;
        ctrls.useRefinement         = false;
        ctrls.maxOneRefinementStep  = true;
        ctrls.usePrevFrameCoeffs    = false;
        break;
    case 5:
        ctrls.enabled               = true;
        ctrls.useChroma             = false;
        ctrls.filterTapLvl          = 2;
        ctrls.useRefinement         = false;
        ctrls.maxOneRefinementStep  = true;
        ctrls.usePrevFrameCoeffs    = false;
        break;
    case 6:
        ctrls.enabled               = true;
        ctrls.useChroma             = false;
        ctrls.filterTapLvl          = 2;
        ctrls.useRefinement         = false;
        ctrls.maxOneRefinementStep  = true;
        ctrls.usePrevFrameCoeffs    = true;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
}

// svt_aom_set_sg_filter_ctrls (enc_mode_config.c:1303-1362), 1:1.
void setSgFilterCtrls(SgFilterCtrls& ctrls, std::uint8_t sgFilterLvl) {
    switch (sgFilterLvl) {
    case 0:
        ctrls.enabled = false;
        break;
    case 1:
        ctrls.enabled    = true;
        ctrls.useChroma  = true;
        ctrls.startEp[0] = 0;
        ctrls.endEp[0]   = 16;
        ctrls.epInc[0]   = 1;
        ctrls.startEp[1] = 0;
        ctrls.endEp[1]   = 16;
        ctrls.epInc[1]   = 1;
        ctrls.refine[0]  = 1;
        ctrls.refine[1]  = 1;
        break;
    case 2:
        ctrls.enabled    = true;
        ctrls.useChroma  = true;
        ctrls.startEp[0] = 0;
        ctrls.endEp[0]   = 16;
        ctrls.epInc[0]   = 1;
        ctrls.startEp[1] = 4;
        ctrls.endEp[1]   = 5;
        ctrls.epInc[1]   = 1;
        ctrls.refine[0]  = 1;
        ctrls.refine[1]  = 0;
        break;
    case 3:
        ctrls.enabled    = true;
        ctrls.useChroma  = true;
        ctrls.startEp[0] = 0;
        ctrls.endEp[0]   = 16;
        ctrls.epInc[0]   = 8;
        ctrls.startEp[1] = 4;
        ctrls.endEp[1]   = 5;
        ctrls.epInc[1]   = 1;
        ctrls.refine[0]  = 1;
        ctrls.refine[1]  = 0;
        break;
    case 4:
        ctrls.enabled    = true;
        ctrls.useChroma  = false;
        ctrls.startEp[0] = 0;
        ctrls.endEp[0]   = 16;
        ctrls.epInc[0]   = 8;
        ctrls.startEp[1] = 4;
        ctrls.endEp[1]   = 5;
        ctrls.epInc[1]   = 1;
        ctrls.refine[0]  = 1;
        ctrls.refine[1]  = 0;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
}

// ---------------------------------------------------------------------------
// Batch B: the small helper family.
// ---------------------------------------------------------------------------

// svt_aom_get_gm_core_level (enc_mode_config.c:181-199), 1:1 with the
// CONFIG_ENABLE_GLOBAL_MOTION fork threaded as a runtime parameter (the
// setGmControls precedent; the pinned build resolves the macro to 1).
std::uint8_t getGmCoreLevel(EncMode encMode, bool superResOff, int configEnableGlobalMotion) {
    if (!configEnableGlobalMotion) {
        (void)encMode;
        (void)superResOff;
        return 0;
    }
    std::uint8_t gmLevel = 0;
    if (superResOff) {
        if (encMode <= ENC_MR) {
            gmLevel = 2;
        } else if (encMode <= ENC_M4) {
            gmLevel = 4;
        } else {
            gmLevel = 0;
        }
    }
    return gmLevel;
}

// svt_aom_derive_gm_level (enc_mode_config.c:201-215), 1:1 with the pcs
// wall folded into the two consumed scalars.
std::uint8_t deriveGmLevel(EncMode encMode, bool isISlice, bool superResOff,
                           int configEnableGlobalMotion) {
    std::uint8_t gmLevel = 0;
    // disable global motion when reference scaling enabled,
    // even if current pic is not scaled, because its reference
    // pics might be scaled in different size
    // super-res is ok for its reference pics are always upscaled
    // to original size
    if (!isISlice) {
        gmLevel = getGmCoreLevel(encMode, superResOff, configEnableGlobalMotion);
    }
    return gmLevel;
}

// svt_aom_derive_input_resolution (sequence_control_set.c:120-139), 1:1
// with the thresholds of definitions.h:1819-1824. NAMED DEVIATION: the
// EbErrorType return (always EB_ErrorNone) is dropped.
ResolutionRange deriveInputResolution(std::uint32_t inputSize) {
    if (inputSize < 0x28500u) {        // INPUT_SIZE_240p_TH
        return INPUT_SIZE_240p_RANGE;
    } else if (inputSize < 0x4CE00u) {   // INPUT_SIZE_360p_TH
        return INPUT_SIZE_360p_RANGE;
    } else if (inputSize < 0xA1400u) {   // INPUT_SIZE_480p_TH
        return INPUT_SIZE_480p_RANGE;
    } else if (inputSize < 0x16DA00u) {  // INPUT_SIZE_720p_TH
        return INPUT_SIZE_720p_RANGE;
    } else if (inputSize < 0x535200u) {  // INPUT_SIZE_1080p_TH
        return INPUT_SIZE_1080p_RANGE;
    } else if (inputSize < 0x140A000u) {  // INPUT_SIZE_4K_TH
        return INPUT_SIZE_4K_RANGE;
    }
    return INPUT_SIZE_8K_RANGE;
}

// svt_aom_get_max_can_count (enc_mode_config.c:1936-1981), 1:1.
std::uint16_t getMaxCanCount(EncMode encMode, bool rtc) {
    std::uint16_t memMaxCanCount;
    if (rtc) {
        // RTC allocation upper bound: sized to cover the worst-case candidate injection count per preset
        // (up to 3L pred structure), with headroom for preset-boundary tuning (e.g. an Mn tool tested at Mn+1).
        if (encMode <= ENC_M7) {
            memMaxCanCount = 150;
        } else if (encMode <= ENC_M8) {
            memMaxCanCount = 75;
        } else if (encMode <= ENC_M10) {
            memMaxCanCount = 65;
        } else if (encMode <= ENC_M11) {
            memMaxCanCount = 15;
        } else {
            memMaxCanCount = 10;
        }
    } else {
        if (encMode <= ENC_M1) {
            memMaxCanCount = 1225;
        } else if (encMode <= ENC_M2) {
            memMaxCanCount = 1000;
        } else if (encMode <= ENC_M3) {
            memMaxCanCount = 720;
        } else if (encMode <= ENC_M4) {
            memMaxCanCount = 576;
        } else if (encMode <= ENC_M5) {
            memMaxCanCount = 369;
        } else if (encMode <= ENC_M6) {
            memMaxCanCount = 236;
        } else if (encMode <= ENC_M9) {
            memMaxCanCount = 190;
        } else {
            memMaxCanCount = 80;
        }
    }
    return memMaxCanCount;
}

// ---------------------------------------------------------------------------
// Batch B: the sig_deriv_multi_processes family (enc_mode_config.c
// :1988-2518), 1:1 per variant. The walls fold into the two carriers; the
// derived walls (isBase / isNotHighestLayer / isNotLastLayer) arrive as
// annotated bools.
// ---------------------------------------------------------------------------

// svt_aom_sig_deriv_multi_processes_default (:1988-2188).
void sigDerivMultiProcessesDefault(MultiProcPpcs& pcs, const MultiProcScs& scs,
                                   int configEnableGlobalMotion) {
    // Set GM ctrls assuming super-res is off for gm-pp need (:1999)
    setGmControls(pcs.gm, deriveGmLevel(pcs.encMode, pcs.isISlice, true, configEnableGlobalMotion),
                  configEnableGlobalMotion);

    // If enabled here, the hme enable flags should also be enabled in ResourceCoordinationProcess
    // to ensure that resources are allocated for the downsampled pictures used in HME
    pcs.enableHmeFlag        = true;
    pcs.enableHmeLevel0Flag  = true;
    if (pcs.scClass5) {
        if (pcs.encMode <= ENC_M2) {
            pcs.enableHmeLevel1Flag = true;
            pcs.enableHmeLevel2Flag = true;
        } else {
            pcs.enableHmeLevel1Flag = true;
            pcs.enableHmeLevel2Flag = false;
        }
    } else {
        pcs.enableHmeLevel1Flag = true;
        pcs.enableHmeLevel2Flag = false;
    }

    switch (pcs.tfHmeMeLevel) {
    case 0:
        pcs.tfEnableHmeFlag        = true;
        pcs.tfEnableHmeLevel0Flag  = true;
        pcs.tfEnableHmeLevel1Flag  = true;
        pcs.tfEnableHmeLevel2Flag  = true;
        break;
    case 1:
    case 2:
        pcs.tfEnableHmeFlag        = true;
        pcs.tfEnableHmeLevel0Flag  = true;
        pcs.tfEnableHmeLevel1Flag  = true;
        pcs.tfEnableHmeLevel2Flag  = false;
        break;
    case 3:
    case 4:
        pcs.tfEnableHmeFlag        = true;
        pcs.tfEnableHmeLevel0Flag  = true;
        pcs.tfEnableHmeLevel1Flag  = false;
        pcs.tfEnableHmeLevel2Flag  = false;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
    // Set the Multi-Pass PD level
    pcs.multiPassPdLevel = kMultiPassPdOn;

    // Set intra-bc level
    std::uint8_t intrabcLevel = 0;
    if (!scs.enableIntrabc) {
        intrabcLevel = 0;
    } else if (pcs.scClass5) {
        if (pcs.isISlice) {
            if (pcs.encMode <= ENC_M3) {
                intrabcLevel = 2;
            } else if (pcs.encMode <= ENC_M5) {
                intrabcLevel = 3;
            } else if (pcs.encMode <= ENC_M8) {
                intrabcLevel = 5;
            } else if (pcs.encMode <= ENC_M9) {
                intrabcLevel = 6;
            } else {
                intrabcLevel = 0;
            }
        } else {
            intrabcLevel = 0;
        }
    }
    setIntrabcLevel(pcs.intrabcCtrls, intrabcLevel);
    pcs.allowIntrabc = pcs.intrabcCtrls.enabled != false;

    // Set palette level
    if (pcs.scClass5) {
        if (pcs.encMode <= ENC_M0) {
            pcs.paletteLevel = pcs.isISlice ? 1 : 0;
        } else if (pcs.encMode <= ENC_M1) {
            pcs.paletteLevel = pcs.isISlice ? 2 : 0;
        } else if (pcs.encMode <= ENC_M2) {
            pcs.paletteLevel = pcs.isISlice ? 4 : 0;
        } else if (pcs.encMode <= ENC_M5) {
            pcs.paletteLevel = pcs.isISlice ? 5 : 0;
        } else if (pcs.encMode <= ENC_M9) {
            pcs.paletteLevel = pcs.isISlice ? 6 : 0;
        } else if (pcs.encMode <= ENC_M10) {
            pcs.paletteLevel = pcs.isISlice ? 8 : 0;
        } else {
            pcs.paletteLevel = 0;
        }
    } else {
        pcs.paletteLevel = 0;
    }
    setPaletteLevel(pcs.paletteCtrls, pcs.paletteLevel);

    pcs.allowScreenContentTools =
        (pcs.scClass5 && (pcs.paletteLevel != 0 || pcs.allowIntrabc)) ? true : false;

    // Set CDEF level
    std::uint8_t cdefSearchLevel = 0;
    if (scs.seqCdefLevel == 0 || pcs.allowIntrabc) {
        cdefSearchLevel = 0;
    } else if (scs.cdefLevel != kDefaultSentinel) {
        cdefSearchLevel = (std::uint8_t)scs.cdefLevel;
    } else if (pcs.encMode <= ENC_MR) {
        cdefSearchLevel = 1;
    } else if (pcs.encMode <= ENC_M2) {
        cdefSearchLevel = 2;
    } else if (pcs.encMode <= ENC_M5) {
        cdefSearchLevel = 5;
    } else if (pcs.encMode <= ENC_M7) {
        cdefSearchLevel = pcs.isBase ? 5 : 6;
    } else {
        cdefSearchLevel = 7;
    }
    setCdefSearchCtrls(pcs.cdefSearchCtrls, cdefSearchLevel, pcs.isBase, pcs.isNotHighestLayer);
    pcs.cdefLevel = cdefSearchLevel;

    std::uint8_t cdefReconLevel = 0;
    if (scs.fastDecode == 0 || pcs.gm.inputResolution <= INPUT_SIZE_360p_RANGE) {
        if (pcs.encMode <= ENC_M8) {
            cdefReconLevel = 0;
        } else if (pcs.encMode <= ENC_M10) {
            cdefReconLevel = 1;
        } else {
            cdefReconLevel = 2;
        }
    } else if (scs.fastDecode == 1) {
        cdefReconLevel = 1;
    } else {  // fast_decode 2
        if (pcs.encMode <= ENC_M8) {
            cdefReconLevel = 2;
        } else {
            cdefReconLevel = 1;
        }
    }
    setCdefReconCtrls(pcs.cdefReconCtrls, cdefReconLevel);

    std::uint8_t wn = 0, sg = 0;
    // If restoration filtering is enabled at the sequence level, derive the settings used for this frame
    if (scs.seqEnableRestoration) {
        // As allocation has already happened based on the initial input resolution/QP, the resolution/QP
        // changes should not impact enabling restoration. For some presets, restoration is off for 8K
        // and above and memory allocation is not performed. So, if we switch to smaller resolution, we need
        // to keep restoration off.
        ResolutionRange initInputResolution =
            deriveInputResolution(scs.maxInitialInputLumaWidth * scs.maxInitialInputLumaHeight);
        wn = getWnFilterLevelDefault(pcs.encMode, (std::uint8_t)initInputResolution, pcs.isNotLastLayer);
        sg = getSgFilterLevelDefault(pcs.encMode, (std::uint8_t)initInputResolution,
                                     (std::uint8_t)scs.fastDecode);
    }
    setWnFilterCtrls(pcs.wnFilterCtrls, wn);
    setSgFilterCtrls(pcs.sgFilterCtrls, sg);

    // Set whether restoration filtering is enabled for this frame
    pcs.enableRestoration = (wn > 0 || sg > 0);

    // Set frame end cdf update mode
    pcs.frameEndCdfUpdateMode = 1;

    // User accessible setting for forcing different levels of
    // high bit depth mode decision; also has a check to make sure
    // encoder bit depth > 8 to work in full hbd-md
    if (scs.encoderBitDepth == 8) {  // SVT_EFFECTIVE_BIT_DEPTH(bd) == EB_EIGHT_BIT
        pcs.hbdMd = 0;
    } else if (scs.hbdMds != kDefaultSentinel) {
        pcs.hbdMd = (std::uint8_t)scs.hbdMds;
    } else {
        // Default preset-defined behavior
        if (pcs.encMode <= ENC_MR) {
            pcs.hbdMd = 1;
        } else if (pcs.encMode <= ENC_M5) {
            pcs.hbdMd = pcs.isBase ? 2 : 0;
        } else {
            pcs.hbdMd = pcs.isISlice ? 2 : 0;
        }
    }

    pcs.maxCanCount                = getMaxCanCount(pcs.encMode, false);
    if (pcs.encMode <= ENC_M1) {
        pcs.useBestMeUnipredCandOnly = 0;
    } else {
        pcs.useBestMeUnipredCandOnly = 1;
    }
}

// svt_aom_sig_deriv_multi_processes_rtc (:2190-2367).
void sigDerivMultiProcessesRtc(MultiProcPpcs& pcs, const MultiProcScs& scs,
                               int configEnableGlobalMotion, int ftrRtcInterPalette) {
    // Set GM ctrls assuming super-res is off for gm-pp need (:2198)
    setGmControls(pcs.gm, deriveGmLevel(pcs.encMode, pcs.isISlice, true, configEnableGlobalMotion),
                  configEnableGlobalMotion);

    // If enabled here, the hme enable flags should also be enabled in ResourceCoordinationProcess
    // to ensure that resources are allocated for the downsampled pictures used in HME
    pcs.enableHmeFlag        = true;
    pcs.enableHmeLevel0Flag  = true;
    if (pcs.useFlatIpp) {
        if (pcs.encMode <= ENC_M11 ||
            (pcs.encMode <= ENC_M12 && pcs.picAvgVariance < kRtcLowVarianceTh)) {
            pcs.enableHmeLevel1Flag = true;
            pcs.enableHmeLevel2Flag = false;
        } else {
            pcs.enableHmeLevel1Flag = false;
            pcs.enableHmeLevel2Flag = false;
        }
    } else {
        pcs.enableHmeLevel1Flag = true;
        pcs.enableHmeLevel2Flag = false;
    }

    switch (pcs.tfHmeMeLevel) {
    case 0:
        pcs.tfEnableHmeFlag        = true;
        pcs.tfEnableHmeLevel0Flag  = true;
        pcs.tfEnableHmeLevel1Flag  = true;
        pcs.tfEnableHmeLevel2Flag  = true;
        break;
    case 1:
    case 2:
        pcs.tfEnableHmeFlag        = true;
        pcs.tfEnableHmeLevel0Flag  = true;
        pcs.tfEnableHmeLevel1Flag  = true;
        pcs.tfEnableHmeLevel2Flag  = false;
        break;
    case 3:
    case 4:
        pcs.tfEnableHmeFlag        = true;
        pcs.tfEnableHmeLevel0Flag  = true;
        pcs.tfEnableHmeLevel1Flag  = false;
        pcs.tfEnableHmeLevel2Flag  = false;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
    // Set the Multi-Pass PD level
    pcs.multiPassPdLevel = kMultiPassPdOn;

    // Set intra-bc level
    std::uint8_t intrabcLevel = 0;
    setIntrabcLevel(pcs.intrabcCtrls, intrabcLevel);
    pcs.allowIntrabc = pcs.intrabcCtrls.enabled != false;

    // Set palette level
    if (pcs.scClass5) {
        if (ftrRtcInterPalette) {
            // Palette also on intra blocks inside inter frames at M7-M8, where the static text/UI of a
            // screen-share stream lives. The level must be set here, pre-ME, so allow_screen_content_tools
            // below accounts for it; the frame-idle gate needs this frame's norm_me_dist, which only
            // exists post-ME, so it runs in svt_aom_sig_deriv_mode_decision_config_rtc. Active inter
            // frames therefore carry allow_screen_content_tools=1 even where most blocks decline
            // palette: the cost is one header bit plus the per-block palette_y_mode flag on
            // palette-eligible intra blocks.
            if (pcs.encMode <= ENC_M7) {
                pcs.paletteLevel = pcs.isISlice ? 5 : 7;
            } else if (pcs.encMode <= ENC_M8) {
                pcs.paletteLevel = 7;
            } else {
                pcs.paletteLevel = 0;
            }
        } else {
            if (pcs.encMode <= ENC_M7) {
                pcs.paletteLevel = pcs.isISlice ? 5 : 0;
            } else if (pcs.encMode <= ENC_M8) {
                pcs.paletteLevel = pcs.isISlice ? 7 : 0;
            } else {
                pcs.paletteLevel = 0;
            }
        }
    } else {
        pcs.paletteLevel = 0;
    }
    setPaletteLevel(pcs.paletteCtrls, pcs.paletteLevel);

    pcs.allowScreenContentTools =
        (pcs.scClass5 && (pcs.paletteLevel != 0 || pcs.allowIntrabc)) ? true : false;

    // Set CDEF level
    std::uint8_t cdefSearchLevel = 0;
    if (scs.seqCdefLevel == 0 || pcs.allowIntrabc) {
        cdefSearchLevel = 0;
    } else if (scs.cdefLevel != kDefaultSentinel) {
        cdefSearchLevel = (std::uint8_t)scs.cdefLevel;
    } else {
        if (pcs.useFlatIpp) {
            if (pcs.encMode <= ENC_M7) {
                cdefSearchLevel = 5;
            } else if (pcs.encMode <= ENC_M11) {
                if (pcs.gm.inputResolution <= INPUT_SIZE_360p_RANGE) {
                    cdefSearchLevel = 6;
                } else {
                    cdefSearchLevel = pcs.isISlice ? 5 : 8;
                }
            } else if (pcs.encMode <= ENC_M12 && pcs.picAvgVariance < kRtcUltraLowVarianceTh) {
                // Ultra-easy M12: keep M11-grade CDEF search for preset continuity.
                if (pcs.gm.inputResolution <= INPUT_SIZE_360p_RANGE) {
                    cdefSearchLevel = 6;
                } else {
                    cdefSearchLevel = pcs.isISlice ? 5 : 8;
                }
            } else {
                cdefSearchLevel = pcs.isISlice ? 5 : 9;
            }
        } else {
            if (pcs.encMode <= ENC_M7) {
                cdefSearchLevel = 5;
            } else if (pcs.encMode <= ENC_M8) {
                if (pcs.gm.inputResolution <= INPUT_SIZE_360p_RANGE) {
                    cdefSearchLevel = 6;
                } else {
                    cdefSearchLevel = pcs.isISlice ? 5 : 8;
                }
            } else {
                cdefSearchLevel = pcs.isISlice ? 5 : 8;
            }
        }
    }
    setCdefSearchCtrls(pcs.cdefSearchCtrls, cdefSearchLevel, pcs.isBase, pcs.isNotHighestLayer);
    pcs.cdefLevel = cdefSearchLevel;

    std::uint8_t cdefReconLevel = 0;
    setCdefReconCtrls(pcs.cdefReconCtrls, cdefReconLevel);

    std::uint8_t wn = 0, sg = 0;
    // If restoration filtering is enabled at the sequence level, derive the settings used for this frame
    if (scs.seqEnableRestoration) {
        // As allocation has already happened based on the initial input resolution/QP, the resolution/QP
        // changes should not impact enabling restoration. For some presets, restoration is off for 8K
        // and above and memory allocation is not performed. So, if we switch to smaller resolution, we need
        // to keep restoration off.
        ResolutionRange initInputResolution =
            deriveInputResolution(scs.maxInitialInputLumaWidth * scs.maxInitialInputLumaHeight);
        wn = getWnFilterLevelRtc((std::uint8_t)initInputResolution);
        sg = getSgFilterLevelRtc((std::uint8_t)initInputResolution, (std::uint8_t)scs.fastDecode);
    }
    setWnFilterCtrls(pcs.wnFilterCtrls, wn);
    setSgFilterCtrls(pcs.sgFilterCtrls, sg);

    // Set whether restoration filtering is enabled for this frame
    pcs.enableRestoration = (wn > 0 || sg > 0);

    // Set frame end cdf update mode
    pcs.frameEndCdfUpdateMode = 1;

    if (scs.enableHbdModeDecision == kDefaultSentinel) {
        pcs.hbdMd = pcs.isISlice ? 2 : 0;
    } else {
        pcs.hbdMd = (std::uint8_t)scs.enableHbdModeDecision;
    }

    pcs.maxCanCount                 = getMaxCanCount(pcs.encMode, true);
    pcs.useBestMeUnipredCandOnly    = 1;
}

// svt_aom_sig_deriv_multi_processes_allintra (:2369-2518).
void sigDerivMultiProcessesAllIntra(MultiProcPpcs& pcs, const MultiProcScs& scs) {
    // Set the Multi-Pass PD level
    pcs.multiPassPdLevel = kMultiPassPdOn;

    // Set intra-bc level
    std::uint8_t intrabcLevel = 0;
    if (!scs.enableIntrabc) {
        intrabcLevel = 0;
    } else if (pcs.scClass5) {
        // Use intrabc_level 1 or 2 to achieve maximum intra-BC coding gain (higher computational complexity)
        if (pcs.encMode <= ENC_MR) {
            intrabcLevel = 1;
        } else if (pcs.encMode <= ENC_M0) {
            intrabcLevel = 3;
        } else if (pcs.encMode <= ENC_M1) {
            intrabcLevel = 4;
        } else if (pcs.encMode <= ENC_M2) {
            intrabcLevel = 5;
        } else if (pcs.encMode <= ENC_M3) {
            intrabcLevel = 6;
        } else if (pcs.encMode <= ENC_M4) {
            intrabcLevel = kMaxIntrabcLevel;
        } else {
            intrabcLevel = 0;
        }
    } else {
        intrabcLevel = 0;
    }
    setIntrabcLevel(pcs.intrabcCtrls, intrabcLevel);
    pcs.allowIntrabc = pcs.intrabcCtrls.enabled != false;

    // Set palette level
    if (pcs.scClass5) {
        if (pcs.encMode <= ENC_M2) {
            pcs.paletteLevel = 2;
        } else if (pcs.encMode <= ENC_M3) {
            pcs.paletteLevel = 3;
        } else if (pcs.encMode <= ENC_M5) {
            pcs.paletteLevel = 4;
        } else if (pcs.encMode <= ENC_M6) {
            pcs.paletteLevel = 5;
        } else if (pcs.encMode <= ENC_M7) {
            pcs.paletteLevel = 7;
        } else {
            pcs.paletteLevel = 0;
        }
    } else {
        pcs.paletteLevel = 0;
    }
    setPaletteLevel(pcs.paletteCtrls, pcs.paletteLevel);

    pcs.allowScreenContentTools = (pcs.paletteLevel != 0 || pcs.allowIntrabc) ? true : false;

    // Set CDEF level
    std::uint8_t cdefSearchLevel = 0;
    if (scs.seqCdefLevel == 0 || pcs.allowIntrabc) {
        cdefSearchLevel = 0;
    } else if (scs.cdefLevel != kDefaultSentinel) {
        cdefSearchLevel = (std::uint8_t)scs.cdefLevel;
    } else {
        if ((scs.fastDecode == 0 || pcs.gm.inputResolution <= INPUT_SIZE_360p_RANGE)) {
            if (pcs.encMode <= ENC_MR) {
                cdefSearchLevel = 1;
            } else if (pcs.encMode <= ENC_M0) {
                cdefSearchLevel = 2;
            } else if (pcs.encMode <= ENC_M3) {
                cdefSearchLevel = 3;
            } else if (pcs.encMode <= ENC_M5) {
                cdefSearchLevel = 5;
            } else if (pcs.encMode <= ENC_M6) {
                cdefSearchLevel = 7;
            } else {
                // For fd1/fd2, disable CDEF search if fd0 uses level 10 or 0
                cdefSearchLevel = 10;
            }
        } else {
            if (pcs.encMode <= ENC_M3) {
                cdefSearchLevel = 3;
            } else if (pcs.encMode <= ENC_M5) {
                cdefSearchLevel = 5;
            } else if (pcs.encMode <= ENC_M7) {
                cdefSearchLevel = 7;
            } else {
                cdefSearchLevel = 0;
            }
        }
    }
    setCdefSearchCtrls(pcs.cdefSearchCtrls, cdefSearchLevel, pcs.isBase, pcs.isNotHighestLayer);
    pcs.cdefLevel = cdefSearchLevel;

    std::uint8_t cdefReconLevel = 0;
    if ((scs.fastDecode == 0 || pcs.gm.inputResolution <= INPUT_SIZE_360p_RANGE)) {
        if (pcs.encMode <= ENC_M7) {
            cdefReconLevel = 0;
        } else {
            cdefReconLevel = 1;
        }
    } else if (scs.fastDecode == 1) {
        if (pcs.encMode <= ENC_M7) {
            cdefReconLevel = 1;
        } else {
            cdefReconLevel = 3;
        }
    } else {
        cdefReconLevel = 3;
    }
    setCdefReconCtrls(pcs.cdefReconCtrls, cdefReconLevel);

    std::uint8_t wn = 0, sg = 0;
    // If restoration filtering is enabled at the sequence level, derive the settings used for this frame
    if (scs.seqEnableRestoration) {
        // As allocation has already happened based on the initial input resolution/QP, the resolution/QP
        // changes should not impact enabling restoration. For some presets, restoration is off for 8K
        // and above and memory allocation is not performed. So, if we switch to smaller resolution, we need
        // to keep restoration off.
        ResolutionRange initInputResolution =
            deriveInputResolution(scs.maxInitialInputLumaWidth * scs.maxInitialInputLumaHeight);
        (void)initInputResolution;
        wn = getWnFilterLevelAllIntra(pcs.encMode);
        sg = getSgFilterLevelAllIntra(pcs.encMode);
    }
    setWnFilterCtrls(pcs.wnFilterCtrls, wn);
    setSgFilterCtrls(pcs.sgFilterCtrls, sg);

    // Set whether restoration filtering is enabled for this frame
    pcs.enableRestoration = (wn > 0 || sg > 0);

    // Set frame end cdf update mode
    pcs.frameEndCdfUpdateMode = 1;
    if (scs.enableHbdModeDecision == kDefaultSentinel) {
        if (pcs.encMode <= ENC_MR) {
            pcs.hbdMd = 1;
        } else {
            pcs.hbdMd = 2;
        }
    } else {
        pcs.hbdMd = (std::uint8_t)scs.enableHbdModeDecision;
    }
    pcs.maxCanCount = getMaxCanCount(pcs.encMode, false);
}


// ---------------------------------------------------------------------------
// Batch B rung 4: set_inter_comp_controls, the level getters, the
// sg/restoration enable wrappers, the pre-analysis derivations.
// ---------------------------------------------------------------------------

// set_inter_comp_controls (enc_mode_config.c:2627-2709), 1:1.
void setInterCompCtrls(InterCompCtrls& ctrls, std::uint8_t interCompMode) {
    switch (interCompMode) {
    case 0:  // OFF (AVG only)
        ctrls.totCompTypes     = kMdCompDist;
        ctrls.doNearestNearest = false;
        ctrls.doNearNear       = false;
        ctrls.doMe             = false;
        ctrls.doPme            = false;
        ctrls.doNearestNearNew = false;
        ctrls.do3x3Bi          = false;
        ctrls.doGlobal         = false;
        break;
    case 1:  // FULL
        ctrls.totCompTypes     = kMdCompTypes;
        ctrls.doNearestNearest = true;
        ctrls.doNearNear       = true;
        ctrls.doMe             = true;
        ctrls.doPme            = true;
        ctrls.doNearestNearNew = true;
        ctrls.do3x3Bi          = true;
        ctrls.doGlobal         = true;

        ctrls.skipOnRefInfo    = false;
        ctrls.useRate          = true;
        ctrls.pred0ToPred1Mult = 0;
        ctrls.maxMvLength      = 0;
        ctrls.noSymDist        = false;
        break;
    case 2:
        ctrls.totCompTypes     = kMdCompTypes;
        ctrls.doNearestNearest = true;
        ctrls.doNearNear       = true;
        ctrls.doMe             = true;
        ctrls.doPme            = true;
        ctrls.doNearestNearNew = false;
        ctrls.do3x3Bi          = false;
        ctrls.doGlobal         = true;

        ctrls.skipOnRefInfo    = false;
        ctrls.useRate          = false;
        ctrls.pred0ToPred1Mult = 1;
        ctrls.maxMvLength      = 0;
        ctrls.noSymDist        = false;
        break;
    case 3:
        ctrls.totCompTypes     = kMdCompTypes;
        ctrls.doNearestNearest = true;
        ctrls.doNearNear       = true;
        ctrls.doMe             = true;
        ctrls.doPme            = false;
        ctrls.doNearestNearNew = false;
        ctrls.do3x3Bi          = false;
        ctrls.doGlobal         = true;

        ctrls.skipOnRefInfo    = true;
        ctrls.useRate          = false;
        ctrls.pred0ToPred1Mult = 1;
        ctrls.maxMvLength      = 0;
        ctrls.noSymDist        = true;
        break;
    case 4:
        ctrls.totCompTypes     = kMdCompTypes;
        ctrls.doNearestNearest = true;
        ctrls.doNearNear       = true;
        ctrls.doMe             = true;
        ctrls.doPme            = false;
        ctrls.doNearestNearNew = false;
        ctrls.do3x3Bi          = false;
        ctrls.doGlobal         = true;

        ctrls.skipOnRefInfo    = true;
        ctrls.useRate          = false;
        ctrls.pred0ToPred1Mult = 4;
        ctrls.maxMvLength      = 32;
        ctrls.noSymDist        = true;
        break;
    default:  // assert(0) in the reference; it writes nothing and we keep that.
        break;
    }
}

// get_inter_compound_level (enc_mode_config.c:8839-8850), 1:1.
std::uint8_t getInterCompoundLevel(EncMode encMode) {
    std::uint8_t compLevel;
    if (encMode <= ENC_M0) {
        compLevel = 3;
    } else if (encMode <= ENC_M2) {
        compLevel = 4;
    } else {
        compLevel = 0;
    }
    return compLevel;
}

// get_filter_intra_level_default (:8852-8863), 1:1.
std::uint8_t getFilterIntraLevelDefault(EncMode encMode) {
    std::uint8_t filterIntraLevel;
    if (encMode <= ENC_M1) {
        filterIntraLevel = 1;
    } else if (encMode <= ENC_M5) {
        filterIntraLevel = 2;
    } else {
        filterIntraLevel = 0;
    }
    return filterIntraLevel;
}

// get_filter_intra_level_rtc (:8865-8869), 1:1.
std::uint8_t getFilterIntraLevelRtc() {
    return 0;
}

// get_filter_intra_level_allintra (:8871-8882), 1:1.
std::uint8_t getFilterIntraLevelAllIntra(EncMode encMode) {
    std::uint8_t filterIntraLevel;
    if (encMode <= ENC_M0) {
        filterIntraLevel = 1;
    } else if (encMode <= ENC_M6) {
        filterIntraLevel = 2;
    } else {
        filterIntraLevel = 0;
    }
    return filterIntraLevel;
}

// svt_aom_get_inter_intra_level (:8884-8894), 1:1.
std::uint8_t getInterIntraLevel(EncMode encMode, std::uint8_t transitionPresent) {
    std::uint8_t interIntraLevel = 0;
    if (encMode <= ENC_M1) {
        interIntraLevel = 2;
    } else if (encMode <= ENC_M8) {
        interIntraLevel = transitionPresent ? 2 : 0;
    } else {
        interIntraLevel = 0;
    }
    return interIntraLevel;
}

// svt_aom_get_intra_mode_levels_allintra (:5429-5453), 1:1.
void getIntraModeLevelsAllIntra(EncMode encMode, std::uint32_t* intraLevel,
                                std::uint32_t* distBasedAngIntraLevel) {
    std::uint32_t intra;
    std::uint32_t dist;
    if (encMode <= ENC_M4) {
        intra = 1;
        dist  = 0;
    } else if (encMode <= ENC_M5) {
        intra = 2;
        dist  = 0;
    } else if (encMode <= ENC_M6) {
        intra = 6;
        dist  = 0;
    } else if (encMode <= ENC_M8) {
        intra = 7;
        dist  = 0;
    } else {
        intra = 8;
        dist  = 0;
    }
    *intraLevel                = intra;
    *distBasedAngIntraLevel    = dist;
}

// svt_aom_get_enable_me_8x8 (:146-173), 1:1.
std::uint8_t getEnableMe8x8(EncMode encMode, std::uint8_t inputResolution, bool rtcTune) {
    std::uint8_t enableMe8x8 = 0;
    if (rtcTune) {
        if (encMode <= ENC_M8) {
            if (inputResolution <= INPUT_SIZE_720p_RANGE) {
                enableMe8x8 = 1;
            } else {
                enableMe8x8 = 0;
            }
        } else {
            enableMe8x8 = 0;
        }
    } else {
        if (encMode <= ENC_M5) {
            enableMe8x8 = 1;
        } else if (encMode <= ENC_M8) {
            if (inputResolution <= INPUT_SIZE_720p_RANGE) {
                enableMe8x8 = 1;
            } else {
                enableMe8x8 = 0;
            }
        } else {
            enableMe8x8 = 0;
        }
    }
    return enableMe8x8;
}

// svt_aom_get_enable_me_16x16 (:175-179), 1:1.
std::uint8_t getEnableMe16x16(EncMode) {
    return 1;
}

// svt_aom_get_enable_sg_default (:2711-2715), 1:1.
std::uint8_t getEnableSgDefault(EncMode encMode, std::uint8_t inputResolution,
                                std::uint8_t fastDecode) {
    return getSgFilterLevelDefault(encMode, inputResolution, fastDecode) > 0 ? 1 : 0;
}

// svt_aom_get_enable_sg_rtc (:2717-2721), 1:1.
std::uint8_t getEnableSgRtc(std::uint8_t inputResolution, std::uint8_t fastDecode) {
    return getSgFilterLevelRtc(inputResolution, fastDecode) > 0 ? 1 : 0;
}

// svt_aom_get_enable_sg_allintra (:2723-2727), 1:1.
std::uint8_t getEnableSgAllIntra(EncMode encMode) {
    return getSgFilterLevelAllIntra(encMode) > 0 ? 1 : 0;
}

// svt_aom_get_enable_restoration_default (:2733-2748), 1:1.
std::uint8_t getEnableRestorationDefault(EncMode encMode, std::int8_t configEnableRestoration,
                                         std::uint8_t inputResolution, std::uint8_t fastDecode) {
    if (configEnableRestoration != (std::int8_t)kDefaultSentinel) {
        return (std::uint8_t)configEnableRestoration;
    }
    std::uint8_t wn = 0;
    for (int isRef = 0; isRef < 2; isRef++) {
        wn = getWnFilterLevelDefault(encMode, inputResolution, isRef != 0);
        if (wn) {
            break;
        }
    }
    std::uint8_t sg = getEnableSgDefault(encMode, inputResolution, fastDecode);
    return (sg > 0 || wn > 0) ? 1 : 0;
}

// svt_aom_get_enable_restoration_rtc (:2750-2765), 1:1.
std::uint8_t getEnableRestorationRtc(std::int8_t configEnableRestoration,
                                     std::uint8_t inputResolution, std::uint8_t fastDecode) {
    if (configEnableRestoration != (std::int8_t)kDefaultSentinel) {
        return (std::uint8_t)configEnableRestoration;
    }
    std::uint8_t wn = 0;
    for (int isRef = 0; isRef < 2; isRef++) {
        wn = getWnFilterLevelRtc(inputResolution);
        if (wn) {
            break;
        }
    }
    std::uint8_t sg = getEnableSgRtc(inputResolution, fastDecode);
    return (sg > 0 || wn > 0) ? 1 : 0;
}

// svt_aom_get_enable_restoration_allintra (:2767-2781), 1:1.
std::uint8_t getEnableRestorationAllIntra(EncMode encMode, std::int8_t configEnableRestoration) {
    if (configEnableRestoration != (std::int8_t)kDefaultSentinel) {
        return (std::uint8_t)configEnableRestoration;
    }
    std::uint8_t wn = 0;
    for (int isRef = 0; isRef < 2; isRef++) {
        wn = getWnFilterLevelAllIntra(encMode);
        if (wn) {
            break;
        }
    }
    std::uint8_t sg = getEnableSgAllIntra(encMode);
    return (sg > 0 || wn > 0) ? 1 : 0;
}

// svt_aom_sig_deriv_pre_analysis_pcs (:2788-2811), 1:1.
void sigDerivPreAnalysisPcs(PreAnalysisPcs& pcs) {
    // Derive ME enable flags based on current enc_mode
    ResolutionRange resolution = deriveInputResolution(pcs.maxInputLumaWidth * pcs.maxInputLumaHeight);
    pcs.enableMe16x16 = getEnableMe16x16(pcs.encMode) != 0;
    pcs.enableMe8x8 = pcs.enableMe16x16
        ? getEnableMe8x8(pcs.encMode, (std::uint8_t)resolution, pcs.rtcTune) != 0
        : false;

    // Derive HME Flag
    // Set here to allocate resources for the downsampled pictures used in HME (generated in PictureAnalysis)
    // Will be later updated for SC/NSC in PictureDecisionProcess
    pcs.enableHmeFlag        = true;
    pcs.enableHmeLevel0Flag  = true;
    pcs.enableHmeLevel1Flag  = true;
    pcs.enableHmeLevel2Flag  = true;
    pcs.tfEnableHmeFlag      = true;
    pcs.tfEnableHmeLevel0Flag = true;
    pcs.tfEnableHmeLevel1Flag = true;
    pcs.tfEnableHmeLevel2Flag = true;
}

// svt_aom_sig_deriv_pre_analysis_scs (:2818-2918), 1:1 with the five
// CONFIG_ENABLE_* forks threaded as runtime values (the pinned build
// resolves all five to 1; both arm shapes are tested).
void sigDerivPreAnalysisScs(PreAnalysisScs& scs, std::int8_t encMode, const PreAnalysisCfg& cfg) {
    const EncMode mode = (EncMode)encMode;
    const bool allintra = scs.allintra;
    // initialize sequence level enable_superres
    if (cfg.superres) {
        scs.seqEnableSuperres = scs.superresMode > 0 /* SUPERRES_NONE */ ? true : false;
    } else {
        scs.seqEnableSuperres = false;
    }
    if (cfg.interIntra) {
        std::uint8_t iiAllowed = 0;
        for (std::uint8_t transitionPresent = 0; transitionPresent < 2; transitionPresent++) {
            if (iiAllowed) {
                break;
            }
            iiAllowed |= getInterIntraLevel(mode, transitionPresent);
        }
        scs.seqEnableInterintraCompound = iiAllowed ? true : false;
    } else {
        scs.seqEnableInterintraCompound = false;
    }
    const bool rtcTune = scs.rtcTune;
    if (cfg.filterIntra) {
        std::uint8_t isFilterIntraUsed =
            allintra ? getFilterIntraLevelAllIntra(mode)
            : rtcTune ? getFilterIntraLevelRtc()
                      : getFilterIntraLevelDefault(mode);
        scs.seqFilterIntraLevel = isFilterIntraUsed ? 1 : 0;
    } else {
        scs.seqFilterIntraLevel = 0;
    }
    if (cfg.interCompound) {
        if (getInterCompoundLevel(mode)) {
            scs.seqEnableJntComp        = 1;  // DISTANCE
            scs.seqEnableMaskedCompound = 1;  // DIFF+WEDGE
        } else {
            scs.seqEnableJntComp        = 0;
            scs.seqEnableMaskedCompound = 0;
        }
    } else {
        scs.seqEnableJntComp        = 0;
        scs.seqEnableMaskedCompound = 0;
    }
    // For non-still-image or non-all-intra configurations, keep edge filter always ON, otherwise OFF unless angular refinement pruning techniques are active
    if (allintra) {
        // Flag indicating whether angular refinement pruning is active
        std::uint32_t intraLevel = 0;
        std::uint32_t distBasedAngIntraLevel = 0;
        getIntraModeLevelsAllIntra(mode, &intraLevel, &distBasedAngIntraLevel);
        scs.seqEnableIntraEdgeFilter =
            distBasedAngIntraLevel >= 1 || angularPredLevel[intraLevel] == 2 ||
                    angularPredLevel[intraLevel] == 3
                ? 1
                : 0;
    } else {
        scs.seqEnableIntraEdgeFilter = 1;
    }
    if (cfg.restoration) {
        if (scs.enableRestorationFiltering == kDefaultSentinel) {
            // As allocation has already happened based on the initial input resolution, the resolution
            // changes should not impact enabling restoration. For some presets, restoration is off for 8K
            // and above and memory allocation is not performed. So, if we switch to smaller resolution, we need
            // to keep restoration off
            ResolutionRange initInputResolution =
                deriveInputResolution(scs.maxInitialInputLumaWidth * scs.maxInitialInputLumaHeight);
            scs.seqEnableRestoration =
                allintra
                    ? getEnableRestorationAllIntra(mode,
                                                   (std::int8_t)scs.enableRestorationFiltering)
                    : rtcTune ? getEnableRestorationRtc((std::int8_t)scs.enableRestorationFiltering,
                                                        (std::uint8_t)initInputResolution,
                                                        scs.fastDecode)
                              : getEnableRestorationDefault(mode,
                                                            (std::int8_t)scs.enableRestorationFiltering,
                                                            (std::uint8_t)initInputResolution,
                                                            scs.fastDecode);
        } else {
            ResolutionRange frRes =
                deriveInputResolution(scs.maxInitialInputLumaWidth * scs.maxInitialInputLumaHeight);
            const std::uint8_t autoEn =
                allintra ? getEnableRestorationAllIntra(mode, (std::int8_t)kDefaultSentinel)
                : rtcTune
                    ? getEnableRestorationRtc((std::int8_t)kDefaultSentinel,
                                              (std::uint8_t)frRes, scs.fastDecode)
                    : getEnableRestorationDefault(mode, (std::int8_t)kDefaultSentinel,
                                                  (std::uint8_t)frRes, scs.fastDecode);
            scs.seqEnableRestoration =
                (scs.enableRestorationFiltering != 0 && autoEn) ? 1 : 0;
        }
    } else {
        scs.seqEnableRestoration = 0;
    }
    if (scs.cdefLevel == kDefaultSentinel) {
        scs.seqCdefLevel = 1;
    } else {
        scs.seqCdefLevel = (std::uint8_t)(scs.cdefLevel > 0);
    }
    scs.seqEnableWarpedMotion = true;
}

// svt_aom_is_ref_same_size (:2925-2941), 1:1 with the EbReferenceObject
// wall folded into the per-slot present/width/height arrays.
bool isRefSameSize(const RefSameSizePcs& pcs, std::uint8_t listIdx, std::uint8_t refIdx) {
    if (!pcs.isBSlice) {
        return false;
    }
    const int refCount = pcs.refListCountTry[listIdx];
    if (refCount == 0 || !pcs.refPresent[listIdx][refIdx]) {
        return false;
    }
    return pcs.refPicWidth[listIdx][refIdx] == pcs.frameWidth &&
        pcs.refPicHeight[listIdx][refIdx] == pcs.frameHeight;
}


}  // namespace mode_config
}  // namespace l16_rdo
