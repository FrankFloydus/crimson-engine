////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace quader {
namespace {

void expect_radial_instance_matrix(const QMat4 &actual, const QMat4 &expected) {
  for (std::size_t index = 0; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], 0.0002f);
  }
}

QMat4 radial_member_world(const SceneDocument &scene, EntityId occurrence,
                          EntityId member) {
  const auto *component = scene.instance_occurrence(occurrence);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) return {};
  const auto inverse_definition =
      try_scene_matrix_inverse(scene.world_transform_matrix(component->definition));
  EXPECT_TRUE(inverse_definition.has_value());
  return inverse_definition.has_value()
      ? scene_transform_multiply(scene.world_transform_matrix(occurrence),
            scene_transform_multiply(*inverse_definition,
                                     scene.world_transform_matrix(member)))
      : QMat4{};
}

/// Exercises array acceptance through the public tool API and real scene owners.
class RadialArrayInstanceHarness final {
public:
  RadialArrayInstanceHarness()
      : editor(QuaderApiSessionInternalAccess::from_scene_session(session)) {}

  QuaderModelingObjectRef box(std::string name, QVec3 position = {4, 0, 0}) {
    const auto result = editor.modeling().create_box(
        {.min = {-1, -0.5f, -0.25f}, .max = {2, 1.5f, 0.75f}}, std::move(name));
    EXPECT_TRUE(result.ok()) << result.error_message();
    if (!result.ok()) return {};
    EXPECT_TRUE(editor.scene().set_transform(result.value().entity,
                                             {.position = position}).ok());
    return result.value();
  }

  EntityId instance(QuaderModelingObjectRef member) {
    EXPECT_TRUE(editor.selection().select_object(member).ok());
    const auto created = editor.modeling().create_instance_from_selection();
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? session.scene().primary_selection() : kInvalidEntityId;
  }

  void begin(std::vector<EntityId> sources, bool linked) {
    ASSERT_FALSE(sources.empty());
    ASSERT_TRUE(editor.selection().select_objects(sources, sources.back()).ok());
    session.history().clear();
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::RadialArray).ok());
    ASSERT_TRUE(session.radial_array().set_create_instances(linked).ok);
    ASSERT_TRUE(session.radial_array().set_copies(3u).ok);
    ASSERT_TRUE(session.radial_array().set_degrees(150.0).ok);
    ASSERT_TRUE(session.radial_array()
                    .update_center_candidate({-1, 0, -2}, {0, 1, 0}, 1.0).ok);
    ASSERT_TRUE(session.radial_array().preview_state().active);
  }

  void commit() {
    const auto result = editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(result.ok()) << result.error_message();
    ASSERT_TRUE(result.changed());
    EXPECT_EQ(session.history().undo_count(), 1u);
    ASSERT_NE(session.history().undo_label(), nullptr);
    EXPECT_EQ(*session.history().undo_label(), "Radial Array");
  }

  SceneSession session;
  QuaderEditorApi editor;
};

TEST(RadialArrayInstanceTests,
     RemembersAllOptionsAcrossApplyCancelAndServiceReplacement) {
  RadialArraySessionDefaults defaults;
  for (int run = 0; run < 2; ++run) {
    RadialArrayInstanceHarness h;
    const auto source = h.box("Remembered Radial Source");
    ASSERT_TRUE(h.editor.selection().select_object(source).ok());
    RadialArrayToolService tool(
        h.session.scene(), h.session.assets(), h.session.history(),
        h.session.modeling(), h.session.selection(), h.session.instances(),
        h.session.structural_lifecycle(), &defaults);
    ASSERT_TRUE(tool.activate(2.0).ok);
    if (run == 0) {
      ASSERT_TRUE(tool.set_mode(RadialArrayMode::Spacing).ok);
      ASSERT_TRUE(tool.set_degrees(45.0).ok);
      ASSERT_TRUE(tool.set_copies(3u).ok);
      ASSERT_TRUE(tool.set_repeats(2u).ok);
      ASSERT_TRUE(tool.set_create_instances(true).ok);
      // Choosing the current automatic value still expresses explicit spacing.
      ASSERT_TRUE(tool.set_repeat_spacing(2.0).ok);
      ASSERT_TRUE(defaults.repeat_spacing.has_value());
      ASSERT_TRUE(tool.update_center_candidate({}, {0, 1, 0}, 1.0).ok);
      ASSERT_TRUE(tool.lock_center().ok);
      ASSERT_TRUE(tool.cancel().ok);
      ASSERT_TRUE(tool.activate(9.0).ok);
    }
    EXPECT_EQ(tool.mode(), RadialArrayMode::Spacing);
    EXPECT_DOUBLE_EQ(tool.degrees(), 45.0);
    EXPECT_EQ(tool.copies(), 3u);
    EXPECT_EQ(tool.repeats(), 2u);
    EXPECT_TRUE(tool.create_instances());
    EXPECT_DOUBLE_EQ(tool.repeat_spacing(), 2.0);
    EXPECT_FALSE(tool.center_locked());
    EXPECT_FALSE(tool.has_valid_center());
    EXPECT_FALSE(tool.set_copies(kRadialArrayMaximumCopies + 1u).ok);
    EXPECT_FALSE(tool.set_repeat_spacing(-1.0).ok);
    EXPECT_EQ(defaults.copies, 3u);
    EXPECT_DOUBLE_EQ(defaults.repeat_spacing.value(), 2.0);
    ASSERT_TRUE(tool.update_center_candidate({}, {0, 1, 0}, 5.0).ok);
    EXPECT_DOUBLE_EQ(tool.repeat_spacing(), 2.0);
    ASSERT_TRUE(tool.apply().ok);
    EXPECT_EQ(defaults.copies, 3u);
    EXPECT_EQ(defaults.repeats, 2u);
  }
  EXPECT_FALSE(RadialArraySessionDefaults{}.repeat_spacing.has_value());
}

TEST(RadialArrayInstanceTests,
     AutomaticSpacingAndCapacityRemainLocalToEachOperation) {
  RadialArrayInstanceHarness h;
  const auto source = h.box("Radial Capacity Source");
  const auto second = h.box("Radial Capacity Second");
  ASSERT_TRUE(h.editor.selection().select_object(source).ok());
  RadialArraySessionDefaults defaults;
  RadialArrayToolService tool(
      h.session.scene(), h.session.assets(), h.session.history(),
      h.session.modeling(), h.session.selection(), h.session.instances(),
      h.session.structural_lifecycle(), &defaults);
  ASSERT_TRUE(tool.activate(2.0).ok);
  ASSERT_TRUE(tool.set_copies(128u).ok);
  ASSERT_TRUE(tool.set_repeats(8u).ok);
  ASSERT_TRUE(tool.update_center_candidate({}, {0, 1, 0}, 2.0).ok);
  const double first_spacing = tool.repeat_spacing();
  ASSERT_TRUE(tool.cancel().ok);
  EXPECT_FALSE(defaults.repeat_spacing.has_value());
  ASSERT_TRUE(h.editor.selection()
                  .select_objects({source.entity, second.entity}, second.entity)
                  .ok());
  ASSERT_TRUE(tool.activate(3.0).ok);
  EXPECT_EQ(tool.copies(), 128u);
  EXPECT_EQ(tool.repeats(), 4u);
  EXPECT_EQ(defaults.copies, 128u);
  EXPECT_EQ(defaults.repeats, 8u);
  EXPECT_DOUBLE_EQ(tool.repeat_spacing(), 3.0);
  ASSERT_TRUE(tool.update_center_candidate({-10, 0, 0}, {0, 1, 0}, 3.0).ok);
  EXPECT_GT(tool.repeat_spacing(), first_spacing);
  EXPECT_FALSE(defaults.repeat_spacing.has_value());
  ASSERT_TRUE(tool.cancel().ok);
  ASSERT_TRUE(h.editor.selection().select_object(source).ok());
  ASSERT_TRUE(tool.activate(1.0).ok);
  EXPECT_EQ(tool.repeats(), 8u);
}

TEST(RadialArrayInstanceTests, LinkedMeshArrayConvertsOriginalAndPreservesRedoIdentities) {
  RadialArrayInstanceHarness h;
  const auto source = h.box("Linked Radial Source");
  ASSERT_NE(source.entity, kInvalidEntityId);
  const auto parent = h.session.scene().parent(source.entity);
  const auto original_world = h.session.scene().world_transform_matrix(source.entity);
  const auto original_mesh = h.session.scene().mesh(source.entity)->mesh;
  const auto original_entities = h.session.scene().entities();
  const auto original_assets = h.session.assets().mesh_ids();
  h.begin({source.entity}, true);
  const auto preview = h.session.radial_array().preview_state();
  ASSERT_EQ(preview.sources.size(), 1u);
  h.commit();
  const auto selected = h.session.scene().selected_entities();
  ASSERT_EQ(selected.size(), 3u);
  const EntityId definition = h.session.scene().containing_instance_definition(source.entity);
  ASSERT_NE(definition, kInvalidEntityId);
  EXPECT_EQ(h.session.assets().mesh_ids(), original_assets);
  EXPECT_EQ(h.session.scene().mesh(source.entity)->mesh, original_mesh);
  std::vector<QMat4> expected{original_world};
  expected.insert(expected.end(), preview.sources.front().generated_world_transforms.begin(),
                  preview.sources.front().generated_world_transforms.end());
  for (std::size_t index = 0; index < selected.size(); ++index) {
    ASSERT_NE(h.session.scene().instance_occurrence(selected[index]), nullptr);
    EXPECT_EQ(h.session.scene().instance_occurrence(selected[index])->definition, definition);
    EXPECT_EQ(h.session.scene().parent(selected[index]), parent);
    expect_radial_instance_matrix(radial_member_world(h.session.scene(), selected[index],
                                                       source.entity), expected[index]);
  }
  ASSERT_TRUE(h.editor.history().undo().ok());
  EXPECT_EQ(h.session.scene().entities(), original_entities);
  EXPECT_EQ(h.session.scene().parent(source.entity), parent);
  expect_radial_instance_matrix(h.session.scene().world_transform_matrix(source.entity),
                                original_world);
  EXPECT_EQ(h.session.scene().selected_entities(), std::vector<EntityId>{source.entity});
  ASSERT_TRUE(h.editor.history().redo().ok());
  EXPECT_EQ(h.session.scene().selected_entities(), selected);
  EXPECT_EQ(h.session.scene().containing_instance_definition(source.entity), definition);

  ASSERT_TRUE(h.editor.modeling().enter_instance_edit(selected.front()).ok());
  const auto *before_asset = h.session.assets().mesh(original_mesh);
  ASSERT_NE(before_asset, nullptr);
  ASSERT_TRUE(before_asset->authored_polygon_document);
  const auto vertex = before_asset->authored_polygon_document->vertices.front();
  ASSERT_TRUE(h.editor.modeling().mesh(source).vertices()
                  .only({static_cast<QuaderElementId>(vertex.id)})
                  .replace().ok());
  const auto moved = h.editor.modeling().move_selection({0, 0.25f, 0});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  ASSERT_TRUE(moved.changed());
  const auto edited_position =
      h.session.assets().mesh(original_mesh)->authored_polygon_document->vertices.front().position;
  EXPECT_NE(edited_position.y, vertex.position.y);
  const auto projection = h.session.scene().mesh_occurrence_projection();
  std::size_t edited_occurrences = 0;
  for (const auto &record : projection->records()) {
    if (std::ranges::find(selected, record.display_entity) == selected.end()) continue;
    EXPECT_EQ(record.geometry_entity, source.entity);
    const auto mesh = h.session.scene().mesh(record.geometry_entity)->mesh;
    EXPECT_EQ(mesh, original_mesh);
    const auto &position = h.session.assets().mesh(mesh)->authored_polygon_document->vertices.front().position;
    EXPECT_FLOAT_EQ(position.y, edited_position.y);
    ++edited_occurrences;
  }
  EXPECT_EQ(edited_occurrences, selected.size());
  ASSERT_TRUE(h.editor.history().undo().ok());
  const auto restored_position =
      h.session.assets().mesh(original_mesh)->authored_polygon_document->vertices.front().position;
  EXPECT_FLOAT_EQ(restored_position.x, vertex.position.x);
  EXPECT_FLOAT_EQ(restored_position.y, vertex.position.y);
  EXPECT_FLOAT_EQ(restored_position.z, vertex.position.z);
  for (const EntityId occurrence : selected) {
    EXPECT_EQ(h.session.scene().instance_occurrence(occurrence)->definition, definition);
  }
}

TEST(RadialArrayInstanceTests, IndependentMeshArrayKeepsOriginalMeshAndCreatesEditableCopies) {
  RadialArrayInstanceHarness h;
  const auto source = h.box("Independent Radial Source");
  const auto original_mesh = h.session.scene().mesh(source.entity)->mesh;
  h.begin({source.entity}, false);
  const auto preview = h.session.radial_array().preview_state();
  h.commit();
  const auto selected = h.session.scene().selected_entities();
  ASSERT_EQ(selected.size(), 3u);
  EXPECT_EQ(h.session.scene().mesh(source.entity)->mesh, original_mesh);
  std::size_t copy = 0;
  for (const EntityId entity : selected) {
    EXPECT_EQ(h.session.scene().instance_occurrence(entity), nullptr);
    ASSERT_NE(h.session.scene().modeling_object(entity), nullptr);
    if (entity == source.entity) continue;
    EXPECT_NE(h.session.scene().mesh(entity)->mesh, original_mesh);
    expect_radial_instance_matrix(h.session.scene().world_transform_matrix(entity),
        preview.sources.front().generated_world_transforms[copy++]);
  }
  EXPECT_EQ(copy, 2u);
}

TEST(RadialArrayInstanceTests, ExistingOccurrenceCanCreateLinkedOrIndependentCopies) {
  for (const bool linked : {false, true}) {
    SCOPED_TRACE(linked);
    RadialArrayInstanceHarness h;
    const auto member = h.box("Existing Instance Member");
    const EntityId source = h.instance(member);
    ASSERT_NE(source, kInvalidEntityId);
    const EntityId definition = h.session.scene().instance_occurrence(source)->definition;
    const AssetId original_mesh = h.session.scene().mesh(member.entity)->mesh;
    const auto original_entities = h.session.scene().entities();
    h.begin({source}, linked);
    const auto preview = h.session.radial_array().preview_state();
    ASSERT_EQ(preview.sources.size(), 1u);
    h.commit();
    const auto selected = h.session.scene().selected_entities();
    ASSERT_EQ(selected.size(), 3u);
    ASSERT_NE(h.session.scene().instance_occurrence(source), nullptr);
    EXPECT_EQ(h.session.scene().instance_occurrence(source)->definition, definition);
    std::size_t copy = 0;
    std::vector<AssetId> copied_assets;
    for (const EntityId entity : selected) {
      if (entity == source) continue;
      if (linked) {
        ASSERT_NE(h.session.scene().instance_occurrence(entity), nullptr);
        EXPECT_EQ(h.session.scene().instance_occurrence(entity)->definition, definition);
        expect_radial_instance_matrix(radial_member_world(h.session.scene(), entity,
                                                           member.entity),
            preview.sources.front().generated_world_transforms[copy]);
      } else {
        EXPECT_EQ(h.session.scene().instance_occurrence(entity), nullptr);
        ASSERT_NE(h.session.scene().modeling_object(entity), nullptr);
        ASSERT_NE(h.session.scene().mesh(entity), nullptr);
        const AssetId mesh = h.session.scene().mesh(entity)->mesh;
        EXPECT_NE(mesh, original_mesh);
        EXPECT_EQ(std::ranges::find(copied_assets, mesh), copied_assets.end());
        copied_assets.push_back(mesh);
        expect_radial_instance_matrix(h.session.scene().world_transform_matrix(entity),
            preview.sources.front().generated_world_transforms[copy]);
      }
      ++copy;
    }
    EXPECT_EQ(copy, 2u);
    ASSERT_TRUE(h.editor.history().undo().ok());
    EXPECT_EQ(h.session.scene().entities(), original_entities);
    EXPECT_EQ(h.session.scene().selected_entities(), std::vector<EntityId>{source});
    ASSERT_TRUE(h.editor.history().redo().ok());
    EXPECT_EQ(h.session.scene().selected_entities(), selected);
    EXPECT_EQ(h.session.scene().instance_occurrence(source)->definition, definition);
  }
}

TEST(RadialArrayInstanceTests, MultipleSourcesRetainSeparateFamiliesPivotsParentsAndMaterials) {
  RadialArrayInstanceHarness h;
  const auto parent = h.editor.scene().create_entity(
      {.name = "Transformed Array Parent", .kind = QuaderEntityKind::Empty});
  ASSERT_TRUE(parent.ok());
  ASSERT_TRUE(h.editor.scene().set_transform(parent.value().id,
      {.position = {3, 2, -1}, .rotation_euler = {0, 0.4f, 0}, .scale = {2, 2, 2}}).ok());
  const auto first = h.box("First Family", {4, 0, 0});
  const auto second = h.box("Second Family", {-2, 1, 4});
  ASSERT_TRUE(h.editor.scene().reparent(first.entity, parent.value().id, false).ok());
  auto custom = *h.session.scene().transform(first.entity);
  custom.custom_pivot = true;
  h.session.scene().set_transform(first.entity, custom);
  const AssetId first_material = h.session.assets().add_material({});
  const AssetId second_material = h.session.assets().add_material({});
  ASSERT_TRUE(h.session.scene().set_material(first.entity, {.material = first_material}));
  ASSERT_TRUE(h.session.scene().set_material(second.entity, {.material = second_material}));
  const std::array members{first.entity, second.entity};
  const std::array parents{h.session.scene().parent(first.entity),
                            h.session.scene().parent(second.entity)};
  const std::array original_world{h.session.scene().world_transform_matrix(first.entity),
                                   h.session.scene().world_transform_matrix(second.entity)};
  h.begin({first.entity, second.entity}, true);
  ASSERT_TRUE(h.session.radial_array().set_repeats(2u).ok);
  ASSERT_TRUE(h.session.radial_array().set_repeat_spacing(2.5).ok);
  const auto preview = h.session.radial_array().preview_state();
  ASSERT_EQ(preview.sources.size(), 2u);
  h.commit();
  EXPECT_EQ(h.session.scene().selected_entities().size(), 12u);
  const auto first_definition = h.session.scene().containing_instance_definition(first.entity);
  const auto second_definition = h.session.scene().containing_instance_definition(second.entity);
  EXPECT_NE(first_definition, second_definition);
  for (std::size_t index = 0; index < members.size(); ++index) {
    const EntityId member = members[index];
    const auto found = std::ranges::find(preview.sources, member,
                                          &RadialArrayPreviewSource::source_entity);
    ASSERT_NE(found, preview.sources.end());
    const auto definition = h.session.scene().containing_instance_definition(member);
    const auto occurrences = h.session.scene().instance_occurrences_for_definition(definition);
    ASSERT_EQ(occurrences.size(), 6u);
    std::vector<QMat4> expected{original_world[index]};
    expected.insert(expected.end(), found->generated_world_transforms.begin(),
                    found->generated_world_transforms.end());
    for (std::size_t copy = 0; copy < occurrences.size(); ++copy) {
      EXPECT_EQ(h.session.scene().parent(occurrences[copy]), parents[index]);
      expect_radial_instance_matrix(radial_member_world(h.session.scene(), occurrences[copy],
                                                         member), expected[copy]);
    }
    ASSERT_NE(h.session.scene().material(member), nullptr);
    EXPECT_EQ(h.session.scene().material(member)->material, found->material);
  }
}

TEST(RadialArrayInstanceTests, IndependentCompoundOccurrencePreservesEveryMemberAndItsMaterial) {
  RadialArrayInstanceHarness h;
  const auto first = h.box("Compound Member A", {4, 0, 0});
  const auto second = h.box("Compound Member B", {4, 3, 0});
  const AssetId material_a = h.session.assets().add_material({});
  const AssetId material_b = h.session.assets().add_material({});
  ASSERT_TRUE(h.session.scene().set_material(first.entity, {.material = material_a}));
  ASSERT_TRUE(h.session.scene().set_material(second.entity, {.material = material_b}));
  ASSERT_TRUE(h.editor.selection().select_objects({first.entity, second.entity}, second.entity).ok());
  ASSERT_TRUE(h.editor.modeling().create_instance().ok());
  const EntityId source = h.session.scene().primary_selection();
  const EntityId definition = h.session.scene().instance_occurrence(source)->definition;
  h.begin({source}, false);
  const auto preview = h.session.radial_array().preview_state();
  ASSERT_EQ(preview.sources.size(), 2u);
  h.commit();
  const auto selected = h.session.scene().selected_entities();
  ASSERT_EQ(selected.size(), 5u);
  ASSERT_NE(h.session.scene().instance_occurrence(source), nullptr);
  EXPECT_EQ(h.session.scene().children(definition),
            (std::vector<EntityId>{first.entity, second.entity}));
  for (const auto &member : preview.sources) {
    std::vector<EntityId> copies;
    for (const EntityId entity : selected) {
      if (entity == source) continue;
      EXPECT_EQ(h.session.scene().instance_occurrence(entity), nullptr);
      ASSERT_NE(h.session.scene().material(entity), nullptr);
      if (h.session.scene().material(entity)->material == member.material) copies.push_back(entity);
    }
    ASSERT_EQ(copies.size(), 2u);
    for (std::size_t index = 0; index < copies.size(); ++index) {
      expect_radial_instance_matrix(h.session.scene().world_transform_matrix(copies[index]),
                                    member.generated_world_transforms[index]);
      EXPECT_NE(h.session.scene().mesh(copies[index])->mesh, member.mesh);
    }
  }
  ASSERT_TRUE(h.editor.history().undo().ok());
  EXPECT_EQ(h.session.scene().selected_entities(), std::vector<EntityId>{source});
  ASSERT_TRUE(h.editor.history().redo().ok());
  EXPECT_EQ(h.session.scene().selected_entities(), selected);
}

TEST(RadialArrayInstanceTests, TogglingPreviewAndCancelNeverConvertsSources) {
  RadialArrayInstanceHarness h;
  const auto source = h.box("Canceled Instance Array");
  h.begin({source.entity}, false);
  const auto entities = h.session.scene().entities();
  const auto assets = h.session.assets().mesh_ids();
  const auto content_revision = h.session.modeling().content_revision();
  const auto scene_revision = h.session.scene().revision();
  const auto original_preview = h.session.radial_array().preview_state();
  for (const bool linked : {true, false, true}) {
    ASSERT_TRUE(h.session.radial_array().set_create_instances(linked).ok);
    const auto preview = h.session.radial_array().preview_state();
    ASSERT_EQ(preview.sources.size(), original_preview.sources.size());
    EXPECT_EQ(preview.sources.front().generated_world_transforms.size(), 2u);
    EXPECT_EQ(h.session.scene().entities(), entities);
    EXPECT_EQ(h.session.assets().mesh_ids(), assets);
    EXPECT_EQ(h.session.scene().revision(), scene_revision);
    EXPECT_EQ(h.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(h.session.history().undo_count(), 0u);
  }
  ASSERT_TRUE(h.editor.tools().cancel_active_tool_preview().ok());
  EXPECT_FALSE(h.session.radial_array().preview_state().active);
  EXPECT_EQ(h.session.scene().entities(), entities);
  EXPECT_EQ(h.session.scene().selected_entities(), std::vector<EntityId>{source.entity});
  EXPECT_EQ(h.session.history().undo_count(), 0u);
}

TEST(RadialArrayInstanceTests, FailedIndependentMaterializationRestoresSourceAndRedoBranch) {
  for (const auto failure : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                              ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(failure));
    RadialArrayInstanceHarness h;
    const auto member = h.box("Rollback Instance Member");
    const EntityId source = h.instance(member);
    h.session.history().clear();
    ASSERT_TRUE(h.editor.scene().rename(source, "Pending Redo").ok());
    ASSERT_TRUE(h.editor.history().undo().ok());
    ASSERT_TRUE(h.editor.tools().activate(ViewportToolId::RadialArray).ok());
    ASSERT_TRUE(h.session.radial_array().set_create_instances(false).ok);
    ASSERT_TRUE(h.session.radial_array().set_copies(3u).ok);
    ASSERT_TRUE(h.session.radial_array().update_center_candidate({}, {0, 1, 0}, 1).ok);
    const auto entities = h.session.scene().entities();
    const auto assets = h.session.assets().mesh_ids();
    const auto world = h.session.scene().world_transform_matrix(source);
    const auto revision = h.session.modeling().content_revision();
    h.session.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
    const auto rejected = h.session.radial_array().apply();
    h.session.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(rejected.ok) << rejected.message;
    EXPECT_FALSE(rejected.changed) << rejected.message;
    EXPECT_EQ(h.session.scene().entities(), entities);
    EXPECT_EQ(h.session.assets().mesh_ids(), assets);
    EXPECT_EQ(h.session.modeling().content_revision(), revision);
    EXPECT_EQ(h.session.scene().selected_entities(), std::vector<EntityId>{source});
    expect_radial_instance_matrix(h.session.scene().world_transform_matrix(source), world);
    EXPECT_EQ(h.session.history().undo_count(), 0u);
    EXPECT_EQ(h.session.history().redo_count(), 1u);
    EXPECT_TRUE(h.session.radial_array().active());
    EXPECT_TRUE(h.session.radial_array().has_valid_center());
    EXPECT_TRUE(h.session.radial_array().preview_state().active);
    if (failure == ModelingSdkProjectionFailurePoint::AfterSdkMutation) {
      h.commit();
      ASSERT_TRUE(h.editor.history().undo().ok());
      EXPECT_EQ(h.session.scene().entities(), entities);
      EXPECT_EQ(h.session.scene().selected_entities(), std::vector<EntityId>{source});
      continue;
    }
    ASSERT_TRUE(h.session.radial_array().cancel().ok);
    ASSERT_TRUE(h.editor.history().redo().ok());
    EXPECT_EQ(h.editor.query().entity(source)->name, "Pending Redo");
  }
}

TEST(RadialArrayInstanceTests, LinkedShearRejectionRestoresPreviewAndAllowsRepresentableRetry) {
  RadialArrayInstanceHarness h;
  const auto parent = h.editor.scene().create_entity(
      {.name = "Nonuniform Array Parent", .kind = QuaderEntityKind::Empty});
  ASSERT_TRUE(parent.ok());
  ASSERT_TRUE(h.editor.scene().set_transform(parent.value().id, {.scale = {2, 1, 1}}).ok());
  const auto source = h.box("Shear Rejection Source");
  ASSERT_TRUE(h.editor.scene().reparent(source.entity, parent.value().id, false).ok());
  const auto source_world = h.session.scene().world_transform_matrix(source.entity);
  h.begin({source.entity}, true);
  ASSERT_TRUE(h.session.radial_array().set_degrees(90).ok);
  const auto entities = h.session.scene().entities();
  const auto meshes = h.session.assets().mesh_ids();
  const auto content_revision = h.session.modeling().content_revision();
  const auto rejected = h.session.radial_array().apply();
  EXPECT_FALSE(rejected.ok) << rejected.message;
  EXPECT_FALSE(rejected.changed) << rejected.message;
  EXPECT_EQ(h.session.scene().entities(), entities);
  EXPECT_EQ(h.session.assets().mesh_ids(), meshes);
  EXPECT_EQ(h.session.modeling().content_revision(), content_revision);
  EXPECT_EQ(h.session.scene().parent(source.entity), parent.value().id);
  EXPECT_EQ(h.session.scene().containing_instance_definition(source.entity), kInvalidEntityId);
  expect_radial_instance_matrix(h.session.scene().world_transform_matrix(source.entity), source_world);
  EXPECT_EQ(h.session.history().undo_count(), 0u);
  EXPECT_TRUE(h.session.radial_array().active());
  EXPECT_TRUE(h.session.radial_array().has_valid_center());
  EXPECT_TRUE(h.session.radial_array().preview_state().active);
  ASSERT_TRUE(h.session.radial_array().set_degrees(180).ok);
  h.commit();
  EXPECT_EQ(h.session.scene().selected_entities().size(), 3u);
  ASSERT_TRUE(h.editor.history().undo().ok());
  EXPECT_EQ(h.session.scene().entities(), entities);
  expect_radial_instance_matrix(h.session.scene().world_transform_matrix(source.entity), source_world);
}

TEST(RadialArrayInstanceTests, FailedSamplingBakeRemovesAllocatedCopiesAndAllowsRetry) {
  RadialArrayInstanceHarness h;
  const auto member = h.box("Sampling Failure Member");
  const EntityId source = h.instance(member);
  QMat4 sampling;
  sampling.values[0] = 1.5f;
  ASSERT_TRUE(h.session.scene().set_texture_mapping(source, {sampling}));
  h.begin({source}, false);
  const auto entities = h.session.scene().entities();
  const auto assets = h.session.assets().mesh_ids();
  const auto asset_revision = h.session.assets().revision();
  const auto scene_revision = h.session.scene().revision();
  const auto content_revision = h.session.modeling().content_revision();
  bool armed_after_placement = false;
  h.session.history().set_change_observer([&] {
    if (armed_after_placement) return;
    for (const EntityId entity : h.session.scene().entities()) {
      if (entity == member.entity || h.session.scene().mesh(entity) == nullptr ||
          h.session.scene().is_instance_definition_member(entity)) continue;
      // Duplicates have been allocated and the first has left definition storage.
      armed_after_placement = true;
      h.session.modeling().set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::BeforeCommit);
      break;
    }
  });
  const auto rejected = h.session.radial_array().apply();
  h.session.history().set_change_observer({});
  h.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(armed_after_placement);
  EXPECT_FALSE(rejected.ok) << rejected.message;
  EXPECT_FALSE(rejected.changed) << rejected.message;
  EXPECT_EQ(h.session.scene().entities(), entities);
  EXPECT_EQ(h.session.assets().mesh_ids(), assets);
  EXPECT_EQ(h.session.assets().revision(), asset_revision);
  EXPECT_EQ(h.session.scene().revision(), scene_revision);
  EXPECT_EQ(h.session.modeling().content_revision(), content_revision);
  EXPECT_EQ(h.session.history().undo_count(), 0u);
  EXPECT_TRUE(h.session.radial_array().active());
  EXPECT_TRUE(h.session.radial_array().has_valid_center());
  EXPECT_TRUE(h.session.radial_array().preview_state().active);
  h.commit();
  EXPECT_EQ(h.session.scene().selected_entities().size(), 3u);
}

TEST(RadialArrayInstanceTests, IndependentShearRejectionPreservesOccurrenceAndCanRetryLinked) {
  RadialArrayInstanceHarness h;
  const auto member = h.box("Sheared Instance Member");
  ASSERT_TRUE(h.editor.scene().set_transform(member.entity,
      {.position = {4, 0, 0}, .rotation_euler = {0, 0.7853982f, 0}}).ok());
  const EntityId source = h.instance(member);
  ASSERT_TRUE(h.editor.scene().set_transform(source,
      {.position = {4, 0, 0}, .scale = {2, 1, 1}}).ok());
  h.begin({source}, false);
  const auto entities = h.session.scene().entities();
  const auto meshes = h.session.assets().mesh_ids();
  const auto rejected = h.session.radial_array().apply();
  EXPECT_FALSE(rejected.ok) << rejected.message;
  EXPECT_FALSE(rejected.changed) << rejected.message;
  EXPECT_EQ(h.session.scene().entities(), entities);
  EXPECT_EQ(h.session.assets().mesh_ids(), meshes);
  EXPECT_EQ(h.session.scene().selected_entities(), std::vector<EntityId>{source});
  EXPECT_EQ(h.session.history().undo_count(), 0u);
  EXPECT_TRUE(h.session.radial_array().active());
  EXPECT_TRUE(h.session.radial_array().has_valid_center());
  EXPECT_TRUE(h.session.radial_array().preview_state().active);
  ASSERT_TRUE(h.session.radial_array().set_create_instances(true).ok);
  h.commit();
  EXPECT_EQ(h.session.scene().selected_entities().size(), 3u);
}

TEST(RadialArrayInstanceTests, ExplicitConversionUsesRequestedRootsAndRejectsInstanceEditScope) {
  RadialArrayInstanceHarness h;
  const auto first = h.box("Requested Source", {4, 0, 0});
  const auto unrelated = h.box("Unrelated Selection", {-20, 0, 0});
  ASSERT_TRUE(h.editor.selection().select_object(unrelated).ok());
  h.session.history().clear();
  const auto original_world = h.session.scene().world_transform_matrix(first.entity);
  auto transaction = h.session.history().begin_transaction("Explicit Source Conversion");
  const std::array roots{first.entity};
  const auto converted = h.session.instances().create_instance_from_roots_in_transaction(
      transaction, roots);
  ASSERT_TRUE(converted.ok) << converted.message;
  EXPECT_EQ(h.session.scene().containing_instance_definition(first.entity), converted.definition);
  EXPECT_EQ(h.session.scene().containing_instance_definition(unrelated.entity), kInvalidEntityId);
  expect_radial_instance_matrix(radial_member_world(h.session.scene(), converted.occurrence,
                                                     first.entity), original_world);
  ASSERT_TRUE(transaction.commit().ok);
  ASSERT_TRUE(h.editor.modeling().enter_instance_edit(converted.occurrence).ok());
  EXPECT_FALSE(h.session.instances().can_create_instance_from_roots(roots));
  auto forbidden = h.session.history().begin_transaction("Rejected Nested Instance");
  EXPECT_FALSE(h.session.instances().create_instance_from_roots_in_transaction(
      forbidden, roots).ok);
  ASSERT_TRUE(forbidden.cancel().ok);
}

TEST(RadialArrayInstanceTests, MeshHierarchySourcesRejectLinkingAndRetryAsIndependentCopies) {
  for (const bool select_child : {false, true}) {
    SCOPED_TRACE(select_child);
    RadialArrayInstanceHarness h;
    const auto parent = h.box("Parent Mesh Source");
    const auto child = h.box("Child Mesh", {0, 3, 0});
    ASSERT_TRUE(h.editor.scene().reparent(child.entity, parent.entity, false).ok());
    const std::vector<EntityId> sources = select_child
        ? std::vector<EntityId>{parent.entity, child.entity}
        : std::vector<EntityId>{parent.entity};
    h.begin(sources, true);
    const auto entities = h.session.scene().entities();
    const auto meshes = h.session.assets().mesh_ids();
    const auto scene_revision = h.session.scene().revision();
    const auto content_revision = h.session.modeling().content_revision();
    const auto rejected = h.session.radial_array().apply();
    EXPECT_FALSE(rejected.ok) << rejected.message;
    EXPECT_FALSE(rejected.changed) << rejected.message;
    EXPECT_EQ(h.session.scene().entities(), entities);
    EXPECT_EQ(h.session.assets().mesh_ids(), meshes);
    EXPECT_EQ(h.session.scene().revision(), scene_revision);
    EXPECT_EQ(h.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(h.session.scene().parent(child.entity), parent.entity);
    EXPECT_EQ(h.session.scene().containing_instance_definition(parent.entity), kInvalidEntityId);
    EXPECT_EQ(h.session.scene().containing_instance_definition(child.entity), kInvalidEntityId);
    EXPECT_EQ(h.session.scene().selected_entities(), sources);
    EXPECT_EQ(h.session.history().undo_count(), 0u);
    EXPECT_TRUE(h.session.radial_array().active());
    EXPECT_TRUE(h.session.radial_array().has_valid_center());
    EXPECT_TRUE(h.session.radial_array().preview_state().active);
    ASSERT_TRUE(h.session.radial_array().set_create_instances(false).ok);
    h.commit();
    EXPECT_EQ(h.session.scene().selected_entities().size(), sources.size() * 3u);
    EXPECT_EQ(h.session.scene().parent(child.entity), parent.entity);
    for (const EntityId entity : h.session.scene().selected_entities()) {
      if (entity == parent.entity || entity == child.entity) continue;
      EXPECT_EQ(h.session.scene().instance_occurrence(entity), nullptr);
      EXPECT_FALSE(h.session.scene().has_children(entity));
    }
    ASSERT_TRUE(h.editor.history().undo().ok());
    EXPECT_EQ(h.session.scene().entities(), entities);
    EXPECT_EQ(h.session.scene().parent(child.entity), parent.entity);
    EXPECT_EQ(h.session.scene().selected_entities(), sources);
  }
}

} // namespace
} // namespace quader
