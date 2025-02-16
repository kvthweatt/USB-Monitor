// src/analysis/ProtocolAnalyzer.cpp
#include "ProtocolAnalyzer.hpp"
#include "../core/UsbDevice.hpp"
#include <QTimer>
#include <deque>
#include <mutex>
#include <chrono>

namespace usb_monitor {

struct TransferRecord {
    std::chrono::steady_clock::time_point timestamp;
    uint8_t endpointAddress;
    std::vector<uint8_t> data;
    bool isInput;
    int status;
};

class ProtocolAnalyzer::Private {
public:
    std::map<const UsbDevice*, std::deque<TransferRecord>> transferHistory;
    std::map<const UsbDevice*, QTimer*> monitoringTimers;
    size_t maxHistorySize{1000};
    std::mutex historyMutex;
    ProtocolAnalyzer* q_ptr;
    
    void recordTransfer(const UsbDevice* device,
                       uint8_t endpointAddress,
                       const std::vector<uint8_t>& data,
                       bool isInput,
                       int status) {
        std::lock_guard<std::mutex> lock(historyMutex);
        
        auto& history = transferHistory[device];
        
        // Add new record
        TransferRecord record{
            std::chrono::steady_clock::now(),
            endpointAddress,
            data,
            isInput,
            status
        };
        
        history.push_back(std::move(record));
        
        // Trim history if needed
        while (history.size() > maxHistorySize) {
            history.pop_front();
        }
    }
    
    void analyzeProtocol(const UsbDevice* device) {
        if (!device || !device->isOpen()) return;
        
        // Monitor endpoints for activity
        libusb_device* dev = device->nativeDevice();
        libusb_config_descriptor* config;
        if (libusb_get_active_config_descriptor(dev, &config) == 0) {
            for (int i = 0; i < config->bNumInterfaces; i++) {
                const libusb_interface* interface = &config->interface[i];
                for (int j = 0; j < interface->num_altsetting; j++) {
                    const libusb_interface_descriptor* setting = &interface->altsetting[j];
                    
                    for (int k = 0; k < setting->bNumEndpoints; k++) {
                        const libusb_endpoint_descriptor* endpoint = &setting->endpoint[k];
                        
                        // Simulate some transfer activity for demonstration
                        std::vector<uint8_t> dummyData(endpoint->wMaxPacketSize);
                        recordTransfer(device,
                                     endpoint->bEndpointAddress,
                                     dummyData,
                                     endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN,
                                     0);
                    }
                }
            }
            libusb_free_config_descriptor(config);
        }
        
        // Analyze transfer patterns and emit findings
        analyzeTransferPatterns(device);
    }
    
    void analyzeTransferPatterns(const UsbDevice* device) {
        std::lock_guard<std::mutex> lock(historyMutex);
        
        auto it = transferHistory.find(device);
        if (it == transferHistory.end() || it->second.empty()) {
            return;
        }
        
        const auto& history = it->second;
        
        // Calculate transfer statistics
        std::map<uint8_t, size_t> endpointFrequency;
        std::map<uint8_t, double> averageTransferSize;
        std::map<uint8_t, int> errorCount;
        
        for (const auto& record : history) {
            endpointFrequency[record.endpointAddress]++;
            averageTransferSize[record.endpointAddress] += record.data.size();
            if (record.status != 0) {
                errorCount[record.endpointAddress]++;
            }
        }
        
        // Normalize average transfer sizes
        for (auto& pair : averageTransferSize) {
            pair.second /= endpointFrequency[pair.first];
        }
        
        // Detect potential protocol patterns
        ProtocolPattern pattern;
        pattern.timestamp = std::chrono::steady_clock::now();
        pattern.device = device;
        
        // Most active endpoint
        auto maxFreqIt = std::max_element(
            endpointFrequency.begin(),
            endpointFrequency.end(),
            [](const auto& a, const auto& b) {
                return a.second < b.second;
            }
        );
        if (maxFreqIt != endpointFrequency.end()) {
            pattern.primaryEndpoint = maxFreqIt->first;
        }
        
        // Check for regular transfer sizes
        bool regularSizes = true;
        double firstSize = 0;
        for (const auto& pair : averageTransferSize) {
            if (firstSize == 0) {
                firstSize = pair.second;
            } else if (std::abs(pair.second - firstSize) > 1.0) {
                regularSizes = false;
                break;
            }
        }
        pattern.hasRegularTransferSizes = regularSizes;
        
        // Check error rates
        for (const auto& pair : errorCount) {
            double errorRate = static_cast<double>(pair.second) /
                             endpointFrequency[pair.first];
            if (errorRate > 0.1) { // More than 10% errors
                pattern.problematicEndpoints.push_back(pair.first);
            }
        }
        
        if (q_ptr) {
            emit q_ptr->protocolPatternDetected(pattern);
        }
    }
};

ProtocolAnalyzer::ProtocolAnalyzer(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
}

ProtocolAnalyzer::~ProtocolAnalyzer() {
    // Clean up monitoring timers
    for (auto& pair : d->monitoringTimers) {
        pair.second->stop();
        delete pair.second;
    }
}

void ProtocolAnalyzer::startMonitoring(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    
    // Check if already monitoring
    if (d->monitoringTimers.find(device.get()) != d->monitoringTimers.end()) {
        return;
    }
    
    // Create timer for periodic updates
    QTimer* timer = new QTimer(this);
    timer->setInterval(100); // Update every 100ms
    
    connect(timer, &QTimer::timeout, [this, dev = device.get()]() {
        d->analyzeProtocol(dev);
    });
    
    d->monitoringTimers[device.get()] = timer;
    timer->start();
    
    // Initialize history for device
    {
        std::lock_guard<std::mutex> lock(d->historyMutex);
        d->transferHistory[device.get()] = std::deque<TransferRecord>();
    }
    
    // Initial analysis
    d->analyzeProtocol(device.get());
}

void ProtocolAnalyzer::stopMonitoring(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    
    auto it = d->monitoringTimers.find(device.get());
    if (it != d->monitoringTimers.end()) {
        it->second->stop();
        delete it->second;
        d->monitoringTimers.erase(it);
    }
    
    {
        std::lock_guard<std::mutex> lock(d->historyMutex);
        d->transferHistory.erase(device.get());
    }
}

std::vector<TransferInfo> ProtocolAnalyzer::getRecentTransfers(
    const UsbDevice* device, size_t maxCount) const {
    std::vector<TransferInfo> result;
    
    std::lock_guard<std::mutex> lock(d->historyMutex);
    
    auto it = d->transferHistory.find(device);
    if (it == d->transferHistory.end()) {
        return result;
    }
    
    const auto& history = it->second;
    size_t count = std::min(maxCount, history.size());
    
    result.reserve(count);
    auto startIt = history.end() - count;
    
    for (auto it = startIt; it != history.end(); ++it) {
        TransferInfo info;
        info.timestamp = it->timestamp;
        info.endpointAddress = it->endpointAddress;
        info.dataSize = it->data.size();
        info.isInput = it->isInput;
        info.status = it->status;
        result.push_back(info);
    }
    
    return result;
}

void ProtocolAnalyzer::clearHistory(const UsbDevice* device) {
    std::lock_guard<std::mutex> lock(d->historyMutex);
    
    auto it = d->transferHistory.find(device);
    if (it != d->transferHistory.end()) {
        it->second.clear();
    }
}

void ProtocolAnalyzer::setMaxHistorySize(size_t size) {
    std::lock_guard<std::mutex> lock(d->historyMutex);
    
    d->maxHistorySize = size;
    
    // Trim histories if needed
    for (auto& pair : d->transferHistory) {
        while (pair.second.size() > size) {
            pair.second.pop_front();
        }
    }
}

} // namespace usb_monitor
