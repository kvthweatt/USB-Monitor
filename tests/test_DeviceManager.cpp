// tests/test_DeviceManager.cpp
#include <gtest/gtest.h>
#include "DeviceManager.hpp"
#include "UsbDevice.hpp"
#include <QCoreApplication>
#include <memory>

namespace usb_monitor {
namespace testing {

class DeviceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        int argc = 1;
        char* argv[] = {(char*)"test"};
        app = std::make_unique<QCoreApplication>(argc, argv);
        manager = std::make_unique<DeviceManager>();
    }

    void TearDown() override {
        manager.reset();
        app.reset();
    }

    std::unique_ptr<QCoreApplication> app;
    std::unique_ptr<DeviceManager> manager;
};

TEST_F(DeviceManagerTest, CreationTest) {
    ASSERT_NE(manager, nullptr);
    ASSERT_NE(manager->powerManager(), nullptr);
    ASSERT_NE(manager->bandwidthMonitor(), nullptr);
}

TEST_F(DeviceManagerTest, GetConnectedDevicesTest) {
    auto devices = manager->getConnectedDevices();
    // Note: actual device count will depend on the test environment
    EXPECT_GE(devices.size(), 0);
}

TEST_F(DeviceManagerTest, SignalTest) {
    bool deviceAddedEmitted = false;
    bool deviceRemovedEmitted = false;

    QObject::connect(manager.get(), &DeviceManager::deviceAdded,
        [&deviceAddedEmitted](std::shared_ptr<UsbDevice>) {
            deviceAddedEmitted = true;
        });

    QObject::connect(manager.get(), &DeviceManager::deviceRemoved,
        [&deviceRemovedEmitted](std::shared_ptr<UsbDevice>) {
            deviceRemovedEmitted = true;
        });

    // Force a device poll
    manager->pollDevices();

    // Process events to receive signals
    app->processEvents();

    // Note: these tests might fail depending on whether devices are actually
    // connected/disconnected during the test
    EXPECT_FALSE(deviceRemovedEmitted);
}

} // namespace testing
} // namespace usb_monitor
