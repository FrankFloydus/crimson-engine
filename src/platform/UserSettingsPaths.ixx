module;

#include <string>
#include <string_view>

export module quader.platform.user_settings_paths;

export namespace quader {

class UserSettingsPaths final {
public:
    UserSettingsPaths();

    [[nodiscard]] std::string_view settings_directory() const;
    [[nodiscard]] std::string_view imgui_ini_file() const;
    [[nodiscard]] std::string_view render_driver_settings_file() const;
    [[nodiscard]] const char* imgui_ini_file_c_str() const;

private:
    std::string settings_directory_;
    std::string imgui_ini_file_;
    std::string render_driver_settings_file_;
};

} // namespace quader
