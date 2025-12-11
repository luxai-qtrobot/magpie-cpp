#include <magpie/utils/logger.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace magpie {

namespace {
    int levelValue(Logger::Level lvl) {
        switch (lvl) {
            case Logger::Level::Debug: return 10;
            case Logger::Level::Info:  return 20;
            case Logger::Level::Warn:  return 30;
            case Logger::Level::Error: return 40;
        }
        return 20;
    }

    Logger::Level parseLevel(const std::string& name) {
        std::string up = name;
        std::transform(up.begin(), up.end(), up.begin(), ::toupper);

        if (up == "DEBUG") return Logger::Level::Debug;
        if (up == "INFO")  return Logger::Level::Info;
        if (up == "WARN" || up == "WARNING") return Logger::Level::Warn;
        if (up == "ERROR") return Logger::Level::Error;

        return Logger::Level::Info;
    }
} // namespace

Logger::Level& Logger::currentLevel() {
    static Level lvl = Level::Info;
    return lvl;
}

void Logger::setLevel(Level level) {
    currentLevel() = level;
}

void Logger::setLevel(const std::string& levelName) {
    currentLevel() = parseLevel(levelName);
}

Logger::Level Logger::level() {
    return currentLevel();
}

bool Logger::shouldLog(Level msgLevel) {
    return levelValue(msgLevel) >= levelValue(currentLevel());
}

std::string Logger::formattedTime() {
    using clock = std::chrono::system_clock;
    auto now = clock::now();

    auto inTimeT = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &inTimeT);
#else
    localtime_r(&inTimeT, &tm);
#endif

    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()) % static_cast<long>(1000000);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y.%m.%d %H:%M:%S")
        << '.' << std::setw(6) << std::setfill('0') << micros.count();
    return oss.str();
}

const char* Logger::levelTag(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "INFO";
}

const char* Logger::colorCode(Level level) {
    switch (level) {
        case Level::Debug: return "\033[90m";
        case Level::Info:  return "\033[32m";
        case Level::Warn:  return "\033[33m";
        case Level::Error: return "\033[31m";
    }
    return "\033[0m";
}

void Logger::debug(const std::string& message) {
    if (!shouldLog(Level::Debug)) return;
    std::cerr << colorCode(Level::Debug)
              << "[DEBUG] [" << formattedTime() << "]:\033[0m "
              << message << '\n';
}

void Logger::info(const std::string& message) {
    if (!shouldLog(Level::Info)) return;
    std::cerr << colorCode(Level::Info)
              << "[INFO] [" << formattedTime() << "]:\033[0m "
              << message << '\n';
}

void Logger::warning(const std::string& message) {
    if (!shouldLog(Level::Warn)) return;
    std::cerr << colorCode(Level::Warn)
              << "[WARN] [" << formattedTime() << "]:\033[0m "
              << message << '\n';
}

void Logger::error(const std::string& message) {
    if (!shouldLog(Level::Error)) return;
    std::cerr << colorCode(Level::Error)
              << "[ERROR] [" << formattedTime() << "]:\033[0m "
              << message << '\n';
}

void Logger::prettyPrint(const std::string& message) {
    if (!shouldLog(Level::Info)) return;
    std::cerr << colorCode(Level::Info)
              << "[INFO] [" << formattedTime() << "]:\033[0m\n"
              << message << '\n';
}

} // namespace magpie
