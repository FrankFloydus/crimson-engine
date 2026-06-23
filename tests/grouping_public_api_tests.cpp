////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderCommandIds.h"
#include "api/QuaderEditorApi.h"
#include "assets/MaterialDocument.h"
#include "assets/TextureAsset.h"
#include "scene/document/ProjectSession.h"
#include "ui/ViewportShortcutController.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace quader;

constexpr float kEpsilon = 0.001f;

/**
 * Represents a Public API Grouping Harness value used by the automated test suite.
 */
struct PublicApiGroupingHarness {
    ProjectSession session;
    QuaderEditorApi editor;

    PublicApiGroupingHarness()
        : session()
        , editor(session) {}
};

QuaderModelingObjectRef createBox(
        QuaderEditorApi& editor,
        std::string name,
        Vec3 position = {}) {
    QuaderCreateBoxArgs args;
    args.bounds = {.min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}};
    args.name = std::move(name);

    const QuaderApiResult<QuaderModelingObjectRef> created = editor.modeling().createBox(args);
    EXPECT_TRUE(created.ok()) << created.errorMessage();

    TransformComponent transform;
    transform.position = position;
    const QuaderApiVoidResult moved = editor.object(created.value()).setTransform(transform);
    EXPECT_TRUE(moved.ok()) << moved.errorMessage();
    return created.value();
}

AssetId createTexturedMaterial(QuaderEditorApi& editor, std::string name, Vec3 color) {
    TextureAsset baseColor;
    baseColor.name = name + " Base";
    baseColor.usage = TextureUsage::BaseColor;
    baseColor.width = 4;
    baseColor.height = 4;
    baseColor.generated = true;
    const QuaderApiResult<QuaderAssetRef> baseTexture = editor.assets().addTexture(std::move(baseColor));
    EXPECT_TRUE(baseTexture.ok()) << baseTexture.errorMessage();

    TextureAsset roughness;
    roughness.name = name + " Roughness";
    roughness.usage = TextureUsage::Roughness;
    roughness.colorSpace = TextureColorSpace::Linear;
    roughness.width = 4;
    roughness.height = 4;
    roughness.generated = true;
    const QuaderApiResult<QuaderAssetRef> roughnessTexture = editor.assets().addTexture(std::move(roughness));
    EXPECT_TRUE(roughnessTexture.ok()) << roughnessTexture.errorMessage();

    MaterialDocument material =
            makeDefaultQuaderMaterial(std::move(name), baseTexture.value().id, roughnessTexture.value().id);
    material.baseColor = color;
    const QuaderApiResult<QuaderAssetRef> created = editor.assets().addMaterial(std::move(material));
    EXPECT_TRUE(created.ok()) << created.errorMessage();
    return created.value().id;
}

void expectVecNear(Vec3 actual, Vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, kEpsilon);
    EXPECT_NEAR(actual.y, expected.y, kEpsilon);
    EXPECT_NEAR(actual.z, expected.z, kEpsilon);
}

std::optional<QuaderEntitySnapshot> entity(const QuaderEditorApi& editor, EntityId id) {
    return editor.query().entity(id);
}

std::optional<QuaderEntitySnapshot> entityNamed(const QuaderEditorApi& editor, std::string_view name) {
    for (const QuaderEntitySnapshot& snapshot : editor.query().entities()) {
        if (snapshot.name == name) {
            return snapshot;
        }
    }
    return std::nullopt;
}

std::vector<QuaderEntitySnapshot> childrenOf(const QuaderEditorApi& editor, EntityId parent) {
    std::vector<QuaderEntitySnapshot> children;
    for (const QuaderEntitySnapshot& snapshot : editor.query().entities()) {
        if (snapshot.parent == parent) {
            children.push_back(snapshot);
        }
    }
    std::sort(children.begin(), children.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return children;
}

std::optional<QuaderEntitySnapshot> activeSelection(const QuaderEditorApi& editor) {
    for (const QuaderEntitySnapshot& snapshot : editor.query().entities()) {
        if (snapshot.active) {
            return snapshot;
        }
    }
    return std::nullopt;
}

Vec3 worldPosition(const QuaderEditorApi& editor, EntityId entityId) {
    Vec3 position;
    EntityId current = entityId;
    int guard = 0;
    while (current != InvalidEntityId && guard++ < 64) {
        const std::optional<QuaderEntitySnapshot> snapshot = entity(editor, current);
        if (!snapshot.has_value()) {
            break;
        }
        position = position + snapshot->transform.position;
        current = snapshot->parent;
    }
    return position;
}

bool containsEntity(const QuaderEditorApi& editor, EntityId entityId) {
    return entity(editor, entityId).has_value();
}

std::vector<EntityId> selectedEntities(const QuaderEditorApi& editor) {
    std::vector<EntityId> selected;
    for (const QuaderEntitySnapshot& snapshot : editor.query().entities()) {
        if (snapshot.selected) {
            selected.push_back(snapshot.id);
        }
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}

void selectObjects(QuaderEditorApi& editor, std::initializer_list<EntityId> entities, EntityId active) {
    const QuaderApiVoidResult selected = editor.selection().selectObjects(entities, active);
    ASSERT_TRUE(selected.ok()) << selected.errorMessage();
}

EntityId groupSelection(
        QuaderEditorApi& editor,
        std::string name,
        TransformComponent transform = {}) {
    const QuaderApiResult<QuaderEntityRef> grouped =
            editor.scene().groupSelection(std::move(name), transform);
    EXPECT_TRUE(grouped.ok()) << grouped.errorMessage();
    EXPECT_TRUE(grouped.changed());
    return grouped.value().id;
}

void selectGroupForCommandDelete(QuaderEditorApi& editor, EntityId group) {
    const QuaderApiVoidResult selected = editor.selection().selectObject(group);
    ASSERT_TRUE(selected.ok()) << selected.errorMessage();
    const QuaderApiVoidResult mode = editor.modeling().setSelectionMode(ModelingSelectionMode::Group);
    ASSERT_TRUE(mode.ok()) << mode.errorMessage();
}

void expectMaterial(const QuaderEditorApi& editor, EntityId entityId, AssetId material) {
    const std::optional<QuaderEntitySnapshot> snapshot = entity(editor, entityId);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->hasMaterial);
    EXPECT_EQ(snapshot->material, material);
}

} // namespace

TEST(GroupingPublicApiTests, GroupSelectionParentsSelectedRootsAndUndoRedoRestoresHierarchy) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef left = createBox(h.editor, "Left", {-3.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef right = createBox(h.editor, "Right", {4.0f, 0.0f, 0.0f});
    selectObjects(h.editor, {left.entity, right.entity}, right.entity);

    TransformComponent groupTransform;
    groupTransform.position = {10.0f, 0.0f, 0.0f};
    const EntityId group = groupSelection(h.editor, "Pair", groupTransform);

    ASSERT_TRUE(containsEntity(h.editor, group));
    EXPECT_EQ(entity(h.editor, group)->kind, QuaderEntityKind::Empty);
    EXPECT_EQ(entity(h.editor, left.entity)->parent, group);
    EXPECT_EQ(entity(h.editor, right.entity)->parent, group);
    EXPECT_TRUE(entity(h.editor, group)->selected);
    EXPECT_TRUE(entity(h.editor, group)->active);
    expectVecNear(worldPosition(h.editor, left.entity), {-3.0f, 0.0f, 0.0f});
    expectVecNear(worldPosition(h.editor, right.entity), {4.0f, 0.0f, 0.0f});

    ASSERT_TRUE(h.editor.history().undo().ok());
    EXPECT_FALSE(containsEntity(h.editor, group));
    EXPECT_EQ(entity(h.editor, left.entity)->parent, InvalidEntityId);
    EXPECT_EQ(entity(h.editor, right.entity)->parent, InvalidEntityId);
    EXPECT_EQ(selectedEntities(h.editor), (std::vector<EntityId>{left.entity, right.entity}));

    ASSERT_TRUE(h.editor.history().redo().ok());
    const std::optional<QuaderEntitySnapshot> regrouped = entityNamed(h.editor, "Pair");
    ASSERT_TRUE(regrouped.has_value());
    EXPECT_EQ(entity(h.editor, left.entity)->parent, regrouped->id);
    EXPECT_EQ(entity(h.editor, right.entity)->parent, regrouped->id);
    EXPECT_TRUE(regrouped->selected);
    EXPECT_TRUE(regrouped->active);
}

TEST(GroupingPublicApiTests, GroupSelectionUsesSelectedRootsOnlyWhenParentAndChildAreSelected) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef child = createBox(h.editor, "Child", {1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef sibling = createBox(h.editor, "Sibling", {3.0f, 0.0f, 0.0f});
    selectObjects(h.editor, {child.entity, sibling.entity}, sibling.entity);
    const EntityId existingGroup = groupSelection(h.editor, "Existing Group");

    selectObjects(h.editor, {existingGroup, child.entity}, existingGroup);
    const EntityId outer = groupSelection(h.editor, "Outer Group");

    EXPECT_EQ(entity(h.editor, existingGroup)->parent, outer);
    EXPECT_EQ(entity(h.editor, child.entity)->parent, existingGroup);
    EXPECT_EQ(entity(h.editor, sibling.entity)->parent, existingGroup);
    EXPECT_EQ(childrenOf(h.editor, outer).size(), 1u);
}

TEST(GroupingPublicApiTests, UngroupUndoRedoRestoresGroupSelectionAndChildPlacement) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef first = createBox(h.editor, "First", {-2.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef second = createBox(h.editor, "Second", {2.0f, 0.0f, 0.0f});
    selectObjects(h.editor, {first.entity, second.entity}, first.entity);
    TransformComponent groupTransform;
    groupTransform.position = {5.0f, 0.0f, 0.0f};
    const EntityId group = groupSelection(h.editor, "Ungroup Me", groupTransform);

    const QuaderApiVoidResult ungrouped = h.editor.scene().ungroupSelection();
    ASSERT_TRUE(ungrouped.ok()) << ungrouped.errorMessage();
    ASSERT_TRUE(ungrouped.changed());
    EXPECT_FALSE(containsEntity(h.editor, group));
    EXPECT_EQ(entity(h.editor, first.entity)->parent, InvalidEntityId);
    EXPECT_EQ(entity(h.editor, second.entity)->parent, InvalidEntityId);
    expectVecNear(worldPosition(h.editor, first.entity), {-2.0f, 0.0f, 0.0f});
    expectVecNear(worldPosition(h.editor, second.entity), {2.0f, 0.0f, 0.0f});

    ASSERT_TRUE(h.editor.history().undo().ok());
    const std::optional<QuaderEntitySnapshot> restored = activeSelection(h.editor);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->name, "Ungroup Me");
    EXPECT_EQ(restored->kind, QuaderEntityKind::Empty);
    EXPECT_EQ(entity(h.editor, first.entity)->parent, restored->id);
    EXPECT_EQ(entity(h.editor, second.entity)->parent, restored->id);
    expectVecNear(worldPosition(h.editor, first.entity), {-2.0f, 0.0f, 0.0f});
    expectVecNear(worldPosition(h.editor, second.entity), {2.0f, 0.0f, 0.0f});

    ASSERT_TRUE(h.editor.history().redo().ok());
    EXPECT_FALSE(entityNamed(h.editor, "Ungroup Me").has_value());
    EXPECT_EQ(entity(h.editor, first.entity)->parent, InvalidEntityId);
    EXPECT_EQ(entity(h.editor, second.entity)->parent, InvalidEntityId);
}

TEST(GroupingPublicApiTests, SceneDestroyGroupUndoRedoRestoresMaterialsTexturesAndTransforms) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef red = createBox(h.editor, "Red", {-4.0f, 1.0f, 0.0f});
    const QuaderModelingObjectRef blue = createBox(h.editor, "Blue", {6.0f, -2.0f, 0.0f});
    const AssetId redMaterial = createTexturedMaterial(h.editor, "Red Material", {1.0f, 0.0f, 0.0f});
    const AssetId blueMaterial = createTexturedMaterial(h.editor, "Blue Material", {0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(h.editor.assets().assignMaterialToObject(red, redMaterial).ok());
    ASSERT_TRUE(h.editor.assets().assignMaterialToObject(blue, blueMaterial).ok());

    selectObjects(h.editor, {red.entity, blue.entity}, red.entity);
    TransformComponent groupTransform;
    groupTransform.position = {12.0f, 0.0f, 0.0f};
    const EntityId group = groupSelection(h.editor, "Textured Group", groupTransform);

    ASSERT_TRUE(h.editor.scene().destroy(group).ok());
    EXPECT_FALSE(containsEntity(h.editor, group));
    EXPECT_FALSE(containsEntity(h.editor, red.entity));
    EXPECT_FALSE(containsEntity(h.editor, blue.entity));

    ASSERT_TRUE(h.editor.history().undo().ok());
    const std::optional<QuaderEntitySnapshot> restoredGroup = activeSelection(h.editor);
    ASSERT_TRUE(restoredGroup.has_value());
    EXPECT_EQ(restoredGroup->name, "Textured Group");
    const std::optional<QuaderEntitySnapshot> restoredRed = entityNamed(h.editor, "Red");
    const std::optional<QuaderEntitySnapshot> restoredBlue = entityNamed(h.editor, "Blue");
    ASSERT_TRUE(restoredRed.has_value());
    ASSERT_TRUE(restoredBlue.has_value());
    EXPECT_EQ(restoredRed->parent, restoredGroup->id);
    EXPECT_EQ(restoredBlue->parent, restoredGroup->id);
    expectVecNear(worldPosition(h.editor, restoredRed->id), {-4.0f, 1.0f, 0.0f});
    expectVecNear(worldPosition(h.editor, restoredBlue->id), {6.0f, -2.0f, 0.0f});
    expectMaterial(h.editor, restoredRed->id, redMaterial);
    expectMaterial(h.editor, restoredBlue->id, blueMaterial);
    EXPECT_EQ(h.editor.query().assets().textureCount, 4u);

    ASSERT_TRUE(h.editor.history().redo().ok());
    EXPECT_FALSE(entityNamed(h.editor, "Textured Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Red").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Blue").has_value());
}

TEST(GroupingPublicApiTests, SceneDestroyNestedGroupUndoRedoRestoresNestedHierarchy) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef left = createBox(h.editor, "Nested Left", {-2.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef right = createBox(h.editor, "Nested Right", {2.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef top = createBox(h.editor, "Nested Top", {0.0f, 3.0f, 0.0f});

    selectObjects(h.editor, {left.entity, right.entity}, left.entity);
    const EntityId innerGroup = groupSelection(h.editor, "Inner Group");
    selectObjects(h.editor, {innerGroup, top.entity}, innerGroup);
    const EntityId outerGroup = groupSelection(h.editor, "Outer Group");

    ASSERT_TRUE(h.editor.scene().destroy(outerGroup).ok());
    ASSERT_TRUE(h.editor.history().undo().ok());

    const std::optional<QuaderEntitySnapshot> restoredOuter = entityNamed(h.editor, "Outer Group");
    const std::optional<QuaderEntitySnapshot> restoredInner = entityNamed(h.editor, "Inner Group");
    const std::optional<QuaderEntitySnapshot> restoredTop = entityNamed(h.editor, "Nested Top");
    ASSERT_TRUE(restoredOuter.has_value());
    ASSERT_TRUE(restoredInner.has_value());
    ASSERT_TRUE(restoredTop.has_value());
    EXPECT_EQ(restoredInner->parent, restoredOuter->id);
    EXPECT_EQ(restoredTop->parent, restoredOuter->id);
    EXPECT_EQ(entityNamed(h.editor, "Nested Left")->parent, restoredInner->id);
    EXPECT_EQ(entityNamed(h.editor, "Nested Right")->parent, restoredInner->id);

    ASSERT_TRUE(h.editor.history().redo().ok());
    EXPECT_FALSE(entityNamed(h.editor, "Outer Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Inner Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Nested Left").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Nested Right").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Nested Top").has_value());
}

TEST(GroupingPublicApiTests, SceneDestroyGroupedChildUndoKeepsChildInsideParentGroup) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef child = createBox(h.editor, "Child Mesh", {-1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef sibling = createBox(h.editor, "Sibling Mesh", {1.0f, 0.0f, 0.0f});
    const AssetId material = createTexturedMaterial(h.editor, "Child Material", {0.2f, 0.8f, 0.4f});
    ASSERT_TRUE(h.editor.assets().assignMaterialToObject(child, material).ok());
    selectObjects(h.editor, {child.entity, sibling.entity}, child.entity);
    const EntityId group = groupSelection(h.editor, "Parent Group");

    ASSERT_TRUE(h.editor.object(child).destroy().ok());
    EXPECT_FALSE(containsEntity(h.editor, child.entity));

    ASSERT_TRUE(h.editor.history().undo().ok());
    const std::optional<QuaderEntitySnapshot> restoredChild = entityNamed(h.editor, "Child Mesh");
    ASSERT_TRUE(restoredChild.has_value());
    EXPECT_EQ(restoredChild->parent, group);
    expectMaterial(h.editor, restoredChild->id, material);
    expectVecNear(worldPosition(h.editor, restoredChild->id), {-1.0f, 0.0f, 0.0f});
}

TEST(GroupingPublicApiTests, ModelingDeleteCommandDeletesSelectedGroupInsteadOfStaleModelingObject) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef groupedA = createBox(h.editor, "Grouped A", {-1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef groupedB = createBox(h.editor, "Grouped B", {1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef decoy = createBox(h.editor, "Do Not Delete", {8.0f, 0.0f, 0.0f});
    ASSERT_TRUE(h.editor.selection().selectObject(decoy).ok());

    selectObjects(h.editor, {groupedA.entity, groupedB.entity}, groupedA.entity);
    const EntityId group = groupSelection(h.editor, "Delete Me");
    selectGroupForCommandDelete(h.editor, group);

    const QuaderApiVoidResult deleted = h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(deleted.changed());
    EXPECT_FALSE(entityNamed(h.editor, "Delete Me").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Grouped A").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Grouped B").has_value());
    EXPECT_TRUE(entityNamed(h.editor, "Do Not Delete").has_value());
}

TEST(GroupingPublicApiTests, ViewportDeleteShortcutDeletesSelectedGroup) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef left = createBox(h.editor, "Shortcut Left", {-1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef right = createBox(h.editor, "Shortcut Right", {1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef decoy = createBox(h.editor, "Shortcut Decoy", {9.0f, 0.0f, 0.0f});
    ASSERT_TRUE(h.editor.selection().selectObject(decoy).ok());

    selectObjects(h.editor, {left.entity, right.entity}, left.entity);
    const EntityId group = groupSelection(h.editor, "Shortcut Group");
    selectGroupForCommandDelete(h.editor, group);

    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame inputFrame;
    inputFrame.viewportFocused = true;
    inputFrame.viewportSize = {800.0f, 600.0f};
    inputFrame.pressedKeys = {ViewportKey::Delete};
    const ViewportInputResult input = router.handleFrame(inputFrame, camera);
    ASSERT_TRUE(input.shortcutHandled);
    ASSERT_TRUE(input.consumed);
    ASSERT_EQ(input.shortcut.kind, ViewportShortcutKind::DeleteSelection);

    const ViewportShortcutController controller;
    const ViewportShortcutControllerResult result =
            controller.handleModelingShortcut(input, h.editor, h.session.modeling());

    EXPECT_TRUE(result.commandExecuted);
    EXPECT_TRUE(result.invalidateHover);
    EXPECT_TRUE(result.markOverlayDirty);
    EXPECT_FALSE(entityNamed(h.editor, "Shortcut Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Shortcut Left").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Shortcut Right").has_value());
    EXPECT_TRUE(entityNamed(h.editor, "Shortcut Decoy").has_value());

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryUndo).ok());
    const std::optional<QuaderEntitySnapshot> restoredGroup = activeSelection(h.editor);
    ASSERT_TRUE(restoredGroup.has_value());
    EXPECT_EQ(restoredGroup->name, "Shortcut Group");
    EXPECT_TRUE(restoredGroup->selected);
    EXPECT_EQ(entityNamed(h.editor, "Shortcut Left")->parent, restoredGroup->id);
    EXPECT_EQ(entityNamed(h.editor, "Shortcut Right")->parent, restoredGroup->id);

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryRedo).ok());
    EXPECT_FALSE(entityNamed(h.editor, "Shortcut Group").has_value());
    EXPECT_TRUE(entityNamed(h.editor, "Shortcut Decoy").has_value());
}

TEST(GroupingPublicApiTests, ContextDeleteActionDeletesSelectedGroup) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef first = createBox(h.editor, "Action First", {-2.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef second = createBox(h.editor, "Action Second", {2.0f, 0.0f, 0.0f});
    const AssetId material = createTexturedMaterial(h.editor, "Action Material", {0.1f, 0.5f, 0.9f});
    ASSERT_TRUE(h.editor.assets().assignMaterialToObject(first, material).ok());
    ASSERT_TRUE(h.editor.assets().assignMaterialToObject(second, material).ok());
    selectObjects(h.editor, {first.entity, second.entity}, first.entity);
    const EntityId group = groupSelection(h.editor, "Action Group");
    selectGroupForCommandDelete(h.editor, group);

    const QuaderApiVoidResult deleted = h.editor.commands().execute(QuaderCommand::DeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(deleted.changed());
    EXPECT_FALSE(entityNamed(h.editor, "Action Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Action First").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Action Second").has_value());

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryUndo).ok());
    const std::optional<QuaderEntitySnapshot> restoredGroup = activeSelection(h.editor);
    ASSERT_TRUE(restoredGroup.has_value());
    EXPECT_EQ(restoredGroup->name, "Action Group");
    const std::optional<QuaderEntitySnapshot> restoredFirst = entityNamed(h.editor, "Action First");
    const std::optional<QuaderEntitySnapshot> restoredSecond = entityNamed(h.editor, "Action Second");
    ASSERT_TRUE(restoredFirst.has_value());
    ASSERT_TRUE(restoredSecond.has_value());
    EXPECT_EQ(restoredFirst->parent, restoredGroup->id);
    EXPECT_EQ(restoredSecond->parent, restoredGroup->id);
    expectMaterial(h.editor, restoredFirst->id, material);
    expectMaterial(h.editor, restoredSecond->id, material);
}

TEST(GroupingPublicApiTests, ObjectModeSceneSelectedGroupDeleteUsesSceneRoots) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef child = createBox(h.editor, "Object Mode Child", {-1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef sibling = createBox(h.editor, "Object Mode Sibling", {1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef decoy = createBox(h.editor, "Object Mode Decoy", {8.0f, 0.0f, 0.0f});
    ASSERT_TRUE(h.editor.selection().selectObject(decoy).ok());

    selectObjects(h.editor, {child.entity, sibling.entity}, child.entity);
    const EntityId group = groupSelection(h.editor, "Object Mode Group");
    ASSERT_TRUE(h.editor.modeling().setSelectionMode(ModelingSelectionMode::Object).ok());
    ASSERT_TRUE(h.editor.selection().selectObject(group).ok());
    ASSERT_EQ(h.editor.modeling().selectionSummary().mode, ModelingSelectionMode::Object);

    const QuaderApiVoidResult deleted = h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(deleted.changed());
    EXPECT_FALSE(entityNamed(h.editor, "Object Mode Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Object Mode Child").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Object Mode Sibling").has_value());
    EXPECT_TRUE(entityNamed(h.editor, "Object Mode Decoy").has_value());
}

TEST(GroupingPublicApiTests, ComponentModeSceneSelectedGroupDeleteUsesSceneRoots) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef child = createBox(h.editor, "Component Mode Child", {-1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef sibling = createBox(h.editor, "Component Mode Sibling", {1.0f, 0.0f, 0.0f});
    selectObjects(h.editor, {child.entity, sibling.entity}, child.entity);
    const EntityId group = groupSelection(h.editor, "Component Mode Group");

    ASSERT_TRUE(h.editor.modeling().setSelectionMode(ModelingSelectionMode::Face).ok());
    ASSERT_TRUE(h.editor.selection().selectObject(group).ok());
    ASSERT_EQ(h.editor.modeling().selectionSummary().mode, ModelingSelectionMode::Face);

    const QuaderApiVoidResult deleted = h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection);
    ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    EXPECT_TRUE(deleted.changed());
    EXPECT_FALSE(entityNamed(h.editor, "Component Mode Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Component Mode Child").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Component Mode Sibling").has_value());

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryUndo).ok());
    const std::optional<QuaderEntitySnapshot> restoredGroup = activeSelection(h.editor);
    ASSERT_TRUE(restoredGroup.has_value());
    EXPECT_EQ(restoredGroup->name, "Component Mode Group");
    EXPECT_EQ(entityNamed(h.editor, "Component Mode Child")->parent, restoredGroup->id);
    EXPECT_EQ(entityNamed(h.editor, "Component Mode Sibling")->parent, restoredGroup->id);
}

TEST(GroupingPublicApiTests, EmptySceneSelectionWithStaleModelingObjectDoesNotDeleteFallbackObject) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef decoy = createBox(h.editor, "Stale Delete Decoy", {4.0f, 0.0f, 0.0f});
    ASSERT_TRUE(h.editor.selection().selectObject(decoy).ok());
    ASSERT_EQ(h.editor.modeling().selectionSummary().mode, ModelingSelectionMode::Object);

    std::vector<EntityId> emptySelection;
    ASSERT_TRUE(h.editor.selection().selectObjects(std::span<const EntityId>(emptySelection.data(), emptySelection.size())).ok());
    ASSERT_TRUE(selectedEntities(h.editor).empty());

    const QuaderApiVoidResult deleted = h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection);
    EXPECT_FALSE(deleted.ok());
    EXPECT_FALSE(deleted.changed());
    EXPECT_TRUE(entityNamed(h.editor, "Stale Delete Decoy").has_value());
}

TEST(GroupingPublicApiTests, ModelingDeleteCommandUndoRedoRestoresSelectedGroupSubtree) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef first = createBox(h.editor, "Command First", {-3.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef second = createBox(h.editor, "Command Second", {3.0f, 0.0f, 0.0f});
    const AssetId material = createTexturedMaterial(h.editor, "Command Material", {0.7f, 0.7f, 0.1f});
    ASSERT_TRUE(h.editor.assets().assignMaterialToObject(first, material).ok());
    ASSERT_TRUE(h.editor.assets().assignMaterialToObject(second, material).ok());
    selectObjects(h.editor, {first.entity, second.entity}, first.entity);
    const EntityId group = groupSelection(h.editor, "Command Group");
    selectGroupForCommandDelete(h.editor, group);

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection).ok());
    ASSERT_FALSE(entityNamed(h.editor, "Command Group").has_value());

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryUndo).ok());
    const std::optional<QuaderEntitySnapshot> restoredGroup = activeSelection(h.editor);
    ASSERT_TRUE(restoredGroup.has_value());
    EXPECT_EQ(restoredGroup->name, "Command Group");
    const std::optional<QuaderEntitySnapshot> restoredFirst = entityNamed(h.editor, "Command First");
    const std::optional<QuaderEntitySnapshot> restoredSecond = entityNamed(h.editor, "Command Second");
    ASSERT_TRUE(restoredFirst.has_value());
    ASSERT_TRUE(restoredSecond.has_value());
    EXPECT_EQ(restoredFirst->parent, restoredGroup->id);
    EXPECT_EQ(restoredSecond->parent, restoredGroup->id);
    expectVecNear(worldPosition(h.editor, restoredFirst->id), {-3.0f, 0.0f, 0.0f});
    expectVecNear(worldPosition(h.editor, restoredSecond->id), {3.0f, 0.0f, 0.0f});
    expectMaterial(h.editor, restoredFirst->id, material);
    expectMaterial(h.editor, restoredSecond->id, material);

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryRedo).ok());
    EXPECT_FALSE(entityNamed(h.editor, "Command Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Command First").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Command Second").has_value());
}

TEST(GroupingPublicApiTests, ModelingDeleteGroupedChildUndoKeepsChildGroupedAndTextured) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef target = createBox(h.editor, "Delete Child", {-2.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef sibling = createBox(h.editor, "Keep Sibling", {2.0f, 0.0f, 0.0f});
    const AssetId material = createTexturedMaterial(h.editor, "Delete Child Material", {0.9f, 0.2f, 0.2f});
    ASSERT_TRUE(h.editor.assets().assignMaterialToObject(target, material).ok());
    selectObjects(h.editor, {target.entity, sibling.entity}, target.entity);
    const EntityId group = groupSelection(h.editor, "Child Delete Group");

    ASSERT_TRUE(h.editor.modeling().setSelectionMode(ModelingSelectionMode::Object).ok());
    ASSERT_TRUE(h.editor.selection().selectObject(target).ok());
    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection).ok());
    EXPECT_FALSE(entityNamed(h.editor, "Delete Child").has_value());
    EXPECT_TRUE(entityNamed(h.editor, "Child Delete Group").has_value());

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryUndo).ok());
    const std::optional<QuaderEntitySnapshot> restoredChild = entityNamed(h.editor, "Delete Child");
    ASSERT_TRUE(restoredChild.has_value());
    EXPECT_EQ(restoredChild->parent, group);
    expectMaterial(h.editor, restoredChild->id, material);
    expectVecNear(worldPosition(h.editor, restoredChild->id), {-2.0f, 0.0f, 0.0f});
}

TEST(GroupingPublicApiTests, RestoredGroupCanBeDeletedAgainWithModelingCommand) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef first = createBox(h.editor, "Again First", {-1.5f, 0.0f, 0.0f});
    const QuaderModelingObjectRef second = createBox(h.editor, "Again Second", {1.5f, 0.0f, 0.0f});
    selectObjects(h.editor, {first.entity, second.entity}, second.entity);
    const EntityId group = groupSelection(h.editor, "Again Group");
    selectGroupForCommandDelete(h.editor, group);

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection).ok());
    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryUndo).ok());
    const std::optional<QuaderEntitySnapshot> restoredGroup = entityNamed(h.editor, "Again Group");
    ASSERT_TRUE(restoredGroup.has_value());

    selectGroupForCommandDelete(h.editor, restoredGroup->id);
    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection).ok());
    EXPECT_FALSE(entityNamed(h.editor, "Again Group").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Again First").has_value());
    EXPECT_FALSE(entityNamed(h.editor, "Again Second").has_value());
}

TEST(GroupingPublicApiTests, SceneAwareDeleteRedoClearedByDocumentEditDoesNotReplayRuntimeRedo) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef first = createBox(h.editor, "Redo Guard First", {-1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef second = createBox(h.editor, "Redo Guard Second", {1.0f, 0.0f, 0.0f});
    selectObjects(h.editor, {first.entity, second.entity}, first.entity);
    const EntityId group = groupSelection(h.editor, "Redo Guard Group");
    selectGroupForCommandDelete(h.editor, group);

    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::ModelingDeleteSelection).ok());
    ASSERT_TRUE(h.editor.commands().execute(QuaderCommand::HistoryUndo).ok());
    const std::optional<QuaderEntitySnapshot> restoredGroup = entityNamed(h.editor, "Redo Guard Group");
    ASSERT_TRUE(restoredGroup.has_value());
    ASSERT_TRUE(entityNamed(h.editor, "Redo Guard First").has_value());
    ASSERT_TRUE(entityNamed(h.editor, "Redo Guard Second").has_value());
    ASSERT_TRUE(h.editor.history().canRedo());

    ASSERT_TRUE(h.editor.scene().rename(restoredGroup->id, "Redo Guard Edited").ok());
    EXPECT_FALSE(h.editor.history().canRedo());

    const QuaderApiVoidResult staleRedo = h.editor.commands().execute(QuaderCommand::HistoryRedo);
    EXPECT_FALSE(staleRedo.ok());
    EXPECT_TRUE(entityNamed(h.editor, "Redo Guard Edited").has_value());
    EXPECT_TRUE(entityNamed(h.editor, "Redo Guard First").has_value());
    EXPECT_TRUE(entityNamed(h.editor, "Redo Guard Second").has_value());
}

TEST(GroupingPublicApiTests, GroupVisibilityLockAndTransformBatchUndoRedoSurvivesChildren) {
    PublicApiGroupingHarness h;

    const QuaderModelingObjectRef first = createBox(h.editor, "Batch First", {-1.0f, 0.0f, 0.0f});
    const QuaderModelingObjectRef second = createBox(h.editor, "Batch Second", {1.0f, 0.0f, 0.0f});
    selectObjects(h.editor, {first.entity, second.entity}, first.entity);
    const EntityId group = groupSelection(h.editor, "Batch Group");

    TransformComponent moved;
    moved.position = {2.0f, 3.0f, 0.0f};
    ASSERT_TRUE(h.editor.scene().batch("Group Flags")
                    .setTransform(group, moved)
                    .setVisible(group, false)
                    .setLocked(group, true)
                    .commit()
                    .ok());

    EXPECT_FALSE(entity(h.editor, group)->visible);
    EXPECT_TRUE(entity(h.editor, group)->locked);
    expectVecNear(entity(h.editor, group)->transform.position, {2.0f, 3.0f, 0.0f});
    EXPECT_EQ(entity(h.editor, first.entity)->parent, group);
    EXPECT_EQ(entity(h.editor, second.entity)->parent, group);

    ASSERT_TRUE(h.editor.history().undo().ok());
    EXPECT_TRUE(entity(h.editor, group)->visible);
    EXPECT_FALSE(entity(h.editor, group)->locked);
    expectVecNear(entity(h.editor, group)->transform.position, {});
    EXPECT_EQ(entity(h.editor, first.entity)->parent, group);
    EXPECT_EQ(entity(h.editor, second.entity)->parent, group);

    ASSERT_TRUE(h.editor.history().redo().ok());
    EXPECT_FALSE(entity(h.editor, group)->visible);
    EXPECT_TRUE(entity(h.editor, group)->locked);
    expectVecNear(entity(h.editor, group)->transform.position, {2.0f, 3.0f, 0.0f});
}

TEST(GroupingPublicApiTests, DuplicateSelectedHierarchyCopiesStateSelectsCloneAndUndoRedoAsOneOperation) {
    PublicApiGroupingHarness h;

    QuaderCreateEntityArgs parentArgs;
    parentArgs.name = "Duplicate Parent";
    parentArgs.kind = QuaderEntityKind::Empty;
    const QuaderApiResult<QuaderEntityRef> parent = h.editor.scene().createEntity(parentArgs);
    ASSERT_TRUE(parent.ok()) << parent.errorMessage();

    QuaderTransform groupTransform;
    groupTransform.position = {1.0f, 2.0f, 3.0f};
    groupTransform.rotationEuler = {0.1f, 0.2f, 0.3f};
    QuaderCreateEntityArgs groupArgs;
    groupArgs.name = "Duplicate Source";
    groupArgs.kind = QuaderEntityKind::Empty;
    groupArgs.parent = parent.value().id;
    groupArgs.transform = groupTransform;
    const QuaderApiResult<QuaderEntityRef> group = h.editor.scene().createEntity(groupArgs);
    ASSERT_TRUE(group.ok()) << group.errorMessage();

    QuaderTransform childTransform;
    childTransform.position = {4.0f, 5.0f, 6.0f};
    childTransform.rotationEuler = {0.4f, 0.5f, 0.6f};
    QuaderCreateEntityArgs childArgs;
    childArgs.name = "Duplicate Child";
    childArgs.kind = QuaderEntityKind::Mesh;
    childArgs.parent = group.value().id;
    childArgs.transform = childTransform;
    const QuaderApiResult<QuaderEntityRef> child = h.editor.scene().createEntity(childArgs);
    ASSERT_TRUE(child.ok()) << child.errorMessage();

    MeshAsset mesh;
    mesh.name = "Duplicate Child Mesh";
    const QuaderApiResult<QuaderAssetRef> meshAsset = h.editor.assets().addStandaloneMesh(std::move(mesh));
    ASSERT_TRUE(meshAsset.ok()) << meshAsset.errorMessage();
    h.session.scene().setMesh(child.value().id, MeshComponent{.mesh = meshAsset.value().id});

    const QuaderApiResult<QuaderAssetRef> material =
            h.editor.assets().addMaterial(QuaderMaterialArgs{.name = "Duplicate Material"});
    ASSERT_TRUE(material.ok()) << material.errorMessage();
    ASSERT_TRUE(h.editor.assets().assignMaterialToEntity(child.value().id, material.value().id).ok());

    ASSERT_TRUE(h.editor.scene().setVisible(group.value().id, false).ok());
    ASSERT_TRUE(h.editor.scene().setLocked(group.value().id, true).ok());
    ASSERT_TRUE(h.editor.scene().setVisible(child.value().id, false).ok());
    ASSERT_TRUE(h.editor.scene().setLocked(child.value().id, true).ok());

    selectObjects(h.editor, {group.value().id, child.value().id}, child.value().id);
    const QuaderApiResult<QuaderDuplicateHierarchyResult> duplicated =
            h.editor.scene().duplicateSelectedHierarchy();
    ASSERT_TRUE(duplicated.ok()) << duplicated.errorMessage();
    ASSERT_TRUE(duplicated.changed());
    ASSERT_EQ(duplicated.value.rootEntities.size(), 1u);
    const EntityId duplicateGroup = duplicated.value.rootEntities.front().id;

    const std::optional<QuaderEntitySnapshot> duplicateGroupSnapshot = entity(h.editor, duplicateGroup);
    ASSERT_TRUE(duplicateGroupSnapshot.has_value());
    EXPECT_EQ(duplicateGroupSnapshot->name, "Duplicate Source_1");
    EXPECT_EQ(duplicateGroupSnapshot->parent, parent.value().id);
    EXPECT_FALSE(duplicateGroupSnapshot->visible);
    EXPECT_TRUE(duplicateGroupSnapshot->locked);
    expectVecNear(duplicateGroupSnapshot->transform.position, groupTransform.position);
    expectVecNear(duplicateGroupSnapshot->transform.rotationEuler, groupTransform.rotationEuler);

    const std::vector<QuaderEntitySnapshot> duplicateChildren = childrenOf(h.editor, duplicateGroup);
    ASSERT_EQ(duplicateChildren.size(), 1u);
    const QuaderEntitySnapshot& duplicateChild = duplicateChildren.front();
    EXPECT_EQ(duplicateChild.name, "Duplicate Child_1");
    EXPECT_EQ(duplicateChild.parent, duplicateGroup);
    EXPECT_FALSE(duplicateChild.visible);
    EXPECT_TRUE(duplicateChild.locked);
    EXPECT_TRUE(duplicateChild.hasMesh);
    EXPECT_EQ(duplicateChild.mesh, meshAsset.value().id);
    EXPECT_TRUE(duplicateChild.hasMaterial);
    EXPECT_EQ(duplicateChild.material, material.value().id);
    expectVecNear(duplicateChild.transform.position, childTransform.position);
    expectVecNear(duplicateChild.transform.rotationEuler, childTransform.rotationEuler);
    EXPECT_EQ(selectedEntities(h.editor), (std::vector<EntityId>{duplicateGroup}));
    ASSERT_TRUE(activeSelection(h.editor).has_value());
    EXPECT_EQ(activeSelection(h.editor)->id, duplicateGroup);
    EXPECT_EQ(h.editor.query().history().undoLabel, "Duplicate Hierarchy");

    ASSERT_TRUE(h.editor.history().undo().ok());
    EXPECT_FALSE(entity(h.editor, duplicateGroup).has_value());
    EXPECT_FALSE(entity(h.editor, duplicateChild.id).has_value());
    EXPECT_EQ(selectedEntities(h.editor), (std::vector<EntityId>{group.value().id, child.value().id}));
    ASSERT_TRUE(activeSelection(h.editor).has_value());
    EXPECT_EQ(activeSelection(h.editor)->id, child.value().id);
    EXPECT_EQ(h.editor.query().history().redoLabel, "Duplicate Hierarchy");

    ASSERT_TRUE(h.editor.history().redo().ok());
    ASSERT_TRUE(entity(h.editor, duplicateGroup).has_value());
    EXPECT_EQ(childrenOf(h.editor, duplicateGroup).size(), 1u);
    EXPECT_EQ(selectedEntities(h.editor), (std::vector<EntityId>{duplicateGroup}));
}
