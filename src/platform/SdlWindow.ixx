module;

#include <SDL3/SDL_video.h>

#include <string>

export module quader.platform.sdl_window;

export namespace quader {

class SdlWindow {
public:
    SdlWindow(const char* title, int width, int height);

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    ~SdlWindow();

    [[nodiscard]] SDL_Window* native_handle() const;
    [[nodiscard]] std::string vulkan_error() const;

private:
    SDL_Window* window_ = nullptr;
};

} // namespace quader
