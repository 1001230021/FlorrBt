#include "server.h"
#include "../Engine/logger.h"
#include <cstdlib>
#include <exception>
#include <string>

namespace
{
void LogTerminate()
{
    try
    {
        auto exception = std::current_exception();
        if (exception) std::rethrow_exception(exception);
        LOG_FATAL("server", "Server terminated without an active exception");
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("server", std::string("Server terminated: ") + e.what());
    }
    catch (...)
    {
        LOG_FATAL("server", "Server terminated with an unknown exception");
    }
    std::abort();
}
}

int main()
{
    std::set_terminate(LogTerminate);
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
