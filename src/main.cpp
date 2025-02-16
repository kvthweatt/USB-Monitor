#include "gui/MainWindow.hpp"
#include "core/DeviceManager.hpp"
#include "utils/ConfigManager.hpp"
#include "core/Logger.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QMessageBox>
#include <QDir>
#include <iostream>

using namespace usb_monitor;

void setupCommandLineParser(QCommandLineParser& parser) {
    parser.setApplicationDescription("USB Device Monitor");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add command line options
    QCommandLineOption minimizedOption(
        QStringList() << "m" << "minimized",
        "Start the application minimized to system tray."
    );
    parser.addOption(minimizedOption);

    QCommandLineOption configOption(
        QStringList() << "c" << "config",
        "Specify configuration file path.",
        "config"
    );
    parser.addOption(configOption);

    QCommandLineOption logFileOption(
        QStringList() << "l" << "log-file",
        "Specify log file path.",
        "log-file"
    );
    parser.addOption(logFileOption);

    QCommandLineOption logLevelOption(
        QStringList() << "v" << "verbosity",
        "Set log level (0-4: debug, info, warning, error, critical).",
        "level",
        "1"
    );
    parser.addOption(logLevelOption);
}

void initializeLogger(const QCommandLineParser& parser) {
    auto& logger = Logger::instance();

    // Set log file if specified
    if (parser.isSet("log-file")) {
        logger.setLogFile(parser.value("log-file").toStdString());
        logger.setLogDestination(LogDestination::All);
    }

    // Set log level
    if (parser.isSet("verbosity")) {
        int level = parser.value("verbosity").toInt();
        switch (level) {
            case 0: logger.setLogLevel(LogLevel::Debug); break;
            case 1: logger.setLogLevel(LogLevel::Info); break;
            case 2: logger.setLogLevel(LogLevel::Warning); break;
            case 3: logger.setLogLevel(LogLevel::Error); break;
            case 4: logger.setLogLevel(LogLevel::Critical); break;
            default: logger.setLogLevel(LogLevel::Info); break;
        }
    }

    LOG_INFO("Application starting...");
}

bool loadConfiguration(ConfigManager& config, const QCommandLineParser& parser) {
    QString configPath;
    
    if (parser.isSet("config")) {
        configPath = parser.value("config");
    } else {
        // Default config locations
        QStringList configLocations = {
            QDir::currentPath() + "/config.json",
            QDir::homePath() + "/.config/usb-monitor/config.json",
            "/etc/usb-monitor/config.json"
        };

        for (const auto& path : configLocations) {
            if (QFile::exists(path)) {
                configPath = path;
                break;
            }
        }
    }

    if (!configPath.isEmpty()) {
        if (!config.loadFromFile(configPath.toStdString())) {
            LOG_WARNING("Failed to load configuration from " + configPath.toStdString());
            return false;
        }
        LOG_INFO("Loaded configuration from " + configPath.toStdString());
        return true;
    }

    LOG_INFO("No configuration file found, using defaults");
    return true;
}

void handleUnexpectedExceptions() {
    try {
        throw;  // Rethrow the current exception
    } catch (const std::exception& e) {
        LOG_CRITICAL("Unhandled exception: " + std::string(e.what()));
        QMessageBox::critical(nullptr, "Critical Error",
            QString("An unhandled error occurred: %1\n\n"
                   "The application will now close.").arg(e.what()));
    } catch (...) {
        LOG_CRITICAL("Unknown unhandled exception");
        QMessageBox::critical(nullptr, "Critical Error",
            "An unknown error occurred.\n\n"
            "The application will now close.");
    }
}

int main(int argc, char *argv[]) {
    // Set up global exception handler
    std::set_terminate([]() {
        handleUnexpectedExceptions();
        std::abort();
    });

    try {
        QApplication app(argc, argv);
        app.setApplicationName("USB Device Monitor");
        app.setApplicationVersion("2.0.0");
        app.setOrganizationName("USB Monitor Project");
        app.setOrganizationDomain("usb-monitor.org");

        // Parse command line arguments
        QCommandLineParser parser;
        setupCommandLineParser(parser);
        parser.process(app);

        // Initialize logger
        initializeLogger(parser);

        // Load configuration
        ConfigManager configManager;
        if (!loadConfiguration(configManager, parser)) {
            return 1;
        }

        // Create and show main window
        MainWindow mainWindow;
        if (!parser.isSet("minimized")) {
            mainWindow.show();
        }

        // Start monitoring devices
        //mainWindow.setupDeviceMonitoring();

        LOG_INFO("Application initialized successfully");

        return app.exec();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        LOG_CRITICAL("Fatal error: " + std::string(e.what()));
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        LOG_CRITICAL("Unknown fatal error occurred");
        return 1;
    }
}
