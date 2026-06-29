#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>


enum class Priority { DEBUG, INFO, WARNING, ERROR, FATAL };

class CLogger {
    std::string sender;

    std::string prioStr(Priority p) const {
        switch (p) {
        case Priority::DEBUG:   return "DEBUG";
        case Priority::INFO:    return "INFO";
        case Priority::WARNING: return "WARN";
        case Priority::ERROR:   return "ERROR";
        case Priority::FATAL:   return "FATAL";
        }
        return "UNKNOWN";
    }

    std::string nowStr() const {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm* tm = std::localtime(&t);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

public:
    int m_DebugMsg{0};
    int m_InfoMsg{0};
    int m_WarningMsg{0};
    int m_ErrorMsg{0};
    int m_FatalMsg{0};

    explicit CLogger(const std::string& name) : sender(name) {}

    void log(Priority p, const std::string& msg) {
        std::cout << "[" << sender << "]"
            << "[" << prioStr(p) << "] "
            << nowStr() << ": "
            << msg << std::endl;
    }

    void debug(const std::string& msg) { log(Priority::DEBUG, msg); m_DebugMsg++; }
    void info(const std::string& msg) { log(Priority::INFO, msg); m_InfoMsg++; }
    void warn(const std::string& msg) { log(Priority::WARNING, msg); m_WarningMsg++; }
    void error(const std::string& msg) { log(Priority::ERROR, msg); m_ErrorMsg++; }
    void fatal(const std::string& msg) { log(Priority::FATAL, msg); m_FatalMsg++; }
};

#define LOG_DEBUG(sender, msg)   CLogger(sender).debug(msg)
#define LOG_INFO(sender, msg)    CLogger(sender).info(msg)
#define LOG_WARN(sender, msg)    CLogger(sender).warn(msg)
#define LOG_ERROR(sender, msg)   CLogger(sender).error(msg)
#define LOG_FATAL(sender, msg)   CLogger(sender).fatal(msg)