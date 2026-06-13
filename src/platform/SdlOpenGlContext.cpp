module;
#include <SDL3/SDL.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#endif

#include <stdexcept>
#include <string>

module quader.platform.sdl_opengl_context;

namespace quader {
namespace {

[[nodiscard]] std::runtime_error sdl_error(const char* operation)
{
    return std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
}

} // namespace

SdlOpenGlContext::SdlOpenGlContext(const SdlWindow& window, SdlOpenGlContextDescriptor descriptor)
    : window_(window.native_handle())
{
    context_ = SDL_GL_CreateContext(window_);
    if (context_ == nullptr) {
        throw sdl_error("SDL_GL_CreateContext");
    }

    try {
        make_current();
        set_swap_interval(descriptor.vsync ? 1 : 0);
    } catch (...) {
        SDL_GL_DestroyContext(context_);
        context_ = nullptr;
        throw;
    }
}

SdlOpenGlContext::~SdlOpenGlContext()
{
    if (context_ == nullptr) {
        return;
    }

    if (is_current()) {
        SDL_GL_MakeCurrent(window_, nullptr);
    }

    SDL_GL_DestroyContext(context_);
    context_ = nullptr;
}

SDL_GLContext SdlOpenGlContext::native_handle() const noexcept
{
    return context_;
}

OpenGlSharedContextHandle SdlOpenGlContext::shared_context_handle() const
{
    make_current();

#if defined(_WIN32)
    HGLRC current_context = wglGetCurrentContext();
    if (current_context == nullptr) {
        throw std::runtime_error("wglGetCurrentContext returned null for the SDL OpenGL context.");
    }

    return OpenGlSharedContextHandle {
        .api = OpenGlSharedContextApi::windows_wgl,
        .native_handle = current_context,
    };
#else
    return {};
#endif
}

bool SdlOpenGlContext::is_current() const noexcept
{
    return SDL_GL_GetCurrentContext() == context_;
}

bool SdlOpenGlContext::try_make_current() const noexcept
{
    return SDL_GL_MakeCurrent(window_, context_);
}

void SdlOpenGlContext::make_current() const
{
    if (!try_make_current()) {
        throw sdl_error("SDL_GL_MakeCurrent");
    }
}

void SdlOpenGlContext::clear_current() const
{
    if (!SDL_GL_MakeCurrent(window_, nullptr)) {
        throw sdl_error("SDL_GL_MakeCurrent(clear)");
    }
}

void SdlOpenGlContext::set_swap_interval(int interval) const
{
    make_current();
    if (!SDL_GL_SetSwapInterval(interval)) {
        throw sdl_error("SDL_GL_SetSwapInterval");
    }
}

void SdlOpenGlContext::swap_window() const
{
    if (!SDL_GL_SwapWindow(window_)) {
        throw sdl_error("SDL_GL_SwapWindow");
    }
}

} // namespace quader
