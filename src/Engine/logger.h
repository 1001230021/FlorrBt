#pragma once
#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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
    using Sink = std::function<void(const std::string&, ELogPriority, const std::string&)>;

    struct sink_state
    {
        explicit sink_state(Sink sink) : callback(std::move(sink)) {}
        std::recursive_mutex mutex;
        Sink callback;
    };

    struct sink_entry
    {
        size_t id = 0;
        std::shared_ptr<sink_state> state;

        void operator()(const std::string& sender, ELogPriority priority, const std::string& msg) const
        {
            std::lock_guard<std::recursive_mutex> lock(state->mutex);
            const auto callback = state->callback;
            if (callback) callback(sender, priority, msg);
        }
    };

    explicit CLogger(const std::string& name) : m_sender(name) {}

    void Log(ELogPriority priority, const std::string& msg)
    {
        std::cout << FormatLine(m_sender, priority, msg) << std::endl;
        // Workers log concurrently with console/RCON subscription changes.
        // Copy callbacks under the lock, invoke outside it (callbacks may log).
        std::vector<sink_entry> sinks;
        {
            std::lock_guard<std::mutex> lock(SinkMutex());
            sinks = Sinks();
        }
        for (const auto& sink : sinks)
        {
            sink(m_sender, priority, msg);
        }
    }

    static std::string FormatLine(const std::string& sender, ELogPriority priority, const std::string& msg)
    {
        return "[" + sender + "][" + PriorityToString(priority) + "] " + NowString() + ": " + msg;
    }

    static size_t AddSink(Sink sink)
    {
        std::lock_guard<std::mutex> lock(SinkMutex());
        Sinks().push_back({ ++s_next_sink_id, std::make_shared<sink_state>(std::move(sink)) });
        return s_next_sink_id;
    }

    static void RemoveSink(size_t id)
    {
        std::shared_ptr<sink_state> removed;
        {
            std::lock_guard<std::mutex> lock(SinkMutex());
            auto& sinks = Sinks();
            const auto it = std::find_if(sinks.begin(), sinks.end(), [id](const sink_entry& entry) { return entry.id == id; });
            if (it != sinks.end()) { removed = it->state; sinks.erase(it); }
        }
        // Also wait for an in-flight invocation. Existing GUI subscribers
        // capture `this` and can safely destroy themselves after removal;
        // callbacks in an older copied list see an empty callback and skip.
        if (removed)
        {
            std::lock_guard<std::recursive_mutex> lock(removed->mutex);
            removed->callback = {};
        }
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
    static std::mutex& SinkMutex()
    {
        static std::mutex mutex;
        return mutex;
    }
    static std::vector<sink_entry>& Sinks()
    {
        static std::vector<sink_entry> g_sinks;
        return g_sinks;
    }

    static std::string PriorityToString(ELogPriority priority)
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

    static std::string NowString()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm local_time{};
#ifdef _WIN32
        localtime_s(&local_time, &time);
#else
        localtime_r(&time, &local_time);
#endif

        std::ostringstream oss;
        oss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    std::string m_sender;
    inline static size_t s_next_sink_id = 0;
};

#define LOG_DEBUG(sender, msg) CLogger(sender).Debug(msg)
#define LOG_INFO(sender, msg) CLogger(sender).Info(msg)
#define LOG_WARN(sender, msg) CLogger(sender).Warn(msg)
#define LOG_ERROR(sender, msg) CLogger(sender).Error(msg)
#define LOG_FATAL(sender, msg) CLogger(sender).Fatal(msg)
