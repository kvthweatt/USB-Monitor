#pragma once
#include <QObject>
#include <memory>
#include <string>
#include <variant>
#include <map>

namespace usb_monitor {

using ConfigValue = std::variant<bool, int, double, std::string>;

class ConfigManager : public QObject {
    Q_OBJECT

public:
    explicit ConfigManager(QObject* parent = nullptr);
    ~ConfigManager();

    // Configuration access
    bool getBool(const std::string& key, bool defaultValue = false) const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    double getDouble(const std::string& key, double defaultValue = 0.0) const;
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    
    void setBool(const std::string& key, bool value);
    void setInt(const std::string& key, int value);
    void setDouble(const std::string& key, double value);
    void setString(const std::string& key, const std::string& value);
    
    // Device-specific settings
    std::map<std::string, ConfigValue> getDeviceSettings(uint16_t vendorId, uint16_t productId) const;
    void setDeviceSettings(uint16_t vendorId, uint16_t productId, 
                          const std::map<std::string, ConfigValue>& settings);
    
    // File operations
    bool loadFromFile(const std::string& filename);
    bool saveToFile(const std::string& filename) const;
    
    // Default settings
    void resetToDefaults();
    
signals:
    void configChanged(const std::string& key);
    void deviceConfigChanged(uint16_t vendorId, uint16_t productId);

private:
    class Private;
    std::unique_ptr<Private> d;
};

}
