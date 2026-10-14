#include "WindowPlacement.h"
#include "assets/AtomicFile.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <array>
#include <cstdint>
#include <fstream>

namespace crimson {
namespace {
HWND nativeWindow(SDL_Window* window) {
    return window ? static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr)) : nullptr;
}
}
bool restoreWindowPlacement(SDL_Window* window,const std::filesystem::path& file) noexcept {
    try {
        const auto handle=nativeWindow(window);
        if (!handle || !std::filesystem::exists(file)) return false;
        std::ifstream input(file);
        const auto saved=nlohmann::json::parse(input);
        if (saved.at("version")!=1) return false;
        const auto rectangle=saved.at("normalRectangle").get<std::array<LONG,4>>();
        const auto width=static_cast<std::int64_t>(rectangle[2])-rectangle[0];
        const auto height=static_cast<std::int64_t>(rectangle[3])-rectangle[1];
        if (width<=0 || height<=0 || width>65536 || height>65536) return false;
        WINDOWPLACEMENT placement{};
        placement.length=sizeof(placement);
        const bool maximized=saved.value("maximized",false);
        placement.showCmd=SW_HIDE;
        placement.rcNormalPosition={rectangle[0],rectangle[1],rectangle[2],rectangle[3]};
        const auto maximizedPosition=saved.value("maximizedPosition",std::array<LONG,2>{-1,-1});
        placement.ptMaxPosition={maximizedPosition[0],maximizedPosition[1]};
        // Restore workspace coordinates without showing the native window.
        // SDL_OnWindowShown applies SDL's pending flags, restoring a window
        // when no maximize was queued. Showing it directly with Win32's
        // SW_SHOWMAXIMIZED would therefore be undone by that callback.
        const auto initialDpi=GetDpiForWindow(handle);
        if (!SetWindowPlacement(handle,&placement)) return false;
        // Moving to another monitor can make SDL's WM_DPICHANGED handler
        // preserve the startup client size. Reapply the saved rectangle once
        // the destination DPI is established.
        if (GetDpiForWindow(handle)!=initialDpi && !SetWindowPlacement(handle,&placement)) return false;
        if (maximized && !SDL_MaximizeWindow(window)) return false;
        return SDL_ShowWindow(window);
    } catch (...) { return false; }
}
bool saveWindowPlacement(SDL_Window* window,const std::filesystem::path& file) noexcept {
    try {
        const auto handle=nativeWindow(window);
        WINDOWPLACEMENT placement{};
        placement.length=sizeof(placement);
        if (!handle || !GetWindowPlacement(handle,&placement)) return false;
        const auto& rectangle=placement.rcNormalPosition;
        const bool maximized=placement.showCmd==SW_SHOWMAXIMIZED ||
            (placement.showCmd==SW_SHOWMINIMIZED && (placement.flags&WPF_RESTORETOMAXIMIZED));
        const nlohmann::json saved{{"version",1},{"maximized",maximized},
            {"maximizedPosition",{placement.ptMaxPosition.x,placement.ptMaxPosition.y}},
            {"normalRectangle",{rectangle.left,rectangle.top,rectangle.right,rectangle.bottom}}};
        writeFileAtomically(file,saved.dump(2)+"\n");
        return true;
    } catch (...) { return false; }
}
}
