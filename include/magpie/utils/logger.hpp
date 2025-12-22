#pragma once

#include <atomic>
#include <mutex>
#include <string>

// fmt (Ubuntu libfmt-dev 6.1.2 compatible)
#include <fmt/format.h>

namespace magpie {

class Logger {
public:
    enum class Level {
        Debug = 10,
        Info  = 20,
        Warn  = 30,
        Error = 40
    };

    // Set log level
    static void setLevel(Level level);
    static void setLevel(const std::string& levelName);

    // Get current level
    static Level level();

    // Enable/disable ANSI colors (enabled by default on non-Windows)
    static void setUseColors(bool enabled);
    static bool useColors();

    // Backward-compatible message overloads
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);

    // fmt-style formatting overloads (works with fmt 6.1.2)
    template <typename... Args>
    static void debug(const char* f, Args&&... args) {
        if (!shouldLog(Level::Debug)) return;
        debug(fmt::format(f, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void info(const char* f, Args&&... args) {
        if (!shouldLog(Level::Info)) return;
        info(fmt::format(f, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void warning(const char* f, Args&&... args) {
        if (!shouldLog(Level::Warn)) return;
        warning(fmt::format(f, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void error(const char* f, Args&&... args) {
        if (!shouldLog(Level::Error)) return;
        error(fmt::format(f, std::forward<Args>(args)...));
    }

    // Pretty print helper (prints message on next line)
    static void prettyPrint(const std::string& message);

private:
    static bool shouldLog(Level msgLevel);

    static std::string formattedTime();
    static const char* levelTag(Level level);
    static const char* colorCode(Level level);

    static std::atomic<int>& currentLevelAtomic();
    static std::atomic<bool>& useColorsAtomic();
    static std::mutex& outputMutex();

    static Level parseLevel(const std::string& name);

    // Internal sink that can access private helpers safely
    static void writeLine(Level lvl, const std::string& msg);
    static const char* resetCode();
};

} // namespace magpie
