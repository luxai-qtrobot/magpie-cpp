#include <magpie/utils/logger.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace magpie {

static int levelValue(Logger::Level lvl) {
    switch (lvl) {
        case Logger::Level::Debug: return 10;
        case Logger::Level::Info:  return 20;
        case Logger::Level::Warn:  return 30;
        case Logger::Level::Error: return 40;
    }
    return 20;
}

std::atomic<int>& Logger::currentLevelAtomic() {
    static std::atomic<int> lvl{ levelValue(Level::Info) };
    return lvl;
}

std::atomic<bool>& Logger::useColorsAtomic() {
#if defined(_WIN32)
    static std::atomic<bool> enabled{ false };
#else
    static std::atomic<bool> enabled{ true };
#endif
    return enabled;
}

std::mutex& Logger::outputMutex() {
    static std::mutex m;
    return m;
}

void Logger::setLevel(Level level) {
    currentLevelAtomic().store(levelValue(level), std::memory_order_relaxed);
}

void Logger::setLevel(const std::string& levelName) {
    setLevel(parseLevel(levelName));
}

Logger::Level Logger::level() {
    int v = currentLevelAtomic().load(std::memory_order_relaxed);
    if (v <= 10) return Level::Debug;
    if (v <= 20) return Level::Info;
    if (v <= 30) return Level::Warn;
    return Level::Error;
}

void Logger::setUseColors(bool enabled) {
    useColorsAtomic().store(enabled, std::memory_order_relaxed);
}

bool Logger::useColors() {
    return useColorsAtomic().load(std::memory_order_relaxed);
}

Logger::Level Logger::parseLevel(const std::string& name) {
    std::string up = name;
    std::transform(up.begin(), up.end(), up.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (up == "DEBUG") return Level::Debug;
    if (up == "INFO")  return Level::Info;
    if (up == "WARN" || up == "WARNING") return Level::Warn;
    if (up == "ERROR") return Level::Error;

    return Level::Info;
}

bool Logger::shouldLog(Level msgLevel) {
    const int cur = currentLevelAtomic().load(std::memory_order_relaxed);
    return levelValue(msgLevel) >= cur;
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
    if (!useColors()) return "";

    switch (level) {
        case Level::Debug: return "\033[90m"; // grey
        case Level::Info:  return "\033[32m"; // green
        case Level::Warn:  return "\033[33m"; // yellow
        case Level::Error: return "\033[31m"; // red
    }
    return "";
}

const char* Logger::resetCode() {
    return useColors() ? "\033[0m" : "";
}

void Logger::writeLine(Level lvl, const std::string& msg) {
    std::lock_guard<std::mutex> lock(outputMutex());

    std::cerr << colorCode(lvl)
              << '[' << levelTag(lvl) << "] "
              << '[' << formattedTime() << ']'
              << resetCode()
              << ": " << msg << '\n';
}

void Logger::debug(const std::string& message) {
    if (!shouldLog(Level::Debug)) return;
    writeLine(Level::Debug, message);
}

void Logger::info(const std::string& message) {
    if (!shouldLog(Level::Info)) return;
    writeLine(Level::Info, message);
}

void Logger::warning(const std::string& message) {
    if (!shouldLog(Level::Warn)) return;
    writeLine(Level::Warn, message);
}

void Logger::error(const std::string& message) {
    if (!shouldLog(Level::Error)) return;
    writeLine(Level::Error, message);
}

void Logger::prettyPrint(const std::string& message) {
    if (!shouldLog(Level::Info)) return;

    std::lock_guard<std::mutex> lock(outputMutex());
    std::cerr << colorCode(Level::Info)
              << "[INFO] "
              << '[' << formattedTime() << ']'
              << resetCode()
              << ":\n"
              << message << '\n';
}

} // namespace magpie
