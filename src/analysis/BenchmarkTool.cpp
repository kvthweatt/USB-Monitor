#include "BenchmarkTool.hpp"
#include "UsbDevice.hpp"
#include <QTimer>
#include <random>
#include <thread>
#include <atomic>

namespace usb_monitor {

struct TestConfig {
    size_t transferSize{4096};
    std::chrono::seconds duration{30};
    size_t iterations{1000};
};

class BenchmarkTool::Private {
public:
    std::shared_ptr<UsbDevice> device;
    TestConfig config;
    BenchmarkResults results;
    std::vector<double> latencies;
    std::atomic<bool> running{false};
    std::thread benchmarkThread;
    
    // Test data buffers
    std::vector<uint8_t> writeBuffer;
    std::vector<uint8_t> readBuffer;
    
    void initializeBuffers() {
        writeBuffer.resize(config.transferSize);
        readBuffer.resize(config.transferSize);
        
        // Fill write buffer with random data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& byte : writeBuffer) {
            byte = static_cast<uint8_t>(dis(gen));
        }
    }
    
    double measureTransfer(bool isRead, uint8_t endpoint) {
        auto start = std::chrono::high_resolution_clock::now();
        
        int transferred = 0;
        int result;
        
        if (isRead) {
            result = libusb_bulk_transfer(
                device->nativeHandle(),
                endpoint | LIBUSB_ENDPOINT_IN,
                readBuffer.data(),
                readBuffer.size(),
                &transferred,
                1000
            );
        } else {
            result = libusb_bulk_transfer(
                device->nativeHandle(),
                endpoint,
                writeBuffer.data(),
                writeBuffer.size(),
                &transferred,
                1000
            );
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        
        if (result != 0) {
            results.failedTransfers++;
            return 0.0;
        }
        
        results.totalTransfers++;
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
};

BenchmarkTool::BenchmarkTool(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
}

BenchmarkTool::~BenchmarkTool() {
    stopBenchmark();
}

void BenchmarkTool::setTransferSize(size_t bytes) {
    d->config.transferSize = bytes;
}

void BenchmarkTool::setDuration(std::chrono::seconds duration) {
    d->config.duration = duration;
}

void BenchmarkTool::setIterations(size_t count) {
    d->config.iterations = count;
}

bool BenchmarkTool::startBenchmark(std::shared_ptr<UsbDevice> device) {
    if (d->running || !device || !device->isOpen()) {
        return false;
    }
    
    d->device = device;
    d->running = true;
    d->results = BenchmarkResults{};
    d->latencies.clear();
    
    // Initialize test buffers
    d->initializeBuffers();
    
    // Start benchmark thread
    d->benchmarkThread = std::thread([this]() {
        auto start = std::chrono::steady_clock::now();
        
        runThroughputTest();
        runLatencyTest();
        runStressTest();
        
        d->results.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
            
        d->running = false;
        emit benchmarkComplete(d->results);
    });
    
    d->benchmarkThread.detach();
    return true;
}

void BenchmarkTool::stopBenchmark() {
    d->running = false;
    if (d->benchmarkThread.joinable()) {
        d->benchmarkThread.join();
    }
}

bool BenchmarkTool::isRunning() const {
    return d->running;
}

BenchmarkResults BenchmarkTool::getResults() const {
    return d->results;
}

std::vector<double> BenchmarkTool::getLatencyDistribution() const {
    return d->latencies;
}

void BenchmarkTool::runThroughputTest() {
    if (!d->running) return;
    
    // Find bulk endpoints
    libusb_config_descriptor* config;
    if (libusb_get_active_config_descriptor(d->device->nativeDevice(), &config) != 0) {
        emit benchmarkError("Failed to get config descriptor");
        return;
    }
    
    uint8_t inEndpoint = 0, outEndpoint = 0;
    
    // Find first bulk IN and OUT endpoints
    for (int i = 0; i < config->bNumInterfaces && d->running; i++) {
        const auto* interface = &config->interface[i];
        for (int j = 0; j < interface->num_altsetting && d->running; j++) {
            const auto* setting = &interface->altsetting[j];
            for (int k = 0; k < setting->bNumEndpoints && d->running; k++) {
                const auto* endpoint = &setting->endpoint[k];
                if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) {
                    if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        inEndpoint = endpoint->bEndpointAddress;
                    } else {
                        outEndpoint = endpoint->bEndpointAddress;
                    }
                }
            }
        }
    }
    
    libusb_free_config_descriptor(config);
    
    if (!inEndpoint || !outEndpoint) {
        emit benchmarkError("No suitable bulk endpoints found");
        return;
    }
    
    // Measure write throughput
    size_t totalWritten = 0;
    auto writeStart = std::chrono::steady_clock::now();
    
    while (d->running && 
           std::chrono::steady_clock::now() - writeStart < std::chrono::seconds(5)) {
        if (d->measureTransfer(false, outEndpoint) > 0) {
            totalWritten += d->config.transferSize;
        }
        emit benchmarkProgress(20); // 20% progress after write test
    }
    
    // Measure read throughput
    size_t totalRead = 0;
    auto readStart = std::chrono::steady_clock::now();
    
    while (d->running && 
           std::chrono::steady_clock::now() - readStart < std::chrono::seconds(5)) {
        if (d->measureTransfer(true, inEndpoint) > 0) {
            totalRead += d->config.transferSize;
        }
        emit benchmarkProgress(40); // 40% progress after read test
    }
    
    // Calculate throughput
    d->results.writeThroughput = totalWritten / 5.0; // bytes per second
    d->results.readThroughput = totalRead / 5.0;     // bytes per second
}

void BenchmarkTool::runLatencyTest() {
    if (!d->running) return;
    
    // Perform quick latency measurements
    std::vector<double> latencies;
    latencies.reserve(100);
    
    libusb_config_descriptor* config;
    if (libusb_get_active_config_descriptor(d->device->nativeDevice(), &config) != 0) {
        return;
    }
    
    // Find first interrupt endpoint
    uint8_t intEndpoint = 0;
    for (int i = 0; i < config->bNumInterfaces && d->running; i++) {
        const auto* interface = &config->interface[i];
        for (int j = 0; j < interface->num_altsetting && d->running; j++) {
            const auto* setting = &interface->altsetting[j];
            for (int k = 0; k < setting->bNumEndpoints && d->running; k++) {
                const auto* endpoint = &setting->endpoint[k];
                if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_INTERRUPT) {
                    intEndpoint = endpoint->bEndpointAddress;
                    break;
                }
            }
        }
    }
    
    libusb_free_config_descriptor(config);
    
    if (intEndpoint) {
        // Measure interrupt transfer latency
        for (size_t i = 0; i < 100 && d->running; i++) {
            double latency = d->measureTransfer(true, intEndpoint);
            if (latency > 0) {
                latencies.push_back(latency);
            }
            emit benchmarkProgress(40 + (i * 20) / 100);
        }
    }
    
    // Calculate latency statistics
    if (!latencies.empty()) {
        d->results.averageLatency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        d->results.maxLatency = *std::max_element(latencies.begin(), latencies.end());
        d->latencies = std::move(latencies);
    }
}

void BenchmarkTool::runStressTest() {
    if (!d->running) return;
    
    // Perform rapid alternating reads and writes
    libusb_config_descriptor* config;
    if (libusb_get_active_config_descriptor(d->device->nativeDevice(), &config) != 0) {
        return;
    }
    
    std::vector<uint8_t> endpoints;
    
    // Collect all suitable endpoints
    for (int i = 0; i < config->bNumInterfaces && d->running; i++) {
        const auto* interface = &config->interface[i];
        for (int j = 0; j < interface->num_altsetting && d->running; j++) {
            const auto* setting = &interface->altsetting[j];
            for (int k = 0; k < setting->bNumEndpoints && d->running; k++) {
                const auto* endpoint = &setting->endpoint[k];
                if ((endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) {
                    endpoints.push_back(endpoint->bEndpointAddress);
                }
            }
        }
    }
    
    libusb_free_config_descriptor(config);
    
    // Perform stress test
    auto stressStart = std::chrono::steady_clock::now();
    size_t iterations = 0;
    
    while (d->running && 
           std::chrono::steady_clock::now() - stressStart < std::chrono::seconds(5)) {
        for (uint8_t endpoint : endpoints) {
            if (!d->running) break;
            
            bool isRead = endpoint & LIBUSB_ENDPOINT_IN;
            d->measureTransfer(isRead, endpoint);
            iterations++;
            
            emit benchmarkProgress(60 + (iterations * 40) / 1000);
            if (iterations >= 1000) break;
        }
        if (iterations >= 1000) break;
    }
}

}
