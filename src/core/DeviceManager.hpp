#pragma once
#include <QObject>
#include <memory>
#include <vector>
#include <string>

struct libusb_context;
struct libusb_device;

namespace usb_monitor {

class UsbDevice;
class PowerManager;
class BandwidthMonitor;

class DeviceManager : public QObject {
    Q_OBJECT

public:
    explicit DeviceManager(QObject* parent = nullptr);
    ~DeviceManager();

    std::vector<std::shared_ptr<UsbDevice>> getConnectedDevices() const;
    PowerManager* powerManager() const;
    BandwidthMonitor* bandwidthMonitor() const;

public slots:
    void pollDevices();

signals:
    void deviceAdded(std::shared_ptr<UsbDevice> device);
    void deviceRemoved(std::shared_ptr<UsbDevice> device);
    void error(const std::string& message);

private:
    void setupHotplugSupport();
    void handleDeviceArrival(libusb_device* device);
    void handleDeviceRemoval(libusb_device* device);
    std::string getDeviceIdentifier(libusb_device* device);

    class Private;
    std::unique_ptr<Private> d;
};

}
