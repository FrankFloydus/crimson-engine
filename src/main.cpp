#include <exception>
#include <optional>
#include <string>

#include "diagnostics/LogMacros.h"

import quader.app;
import quader.diagnostics.fatal_diagnostics;
import quader.diagnostics.logger;
import quader.platform.fatal_error_dialog;
import quader.platform.sdl_user_settings_paths;

namespace {

void safely_log_fatal_error(const std::string& message)
{
    try {
        QUADER_LOG_CRITICAL("Fatal error: {}", message);
    } catch (...) {
    }
}

} // namespace

int main(int, char**)
{
    std::optional<quader::LoggerSession> logger_session;

    try {
        logger_session.emplace();
        quader::install_fatal_diagnostics();
        QUADER_LOG_INFO("Quader Native starting.");

        quader::Application app(quader::resolve_sdl_user_settings_paths());
        const int exit_code = app.run();

        QUADER_LOG_INFO("Quader Native stopped with exit code {}.", exit_code);
        return exit_code;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        safely_log_fatal_error(message);
        quader::show_fatal_error_dialog(message);
        return 1;
    } catch (...) {
        const std::string message = "Unknown fatal error.";
        safely_log_fatal_error(message);
        quader::show_fatal_error_dialog(message);
        return 1;
    }
}
