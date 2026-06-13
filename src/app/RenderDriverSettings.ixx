module;

#include <optional>
#include <string>
#include <string_view>

export module quader.app.render_driver_settings;

export import quader.platform.user_settings_paths;
export import quader.render.driver_kind;

export namespace quader {

[[nodiscard]] std::optional<RenderDriverSelection> parse_render_driver_settings(
    std::string_view contents) noexcept;
[[nodiscard]] std::string serialize_render_driver_settings(RenderDriverSelection selection);

class RenderDriverSettingsStore final {
public:
    explicit RenderDriverSettingsStore(const UserSettingsPaths& paths);
    explicit RenderDriverSettingsStore(std::string settings_file);

    [[nodiscard]] std::string_view settings_file() const noexcept;
    [[nodiscard]] RenderDriverSelection load_selection() const;
    void save_selection(RenderDriverSelection selection) const;

private:
    std::string settings_file_;
};

} // namespace quader
