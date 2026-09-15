#include "Logger.h"

namespace CarSentinel {

LogLevel Logger::currentLevel = LogLevel::INFO;

void Logger::begin(LogLevel level) {
    currentLevel = level;
}

void Logger::setLevel(LogLevel level) {
    currentLevel = level;
}

LogLevel Logger::getLevel() {
    return currentLevel;
}

void Logger::log(LogLevel level, const char* levelName, const char* tag, const String& message) {
    if (level > currentLevel || currentLevel == LogLevel::NONE) {
        return;
    }
    Serial.printf("[%s] %lu %s: %s\n", levelName, millis(), tag, message.c_str());
}

void Logger::error(const char* tag, const String& message) { log(LogLevel::ERROR, "ERROR", tag, message); }
void Logger::warn(const char* tag, const String& message) { log(LogLevel::WARN, "WARN", tag, message); }
void Logger::info(const char* tag, const String& message) { log(LogLevel::INFO, "INFO", tag, message); }
void Logger::debug(const char* tag, const String& message) { log(LogLevel::DEBUG, "DEBUG", tag, message); }
void Logger::trace(const char* tag, const String& message) { log(LogLevel::TRACE, "TRACE", tag, message); }

}  // namespace CarSentinel
