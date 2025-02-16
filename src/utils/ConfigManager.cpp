#include "ConfigManager.hpp"
#include <QSettings>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

namespace usb_monitor {

template<class... Ts> 
struct overloaded : Ts... { 
    using Ts::operator()...; 
};
template<class... Ts> 
overloaded(Ts...) -> overloaded<Ts...>;

class ConfigManager::Private {
public:
    std::map<std::string, ConfigValue> globalSettings;
    std::map<std::string, std::map<std::string, ConfigValue>> deviceSettings;
    
    // Utility functions for JSON conversion
    QJsonValue toJsonValue(const ConfigValue& value) const {
        return std::visit(overloaded{
	    [](bool b) -> QJsonValue { return b; },
	    [](int i) -> QJsonValue { return i; },
	    [](double d) -> QJsonValue { return d; },
	    [](const std::string& s) -> QJsonValue { return QString::fromStdString(s); }
	}, value);
    }
    
    ConfigValue fromJsonValue(const QJsonValue& json) const {
        switch (json.type()) {
            case QJsonValue::Bool:
                return json.toBool();
            case QJsonValue::Double:
                return json.toInt(); // Try integer first
            case QJsonValue::String:
                return json.toString().toStdString();
            default:
                return false; // Default value for unsupported types
        }
    }
    
    void setDefaults() {
        // Global settings defaults
        globalSettings = {
            {"autoConnect", true},
            {"pollInterval", 1000},
            {"maxHistorySize", 1000},
            {"logLevel", 2},
            {"uiTheme", std::string("system")},
            {"minimizeToTray", true}
        };
    }
    
    std::string makeDeviceKey(uint16_t vendorId, uint16_t productId) const {
        char buffer[10];
        snprintf(buffer, sizeof(buffer), "%04x:%04x", vendorId, productId);
        return buffer;
    }
};

ConfigManager::ConfigManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
    d->setDefaults();
}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::getBool(const std::string& key, bool defaultValue) const {
    auto it = d->globalSettings.find(key);
    if (it != d->globalSettings.end()) {
        if (std::holds_alternative<bool>(it->second)) {
            return std::get<bool>(it->second);
        }
    }
    return defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) const {
    auto it = d->globalSettings.find(key);
    if (it != d->globalSettings.end()) {
        if (std::holds_alternative<int>(it->second)) {
            return std::get<int>(it->second);
        }
    }
    return defaultValue;
}

double ConfigManager::getDouble(const std::string& key, double defaultValue) const {
    auto it = d->globalSettings.find(key);
    if (it != d->globalSettings.end()) {
        if (std::holds_alternative<double>(it->second)) {
            return std::get<double>(it->second);
        }
    }
    return defaultValue;
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = d->globalSettings.find(key);
    if (it != d->globalSettings.end()) {
        if (std::holds_alternative<std::string>(it->second)) {
            return std::get<std::string>(it->second);
        }
    }
    return defaultValue;
}

void ConfigManager::setBool(const std::string& key, bool value) {
    d->globalSettings[key] = value;
    emit configChanged(key);
}

void ConfigManager::setInt(const std::string& key, int value) {
    d->globalSettings[key] = value;
    emit configChanged(key);
}

void ConfigManager::setDouble(const std::string& key, double value) {
    d->globalSettings[key] = value;
    emit configChanged(key);
}

void ConfigManager::setString(const std::string& key, const std::string& value) {
    d->globalSettings[key] = value;
    emit configChanged(key);
}

std::map<std::string, ConfigValue> ConfigManager::getDeviceSettings(
    uint16_t vendorId, uint16_t productId) const {
    std::string key = d->makeDeviceKey(vendorId, productId);
    auto it = d->deviceSettings.find(key);
    if (it != d->deviceSettings.end()) {
        return it->second;
    }
    return {};
}

void ConfigManager::setDeviceSettings(
    uint16_t vendorId, uint16_t productId,
    const std::map<std::string, ConfigValue>& settings) {
    std::string key = d->makeDeviceKey(vendorId, productId);
    d->deviceSettings[key] = settings;
    emit deviceConfigChanged(vendorId, productId);
}

bool ConfigManager::loadFromFile(const std::string& filename) {
    QFile file(QString::fromStdString(filename));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) {
        return false;
    }
    
    QJsonObject root = doc.object();
    
    // Load global settings
    QJsonObject globals = root["global"].toObject();
    for (auto it = globals.begin(); it != globals.end(); ++it) {
        std::string key = it.key().toStdString();
        d->globalSettings[key] = d->fromJsonValue(it.value());
    }
    
    // Load device settings
    QJsonObject devices = root["devices"].toObject();
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        std::string deviceKey = it.key().toStdString();
        QJsonObject deviceSettings = it.value().toObject();
        
        std::map<std::string, ConfigValue> settings;
        for (auto sit = deviceSettings.begin(); sit != deviceSettings.end(); ++sit) {
            std::string key = sit.key().toStdString();
            settings[key] = d->fromJsonValue(sit.value());
        }
        
        d->deviceSettings[deviceKey] = std::move(settings);
    }
    
    return true;
}

bool ConfigManager::saveToFile(const std::string& filename) const {
    QJsonObject root;
    
    // Save global settings
    QJsonObject globals;
    for (const auto& [key, value] : d->globalSettings) {
        globals[QString::fromStdString(key)] = d->toJsonValue(value);
    }
    root["global"] = globals;
    
    // Save device settings
    QJsonObject devices;
    for (const auto& [deviceKey, settings] : d->deviceSettings) {
        QJsonObject deviceSettings;
        for (const auto& [key, value] : settings) {
            deviceSettings[QString::fromStdString(key)] = d->toJsonValue(value);
        }
        devices[QString::fromStdString(deviceKey)] = deviceSettings;
    }
    root["devices"] = devices;
    
    QJsonDocument doc(root);
    
    QFile file(QString::fromStdString(filename));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    file.write(doc.toJson());
    return true;
}

void ConfigManager::resetToDefaults() {
    d->setDefaults();
    d->deviceSettings.clear();
    
    // Notify about changes
    for (const auto& [key, _] : d->globalSettings) {
        emit configChanged(key);
    }
}

}
