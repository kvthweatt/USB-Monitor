#pragma once
#include <QObject>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

namespace usb_monitor {

class UsbDevice;
struct PowerStats;
struct BandwidthStats;

enum class ExportFormat {
    CSV,
    JSON,
    XML,
    HTML,
    PDF
};

struct ExportOptions {
    bool includeDeviceInfo{true};
    bool includePowerStats{true};
    bool includeBandwidthStats{true};
    bool includeTransferHistory{true};
    bool includeSecurityEvents{true};
    std::chrono::system_clock::time_point startTime{};
    std::chrono::system_clock::time_point endTime{};
};

class ExportManager : public QObject {
    Q_OBJECT

public:
    explicit ExportManager(QObject* parent = nullptr);
    ~ExportManager();

    // Export functionality
    bool exportDevice(const UsbDevice* device, 
                     const std::string& filename,
                     ExportFormat format,
                     const ExportOptions& options = ExportOptions{});
    
    bool exportAllDevices(const std::string& filename,
                         ExportFormat format,
                         const ExportOptions& options = ExportOptions{});
    
    // Template management
    bool saveAsTemplate(const std::string& name, const ExportOptions& options);
    ExportOptions loadTemplate(const std::string& name) const;
    std::vector<std::string> getTemplateNames() const;
    void deleteTemplate(const std::string& name);

signals:
    void exportProgress(int percent);
    void exportComplete(const std::string& filename);
    void exportError(const std::string& error);

private:
    bool exportToCSV(const UsbDevice* device, 
                    const std::string& filename,
                    const ExportOptions& options);
    bool exportToJSON(const UsbDevice* device,
                     const std::string& filename,
                     const ExportOptions& options);
    bool exportToXML(const UsbDevice* device,
                    const std::string& filename,
                    const ExportOptions& options);
    bool exportToHTML(const UsbDevice* device,
                     const std::string& filename,
                     const ExportOptions& options);
    bool exportToPDF(const UsbDevice* device,
                    const std::string& filename,
                    const ExportOptions& options);

    class Private;
    std::unique_ptr<Private> d;
};

}
