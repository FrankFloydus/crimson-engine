////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/transform/mesh_transform_bake_service.hpp"

#include "assets/asset_registry.hpp"
#include "core/i_undoable_command.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_host_publication_transaction.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"

#include <quader/modeling/neutral/polygon/document_model.hpp>
#include <quader/modeling/neutral/polygon/document_operations.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

bool valid_mode(MeshTransformBakeMode mode) {
  switch (mode) {
  case MeshTransformBakeMode::Transforms:
  case MeshTransformBakeMode::Location:
  case MeshTransformBakeMode::Rotation:
  case MeshTransformBakeMode::Scale:
    return true;
  }
  return false;
}

std::string bake_label(MeshTransformBakeMode mode) {
  switch (mode) {
  case MeshTransformBakeMode::Location:
    return "Bake Location";
  case MeshTransformBakeMode::Rotation:
    return "Bake Rotation";
  case MeshTransformBakeMode::Scale:
    return "Bake Scale";
  case MeshTransformBakeMode::Transforms:
    return "Bake Transforms";
  }
  return "Bake Transforms";
}

bool eligible_mesh(const SceneDocument &scene, const AssetRegistry &assets,
                   EntityId entity) {
  if (!scene.exists(entity) || scene.kind(entity) != EntityKind::Mesh ||
      scene.is_effectively_locked(entity) ||
      !scene.is_effectively_visible(entity) ||
      scene.instance_occurrence(entity) != nullptr) {
    return false;
  }
  const TransformComponent *transform = scene.transform(entity);
  const ModelingObjectComponent *object = scene.modeling_object(entity);
  const MeshComponent *binding = scene.mesh(entity);
  const MeshAsset *mesh = binding != nullptr ? assets.mesh(binding->mesh) : nullptr;
  return transform != nullptr && scene_transform_is_finite(*transform) &&
         object != nullptr && object->sdk_owned &&
         object->object_id != kInvalidModelingObjectId && mesh != nullptr &&
         mesh->has_stable_cpu_topology && mesh->has_authored_polygon_document &&
         mesh->authored_polygon_document != nullptr;
}

TransformComponent reset_channels(TransformComponent transform,
                                  MeshTransformBakeMode mode) {
  if (mode == MeshTransformBakeMode::Transforms ||
      mode == MeshTransformBakeMode::Location) {
    transform.position = {};
  }
  if (mode == MeshTransformBakeMode::Transforms ||
      mode == MeshTransformBakeMode::Rotation) {
    transform.rotation_euler = {};
  }
  if (mode == MeshTransformBakeMode::Transforms ||
      mode == MeshTransformBakeMode::Scale) {
    transform.scale = {1.0f, 1.0f, 1.0f};
  }
  return transform;
}

bool same_affine_without_scale_bias(QMat4 actual, QMat4 expected) {
  for (std::size_t column = 0; column < 3u; ++column) {
    const std::size_t offset = column * 4u;
    const double length = std::hypot(
        static_cast<double>(expected.values[offset]),
        static_cast<double>(expected.values[offset + 1u]),
        static_cast<double>(expected.values[offset + 2u]));
    if (!std::isfinite(length) || length == 0.0) {
      return false;
    }
    for (std::size_t row = 0; row < 3u; ++row) {
      actual.values[offset + row] =
          static_cast<float>(actual.values[offset + row] / length);
      expected.values[offset + row] =
          static_cast<float>(expected.values[offset + row] / length);
    }
  }
  return scene_matrix_near_equal(actual, expected);
}

std::optional<QMat4> relative_affine(const QMat4 &new_frame,
                                    const QMat4 &old_frame) {
  if (!scene_matrix_is_finite(new_frame) || !scene_matrix_is_finite(old_frame)) {
    return std::nullopt;
  }
  const glm::dmat4 new_matrix = scene_matrix_to_double(new_frame);
  const glm::dmat4 old_matrix = scene_matrix_to_double(old_frame);
  const glm::dmat3 new_linear{new_matrix};
  const double determinant = glm::determinant(new_linear);
  if (!std::isfinite(determinant) || determinant == 0.0) {
    return std::nullopt;
  }
  const glm::dmat3 inverse_linear = glm::inverse(new_linear);
  const glm::dmat3 linear = inverse_linear * glm::dmat3{old_matrix};
  // Subtract origins before applying the inverse. Equal retained positions
  // cancel exactly even when the scene is far from the coordinate origin.
  const glm::dvec3 translation = inverse_linear *
      (glm::dvec3{old_matrix[3]} - glm::dvec3{new_matrix[3]});
  glm::dmat4 relative{1.0};
  for (glm::length_t column = 0; column < 3; ++column) {
    relative[column] = glm::dvec4{linear[column], 0.0};
  }
  relative[3] = glm::dvec4{translation, 1.0};
  QMat4 result;
  for (glm::length_t column = 0; column < 4; ++column) {
    for (glm::length_t row = 0; row < 4; ++row) {
      const double value = relative[column][row];
      if (!std::isfinite(value) ||
          std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
        return std::nullopt;
      }
      result.values[static_cast<std::size_t>(column * 4 + row)] = static_cast<float>(value);
    }
  }
  return result;
}

modeling::Transform3 sdk_transform(const QMat4 &matrix) {
  const auto column = [&matrix](std::size_t offset) {
    return modeling::Vec3{matrix.values[offset], matrix.values[offset + 1u],
                          matrix.values[offset + 2u]};
  };
  return {.x_axis = column(0u), .y_axis = column(4u),
          .z_axis = column(8u), .origin = column(12u)};
}

/// Retains exact host payloads and copy-on-write identity for one baked mesh.
struct BakedMeshEdit {
  EntityId entity = kInvalidEntityId;
  MeshComponent before_component;
  MeshAsset before_asset;
  MeshAsset after_asset;
  AssetId after_mesh = kInvalidAssetId;
  bool clone_on_write = false;
  std::optional<TextureMappingComponent> before_mapping;
  std::optional<TextureMappingComponent> after_mapping;
};

/// Holds the complete preflighted host mutation for one bake history item.
struct BakePlan {
  std::vector<DocumentTransformEdit> transforms;
  std::vector<BakedMeshEdit> meshes;
  std::vector<EntityId> participants;
};

bool same_uv(QVec2 actual, QVec2 expected) {
  const auto same = [](float a, float b) {
    return std::isfinite(a) && std::isfinite(b) &&
           std::abs(a - b) <=
               0.0001f + 0.00001f * std::max(std::abs(a), std::abs(b));
  };
  return same(actual.x, expected.x) && same(actual.y, expected.y);
}

bool preserves_displayed_uvs(const quader_poly::Document &before,
                             const quader_poly::Document &after,
                             const QMat4 &before_sampling,
                             const QMat4 &after_sampling) {
  if (before.faces.size() != after.faces.size()) {
    return false;
  }
  std::unordered_map<quader_poly::ElementId, const quader_poly::Face *> faces;
  for (const auto &face : before.faces) {
    faces.emplace(face.id, &face);
  }
  std::unordered_map<quader_poly::ElementId, const quader_poly::Vertex *> before_vertices;
  std::unordered_map<quader_poly::ElementId, const quader_poly::Vertex *> after_vertices;
  before_vertices.reserve(before.vertices.size());
  after_vertices.reserve(after.vertices.size());
  for (const auto &vertex : before.vertices) {
    before_vertices.emplace(vertex.id, &vertex);
  }
  for (const auto &vertex : after.vertices) {
    after_vertices.emplace(vertex.id, &vertex);
  }
  const auto source_mapping =
      MeshProjectionService::texture_mapping_transform(before_sampling);
  const auto destination_mapping =
      MeshProjectionService::texture_mapping_transform(after_sampling);
  for (const auto &face : after.faces) {
    const auto found = faces.find(face.id);
    if (found == faces.end()) {
      return false;
    }
    const auto &source = *found->second;
    if (source.vertices.size() != face.vertices.size() ||
        source.uvs.size() != source.vertices.size() ||
        face.uvs.size() != face.vertices.size()) {
      return false;
    }
    const QVec3 source_normal = quader_poly::face_normal(before, source);
    const QVec3 destination_normal = quader_poly::face_normal(after, face);
    const auto source_carrier =
        quader_poly::face_texture_carrier(before, source, source_normal);
    const auto destination_carrier =
        quader_poly::face_texture_carrier(after, face, destination_normal);
    std::unordered_map<quader_poly::ElementId, QVec2> source_uvs;
    for (std::size_t corner = 0; corner < source.vertices.size(); ++corner) {
      const auto vertex_entry = before_vertices.find(source.vertices[corner]);
      if (vertex_entry == before_vertices.end()) {
        return false;
      }
      const auto *vertex = vertex_entry->second;
      source_uvs.emplace(
          vertex->id, quader_poly::evaluate_texture_mapping(
                          quader_poly::apply_face_texture_transform(
                              source.uvs[corner], source.texture_transform),
                          vertex->position, source_normal, source_carrier,
                          source_mapping));
    }
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const auto vertex_entry = after_vertices.find(face.vertices[corner]);
      const auto uv = source_uvs.find(face.vertices[corner]);
      if (vertex_entry == after_vertices.end() || uv == source_uvs.end()) {
        return false;
      }
      const auto *vertex = vertex_entry->second;
      if (!same_uv(quader_poly::evaluate_texture_mapping(
                       quader_poly::apply_face_texture_transform(
                           face.uvs[corner], face.texture_transform),
                       vertex->position, destination_normal, destination_carrier,
                       destination_mapping),
                   uv->second)) {
        return false;
      }
    }
  }
  return true;
}

std::optional<BakedMeshEdit> prepare_mesh(
    const SceneDocument &scene, const AssetRegistry &assets, EntityId entity,
    const QMat4 &bake_transform, std::string &error) {
  const MeshComponent *binding = scene.mesh(entity);
  const MeshAsset *asset = binding != nullptr ? assets.mesh(binding->mesh) : nullptr;
  if (asset == nullptr || asset->authored_polygon_document == nullptr) {
    error = "Baked mesh authored geometry is unavailable.";
    return std::nullopt;
  }
  const auto inverse_bake = try_scene_matrix_inverse(bake_transform);
  if (!inverse_bake.has_value()) {
    error = "Baking requires an invertible mesh transform.";
    return std::nullopt;
  }
  BakedMeshEdit edit{
      .entity = entity,
      .before_component = *binding,
      .before_asset = *asset,
      .clone_on_write = scene.mesh_binding_reference_count(binding->mesh) > 1u,
  };
  const TextureMappingComponent *mapping = scene.texture_mapping(entity);
  const QMat4 before_sampling = mapping != nullptr ? mapping->sampling_from_local : QMat4{};
  const QMat4 after_sampling = mapping != nullptr
      ? scene_transform_multiply(bake_transform, scene_transform_multiply(
                                                    before_sampling, *inverse_bake))
      : QMat4{};
  if (mapping != nullptr) {
    edit.before_mapping = *mapping;
    edit.after_mapping = TextureMappingComponent{after_sampling};
  }
  if (!texture_mapping_is_valid({before_sampling}) ||
      !texture_mapping_is_valid({after_sampling})) {
    error = "Baked mesh texture placement would become invalid.";
    return std::nullopt;
  }
  quader_poly::Document before = *asset->authored_polygon_document;
  quader_poly::ensure_face_uvs(before);
  auto document = MeshProjectionService::sdk_document_from_polygon_document(before);
  if (!document.ok()) {
    error = document.error_message();
    return std::nullopt;
  }
  auto selected = document.value().select_all_vertices();
  if (!selected.ok()) {
    error = selected.error_message();
    return std::nullopt;
  }
  auto transformed = document.value().transform_selection(sdk_transform(bake_transform));
  if (!transformed.ok() || !transformed.value().success) {
    error = transformed.error_message().empty()
                ? "Mesh geometry could not be baked."
                : transformed.error_message();
    return std::nullopt;
  }
  const bool reflected = glm::determinant(glm::dmat3{
                             scene_matrix_to_double(bake_transform)}) < 0.0;
  auto preserved = MeshProjectionService::rebase_sdk_document_texture_mapping(
      before, document.value(), before_sampling, after_sampling, reflected);
  if (!preserved.ok()) {
    error = preserved.error_message();
    return std::nullopt;
  }
  const auto after = MeshProjectionService::polygon_document_from_sdk_document(preserved.value());
  if (!after.has_value() ||
      !preserves_displayed_uvs(before, *after, before_sampling, after_sampling)) {
    error = "Texture placement cannot be preserved for this baked mesh.";
    return std::nullopt;
  }
  auto compiled = MeshProjectionService::compile_sdk_document_mesh(preserved.value());
  if (!compiled.has_value()) {
    error = "Baked mesh could not be compiled.";
    return std::nullopt;
  }
  compiled->name = asset->name;
  if (!assets.validate_mesh(*compiled).ok) {
    error = "Baked mesh geometry is invalid.";
    return std::nullopt;
  }
  edit.after_asset = std::move(*compiled);
  edit.after_mesh = edit.clone_on_write ? kInvalidAssetId : binding->mesh;
  return edit;
}

/// Resolves final hierarchy placements without temporarily mutating the scene.
class BakePlanner {
public:
  BakePlanner(const SceneDocument &scene, const AssetRegistry &assets,
              std::span<const EntityId> selected, MeshTransformBakeMode mode)
      : scene_(scene), assets_(assets), selected_(selected.begin(), selected.end()),
        mode_(mode) {}

  std::optional<BakePlan> prepare(std::string &error) {
    for (const EntityId entity : selected_) {
      if (!resolve_world(entity, error).has_value()) {
        return std::nullopt;
      }
      for (const EntityId child : scene_.children(entity)) {
        if (!resolve_world(child, error).has_value()) {
          return std::nullopt;
        }
      }
    }
    BakePlan plan;
    for (const auto &[entity, after] : changed_locals_) {
      const TransformComponent before = *scene_.transform(entity);
      if (!document_command_same_transform(before, after)) {
        plan.transforms.push_back({.entity = entity, .before = before, .after = after});
      }
    }
    if (plan.transforms.empty()) {
      return plan;
    }
    for (const EntityId entity : selected_) {
      const QMat4 before_world = scene_.world_transform_matrix(entity);
      const QMat4 after_world = worlds_.at(entity);
      if (before_world.values != after_world.values ||
          !document_command_same_transform(*scene_.transform(entity),
                                             changed_locals_.at(entity))) {
        const EntityId parent = scene_.parent(entity);
        const bool parent_unchanged = parent == kInvalidEntityId ||
            worlds_.at(parent).values == scene_.world_transform_matrix(parent).values;
        const auto relative = parent_unchanged
            ? relative_affine(compose_transform(changed_locals_.at(entity).spatial_transform()),
                              compose_transform(scene_.transform(entity)->spatial_transform()))
            : relative_affine(after_world, before_world);
        if (!relative.has_value()) {
          error = "Baking requires an invertible remaining object transform.";
          return std::nullopt;
        }
        auto mesh = prepare_mesh(scene_, assets_, entity, *relative, error);
        if (!mesh.has_value()) {
          return std::nullopt;
        }
        plan.meshes.push_back(std::move(*mesh));
      }
      const auto descendants = scene_.descendants(entity, true);
      plan.participants.insert(plan.participants.end(), descendants.begin(), descendants.end());
    }
    std::ranges::sort(plan.transforms, {}, &DocumentTransformEdit::entity);
    std::ranges::sort(plan.meshes, {}, &BakedMeshEdit::entity);
    std::ranges::sort(plan.participants);
    plan.participants.erase(std::unique(plan.participants.begin(), plan.participants.end()),
                            plan.participants.end());
    return plan;
  }

private:
  std::optional<QMat4> resolve_world(EntityId entity, std::string &error) {
    if (entity == kInvalidEntityId) {
      return QMat4{};
    }
    if (const auto found = worlds_.find(entity); found != worlds_.end()) {
      return found->second;
    }
    const auto parent_world = resolve_world(scene_.parent(entity), error);
    if (!parent_world.has_value()) {
      return std::nullopt;
    }
    const TransformComponent *before = scene_.transform(entity);
    TransformComponent after = before != nullptr ? *before : TransformComponent{};
    if (selected_.contains(entity)) {
      after = reset_channels(after, mode_);
      changed_locals_[entity] = after;
    } else if (selected_.contains(scene_.parent(entity)) &&
               parent_world->values !=
                   scene_.world_transform_matrix(scene_.parent(entity)).values) {
      const QMat4 original_world = scene_.world_transform_matrix(entity);
      const auto requested = relative_affine(*parent_world, original_world);
      auto compensated = requested.has_value()
          ? try_transform_component_from_matrix(*requested, after.rotation_euler)
          : std::nullopt;
      if (!requested.has_value() || !compensated.has_value() ||
          !same_affine_without_scale_bias(
              compose_transform(compensated->spatial_transform()),
              *requested)) {
        error = "Baking would require an unsupported sheared child transform.";
        return std::nullopt;
      }
      if (before == nullptr) {
        error = "Baking cannot compensate a child without an authored transform.";
        return std::nullopt;
      }
      compensated->custom_pivot = after.custom_pivot;
      after = *compensated;
      changed_locals_[entity] = after;
    }
    const QMat4 world = scene_transform_multiply(
        *parent_world, compose_transform(after.spatial_transform()));
    if (!scene_matrix_is_finite(world) || !scene_transform_is_finite(after)) {
      error = "Baked hierarchy transform is invalid.";
      return std::nullopt;
    }
    worlds_.emplace(entity, world);
    return world;
  }

  const SceneDocument &scene_;
  const AssetRegistry &assets_;
  std::unordered_set<EntityId> selected_;
  MeshTransformBakeMode mode_;
  std::unordered_map<EntityId, TransformComponent> changed_locals_;
  std::unordered_map<EntityId, QMat4> worlds_;
};

/// Publishes an already validated mesh bake and replays its exact stored history.
class BakeMeshTransformsCommand final : public IUndoableCommand {
public:
  BakeMeshTransformsCommand(SceneDocument &scene, AssetRegistry &assets,
                            ModelingSessionService &modeling, BakePlan plan,
                            std::string label)
      : scene_(scene), assets_(assets), modeling_(modeling),
        plan_(std::move(plan)), label_(std::move(label)) {}

  const std::string &label() const override { return label_; }
  CommandResult undo() override { return apply(false); }
  CommandResult redo() override { return apply(true); }

private:
  CommandResult apply(bool after) {
    if (modeling_.operation_preview_active()) {
      return command_invalid_result("Commit or cancel the active preview first.");
    }
    std::vector<AssetId> asset_ids;
    for (const auto &mesh : plan_.meshes) {
      asset_ids.push_back(mesh.before_component.mesh);
      if (mesh.after_mesh != kInvalidAssetId && assets_.mesh(mesh.after_mesh) != nullptr) {
        asset_ids.push_back(mesh.after_mesh);
      }
    }
    std::ranges::sort(asset_ids);
    asset_ids.erase(std::unique(asset_ids.begin(), asset_ids.end()), asset_ids.end());
    ModelingHostPublicationTransaction publication(
        scene_, assets_, ModelingHostPublicationTransaction::RestoreHostState{},
        {.sdk_rollback = ModelingSdkRollbackPolicy::None,
         .checkpoint_scope = ModelingHostPublicationCheckpointScope::MeshProjection,
         .capture_assets = true,
         .capture_scene = true,
         .capture_scene_entities = true,
         .capture_bindings = false,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = plan_.participants,
         .affected_asset_ids = asset_ids});
    if (!publication.ready()) {
      return command_invalid_result("Bake publication checkpoint could not be captured.");
    }
    const auto failure = [&publication](std::string message) {
      CommandResult result = command_invalid_result(std::move(message));
      if (!publication.rollback()) {
        result.changed = true;
        result.message += "; host publication rollback failed.";
      }
      return result;
    };
    for (auto &mesh : plan_.meshes) {
      if (!scene_.exists(mesh.entity) || !apply_mesh(mesh, after, publication)) {
        return failure("Baked mesh asset could not be published.");
      }
    }
    std::vector<SceneTransformAssignment> transforms;
    transforms.reserve(plan_.transforms.size());
    for (const auto &edit : plan_.transforms) {
      transforms.push_back({.entity = edit.entity, .transform = after ? edit.after : edit.before});
    }
    std::vector<SceneTextureMappingAssignment> mappings;
    std::vector<EntityId> modeling_entities;
    for (const auto &mesh : plan_.meshes) {
      mappings.push_back({.entity = mesh.entity,
                          .mapping = after ? mesh.after_mapping : mesh.before_mapping});
      modeling_entities.push_back(mesh.entity);
    }
    if (!scene_.set_transforms_and_texture_mappings(transforms, mappings)) {
      return failure("Baked mesh placement could not be published.");
    }
    if (!modeling_entities.empty()) {
      const auto restored = modeling_.restore_mesh_documents_from_scene(modeling_entities);
      if (!restored.ok) {
        return failure(restored.message.empty()
                           ? "Baked modeling documents could not be restored."
                           : restored.message);
      }
    }
    // Targeted SDK restore is already atomic and complete at this boundary.
    // As with pivot rebasing, retain that branch if host finalization refuses.
    if (!publication.commit()) {
      return {.ok = false, .changed = true,
              .message = "Bake publication could not be finalized."};
    }
    return command_changed_result();
  }

  bool apply_mesh(BakedMeshEdit &mesh, bool after,
                   ModelingHostPublicationTransaction &publication) {
    if (!mesh.clone_on_write) {
      return assets_.replace_mesh(mesh.before_component.mesh,
                                  after ? mesh.after_asset : mesh.before_asset);
    }
    if (!after) {
      return scene_.set_mesh(mesh.entity, mesh.before_component) &&
             (mesh.after_mesh == kInvalidAssetId || assets_.remove_mesh(mesh.after_mesh));
    }
    if (mesh.after_mesh == kInvalidAssetId) {
      mesh.after_mesh = assets_.add_mesh(mesh.after_asset);
      if (mesh.after_mesh == kInvalidAssetId) {
        return false;
      }
      publication.track_created_asset(mesh.after_mesh);
    } else if (assets_.mesh(mesh.after_mesh) == nullptr) {
      if (assets_.add_mesh(mesh.after_mesh, mesh.after_asset) == kInvalidAssetId) {
        return false;
      }
      publication.track_created_asset(mesh.after_mesh);
    } else if (!assets_.replace_mesh(mesh.after_mesh, mesh.after_asset)) {
      return false;
    }
    MeshComponent component = mesh.before_component;
    component.mesh = mesh.after_mesh;
    return scene_.set_mesh(mesh.entity, component);
  }

  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingSessionService &modeling_;
  BakePlan plan_;
  std::string label_;
};

} // namespace

MeshTransformBakeService::MeshTransformBakeService(
    SceneDocument &scene, AssetRegistry &assets, EditorHistoryService &history,
    ModelingSessionService &modeling)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling) {}

bool MeshTransformBakeService::can_bake(
    const EditorSelectionService &selection, MeshTransformBakeMode mode) const {
  if (!valid_mode(mode) || selection.mode() != ModelingSelectionMode::Object ||
      modeling_.operation_preview_active()) {
    return false;
  }
  const auto entities = scene_.selected_entities();
  return !entities.empty() && std::ranges::all_of(entities, [&](EntityId entity) {
    return eligible_mesh(scene_, assets_, entity);
  });
}

CommandResult MeshTransformBakeService::bake(
    const EditorSelectionService &selection, MeshTransformBakeMode mode) {
  if (!can_bake(selection, mode)) {
    return command_invalid_result("Select editable unlocked meshes in Mesh selection mode to bake transforms.");
  }
  const auto entities = scene_.selected_entities();
  std::string error;
  BakePlanner planner(scene_, assets_, entities, mode);
  auto plan = planner.prepare(error);
  if (!plan.has_value()) {
    return command_invalid_result(std::move(error));
  }
  if (plan->transforms.empty() && plan->meshes.empty()) {
    return command_unchanged_result();
  }
  return history_.execute(std::make_unique<BakeMeshTransformsCommand>(
      scene_, assets_, modeling_, std::move(*plan), bake_label(mode)));
}

} // namespace quader
