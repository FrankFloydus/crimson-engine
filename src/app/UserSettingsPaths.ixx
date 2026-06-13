module;

#include <string>
#include <string_view>

export module quader.app.user_settings_paths;

export namespace quader {

class UserSettingsPaths final {
public:
    explicit UserSettingsPaths(std::string settings_directory);

    [[nodiscard]] std::string_view settings_directory() const noexcept;
    [[nodiscard]] std::string_view imgui_ini_file() const noexcept;
    [[nodiscard]] std::string_view render_driver_settings_file() const noexcept;

private:
    std::string settings_directory_;
    std::string imgui_ini_file_;
    std::string render_driver_settings_file_;
};

} // namespace quader
