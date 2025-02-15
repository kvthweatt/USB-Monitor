#pragma once
#include <QObject>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

namespace usb_monitor {

class UsbDevice;
class DeviceAuthorizer;

enum class SecurityLevel {
    Low,
    Medium,
    High,
    Custom
};

enum class SecurityEvent {
    DeviceConnected,
    DeviceDisconnected,
    AuthorizationGranted,
    AuthorizationDenied,
    UnauthorizedAccess,
    MaliciousActivityDetected,
    ProtocolViolation,
    PolicyViolation
};

struct SecurityRule {
    uint16_t vendorId;
    uint16_t productId;
    bool isWhitelisted;
    bool requireAuthorization;
    SecurityLevel securityLevel;
    std::vector<std::string> allowedInterfaces;
    std::chrono::system_clock::time_point expiryDate;
};

struct SecurityEventInfo {
    SecurityEvent event;
    std::chrono::system_clock::time_point timestamp;
    std::string deviceId;
    std::string description;
    SecurityLevel securityLevel;
};

class SecurityManager : public QObject {
    Q_OBJECT

public:
    explicit SecurityManager(QObject* parent = nullptr);
    ~SecurityManager();

    // Device security
    bool isDeviceAllowed(const UsbDevice* device) const;
    bool authorizeDevice(UsbDevice* device);
    void revokeAuthorization(UsbDevice* device);
    
    // Security rules
    void addSecurityRule(const SecurityRule& rule);
    void removeSecurityRule(uint16_t vendorId, uint16_t productId);
    std::vector<SecurityRule> getSecurityRules() const;
    void clearSecurityRules();
    
    // Security levels
    void setSecurityLevel(SecurityLevel level);
    SecurityLevel getSecurityLevel() const;
    void setCustomSecurityPolicy(const std::string& policyFile);
    
    // Event management
    std::vector<SecurityEventInfo> getSecurityEvents(
        const std::chrono::system_clock::time_point& start,
        const std::chrono::system_clock::time_point& end) const;
    void clearSecurityEvents();
    
    // Configuration
    bool loadSecurityConfig(const std::string& filename);
    bool saveSecurityConfig(const std::string& filename) const;

signals:
    void securityEventOccurred(const SecurityEventInfo& event);
    void securityLevelChanged(SecurityLevel level);
    void deviceBlocked(const UsbDevice* device, const std::string& reason);
    void configurationChanged();

private:
    void logSecurityEvent(SecurityEvent event, 
                         const UsbDevice* device,
                         const std::string& description);
    bool validateDeviceProtocol(const UsbDevice* device) const;
    void checkDeviceCompliance(const UsbDevice* device);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace usb_monitor
