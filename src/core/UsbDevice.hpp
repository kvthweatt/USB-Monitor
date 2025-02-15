#pragma once
#include <usb-monitor/Types.hpp>
#include <libusb-1.0/libusb.h>
#include <QObject>
#include <memory>
#include <string>

namespace usb_monitor {

class UsbDevice : public QObject {
    Q_OBJECT

public:
    explicit UsbDevice(libusb_device* device, libusb_context* context, QObject* parent = nullptr);
    ~UsbDevice();

    DeviceIdentifier identifier() const;
    std::string description() const;
    DeviceClass deviceClass() const;
    
    bool open();
    void close();
    bool isOpen() const;
    
    bool reset();
    bool setConfiguration(int config);
    bool claimInterface(int interface);
    bool releaseInterface(int interface);
    
    PowerStats getPowerStats() const;
    BandwidthStats getBandwidthStats() const;
    
    libusb_device* nativeDevice() const;
    libusb_device_handle* nativeHandle() const;

signals:
    void powerChanged(const PowerStats& stats);
    void bandwidthChanged(const BandwidthStats& stats);
    void errorOccurred(const std::string& error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

}
