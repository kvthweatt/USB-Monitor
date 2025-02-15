// src/gui/SystemTrayIcon.cpp
#include "SystemTrayIcon.hpp"
#include "MainWindow.hpp"
#include "DeviceManager.hpp"
#include "UsbDevice.hpp"
#include <QMenu>
#include <QAction>
#include <QApplication>

namespace usb_monitor {

class SystemTrayIcon::Private {
public:
    MainWindow* mainWindow{nullptr};
    QMenu* trayMenu{nullptr};
    QMenu* devicesMenu{nullptr};
    std::map<const UsbDevice*, QAction*> deviceActions;
};

SystemTrayIcon::SystemTrayIcon(MainWindow* mainWindow)
    : QSystemTrayIcon(mainWindow)
    , d(std::make_unique<Private>()) {
    
    d->mainWindow = mainWindow;
    
    // Set icon
    setIcon(QIcon(":/icons/usb.png"));
    setToolTip("USB Device Monitor");
    
    createMenu();
    
    // Connect signals
    connect(this, &QSystemTrayIcon::activated,
            this, &SystemTrayIcon::handleActivated);
    
    // Connect to device manager signals
    auto deviceManager = mainWindow->findChild<DeviceManager*>();
    if (deviceManager) {
        connect(deviceManager, &DeviceManager::deviceAdded,
                this, &SystemTrayIcon::handleDeviceAdded);
        connect(deviceManager, &DeviceManager::deviceRemoved,
                this, &SystemTrayIcon::handleDeviceRemoved);
    }
}

SystemTrayIcon::~SystemTrayIcon() = default;

void SystemTrayIcon::createMenu() {
    d->trayMenu = new QMenu();
    
    // Create devices submenu
    d->devicesMenu = new QMenu("Devices");
    d->trayMenu->addMenu(d->devicesMenu);
    d->trayMenu->addSeparator();
    
    // Add actions
    auto showAction = d->trayMenu->addAction("Show Monitor");
    connect(showAction, &QAction::triggered, d->mainWindow, &MainWindow::show);
    
    auto hideAction = d->trayMenu->addAction("Hide Monitor");
    connect(hideAction, &QAction::triggered, d->mainWindow, &MainWindow::hide);
    
    d->trayMenu->addSeparator();
    
    auto quitAction = d->trayMenu->addAction("Quit");
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    
    setContextMenu(d->trayMenu);
}

void SystemTrayIcon::handleActivated(QSystemTrayIcon::ActivationReason reason) {
    switch (reason) {
        case QSystemTrayIcon::Trigger:
            // Single click - toggle window visibility
            if (d->mainWindow->isVisible()) {
                d->mainWindow->hide();
            } else {
                d->mainWindow->show();
                d->mainWindow->raise();
                d->mainWindow->activateWindow();
            }
            break;
            
        case QSystemTrayIcon::MiddleClick:
            // Middle click - show devices menu
            d->devicesMenu->popup(QCursor::pos());
            break;
            
        default:
            break;
    }
}

void SystemTrayIcon::handleDeviceAdded(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    
    // Create action for device
    auto action = new QAction(QString::fromStdString(device->description()), d->devicesMenu);
    action->setCheckable(true);
    action->setChecked(device->isOpen());
    
    connect(action, &QAction::triggered, [this, device](bool checked) {
        if (checked) {
            device->open();
        } else {
            device->close();
        }
    });
    
    d->deviceActions[device.get()] = action;
    d->devicesMenu->addAction(action);
    
    // Show notification
    showNotification("USB Device Connected",
                    QString::fromStdString(device->description()));
}

void SystemTrayIcon::handleDeviceRemoved(std::shared_ptr<UsbDevice> device) {
    if (!device) return;
    
    // Remove action for device
    auto it = d->deviceActions.find(device.get());
    if (it != d->deviceActions.end()) {
        d->devicesMenu->removeAction(it->second);
        delete it->second;
        d->deviceActions.erase(it);
    }
    
    // Show notification
    showNotification("USB Device Disconnected",
                    QString::fromStdString(device->description()));
}

void SystemTrayIcon::showNotification(const QString& title, const QString& message) {
    if (supportsMessages()) {
        showMessage(title, message, QSystemTrayIcon::Information, 3000);
    }
}

void SystemTrayIcon::updateMenu() {
    // Update device actions
    for (const auto& [device, action] : d->deviceActions) {
        action->setChecked(device->isOpen());
    }
}

} // namespace usb_monitor
