module;

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

module quader.platform.user_settings_paths;

namespace quader {
namespace {

constexpr const char* kOrganizationName = "Quader";
constexpr const char* kApplicationName = "Quader Native";
constexpr std::string_view kImGuiIniFilename = "imgui.ini";
constexpr std::string_view kRenderDriverSettingsFilename = "render_driver_settings.ini";

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

std::string join_path(std::string base, std::string_view leaf)
{
    if (base.empty()) {
        return std::string(leaf);
    }

    const char last = base.back();
    if (last != '/' && last != '\\') {
        base.push_back('/');
    }

    base.append(leaf);
    return base;
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

UserSettingsPaths::UserSettingsPaths()
    : settings_directory_(resolve_sdl_pref_settings_directory())
    , imgui_ini_file_(join_path(settings_directory_, kImGuiIniFilename))
    , render_driver_settings_file_(join_path(settings_directory_, kRenderDriverSettingsFilename))
{
    ensure_settings_directory_exists(settings_directory_);
}

std::string_view UserSettingsPaths::settings_directory() const
{
    return settings_directory_;
}

std::string_view UserSettingsPaths::imgui_ini_file() const
{
    return imgui_ini_file_;
}

std::string_view UserSettingsPaths::render_driver_settings_file() const
{
    return render_driver_settings_file_;
}

const char* UserSettingsPaths::imgui_ini_file_c_str() const
{
    return imgui_ini_file_.c_str();
}

} // namespace quader
