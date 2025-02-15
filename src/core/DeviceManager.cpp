#include "DeviceManager.hpp"
#include "UsbDevice.hpp"
#include "PowerManager.hpp"
#include "BandwidthMonitor.hpp"
#include <QTimer>
#include <sstream>
#include <map>
#include <mutex>

namespace usb_monitor {

class DeviceManager::Private {
public:
    libusb_context* context{nullptr};
    std::map<std::string, std::shared_ptr<UsbDevice>> devices;
    std::mutex devicesMutex;
    QTimer* pollTimer{nullptr};
    std::unique_ptr<PowerManager> powerMgr;
    std::unique_ptr<BandwidthMonitor> bwMonitor;
    bool hotplugSupported{false};
    libusb_hotplug_callback_handle hotplugHandle;

    // Hotplug callback wrapper
    static int LIBUSB_CALL hotplugCallback(libusb_context*, 
                                         libusb_device* device,
                                         libusb_hotplug_event event,
                                         void* user_data) {
        auto manager = static_cast<DeviceManager*>(user_data);
        if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
            manager->handleDeviceArrival(device);
        } else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
            manager->handleDeviceRemoval(device);
        }
        return 0;
    }
};

DeviceManager::DeviceManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
    
    // Initialize libusb
    if (libusb_init(&d->context) != LIBUSB_SUCCESS) {
        emit error("Failed to initialize libusb");
        return;
    }
    
    // Create managers
    d->powerMgr = std::make_unique<PowerManager>(d->context);
    d->bwMonitor = std::make_unique<BandwidthMonitor>();
    
    // Setup polling timer as fallback
    d->pollTimer = new QTimer(this);
    connect(d->pollTimer, &QTimer::timeout, this, &DeviceManager::pollDevices);
    
    // Try to setup hotplug support
    setupHotplugSupport();
    
    // Start polling if hotplug is not available
    if (!d->hotplugSupported) {
        d->pollTimer->start(POLLING_INTERVAL);
    }
}

DeviceManager::~DeviceManager() {
    // Stop polling/hotplug
    if (d->pollTimer) {
        d->pollTimer->stop();
    }
    
    if (d->hotplugSupported) {
        libusb_hotplug_deregister_callback(d->context, d->hotplugHandle);
    }
    
    // Cleanup devices
    {
        std::lock_guard<std::mutex> lock(d->devicesMutex);
        d->devices.clear();
    }
    
    // Cleanup libusb
    if (d->context) {
        libusb_exit(d->context);
        d->context = nullptr;
    }
}

std::vector<std::shared_ptr<UsbDevice>> DeviceManager::getConnectedDevices() const {
    std::vector<std::shared_ptr<UsbDevice>> result;
    std::lock_guard<std::mutex> lock(d->devicesMutex);
    
    result.reserve(d->devices.size());
    for (const auto& [_, device] : d->devices) {
        result.push_back(device);
    }
    
    return result;
}

PowerManager* DeviceManager::powerManager() const {
    return d->powerMgr.get();
}

BandwidthMonitor* DeviceManager::bandwidthMonitor() const {
    return d->bwMonitor.get();
}

void DeviceManager::setupHotplugSupport() {
    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        return;
    }
    
    int result = libusb_hotplug_register_callback(
        d->context,
        static_cast<libusb_hotplug_event>(
            LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
            LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
        LIBUSB_HOTPLUG_ENUMERATE,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        Private::hotplugCallback,
        this,
        &d->hotplugHandle
    );
    
    if (result == LIBUSB_SUCCESS) {
        d->hotplugSupported = true;
    }
}

void DeviceManager::pollDevices() {
    libusb_device** list;
    ssize_t count = libusb_get_device_list(d->context, &list);
    
    if (count < 0) {
        emit error("Failed to get device list");
        return;
    }
    
    // Track current devices to detect removals
    std::map<std::string, bool> currentDevices;
    {
        std::lock_guard<std::mutex> lock(d->devicesMutex);
        for (const auto& [id, _] : d->devices) {
            currentDevices[id] = false;
        }
    }
    
    // Check for new/existing devices
    for (ssize_t i = 0; i < count; i++) {
        libusb_device* device = list[i];
        std::string id = getDeviceIdentifier(device);
        
        std::lock_guard<std::mutex> lock(d->devicesMutex);
        if (d->devices.find(id) == d->devices.end()) {
            // New device
            handleDeviceArrival(device);
        }
        currentDevices[id] = true;
    }
    
    // Check for removed devices
    for (const auto& [id, present] : currentDevices) {
        if (!present) {
            std::lock_guard<std::mutex> lock(d->devicesMutex);
            auto it = d->devices.find(id);
            if (it != d->devices.end()) {
                handleDeviceRemoval(it->second->nativeDevice());
            }
        }
    }
    
    libusb_free_device_list(list, 1);
}

void DeviceManager::handleDeviceArrival(libusb_device* device) {
    std::string id = getDeviceIdentifier(device);
    
    // Check if device already exists
    {
        std::lock_guard<std::mutex> lock(d->devicesMutex);
        if (d->devices.find(id) != d->devices.end()) {
            return;
        }
    }
    
    // Create new device object
    auto usbDevice = std::make_shared<UsbDevice>(device, d->context);
    
    // Start monitoring
    d->powerMgr->startMonitoring(usbDevice);
    d->bwMonitor->startMonitoring(usbDevice);
    
    // Add to devices map
    {
        std::lock_guard<std::mutex> lock(d->devicesMutex);
        d->devices[id] = usbDevice;
    }
    
    emit deviceAdded(usbDevice);
}

void DeviceManager::handleDeviceRemoval(libusb_device* device) {
    std::string id = getDeviceIdentifier(device);
    
    std::shared_ptr<UsbDevice> removedDevice;
    {
        std::lock_guard<std::mutex> lock(d->devicesMutex);
        auto it = d->devices.find(id);
        if (it != d->devices.end()) {
            removedDevice = it->second;
            d->devices.erase(it);
        }
    }
    
    if (removedDevice) {
        // Stop monitoring
        d->powerMgr->stopMonitoring(removedDevice);
        d->bwMonitor->stopMonitoring(removedDevice);
        
        emit deviceRemoved(removedDevice);
    }
}

std::string DeviceManager::getDeviceIdentifier(libusb_device* device) {
    uint8_t busNum = libusb_get_bus_number(device);
    uint8_t devAddr = libusb_get_device_address(device);
    
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) != 0) {
        desc.idVendor = 0;
        desc.idProduct = 0;
    }
    
    std::stringstream ss;
    ss << std::hex << std::uppercase
       << std::setw(4) << std::setfill('0') << desc.idVendor << ":"
       << std::setw(4) << std::setfill('0') << desc.idProduct << ":"
       << std::setw(2) << std::setfill('0') << static_cast<int>(busNum) << ":"
       << std::setw(2) << std::setfill('0') << static_cast<int>(devAddr);
    
    return ss.str();
}

} // namespace usb_monitor
