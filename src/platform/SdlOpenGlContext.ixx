module;

#include "platform/SdlForward.h"

export module quader.platform.sdl_opengl_context;

export import quader.render.opengl_texture;
export import quader.platform.sdl_window;

export namespace quader {

struct SdlOpenGlContextDescriptor {
    bool vsync = true;
};

class SdlOpenGlContext {
public:
    SdlOpenGlContext(const SdlWindow& window, SdlOpenGlContextDescriptor descriptor = {});

    SdlOpenGlContext(const SdlOpenGlContext&) = delete;
    SdlOpenGlContext& operator=(const SdlOpenGlContext&) = delete;

    ~SdlOpenGlContext();

    [[nodiscard]] SDL_GLContext native_handle() const noexcept;
    [[nodiscard]] OpenGlSharedContextHandle shared_context_handle() const;
    [[nodiscard]] bool is_current() const noexcept;
    [[nodiscard]] bool try_make_current() const noexcept;

    void make_current() const;
    void clear_current() const;
    void set_swap_interval(int interval) const;
    void swap_window() const;

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
};

} // namespace quader
