#include "ExportManager.hpp"
#include "UsbDevice.hpp"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QXmlStreamWriter>
#include <QDir>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace usb_monitor {

class ExportManager::Private {
public:
    std::map<std::string, ExportOptions> templates;
    
    std::string formatDateTime(const std::chrono::system_clock::time_point& tp) const {
        auto t = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    void writeDeviceInfo(QJsonObject& json, const UsbDevice* device) const {
        if (!device) return;
        
        auto id = device->identifier();
        json["vendorId"] = QString::number(id.vendorId, 16);
        json["productId"] = QString::number(id.productId, 16);
        json["busNumber"] = id.busNumber;
        json["deviceAddress"] = id.deviceAddress;
        json["description"] = QString::fromStdString(device->description());
        json["deviceClass"] = static_cast<int>(device->deviceClass());
    }
    
    void writePowerStats(QJsonObject& json, const PowerStats& stats) const {
        json["currentUsage"] = stats.currentUsage;
        json["voltage"] = stats.voltage;
        json["powerUsage"] = stats.powerUsage;
        json["selfPowered"] = stats.selfPowered;
        json["maxPower"] = stats.maxPower;
    }
    
    void writeBandwidthStats(QJsonObject& json, const BandwidthStats& stats) const {
        json["bytesRead"] = qint64(stats.bytesRead);
        json["bytesWritten"] = qint64(stats.bytesWritten);
        json["readSpeed"] = stats.readSpeed;
        json["writeSpeed"] = stats.writeSpeed;
        json["speedClass"] = stats.speedClass;
    }
    
    void generateHTML(std::stringstream& html,
                     const UsbDevice* device,
                     const ExportOptions& options) const {
        html << "<!DOCTYPE html>\n"
             << "<html>\n"
             << "<head>\n"
             << "  <title>USB Device Report</title>\n"
             << "  <style>\n"
             << "    body { font-family: Arial, sans-serif; margin: 20px; }\n"
             << "    table { border-collapse: collapse; width: 100%; }\n"
             << "    th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n"
             << "    th { background-color: #f2f2f2; }\n"
             << "  </style>\n"
             << "</head>\n"
             << "<body>\n"
             << "  <h1>USB Device Report</h1>\n"
             << "  <p>Generated: " << formatDateTime(std::chrono::system_clock::now()) << "</p>\n";
        
        if (options.includeDeviceInfo && device) {
            auto id = device->identifier();
            html << "  <h2>Device Information</h2>\n"
                 << "  <table>\n"
                 << "    <tr><th>Vendor ID</th><td>" << std::hex << id.vendorId << "</td></tr>\n"
                 << "    <tr><th>Product ID</th><td>" << std::hex << id.productId << "</td></tr>\n"
                 << "    <tr><th>Description</th><td>" << device->description() << "</td></tr>\n"
                 << "  </table>\n";
        }
        
        // Add sections for power stats, bandwidth stats, etc.
        
        html << "</body>\n"
             << "</html>\n";
    }
};

ExportManager::ExportManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
}

ExportManager::~ExportManager() = default;

bool ExportManager::exportDevice(const UsbDevice* device,
                               const std::string& filename,
                               ExportFormat format,
                               const ExportOptions& options) {
    if (!device) {
        emit exportError("No device specified");
        return false;
    }
    
    bool success = false;
    emit exportProgress(0);
    
    try {
        switch (format) {
            case ExportFormat::CSV:
                success = exportToCSV(device, filename, options);
                break;
            case ExportFormat::JSON:
                success = exportToJSON(device, filename, options);
                break;
            case ExportFormat::XML:
                success = exportToXML(device, filename, options);
                break;
            case ExportFormat::HTML:
                success = exportToHTML(device, filename, options);
                break;
            case ExportFormat::PDF:
                success = exportToPDF(device, filename, options);
                break;
        }
    } catch (const std::exception& e) {
        emit exportError(std::string("Export failed: ") + e.what());
        return false;
    }
    
    if (success) {
        emit exportProgress(100);
        emit exportComplete(filename);
    }
    
    return success;
}

bool ExportManager::exportAllDevices(const std::string& filename,
                                   ExportFormat format,
                                   const ExportOptions& options) {
    // Implementation would be similar to exportDevice but handle multiple devices
    // This would typically be called with a list of devices from DeviceManager
    return false; // TODO: Implement
}

bool ExportManager::exportToCSV(const UsbDevice* device,
                              const std::string& filename,
                              const ExportOptions& options) {
    std::ofstream file(filename);
    if (!file) {
        emit exportError("Failed to open file for writing");
        return false;
    }
    
    // Write headers
    file << "Timestamp,Type,Value\n";
    
    if (options.includeDeviceInfo) {
        auto id = device->identifier();
        file << d->formatDateTime(std::chrono::system_clock::now()) << ",VendorID,0x"
             << std::hex << std::setw(4) << std::setfill('0') << id.vendorId << "\n";
        file << d->formatDateTime(std::chrono::system_clock::now()) << ",ProductID,0x"
             << std::hex << std::setw(4) << std::setfill('0') << id.productId << "\n";
    }
    
    if (options.includePowerStats) {
        auto stats = device->getPowerStats();
        file << d->formatDateTime(std::chrono::system_clock::now()) << ",Current,"
             << stats.currentUsage << "\n";
        file << d->formatDateTime(std::chrono::system_clock::now()) << ",Voltage,"
             << stats.voltage << "\n";
        file << d->formatDateTime(std::chrono::system_clock::now()) << ",Power,"
             << stats.powerUsage << "\n";
    }
    
    if (options.includeBandwidthStats) {
        auto stats = device->getBandwidthStats();
        file << d->formatDateTime(std::chrono::system_clock::now()) << ",BytesRead,"
             << stats.bytesRead << "\n";
        file << d->formatDateTime(std::chrono::system_clock::now()) << ",BytesWritten,"
             << stats.bytesWritten << "\n";
    }
    
    return true;
}

bool ExportManager::exportToJSON(const UsbDevice* device,
                               const std::string& filename,
                               const ExportOptions& options) {
    QJsonObject root;
    root["timestamp"] = QString::fromStdString(
        d->formatDateTime(std::chrono::system_clock::now()));
    
    if (options.includeDeviceInfo) {
        QJsonObject deviceInfo;
        d->writeDeviceInfo(deviceInfo, device);
        root["deviceInfo"] = deviceInfo;
    }
    
    if (options.includePowerStats) {
        QJsonObject powerStats;
        d->writePowerStats(powerStats, device->getPowerStats());
        root["powerStats"] = powerStats;
    }
    
    if (options.includeBandwidthStats) {
        QJsonObject bandwidthStats;
        d->writeBandwidthStats(bandwidthStats, device->getBandwidthStats());
        root["bandwidthStats"] = bandwidthStats;
    }
    
    QJsonDocument doc(root);
    QFile file(QString::fromStdString(filename));
    
    if (!file.open(QIODevice::WriteOnly)) {
        emit exportError("Failed to open file for writing");
        return false;
    }
    
    file.write(doc.toJson());
    return true;
}

bool ExportManager::exportToXML(const UsbDevice* device,
                              const std::string& filename,
                              const ExportOptions& options) {
    QFile file(QString::fromStdString(filename));
    if (!file.open(QIODevice::WriteOnly)) {
        emit exportError("Failed to open file for writing");
        return false;
    }
    
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    
    xml.writeStartDocument();
    xml.writeStartElement("UsbDeviceReport");
    xml.writeAttribute("timestamp",
        QString::fromStdString(d->formatDateTime(std::chrono::system_clock::now())));
    
    if (options.includeDeviceInfo) {
        xml.writeStartElement("DeviceInfo");
        auto id = device->identifier();
        xml.writeTextElement("VendorId", QString::number(id.vendorId, 16));
        xml.writeTextElement("ProductId", QString::number(id.productId, 16));
        xml.writeTextElement("Description",
            QString::fromStdString(device->description()));
        xml.writeEndElement(); // DeviceInfo
    }
    
    if (options.includePowerStats) {
        xml.writeStartElement("PowerStats");
        auto stats = device->getPowerStats();
        xml.writeTextElement("CurrentUsage", QString::number(stats.currentUsage));
        xml.writeTextElement("Voltage", QString::number(stats.voltage));
        xml.writeTextElement("PowerUsage", QString::number(stats.powerUsage));
        xml.writeEndElement(); // PowerStats
    }
    
    if (options.includeBandwidthStats) {
        xml.writeStartElement("BandwidthStats");
        auto stats = device->getBandwidthStats();
        xml.writeTextElement("BytesRead", QString::number(stats.bytesRead));
        xml.writeTextElement("BytesWritten", QString::number(stats.bytesWritten));
        xml.writeTextElement("ReadSpeed", QString::number(stats.readSpeed));
        xml.writeTextElement("WriteSpeed", QString::number(stats.writeSpeed));
        xml.writeEndElement(); // BandwidthStats
    }
    
    xml.writeEndElement(); // UsbDeviceReport
    xml.writeEndDocument();
    
    return true;
}

bool ExportManager::exportToHTML(const UsbDevice* device,
                               const std::string& filename,
                               const ExportOptions& options) {
    std::stringstream html;
    d->generateHTML(html, device, options);
    
    std::ofstream file(filename);
    if (!file) {
        emit exportError("Failed to open file for writing");
        return false;
    }
    
    file << html.str();
    return true;
}

bool ExportManager::exportToPDF(const UsbDevice* device,
                              const std::string& filename,
                              const ExportOptions& options) {
    // For PDF export, we'll generate HTML first and then convert it
    // This would typically use a library like QPrinter or wkhtmltopdf
    // For now, we'll just return false
    emit exportError("PDF export not implemented");
    return false;
}

ExportOptions ExportManager::loadTemplate(const std::string& name) const {
    auto it = d->templates.find(name);
    if (it != d->templates.end()) {
        return it->second;
    }
    return ExportOptions{}; // Return default options if template not found
}

std::vector<std::string> ExportManager::getTemplateNames() const {
    std::vector<std::string> names;
    names.reserve(d->templates.size());
    
    for (const auto& [name, _] : d->templates) {
        names.push_back(name);
    }
    
    return names;
}

void ExportManager::deleteTemplate(const std::string& name) {
    d->templates.erase(name);
}

}
