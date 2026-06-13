module;

#include <utils/Panic.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

module quader.filament.diagnostics;

import quader.diagnostics.fatal_diagnostics;

namespace quader {
namespace {

[[nodiscard]] const char* safe_c_string(const char* value) noexcept
{
    return value == nullptr ? "" : value;
}

void write_stderr_fallback(std::string_view message) noexcept
{
    std::fwrite(message.data(), sizeof(char), message.size(), stderr);
    std::fwrite("\n", sizeof(char), 1, stderr);
    std::fflush(stderr);
}

void filament_panic_handler(void*, const utils::Panic& panic) noexcept
{
    try {
        std::string message = "Filament panic";
        const char* type = safe_c_string(panic.getType());
        if (type[0] != '\0') {
            message += " (";
            message += type;
            message += ")";
        }

        message += ": ";
        message += safe_c_string(panic.what());

        const char* reason = safe_c_string(panic.getReason());
        if (reason[0] != '\0') {
            message += "\nReason: ";
            message += reason;
        }

        const char* function = safe_c_string(panic.getFunction());
        if (function[0] != '\0') {
            message += "\nFunction: ";
            message += function;
        }

        const char* file = safe_c_string(panic.getFile());
        if (file[0] != '\0') {
            message += "\nLocation: ";
            message += file;
            message += ":";
            message += std::to_string(panic.getLine());
        }

        report_fatal_diagnostic(std::move(message));
    } catch (...) {
        write_stderr_fallback("Filament panic occurred, but the panic handler failed while formatting details.");
    }
}

} // namespace

void install_filament_fatal_diagnostics() noexcept
{
    utils::Panic::setPanicHandler(&filament_panic_handler, nullptr);
}

} // namespace quader
