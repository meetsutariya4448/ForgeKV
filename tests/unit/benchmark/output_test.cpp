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

TEST(BenchmarkOutputTest, JsonEscapesInvalidUtf8Bytes) {
    EXPECT_EQ(json_escape(std::string("\xc3(", 2)), "\\u00c3(");
    EXPECT_EQ(json_escape(std::string("\xc0\xaf", 2)), "\\u00c0\\u00af");
    EXPECT_EQ(json_escape(std::string("\xed\xa0\x80", 3)),
              "\\u00ed\\u00a0\\u0080");
    EXPECT_EQ(json_escape(std::string("\xf4\x90\x80\x80", 4)),
              "\\u00f4\\u0090\\u0080\\u0080");
    EXPECT_EQ(json_escape(std::string("\xe2\x82", 2)), "\\u00e2\\u0082");
    EXPECT_EQ(json_escape("\xf0\x9f\x94\xa5"), "\xf0\x9f\x94\xa5");
}

}  // namespace
}  // namespace forgekv::benchmark
