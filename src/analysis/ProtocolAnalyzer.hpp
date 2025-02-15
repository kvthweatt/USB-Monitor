// src/analysis/ProtocolAnalyzer.hpp
#pragma once
#include <QObject>
#include <memory>
#include <chrono>
#include <vector>

namespace usb_monitor {

class UsbDevice;

struct TransferInfo {
    std::chrono::steady_clock::time_point timestamp;
    uint8_t endpointAddress;
    size_t dataSize;
    bool isInput;
    int status;
};

struct ProtocolPattern {
    std::chrono::steady_clock::time_point timestamp;
    const UsbDevice* device;
    uint8_t primaryEndpoint;
    bool hasRegularTransferSizes;
    std::vector<uint8_t> problematicEndpoints;
};

class ProtocolAnalyzer : public QObject {
    Q_OBJECT

public:
    explicit ProtocolAnalyzer(QObject* parent = nullptr);
    ~ProtocolAnalyzer();

    void startMonitoring(std::shared_ptr<UsbDevice> device);
    void stopMonitoring(std::shared_ptr<UsbDevice> device);
    
    std::vector<TransferInfo> getRecentTransfers(const UsbDevice* device,
                                                size_t maxCount = 100) const;
    
    void clearHistory(const UsbDevice* device);
    void setMaxHistorySize(size_t size);

signals:
    void protocolPatternDetected(const ProtocolPattern& pattern);
    void transferError(const UsbDevice* device, uint8_t endpoint, int status);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace usb_monitor
