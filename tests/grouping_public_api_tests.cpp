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

QuaderBounds unit_box_bounds() {
  return {.min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}};
}

QuaderTransform transform_at(QVec3 position) {
  QuaderTransform transform;
  transform.position = position;
  return transform;
}

void given_box_at(BehaviorScenario &scenario, std::string_view alias,
                  std::string name, QVec3 position) {
  scenario.given().box(alias, unit_box_bounds(), std::move(name));
  scenario.given().transform(alias, transform_at(position));
}

AssetId create_textured_material(BehaviorScenario &scenario, std::string name,
                                 QVec3 color) {
  QuaderTextureAsset base_color;
  base_color.name = name + " Base";
  base_color.usage = QuaderTextureUsage::BaseColor;
  base_color.width = 4;
  base_color.height = 4;
  base_color.generated = true;
  const QuaderApiResult<QuaderAssetRef> base_texture =
      scenario.api().assets().add_texture(std::move(base_color));
  EXPECT_TRUE(base_texture.ok()) << base_texture.error_message();

  QuaderTextureAsset roughness;
  roughness.name = name + " Roughness";
  roughness.usage = QuaderTextureUsage::Roughness;
  roughness.color_space = QuaderTextureColorSpace::Linear;
  roughness.width = 4;
  roughness.height = 4;
  roughness.generated = true;
  const QuaderApiResult<QuaderAssetRef> roughness_texture =
      scenario.api().assets().add_texture(std::move(roughness));
  EXPECT_TRUE(roughness_texture.ok()) << roughness_texture.error_message();

  QuaderMaterialAsset material;
  material.name = std::move(name);
  material.base_color = color;
  material.texture_slots.emplace("baseColorTexture", base_texture.value().id);
  material.texture_slots.emplace("roughnessTexture",
                                 roughness_texture.value().id);
  const QuaderApiResult<QuaderAssetRef> created =
      scenario.api().assets().add_material(std::move(material));
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value().id;
}

AssetId create_material(BehaviorScenario &scenario, std::string name) {
  const QuaderApiResult<QuaderAssetRef> material =
      scenario.api().assets().add_material(
          QuaderMaterialArgs{.name = std::move(name)});
  EXPECT_TRUE(material.ok()) << material.error_message();
  return material.value().id;
}

void assign_material(BehaviorScenario &scenario, std::string_view alias,
                     AssetId material) {
  const QuaderApiVoidResult assigned =
      scenario.api().assets().assign_material_to_object(scenario.object(alias),
                                                        material);
  ASSERT_TRUE(assigned.ok()) << assigned.error_message();
}

void select_group_for_command_delete(BehaviorScenario &scenario,
                                     std::string_view group_alias) {
  scenario.given().selected(group_alias);
  scenario.given().selection_mode(QuaderSelectionMode::Group);
}

void remember_restored_subtree(BehaviorScenario &scenario,
                               std::string_view group_alias,
                               std::string_view group_name) {
  scenario.remember_entity_named(group_alias, group_name);
}

std::optional<QuaderEntitySnapshot> entity_by_id(const QuaderApi &api,
                                                 EntityId entity) {
  return api.query().entity(entity);
}

std::vector<QuaderEntitySnapshot> children_of(const QuaderApi &api,
                                              EntityId parent) {
  std::vector<QuaderEntitySnapshot> children;
  for (const QuaderEntitySnapshot &snapshot : api.query().entities()) {
    if (snapshot.parent == parent) {
      children.push_back(snapshot);
    }
  }
  return children;
}

std::vector<EntityId> selected_entities(const QuaderApi &api) {
  std::vector<EntityId> selected;
  for (const QuaderEntitySnapshot &snapshot : api.query().entities()) {
    if (snapshot.selected) {
      selected.push_back(snapshot.id);
    }
  }
  return selected;
}

EntityId selected_entity_excluding(const QuaderApi &api, EntityId excluded) {
  for (const EntityId entity : selected_entities(api)) {
    if (entity != excluded) {
      return entity;
    }
  }
  return kInvalidEntityId;
}

} // namespace

TEST(GroupingPublicApiTests, BehaviorWorldPositionExpectationUsesSceneWorldTransform) {
  BehaviorScenario scenario = BehaviorScenario::new_project();

  QuaderTransform parent_transform;
  parent_transform.position = {10.0f, -1.0f, 0.0f};
  parent_transform.rotation_euler = {0.0f, 0.0f, kHalfPi};
  parent_transform.scale = {2.0f, 3.0f, 1.0f};

  QuaderTransform child_transform;
  child_transform.position = {1.0f, 0.0f, 0.0f};

  scenario.given().entity("parent", "Rotated Parent", QuaderEntityKind::Empty,
                          parent_transform);
  scenario.given().entity("child", "World Child", QuaderEntityKind::Empty,
                          child_transform, "parent");

  scenario.then().scene().world_position_near("child", {10.0f, 1.0f, 0.0f});
}

TEST(GroupingPublicApiTests, GroupSelectionParentsSelectedRootsAndUndoRedoRestoresHierarchy) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "left", "Left", {-3.0f, 0.0f, 0.0f});
  given_box_at(scenario, "right", "Right", {4.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"left", "right"}, "right");

  QuaderTransform group_transform;
  group_transform.position = {10.0f, 0.0f, 0.0f};
  scenario.when().group_selection("group", "Pair", group_transform);

  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .has_alias("group")
      .alias_kind("group", QuaderEntityKind::Empty)
      .parent("left", "group")
      .parent("right", "group")
      .selected_aliases({"group"})
      .active_alias("group")
      .world_position_near("left", {-3.0f, 0.0f, 0.0f})
      .world_position_near("right", {4.0f, 0.0f, 0.0f});

  scenario.when().undo();
  scenario.then()
      .scene()
      .missing_alias("group")
      .no_parent("left")
      .no_parent("right")
      .selected_aliases({"left", "right"});

  scenario.when().redo();
  scenario.remember_entity_named("group", "Pair");
  scenario.then()
      .scene()
      .parent("left", "group")
      .parent("right", "group")
      .selected_aliases({"group"})
      .active_alias("group");
}

TEST(GroupingPublicApiTests, GroupSelectionUsesSelectedRootsOnlyWhenParentAndChildAreSelected) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "child", "Child", {1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "sibling", "Sibling", {3.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"child", "sibling"}, "sibling");
  scenario.when().group_selection("existingGroup", "Existing Group");

  scenario.given().selected_aliases({"existingGroup", "child"},
                                    "existingGroup");
  scenario.when().group_selection("outer", "Outer Group");

  scenario.then()
      .scene()
      .parent("existingGroup", "outer")
      .parent("child", "existingGroup")
      .parent("sibling", "existingGroup")
      .child_count("outer", 1u);
}

TEST(GroupingPublicApiTests, UngroupUndoRedoRestoresGroupSelectionAndChildPlacement) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "First", {-2.0f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Second", {2.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"first", "second"}, "first");
  QuaderTransform group_transform;
  group_transform.position = {5.0f, 0.0f, 0.0f};
  scenario.when().group_selection("group", "Ungroup Me", group_transform);

  scenario.when().ungroup_selection();
  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .missing_alias("group")
      .no_parent("first")
      .no_parent("second")
      .world_position_near("first", {-2.0f, 0.0f, 0.0f})
      .world_position_near("second", {2.0f, 0.0f, 0.0f});

  scenario.when().undo();
  remember_restored_subtree(scenario, "group", "Ungroup Me");
  scenario.then()
      .scene()
      .active_alias("group")
      .alias_kind("group", QuaderEntityKind::Empty)
      .parent("first", "group")
      .parent("second", "group")
      .world_position_near("first", {-2.0f, 0.0f, 0.0f})
      .world_position_near("second", {2.0f, 0.0f, 0.0f});

  scenario.when().redo();
  scenario.then()
      .scene()
      .named_missing("Ungroup Me")
      .no_parent("first")
      .no_parent("second");
}

TEST(GroupingPublicApiTests, SceneDestroyGroupUndoRedoRestoresMaterialsTexturesAndTransforms) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "red", "Red", {-4.0f, 1.0f, 0.0f});
  given_box_at(scenario, "blue", "Blue", {6.0f, -2.0f, 0.0f});
  const AssetId red_material =
      create_textured_material(scenario, "Red Material", {1.0f, 0.0f, 0.0f});
  const AssetId blue_material =
      create_textured_material(scenario, "Blue Material", {0.0f, 0.0f, 1.0f});
  assign_material(scenario, "red", red_material);
  assign_material(scenario, "blue", blue_material);

  scenario.given().selected_aliases({"red", "blue"}, "red");
  QuaderTransform group_transform;
  group_transform.position = {12.0f, 0.0f, 0.0f};
  scenario.when().group_selection("group", "Textured Group", group_transform);

  scenario.when().destroy("group");
  scenario.then()
      .scene()
      .missing_alias("group")
      .missing_alias("red")
      .missing_alias("blue");

  scenario.when().undo();
  scenario.remember_entity_named("group", "Textured Group");
  scenario.remember_entity_named("red", "Red");
  scenario.remember_entity_named("blue", "Blue");
  scenario.then()
      .scene()
      .active_alias("group")
      .parent("red", "group")
      .parent("blue", "group")
      .world_position_near("red", {-4.0f, 1.0f, 0.0f})
      .world_position_near("blue", {6.0f, -2.0f, 0.0f})
      .has_material("red", red_material)
      .has_material("blue", blue_material);
  scenario.then().assets().texture_count(4u);

  scenario.when().redo();
  scenario.then()
      .scene()
      .named_missing("Textured Group")
      .named_missing("Red")
      .named_missing("Blue");
}

TEST(GroupingPublicApiTests, SceneDestroyNestedGroupUndoRedoRestoresNestedHierarchy) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "left", "Nested Left", {-2.0f, 0.0f, 0.0f});
  given_box_at(scenario, "right", "Nested Right", {2.0f, 0.0f, 0.0f});
  given_box_at(scenario, "top", "Nested Top", {0.0f, 3.0f, 0.0f});

  scenario.given().selected_aliases({"left", "right"}, "left");
  scenario.when().group_selection("inner", "Inner Group");
  scenario.given().selected_aliases({"inner", "top"}, "inner");
  scenario.when().group_selection("outer", "Outer Group");

  scenario.when().destroy("outer");
  scenario.when().undo();
  scenario.remember_entity_named("outer", "Outer Group");
  scenario.remember_entity_named("inner", "Inner Group");
  scenario.remember_entity_named("top", "Nested Top");
  scenario.remember_entity_named("left", "Nested Left");
  scenario.remember_entity_named("right", "Nested Right");
  scenario.then()
      .scene()
      .parent("inner", "outer")
      .parent("top", "outer")
      .parent("left", "inner")
      .parent("right", "inner");

  scenario.when().redo();
  scenario.then()
      .scene()
      .named_missing("Outer Group")
      .named_missing("Inner Group")
      .named_missing("Nested Left")
      .named_missing("Nested Right")
      .named_missing("Nested Top");
}

TEST(GroupingPublicApiTests, SceneDestroyGroupedChildUndoKeepsChildInsideParentGroup) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  scenario.given().entity("child", "Child Entity", QuaderEntityKind::Empty,
                          transform_at({-1.0f, 0.0f, 0.0f}));
  scenario.given().entity("sibling", "Sibling Entity", QuaderEntityKind::Empty,
                          transform_at({1.0f, 0.0f, 0.0f}));
  scenario.given().selected_aliases({"child", "sibling"}, "child");
  scenario.when().group_selection("group", "Parent Group");

  scenario.when().destroy("child");
  scenario.then().scene().missing_alias("child");

  scenario.when().undo();
  scenario.remember_entity_named("child", "Child Entity");
  scenario.then()
      .scene()
      .parent("child", "group")
      .world_position_near("child", {-1.0f, 0.0f, 0.0f});
}

TEST(GroupingPublicApiTests, ModelingDeleteCommandDeletesSelectedGroupInsteadOfStaleModelingObject) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "groupedA", "Grouped A", {-1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "groupedB", "Grouped B", {1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "decoy", "Do Not Delete", {8.0f, 0.0f, 0.0f});
  scenario.given().selected("decoy");

  scenario.given().selected_aliases({"groupedA", "groupedB"}, "groupedA");
  scenario.when().group_selection("group", "Delete Me");
  select_group_for_command_delete(scenario, "group");

  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .named_missing("Delete Me")
      .named_missing("Grouped A")
      .named_missing("Grouped B")
      .named_exists("Do Not Delete");
}

TEST(GroupingPublicApiTests, ViewportDeleteShortcutDeletesSelectedGroup) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "left", "Shortcut Left", {-1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "right", "Shortcut Right", {1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "decoy", "Shortcut Decoy", {9.0f, 0.0f, 0.0f});
  scenario.given().selected("decoy");

  scenario.given().selected_aliases({"left", "right"}, "left");
  scenario.when().group_selection("group", "Shortcut Group");
  select_group_for_command_delete(scenario, "group");

  scenario.when().dispatch_viewport_shortcut({
      .kind = ViewportShortcutKind::DeleteSelection,
      .context = ViewportShortcutContext::Global,
  });
  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .named_missing("Shortcut Group")
      .named_missing("Shortcut Left")
      .named_missing("Shortcut Right")
      .named_exists("Shortcut Decoy");

  scenario.when().execute(quader_command::kHistoryUndo);
  scenario.remember_entity_named("group", "Shortcut Group");
  scenario.remember_entity_named("left", "Shortcut Left");
  scenario.remember_entity_named("right", "Shortcut Right");
  scenario.then()
      .scene()
      .active_alias("group")
      .parent("left", "group")
      .parent("right", "group");

  scenario.when().execute(quader_command::kHistoryRedo);
  scenario.then()
      .scene()
      .named_missing("Shortcut Group")
      .named_exists("Shortcut Decoy");
}

TEST(GroupingPublicApiTests, ContextDeleteActionDeletesSelectedGroup) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "Action First", {-2.0f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Action Second", {2.0f, 0.0f, 0.0f});
  const AssetId material =
      create_textured_material(scenario, "Action Material", {0.1f, 0.5f, 0.9f});
  assign_material(scenario, "first", material);
  assign_material(scenario, "second", material);
  scenario.given().selected_aliases({"first", "second"}, "first");
  scenario.when().group_selection("group", "Action Group");
  select_group_for_command_delete(scenario, "group");

  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .named_missing("Action Group")
      .named_missing("Action First")
      .named_missing("Action Second");

  scenario.when().execute(quader_command::kHistoryUndo);
  scenario.remember_entity_named("group", "Action Group");
  scenario.remember_entity_named("first", "Action First");
  scenario.remember_entity_named("second", "Action Second");
  scenario.then()
      .scene()
      .active_alias("group")
      .parent("first", "group")
      .parent("second", "group")
      .has_material("first", material)
      .has_material("second", material);
}

TEST(GroupingPublicApiTests, ObjectModeSceneSelectedGroupDeleteUsesSceneRoots) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "child", "Object Mode Child", {-1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "sibling", "Object Mode Sibling", {1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "decoy", "Object Mode Decoy", {8.0f, 0.0f, 0.0f});
  scenario.given().selected("decoy");

  scenario.given().selected_aliases({"child", "sibling"}, "child");
  scenario.when().group_selection("group", "Object Mode Group");
  scenario.given()
      .selection_mode(QuaderSelectionMode::Object)
      .selected("group");
  scenario.then().selection().mode(QuaderSelectionMode::Object);

  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .named_missing("Object Mode Group")
      .named_missing("Object Mode Child")
      .named_missing("Object Mode Sibling")
      .named_exists("Object Mode Decoy");
}

TEST(GroupingPublicApiTests, ComponentModeSceneSelectedGroupForcesEntityModeAndDeleteUsesSceneRoots) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "child", "Component Mode Child", {-1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "sibling", "Component Mode Sibling",
               {1.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"child", "sibling"}, "child");
  scenario.when().group_selection("group", "Component Mode Group");

  scenario.given().selection_mode(QuaderSelectionMode::Face).selected("group");
  scenario.then().selection().mode(QuaderSelectionMode::Object);

  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .named_missing("Component Mode Group")
      .named_missing("Component Mode Child")
      .named_missing("Component Mode Sibling");

  scenario.when().execute(quader_command::kHistoryUndo);
  scenario.remember_entity_named("group", "Component Mode Group");
  scenario.remember_entity_named("child", "Component Mode Child");
  scenario.remember_entity_named("sibling", "Component Mode Sibling");
  scenario.then()
      .scene()
      .active_alias("group")
      .parent("child", "group")
      .parent("sibling", "group");
}

TEST(GroupingPublicApiTests, EmptySceneSelectionWithStaleModelingObjectDoesNotDeleteFallbackObject) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "decoy", "Stale Delete Decoy", {4.0f, 0.0f, 0.0f});
  scenario.given().selected("decoy");
  scenario.then().selection().mode(QuaderSelectionMode::Object);

  scenario.when().clear_scene_selection();
  scenario.then().scene().selected_aliases({});

  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.then().command().failed();
  scenario.then().scene().named_exists("Stale Delete Decoy");
}

TEST(GroupingPublicApiTests, ModelingDeleteCommandUndoRedoRestoresSelectedGroupSubtree) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "Command First", {-3.0f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Command Second", {3.0f, 0.0f, 0.0f});
  const AssetId material = create_textured_material(
      scenario, "Command Material", {0.7f, 0.7f, 0.1f});
  assign_material(scenario, "first", material);
  assign_material(scenario, "second", material);
  scenario.given().selected_aliases({"first", "second"}, "first");
  scenario.when().group_selection("group", "Command Group");
  select_group_for_command_delete(scenario, "group");

  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.then().scene().named_missing("Command Group");

  scenario.when().execute(quader_command::kHistoryUndo);
  scenario.remember_entity_named("group", "Command Group");
  scenario.remember_entity_named("first", "Command First");
  scenario.remember_entity_named("second", "Command Second");
  scenario.then()
      .scene()
      .active_alias("group")
      .parent("first", "group")
      .parent("second", "group")
      .world_position_near("first", {-3.0f, 0.0f, 0.0f})
      .world_position_near("second", {3.0f, 0.0f, 0.0f})
      .has_material("first", material)
      .has_material("second", material);

  scenario.when().execute(quader_command::kHistoryRedo);
  scenario.then()
      .scene()
      .named_missing("Command Group")
      .named_missing("Command First")
      .named_missing("Command Second");
}

TEST(GroupingPublicApiTests, ModelingDeleteGroupedChildUndoKeepsChildGroupedAndTextured) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "target", "Delete Child", {-2.0f, 0.0f, 0.0f});
  given_box_at(scenario, "sibling", "Keep Sibling", {2.0f, 0.0f, 0.0f});
  const AssetId material = create_textured_material(
      scenario, "Delete Child Material", {0.9f, 0.2f, 0.2f});
  assign_material(scenario, "target", material);
  scenario.given().selected_aliases({"target", "sibling"}, "target");
  scenario.when().group_selection("group", "Child Delete Group");

  scenario.given()
      .selection_mode(QuaderSelectionMode::Object)
      .selected("target");
  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.then()
      .scene()
      .named_missing("Delete Child")
      .named_exists("Child Delete Group");

  scenario.when().execute(quader_command::kHistoryUndo);
  scenario.remember_entity_named("target", "Delete Child");
  scenario.then()
      .scene()
      .parent("target", "group")
      .has_material("target", material)
      .world_position_near("target", {-2.0f, 0.0f, 0.0f});
}

TEST(GroupingPublicApiTests, RestoredGroupCanBeDeletedAgainWithModelingCommand) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "Again First", {-1.5f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Again Second", {1.5f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"first", "second"}, "second");
  scenario.when().group_selection("group", "Again Group");
  select_group_for_command_delete(scenario, "group");

  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.when().execute(quader_command::kHistoryUndo);
  scenario.remember_entity_named("group", "Again Group");

  select_group_for_command_delete(scenario, "group");
  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.then()
      .scene()
      .named_missing("Again Group")
      .named_missing("Again First")
      .named_missing("Again Second");
}

TEST(GroupingPublicApiTests, SceneAwareDeleteRedoClearedByDocumentEditDoesNotReplayRuntimeRedo) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "Redo Guard First", {-1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Redo Guard Second", {1.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"first", "second"}, "first");
  scenario.when().group_selection("group", "Redo Guard Group");
  select_group_for_command_delete(scenario, "group");

  scenario.when().execute(quader_command::kModelingDeleteSelection);
  scenario.when().execute(quader_command::kHistoryUndo);
  scenario.remember_entity_named("group", "Redo Guard Group");
  scenario.then()
      .scene()
      .named_exists("Redo Guard First")
      .named_exists("Redo Guard Second");
  scenario.then().history().can_redo();

  scenario.when().rename("group", "Redo Guard Edited");
  scenario.then().history().can_redo(false);

  scenario.when().execute(quader_command::kHistoryRedo);
  scenario.then().command().failed();
  scenario.then()
      .scene()
      .named_exists("Redo Guard Edited")
      .named_exists("Redo Guard First")
      .named_exists("Redo Guard Second");
}

TEST(GroupingPublicApiTests, GroupVisibilityLockAndTransformBatchUndoRedoSurvivesChildren) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "Batch First", {-1.0f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Batch Second", {1.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"first", "second"}, "first");
  scenario.when().group_selection("group", "Batch Group");

  QuaderTransform moved;
  moved.position = {2.0f, 3.0f, 0.0f};
  scenario.when().set_group_flags("group", moved, false, true);
  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .visible("group", false)
      .locked("group", true)
      .entity_transform_near("group", moved)
      .parent("first", "group")
      .parent("second", "group");

  scenario.when().undo();
  scenario.then()
      .scene()
      .visible("group", true)
      .locked("group", false)
      .entity_transform_near("group", QuaderTransform{})
      .parent("first", "group")
      .parent("second", "group");

  scenario.when().redo();
  scenario.then()
      .scene()
      .visible("group", false)
      .locked("group", true)
      .entity_transform_near("group", moved);
}

TEST(GroupingPublicApiTests, DuplicateSelectedHierarchyCopiesStateSelectsCloneAndUndoRedoAsOneOperation) {
  BehaviorScenario scenario = BehaviorScenario::new_project();

  QuaderTransform parent_transform;
  scenario.given().entity("parent", "Duplicate Parent", QuaderEntityKind::Empty,
                          parent_transform);

  QuaderTransform group_create_transform;
  group_create_transform.position = {1.0f, 2.0f, 3.0f};
  group_create_transform.rotation_euler = {0.1f, 0.2f, 0.3f};
  scenario.given().entity("group", "Duplicate Source", QuaderEntityKind::Empty,
                          group_create_transform, "parent");

  given_box_at(scenario, "child", "Duplicate Child", {4.0f, 5.0f, 6.0f});
  scenario.when().reparent("child", "group", false);
  const AssetId child_mesh = scenario.object("child").mesh;

  const AssetId material = create_material(scenario, "Duplicate Material");
  assign_material(scenario, "child", material);
  scenario.when().set_visible("group", false);
  scenario.when().set_visible("child", false);

  scenario.given().selected_aliases({"group", "child"}, "child");
  scenario.when().duplicate_selected_hierarchy("group.copy");
  scenario.then().command().ok_changed();
  scenario.remember_entity_named("child.copy", "Duplicate Child_1");

  QuaderTransform group_expected;
  group_expected.position = group_create_transform.position;
  group_expected.rotation_euler = group_create_transform.rotation_euler;
  QuaderTransform child_expected;
  child_expected.position = {4.0f, 5.0f, 6.0f};

  scenario.then()
      .scene()
      .alias_name("group.copy", "Duplicate Source_1")
      .parent("group.copy", "parent")
      .visible("group.copy", false)
      .locked("group.copy", false)
      .entity_transform_near("group.copy", group_expected)
      .child_count("group.copy", 1u)
      .alias_name("child.copy", "Duplicate Child_1")
      .parent("child.copy", "group.copy")
      .visible("child.copy", false)
      .locked("child.copy", false)
      .has_mesh("child.copy", child_mesh)
      .has_material("child.copy", material)
      .entity_transform_near("child.copy", child_expected)
      .selected_aliases({"group.copy"})
      .active_alias("group.copy");
  scenario.then().history().undo_label("Duplicate Hierarchy");

  scenario.when().undo();
  scenario.then()
      .scene()
      .missing_alias("group.copy")
      .missing_alias("child.copy")
      .selected_aliases({"group", "child"})
      .active_alias("child");
  scenario.then().history().redo_label("Duplicate Hierarchy");

  scenario.when().redo();
  scenario.remember_entity_named("group.copy", "Duplicate Source_1");
  scenario.remember_entity_named("child.copy", "Duplicate Child_1");
  scenario.then()
      .scene()
      .has_alias("group.copy")
      .child_count("group.copy", 1u)
      .selected_aliases({"group.copy"});
}

TEST(GroupingPublicApiTests, DuplicateMixedMeshAndLightHierarchyCopiesStateAndUndoRedoAsOneOperation) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "mesh", "Mixed Duplicate Mesh", {-2.0f, 0.0f, 0.0f});

  QuaderCreatePointLightArgs light_args;
  light_args.name = "Mixed Duplicate Light";
  light_args.transform.position = {3.0f, 1.0f, 0.0f};
  const QuaderApiResult<QuaderEntityRef> light =
      scenario.api().scene().create_point_light(light_args);
  ASSERT_TRUE(light.ok) << light.message;
  scenario.remember("light", light.value);

  scenario.given().selected_aliases({"mesh", "light"}, "light");
  scenario.when().group_selection("group", "Mixed Duplicate Group");
  const EntityId source_group = scenario.entity("group");

  scenario.when().execute(quader_command::kDuplicateSelection);
  scenario.then().command().ok_changed();
  const EntityId duplicate_group =
      selected_entity_excluding(scenario.api(), source_group);
  ASSERT_NE(duplicate_group, kInvalidEntityId);
  scenario.remember("group.copy", duplicate_group);

  const std::vector<QuaderEntitySnapshot> children =
      children_of(scenario.api(), duplicate_group);
  ASSERT_EQ(children.size(), 2u);
  EXPECT_TRUE(std::any_of(
      children.begin(), children.end(),
      [](const QuaderEntitySnapshot &child) { return child.has_mesh; }));
  EXPECT_TRUE(std::any_of(
      children.begin(), children.end(), [](const QuaderEntitySnapshot &child) {
        return child.has_light && child.light.type == QuaderLightType::Point;
      }));
  scenario.then().history().undo_label("Duplicate Selection");

  scenario.when().undo();
  EXPECT_FALSE(entity_by_id(scenario.api(), duplicate_group).has_value());
  scenario.then().history().redo_label("Duplicate Selection");

  scenario.when().redo();
  const EntityId redone_duplicate_group =
      selected_entity_excluding(scenario.api(), source_group);
  ASSERT_NE(redone_duplicate_group, kInvalidEntityId);
  EXPECT_EQ(children_of(scenario.api(), redone_duplicate_group).size(), 2u);
}

TEST(GroupingPublicApiTests, DeleteMixedMeshAndLightHierarchyUndoRedoRestoresSceneComponents) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "mesh", "Mixed Delete Mesh", {-1.0f, 0.0f, 0.0f});

  QuaderCreatePointLightArgs light_args;
  light_args.name = "Mixed Delete Light";
  light_args.transform.position = {2.0f, 4.0f, 0.0f};
  const QuaderApiResult<QuaderEntityRef> light =
      scenario.api().scene().create_point_light(light_args);
  ASSERT_TRUE(light.ok) << light.message;
  scenario.remember("light", light.value);

  scenario.given().selected_aliases({"mesh", "light"}, "light");
  scenario.when().group_selection("group", "Mixed Delete Group");
  const EntityId group = scenario.entity("group");
  const EntityId mesh = scenario.entity("mesh");
  const EntityId light_entity = scenario.entity("light");
  select_group_for_command_delete(scenario, "group");

  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.then().command().ok_changed();
  EXPECT_FALSE(entity_by_id(scenario.api(), group).has_value());
  EXPECT_FALSE(entity_by_id(scenario.api(), mesh).has_value());
  EXPECT_FALSE(entity_by_id(scenario.api(), light_entity).has_value());
  scenario.then().history().undo_label("Delete Selection");

  scenario.when().undo();
  ASSERT_TRUE(entity_by_id(scenario.api(), group).has_value());
  const std::optional<QuaderEntitySnapshot> restored_mesh =
      entity_by_id(scenario.api(), mesh);
  const std::optional<QuaderEntitySnapshot> restored_light =
      entity_by_id(scenario.api(), light_entity);
  ASSERT_TRUE(restored_mesh.has_value());
  ASSERT_TRUE(restored_light.has_value());
  EXPECT_TRUE(restored_mesh->has_mesh);
  EXPECT_TRUE(restored_light->has_light);
  EXPECT_EQ(restored_light->light.type, QuaderLightType::Point);
  EXPECT_EQ(restored_mesh->parent, group);
  EXPECT_EQ(restored_light->parent, group);

  scenario.when().redo();
  EXPECT_FALSE(entity_by_id(scenario.api(), group).has_value());
  EXPECT_FALSE(entity_by_id(scenario.api(), mesh).has_value());
  EXPECT_FALSE(entity_by_id(scenario.api(), light_entity).has_value());
}
