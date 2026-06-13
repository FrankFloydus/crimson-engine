module;

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <memory>
#include <stdexcept>
#include <string>

module quader.platform.sdl_user_settings_paths;

namespace quader {
namespace {

constexpr const char* kOrganizationName = "Quader";
constexpr const char* kApplicationName = "Quader Native";

struct SdlAllocatedStringDeleter {
    void operator()(char* value) const noexcept
    {
        SDL_free(value);
    }
};

std::string sdl_error_message()
{
    const char* error = SDL_GetError();
    if (error == nullptr || error[0] == '\0') {
        return "SDL reported no error details.";
    }

    return error;
}

std::string resolve_sdl_pref_settings_directory()
{
    using SdlAllocatedString = std::unique_ptr<char, SdlAllocatedStringDeleter>;

    SdlAllocatedString directory(SDL_GetPrefPath(kOrganizationName, kApplicationName));
    if (directory == nullptr || directory.get()[0] == '\0') {
        throw std::runtime_error(
            "SDL_GetPrefPath failed to resolve the Quader Native user settings directory: " +
            sdl_error_message());
    }

    return directory.get();
}

void throw_if_not_directory(const std::string& directory, const SDL_PathInfo& path_info)
{
    if (path_info.type == SDL_PATHTYPE_DIRECTORY) {
        return;
    }

    throw std::runtime_error(
        "Quader Native user settings path exists but is not a directory: " + directory);
}

void ensure_settings_directory_exists(const std::string& directory)
{
    if (directory.empty()) {
        throw std::runtime_error("Quader Native user settings directory resolved to an empty path.");
    }

    SDL_PathInfo path_info {};
    if (SDL_GetPathInfo(directory.c_str(), &path_info)) {
        throw_if_not_directory(directory, path_info);
        return;
    }

    const std::string initial_path_error = sdl_error_message();
    if (!SDL_CreateDirectory(directory.c_str())) {
        const std::string create_error = sdl_error_message();
        if (SDL_GetPathInfo(directory.c_str(), &path_info)) {
            throw_if_not_directory(directory, path_info);
            return;
        }

        throw std::runtime_error(
            "Failed to create Quader Native user settings directory '" + directory +
            "': " + create_error + " (initial path check: " + initial_path_error + ")");
    }

    if (!SDL_GetPathInfo(directory.c_str(), &path_info)) {
        throw std::runtime_error(
            "Created Quader Native user settings directory '" + directory +
            "', but verification failed: " + sdl_error_message());
    }

    throw_if_not_directory(directory, path_info);
}

} // namespace

UserSettingsPaths resolve_sdl_user_settings_paths()
{
    std::string settings_directory = resolve_sdl_pref_settings_directory();
    ensure_settings_directory_exists(settings_directory);
    return UserSettingsPaths(std::move(settings_directory));
}

} // namespace quader
