#include "BandwidthMonitor.hpp"
#include "UsbDevice.hpp"
#include <usb-monitor/Constants.hpp>
#include <QTimer>
#include <map>
#include <mutex>
#include <deque>
#include <chrono>

namespace usb_monitor {

struct TransferStats {
    std::deque<std::pair<std::chrono::steady_clock::time_point, uint64_t>> readHistory;
    std::deque<std::pair<std::chrono::steady_clock::time_point, uint64_t>> writeHistory;
    uint64_t totalBytesRead{0};
    uint64_t totalBytesWritten{0};
};

class BandwidthMonitor::Private {
	friend class BandwidthMonitor;
	
public:
    std::map<const UsbDevice*, TransferStats> deviceStats;
    std::map<const UsbDevice*, QTimer*> monitoringTimers;
    std::mutex statsMutex;
    BandwidthMonitor* q_ptr;
    
    void updateDeviceBandwidth(const UsbDevice* device) {
        if (!device || !device->isOpen()) return;
        
        auto now = std::chrono::steady_clock::now();
        auto& stats = deviceStats[device];
        
        // Get endpoint descriptors to monitor transfer activity
        libusb_device* dev = device->nativeDevice();
        libusb_config_descriptor* config;
        if (libusb_get_active_config_descriptor(dev, &config) == 0) {
            for (int i = 0; i < config->bNumInterfaces; i++) {
                const libusb_interface* interface = &config->interface[i];
                for (int j = 0; j < interface->num_altsetting; j++) {
                    const libusb_interface_descriptor* setting = &interface->altsetting[j];
                    
                    for (int k = 0; k < setting->bNumEndpoints; k++) {
                        const libusb_endpoint_descriptor* endpoint = &setting->endpoint[k];
                        
                        // Check endpoint direction and update stats
                        if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                            // IN endpoint (device to host)
                            updateTransferStats(stats.readHistory, stats.totalBytesRead,
                                             endpoint->wMaxPacketSize, now);
                        } else {
                            // OUT endpoint (host to device)
                            updateTransferStats(stats.writeHistory, stats.totalBytesWritten,
                                             endpoint->wMaxPacketSize, now);
                        }
                    }
                }
            }
            libusb_free_config_descriptor(config);
        }
        
        // Calculate current bandwidth stats
        BandwidthStats bwStats;
        bwStats.bytesRead = stats.totalBytesRead;
        bwStats.bytesWritten = stats.totalBytesWritten;
        bwStats.readSpeed = calculateSpeed(stats.readHistory);
        bwStats.writeSpeed = calculateSpeed(stats.writeHistory);
        bwStats.speedClass = libusb_get_device_speed(dev);
        
        if (q_ptr) {
            Q_EMIT q_ptr->statsUpdated(device, bwStats);
        }
    }
    
private:
    void updateTransferStats(std::deque<std::pair<std::chrono::steady_clock::time_point, uint64_t>>& history,
                            uint64_t& totalBytes,
                            uint16_t maxPacketSize,
                            std::chrono::steady_clock::time_point now) {
        // Simulate some transfer activity for demonstration
        // In a real implementation, you'd track actual transfers
        totalBytes += maxPacketSize;
        history.emplace_back(now, totalBytes);
        
        // Keep only last 5 seconds of history
        while (!history.empty() && 
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   now - history.front().first).count() > BANDWIDTH_WINDOW) {
            history.pop_front();
        }
    }
    
    double calculateSpeed(const std::deque<std::pair<std::chrono::steady_clock::time_point, uint64_t>>& history) {
        if (history.size() < 2) return 0.0;
        
        auto timeSpan = std::chrono::duration_cast<std::chrono::milliseconds>(
            history.back().first - history.front().first).count() / 1000.0;
        auto byteSpan = history.back().second - history.front().second;
        
        return timeSpan > 0.0 ? byteSpan / timeSpan : 0.0;
    }
};

BandwidthMonitor::BandwidthMonitor(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
}

BandwidthMonitor::~BandwidthMonitor() {
    // Clean up monitoring timers
    for (auto& pair : d->monitoringTimers) {
        pair.second->stop();
        delete pair.second;
    }
}

void BandwidthMonitor::startMonitoring(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    
    // Check if already monitoring
    if (d->monitoringTimers.find(device.get()) != d->monitoringTimers.end()) {
        return;
    }
    
    // Create timer for periodic updates
    QTimer* timer = new QTimer(this);
    timer->setInterval(100); // Update every 100ms for smoother readings
    
    connect(timer, &QTimer::timeout, [this, dev = device.get()]() {
        d->updateDeviceBandwidth(dev);
    });
    
    d->monitoringTimers[device.get()] = timer;
    timer->start();
    
    // Initialize stats
    std::lock_guard<std::mutex> lock(d->statsMutex);
    d->deviceStats[device.get()] = TransferStats{};
}

void BandwidthMonitor::stopMonitoring(std::shared_ptr<UsbDevice> device) {
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

BandwidthStats BandwidthMonitor::getDeviceStats(const UsbDevice* device) const {
    BandwidthStats stats{};
    std::lock_guard<std::mutex> lock(d->statsMutex);
    
    auto it = d->deviceStats.find(device);
    if (it != d->deviceStats.end()) {
        const auto& transferStats = it->second;
        stats.bytesRead = transferStats.totalBytesRead;
        stats.bytesWritten = transferStats.totalBytesWritten;
        stats.readSpeed = d->calculateSpeed(transferStats.readHistory);
        stats.writeSpeed = d->calculateSpeed(transferStats.writeHistory);
        if (device->nativeDevice()) {
            stats.speedClass = libusb_get_device_speed(device->nativeDevice());
        }
    }
    return stats;
}

void BandwidthMonitor::resetStats(const UsbDevice* device) {
    std::lock_guard<std::mutex> lock(d->statsMutex);
    auto it = d->deviceStats.find(device);
    if (it != d->deviceStats.end()) {
        it->second = TransferStats{};
    }
}

}
