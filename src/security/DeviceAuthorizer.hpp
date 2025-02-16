#pragma once
#include <QObject>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

namespace usb_monitor {

class UsbDevice;

enum class AuthorizationMethod {
    Automatic,
    UserPrompt,
    SystemPolicy,
    Certificate,
    Custom
};

struct AuthorizationPolicy {
    bool autoAuthorizeKnownDevices{true};
    bool requireUserConfirmation{false};
    bool checkDeviceCertificates{false};
    bool enforceSystemPolicies{true};
    std::chrono::seconds authorizationTimeout{30};
};

struct AuthorizationResult {
    bool authorized{false};
    std::string reason;
    std::chrono::system_clock::time_point timestamp;
    AuthorizationMethod method;
};

class DeviceAuthorizer : public QObject {
    Q_OBJECT

public:
    explicit DeviceAuthorizer(QObject* parent = nullptr);
    ~DeviceAuthorizer() override;

    // Authorization control
    AuthorizationResult authorizeDevice(UsbDevice* device);
    void revokeAuthorization(UsbDevice* device);
    bool isAuthorized(const UsbDevice* device) const;
    
    // Policy management
    void setAuthorizationPolicy(const AuthorizationPolicy& policy);
    AuthorizationPolicy getAuthorizationPolicy() const;
    
    // Certificate management
    bool addTrustedCertificate(const std::string& certPath);
    void removeTrustedCertificate(const std::string& certId);
    std::vector<std::string> getTrustedCertificates() const;
    
    // Custom authorization
    void registerCustomAuthorizationMethod(
        const std::string& name,
        std::function<AuthorizationResult(UsbDevice*)> method);
    
    // Authorization history
    std::vector<AuthorizationResult> getAuthorizationHistory(
        const UsbDevice* device) const;
    void clearAuthorizationHistory(const UsbDevice* device);

signals:
    void deviceAuthorized(const UsbDevice* device);
    void deviceAuthorizationRevoked(const UsbDevice* device);
    void authorizationFailed(const UsbDevice* device, const std::string& reason);
    void policyChanged();

private:
    bool validateDeviceCertificate(const UsbDevice* device) const;
    bool checkSystemPolicies(const UsbDevice* device) const;
    AuthorizationResult promptUserForAuthorization(UsbDevice* device);
    void logAuthorizationAttempt(const UsbDevice* device, 
                                const AuthorizationResult& result);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace usb_monitor
