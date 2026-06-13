module;

#include <optional>
#include <string_view>

export module quader.render.driver_kind;

export namespace quader {

enum class RenderDriverKind {
    automatic,
    vulkan,
    opengl,
    metal,
};

struct RenderDriverSelection {
    RenderDriverKind requested_driver = RenderDriverKind::automatic;

    [[nodiscard]] bool is_automatic() const noexcept
    {
        return requested_driver == RenderDriverKind::automatic;
    }
};

[[nodiscard]] std::string_view render_driver_kind_token(RenderDriverKind kind) noexcept;
[[nodiscard]] std::string_view render_driver_kind_display_name(RenderDriverKind kind) noexcept;
[[nodiscard]] std::optional<RenderDriverKind> parse_render_driver_kind(
    std::string_view token) noexcept;

} // namespace quader
