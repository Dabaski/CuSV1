// l14_utility - the platform surfaces of svt_time.c and svt_malloc.h
// (A13, TM1 + AL1). References:
// third_party/SVT-AV1/Source/Lib/Codec/svt_time.c,
// third_party/SVT-AV1/Source/Lib/Codec/svt_malloc.h.
//
// LANDED (TM1): compute_overall_elapsed_time_ms (svt_time.c:31-:36).
// PARKED (named): svt_av1_get_time :38-:55 - the OS clock shell
// (_ftime_s / clock_gettime / gettimeofday).
//
// LANDED (AL1): the svt_malloc.h inline surface - align_addr :33,
// check_size_argument_overflow :36-:48, get_aligned_malloc_size :50-:52,
// the malloc-address location trio :54-:66, svt_aom_memalign :68-:82,
// svt_aom_malloc :84-:86, svt_aom_free :88-:93, svt_aom_memset16
// :95-:102, with ADDRESS_STORAGE_SIZE / DEFAULT_ALIGNMENT /
// AOM_MAX_ALLOCABLE_MEMORY (:29-:31).
// BRIEF CORRECTION (measured): the brief says the wrappers align to 32
// bytes - svt_aom_malloc aligns to DEFAULT_ALIGNMENT = 2 * sizeof(void*)
// = 16 on x64 (svt_malloc.h:30), and ALVALUE (the EB_MALLOC_ALIGNED
// alignment) is 64, not 32 (definitions.h:1966).
//
// PARKED (named): the EB_* macro family :139-:335 - macro-over-malloc
// shell bound to the debug tracking; the DEBUG_MEMORY_USAGE block of
// svt_malloc.c :20-:332 - debug-only (svt_malloc.h:23-:25 defines it
// only #ifndef NDEBUG, so the Release build compiles it out) + the
// svt_threads mutex wall; svt_print_alloc_fail_impl (svt_malloc.c:16-:18)
// - the SVT_FATAL logging wall (svt_log.c).
#pragma once

#include <cstddef>
#include <cstdint>

namespace l14_utility {

// compute_overall_elapsed_time_ms - svt_time.c:31-:36 verbatim.
double computeOverallElapsedTimeMs(std::uint64_t start_seconds, std::uint64_t start_useconds,
                                   std::uint64_t finish_seconds, std::uint64_t finish_useconds);

// ADDRESS_STORAGE_SIZE / DEFAULT_ALIGNMENT / AOM_MAX_ALLOCABLE_MEMORY -
// svt_malloc.h:29-:31.
constexpr std::size_t kAddressStorageSize = sizeof(std::size_t);
constexpr std::size_t kDefaultAlignment = 2 * sizeof(void*);
constexpr std::uint64_t kAomMaxAllocableMemory = 8589934592ull;  // 8 GB

// align_addr - svt_malloc.h:33 verbatim: round addr up to the boundary.
void* alignAddr(const void* addr, std::size_t align);

// check_size_argument_overflow - svt_malloc.h:36-:48 verbatim: 1 ok,
// 0 overflow.
std::int32_t checkSizeArgumentOverflow(std::uint64_t nmemb, std::uint64_t size);

// get_aligned_malloc_size - svt_malloc.h:50-:52 verbatim.
std::size_t getAlignedMallocSize(std::size_t size, std::size_t align);

// get/set/get_actual_malloc_address - svt_malloc.h:54-:66 verbatim: the
// raw malloc base stored one size_t below the aligned pointer.
std::size_t* getMallocAddressLocation(void* const mem);
void setActualMallocAddress(void* const mem, const void* const malloc_addr);
void* getActualMallocAddress(void* const mem);

// svt_aom_memalign - svt_malloc.h:68-:82 verbatim. Returns nullptr on
// the allocation-size overflow check.
void* svtAomMemalign(std::size_t align, std::size_t size);

// svt_aom_malloc - svt_malloc.h:84-:86 verbatim.
void* svtAomMalloc(std::size_t size);

// svt_aom_free - svt_malloc.h:88-:93 verbatim: recover the raw base and
// free it.
void svtAomFree(void* memblk);

// svt_aom_memset16 - svt_malloc.h:95-:102 verbatim: uint16 fill,
// returns dest.
void* svtAomMemset16(void* dest, std::int32_t val, std::size_t length);

}  // namespace l14_utility
