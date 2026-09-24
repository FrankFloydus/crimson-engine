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
#include <limits>
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

std::vector<std::string> root_entity_names(const QuaderApi &api) {
  const std::vector<QuaderEntitySnapshot> entities = api.query().entity_tree();
  std::vector<std::string> names;
  for (const QuaderEntitySnapshot &snapshot : entities) {
    if (snapshot.parent == kInvalidEntityId) {
      names.push_back(snapshot.name);
    }
  }
  return names;
}

std::vector<std::string> child_entity_names(const QuaderApi &api,
                                           EntityId parent) {
  std::vector<std::string> names;
  for (const QuaderEntitySnapshot &snapshot : api.query().entity_tree()) {
    if (snapshot.parent == parent) {
      names.push_back(snapshot.name);
    }
  }
  return names;
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

TEST(GroupingPublicApiTests,
     MoveEntitiesReordersSingleAndMultipleRootsWithExactUndoAndNoOpHistory) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  for (const std::string name : {"A", "B", "C", "D", "E"}) {
    scenario.given().entity(name, name, QuaderEntityKind::Empty);
  }
  scenario.given().selected_aliases({"B", "D"}, "B");
  const std::vector<EntityId> moved{scenario.entity("D"), scenario.entity("B"),
                                  scenario.entity("B")};
  const QuaderApiVoidResult result = scenario.api().scene().move_entities(
      moved, kInvalidEntityId, scenario.entity("E"));
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.changed());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "C", "B", "D", "E"}));
  scenario.then().scene().selected_aliases({"B", "D"}).active_alias("B");
  scenario.then().history().undo_label("Move Entities");

  ASSERT_TRUE(scenario.api().history().undo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "B", "C", "D", "E"}));
  ASSERT_TRUE(scenario.api().history().redo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "C", "B", "D", "E"}));

  const std::vector<EntityId> single{scenario.entity("E")};
  ASSERT_TRUE(scenario.api().scene().move_entities(
      single, kInvalidEntityId, scenario.entity("A")).ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"E", "A", "C", "B", "D"}));
  ASSERT_TRUE(scenario.api().history().undo().ok());
  const QuaderRevisionStamp before = scenario.api().query().revisions();
  const QuaderApiVoidResult unchanged = scenario.api().scene().move_entities(
      moved, kInvalidEntityId, scenario.entity("B"));
  EXPECT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(scenario.api().query().revisions().scene_revision,
            before.scene_revision);
  EXPECT_TRUE(scenario.api().history().can_redo());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "C", "B", "D", "E"}));
}

TEST(GroupingPublicApiTests,
     MoveEntitiesReparentsSelectedRootsIntoAndOutOfGroupsPreservingWorldState) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  scenario.given().entity("source", "Source", QuaderEntityKind::Empty,
                          transform_at({2.0f, 3.0f, 0.0f}));
  scenario.given().entity("child", "Child", QuaderEntityKind::Empty,
                          transform_at({1.0f, 0.0f, 0.0f}), "source");
  QuaderTransform target = transform_at({10.0f, -3.0f, 0.0f});
  target.rotation_euler.z = kHalfPi;
  target.scale = {2.0f, 2.0f, 2.0f};
  scenario.given().entity("target", "Target", QuaderEntityKind::Empty, target);
  given_box_at(scenario, "mesh", "Mesh", {-2.0f, 1.0f, 0.0f});
  const QuaderModelingObjectRef mesh = scenario.object("mesh");
  scenario.given().selected_aliases({"source", "child", "mesh"}, "child");
  const std::vector<EntityId> moved{scenario.entity("mesh"),
                                  scenario.entity("child"),
                                  scenario.entity("source")};
  const QuaderApiVoidResult result = scenario.api().scene().move_entities(
      moved, scenario.entity("target"));
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.changed());
  scenario.then().scene()
      .parent("source", "target").parent("mesh", "target")
      .parent("child", "source")
      .world_position_near("source", {2.0f, 3.0f, 0.0f})
      .world_position_near("child", {3.0f, 3.0f, 0.0f})
      .world_position_near("mesh", {-2.0f, 1.0f, 0.0f})
      .selected_aliases({"source", "child", "mesh"}).active_alias("child");
  EXPECT_EQ(scenario.api().query().entity(mesh.entity)->modeling_object_id,
            mesh.modeling_object_id);
  EXPECT_EQ(scenario.api().query().entity(mesh.entity)->mesh, mesh.mesh);

  ASSERT_TRUE(scenario.api().history().undo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"Source", "Target", "Mesh"}));
  scenario.then().scene().parent("child", "source")
      .world_position_near("child", {3.0f, 3.0f, 0.0f})
      .selected_aliases({"source", "child", "mesh"}).active_alias("child");
  ASSERT_TRUE(scenario.api().history().redo().ok());
  ASSERT_TRUE(scenario.api().scene().move_entities(
      moved, kInvalidEntityId, scenario.entity("target")).ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"Source", "Mesh", "Target"}));
  scenario.then().scene().parent("child", "source")
      .world_position_near("child", {3.0f, 3.0f, 0.0f})
      .world_position_near("mesh", {-2.0f, 1.0f, 0.0f});
  ASSERT_TRUE(scenario.api().history().undo().ok());
  scenario.then().scene().parent("source", "target").parent("mesh", "target");
}

TEST(GroupingPublicApiTests,
     MoveEntitiesRejectsInvalidLockedAndSingularDestinationsWithoutMutation) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  scenario.given().entity("group", "Group", QuaderEntityKind::Empty);
  scenario.given().entity("child", "Child", QuaderEntityKind::Empty,
                          QuaderTransform{}, "group");
  scenario.given().entity("other", "Other", QuaderEntityKind::Empty);
  QuaderTransform singular;
  singular.scale = {0.0f, 1.0f, 1.0f};
  scenario.given().entity("singular", "Singular", QuaderEntityKind::Empty,
                          singular);
  scenario.given().entity("locked", "Locked", QuaderEntityKind::Empty);
  ASSERT_TRUE(scenario.api().scene().set_locked(scenario.entity("locked"), true).ok());
  const EntityId root = scenario.api().query().entity(scenario.entity("group"))->parent;
  const QuaderRevisionStamp before = scenario.api().query().revisions();
  const QuaderHistorySnapshot history = scenario.api().history().snapshot();
  const auto expect_rejected = [&](std::vector<EntityId> moved, EntityId parent,
                                   EntityId anchor = kInvalidEntityId) {
    const QuaderApiVoidResult result =
        scenario.api().scene().move_entities(moved, parent, anchor);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(scenario.api().query().revisions().scene_revision,
              before.scene_revision);
    EXPECT_EQ(scenario.api().history().snapshot().undo_label, history.undo_label);
    EXPECT_EQ(root_entity_names(scenario.api()),
              (std::vector<std::string>{"Group", "Other", "Singular", "Locked"}));
    EXPECT_EQ(scenario.api().query().entity(scenario.entity("child"))->parent,
              scenario.entity("group"));
  };
  expect_rejected({scenario.entity("group")}, scenario.entity("child"));
  expect_rejected({root}, scenario.entity("group"));
  expect_rejected({scenario.entity("other"), std::numeric_limits<EntityId>::max()}, root);
  expect_rejected({scenario.entity("other")}, std::numeric_limits<EntityId>::max());
  expect_rejected({scenario.entity("other")}, root, scenario.entity("child"));
  expect_rejected({scenario.entity("other")}, scenario.entity("locked"));
  expect_rejected({scenario.entity("other"), scenario.entity("locked")}, scenario.entity("group"));
  expect_rejected({scenario.entity("other"), scenario.entity("child")}, scenario.entity("singular"));
}

TEST(GroupingPublicApiTests,
     MoveEntitiesRestoresLayerMembershipAfterUndoingInstanceMemberInsertion) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "original", "Original Member", {});
  scenario.given().selected("original");
  const auto instance = scenario.api().modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId definition = instance.value().definition.root.id;
  given_box_at(scenario, "added", "Added Member", {3.0f, 1.0f, 0.0f});
  scenario.given().selected("added");
  const auto layer = scenario.editor().layers().create_from_selection("Display");
  ASSERT_TRUE(layer.ok()) << layer.error_message();
  const std::vector<EntityId> added{scenario.entity("added")};
  const QuaderApiVoidResult result =
      scenario.api().scene().move_entities(added, definition);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_EQ(scenario.api().query().entity(added.front())->parent, definition);
  EXPECT_EQ(scenario.api().query().entity(added.front())->layer_id,
            kInvalidQuaderLayerId);
  ASSERT_TRUE(scenario.api().history().undo().ok());
  EXPECT_EQ(scenario.api().query().entity(added.front())->layer_id, layer.value().id);
  scenario.then().scene().no_parent("added")
      .world_position_near("added", {3.0f, 1.0f, 0.0f});
  ASSERT_TRUE(scenario.api().history().redo().ok());
  EXPECT_EQ(scenario.api().query().entity(added.front())->parent, definition);
  EXPECT_EQ(scenario.api().query().entity(added.front())->layer_id,
            kInvalidQuaderLayerId);
}

TEST(GroupingPublicApiTests,
     MoveEntitiesCompensatesPartialInstanceMemberExtractionExactly) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "First Member", {-2.0f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Second Member", {2.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"first", "second"}, "first");
  const auto instance = scenario.api().modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId definition = instance.value().definition.root.id;
  ASSERT_EQ(instance.value().definition.members.size(), 2u);
  const std::vector<EntityId> members{scenario.entity("first"), scenario.entity("second")};
  const QuaderRevisionStamp before = scenario.api().query().revisions();
  const QuaderHistorySnapshot history = scenario.api().history().snapshot();
  const auto names = root_entity_names(scenario.api());
  const QuaderApiVoidResult result =
      scenario.api().scene().move_entities(members, kInvalidEntityId);
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(scenario.api().query().revisions().scene_revision, before.scene_revision);
  EXPECT_EQ(scenario.api().query().revisions().scene_layer_revision,
            before.scene_layer_revision);
  EXPECT_EQ(scenario.api().history().snapshot().undo_label, history.undo_label);
  EXPECT_EQ(root_entity_names(scenario.api()), names);
  for (const EntityId member : members) {
    EXPECT_EQ(scenario.api().query().entity(member)->parent, definition);
    EXPECT_TRUE(scenario.api().query().entity(member)->has_modeling_object);
  }
  EXPECT_EQ(scenario.api().query().entity(instance.value().occurrence.root.id)
                ->instance_definition, definition);
}

TEST(GroupingPublicApiTests,
     MoveEntitiesRejectsNestedLiveInstanceInsertionWhoseUndoWouldBeInvalid) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "member", "Nested Member", {});
  scenario.given().selected("member");
  scenario.when().group_selection("nested", "Nested Group");
  const auto instance = scenario.api().modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  given_box_at(scenario, "external", "External Member", {4.0f, 0.0f, 0.0f});
  const std::vector<EntityId> external{scenario.entity("external")};
  const QuaderRevisionStamp before = scenario.api().query().revisions();
  const auto result = scenario.api().scene().move_entities(
      external, scenario.entity("nested"));
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(scenario.api().query().revisions().scene_revision, before.scene_revision);
  scenario.then().scene().no_parent("external")
      .world_position_near("external", {4.0f, 0.0f, 0.0f});
}

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
  QuaderSelectionSummary summary =
      scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Object);
  EXPECT_TRUE(summary.has_selection);
  scenario.then().overlay().empty();

  scenario.when().undo();
  scenario.then()
      .scene()
      .missing_alias("group")
      .no_parent("left")
      .no_parent("right")
      .selected_aliases({"left", "right"});
  summary = scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Object);
  EXPECT_TRUE(summary.has_selection);

  scenario.when().redo();
  scenario.remember_entity_named("group", "Pair");
  scenario.then()
      .scene()
      .parent("left", "group")
      .parent("right", "group")
      .selected_aliases({"group"})
      .active_alias("group");
  summary = scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Object);
  EXPECT_TRUE(summary.has_selection);
  scenario.then().overlay().empty();
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

TEST(GroupingPublicApiTests,
     GroupNoncontiguousRootsUsesAuthoredOrderAndRestoresExactSlotsOnUndo) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  for (const std::string name : {"A", "B", "C", "D", "E"}) {
    scenario.given().entity(name, name, QuaderEntityKind::Empty);
  }
  const std::vector<EntityId> moved{scenario.entity("D")};
  ASSERT_TRUE(scenario.api().scene().move_entities(
      moved, kInvalidEntityId, scenario.entity("B")).ok());
  const std::vector<std::string> original{"A", "D", "B", "C", "E"};
  ASSERT_EQ(root_entity_names(scenario.api()), original);
  scenario.given().selected_aliases({"C", "D"}, "C");

  scenario.when().group_selection("group", "Pair");
  scenario.then().command().ok_changed();
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "Pair", "B", "E"}));
  EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("group")),
            (std::vector<std::string>{"D", "C"}));
  scenario.then().scene().no_parent("group")
      .parent("D", "group").parent("C", "group")
      .selected_aliases({"group"}).active_alias("group");

  ASSERT_TRUE(scenario.api().history().undo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()), original);
  scenario.then().scene().no_parent("C").no_parent("D")
      .selected_aliases({"C", "D"}).active_alias("C");
  ASSERT_TRUE(scenario.api().history().redo().ok());
  scenario.remember_entity_named("group", "Pair");
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "Pair", "B", "E"}));
  EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("group")),
            (std::vector<std::string>{"D", "C"}));
}

TEST(GroupingPublicApiTests,
     UngroupMultipleRootsExpandsTheirAuthoredSlotsAndRestoresThemOnUndo) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  for (const std::string name : {"A", "First Group", "B", "Second Group", "C"}) {
    scenario.given().entity(name, name, QuaderEntityKind::Empty);
  }
  scenario.given().entity("x", "X", QuaderEntityKind::Empty, {}, "First Group");
  scenario.given().entity("y", "Y", QuaderEntityKind::Empty, {}, "First Group");
  scenario.given().entity("u", "U", QuaderEntityKind::Empty, {}, "Second Group");
  scenario.given().entity("v", "V", QuaderEntityKind::Empty, {}, "Second Group");
  const std::vector<EntityId> moved{scenario.entity("y")};
  ASSERT_TRUE(scenario.api().scene().move_entities(
      moved, scenario.entity("First Group"), scenario.entity("x")).ok());
  scenario.given().selected_aliases({"Second Group", "First Group"}, "Second Group");
  const std::vector<std::string> grouped{
      "A", "First Group", "B", "Second Group", "C"};
  const std::vector<std::string> expanded{"A", "Y", "X", "B", "U", "V", "C"};
  ASSERT_EQ(root_entity_names(scenario.api()), grouped);

  scenario.when().ungroup_selection();
  scenario.then().command().ok_changed();
  EXPECT_EQ(root_entity_names(scenario.api()), expanded);
  scenario.then().scene().no_parent("x").no_parent("y")
      .no_parent("u").no_parent("v").selected_aliases({"x", "y", "u", "v"});

  ASSERT_TRUE(scenario.api().history().undo().ok());
  scenario.remember_entity_named("First Group", "First Group");
  scenario.remember_entity_named("Second Group", "Second Group");
  EXPECT_EQ(root_entity_names(scenario.api()), grouped);
  EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("First Group")),
            (std::vector<std::string>{"Y", "X"}));
  EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("Second Group")),
            (std::vector<std::string>{"U", "V"}));
  scenario.then().scene().selected_aliases({"First Group", "Second Group"})
      .active_alias("Second Group");
  ASSERT_TRUE(scenario.api().history().redo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()), expanded);
}

TEST(GroupingPublicApiTests,
     ReparentAndUnparentUndoRestoreExactRootAndChildSlots) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  scenario.given().entity("a", "A", QuaderEntityKind::Empty);
  given_box_at(scenario, "subject", "Subject", {3.0f, 0.0f, 0.0f});
  scenario.given().entity("target", "Target", QuaderEntityKind::Empty,
                          transform_at({10.0f, 0.0f, 0.0f}));
  scenario.given().entity("z", "Z", QuaderEntityKind::Empty);
  scenario.given().entity("x", "X", QuaderEntityKind::Empty, {}, "target");
  scenario.given().entity("y", "Y", QuaderEntityKind::Empty, {}, "target");
  const QuaderModelingObjectRef subject = scenario.object("subject");
  scenario.given().selected("subject");

  ASSERT_TRUE(scenario.api().scene().reparent(
      subject.entity, scenario.entity("target")).ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "Target", "Z"}));
  EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("target")),
            (std::vector<std::string>{"X", "Y", "Subject"}));
  scenario.then().scene().world_position_near("subject", {3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(scenario.api().history().undo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "Subject", "Target", "Z"}));
  EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("target")),
            (std::vector<std::string>{"X", "Y"}));
  ASSERT_TRUE(scenario.api().history().redo().ok());

  const std::vector<EntityId> moved{subject.entity};
  ASSERT_TRUE(scenario.api().scene().move_entities(
      moved, scenario.entity("target"), scenario.entity("y")).ok());
  ASSERT_EQ(child_entity_names(scenario.api(), scenario.entity("target")),
            (std::vector<std::string>{"X", "Subject", "Y"}));
  ASSERT_TRUE(scenario.api().scene().reparent(
      subject.entity, kInvalidEntityId).ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "Target", "Z", "Subject"}));
  scenario.then().scene().no_parent("subject")
      .world_position_near("subject", {3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(scenario.api().history().undo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "Target", "Z"}));
  EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("target")),
            (std::vector<std::string>{"X", "Subject", "Y"}));
  ASSERT_TRUE(scenario.api().history().redo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "Target", "Z", "Subject"}));
  scenario.then().scene().no_parent("subject")
      .world_position_near("subject", {3.0f, 0.0f, 0.0f})
      .selected_aliases({"subject"}).active_alias("subject");
  const auto restored = scenario.api().query().entity(subject.entity);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->uuid, subject.entity_uuid);
  EXPECT_EQ(restored->modeling_object_id, subject.modeling_object_id);
  EXPECT_EQ(restored->mesh, subject.mesh);
}

TEST(GroupingPublicApiTests,
     SequentialReparentBatchRestoresEachLiveRootPlacementOnUndo) {
  for (const bool return_first_to_root : {false, true}) {
    SCOPED_TRACE(return_first_to_root);
    BehaviorScenario scenario = BehaviorScenario::new_project();
    for (const std::string name : {"A", "B", "C", "D", "Group"}) {
      scenario.given().entity(name, name, QuaderEntityKind::Empty);
    }
    scenario.given().entity("child", "Child", QuaderEntityKind::Empty, {}, "Group");
    const std::vector<EntityId> moved{scenario.entity("D")};
    ASSERT_TRUE(scenario.api().scene().move_entities(
        moved, kInvalidEntityId, scenario.entity("B")).ok());
    const std::vector<std::string> original{"A", "D", "B", "C", "Group"};
    ASSERT_EQ(root_entity_names(scenario.api()), original);

    auto batch = scenario.api().scene().batch("Sequential Root Parenting");
    batch.reparent(scenario.entity("D"), scenario.entity("Group"));
    if (return_first_to_root) {
      batch.reparent(scenario.entity("D"), kInvalidEntityId);
    } else {
      batch.reparent(scenario.entity("B"), scenario.entity("Group"));
    }
    const QuaderApiVoidResult result = batch.commit();
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    const std::vector<std::string> after_roots = return_first_to_root
        ? std::vector<std::string>{"A", "B", "C", "Group", "D"}
        : std::vector<std::string>{"A", "C", "Group"};
    const std::vector<std::string> after_children = return_first_to_root
        ? std::vector<std::string>{"Child"}
        : std::vector<std::string>{"Child", "D", "B"};
    EXPECT_EQ(root_entity_names(scenario.api()), after_roots);
    EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("Group")),
              after_children);
    scenario.then().history().undo_label("Sequential Root Parenting");

    ASSERT_TRUE(scenario.api().history().undo().ok());
    EXPECT_EQ(root_entity_names(scenario.api()), original);
    EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("Group")),
              (std::vector<std::string>{"Child"}));
    ASSERT_TRUE(scenario.api().history().redo().ok());
    EXPECT_EQ(root_entity_names(scenario.api()), after_roots);
    EXPECT_EQ(child_entity_names(scenario.api(), scenario.entity("Group")),
              after_children);
  }
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
  EXPECT_TRUE(scenario.api().modeling().selection_summary().has_selection);

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

  scenario.when().execute(quader_command::kDeleteSelection);
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

  scenario.when().execute(quader_command::kEditUndo);
  scenario.remember_entity_named("group", "Shortcut Group");
  scenario.remember_entity_named("left", "Shortcut Left");
  scenario.remember_entity_named("right", "Shortcut Right");
  scenario.then()
      .scene()
      .active_alias("group")
      .parent("left", "group")
      .parent("right", "group");

  scenario.when().execute(quader_command::kEditRedo);
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

  scenario.when().execute(quader_command::kEditUndo);
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

  scenario.when().execute(quader_command::kDeleteSelection);
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

  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.then().command().ok_changed();
  scenario.then()
      .scene()
      .named_missing("Component Mode Group")
      .named_missing("Component Mode Child")
      .named_missing("Component Mode Sibling");

  scenario.when().execute(quader_command::kEditUndo);
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

  scenario.when().execute(quader_command::kDeleteSelection);
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

  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.then().scene().named_missing("Command Group");

  scenario.when().execute(quader_command::kEditUndo);
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

  scenario.when().execute(quader_command::kEditRedo);
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
  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.then()
      .scene()
      .named_missing("Delete Child")
      .named_exists("Child Delete Group");

  scenario.when().execute(quader_command::kEditUndo);
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

  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.when().execute(quader_command::kEditUndo);
  scenario.remember_entity_named("group", "Again Group");

  select_group_for_command_delete(scenario, "group");
  scenario.when().execute(quader_command::kDeleteSelection);
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

  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.when().execute(quader_command::kEditUndo);
  scenario.remember_entity_named("group", "Redo Guard Group");
  scenario.then()
      .scene()
      .named_exists("Redo Guard First")
      .named_exists("Redo Guard Second");
  scenario.then().history().can_redo();

  scenario.when().rename("group", "Redo Guard Edited");
  scenario.then().history().can_redo(false);

  scenario.when().execute(quader_command::kEditRedo);
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
  EXPECT_EQ(scenario.modeling_object_count(), 2u);
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
  EXPECT_EQ(scenario.modeling_object_count(), 1u);
  scenario.then().scene().selected_aliases({"group"});
  scenario.then().history().redo_label("Duplicate Selection");

  scenario.when().redo();
  const EntityId redone_duplicate_group =
      selected_entity_excluding(scenario.api(), source_group);
  ASSERT_NE(redone_duplicate_group, kInvalidEntityId);
  EXPECT_EQ(scenario.modeling_object_count(), 2u);
  EXPECT_EQ(children_of(scenario.api(), redone_duplicate_group).size(), 2u);
  EXPECT_EQ(selected_entities(scenario.api()),
            std::vector<EntityId>{redone_duplicate_group});
}

TEST(GroupingPublicApiTests,
     GroupAndUngroupPreserveSceneOwnedGroupSelectionMode) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "First", {-2.0f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Second", {2.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"first", "second"}, "second");
  scenario.given().selection_mode(QuaderSelectionMode::Group);

  scenario.when().group_selection("group", "Group Mode");
  scenario.then().command().ok_changed();
  QuaderSelectionSummary summary =
      scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Group);

  scenario.when().ungroup_selection();
  scenario.then().command().ok_changed();
  summary = scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Group);

  scenario.when().undo();
  summary = scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Group);

  scenario.when().redo();
  summary = scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Group);
}

TEST(GroupingPublicApiTests,
     GroupHidesComponentSelectionAndUndoRedoRestoreEachSemanticState) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "Component First", {-2.0f, 0.0f, 0.0f});
  given_box_at(scenario, "second", "Component Second", {2.0f, 0.0f, 0.0f});
  scenario.given().selected_aliases({"first", "second"}, "first");
  ASSERT_TRUE(
      scenario.editor().selection().select_face(scenario.object("first"), 1).ok());
  QuaderSelectionSummary summary =
      scenario.api().modeling().selection_summary();
  ASSERT_EQ(summary.mode, QuaderSelectionMode::Face);
  ASSERT_EQ(summary.selected_face_count, 1u);

  scenario.when().group_selection("group", "Component Selection Group");
  scenario.then().command().ok_changed();
  summary = scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Object);
  EXPECT_EQ(summary.selected_face_count, 0u);
  scenario.then().overlay().empty();

  scenario.when().undo();
  summary = scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Face);
  EXPECT_EQ(summary.selected_face_count, 1u);
  scenario.then().overlay().has_line(ViewportOverlayLayer::SelectedFaceEdge);

  scenario.when().redo();
  summary = scenario.api().modeling().selection_summary();
  EXPECT_EQ(summary.mode, QuaderSelectionMode::Object);
  EXPECT_EQ(summary.selected_face_count, 0u);
  scenario.then().overlay().empty();
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

TEST(GroupingPublicApiTests,
     DeleteSeparatedRootsRestoresAuthoredSlotsBetweenSurvivorsOnUndo) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  for (const std::string name : {"A", "B", "C", "D", "E"}) {
    scenario.given().entity(name, name, QuaderEntityKind::Empty);
  }
  const std::vector<EntityId> moved{scenario.entity("D")};
  ASSERT_TRUE(scenario.api().scene().move_entities(
      moved, kInvalidEntityId, scenario.entity("B")).ok());
  const std::vector<std::string> original{"A", "D", "B", "C", "E"};
  ASSERT_EQ(root_entity_names(scenario.api()), original);
  scenario.given().selected_aliases({"C", "D"}, "C");
  scenario.given().selection_mode(QuaderSelectionMode::Object);

  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.then().command().ok_changed();
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "B", "E"}));
  ASSERT_TRUE(scenario.api().history().undo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()), original);
  scenario.then().scene().no_parent("C").no_parent("D")
      .selected_aliases({"C", "D"}).active_alias("C");
  ASSERT_TRUE(scenario.api().history().redo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()),
            (std::vector<std::string>{"A", "B", "E"}));
  ASSERT_TRUE(scenario.api().history().undo().ok());
  EXPECT_EQ(root_entity_names(scenario.api()), original);
}

TEST(GroupingPublicApiTests,
     DeleteEveryMixedRootUndoRestoresExactSiblingOrderAndCanRepeat) {
  BehaviorScenario scenario = BehaviorScenario::new_project();
  given_box_at(scenario, "first", "Delete All First", {-2.0f, 0.0f, 0.0f});

  QuaderCreatePointLightArgs light_args;
  light_args.name = "Delete All Light";
  const QuaderApiResult<QuaderEntityRef> light =
      scenario.api().scene().create_point_light(light_args);
  ASSERT_TRUE(light.ok) << light.message;
  scenario.remember("light", light.value);

  given_box_at(scenario, "second", "Delete All Second", {2.0f, 0.0f, 0.0f});
  const std::vector<std::string> expected_order = {
      "Delete All First", "Delete All Light", "Delete All Second"};
  EXPECT_EQ(root_entity_names(scenario.api()), expected_order);

  scenario.given().selection_mode(QuaderSelectionMode::Object);
  const QuaderApiVoidResult selected_all =
      scenario.api().selection().select_all();
  ASSERT_TRUE(selected_all.ok()) << selected_all.error_message();
  const std::optional<QuaderEntitySnapshot> first_before =
      entity_by_id(scenario.api(), scenario.entity("first"));
  const std::optional<QuaderEntitySnapshot> light_before =
      entity_by_id(scenario.api(), scenario.entity("light"));
  const std::optional<QuaderEntitySnapshot> second_before =
      entity_by_id(scenario.api(), scenario.entity("second"));
  ASSERT_TRUE(first_before.has_value());
  ASSERT_TRUE(light_before.has_value());
  ASSERT_TRUE(second_before.has_value());
  ASSERT_TRUE(first_before->has_mesh);
  ASSERT_TRUE(first_before->has_modeling_object);
  ASSERT_TRUE(light_before->has_light);
  ASSERT_TRUE(second_before->has_mesh);
  ASSERT_TRUE(second_before->has_modeling_object);

  const auto expect_exact_restore = [&] {
    const std::optional<QuaderEntitySnapshot> first =
        entity_by_id(scenario.api(), first_before->id);
    const std::optional<QuaderEntitySnapshot> restored_light =
        entity_by_id(scenario.api(), light_before->id);
    const std::optional<QuaderEntitySnapshot> second =
        entity_by_id(scenario.api(), second_before->id);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(restored_light.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->uuid, first_before->uuid);
    EXPECT_EQ(first->editor_object_id, first_before->editor_object_id);
    EXPECT_EQ(first->parent, first_before->parent);
    EXPECT_EQ(first->modeling_object_id, first_before->modeling_object_id);
    EXPECT_EQ(first->mesh, first_before->mesh);
    EXPECT_EQ(restored_light->uuid, light_before->uuid);
    EXPECT_EQ(restored_light->editor_object_id,
              light_before->editor_object_id);
    EXPECT_EQ(restored_light->parent, light_before->parent);
    EXPECT_EQ(restored_light->light.type, light_before->light.type);
    EXPECT_FLOAT_EQ(restored_light->light.color.x,
                    light_before->light.color.x);
    EXPECT_FLOAT_EQ(restored_light->light.color.y,
                    light_before->light.color.y);
    EXPECT_FLOAT_EQ(restored_light->light.color.z,
                    light_before->light.color.z);
    EXPECT_FLOAT_EQ(restored_light->light.intensity,
                    light_before->light.intensity);
    EXPECT_FLOAT_EQ(restored_light->light.range, light_before->light.range);
    EXPECT_EQ(restored_light->light.casts_shadows,
              light_before->light.casts_shadows);
    EXPECT_EQ(second->uuid, second_before->uuid);
    EXPECT_EQ(second->editor_object_id, second_before->editor_object_id);
    EXPECT_EQ(second->parent, second_before->parent);
    EXPECT_EQ(second->modeling_object_id, second_before->modeling_object_id);
    EXPECT_EQ(second->mesh, second_before->mesh);
    EXPECT_TRUE(scenario.api().query().mesh(first->mesh).has_value());
    EXPECT_TRUE(scenario.api().query().mesh(second->mesh).has_value());
    EXPECT_EQ(first->selected, first_before->selected);
    EXPECT_EQ(restored_light->selected, light_before->selected);
    EXPECT_EQ(second->selected, second_before->selected);
    EXPECT_EQ(first->active, first_before->active);
    EXPECT_EQ(restored_light->active, light_before->active);
    EXPECT_EQ(second->active, second_before->active);
    EXPECT_EQ(root_entity_names(scenario.api()), expected_order);
  };

  scenario.when().execute(quader_command::kDeleteSelection);
  scenario.then().command().ok_changed();
  EXPECT_TRUE(root_entity_names(scenario.api()).empty());
  EXPECT_FALSE(entity_by_id(scenario.api(), first_before->id).has_value());
  EXPECT_FALSE(entity_by_id(scenario.api(), light_before->id).has_value());
  EXPECT_FALSE(entity_by_id(scenario.api(), second_before->id).has_value());

  QuaderApiVoidResult undo = scenario.api().history().undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  EXPECT_TRUE(undo.changed());
  expect_exact_restore();
  EXPECT_EQ(selected_entities(scenario.api()),
            (std::vector<EntityId>{scenario.entity("first"),
                                   scenario.entity("light"),
                                   scenario.entity("second")}));

  const QuaderApiVoidResult redo = scenario.api().history().redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
  EXPECT_TRUE(redo.changed());
  EXPECT_TRUE(root_entity_names(scenario.api()).empty());

  undo = scenario.api().history().undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  EXPECT_TRUE(undo.changed());
  expect_exact_restore();
}
