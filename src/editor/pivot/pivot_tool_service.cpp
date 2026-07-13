////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/pivot/pivot_tool_service.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "core/i_undoable_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "math/glm_bridge.hpp"
#include "mesh/polygon/document_model.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform_pivot.hpp"
#include "scene/scene_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kPivotPositionEpsilon = 0.00001f;

bool near_same_point(QVec3 left, QVec3 right) {
  return length_squared(left - right) <=
         kPivotPositionEpsilon * kPivotPositionEpsilon;
}

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool mesh_has_editable_finite_geometry(const MeshAsset &mesh) {
  if (!mesh.has_stable_cpu_topology || mesh.vertices.empty() ||
      !std::all_of(mesh.vertices.begin(), mesh.vertices.end(),
                   [](const MeshVertex &vertex) {
                     return finite_vector(vertex.position);
                   })) {
    return false;
  }
  return mesh.authored_polygon_document == nullptr ||
         std::all_of(mesh.authored_polygon_document->vertices.begin(),
                     mesh.authored_polygon_document->vertices.end(),
                     [](const quader_poly::Vertex &vertex) {
                       return finite_vector(vertex.position);
                     });
}

bool invertible_world_matrix(const QMat4 &matrix) {
  if (!scene_matrix_is_finite(matrix)) {
    return false;
  }
  return scene_matrix_is_finite(inverse(matrix));
}

bool pivot_rebase_environment_is_valid(const SceneDocument &scene,
                                       EntityId target) {
  if (!invertible_world_matrix(scene.world_transform_matrix(target))) {
    return false;
  }
  for (const EntityId child : scene.children(target)) {
    const TransformComponent *child_transform = scene.transform(child);
    if (child_transform != nullptr &&
        (!scene_transform_is_finite(*child_transform) ||
         !scene_matrix_is_finite(scene.world_transform_matrix(child)))) {
      return false;
    }
  }
  return true;
}

bool same_transform(const TransformComponent &left,
                    const TransformComponent &right) {
  return left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.z == right.position.z &&
         left.rotation_euler.x == right.rotation_euler.x &&
         left.rotation_euler.y == right.rotation_euler.y &&
         left.rotation_euler.z == right.rotation_euler.z &&
         left.scale.x == right.scale.x && left.scale.y == right.scale.y &&
         left.scale.z == right.scale.z &&
         left.custom_pivot == right.custom_pivot;
}

/**
 * Stores before/after transforms changed by one pivot rebase command.
 */
struct PivotTransformEdit {
    EntityId entity = kInvalidEntityId;
    TransformComponent before;
    TransformComponent after;
};

/**
 * Stores mesh asset/component edits required to compensate a mesh origin move.
 */
struct PivotMeshEdit {
    bool active = false;
    bool clone_on_write = false;
    EntityId entity = kInvalidEntityId;
    AssetId before_mesh = kInvalidAssetId;
    AssetId after_mesh = kInvalidAssetId;
    MeshComponent before_component;
    MeshAsset before_asset;
    MeshAsset after_asset;
};

/**
 * Collects all transform and mesh edits that make one pivot apply undoable.
 */
struct PivotRebasePlan {
    std::vector<PivotTransformEdit> transforms;
    PivotMeshEdit mesh;
};

/**
 * Describes one target origin and its resolved destination in world space.
 */
struct PivotRebaseRequest {
  EntityId target = kInvalidEntityId;
  QVec3 world_pivot = {};
};

void offset_mesh_vertices(MeshAsset &mesh, QVec3 local_delta) {
  for (MeshVertex &vertex : mesh.vertices) {
    vertex.position = vertex.position - local_delta;
  }
  if (mesh.authored_polygon_document != nullptr) {
    mesh.authored_polygon_document =
        std::make_shared<quader_poly::Document>(
            *mesh.authored_polygon_document);
    for (quader_poly::Vertex &vertex :
         mesh.authored_polygon_document->vertices) {
      vertex.position = vertex.position - local_delta;
    }
  }
}

int mesh_usage_count(const SceneDocument &scene, AssetId mesh) {
  int count = 0;
  for (const EntityId entity : scene.entities()) {
    const MeshComponent *component = scene.mesh(entity);
    if (component != nullptr && component->mesh == mesh) {
      ++count;
    }
  }
  return count;
}

std::optional<std::string>
prepare_mesh_edit(const SceneDocument &scene, const AssetRegistry &assets,
                  EntityId target, QVec3 local_delta,
                  PivotMeshEdit &edit) {
  const MeshComponent *component = scene.mesh(target);
  if (component == nullptr) {
    return std::nullopt;
  }
  const MeshAsset *mesh = assets.mesh(component->mesh);
  if (mesh == nullptr) {
    return std::string("Pivot target mesh asset does not exist.");
  }
  if (!mesh_has_editable_finite_geometry(*mesh)) {
    return std::string("Pivot target mesh does not have editable CPU mesh data.");
  }

  edit.active = true;
  edit.entity = target;
  edit.before_mesh = component->mesh;
  edit.before_component = *component;
  edit.before_asset = *mesh;
  edit.after_asset = *mesh;
  offset_mesh_vertices(edit.after_asset, local_delta);
  edit.clone_on_write = mesh_usage_count(scene, component->mesh) > 1;
  edit.after_mesh = edit.clone_on_write ? kInvalidAssetId : component->mesh;
  return std::nullopt;
}

std::optional<PivotRebasePlan>
make_rebase_plan(const SceneDocument &scene, const AssetRegistry &assets,
                 EntityId target, QVec3 pivot,
                 std::string &error_message) {
  if (!finite_vector(pivot)) {
    error_message = "Pivot position is not finite.";
    return std::nullopt;
  }
  if (!scene.exists(target)) {
    error_message = "Pivot target no longer exists.";
    return std::nullopt;
  }
  const TransformComponent *target_before = scene.transform(target);
  if (target_before == nullptr) {
    error_message = "Pivot target has no transform.";
    return std::nullopt;
  }

  const QMat4 target_world_before = scene.world_transform_matrix(target);
  if (!invertible_world_matrix(target_world_before)) {
    error_message = "Pivot target world transform is not invertible.";
    return std::nullopt;
  }
  const QVec3 world_delta =
      pivot - transform_point(target_world_before, QVec3{});
  const QVec3 target_local_delta =
      transform_vector(inverse(target_world_before), world_delta);
  if (!finite_vector(target_local_delta)) {
    error_message = "Pivot target world transform is not invertible.";
    return std::nullopt;
  }
  PivotRebasePlan plan;

  const EntityId parent = scene.parent(target);
  const QMat4 parent_world = parent == kInvalidEntityId
                                 ? scene_transform_identity_matrix()
                                 : scene.world_transform_matrix(parent);
  if (!invertible_world_matrix(parent_world)) {
    error_message = "Pivot target parent transform is not invertible.";
    return std::nullopt;
  }
  const QVec3 parent_local_delta =
      transform_vector(inverse(parent_world), world_delta);
  if (!finite_vector(parent_local_delta)) {
    error_message = "Pivot target parent transform is not invertible.";
    return std::nullopt;
  }
  TransformComponent target_after_transform = *target_before;
  target_after_transform.position += parent_local_delta;
  target_after_transform.custom_pivot = true;
  const QMat4 target_world_after = scene_transform_multiply(
      parent_world, compose_transform(target_after_transform.spatial_transform()));
  if (!scene_transform_is_finite(target_after_transform) ||
      !scene_matrix_is_finite(target_world_after)) {
    error_message = "Pivot target transform would become invalid.";
    return std::nullopt;
  }
  if (!same_transform(*target_before, target_after_transform)) {
    plan.transforms.push_back(PivotTransformEdit{
        .entity = target,
        .before = *target_before,
        .after = target_after_transform,
    });
  }

  for (const EntityId child : scene.children(target)) {
    const TransformComponent *child_before = scene.transform(child);
    if (child_before == nullptr) {
      continue;
    }
    TransformComponent compensated = *child_before;
    compensated.position = compensated.position - target_local_delta;
    const QMat4 child_world_after = scene_transform_multiply(
        target_world_after, compose_transform(compensated.spatial_transform()));
    if (!scene_transform_is_finite(compensated) ||
        !scene_matrix_is_finite(child_world_after)) {
      error_message = "Pivot child transform could not be compensated.";
      return std::nullopt;
    }
    if (!same_transform(*child_before, compensated)) {
      plan.transforms.push_back(PivotTransformEdit{
          .entity = child,
          .before = *child_before,
          .after = compensated,
      });
    }
  }

  if (scene.mesh(target) != nullptr &&
      (target_local_delta.x != 0.0f || target_local_delta.y != 0.0f ||
       target_local_delta.z != 0.0f)) {
    if (const std::optional<std::string> mesh_error =
            prepare_mesh_edit(scene, assets, target, target_local_delta,
                              plan.mesh)) {
      error_message = *mesh_error;
      return std::nullopt;
    }
  }

  if (plan.transforms.empty() && !plan.mesh.active) {
    error_message.clear();
    return std::nullopt;
  }
  return plan;
}

void apply_transforms_before(SceneDocument &scene,
                             const PivotRebasePlan &plan) {
  std::vector<SceneTransformAssignment> assignments;
  assignments.reserve(plan.transforms.size());
  for (const PivotTransformEdit &edit : plan.transforms) {
    assignments.push_back(SceneTransformAssignment{
        .entity = edit.entity,
        .transform = edit.before,
    });
  }
  scene.set_transforms(assignments);
}

void apply_transforms_after(SceneDocument &scene,
                            const PivotRebasePlan &plan) {
  std::vector<SceneTransformAssignment> assignments;
  assignments.reserve(plan.transforms.size());
  for (const PivotTransformEdit &edit : plan.transforms) {
    assignments.push_back(SceneTransformAssignment{
        .entity = edit.entity,
        .transform = edit.after,
    });
  }
  scene.set_transforms(assignments);
}

bool apply_mesh_before(SceneDocument &scene, AssetRegistry &assets,
                       PivotRebasePlan &plan) {
  if (!plan.mesh.active) {
    return true;
  }
  if (plan.mesh.clone_on_write) {
    scene.set_mesh(plan.mesh.entity, plan.mesh.before_component);
    if (plan.mesh.after_mesh != kInvalidAssetId) {
      (void)assets.remove_mesh(plan.mesh.after_mesh);
    }
    return true;
  }
  return assets.replace_mesh(plan.mesh.before_mesh, plan.mesh.before_asset);
}

bool apply_mesh_after(SceneDocument &scene, AssetRegistry &assets,
                      PivotRebasePlan &plan) {
  if (!plan.mesh.active) {
    return true;
  }
  if (!plan.mesh.clone_on_write) {
    return assets.replace_mesh(plan.mesh.before_mesh, plan.mesh.after_asset);
  }

  if (plan.mesh.after_mesh == kInvalidAssetId) {
    plan.mesh.after_mesh = assets.add_mesh(plan.mesh.after_asset);
    if (plan.mesh.after_mesh == kInvalidAssetId) {
      return false;
    }
  } else if (assets.mesh(plan.mesh.after_mesh) == nullptr) {
    if (assets.add_mesh(plan.mesh.after_mesh, plan.mesh.after_asset) ==
        kInvalidAssetId) {
      return false;
    }
  } else if (!assets.replace_mesh(plan.mesh.after_mesh,
                                  plan.mesh.after_asset)) {
    return false;
  }

  MeshComponent after_component = plan.mesh.before_component;
  after_component.mesh = plan.mesh.after_mesh;
  scene.set_mesh(plan.mesh.entity, after_component);
  return true;
}

bool apply_plan_before(SceneDocument &scene, AssetRegistry &assets,
                       PivotRebasePlan &plan) {
  if (!apply_mesh_before(scene, assets, plan)) {
    return false;
  }
  apply_transforms_before(scene, plan);
  return true;
}

bool apply_plan_after(SceneDocument &scene, AssetRegistry &assets,
                      PivotRebasePlan &plan) {
  if (!apply_mesh_after(scene, assets, plan)) {
    return false;
  }
  apply_transforms_after(scene, plan);
  return true;
}

/**
 * Applies one or more hierarchy-sensitive origin rebases as one history item.
 */
class PivotRebaseCommand final : public IUndoableCommand {
public:
  PivotRebaseCommand(SceneDocument &scene, AssetRegistry &assets,
                     ModelingSessionService &modeling,
                     std::vector<PivotRebaseRequest> requests,
                     std::string label)
      : scene_(scene), assets_(assets), modeling_(modeling),
        requests_(std::move(requests)), label_(std::move(label)) {}

  const std::string &label() const override { return label_; }

  CommandResult undo() override {
    std::vector<std::size_t> undone;
    undone.reserve(plans_.size());
    for (std::size_t index = plans_.size(); index > 0u; --index) {
      const std::size_t plan_index = index - 1u;
      if (!apply_plan_before(scene_, assets_, plans_[plan_index])) {
        for (auto rollback = undone.rbegin(); rollback != undone.rend();
             ++rollback) {
          if (!apply_plan_after(scene_, assets_, plans_[*rollback])) {
            modeling_.reset_from_scene();
            return command_invalid_result(
                "Unable to restore pivot mesh asset; rollback failed.");
          }
        }
        modeling_.reset_from_scene();
        return command_invalid_result("Unable to restore pivot mesh asset.");
      }
      undone.push_back(plan_index);
    }
    modeling_.reset_from_scene();
    return plans_.empty() ? command_unchanged_result()
                          : command_changed_result();
  }

  CommandResult redo() override {
    return initialized_ ? redo_resolved_plans() : resolve_and_apply_plans();
  }

private:
  CommandResult resolve_and_apply_plans() {
    plans_.clear();
    plans_.reserve(requests_.size());
    for (const PivotRebaseRequest &request : requests_) {
      std::string error_message;
      std::optional<PivotRebasePlan> plan = make_rebase_plan(
          scene_, assets_, request.target, request.world_pivot, error_message);
      if (!plan.has_value()) {
        if (error_message.empty()) {
          continue;
        }
        return rollback_initial_apply(std::move(error_message));
      }
      if (!apply_plan_after(scene_, assets_, *plan)) {
        return rollback_initial_apply("Unable to apply pivot mesh asset.");
      }
      plans_.push_back(std::move(*plan));
    }

    initialized_ = true;
    modeling_.reset_from_scene();
    return plans_.empty() ? command_unchanged_result()
                          : command_changed_result();
  }

  CommandResult rollback_initial_apply(std::string error_message) {
    for (std::size_t index = plans_.size(); index > 0u; --index) {
      if (!apply_plan_before(scene_, assets_, plans_[index - 1u])) {
        plans_.clear();
        modeling_.reset_from_scene();
        return command_invalid_result(std::move(error_message) +
                                      "; rollback failed.");
      }
    }
    plans_.clear();
    modeling_.reset_from_scene();
    return command_invalid_result(std::move(error_message));
  }

  CommandResult redo_resolved_plans() {
    std::vector<std::size_t> redone;
    redone.reserve(plans_.size());
    for (std::size_t index = 0u; index < plans_.size(); ++index) {
      if (!apply_plan_after(scene_, assets_, plans_[index])) {
        for (auto rollback = redone.rbegin(); rollback != redone.rend();
             ++rollback) {
          if (!apply_plan_before(scene_, assets_, plans_[*rollback])) {
            modeling_.reset_from_scene();
            return command_invalid_result(
                "Unable to apply pivot mesh asset; rollback failed.");
          }
        }
        modeling_.reset_from_scene();
        return command_invalid_result("Unable to apply pivot mesh asset.");
      }
      redone.push_back(index);
    }
    modeling_.reset_from_scene();
    return plans_.empty() ? command_unchanged_result()
                          : command_changed_result();
  }

  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingSessionService &modeling_;
  std::vector<PivotRebaseRequest> requests_;
  std::vector<PivotRebasePlan> plans_;
  std::string label_;
  bool initialized_ = false;
};

/**
 * Stores the geometry-derived frame used to transfer a pivot offset.
 */
struct PivotGeometryFrame {
  QVec3 world_origin = {};
  QVec3 world_bounds_center = {};
  QVec3 world_hybrid_center = {};
  glm::mat3 world_rotation{1.0f};
};

/**
 * Stores a fully validated copy-pivot selection and its target requests.
 */
struct CopyPivotSelection {
  bool ok = false;
  std::string message;
  std::vector<PivotRebaseRequest> requests;
};

bool finite_value(float value) { return std::isfinite(value); }

bool finite_matrix(const QMat4 &matrix) {
  return std::all_of(matrix.values.begin(), matrix.values.end(),
                     [](float value) { return finite_value(value); });
}

bool finite_quaternion(const glm::dquat &value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::isfinite(value.w);
}

std::optional<glm::dquat>
rotation_quaternion_from_normalized_basis(const glm::dmat3 &basis) {
  const double m00 = basis[0][0];
  const double m11 = basis[1][1];
  const double m22 = basis[2][2];
  double w = 0.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  const auto signed_root = [](double trace, bool negative)
      -> std::optional<double> {
    if (!std::isfinite(trace) || trace <= 0.0) {
      return std::nullopt;
    }
    const double root = 2.0 * std::sqrt(trace);
    return negative ? -root : root;
  };

  std::optional<double> scale;
  if (m22 < 0.0) {
    if (m00 > m11) {
      scale = signed_root(1.0 + m00 - m11 - m22,
                          basis[1][2] < basis[2][1]);
      if (!scale.has_value()) {
        return std::nullopt;
      }
      x = 0.25 * *scale;
      w = (basis[1][2] - basis[2][1]) / *scale;
      y = (basis[0][1] + basis[1][0]) / *scale;
      z = (basis[2][0] + basis[0][2]) / *scale;
    } else {
      scale = signed_root(1.0 - m00 + m11 - m22,
                          basis[2][0] < basis[0][2]);
      if (!scale.has_value()) {
        return std::nullopt;
      }
      y = 0.25 * *scale;
      w = (basis[2][0] - basis[0][2]) / *scale;
      x = (basis[0][1] + basis[1][0]) / *scale;
      z = (basis[1][2] + basis[2][1]) / *scale;
    }
  } else if (m00 < -m11) {
    scale = signed_root(1.0 - m00 - m11 + m22,
                        basis[0][1] < basis[1][0]);
    if (!scale.has_value()) {
      return std::nullopt;
    }
    z = 0.25 * *scale;
    w = (basis[0][1] - basis[1][0]) / *scale;
    x = (basis[2][0] + basis[0][2]) / *scale;
    y = (basis[1][2] + basis[2][1]) / *scale;
  } else {
    scale = signed_root(1.0 + m00 + m11 + m22, false);
    if (!scale.has_value()) {
      return std::nullopt;
    }
    w = 0.25 * *scale;
    x = (basis[1][2] - basis[2][1]) / *scale;
    y = (basis[2][0] - basis[0][2]) / *scale;
    z = (basis[0][1] - basis[1][0]) / *scale;
  }

  glm::dquat rotation{w, x, y, z};
  if (!finite_quaternion(rotation) || glm::dot(rotation, rotation) <= 0.0) {
    return std::nullopt;
  }
  return glm::normalize(rotation);
}

std::optional<glm::mat3> pure_world_rotation(const QMat4 &world_matrix) {
  if (!finite_matrix(world_matrix)) {
    return std::nullopt;
  }

  const glm::mat4 world = to_glm(world_matrix);
  glm::dmat3 normalized_basis;
  for (glm::length_t column = 0; column < 3; ++column) {
    const glm::dvec3 basis_vector{world[column]};
    const double basis_length = glm::length(basis_vector);
    if (!std::isfinite(basis_length) || basis_length == 0.0) {
      return std::nullopt;
    }
    normalized_basis[column] = basis_vector / basis_length;
  }
  const double determinant = glm::determinant(normalized_basis);
  if (!std::isfinite(determinant) || determinant == 0.0) {
    return std::nullopt;
  }
  if (determinant < 0.0) {
    normalized_basis = -normalized_basis;
  }
  const std::optional<glm::dquat> orientation =
      rotation_quaternion_from_normalized_basis(normalized_basis);
  if (!orientation.has_value()) {
    return std::nullopt;
  }

  const glm::mat3 rotation = glm::mat3(glm::mat3_cast(*orientation));
  for (glm::length_t column = 0; column < 3; ++column) {
    for (glm::length_t row = 0; row < 3; ++row) {
      if (!finite_value(rotation[column][row])) {
        return std::nullopt;
      }
    }
  }
  return rotation;
}

std::optional<QVec3> local_mean_vertex(const MeshAsset &mesh) {
  QVec3 sum;
  std::size_t count = 0u;
  if (mesh.authored_polygon_document != nullptr &&
      !mesh.authored_polygon_document->vertices.empty()) {
    count = mesh.authored_polygon_document->vertices.size();
    for (const quader_poly::Vertex &vertex :
         mesh.authored_polygon_document->vertices) {
      if (!finite_vector(vertex.position)) {
        return std::nullopt;
      }
      sum += vertex.position;
    }
  } else {
    count = mesh.vertices.size();
    for (const MeshVertex &vertex : mesh.vertices) {
      if (!finite_vector(vertex.position)) {
        return std::nullopt;
      }
      sum += vertex.position;
    }
  }
  if (count == 0u) {
    return std::nullopt;
  }
  return sum / static_cast<float>(count);
}

std::optional<PivotGeometryFrame>
pivot_geometry_frame(const SceneDocument &scene, const AssetRegistry &assets,
                     EntityId entity, std::string_view role,
                     std::string &error_message) {
  const TransformComponent *transform = scene.transform(entity);
  const MeshComponent *component = scene.mesh(entity);
  if (transform == nullptr || component == nullptr) {
    error_message = std::string(role) + " is not a mesh object.";
    return std::nullopt;
  }
  const MeshAsset *mesh = assets.mesh(component->mesh);
  if (mesh == nullptr) {
    error_message = std::string(role) + " mesh asset does not exist.";
    return std::nullopt;
  }
  if ((mesh->authored_polygon_document == nullptr ||
       mesh->authored_polygon_document->vertices.empty()) &&
      !mesh->has_stable_cpu_topology) {
    error_message =
        std::string(role) + " has no stable authored or CPU mesh vertices.";
    return std::nullopt;
  }
  const std::optional<SceneBounds> local_bounds = local_mesh_bounds(*mesh);
  const std::optional<QVec3> local_mean = local_mean_vertex(*mesh);
  if (!local_bounds.has_value() || !local_mean.has_value()) {
    error_message = std::string(role) + " has no usable mesh vertices.";
    return std::nullopt;
  }

  const QMat4 world_matrix = scene.world_transform_matrix(entity);
  if (!invertible_world_matrix(world_matrix)) {
    error_message =
        std::string(role) + " has a non-invertible world transform.";
    return std::nullopt;
  }
  const std::optional<glm::mat3> rotation =
      pure_world_rotation(world_matrix);
  const std::optional<SceneBounds> world_bounds =
      world_bounds_from_local_mesh_bounds(world_matrix, local_bounds);
  if (!rotation.has_value() || !world_bounds.has_value() ||
      !finite_vector(world_bounds->min) ||
      !finite_vector(world_bounds->max)) {
    error_message = std::string(role) + " has an invalid world transform.";
    return std::nullopt;
  }

  const QVec3 world_origin = transform_point(world_matrix, {});
  const QVec3 world_bounds_center = bounds_center(*world_bounds);
  const QVec3 world_mean = transform_point(world_matrix, *local_mean);
  const QVec3 world_hybrid_center =
      (world_bounds_center + world_mean) * 0.5f;
  if (!finite_vector(world_origin) || !finite_vector(world_bounds_center) ||
      !finite_vector(world_mean) || !finite_vector(world_hybrid_center)) {
    error_message = std::string(role) + " has invalid mesh geometry.";
    return std::nullopt;
  }

  return PivotGeometryFrame{
      .world_origin = world_origin,
      .world_bounds_center = world_bounds_center,
      .world_hybrid_center = world_hybrid_center,
      .world_rotation = *rotation,
  };
}

std::size_t entity_hierarchy_depth(const SceneDocument &scene,
                                   EntityId entity) {
  const std::size_t maximum_depth = scene.entities().size();
  std::size_t depth = 0u;
  EntityId parent = scene.parent(entity);
  while (parent != kInvalidEntityId && depth < maximum_depth) {
    ++depth;
    parent = scene.parent(parent);
  }
  return depth;
}

CopyPivotSelection collect_copy_pivot_selection(
    const SceneDocument &scene, const AssetRegistry &assets,
    const EditorSelectionService &selection) {
  const EditorSelectionState state = selection.state();
  if (state.mode != ModelingSelectionMode::Object) {
    return {.message =
                "Copy Pivot From Active only works in object mode."};
  }

  const std::vector<EntityId> selected = scene.selected_entities();
  const EntityId source = state.active_entity;
  if (source == kInvalidEntityId || !scene.exists(source) ||
      std::find(selected.begin(), selected.end(), source) == selected.end() ||
      scene.mesh(source) == nullptr) {
    return {.message = "Copy Pivot From Active needs an active mesh."};
  }
  if (!scene.is_effectively_visible(source)) {
    return {.message = "The active pivot source must be visible."};
  }

  std::string error_message;
  const std::optional<PivotGeometryFrame> source_frame =
      pivot_geometry_frame(scene, assets, source, "Active pivot source",
                           error_message);
  if (!source_frame.has_value()) {
    return {.message = std::move(error_message)};
  }
  const std::optional<QVec3> source_pivot =
      resolve_entity_transform_visible_pivot(scene, assets, source);
  if (!source_pivot.has_value() || !finite_vector(*source_pivot)) {
    return {.message = "Active pivot source has no valid visible pivot."};
  }
  const glm::vec3 source_local_offset =
      glm::transpose(source_frame->world_rotation) *
      to_glm(*source_pivot - source_frame->world_hybrid_center);

  CopyPivotSelection result;
  bool found_target = false;
  for (const EntityId entity : selected) {
    if (entity == source || scene.mesh(entity) == nullptr) {
      continue;
    }
    found_target = true;
    if (!scene.is_effectively_visible(entity)) {
      return {.message = "Every pivot target must be visible."};
    }
    if (scene.is_effectively_locked(entity)) {
      return {.message = "Locked mesh objects cannot receive a copied pivot."};
    }
    if (!pivot_rebase_environment_is_valid(scene, entity)) {
      return {.message = "Pivot target has an invalid transform hierarchy."};
    }
    const MeshComponent *component = scene.mesh(entity);
    const MeshAsset *mesh = component != nullptr
                                ? assets.mesh(component->mesh)
                                : nullptr;
    if (mesh == nullptr) {
      return {.message = "Pivot target mesh asset does not exist."};
    }
    if (!mesh->has_stable_cpu_topology || mesh->vertices.empty()) {
      return {.message =
                  "Pivot target mesh does not have editable CPU mesh data."};
    }

    const std::optional<PivotGeometryFrame> target_frame =
        pivot_geometry_frame(scene, assets, entity, "Pivot target",
                             error_message);
    if (!target_frame.has_value()) {
      return {.message = std::move(error_message)};
    }
    const QVec3 target_pivot =
        target_frame->world_hybrid_center +
        from_glm(target_frame->world_rotation * source_local_offset);
    if (!finite_vector(target_pivot)) {
      return {.message = "Copied pivot position is not finite."};
    }
    const std::optional<QVec3> current_pivot =
        resolve_entity_transform_visible_pivot(scene, assets, entity);
    if (!current_pivot.has_value() || !finite_vector(*current_pivot)) {
      return {.message = "Pivot target has no valid visible pivot."};
    }
    if (!near_same_point(*current_pivot, target_pivot)) {
      result.requests.push_back({
          .target = entity,
          .world_pivot = target_pivot,
      });
    }
  }

  if (!found_target) {
    return {.message =
                "Select at least one other mesh to receive the active pivot."};
  }

  std::sort(result.requests.begin(), result.requests.end(),
            [&](const PivotRebaseRequest &left,
                const PivotRebaseRequest &right) {
              const std::size_t left_depth =
                  entity_hierarchy_depth(scene, left.target);
              const std::size_t right_depth =
                  entity_hierarchy_depth(scene, right.target);
              if (left_depth != right_depth) {
                return left_depth < right_depth;
              }
              return left.target < right.target;
            });
  result.ok = true;
  return result;
}

/**
 * Stores one unique quick-align preset and its resolved world position.
 */
struct PivotAlignPreset {
    std::string_view id;
    QVec3 position;
};

std::vector<PivotAlignPreset> pivot_align_presets(const SceneBounds &bounds) {
  const QVec3 center = bounds_center(bounds);
  return {
      {.id = "pivot.align.center", .position = center},
      {.id = "pivot.align.bottom_center",
       .position = {center.x, bounds.min.y, center.z}},
      {.id = "pivot.align.top_center",
       .position = {center.x, bounds.max.y, center.z}},
      {.id = "pivot.align.bottom_left_front",
       .position = {bounds.min.x, bounds.min.y, bounds.min.z}},
      {.id = "pivot.align.bottom_left_back",
       .position = {bounds.min.x, bounds.min.y, bounds.max.z}},
      {.id = "pivot.align.bottom_right_front",
       .position = {bounds.max.x, bounds.min.y, bounds.min.z}},
      {.id = "pivot.align.bottom_right_back",
       .position = {bounds.max.x, bounds.min.y, bounds.max.z}},
      {.id = "pivot.align.top_left_front",
       .position = {bounds.min.x, bounds.max.y, bounds.min.z}},
      {.id = "pivot.align.top_left_back",
       .position = {bounds.min.x, bounds.max.y, bounds.max.z}},
      {.id = "pivot.align.top_right_front",
       .position = {bounds.max.x, bounds.max.y, bounds.min.z}},
      {.id = "pivot.align.top_right_back",
       .position = {bounds.max.x, bounds.max.y, bounds.max.z}},
  };
}

std::optional<QVec3> bounds_position_for_action(const SceneBounds &bounds,
                                                std::string_view action_id) {
  const std::vector<PivotAlignPreset> presets = pivot_align_presets(bounds);
  for (const PivotAlignPreset &preset : presets) {
    if (preset.id == action_id) {
      return preset.position;
    }
  }
  return std::nullopt;
}

std::size_t nearest_align_preset_index(
    const std::vector<PivotAlignPreset> &presets, QVec3 world_position) {
  std::size_t best_index = 0u;
  float best_distance = std::numeric_limits<float>::max();
  for (std::size_t index = 0u; index < presets.size(); ++index) {
    const float distance =
        length_squared(presets[index].position - world_position);
    if (distance < best_distance) {
      best_distance = distance;
      best_index = index;
    }
  }
  return best_index;
}

std::optional<QVec3> cycled_bounds_position_for_action(
    const SceneBounds &bounds, QVec3 current_position,
    std::string_view action_id) {
  if (action_id != "pivot.align.next" && action_id != "pivot.align.prev") {
    return std::nullopt;
  }
  const std::vector<PivotAlignPreset> presets = pivot_align_presets(bounds);
  if (presets.empty()) {
    return std::nullopt;
  }
  const std::size_t current =
      nearest_align_preset_index(presets, current_position);
  const std::size_t next =
      action_id == "pivot.align.next"
          ? (current + 1u) % presets.size()
          : (current + presets.size() - 1u) % presets.size();
  return presets[next].position;
}

} // namespace

PivotToolService::PivotToolService(SceneDocument &scene, AssetRegistry &assets,
                                   EditorHistoryService &history,
                                   ModelingSessionService &modeling)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling) {}

bool PivotToolService::persistent_active() const {
  return persistent_.has_value() && persistent_session_is_valid();
}

PivotHandleState PivotToolService::handle_state() const {
  if (persistent_active()) {
    return {
        .visible = true,
        .editing = true,
        .persistent = true,
        .target = persistent_->target,
        .world_position = persistent_->preview_world_position,
    };
  }
  if (quick_edit_active()) {
    return {
        .visible = true,
        .editing = true,
        .persistent = false,
        .target = kInvalidEntityId,
        .world_position = quick_.world_position,
    };
  }
  return {};
}

SelectionPivotOverride PivotToolService::selection_pivot_override() const {
  if (!quick_.override_valid || scene_.selected_entities().empty() ||
      quick_.selection_revision != scene_.selection_revision()) {
    return {};
  }
  return {
      .valid = true,
      .world_position = quick_.world_position,
  };
}

std::optional<QVec3> PivotToolService::persistent_preview_position() const {
  return persistent_active()
             ? std::optional<QVec3>(persistent_->preview_world_position)
             : std::nullopt;
}

bool PivotToolService::can_activate_persistent(
    const EditorSelectionService &selection) const {
  const std::optional<EntityId> target = active_selection_target(selection);
  return target.has_value() &&
         target_is_eligible(*target, target_kind(*target, selection));
}

bool PivotToolService::can_copy_pivot_from_active(
    const EditorSelectionService &selection) const {
  return collect_copy_pivot_selection(scene_, assets_, selection).ok;
}

CommandResult
PivotToolService::activate_persistent(EditorSelectionService &selection) {
  const std::optional<EntityId> target = active_selection_target(selection);
  if (!target.has_value()) {
    return command_invalid_result("Select one mesh or group to edit its pivot.");
  }
  if (!scene_.exists(*target)) {
    return command_invalid_result("Pivot target no longer exists.");
  }
  const PivotTargetKind kind = target_kind(*target, selection);
  if (!target_is_eligible(*target, kind)) {
    return command_invalid_result(
        "Select one visible, unlocked mesh or group in object mode.");
  }

  const std::optional<QVec3> committed_pivot =
      resolve_entity_transform_visible_pivot(scene_, assets_, *target);
  if (!committed_pivot.has_value() || !finite_vector(*committed_pivot)) {
    return command_invalid_result("Pivot target has no valid visible pivot.");
  }
  const SelectionPivotOverride temporary_pivot = selection_pivot_override();
  const QVec3 preview_pivot =
      temporary_pivot.valid ? temporary_pivot.world_position : *committed_pivot;
  const std::optional<SceneBounds> resolved_bounds =
      kind == PivotTargetKind::Group
          ? entity_hierarchy_world_bounds(scene_, assets_, *target)
          : entity_world_bounds(scene_, assets_, *target);
  if (!resolved_bounds.has_value() ||
      !finite_vector(resolved_bounds->min) ||
      !finite_vector(resolved_bounds->max)) {
    return command_invalid_result("Pivot target has no valid bounds.");
  }

  const std::vector<EntityId> selected = scene_.selected_entities();
  if (selected.size() != 1u || selected.front() != *target ||
      scene_.primary_selection() != *target) {
    apply_scene_selection(scene_,
                          SceneSelectionState{
                              .selected = {*target},
                              .active = *target,
                          },
                          SceneSelectionApplicationMode::RuntimeSync);
    modeling_.project_object_selection_from_scene(true);
  }
  persistent_ = PersistentSession{
      .target = *target,
      .kind = kind,
      .original_world_position = *committed_pivot,
      .preview_world_position = preview_pivot,
      .target_uuid = scene_.uuid(*target),
      .target_parent = scene_.parent(*target),
      .selection_mode = selection.mode(),
      .selection_revision = scene_.selection_revision(),
      .target_bounds_min = resolved_bounds->min,
      .target_bounds_max = resolved_bounds->max,
      .validated_scene_revision = scene_.revision(),
      .validated_asset_revision = assets_.revision(),
  };
  return command_changed_result();
}

CommandResult PivotToolService::cancel_persistent() {
  if (!persistent_.has_value()) {
    return command_unchanged_result();
  }
  persistent_.reset();
  return command_changed_result();
}

CommandResult PivotToolService::apply_persistent() {
  if (!persistent_.has_value()) {
    return command_unchanged_result();
  }
  if (!persistent_session_is_valid()) {
    persistent_.reset();
    return command_invalid_result(
        "Pivot target changed before the operation was applied.");
  }
  const PersistentSession session = *persistent_;
  persistent_.reset();
  if (near_same_point(session.original_world_position,
                      session.preview_world_position)) {
    clear_quick_override();
    return command_changed_result();
  }

  CommandResult result = history_.execute(std::make_unique<PivotRebaseCommand>(
      scene_, assets_, modeling_,
      std::vector<PivotRebaseRequest>{{
          .target = session.target,
          .world_pivot = session.preview_world_position,
      }},
      "Set Pivot"));
  if (result.ok) {
    clear_quick_override();
  }
  return result;
}

CommandResult PivotToolService::copy_pivot_from_active(
    const EditorSelectionService &selection) {
  CopyPivotSelection copy =
      collect_copy_pivot_selection(scene_, assets_, selection);
  if (!copy.ok) {
    return command_invalid_result(std::move(copy.message));
  }
  if (copy.requests.empty()) {
    return command_unchanged_result();
  }
  return history_.execute(std::make_unique<PivotRebaseCommand>(
      scene_, assets_, modeling_, std::move(copy.requests),
      "Copy Pivot From Active"));
}

CommandResult
PivotToolService::set_snap_enabled(std::string_view setting_id, bool enabled) {
  bool *target = nullptr;
  if (setting_id == kPivotSnapVertexSetting) {
    target = &snap_settings_.vertex;
  } else if (setting_id == kPivotSnapEdgeMidpointSetting) {
    target = &snap_settings_.edge_midpoint;
  } else if (setting_id == kPivotSnapFaceCenterSetting) {
    target = &snap_settings_.face_center;
  } else if (setting_id == kPivotSnapGridSetting) {
    target = &snap_settings_.grid;
  }
  if (target == nullptr) {
    return command_invalid_result("Unknown pivot snap setting.");
  }
  if (*target == enabled) {
    return command_unchanged_result();
  }
  *target = enabled;
  return command_changed_result();
}

CommandResult PivotToolService::execute_align_action(std::string_view action_id) {
  if (!persistent_.has_value()) {
    return command_invalid_result("Pivot tool is not active.");
  }
  if (!persistent_session_is_valid()) {
    persistent_.reset();
    return command_invalid_result(
        "Pivot target changed before the operation was applied.");
  }
  const std::optional<SceneBounds> bounds = target_bounds();
  if (!bounds.has_value()) {
    return command_invalid_result("Pivot target has no bounds.");
  }
  std::optional<QVec3> world_position = cycled_bounds_position_for_action(
      *bounds, persistent_->preview_world_position, action_id);
  if (!world_position.has_value()) {
    world_position = bounds_position_for_action(*bounds, action_id);
  }
  if (!world_position.has_value()) {
    return command_invalid_result("Unknown pivot align action.");
  }
  return align_to_world_position(*world_position);
}

void PivotToolService::set_persistent_preview_position(QVec3 world_position) {
  if (!finite_vector(world_position)) {
    return;
  }
  if (persistent_.has_value() && !persistent_session_is_valid()) {
    persistent_.reset();
  } else if (persistent_.has_value()) {
    persistent_->preview_world_position = world_position;
  }
}

void PivotToolService::begin_quick(QVec3 initial_world_position) {
  if (scene_.selected_entities().empty() ||
      !finite_vector(initial_world_position)) {
    clear_quick_override();
    return;
  }
  quick_ = QuickState{
      .override_valid = true,
      .editing = true,
      .selection_revision = scene_.selection_revision(),
      .world_position = initial_world_position,
  };
  ++selection_pivot_revision_;
}

void PivotToolService::set_quick_preview_position(QVec3 world_position) {
  if (!quick_edit_active() || !finite_vector(world_position)) {
    return;
  }
  if (quick_.world_position.x == world_position.x &&
      quick_.world_position.y == world_position.y &&
      quick_.world_position.z == world_position.z && quick_.override_valid) {
    return;
  }
  quick_.world_position = world_position;
  quick_.override_valid = true;
  ++selection_pivot_revision_;
}

void PivotToolService::end_quick_edit() {
  if (quick_edit_active()) {
    quick_.editing = false;
  }
}

void PivotToolService::clear_quick_override() {
  if (!quick_.override_valid && !quick_.editing &&
      quick_.selection_revision == 0 && quick_.world_position.x == 0.0f &&
      quick_.world_position.y == 0.0f && quick_.world_position.z == 0.0f) {
    return;
  }
  quick_ = QuickState{};
  ++selection_pivot_revision_;
}

bool PivotToolService::quick_edit_active() const {
  return quick_.editing && quick_.override_valid &&
         quick_.selection_revision == scene_.selection_revision() &&
         !scene_.selected_entities().empty();
}

std::optional<EntityId> PivotToolService::active_selection_target(
    const EditorSelectionService &selection) const {
  const EditorSelectionState state = selection.state();
  if (state.mode != ModelingSelectionMode::Object &&
      state.mode != ModelingSelectionMode::Group) {
    return std::nullopt;
  }
  if (state.target.entity != kInvalidEntityId &&
      scene_.exists(state.target.entity)) {
    return state.target.entity;
  }
  if (state.active_entity != kInvalidEntityId &&
      scene_.exists(state.active_entity)) {
    return state.active_entity;
  }
  const std::vector<EntityId> selected = scene_.selected_entities();
  if (!selected.empty()) {
    return selected.back();
  }
  return std::nullopt;
}

PivotToolService::PivotTargetKind PivotToolService::target_kind(
    EntityId target, const EditorSelectionService &selection) const {
  (void)selection;
  if (scene_.has_children(target)) {
    return PivotTargetKind::Group;
  }
  return PivotTargetKind::Mesh;
}

bool PivotToolService::target_is_eligible(EntityId target,
                                          PivotTargetKind kind) const {
  if (!scene_.exists(target) || scene_.transform(target) == nullptr ||
      !scene_.is_effectively_visible(target) ||
      scene_.is_effectively_locked(target) ||
      !pivot_rebase_environment_is_valid(scene_, target)) {
    return false;
  }
  const std::optional<SceneBounds> bounds =
      kind == PivotTargetKind::Group
          ? entity_hierarchy_world_bounds(scene_, assets_, target)
          : entity_world_bounds(scene_, assets_, target);
  if (!bounds.has_value() || !finite_vector(bounds->min) ||
      !finite_vector(bounds->max) ||
      !finite_vector(bounds_center(*bounds))) {
    return false;
  }
  if (kind == PivotTargetKind::Group) {
    if (!scene_.has_children(target) ||
        scene_.kind(target) == EntityKind::Light) {
      return false;
    }
    const MeshComponent *mesh = scene_.mesh(target);
    if (mesh == nullptr) {
      return true;
    }
    const MeshAsset *asset = assets_.mesh(mesh->mesh);
    return asset != nullptr && mesh_has_editable_finite_geometry(*asset);
  }
  const MeshComponent *mesh = scene_.mesh(target);
  const MeshAsset *asset = mesh != nullptr ? assets_.mesh(mesh->mesh) : nullptr;
  return scene_.kind(target) == EntityKind::Mesh && asset != nullptr &&
         mesh_has_editable_finite_geometry(*asset);
}

bool PivotToolService::persistent_session_is_valid() const {
  if (!persistent_.has_value()) {
    return false;
  }
  PersistentSession &session = *persistent_;
  const std::vector<EntityId> selected = scene_.selected_entities();
  if (scene_.selection_revision() != session.selection_revision ||
      scene_.uuid(session.target) != session.target_uuid ||
      scene_.parent(session.target) != session.target_parent ||
      modeling_.selection_mode() != session.selection_mode ||
      selected.size() != 1u || selected.front() != session.target ||
      scene_.primary_selection() != session.target) {
    return false;
  }
  if (scene_.revision() == session.validated_scene_revision &&
      assets_.revision() == session.validated_asset_revision) {
    return true;
  }
  if (!target_is_eligible(session.target, session.kind)) {
    return false;
  }
  const std::optional<SceneBounds> bounds =
      session.kind == PivotTargetKind::Group
          ? entity_hierarchy_world_bounds(scene_, assets_, session.target)
          : entity_world_bounds(scene_, assets_, session.target);
  const std::optional<QVec3> committed_pivot =
      resolve_entity_transform_visible_pivot(scene_, assets_, session.target);
  const bool valid =
      bounds.has_value() && committed_pivot.has_value() &&
      finite_vector(bounds->min) && finite_vector(bounds->max) &&
      finite_vector(*committed_pivot) &&
      near_same_point(bounds->min, session.target_bounds_min) &&
      near_same_point(bounds->max, session.target_bounds_max) &&
      near_same_point(*committed_pivot, session.original_world_position);
  if (valid) {
    session.validated_scene_revision = scene_.revision();
    session.validated_asset_revision = assets_.revision();
  }
  return valid;
}

std::optional<SceneBounds> PivotToolService::target_bounds() const {
  if (!persistent_active()) {
    return std::nullopt;
  }
  return persistent_->kind == PivotTargetKind::Group
             ? entity_hierarchy_world_bounds(scene_, assets_,
                                             persistent_->target)
             : entity_world_bounds(scene_, assets_, persistent_->target);
}

CommandResult PivotToolService::align_to_world_position(QVec3 world_position) {
  if (!persistent_.has_value()) {
    return command_invalid_result("Pivot tool is not active.");
  }
  if (!persistent_session_is_valid()) {
    persistent_.reset();
    return command_invalid_result(
        "Pivot target changed before the operation was applied.");
  }
  if (!finite_vector(world_position)) {
    return command_invalid_result("Pivot position is not finite.");
  }
  if (near_same_point(persistent_->preview_world_position, world_position)) {
    return command_unchanged_result();
  }
  persistent_->preview_world_position = world_position;
  return command_changed_result();
}

} // namespace quader
