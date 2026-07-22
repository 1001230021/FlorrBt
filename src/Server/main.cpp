#include "server.h"
#include "../Engine/logger.h"
#include "../Shared/game_config.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::string GetLastServerCrashContext();

namespace
{
bool LooksLikeServerRoot(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path / game_config::lobby_map_path, ec);
}

std::optional<std::filesystem::path> FindServerRootFrom(std::filesystem::path path)
{
    std::error_code ec;
    path = std::filesystem::absolute(path, ec);
    if (ec) return std::nullopt;
    if (std::filesystem::is_regular_file(path, ec)) path = path.parent_path();

    for (;;)
    {
        if (LooksLikeServerRoot(path)) return path;
        std::filesystem::path parent = path.parent_path();
        if (parent.empty() || parent == path) break;
        path = parent;
    }
    return std::nullopt;
}

#ifdef _WIN32
std::optional<std::filesystem::path> ServerExecutablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return std::nullopt;
        if (written < buffer.size() - 1)
        {
            buffer.resize(written);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}
#endif

void NormalizeServerWorkingDirectory()
{
    if (LooksLikeServerRoot(std::filesystem::current_path())) return;

    if (auto root = FindServerRootFrom(std::filesystem::current_path()))
    {
        std::filesystem::current_path(*root);
        return;
    }

#ifdef _WIN32
    if (auto exe_path = ServerExecutablePath())
    {
        if (auto root = FindServerRootFrom(*exe_path))
        {
            std::filesystem::current_path(*root);
            return;
        }
    }
#endif
}

std::string CrashTimestamp()
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
    oss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.'
        << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

void AppendCrashRecord(const std::string& message)
{
    std::string line = "[crash][FATAL] " + CrashTimestamp() + ": " + message;
    std::cerr << line << std::endl;

    std::ofstream file(game_config::server_log_path, std::ios::app | std::ios::binary);
    if (file) file << line << std::endl;
}

void LogTerminate()
{
    try
    {
        auto exception = std::current_exception();
        if (exception) std::rethrow_exception(exception);
        AppendCrashRecord("Server terminated without an active exception; last_context={" + GetLastServerCrashContext() + "}");
        LOG_FATAL("server", "Server terminated without an active exception");
    }
    catch (const std::exception& e)
    {
        std::string message = std::string("Server terminated: ") + e.what() +
                              "; last_context={" + GetLastServerCrashContext() + "}";
        AppendCrashRecord(message);
        LOG_FATAL("server", message);
    }
    catch (...)
    {
        std::string message = "Server terminated with an unknown exception; last_context={" +
                              GetLastServerCrashContext() + "}";
        AppendCrashRecord(message);
        LOG_FATAL("server", message);
    }
    std::abort();
}

#ifdef _WIN32
std::string SehCodeName(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    default:
        return "UNKNOWN_SEH_EXCEPTION";
    }
}

LONG WINAPI LogUnhandledSehException(EXCEPTION_POINTERS* pointers)
{
    DWORD code = 0;
    void* address = nullptr;
    if (pointers && pointers->ExceptionRecord)
    {
        code = pointers->ExceptionRecord->ExceptionCode;
        address = pointers->ExceptionRecord->ExceptionAddress;
    }

    std::ostringstream oss;
    oss << "Unhandled SEH exception " << SehCodeName(code)
        << " code=0x" << std::hex << std::uppercase << code
        << " address=0x" << reinterpret_cast<std::uintptr_t>(address)
        << std::dec << "; last_context={" << GetLastServerCrashContext() << "}";
    AppendCrashRecord(oss.str());
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
}

int main()
{
    NormalizeServerWorkingDirectory();
    std::set_terminate(LogTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(LogUnhandledSehException);
#endif
    try
    {
        CServer server;
        server.Init();
        server.Run();
        return 0;
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("server", std::string("Unhandled server exception: ") + e.what());
    }
    catch (...)
    {
        LOG_FATAL("server", "Unhandled unknown server exception");
    }
    return 1;
}
