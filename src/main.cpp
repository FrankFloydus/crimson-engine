#include "quader/app/App.h"
#include "quader/core/Logger.h"

#include <filesystem>

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
    LOG_INFO("Booting Quader from {}", exe.string());

    quader::App app;
    if (!app.initialize(exe)) {
        LOG_CRITICAL("Application initialization failed");
        app.shutdown();
        quader::Logger::shutdown();
        return 1;
    }

    const int result = app.run();
    app.shutdown();
    LOG_INFO("Quader shutdown complete");
    quader::Logger::shutdown();
    return result;
}
