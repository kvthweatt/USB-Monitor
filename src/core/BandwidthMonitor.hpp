#pragma once
#include <QObject>
#include <memory>
#include "usb-monitor/Types.hpp"

namespace usb_monitor {

class UsbDevice;

class BandwidthMonitor : public QObject {
    Q_OBJECT

public:
    explicit BandwidthMonitor(QObject* parent = nullptr);
    ~BandwidthMonitor();

    void startMonitoring(std::shared_ptr<UsbDevice> device);
    void stopMonitoring(std::shared_ptr<UsbDevice> device);
    
    BandwidthStats getDeviceStats(const UsbDevice* device) const;
    void resetStats(const UsbDevice* device);
    
signals:
    void statsUpdated(const UsbDevice* device, const BandwidthStats& stats);
    void errorOccurred(const std::string& error);

private:
    class Private;
    std::unique_ptr<Private> d;
};

}
