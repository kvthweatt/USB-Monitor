#include "MainWindow.hpp"
#include "../core/DeviceManager.hpp"
#include "../core/UsbDevice.hpp"
#include "DeviceTreeWidget.hpp"
#include "TopologyView.hpp"
#include "SystemTrayIcon.hpp"
#include "../security/SecurityManager.hpp"
#include "../analysis/ProtocolAnalyzer.hpp"
#include "../analysis/BenchmarkTool.hpp"
#include "../utils/ConfigManager.hpp"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QMessageBox>
#include <QSettings>
#include <QCloseEvent>
#include <QApplication>

namespace usb_monitor {

class MainWindow::Private {
public:
    std::unique_ptr<DeviceManager> deviceManager;
    std::unique_ptr<SecurityManager> securityManager;
    std::unique_ptr<ProtocolAnalyzer> protocolAnalyzer;
    std::unique_ptr<BenchmarkTool> benchmarkTool;
    std::unique_ptr<ConfigManager> configManager;
    std::unique_ptr<SystemTrayIcon> systemTrayIcon;
    
    DeviceTreeWidget* deviceTree{nullptr};
    TopologyView* topologyView{nullptr};
    QDockWidget* detailsDock{nullptr};
    QDockWidget* analysisDock{nullptr};
    
    std::shared_ptr<UsbDevice> selectedDevice;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , d(std::make_unique<Private>()) {
    
    setWindowTitle("USB Device Monitor");
    resize(1200, 800);
    
    // Initialize managers
    d->deviceManager = std::make_unique<DeviceManager>();
    d->securityManager = std::make_unique<SecurityManager>();
    d->protocolAnalyzer = std::make_unique<ProtocolAnalyzer>();
    d->benchmarkTool = std::make_unique<BenchmarkTool>();
    d->configManager = std::make_unique<ConfigManager>();
    
    setupUi();
    loadSettings();
    
    // Initialize system tray after UI setup
    d->systemTrayIcon = std::make_unique<SystemTrayIcon>(this);
    d->systemTrayIcon->show();
    
    setupDeviceMonitoring();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    setupMenus();
    setupToolbar();
    setupDockWidgets();
    setupStatusBar();
    
    // Create central widget with device tree
    d->deviceTree = new DeviceTreeWidget(this);
    setCentralWidget(d->deviceTree);
    
    // Create topology view
    d->topologyView = new TopologyView(this);
    auto topologyDock = new QDockWidget("Device Topology", this);
    topologyDock->setWidget(d->topologyView);
    addDockWidget(Qt::RightDockWidgetArea, topologyDock);
}

void MainWindow::setupMenus() {
    // File menu
    auto fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Export Data...", this, &MainWindow::exportData);
    fileMenu->addSeparator();
    fileMenu->addAction("&Settings...", this, &MainWindow::showSettings);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);
    
    // View menu
    auto viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("&Device Details", this, &MainWindow::showDeviceDetails);
    viewMenu->addAction("&Power Management", this, &MainWindow::showPowerManagement);
    viewMenu->addAction("&Bandwidth Analysis", this, &MainWindow::showBandwidthAnalysis);
    
    // Tools menu
    auto toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("&Security Settings...", this, &MainWindow::showSecuritySettings);
    toolsMenu->addAction("&Protocol Analysis", this, &MainWindow::showProtocolAnalysis);
    toolsMenu->addAction("&Benchmark", this, &MainWindow::runBenchmark);
    
    // Help menu
    auto helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About", this, &MainWindow::showAbout);
}

void MainWindow::setupToolbar() {
    auto toolbar = addToolBar("Main Toolbar");
    toolbar->setMovable(false);
    
    toolbar->addAction("Refresh", d->deviceTree, &DeviceTreeWidget::refresh);
    toolbar->addSeparator();
    toolbar->addAction("Details", this, &MainWindow::showDeviceDetails);
    toolbar->addAction("Power", this, &MainWindow::showPowerManagement);
    toolbar->addAction("Bandwidth", this, &MainWindow::showBandwidthAnalysis);
    toolbar->addSeparator();
    toolbar->addAction("Security", this, &MainWindow::showSecuritySettings);
    toolbar->addAction("Analysis", this, &MainWindow::showProtocolAnalysis);
    toolbar->addAction("Benchmark", this, &MainWindow::runBenchmark);
}

void MainWindow::setupDockWidgets() {
    // Details dock
    d->detailsDock = new QDockWidget("Device Details", this);
    d->detailsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, d->detailsDock);
    
    // Analysis dock
    d->analysisDock = new QDockWidget("Analysis", this);
    d->analysisDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, d->analysisDock);
}

void MainWindow::setupStatusBar() {
    statusBar()->showMessage("Ready");
}

void MainWindow::setupDeviceMonitoring() {
    // Connect device tree to device manager
    d->deviceTree->setDeviceManager(d->deviceManager.get());
    d->topologyView->setDeviceManager(d->deviceManager.get());
    
    // Handle device selection
    connect(d->deviceTree, &DeviceTreeWidget::deviceSelected,
            this, &MainWindow::handleDeviceSelected);
    
    // Update status bar with device events
    connect(d->deviceManager.get(), &DeviceManager::deviceAdded,
            this, [this](const std::shared_ptr<UsbDevice>& device) {
        statusBar()->showMessage("Device connected: " + 
                               QString::fromStdString(device->description()), 3000);
    });
    
    connect(d->deviceManager.get(), &DeviceManager::deviceRemoved,
            this, [this](const std::shared_ptr<UsbDevice>& device) {
        statusBar()->showMessage("Device disconnected: " + 
                               QString::fromStdString(device->description()), 3000);
    });
}

void MainWindow::handleDeviceSelected(const std::shared_ptr<UsbDevice>& device) {
    d->selectedDevice = device;
    showDeviceDetails();
}

void MainWindow::loadSettings() {
    QSettings settings;
    restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
    restoreState(settings.value("mainwindow/state").toByteArray());
}

void MainWindow::saveSettings() {
    QSettings settings;
    settings.setValue("mainwindow/geometry", saveGeometry());
    settings.setValue("mainwindow/state", saveState());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    event->accept();
}

// Actions
void MainWindow::showDeviceDetails() {
    if (!d->selectedDevice) {
        QMessageBox::information(this, "Information", "Please select a device first.");
        return;
    }
    // Show device details in the details dock
    // Implementation depends on DeviceDetailsWidget class
}

void MainWindow::showPowerManagement() {
    if (!d->selectedDevice) {
        QMessageBox::information(this, "Information", "Please select a device first.");
        return;
    }
    // Show power management interface
    // Implementation depends on PowerManagementWidget class
}

void MainWindow::showBandwidthAnalysis() {
    if (!d->selectedDevice) {
        QMessageBox::information(this, "Information", "Please select a device first.");
        return;
    }
    // Show bandwidth analysis interface
    // Implementation depends on BandwidthAnalysisWidget class
}

void MainWindow::showSecuritySettings() {
    // Show security settings dialog
    // Implementation depends on SecuritySettingsDialog class
}

void MainWindow::showProtocolAnalysis() {
    if (!d->selectedDevice) {
        QMessageBox::information(this, "Information", "Please select a device first.");
        return;
    }
    // Show protocol analysis interface
    // Implementation depends on ProtocolAnalysisWidget class
}

void MainWindow::runBenchmark() {
    if (!d->selectedDevice) {
        QMessageBox::information(this, "Information", "Please select a device first.");
        return;
    }
    // Run benchmark tool
    // Implementation depends on BenchmarkDialog class
}

void MainWindow::exportData() {
    // Show export dialog
    // Implementation depends on ExportDialog class
}

void MainWindow::showSettings() {
    // Show settings dialog
    // Implementation depends on SettingsDialog class
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About USB Device Monitor",
        "USB Device Monitor 2.0.0\n\n"
        "A comprehensive USB device monitoring and management tool.\n\n"
        "Features:\n"
        "- Device monitoring and management\n"
        "- Power consumption tracking\n"
        "- Bandwidth analysis\n"
        "- Security features\n"
        "- Protocol analysis\n"
        "- Device benchmarking\n");
}

} // namespace usb_monitor
