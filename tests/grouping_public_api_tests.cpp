////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/behavior_expect.hpp"
#include "support/behavior_scenario.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace quader;
using quader::test::BehaviorScenario;

constexpr float kHalfPi = 1.57079632679f;

QuaderBounds unitBoxBounds() {
    return {.min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}};
}

QuaderTransform transformAt(QVec3 position) {
    QuaderTransform transform;
    transform.position = position;
    return transform;
}

void givenBoxAt(BehaviorScenario& scenario, std::string_view alias, std::string name, QVec3 position) {
    scenario.given().box(alias, unitBoxBounds(), std::move(name));
    scenario.given().transform(alias, transformAt(position));
}

AssetId createTexturedMaterial(BehaviorScenario& scenario, std::string name, QVec3 color) {
    QuaderTextureAsset baseColor;
    baseColor.name = name + " Base";
    baseColor.usage = QuaderTextureUsage::BaseColor;
    baseColor.width = 4;
    baseColor.height = 4;
    baseColor.generated = true;
    const QuaderApiResult<QuaderAssetRef> baseTexture = scenario.api().assets().addTexture(std::move(baseColor));
    EXPECT_TRUE(baseTexture.ok()) << baseTexture.errorMessage();

    QuaderTextureAsset roughness;
    roughness.name = name + " Roughness";
    roughness.usage = QuaderTextureUsage::Roughness;
    roughness.colorSpace = QuaderTextureColorSpace::Linear;
    roughness.width = 4;
    roughness.height = 4;
    roughness.generated = true;
    const QuaderApiResult<QuaderAssetRef> roughnessTexture = scenario.api().assets().addTexture(std::move(roughness));
    EXPECT_TRUE(roughnessTexture.ok()) << roughnessTexture.errorMessage();

    QuaderMaterialAsset material;
    material.name = std::move(name);
    material.baseColor = color;
    material.textureSlots.emplace("baseColorTexture", baseTexture.value().id);
    material.textureSlots.emplace("roughnessTexture", roughnessTexture.value().id);
    const QuaderApiResult<QuaderAssetRef> created = scenario.api().assets().addMaterial(std::move(material));
    EXPECT_TRUE(created.ok()) << created.errorMessage();
    return created.value().id;
}

AssetId createMaterial(BehaviorScenario& scenario, std::string name) {
    const QuaderApiResult<QuaderAssetRef> material =
            scenario.api().assets().addMaterial(QuaderMaterialArgs{.name = std::move(name)});
    EXPECT_TRUE(material.ok()) << material.errorMessage();
    return material.value().id;
}

void assignMaterial(BehaviorScenario& scenario, std::string_view alias, AssetId material) {
    const QuaderApiVoidResult assigned = scenario.api().assets().assignMaterialToObject(scenario.object(alias), material);
    ASSERT_TRUE(assigned.ok()) << assigned.errorMessage();
}

void selectGroupForCommandDelete(BehaviorScenario& scenario, std::string_view groupAlias) {
    scenario.given().selected(groupAlias);
    scenario.given().selectionMode(QuaderSelectionMode::Group);
}

void rememberRestoredSubtree(BehaviorScenario& scenario, std::string_view groupAlias, std::string_view groupName) {
    scenario.rememberEntityNamed(groupAlias, groupName);
}

std::optional<QuaderEntitySnapshot> entityById(const QuaderApi& api, EntityId entity) {
    return api.query().entity(entity);
}

std::vector<QuaderEntitySnapshot> childrenOf(const QuaderApi& api, EntityId parent) {
    std::vector<QuaderEntitySnapshot> children;
    for (const QuaderEntitySnapshot& snapshot : api.query().entities()) {
        if (snapshot.parent == parent) {
            children.push_back(snapshot);
        }
    }
    return children;
}

std::vector<EntityId> selectedEntities(const QuaderApi& api) {
    std::vector<EntityId> selected;
    for (const QuaderEntitySnapshot& snapshot : api.query().entities()) {
        if (snapshot.selected) {
            selected.push_back(snapshot.id);
        }
    }
    return selected;
}

EntityId selectedEntityExcluding(const QuaderApi& api, EntityId excluded) {
    for (const EntityId entity : selectedEntities(api)) {
        if (entity != excluded) {
            return entity;
        }
    }
    return InvalidEntityId;
}

} // namespace

TEST(GroupingPublicApiTests, BehaviorWorldPositionExpectationUsesSceneWorldTransform) {
    BehaviorScenario scenario = BehaviorScenario::newProject();

    QuaderTransform parentTransform;
    parentTransform.position = {10.0f, -1.0f, 0.0f};
    parentTransform.rotationEuler = {0.0f, 0.0f, kHalfPi};
    parentTransform.scale = {2.0f, 3.0f, 1.0f};

    QuaderTransform childTransform;
    childTransform.position = {1.0f, 0.0f, 0.0f};

    scenario.given().entity("parent", "Rotated Parent", QuaderEntityKind::Empty, parentTransform);
    scenario.given().entity("child", "World Child", QuaderEntityKind::Empty, childTransform, "parent");

    scenario.then().scene().worldPositionNear("child", {10.0f, 1.0f, 0.0f});
}

TEST(GroupingPublicApiTests, GroupSelectionParentsSelectedRootsAndUndoRedoRestoresHierarchy) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "left", "Left", {-3.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "right", "Right", {4.0f, 0.0f, 0.0f});
    scenario.given().selectedAliases({"left", "right"}, "right");

    QuaderTransform groupTransform;
    groupTransform.position = {10.0f, 0.0f, 0.0f};
    scenario.when().groupSelection("group", "Pair", groupTransform);

    scenario.then().command().okChanged();
    scenario.then().scene()
            .hasAlias("group")
            .aliasKind("group", QuaderEntityKind::Empty)
            .parent("left", "group")
            .parent("right", "group")
            .selectedAliases({"group"})
            .activeAlias("group")
            .worldPositionNear("left", {-3.0f, 0.0f, 0.0f})
            .worldPositionNear("right", {4.0f, 0.0f, 0.0f});

    scenario.when().undo();
    scenario.then().scene()
            .missingAlias("group")
            .noParent("left")
            .noParent("right")
            .selectedAliases({"left", "right"});

    scenario.when().redo();
    scenario.rememberEntityNamed("group", "Pair");
    scenario.then().scene()
            .parent("left", "group")
            .parent("right", "group")
            .selectedAliases({"group"})
            .activeAlias("group");
}

TEST(GroupingPublicApiTests, GroupSelectionUsesSelectedRootsOnlyWhenParentAndChildAreSelected) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "child", "Child", {1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "sibling", "Sibling", {3.0f, 0.0f, 0.0f});
    scenario.given().selectedAliases({"child", "sibling"}, "sibling");
    scenario.when().groupSelection("existingGroup", "Existing Group");

    scenario.given().selectedAliases({"existingGroup", "child"}, "existingGroup");
    scenario.when().groupSelection("outer", "Outer Group");

    scenario.then().scene()
            .parent("existingGroup", "outer")
            .parent("child", "existingGroup")
            .parent("sibling", "existingGroup")
            .childCount("outer", 1u);
}

TEST(GroupingPublicApiTests, UngroupUndoRedoRestoresGroupSelectionAndChildPlacement) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "first", "First", {-2.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "second", "Second", {2.0f, 0.0f, 0.0f});
    scenario.given().selectedAliases({"first", "second"}, "first");
    QuaderTransform groupTransform;
    groupTransform.position = {5.0f, 0.0f, 0.0f};
    scenario.when().groupSelection("group", "Ungroup Me", groupTransform);

    scenario.when().ungroupSelection();
    scenario.then().command().okChanged();
    scenario.then().scene()
            .missingAlias("group")
            .noParent("first")
            .noParent("second")
            .worldPositionNear("first", {-2.0f, 0.0f, 0.0f})
            .worldPositionNear("second", {2.0f, 0.0f, 0.0f});

    scenario.when().undo();
    rememberRestoredSubtree(scenario, "group", "Ungroup Me");
    scenario.then().scene()
            .activeAlias("group")
            .aliasKind("group", QuaderEntityKind::Empty)
            .parent("first", "group")
            .parent("second", "group")
            .worldPositionNear("first", {-2.0f, 0.0f, 0.0f})
            .worldPositionNear("second", {2.0f, 0.0f, 0.0f});

    scenario.when().redo();
    scenario.then().scene()
            .namedMissing("Ungroup Me")
            .noParent("first")
            .noParent("second");
}

TEST(GroupingPublicApiTests, SceneDestroyGroupUndoRedoRestoresMaterialsTexturesAndTransforms) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "red", "Red", {-4.0f, 1.0f, 0.0f});
    givenBoxAt(scenario, "blue", "Blue", {6.0f, -2.0f, 0.0f});
    const AssetId redMaterial = createTexturedMaterial(scenario, "Red Material", {1.0f, 0.0f, 0.0f});
    const AssetId blueMaterial = createTexturedMaterial(scenario, "Blue Material", {0.0f, 0.0f, 1.0f});
    assignMaterial(scenario, "red", redMaterial);
    assignMaterial(scenario, "blue", blueMaterial);

    scenario.given().selectedAliases({"red", "blue"}, "red");
    QuaderTransform groupTransform;
    groupTransform.position = {12.0f, 0.0f, 0.0f};
    scenario.when().groupSelection("group", "Textured Group", groupTransform);

    scenario.when().destroy("group");
    scenario.then().scene().missingAlias("group").missingAlias("red").missingAlias("blue");

    scenario.when().undo();
    scenario.rememberEntityNamed("group", "Textured Group");
    scenario.rememberEntityNamed("red", "Red");
    scenario.rememberEntityNamed("blue", "Blue");
    scenario.then().scene()
            .activeAlias("group")
            .parent("red", "group")
            .parent("blue", "group")
            .worldPositionNear("red", {-4.0f, 1.0f, 0.0f})
            .worldPositionNear("blue", {6.0f, -2.0f, 0.0f})
            .hasMaterial("red", redMaterial)
            .hasMaterial("blue", blueMaterial);
    scenario.then().assets().textureCount(4u);

    scenario.when().redo();
    scenario.then().scene()
            .namedMissing("Textured Group")
            .namedMissing("Red")
            .namedMissing("Blue");
}

TEST(GroupingPublicApiTests, SceneDestroyNestedGroupUndoRedoRestoresNestedHierarchy) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "left", "Nested Left", {-2.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "right", "Nested Right", {2.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "top", "Nested Top", {0.0f, 3.0f, 0.0f});

    scenario.given().selectedAliases({"left", "right"}, "left");
    scenario.when().groupSelection("inner", "Inner Group");
    scenario.given().selectedAliases({"inner", "top"}, "inner");
    scenario.when().groupSelection("outer", "Outer Group");

    scenario.when().destroy("outer");
    scenario.when().undo();
    scenario.rememberEntityNamed("outer", "Outer Group");
    scenario.rememberEntityNamed("inner", "Inner Group");
    scenario.rememberEntityNamed("top", "Nested Top");
    scenario.rememberEntityNamed("left", "Nested Left");
    scenario.rememberEntityNamed("right", "Nested Right");
    scenario.then().scene()
            .parent("inner", "outer")
            .parent("top", "outer")
            .parent("left", "inner")
            .parent("right", "inner");

    scenario.when().redo();
    scenario.then().scene()
            .namedMissing("Outer Group")
            .namedMissing("Inner Group")
            .namedMissing("Nested Left")
            .namedMissing("Nested Right")
            .namedMissing("Nested Top");
}

TEST(GroupingPublicApiTests, SceneDestroyGroupedChildUndoKeepsChildInsideParentGroup) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    scenario.given().entity("child", "Child Entity", QuaderEntityKind::Empty, transformAt({-1.0f, 0.0f, 0.0f}));
    scenario.given().entity("sibling", "Sibling Entity", QuaderEntityKind::Empty, transformAt({1.0f, 0.0f, 0.0f}));
    scenario.given().selectedAliases({"child", "sibling"}, "child");
    scenario.when().groupSelection("group", "Parent Group");

    scenario.when().destroy("child");
    scenario.then().scene().missingAlias("child");

    scenario.when().undo();
    scenario.rememberEntityNamed("child", "Child Entity");
    scenario.then().scene()
            .parent("child", "group")
            .worldPositionNear("child", {-1.0f, 0.0f, 0.0f});
}

TEST(GroupingPublicApiTests, ModelingDeleteCommandDeletesSelectedGroupInsteadOfStaleModelingObject) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "groupedA", "Grouped A", {-1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "groupedB", "Grouped B", {1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "decoy", "Do Not Delete", {8.0f, 0.0f, 0.0f});
    scenario.given().selected("decoy");

    scenario.given().selectedAliases({"groupedA", "groupedB"}, "groupedA");
    scenario.when().groupSelection("group", "Delete Me");
    selectGroupForCommandDelete(scenario, "group");

    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.then().command().okChanged();
    scenario.then().scene()
            .namedMissing("Delete Me")
            .namedMissing("Grouped A")
            .namedMissing("Grouped B")
            .namedExists("Do Not Delete");
}

TEST(GroupingPublicApiTests, ViewportDeleteShortcutDeletesSelectedGroup) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "left", "Shortcut Left", {-1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "right", "Shortcut Right", {1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "decoy", "Shortcut Decoy", {9.0f, 0.0f, 0.0f});
    scenario.given().selected("decoy");

    scenario.given().selectedAliases({"left", "right"}, "left");
    scenario.when().groupSelection("group", "Shortcut Group");
    selectGroupForCommandDelete(scenario, "group");

    scenario.when().dispatchViewportShortcut({
            .kind = ViewportShortcutKind::DeleteSelection,
            .context = ViewportShortcutContext::Global,
    });
    scenario.then().command().okChanged();
    scenario.then().scene()
            .namedMissing("Shortcut Group")
            .namedMissing("Shortcut Left")
            .namedMissing("Shortcut Right")
            .namedExists("Shortcut Decoy");

    scenario.when().execute(QuaderCommand::HistoryUndo);
    scenario.rememberEntityNamed("group", "Shortcut Group");
    scenario.rememberEntityNamed("left", "Shortcut Left");
    scenario.rememberEntityNamed("right", "Shortcut Right");
    scenario.then().scene()
            .activeAlias("group")
            .parent("left", "group")
            .parent("right", "group");

    scenario.when().execute(QuaderCommand::HistoryRedo);
    scenario.then().scene().namedMissing("Shortcut Group").namedExists("Shortcut Decoy");
}

TEST(GroupingPublicApiTests, ContextDeleteActionDeletesSelectedGroup) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "first", "Action First", {-2.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "second", "Action Second", {2.0f, 0.0f, 0.0f});
    const AssetId material = createTexturedMaterial(scenario, "Action Material", {0.1f, 0.5f, 0.9f});
    assignMaterial(scenario, "first", material);
    assignMaterial(scenario, "second", material);
    scenario.given().selectedAliases({"first", "second"}, "first");
    scenario.when().groupSelection("group", "Action Group");
    selectGroupForCommandDelete(scenario, "group");

    scenario.when().execute(QuaderCommand::DeleteSelection);
    scenario.then().command().okChanged();
    scenario.then().scene()
            .namedMissing("Action Group")
            .namedMissing("Action First")
            .namedMissing("Action Second");

    scenario.when().execute(QuaderCommand::HistoryUndo);
    scenario.rememberEntityNamed("group", "Action Group");
    scenario.rememberEntityNamed("first", "Action First");
    scenario.rememberEntityNamed("second", "Action Second");
    scenario.then().scene()
            .activeAlias("group")
            .parent("first", "group")
            .parent("second", "group")
            .hasMaterial("first", material)
            .hasMaterial("second", material);
}

TEST(GroupingPublicApiTests, ObjectModeSceneSelectedGroupDeleteUsesSceneRoots) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "child", "Object Mode Child", {-1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "sibling", "Object Mode Sibling", {1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "decoy", "Object Mode Decoy", {8.0f, 0.0f, 0.0f});
    scenario.given().selected("decoy");

    scenario.given().selectedAliases({"child", "sibling"}, "child");
    scenario.when().groupSelection("group", "Object Mode Group");
    scenario.given().selectionMode(QuaderSelectionMode::Object).selected("group");
    scenario.then().selection().mode(QuaderSelectionMode::Object);

    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.then().command().okChanged();
    scenario.then().scene()
            .namedMissing("Object Mode Group")
            .namedMissing("Object Mode Child")
            .namedMissing("Object Mode Sibling")
            .namedExists("Object Mode Decoy");
}

TEST(GroupingPublicApiTests, ComponentModeSceneSelectedGroupForcesEntityModeAndDeleteUsesSceneRoots) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "child", "Component Mode Child", {-1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "sibling", "Component Mode Sibling", {1.0f, 0.0f, 0.0f});
    scenario.given().selectedAliases({"child", "sibling"}, "child");
    scenario.when().groupSelection("group", "Component Mode Group");

    scenario.given().selectionMode(QuaderSelectionMode::Face).selected("group");
    scenario.then().selection().mode(QuaderSelectionMode::Object);

    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.then().command().okChanged();
    scenario.then().scene()
            .namedMissing("Component Mode Group")
            .namedMissing("Component Mode Child")
            .namedMissing("Component Mode Sibling");

    scenario.when().execute(QuaderCommand::HistoryUndo);
    scenario.rememberEntityNamed("group", "Component Mode Group");
    scenario.rememberEntityNamed("child", "Component Mode Child");
    scenario.rememberEntityNamed("sibling", "Component Mode Sibling");
    scenario.then().scene().activeAlias("group").parent("child", "group").parent("sibling", "group");
}

TEST(GroupingPublicApiTests, EmptySceneSelectionWithStaleModelingObjectDoesNotDeleteFallbackObject) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "decoy", "Stale Delete Decoy", {4.0f, 0.0f, 0.0f});
    scenario.given().selected("decoy");
    scenario.then().selection().mode(QuaderSelectionMode::Object);

    scenario.when().clearSceneSelection();
    scenario.then().scene().selectedAliases({});

    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.then().command().failed();
    scenario.then().scene().namedExists("Stale Delete Decoy");
}

TEST(GroupingPublicApiTests, ModelingDeleteCommandUndoRedoRestoresSelectedGroupSubtree) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "first", "Command First", {-3.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "second", "Command Second", {3.0f, 0.0f, 0.0f});
    const AssetId material = createTexturedMaterial(scenario, "Command Material", {0.7f, 0.7f, 0.1f});
    assignMaterial(scenario, "first", material);
    assignMaterial(scenario, "second", material);
    scenario.given().selectedAliases({"first", "second"}, "first");
    scenario.when().groupSelection("group", "Command Group");
    selectGroupForCommandDelete(scenario, "group");

    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.then().scene().namedMissing("Command Group");

    scenario.when().execute(QuaderCommand::HistoryUndo);
    scenario.rememberEntityNamed("group", "Command Group");
    scenario.rememberEntityNamed("first", "Command First");
    scenario.rememberEntityNamed("second", "Command Second");
    scenario.then().scene()
            .activeAlias("group")
            .parent("first", "group")
            .parent("second", "group")
            .worldPositionNear("first", {-3.0f, 0.0f, 0.0f})
            .worldPositionNear("second", {3.0f, 0.0f, 0.0f})
            .hasMaterial("first", material)
            .hasMaterial("second", material);

    scenario.when().execute(QuaderCommand::HistoryRedo);
    scenario.then().scene()
            .namedMissing("Command Group")
            .namedMissing("Command First")
            .namedMissing("Command Second");
}

TEST(GroupingPublicApiTests, ModelingDeleteGroupedChildUndoKeepsChildGroupedAndTextured) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "target", "Delete Child", {-2.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "sibling", "Keep Sibling", {2.0f, 0.0f, 0.0f});
    const AssetId material = createTexturedMaterial(scenario, "Delete Child Material", {0.9f, 0.2f, 0.2f});
    assignMaterial(scenario, "target", material);
    scenario.given().selectedAliases({"target", "sibling"}, "target");
    scenario.when().groupSelection("group", "Child Delete Group");

    scenario.given().selectionMode(QuaderSelectionMode::Object).selected("target");
    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.then().scene().namedMissing("Delete Child").namedExists("Child Delete Group");

    scenario.when().execute(QuaderCommand::HistoryUndo);
    scenario.rememberEntityNamed("target", "Delete Child");
    scenario.then().scene()
            .parent("target", "group")
            .hasMaterial("target", material)
            .worldPositionNear("target", {-2.0f, 0.0f, 0.0f});
}

TEST(GroupingPublicApiTests, RestoredGroupCanBeDeletedAgainWithModelingCommand) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "first", "Again First", {-1.5f, 0.0f, 0.0f});
    givenBoxAt(scenario, "second", "Again Second", {1.5f, 0.0f, 0.0f});
    scenario.given().selectedAliases({"first", "second"}, "second");
    scenario.when().groupSelection("group", "Again Group");
    selectGroupForCommandDelete(scenario, "group");

    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.when().execute(QuaderCommand::HistoryUndo);
    scenario.rememberEntityNamed("group", "Again Group");

    selectGroupForCommandDelete(scenario, "group");
    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.then().scene()
            .namedMissing("Again Group")
            .namedMissing("Again First")
            .namedMissing("Again Second");
}

TEST(GroupingPublicApiTests, SceneAwareDeleteRedoClearedByDocumentEditDoesNotReplayRuntimeRedo) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "first", "Redo Guard First", {-1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "second", "Redo Guard Second", {1.0f, 0.0f, 0.0f});
    scenario.given().selectedAliases({"first", "second"}, "first");
    scenario.when().groupSelection("group", "Redo Guard Group");
    selectGroupForCommandDelete(scenario, "group");

    scenario.when().execute(QuaderCommand::ModelingDeleteSelection);
    scenario.when().execute(QuaderCommand::HistoryUndo);
    scenario.rememberEntityNamed("group", "Redo Guard Group");
    scenario.then().scene()
            .namedExists("Redo Guard First")
            .namedExists("Redo Guard Second");
    scenario.then().history().canRedo();

    scenario.when().rename("group", "Redo Guard Edited");
    scenario.then().history().canRedo(false);

    scenario.when().execute(QuaderCommand::HistoryRedo);
    scenario.then().command().failed();
    scenario.then().scene()
            .namedExists("Redo Guard Edited")
            .namedExists("Redo Guard First")
            .namedExists("Redo Guard Second");
}

TEST(GroupingPublicApiTests, GroupVisibilityLockAndTransformBatchUndoRedoSurvivesChildren) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "first", "Batch First", {-1.0f, 0.0f, 0.0f});
    givenBoxAt(scenario, "second", "Batch Second", {1.0f, 0.0f, 0.0f});
    scenario.given().selectedAliases({"first", "second"}, "first");
    scenario.when().groupSelection("group", "Batch Group");

    QuaderTransform moved;
    moved.position = {2.0f, 3.0f, 0.0f};
    scenario.when().setGroupFlags("group", moved, false, true);
    scenario.then().command().okChanged();
    scenario.then().scene()
            .visible("group", false)
            .locked("group", true)
            .entityTransformNear("group", moved)
            .parent("first", "group")
            .parent("second", "group");

    scenario.when().undo();
    scenario.then().scene()
            .visible("group", true)
            .locked("group", false)
            .entityTransformNear("group", QuaderTransform{})
            .parent("first", "group")
            .parent("second", "group");

    scenario.when().redo();
    scenario.then().scene()
            .visible("group", false)
            .locked("group", true)
            .entityTransformNear("group", moved);
}

TEST(GroupingPublicApiTests, DuplicateSelectedHierarchyCopiesStateSelectsCloneAndUndoRedoAsOneOperation) {
    BehaviorScenario scenario = BehaviorScenario::newProject();

    QuaderTransform parentTransform;
    scenario.given().entity("parent", "Duplicate Parent", QuaderEntityKind::Empty, parentTransform);

    QuaderTransform groupCreateTransform;
    groupCreateTransform.position = {1.0f, 2.0f, 3.0f};
    groupCreateTransform.rotationEuler = {0.1f, 0.2f, 0.3f};
    scenario.given().entity("group", "Duplicate Source", QuaderEntityKind::Empty, groupCreateTransform, "parent");

    givenBoxAt(scenario, "child", "Duplicate Child", {4.0f, 5.0f, 6.0f});
    scenario.when().reparent("child", "group", false);
    const AssetId childMesh = scenario.object("child").mesh;

    const AssetId material = createMaterial(scenario, "Duplicate Material");
    assignMaterial(scenario, "child", material);
    scenario.when().setVisible("group", false);
    scenario.when().setVisible("child", false);

    scenario.given().selectedAliases({"group", "child"}, "child");
    scenario.when().duplicateSelectedHierarchy("group.copy");
    scenario.then().command().okChanged();
    scenario.rememberEntityNamed("child.copy", "Duplicate Child_1");

    QuaderTransform groupExpected;
    groupExpected.position = groupCreateTransform.position;
    groupExpected.rotationEuler = groupCreateTransform.rotationEuler;
    QuaderTransform childExpected;
    childExpected.position = {4.0f, 5.0f, 6.0f};

    scenario.then().scene()
            .aliasName("group.copy", "Duplicate Source_1")
            .parent("group.copy", "parent")
            .visible("group.copy", false)
            .locked("group.copy", false)
            .entityTransformNear("group.copy", groupExpected)
            .childCount("group.copy", 1u)
            .aliasName("child.copy", "Duplicate Child_1")
            .parent("child.copy", "group.copy")
            .visible("child.copy", false)
            .locked("child.copy", false)
            .hasMesh("child.copy", childMesh)
            .hasMaterial("child.copy", material)
            .entityTransformNear("child.copy", childExpected)
            .selectedAliases({"group.copy"})
            .activeAlias("group.copy");
    scenario.then().history().undoLabel("Duplicate Hierarchy");

    scenario.when().undo();
    scenario.then().scene()
            .missingAlias("group.copy")
            .missingAlias("child.copy")
            .selectedAliases({"group", "child"})
            .activeAlias("child");
    scenario.then().history().redoLabel("Duplicate Hierarchy");

    scenario.when().redo();
    scenario.rememberEntityNamed("group.copy", "Duplicate Source_1");
    scenario.rememberEntityNamed("child.copy", "Duplicate Child_1");
    scenario.then().scene()
            .hasAlias("group.copy")
            .childCount("group.copy", 1u)
            .selectedAliases({"group.copy"});
}

TEST(GroupingPublicApiTests, DuplicateMixedMeshAndLightHierarchyCopiesStateAndUndoRedoAsOneOperation) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "mesh", "Mixed Duplicate Mesh", {-2.0f, 0.0f, 0.0f});

    QuaderCreatePointLightArgs lightArgs;
    lightArgs.name = "Mixed Duplicate Light";
    lightArgs.transform.position = {3.0f, 1.0f, 0.0f};
    const QuaderApiResult<QuaderEntityRef> light = scenario.api().scene().createPointLight(lightArgs);
    ASSERT_TRUE(light.ok) << light.message;
    scenario.remember("light", light.value);

    scenario.given().selectedAliases({"mesh", "light"}, "light");
    scenario.when().groupSelection("group", "Mixed Duplicate Group");
    const EntityId sourceGroup = scenario.entity("group");

    scenario.when().execute(QuaderCommand::DuplicateSelection);
    scenario.then().command().okChanged();
    const EntityId duplicateGroup = selectedEntityExcluding(scenario.api(), sourceGroup);
    ASSERT_NE(duplicateGroup, InvalidEntityId);
    scenario.remember("group.copy", duplicateGroup);

    const std::vector<QuaderEntitySnapshot> children = childrenOf(scenario.api(), duplicateGroup);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_TRUE(std::any_of(children.begin(), children.end(), [](const QuaderEntitySnapshot& child) {
        return child.hasMesh;
    }));
    EXPECT_TRUE(std::any_of(children.begin(), children.end(), [](const QuaderEntitySnapshot& child) {
        return child.hasLight && child.light.type == QuaderLightType::Point;
    }));
    scenario.then().history().undoLabel("Duplicate Selection");

    scenario.when().undo();
    EXPECT_FALSE(entityById(scenario.api(), duplicateGroup).has_value());
    scenario.then().history().redoLabel("Duplicate Selection");

    scenario.when().redo();
    const EntityId redoneDuplicateGroup = selectedEntityExcluding(scenario.api(), sourceGroup);
    ASSERT_NE(redoneDuplicateGroup, InvalidEntityId);
    EXPECT_EQ(childrenOf(scenario.api(), redoneDuplicateGroup).size(), 2u);
}

TEST(GroupingPublicApiTests, DeleteMixedMeshAndLightHierarchyUndoRedoRestoresSceneComponents) {
    BehaviorScenario scenario = BehaviorScenario::newProject();
    givenBoxAt(scenario, "mesh", "Mixed Delete Mesh", {-1.0f, 0.0f, 0.0f});

    QuaderCreatePointLightArgs lightArgs;
    lightArgs.name = "Mixed Delete Light";
    lightArgs.transform.position = {2.0f, 4.0f, 0.0f};
    const QuaderApiResult<QuaderEntityRef> light = scenario.api().scene().createPointLight(lightArgs);
    ASSERT_TRUE(light.ok) << light.message;
    scenario.remember("light", light.value);

    scenario.given().selectedAliases({"mesh", "light"}, "light");
    scenario.when().groupSelection("group", "Mixed Delete Group");
    const EntityId group = scenario.entity("group");
    const EntityId mesh = scenario.entity("mesh");
    const EntityId lightEntity = scenario.entity("light");
    selectGroupForCommandDelete(scenario, "group");

    scenario.when().execute(QuaderCommand::DeleteSelection);
    scenario.then().command().okChanged();
    EXPECT_FALSE(entityById(scenario.api(), group).has_value());
    EXPECT_FALSE(entityById(scenario.api(), mesh).has_value());
    EXPECT_FALSE(entityById(scenario.api(), lightEntity).has_value());
    scenario.then().history().undoLabel("Delete Selection");

    scenario.when().undo();
    ASSERT_TRUE(entityById(scenario.api(), group).has_value());
    const std::optional<QuaderEntitySnapshot> restoredMesh = entityById(scenario.api(), mesh);
    const std::optional<QuaderEntitySnapshot> restoredLight = entityById(scenario.api(), lightEntity);
    ASSERT_TRUE(restoredMesh.has_value());
    ASSERT_TRUE(restoredLight.has_value());
    EXPECT_TRUE(restoredMesh->hasMesh);
    EXPECT_TRUE(restoredLight->hasLight);
    EXPECT_EQ(restoredLight->light.type, QuaderLightType::Point);
    EXPECT_EQ(restoredMesh->parent, group);
    EXPECT_EQ(restoredLight->parent, group);

    scenario.when().redo();
    EXPECT_FALSE(entityById(scenario.api(), group).has_value());
    EXPECT_FALSE(entityById(scenario.api(), mesh).has_value());
    EXPECT_FALSE(entityById(scenario.api(), lightEntity).has_value());
}
