#include <gtest/gtest.h>
#include "../core/BandwidthMonitor.hpp"

namespace usb_monitor {
namespace testing {

class BandwidthMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }

    void TearDown() override {
        // Cleanup code
    }
};

TEST_F(BandwidthMonitorTest, BasicTest) {
    EXPECT_TRUE(true);
}

} // namespace testing
}
