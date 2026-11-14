#include <cmath>
#include <cstring>
#include <cstdint>

#include "cdef_driver.h"

namespace l10_filters {
namespace cdef_driver {

// definitions.h:687-style clamp (the reference uses the AOM clamp macro).
static inline std::int32_t clampI32(std::int32_t v, std::int32_t low, std::int32_t high) {
    return v < low ? low : (v > high ? high : v);
}

// compute_cdef_dist (cdef_process.c:266-:290). 1:1 dispatch; the dst
// doffset is a pixel offset (element offset at 16 bit, byte offset at 8).
std::uint64_t computeCdefDist(const std::uint8_t* dst, std::int32_t doffset, std::int32_t dstride,
    const std::uint8_t* src, const CdefList* dlist, std::int32_t cdefCount, CdefBlockSize bsize,
    std::int32_t coeffShift, std::uint8_t subsamplingFactor, bool is16bit) {
    std::uint64_t currMse = 0;
    if (is16bit) {
        currMse = cdefComputeDist16bit(reinterpret_cast<const std::uint16_t*>(dst) + doffset, dstride,
            reinterpret_cast<const std::uint16_t*>(src), dlist, cdefCount, bsize, coeffShift, subsamplingFactor);
    } else {
        currMse = cdefComputeDist8bit(dst + doffset, dstride, src, dlist, cdefCount, bsize, coeffShift,
            subsamplingFactor);
    }
    return currMse;
}

// svt_pick_cdef_from_qp (enc_cdef.c:823-:873). 1:1: the screen branch
// truncates (plain (int32_t) casts), the inter/intra branches roundf; the
// qtx call site is threaded (AcQuantQtxFn).
void pickCdefFromQp(const PickQpParams& p, std::int32_t* predYStrength, std::int32_t* predUvStrength) {
    const std::int32_t q = p.acQuantQtx(p.baseQIdx, 0, p.bitDepth) >> (p.bitDepth - 8);

    std::int32_t yF1 = 0, yF2 = 0;
    std::int32_t uvF1 = 0, uvF2 = 0;

    const bool isIntra = p.isKeyFrame || p.intraOnly;

    if (p.isScreenContent) {
        yF1 = (std::int32_t)(5.88217781e-06 * q * q + 6.10391455e-03 * q + 9.95043102e-02);

        yF2 = (std::int32_t)(-7.79934857e-06 * q * q + 6.58957830e-03 * q + 8.81045025e-01);

        uvF1 = (std::int32_t)(-6.79500136e-06 * q * q + 1.02695586e-02 * q + 1.36126802e-01);

        uvF2 = (std::int32_t)(-9.99613695e-08 * q * q - 1.79361339e-05 * q + 1.17022324e+0);
    } else if (!isIntra) {
        yF1 = (std::int32_t)roundf(q * q * -0.0000023593946f + q * 0.0068615186f + 0.02709886f);

        yF2 = (std::int32_t)roundf(q * q * -0.00000057629734f + q * 0.0013993345f + 0.03831067f);

        uvF1 = (std::int32_t)roundf(q * q * -0.0000007095069f + q * 0.0034628846f + 0.00887099f);

        uvF2 = (std::int32_t)roundf(q * q * 0.00000023874085f + q * 0.00028223585f + 0.05576307f);
    } else { // Intra
        yF1 = (std::int32_t)roundf(q * q * 0.0000033731974f + q * 0.008070594f + 0.0187634f);

        yF2 = (std::int32_t)roundf(q * q * 0.0000029167343f + q * 0.0027798624f + 0.0079405f);

        uvF1 = (std::int32_t)roundf(q * q * -0.0000130790995f + q * 0.012892405f - 0.00748388f);

        uvF2 = (std::int32_t)roundf(q * q * 0.0000032651783f + q * 0.00035520183f + 0.00228092f);
    }

    // Clamp to AV1 limits
    yF1 = clampI32(yF1, 0, 15);
    yF2 = clampI32(yF2, 0, 3);
    uvF1 = clampI32(uvF1, 0, 15);
    uvF2 = clampI32(uvF2, 0, 3);

    // Pack primary + secondary
    *predYStrength = yF1 * kCdefSecStrengths + yF2;
    *predUvStrength = uvF1 * kCdefSecStrengths + uvF2;
}

// propagate_cdef_strength (enc_cdef.c:875-:895). 1:1 over the flat grid;
// MI_SIZE_64X64 = 16 (definitions.h:376) is the 64x64 mi step.
void propagateCdefStrength(CdefMb* miGrid, std::int32_t miStride, std::int32_t sbIndex, int bsize,
    std::int8_t strength) {
    constexpr std::int32_t kMiSize64x64 = 16;
    miGrid[sbIndex].cdefStrength = strength;
    switch (bsize) {
    case 15: // BLOCK_128X128
        miGrid[sbIndex + kMiSize64x64].cdefStrength = strength;
        miGrid[sbIndex + kMiSize64x64 * miStride].cdefStrength = strength;
        miGrid[sbIndex + kMiSize64x64 * miStride + kMiSize64x64].cdefStrength = strength;
        break;
    case 14: // BLOCK_128X64
        miGrid[sbIndex + kMiSize64x64].cdefStrength = strength;
        break;
    case 13: // BLOCK_64X128
        miGrid[sbIndex + kMiSize64x64 * miStride].cdefStrength = strength;
        break;
    default:
        break;
    }
}

// copy_recon_enc (cdef_process.c:46-:150). 1:1; the EB_CALLOC_ALIGNED_
// ARRAY allocation wall is threaded as bufferAlloc.
void copyReconEnc(const CdefScsWalls& scs, const CdefPic& src, CdefPic& dst, int numPlanes, bool skipCopy,
    std::uint8_t* bufferAlloc) {
    dst.border = src.border;
    dst.width = src.width;
    dst.height = src.height;
    dst.maxWidth = src.maxWidth;
    dst.maxHeight = src.maxHeight;
    dst.bitDepth = src.bitDepth;
    dst.colorFormat = src.colorFormat;

    dst.stride[0] = src.stride[0];
    dst.stride[1] = src.stride[1];
    dst.stride[2] = src.stride[2];

    dst.lumaSize = src.lumaSize;
    dst.chromaSize = src.chromaSize;
    dst.packedFlag = src.packedFlag;

    dst.strideBitInc[0] = src.strideBitInc[0];
    dst.strideBitInc[1] = src.strideBitInc[1];
    dst.strideBitInc[2] = src.strideBitInc[2];

    dst.bufferEnableMask = scs.monoChrome ? kPicBufLumaMask : kPicBufFullMask;

    const int ssX = scs.subsamplingX;
    const int ssY = scs.subsamplingY;
    const std::uint32_t bytesPerPixel = scs.is16bitPipeline ? 2 : 1;

    // Get frame size to alloc
    std::uint32_t allocSz = 0;
    std::uint32_t bufferSize[3] = {0};
    if (dst.bufferEnableMask & kPicBufYFlag) {
        allocSz += bufferSize[0] = dst.lumaSize * bytesPerPixel;
    }

    if (dst.bufferEnableMask & kPicBufCbFlag) {
        allocSz += bufferSize[1] = dst.chromaSize * bytesPerPixel;
    }

    if (dst.bufferEnableMask & kPicBufCrFlag) {
        allocSz += bufferSize[2] = dst.chromaSize * bytesPerPixel;
    }

    dst.bufferAlloc = bufferAlloc;
    dst.bufferAllocSz = allocSz;
    std::uint32_t assignedSpace = 0;
    if (dst.bufferEnableMask & kPicBufYFlag) {
        dst.buffer[0] = dst.bufferAlloc + (dst.border + (dst.stride[0] * dst.border)) * bytesPerPixel;
        assignedSpace += bufferSize[0];
    } else {
        dst.buffer[0] = nullptr;
    }

    if (dst.bufferEnableMask & kPicBufCbFlag) {
        dst.buffer[1] = dst.bufferAlloc + assignedSpace +
            ((dst.border >> ssX) + (dst.stride[1] * (dst.border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[1];
    } else {
        dst.buffer[1] = nullptr;
    }

    if (dst.bufferEnableMask & kPicBufCrFlag) {
        dst.buffer[2] = dst.bufferAlloc + assignedSpace +
            ((dst.border >> ssX) + (dst.stride[2] * (dst.border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[2];
    } else {
        dst.buffer[2] = nullptr;
    }

    const int useHighbd = scs.is16bitPipeline ? 1 : 0;

    if (!skipCopy) {
        for (int plane = 0; plane < numPlanes; ++plane) {
            const std::uint8_t* srcBuf = src.buffer[plane];
            std::uint8_t* dstBuf = dst.buffer[plane];
            const std::int32_t srcStride = src.stride[plane];
            const std::int32_t dstStride = dst.stride[plane];

            const int subX = plane ? scs.subsamplingX : 0;
            const int subY = plane ? scs.subsamplingY : 0;

            const std::int32_t height = (src.height + subY) >> subY;
            for (int row = 0; row < height; ++row) {
                memcpy(dstBuf, srcBuf, ((src.width + subX) >> subX) * sizeof(*srcBuf) << useHighbd);
                srcBuf += srcStride << useHighbd;
                dstBuf += dstStride << useHighbd;
            }
        }
    }
}

// set_unscaled_input_16bit (cdef_process.c:31-:44). 1:1 dispatch; the
// three pic-buffer callees are threaded.
void setUnscaledInput16bit(const CdefScsWalls& scs, const CdefPic& input, CdefPic& output, CopyBufferInfoFn copyInfo,
    Convert8To16Fn convert8To16, Pack2dPicFn pack2dPic) {
    copyInfo(input, output);
    if (input.bitDepth == 8) {
        convert8To16(input, output, scs.subsamplingX, scs.subsamplingY);
    } else {
        pack2dPic(input, output);
    }
}

// cdef_seg_search (cdef_process.c:292-:666). 1:1; the walls are the
// CdefSegCtx fields, svt_sb_compute_cdef_list the callback. The landed
// cdefFilterFbLbd / cdefFilterFb / cdefCopyRect8 / cdefCopySb8To16 /
// computeCdefDist carry the kernel calls. CDEF geometry constants:
// CDEF_VBORDER 3, CDEF_HBORDER 8, CDEF_HALO 2, CDEF_BSTRIDE 144
// (kCdefBstride), MI_SIZE_LOG2 2, MI_SIZE_64X64 16, MI_SIZE_128X128 32,
// MAX_SB_SIZE_LOG2 7.
void cdefSegSearch(CdefSegCtx& ctx, std::uint32_t segmentIndex) {
    constexpr std::int32_t kMiSize64x64 = 16;
    constexpr std::int32_t kMiSize128x128 = 32;
    constexpr std::int32_t kMiSizeLog2 = 2;
    constexpr std::int32_t kCdefVBorder = 3;
    constexpr std::int32_t kCdefHBorder = 8;
    constexpr std::int32_t kCdefHalo = 2;
    constexpr std::int32_t kMaxSbSizeLog2 = 7;
    constexpr std::uint64_t kDefaultMseUv = 1040400;

    const std::uint32_t b64PicWidth = (ctx.alignedWidth + 64 - 1) / 64;
    const std::uint32_t b64PicHeight = (ctx.alignedHeight + 64 - 1) / 64;
    const std::uint32_t xSegIdx = segmentIndex % ctx.cdefSegmentsColumnCount;
    const std::uint32_t ySegIdx = segmentIndex / ctx.cdefSegmentsColumnCount;
    const std::uint32_t xB64StartIdx = (xSegIdx * b64PicWidth) / ctx.cdefSegmentsColumnCount;
    const std::uint32_t xB64EndIdx = ((xSegIdx + 1) * b64PicWidth) / ctx.cdefSegmentsColumnCount;
    const std::uint32_t yB64StartIdx = (ySegIdx * b64PicHeight) / ctx.cdefSegmentsRowCount;
    const std::uint32_t yB64EndIdx = ((ySegIdx + 1) * b64PicHeight) / ctx.cdefSegmentsRowCount;

    const std::int32_t miRows = ctx.miRows;
    const std::int32_t miCols = ctx.miCols;
    const CdefSearchControls* cdefCtrls = &ctx.ctrls;
    const int firstPassFsNum = cdefCtrls->firstPassFsNum;
    const int defaultSecondPassFsNum = cdefCtrls->defaultSecondPassFsNum;
    const std::int32_t coeffShift = ctx.encoderBitDepth - 8 > 0 ? ctx.encoderBitDepth - 8 : 0;
    const std::int32_t nvfb = (miRows + kMiSize64x64 - 1) / kMiSize64x64;
    const std::int32_t nhfb = (miCols + kMiSize64x64 - 1) / kMiSize64x64;
    const std::int32_t damping = 3 + (ctx.baseQIdx >> 6);
    const std::int32_t numPlanes = 3;
    const bool sb64 = ctx.sb64;
    CdefList dlistLocal[kMiSize128x128 * kMiSize128x128];
    CdefList* dlist;

    const bool is16bit = ctx.is16bit;

    std::int32_t toffPrev = kCdefVBorder;
    std::int32_t loffPrev = kCdefHBorder;
    std::int32_t ysizePrev = (1 << kMaxSbSizeLog2) + 2 * kCdefVBorder;
    std::int32_t xsizePrev = (1 << kMaxSbSizeLog2) + 2 * kCdefHBorder;

    std::uint16_t* inbuf = ctx.scratch.inbuf; // 16-bit sentinel buffer
    std::uint16_t* in = inbuf + kCdefVBorder * kCdefBstride + kCdefHBorder;
    // 8-bit padded buffer for the native interior path
    std::uint8_t* inbuf8 = ctx.scratch.inbuf8;
    std::uint8_t* in8 = inbuf8 + kCdefVBorder * kCdefBstride + kCdefHBorder;
    std::uint16_t* tmpDst = ctx.scratch.tmpDst;

    (void)kMiSizeLog2;

    // Loop over all filter blocks (64x64)
    for (std::uint32_t fbr = yB64StartIdx; fbr < yB64EndIdx; ++fbr) {
        for (std::uint32_t fbc = xB64StartIdx; fbc < xB64EndIdx; ++fbc) {
            std::int32_t dirinit = 0;
            const std::uint32_t lc = kMiSize64x64 * fbc;
            const std::uint32_t lr = kMiSize64x64 * fbr;
            int nhb = miCols - lc < kMiSize64x64 ? miCols - lc : kMiSize64x64;
            int nvb = miRows - lr < kMiSize64x64 ? miRows - lr : kMiSize64x64;
            int hbStep = 1; // these should be all time with 64x64 SBs
            int vbStep = 1;
            int bs = 12; // BLOCK_64X64
            const int bsize = ctx.miGrid[lr * ctx.miStride + lc].bsize;
            if (((fbc & 1) && (bsize == 15 || bsize == 14)) || ((fbr & 1) && (bsize == 15 || bsize == 13))) {
                continue;
            }
            if (bsize == 15 || bsize == 14 || bsize == 13) {
                bs = bsize;
            }

            if (bs == 15 || bs == 14) {
                nhb = miCols - lc < kMiSize128x128 ? miCols - lc : kMiSize128x128;
                hbStep = 2;
            }
            if (bs == 15 || bs == 13) {
                nvb = miRows - lr < kMiSize128x128 ? miRows - lr : kMiSize128x128;
                vbStep = 2;
            }
            const std::uint32_t fbIdx = fbr * nhfb + fbc;
            // For SB=64, compute the dlist straight into the per-fb cache
            dlist = sb64 ? ctx.cdefFbList[fbIdx].dlist : dlistLocal;
            const std::int32_t cdefCount =
                ctx.computeCdefList(ctx.miGrid, ctx.miStride, (std::int32_t)lr, (std::int32_t)lc, dlist, bs);
            if (sb64) {
                ctx.cdefFbList[fbIdx].cdefCount = cdefCount;
            }
            if (cdefCount == 0) {
                ctx.skipCdefSeg[fbIdx] = 1;
                continue;
            }
            ctx.skipCdefSeg[fbIdx] = 0;

            const std::int32_t toff = kCdefVBorder * (fbr != 0);
            const std::int32_t loff = kCdefHBorder * (fbc != 0);
            const std::int32_t boff = kCdefVBorder * ((std::int32_t)fbr + vbStep < nvfb);
            const std::int32_t roff = kCdefHBorder * ((std::int32_t)fbc + hbStep < nhfb);

            std::uint8_t (*dir)[kCdefNblocks][kCdefNblocks] = &ctx.cdefDirData[fbIdx].dir;
            std::int32_t (*var)[kCdefNblocks][kCdefNblocks] = &ctx.cdefDirData[fbIdx].var;
            for (int pli = 0; pli < numPlanes; pli++) {
                const std::int32_t miWideL2 = kMiSizeLog2 - (pli == 0 ? 0 : 1);
                const std::int32_t miHighL2 = kMiSizeLog2 - (pli == 0 ? 0 : 1);
                const std::int32_t ysize = (nvb << miHighL2) + boff + toff;
                const std::int32_t xsize = (nhb << miWideL2) + roff + loff;

                // 8-bit content (RTC): filter via the boundary-aware hybrid;
                // otherwise the 16-bit sentinel gather
                const bool native8bit = !is16bit;
                if (!native8bit) {
                    bool needToReset =
                        toffPrev > toff || loffPrev > loff || ysize < ysizePrev || xsize < xsizePrev;
                    if (needToReset) {
                        std::uint16_t* p = &in[(-toffPrev * kCdefBstride - loffPrev)];
                        // the reference's (uint8_t)CDEF_VERY_LARGE truncation:
                        // 0x7F7F -> 0x7F, so each uint16 cell becomes 0x7F7F
                        for (int r = 0; r < ysizePrev; r++) {
                            memset(p, kCdefVeryLarge & 0xFF, sizeof(p[0]) * xsizePrev);
                            p += kCdefBstride;
                        }
                    }
                    toffPrev = toff;
                    loffPrev = loff;
                    ysizePrev = ysize;
                    xsizePrev = xsize;

                    cdefCopySb8To16(&in[(-toff * kCdefBstride - loff)], kCdefBstride, ctx.src[pli],
                        (lr << miHighL2) - toff, (lc << miWideL2) - loff, ctx.strideSrc[pli], ysize, xsize, is16bit);
                }
                if (native8bit) {
                    // The bounded 8-bit kernel reads only +/-CDEF_HALO
                    const std::int32_t toff8 = kCdefHalo * (fbr != 0);
                    const std::int32_t loff8 = kCdefHalo * (fbc != 0);
                    const std::int32_t boff8 = kCdefHalo * ((std::int32_t)fbr + vbStep < nvfb);
                    const std::int32_t roff8 = kCdefHalo * ((std::int32_t)fbc + hbStep < nhfb);
                    const std::int32_t ysize8 = (nvb << miHighL2) + boff8 + toff8;
                    const std::int32_t xsize8 = (nhb << miWideL2) + roff8 + loff8;
                    cdefCopyRect8(&in8[(-toff8 * kCdefBstride - loff8)], kCdefBstride, ctx.src[pli],
                        (lr << miHighL2) - toff8, (lc << miWideL2) - loff8, ctx.strideSrc[pli], ysize8, xsize8);
                }

                std::uint8_t subsamplingFactor = cdefCtrls->subsamplingFactor;
                // Cap the subsampling for certain block sizes
                // (plane_bsize per the reference's hardcoded 4:2:0:
                // luma BLOCK_8X8 = 3, chroma BLOCK_4X4 = 0,
                // definitions.h:883-:905 ordinals)
                const int planeBsize = (pli == 0) ? 3 : 0;
                switch (planeBsize) {
                case 3: subsamplingFactor = subsamplingFactor < 4 ? subsamplingFactor : 4; break;
                case 2: // BLOCK_8X4
                case 1: // BLOCK_4X8
                    subsamplingFactor = subsamplingFactor < 2 ? subsamplingFactor : 2;
                    break;
                case 0: subsamplingFactor = subsamplingFactor < 1 ? subsamplingFactor : 1; break;
                default: break;
                }

                // first cdef stage
                for (int gi = 0; gi < firstPassFsNum; gi++) {
                    // Check if chroma filter is set to be tested
                    if (pli && (cdefCtrls->defaultFirstPassFsUv[gi] == -1)) {
                        ctx.mseSeg[1][fbIdx][gi] = kDefaultMseUv * 64;
                        continue;
                    }

                    if (native8bit) {
                        cdefFilterFbLbd(reinterpret_cast<std::uint8_t*>(tmpDst), 0, in8, toff == 0, loff == 0,
                            boff == 0, roff == 0, nvb << miHighL2, nhb << miWideL2, pli == 0 ? 0 : 1,
                            pli == 0 ? 0 : 1, *dir, &dirinit, *var, pli, dlist, cdefCount,
                            cdefCtrls->defaultFirstPassFs[gi], damping, coeffShift, subsamplingFactor);
                    } else {
                        cdefFilterFb(is16bit ? nullptr : reinterpret_cast<std::uint8_t*>(tmpDst),
                            is16bit ? tmpDst : nullptr, 0, in, pli == 0 ? 0 : 1, pli == 0 ? 0 : 1, *dir, &dirinit,
                            *var, pli, dlist, cdefCount, cdefCtrls->defaultFirstPassFs[gi], damping, coeffShift,
                            subsamplingFactor);
                    }
                    const std::uint64_t currMse = computeCdefDist(ctx.ref[pli],
                        (lr << miHighL2) * ctx.strideRef[pli] + (lc << miWideL2), ctx.strideRef[pli],
                        reinterpret_cast<const std::uint8_t*>(tmpDst), dlist, cdefCount,
                        static_cast<CdefBlockSize>(planeBsize), coeffShift, subsamplingFactor, is16bit);

                    if (pli < 2) {
                        ctx.mseSeg[pli][fbIdx][gi] = currMse * subsamplingFactor;
                    } else {
                        ctx.mseSeg[1][fbIdx][gi] += (currMse * subsamplingFactor);
                    }
                }

                // second cdef stage
                for (int gi = firstPassFsNum; gi < firstPassFsNum + defaultSecondPassFsNum; gi++) {
                    if (pli && (cdefCtrls->defaultSecondPassFsUv[gi - firstPassFsNum] == -1)) {
                        ctx.mseSeg[1][fbIdx][gi] = kDefaultMseUv * 64;
                        continue;
                    }

                    if (native8bit) {
                        cdefFilterFbLbd(reinterpret_cast<std::uint8_t*>(tmpDst), 0, in8, toff == 0, loff == 0,
                            boff == 0, roff == 0, nvb << miHighL2, nhb << miWideL2, pli == 0 ? 0 : 1,
                            pli == 0 ? 0 : 1, *dir, &dirinit, *var, pli, dlist, cdefCount,
                            cdefCtrls->defaultSecondPassFs[gi - firstPassFsNum], damping, coeffShift,
                            subsamplingFactor);
                    } else {
                        cdefFilterFb(is16bit ? nullptr : reinterpret_cast<std::uint8_t*>(tmpDst),
                            is16bit ? tmpDst : nullptr, 0, in, pli == 0 ? 0 : 1, pli == 0 ? 0 : 1, *dir, &dirinit,
                            *var, pli, dlist, cdefCount, cdefCtrls->defaultSecondPassFs[gi - firstPassFsNum],
                            damping, coeffShift, subsamplingFactor);
                    }
                    const std::uint64_t currMse = computeCdefDist(ctx.ref[pli],
                        (lr << miHighL2) * ctx.strideRef[pli] + (lc << miWideL2), ctx.strideRef[pli],
                        reinterpret_cast<const std::uint8_t*>(tmpDst), dlist, cdefCount,
                        static_cast<CdefBlockSize>(planeBsize), coeffShift, subsamplingFactor, is16bit);

                    if (pli < 2) {
                        ctx.mseSeg[pli][fbIdx][gi] = currMse * subsamplingFactor;
                    } else {
                        ctx.mseSeg[1][fbIdx][gi] += (currMse * subsamplingFactor);
                    }
                }
            }
        }
    }
}

// svt_av1_cdef_frame (enc_cdef.c:300-:697). 1:1; the walls are the
// CdefFrameCtx fields; cdefApplyFb / cdefCopySb8To16 / cdefCopyRect16 /
// cdefFillRect / cdefFilterFb are the landed primitives. CDEF geometry:
// CDEF_VBORDER 3, CDEF_HBORDER 8, CDEF_HALO 2 (kCdefBstride 144,
// kCdefVeryLarge).
void cdefFrame(CdefFrameCtx& ctx) {
    constexpr std::int32_t kMiSize64x64 = 16;
    constexpr std::int32_t kMiSizeLog2 = 2;
    constexpr std::int32_t kCdefVBorder = 3;
    constexpr std::int32_t kCdefHBorder = 8;
    constexpr std::int32_t kCdefHalo = 2;

    const bool is16bit = ctx.is16bit;
    const std::int32_t numPlanes = ctx.numPlanes;

    const std::int32_t coeffShift = ctx.encoderBitDepth - 8 > 0 ? ctx.encoderBitDepth - 8 : 0;
    const std::int32_t nvfb = (ctx.miRows + kMiSize64x64 - 1) / kMiSize64x64;
    const std::int32_t nhfb = (ctx.miCols + kMiSize64x64 - 1) / kMiSize64x64;
    const std::uint32_t cdefSize = (nhfb + 2) * 2;

    std::uint8_t* rowCdef = ctx.rowCdef;
    memset(rowCdef, 1, cdefSize);
    std::uint8_t* prevRowCdef = rowCdef + 1;
    std::uint8_t* currRowCdef = prevRowCdef + nhfb + 2;
    std::int32_t xdec[3];
    std::int32_t ydec[3];
    std::int32_t miWideL2[3];
    std::int32_t miHighL2[3];
    for (std::int32_t pli = 0; pli < numPlanes; pli++) {
        xdec[pli] = pli == 0 ? 0 : 1;
        ydec[pli] = pli == 0 ? 0 : 1;
        miWideL2[pli] = kMiSizeLog2 - xdec[pli];
        miHighL2[pli] = kMiSizeLog2 - ydec[pli];
    }

    const std::int32_t stride = (ctx.miCols << kMiSizeLog2) + 2 * kCdefHBorder;
    // Frame-level check: if every UV strength entry is 0, no chroma block
    // will ever be filtered.
    bool chromaFilterOff = (numPlanes > 1);
    if (chromaFilterOff) {
        for (std::int32_t i = 0; i < ctx.nbCdefStrengths; i++) {
            if (ctx.cdefUvStrength[i] != 0) {
                chromaFilterOff = false;
                break;
            }
        }
    }
    const std::int32_t activePlanes = chromaFilterOff ? 1 : numPlanes;
    std::int32_t mbmiCdefStrength = 0;
    for (std::int32_t fbr = 0; fbr < nvfb; fbr++) {
        std::int32_t cdefLeft = 1;
        for (std::int32_t fbc = 0; fbc < nhfb; fbc++) {
            currRowCdef[fbc] = 0;

            const CdefMb& mbmi = ctx.miGrid[kMiSize64x64 * fbr * ctx.miStride + kMiSize64x64 * fbc];

            if (ctx.nbCdefStrengths > 1) {
                mbmiCdefStrength = mbmi.cdefStrength;
            }
            const std::uint8_t cdefYStrength = ctx.cdefYStrength[mbmiCdefStrength];
            const std::uint8_t cdefUvStrength = ctx.cdefUvStrength[mbmiCdefStrength];
            if (cdefYStrength == 0 && cdefUvStrength == 0) {
                cdefLeft = 0;
                continue;
            }
            // Reuse the dlist/count computed by the search (SB=64 and the
            // frame was actually searched).
            const bool useDlistCache = ctx.sbSize == 64 && !ctx.useReferenceCdefFs && !ctx.useQpStrength;
            CdefList dlistLocal[kMiSize64x64 * kMiSize64x64];
            CdefList* dlist;
            std::int32_t cdefCount;
            if (useDlistCache) {
                cdefCount = ctx.cdefFbList[fbr * nhfb + fbc].cdefCount;
                dlist = ctx.cdefFbList[fbr * nhfb + fbc].dlist;
            } else {
                cdefCount = ctx.computeCdefList(
                    ctx.miGrid, ctx.miStride, fbr * kMiSize64x64, fbc * kMiSize64x64, dlistLocal, 12);
                dlist = dlistLocal;
            }
            if (cdefCount == 0) {
                cdefLeft = 0;
                continue;
            }

            currRowCdef[fbc] = 1;

            std::int32_t nhb, nvb;
            std::int32_t cstart = 0;
            if (!cdefLeft) {
                cstart = -kCdefHBorder;
            }

            nhb = ctx.miCols - kMiSize64x64 * fbc < kMiSize64x64 ? ctx.miCols - kMiSize64x64 * fbc : kMiSize64x64;
            nvb = ctx.miRows - kMiSize64x64 * fbr < kMiSize64x64 ? ctx.miRows - kMiSize64x64 * fbr : kMiSize64x64;
            // A fb edge is a frame boundary iff the fb sits in the first/last
            // fb row or column.
            const std::int32_t frameTop = (fbr == 0);
            const std::int32_t frameLeft = (fbc == 0);
            const std::int32_t frameBottom = (fbr == nvfb - 1);
            const std::int32_t frameRight = (fbc == nhfb - 1);

            std::int32_t dirinit = !(ctx.useReferenceCdefFs || ctx.useQpStrength);
            // When SB 128 is used, the search for certain blocks is skipped
            if (ctx.sbSize == 128) {
                const int bsize = mbmi.bsize;
                if (((fbc & 1) && (bsize == 15 || bsize == 14)) || ((fbr & 1) && (bsize == 15 || bsize == 13))) {
                    dirinit = 0;
                }
            }
            std::uint8_t (*dir)[kCdefNblocks][kCdefNblocks] = &ctx.cdefDirData[fbr * nhfb + fbc].dir;
            std::int32_t (*var)[kCdefNblocks][kCdefNblocks] = &ctx.cdefDirData[fbr * nhfb + fbc].var;
            for (std::int32_t pli = 0; pli < activePlanes; pli++) {
                const std::int32_t cdefStrength = pli ? cdefUvStrength : cdefYStrength;
                const std::int32_t damping = ctx.cdefDamping;

                const std::int32_t hsize = nhb << miWideL2[pli];
                const std::int32_t vsize = nvb << miHighL2[pli];
                const std::int32_t rend = (fbr == nvfb - 1) ? vsize : vsize + kCdefVBorder;
                const std::int32_t cend = (fbc == nhfb - 1) ? hsize : hsize + kCdefHBorder;

                const std::int32_t coffset = fbc * kMiSize64x64 << miWideL2[pli];
                std::uint8_t* recBuff = ctx.reconBuf[pli];
                const std::uint32_t recStride = ctx.reconStride[pli];

                // 8-bit pipeline: build src8 directly from recon and filter
                // via the boundary-aware hybrid
                if (!is16bit) {
                    cdefApplyFb(recBuff, recStride, ctx.src8, reinterpret_cast<std::uint8_t*>(ctx.linebuf[pli]),
                        reinterpret_cast<std::uint8_t*>(ctx.colbuf[pli]), prevRowCdef, fbr, fbc, nvfb, nhfb,
                        coffset, miHighL2[pli], miWideL2[pli], nhb, nvb, stride, rend, cend, cstart, cdefLeft, *dir,
                        &dirinit, *var, pli, dlist, cdefCount, cdefStrength, damping, coeffShift, xdec[pli],
                        ydec[pli], frameTop, frameLeft, frameBottom, frameRight);
                    continue;
                }

                // 16-bit sentinel path: copy the halo we need from recon
                const int haloCol = kCdefHBorder - kCdefHalo;
                const int haloRow = kCdefVBorder - kCdefHalo;
                const int topOff = haloRow * kCdefBstride + kCdefHBorder;
                const int tlOff = haloRow * kCdefBstride + haloCol;
                const int trOff = haloRow * kCdefBstride + kCdefHBorder + hsize;
                const int lrow = haloRow * stride;
                const int eCs = cstart < -kCdefHalo ? -kCdefHalo : cstart;
                const int eCright = cend > hsize + kCdefHalo ? hsize + kCdefHalo : cend;
                const int eRbot = rend > vsize + kCdefHalo ? vsize + kCdefHalo : rend;
                cdefCopySb8To16(&ctx.src16[kCdefVBorder * kCdefBstride + kCdefHBorder + eCs], kCdefBstride, recBuff,
                    (kMiSize64x64 << miHighL2[pli]) * fbr, coffset + eCs, recStride, eRbot, eCright - eCs, is16bit);
                if (!prevRowCdef[fbc]) {
                    cdefCopySb8To16(&ctx.src16[topOff], kCdefBstride, recBuff,
                        (kMiSize64x64 << miHighL2[pli]) * fbr - kCdefHalo, coffset, recStride, kCdefHalo, hsize,
                        is16bit);
                } else if (fbr > 0) {
                    cdefCopyRect16(&ctx.src16[topOff], kCdefBstride, &ctx.linebuf[pli][lrow + coffset], stride,
                        kCdefHalo, hsize);
                } else {
                    cdefFillRect(&ctx.src16[topOff], kCdefBstride, kCdefHalo, hsize, kCdefVeryLarge);
                }

                if (!prevRowCdef[fbc - 1]) {
                    cdefCopySb8To16(&ctx.src16[tlOff], kCdefBstride, recBuff,
                        (kMiSize64x64 << miHighL2[pli]) * fbr - kCdefHalo, coffset - kCdefHalo, recStride, kCdefHalo,
                        kCdefHalo, is16bit);
                } else if (fbr > 0 && fbc > 0) {
                    cdefCopyRect16(&ctx.src16[tlOff], kCdefBstride, &ctx.linebuf[pli][lrow + coffset - kCdefHalo],
                        stride, kCdefHalo, kCdefHalo);
                } else {
                    cdefFillRect(&ctx.src16[tlOff], kCdefBstride, kCdefHalo, kCdefHalo, kCdefVeryLarge);
                }

                if (!prevRowCdef[fbc + 1]) {
                    cdefCopySb8To16(&ctx.src16[trOff], kCdefBstride, recBuff,
                        (kMiSize64x64 << miHighL2[pli]) * fbr - kCdefHalo, coffset + hsize, recStride, kCdefHalo,
                        kCdefHalo, is16bit);
                } else if (fbr > 0 && fbc < nhfb - 1) {
                    cdefCopyRect16(&ctx.src16[trOff], kCdefBstride, &ctx.linebuf[pli][lrow + coffset + hsize], stride,
                        kCdefHalo, kCdefHalo);
                } else {
                    cdefFillRect(&ctx.src16[trOff], kCdefBstride, kCdefHalo, kCdefHalo, kCdefVeryLarge);
                }

                if (cdefLeft) {
                    cdefCopyRect16(&ctx.src16[haloCol], kCdefBstride, &ctx.colbuf[pli][haloCol], kCdefHBorder,
                        rend + kCdefVBorder, kCdefHalo);
                }

                // Save pixels for the fb to the right.
                if (fbc < nhfb - 1) {
                    cdefCopyRect16(&ctx.colbuf[pli][haloCol], kCdefHBorder, &ctx.src16[hsize + haloCol],
                        kCdefBstride, rend + kCdefVBorder, kCdefHalo);
                }

                if (fbr < nvfb - 1) {
                    cdefCopySb8To16(&ctx.linebuf[pli][coffset], stride, recBuff,
                        (kMiSize64x64 << miHighL2[pli]) * (fbr + 1) - kCdefVBorder, coffset, recStride, kCdefVBorder,
                        hsize, is16bit);
                }

                if (frameTop) {
                    cdefFillRect(&ctx.src16[haloRow * kCdefBstride + haloCol], kCdefBstride, kCdefHalo,
                        hsize + 2 * kCdefHalo, kCdefVeryLarge);
                }
                if (frameLeft) {
                    cdefFillRect(&ctx.src16[haloRow * kCdefBstride + haloCol], kCdefBstride, vsize + 2 * kCdefHalo,
                        kCdefHalo, kCdefVeryLarge);
                }
                if (frameBottom) {
                    cdefFillRect(&ctx.src16[(vsize + kCdefVBorder) * kCdefBstride + haloCol], kCdefBstride,
                        kCdefHalo, hsize + 2 * kCdefHalo, kCdefVeryLarge);
                }
                if (frameRight) {
                    cdefFillRect(&ctx.src16[haloRow * kCdefBstride + kCdefHBorder + hsize], kCdefBstride,
                        vsize + 2 * kCdefHalo, kCdefHalo, kCdefVeryLarge);
                }
                // dir/var init for the non-searched frames
                if (cdefStrength || !dirinit) {
                    cdefFilterFb(is16bit ? nullptr
                                         : &recBuff[recStride * (kMiSize64x64 * fbr << miHighL2[pli]) +
                                                   (fbc * kMiSize64x64 << miWideL2[pli])],
                        is16bit ? &reinterpret_cast<std::uint16_t*>(recBuff)[recStride *
                                                                                 (kMiSize64x64 * fbr << miHighL2[pli]) +
                                                                             (fbc * kMiSize64x64 << miWideL2[pli])]
                                : nullptr, recStride, &ctx.src16[kCdefVBorder * kCdefBstride + kCdefHBorder],
                        xdec[pli], ydec[pli], *dir, &dirinit, *var, pli, dlist, cdefCount, cdefStrength, damping,
                        coeffShift, 1); // no subsampling
                }
            }
            cdefLeft = 1;
        }
        {
            std::uint8_t* tmp = prevRowCdef;
            prevRowCdef = currRowCdef;
            currRowCdef = tmp;
        }
    }
}

// finish_cdef_search (enc_cdef.c:897-:1094). 1:1; the lambda-assign wall
// is threaded (fullLambda - only full_lambda is consumed), the
// RDCOST/av1_cost_literal macros carried locally (rd_cost.h:36-38 /
// md_rate_estimation.h:31: RPOT(R*RM, 9) + D << 7 / n << 9),
// CDEF_STRENGTH_BITS 6 (cdef.h:21), CDEF_DAMPING_FROM_QP (3 +
// (base_q_idx >> 6), enc_cdef.c:895), CDEF_MAX_STRENGTHS 16
// (definitions.h:1440).
void finishCdefSearch(CdefFinishCtx& ctx) {
    constexpr std::int32_t kMiSize64x64 = 16;
    constexpr int kCdefStrengthBits = 6;
    const int kAv1ProbCostShift = 9;
    const int kRddivBits = 7;

    // RDCOST(RM, R, D) (rd_cost.h:36-38): RPOT(R * RM, 9) + D << 7.
    auto rdcost = [&](std::int64_t rate, std::int64_t dist) -> std::int64_t {
        return ((rate * (std::int64_t)ctx.fullLambda + ((std::int64_t)1 << (kAv1ProbCostShift - 1))) >>
                   kAv1ProbCostShift) +
            (dist * ((std::int64_t)1 << kRddivBits));
    };
    // av1_cost_literal(n) (md_rate_estimation.h:31).
    auto costLiteral = [](int n) -> std::int64_t { return (std::int64_t)n << kAv1ProbCostShift; };

    const std::int32_t miRows = ctx.miRows;
    const std::int32_t miCols = ctx.miCols;

    std::uint64_t bestTotMse = (std::uint64_t)1 << 63;
    std::int32_t sbCount;
    const std::int32_t nvfb = (miRows + kMiSize64x64 - 1) / kMiSize64x64;
    const std::int32_t nhfb = (miCols + kMiSize64x64 - 1) / kMiSize64x64;

    const CdefSearchControls* cdefSearchCtrls = &ctx.ctrls;

    if (cdefSearchCtrls->useQpStrength) {
        const bool allintra = ctx.allintra;
        const std::uint8_t scClass1 = ctx.scClass1;
        const std::uint8_t scClass5 = ctx.scClass5;
        const int sc = allintra ? scClass5 : scClass1;
        std::int32_t predY, predUv;
        PickQpParams p;
        p.baseQIdx = ctx.baseQIdx;
        p.bitDepth = ctx.is16bitPipeline ? 10 : 8;
        p.isKeyFrame = ctx.qpIsKeyFrame;
        p.intraOnly = ctx.qpIntraOnly;
        p.isScreenContent = sc != 0;
        p.acQuantQtx = ctx.acQuantQtx;
        pickCdefFromQp(p, &predY, &predUv);
        ctx.cdefBits = 0;
        ctx.nbCdefStrengths = 1;
        ctx.cdefYStrength[0] = static_cast<std::uint8_t>(predY);
        ctx.cdefUvStrength[0] = static_cast<std::uint8_t>(predUv);
        ctx.cdefDamping = static_cast<std::uint8_t>(3 + ((ctx.baseQIdx) >> 6));
        // nb_cdef_strengths==1 -> apply uses strength index 0 for every SB;
        // no per-SB cdef_strength propagation needed.
        return;
    }

    const int firstPassFsNum = cdefSearchCtrls->firstPassFsNum;
    const int defaultSecondPassFsNum = cdefSearchCtrls->defaultSecondPassFsNum;

    ctx.cdefBits = 0;
    ctx.nbCdefStrengths = 1;
    ctx.cdefYStrength[0] = static_cast<std::uint8_t>(cdefSearchCtrls->predYF);
    ctx.cdefUvStrength[0] = static_cast<std::uint8_t>(cdefSearchCtrls->predUvF);
    ctx.cdefDamping = static_cast<std::uint8_t>(3 + ((ctx.baseQIdx) >> 6));

    if (cdefSearchCtrls->useReferenceCdefFs) {
        return;
    }

    // Persistent per-pcs scratch: the per-sb mse pointer arrays
    // (pcs->cdef_mse_ptr, into the mse_seg blocks) and the sb_index list.
    std::int32_t* sbIndex = ctx.sbIndex;

    const std::int32_t startGi = 0;
    const std::int32_t endGi = firstPassFsNum + defaultSecondPassFsNum;
    std::int32_t nbStrengths;
    std::int32_t nbStrengthBits = 0;

    // svt_aom_lambda_assign(pcs, &fast, &full, bd, q, false) - the rc wall
    // is threaded; only full_lambda is consumed.
    sbCount = 0;

    for (std::int32_t fbr = 0; fbr < nvfb; ++fbr) {
        for (std::int32_t fbc = 0; fbc < nhfb; ++fbc) {
            const CdefMb& mbmi = ctx.miGrid[kMiSize64x64 * fbr * ctx.miStride + kMiSize64x64 * fbc];
            if (((fbc & 1) && (mbmi.bsize == 15 || mbmi.bsize == 14)) ||
                ((fbr & 1) && (mbmi.bsize == 15 || mbmi.bsize == 13))) {
                continue;
            }
            if (ctx.skipCdefSeg[fbr * nhfb + fbc]) {
                continue;
            }

            ctx.sbMsePtr[0][sbCount] = reinterpret_cast<std::uint64_t*>(ctx.mseSeg[0]) +
                (std::size_t)(fbr * nhfb + fbc) * kCdefTotalStrengths;
            ctx.sbMsePtr[1][sbCount] = reinterpret_cast<std::uint64_t*>(ctx.mseSeg[1]) +
                (std::size_t)(fbr * nhfb + fbc) * kCdefTotalStrengths;
            sbIndex[sbCount] = kMiSize64x64 * fbr * ctx.miStride + kMiSize64x64 * fbc;
            sbCount++;
        }
    }

    // Scale down the cost of the (0,0) filter strength to bias selection
    // towards off.  When off, we can save the cost of the application
    if (ctx.zeroFsCostBias) {
        const bool is16bit = ctx.is16bitPipeline;
        std::uint16_t factor;
        for (std::int32_t i = 0; i < sbCount; i++) {
            if (is16bit) {
                factor = ctx.zeroFsCostBias;
                if (ctx.sbMsePtr[0][i][0] < 5000) {
                    factor = factor - 10 < 64 ? factor - 10 : 64;
                } else if (ctx.sbMsePtr[0][i][0] < 10000) {
                    factor = factor - 5 < 64 ? factor - 5 : 64;
                } else if (ctx.sbMsePtr[0][i][0] > 25000) {
                    factor = factor + 1 < 64 ? factor + 1 : 64;
                }
                ctx.sbMsePtr[0][i][0] = (factor * ctx.sbMsePtr[0][i][0]) >> 6;

                factor = ctx.zeroFsCostBias;
                if (ctx.sbMsePtr[1][i][0] < 5000) {
                    factor = factor - 10 < 64 ? factor - 10 : 64;
                } else if (ctx.sbMsePtr[1][i][0] < 10000) {
                    factor = factor - 5 < 64 ? factor - 5 : 64;
                } else if (ctx.sbMsePtr[1][i][0] > 25000) {
                    factor = factor + 1 < 64 ? factor + 1 : 64;
                }
                ctx.sbMsePtr[1][i][0] = (factor * ctx.sbMsePtr[1][i][0]) >> 6;
            } else {
                factor = ctx.zeroFsCostBias;
                if (ctx.sbMsePtr[0][i][0] > 25000) {
                    factor = factor + 2 < 64 ? factor + 2 : 64;
                } else if (ctx.sbMsePtr[0][i][0] > 10000) {
                    factor = factor + 1 < 64 ? factor + 1 : 64;
                }
                ctx.sbMsePtr[0][i][0] = (factor * ctx.sbMsePtr[0][i][0]) >> 6;

                factor = ctx.zeroFsCostBias;
                if (ctx.sbMsePtr[1][i][0] > 25000) {
                    factor = factor + 2 < 64 ? factor + 2 : 64;
                } else if (ctx.sbMsePtr[1][i][0] > 10000) {
                    factor = factor + 1 < 64 ? factor + 1 : 64;
                }
                ctx.sbMsePtr[1][i][0] = (factor * ctx.sbMsePtr[1][i][0]) >> 6;
            }
        }
    }

    // Compute cost of (strength=0) to derive pcs->cdef_dist_dev
    std::int64_t zeroDist = 0;
    for (std::int32_t i = 0; i < sbCount; i++) {
        zeroDist += ctx.sbMsePtr[0][i][0] + ctx.sbMsePtr[1][i][0];
    }
    const std::uint64_t zeroCost = (std::uint64_t)rdcost(costLiteral(kCdefStrengthBits * 2), zeroDist << 4);

    // Search for different numbers of signalling bits
    std::int32_t bestLev0[kCdefMaxStrengths] = {0};
    std::int32_t bestLev1[kCdefMaxStrengths] = {0};
    for (std::int32_t i = 0; i <= 3; i++) {
        nbStrengths = 1 << i;
        const std::uint64_t totMse = (std::uint64_t)cdefJointStrengthSearchDual(
            bestLev0, bestLev1, nbStrengths, ctx.sbMsePtr, sbCount, startGi, endGi);

        const int totalBits = sbCount * i + nbStrengths * kCdefStrengthBits * 2;
        const std::uint64_t cost = (std::uint64_t)rdcost(costLiteral(totalBits), totMse * 16);
        if (cost < bestTotMse) {
            bestTotMse = cost;
            nbStrengthBits = i;
            for (std::int32_t j = 0; j < 1 << nbStrengthBits; j++) {
                ctx.cdefYStrength[j] = static_cast<std::uint8_t>(bestLev0[j]);
                ctx.cdefUvStrength[j] = static_cast<std::uint8_t>(
                    cdefSearchCtrls->uvFromY ? bestLev0[j] : bestLev1[j]);
            }
        }
    }

    ctx.cdefDistDev = zeroCost == 0 ? 0 : (std::int32_t)(1000 - ((1000 * bestTotMse) / zeroCost));
    nbStrengths = 1 << nbStrengthBits;

    ctx.cdefBits = static_cast<std::uint8_t>(nbStrengthBits);
    ctx.nbCdefStrengths = nbStrengths;

    // Assign each filter block its best strength index
    for (std::int32_t i = 0; i < sbCount; i++) {
        std::int32_t bestGi = 0;
        std::uint64_t bestMse = (std::uint64_t)1 << 63;
        for (std::int32_t gi = 0; gi < ctx.nbCdefStrengths; gi++) {
            const std::uint64_t curr =
                ctx.sbMsePtr[0][i][ctx.cdefYStrength[gi]] + ctx.sbMsePtr[1][i][ctx.cdefUvStrength[gi]];
            if (curr < bestMse) {
                bestGi = gi;
                bestMse = curr;
            }
        }
        propagateCdefStrength(ctx.miGrid, ctx.miStride, sbIndex[i], ctx.miGrid[sbIndex[i]].bsize,
            static_cast<std::int8_t>(bestGi));
    }

    // Map search indices back to actual filter strengths
    std::int32_t filterMap[kCdefTotalStrengths] = {0};
    for (std::int32_t i = 0; i < firstPassFsNum; i++) {
        filterMap[i] = cdefSearchCtrls->defaultFirstPassFs[i];
    }
    for (std::int32_t i = 0; i < defaultSecondPassFsNum; i++) {
        filterMap[firstPassFsNum + i] = cdefSearchCtrls->defaultSecondPassFs[i];
    }

    for (std::int32_t i = 0; i < ctx.nbCdefStrengths; i++) {
        ctx.cdefYStrength[i] = static_cast<std::uint8_t>(filterMap[ctx.cdefYStrength[i]]);
        ctx.cdefUvStrength[i] = static_cast<std::uint8_t>(filterMap[ctx.cdefUvStrength[i]]);
    }

    ctx.cdefDamping = static_cast<std::uint8_t>(3 + ((ctx.baseQIdx) >> 6));
}

// get_own_recon (rest_process.c:147-:190). 1:1: the no-boundary early
// return, then the row copies - luma full height, chroma >> ss, widths
// scaled by is_16bit.
CdefPic getOwnRecon(const CdefScsWalls& scs, const CdefPic& reconPic, const CdefPic& orgRecFrame,
    bool useBoundariesInRestSearch, bool is16bit) {
    // if boundaries are not used, don't need to copy pic to new buffer
    if (!useBoundariesInRestSearch) {
        return reconPic;
    }

    const std::uint32_t ssX = scs.subsamplingX;
    const std::uint32_t ssY = scs.subsamplingY;

    const int useHighbd = is16bit ? 1 : 0;
    const std::int32_t orgStrideY = orgRecFrame.stride[0] << useHighbd;
    const std::int32_t orgStrideCb = orgRecFrame.stride[1] << useHighbd;
    const std::int32_t orgStrideCr = orgRecFrame.stride[2] << useHighbd;

    const std::int32_t recStrideY = reconPic.stride[0] << useHighbd;
    const std::int32_t recStrideCb = reconPic.stride[1] << useHighbd;
    const std::int32_t recStrideCr = reconPic.stride[2] << useHighbd;

    std::uint8_t* orgPtr = orgRecFrame.buffer[0];
    std::uint8_t* orgPtrCb = orgRecFrame.buffer[1];
    std::uint8_t* orgPtrCr = orgRecFrame.buffer[2];

    const std::uint8_t* recPtr = reconPic.buffer[0];
    const std::uint8_t* recPtrCb = reconPic.buffer[1];
    const std::uint8_t* recPtrCr = reconPic.buffer[2];

    const int recWidth = reconPic.width << useHighbd;

    for (int r = 0; r < reconPic.height; ++r) {
        memcpy(orgPtr + r * orgStrideY, recPtr + r * recStrideY, recWidth);
    }

    for (int r = 0; r < (reconPic.height >> ssY); ++r) {
        memcpy(orgPtrCb + r * orgStrideCb, recPtrCb + r * recStrideCb, recWidth >> ssX);
        memcpy(orgPtrCr + r * orgStrideCr, recPtrCr + r * recStrideCr, recWidth >> ssX);
    }
    return orgRecFrame;
}

// copy_statistics_to_ref_obj_ect (rest_process.c:192-:251). 1:1.
void copyStatisticsToRefObjEct(const RestHdr& hdr, const RestCmWalls& cmWalls, const RestPcsWalls& pcs,
    RestRefStat& obj) {
    obj.intraCodedArea = (std::uint8_t)pcs.intraCodedArea;
    obj.skipCodedArea = (std::uint8_t)pcs.skipCodedArea;
    obj.hpCodedArea = (std::uint8_t)pcs.hpCodedArea;
    obj.isMfmvUsed = hdr.useRefFrameMvs;

    obj.filterLevel[0] = hdr.filterLevel[0];
    obj.filterLevel[1] = hdr.filterLevel[1];
    obj.filterLevelU = hdr.filterLevelU;
    obj.filterLevelV = hdr.filterLevelV;
    obj.dlfDistDev = pcs.dlfDistDev;
    obj.cdefDistDev = pcs.cdefDistDev;

    obj.refCdefStrengthsNum = pcs.nbCdefStrengths;
    for (std::uint32_t i = 0; i < pcs.nbCdefStrengths; i++) {
        obj.refCdefStrengths[0][i] = hdr.cdefYStrength[i];
        obj.refCdefStrengths[1][i] = hdr.cdefUvStrength[i];
    }
    for (std::uint32_t sbIndex = 0; sbIndex < pcs.b64TotalCount; ++sbIndex) {
        obj.sbIntra[sbIndex] = pcs.sbIntra[sbIndex];
        obj.sbSkip[sbIndex] = pcs.sbSkip[sbIndex];
        obj.sb64x64Mvp[sbIndex] = pcs.sb64x64Mvp[sbIndex];
        obj.sbMe64x64Dist[sbIndex] = pcs.me64x64Distortion[sbIndex];
        obj.sbMe8x8CostVar[sbIndex] = pcs.me8x8CostVariance[sbIndex];
        obj.sbMinSqSize[sbIndex] = pcs.sbMinSqSize[sbIndex];
        obj.sbMaxSqSize[sbIndex] = pcs.sbMaxSqSize[sbIndex];
    }
    obj.tmpLayerIdx = pcs.temporalLayerIndex;
    obj.isSceneChange = pcs.sceneChangeFlag;

    if (cmWalls.mfmvEnabled || !pcs.isNotScaled) {
        obj.frameType = hdr.frameType;
        obj.orderHint = pcs.curOrderHint;
        memcpy(obj.refOrderHint, pcs.refOrderHint, sizeof(obj.refOrderHint));
    }
    // Copy the prev frame wn filter coeffs
    if (cmWalls.wnFilterEnabled && cmWalls.wnUsePrevFrameCoeffs) {
        for (std::int32_t plane = 0; plane < 3; ++plane) {
            const std::int32_t ntiles = pcs.unitsPerTile[plane];
            const bool unitInfoValid = pcs.frameRestorationType[plane] != restoration::RESTORE_NONE ||
                (pcs.sliceType != 0 && pcs.enableRestoration && !hdr.allowIntrabc);
            for (std::int32_t u = 0; u < ntiles; ++u) {
                if (!unitInfoValid) {
                    obj.unitInfo[plane][u].restorationType = restoration::RESTORE_NONE;
                    continue;
                }
                obj.unitInfo[plane][u].restorationType = pcs.unitInfo[plane][u].restorationType;
                if (pcs.unitInfo[plane][u].restorationType == restoration::RESTORE_WIENER) {
                    obj.unitInfo[plane][u].wienerInfo = pcs.unitInfo[plane][u].wienerInfo;
                }
            }
        }
    }
}

} // namespace cdef_driver
} // namespace l10_filters