#pragma once

#include <Arduino.h>

// Structured logging: [LEVEL] uptime_ms tag: message
// Level is runtime-configurable so a device can be quieted/verbosed without reflashing.
namespace CarSentinel {

enum class LogLevel : uint8_t {
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    TRACE = 5
};

class Logger {
public:
    static void begin(LogLevel level = LogLevel::INFO);
    static void setLevel(LogLevel level);
    static LogLevel getLevel();

    static void error(const char* tag, const String& message);
    static void warn(const char* tag, const String& message);
    static void info(const char* tag, const String& message);
    static void debug(const char* tag, const String& message);
    static void trace(const char* tag, const String& message);

private:
    static LogLevel currentLevel;
    static void log(LogLevel level, const char* levelName, const char* tag, const String& message);
};

}  // namespace CarSentinel
