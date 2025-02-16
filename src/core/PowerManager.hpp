#pragma once
#include <QObject>
#include <memory>
#include <usb-monitor/Types.hpp>
#include <libusb-1.0/libusb.h>

namespace usb_monitor {

class UsbDevice;

class PowerManager : public QObject {
    Q_OBJECT

public:
    explicit PowerManager(libusb_context* context, QObject* parent = nullptr);
    ~PowerManager();

    void startMonitoring(std::shared_ptr<UsbDevice> device);
    void stopMonitoring(std::shared_ptr<UsbDevice> device);
    
    PowerStats getDevicePowerStats(const UsbDevice* device) const;
    bool setPowerState(UsbDevice* device, bool enabled);
    bool supportsDevicePower(const UsbDevice* device) const;

signals:
    void powerStatsUpdated(const UsbDevice* device, const PowerStats& stats);
    void errorOccurred(const std::string& error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

}
