#include <doctest.h>
#include <platform.h>

// svt_time.c:31-:36. The +0.5 fudge is part of the verbatim value.
TEST_CASE("computeOverallElapsedTimeMs folds the seconds and useconds") {
    CHECK(l14_utility::computeOverallElapsedTimeMs(0, 0, 1, 500000) == doctest::Approx(1500.5));
    CHECK(l14_utility::computeOverallElapsedTimeMs(10, 0, 12, 0) == doctest::Approx(2000.5));
    CHECK(l14_utility::computeOverallElapsedTimeMs(0, 0, 0, 0) == doctest::Approx(0.5));
    // No borrow: the reference subtracts the raw useconds.
    CHECK(l14_utility::computeOverallElapsedTimeMs(1, 900000, 2, 100000) == doctest::Approx(200.5));
    CHECK(l14_utility::computeOverallElapsedTimeMs(5, 0, 4, 0) == doctest::Approx(-1000.0 + 0.5));
}

TEST_CASE("alignAddr rounds up to the boundary") {
    CHECK(l14_utility::alignAddr((const void*)0x1013, 16) == (void*)0x1020);
    CHECK(l14_utility::alignAddr((const void*)0x1010, 16) == (void*)0x1010);
    CHECK(l14_utility::alignAddr((const void*)0x1001, 64) == (void*)0x1040);
    CHECK(l14_utility::alignAddr((const void*)0x0, 8) == (void*)0x0);
}

TEST_CASE("checkSizeArgumentOverflow applies the 8 GB cap") {
    CHECK(l14_utility::checkSizeArgumentOverflow(1, 100) == 1);
    CHECK(l14_utility::checkSizeArgumentOverflow(0, 1000000000000ull) == 1);
    CHECK(l14_utility::checkSizeArgumentOverflow(1, 8589934592ull + 1) == 0);
    CHECK(l14_utility::checkSizeArgumentOverflow(2, 4294967296ull) == 1);
    CHECK(l14_utility::checkSizeArgumentOverflow(3, 4294967296ull) == 0);
}

TEST_CASE("memset16 fills and returns the destination") {
    std::uint16_t buf[7];
    void* ret = l14_utility::svtAomMemset16(buf, 0xABCD, 7);
    CHECK(ret == (void*)buf);
    for (int i = 0; i < 7; ++i) {
        CHECK(buf[i] == 0xABCD);
    }
    l14_utility::svtAomMemset16(buf, 0, 7);
    for (int i = 0; i < 7; ++i) {
        CHECK(buf[i] == 0);
    }
}

TEST_CASE("memalign returns aligned storage with a recoverable raw base") {
    const std::size_t aligns[5] = {8, 16, 32, 64, 128};
    const std::size_t sizes[5] = {1, 7, 16, 33, 1000};
    for (int ai = 0; ai < 5; ++ai) {
        for (int si = 0; si < 5; ++si) {
            void* p = l14_utility::svtAomMemalign(aligns[ai], sizes[si]);
            REQUIRE(p != nullptr);
            CHECK(reinterpret_cast<std::uintptr_t>(p) % aligns[ai] == 0);
            void* raw = l14_utility::getActualMallocAddress(p);
            CHECK(raw != nullptr);
            CHECK(reinterpret_cast<std::uintptr_t>(raw) < reinterpret_cast<std::uintptr_t>(p));
            l14_utility::svtAomFree(p);
        }
    }
}

TEST_CASE("svtAomMalloc aligns to DEFAULT_ALIGNMENT (16 on x64)") {
    for (int i = 0; i < 32; ++i) {
        void* p = l14_utility::svtAomMalloc(17 + i);
        REQUIRE(p != nullptr);
        CHECK(reinterpret_cast<std::uintptr_t>(p) % 16 == 0);
        l14_utility::svtAomFree(p);
    }
}

TEST_CASE("memalign returns null on the 8 GB overflow") {
    CHECK(l14_utility::svtAomMemalign(16, 8589934592ull) == nullptr);
    CHECK(l14_utility::svtAomMalloc(8589934592ull) == nullptr);
}

TEST_CASE("svtAomFree accepts null") {
    l14_utility::svtAomFree(nullptr);
    // The null free must leave the allocator usable: a real allocation and
    // free still succeed and keep the alignment contract.
    void* p = l14_utility::svtAomMalloc(64);
    REQUIRE(p != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % 16 == 0);
    l14_utility::svtAomFree(p);
}
