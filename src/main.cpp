#include <exception>
#include <string>

#include "diagnostics/LogMacros.h"

import quader.app;
import quader.diagnostics.logger;
import quader.platform.fatal_error_dialog;

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
    try {
        quader::Logger::initialize();
        QUADER_LOG_INFO("Quader Native starting.");

        quader::Application app;
        const int exit_code = app.run();

        QUADER_LOG_INFO("Quader Native stopped with exit code {}.", exit_code);
        quader::Logger::shutdown();
        return exit_code;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        safely_log_fatal_error(message);
        quader::show_fatal_error_dialog(message);
        quader::Logger::shutdown();
        return 1;
    } catch (...) {
        const std::string message = "Unknown fatal error.";
        safely_log_fatal_error(message);
        quader::show_fatal_error_dialog(message);
        quader::Logger::shutdown();
        return 1;
    }
}
