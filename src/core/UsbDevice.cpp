#include "UsbDevice.hpp"
#include <usb-monitor/Constants.hpp>
#include <QDebug>
#include <sstream>

namespace usb_monitor {

class UsbDevice::Private {
public:
    libusb_device* device{nullptr};
    libusb_device_handle* handle{nullptr};
    libusb_context* context{nullptr};
    libusb_device_descriptor descriptor{};
    DeviceIdentifier identifier{};
    PowerStats powerStats{};
    BandwidthStats bandwidthStats{};
    bool isOpened{false};
    
    void updateIdentifier() {
        identifier.busNumber = libusb_get_bus_number(device);
        identifier.deviceAddress = libusb_get_device_address(device);
        identifier.vendorId = descriptor.idVendor;
        identifier.productId = descriptor.idProduct;
    }
    
    std::string getStringDescriptor(uint8_t index) {
        if (!handle || index == 0) return "";
        
        unsigned char buffer[MAX_STRING_LENGTH];
        int ret = libusb_get_string_descriptor_ascii(
            handle, index, buffer, sizeof(buffer));
            
        if (ret < 0) return "";
        return std::string(reinterpret_cast<char*>(buffer), ret);
    }
};

UsbDevice::UsbDevice(libusb_device* device, libusb_context* context, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
    
    d->device = device;
    d->context = context;
    libusb_ref_device(device);
    
    if (libusb_get_device_descriptor(device, &d->descriptor) == 0) {
        d->updateIdentifier();
    }
}

UsbDevice::~UsbDevice() {
    close();
    if (d->device) {
        libusb_unref_device(d->device);
    }
}

DeviceIdentifier UsbDevice::identifier() const {
    return d->identifier;
}

std::string UsbDevice::description() const {
    std::stringstream ss;
    
    if (d->handle) {
        std::string manufacturer = d->getStringDescriptor(d->descriptor.iManufacturer);
        std::string product = d->getStringDescriptor(d->descriptor.iProduct);
        std::string serial = d->getStringDescriptor(d->descriptor.iSerialNumber);
        
        if (!manufacturer.empty()) ss << manufacturer << " ";
        if (!product.empty()) ss << product << " ";
        if (!serial.empty()) ss << "(" << serial << ")";
    }
    
    if (ss.str().empty()) {
        ss << "Unknown Device "
           << std::hex << std::uppercase
           << d->descriptor.idVendor << ":"
           << d->descriptor.idProduct;
    }
    
    return ss.str();
}

DeviceClass UsbDevice::deviceClass() const {
    return static_cast<DeviceClass>(d->descriptor.bDeviceClass);
}

bool UsbDevice::open() {
    if (d->isOpened) return true;
    
    int ret = libusb_open(d->device, &d->handle);
    if (ret != LIBUSB_SUCCESS) {
        emit errorOccurred("Failed to open device: " + 
                          std::string(libusb_error_name(ret)));
        return false;
    }
    
    d->isOpened = true;
    return true;
}

void UsbDevice::close() {
    if (d->handle) {
        libusb_close(d->handle);
        d->handle = nullptr;
    }
    d->isOpened = false;
}

bool UsbDevice::isOpen() const {
    return d->isOpened;
}

bool UsbDevice::reset() {
    if (!d->handle) return false;
    
    int ret = libusb_reset_device(d->handle);
    if (ret != LIBUSB_SUCCESS) {
        emit errorOccurred("Failed to reset device: " + 
                          std::string(libusb_error_name(ret)));
        return false;
    }
    return true;
}

bool UsbDevice::setConfiguration(int config) {
    if (!d->handle) return false;
    
    int ret = libusb_set_configuration(d->handle, config);
    if (ret != LIBUSB_SUCCESS) {
        emit errorOccurred("Failed to set configuration: " + 
                          std::string(libusb_error_name(ret)));
        return false;
    }
    return true;
}

bool UsbDevice::claimInterface(int interface) {
    if (!d->handle) return false;
    
    int ret = libusb_claim_interface(d->handle, interface);
    if (ret != LIBUSB_SUCCESS) {
        emit errorOccurred("Failed to claim interface: " + 
                          std::string(libusb_error_name(ret)));
        return false;
    }
    return true;
}

bool UsbDevice::releaseInterface(int interface) {
    if (!d->handle) return false;
    
    int ret = libusb_release_interface(d->handle, interface);
    if (ret != LIBUSB_SUCCESS) {
        emit errorOccurred("Failed to release interface: " + 
                          std::string(libusb_error_name(ret)));
        return false;
    }
    return true;
}

PowerStats UsbDevice::getPowerStats() const {
    return d->powerStats;
}

BandwidthStats UsbDevice::getBandwidthStats() const {
    return d->bandwidthStats;
}

libusb_device* UsbDevice::nativeDevice() const {
    return d->device;
}

libusb_device_handle* UsbDevice::nativeHandle() const {
    return d->handle;
}

} // namespace usb_monitor
