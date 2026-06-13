#include <gtest/gtest.h>

#include <optional>

import quader.app.editor_hover;
import quader.render.viewport_gizmos;
import quader.scene.scene;

TEST(EditorHover, StoresAndClearsHoveredEntityAndPayload)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorHoverModel hover;
    const quader::DebugDrawPickPayload payload = quader::transform_gizmo_pick_payload(
        quader::RenderEntityId { cube.value },
        quader::DebugDrawPickRole::transform_move_axis,
        quader::DebugDrawAxis::x);

    EXPECT_TRUE(hover.set_hovered_pick(cube, payload));
    ASSERT_TRUE(hover.hovered_entity().has_value());
    EXPECT_EQ(cube, *hover.hovered_entity());
    ASSERT_TRUE(hover.hovered_debug_payload().has_value());
    EXPECT_EQ(payload, *hover.hovered_debug_payload());

    EXPECT_TRUE(hover.clear());
    EXPECT_FALSE(hover.hovered_entity().has_value());
    EXPECT_FALSE(hover.hovered_debug_payload().has_value());
}

TEST(EditorHover, ValidationClearsDestroyedOwner)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorHoverModel hover;

    ASSERT_TRUE(hover.set_hovered_pick(cube, std::nullopt));
    ASSERT_TRUE(scene.destroy_entity(cube));

    EXPECT_TRUE(hover.validate_against(scene));
    EXPECT_FALSE(hover.hovered_entity().has_value());
}
