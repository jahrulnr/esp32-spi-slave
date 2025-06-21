#include <SPIFFS.h>
#include "lib/Utils/SpiAllocator.h"
#include "Logger.h"

namespace Utils {

Logger::Logger() : _serialEnabled(true),
                 _logLevel(LogLevel::INFO){
}

Logger::~Logger() {
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

bool Logger::init(bool serialEnabled) {
    _serialEnabled = serialEnabled;
    
    if (_serialEnabled) {
        Serial.begin(115200);
    }
    
    return true;
}

void Logger::setLogLevel(LogLevel level) {
    _logLevel = level;
}

void Logger::log(LogLevel level, const String& message) {
    if (level < _logLevel) {
        return;
    }
    
    unsigned long currentTime = millis();
    String logMessage = String(currentTime) + " [" + logLevelToString(level) + "] " + message;
    
    // Always log to Serial and file synchronously for immediate feedback
    if (_serialEnabled) {
        Serial.println(logMessage);
    }
}

String Logger::logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

String Logger::logLevelToLowerString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return "debug";
        case LogLevel::INFO:
            return "info";
        case LogLevel::WARNING:
            return "warning";
        case LogLevel::ERROR:
            return "error";
        case LogLevel::CRITICAL:
            return "critical";
        default:
            return "unknown";
    }
}

String Logger::formatString(const char* format, va_list args) {
    char buffer[256]; // Buffer to hold formatted string
    
    // Format the string
    vsnprintf(buffer, sizeof(buffer), format, args);
    
    // Return as a String object
    return String(buffer);
}

void Logger::debug(const String& format, ...) {
    va_list args;
    debug(format.c_str(), args);
}

void Logger::debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    String formattedMessage = formatString(format, args);
    va_end(args);
    
    log(LogLevel::DEBUG, formattedMessage);
}

void Logger::info(const String& format, ...) {
    va_list args;
    info(format.c_str(), args);
}

void Logger::info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    String formattedMessage = formatString(format, args);
    va_end(args);
    
    log(LogLevel::INFO, formattedMessage);
}

void Logger::warning(const String& format, ...) {
    va_list args;
    warning(format.c_str(), args);
}

void Logger::warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    String formattedMessage = formatString(format, args);
    va_end(args);
    
    log(LogLevel::WARNING, formattedMessage);
}

void Logger::error(const String& format, ...) {
    va_list args;
    error(format.c_str(), args);
}

void Logger::error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    String formattedMessage = formatString(format, args);
    va_end(args);
    
    log(LogLevel::ERROR, formattedMessage);
}

void Logger::critical(const String& format, ...) {
    va_list args;
    critical(format.c_str(), args);
}

void Logger::critical(const char* format, ...) {
    va_list args;
    va_start(args, format);
    String formattedMessage = formatString(format, args);
    va_end(args);
    
    log(LogLevel::CRITICAL, formattedMessage);
}

void Logger::log(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    String formattedMessage = formatString(format, args);
    va_end(args);
    
    log(level, formattedMessage);
}

} // namespace Utils
