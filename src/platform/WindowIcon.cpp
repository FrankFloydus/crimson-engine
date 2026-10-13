#include "WindowIcon.h"

#include <SDL3/SDL.h>
#include <windows.h>

namespace crimson {

bool applyWindowClassIcons(SDL_Window* window) noexcept {
    if (!window) return false;
    const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return false;

    const auto largeIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    const auto smallIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    // Explicit window icons notify the taskbar instead of relying on its cached
    // executable icon. SDL retains ownership until its window class is removed.
    if (largeIcon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    if (smallIcon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    return largeIcon && smallIcon;
}

} // namespace crimson
