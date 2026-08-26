#include "forgekv/version.hpp"

#include <gtest/gtest.h>

TEST(VersionTest, ReportsProjectVersion) {
    EXPECT_EQ(forgekv::version(), "0.1.0");
}
