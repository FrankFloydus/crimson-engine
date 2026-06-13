module;

#include <SDL3/SDL.h>

#include <stdexcept>

module quader.platform.sdl_platform_context;

namespace quader {

SdlPlatformContext::SdlPlatformContext()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(SDL_GetError());
    }
}

SdlPlatformContext::~SdlPlatformContext()
{
    SDL_Quit();
}

} // namespace quader
