#include "SecurityManager.hpp"
#include "DeviceAuthorizer.hpp"
#include "../core/UsbDevice.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <deque>

namespace usb_monitor {

struct SecurityState {
    std::vector<SecurityRule> rules;
    std::deque<SecurityEventInfo> events;
    std::map<std::string, bool> authorizedDevices;
    SecurityLevel currentLevel{SecurityLevel::Medium};
    size_t maxEventHistory{10000};
};

class SecurityManager::Private {
public:
    std::unique_ptr<DeviceAuthorizer> authorizer;
    SecurityState state;
    mutable std::mutex stateMutex;
    SecurityManager* q_ptr;
    
    bool matchesRule(const SecurityRule& rule, const UsbDevice* device) const {
        if (!device) return false;
        auto id = device->identifier();
        return (rule.vendorId == id.vendorId && rule.productId == id.productId);
    }
    
    void enforceSecurityLevel(SecurityLevel level) {
        // Adjust security policies based on level
        AuthorizationPolicy policy;
        
        switch (level) {
            case SecurityLevel::Low:
                policy.autoAuthorizeKnownDevices = true;
                policy.requireUserConfirmation = false;
                policy.checkDeviceCertificates = false;
                policy.enforceSystemPolicies = false;
                break;
                
            case SecurityLevel::Medium:
                policy.autoAuthorizeKnownDevices = true;
                policy.requireUserConfirmation = true;
                policy.checkDeviceCertificates = false;
                policy.enforceSystemPolicies = true;
                break;
                
            case SecurityLevel::High:
                policy.autoAuthorizeKnownDevices = false;
                policy.requireUserConfirmation = true;
                policy.checkDeviceCertificates = true;
                policy.enforceSystemPolicies = true;
                policy.authorizationTimeout = std::chrono::seconds(15);
                break;
                
            case SecurityLevel::Custom:
                // Custom policies are loaded from configuration
                return;
        }
        
        authorizer->setAuthorizationPolicy(policy);
    }
    
    bool validateDeviceInterfaces(const UsbDevice* device, const SecurityRule& rule) const {
        if (rule.allowedInterfaces.empty()) return true;
        
        // Get device interfaces
        libusb_config_descriptor* config;
        if (libusb_get_active_config_descriptor(device->nativeDevice(), &config) != 0) {
            return false;
        }
        
        bool valid = true;
        for (int i = 0; i < config->bNumInterfaces; i++) {
            const auto* interface = &config->interface[i];
            for (int j = 0; j < interface->num_altsetting; j++) {
                const auto* setting = &interface->altsetting[j];
                
                // Convert interface class to string identifier
                std::stringstream ss;
                ss << "0x" << std::hex << std::uppercase 
                   << std::setw(2) << std::setfill('0') 
                   << static_cast<int>(setting->bInterfaceClass);
                std::string interfaceId = ss.str();
                
                // Check if interface is allowed
                if (std::find(rule.allowedInterfaces.begin(),
                            rule.allowedInterfaces.end(),
                            interfaceId) == rule.allowedInterfaces.end()) {
                    valid = false;
                    break;
                }
            }
            if (!valid) break;
        }
        
        libusb_free_config_descriptor(config);
        return valid;
    }
    
    SecurityRule findMatchingRule(const UsbDevice* device) const {
        std::lock_guard<std::mutex> lock(stateMutex);
        for (const auto& rule : state.rules) {
            if (matchesRule(rule, device)) {
                return rule;
            }
        }
        return SecurityRule{}; // Return default rule
    }
    
    void pruneEventHistory() {
        std::lock_guard<std::mutex> lock(stateMutex);
        while (state.events.size() > state.maxEventHistory) {
            state.events.pop_front();
        }
    }
    
    bool saveJsonConfig(const std::string& filename) const {
        QJsonObject root;
        
        // Save security level
        root["securityLevel"] = static_cast<int>(state.currentLevel);
        
        // Save rules
        QJsonArray rulesArray;
        for (const auto& rule : state.rules) {
            QJsonObject ruleObj;
            ruleObj["vendorId"] = QString::number(rule.vendorId, 16);
            ruleObj["productId"] = QString::number(rule.productId, 16);
            ruleObj["isWhitelisted"] = rule.isWhitelisted;
            ruleObj["requireAuthorization"] = rule.requireAuthorization;
            ruleObj["securityLevel"] = static_cast<int>(rule.securityLevel);
            
            QJsonArray interfacesArray;
            for (const auto& iface : rule.allowedInterfaces) {
                interfacesArray.append(QString::fromStdString(iface));
            }
            ruleObj["allowedInterfaces"] = interfacesArray;
            
            if (rule.expiryDate != std::chrono::system_clock::time_point{}) {
                auto expiryTime = std::chrono::system_clock::to_time_t(rule.expiryDate);
                ruleObj["expiryDate"] = QString::fromStdString(
                    std::ctime(&expiryTime));
            }
            
            rulesArray.append(ruleObj);
        }
        root["rules"] = rulesArray;
        
        // Write to file
        QFile file(QString::fromStdString(filename));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        
        file.write(QJsonDocument(root).toJson());
        return true;
    }
    
    bool loadJsonConfig(const std::string& filename) {
        QFile file(QString::fromStdString(filename));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isNull()) {
            return false;
        }
        
        QJsonObject root = doc.object();
        
        // Load security level
        if (root.contains("securityLevel")) {
            auto level = static_cast<SecurityLevel>(root["securityLevel"].toInt());
            q_ptr->setSecurityLevel(level);
        }
        
        // Load rules
        std::vector<SecurityRule> newRules;
        QJsonArray rulesArray = root["rules"].toArray();
        for (const auto& value : rulesArray) {
            QJsonObject ruleObj = value.toObject();
            
            SecurityRule rule;
            rule.vendorId = ruleObj["vendorId"].toString().toUInt(nullptr, 16);
            rule.productId = ruleObj["productId"].toString().toUInt(nullptr, 16);
            rule.isWhitelisted = ruleObj["isWhitelisted"].toBool();
            rule.requireAuthorization = ruleObj["requireAuthorization"].toBool();
            rule.securityLevel = static_cast<SecurityLevel>(
                ruleObj["securityLevel"].toInt());
            
            QJsonArray interfacesArray = ruleObj["allowedInterfaces"].toArray();
            for (const auto& iface : interfacesArray) {
                rule.allowedInterfaces.push_back(iface.toString().toStdString());
            }
            
            if (ruleObj.contains("expiryDate")) {
                std::istringstream ss(ruleObj["expiryDate"].toString().toStdString());
                std::tm tm = {};
                ss >> std::get_time(&tm, "%c");
                rule.expiryDate = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
            
            newRules.push_back(rule);
        }
        
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state.rules = std::move(newRules);
        }
        
        return true;
    }
};

SecurityManager::SecurityManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
    d->q_ptr = this;
    d->authorizer = std::make_unique<DeviceAuthorizer>();
    
    // Connect authorizer signals
    connect(d->authorizer.get(), &DeviceAuthorizer::deviceAuthorized,
            [this](const UsbDevice* device) {
        logSecurityEvent(SecurityEvent::AuthorizationGranted, device,
                        "Device authorization granted");
    });
    
    connect(d->authorizer.get(), &DeviceAuthorizer::deviceAuthorizationRevoked,
            [this](const UsbDevice* device) {
        logSecurityEvent(SecurityEvent::AuthorizationDenied, device,
                        "Device authorization revoked");
    });
    
    connect(d->authorizer.get(), &DeviceAuthorizer::authorizationFailed,
            [this](const UsbDevice* device, const std::string& reason) {
        logSecurityEvent(SecurityEvent::AuthorizationDenied, device,
                        "Authorization failed: " + reason);
    });
}

SecurityManager::~SecurityManager() = default;

bool SecurityManager::isDeviceAllowed(const UsbDevice* device) {
    if (!device) return false;
    
    // Check if device is already authorized
    {
        std::lock_guard<std::mutex> lock(d->stateMutex);
        auto id = device->identifier();
        std::string deviceKey = std::to_string(id.vendorId) + ":" + 
                               std::to_string(id.productId);
        auto it = d->state.authorizedDevices.find(deviceKey);
        if (it != d->state.authorizedDevices.end()) {
            return it->second;
        }
    }
    
    // Find matching security rule
    auto rule = d->findMatchingRule(device);
    
    // Check whitelist/blacklist
    if (!rule.isWhitelisted) {
        logSecurityEvent(SecurityEvent::UnauthorizedAccess, device,
                        "Device is not whitelisted");
        return false;
    }
    
    // Validate interfaces
    if (!d->validateDeviceInterfaces(device, rule)) {
        logSecurityEvent(SecurityEvent::PolicyViolation, device,
                        "Device uses unauthorized interfaces");
        return false;
    }
    
    // Check expiry
    if (rule.expiryDate != std::chrono::system_clock::time_point{} &&
        std::chrono::system_clock::now() > rule.expiryDate) {
        logSecurityEvent(SecurityEvent::PolicyViolation, device,
                        "Security rule has expired");
        return false;
    }
    
    return true;
}

bool SecurityManager::authorizeDevice(UsbDevice* device) {
    if (!device) return false;
    
    // Check if device is allowed by security rules
    if (!isDeviceAllowed(device)) {
        emit deviceBlocked(device, "Device is not allowed by security rules");
        return false;
    }
    
    // Perform protocol validation
    if (!validateDeviceProtocol(device)) {
        emit deviceBlocked(device, "Device failed protocol validation");
        return false;
    }
    
    // Attempt authorization
    auto result = d->authorizer->authorizeDevice(device);
    if (!result.authorized) {
        emit deviceBlocked(device, result.reason);
        return false;
    }
    
    // Update authorized devices list
    {
        std::lock_guard<std::mutex> lock(d->stateMutex);
        auto id = device->identifier();
        std::string deviceKey = std::to_string(id.vendorId) + ":" + 
                               std::to_string(id.productId);
        d->state.authorizedDevices[deviceKey] = true;
    }
    
    return true;
}

void SecurityManager::revokeAuthorization(UsbDevice* device) {
    if (!device) return;
    
    d->authorizer->revokeAuthorization(device);
    
    // Update authorized devices list
    {
        std::lock_guard<std::mutex> lock(d->stateMutex);
        auto id = device->identifier();
        std::string deviceKey = std::to_string(id.vendorId) + ":" + 
                               std::to_string(id.productId);
        d->state.authorizedDevices.erase(deviceKey);
    }
}

void SecurityManager::addSecurityRule(const SecurityRule& rule) {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    
    // Remove any existing rule for the same device
    auto it = std::remove_if(d->state.rules.begin(), d->state.rules.end(),
        [&rule](const SecurityRule& existing) {
            return existing.vendorId == rule.vendorId &&
                   existing.productId == rule.productId;
        });
    d->state.rules.erase(it, d->state.rules.end());
    
    // Add new rule
    d->state.rules.push_back(rule);
    
    emit configurationChanged();
}

void SecurityManager::removeSecurityRule(uint16_t vendorId, uint16_t productId) {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    
    auto it = std::remove_if(d->state.rules.begin(), d->state.rules.end(),
        [vendorId, productId](const SecurityRule& rule) {
            return rule.vendorId == vendorId && rule.productId == productId;
        });
    
    if (it != d->state.rules.end()) {
        d->state.rules.erase(it, d->state.rules.end());
        emit configurationChanged();
    }
}

std::vector<SecurityRule> SecurityManager::getSecurityRules() const {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    return d->state.rules;
}

void SecurityManager::clearSecurityRules() {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    d->state.rules.clear();
    emit configurationChanged();
}

void SecurityManager::setSecurityLevel(SecurityLevel level) {
    {
        std::lock_guard<std::mutex> lock(d->stateMutex);
        if (d->state.currentLevel == level) return;
        d->state.currentLevel = level;
    }
    
    d->enforceSecurityLevel(level);
    emit securityLevelChanged(level);
}

SecurityLevel SecurityManager::getSecurityLevel() const {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    return d->state.currentLevel;
}

void SecurityManager::setCustomSecurityPolicy(const std::string& policyFile) {
    // Load custom security policy from file
    if (!loadSecurityConfig(policyFile)) {
        return;
    }
    
    setSecurityLevel(SecurityLevel::Custom);
}

std::vector<SecurityEventInfo> SecurityManager::getSecurityEvents(
    const std::chrono::system_clock::time_point& start,
    const std::chrono::system_clock::time_point& end) const {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    
    std::vector<SecurityEventInfo> result;
    for (const auto& event : d->state.events) {
        if (event.timestamp >= start && event.timestamp <= end) {
            result.push_back(event);
        }
    }
    
    return result;
}

void SecurityManager::clearSecurityEvents() {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    d->state.events.clear();
}

bool SecurityManager::loadSecurityConfig(const std::string& filename) {
    if (!d->loadJsonConfig(filename)) {
        return false;
    }
    
    emit configurationChanged();
    return true;
}

bool SecurityManager::saveSecurityConfig(const std::string& filename) const {
    return d->saveJsonConfig(filename);
}



void SecurityManager::logSecurityEvent(SecurityEvent event,
                                     const UsbDevice* device,
                                     const std::string& description) {
    if (!device) return;
    
    SecurityEventInfo eventInfo;
    eventInfo.event = event;
    eventInfo.timestamp = std::chrono::system_clock::now();
    
    auto id = device->identifier();
    std::stringstream ss;
    ss << std::hex << std::uppercase
       << std::setw(4) << std::setfill('0') << id.vendorId << ":"
       << std::setw(4) << std::setfill('0') << id.productId;
    eventInfo.deviceId = ss.str();
    
    eventInfo.description = description;
    eventInfo.securityLevel = getSecurityLevel();
    
    {
        std::lock_guard<std::mutex> lock(d->stateMutex);
        d->state.events.push_back(eventInfo);
        while (d->state.events.size() > d->state.maxEventHistory) {
            d->state.events.pop_front();
        }
    }
    
    emit securityEventOccurred(eventInfo);
}

bool SecurityManager::validateDeviceProtocol(const UsbDevice* device) {
    if (!device || !device->isOpen()) return false;
    
    // Get device configuration descriptor
    libusb_config_descriptor* config;
    if (libusb_get_active_config_descriptor(device->nativeDevice(), &config) != 0) {
        return false;
    }
    
    bool valid = true;
    
    // Check for suspicious configurations
    if (config->bNumInterfaces > 32) {  // Unusually high number of interfaces
        valid = false;
        logSecurityEvent(SecurityEvent::ProtocolViolation, device,
                        "Suspicious number of interfaces");
    }
    
    // Check interface descriptors
    for (int i = 0; i < config->bNumInterfaces && valid; i++) {
        const auto* interface = &config->interface[i];
        
        // Check for suspicious alternate settings
        if (interface->num_altsetting > 16) {  // Unusually high number
            valid = false;
            logSecurityEvent(SecurityEvent::ProtocolViolation, device,
                           "Suspicious number of alternate settings");
            break;
        }
        
        // Validate each alternate setting
        for (int j = 0; j < interface->num_altsetting && valid; j++) {
            const auto* setting = &interface->altsetting[j];
            
            // Check for valid class codes
            switch (setting->bInterfaceClass) {
                case LIBUSB_CLASS_PER_INTERFACE:
                case LIBUSB_CLASS_AUDIO:
                case LIBUSB_CLASS_COMM:
                case LIBUSB_CLASS_HID:
                case LIBUSB_CLASS_PRINTER:
                case LIBUSB_CLASS_MASS_STORAGE:
                case LIBUSB_CLASS_HUB:
                case LIBUSB_CLASS_DATA:
                case LIBUSB_CLASS_VIDEO:
                    break;  // These are common/valid classes
                    
                default:
                    // Log warning for unknown class codes
                    if (setting->bInterfaceClass < LIBUSB_CLASS_VENDOR_SPEC) {
                        valid = false;
                        logSecurityEvent(SecurityEvent::ProtocolViolation, device,
                                       "Unknown interface class detected");
                        break;
                    }
            }
            
            // Validate endpoints
            for (int k = 0; k < setting->bNumEndpoints && valid; k++) {
                const auto* endpoint = &setting->endpoint[k];
                
                // Check for valid transfer types
                switch (endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) {
                    case LIBUSB_TRANSFER_TYPE_CONTROL:
                    case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
                    case LIBUSB_TRANSFER_TYPE_BULK:
                    case LIBUSB_TRANSFER_TYPE_INTERRUPT:
                        break;
                        
                    default:
                        valid = false;
                        logSecurityEvent(SecurityEvent::ProtocolViolation, device,
                                       "Invalid endpoint transfer type");
                        break;
                }
                
                // Check for reasonable max packet size
                if (endpoint->wMaxPacketSize > 16384) {  // Unusually large
                    valid = false;
                    logSecurityEvent(SecurityEvent::ProtocolViolation, device,
                                   "Suspicious max packet size");
                    break;
                }
            }
        }
    }
    
    libusb_free_config_descriptor(config);
    return valid;
}

void SecurityManager::checkDeviceCompliance(const UsbDevice* device) {
    if (!device) return;
    
    auto rule = d->findMatchingRule(device);
    
    // Check interface compliance
    if (!d->validateDeviceInterfaces(device, rule)) {
        logSecurityEvent(SecurityEvent::PolicyViolation, device,
                        "Non-compliant interface detected");
        emit deviceBlocked(device, "Device violates interface restrictions");
    }
    
    // Check protocol compliance
    if (!validateDeviceProtocol(device)) {
        logSecurityEvent(SecurityEvent::ProtocolViolation, device,
                        "Protocol validation failed");
        emit deviceBlocked(device, "Device violates USB protocol specifications");
    }
    
    // Additional compliance checks can be added here
}

}
