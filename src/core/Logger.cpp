#include "Logger.hpp"
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <mutex>
#include <deque>
#include <fstream>
#include <filesystem>
#include <iomanip>

namespace usb_monitor {

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string message;
    std::string source;
    std::string function;
};

class Logger::Private {
public:
    LogLevel currentLevel{LogLevel::Info};
    LogDestination destination{LogDestination::Console};
    std::string logFile;
    size_t maxFileSize{10 * 1024 * 1024}; // 10MB default
    std::chrono::hours maxLogAge{24 * 7};  // 1 week default
    bool includeTimestamps{true};
    bool includeSourceInfo{true};
    
    std::deque<LogEntry> recentLogs;
    size_t maxRecentLogs{1000};
    std::mutex logMutex;
    std::unique_ptr<std::ofstream> fileStream;
    
    void openLogFile() {
        if (!logFile.empty()) {
            fileStream = std::make_unique<std::ofstream>(
                logFile, std::ios::app);
        }
    }
    
    void closeLogFile() {
        if (fileStream) {
            fileStream->close();
            fileStream.reset();
        }
    }
    
    void writeToConsole(const std::string& formattedMessage) {
        std::cout << formattedMessage << std::endl;
    }
    
    void writeToFile(const std::string& formattedMessage) {
        if (!fileStream || !fileStream->is_open()) {
            openLogFile();
        }
        
        if (fileStream && fileStream->is_open()) {
            (*fileStream) << formattedMessage << std::endl;
            fileStream->flush();
        }
    }
    
    void writeToSystem(const std::string& formattedMessage) {
#ifdef Q_OS_LINUX
        syslog(LOG_INFO, "%s", formattedMessage.c_str());
#elif defined(Q_OS_WINDOWS)
        OutputDebugStringA(formattedMessage.c_str());
#endif
    }
    
    void pruneRecentLogs() {
        while (recentLogs.size() > maxRecentLogs) {
            recentLogs.pop_front();
        }
    }
    
    bool shouldRotateLogFile() {
        if (logFile.empty() || !std::filesystem::exists(logFile)) {
            return false;
        }
        
        auto fileSize = std::filesystem::file_size(logFile);
        if (fileSize >= maxFileSize) {
            return true;
        }
        
        auto lastWrite = std::filesystem::last_write_time(logFile);
        auto now = std::filesystem::file_time_type::clock::now();
        auto age = std::chrono::duration_cast<std::chrono::hours>(
            now - lastWrite);
            
        return age >= maxLogAge;
    }
    
    void rotateLogFile(const std::string& oldFile) {
        closeLogFile();
        
        // Generate new filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
        
        std::string newFile = oldFile + "." + ss.str();
        
        try {
            std::filesystem::rename(oldFile, newFile);
            emit Logger::instance().logFileRotated(oldFile, newFile);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Failed to rotate log file: " << e.what() << std::endl;
        }
        
        openLogFile();
    }
};

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : d(std::make_unique<Private>()) {
}

Logger::~Logger() = default;

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(d->logMutex);
    d->currentLevel = level;
}

void Logger::setLogDestination(LogDestination dest) {
    std::lock_guard<std::mutex> lock(d->logMutex);
    d->destination = dest;
}

void Logger::setLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(d->logMutex);
    d->closeLogFile();
    d->logFile = filename;
    d->openLogFile();
}

void Logger::setMaxFileSize(size_t bytes) {
    std::lock_guard<std::mutex> lock(d->logMutex);
    d->maxFileSize = bytes;
}

void Logger::setMaxLogAge(std::chrono::hours age) {
    std::lock_guard<std::mutex> lock(d->logMutex);
    d->maxLogAge = age;
}

void Logger::enableTimestamps(bool enable) {
    std::lock_guard<std::mutex> lock(d->logMutex);
    d->includeTimestamps = enable;
}

void Logger::enableSourceInfo(bool enable) {
    std::lock_guard<std::mutex> lock(d->logMutex);
    d->includeSourceInfo = enable;
}

void Logger::debug(const std::string& message,
                  const std::string& source,
                  const std::string& function) {
    log(LogLevel::Debug, message, source, function);
}

void Logger::info(const std::string& message,
                 const std::string& source,
                 const std::string& function) {
    log(LogLevel::Info, message, source, function);
}

void Logger::warning(const std::string& message,
                    const std::string& source,
                    const std::string& function) {
    log(LogLevel::Warning, message, source, function);
}

void Logger::error(const std::string& message,
                  const std::string& source,
                  const std::string& function) {
    log(LogLevel::Error, message, source, function);
}

void Logger::critical(const std::string& message,
                     const std::string& source,
                     const std::string& function) {
    log(LogLevel::Critical, message, source, function);
}

void Logger::log(LogLevel level,
                const std::string& message,
                const std::string& source,
                const std::string& function) {
    std::lock_guard<std::mutex> lock(d->logMutex);
    
    if (level < d->currentLevel) {
        return;
    }
    
    // Create log entry
    LogEntry entry{
        std::chrono::system_clock::now(),
        level,
        message,
        source,
        function
    };
    
    // Add to recent logs
    d->recentLogs.push_back(entry);
    d->pruneRecentLogs();
    
    // Format message
    std::string formattedMessage = formatLogMessage(
        level, message, source, function);
    
    // Write to configured destinations
    if (d->destination == LogDestination::Console ||
        d->destination == LogDestination::All) {
        d->writeToConsole(formattedMessage);
    }
    
    if (d->destination == LogDestination::File ||
        d->destination == LogDestination::All) {
        rotateLogFileIfNeeded();
        d->writeToFile(formattedMessage);
    }
    
    if (d->destination == LogDestination::System ||
        d->destination == LogDestination::All) {
        d->writeToSystem(formattedMessage);
    }
    
    emit logAdded(level, message);
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(d->logMutex);
    if (d->fileStream) {
        d->fileStream->flush();
    }
}

void Logger::clear() {
    std::lock_guard<std::mutex> lock(d->logMutex);
    d->recentLogs.clear();
    
    if (!d->logFile.empty()) {
        d->closeLogFile();
        std::ofstream ofs(d->logFile, std::ofstream::trunc);
        d->openLogFile();
    }
}

std::vector<std::string> Logger::getRecentLogs(size_t count) const {
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(d->logMutex);
    
    size_t start = (count >= d->recentLogs.size()) ? 0 :
                   d->recentLogs.size() - count;
    
    for (size_t i = start; i < d->recentLogs.size(); ++i) {
        const auto& entry = d->recentLogs[i];
        result.push_back(formatLogMessage(
            entry.level,
            entry.message,
            entry.source,
            entry.function
        ));
    }
    
    return result;
}

bool Logger::exportLogs(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(d->logMutex);
    
    try {
        std::ofstream file(filename);
        if (!file) {
            return false;
        }
        
        for (const auto& entry : d->recentLogs) {
            file << formatLogMessage(
                entry.level,
                entry.message,
                entry.source,
                entry.function
            ) << std::endl;
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void Logger::rotateLogFileIfNeeded() {
    if (d->shouldRotateLogFile()) {
        d->rotateLogFile(d->logFile);
    }
}

std::string Logger::formatLogMessage(LogLevel level,
                                   const std::string& message,
                                   const std::string& source,
                                   const std::string& function) const {
    std::stringstream ss;
    
    if (d->includeTimestamps) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << " ";
    }
    
    ss << "[" << getLevelString(level) << "] ";
    
    if (d->includeSourceInfo && !source.empty()) {
        ss << source;
        if (!function.empty()) {
            ss << ":" << function;
        }
        ss << " - ";
    }
    
    ss << message;
    return ss.str();
}

std::string Logger::getLevelString(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARNING";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

} // namespace usb_monitor
