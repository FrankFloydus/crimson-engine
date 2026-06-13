module;

#include <utils/Panic.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(_MSC_VER)
#include <crtdbg.h>
#include <csignal>
#include <cstdlib>
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

module quader.diagnostics.fatal_diagnostics;

import quader.diagnostics.logger;

namespace quader {
namespace {

std::mutex fatal_context_mutex;
std::string last_operation;
std::string last_fatal_message;
std::atomic_bool fatal_dialog_shown = false;

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

void log_fatal_message(std::string_view message) noexcept
{
    try {
        Logger::critical(message);
    } catch (...) {
    }
    write_stderr_fallback(message);
}

void remember_fatal_message(std::string_view message) noexcept
{
    try {
        const std::lock_guard lock(fatal_context_mutex);
        last_fatal_message.assign(message.begin(), message.end());
    } catch (...) {
    }
}

[[nodiscard]] std::string remembered_fatal_message()
{
    const std::lock_guard lock(fatal_context_mutex);
    return last_fatal_message;
}

[[nodiscard]] std::string message_with_last_operation(std::string message)
{
    const std::string operation = last_fatal_operation();
    if (!operation.empty()) {
        message += "\nLast Quader operation: ";
        message += operation;
    }
    return message;
}

void show_native_fatal_dialog_once(std::string_view message) noexcept
{
#if defined(_WIN32)
    if (fatal_dialog_shown.exchange(true)) {
        return;
    }

    MessageBoxA(
        nullptr,
        std::string(message).c_str(),
        "Quader Native - Fatal Error",
        MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);
#else
    (void)message;
#endif
}

[[noreturn]] void exit_after_fatal_diagnostics() noexcept
{
    std::_Exit(EXIT_FAILURE);
}

void remember_log_and_show(std::string message) noexcept
{
    try {
        message = message_with_last_operation(std::move(message));
        remember_fatal_message(message);
        log_fatal_message(message);
        show_native_fatal_dialog_once(message);
    } catch (...) {
        write_stderr_fallback("Fatal diagnostics failed while reporting a fatal error.");
    }
}

[[noreturn]] void terminate_handler() noexcept
{
    std::string message = remembered_fatal_message();
    if (message.empty()) {
        message = "std::terminate was called.";
        if (std::current_exception() != nullptr) {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& error) {
                message += "\nException: ";
                message += error.what();
            } catch (...) {
                message += "\nException: unknown non-standard exception.";
            }
        }
    }

    remember_log_and_show(std::move(message));
    exit_after_fatal_diagnostics();
}

#if defined(_MSC_VER)

[[nodiscard]] std::string narrow_wide_string(const wchar_t* value)
{
    if (value == nullptr || value[0] == L'\0') {
        return {};
    }

    std::string text;
    for (const wchar_t* cursor = value; *cursor != L'\0'; ++cursor) {
        text.push_back(*cursor >= 0 && *cursor <= 0x7f ? static_cast<char>(*cursor) : '?');
    }
    return text;
}

[[nodiscard]] const char* report_type_name(int report_type) noexcept
{
    switch (report_type) {
    case _CRT_ASSERT:
        return "CRT assertion";
    case _CRT_ERROR:
        return "CRT error";
    case _CRT_WARN:
        return "CRT warning";
    default:
        return "CRT report";
    }
}

int __cdecl crt_report_hook(int report_type, char* message, int* return_value)
{
    if (report_type != _CRT_ASSERT && report_type != _CRT_ERROR) {
        return FALSE;
    }

    std::string formatted = report_type_name(report_type);
    formatted += ": ";
    formatted += safe_c_string(message);
    if (return_value != nullptr) {
        *return_value = 0;
    }
    remember_log_and_show(std::move(formatted));
    return TRUE;
}

void __cdecl invalid_parameter_handler(
    const wchar_t* expression,
    const wchar_t* function,
    const wchar_t* file,
    unsigned int line,
    uintptr_t)
{
    std::string message = "Invalid CRT parameter.";
    const std::string expression_text = narrow_wide_string(expression);
    if (!expression_text.empty()) {
        message += "\nExpression: ";
        message += expression_text;
    }
    const std::string function_text = narrow_wide_string(function);
    if (!function_text.empty()) {
        message += "\nFunction: ";
        message += function_text;
    }
    const std::string file_text = narrow_wide_string(file);
    if (!file_text.empty()) {
        message += "\nLocation: ";
        message += file_text;
        message += ":";
        message += std::to_string(line);
    }
    remember_log_and_show(std::move(message));
    exit_after_fatal_diagnostics();
}

void __cdecl purecall_handler()
{
    remember_log_and_show("Pure virtual function call.");
    exit_after_fatal_diagnostics();
}

void signal_abort_handler(int)
{
    std::string message = remembered_fatal_message();
    if (message.empty()) {
        message = "The process called abort().";
        message = message_with_last_operation(std::move(message));
        write_stderr_fallback(message);
    }
    exit_after_fatal_diagnostics();
}

#endif

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

        remember_log_and_show(std::move(message));
    } catch (...) {
        write_stderr_fallback("Filament panic occurred, but the panic handler failed while formatting details.");
    }
}

} // namespace

void install_fatal_diagnostics() noexcept
{
    utils::Panic::setPanicHandler(&filament_panic_handler, nullptr);
    std::set_terminate(&terminate_handler);

#if defined(_MSC_VER)
    _CrtSetReportHook(&crt_report_hook);
    _set_invalid_parameter_handler(&invalid_parameter_handler);
    _set_purecall_handler(&purecall_handler);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, &signal_abort_handler);
#endif
}

void record_fatal_operation(std::string_view operation)
{
    const std::lock_guard lock(fatal_context_mutex);
    last_operation.assign(operation.begin(), operation.end());
}

std::string last_fatal_operation()
{
    const std::lock_guard lock(fatal_context_mutex);
    return last_operation;
}

} // namespace quader
