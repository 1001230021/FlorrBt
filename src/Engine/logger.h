#pragma once
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

enum class ELogPriority
{
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

class CLogger
{
  public:
    explicit CLogger(const std::string& name) : m_sender(name) {}

    void Log(ELogPriority priority, const std::string& msg)
    {
        std::cout << "[" << m_sender << "]"
                  << "[" << PriorityToString(priority) << "] " << NowString() << ": " << msg << std::endl;
    }

    void Debug(const std::string& msg)
    {
        Log(ELogPriority::Debug, msg);
        ++m_debug_msg;
    }

    void Info(const std::string& msg)
    {
        Log(ELogPriority::Info, msg);
        ++m_info_msg;
    }

    void Warn(const std::string& msg)
    {
        Log(ELogPriority::Warning, msg);
        ++m_warning_msg;
    }

    void Error(const std::string& msg)
    {
        Log(ELogPriority::Error, msg);
        ++m_error_msg;
    }

    void Fatal(const std::string& msg)
    {
        Log(ELogPriority::Fatal, msg);
        ++m_fatal_msg;
    }

    int m_debug_msg = 0;
    int m_info_msg = 0;
    int m_warning_msg = 0;
    int m_error_msg = 0;
    int m_fatal_msg = 0;

  private:
    std::string PriorityToString(ELogPriority priority) const
    {
        switch (priority)
        {
        case ELogPriority::Debug:
            return "DEBUG";
        case ELogPriority::Info:
            return "INFO";
        case ELogPriority::Warning:
            return "WARN";
        case ELogPriority::Error:
            return "ERROR";
        case ELogPriority::Fatal:
            return "FATAL";
        }
        return "UNKNOWN";
    }

    std::string NowString() const
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm* local_time = std::localtime(&time);

        std::ostringstream oss;
        oss << std::put_time(local_time, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    std::string m_sender;
};

#define LOG_DEBUG(sender, msg) CLogger(sender).Debug(msg)
#define LOG_INFO(sender, msg) CLogger(sender).Info(msg)
#define LOG_WARN(sender, msg) CLogger(sender).Warn(msg)
#define LOG_ERROR(sender, msg) CLogger(sender).Error(msg)
#define LOG_FATAL(sender, msg) CLogger(sender).Fatal(msg)