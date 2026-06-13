module;
#include <SDL3/SDL.h>

#include <cstdint>
#include <stdexcept>
#include <string>

module quader.platform.sdl_window;

namespace quader {
namespace {

[[nodiscard]] SDL_WindowFlags to_sdl_window_flags(SdlWindowGraphicsApi graphics_api)
{
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    switch (graphics_api) {
    case SdlWindowGraphicsApi::vulkan:
        flags |= SDL_WINDOW_VULKAN;
        break;
    case SdlWindowGraphicsApi::opengl:
        flags |= SDL_WINDOW_OPENGL;
        break;
    case SdlWindowGraphicsApi::metal:
        flags |= SDL_WINDOW_METAL;
        break;
    }

    return flags;
}

[[nodiscard]] int to_sdl_gl_profile(SdlOpenGlProfile profile) noexcept
{
    switch (profile) {
    case SdlOpenGlProfile::core:
        return SDL_GL_CONTEXT_PROFILE_CORE;
    case SdlOpenGlProfile::compatibility:
        return SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    case SdlOpenGlProfile::es:
        return SDL_GL_CONTEXT_PROFILE_ES;
    }

    return SDL_GL_CONTEXT_PROFILE_CORE;
}

void set_gl_attribute(SDL_GLAttr attribute, int value, const char* name)
{
    if (!SDL_GL_SetAttribute(attribute, value)) {
        throw std::runtime_error(std::string("SDL_GL_SetAttribute(") + name + ") failed: " + SDL_GetError());
    }
}

void configure_opengl_window_attributes(const SdlOpenGlWindowAttributes& attributes)
{
    SDL_GL_ResetAttributes();
    set_gl_attribute(SDL_GL_CONTEXT_MAJOR_VERSION, attributes.major_version, "SDL_GL_CONTEXT_MAJOR_VERSION");
    set_gl_attribute(SDL_GL_CONTEXT_MINOR_VERSION, attributes.minor_version, "SDL_GL_CONTEXT_MINOR_VERSION");
    set_gl_attribute(SDL_GL_CONTEXT_PROFILE_MASK, to_sdl_gl_profile(attributes.profile), "SDL_GL_CONTEXT_PROFILE_MASK");
    set_gl_attribute(SDL_GL_RED_SIZE, attributes.red_bits, "SDL_GL_RED_SIZE");
    set_gl_attribute(SDL_GL_GREEN_SIZE, attributes.green_bits, "SDL_GL_GREEN_SIZE");
    set_gl_attribute(SDL_GL_BLUE_SIZE, attributes.blue_bits, "SDL_GL_BLUE_SIZE");
    set_gl_attribute(SDL_GL_ALPHA_SIZE, attributes.alpha_bits, "SDL_GL_ALPHA_SIZE");
    set_gl_attribute(SDL_GL_DEPTH_SIZE, attributes.depth_bits, "SDL_GL_DEPTH_SIZE");
    set_gl_attribute(SDL_GL_STENCIL_SIZE, attributes.stencil_bits, "SDL_GL_STENCIL_SIZE");
    set_gl_attribute(SDL_GL_DOUBLEBUFFER, attributes.double_buffer ? 1 : 0, "SDL_GL_DOUBLEBUFFER");
    set_gl_attribute(SDL_GL_ACCELERATED_VISUAL, attributes.accelerated_visual ? 1 : 0, "SDL_GL_ACCELERATED_VISUAL");
}

} // namespace

SdlWindow::SdlWindow(const SdlWindowDescriptor& descriptor)
{
    if (descriptor.graphics_api == SdlWindowGraphicsApi::opengl) {
        configure_opengl_window_attributes(descriptor.opengl_attributes);
    }

    window_ = SDL_CreateWindow(
        descriptor.title,
        descriptor.width,
        descriptor.height,
        to_sdl_window_flags(descriptor.graphics_api));

    if (window_ == nullptr) {
        throw std::runtime_error(SDL_GetError());
    }
}

SdlWindow::~SdlWindow()
{
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
}

SDL_Window* SdlWindow::native_handle() const
{
    return window_;
}

uint32_t SdlWindow::id() const
{
    return SDL_GetWindowID(window_);
}

bool SdlWindow::is_relative_mouse_mode_enabled() const
{
    return SDL_GetWindowRelativeMouseMode(window_);
}

std::string SdlWindow::last_error() const
{
    return SDL_GetError();
}

void SdlWindow::set_relative_mouse_mode(bool enabled)
{
    if (is_relative_mouse_mode_enabled() == enabled) {
        return;
    }

    if (!SDL_SetWindowRelativeMouseMode(window_, enabled)) {
        throw std::runtime_error(std::string("SDL_SetWindowRelativeMouseMode failed: ") + SDL_GetError());
    }
}

} // namespace quader
