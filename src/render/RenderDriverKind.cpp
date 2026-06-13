module;

#include <cstddef>
#include <optional>
#include <string_view>

module quader.render.driver_kind;

namespace quader {
namespace {

[[nodiscard]] char ascii_lower(char value) noexcept
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }

    return value;
}

[[nodiscard]] bool ascii_equals_ignore_case(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool is_ascii_space(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

[[nodiscard]] std::string_view trim_ascii_space(std::string_view value) noexcept
{
    while (!value.empty() && is_ascii_space(value.front())) {
        value.remove_prefix(1);
    }

    while (!value.empty() && is_ascii_space(value.back())) {
        value.remove_suffix(1);
    }

    return value;
}

} // namespace

std::string_view render_driver_kind_token(RenderDriverKind kind) noexcept
{
    switch (kind) {
    case RenderDriverKind::automatic:
        return "automatic";
    case RenderDriverKind::vulkan:
        return "vulkan";
    case RenderDriverKind::opengl:
        return "opengl";
    case RenderDriverKind::metal:
        return "metal";
    }

    return "automatic";
}

std::string_view render_driver_kind_display_name(RenderDriverKind kind) noexcept
{
    switch (kind) {
    case RenderDriverKind::automatic:
        return "Automatic";
    case RenderDriverKind::vulkan:
        return "Vulkan";
    case RenderDriverKind::opengl:
        return "OpenGL";
    case RenderDriverKind::metal:
        return "Metal";
    }

    return "Automatic";
}

std::optional<RenderDriverKind> parse_render_driver_kind(std::string_view token) noexcept
{
    token = trim_ascii_space(token);

    if (ascii_equals_ignore_case(token, "automatic") || ascii_equals_ignore_case(token, "auto")) {
        return RenderDriverKind::automatic;
    }
    if (ascii_equals_ignore_case(token, "vulkan")) {
        return RenderDriverKind::vulkan;
    }
    if (ascii_equals_ignore_case(token, "opengl") || ascii_equals_ignore_case(token, "open_gl")) {
        return RenderDriverKind::opengl;
    }
    if (ascii_equals_ignore_case(token, "metal")) {
        return RenderDriverKind::metal;
    }

    return std::nullopt;
}

} // namespace quader
