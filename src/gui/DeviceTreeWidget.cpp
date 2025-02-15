// src/gui/DeviceTreeWidget.cpp
#include "DeviceTreeWidget.hpp"
#include "DeviceManager.hpp"
#include "UsbDevice.hpp"
#include <QHeaderView>
#include <QTimer>
#include <map>

namespace usb_monitor {

class DeviceTreeWidget::Private {
public:
    DeviceManager* manager{nullptr};
    std::map<const UsbDevice*, QTreeWidgetItem*> deviceItems;
    QTimer* updateTimer{nullptr};
};

DeviceTreeWidget::DeviceTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
    , d(std::make_unique<Private>()) {
    
    setHeaderLabels({
        "Device",
        "VID:PID",
        "Power",
        "Bandwidth",
        "Status"
    });
    
    setUniformRowHeights(true);
    setAlternatingRowColors(true);
    setRootIsDecorated(true);
    setSortingEnabled(true);
    setExpandsOnDoubleClick(true);
    
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    header()->setStretchLastSection(true);
    
    // Create update timer for stats
    d->updateTimer = new QTimer(this);
    connect(d->updateTimer, &QTimer::timeout, this, &DeviceTreeWidget::updateDeviceStats);
    d->updateTimer->start(1000); // Update every second
    
    connect(this, &QTreeWidget::itemSelectionChanged,
            this, &DeviceTreeWidget::handleItemSelectionChanged);
}

DeviceTreeWidget::~DeviceTreeWidget() = default;

void DeviceTreeWidget::setDeviceManager(DeviceManager* manager) {
    if (d->manager) {
        disconnect(d->manager, nullptr, this, nullptr);
    }
    
    d->manager = manager;
    
    if (manager) {
        connect(manager, &DeviceManager::deviceAdded,
                this, &DeviceTreeWidget::handleDeviceAdded);
        connect(manager, &DeviceManager::deviceRemoved,
                this, &DeviceTreeWidget::handleDeviceRemoved);
        
        // Load initial devices
        clear();
        d->deviceItems.clear();
        
        for (const auto& device : manager->getConnectedDevices()) {
            handleDeviceAdded(device);
        }
    }
}

void DeviceTreeWidget::refresh() {
    if (!d->manager) return;
    
    clear();
    d->deviceItems.clear();
    
    for (const auto& device : d->manager->getConnectedDevices()) {
        handleDeviceAdded(device);
    }
}

void DeviceTreeWidget::handleDeviceAdded(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    createDeviceItem(device);
}

void DeviceTreeWidget::handleDeviceRemoved(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    
    auto it = d->deviceItems.find(device.get());
    if (it != d->deviceItems.end()) {
        delete it->second;
        d->deviceItems.erase(it);
    }
}

void DeviceTreeWidget::handleItemSelectionChanged() {
    auto items = selectedItems();
    if (items.isEmpty()) {
        emit deviceSelected(nullptr);
        return;
    }
    
    auto item = items.first();
    for (const auto& pair : d->deviceItems) {
        if (pair.second == item) {
            // Find the corresponding device from the manager
            for (const auto& device : d->manager->getConnectedDevices()) {
                if (device.get() == pair.first) {
                    emit deviceSelected(device);
                    return;
                }
            }
        }
    }
}

QTreeWidgetItem* DeviceTreeWidget::findDeviceItem(const UsbDevice* device) const {
    auto it = d->deviceItems.find(device);
    return it != d->deviceItems.end() ? it->second : nullptr;
}

void DeviceTreeWidget::createDeviceItem(const std::shared_ptr<UsbDevice>& device) {
    auto item = new QTreeWidgetItem(this);
    d->deviceItems[device.get()] = item;
    
    updateDeviceItem(item, device);
    
    // Add interface items
    libusb_config_descriptor* config;
    if (libusb_get_active_config_descriptor(device->nativeDevice(), &config) == 0) {
        for (int i = 0; i < config->bNumInterfaces; i++) {
            const libusb_interface* interface = &config->interface[i];
            for (int j = 0; j < interface->num_altsetting; j++) {
                const libusb_interface_descriptor* setting = &interface->altsetting[j];
                
                auto interfaceItem = new QTreeWidgetItem(item);
                interfaceItem->setText(0, QString("Interface %1").arg(i));
                interfaceItem->setText(1, QString("Class: 0x%1")
                    .arg(setting->bInterfaceClass, 2, 16, QChar('0')));
                
                // Add endpoint items
                for (int k = 0; k < setting->bNumEndpoints; k++) {
                    const libusb_endpoint_descriptor* endpoint = &setting->endpoint[k];
                    
                    auto endpointItem = new QTreeWidgetItem(interfaceItem);
                    endpointItem->setText(0, QString("Endpoint 0x%1")
                        .arg(endpoint->bEndpointAddress, 2, 16, QChar('0')));
                    endpointItem->setText(1, QString("Max Packet: %1")
                        .arg(endpoint->wMaxPacketSize));
                }
            }
        }
        libusb_free_config_descriptor(config);
    }
}

void DeviceTreeWidget::updateDeviceItem(QTreeWidgetItem* item, const std::shared_ptr<UsbDevice>& device) {
    auto powerStats = d->manager->powerManager()->getDevicePowerStats(device.get());
    auto bwStats = d->manager->bandwidthMonitor()->getDeviceStats(device.get());
    
    item->setText(0, QString::fromStdString(device->description()));
    item->setText(1, QString("%1:%2")
        .arg(device->identifier().vendorId, 4, 16, QChar('0'))
        .arg(device->identifier().productId, 4, 16, QChar('0')));
    item->setText(2, formatPower(powerStats.powerUsage));
    item->setText(3, formatSpeed(bwStats.readSpeed + bwStats.writeSpeed));
    item->setText(4, device->isOpen() ? "Connected" : "Not Connected");
}

void DeviceTreeWidget::updateDeviceStats() {
    for (const auto& pair : d->deviceItems) {
        for (const auto& device : d->manager->getConnectedDevices()) {
            if (device.get() == pair.first) {
                updateDeviceItem(pair.second, device);
                break;
            }
        }
    }
}

QString DeviceTreeWidget::formatSpeed(double bytesPerSecond) const {
    if (bytesPerSecond < 1024)
        return QString("%1 B/s").arg(bytesPerSecond, 0, 'f', 1);
    if (bytesPerSecond < 1024 * 1024)
        return QString("%1 KB/s").arg(bytesPerSecond / 1024, 0, 'f', 1);
    if (bytesPerSecond < 1024 * 1024 * 1024)
        return QString("%1 MB/s").arg(bytesPerSecond / (1024 * 1024), 0, 'f', 1);
    return QString("%1 GB/s").arg(bytesPerSecond / (1024 * 1024 * 1024), 0, 'f', 1);
}

QString DeviceTreeWidget::formatPower(double milliwatts) const {
    if (milliwatts < 1000)
        return QString("%1 mW").arg(milliwatts, 0, 'f', 1);
    return QString("%1 W").arg(milliwatts / 1000, 0, 'f', 2);
}

} // namespace usb_monitor
