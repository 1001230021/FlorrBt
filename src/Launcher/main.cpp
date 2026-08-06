#include "../Engine/json_value.h"
#include "../Shared/version.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
constexpr std::uint32_t hot_reload_exit_code = 42;
constexpr std::chrono::seconds default_ready_timeout(30);

struct reload_request
{
    std::filesystem::path snapshot;
    std::filesystem::path candidate;
};

struct process_result
{
    bool started = false;
    bool ready = false;
    std::uint32_t exit_code = 1;
};

std::filesystem::path Absolute(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::path result = std::filesystem::absolute(path, error);
    return error ? path : result;
}

bool LooksLikeServerRoot(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path / "data/maps/garden.tmj", error);
}

std::optional<std::filesystem::path> FindServerRoot(std::filesystem::path path)
{
    path = Absolute(path);
    std::error_code error;
    if (std::filesystem::is_regular_file(path, error)) path = path.parent_path();
    for (;;)
    {
        if (LooksLikeServerRoot(path)) return path;
        std::filesystem::path parent = path.parent_path();
        if (parent.empty() || parent == path) return std::nullopt;
        path = parent;
    }
}

#ifdef _WIN32
std::filesystem::path LauncherPath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return {};
        if (written < buffer.size() - 1)
        {
            buffer.resize(written);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring QuoteArgument(const std::filesystem::path& value)
{
    const std::wstring text = value.wstring();
    std::wstring result = L"\"";
    size_t slash_count = 0;
    for (wchar_t ch : text)
    {
        if (ch == L'\\')
        {
            ++slash_count;
            continue;
        }
        if (ch == L'\"')
        {
            result.append(slash_count * 2 + 1, L'\\');
            result.push_back(L'\"');
            slash_count = 0;
            continue;
        }
        result.append(slash_count, L'\\');
        slash_count = 0;
        result.push_back(ch);
    }
    result.append(slash_count * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

process_result RunServer(const std::filesystem::path& executable, const std::filesystem::path& root,
                         const std::filesystem::path& ready_file, const std::filesystem::path& snapshot,
                         std::chrono::seconds ready_timeout)
{
    process_result result;
    std::error_code file_error;
    std::filesystem::remove(ready_file, file_error);

    std::wstring command = QuoteArgument(executable) + L" --ready-file " + QuoteArgument(ready_file);
    if (!snapshot.empty()) command += L" --restore " + QuoteArgument(snapshot);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring working_directory = root.wstring();
    if (!CreateProcessW(executable.wstring().c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
                        CREATE_NEW_PROCESS_GROUP, nullptr, working_directory.c_str(), &startup, &process))
    {
        std::cerr << "[launcher] Failed to start " << executable.string() << " (Win32 " << GetLastError() << ")\n";
        return result;
    }
    result.started = true;
    CloseHandle(process.hThread);

    const auto deadline = std::chrono::steady_clock::now() + ready_timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (std::filesystem::exists(ready_file, file_error))
        {
            result.ready = true;
            break;
        }
        if (WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0) break;
    }

    if (!result.ready && WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0)
    {
        std::cerr << "[launcher] Server did not become ready within " << ready_timeout.count() << " seconds\n";
        TerminateProcess(process.hProcess, 1);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    result.exit_code = static_cast<std::uint32_t>(exit_code);
    CloseHandle(process.hProcess);
    std::filesystem::remove(ready_file, file_error);
    return result;
}
#endif

std::optional<reload_request> ReadReloadRequest(const std::filesystem::path& path, std::string& error)
{
    std::optional<CJsonValue> root = CJsonValue::LoadFromFile(path, &error);
    if (!root || !root->IsObject())
    {
        if (error.empty()) error = "Reload request root is not an object";
        return std::nullopt;
    }
    const CJsonValue* snapshot = root->Find("snapshot");
    const CJsonValue* candidate = root->Find("candidate");
    if (!snapshot || !snapshot->IsString() || snapshot->AsString().empty())
    {
        error = "Reload request does not contain a snapshot path";
        return std::nullopt;
    }

    reload_request request;
    request.snapshot = snapshot->AsString();
    if (candidate && candidate->IsString()) request.candidate = candidate->AsString();
    return request;
}

std::filesystem::path ResolveCandidate(const std::filesystem::path& requested,
                                       const std::filesystem::path& current,
                                       const std::filesystem::path& root)
{
    if (requested.empty()) return current;
    std::filesystem::path candidate = requested.is_absolute() ? requested : root / requested;
    std::error_code error;
    if (std::filesystem::exists(candidate, error)) return Absolute(candidate);
    std::cerr << "[launcher] Candidate " << candidate.string() << " does not exist; reloading current binary\n";
    return current;
}
} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] && std::string_view(argv[i]) == "--version")
        {
            std::cout << florrbt::version_label << '\n';
            return 0;
        }
    }

#ifndef _WIN32
    std::cerr << "FlorrBt Launcher currently requires Windows.\n";
    return 1;
#else
    std::cout << "[launcher] " << florrbt::version_label << '\n';
    const std::filesystem::path launcher_path = LauncherPath();
    const std::filesystem::path launcher_directory = launcher_path.parent_path();
    std::optional<std::filesystem::path> root = FindServerRoot(std::filesystem::current_path());
    if (!root) root = FindServerRoot(launcher_directory);
    if (!root)
    {
        std::cerr << "[launcher] Could not locate the FlorrBt server root\n";
        return 1;
    }
    std::filesystem::current_path(*root);

    std::filesystem::path server_executable;
    std::chrono::seconds ready_timeout = default_ready_timeout;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i] ? argv[i] : "";
        if (argument == "--server" && i + 1 < argc)
        {
            server_executable = argv[++i];
            continue;
        }
        if (argument == "--ready-timeout" && i + 1 < argc)
        {
            try
            {
                ready_timeout = std::chrono::seconds(std::max(1, std::stoi(argv[++i])));
            } catch (...)
            {
                std::cerr << "[launcher] Invalid ready timeout\n";
                return 1;
            }
            continue;
        }
        if (server_executable.empty()) server_executable = argument;
    }

    if (server_executable.empty())
    {
        const std::array candidates = {
            launcher_directory / "FlorrBt.Server.exe", *root / "x64/Release/FlorrBt.Server.exe",
            *root / "x64/Debug/FlorrBt.Server.exe",
        };
        std::error_code error;
        for (const auto& candidate : candidates)
        {
            if (std::filesystem::exists(candidate, error))
            {
                server_executable = candidate;
                break;
            }
        }
    }
    if (server_executable.empty())
    {
        std::cerr << "[launcher] No server executable was found; use --server <path>\n";
        return 1;
    }
    if (!server_executable.is_absolute()) server_executable = *root / server_executable;
    server_executable = Absolute(server_executable);

    const std::filesystem::path request_path = *root / "data/hot_reload/reload_request.json";
    std::filesystem::path snapshot;
    std::filesystem::path previous_executable = server_executable;
    std::uint64_t launch_generation = 0;
    bool rollback_attempt = false;

    for (;;)
    {
        const std::filesystem::path ready_file =
            *root / "data/hot_reload" /
            ("ready-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++launch_generation) + ".json");
        std::cout << "[launcher] Starting " << server_executable.string();
        if (!snapshot.empty()) std::cout << " from " << snapshot.string();
        std::cout << '\n';

        process_result result = RunServer(server_executable, *root, ready_file, snapshot, ready_timeout);
        if (!result.started || !result.ready)
        {
            if (!snapshot.empty() && !rollback_attempt && server_executable != previous_executable)
            {
                std::cerr << "[launcher] Candidate restore failed; rolling back to " << previous_executable.string()
                          << '\n';
                server_executable = previous_executable;
                rollback_attempt = true;
                continue;
            }
            std::cerr << "[launcher] Server startup failed with exit code " << result.exit_code << '\n';
            return static_cast<int>(result.exit_code);
        }

        rollback_attempt = false;
        if (result.exit_code != hot_reload_exit_code) return static_cast<int>(result.exit_code);

        std::string request_error;
        std::optional<reload_request> request = ReadReloadRequest(request_path, request_error);
        if (!request)
        {
            std::cerr << "[launcher] Hot reload request is invalid: " << request_error << '\n';
            return 1;
        }
        std::error_code remove_error;
        std::filesystem::remove(request_path, remove_error);
        if (!request->snapshot.is_absolute()) request->snapshot = *root / request->snapshot;
        snapshot = Absolute(request->snapshot);
        previous_executable = server_executable;
        server_executable = ResolveCandidate(request->candidate, server_executable, *root);
    }
#endif
}
