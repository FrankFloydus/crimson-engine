module;

#include <string>
#include <string_view>

export module quader.render.status;

export namespace quader {

enum class RenderEngineKind {
    unavailable,
    filament,
};

[[nodiscard]] constexpr std::string_view render_engine_kind_token(RenderEngineKind kind) noexcept
{
    switch (kind) {
    case RenderEngineKind::filament:
        return "filament";
    case RenderEngineKind::unavailable:
        return "unavailable";
    }

    return "unavailable";
}

[[nodiscard]] constexpr std::string_view render_engine_kind_display_name(RenderEngineKind kind) noexcept
{
    switch (kind) {
    case RenderEngineKind::filament:
        return "Filament";
    case RenderEngineKind::unavailable:
        return "Unavailable";
    }

    return "Unavailable";
}

struct RendererStatus {
    bool initialized = false;
    RenderEngineKind engine_kind = RenderEngineKind::unavailable;
    std::string engine_name = "Unavailable";
    std::string backend_name = "Unavailable";
    std::string message = "Renderer has not been initialized.";
};

} // namespace quader
