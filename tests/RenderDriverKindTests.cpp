#include <gtest/gtest.h>

#include <optional>

import quader.render.driver_kind;

namespace quader {
namespace {

TEST(RenderDriverKindTests, RoundTripsStableTokens)
{
    EXPECT_EQ(parse_render_driver_kind("automatic"), RenderDriverKind::automatic);
    EXPECT_EQ(parse_render_driver_kind("vulkan"), RenderDriverKind::vulkan);
    EXPECT_EQ(parse_render_driver_kind("opengl"), RenderDriverKind::opengl);
    EXPECT_EQ(parse_render_driver_kind("metal"), RenderDriverKind::metal);
}

TEST(RenderDriverKindTests, AcceptsFriendlyStartupTokens)
{
    EXPECT_EQ(parse_render_driver_kind(" auto "), RenderDriverKind::automatic);
    EXPECT_EQ(parse_render_driver_kind("Vulkan"), RenderDriverKind::vulkan);
    EXPECT_EQ(parse_render_driver_kind("Open_GL"), RenderDriverKind::opengl);
}

TEST(RenderDriverKindTests, RejectsUnknownTokens)
{
    EXPECT_EQ(parse_render_driver_kind(""), std::nullopt);
    EXPECT_EQ(parse_render_driver_kind("direct3d"), std::nullopt);
}

TEST(RenderDriverKindTests, ProvidesTokensAndDisplayNames)
{
    EXPECT_EQ(render_driver_kind_token(RenderDriverKind::automatic), "automatic");
    EXPECT_EQ(render_driver_kind_token(RenderDriverKind::vulkan), "vulkan");
    EXPECT_EQ(render_driver_kind_token(RenderDriverKind::opengl), "opengl");
    EXPECT_EQ(render_driver_kind_token(RenderDriverKind::metal), "metal");

    EXPECT_EQ(render_driver_kind_display_name(RenderDriverKind::automatic), "Automatic");
    EXPECT_EQ(render_driver_kind_display_name(RenderDriverKind::vulkan), "Vulkan");
    EXPECT_EQ(render_driver_kind_display_name(RenderDriverKind::opengl), "OpenGL");
    EXPECT_EQ(render_driver_kind_display_name(RenderDriverKind::metal), "Metal");
}

TEST(RenderDriverKindTests, SelectionDefaultsToAutomatic)
{
    const RenderDriverSelection selection;

    EXPECT_TRUE(selection.is_automatic());
    EXPECT_EQ(selection.requested_driver, RenderDriverKind::automatic);
}

} // namespace
} // namespace quader
