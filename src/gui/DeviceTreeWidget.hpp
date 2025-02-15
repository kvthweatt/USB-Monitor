// src/gui/DeviceTreeWidget.hpp
#pragma once
#include <QTreeWidget>
#include <memory>

namespace usb_monitor {

class DeviceManager;
class UsbDevice;

class DeviceTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    explicit DeviceTreeWidget(QWidget* parent = nullptr);
    ~DeviceTreeWidget();

    void setDeviceManager(DeviceManager* manager);

public slots:
    void refresh();

signals:
    void deviceSelected(std::shared_ptr<UsbDevice> device);

private slots:
    void handleDeviceAdded(std::shared_ptr<UsbDevice> device);
    void handleDeviceRemoved(std::shared_ptr<UsbDevice> device);
    void handleItemSelectionChanged();
    void updateDeviceStats();

private:
    QTreeWidgetItem* findDeviceItem(const UsbDevice* device) const;
    void updateDeviceItem(QTreeWidgetItem* item, const std::shared_ptr<UsbDevice>& device);
    void createDeviceItem(const std::shared_ptr<UsbDevice>& device);
    QString formatSpeed(double bytesPerSecond) const;
    QString formatPower(double milliwatts) const;

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace usb_monitor
