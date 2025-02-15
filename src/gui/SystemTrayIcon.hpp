// src/gui/SystemTrayIcon.hpp
#pragma once
#include <QSystemTrayIcon>
#include <memory>

class QMenu;

namespace usb_monitor {

class MainWindow;

class SystemTrayIcon : public QSystemTrayIcon {
    Q_OBJECT

public:
    explicit SystemTrayIcon(MainWindow* mainWindow);
    ~SystemTrayIcon();

private slots:
    void handleActivated(QSystemTrayIcon::ActivationReason reason);
    void handleDeviceAdded(std::shared_ptr<class UsbDevice> device);
    void handleDeviceRemoved(std::shared_ptr<class UsbDevice> device);
    void showNotification(const QString& title, const QString& message);

private:
    void createMenu();
    void updateMenu();

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace usb_monitor
