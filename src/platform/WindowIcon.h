#pragma once

struct SDL_Window;

namespace crimson {

// Borrows SDL's class icons; the window must be destroyed before SDL_Quit.
bool applyWindowClassIcons(SDL_Window* window) noexcept;

} // namespace crimson
