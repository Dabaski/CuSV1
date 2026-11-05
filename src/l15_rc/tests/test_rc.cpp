#include <doctest.h>
#include <cstdint>
#include <fstream>
#include <regex>
#include <string>

#include "rc_tables.h"

// RC1 verification: the transcribed tables are diff-verified entry-by-entry
// against the REFERENCE FILE ITSELF (parsed at runtime from REPO_ROOT) - the
// transcription carries zero hand-typed values, so the check is mechanical
// both ways. The paired-metric caveat applies and is stated in the register:
// this slice moves the coverage number by transcription, not by algorithm.

TEST_CASE("all 18 rc tables match the reference entry-for-entry") {
    std::ifstream f(REPO_ROOT "/third_party/SVT-AV1/Source/Lib/Codec/rc_tables.h", std::ios::binary);
    REQUIRE(f.good());
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::regex tableRe(R"(static const int (\w+)\[QINDEX_RANGE\] = \{)");
    auto begin = std::sregex_iterator(text.begin(), text.end(), tableRe);
    REQUIRE(begin != std::sregex_iterator());

    int tables = 0;
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        const std::string name = (*it)[1].str();
        const std::size_t start = it->position(0) + it->length(0);
        const std::size_t end = text.find("};", start);
        REQUIRE(end != std::string::npos);
        const std::string body = text.substr(start, end - start);

        std::vector<std::int32_t> ref;
        std::regex numRe(R"(-?\d+)");
        for (auto n = std::sregex_iterator(body.begin(), body.end(), numRe); n != std::sregex_iterator(); ++n)
            ref.push_back(std::atoi(n->str().c_str()));

        INFO(name);
        CHECK(ref.size() == 256);

        std::int32_t len = 0;
        const std::int32_t* ours = l15_rc::findTable(name.c_str(), len);
        REQUIRE_MESSAGE(ours != nullptr, "port table missing");
        REQUIRE(len == 256);

        std::int32_t mismatches = 0;
        for (std::size_t i = 0; i < ref.size(); ++i)
            if (ours[i] != ref[i]) ++mismatches;
        CHECK(mismatches == 0);
        ++tables;
    }
    CHECK(tables == 18);
}
