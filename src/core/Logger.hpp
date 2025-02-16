#pragma once
#include <QObject>
#include <string>
#include <memory>
#include <sstream>
#include <chrono>

namespace usb_monitor {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

enum class LogDestination {
    Console,
    File,
    System,
    All
};

class Logger : public QObject {
    Q_OBJECT

public:
    static Logger& instance();
    
    // Delete copy and move constructors/assignments
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // Configuration
    void setLogLevel(LogLevel level);
    void setLogDestination(LogDestination dest);
    void setLogFile(const std::string& filename);
    void setMaxFileSize(size_t bytes);
    void setMaxLogAge(std::chrono::hours age);
    void enableTimestamps(bool enable);
    void enableSourceInfo(bool enable);

    // Logging methods
    void debug(const std::string& message, 
              const std::string& source = "",
              const std::string& function = "");
    void info(const std::string& message,
             const std::string& source = "",
             const std::string& function = "");
    void warning(const std::string& message,
                const std::string& source = "",
                const std::string& function = "");
    void error(const std::string& message,
              const std::string& source = "",
              const std::string& function = "");
    void critical(const std::string& message,
                 const std::string& source = "",
                 const std::string& function = "");

    // Utility methods
    void flush();
    void clear();
    std::vector<std::string> getRecentLogs(size_t count = 100) const;
    bool exportLogs(const std::string& filename) const;

signals:
    void logAdded(LogLevel level, const std::string& message);
    void logFileRotated(const std::string& oldFile, const std::string& newFile);

private:
    Logger();
    ~Logger();

    void log(LogLevel level, 
             const std::string& message,
             const std::string& source = "",
             const std::string& function = "");
    void rotateLogFileIfNeeded();
    std::string formatLogMessage(LogLevel level,
                               const std::string& message,
                               const std::string& source,
                               const std::string& function) const;
    std::string getLevelString(LogLevel level) const;

    class Private;
    std::unique_ptr<Private> d;
};

// Convenience macros for logging
#define LOG_DEBUG(msg) \
    Logger::instance().debug(msg, __FILE__, __FUNCTION__)
#define LOG_INFO(msg) \
    Logger::instance().info(msg, __FILE__, __FUNCTION__)
#define LOG_WARNING(msg) \
    Logger::instance().warning(msg, __FILE__, __FUNCTION__)
#define LOG_ERROR(msg) \
    Logger::instance().error(msg, __FILE__, __FUNCTION__)
#define LOG_CRITICAL(msg) \
    Logger::instance().critical(msg, __FILE__, __FUNCTION__)

} // namespace usb_monitor
