// l11_resize - the resize.c 1D kernel family (A11, AN21).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/resize.c :147-:489.
// LANDED: get_down2_length/get_down2_steps/down2_symeven/down2_symodd/
// choose_interp_filter/interpolate_core/interpolate/resize_multistep/
// fill_arr_to_col/fill_col_to_arr/resize_plane_c/resize_plane_horizontal
// + the five 64x8 interp filter tables (mechanically extracted).
// PARKED: highbd family :489-:781, save_YUV_to_file, pack/unpack_highbd,
// the svt_aom_resize_frame driver, analyze_hor_freq.
#pragma once

#include <cstdint>

namespace resize {

// get_down2_length resize.c:147
int getDown2Length(int length, int steps);
// get_down2_steps resize.c:154
int getDown2Steps(int inLength, int outLength);
// svt_av1_down2_symeven_c resize.c:170
void down2Symeven(const std::uint8_t* input, int length, std::uint8_t* output);
// down2_symodd resize.c:221
void down2Symodd(const std::uint8_t* input, int length, std::uint8_t* output);
// choose_interp_filter resize.c:272
const std::int16_t* chooseInterpFilter(int inLength, int outLength);
// svt_av1_interpolate_core_c resize.c:287
void interpolateCore(const std::uint8_t* input, int inLength, std::uint8_t* output, int outLength,
                     const std::int16_t* interpFilters);
// interpolate resize.c:360
void interpolate(const std::uint8_t* input, int inLength, std::uint8_t* output, int outLength);
// resize_multistep resize.c:366
void resizeMultistep(const std::uint8_t* input, int length, std::uint8_t* output, int olength, std::uint8_t* otmp);
// fill_arr_to_col resize.c:404
void fillArrToCol(std::uint8_t* img, int stride, int len, const std::uint8_t* arr);
// fill_col_to_arr resize.c:413
void fillColToArr(std::uint8_t* img, int stride, int len, std::uint8_t* arr);
// svt_av1_resize_plane_c resize.c:422; EbErrorType -> bool (deviation:
// EB_MALLOC_ARRAY -> new[], the EB_ErrorInsufficientResources branch is
// unreachable under throwing new).
bool resizePlaneC(const std::uint8_t* input, int height, int width, int inStride, std::uint8_t* output, int height2,
                  int width2, int outStride);
// svt_av1_resize_plane_horizontal resize.c:464; EbErrorType -> bool (same
// allocation deviation as resizePlaneC).
bool resizePlaneHorizontal(const std::uint8_t* input, int height, int width, int inStride, std::uint8_t* output,
                           int height2, int width2, int outStride);

// svt_av1_highbd_interpolate_core_c resize.c:489
void hbdInterpolateCore(const std::uint16_t* input, int inLength, std::uint16_t* output, int outLength, int bd,
                        const std::int16_t* interpFilters);
// highbd_interpolate resize.c:562
void hbdInterpolate(const std::uint16_t* input, int inLength, std::uint16_t* output, int outLength, int bd);
// svt_av1_highbd_down2_symeven_c resize.c:568
void hbdDown2Symeven(const std::uint16_t* input, int length, std::uint16_t* output, int bd);
// highbd_down2_symodd resize.c:619
void hbdDown2Symodd(const std::uint16_t* input, int length, std::uint16_t* output, int bd);
// highbd_resize_multistep resize.c:670
void hbdResizeMultistep(const std::uint16_t* input, int length, std::uint16_t* output, int olength,
                        std::uint16_t* otmp, int bd);
// highbd_fill_col_to_arr resize.c:707 (const arr: landed 8-bit fillColToArr shape)
void hbdFillColToArr(std::uint16_t* img, int stride, int len, std::uint16_t* arr);
// highbd_fill_arr_to_col resize.c:716
void hbdFillArrToCol(std::uint16_t* img, int stride, int len, const std::uint16_t* arr);
// svt_av1_highbd_resize_plane_c resize.c:725; EbErrorType -> bool (same
// allocation deviation as resizePlaneC).
bool hbdResizePlaneC(const std::uint16_t* input, int height, int width, int inStride, std::uint16_t* output,
                     int height2, int width2, int outStride, int bd);
// svt_av1_highbd_resize_plane_horizontal resize.c:761; EbErrorType -> bool.
bool hbdResizePlaneHorizontal(const std::uint16_t* input, int height, int width, int inStride, std::uint16_t* output,
                              int height2, int width2, int outStride, int bd);

} // namespace resize
