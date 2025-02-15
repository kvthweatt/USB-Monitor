#pragma once
#include <QMainWindow>
#include <memory>

namespace usb_monitor {

class DeviceManager;
class DeviceTreeWidget;
class TopologyView;
class SystemTrayIcon;
class SecurityManager;
class ProtocolAnalyzer;
class BenchmarkTool;
class ConfigManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void setupDeviceMonitoring();
    void handleDeviceSelected(const std::shared_ptr<class UsbDevice>& device);
    void showDeviceDetails();
    void showPowerManagement();
    void showBandwidthAnalysis();
    void showSecuritySettings();
    void showProtocolAnalysis();
    void runBenchmark();
    void exportData();
    void showSettings();
    void showAbout();

private:
    void setupUi();
    void setupMenus();
    void setupToolbar();
    void setupDockWidgets();
    void setupStatusBar();
    void loadSettings();
    void saveSettings();

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace usb_monitor
