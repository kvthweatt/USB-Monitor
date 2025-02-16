#include "DeviceAuthorizer.hpp"
#include "../core/UsbDevice.hpp"
#include <QMessageBox>
#include <QApplication>
#include <QTimer>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <deque>
#include <mutex>
#include <map>

namespace usb_monitor {

struct DeviceAuthState {
    bool isAuthorized{false};
    std::chrono::system_clock::time_point lastAuthAttempt;
    std::deque<AuthorizationResult> history;
    size_t maxHistorySize{100};
};

class DeviceAuthorizer::Private {
public:
    AuthorizationPolicy policy;
    std::map<const UsbDevice*, DeviceAuthState> deviceStates;
    std::vector<std::string> trustedCertificates;
    std::map<std::string, std::function<AuthorizationResult(UsbDevice*)>> customMethods;
    mutable std::mutex stateMutex;
    
    void pruneHistory(DeviceAuthState& state) {
        while (state.history.size() > state.maxHistorySize) {
            state.history.pop_front();
        }
    }
    
    bool isAuthorizationExpired(const DeviceAuthState& state) const {
        if (!state.isAuthorized) return true;
        
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
            now - state.lastAuthAttempt).count();
            
        // Re-authorize every 24 hours
        return elapsed >= 24;
    }
    
    AuthorizationResult createResult(bool authorized, 
                                   const std::string& reason,
                                   AuthorizationMethod method) {
        AuthorizationResult result;
        result.authorized = authorized;
        result.reason = reason;
        result.timestamp = std::chrono::system_clock::now();
        result.method = method;
        return result;
    }
    
    bool validateCertificate(const std::string& certPath) {
        FILE* fp = fopen(certPath.c_str(), "r");
        if (!fp) return false;
        
        X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
        fclose(fp);
        
        if (!cert) return false;
        
        // Basic validation
        bool valid = true;
        
        // Check expiration
        if (X509_cmp_current_time(X509_get_notBefore(cert)) >= 0 ||
            X509_cmp_current_time(X509_get_notAfter(cert)) <= 0) {
            valid = false;
        }
        
        // Check signature
        EVP_PKEY* pkey = X509_get_pubkey(cert);
        if (pkey) {
            if (X509_verify(cert, pkey) <= 0) {
                valid = false;
            }
            EVP_PKEY_free(pkey);
        } else {
            valid = false;
        }
        
        X509_free(cert);
        return valid;
    }
};

DeviceAuthorizer::DeviceAuthorizer(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>()) {
    
    // Set default policy
    d->policy.autoAuthorizeKnownDevices = true;
    d->policy.requireUserConfirmation = true;
    d->policy.checkDeviceCertificates = false;
    d->policy.enforceSystemPolicies = true;
    d->policy.authorizationTimeout = std::chrono::seconds(30);
}

DeviceAuthorizer::~DeviceAuthorizer() = default;

AuthorizationResult DeviceAuthorizer::authorizeDevice(UsbDevice* device) {
    if (!device) {
        return d->createResult(false, "Invalid device", AuthorizationMethod::Automatic);
    }
    
    std::lock_guard<std::mutex> lock(d->stateMutex);
    auto& state = d->deviceStates[device];
    
    // Check if already authorized and not expired
    if (state.isAuthorized && !d->isAuthorizationExpired(state)) {
        return d->createResult(true, "Already authorized", AuthorizationMethod::Automatic);
    }
    
    // Reset authorization state
    state.isAuthorized = false;
    state.lastAuthAttempt = std::chrono::system_clock::now();
    
    AuthorizationResult result;
    
    // Try automatic authorization for known devices
    if (d->policy.autoAuthorizeKnownDevices) {
        auto id = device->identifier();
        auto desc = device->description();
        
        // Check if this is a common/known device type
        bool isKnownDevice = false;
        switch (device->deviceClass()) {
            case DeviceClass::HID:
            case DeviceClass::Hub:
            case DeviceClass::Printer:
            case DeviceClass::MassStorage:
                isKnownDevice = true;
                break;
            default:
                break;
        }
        
        if (isKnownDevice) {
            result = d->createResult(true, "Known device type", 
                                   AuthorizationMethod::Automatic);
            state.isAuthorized = true;
            state.history.push_back(result);
            d->pruneHistory(state);
            return result;
        }
    }
    
    // Check system policies
    if (d->policy.enforceSystemPolicies) {
        if (!checkSystemPolicies(device)) {
            result = d->createResult(false, "System policy violation",
                                   AuthorizationMethod::SystemPolicy);
            state.history.push_back(result);
            d->pruneHistory(state);
            return result;
        }
    }
    
    // Check device certificate
    if (d->policy.checkDeviceCertificates) {
        if (!validateDeviceCertificate(device)) {
            result = d->createResult(false, "Certificate validation failed",
                                   AuthorizationMethod::Certificate);
            state.history.push_back(result);
            d->pruneHistory(state);
            return result;
        }
    }
    
    // Try custom authorization methods
    auto id = device->identifier();
    std::string deviceKey = std::to_string(id.vendorId) + ":" + 
                           std::to_string(id.productId);
                           
    auto methodIt = d->customMethods.find(deviceKey);
    if (methodIt != d->customMethods.end()) {
        result = methodIt->second(device);
        if (!result.authorized) {
            state.history.push_back(result);
            d->pruneHistory(state);
            return result;
        }
    }
    
    // Prompt user if required
    if (d->policy.requireUserConfirmation) {
        result = promptUserForAuthorization(device);
        state.history.push_back(result);
        d->pruneHistory(state);
        
        if (result.authorized) {
            state.isAuthorized = true;
        }
        
        return result;
    }
    
    // Default to authorized if all checks pass
    result = d->createResult(true, "All checks passed",
                           AuthorizationMethod::Automatic);
    state.isAuthorized = true;
    state.history.push_back(result);
    d->pruneHistory(state);
    
    return result;
}

void DeviceAuthorizer::revokeAuthorization(UsbDevice* device) {
    if (!device) return;
    
    std::lock_guard<std::mutex> lock(d->stateMutex);
    auto& state = d->deviceStates[device];
    
    if (state.isAuthorized) {
        state.isAuthorized = false;
        state.lastAuthAttempt = std::chrono::system_clock::now();
        
        AuthorizationResult result = d->createResult(
            false, "Authorization revoked", AuthorizationMethod::Automatic);
        state.history.push_back(result);
        d->pruneHistory(state);
        
        emit deviceAuthorizationRevoked(device);
    }
}

bool DeviceAuthorizer::isAuthorized(const UsbDevice* device) const {
    if (!device) return false;
    
    std::lock_guard<std::mutex> lock(d->stateMutex);
    auto it = d->deviceStates.find(device);
    if (it != d->deviceStates.end()) {
        return it->second.isAuthorized && !d->isAuthorizationExpired(it->second);
    }
    return false;
}

void DeviceAuthorizer::setAuthorizationPolicy(const AuthorizationPolicy& policy) {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    d->policy = policy;
    emit policyChanged();
}

AuthorizationPolicy DeviceAuthorizer::getAuthorizationPolicy() const {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    return d->policy;
}

bool DeviceAuthorizer::addTrustedCertificate(const std::string& certPath) {
    if (!d->validateCertificate(certPath)) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(d->stateMutex);
    d->trustedCertificates.push_back(certPath);
    return true;
}

void DeviceAuthorizer::removeTrustedCertificate(const std::string& certId) {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    auto it = std::remove(d->trustedCertificates.begin(),
                         d->trustedCertificates.end(),
                         certId);
    d->trustedCertificates.erase(it, d->trustedCertificates.end());
}

std::vector<std::string> DeviceAuthorizer::getTrustedCertificates() const {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    return d->trustedCertificates;
}

void DeviceAuthorizer::registerCustomAuthorizationMethod(
    const std::string& name,
    std::function<AuthorizationResult(UsbDevice*)> method) {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    d->customMethods[name] = std::move(method);
}

std::vector<AuthorizationResult> DeviceAuthorizer::getAuthorizationHistory(
    const UsbDevice* device) const {
    std::vector<AuthorizationResult> result;
    
    std::lock_guard<std::mutex> lock(d->stateMutex);
    auto it = d->deviceStates.find(device);
    if (it != d->deviceStates.end()) {
        result.assign(it->second.history.begin(), it->second.history.end());
    }
    
    return result;
}

void DeviceAuthorizer::clearAuthorizationHistory(const UsbDevice* device) {
    std::lock_guard<std::mutex> lock(d->stateMutex);
    auto it = d->deviceStates.find(device);
    if (it != d->deviceStates.end()) {
        it->second.history.clear();
    }
}

bool DeviceAuthorizer::validateDeviceCertificate(const UsbDevice* device) const {
    if (!device) return false;
    
    // In a real implementation, this would validate device-provided certificates
    // against trusted certificates. For now, we just check if certificates
    // are required and available.
    
    std::lock_guard<std::mutex> lock(d->stateMutex);
    if (d->policy.checkDeviceCertificates && d->trustedCertificates.empty()) {
        return false;
    }
    
    return true;
}

bool DeviceAuthorizer::checkSystemPolicies(const UsbDevice* device) const {
    if (!device) return false;
    
    // Basic system policy checks
    auto id = device->identifier();
    
    // Check for high-speed devices on USB 1.1 ports
    if (libusb_get_device_speed(device->nativeDevice()) > LIBUSB_SPEED_FULL) {
        auto config = device->deviceClass();
        if (config == DeviceClass::MassStorage || 
            config == DeviceClass::Video ||
            config == DeviceClass::AudioVideo) {
            return false;
        }
    }
    
    // Check for potentially dangerous device classes
    switch (device->deviceClass()) {
        case DeviceClass::VendorSpecific:
        case DeviceClass::Diagnostic:
        case DeviceClass::Wireless:
            // These might need special attention
            return false;
            
        default:
            break;
    }
    
    return true;
}

AuthorizationResult DeviceAuthorizer::promptUserForAuthorization(UsbDevice* device) {
    if (!device) {
        return d->createResult(false, "Invalid device", 
                             AuthorizationMethod::UserPrompt);
    }
    
    // Create message box for user prompt
    auto description = QString::fromStdString(device->description());
    auto id = device->identifier();
    
    QString message = QString("Do you want to authorize the following USB device?\n\n"
                            "Device: %1\n"
                            "Vendor ID: 0x%2\n"
                            "Product ID: 0x%3\n"
                            "Bus: %4 Address: %5")
                            .arg(description)
                            .arg(id.vendorId, 4, 16, QChar('0'))
                            .arg(id.productId, 4, 16, QChar('0'))
                            .arg(id.busNumber)
                            .arg(id.deviceAddress);
    
    QMessageBox box(QMessageBox::Question,
                   "USB Device Authorization",
                   message,
                   QMessageBox::Yes | QMessageBox::No);
    
    // Set timeout if specified
    QTimer timer;
    if (d->policy.authorizationTimeout.count() > 0) {
        timer.setInterval(d->policy.authorizationTimeout);
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout,
                        &box, &QMessageBox::reject);
        timer.start();
    }
    
    bool authorized = (box.exec() == QMessageBox::Yes);
    
    return d->createResult(authorized,
                          authorized ? "User authorized device" : "User denied authorization",
                          AuthorizationMethod::UserPrompt);
}

} // namespace usb_monitor
