#pragma once
#include <QObject>
#include <memory>
#include <chrono>
#include <vector>

namespace usb_monitor {

class UsbDevice;

struct BenchmarkResults {
    double readThroughput;     // bytes/second
    double writeThroughput;    // bytes/second
    double averageLatency;     // milliseconds
    double maxLatency;         // milliseconds
    size_t totalTransfers;
    size_t failedTransfers;
    std::chrono::milliseconds duration;
};

class BenchmarkTool : public QObject {
    Q_OBJECT

public:
    explicit BenchmarkTool(QObject* parent = nullptr);
    ~BenchmarkTool();

    // Configuration
    void setTransferSize(size_t bytes);
    void setDuration(std::chrono::seconds duration);
    void setIterations(size_t count);

    // Benchmark control
    bool startBenchmark(std::shared_ptr<UsbDevice> device);
    void stopBenchmark();
    bool isRunning() const;

    // Results
    BenchmarkResults getResults() const;
    std::vector<double> getLatencyDistribution() const;
    
signals:
    void benchmarkProgress(int percentComplete);
    void benchmarkComplete(const BenchmarkResults& results);
    void benchmarkError(const std::string& error);

private:
    void runThroughputTest();
    void runLatencyTest();
    void runStressTest();
    
    class Private;
    std::unique_ptr<Private> d;
};

}
