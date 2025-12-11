#pragma once

#include <string>

namespace magpie {

class Logger {
public:
    enum class Level {
        Debug = 10,
        Info  = 20,
        Warn  = 30,
        Error = 40
    };

    // Set log level by enum
    static void setLevel(Level level);

    // Set log level by string: "DEBUG", "INFO", "WARN", "ERROR"
    static void setLevel(const std::string& levelName);

    // Get current level
    static Level level();

    // Logging methods
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);

    // Simple pretty print helper
    static void prettyPrint(const std::string& message);

private:
    static Level& currentLevel();
    static bool shouldLog(Level msgLevel);

    static std::string formattedTime();
    static const char* levelTag(Level level);
    static const char* colorCode(Level level);
};

} // namespace magpie
