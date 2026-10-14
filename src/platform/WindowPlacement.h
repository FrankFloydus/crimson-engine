#pragma once

#include <filesystem>

struct SDL_Window;

namespace crimson {
// Windows stores the restored rectangle even while the editor is maximized or
// minimized. Keep that rectangle and the maximized state together so reopening
// uses the same monitor and still has the correct Restore Down dimensions.
// Restore takes an initially hidden window and shows it through SDL so its
// pending maximized state agrees with the native show/resize callbacks.
bool restoreWindowPlacement(SDL_Window*, const std::filesystem::path&) noexcept;
bool saveWindowPlacement(SDL_Window*, const std::filesystem::path&) noexcept;
}
