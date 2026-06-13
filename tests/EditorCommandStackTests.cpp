#include <gtest/gtest.h>

import quader.app.editor_command_stack;
import quader.scene.scene;

TEST(EditorCommandStack, PreviewAndFinishTransformEditCreatesUndoableCommand)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorCommandStack commands;

    ASSERT_TRUE(commands.begin_transform_edit(scene, cube));

    quader::TransformComponent moved = *scene.transform(cube);
    moved.position.x = 3.0f;
    EXPECT_TRUE(commands.preview_transform_edit(scene, cube, moved));
    EXPECT_TRUE(commands.finish_transform_edit(scene));
    EXPECT_EQ(1U, commands.undo_count());
    EXPECT_EQ(0U, commands.redo_count());
    ASSERT_NE(nullptr, scene.transform(cube));
    EXPECT_FLOAT_EQ(3.0f, scene.transform(cube)->position.x);

    EXPECT_TRUE(commands.undo(scene));
    EXPECT_FLOAT_EQ(0.0f, scene.transform(cube)->position.x);
    EXPECT_EQ(0U, commands.undo_count());
    EXPECT_EQ(1U, commands.redo_count());

    EXPECT_TRUE(commands.redo(scene));
    EXPECT_FLOAT_EQ(3.0f, scene.transform(cube)->position.x);
}

TEST(EditorCommandStack, CancelTransformEditRestoresPreview)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorCommandStack commands;

    ASSERT_TRUE(commands.begin_transform_edit(scene, cube));

    quader::TransformComponent moved = *scene.transform(cube);
    moved.position.y = 5.0f;
    EXPECT_TRUE(commands.preview_transform_edit(scene, cube, moved));
    EXPECT_TRUE(commands.has_active_transform_edit());
    EXPECT_TRUE(commands.cancel_transform_edit(scene));

    ASSERT_NE(nullptr, scene.transform(cube));
    EXPECT_FLOAT_EQ(0.0f, scene.transform(cube)->position.y);
    EXPECT_EQ(0U, commands.undo_count());
}
