#include <gtest/gtest.h>

#include <vector>

import quader.render.driver_startup_plan;

namespace quader {
namespace {

TEST(RenderDriverStartupPlanTests, AutomaticPlanTriesImplementedDriversInPreferredOrder)
{
    const RenderDriverStartupPlan plan = make_render_driver_startup_plan(RenderDriverSelection {});

    ASSERT_EQ(plan.candidate_count, 2U);
    EXPECT_EQ(plan.candidates[0].driver, RenderDriverKind::vulkan);
    EXPECT_EQ(plan.candidates[0].surface, RenderSurfaceKind::vulkan);
    EXPECT_EQ(
        plan.candidates[0].availability,
        RenderDriverBuildAvailability::implemented);
    EXPECT_EQ(plan.candidates[1].driver, RenderDriverKind::opengl);
    EXPECT_EQ(plan.candidates[1].surface, RenderSurfaceKind::opengl);
    EXPECT_EQ(
        plan.candidates[1].availability,
        RenderDriverBuildAvailability::implemented);
}

TEST(RenderDriverStartupPlanTests, ExplicitVulkanPlanContainsOnlyVulkan)
{
    const RenderDriverStartupPlan plan = make_render_driver_startup_plan(RenderDriverSelection {
        .requested_driver = RenderDriverKind::vulkan,
    });

    ASSERT_EQ(plan.candidate_count, 1U);
    EXPECT_EQ(plan.candidates[0].driver, RenderDriverKind::vulkan);
    EXPECT_EQ(plan.candidates[0].surface, RenderSurfaceKind::vulkan);
}

TEST(RenderDriverStartupPlanTests, ExplicitOpenGlPlanDoesNotFallbackToVulkan)
{
    const RenderDriverStartupPlan plan = make_render_driver_startup_plan(RenderDriverSelection {
        .requested_driver = RenderDriverKind::opengl,
    });

    ASSERT_EQ(plan.candidate_count, 1U);
    EXPECT_EQ(plan.candidates[0].driver, RenderDriverKind::opengl);
    EXPECT_EQ(plan.candidates[0].surface, RenderSurfaceKind::opengl);
    EXPECT_EQ(
        plan.candidates[0].availability,
        RenderDriverBuildAvailability::implemented);
}

TEST(RenderDriverStartupPlanTests, ExplicitMetalPlanDoesNotFallbackToVulkan)
{
    const RenderDriverStartupPlan plan = make_render_driver_startup_plan(RenderDriverSelection {
        .requested_driver = RenderDriverKind::metal,
    });

    ASSERT_EQ(plan.candidate_count, 1U);
    EXPECT_EQ(plan.candidates[0].driver, RenderDriverKind::metal);
    EXPECT_EQ(plan.candidates[0].surface, RenderSurfaceKind::metal);
    EXPECT_EQ(
        plan.candidates[0].availability,
        RenderDriverBuildAvailability::not_implemented_in_this_build);
}

TEST(RenderDriverStartupPlanTests, UnavailableBackendMessagesArePrecise)
{
    EXPECT_EQ(
        describe_render_driver_unavailable(RenderDriverKind::metal),
        "Metal backend is not implemented in this build.");
}

TEST(RenderDriverStartupPlanTests, AutomaticFailureAggregatesAttemptMessagesInOrder)
{
    const std::vector<RenderDriverStartupFailure> failures {
        RenderDriverStartupFailure {
            .driver = RenderDriverKind::vulkan,
            .message = "Vulkan device not found.",
        },
        RenderDriverStartupFailure {
            .driver = RenderDriverKind::opengl,
            .message = "OpenGL context failed.",
        },
    };

    EXPECT_EQ(
        describe_automatic_render_driver_failure(failures),
        "Automatic render driver selection failed. Tried Vulkan: Vulkan device not found.; "
        "OpenGL: OpenGL context failed.");
}

TEST(RenderDriverStartupPlanTests, ExplicitFailurePreservesBackendDetail)
{
    EXPECT_EQ(
        describe_requested_render_driver_failure(
            RenderDriverKind::vulkan,
            "Render engine failed to initialize (Vulkan): Filament Engine::create returned null."),
        "Requested render driver Vulkan failed to initialize: Render engine failed to initialize "
        "(Vulkan): Filament Engine::create returned null.");
}

} // namespace
} // namespace quader
