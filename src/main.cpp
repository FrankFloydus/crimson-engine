#include "quader/app/App.h"
#include "quader/core/Logger.h"
#include "quader/platform/FatalErrorHandler.h"

#include <exception>
#include <filesystem>
#include <memory>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace {

std::filesystem::path executablePath(int argc, char** argv) {
    if (argc > 0 && argv && argv[0]) {
        return std::filesystem::absolute(argv[0]);
    }

#if defined(_WIN32)
    wchar_t path[MAX_PATH] = {};
    const DWORD count = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (count > 0) {
        return std::filesystem::path(path);
    }
#endif

    return std::filesystem::current_path() / "quader.exe";
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path exe = executablePath(argc, argv);
    const std::filesystem::path logPath = exe.parent_path() / "quader.log";
    quader::Logger::initialize({.filePath = logPath.string()});
    quader::installFatalErrorHandler(logPath);
    LOG_INFO("Booting Quader from {}", exe.string());

    try {
        auto app = std::make_unique<quader::App>();
        if (!app->initialize(exe)) {
            LOG_CRITICAL("Application initialization failed");
            app->shutdown();
            quader::Logger::shutdown();
            return 1;
        }

        const int result = app->run();
        app->shutdown();
        LOG_INFO("Quader shutdown complete");
        quader::Logger::shutdown();
        return result;
    } catch (const std::exception& error) {
        LOG_CRITICAL("Unhandled exception in Quader main: {}", error.what());
        quader::reportFatalErrorAndExit(quader::FatalErrorCode::UnhandledCppException, error.what(), 1);
    } catch (...) {
        LOG_CRITICAL("Unhandled non-standard exception in Quader main");
        quader::reportFatalErrorAndExit(
                quader::FatalErrorCode::UnhandledNonStandardException,
                "Unhandled non-standard exception in Quader main.",
                1);
    }
}
