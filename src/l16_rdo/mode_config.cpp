// l16_rdo - EMC1 slice 1: the standalone arithmetic of the A4
// mode-config surface.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/enc_mode_config.c.
// Every span is cited at its site; deviations are named where the wall
// threads as a parameter (the allowPalette / RE8A DeblockContext
// precedents).
#include "mode_config.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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


// set_obmc_controls (enc_mode_config.c:2943-3007), 1:1.
void setObmcCtrls(ObmcCtrls& ctrls, std::uint8_t obmcMode) {
    switch (obmcMode) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled                = 1;
        ctrls.maxBlkSizeToRefine     = 128;
        ctrls.maxBlkSize             = 128;
        ctrls.refineLevel            = 0;
        ctrls.transFaceOff           = 0;
        ctrls.fpelSearchRange        = 16;
        ctrls.fpelSearchDiag         = 1;
        break;
    case 2:
        ctrls.enabled                = 1;
        ctrls.maxBlkSizeToRefine     = 64;
        ctrls.maxBlkSize             = 128;
        ctrls.refineLevel            = 1;
        ctrls.transFaceOff           = 0;
        ctrls.fpelSearchRange        = 16;
        ctrls.fpelSearchDiag         = 1;
        break;
    case 3:
        ctrls.enabled                = 1;
        ctrls.maxBlkSizeToRefine     = 32;
        ctrls.maxBlkSize             = 128;
        ctrls.refineLevel            = 1;
        ctrls.transFaceOff           = 0;
        ctrls.fpelSearchRange        = 8;
        ctrls.fpelSearchDiag         = 0;
        break;
    case 4:
        ctrls.enabled                = 1;
        ctrls.maxBlkSizeToRefine     = 32;
        ctrls.maxBlkSize             = 128;
        ctrls.refineLevel            = 1;
        ctrls.transFaceOff           = 1;
        ctrls.fpelSearchRange        = 16;
        ctrls.fpelSearchDiag         = 1;
        break;
    case 5:
        ctrls.enabled                = 1;
        ctrls.maxBlkSizeToRefine     = 32;
        ctrls.maxBlkSize             = 32;
        ctrls.refineLevel            = 4;
        ctrls.transFaceOff           = 1;
        ctrls.fpelSearchRange        = 8;
        ctrls.fpelSearchDiag         = 0;
        break;
    case 6:
        ctrls.enabled                = 1;
        ctrls.maxBlkSizeToRefine     = 16;
        ctrls.maxBlkSize             = 16;
        ctrls.refineLevel            = 4;
        ctrls.transFaceOff           = 1;
        ctrls.fpelSearchRange        = 8;
        ctrls.fpelSearchDiag         = 0;
        break;
    default:
        ctrls.enabled = 0;
        break;
    }
}

// dimensions_require_8x8 (enc_mode_config.c:3012-3028), 1:1. Assumes the
// max block size is 64x64 because ISLICEs do not use 128x128 blocks,
// even when enabled at the sequence level. Not NSQ aware.
bool dimensionsRequire8x8(std::uint16_t alignedWidth, std::uint16_t alignedHeight) {
    // start checking if 64x64 is allowed b/c we don't use 128x128 for ISLICE
    const std::uint16_t startBsize    = 64;
    std::uint16_t leftoverWidth  = (std::uint16_t)(alignedWidth % startBsize);
    std::uint16_t leftoverHeight = (std::uint16_t)(alignedHeight % startBsize);
    for (std::uint16_t halfBsize = (std::uint16_t)(startBsize >> 1); halfBsize >= 8;
         halfBsize = (std::uint16_t)(halfBsize >> 1)) {
        // If allowable block size is found, don't require 8x8
        if ((leftoverWidth == 0 || leftoverWidth > halfBsize) &&
            (leftoverHeight == 0 || leftoverHeight > halfBsize)) {
            return false;
        }
        leftoverWidth  = (std::uint16_t)(leftoverWidth % halfBsize);
        leftoverHeight = (std::uint16_t)(leftoverHeight % halfBsize);
    }
    // No allowable block size from 16x16-64x64 was found; therefore, require 8x8
    return true;
}

// ---------------------------------------------------------------------------
// Batch C rung 1: set_depth_removal_level_controls (:3030-3343) and its
// get_sb128_me_data dependency (:63-115).
// ---------------------------------------------------------------------------

// quantizer_to_qindex (md_process.c:20-23, verbatim) - the rate_est.cpp
// twin duplicated (the l7/l13/l16 duplication precedent).
static const std::uint8_t kQuantizerToQindex[64] = {
    0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60,  64,  68,  72,  76,  80,  84,
    88,  92,  96,  100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152, 156, 160, 164, 168, 172,
    176, 180, 184, 188, 192, 196, 200, 204, 208, 212, 216, 220, 224, 228, 232, 236, 240, 244, 249, 255};

// RDCOST (rd_cost.h:36-38) with ROUND_POWER_OF_TWO (definitions.h:457),
// AV1_PROB_COST_SHIFT 9 (md_rate_estimation.h:29) and RDDIV_BITS 7
// (rd_cost.h:34) - the rate_est.cpp twin duplicated.
static std::int64_t rdCost(std::int64_t rdmult, std::int64_t rate, std::int64_t dist) {
    return (((rate * rdmult) + (((1 << 9) >> 1))) >> 9) + (dist * ((std::int64_t)1 << 7));
}

// get_sb128_me_data (enc_mode_config.c:63-115), 1:1. The
// PictureControlSet/ModeDecisionContext walls thread as the consumed
// scalars + the five ME arrays (pcs.h:866-870).
static void getSb128MeData(std::uint8_t b64Size, std::uint32_t alignedWidth,
                           std::uint32_t alignedHeight, std::uint32_t sbOriginX,
                           std::uint32_t sbOriginY, const std::uint32_t* me64x64Distortion,
                           const std::uint32_t* me32x32Distortion,
                           const std::uint32_t* me16x16Distortion,
                           const std::uint32_t* me8x8Distortion,
                           const std::uint32_t* me8x8CostVariance, std::uint32_t* me64x64Dist,
                           std::uint32_t* me32x32Dist, std::uint32_t* me16x16Dist,
                           std::uint32_t* me8x8Dist, std::uint32_t* me8x8CostVar) {
    const std::uint32_t meSbSize         = b64Size;
    const std::uint32_t mePicWidthInSb  = (alignedWidth + meSbSize - 1) / meSbSize;
    const std::uint32_t mePicHeightInSb = (alignedHeight + meSbSize - 1) / meSbSize;
    const std::uint32_t meSbX = (sbOriginX / meSbSize);
    const std::uint32_t meSbY = (sbOriginY / meSbSize);
    const std::uint32_t meSbAddr = meSbX + meSbY * mePicWidthInSb;

    std::uint32_t dist64 = me64x64Distortion[meSbAddr];
    std::uint32_t dist32 = me32x32Distortion[meSbAddr];
    std::uint32_t dist16 = me16x16Distortion[meSbAddr];
    std::uint32_t dist8  = me8x8Distortion[meSbAddr];
    std::uint32_t meCostVar = me8x8CostVariance[meSbAddr];
    int count = 1;
    if (meSbX + 1 < mePicWidthInSb) {
        dist64 += me64x64Distortion[meSbAddr + 1];
        dist32 += me32x32Distortion[meSbAddr + 1];
        dist16 += me16x16Distortion[meSbAddr + 1];
        dist8 += me8x8Distortion[meSbAddr + 1];
        meCostVar = std::max(meCostVar, me8x8CostVariance[meSbAddr + 1]);
        count++;
    }
    if (meSbY + 1 < mePicHeightInSb) {
        dist64 += me64x64Distortion[meSbAddr + mePicWidthInSb];
        dist32 += me32x32Distortion[meSbAddr + mePicWidthInSb];
        dist16 += me16x16Distortion[meSbAddr + mePicWidthInSb];
        dist8 += me8x8Distortion[meSbAddr + mePicWidthInSb];
        meCostVar = std::max(meCostVar, me8x8CostVariance[meSbAddr + mePicWidthInSb]);
        count++;
    }
    if (meSbX + 1 < mePicWidthInSb && meSbY + 1 < mePicHeightInSb) {
        dist64 += me64x64Distortion[meSbAddr + mePicWidthInSb + 1];
        dist32 += me32x32Distortion[meSbAddr + mePicWidthInSb + 1];
        dist16 += me16x16Distortion[meSbAddr + mePicWidthInSb + 1];
        dist8 += me8x8Distortion[meSbAddr + mePicWidthInSb + 1];
        meCostVar = std::max(meCostVar, me8x8CostVariance[meSbAddr + mePicWidthInSb + 1]);
        count++;
    }
    dist64 /= count;
    dist32 /= count;
    dist16 /= count;
    dist8 /= count;

    *me64x64Dist   = dist64;
    *me32x32Dist   = dist32;
    *me16x16Dist   = dist16;
    *me8x8Dist     = dist8;
    *me8x8CostVar  = meCostVar;
}

// The :3068-3224 switch, 1:1 (the fold's struct is the
// getGmCorrespondence precedent). NO default case in the reference:
// level > 15 leaves enabled unwritten (enabledWritten = false) and the
// locals at their pre-switch init values.
void getDepthRemovalLevelParams(std::uint8_t level, DepthRemovalLevelParams& params) {
    // Pre-switch inits (:3043-3052).
    params.enabledWritten                     = false;
    params.enabled                            = 0;
    params.disallow4x4CostThMultiplier        = 0;
    params.disallowBelow16x16CostThMultiplier = 0;
    params.disallowBelow32x32CostThMultiplier = 0;
    params.disallowBelow64x64CostThMultiplier = 0;
    params.dev16x16To8x8Th                    = kMaxSignedValue;
    params.dev32x32To16x16Th                  = kMaxSignedValue;
    params.qpScaleFactor                      = 0;

    switch (level) {
    case 0:
        params.enabledWritten = true;
        params.enabled        = 0;
        break;
    case 1:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 0;
        params.disallowBelow32x32CostThMultiplier = 0;
        params.disallowBelow64x64CostThMultiplier = 0;
        params.dev16x16To8x8Th                    = 0;
        params.dev32x32To16x16Th                  = 0;
        params.qpScaleFactor                      = 1;
        break;
    case 2:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 0;
        params.disallowBelow32x32CostThMultiplier = 0;
        params.disallowBelow64x64CostThMultiplier = 0;
        params.dev16x16To8x8Th                    = 10;
        params.dev32x32To16x16Th                  = 0;
        params.qpScaleFactor                      = 1;
        break;
    case 3:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 0;
        params.disallowBelow32x32CostThMultiplier = 0;
        params.disallowBelow64x64CostThMultiplier = 0;
        params.dev16x16To8x8Th                    = 20;
        params.dev32x32To16x16Th                  = 0;
        params.qpScaleFactor                      = 1;
        break;
    case 4:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 0;
        params.disallowBelow32x32CostThMultiplier = 0;
        params.disallowBelow64x64CostThMultiplier = 0;
        params.dev16x16To8x8Th                    = 30;
        params.dev32x32To16x16Th                  = 0;
        params.qpScaleFactor                      = 1;
        break;
    case 5:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 6;
        params.disallowBelow32x32CostThMultiplier = 6;
        params.disallowBelow64x64CostThMultiplier = 0;
        params.dev16x16To8x8Th                    = 40;
        params.dev32x32To16x16Th                  = 0;
        params.qpScaleFactor                      = 1;
        break;
    case 6:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 6;
        params.disallowBelow32x32CostThMultiplier = 6;
        params.disallowBelow64x64CostThMultiplier = 0;
        params.dev16x16To8x8Th                    = 50;
        params.dev32x32To16x16Th                  = 25;
        params.qpScaleFactor                      = 1;
        break;
    case 7:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 6;
        params.disallowBelow32x32CostThMultiplier = 6;
        params.disallowBelow64x64CostThMultiplier = 0;
        params.dev16x16To8x8Th                    = 50;
        params.dev32x32To16x16Th                  = 25;
        params.qpScaleFactor                      = 2;
        break;
    case 8:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 16;
        params.disallowBelow32x32CostThMultiplier = 8;
        params.disallowBelow64x64CostThMultiplier = 8;
        params.dev16x16To8x8Th                    = 100;
        params.dev32x32To16x16Th                  = 50;
        params.qpScaleFactor                      = 3;
        break;
    case 9:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 32;
        params.disallowBelow32x32CostThMultiplier = 8;
        params.disallowBelow64x64CostThMultiplier = 8;
        params.dev16x16To8x8Th                    = 100;
        params.dev32x32To16x16Th                  = 50;
        params.qpScaleFactor                      = 3;
        break;
    case 10:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 128;
        params.disallowBelow32x32CostThMultiplier = 8;
        params.disallowBelow64x64CostThMultiplier = 8;
        params.dev16x16To8x8Th                    = 200;
        params.dev32x32To16x16Th                  = 75;
        params.qpScaleFactor                      = 3;
        break;
    case 11:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 128;
        params.disallowBelow32x32CostThMultiplier = 8;
        params.disallowBelow64x64CostThMultiplier = 8;
        params.dev16x16To8x8Th                    = 250;
        params.dev32x32To16x16Th                  = 125;
        params.qpScaleFactor                      = 3;
        break;
    case 12:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 128;
        params.disallowBelow32x32CostThMultiplier = 16;
        params.disallowBelow64x64CostThMultiplier = 8;
        params.dev16x16To8x8Th                    = 250;
        params.dev32x32To16x16Th                  = 150;
        params.qpScaleFactor                      = 4;
        break;
    case 13:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 256;
        params.disallowBelow32x32CostThMultiplier = 16;
        params.disallowBelow64x64CostThMultiplier = 8;
        params.dev16x16To8x8Th                    = 250;
        params.dev32x32To16x16Th                  = 150;
        params.qpScaleFactor                      = 4;
        break;
    case 14:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 256;
        params.disallowBelow32x32CostThMultiplier = 16;
        params.disallowBelow64x64CostThMultiplier = 16;
        params.dev16x16To8x8Th                    = 250;
        params.dev32x32To16x16Th                  = 150;
        params.qpScaleFactor                      = 4;
        break;
    case 15:
        params.enabledWritten                     = true;
        params.enabled                            = 1;
        params.disallow4x4CostThMultiplier        = 64;
        params.disallowBelow16x16CostThMultiplier = 384;
        params.disallowBelow32x32CostThMultiplier = 24;
        params.disallowBelow64x64CostThMultiplier = 24;
        params.dev16x16To8x8Th                    = 300;
        params.dev32x32To16x16Th                  = 200;
        params.qpScaleFactor                      = 4;
        break;
    }
}

// set_depth_removal_level_controls (enc_mode_config.c:3030-3343), 1:1.
void setDepthRemovalLevelCtrls(DepthRemovalCtrls& ctrls, const DepthRemovalPcs& pcs,
                               DepthRemovalCtx& ctx, std::uint8_t depthRemovalLevel) {
    if (pcs.isISlice) {
        ctrls.enabled = 0;
    } else {
        // me_distortion => EB_8_BIT_MD (:3036-3037).
        const std::uint32_t fastLambda = ctx.fastLambdaMd[0];

        const std::uint32_t sbSize = 64 * 64;

        const std::uint64_t costThRate = (std::uint64_t)1 << 13;

        std::uint64_t disallow4x4CostThMultiplier        = 0;
        std::uint64_t disallowBelow16x16CostThMultiplier = 0;
        std::uint64_t disallowBelow32x32CostThMultiplier = 0;
        std::uint64_t disallowBelow64x64CostThMultiplier = 0;

        std::int64_t dev16x16To8x8Th   = kMaxSignedValue;
        std::int64_t dev32x32To16x16Th = kMaxSignedValue;
        std::int64_t dev32x32To8x8Th   = kMaxSignedValue;

        std::int8_t qpScaleFactor = 0;

        // modulate the depth-removal level using: (1) tpl-information,
        // specifically the SB delta-qp, and (2) the high-freq information
        // (:3054-3066). The delta_q_present || r0_delta_qp_md gate folds
        // into modulateByQp.
        if (pcs.modulateByQp) {
            const int diff = (int)ctx.sbQindex - (int)kQuantizerToQindex[pcs.pictureQp];
            if (diff <= -12) {
                depthRemovalLevel = (std::uint8_t)std::max(0, (int)depthRemovalLevel - 4);
            } else if (diff <= -6) {
                depthRemovalLevel = (std::uint8_t)std::max(0, (int)depthRemovalLevel - 3);
            } else if (diff <= -3) {
                depthRemovalLevel = (std::uint8_t)std::max(0, (int)depthRemovalLevel - 2);
            } else if (diff < 0) {
                depthRemovalLevel = (std::uint8_t)std::max(0, (int)depthRemovalLevel - 1);
            }
        }

        DepthRemovalLevelParams params;
        getDepthRemovalLevelParams(depthRemovalLevel, params);
        if (params.enabledWritten) {
            ctrls.enabled                            = params.enabled;
            disallow4x4CostThMultiplier              = params.disallow4x4CostThMultiplier;
            disallowBelow16x16CostThMultiplier       = params.disallowBelow16x16CostThMultiplier;
            disallowBelow32x32CostThMultiplier       = params.disallowBelow32x32CostThMultiplier;
            disallowBelow64x64CostThMultiplier       = params.disallowBelow64x64CostThMultiplier;
            dev16x16To8x8Th                          = params.dev16x16To8x8Th;
            dev32x32To16x16Th                        = params.dev32x32To16x16Th;
            qpScaleFactor                            = params.qpScaleFactor;
        }
        if (ctrls.enabled) {
            // Use info from ref. frames (if available) (:3228-3262).
            const bool useRefInfo = pcs.rtc ? false : true;
            if (useRefInfo) {
                const bool isRefL0Avail = isRefSameSize(pcs.ref, 0, 0);
                const bool isRefL1Avail = isRefSameSize(pcs.ref, 1, 0);

                if (isRefL0Avail) {
                    std::uint8_t sbMinSqSize = (std::uint8_t)~0;

                    if (std::abs((int)(pcs.pictureNumber - pcs.refPocL0)) <= 1) {
                        sbMinSqSize = pcs.refSbMinSqSizeL0;
                    }

                    if (isRefL1Avail) {
                        if (std::abs((int)(pcs.pictureNumber - pcs.refPocL1)) <= 1) {
                            sbMinSqSize =
                                (std::uint8_t)std::min(sbMinSqSize, pcs.refSbMinSqSizeL1);
                        }
                    }

                    if (sbMinSqSize != (std::uint8_t)~0) {
                        if (sbMinSqSize >= 64) {
                            dev32x32To16x16Th = dev32x32To16x16Th + 5;
                            dev16x16To8x8Th   = dev16x16To8x8Th + 20;
                        } else if (sbMinSqSize >= 32) {
                            dev16x16To8x8Th = dev16x16To8x8Th + 15;
                        }
                    }
                }
            }
            std::uint32_t dist64, dist32, dist16, dist8, me8x8CostVariance;
            if (pcs.superBlockSize == 64) {
                dist64              = pcs.me64x64Distortion[ctx.sbIndex];
                dist32              = pcs.me32x32Distortion[ctx.sbIndex];
                dist16              = pcs.me16x16Distortion[ctx.sbIndex];
                dist8               = pcs.me8x8Distortion[ctx.sbIndex];
                me8x8CostVariance   = pcs.me8x8CostVariance[ctx.sbIndex];
            } else {
                getSb128MeData(pcs.b64Size, pcs.alignedWidth, pcs.alignedHeight, ctx.sbOriginX,
                               ctx.sbOriginY, pcs.me64x64Distortion, pcs.me32x32Distortion,
                               pcs.me16x16Distortion, pcs.me8x8Distortion, pcs.me8x8CostVariance,
                               &dist64, &dist32, &dist16, &dist8, &me8x8CostVariance);
            }

            // dev_16x16_to_8x8_th, dev_32x32_to_16x16_th =
            // f(me_8x8_cost_variance) (:3274-3284). The divisor's
            // unsigned arithmetic WRAPS for picture_qp > 53 - the
            // reference's own behavior, mirrored.
            me8x8CostVariance /= std::max(std::max(63u - (pcs.pictureQp + 10u), 1u), 1u);
            if (me8x8CostVariance < kLow8x8DistVarTh) {
                dev16x16To8x8Th = dev16x16To8x8Th << 2;
            } else if (me8x8CostVariance < kHigh8x8DistVarTh) {
                dev16x16To8x8Th   = dev16x16To8x8Th << 1;
                dev32x32To16x16Th = dev32x32To16x16Th >> 1;
            } else {
                dev16x16To8x8Th   = 0;
                dev32x32To16x16Th = 0;
            }

            // dev_16x16_to_8x8_th, dev_32x32_to_16x16_th = f(QP)
            // (:3286-3288); the unsigned wrap mirrors.
            const std::uint32_t qpWeight =
                std::max(std::max(63u - (pcs.pictureQp + 10u), 1u) >> 4, 1u);
            dev16x16To8x8Th   *= (std::int64_t)(qpWeight * (std::uint32_t)qpScaleFactor);
            dev32x32To16x16Th *= (std::int64_t)(qpWeight * (std::uint32_t)qpScaleFactor);
            // dev_32x32_to_8x8_th = f(dev_32x32_to_16x16_th); a bit higher
            // (:3289-3290).
            dev32x32To8x8Th = (dev32x32To16x16Th * ((1 << 2) + 1)) >> 2;

            const std::uint64_t disallowBelow16x16CostTh = disallowBelow16x16CostThMultiplier
                ? (std::uint64_t)rdCost((std::int64_t)fastLambda, (std::int64_t)costThRate,
                                        (std::int64_t)((sbSize >> 3) *
                                                       disallowBelow16x16CostThMultiplier))
                : 0;
            const std::uint64_t disallowBelow32x32CostTh = disallowBelow32x32CostThMultiplier
                ? (std::uint64_t)rdCost((std::int64_t)fastLambda, (std::int64_t)costThRate,
                                        (std::int64_t)((sbSize >> 3) *
                                                       disallowBelow32x32CostThMultiplier))
                : 0;
            const std::uint64_t disallowBelow64x64CostTh = disallowBelow64x64CostThMultiplier
                ? (std::uint64_t)rdCost((std::int64_t)fastLambda, (std::int64_t)costThRate,
                                        (std::int64_t)((sbSize >> 3) *
                                                       disallowBelow64x64CostThMultiplier))
                : 0;

            const std::uint64_t cost64x64 = (std::uint64_t)rdCost((std::int64_t)fastLambda, 0,
                                                                  (std::int64_t)dist64);
            const std::uint64_t cost32x32 = (std::uint64_t)rdCost((std::int64_t)fastLambda, 0,
                                                                  (std::int64_t)dist32);
            const std::uint64_t cost16x16 = (std::uint64_t)rdCost((std::int64_t)fastLambda, 0,
                                                                  (std::int64_t)dist16);
            const std::uint64_t cost8x8   = (std::uint64_t)rdCost((std::int64_t)fastLambda, 0,
                                                                  (std::int64_t)dist8);

            const std::int64_t dev32x32To16x16 =
                (std::int64_t)(((std::int64_t)std::max(cost32x32, (std::uint64_t)1) -
                                (std::int64_t)std::max(cost16x16, (std::uint64_t)1)) *
                               1000) /
                (std::int64_t)std::max(cost16x16, (std::uint64_t)1);

            const std::int64_t dev32x32To8x8 =
                (std::int64_t)(((std::int64_t)std::max(cost32x32, (std::uint64_t)1) -
                                (std::int64_t)std::max(cost8x8, (std::uint64_t)1)) *
                               1000) /
                (std::int64_t)std::max(cost8x8, (std::uint64_t)1);

            const std::int64_t dev16x16To8x8 =
                (std::int64_t)(((std::int64_t)std::max(cost16x16, (std::uint64_t)1) -
                                (std::int64_t)std::max(cost8x8, (std::uint64_t)1)) *
                               1000) /
                (std::int64_t)std::max(cost8x8, (std::uint64_t)1);

            // Enable depth removal at a given depth if the entire SB can be
            // covered by blocks of that size (to avoid disallowing
            // necessary blocks) (:3316-3332).
            ctrls.disallowBelow64x64 = (std::uint8_t)(
                (((pcs.sbGeomWidth % 64) == 0 || (pcs.sbGeomWidth % 64) > 32) &&
                 ((pcs.sbGeomHeight % 64) == 0 || (pcs.sbGeomHeight % 64) > 32))
                    ? (ctrls.disallowBelow64x64 || cost64x64 < disallowBelow64x64CostTh)
                    : 0);

            ctrls.disallowBelow32x32 = (std::uint8_t)(
                (((pcs.sbGeomWidth % 32) == 0 || (pcs.sbGeomWidth % 32) > 16) &&
                 ((pcs.sbGeomHeight % 32) == 0 || (pcs.sbGeomHeight % 32) > 16))
                    ? (ctrls.disallowBelow32x32 || cost32x32 < disallowBelow32x32CostTh ||
                       (dev32x32To16x16 < dev32x32To16x16Th &&
                        dev32x32To8x8 < dev32x32To8x8Th))
                    : 0);

            ctrls.disallowBelow16x16 = (std::uint8_t)(
                (!dimensionsRequire8x8(pcs.sbGeomWidth, pcs.sbGeomHeight))
                    ? (ctrls.disallowBelow16x16 || cost16x16 < disallowBelow16x16CostTh ||
                       dev16x16To8x8 < dev16x16To8x8Th)
                    : 0);
            if (!ctx.disallow4x4 && disallow4x4CostThMultiplier) {
                const std::uint64_t disallow4x4CostTh =
                    (std::uint64_t)rdCost((std::int64_t)fastLambda, (std::int64_t)costThRate,
                                          (std::int64_t)((sbSize >> 1) *
                                                         disallow4x4CostThMultiplier));

                if (cost8x8 < disallow4x4CostTh && me8x8CostVariance < kLow8x8DistVarTh) {
                    ctx.disallow4x4 = 1;
                }
            }
        }
    }
}


// ---------------------------------------------------------------------------
// Batch C rung 2: the MD search setters.
// ---------------------------------------------------------------------------

// md_nsq_motion_search_controls (enc_mode_config.c:3348-3373), 1:1.
void setMdNsqMotionSearchCtrls(MdNsqMotionSearchCtrls& ctrls, std::uint8_t mdNsqMvSearchLevel) {
    switch (mdNsqMvSearchLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled            = 1;
        ctrls.distType           = (std::uint8_t)VAR;
        ctrls.fullPelSearchWidth  = 32;
        ctrls.fullPelSearchHeight = 16;
        ctrls.enablePsad         = 1;
        break;
    case 2:
        ctrls.enabled            = 1;
        ctrls.distType           = (std::uint8_t)VAR;
        ctrls.fullPelSearchWidth  = 16;
        ctrls.fullPelSearchHeight = 8;
        ctrls.enablePsad         = 1;
        break;
    default:
        // The reference assert(0)s (:3369-3371) and writes nothing.
        break;
    }
}

// svt_aom_md_pme_search_controls (enc_mode_config.c:3375-3451), 1:1.
void setMdPmeCtrls(MdPmeCtrls& ctrls, std::uint8_t mdPmeLevel) {
    switch (mdPmeLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled                  = 1;
        ctrls.distType                 = (std::uint8_t)VAR;
        ctrls.fullPelSearchWidth       = 9;
        ctrls.fullPelSearchHeight      = 9;
        ctrls.earlyCheckMvThMultiplier = kMinSignedValue;
        ctrls.preFpPmeToMeCostTh       = kMaxSignedValue;
        ctrls.preFpPmeToMeMvTh         = kMinSignedValue;
        ctrls.postFpPmeToMeCostTh      = kMaxSignedValue;
        ctrls.postFpPmeToMeMvTh        = kMinSignedValue;
        ctrls.enablePsad               = 0;
        ctrls.saQWeight                = 0;
        break;
    case 2:
        ctrls.enabled                  = 1;
        ctrls.distType                 = (std::uint8_t)VAR;
        ctrls.fullPelSearchWidth       = 9;
        ctrls.fullPelSearchHeight      = 9;
        ctrls.earlyCheckMvThMultiplier = kMinSignedValue;
        ctrls.preFpPmeToMeCostTh       = kMaxSignedValue;
        ctrls.preFpPmeToMeMvTh         = kMinSignedValue;
        ctrls.postFpPmeToMeCostTh      = kMaxSignedValue;
        ctrls.postFpPmeToMeMvTh        = kMinSignedValue;
        ctrls.enablePsad               = 0;
        ctrls.saQWeight                = 1;
        break;
    case 3:
        ctrls.enabled                  = 1;
        ctrls.distType                 = (std::uint8_t)VAR;
        ctrls.fullPelSearchWidth       = 9;
        ctrls.fullPelSearchHeight      = 7;
        ctrls.earlyCheckMvThMultiplier = kMinSignedValue;
        ctrls.preFpPmeToMeCostTh       = kMaxSignedValue;
        ctrls.preFpPmeToMeMvTh         = 16;
        ctrls.postFpPmeToMeCostTh      = 50;
        ctrls.postFpPmeToMeMvTh        = kMinSignedValue;
        ctrls.enablePsad               = 0;
        ctrls.saQWeight                = 1;
        break;
    case 4:
        ctrls.enabled                  = 1;
        ctrls.distType                 = (std::uint8_t)SAD;
        ctrls.fullPelSearchWidth       = 7;
        ctrls.fullPelSearchHeight      = 5;
        ctrls.earlyCheckMvThMultiplier = kMinSignedValue;
        ctrls.preFpPmeToMeCostTh       = 25;
        ctrls.preFpPmeToMeMvTh         = 16;
        ctrls.postFpPmeToMeCostTh      = 50;
        ctrls.postFpPmeToMeMvTh        = 32;
        ctrls.enablePsad               = 1;
        ctrls.saQWeight                = 1;
        break;
    case 5:
        ctrls.enabled                  = 1;
        ctrls.distType                 = (std::uint8_t)SAD;
        ctrls.fullPelSearchWidth       = 7;
        ctrls.fullPelSearchHeight      = 5;
        ctrls.earlyCheckMvThMultiplier = 64;
        ctrls.preFpPmeToMeCostTh       = 25;
        ctrls.preFpPmeToMeMvTh         = 16;
        ctrls.postFpPmeToMeCostTh      = 50;
        ctrls.postFpPmeToMeMvTh        = 32;
        ctrls.enablePsad               = 1;
        ctrls.saQWeight                = 1;
        break;
    default:
        // The reference assert(0)s (:3447-3449) and writes nothing.
        break;
    }
}

// set_subres_controls (enc_mode_config.c:3453-3476), 1:1. The
// post-switch adjustment reads step - for level > 2 the switch wrote
// nothing, so step is the struct's prior state (documented).
void setSubresCtrls(SubresCtrls& ctrls, std::uint8_t subresLevel) {
    switch (subresLevel) {
    case 0:
        ctrls.step = 0;
        break;
    case 1:
        ctrls.step = 1;
        break;
    case 2:
        ctrls.step = 2;
        break;
    default:
        // The reference assert(0)s (:3466-3468) and writes nothing.
        break;
    }
    // Set the TH used to determine if subres is safe to use (based on
    // ODD vs. EVEN rows' distortion).
    if (ctrls.step == 0) {
        ctrls.oddToEvenDeviationTh = 0;
    } else {
        ctrls.oddToEvenDeviationTh = 5;
    }
}

// set_pf_controls (enc_mode_config.c:3478-3503), 1:1. The
// #if !CONFIG_ENABLE_TX_PF_N2 assert is compiled out in the pinned build
// (CONFIG_ENABLE_TX_PF_N2 = 1) - named in the header.
void setPfCtrls(PfCtrls& ctrls, std::uint8_t pfLevel) {
    switch (pfLevel) {
    case 0:
        ctrls.pfShape = (std::uint8_t)ONLY_DC_SHAPE;
        break;
    case 1:
        ctrls.pfShape = (std::uint8_t)DEFAULT_SHAPE;
        break;
    case 2:
        ctrls.pfShape = (std::uint8_t)N2_SHAPE;
        break;
    case 3:
        ctrls.pfShape = (std::uint8_t)N4_SHAPE;
        break;
    default:
        // The reference assert(0)s (:3494-3496) and writes nothing.
        break;
    }
}

// md_sq_motion_search_controls (enc_mode_config.c:3508-3628), 1:1.
void setMdSqMotionSearchCtrls(MdSqMotionSearchCtrls& ctrls, std::uint8_t mdSqMvSearchLevel) {
    switch (mdSqMvSearchLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled            = 1;
        ctrls.distType           = (std::uint8_t)SAD;
        ctrls.pameDistortionTh   = 10;
        ctrls.sprsLev0Enabled    = 1;
        ctrls.sprsLev0Step       = 4;
        ctrls.sprsLev0W          = 15;
        ctrls.sprsLev0H          = 15;
        ctrls.maxSprsLev0W       = 150;
        ctrls.maxSprsLev0H       = 150;
        ctrls.sprsLev0Multiplier = 500;
        ctrls.sprsLev1Enabled    = 1;
        ctrls.sprsLev1Step       = 2;
        ctrls.sprsLev1W          = 4;
        ctrls.sprsLev1H          = 4;
        ctrls.maxSprsLev1W       = 50;
        ctrls.maxSprsLev1H       = 50;
        ctrls.sprsLev1Multiplier = 500;
        ctrls.sprsLev2Enabled    = 1;
        ctrls.sprsLev2Step       = 1;
        ctrls.sprsLev2W          = 3;
        ctrls.sprsLev2H          = 3;
        ctrls.enablePsad         = 1;
        break;
    case 2:
        ctrls.enabled            = 1;
        ctrls.distType           = (std::uint8_t)SAD;
        ctrls.pameDistortionTh   = 10;
        ctrls.sprsLev0Enabled    = 1;
        ctrls.sprsLev0Step       = 4;
        ctrls.sprsLev0W          = 15;
        ctrls.sprsLev0H          = 15;
        ctrls.maxSprsLev0W       = 150;
        ctrls.maxSprsLev0H       = 150;
        ctrls.sprsLev0Multiplier = 400;
        ctrls.sprsLev1Enabled    = 1;
        ctrls.sprsLev1Step       = 2;
        ctrls.sprsLev1W          = 4;
        ctrls.sprsLev1H          = 4;
        ctrls.maxSprsLev1W       = 50;
        ctrls.maxSprsLev1H       = 50;
        ctrls.sprsLev1Multiplier = 400;
        ctrls.sprsLev2Enabled    = 1;
        ctrls.sprsLev2Step       = 1;
        ctrls.sprsLev2W          = 3;
        ctrls.sprsLev2H          = 3;
        ctrls.enablePsad         = 1;
        break;
    case 3:
        ctrls.enabled            = 1;
        ctrls.distType           = (std::uint8_t)SAD;
        ctrls.pameDistortionTh   = 10;
        ctrls.sprsLev0Enabled    = 1;
        ctrls.sprsLev0Step       = 4;
        ctrls.sprsLev0W          = 15;
        ctrls.sprsLev0H          = 15;
        ctrls.maxSprsLev0W       = 150;
        ctrls.maxSprsLev0H       = 150;
        ctrls.sprsLev0Multiplier = 300;
        ctrls.sprsLev1Enabled    = 1;
        ctrls.sprsLev1Step       = 2;
        ctrls.sprsLev1W          = 4;
        ctrls.sprsLev1H          = 4;
        ctrls.maxSprsLev1W       = 50;
        ctrls.maxSprsLev1H       = 50;
        ctrls.sprsLev1Multiplier = 300;
        ctrls.sprsLev2Enabled    = 1;
        ctrls.sprsLev2Step       = 1;
        ctrls.sprsLev2W          = 3;
        ctrls.sprsLev2H          = 3;
        ctrls.enablePsad         = 1;
        break;
    case 4:
        ctrls.enabled            = 1;
        ctrls.distType           = (std::uint8_t)SAD;
        ctrls.pameDistortionTh   = 10;
        ctrls.sprsLev0Enabled    = 1;
        ctrls.sprsLev0Step       = 4;
        ctrls.sprsLev0W          = 15;
        ctrls.sprsLev0H          = 15;
        ctrls.maxSprsLev0W       = 150;
        ctrls.maxSprsLev0H       = 150;
        ctrls.sprsLev0Multiplier = 100;
        ctrls.sprsLev1Enabled    = 1;
        ctrls.sprsLev1Step       = 2;
        ctrls.sprsLev1W          = 4;
        ctrls.sprsLev1H          = 4;
        ctrls.maxSprsLev1W       = 50;
        ctrls.maxSprsLev1H       = 50;
        ctrls.sprsLev1Multiplier = 100;
        ctrls.sprsLev2Enabled    = 1;
        ctrls.sprsLev2Step       = 1;
        ctrls.sprsLev2W          = 3;
        ctrls.sprsLev2H          = 3;
        ctrls.enablePsad         = 1;
        break;
    default:
        // The reference assert(0)s (:3624-3626) and writes nothing.
        break;
    }
}


// ---------------------------------------------------------------------------
// Batch C rung 3: the subpel/rdoq/shaving setters.
// ---------------------------------------------------------------------------

// md_subpel_me_controls (enc_mode_config.c:3633-3774), 1:1.
void setMdSubpelMeCtrls(MdSubPelSearchCtrls& ctrls, std::uint8_t mdSubpelMeLevel) {
    switch (mdSubpelMeLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_8_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)EIGHTH_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.skipDiagRefinement = 0;
        ctrls.minBlkSz           = 0;
        ctrls.mvpTh              = 0;
        ctrls.hpMvTh             = kMaxSignedValue;
        ctrls.biasFp             = 0;
        break;
    case 2:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_4_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)EIGHTH_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.skipDiagRefinement = 0;
        ctrls.minBlkSz           = 4;
        ctrls.mvpTh              = 18;
        ctrls.hpMvTh             = 32;
        ctrls.biasFp             = 0;
        break;
    case 3:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_4_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)EIGHTH_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.skipDiagRefinement = 0;
        ctrls.minBlkSz           = 4;
        ctrls.mvpTh              = 18;
        ctrls.hpMvTh             = 32;
        ctrls.biasFp             = 104;
        break;
    case 4:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_4_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)EIGHTH_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE_PRUNED;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.skipDiagRefinement = 0;
        ctrls.minBlkSz           = 4;
        ctrls.mvpTh              = 18;
        ctrls.hpMvTh             = 32;
        ctrls.biasFp             = 104;
        break;
    case 5:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_4_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)EIGHTH_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE_PRUNED;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.skipDiagRefinement = 0;
        ctrls.minBlkSz           = 4;
        ctrls.mvpTh              = 18;
        ctrls.hpMvTh             = 32;
        ctrls.biasFp             = 110;
        break;
    case 6:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_4_TAPS;
        ctrls.subpelItersPerStep = 1;
        ctrls.maxPrecision       = (std::uint8_t)QUARTER_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE_PRUNED;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.skipDiagRefinement = 3;
        ctrls.minBlkSz           = 4;
        ctrls.mvpTh              = 12;
        ctrls.hpMvTh             = 32;
        ctrls.biasFp             = 110;
        break;
    // Cases 7-10: SUBPEL_FIXED_STAGE_SEARCH path. Unused fields (not
    // applicable to md_subpel_search_fixed_stage): subpel_search_type,
    // subpel_iters_per_step, round_dev_th, skip_diag_refinement, mvp_th,
    // hp_mv_th.
    case 7:
        ctrls.enabled            = 1;
        ctrls.maxPrecision       = (std::uint8_t)QUARTER_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_FIXED_STAGE_SEARCH;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.minBlkSz           = 4;
        ctrls.biasFp             = 110;
        break;
    case 8:
        ctrls.enabled            = 1;
        ctrls.maxPrecision       = (std::uint8_t)QUARTER_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_FIXED_STAGE_SEARCH;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 10;
        ctrls.minBlkSz           = 4;
        ctrls.biasFp             = 110;
        break;
    case 9:
        ctrls.enabled            = 1;
        ctrls.maxPrecision       = (std::uint8_t)QUARTER_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_FIXED_STAGE_SEARCH;
        ctrls.predVarianceTh     = 100;
        ctrls.absThMult          = 20;
        ctrls.minBlkSz           = 4;
        ctrls.biasFp             = 110;
        break;
    case 10:
        ctrls.enabled            = 1;
        ctrls.maxPrecision       = (std::uint8_t)QUARTER_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_FIXED_STAGE_SEARCH;
        ctrls.predVarianceTh     = 100;
        ctrls.absThMult          = 25;
        ctrls.minBlkSz           = 4;
        ctrls.biasFp             = 110;
        break;
    default:
        // The reference assert(0)s (:3770-3772) and writes nothing.
        break;
    }
}

// md_subpel_pme_controls (enc_mode_config.c:3779-3846), 1:1.
void setMdSubpelPmeCtrls(MdSubPelSearchCtrls& ctrls, std::uint8_t mdSubpelPmeLevel) {
    switch (mdSubpelPmeLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_8_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)EIGHTH_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.minBlkSz           = 0;
        ctrls.mvpTh              = 0;
        ctrls.hpMvTh             = 0;
        ctrls.biasFp             = 0;
        break;
    case 2:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_8_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)EIGHTH_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE_PRUNED;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.minBlkSz           = 0;
        ctrls.mvpTh              = 0;
        ctrls.hpMvTh             = 0;
        ctrls.biasFp             = 104;
        break;
    case 3:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_8_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)EIGHTH_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE_PRUNED;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.minBlkSz           = 0;
        ctrls.mvpTh              = 0;
        ctrls.hpMvTh             = 0;
        ctrls.biasFp             = 110;
        break;
    case 4:
        ctrls.enabled            = 1;
        ctrls.subpelSearchType   = (std::uint8_t)USE_8_TAPS;
        ctrls.subpelItersPerStep = 2;
        ctrls.maxPrecision       = (std::uint8_t)HALF_PEL;
        ctrls.subpelSearchMethod = (std::uint8_t)SUBPEL_TREE_PRUNED;
        ctrls.predVarianceTh     = 0;
        ctrls.absThMult          = 0;
        ctrls.roundDevTh         = kMaxSignedValue;
        ctrls.minBlkSz           = 0;
        ctrls.mvpTh              = 0;
        ctrls.hpMvTh             = 0;
        ctrls.biasFp             = 110;
        break;
    default:
        // The reference assert(0)s (:3842-3844) and writes nothing.
        break;
    }
}

// set_rdoq_controls (enc_mode_config.c:3851-3906), 1:1.
void setRdoqCtrls(RdoqCtrls& ctrls, std::uint8_t rdoqLevel) {
    switch (rdoqLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled      = 1;
        ctrls.cutOffNum    = 0;
        ctrls.skipUv       = 0;
        ctrls.dctDctOnly   = 0;
        ctrls.eobTh        = (std::uint8_t)~0;
        ctrls.eobFastTh    = (std::uint8_t)~0;
        break;
    case 2:
        ctrls.enabled      = 1;
        ctrls.cutOffNum    = 80;
        ctrls.cutOffDenum  = 100;
        ctrls.skipUv       = 0;
        ctrls.dctDctOnly   = 0;
        ctrls.eobTh        = (std::uint8_t)~0;
        ctrls.eobFastTh    = (std::uint8_t)~0;
        break;
    case 3:
        ctrls.enabled      = 1;
        ctrls.cutOffNum    = 60;
        ctrls.cutOffDenum  = 100;
        ctrls.skipUv       = 1;
        ctrls.dctDctOnly   = 1;
        ctrls.eobTh        = (std::uint8_t)~0;
        ctrls.eobFastTh    = (std::uint8_t)~0;
        break;
    case 4:
        ctrls.enabled      = 1;
        ctrls.cutOffNum    = 60;
        ctrls.cutOffDenum  = 100;
        ctrls.skipUv       = 1;
        ctrls.dctDctOnly   = 1;
        ctrls.eobTh        = (std::uint8_t)~0;
        ctrls.eobFastTh    = 30;
        break;
    case 5:
        ctrls.enabled      = 1;
        ctrls.cutOffNum    = 60;
        ctrls.cutOffDenum  = 100;
        ctrls.skipUv       = 1;
        ctrls.dctDctOnly   = 1;
        ctrls.eobTh        = 85;
        ctrls.eobFastTh    = 0;
        break;
    default:
        // The reference assert(0)s (:3902-3904) and writes nothing.
        break;
    }
}

// set_coeff_shaving_controls (enc_mode_config.c:3911-3935), 1:1.
void setCoeffShavingCtrls(CoeffShavingCtrls& ctrls, std::uint8_t coeffShavingLevel) {
    switch (coeffShavingLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled            = 1;
        ctrls.levelThreshold     = 1;
        ctrls.zeroGapThreshold   = 8;
        ctrls.rdZeroStrength     = 32;
        break;
    case 2:
        ctrls.enabled            = 1;
        ctrls.levelThreshold     = 1;
        ctrls.zeroGapThreshold   = 8;
        ctrls.rdZeroStrength     = 0;
        break;
    default:
        // The reference assert(0)s (:3931-3933) and writes nothing.
        break;
    }
}


// ---------------------------------------------------------------------------
// Batch C rung 4: the txt + interpolation-search setters.
// ---------------------------------------------------------------------------

// svt_aom_set_txt_controls (enc_mode_config.c:3964-4137), 1:1.
void setTxtCtrls(TxtCtrls& ctrls, std::uint8_t txtLevel) {
    switch (txtLevel) {
    case 0:
        ctrls.enabled                  = 0;
        ctrls.txtGroupInterLt16x16     = 1;
        ctrls.txtGroupInterGtEq16x16   = 1;
        ctrls.txtGroupIntraLt16x16     = 1;
        ctrls.txtGroupIntraGtEq16x16   = 1;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 0;
        ctrls.satdEarlyExitThInter     = 0;
        ctrls.satdThQWeight            = 0;
        ctrls.txtRateCostTh            = 0;
        break;
    case 1:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupInterGtEq16x16   = kMaxTxTypeGroup;
        ctrls.txtGroupIntraLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupIntraGtEq16x16   = kMaxTxTypeGroup;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 0;
        ctrls.satdEarlyExitThInter     = 0;
        ctrls.satdThQWeight            = 0;
        ctrls.txtRateCostTh            = 0;
        break;
    case 2:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupInterGtEq16x16   = kMaxTxTypeGroup;
        ctrls.txtGroupIntraLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupIntraGtEq16x16   = kMaxTxTypeGroup;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 20;
        ctrls.satdEarlyExitThInter     = 20;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 250;
        break;
    case 3:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupInterGtEq16x16   = kMaxTxTypeGroup;
        ctrls.txtGroupIntraLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupIntraGtEq16x16   = kMaxTxTypeGroup;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 15;
        ctrls.satdEarlyExitThInter     = 15;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 250;
        break;
    case 4:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupInterGtEq16x16   = kMaxTxTypeGroup;
        ctrls.txtGroupIntraLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupIntraGtEq16x16   = kMaxTxTypeGroup;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 10;
        ctrls.satdEarlyExitThInter     = 10;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 250;
        break;
    case 5:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupInterGtEq16x16   = kMaxTxTypeGroup;
        ctrls.txtGroupIntraLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupIntraGtEq16x16   = kMaxTxTypeGroup;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 10;
        ctrls.satdEarlyExitThInter     = 5;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 100;
        break;
    case 6:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupInterGtEq16x16   = 5;
        ctrls.txtGroupIntraLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupIntraGtEq16x16   = kMaxTxTypeGroup;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 10;
        ctrls.satdEarlyExitThInter     = 5;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 100;
        break;
    case 7:
        // ref lvl_5
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = 5;
        ctrls.txtGroupInterGtEq16x16   = 5;
        ctrls.txtGroupIntraLt16x16     = kMaxTxTypeGroup;
        ctrls.txtGroupIntraGtEq16x16   = kMaxTxTypeGroup;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 10;
        ctrls.satdEarlyExitThInter     = 5;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 100;
        break;
    case 8:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = 4;
        ctrls.txtGroupInterGtEq16x16   = 3;
        ctrls.txtGroupIntraLt16x16     = 5;
        ctrls.txtGroupIntraGtEq16x16   = 4;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 10;
        ctrls.satdEarlyExitThInter     = 5;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 100;
        break;
    case 9:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = 3;
        ctrls.txtGroupInterGtEq16x16   = 2;
        ctrls.txtGroupIntraLt16x16     = 4;
        ctrls.txtGroupIntraGtEq16x16   = 3;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 10;
        ctrls.satdEarlyExitThInter     = 5;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 65;
        break;
    case 10:
        ctrls.enabled                  = 1;
        ctrls.txtGroupInterLt16x16     = 2;
        ctrls.txtGroupInterGtEq16x16   = 1;
        ctrls.txtGroupIntraLt16x16     = 3;
        ctrls.txtGroupIntraGtEq16x16   = 2;
        ctrls.earlyExitDistTh          = 0;
        ctrls.earlyExitCoeffTh         = 0;
        ctrls.satdEarlyExitThIntra     = 10;
        ctrls.satdEarlyExitThInter     = 5;
        ctrls.satdThQWeight            = 1;
        ctrls.txtRateCostTh            = 50;
        break;
    default:
        // The reference assert(0)s (:4133-4135) and writes nothing.
        break;
    }
}

// set_interpolation_search_level_ctrls (enc_mode_config.c:4139-4162), 1:1.
void setInterpolationSearchLevelCtrls(InterpolationSearchCtrls& ctrls,
                                      std::uint8_t interpolationSearchLevel) {
    switch (interpolationSearchLevel) {
    case 0:
        ctrls.level = (std::uint8_t)IFS_OFF;
        break;
    case 1:
        ctrls.level = (std::uint8_t)IFS_MDS0;
        break;
    case 2:
        ctrls.level = (std::uint8_t)IFS_MDS1;
        break;
    case 3:
        ctrls.level = (std::uint8_t)IFS_MDS2;
        break;
    case 4:
        ctrls.level = (std::uint8_t)IFS_MDS3;
        break;
    default:
        // The reference assert(0)s (:4158-4160) and writes nothing.
        break;
    }
}


// ---------------------------------------------------------------------------
// Batch C rung 5: set_cand_reduction_ctrls.
// ---------------------------------------------------------------------------

// set_cand_reduction_ctrls (enc_mode_config.c:4164-4394), 1:1.
void setCandReductionCtrls(CandReductionCtrls& ctrls, std::uint8_t candReductionLevel,
                           const CandReductionPcs& pcs, const CandReductionCtx& ctx,
                           std::uint32_t pictureQp, std::uint32_t me8x8CostVariance,
                           std::uint32_t me64x64Distortion, std::uint8_t l0WasSkip,
                           std::uint8_t l1WasSkip, std::uint8_t refSkipPerc) {
    const bool isLpd1 = ctx.pd1Level > REGULAR_PD1;
    const bool isNotLastLayer = ctx.isNotLastLayer;
    const bool useFlatIpp = ctx.rtc && ctx.hierarchicalLevels == 0;
    switch (candReductionLevel) {
    case 0:
        // Filter INTRA reduction
        ctrls.reduceFilterIntra = 0;
        // redundant_cand_level
        ctrls.redundantCandCtrls.scoreTh = 0;
        // use_neighbouring_mode
        ctrls.useNeighbouringModeCtrls.enabled = 0;
        // near_count_ctrls
        ctrls.nearCountCtrls.enabled         = 1;
        ctrls.nearCountCtrls.nearCount       = 3;
        ctrls.nearCountCtrls.nearNearCount   = 3;
        // lpd1_mvp_best_me_list (LPD1 only signal)
        ctrls.lpd1MvpBestMeList = 0;
        // cand_elimination_ctrls
        ctrls.candEliminationCtrls.enabled = 0;
        // reduce_unipred_candidates
        ctrls.reduceUnipredCandidates = 0;
        break;
    case 1:
        // Filter INTRA reduction
        ctrls.reduceFilterIntra = 1;
        // redundant_cand_level
        ctrls.redundantCandCtrls.scoreTh = 0;
        // use_neighbouring_mode
        ctrls.useNeighbouringModeCtrls.enabled = 0;
        // near_count_ctrls
        ctrls.nearCountCtrls.enabled         = 1;
        ctrls.nearCountCtrls.nearCount       = 3;
        ctrls.nearCountCtrls.nearNearCount   = 3;
        // lpd1_mvp_best_me_list (LPD1 only signal)
        ctrls.lpd1MvpBestMeList = 0;
        // cand_elimination_ctrls
        ctrls.candEliminationCtrls.enabled = 0;
        // reduce_unipred_candidates
        ctrls.reduceUnipredCandidates = 0;
        break;
    case 2:
        // Filter INTRA reduction
        ctrls.reduceFilterIntra = 1;
        // redundant_cand_level
        ctrls.redundantCandCtrls.scoreTh = 0;
        // use_neighbouring_mode
        ctrls.useNeighbouringModeCtrls.enabled = 1;
        // near_count_ctrls
        ctrls.nearCountCtrls.enabled         = 1;
        ctrls.nearCountCtrls.nearCount       = 3;
        ctrls.nearCountCtrls.nearNearCount   = 3;
        // lpd1_mvp_best_me_list (LPD1 only signal)
        ctrls.lpd1MvpBestMeList = 0;
        // cand_elimination_ctrls
        ctrls.candEliminationCtrls.enabled    = 1;
        ctrls.candEliminationCtrls.dcOnlyTh   = (std::uint16_t)(isLpd1 ? (useFlatIpp              ? 200
                                                         : isNotLastLayer ? 30
                                                                          : 200)
                                                         : (isNotLastLayer ? 10 : 200));
        ctrls.candEliminationCtrls.skipDcTh   = 0;
        // reduce_unipred_candidates
        ctrls.reduceUnipredCandidates = 0;
        break;
    case 3:
        // Filter INTRA reduction
        ctrls.reduceFilterIntra = 1;
        // redundant_cand_level
        ctrls.redundantCandCtrls.scoreTh = 0;
        // use_neighbouring_mode
        ctrls.useNeighbouringModeCtrls.enabled = 1;
        // near_count_ctrls
        ctrls.nearCountCtrls.enabled         = 1;
        ctrls.nearCountCtrls.nearCount       = 1;
        ctrls.nearCountCtrls.nearNearCount   = 3;
        // lpd1_mvp_best_me_list (LPD1 only signal)
        ctrls.lpd1MvpBestMeList = 0;
        // cand_elimination_ctrls
        ctrls.candEliminationCtrls.enabled    = 1;
        ctrls.candEliminationCtrls.dcOnlyTh   = (std::uint16_t)(isLpd1 ? (useFlatIpp              ? 200
                                                         : isNotLastLayer ? 30
                                                                          : 200)
                                                         : (isNotLastLayer ? 10 : 200));
        ctrls.candEliminationCtrls.skipDcTh   = 0;
        // reduce_unipred_candidates
        ctrls.reduceUnipredCandidates = 1;
        break;
    case 4:
        // Filter INTRA reduction
        ctrls.reduceFilterIntra = 1;
        // redundant_cand_level
        ctrls.redundantCandCtrls.scoreTh = 8;
        ctrls.redundantCandCtrls.magTh   = 64;
        // use_neighbouring_mode
        ctrls.useNeighbouringModeCtrls.enabled = 1;
        // near_count_ctrls
        ctrls.nearCountCtrls.enabled         = 1;
        ctrls.nearCountCtrls.nearCount       = 1;
        ctrls.nearCountCtrls.nearNearCount   = 1;
        // lpd1_mvp_best_me_list (LPD1 only signal)
        ctrls.lpd1MvpBestMeList = 0;
        // cand_elimination_ctrls
        ctrls.candEliminationCtrls.enabled    = 1;
        ctrls.candEliminationCtrls.dcOnlyTh   = (std::uint16_t)(isLpd1 ? (useFlatIpp              ? 600
                                                         : isNotLastLayer ? 30
                                                                          : 600)
                                                         : (isNotLastLayer ? 10 : 200));
        ctrls.candEliminationCtrls.skipDcTh   = 0;
        // reduce_unipred_candidates
        ctrls.reduceUnipredCandidates = 1;
        break;
    case 5:
        // Filter INTRA reduction
        ctrls.reduceFilterIntra = 1;
        // redundant_cand_level
        ctrls.redundantCandCtrls.scoreTh = 8;
        ctrls.redundantCandCtrls.magTh   = 64;
        // use_neighbouring_mode
        ctrls.useNeighbouringModeCtrls.enabled = 1;
        // near_count_ctrls
        ctrls.nearCountCtrls.enabled         = 1;
        ctrls.nearCountCtrls.nearCount       = 1;
        ctrls.nearCountCtrls.nearNearCount   = 1;
        // lpd1_mvp_best_me_list (LPD1 only signal)
        ctrls.lpd1MvpBestMeList = 1;
        // cand_elimination_ctrls
        ctrls.candEliminationCtrls.enabled    = 1;
        ctrls.candEliminationCtrls.dcOnlyTh   = (std::uint16_t)(isLpd1 ? (useFlatIpp              ? 800
                                                         : isNotLastLayer ? 30
                                                                          : 600)
                                                         : (isNotLastLayer ? 10 : 200));
        ctrls.candEliminationCtrls.skipDcTh   = (std::uint16_t)(isNotLastLayer ? 5 : 15);
        // reduce_unipred_candidates
        ctrls.reduceUnipredCandidates =
            (std::uint8_t)((!isNotLastLayer || ((l0WasSkip && l1WasSkip && refSkipPerc > 35) &&
                                 me8x8CostVariance < (500 * pictureQp) &&
                                 me64x64Distortion < (500 * pictureQp)))
                ? 2
                : 1);
        break;
    case 6:
        // Filter INTRA reduction
        ctrls.reduceFilterIntra = 1;
        // redundant_cand_level
        ctrls.redundantCandCtrls.scoreTh = 8;
        ctrls.redundantCandCtrls.magTh   = 64;
        // use_neighbouring_mode
        ctrls.useNeighbouringModeCtrls.enabled = 1;
        // near_count_ctrls
        ctrls.nearCountCtrls.enabled         = 1;
        ctrls.nearCountCtrls.nearCount       = 0;
        ctrls.nearCountCtrls.nearNearCount   = 1;
        // lpd1_mvp_best_me_list (LPD1 only signal)
        ctrls.lpd1MvpBestMeList = 1;
        // cand_elimination_ctrls
        ctrls.candEliminationCtrls.enabled    = 1;
        ctrls.candEliminationCtrls.dcOnlyTh   = (std::uint16_t)(isLpd1 ? (useFlatIpp              ? 800
                                                         : isNotLastLayer ? 30
                                                                          : 600)
                                                         : (isNotLastLayer ? 10 : 200));
        ctrls.candEliminationCtrls.skipDcTh   = (std::uint16_t)(isNotLastLayer ? 5 : 15);
        // reduce_unipred_candidates
        ctrls.reduceUnipredCandidates =
            (std::uint8_t)((!isNotLastLayer || ((l0WasSkip && l1WasSkip && refSkipPerc > 35) &&
                                 me8x8CostVariance < (500 * pictureQp) &&
                                 me64x64Distortion < (500 * pictureQp)))
                ? 2
                : 1);
        break;
    default:
        // The reference assert(0)s (:4384-4386) and writes nothing.
        break;
    }

    // lpd1_mvp_best_me_list can only use this feature when a single
    // unipred ME candidate is selected,
    if (!(pcs.refList0CountTry == 1 && pcs.refList1CountTry == 1 &&
          pcs.useBestMeUnipredCandOnly)) {
        ctrls.lpd1MvpBestMeList = 0;
    }
}

// ---------------------------------------------------------------------------
// EMC2 rung 1: the SB128 walls (enc_mode_config.c:63-:145), the
// :3937-:3963 gap (set_sq_txs_ctrls), set_chroma_controls (:4396-:4463),
// set_wm_controls (:4467-:4521), the nic level getters (:4521-:4586) and
// svt_aom_set_nic_controls (:4588-:4966).
// ---------------------------------------------------------------------------

// get_sb128_me_data (enc_mode_config.c:63-115).
void getSb128MeData(const Sb128Pcs& pcs, const Sb128Ctx& ctx, std::uint32_t* me64x64Dist,
                    std::uint32_t* me32x32Dist, std::uint32_t* me16x16Dist, std::uint32_t* me8x8Dist,
                    std::uint32_t* me8x8CostVar) {
    const std::uint32_t meSbSize          = pcs.b64Size;
    const std::uint32_t mePicWidthInSb    = (pcs.alignedWidth + meSbSize - 1) / meSbSize;
    const std::uint32_t mePicHeightInSb   = (pcs.alignedHeight + meSbSize - 1) / meSbSize;
    const std::uint32_t meSbX             = (ctx.sbOriginX / meSbSize);
    const std::uint32_t meSbY             = (ctx.sbOriginY / meSbSize);
    const std::uint32_t meSbAddr          = meSbX + meSbY * mePicWidthInSb;

    std::uint32_t dist64              = pcs.me64x64Distortion[meSbAddr];
    std::uint32_t dist32              = pcs.me32x32Distortion[meSbAddr];
    std::uint32_t dist16              = pcs.me16x16Distortion[meSbAddr];
    std::uint32_t dist8               = pcs.me8x8Distortion[meSbAddr];
    std::uint32_t me8x8CostVariance   = pcs.me8x8CostVariance[meSbAddr];
    int           count               = 1;
    if (meSbX + 1 < mePicWidthInSb) {
        dist64 += pcs.me64x64Distortion[meSbAddr + 1];
        dist32 += pcs.me32x32Distortion[meSbAddr + 1];
        dist16 += pcs.me16x16Distortion[meSbAddr + 1];
        dist8 += pcs.me8x8Distortion[meSbAddr + 1];
        me8x8CostVariance = std::max(me8x8CostVariance, pcs.me8x8CostVariance[meSbAddr + 1]);
        count++;
    }
    if (meSbY + 1 < mePicHeightInSb) {
        dist64 += pcs.me64x64Distortion[meSbAddr + mePicWidthInSb];
        dist32 += pcs.me32x32Distortion[meSbAddr + mePicWidthInSb];
        dist16 += pcs.me16x16Distortion[meSbAddr + mePicWidthInSb];
        dist8 += pcs.me8x8Distortion[meSbAddr + mePicWidthInSb];
        me8x8CostVariance = std::max(me8x8CostVariance,
                                     pcs.me8x8CostVariance[meSbAddr + mePicWidthInSb]);
        count++;
    }
    if (meSbX + 1 < mePicWidthInSb && meSbY + 1 < mePicHeightInSb) {
        dist64 += pcs.me64x64Distortion[meSbAddr + mePicWidthInSb + 1];
        dist32 += pcs.me32x32Distortion[meSbAddr + mePicWidthInSb + 1];
        dist16 += pcs.me16x16Distortion[meSbAddr + mePicWidthInSb + 1];
        dist8 += pcs.me8x8Distortion[meSbAddr + mePicWidthInSb + 1];
        me8x8CostVariance = std::max(me8x8CostVariance,
                                     pcs.me8x8CostVariance[meSbAddr + mePicWidthInSb + 1]);
        count++;
    }
    dist64 /= count;
    dist32 /= count;
    dist16 /= count;
    dist8 /= count;

    *me64x64Dist = dist64;
    *me32x32Dist = dist32;
    *me16x16Dist = dist16;
    *me8x8Dist   = dist8;
    *me8x8CostVar = me8x8CostVariance;
}

// get_sb128_variance (enc_mode_config.c:120-145).
void getSb128Variance(const Sb128VarPcs& pcs, const Sb128Ctx& ctx, std::uint16_t* variance) {
    const std::uint32_t b64Size       = pcs.b64Size;
    const std::uint32_t b64PicWidth   = (pcs.alignedWidth + b64Size - 1) / b64Size;
    const std::uint32_t b64PicHeight  = (pcs.alignedHeight + b64Size - 1) / b64Size;
    const std::uint32_t b64X          = (ctx.sbOriginX / b64Size);
    const std::uint32_t b64Y          = (ctx.sbOriginY / b64Size);
    const std::uint32_t b64Index      = b64X + b64Y * b64PicWidth;
    const std::uint32_t stride        = pcs.varianceStride;
    std::uint16_t var64 = pcs.variance64[b64Index * stride + kMeTierZeroPu64x64];
    std::uint8_t  count = 1;
    if (b64X + 1 < b64PicWidth) {
        var64 += pcs.variance64[(b64Index + 1) * stride + kMeTierZeroPu64x64];
        count++;
    }
    if (b64Y + 1 < b64PicHeight) {
        var64 += pcs.variance64[(b64Index + b64PicWidth) * stride + kMeTierZeroPu64x64];
        count++;
    }
    if (b64X + 1 < b64PicWidth && b64Y + 1 < b64PicHeight) {
        var64 += pcs.variance64[(b64Index + b64PicWidth + 1) * stride + kMeTierZeroPu64x64];
        count++;
    }
    var64 /= count;
    *variance = var64;
}

// set_sq_txs_ctrls (enc_mode_config.c:3937-3963).
void setSqTxsCtrls(NsqPsqTxsCtrls& ctrls, std::uint8_t psqTxsLvl) {
    switch (psqTxsLvl) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled   = 1;
        ctrls.hvToSqTh  = 1000;
        ctrls.hToVTh    = 100;
        break;
    case 2:
        ctrls.enabled   = 1;
        ctrls.hvToSqTh  = 250;
        ctrls.hToVTh    = 25;
        break;
    case 3:
        ctrls.enabled   = 1;
        ctrls.hvToSqTh  = 150;
        ctrls.hToVTh    = 15;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// svt_aom_set_chroma_controls (enc_mode_config.c:4396-4463).
std::uint8_t setChromaCtrls(UvCtrls& ctrls, std::uint8_t uvLevel) {
    std::uint8_t uvMode = 0;

    switch (uvLevel) {
    case 0:
        uvMode = kChromaMode2;
        ctrls.enabled = 0;
        break;
    case 1:
        uvMode = kChromaMode0;
        ctrls.enabled             = 1;
        ctrls.indUvLastMds        = 0;
        ctrls.interVsIntraCostTh  = 0;
        ctrls.skipIndUvIfOnlyDc   = 0;
        ctrls.uvNicScalingNum     = 16;
        break;
    case 2:
        uvMode = kChromaMode0;
        ctrls.enabled             = 1;
        ctrls.indUvLastMds        = 1;
        ctrls.interVsIntraCostTh  = 0;
        ctrls.skipIndUvIfOnlyDc   = 0;
        ctrls.uvNicScalingNum     = 8;
        break;
    case 3:
        uvMode = kChromaMode0;
        ctrls.enabled             = 1;
        ctrls.indUvLastMds        = 1;
        ctrls.interVsIntraCostTh  = 100;
        ctrls.skipIndUvIfOnlyDc   = 0;
        ctrls.uvNicScalingNum     = 1;
        break;
    case 4:
        uvMode = kChromaMode0;
        ctrls.enabled             = 1;
        ctrls.indUvLastMds        = 2;
        ctrls.interVsIntraCostTh  = 100;
        ctrls.skipIndUvIfOnlyDc   = 1;
        ctrls.uvNicScalingNum     = 1;
        break;
    case 5:
        uvMode = kChromaMode1;
        ctrls.enabled = 1;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }

    ctrls.uvMode = uvMode;
    return uvMode;
}

// svt_aom_set_wm_controls (enc_mode_config.c:4467-4521).
void setWmCtrls(WmCtrls& ctrls, std::uint8_t wmLevel) {
    switch (wmLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled               = 1;
        ctrls.useWmForMvp           = 1;
        ctrls.refinementIterations  = 16;
        ctrls.refineDiag            = 1;
        ctrls.refineLevel           = 0;
        ctrls.lowerBandTh           = 0;
        ctrls.upperBandTh           = (std::uint16_t)~0;
        ctrls.shutApproxIfNotMds0   = 0;
        break;
    case 2:
        ctrls.enabled               = 1;
        ctrls.useWmForMvp           = 1;
        ctrls.refinementIterations  = 8;
        ctrls.refineDiag            = 0;
        ctrls.refineLevel           = 1;
        ctrls.lowerBandTh           = 0;
        ctrls.upperBandTh           = (std::uint16_t)~0;
        ctrls.shutApproxIfNotMds0   = 1;
        break;
    case 3:
        ctrls.enabled               = 1;
        ctrls.useWmForMvp           = 1;
        ctrls.refinementIterations  = 8;
        ctrls.refineDiag            = 0;
        ctrls.refineLevel           = 1;
        ctrls.lowerBandTh           = 1 << 10;
        ctrls.upperBandTh           = (std::uint16_t)~0;
        ctrls.shutApproxIfNotMds0   = 1;
        break;
    case kMaxWarpLvl:
        ctrls.enabled               = 1;
        ctrls.useWmForMvp           = 0;
        ctrls.refinementIterations  = 0;
        ctrls.refineDiag            = 0;
        ctrls.refineLevel           = 1;
        ctrls.lowerBandTh           = 0;
        ctrls.upperBandTh           = (std::uint16_t)~0;
        ctrls.shutApproxIfNotMds0   = 0;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// svt_aom_get_nic_level_default (enc_mode_config.c:4521-4545).
std::uint8_t getNicLevelDefault(EncMode encMode, std::uint8_t isBase) {
    std::uint8_t nicLevel;
    if (encMode <= ENC_MR) {
        nicLevel = isBase ? 1 : 2;
    } else if (encMode <= ENC_M0) {
        nicLevel = isBase ? 2 : 4;
    } else if (encMode <= ENC_M1) {
        nicLevel = isBase ? 4 : 5;
    } else if (encMode <= ENC_M3) {
        nicLevel = isBase ? 5 : 6;
    } else if (encMode <= ENC_M5) {
        nicLevel = 7;
    } else if (encMode <= ENC_M6) {
        nicLevel = 8;
    } else if (encMode <= ENC_M7) {
        nicLevel = isBase ? 8 : 9;
    } else if (encMode <= ENC_M8) {
        nicLevel = 9;
    } else if (encMode <= ENC_M9) {
        nicLevel = isBase ? 9 : 11;
    } else {
        nicLevel = 11;
    }
    return nicLevel;
}

// svt_aom_get_nic_level_rtc (enc_mode_config.c:4547-4557).
std::uint8_t getNicLevelRtc(EncMode encMode) {
    std::uint8_t nicLevel;
    if (encMode <= ENC_M8) {
        nicLevel = 9;
    } else if (encMode <= ENC_M9) {
        nicLevel = 10;
    } else {
        nicLevel = 11;
    }
    return nicLevel;
}

// svt_aom_get_nic_level_allintra (enc_mode_config.c:4559-4586).
std::uint8_t getNicLevelAllIntra(EncMode encMode) {
    std::uint8_t nicLevel;
    if (encMode <= ENC_M0) {
        nicLevel = 1;
    } else if (encMode <= ENC_M2) {
        nicLevel = 3;
    } else if (encMode <= ENC_M4) {
        nicLevel = 5;
    } else if (encMode <= ENC_M6) {
        nicLevel = 6;
    } else if (encMode <= ENC_M7) {
        nicLevel = 7;
    } else {
        nicLevel = 11;
    }

    return nicLevel;
}

// MD_STAGE_NICS_SCAL_NUM (definitions.h:798-817, verbatim; the S0 column
// kept for positional indexing - the consumers read [..][1..3]).
const std::uint8_t kMdStageNicsScalNum[16][4] = {
    {0, 20, 20, 20},  // LEVEL 0
    {0, 18, 18, 18},  // LEVEL 1
    {0, 16, 16, 16},  // LEVEL 2
    {0, 12, 12, 12},  // LEVEL 3
    {0, 10, 10, 10},  // LEVEL 4
    {0, 8, 8, 8},     // LEVEL 5
    {0, 6, 6, 6},     // LEVEL 6
    {0, 4, 5, 5},     // LEVEL 7
    {0, 4, 4, 4},     // LEVEL 8
    {0, 3, 4, 4},     // LEVEL 9
    {0, 3, 3, 3},     // LEVEL 10
    {0, 3, 2, 2},     // LEVEL 11
    {0, 3, 1, 1},     // LEVEL 12
    {0, 2, 1, 1},     // LEVEL 13
    {0, 2, 0, 0},     // LEVEL 14
    {0, 0, 0, 0},     // LEVEL 15
};

// svt_aom_set_nic_controls (enc_mode_config.c:4588-4966).
std::uint8_t setNicCtrls(NicCtrls* ctrls, std::uint8_t nicLevel) {
    NicPruningCtrls* nicPruningCtrls = ctrls ? &ctrls->pruningCtrls : nullptr;
    std::uint8_t     nicScalingLevel = 0;
    std::uint8_t     mdStagingMode   = kMdStagingMode0;

    switch (nicLevel) {
    case 0:  // MAX NIC scaling; no pruning
        nicScalingLevel = 0;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh        = (std::uint64_t)~0;
            nicPruningCtrls->mds2ClassTh        = (std::uint64_t)~0;
            nicPruningCtrls->mds3ClassTh        = (std::uint64_t)~0;
            nicPruningCtrls->iMds3ClassThMult   = 0;
            nicPruningCtrls->enableSkippingMds1 = 0;

            nicPruningCtrls->mds1CandBaseThIntra  = (std::uint64_t)~0;
            nicPruningCtrls->mds1CandBaseThInter  = (std::uint64_t)~0;
            nicPruningCtrls->mds1CandThRankFactor = 0;
            nicPruningCtrls->mds2CandBaseTh       = (std::uint64_t)~0;
            nicPruningCtrls->mds2CandThRankFactor = 0;
            nicPruningCtrls->mds2RelativeDevTh    = 0;
            nicPruningCtrls->mds3CandBaseTh       = (std::uint64_t)~0;

            nicPruningCtrls->mergeInterCandsMult = (std::uint8_t)~0;
        }
        mdStagingMode = kMdStagingMode1;
        break;

    case 1:
        nicScalingLevel = 0;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = (std::uint64_t)~0;

            nicPruningCtrls->mds2ClassTh = 25;
            nicPruningCtrls->mds2BandCnt = 4;

            nicPruningCtrls->mds3ClassTh        = 25;
            nicPruningCtrls->mds3BandCnt        = 4;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 0;

            nicPruningCtrls->mds1CandBaseThIntra  = (std::uint64_t)~0;
            nicPruningCtrls->mds1CandBaseThInter  = (std::uint64_t)~0;
            nicPruningCtrls->mds1CandThRankFactor = 0;
            nicPruningCtrls->mds2CandBaseTh       = 50;
            nicPruningCtrls->mds2CandThRankFactor = 0;
            nicPruningCtrls->mds2RelativeDevTh    = 0;
            nicPruningCtrls->mds3CandBaseTh       = 50;

            nicPruningCtrls->mergeInterCandsMult = (std::uint8_t)~0;
        }
        mdStagingMode = kMdStagingMode1;
        break;

    case 2:
        nicScalingLevel = 1;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = (std::uint64_t)~0;

            nicPruningCtrls->mds2ClassTh = 25;
            nicPruningCtrls->mds2BandCnt = 4;

            nicPruningCtrls->mds3ClassTh        = 25;
            nicPruningCtrls->mds3BandCnt        = 8;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 0;

            nicPruningCtrls->mds1CandBaseThIntra  = 1200;
            nicPruningCtrls->mds1CandBaseThInter  = 500;
            nicPruningCtrls->mds1CandThRankFactor = 0;
            nicPruningCtrls->mds2CandBaseTh       = 30;
            nicPruningCtrls->mds2CandThRankFactor = 0;
            nicPruningCtrls->mds2RelativeDevTh    = 0;
            nicPruningCtrls->mds3CandBaseTh       = 30;

            nicPruningCtrls->mergeInterCandsMult = (std::uint8_t)~0;
        }
        mdStagingMode = kMdStagingMode1;
        break;

    case 3:
        nicScalingLevel = 3;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = (std::uint64_t)~0;

            nicPruningCtrls->mds2ClassTh = 25;
            nicPruningCtrls->mds2BandCnt = 4;

            nicPruningCtrls->mds3ClassTh        = 25;
            nicPruningCtrls->mds3BandCnt        = 8;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 0;

            nicPruningCtrls->mds1CandBaseThIntra  = 1200;
            nicPruningCtrls->mds1CandBaseThInter  = 500;
            nicPruningCtrls->mds1CandThRankFactor = 0;
            nicPruningCtrls->mds2CandBaseTh       = 30;
            nicPruningCtrls->mds2CandThRankFactor = 0;
            nicPruningCtrls->mds2RelativeDevTh    = 0;
            nicPruningCtrls->mds3CandBaseTh       = 25;

            nicPruningCtrls->mergeInterCandsMult = (std::uint8_t)~0;
        }
        mdStagingMode = kMdStagingMode1;
        break;
    case 4:
        nicScalingLevel = 3;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = 500;
            nicPruningCtrls->mds1BandCnt = 3;

            nicPruningCtrls->mds2ClassTh = 25;
            nicPruningCtrls->mds2BandCnt = 8;

            nicPruningCtrls->mds3ClassTh        = 20;
            nicPruningCtrls->mds3BandCnt        = 12;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 0;

            nicPruningCtrls->mds1CandBaseThIntra  = 1200;
            nicPruningCtrls->mds1CandBaseThInter  = 300;
            nicPruningCtrls->mds1CandThRankFactor = 0;
            nicPruningCtrls->mds2CandBaseTh       = 20;
            nicPruningCtrls->mds2CandThRankFactor = 0;
            nicPruningCtrls->mds2RelativeDevTh    = 0;
            nicPruningCtrls->mds3CandBaseTh       = 15;

            nicPruningCtrls->mergeInterCandsMult = (std::uint8_t)~0;
        }
        mdStagingMode = kMdStagingMode1;
        break;
    case 5:
        nicScalingLevel = 6;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = 300;
            nicPruningCtrls->mds1BandCnt = 4;

            nicPruningCtrls->mds2ClassTh = 25;
            nicPruningCtrls->mds2BandCnt = 10;

            nicPruningCtrls->mds3ClassTh        = 15;
            nicPruningCtrls->mds3BandCnt        = 16;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 0;

            nicPruningCtrls->mds1CandBaseThIntra  = 1200;
            nicPruningCtrls->mds1CandBaseThInter  = 300;
            nicPruningCtrls->mds1CandThRankFactor = 0;
            nicPruningCtrls->mds2CandBaseTh       = 20;
            nicPruningCtrls->mds2CandThRankFactor = 0;
            nicPruningCtrls->mds2RelativeDevTh    = 0;
            nicPruningCtrls->mds3CandBaseTh       = 15;

            nicPruningCtrls->mergeInterCandsMult = (std::uint8_t)~0;
        }
        mdStagingMode = kMdStagingMode1;
        break;
    case 6:
        nicScalingLevel = 6;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = 200;
            nicPruningCtrls->mds1BandCnt = 16;

            nicPruningCtrls->mds2ClassTh = 10;
            nicPruningCtrls->mds2BandCnt = 10;

            nicPruningCtrls->mds3ClassTh        = 5;
            nicPruningCtrls->mds3BandCnt        = 16;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 0;

            nicPruningCtrls->mds1CandBaseThIntra  = 1200;
            nicPruningCtrls->mds1CandBaseThInter  = 300;
            nicPruningCtrls->mds1CandThRankFactor = 3;
            nicPruningCtrls->mds2CandBaseTh       = 15;
            nicPruningCtrls->mds2CandThRankFactor = 1;
            nicPruningCtrls->mds2RelativeDevTh    = 5;
            nicPruningCtrls->mds3CandBaseTh       = 15;

            nicPruningCtrls->mergeInterCandsMult = 4;
        }
        mdStagingMode = kMdStagingMode1;
        break;
    case 7:
        nicScalingLevel = 8;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = 200;
            nicPruningCtrls->mds1BandCnt = 16;

            nicPruningCtrls->mds2ClassTh = 10;
            nicPruningCtrls->mds2BandCnt = 10;

            nicPruningCtrls->mds3ClassTh        = 5;
            nicPruningCtrls->mds3BandCnt        = 16;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 0;

            nicPruningCtrls->mds1CandBaseThIntra  = 1200;
            nicPruningCtrls->mds1CandBaseThInter  = 300;
            nicPruningCtrls->mds1CandThRankFactor = 3;
            nicPruningCtrls->mds2CandBaseTh       = 15;
            nicPruningCtrls->mds2CandThRankFactor = 1;
            nicPruningCtrls->mds2RelativeDevTh    = 5;
            nicPruningCtrls->mds3CandBaseTh       = 15;

            nicPruningCtrls->mergeInterCandsMult = 4;
        }
        mdStagingMode = kMdStagingMode1;
        break;
    case 8:
        nicScalingLevel = 13;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = 200;
            nicPruningCtrls->mds1BandCnt = 16;

            nicPruningCtrls->mds2ClassTh = 10;
            nicPruningCtrls->mds2BandCnt = 10;

            nicPruningCtrls->mds3ClassTh        = 5;
            nicPruningCtrls->mds3BandCnt        = 16;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 1;

            nicPruningCtrls->mds1CandBaseThIntra  = 300;
            nicPruningCtrls->mds1CandBaseThInter  = 300;
            nicPruningCtrls->mds1CandThRankFactor = 3;
            nicPruningCtrls->mds2CandBaseTh       = 3;
            nicPruningCtrls->mds2CandThRankFactor = 1;
            nicPruningCtrls->mds2RelativeDevTh    = 5;
            nicPruningCtrls->mds3CandBaseTh       = 3;

            nicPruningCtrls->mergeInterCandsMult = 4;
        }
        mdStagingMode = kMdStagingMode1;
        break;

    case 9:
        nicScalingLevel = 14;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = 200;
            nicPruningCtrls->mds1BandCnt = 16;

            nicPruningCtrls->mds2ClassTh = 10;
            nicPruningCtrls->mds2BandCnt = 10;

            nicPruningCtrls->mds3ClassTh        = 5;
            nicPruningCtrls->mds3BandCnt        = 16;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 1;

            nicPruningCtrls->mds1CandBaseThIntra  = 100;
            nicPruningCtrls->mds1CandBaseThInter  = 100;
            nicPruningCtrls->mds1CandThRankFactor = 3;
            nicPruningCtrls->mds2CandBaseTh       = 1;
            nicPruningCtrls->mds2CandThRankFactor = 1;
            nicPruningCtrls->mds2RelativeDevTh    = 5;
            nicPruningCtrls->mds3CandBaseTh       = 1;

            nicPruningCtrls->mergeInterCandsMult = 4;
        }
        mdStagingMode = kMdStagingMode1;
        break;

    case 10:
        nicScalingLevel = 15;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = 150;
            nicPruningCtrls->mds1BandCnt = 16;

            nicPruningCtrls->mds2ClassTh = 5;
            nicPruningCtrls->mds2BandCnt = 10;

            nicPruningCtrls->mds3ClassTh        = 5;
            nicPruningCtrls->mds3BandCnt        = 16;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 1;

            nicPruningCtrls->mds1CandBaseThIntra  = 1;
            nicPruningCtrls->mds1CandBaseThInter  = 1;
            nicPruningCtrls->mds1CandThRankFactor = 3;
            nicPruningCtrls->mds2CandBaseTh       = 1;
            nicPruningCtrls->mds2CandThRankFactor = 1;
            nicPruningCtrls->mds2RelativeDevTh    = 5;
            nicPruningCtrls->mds3CandBaseTh       = 1;

            nicPruningCtrls->mergeInterCandsMult = 4;
        }
        mdStagingMode = kMdStagingMode1;
        break;

    case 11:
        nicScalingLevel = 15;

        if (nicPruningCtrls) {
            nicPruningCtrls->mds1ClassTh = 75;
            nicPruningCtrls->mds1BandCnt = 16;

            nicPruningCtrls->mds2ClassTh = 0;
            nicPruningCtrls->mds2BandCnt = 10;

            nicPruningCtrls->mds3ClassTh        = 0;
            nicPruningCtrls->mds3BandCnt        = 16;
            nicPruningCtrls->iMds3ClassThMult   = 50;
            nicPruningCtrls->enableSkippingMds1 = 1;

            nicPruningCtrls->mds1CandBaseThIntra  = 1;
            nicPruningCtrls->mds1CandBaseThInter  = 1;
            nicPruningCtrls->mds1CandThRankFactor = 3;
            nicPruningCtrls->mds2CandBaseTh       = 1;
            nicPruningCtrls->mds2CandThRankFactor = 1;
            nicPruningCtrls->mds2RelativeDevTh    = 5;
            nicPruningCtrls->mds3CandBaseTh       = 1;

            nicPruningCtrls->mergeInterCandsMult = 4;
        }
        mdStagingMode = kMdStagingMode1;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }

    if (ctrls) {
        ctrls->scalingCtrls.stage1ScalingNum = kMdStageNicsScalNum[nicScalingLevel][kMdStage1];
        ctrls->scalingCtrls.stage2ScalingNum = kMdStageNicsScalNum[nicScalingLevel][kMdStage2];
        ctrls->scalingCtrls.stage3ScalingNum = kMdStageNicsScalNum[nicScalingLevel][kMdStage3];
        ctrls->mdStagingMode                 = mdStagingMode;
    }
    // return NIC scaling level that can be used for memory allocation
    return nicScalingLevel;
}

// svt_aom_set_nsq_geom_ctrls (enc_mode_config.c:4968-5022).
void setNsqGeomCtrls(NsqGeomCtrls& ctrls, std::uint8_t nsqGeomLevel, std::uint8_t* allowHvaHvb,
                     std::uint8_t* allowHv4, std::uint8_t* minNsqBsize) {
    NsqGeomCtrls local = {};  // the reference's {0} local
    switch (nsqGeomLevel) {
    case 0:
        local.enabled        = 0;
        local.minNsqBlockSize = 0;
        local.allowHv4       = 0;
        local.allowHvaHvb    = 0;
        break;

    case 1:
        local.enabled        = 1;
        local.minNsqBlockSize = 0;
        local.allowHv4       = 1;
        local.allowHvaHvb    = 1;
        break;

    case 2:
        local.enabled        = 1;
        local.minNsqBlockSize = 0;
        local.allowHv4       = 1;
        local.allowHvaHvb    = 0;
        break;
    case 3:
        local.enabled        = 1;
        local.minNsqBlockSize = 8;
        local.allowHv4       = 0;
        local.allowHvaHvb    = 0;
        break;
    case 4:
        local.enabled        = 1;
        local.minNsqBlockSize = 16;
        local.allowHv4       = 0;
        local.allowHvaHvb    = 0;
        break;
    default:
        // The reference assert(0)s here and falls through to the writes
        // with the zero-init local (documented).
        break;
    }

    if (allowHvaHvb) {
        *allowHvaHvb = local.allowHvaHvb;
    }
    if (allowHv4) {
        *allowHv4 = local.allowHv4;
    }
    if (minNsqBsize) {
        *minNsqBsize = local.minNsqBlockSize;
    }
    ctrls = local;  // the memcpy
}

// svt_aom_set_nsq_search_ctrls (enc_mode_config.c:5024-5375).
void setNsqSearchCtrls(NsqSearchCtrls& ctrls, NsqPsqTxsCtrls& psqTxsCtrls, const NsqSearchPcs& pcs,
                       std::uint8_t nsqSearchLevel) {
    bool meDistMod;
    // Whether or not to modulate the nsq_search_level using me-distortion
    if (pcs.isISlice) {
        meDistMod = false;
    } else {
        if (pcs.encMode <= ENC_MR) {
            meDistMod = false;
        } else {
            meDistMod = true;
        }
    }
    if (pcs.mimicOnlyTx4x4) {
        nsqSearchLevel = 0;
    } else if (meDistMod && nsqSearchLevel) {
        std::uint32_t dist8, me8x8CostVariance;
        if (pcs.sbSize == 64) {
            dist8               = pcs.me8x8Distortion[pcs.sbIndex];
            me8x8CostVariance   = pcs.me8x8CostVariance[pcs.sbIndex];
        } else {
            std::uint32_t dist64, dist32, dist16;
            Sb128Ctx sb128Ctx;
            sb128Ctx.sbOriginX = pcs.sbOriginX;
            sb128Ctx.sbOriginY = pcs.sbOriginY;
            getSb128MeData(pcs.sb128, sb128Ctx, &dist64, &dist32, &dist16, &dist8,
                           &me8x8CostVariance);
        }

        const int errorPerSample = 3;
        if (dist8 <= (pcs.sbSize * pcs.sbSize * errorPerSample) && me8x8CostVariance <= 10000) {
            nsqSearchLevel = (std::uint8_t)std::min((int)nsqSearchLevel + 1, 19);
        }
    }

    switch (nsqSearchLevel) {
    case 0:
        ctrls.enabled                = 0;
        ctrls.sqWeight               = (std::uint32_t)~0;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 0;
        ctrls.lowerDepthSplitCostTh  = 0;
        ctrls.hVsVSplitRateTh        = 0;
        ctrls.nonHvSplitRateTh       = 0;
        ctrls.rateThOffsetLte16      = 0;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 0;
        ctrls.componentMultipleTh    = 0;
        ctrls.hvWeight               = (std::uint32_t)~0;
        break;

    case 1:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 105;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 0;
        ctrls.lowerDepthSplitCostTh  = 0;
        ctrls.hVsVSplitRateTh        = 0;
        ctrls.nonHvSplitRateTh       = 0;
        ctrls.rateThOffsetLte16      = 0;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 0;
        ctrls.componentMultipleTh    = 0;
        ctrls.hvWeight               = 115;
        break;

    case 2:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 105;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 150;
        ctrls.lowerDepthSplitCostTh  = 3;
        ctrls.hVsVSplitRateTh        = 0;
        ctrls.nonHvSplitRateTh       = 0;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 0;
        ctrls.componentMultipleTh    = 0;
        ctrls.hvWeight               = 115;
        break;

    case 3:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 105;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 100;
        ctrls.lowerDepthSplitCostTh  = 3;
        ctrls.hVsVSplitRateTh        = 0;
        ctrls.nonHvSplitRateTh       = 0;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 0;
        ctrls.hvWeight               = 115;
        break;

    case 4:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 100;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 100;
        ctrls.lowerDepthSplitCostTh  = 3;
        ctrls.hVsVSplitRateTh        = 0;
        ctrls.nonHvSplitRateTh       = 0;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 80;
        ctrls.hvWeight               = 115;
        break;

    case 5:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 100;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 100;
        ctrls.lowerDepthSplitCostTh  = 5;
        ctrls.hVsVSplitRateTh        = 0;
        ctrls.nonHvSplitRateTh       = 0;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 80;
        ctrls.hvWeight               = 110;
        break;
    case 6:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 100;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 100;
        ctrls.lowerDepthSplitCostTh  = 5;
        ctrls.hVsVSplitRateTh        = 0;
        ctrls.nonHvSplitRateTh       = 0;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 80;
        ctrls.hvWeight               = 100;
        break;

    case 7:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 95;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 80;
        ctrls.lowerDepthSplitCostTh  = 5;
        ctrls.hVsVSplitRateTh        = 0;
        ctrls.nonHvSplitRateTh       = 0;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 80;
        ctrls.hvWeight               = 100;
        break;
    case 8:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 95;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 80;
        ctrls.lowerDepthSplitCostTh  = 5;
        ctrls.hVsVSplitRateTh        = 30;
        ctrls.nonHvSplitRateTh       = 20;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 80;
        ctrls.hvWeight               = 100;
        break;
    case 9:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 95;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 80;
        ctrls.lowerDepthSplitCostTh  = 5;
        ctrls.hVsVSplitRateTh        = 40;
        ctrls.nonHvSplitRateTh       = 30;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 60;
        ctrls.hvWeight               = 100;
        break;
    case 10:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 95;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 60;
        ctrls.lowerDepthSplitCostTh  = 10;
        ctrls.hVsVSplitRateTh        = 40;
        ctrls.nonHvSplitRateTh       = 30;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 60;
        ctrls.hvWeight               = 100;
        break;
    case 11:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 95;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 60;
        ctrls.lowerDepthSplitCostTh  = 10;
        ctrls.hVsVSplitRateTh        = 50;
        ctrls.nonHvSplitRateTh       = 30;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 40;
        ctrls.hvWeight               = 100;
        break;
    case 12:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 95;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 60;
        ctrls.lowerDepthSplitCostTh  = 10;
        ctrls.hVsVSplitRateTh        = 50;
        ctrls.nonHvSplitRateTh       = 30;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 20;
        ctrls.hvWeight               = 100;
        break;
    case 13:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 95;
        ctrls.maxPart0ToPart1Dev     = 0;
        ctrls.nsqSplitCostTh         = 60;
        ctrls.lowerDepthSplitCostTh  = 10;
        ctrls.hVsVSplitRateTh        = 60;
        ctrls.nonHvSplitRateTh       = 40;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 20;
        ctrls.hvWeight               = 100;
        break;
    case 14:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 95;
        ctrls.maxPart0ToPart1Dev     = 5;
        ctrls.nsqSplitCostTh         = 50;
        ctrls.lowerDepthSplitCostTh  = 10;
        ctrls.hVsVSplitRateTh        = 60;
        ctrls.nonHvSplitRateTh       = 40;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 20;
        ctrls.hvWeight               = 100;
        break;
    case 15:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 90;
        ctrls.maxPart0ToPart1Dev     = 20;
        ctrls.nsqSplitCostTh         = 40;
        ctrls.lowerDepthSplitCostTh  = 20;
        ctrls.hVsVSplitRateTh        = 60;
        ctrls.nonHvSplitRateTh       = 50;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 15;
        ctrls.hvWeight               = 75;
        break;
    case 16:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 90;
        ctrls.maxPart0ToPart1Dev     = 50;
        ctrls.nsqSplitCostTh         = 40;
        ctrls.lowerDepthSplitCostTh  = 20;
        ctrls.hVsVSplitRateTh        = 70;
        ctrls.nonHvSplitRateTh       = 60;
        ctrls.rateThOffsetLte16      = 10;
        ctrls.psqTxsLvl              = 0;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 15;
        ctrls.hvWeight               = 75;
        break;
    case 17:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 90;
        ctrls.maxPart0ToPart1Dev     = 50;
        ctrls.nsqSplitCostTh         = 40;
        ctrls.lowerDepthSplitCostTh  = 20;
        ctrls.hVsVSplitRateTh        = 70;
        ctrls.nonHvSplitRateTh       = 60;
        ctrls.rateThOffsetLte16      = 15;
        ctrls.psqTxsLvl              = 1;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 10;
        ctrls.hvWeight               = 75;
        break;
    case 18:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 90;
        ctrls.maxPart0ToPart1Dev     = 75;
        ctrls.nsqSplitCostTh         = 40;
        ctrls.lowerDepthSplitCostTh  = 20;
        ctrls.hVsVSplitRateTh        = 80;
        ctrls.nonHvSplitRateTh       = 70;
        ctrls.rateThOffsetLte16      = 15;
        ctrls.psqTxsLvl              = 1;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 5;
        ctrls.hvWeight               = 75;
        break;
    case 19:
        ctrls.enabled                = 1;
        ctrls.sqWeight               = 90;
        ctrls.maxPart0ToPart1Dev     = 80;
        ctrls.nsqSplitCostTh         = 35;
        ctrls.lowerDepthSplitCostTh  = 20;
        ctrls.hVsVSplitRateTh        = 85;
        ctrls.nonHvSplitRateTh       = 70;
        ctrls.rateThOffsetLte16      = 15;
        ctrls.psqTxsLvl              = 1;
        ctrls.subDepthBlockLvl       = 1;
        ctrls.componentMultipleTh    = 5;
        ctrls.hvWeight               = 75;
        break;
    default:
        // The reference assert(0)s here and writes nothing; the QP tail
        // below still runs on the untouched fields (documented).
        break;
    }
    std::uint32_t qWeight, qWeightDenom;
    getQpBasedThScalingFactors(pcs.nsqQpBasedThScaling, &qWeight, &qWeightDenom, pcs.qp);
    ctrls.componentMultipleTh = (std::uint32_t)(((std::uint64_t)ctrls.componentMultipleTh * qWeight +
                                                 (qWeightDenom >> 1)) /
                                                qWeightDenom);
    ctrls.nsqSplitCostTh      = (std::uint32_t)(((std::uint64_t)ctrls.nsqSplitCostTh * qWeight +
                                            (qWeightDenom >> 1)) /
                                           qWeightDenom);
    int maxPart0ToPart1DevOffset = 5;
    maxPart0ToPart1DevOffset =
        (int)(((std::uint64_t)maxPart0ToPart1DevOffset * qWeight + (qWeightDenom >> 1)) /
              qWeightDenom);
    ctrls.maxPart0ToPart1Dev =
        (std::uint32_t)std::max((int)((int)ctrls.maxPart0ToPart1Dev - maxPart0ToPart1DevOffset), 0);
    if (pcs.pdPass == kPdPass0) {
        ctrls.sqWeight                = 90;
        ctrls.maxPart0ToPart1Dev      = 0;
        ctrls.nsqSplitCostTh          = 60;
        ctrls.lowerDepthSplitCostTh   = 10;
        ctrls.hVsVSplitRateTh         = 60;
        ctrls.nonHvSplitRateTh        = 60;
        ctrls.rateThOffsetLte16       = 10;
        ctrls.psqTxsLvl               = 0;
        ctrls.subDepthBlockLvl        = 0;
        ctrls.componentMultipleTh     = 10;
        ctrls.hvWeight                = 90;
    }

    setSqTxsCtrls(psqTxsCtrls, ctrls.psqTxsLvl);
}

// svt_aom_get_intra_mode_levels_default (enc_mode_config.c:5377-5404).
void getIntraModeLevelsDefault(EncMode encMode, bool isISlice, bool isBase, int transitionPresent,
                               std::uint32_t* intraLevel, std::uint32_t* distBasedAngIntraLevel) {
    std::uint32_t intraLevelL;
    std::uint32_t distBasedAngIntraLevelL;

    if (encMode <= ENC_MR) {
        intraLevelL                = isBase ? 1 : 2;
        distBasedAngIntraLevelL    = 0;
    } else if (encMode <= ENC_M2) {
        intraLevelL                = isBase ? 1 : 2;
        distBasedAngIntraLevelL    = isBase ? 0 : 1;
    } else if (encMode <= ENC_M5) {
        intraLevelL                = isBase ? 1 : 6;
        distBasedAngIntraLevelL    = isISlice ? 0 : 2;
    } else if (encMode <= ENC_M7) {
        intraLevelL                = isBase ? 2 : 6;
        distBasedAngIntraLevelL    = isISlice ? 0 : 2;
    } else if (encMode <= ENC_M11) {
        intraLevelL                = (isISlice || transitionPresent == 1) ? 4 : 6;
        distBasedAngIntraLevelL    = isISlice ? 0 : 2;
    } else {
        intraLevelL                = kMaxIntraLevel - 1;
        distBasedAngIntraLevelL    = 0;
    }

    *intraLevel                = intraLevelL;
    *distBasedAngIntraLevel    = distBasedAngIntraLevelL;
}

// svt_aom_get_intra_mode_levels_rtc (enc_mode_config.c:5406-5427).
void getIntraModeLevelsRtc(EncMode encMode, bool isISlice, int transitionPresent, bool useFlatIpp,
                           std::uint32_t* intraLevel, std::uint32_t* distBasedAngIntraLevel) {
    std::uint32_t intraLevelL;
    std::uint32_t distBasedAngIntraLevelL;

    if ((!useFlatIpp && encMode <= ENC_M7) || (useFlatIpp && encMode <= ENC_M9)) {
        intraLevelL                = (isISlice || transitionPresent == 1) ? 1 : 6;
        distBasedAngIntraLevelL    = 1;
    } else if (encMode <= ENC_M8) {
        intraLevelL                = (isISlice || transitionPresent == 1) ? 4 : 6;
        distBasedAngIntraLevelL    = 1;
    } else if (encMode <= ENC_M10) {
        intraLevelL                = (isISlice || transitionPresent == 1) ? 4 : 6;
        distBasedAngIntraLevelL    = 2;
    } else {
        intraLevelL                = kMaxIntraLevel - 1;
        distBasedAngIntraLevelL    = 0;
    }

    *intraLevel                = intraLevelL;
    *distBasedAngIntraLevel    = distBasedAngIntraLevelL;
}

// set_inter_intra_ctrls (enc_mode_config.c:5455-5481).
void setInterIntraCtrls(InterIntraCompCtrls& ctrls, std::uint8_t interIntraLevel) {
    switch (interIntraLevel) {
    case 0:
        ctrls.enabled        = 0;
        ctrls.useRdModel     = 0;
        ctrls.wedgeModeSq    = 0;
        ctrls.wedgeModeNsq   = 0;
        break;
    case 1:
        ctrls.enabled        = 1;
        ctrls.useRdModel     = 1;
        ctrls.wedgeModeSq    = 1;
        ctrls.wedgeModeNsq   = 1;
        break;
    case 2:
        ctrls.enabled        = 1;
        ctrls.useRdModel     = 0;
        ctrls.wedgeModeSq    = 0;
        ctrls.wedgeModeNsq   = 2;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_pd0_ctrls (enc_mode_config.c:5483-5601).
void setPd0Ctrls(Pd0Ctrls& ctrls, std::uint8_t lpd0Lvl, std::uint8_t hbdMd) {
    if (hbdMd) {  // SVT_EFFECTIVE_HBD_MD is the identity in the pinned build
        ctrls.pd0Level = 0;  // PD0_LVL_0
        return;
    }
    switch (lpd0Lvl) {
    case 0:
        ctrls.pd0Level        = 0;  // PD0_LVL_0
        ctrls.usePd0Detector[0] = 0;
        break;
    case 1:
        ctrls.pd0Level        = 1;  // PD0_LVL_1
        ctrls.usePd0Detector[0] = 0;
        ctrls.usePd0Detector[1] = 0;
        break;
    case 2:
        ctrls.pd0Level        = 2;  // PD0_LVL_2
        ctrls.usePd0Detector[0] = 0;
        ctrls.usePd0Detector[1] = 0;
        ctrls.usePd0Detector[2] = 0;
        break;
    case 3:
        ctrls.pd0Level        = 3;  // PD0_LVL_3
        ctrls.usePd0Detector[0] = 0;
        ctrls.usePd0Detector[1] = 0;
        ctrls.usePd0Detector[2] = 0;
        ctrls.usePd0Detector[3] = 0;
        break;
    case 4:
        ctrls.pd0Level            = 4;  // PD0_LVL_4
        ctrls.usePd0Detector[0]   = 0;
        ctrls.usePd0Detector[1]   = 0;
        ctrls.usePd0Detector[2]   = 0;
        ctrls.usePd0Detector[3]   = 0;
        ctrls.usePd0Detector[4]   = 1;
        ctrls.useRefInfo[4]       = 2;
        ctrls.me8x8CostVarianceTh[4] = 250000;
        ctrls.edgeDistTh[4]       = 16384;
        ctrls.neighMeDistShift[4] = 3;
        break;
    case 5:
        ctrls.pd0Level            = 5;  // PD0_LVL_5
        ctrls.usePd0Detector[0]   = 0;
        ctrls.usePd0Detector[1]   = 0;
        ctrls.usePd0Detector[2]   = 0;
        ctrls.usePd0Detector[3]   = 0;
        ctrls.usePd0Detector[4]   = 1;
        ctrls.useRefInfo[4]       = 2;
        ctrls.me8x8CostVarianceTh[4] = 250000;
        ctrls.edgeDistTh[4]       = 16384;
        ctrls.neighMeDistShift[4] = 3;

        ctrls.usePd0Detector[5]   = 1;
        ctrls.useRefInfo[5]       = 1;
        ctrls.me8x8CostVarianceTh[5] = 250000 >> 1;
        ctrls.edgeDistTh[5]       = 16384;
        ctrls.neighMeDistShift[5] = 2;
        break;
    case 6:
        ctrls.pd0Level            = 5;  // PD0_LVL_5
        ctrls.usePd0Detector[0]   = 0;
        ctrls.usePd0Detector[1]   = 0;
        ctrls.usePd0Detector[2]   = 0;
        ctrls.usePd0Detector[3]   = 0;
        ctrls.usePd0Detector[4]   = 0;
        ctrls.usePd0Detector[5]   = 1;
        ctrls.useRefInfo[5]       = 0;
        ctrls.me8x8CostVarianceTh[5] = 500000;
        ctrls.edgeDistTh[5]       = 16384;
        ctrls.neighMeDistShift[5] = 2;
        break;
    case 7:
        ctrls.pd0Level = 6;  // PD0_LVL_6

        ctrls.usePd0Detector[0]   = 0;
        ctrls.usePd0Detector[1]   = 0;
        ctrls.usePd0Detector[2]   = 0;
        ctrls.usePd0Detector[3]   = 0;
        ctrls.usePd0Detector[4]   = 0;
        ctrls.usePd0Detector[5]   = 1;
        ctrls.useRefInfo[5]       = 0;
        ctrls.me8x8CostVarianceTh[5] = 500000 << 1;
        ctrls.edgeDistTh[5]       = (std::uint32_t)~0;
        ctrls.neighMeDistShift[5] = (std::uint16_t)~0;

        // Set VERY_LIGHT_PD0 controls
        ctrls.usePd0Detector[6]   = 1;
        ctrls.useRefInfo[6]       = 1;
        ctrls.me8x8CostVarianceTh[6] = 250000;
        ctrls.edgeDistTh[6]       = 16384;
        ctrls.neighMeDistShift[6] = 2;
        break;
    case 8:
        ctrls.pd0Level = 6;  // PD0_LVL_6

        ctrls.usePd0Detector[0]   = 0;
        ctrls.usePd0Detector[1]   = 0;
        ctrls.usePd0Detector[2]   = 0;
        ctrls.usePd0Detector[3]   = 0;
        ctrls.usePd0Detector[4]   = 0;
        ctrls.usePd0Detector[5]   = 1;
        ctrls.useRefInfo[5]       = 0;
        ctrls.me8x8CostVarianceTh[5] = 500000 << 1;
        ctrls.edgeDistTh[5]       = (std::uint32_t)~0;
        ctrls.neighMeDistShift[5] = (std::uint16_t)~0;

        // Set VERY_LIGHT_PD0 controls
        ctrls.usePd0Detector[6]   = 1;
        ctrls.useRefInfo[6]       = 2;
        ctrls.me8x8CostVarianceTh[6] = 250000;
        ctrls.edgeDistTh[6]       = 16384;
        ctrls.neighMeDistShift[6] = 2;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_lpd1_ctrls (enc_mode_config.c:5603-5937).
void setLpd1Ctrls(Lpd1Ctrls& ctrls, std::uint8_t lpd1Lvl) {
    switch (lpd1Lvl) {
    case 0:
        ctrls.pd1Level = (std::uint8_t)REGULAR_PD1;  // Light-PD1 path not used
        break;
    case 1:
        ctrls.pd1Level = LPD1_LVL_0;

        // Set LPD1 level 0 controls
        ctrls.useLpd1Detector[0]       = 1;
        ctrls.useRefInfo[0]            = 1;
        ctrls.costThDist[0]            = 25;
        ctrls.costThRate[0]            = 6000 + 40 * 500;
        ctrls.nzCoeffTh[0]             = 0;
        ctrls.maxMvLength[0]           = 300;
        ctrls.me8x8CostVarianceTh[0]   = 250000 >> 3;
        ctrls.skipPd0EdgeDistTh[0]     = 1024;
        ctrls.skipPd0MeShift[0]        = 1;
        break;
    case 2:
        ctrls.pd1Level = LPD1_LVL_0;

        // Set LPD1 level 0 controls
        ctrls.useLpd1Detector[0]       = 1;
        ctrls.useRefInfo[0]            = 1;
        ctrls.costThDist[0]            = 25;
        ctrls.costThRate[0]            = 6000 + 40 * 500;
        ctrls.nzCoeffTh[0]             = 16;
        ctrls.maxMvLength[0]           = 300;
        ctrls.me8x8CostVarianceTh[0]   = 250000 >> 3;
        ctrls.skipPd0EdgeDistTh[0]     = 1024;
        ctrls.skipPd0MeShift[0]        = 1;
        break;
    case 3:
        ctrls.pd1Level = LPD1_LVL_0;

        // Set LPD1 level 0 controls
        ctrls.useLpd1Detector[0]       = 1;
        ctrls.useRefInfo[0]            = 0;
        ctrls.costThDist[0]            = 35;
        ctrls.costThRate[0]            = 6000 + 100 * 500;
        ctrls.nzCoeffTh[0]             = 16;
        ctrls.maxMvLength[0]           = 900;
        ctrls.me8x8CostVarianceTh[0]   = 750000 >> 3;
        ctrls.skipPd0EdgeDistTh[0]     = 16384;
        ctrls.skipPd0MeShift[0]        = 3;
        break;
    case 4:
        ctrls.pd1Level = LPD1_LVL_1;

        // Set LPD1 level 0 controls
        ctrls.useLpd1Detector[0]       = 1;
        ctrls.useRefInfo[0]            = 0;
        ctrls.costThDist[0]            = 256 << 4;
        ctrls.costThRate[0]            = 6000 + 512 * 500;
        ctrls.nzCoeffTh[0]             = 32;
        ctrls.maxMvLength[0]           = 2048;
        ctrls.me8x8CostVarianceTh[0]   = 500000;
        ctrls.skipPd0EdgeDistTh[0]     = 16384;
        ctrls.skipPd0MeShift[0]        = 3;

        // Set LPD1 level 1 controls
        ctrls.useLpd1Detector[1]       = 1;
        ctrls.useRefInfo[1]            = 1;
        ctrls.costThDist[1]            = 256 << 1;
        ctrls.costThRate[1]            = 6000 + 125 * 500;
        ctrls.nzCoeffTh[1]             = 32;
        ctrls.maxMvLength[1]           = 1600;
        ctrls.me8x8CostVarianceTh[1]   = 500000 >> 3;
        ctrls.skipPd0EdgeDistTh[1]     = 16384;
        ctrls.skipPd0MeShift[1]        = 3;
        break;
    case 5:
        ctrls.pd1Level = LPD1_LVL_3;

        // Set LPD1 level 0 controls
        ctrls.useLpd1Detector[0]       = 1;
        ctrls.useRefInfo[0]            = 0;
        ctrls.costThDist[0]            = 256 << 10;
        ctrls.costThRate[0]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[0]             = 512;
        ctrls.maxMvLength[0]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[0]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[0]     = 16384 * 7;
        ctrls.skipPd0MeShift[0]        = 5;

        // Set LPD1 level 1 controls
        ctrls.useLpd1Detector[1]       = 1;
        ctrls.useRefInfo[1]            = 0;
        ctrls.costThDist[1]            = 256 << 8;
        ctrls.costThRate[1]            = 6000 + 4096 * 500;
        ctrls.nzCoeffTh[1]             = 256;
        ctrls.maxMvLength[1]           = 2048 * 8;
        ctrls.me8x8CostVarianceTh[1]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[1]     = 16384 * 6;
        ctrls.skipPd0MeShift[1]        = 5;

        // Set LPD1 level 2 controls
        ctrls.useLpd1Detector[2]       = 1;
        ctrls.useRefInfo[2]            = 0;
        ctrls.costThDist[2]            = 256 << 8;
        ctrls.costThRate[2]            = 6000 + 4096 * 500;
        ctrls.nzCoeffTh[2]             = 164;
        ctrls.maxMvLength[2]           = 2048 * 8;
        ctrls.me8x8CostVarianceTh[2]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[2]     = 16384 * 6;
        ctrls.skipPd0MeShift[2]        = 5;

        // Set LPD1 level 3 controls
        ctrls.useLpd1Detector[3]       = 1;
        ctrls.useRefInfo[3]            = 1;
        ctrls.costThDist[3]            = 256 << 8;
        ctrls.costThRate[3]            = 6000 + 4096 * 500;
        ctrls.nzCoeffTh[3]             = 128;
        ctrls.maxMvLength[3]           = 2048 * 8;
        ctrls.me8x8CostVarianceTh[3]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[3]     = 16384 * 6;
        ctrls.skipPd0MeShift[3]        = 5;
        break;
    case 6:
        ctrls.pd1Level = LPD1_LVL_4;

        // Set LPD1 level 0 controls
        ctrls.useLpd1Detector[0]       = 1;
        ctrls.useRefInfo[0]            = 0;
        ctrls.costThDist[0]            = 256 << 10;
        ctrls.costThRate[0]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[0]             = 512;
        ctrls.maxMvLength[0]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[0]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[0]     = 16384 * 7;
        ctrls.skipPd0MeShift[0]        = 5;

        // Set LPD1 level 1 controls
        ctrls.useLpd1Detector[1]       = 1;
        ctrls.useRefInfo[1]            = 0;
        ctrls.costThDist[1]            = 256 << 10;
        ctrls.costThRate[1]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[1]             = 512;
        ctrls.maxMvLength[1]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[1]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[1]     = 16384 * 7;
        ctrls.skipPd0MeShift[1]        = 5;

        // Set LPD1 level 2 controls
        ctrls.useLpd1Detector[2]       = 1;
        ctrls.useRefInfo[2]            = 0;
        ctrls.costThDist[2]            = 256 << 10;
        ctrls.costThRate[2]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[2]             = 256;
        ctrls.maxMvLength[2]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[2]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[2]     = 16384 * 7;
        ctrls.skipPd0MeShift[2]        = 5;

        // Set LPD1 level 3 controls
        ctrls.useLpd1Detector[3]       = 1;
        ctrls.useRefInfo[3]            = 0;
        ctrls.costThDist[3]            = 256 << 10;
        ctrls.costThRate[3]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[3]             = 164;
        ctrls.maxMvLength[3]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[3]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[3]     = 16384 * 7;
        ctrls.skipPd0MeShift[3]        = 5;

        // Set LPD1 level 4 controls
        ctrls.useLpd1Detector[4]       = 1;
        ctrls.useRefInfo[4]            = 1;
        ctrls.costThDist[4]            = 256 << 4;
        ctrls.costThRate[4]            = 6000 + 1024 * 500;
        ctrls.nzCoeffTh[4]             = 128;
        ctrls.maxMvLength[4]           = 2048;
        ctrls.me8x8CostVarianceTh[4]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[4]     = 16384 * 2;
        ctrls.skipPd0MeShift[4]        = 3;
        break;
    case 7:
        ctrls.pd1Level = LPD1_LVL_5;

        // Set LPD1 level 0 controls
        ctrls.useLpd1Detector[0]       = 1;
        ctrls.useRefInfo[0]            = 0;
        ctrls.costThDist[0]            = 256 << 10;
        ctrls.costThRate[0]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[0]             = 256;
        ctrls.maxMvLength[0]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[0]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[0]     = (std::uint32_t)~0;
        ctrls.skipPd0MeShift[0]        = (std::uint16_t)~0;

        // Set LPD1 level 1 controls
        ctrls.useLpd1Detector[1]       = 1;
        ctrls.useRefInfo[1]            = 0;
        ctrls.costThDist[1]            = 256 << 10;
        ctrls.costThRate[1]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[1]             = 128;
        ctrls.maxMvLength[1]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[1]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[1]     = (std::uint32_t)~0;
        ctrls.skipPd0MeShift[1]        = (std::uint16_t)~0;

        // Set LPD1 level 2 controls
        ctrls.useLpd1Detector[2]       = 1;
        ctrls.useRefInfo[2]            = 0;
        ctrls.costThDist[2]            = 256 << 10;
        ctrls.costThRate[2]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[2]             = 96;
        ctrls.maxMvLength[2]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[2]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[2]     = (std::uint32_t)~0;
        ctrls.skipPd0MeShift[2]        = (std::uint16_t)~0;

        // Set LPD1 level 3 controls
        ctrls.useLpd1Detector[3]       = 1;
        ctrls.useRefInfo[3]            = 0;
        ctrls.costThDist[3]            = 256 << 10;
        ctrls.costThRate[3]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[3]             = 96;
        ctrls.maxMvLength[3]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[3]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[3]     = (std::uint32_t)~0;
        ctrls.skipPd0MeShift[3]        = (std::uint16_t)~0;

        // Set LPD1 level 4 controls
        ctrls.useLpd1Detector[4]       = 1;
        ctrls.useRefInfo[4]            = 1;
        ctrls.costThDist[4]            = 256 << 8;
        ctrls.costThRate[4]            = 6000 + 4096 * 500;
        ctrls.nzCoeffTh[4]             = 64;
        ctrls.maxMvLength[4]           = 2048 * 8;
        ctrls.me8x8CostVarianceTh[4]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[4]     = 16384 * 6;
        ctrls.skipPd0MeShift[4]        = 5;

        // Set LPD1 level 5 controls
        ctrls.useLpd1Detector[5]       = 1;
        ctrls.useRefInfo[5]            = 1;
        ctrls.costThDist[5]            = 256 << 8;
        ctrls.costThRate[5]            = 6000 + 4096 * 500;
        ctrls.nzCoeffTh[5]             = 64;
        ctrls.maxMvLength[5]           = 2048 * 8;
        ctrls.me8x8CostVarianceTh[5]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[5]     = 16384 * 6;
        ctrls.skipPd0MeShift[5]        = 5;
        break;
    case 8:
        ctrls.pd1Level = LPD1_LVL_6;

        // Set LPD1 level 0 controls
        ctrls.useLpd1Detector[0]       = 1;
        ctrls.useRefInfo[0]            = 0;
        ctrls.costThDist[0]            = 256 << 10;
        ctrls.costThRate[0]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[0]             = 256;
        ctrls.maxMvLength[0]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[0]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[0]     = (std::uint32_t)~0;
        ctrls.skipPd0MeShift[0]        = (std::uint16_t)~0;

        // Set LPD1 level 1 controls
        ctrls.useLpd1Detector[1]       = 1;
        ctrls.useRefInfo[1]            = 0;
        ctrls.costThDist[1]            = 256 << 10;
        ctrls.costThRate[1]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[1]             = 128;
        ctrls.maxMvLength[1]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[1]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[1]     = (std::uint32_t)~0;
        ctrls.skipPd0MeShift[1]        = (std::uint16_t)~0;

        // Set LPD1 level 2 controls
        ctrls.useLpd1Detector[2]       = 1;
        ctrls.useRefInfo[2]            = 0;
        ctrls.costThDist[2]            = 256 << 10;
        ctrls.costThRate[2]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[2]             = 96;
        ctrls.maxMvLength[2]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[2]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[2]     = (std::uint32_t)~0;
        ctrls.skipPd0MeShift[2]        = (std::uint16_t)~0;

        // Set LPD1 level 3 controls
        ctrls.useLpd1Detector[3]       = 1;
        ctrls.useRefInfo[3]            = 0;
        ctrls.costThDist[3]            = 256 << 10;
        ctrls.costThRate[3]            = 6000 + 8192 * 500;
        ctrls.nzCoeffTh[3]             = 96;
        ctrls.maxMvLength[3]           = 2048 * 16;
        ctrls.me8x8CostVarianceTh[3]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[3]     = (std::uint32_t)~0;
        ctrls.skipPd0MeShift[3]        = (std::uint16_t)~0;

        // Set LPD1 level 4 controls
        ctrls.useLpd1Detector[4]       = 1;
        ctrls.useRefInfo[4]            = 1;
        ctrls.costThDist[4]            = 256 << 8;
        ctrls.costThRate[4]            = 6000 + 4096 * 500;
        ctrls.nzCoeffTh[4]             = 64;
        ctrls.maxMvLength[4]           = 2048 * 8;
        ctrls.me8x8CostVarianceTh[4]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[4]     = 16384 * 6;
        ctrls.skipPd0MeShift[4]        = 5;

        // Set LPD1 level 5 controls
        ctrls.useLpd1Detector[5]       = 1;
        ctrls.useRefInfo[5]            = 1;
        ctrls.costThDist[5]            = 256 << 8;
        ctrls.costThRate[5]            = 6000 + 4096 * 500;
        ctrls.nzCoeffTh[5]             = 64;
        ctrls.maxMvLength[5]           = 2048 * 8;
        ctrls.me8x8CostVarianceTh[5]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[5]     = 16384 * 6;
        ctrls.skipPd0MeShift[5]        = 5;

        // Set LPD1 level 6 controls
        ctrls.useLpd1Detector[6]       = 1;
        ctrls.useRefInfo[6]            = 1;
        ctrls.costThDist[6]            = 256 << 8;
        ctrls.costThRate[6]            = 6000 + 4096 * 500;
        ctrls.nzCoeffTh[6]             = 64;
        ctrls.maxMvLength[6]           = 2048 * 8;
        ctrls.me8x8CostVarianceTh[6]   = (std::uint32_t)~0;
        ctrls.skipPd0EdgeDistTh[6]     = 16384 * 6;
        ctrls.skipPd0MeShift[6]        = 5;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// svt_aom_set_bipred3x3_controls (enc_mode_config.c:5939-5974).
void setBipred3x3Ctrls(Bipred3x3Ctrls& ctrls, std::uint8_t bipred3x3Injection) {
    switch (bipred3x3Injection) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled       = 1;
        ctrls.searchDiag    = 1;
        ctrls.useBestList   = 0;
        ctrls.useL0L1Dev    = (std::uint8_t)~0;
        break;
    case 2:
        ctrls.enabled       = 1;
        ctrls.searchDiag    = 0;
        ctrls.useBestList   = 0;
        ctrls.useL0L1Dev    = (std::uint8_t)~0;
        break;
    case 3:
        ctrls.enabled       = 1;
        ctrls.searchDiag    = 0;
        ctrls.useBestList   = 1;
        ctrls.useL0L1Dev    = (std::uint8_t)~0;
        break;
    case 4:
        ctrls.enabled       = 1;
        ctrls.searchDiag    = 0;
        ctrls.useBestList   = 1;
        ctrls.useL0L1Dev    = 20;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// svt_aom_set_dist_based_ref_pruning_controls (enc_mode_config.c:5976-6231).
void setDistBasedRefPruningCtrls(RefPruningCtrls& ctrls, std::uint8_t distBasedRefPruningLevel) {
    switch (distBasedRefPruningLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled = 1;

        ctrls.maxDevToBest[kPaMeGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kUni3x3Group]       = (std::uint32_t)~0;
        ctrls.maxDevToBest[kBi3x3Group]        = (std::uint32_t)~0;
        ctrls.maxDevToBest[kNrstNewNearGroup]  = (std::uint32_t)~0;
        ctrls.maxDevToBest[kNrstNearGroup]     = (std::uint32_t)~0;
        ctrls.maxDevToBest[kPredMeGroup]       = (std::uint32_t)~0;
        ctrls.maxDevToBest[kGlobalGroup]       = (std::uint32_t)~0;
        ctrls.maxDevToBest[kWarpGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kObmcGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kInterIntraGroup]   = (std::uint32_t)~0;
        ctrls.maxDevToBest[kInterCompGroup]    = (std::uint32_t)~0;

        ctrls.useTplInfoOffset      = 0;
        ctrls.checkClosestMultiplier = 0;

        ctrls.closestRefs[kPaMeGroup]         = 1;
        ctrls.closestRefs[kUni3x3Group]       = 1;
        ctrls.closestRefs[kBi3x3Group]        = 1;
        ctrls.closestRefs[kNrstNewNearGroup]  = 1;
        ctrls.closestRefs[kNrstNearGroup]     = 1;
        ctrls.closestRefs[kPredMeGroup]       = 1;
        ctrls.closestRefs[kGlobalGroup]       = 1;
        ctrls.closestRefs[kWarpGroup]         = 1;
        ctrls.closestRefs[kObmcGroup]         = 1;
        ctrls.closestRefs[kInterIntraGroup]   = 1;
        ctrls.closestRefs[kInterCompGroup]    = 1;

        break;

    case 2:
        ctrls.enabled = 1;

        ctrls.maxDevToBest[kPaMeGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kUni3x3Group]       = (std::uint32_t)~0;
        ctrls.maxDevToBest[kBi3x3Group]        = (std::uint32_t)~0;
        ctrls.maxDevToBest[kNrstNewNearGroup]  = (std::uint32_t)~0;
        ctrls.maxDevToBest[kNrstNearGroup]     = (std::uint32_t)~0;
        ctrls.maxDevToBest[kPredMeGroup]       = 150;
        ctrls.maxDevToBest[kGlobalGroup]       = (std::uint32_t)~0;
        ctrls.maxDevToBest[kWarpGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kObmcGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kInterIntraGroup]   = (std::uint32_t)~0;
        ctrls.maxDevToBest[kInterCompGroup]    = (std::uint32_t)~0;

        ctrls.useTplInfoOffset      = 0;
        ctrls.checkClosestMultiplier = 0;

        ctrls.closestRefs[kPaMeGroup]         = 1;
        ctrls.closestRefs[kUni3x3Group]       = 1;
        ctrls.closestRefs[kBi3x3Group]        = 1;
        ctrls.closestRefs[kNrstNewNearGroup]  = 1;
        ctrls.closestRefs[kNrstNearGroup]     = 1;
        ctrls.closestRefs[kPredMeGroup]       = 1;
        ctrls.closestRefs[kGlobalGroup]       = 1;
        ctrls.closestRefs[kWarpGroup]         = 1;
        ctrls.closestRefs[kObmcGroup]         = 1;
        ctrls.closestRefs[kInterIntraGroup]   = 1;
        ctrls.closestRefs[kInterCompGroup]    = 1;

        break;
    case 3:
        ctrls.enabled = 1;

        ctrls.maxDevToBest[kPaMeGroup]         = 30;
        ctrls.maxDevToBest[kUni3x3Group]       = 30;
        ctrls.maxDevToBest[kBi3x3Group]        = 30;
        ctrls.maxDevToBest[kNrstNewNearGroup]  = (std::uint32_t)~0;
        ctrls.maxDevToBest[kNrstNearGroup]     = (std::uint32_t)~0;
        ctrls.maxDevToBest[kPredMeGroup]       = 60;
        ctrls.maxDevToBest[kGlobalGroup]       = (std::uint32_t)~0;
        ctrls.maxDevToBest[kWarpGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kObmcGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kInterIntraGroup]   = 30;
        ctrls.maxDevToBest[kInterCompGroup]    = 30;

        ctrls.useTplInfoOffset      = 0;
        ctrls.checkClosestMultiplier = 0;

        ctrls.closestRefs[kPaMeGroup]         = 1;
        ctrls.closestRefs[kUni3x3Group]       = 1;
        ctrls.closestRefs[kBi3x3Group]        = 1;
        ctrls.closestRefs[kNrstNewNearGroup]  = 1;
        ctrls.closestRefs[kNrstNearGroup]     = 1;
        ctrls.closestRefs[kPredMeGroup]       = 1;
        ctrls.closestRefs[kGlobalGroup]       = 1;
        ctrls.closestRefs[kWarpGroup]         = 1;
        ctrls.closestRefs[kObmcGroup]         = 1;
        ctrls.closestRefs[kInterIntraGroup]   = 1;
        ctrls.closestRefs[kInterCompGroup]    = 1;

        break;
    case 4:
        ctrls.enabled = 1;

        ctrls.maxDevToBest[kPaMeGroup]         = 30;
        ctrls.maxDevToBest[kUni3x3Group]       = 30;
        ctrls.maxDevToBest[kBi3x3Group]        = 30;
        ctrls.maxDevToBest[kNrstNewNearGroup]  = 30;
        ctrls.maxDevToBest[kNrstNearGroup]     = 30;
        ctrls.maxDevToBest[kPredMeGroup]       = 30;
        ctrls.maxDevToBest[kGlobalGroup]       = (std::uint32_t)~0;
        ctrls.maxDevToBest[kWarpGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kObmcGroup]         = (std::uint32_t)~0;
        ctrls.maxDevToBest[kInterIntraGroup]   = 30;
        ctrls.maxDevToBest[kInterCompGroup]    = 30;

        ctrls.useTplInfoOffset      = 20;
        ctrls.checkClosestMultiplier = 1;

        ctrls.closestRefs[kPaMeGroup]         = 1;
        ctrls.closestRefs[kUni3x3Group]       = 1;
        ctrls.closestRefs[kBi3x3Group]        = 1;
        ctrls.closestRefs[kNrstNewNearGroup]  = 1;
        ctrls.closestRefs[kNrstNearGroup]     = 1;
        ctrls.closestRefs[kPredMeGroup]       = 1;
        ctrls.closestRefs[kGlobalGroup]       = 1;
        ctrls.closestRefs[kWarpGroup]         = 1;
        ctrls.closestRefs[kObmcGroup]         = 1;
        ctrls.closestRefs[kInterIntraGroup]   = 1;
        ctrls.closestRefs[kInterCompGroup]    = 1;

        break;
    case 5:
        ctrls.enabled                             = 1;
        ctrls.maxDevToBest[kPaMeGroup]            = 30;
        ctrls.maxDevToBest[kUni3x3Group]          = 0;
        ctrls.maxDevToBest[kBi3x3Group]           = 30;
        ctrls.maxDevToBest[kNrstNewNearGroup]     = 30;
        ctrls.maxDevToBest[kNrstNearGroup]        = 30;
        ctrls.maxDevToBest[kPredMeGroup]          = 30;
        ctrls.maxDevToBest[kGlobalGroup]          = (std::uint32_t)~0;
        ctrls.maxDevToBest[kWarpGroup]            = (std::uint32_t)~0;
        ctrls.maxDevToBest[kObmcGroup]            = (std::uint32_t)~0;
        ctrls.maxDevToBest[kInterIntraGroup]      = 30;
        ctrls.maxDevToBest[kInterCompGroup]       = 0;

        ctrls.useTplInfoOffset      = 20;
        ctrls.checkClosestMultiplier = 1;

        ctrls.closestRefs[kPaMeGroup]         = 1;
        ctrls.closestRefs[kUni3x3Group]       = 1;
        ctrls.closestRefs[kBi3x3Group]        = 1;
        ctrls.closestRefs[kNrstNewNearGroup]  = 1;
        ctrls.closestRefs[kNrstNearGroup]     = 1;
        ctrls.closestRefs[kPredMeGroup]       = 1;
        ctrls.closestRefs[kGlobalGroup]       = 1;
        ctrls.closestRefs[kWarpGroup]         = 1;
        ctrls.closestRefs[kObmcGroup]         = 1;
        ctrls.closestRefs[kInterIntraGroup]   = 1;
        ctrls.closestRefs[kInterCompGroup]    = 1;

        break;
    case 6:
        ctrls.enabled                             = 1;
        ctrls.maxDevToBest[kPaMeGroup]            = 30;
        ctrls.maxDevToBest[kUni3x3Group]          = 0;
        ctrls.maxDevToBest[kBi3x3Group]           = 30;
        ctrls.maxDevToBest[kNrstNewNearGroup]     = 30;
        ctrls.maxDevToBest[kNrstNearGroup]        = 30;
        ctrls.maxDevToBest[kPredMeGroup]          = 30;
        ctrls.maxDevToBest[kGlobalGroup]          = (std::uint32_t)~0;
        ctrls.maxDevToBest[kWarpGroup]            = (std::uint32_t)~0;
        ctrls.maxDevToBest[kObmcGroup]            = (std::uint32_t)~0;
        ctrls.maxDevToBest[kInterIntraGroup]      = 30;
        ctrls.maxDevToBest[kInterCompGroup]       = 0;

        ctrls.useTplInfoOffset      = 20;
        ctrls.checkClosestMultiplier = 1;

        ctrls.closestRefs[kPaMeGroup]         = 1;
        ctrls.closestRefs[kUni3x3Group]       = 1;
        ctrls.closestRefs[kBi3x3Group]        = 1;
        ctrls.closestRefs[kNrstNewNearGroup]  = 1;
        ctrls.closestRefs[kNrstNearGroup]     = 1;
        ctrls.closestRefs[kPredMeGroup]       = 1;
        ctrls.closestRefs[kGlobalGroup]       = 1;
        ctrls.closestRefs[kWarpGroup]         = 1;
        ctrls.closestRefs[kObmcGroup]         = 1;
        ctrls.closestRefs[kInterIntraGroup]   = 1;
        ctrls.closestRefs[kInterCompGroup]    = 1;
        break;
    case 7:
        ctrls.enabled                             = 1;
        ctrls.maxDevToBest[kPaMeGroup]            = 10;
        ctrls.maxDevToBest[kUni3x3Group]          = 0;
        ctrls.maxDevToBest[kBi3x3Group]           = 10;
        ctrls.maxDevToBest[kNrstNewNearGroup]     = 10;
        ctrls.maxDevToBest[kNrstNearGroup]        = 10;
        ctrls.maxDevToBest[kPredMeGroup]          = 10;
        ctrls.maxDevToBest[kGlobalGroup]          = (std::uint32_t)~0;
        ctrls.maxDevToBest[kWarpGroup]            = (std::uint32_t)~0;
        ctrls.maxDevToBest[kObmcGroup]            = 10;
        ctrls.maxDevToBest[kInterIntraGroup]      = 10;
        ctrls.maxDevToBest[kInterCompGroup]       = 0;

        ctrls.useTplInfoOffset      = 20;
        ctrls.checkClosestMultiplier = 1;

        ctrls.closestRefs[kPaMeGroup]         = 1;
        ctrls.closestRefs[kUni3x3Group]       = 1;
        ctrls.closestRefs[kBi3x3Group]        = 1;
        ctrls.closestRefs[kNrstNewNearGroup]  = 1;
        ctrls.closestRefs[kNrstNearGroup]     = 1;
        ctrls.closestRefs[kPredMeGroup]       = 1;
        ctrls.closestRefs[kGlobalGroup]       = 1;
        ctrls.closestRefs[kWarpGroup]         = 1;
        ctrls.closestRefs[kObmcGroup]         = 1;
        ctrls.closestRefs[kInterIntraGroup]   = 1;
        ctrls.closestRefs[kInterCompGroup]    = 1;

        break;
    case 8:
        ctrls.enabled = 1;

        ctrls.maxDevToBest[kPaMeGroup]         = 0;
        ctrls.maxDevToBest[kUni3x3Group]       = 0;
        ctrls.maxDevToBest[kBi3x3Group]        = 0;
        ctrls.maxDevToBest[kNrstNewNearGroup]  = 0;
        ctrls.maxDevToBest[kNrstNearGroup]     = 0;
        ctrls.maxDevToBest[kPredMeGroup]       = 0;
        ctrls.maxDevToBest[kGlobalGroup]       = (std::uint32_t)~0;
        ctrls.maxDevToBest[kWarpGroup]         = 0;
        ctrls.maxDevToBest[kObmcGroup]         = 0;
        ctrls.maxDevToBest[kInterIntraGroup]   = 0;
        ctrls.maxDevToBest[kInterCompGroup]    = 0;

        ctrls.useTplInfoOffset      = 20;
        ctrls.checkClosestMultiplier = 1;

        ctrls.closestRefs[kPaMeGroup]         = 1;
        ctrls.closestRefs[kUni3x3Group]       = 1;
        ctrls.closestRefs[kBi3x3Group]        = 1;
        ctrls.closestRefs[kNrstNewNearGroup]  = 1;
        ctrls.closestRefs[kNrstNearGroup]     = 1;
        ctrls.closestRefs[kPredMeGroup]       = 1;
        ctrls.closestRefs[kGlobalGroup]       = 1;
        ctrls.closestRefs[kWarpGroup]         = 1;
        ctrls.closestRefs[kObmcGroup]         = 1;
        ctrls.closestRefs[kInterIntraGroup]   = 1;
        ctrls.closestRefs[kInterCompGroup]    = 1;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_txs_controls (enc_mode_config.c:6233-6303).
void setTxsCtrls(TxsCtrls& ctrls, std::uint8_t txsLevel, bool mimicOnlyTx4x4) {
    if (mimicOnlyTx4x4) {
        txsLevel = 1;
    }

    switch (txsLevel) {
    case 0:
        ctrls.enabled = 0;
        break;
    case 1:
        ctrls.enabled                  = 1;
        ctrls.prevDepthCoeffExitTh     = 1;
        ctrls.intraClassMaxDepthSq     = 2;
        ctrls.intraClassMaxDepthNsq    = 2;
        ctrls.interClassMaxDepthSq     = 2;
        ctrls.interClassMaxDepthNsq    = 2;
        ctrls.depth1TxtGroupOffset     = 0;
        ctrls.depth2TxtGroupOffset     = 0;
        ctrls.quadrantThSf             = 0;
        break;
    case 2:
        ctrls.enabled                  = 1;
        ctrls.prevDepthCoeffExitTh     = 1;
        ctrls.intraClassMaxDepthSq     = 2;
        ctrls.intraClassMaxDepthNsq    = 2;
        ctrls.interClassMaxDepthSq     = 1;
        ctrls.interClassMaxDepthNsq    = 1;
        ctrls.depth1TxtGroupOffset     = 0;
        ctrls.depth2TxtGroupOffset     = 0;
        ctrls.quadrantThSf             = 0;
        break;
    case 3:
        ctrls.enabled                  = 1;
        ctrls.prevDepthCoeffExitTh     = 1;
        ctrls.intraClassMaxDepthSq     = 1;
        ctrls.intraClassMaxDepthNsq    = 0;
        ctrls.interClassMaxDepthSq     = 1;
        ctrls.interClassMaxDepthNsq    = 0;
        ctrls.depth1TxtGroupOffset     = 3;
        ctrls.depth2TxtGroupOffset     = 3;
        ctrls.quadrantThSf             = 0;
        break;
    case 4:
        ctrls.enabled                  = 1;
        ctrls.prevDepthCoeffExitTh     = 2;
        ctrls.intraClassMaxDepthSq     = 1;
        ctrls.intraClassMaxDepthNsq    = 1;
        ctrls.interClassMaxDepthSq     = 0;
        ctrls.interClassMaxDepthNsq    = 0;
        ctrls.depth1TxtGroupOffset     = 4;
        ctrls.depth2TxtGroupOffset     = 4;
        ctrls.quadrantThSf             = 100;
        break;
    case kMaxTxsLevel - 1:
        ctrls.enabled                  = 1;
        ctrls.prevDepthCoeffExitTh     = 100;
        ctrls.intraClassMaxDepthSq     = 1;
        ctrls.intraClassMaxDepthNsq    = 1;
        ctrls.interClassMaxDepthSq     = 0;
        ctrls.interClassMaxDepthNsq    = 0;
        ctrls.depth1TxtGroupOffset     = 4;
        ctrls.depth2TxtGroupOffset     = 4;
        ctrls.quadrantThSf             = 100;
        break;
    default:
        ctrls.enabled = 0;
        break;
    }
}

// set_filter_intra_ctrls (enc_mode_config.c:6305-6324).
void setFilterIntraCtrls(FilterIntraCtrls& ctrls, std::uint8_t fiLvl) {
    switch (fiLvl) {
    case 0:
        ctrls.enabled = false;
        break;
    case 1:
        ctrls.enabled            = true;
        ctrls.maxFilterIntraMode = 4;  // FILTER_PAETH_PRED
        break;
    case 2:
        ctrls.enabled            = true;
        ctrls.maxFilterIntraMode = 0;  // FILTER_DC_PRED
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_spatial_sse_full_loop_level (enc_mode_config.c:6326-6347).
void setSpatialSseFullLoopLevel(SpatialSseCtrls& ctrls, std::uint8_t spatialSseFullLoopLevel) {
    switch (spatialSseFullLoopLevel) {
    case 0:
        ctrls.level = 3;  // SSSE_OFF
        break;
    case 1:
        ctrls.level = 0;  // SSSE_MDS1
        break;
    case 2:
        ctrls.level = 1;  // SSSE_MDS2
        break;
    case 3:
        ctrls.level = 2;  // SSSE_MDS3
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// The shared body of compute_intra_pd0_th / compute_subres_th
// (enc_mode_config.c:6349-6370).
static std::uint64_t computeIntraPd0ThImpl(std::uint32_t superBlockSize, const RdCostCtx& ctx) {
    const std::uint32_t fastLambda = ctx.hbdMd ? ctx.fastLambdaMd[kEb10BitMd]
                                               : ctx.fastLambdaMd[kEb8BitMd];
    const std::uint32_t sbSize     = superBlockSize * superBlockSize;
    const std::uint64_t costThRate = 1 << 13;

    // RDCOST(fast_lambda, cost_th_rate, sb_size * 6)
    const std::uint64_t prod =
        costThRate * (std::uint64_t)fastLambda;
    const std::uint64_t round = (prod + (((std::uint64_t)1 << kAv1ProbCostShift) >> 1)) >>
                                kAv1ProbCostShift;
    return round + ((std::uint64_t)sbSize * 6 << kRddivBits);
}

// compute_intra_pd0_th (enc_mode_config.c:6349-6360).
std::uint64_t computeIntraPd0Th(std::uint32_t superBlockSize, const RdCostCtx& ctx) {
    return computeIntraPd0ThImpl(superBlockSize, ctx);
}

// compute_subres_th (enc_mode_config.c:6361-6370) - the same arithmetic
// as compute_intra_pd0_th in the reference (two identical bodies).
std::uint64_t computeSubresTh(std::uint32_t superBlockSize, const RdCostCtx& ctx) {
    return computeIntraPd0ThImpl(superBlockSize, ctx);
}

// set_lpd1_tx_skip_decision_ctrls (enc_mode_config.c:6372-6408).
void setLpd1TxSkipDecisionCtrls(Lpd1TxSkipDecisionCtrls& ctrls, std::uint8_t level) {
    switch (level) {
    case 0:
        ctrls.skipTxScoreTh = 0;
        break;
    case 1:
        ctrls.skipTxScoreTh = 125;
        ctrls.distEnergyTh  = 30;
        ctrls.rdSkipTh      = 100;
        break;
    case 2:
        ctrls.skipTxScoreTh = 125;
        ctrls.distEnergyTh  = 30;
        ctrls.rdSkipTh      = 200;
        break;
    case 3:
        ctrls.skipTxScoreTh = 70;
        ctrls.distEnergyTh  = 30;
        ctrls.rdSkipTh      = 200;
        break;
    case 4:
        ctrls.skipTxScoreTh = 50;
        ctrls.distEnergyTh  = 30;
        ctrls.rdSkipTh      = 200;
        break;
    case 5:
        ctrls.skipTxScoreTh = 50;
        ctrls.distEnergyTh  = 40;
        ctrls.rdSkipTh      = 200;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_lpd1_tx_ctrls (enc_mode_config.c:6410-6460).
void setLpd1TxCtrls(Lpd1TxCtrls& ctrls, std::uint8_t lpd1TxLevel) {
    switch (lpd1TxLevel) {
    case 0:
        ctrls.zeroYCoeffExit            = 0;
        ctrls.chromaDetectorLevel       = 0;
        ctrls.useUvShortcutsOnYCoeffs   = 0;
        ctrls.useMds3ShortcutsTh        = 0;
        break;
    case 1:
        ctrls.zeroYCoeffExit            = 1;
        ctrls.chromaDetectorLevel       = 1;
        ctrls.useUvShortcutsOnYCoeffs   = 1;
        ctrls.useMds3ShortcutsTh        = 30;
        break;
    case 2:
        ctrls.zeroYCoeffExit            = 1;
        ctrls.chromaDetectorLevel       = 1;
        ctrls.useUvShortcutsOnYCoeffs   = 1;
        ctrls.useMds3ShortcutsTh        = 30;
        break;
    case 3:
        ctrls.zeroYCoeffExit            = 1;
        ctrls.chromaDetectorLevel       = 2;
        ctrls.useUvShortcutsOnYCoeffs   = 1;
        ctrls.useMds3ShortcutsTh        = 30;
        break;
    case 4:
        ctrls.zeroYCoeffExit            = 1;
        ctrls.chromaDetectorLevel       = 3;
        ctrls.useUvShortcutsOnYCoeffs   = 1;
        ctrls.useMds3ShortcutsTh        = 30;
        break;
    case 5:
        ctrls.zeroYCoeffExit            = 1;
        ctrls.chromaDetectorLevel       = 4;
        ctrls.useUvShortcutsOnYCoeffs   = 1;
        ctrls.useMds3ShortcutsTh        = 30;
        break;
    case 6:
        ctrls.zeroYCoeffExit            = 1;
        ctrls.chromaDetectorLevel       = 0;
        ctrls.useUvShortcutsOnYCoeffs   = 1;
        ctrls.useMds3ShortcutsTh        = 30;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_cfl_ctrls (enc_mode_config.c:6462-6498).
void setCflCtrls(CflCtrls& ctrls, std::uint8_t cflLevel) {
    switch (cflLevel) {
    case 0:
        ctrls.enabled = false;
        break;
    case 1:
        ctrls.enabled = true;
        ctrls.itrTh   = 2;
        ctrls.cplxTh  = 0;
        break;
    case 2:
        ctrls.enabled = true;
        ctrls.itrTh   = 1;
        ctrls.cplxTh  = 0;
        break;
    case 3:
        ctrls.enabled = true;
        ctrls.itrTh   = 1;
        ctrls.cplxTh  = 5;
        break;
    case 4:
        ctrls.enabled = true;
        ctrls.itrTh   = 1;
        ctrls.cplxTh  = 10;
        break;
    case 5:
        ctrls.enabled = true;
        ctrls.itrTh   = 1;
        ctrls.cplxTh  = 55;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_rate_est_ctrls (enc_mode_config.c:6500-6547).
void setRateEstCtrls(MdRateEstCtrls& ctrls, std::uint8_t rateEstLevel) {
    switch (rateEstLevel) {
    case 0:
        ctrls.updateSkipCtxDcSignCtx = false;
        ctrls.updateSkipCoeffCtx     = false;
        ctrls.coeffRateEstLvl        = 0;
        ctrls.lpd0QpOffset           = 8;
        ctrls.pd0FastCoeffEstLevel   = 2;
        break;
    case 1:
        ctrls.updateSkipCtxDcSignCtx = true;
        ctrls.updateSkipCoeffCtx     = true;
        ctrls.coeffRateEstLvl        = 1;
        ctrls.lpd0QpOffset           = 0;
        ctrls.pd0FastCoeffEstLevel   = 1;
        break;
    case 2:
        ctrls.updateSkipCtxDcSignCtx = true;
        ctrls.updateSkipCoeffCtx     = false;
        ctrls.coeffRateEstLvl        = 1;
        ctrls.lpd0QpOffset           = 0;
        ctrls.pd0FastCoeffEstLevel   = 2;
        break;
    case 3:
        ctrls.updateSkipCtxDcSignCtx = true;
        ctrls.updateSkipCoeffCtx     = false;
        ctrls.coeffRateEstLvl        = 2;
        ctrls.lpd0QpOffset           = 0;
        ctrls.pd0FastCoeffEstLevel   = 2;
        break;
    case 4:
        ctrls.updateSkipCtxDcSignCtx = false;
        ctrls.updateSkipCoeffCtx     = false;
        ctrls.coeffRateEstLvl        = 2;
        ctrls.lpd0QpOffset           = 0;
        ctrls.pd0FastCoeffEstLevel   = 2;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// get_sb_tpl_intra_stats (enc_mode_config.c:6552-6609).
bool getSbTplIntraStats(const SbTplPcs& pcs, const SbTplCtx& ctx, int* sbAngIntraCount,
                        std::uint8_t* sbMaxIntra, int* sbIntraCount) {
    // Check that TPL data is available and that INTRA was tested in TPL.
    if (pcs.tplEnable && pcs.tplSrcDataReady &&
        (pcs.temporalLayerIndex < pcs.hierarchicalLevels || !pcs.disableIntraPredNref)) {
        const int      aligned16Width = (int)((pcs.alignedWidth + 15) >> 4);
        const std::uint32_t mbOriginX = ctx.sbOriginX;
        const std::uint32_t mbOriginY = ctx.sbOriginY;
        const int      tplBlkSize = pcs.dispenserSearchLevel == 0 ? 16
                                    : pcs.dispenserSearchLevel == 1 ? 32
                                                                    : 64;
        const int      tplBlkStep = pcs.dispenserSearchLevel == 0 ? 1
                                    : pcs.dispenserSearchLevel == 1 ? 2
                                                                    : 4;

        // Get actual SB width (for cases of incomplete SBs)
        const int sbCols = std::max(1, (int)pcs.sbGeomWidth / tplBlkSize);
        const int sbRows = std::max(1, (int)pcs.sbGeomHeight / tplBlkSize);

        int            angIntraCount = 0;
        std::uint8_t   maxIntra      = kDcPred;
        int            intraCount    = 0;

        // Loop over all blocks in the SB
        for (int i = 0; i < sbRows; i++) {
            const int rowBase = (int)((mbOriginY >> 4) + (i * tplBlkStep)) * aligned16Width +
                                (int)(mbOriginX >> 4);
            for (int j = 0; j < sbCols; j++) {
                const std::uint8_t bestMode = pcs.tplBestModes[rowBase + j * tplBlkStep];
                if (bestMode < kIntraModeEnd) {  // is_intra_mode
                    if (maxIntra < bestMode) maxIntra = bestMode;
                    intraCount++;
                }

                if (bestMode >= kVPred && bestMode <= kD67Pred) {  // av1_is_directional_mode
                    angIntraCount++;
                }
            }
        }

        *sbAngIntraCount = angIntraCount;
        *sbMaxIntra      = maxIntra;
        *sbIntraCount    = intraCount;
        return true;
    }
    return false;
}

// set_intra_ctrls (enc_mode_config.c:6611-6796).
void setIntraCtrls(IntraCtrls& ctrls, const IntraSetPcs& pcs, IntraSetCtx& ctx,
                   std::uint8_t intraLevel, std::uint8_t distBasedAngIntraLevel) {
    // If intra is disallowed at the pic level, must disallow at SB level
    if (pcs.skipIntra) {
        intraLevel = 0;
    }

    switch (intraLevel) {
    case 0:
        ctrls.enableIntra          = 0;
        ctrls.intraModeEnd         = kDcPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 0;
        ctrls.pruneUsingEdgeInfo   = false;
        break;
    case 1:
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kPaethPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 0;
        ctrls.pruneUsingEdgeInfo   = false;
        break;
    case 2: {
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kPaethPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 0;
        ctrls.pruneUsingEdgeInfo   = false;
        // Only use TPL info if all INTRA modes are tested
        if (pcs.tplEnable && pcs.tplIntraModeEnd == kPaethPred) {
            int            sbAngIntraCount;
            std::uint8_t   sbMaxIntra;
            int            sbIntraCount;
            if (getSbTplIntraStats(pcs.tpl, pcs.tplCtx, &sbAngIntraCount, &sbMaxIntra,
                                   &sbIntraCount)) {
                // if SB has angluar modes, use full search
                if (sbAngIntraCount) {
                    ctrls.angularPredLevel = 1;
                } else {
                    ctrls.angularPredLevel = 3;
                }
            }
        }
        break;
    }
    case 3: {
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kPaethPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 0;
        ctrls.pruneUsingEdgeInfo   = false;
        // Only use TPL info if all INTRA modes are tested
        if (pcs.tplEnable && pcs.tplIntraModeEnd == kPaethPred) {
            int            sbAngIntraCount;
            std::uint8_t   sbMaxIntra;
            int            sbIntraCount;
            if (getSbTplIntraStats(pcs.tpl, pcs.tplCtx, &sbAngIntraCount, &sbMaxIntra,
                                   &sbIntraCount)) {
                // if SB has angluar modes, use full search
                if (sbAngIntraCount) {
                    ctrls.angularPredLevel = 1;
                } else {
                    ctrls.angularPredLevel = 3;
                }
                ctrls.intraModeEnd = sbMaxIntra;
            }
        }
        break;
    }
    case 4: {
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kSmoothHPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 0;
        ctrls.pruneUsingEdgeInfo   = false;
        // Only use TPL info if all INTRA modes are tested
        if (pcs.tplEnable && pcs.tplIntraModeEnd == kPaethPred) {
            int            sbAngIntraCount;
            std::uint8_t   sbMaxIntra;
            int            sbIntraCount;
            if (getSbTplIntraStats(pcs.tpl, pcs.tplCtx, &sbAngIntraCount, &sbMaxIntra,
                                   &sbIntraCount)) {
                const int tplBlkSize = pcs.tpl.dispenserSearchLevel == 0 ? 16
                                       : pcs.tpl.dispenserSearchLevel == 1 ? 32
                                                                           : 64;
                // Get actual SB width (for cases of incomplete SBs)
                const int sbCols = (int)pcs.tpl.sbGeomWidth / tplBlkSize;
                const int sbRows = (int)pcs.tpl.sbGeomHeight / tplBlkSize;

                // if more than a quarter of SB is angular, use safe intra_level
                if (sbAngIntraCount > ((sbRows * sbCols) >> 2)) {
                    ctrls.angularPredLevel = 1;
                } else if (sbAngIntraCount > 2) {
                    ctrls.angularPredLevel = 2;
                } else {
                    ctrls.angularPredLevel = 4;
                }
                ctrls.intraModeEnd = sbMaxIntra;
            }
        }
        break;
    }
    case 5: {
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kSmoothPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 0;
        ctrls.pruneUsingEdgeInfo   = false;
        // There is no check that all TPL modes are checked, so should only use info about
        // general intra modes, not the specific intra mode selected or whether it's angular
        if (pcs.tplEnable) {
            int            sbAngIntraCount;
            std::uint8_t   sbMaxIntra;
            int            sbIntraCount;
            if (getSbTplIntraStats(pcs.tpl, pcs.tplCtx, &sbAngIntraCount, &sbMaxIntra,
                                   &sbIntraCount)) {
                if (sbIntraCount > 0) {
                    ctrls.angularPredLevel = 2;
                    ctrls.intraModeEnd     = kPaethPred;
                } else if (pcs.refIntraPercentage < 30) {
                    ctrls.angularPredLevel = 0;
                    ctrls.intraModeEnd     = kSmoothPred;
                }
            }
        }
        break;
    }
    case 6:
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kSmoothPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 0;
        ctrls.pruneUsingEdgeInfo   = false;
        break;
    case 7:
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kSmoothPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 1;
        ctrls.pruneUsingEdgeInfo   = false;
        break;
    case 8:
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kSmoothPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 1;
        ctrls.pruneUsingEdgeInfo   = true;
        break;
    case kMaxIntraLevel - 1:
        ctrls.enableIntra          = 1;
        ctrls.intraModeEnd         = kDcPred;
        ctrls.angularPredLevel     = angularPredLevel[intraLevel];
        ctrls.pruneUsingBestMode   = 0;
        ctrls.pruneUsingEdgeInfo   = false;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
    switch (distBasedAngIntraLevel) {
    case 0:
        ctrls.skipAngularDelta1Th = -1;
        ctrls.skipAngularDelta2Th = -1;
        ctrls.skipAngularDelta3Th = -1;
        break;
    case 1:
        ctrls.skipAngularDelta1Th = 25;
        ctrls.skipAngularDelta2Th = 25;
        ctrls.skipAngularDelta3Th = 25;
        break;
    case 2:
        ctrls.skipAngularDelta1Th = 20;
        ctrls.skipAngularDelta2Th = 10;
        ctrls.skipAngularDelta3Th = 5;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
    /* For PD1, the ability to skip intra must be set at the pic level to ensure all SBs
    perform inverse TX and generate the recon. */
    if (ctx.pdPass == kPdPass1) {
        ctx.skipIntra = pcs.skipIntra;

    } else {
        ctx.skipIntra = !(ctrls.enableIntra) || pcs.skipIntra;
    }
}

// set_tx_shortcut_ctrls (enc_mode_config.c:6798-6838).
void setTxShortcutCtrls(TxShortcutCtrls& ctrls, std::uint8_t txShortcutLevel) {
    switch (txShortcutLevel) {
    case 0:
        ctrls.bypassTxTh          = 0;
        ctrls.applyPfOnCoeffs     = 0;
        ctrls.useMds3ShortcutsTh  = 0;
        ctrls.chromaDetectorLevel = 0;
        break;
    case 1:
        ctrls.bypassTxTh          = 4;
        ctrls.applyPfOnCoeffs     = 1;
        ctrls.useMds3ShortcutsTh  = 0;
        ctrls.chromaDetectorLevel = 1;
        break;
    case 2:
        ctrls.bypassTxTh          = 1;
        ctrls.applyPfOnCoeffs     = 1;
        ctrls.useMds3ShortcutsTh  = 0;
        ctrls.chromaDetectorLevel = 1;
        break;
    case 3:
        ctrls.bypassTxTh          = 1;
        ctrls.applyPfOnCoeffs     = 1;
        ctrls.useMds3ShortcutsTh  = 10;
        ctrls.chromaDetectorLevel = 1;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }

    // :6832-6837 - the trailing assert (chroma detector on for ref
    // frames at <= M9) is a level-configuration invariant compiled out
    // under NDEBUG in the tested config (named deviation).
}

// set_mds0_controls (enc_mode_config.c:6840-6861).
void setMds0Ctrls(Mds0Ctrls& ctrls, std::uint8_t mds0Level) {
    switch (mds0Level) {
    case 0:
        ctrls.pruningMethodTh = 0;
        break;
    case 1:
        ctrls.pruningMethodTh                       = 100;
        ctrls.perClassDistToCostTh[0]               = 50;  // CAND_CLASS_0
        ctrls.perClassDistToCostTh[1]               = 10;  // CAND_CLASS_1
        ctrls.perClassDistToCostTh[2]               = 10;  // CAND_CLASS_2
        ctrls.perClassDistToCostTh[3]               = 50;  // CAND_CLASS_3
        break;
    case 2:
        ctrls.pruningMethodTh = (std::uint8_t)~0;
        ctrls.distToCostTh    = 0;
        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_skip_sub_depth_ctrls (enc_mode_config.c:6863-6890).
void setSkipSubDepthCtrls(SkipSubDepthCtrls& ctrls, std::uint8_t skipSubDepthLvl) {
    switch (skipSubDepthLvl) {
    case 0:
        ctrls.enabled = 0;
        break;

    case 1:
        ctrls.enabled = 1;
        // Cond1 ctrls
        ctrls.maxSize          = 16;
        ctrls.quadDeviationTh  = 250;
        ctrls.coeffPerc        = 15;

        break;
    case 2:

        ctrls.enabled = 1;
        // Cond1 ctrls
        ctrls.maxSize          = 16;
        ctrls.quadDeviationTh  = 250;
        ctrls.coeffPerc        = 25;

        break;
    default:
        // The reference assert(0)s here and writes nothing (documented UB).
        break;
    }
}

// set_block_based_depth_refinement_controls (enc_mode_config.c:6892-7065).
void setBlockBasedDepthRefinementCtrls(DepthRefinementCtrls& ctrls,
                                       std::uint8_t blockBasedDepthRefinementLevel) {
    switch (blockBasedDepthRefinementLevel) {
    case 0:
        ctrls.mode = kPd0DepthNoRestriction;
        break;
    case 1:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 200;
        ctrls.s2ParentToCurrentTh     = 0;
        ctrls.e1SubToCurrentTh        = 200;
        ctrls.e2SubToCurrentTh        = 0;
        ctrls.parentMaxCostThMult     = 10;
        ctrls.coeffLvlModulation      = 1;
        ctrls.costBandBasedModulation = 0;
        ctrls.lowerDepthSplitCostTh   = 0;
        ctrls.splitRateTh             = 0;
        ctrls.limitMaxMinToPd0        = 0;
        ctrls.useRefInfo              = 0;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 2;
        break;
    case 2:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 90;
        ctrls.s2ParentToCurrentTh     = 0;
        ctrls.e1SubToCurrentTh        = 90;
        ctrls.e2SubToCurrentTh        = 0;
        ctrls.parentMaxCostThMult     = 10;
        ctrls.coeffLvlModulation      = 1;
        ctrls.costBandBasedModulation = 0;
        ctrls.lowerDepthSplitCostTh   = 10;
        ctrls.splitRateTh             = 10;
        ctrls.limitMaxMinToPd0        = 0;
        ctrls.useRefInfo              = 0;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 2;
        break;
    case 3:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 60;
        ctrls.s2ParentToCurrentTh     = 0;
        ctrls.e1SubToCurrentTh        = 60;
        ctrls.e2SubToCurrentTh        = 0;
        ctrls.parentMaxCostThMult     = 10;
        ctrls.coeffLvlModulation      = 1;
        ctrls.costBandBasedModulation = 0;
        ctrls.lowerDepthSplitCostTh   = 10;
        ctrls.splitRateTh             = 10;
        ctrls.limitMaxMinToPd0        = 0;
        ctrls.useRefInfo              = 0;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 2;
        break;
    case 4:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 30;
        ctrls.s2ParentToCurrentTh     = 0;
        ctrls.e1SubToCurrentTh        = 30;
        ctrls.e2SubToCurrentTh        = 0;
        ctrls.parentMaxCostThMult     = 10;
        ctrls.coeffLvlModulation      = 1;
        ctrls.costBandBasedModulation = 0;
        ctrls.lowerDepthSplitCostTh   = 10;
        ctrls.splitRateTh             = 10;
        ctrls.limitMaxMinToPd0        = 0;
        ctrls.useRefInfo              = 0;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 2;
        break;
    case 5:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 30;
        ctrls.s2ParentToCurrentTh     = (std::uint8_t)~0;
        ctrls.e1SubToCurrentTh        = 30;
        ctrls.e2SubToCurrentTh        = (std::uint8_t)~0;
        ctrls.parentMaxCostThMult     = 10;
        ctrls.coeffLvlModulation      = 1;
        ctrls.costBandBasedModulation = 0;
        ctrls.lowerDepthSplitCostTh   = 10;
        ctrls.splitRateTh             = 10;
        ctrls.limitMaxMinToPd0        = 2;
        ctrls.useRefInfo              = 0;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 2;
        break;
    case 6:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 15;
        ctrls.s2ParentToCurrentTh     = (std::uint8_t)~0;
        ctrls.e1SubToCurrentTh        = 15;
        ctrls.e2SubToCurrentTh        = (std::uint8_t)~0;
        ctrls.parentMaxCostThMult     = 10;
        ctrls.coeffLvlModulation      = 1;
        ctrls.costBandBasedModulation = 0;
        ctrls.lowerDepthSplitCostTh   = 20;
        ctrls.splitRateTh             = 10;
        ctrls.limitMaxMinToPd0        = 1;
        ctrls.useRefInfo              = 0;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 2;
        break;
    case 7:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 15;
        ctrls.s2ParentToCurrentTh     = (std::uint8_t)~0;
        ctrls.e1SubToCurrentTh        = 15;
        ctrls.e2SubToCurrentTh        = (std::uint8_t)~0;
        ctrls.parentMaxCostThMult     = 0;
        ctrls.costBandBasedModulation = 1;
        ctrls.maxCostMultiplier       = 400;
        ctrls.maxBandCnt              = 4;
        ctrls.decrementPerBand[0]     = kMaxSignedValue;
        ctrls.decrementPerBand[1]     = kMaxSignedValue;
        ctrls.decrementPerBand[2]     = 10;
        ctrls.decrementPerBand[3]     = 5;
        ctrls.coeffLvlModulation      = 1;
        ctrls.lowerDepthSplitCostTh   = 20;
        ctrls.splitRateTh             = 5;
        ctrls.limitMaxMinToPd0        = 1;
        ctrls.useRefInfo              = 1;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 2;
        break;
    case 8:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 10;
        ctrls.s2ParentToCurrentTh     = (std::uint8_t)~0;
        ctrls.e1SubToCurrentTh        = 10;
        ctrls.e2SubToCurrentTh        = (std::uint8_t)~0;
        ctrls.parentMaxCostThMult     = 0;
        ctrls.costBandBasedModulation = 1;
        ctrls.maxCostMultiplier       = 400;
        ctrls.maxBandCnt              = 4;
        ctrls.decrementPerBand[0]     = kMaxSignedValue;
        ctrls.decrementPerBand[1]     = kMaxSignedValue;
        ctrls.decrementPerBand[2]     = 10;
        ctrls.decrementPerBand[3]     = 5;
        ctrls.coeffLvlModulation      = 1;
        ctrls.lowerDepthSplitCostTh   = 25;
        ctrls.splitRateTh             = 5;
        ctrls.limitMaxMinToPd0        = 1;
        ctrls.useRefInfo              = 1;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 0;
        break;
    case 9:
        ctrls.mode                    = kPd0DepthAdaptive;
        ctrls.s1ParentToCurrentTh     = 10;
        ctrls.s2ParentToCurrentTh     = (std::uint8_t)~0;
        ctrls.e1SubToCurrentTh        = 10;
        ctrls.e2SubToCurrentTh        = (std::uint8_t)~0;
        ctrls.parentMaxCostThMult     = 0;
        ctrls.costBandBasedModulation = 1;
        ctrls.maxCostMultiplier       = 400;
        ctrls.maxBandCnt              = 4;
        ctrls.decrementPerBand[0]     = kMaxSignedValue;
        ctrls.decrementPerBand[1]     = kMaxSignedValue;
        ctrls.decrementPerBand[2]     = 10;
        ctrls.decrementPerBand[3]     = 5;
        ctrls.coeffLvlModulation      = 1;
        ctrls.lowerDepthSplitCostTh   = 100;
        ctrls.splitRateTh             = 5;
        ctrls.limitMaxMinToPd0        = 1;
        ctrls.useRefInfo              = 1;
        ctrls.qWeight                 = 1;
        ctrls.pd0UnavailModeDepth     = 0;
        break;
    case 10:
        ctrls.mode = kPd0DepthPredPartOnly;
        break;
    }
}

}  // namespace mode_config
}  // namespace l16_rdo
