module;

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

export module quader.render.driver_startup_plan;

export import quader.render.driver_kind;

export namespace quader {

enum class RenderDriverBuildAvailability {
    implemented,
    not_implemented_in_this_build,
    unavailable_on_this_platform,
};

enum class RenderSurfaceKind {
    vulkan,
    opengl,
    metal,
};

struct RenderDriverStartupCandidate {
    RenderDriverKind driver = RenderDriverKind::vulkan;
    RenderSurfaceKind surface = RenderSurfaceKind::vulkan;
    RenderDriverBuildAvailability availability = RenderDriverBuildAvailability::implemented;
};

inline constexpr std::size_t kMaxRenderDriverStartupCandidates = 4;

struct RenderDriverStartupPlan {
    RenderDriverSelection selection;
    std::array<RenderDriverStartupCandidate, kMaxRenderDriverStartupCandidates> candidates {};
    std::size_t candidate_count = 0;

    [[nodiscard]] bool is_empty() const noexcept
    {
        return candidate_count == 0;
    }

    [[nodiscard]] std::span<const RenderDriverStartupCandidate> active_candidates() const noexcept
    {
        return { candidates.data(), candidate_count };
    }
};

struct RenderDriverStartupFailure {
    RenderDriverKind driver = RenderDriverKind::vulkan;
    std::string message;
};

[[nodiscard]] RenderDriverStartupPlan make_render_driver_startup_plan(
    RenderDriverSelection selection) noexcept;
[[nodiscard]] bool render_driver_is_implemented_in_this_build(RenderDriverKind driver) noexcept;
[[nodiscard]] std::string describe_render_driver_unavailable(RenderDriverKind driver);
[[nodiscard]] std::string describe_requested_render_driver_failure(
    RenderDriverKind driver,
    std::string_view reason);
[[nodiscard]] std::string describe_automatic_render_driver_failure(
    std::span<const RenderDriverStartupFailure> failures);

} // namespace quader
