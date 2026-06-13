module;

#include "platform/SdlForward.h"

#include <cstdint>
#include <string>

export module quader.platform.sdl_window;

export namespace quader {

enum class SdlWindowGraphicsApi {
    vulkan,
    opengl,
    metal,
};

enum class SdlOpenGlProfile {
    core,
    compatibility,
    es,
};

struct SdlOpenGlWindowAttributes {
    int major_version = 4;
    int minor_version = 1;
    SdlOpenGlProfile profile = SdlOpenGlProfile::compatibility;
    int red_bits = 8;
    int green_bits = 8;
    int blue_bits = 8;
    int alpha_bits = 8;
    int depth_bits = 24;
    int stencil_bits = 0;
    bool double_buffer = true;
    bool accelerated_visual = true;
};

struct SdlWindowDescriptor {
    const char* title = "";
    int width = 0;
    int height = 0;
    SdlWindowGraphicsApi graphics_api = SdlWindowGraphicsApi::vulkan;
    SdlOpenGlWindowAttributes opengl_attributes = {};
};

class SdlWindow {
public:
    explicit SdlWindow(const SdlWindowDescriptor& descriptor);

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    ~SdlWindow();

    [[nodiscard]] SDL_Window* native_handle() const;
    [[nodiscard]] uint32_t id() const;
    [[nodiscard]] bool is_relative_mouse_mode_enabled() const;
    [[nodiscard]] std::string last_error() const;

    void set_relative_mouse_mode(bool enabled);

private:
    SDL_Window* window_ = nullptr;
};

} // namespace quader
