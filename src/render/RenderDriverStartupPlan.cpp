module;

#include <span>
#include <string>
#include <string_view>

module quader.render.driver_startup_plan;

namespace quader {
namespace {

[[nodiscard]] RenderSurfaceKind surface_for_driver(RenderDriverKind driver) noexcept
{
    switch (driver) {
    case RenderDriverKind::vulkan:
        return RenderSurfaceKind::vulkan;
    case RenderDriverKind::opengl:
        return RenderSurfaceKind::opengl;
    case RenderDriverKind::metal:
        return RenderSurfaceKind::metal;
    case RenderDriverKind::automatic:
        break;
    }

    return RenderSurfaceKind::vulkan;
}

[[nodiscard]] RenderDriverBuildAvailability availability_for_driver(
    RenderDriverKind driver) noexcept
{
    return render_driver_is_implemented_in_this_build(driver)
        ? RenderDriverBuildAvailability::implemented
        : RenderDriverBuildAvailability::not_implemented_in_this_build;
}

void append_candidate(RenderDriverStartupPlan& plan, RenderDriverKind driver) noexcept
{
    if (plan.candidate_count >= plan.candidates.size()) {
        return;
    }

    plan.candidates[plan.candidate_count] = RenderDriverStartupCandidate {
        .driver = driver,
        .surface = surface_for_driver(driver),
        .availability = availability_for_driver(driver),
    };
    ++plan.candidate_count;
}

[[nodiscard]] std::string to_string(std::string_view value)
{
    return std::string(value.data(), value.size());
}

} // namespace

RenderDriverStartupPlan make_render_driver_startup_plan(RenderDriverSelection selection) noexcept
{
    RenderDriverStartupPlan plan;
    plan.selection = selection;

    if (!selection.is_automatic()) {
        append_candidate(plan, selection.requested_driver);
        return plan;
    }

    append_candidate(plan, RenderDriverKind::vulkan);
    append_candidate(plan, RenderDriverKind::opengl);
    return plan;
}

bool render_driver_is_implemented_in_this_build(RenderDriverKind driver) noexcept
{
    switch (driver) {
    case RenderDriverKind::vulkan:
    case RenderDriverKind::opengl:
        return true;
    case RenderDriverKind::automatic:
    case RenderDriverKind::metal:
        return false;
    }

    return false;
}

std::string describe_render_driver_unavailable(RenderDriverKind driver)
{
    return to_string(render_driver_kind_display_name(driver)) +
        " backend is not implemented in this build.";
}

std::string describe_requested_render_driver_failure(
    RenderDriverKind driver,
    std::string_view reason)
{
    return "Requested render driver " + to_string(render_driver_kind_display_name(driver)) +
        " failed to initialize: " + to_string(reason);
}

std::string describe_automatic_render_driver_failure(
    std::span<const RenderDriverStartupFailure> failures)
{
    std::string message = "Automatic render driver selection failed.";
    if (failures.empty()) {
        return message;
    }

    message += " Tried ";
    for (std::size_t index = 0; index < failures.size(); ++index) {
        if (index > 0) {
            message += "; ";
        }
        message += render_driver_kind_display_name(failures[index].driver);
        message += ": ";
        message += failures[index].message;
    }

    return message;
}

} // namespace quader
