module;

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

module quader.app.user_settings_paths;

namespace quader {
namespace {

constexpr std::string_view kImGuiIniFilename = "imgui.ini";
constexpr std::string_view kRenderDriverSettingsFilename = "render_driver_settings.ini";

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

} // namespace

UserSettingsPaths::UserSettingsPaths(std::string settings_directory)
    : settings_directory_(std::move(settings_directory))
    , imgui_ini_file_(join_path(settings_directory_, kImGuiIniFilename))
    , render_driver_settings_file_(join_path(settings_directory_, kRenderDriverSettingsFilename))
{
    if (settings_directory_.empty()) {
        throw std::invalid_argument("User settings directory path is empty.");
    }
}

std::string_view UserSettingsPaths::settings_directory() const noexcept
{
    return settings_directory_;
}

std::string_view UserSettingsPaths::imgui_ini_file() const noexcept
{
    return imgui_ini_file_;
}

std::string_view UserSettingsPaths::render_driver_settings_file() const noexcept
{
    return render_driver_settings_file_;
}

} // namespace quader
