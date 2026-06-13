#include <gtest/gtest.h>

import quader.app.editor_selection;

TEST(EditorSelection, StoresAndClearsSelectedEntity)
{
    quader::EditorSelectionModel selection;
    EXPECT_TRUE(selection.select_entity(quader::SceneEntityId { 42 }));

    ASSERT_TRUE(selection.selected_entity().has_value());
    EXPECT_EQ(42U, selection.selected_entity()->value);
    EXPECT_TRUE(selection.has_selection());

    EXPECT_TRUE(selection.clear());
    EXPECT_FALSE(selection.selected_entity().has_value());
    EXPECT_FALSE(selection.has_selection());
}

TEST(EditorSelection, InvalidEntityClearsSelection)
{
    quader::EditorSelectionModel selection;
    EXPECT_FALSE(selection.select_entity(quader::SceneEntityId {}));

    EXPECT_FALSE(selection.selected_entity().has_value());
}

TEST(EditorSelection, SelectionMutatorsReportChangedState)
{
    quader::EditorSelectionModel selection;

    EXPECT_TRUE(selection.select_entity(quader::SceneEntityId { 42 }));
    EXPECT_FALSE(selection.select_entity(quader::SceneEntityId { 42 }));
    EXPECT_TRUE(selection.select_entity(quader::SceneEntityId { 84 }));
    EXPECT_TRUE(selection.select_entity(quader::SceneEntityId {}));
    EXPECT_FALSE(selection.select_entity(quader::SceneEntityId {}));
    EXPECT_FALSE(selection.clear());
}

TEST(EditorSelection, ValidationClearsDestroyedEntity)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();
    quader::EditorSelectionModel selection;
    ASSERT_TRUE(selection.select_entity(cube));

    ASSERT_TRUE(scene.destroy_entity(cube));
    EXPECT_TRUE(selection.validate_against(scene));
    EXPECT_FALSE(selection.has_selection());
}
