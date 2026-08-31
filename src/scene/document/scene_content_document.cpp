////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/document/scene_content_document.hpp"

#include "scene/scene_transform.hpp"

#include <unordered_map>
#include <utility>

namespace quader {

SceneContentDocument::SceneContentDocument(
    SceneContentDocumentRootPolicy root_policy) {
  if (root_policy == SceneContentDocumentRootPolicy::EnsureSceneRoot) {
    (void)ensure_scene_root();
  }
}

SceneContentDocument::SceneContentDocument(
    SceneContentDocumentRecord record,
    SceneContentDocumentRootPolicy root_policy)
    : record_(std::move(record)) {
  if (root_policy == SceneContentDocumentRootPolicy::EnsureSceneRoot) {
    (void)ensure_scene_root();
  }
}

void SceneContentDocument::rename(std::string name) {
  if (record_.name == name) {
    return;
  }
  record_.name = std::move(name);
  mark_dirty();
}

SceneContentDocumentIdentityReport
SceneContentDocument::validate_object_identity_invariants() const {
  SceneContentDocumentIdentityReport report;
  std::unordered_map<EditorObjectId, EntityId> owners;
  for (const EntityId entity : scene_.entities()) {
    const EditorObjectId object_id = scene_.editor_object_id(entity);
    if (object_id == kInvalidEditorObjectId) {
      report.issues.push_back(SceneContentDocumentIdentityIssue{
          .entity = entity,
          .object_id = object_id,
          .message = "Scene entity is missing an editor object id.",
      });
      continue;
    }
    if (!owners.emplace(object_id, entity).second) {
      report.issues.push_back(SceneContentDocumentIdentityIssue{
          .entity = entity,
          .object_id = object_id,
          .message = "Multiple scene entities share an editor object id.",
      });
      continue;
    }
    if (scene_.entity_for_editor_object_id(object_id) != entity) {
      report.issues.push_back(SceneContentDocumentIdentityIssue{
          .entity = entity,
          .object_id = object_id,
          .message = "Scene editor object identity index is inconsistent.",
      });
    }
  }
  return report;
}

SceneContentDocumentAssetReferenceReport
SceneContentDocument::validate_asset_references() const {
  SceneContentDocumentAssetReferenceReport report;
  for (const EntityId entity : scene_.entities()) {
    const EditorObjectId object_id = scene_.editor_object_id(entity);
    if (const CurveComponent *curve = scene_.curve(entity);
        curve != nullptr && (curve->curve == kInvalidAssetId ||
                             !assets_.has_curve(curve->curve))) {
      report.issues.push_back(SceneContentDocumentAssetReferenceIssue{
          .entity = entity,
          .object_id = object_id,
          .asset = curve->curve,
          .message = "entity references a missing curve asset.",
      });
    }
    if (scene_.kind(entity) == EntityKind::Curve &&
        scene_.curve(entity) == nullptr) {
      report.issues.push_back(SceneContentDocumentAssetReferenceIssue{
          .entity = entity,
          .object_id = object_id,
          .message = "curve entity is missing its authored curve binding.",
      });
    }
    if (const MeshComponent *mesh = scene_.mesh(entity);
        mesh != nullptr && (mesh->mesh == kInvalidAssetId ||
                            !assets_.has_mesh(mesh->mesh))) {
      report.issues.push_back(SceneContentDocumentAssetReferenceIssue{
          .entity = entity,
          .object_id = object_id,
          .asset = mesh->mesh,
          .message = "entity references a missing mesh asset.",
      });
    }
    if (const MaterialComponent *material = scene_.material(entity);
        material != nullptr && material->material.embedded() &&
        assets_.material(material->material.embedded_asset()) == nullptr) {
      report.issues.push_back(SceneContentDocumentAssetReferenceIssue{
          .entity = entity,
          .object_id = object_id,
          .asset = material->material.embedded_asset(),
          .message = "entity references a missing material asset.",
      });
    }
    if (const MaterialComponent *material = scene_.material(entity)) {
      for (const auto &[slot, material_reference] :
           material->material_slots) {
        (void)slot;
        if (!material_reference.valid() ||
            (material_reference.embedded() &&
             assets_.material(material_reference.embedded_asset()) ==
                 nullptr)) {
          report.issues.push_back(SceneContentDocumentAssetReferenceIssue{
              .entity = entity,
              .object_id = object_id,
              .asset = material_reference.embedded_asset(),
              .project_asset = material_reference.project_asset(),
              .message =
                  "entity material slot references a missing material asset.",
          });
        }
      }
    }
  }
  return report;
}

EntityId SceneContentDocument::ensure_scene_root() {
  EntityId root = scene_.scene_root();
  bool created_root = false;
  if (root == kInvalidEntityId) {
    root = scene_.create_entity("scene", EntityKind::Empty);
    if (root == kInvalidEntityId) {
      return kInvalidEntityId;
    }
    created_root = true;
    if (!scene_.set_scene_settings(root, SceneSettingsComponent{})) {
      return kInvalidEntityId;
    }
  }

  if (const NameComponent *name = scene_.name(root);
      name != nullptr && name->name != "scene") {
    (void)scene_.rename_entity(root, "scene");
  }
  if (scene_.kind(root) != EntityKind::Empty) {
    (void)scene_.set_entity_kind(root, EntityKind::Empty);
  }
  if (const VisibilityComponent *visibility = scene_.visibility(root)) {
    if (!visibility->visible) {
      (void)scene_.set_visible(root, true);
    }
    if (!visibility->selectable) {
      (void)scene_.set_selectable(root, true);
    }
  }
  if (const LockComponent *lock = scene_.lock(root);
      lock != nullptr && lock->locked) {
    (void)scene_.set_locked(root, false);
  }

  const std::vector<EntityId> entities = scene_.entities();
  std::vector<SceneParentAssignment> hierarchy;
  std::vector<SceneTransformAssignment> preserved_transforms;
  hierarchy.reserve(entities.size());
  preserved_transforms.reserve(entities.size());
  const QMat4 root_world = scene_.world_transform_matrix(root);
  bool has_orphans = false;
  for (const EntityId entity : entities) {
    EntityId parent = scene_.parent(entity);
    if (entity != root && parent == kInvalidEntityId) {
      parent = root;
      has_orphans = true;
      if (!created_root) {
        const TransformComponent *current = scene_.transform(entity);
        if (current == nullptr) {
          return kInvalidEntityId;
        }
        const std::optional<TransformComponent> preserved =
            try_scene_local_transform_preserving_world(
                root_world, scene_.world_transform_matrix(entity), *current);
        if (!preserved.has_value()) {
          return kInvalidEntityId;
        }
        preserved_transforms.push_back(SceneTransformAssignment{
            .entity = entity,
            .transform = *preserved,
        });
      }
    }
    hierarchy.push_back(SceneParentAssignment{
        .child = entity,
        .parent = parent,
    });
  }
  if (has_orphans && scene_.replace_hierarchy(hierarchy)) {
    scene_.set_transforms(preserved_transforms);
  } else if (has_orphans) {
    for (const SceneParentAssignment &assignment : hierarchy) {
      if (assignment.child != root &&
          scene_.parent(assignment.child) == kInvalidEntityId) {
        (void)scene_.set_parent(assignment.child, root, true);
      }
    }
  }
  return root;
}

std::uint64_t SceneContentDocument::revision() const { return revision_; }

void SceneContentDocument::mark_dirty() { ++revision_; }

} // namespace quader
