#include "forgekv/benchmark/output.hpp"

#include <gtest/gtest.h>

#include <string>

namespace forgekv::benchmark {
namespace {

TEST(BenchmarkOutputTest, JsonEscapesEveryAsciiControlCharacter) {
    std::string controls;
    for (char byte = 0; byte < 0x20; ++byte) controls.push_back(byte);

    EXPECT_EQ(json_escape(controls),
              "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
              "\\b\\t\\n\\u000b\\f\\r\\u000e\\u000f"
              "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
              "\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f");
}

TEST(BenchmarkOutputTest, JsonEscapesDelimitersAndPreservesUtf8Bytes) {
    EXPECT_EQ(json_escape("plain"), "plain");
    EXPECT_EQ(json_escape("\""), "\\\"");
    EXPECT_EQ(json_escape("\\"), "\\\\");
    EXPECT_EQ(json_escape("\xc3\xa9"), "\xc3\xa9");
}

}  // namespace
}  // namespace forgekv::benchmark
