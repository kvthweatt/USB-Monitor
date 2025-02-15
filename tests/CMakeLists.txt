# tests/CMakeLists.txt

# Find GTest package
find_package(GTest REQUIRED)

set(TEST_SOURCES
    main.cpp
    test_UsbDevice.cpp
    test_DeviceManager.cpp
    test_PowerManager.cpp
    test_BandwidthMonitor.cpp
)

add_executable(usb_monitor_tests ${TEST_SOURCES})

target_link_libraries(usb_monitor_tests PRIVATE
    GTest::GTest
    GTest::Main
    Qt5::Core
    Qt5::Widgets
    ${LIBUSB_LIBRARIES}
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    SQLite::SQLite3
)

# Enable CTest integration
include(GoogleTest)
gtest_discover_tests(usb_monitor_tests)
