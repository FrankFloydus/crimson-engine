module;

#include <SDL3/SDL_messagebox.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>
#include <string_view>

module quader.platform.fatal_error_dialog;

namespace quader {

void show_fatal_error_dialog(std::string_view exact_message)
{
    const std::string message = exact_message.empty()
        ? "Unknown fatal error."
        : std::string(exact_message);

    if (SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Quader Native - Fatal Error",
            message.c_str(),
            nullptr)) {
        return;
    }

#if defined(_WIN32)
    MessageBoxA(
        nullptr,
        message.c_str(),
        "Quader Native - Fatal Error",
        MB_OK | MB_ICONERROR);
#endif
}

} // namespace quader
