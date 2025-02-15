#include "PowerManager.hpp"
#include "UsbDevice.hpp"
#include <QTimer>
#include <map>
#include <mutex>

namespace usb_monitor {

class PowerManager::Private {
public:
    libusb_context* context{nullptr};
    std::map<const UsbDevice*, PowerStats> deviceStats;
    std::map<const UsbDevice*, QTimer*> monitoringTimers;
    std::mutex statsMutex;
    
    void updateDevicePower(const UsbDevice* device) {
        if (!device || !device->isOpen()) return;
        
        PowerStats stats{};
        libusb_device_handle* handle = device->nativeHandle();
        
        // Get power information from device descriptor and configurations
        libusb_device* dev = device->nativeDevice();
        libusb_config_descriptor* config;
        if (libusb_get_active_config_descriptor(dev, &config) == 0) {
            stats.maxPower = config->maxPower * 2; // maxPower is in 2mA units
            
            uint8_t bmAttributes = config->bmAttributes;
            stats.selfPowered = (bmAttributes & 0x40) != 0;
            
            libusb_free_config_descriptor(config);
        }
        
        // For supported devices, try to get actual current usage
        unsigned char buffer[2];
        int ret = libusb_control_transfer(
            handle,
            LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
            0xFE, // Get Status request
            0,
            0,
            buffer,
            sizeof(buffer),
            1000
        );
        
        if (ret == 2) {
            stats.currentUsage = (buffer[1] << 8 | buffer[0]) * 2.0; // Convert to mA
        }
        
        // For USB 3.0 devices, try to get more detailed power info
        if (libusb_get_device_speed(dev) >= LIBUSB_SPEED_SUPER) {
            unsigned char bos[128];
            ret = libusb_get_descriptor(
                handle,
                LIBUSB_DT_BOS,
                0,
                bos,
                sizeof(bos)
            );
            
            if (ret > 0) {
                // Parse BOS descriptor for power info
                // This is a simplified version - real implementation would need
                // to parse the full BOS descriptor structure
                stats.voltage = 5.0; // Default USB voltage
                stats.powerUsage = stats.currentUsage * stats.voltage;
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            deviceStats[device] = stats;
        }
        
        emit powerStatsUpdated(device, stats);
    }
};

PowerManager::PowerManager(libusb_context* context, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
    d->context = context;
}

PowerManager::~PowerManager() {
    // Clean up monitoring timers
    for (auto& pair : d->monitoringTimers) {
        pair.second->stop();
        delete pair.second;
    }
}

void PowerManager::startMonitoring(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    
    // Check if already monitoring
    if (d->monitoringTimers.find(device.get()) != d->monitoringTimers.end()) {
        return;
    }
    
    // Create timer for periodic updates
    QTimer* timer = new QTimer(this);
    timer->setInterval(1000); // Update every second
    
    connect(timer, &QTimer::timeout, [this, dev = device.get()]() {
        d->updateDevicePower(dev);
    });
    
    d->monitoringTimers[device.get()] = timer;
    timer->start();
    
    // Get initial power stats
    d->updateDevicePower(device.get());
}

void PowerManager::stopMonitoring(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    
    auto it = d->monitoringTimers.find(device.get());
    if (it != d->monitoringTimers.end()) {
        it->second->stop();
        delete it->second;
        d->monitoringTimers.erase(it);
    }
    
    {
        std::lock_guard<std::mutex> lock(d->statsMutex);
        d->deviceStats.erase(device.get());
    }
}

PowerStats PowerManager::getDevicePowerStats(const UsbDevice* device) const {
    std::lock_guard<std::mutex> lock(d->statsMutex);
    auto it = d->deviceStats.find(device);
    if (it != d->deviceStats.end()) {
        return it->second;
    }
    return PowerStats{};
}

bool PowerManager::setPowerState(UsbDevice* device, bool enabled) {
    if (!device || !device->isOpen()) return false;
    
    libusb_device_handle* handle = device->nativeHandle();
    
    // Try to suspend/resume the device
    int ret = libusb_control_transfer(
        handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE,
        enabled ? LIBUSB_REQUEST_CLEAR_FEATURE : LIBUSB_REQUEST_SET_FEATURE,
        0x00, // USB_DEVICE_REMOTE_WAKEUP
        0,
        nullptr,
        0,
        1000
    );
    
    if (ret < 0) {
        emit errorOccurred("Failed to set power state: " + 
                          std::string(libusb_error_name(ret)));
        return false;
    }
    
    // Update power stats after state change
    d->updateDevicePower(device);
    return true;
}

bool PowerManager::supportsDevicePower(const UsbDevice* device) const {
    if (!device || !device->isOpen()) return false;
    
    // Check if device supports power management
    libusb_device* dev = device->nativeDevice();
    libusb_config_descriptor* config;
    bool supported = false;
    
    if (libusb_get_active_config_descriptor(dev, &config) == 0) {
        // Check for remote wakeup support
        supported = (config->bmAttributes & 0x20) != 0;
        libusb_free_config_descriptor(config);
    }
    
    return supported;
}

} // namespace usb_monitor
