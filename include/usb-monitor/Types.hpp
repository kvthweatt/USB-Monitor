#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace usb_monitor {

struct DeviceIdentifier {
    uint16_t vendorId;
    uint16_t productId;
    uint8_t busNumber;
    uint8_t deviceAddress;
    
    bool operator==(const DeviceIdentifier& other) const {
        return vendorId == other.vendorId &&
               productId == other.productId &&
               busNumber == other.busNumber &&
               deviceAddress == other.deviceAddress;
    }
};

struct PowerStats {
    double currentUsage;    // mA
    double voltage;         // V
    double powerUsage;      // mW
    bool selfPowered;
    uint16_t maxPower;      // mA
};

struct BandwidthStats {
    uint64_t bytesRead;
    uint64_t bytesWritten;
    double readSpeed;       // bytes/sec
    double writeSpeed;      // bytes/sec
    uint8_t speedClass;     // USB_SPEED_*
};

enum class DeviceClass {
    Unspecified = 0x00,
    Audio = 0x01,
    CDC = 0x02,
    HID = 0x03,
    Physical = 0x05,
    Image = 0x06,
    Printer = 0x07,
    MassStorage = 0x08,
    Hub = 0x09,
    CDC_Data = 0x0A,
    SmartCard = 0x0B,
    ContentSecurity = 0x0D,
    Video = 0x0E,
    PersonalHealthcare = 0x0F,
    AudioVideo = 0x10,
    Billboard = 0x11,
    TypeCBridge = 0x12,
    Diagnostic = 0xDC,
    Wireless = 0xE0,
    Miscellaneous = 0xEF,
    ApplicationSpecific = 0xFE,
    VendorSpecific = 0xFF
};

}
