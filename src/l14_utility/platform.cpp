#include <cstdlib>
#include <platform.h>

namespace l14_utility {

// compute_overall_elapsed_time_ms - svt_time.c:31-:36 verbatim. The
// useconds are subtracted raw (no borrow).
double computeOverallElapsedTimeMs(std::uint64_t start_seconds, std::uint64_t start_useconds,
                                   std::uint64_t finish_seconds, std::uint64_t finish_useconds) {
    const std::int64_t s_diff = (std::int64_t)finish_seconds - (std::int64_t)start_seconds,
                      u_diff = (std::int64_t)finish_useconds - (std::int64_t)start_useconds;
    return (double)s_diff * 1000.0 + (double)u_diff / 1000.0 + 0.5;
}

// align_addr - svt_malloc.h:33 verbatim.
void* alignAddr(const void* addr, std::size_t align) {
    return (void*)(((std::size_t)(addr) + ((align)-1)) & ~(std::size_t)((align)-1));
}

// check_size_argument_overflow - svt_malloc.h:36-:48 verbatim.
std::int32_t checkSizeArgumentOverflow(std::uint64_t nmemb, std::uint64_t size) {
    const std::uint64_t total_size = nmemb * size;
    if (nmemb == 0) {
        return 1;
    }
    if (size > kAomMaxAllocableMemory / nmemb) {
        return 0;
    }
    if (total_size != (std::size_t)total_size) {
        return 0;
    }
    return 1;
}

// get_aligned_malloc_size - svt_malloc.h:50-:52 verbatim.
std::size_t getAlignedMallocSize(std::size_t size, std::size_t align) {
    return size + align - 1 + kAddressStorageSize;
}

// svt_malloc.h:54-:66 verbatim.
std::size_t* getMallocAddressLocation(void* const mem) {
    return ((std::size_t*)mem) - 1;
}

void setActualMallocAddress(void* const mem, const void* const malloc_addr) {
    std::size_t* const malloc_addr_location = getMallocAddressLocation(mem);
    *malloc_addr_location = (std::size_t)malloc_addr;
}

void* getActualMallocAddress(void* const mem) {
    const std::size_t* const malloc_addr_location = getMallocAddressLocation(mem);
    return (void*)(*malloc_addr_location);
}

// svt_aom_memalign - svt_malloc.h:68-:82 verbatim.
void* svtAomMemalign(std::size_t align, std::size_t size) {
    void* x = NULL;
    const std::size_t aligned_size = getAlignedMallocSize(size, align);
    if (!checkSizeArgumentOverflow(1, aligned_size)) {
        return NULL;
    }
    void* const addr = std::malloc(aligned_size);
    if (addr) {
        x = alignAddr((std::uint8_t*)addr + kAddressStorageSize, align);
        setActualMallocAddress(x, addr);
    }
    return x;
}

// svt_aom_malloc - svt_malloc.h:84-:86 verbatim.
void* svtAomMalloc(std::size_t size) {
    return svtAomMemalign(kDefaultAlignment, size);
}

// svt_aom_free - svt_malloc.h:88-:93 verbatim.
void svtAomFree(void* memblk) {
    if (memblk) {
        void* addr = getActualMallocAddress(memblk);
        std::free(addr);
    }
}

// svt_aom_memset16 - svt_malloc.h:95-:102 verbatim.
void* svtAomMemset16(void* dest, std::int32_t val, std::size_t length) {
    std::size_t i;
    std::uint16_t* dest16 = (std::uint16_t*)dest;
    for (i = 0; i < length; i++) {
        *dest16++ = (std::uint16_t)val;
    }
    return dest;
}

}  // namespace l14_utility
