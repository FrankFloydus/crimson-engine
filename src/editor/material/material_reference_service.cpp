////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/material_reference_service.hpp"

#include "assets/asset_registry.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/scene_document.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

/// Associates one semantic selection path with its canonical mesh owner.
struct ScopedMeshTarget final {
  EntityId entity = kInvalidEntityId;
  bool context_locked = false;
};

/// Stores one exact native material component before and after replacement.
struct MaterialComponentEdit final {
  EntityId entity = kInvalidEntityId;
  std::optional<MaterialComponent> before;
  std::optional<MaterialComponent> after;
};

[[nodiscard]] bool append_distinct_reference(
    std::vector<MaterialReference> &references, MaterialReference reference) {
  if (!reference.valid() ||
      std::ranges::find(references, reference) != references.end()) {
    return false;
  }
  references.push_back(reference);
  return true;
}

[[nodiscard]] std::vector<std::uint32_t>
live_material_slots(const MeshAsset &mesh) {
  std::vector<std::uint32_t> slots;
  if (mesh.has_authored_polygon_document &&
      mesh.authored_polygon_document != nullptr) {
    slots.reserve(mesh.authored_polygon_document->faces.size());
    for (const quader_poly::Face &face :
         mesh.authored_polygon_document->faces) {
      if (std::ranges::find(slots, face.material_slot) == slots.end()) {
        slots.push_back(face.material_slot);
      }
    }
    return slots;
  }

  const std::vector<MeshPrimitiveSpan> primitives =
      normalized_mesh_primitive_spans(mesh);
  slots.reserve(primitives.size());
  for (const MeshPrimitiveSpan &primitive : primitives) {
    if (std::ranges::find(slots, primitive.material_slot) == slots.end()) {
      slots.push_back(primitive.material_slot);
    }
  }
  return slots;
}

void append_scoped_mesh_target(std::vector<ScopedMeshTarget> &targets,
                               EntityId entity, bool context_locked) {
  const auto found = std::ranges::find_if(
      targets, [entity](const ScopedMeshTarget &target) {
        return target.entity == entity;
      });
  if (found == targets.end()) {
    targets.push_back({.entity = entity,
                       .context_locked = context_locked});
    return;
  }
  found->context_locked = found->context_locked || context_locked;
}

[[nodiscard]] bool linked_occurrence_is_locked(const SceneDocument &scene,
                                               EntityId member) {
  const EntityId definition = scene.containing_instance_definition(member);
  if (definition == kInvalidEntityId || definition == member) {
    return false;
  }
  for (const EntityId occurrence :
       scene.instance_occurrences_for_definition(definition)) {
    if (scene.is_effectively_locked(occurrence)) {
      return true;
    }
  }
  return false;
}

/**
 * Tests one hierarchy segment for local read-only/selectability state while
 * deliberately ignoring visibility. The exclusive ancestor form keeps the
 * intentionally unselectable instance-definition root out of canonical-member
 * policy.
 */
[[nodiscard]] bool hierarchy_segment_is_selectable_ignoring_visibility(
    const SceneDocument &scene, EntityId entity,
    EntityId exclusive_ancestor = kInvalidEntityId) {
  if (!scene.exists(entity) ||
      (exclusive_ancestor != kInvalidEntityId &&
       !scene.exists(exclusive_ancestor))) {
    return false;
  }
  EntityId current = entity;
  while (current != kInvalidEntityId && current != exclusive_ancestor) {
    const VisibilityComponent *visibility = scene.visibility(current);
    if (visibility != nullptr && !visibility->selectable) {
      return false;
    }
    current = scene.parent(current);
  }
  return current == exclusive_ancestor;
}

/** Applies read-only/selectability policy to a mesh and every linked path. */
[[nodiscard]] bool material_target_is_effectively_unselectable(
    const SceneDocument &scene, EntityId entity) {
  const EntityId definition = scene.containing_instance_definition(entity);
  if (definition == kInvalidEntityId || definition == entity) {
    return !hierarchy_segment_is_selectable_ignoring_visibility(scene,
                                                                 entity);
  }
  if (!hierarchy_segment_is_selectable_ignoring_visibility(scene, entity,
                                                            definition)) {
    return true;
  }
  for (const EntityId occurrence :
       scene.instance_occurrences_for_definition(definition)) {
    if (!hierarchy_segment_is_selectable_ignoring_visibility(scene,
                                                              occurrence)) {
      return true;
    }
  }
  return false;
}

void append_group_target(const SceneDocument &scene, EntityId selected_root,
                         std::vector<ScopedMeshTarget> &targets) {
  if (!scene.exists(selected_root)) {
    return;
  }
  for (const EntityId candidate : scene.descendants(selected_root, true)) {
    if (const InstanceOccurrenceComponent *occurrence =
            scene.instance_occurrence(candidate)) {
      if (!scene.is_instance_definition_entity(occurrence->definition)) {
        continue;
      }
      const bool occurrence_locked = scene.is_effectively_locked(candidate);
      for (const EntityId member :
           scene.descendants(occurrence->definition, false)) {
        if (scene.containing_instance_definition(member) !=
                occurrence->definition ||
            scene.mesh(member) == nullptr) {
          continue;
        }
        append_scoped_mesh_target(
            targets, member,
            occurrence_locked || scene.is_effectively_locked(member) ||
                linked_occurrence_is_locked(scene, member));
      }
      continue;
    }
    if (scene.mesh(candidate) != nullptr) {
      append_scoped_mesh_target(targets, candidate,
                                scene.is_effectively_locked(candidate));
    }
  }
}

[[nodiscard]] std::vector<ScopedMeshTarget> scoped_mesh_targets(
    const SceneDocument &scene, const ModelingSessionService &modeling,
    MaterialReferenceUsageScope scope) {
  std::vector<ScopedMeshTarget> targets;
  if (scope == MaterialReferenceUsageScope::ActiveScene) {
    for (const EntityId entity : scene.entity_tree_order()) {
      if (scene.mesh(entity) != nullptr) {
        append_scoped_mesh_target(targets, entity,
                                  scene.is_effectively_locked(entity) ||
                                      linked_occurrence_is_locked(scene,
                                                                  entity));
      }
    }
    return targets;
  }

  const ModelingSelectionMode mode = modeling.selection_mode();
  if (mode == ModelingSelectionMode::Face ||
      mode == ModelingSelectionMode::Vertex ||
      mode == ModelingSelectionMode::Edge) {
    return targets;
  }
  const std::vector<EntityId> selected = scene.selected_entities();
  if (mode == ModelingSelectionMode::Group) {
    for (const EntityId root : selected) {
      append_group_target(scene, root, targets);
    }
    return targets;
  }
  for (const EntityId entity : selected) {
    if (scene.mesh(entity) != nullptr) {
      append_scoped_mesh_target(targets, entity,
                                scene.is_effectively_locked(entity));
    }
  }
  return targets;
}

[[nodiscard]] std::optional<MaterialReference> face_material_reference(
    const SceneDocument &scene, AssetRegistry &assets, EntityId entity,
    quader_poly::ElementId face_id) {
  const MeshComponent *mesh_component = scene.mesh(entity);
  const MaterialComponent *material_component = scene.material(entity);
  const MeshAsset *mesh =
      mesh_component != nullptr ? assets.mesh(mesh_component->mesh) : nullptr;
  if (mesh == nullptr || !mesh->has_authored_polygon_document ||
      mesh->authored_polygon_document == nullptr ||
      material_component == nullptr) {
    return std::nullopt;
  }
  const quader_poly::Face *face =
      quader_poly::find_face(*mesh->authored_polygon_document, face_id);
  if (face == nullptr) {
    return std::nullopt;
  }
  return material_component->material_for_slot(face->material_slot);
}

[[nodiscard]] std::optional<MaterialComponent>
replaced_material_component(const MaterialComponent *existing,
                            MaterialReference source,
                            MaterialReference replacement) {
  if (existing == nullptr) {
    return std::nullopt;
  }
  MaterialComponent desired = *existing;
  bool changed = false;
  if (desired.material == source) {
    desired.material = replacement;
    changed = true;
  }
  for (auto &[slot, reference] : desired.material_slots) {
    (void)slot;
    if (reference == source) {
      reference = replacement;
      changed = true;
    }
  }
  return changed ? std::optional<MaterialComponent>(std::move(desired))
                 : std::nullopt;
}

[[nodiscard]] bool apply_component_state(
    SceneDocument &scene, EntityId entity,
    const std::optional<MaterialComponent> &desired) {
  if (!scene.exists(entity)) {
    return false;
  }
  const MaterialComponent *existing = scene.material(entity);
  if (desired.has_value()) {
    if (existing != nullptr && *existing == *desired) {
      return true;
    }
    return scene.set_material(entity, *desired);
  }
  return existing == nullptr || scene.clear_material(entity);
}

/** Restores material topology, native bindings, and semantic selection. */
class ReplaceMaterialReferencesCommand final : public IUndoableCommand {
public:
  ReplaceMaterialReferencesCommand(
      SceneDocument &scene, ModelingSessionService &modeling,
      std::vector<MaterialComponentEdit> component_edits,
      std::vector<ModelingFaceMaterialReplacementTarget> face_targets,
      MaterialReference source, MaterialReference replacement)
      : scene_(scene), modeling_(modeling),
        component_edits_(std::move(component_edits)),
        face_targets_(std::move(face_targets)), source_(source),
        replacement_(replacement) {}

  [[nodiscard]] const std::string &label() const override { return label_; }

  CommandResult undo() override {
    if (!executed_) {
      return command_invalid_result(
          "Material replacement has not been executed.");
    }
    return restore(before_memento_, before_selection_, false);
  }

  CommandResult redo() override {
    if (executed_) {
      return restore(after_memento_, after_selection_, true);
    }

    before_memento_ = modeling_.capture_history_memento();
    before_selection_ = document_command_capture_selection(scene_);
    if (!before_memento_.valid()) {
      return command_invalid_result(
          "Material replacement restore point could not be captured.");
    }

    for (const MaterialComponentEdit &edit : component_edits_) {
      if (!apply_component_state(scene_, edit.entity, edit.after)) {
        return rollback_initial(
            "A material binding could not be replaced.");
      }
    }

    bool changed = !component_edits_.empty();
    if (!face_targets_.empty()) {
      const ModelingCommandResult face_result =
          modeling_.replace_material_references_on_faces(
              face_targets_, source_, replacement_);
      if (!face_result.ok) {
        return rollback_initial(
            face_result.message.empty()
                ? "Selected face materials could not be replaced."
                : face_result.message);
      }
      changed = changed || face_result.changed;
    }
    if (!changed) {
      return command_unchanged_result();
    }

    after_memento_ = modeling_.capture_history_memento();
    after_selection_ = document_command_capture_selection(scene_);
    if (!after_memento_.valid()) {
      return rollback_initial(
          "Material replacement result could not be captured.");
    }
    executed_ = true;
    return command_changed_result();
  }

private:
  [[nodiscard]] CommandResult restore(
      const ModelingSessionMemento &memento,
      const DocumentSelectionState &selection, bool use_after) {
    const ModelingCommandResult restored =
        modeling_.restore_history_memento(memento);
    if (!restored.ok) {
      return command_invalid_result(
          restored.message.empty()
              ? "Material replacement history could not be restored."
              : restored.message);
    }
    for (const MaterialComponentEdit &edit : component_edits_) {
      const std::optional<MaterialComponent> &desired =
          use_after ? edit.after : edit.before;
      if (!apply_component_state(scene_, edit.entity, desired)) {
        return command_invalid_result(
            "A native material binding could not be restored.");
      }
    }
    document_command_apply_selection(scene_, selection);
    return command_changed_result();
  }

  [[nodiscard]] CommandResult rollback_initial(std::string message) {
    const CommandResult rollback = restore(before_memento_, before_selection_,
                                           false);
    if (!rollback.ok) {
      message += "; rollback failed: " + rollback.message;
    }
    return command_invalid_result(std::move(message));
  }

  SceneDocument &scene_;
  ModelingSessionService &modeling_;
  std::vector<MaterialComponentEdit> component_edits_;
  std::vector<ModelingFaceMaterialReplacementTarget> face_targets_;
  MaterialReference source_;
  MaterialReference replacement_;
  ModelingSessionMemento before_memento_;
  ModelingSessionMemento after_memento_;
  DocumentSelectionState before_selection_;
  DocumentSelectionState after_selection_;
  std::string label_ = "Replace Material";
  bool executed_ = false;
};

} // namespace

MaterialReferenceService::MaterialReferenceService(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingSessionService &modeling)
    : scene_(&scene), assets_(&assets), modeling_(&modeling) {}

std::vector<MaterialReference> MaterialReferenceService::used_materials(
    MaterialReferenceUsageScope scope) const {
  std::vector<MaterialReference> result;
  if (scope == MaterialReferenceUsageScope::Selection &&
      modeling_->selection_mode() == ModelingSelectionMode::Face) {
    const ModelingSelectionExport selection = modeling_->export_selection();
    for (const ModelingSelectionExportObject &object : selection.objects) {
      for (const ModelingSelectionExportFace &face : object.selected_faces) {
        const std::optional<MaterialReference> material =
            face_material_reference(*scene_, *assets_, object.entity, face.id);
        if (material.has_value()) {
          (void)append_distinct_reference(result, *material);
        }
      }
    }
    return result;
  }

  for (const ScopedMeshTarget &target :
       scoped_mesh_targets(*scene_, *modeling_, scope)) {
    const MeshComponent *mesh_component = scene_->mesh(target.entity);
    const MaterialComponent *material_component =
        scene_->material(target.entity);
    const MeshAsset *mesh = mesh_component != nullptr
                                ? assets_->mesh(mesh_component->mesh)
                                : nullptr;
    if (mesh == nullptr || material_component == nullptr) {
      continue;
    }
    for (const std::uint32_t slot : live_material_slots(*mesh)) {
      (void)append_distinct_reference(
          result, material_component->material_for_slot(slot));
    }
  }
  return result;
}

MaterialReferenceReplacementPreparation
MaterialReferenceService::prepare_replacement(
    MaterialReference source, MaterialReference replacement,
    MaterialReferenceUsageScope scope,
    const DestinationValidator &destination_is_valid) const {
  if (!source.valid()) {
    return {
        .ok = false,
        .message = "The material to find is unavailable.",
    };
  }
  if (!replacement.valid()) {
    return {
        .ok = false,
        .message = "The replacement material is unavailable.",
    };
  }
  if (source == replacement) {
    return {.ok = true, .changed = false};
  }
  if (!destination_is_valid || !destination_is_valid(replacement)) {
    return {
        .ok = false,
        .message = "The replacement material does not exist.",
    };
  }

  std::vector<MaterialComponentEdit> component_edits;
  std::vector<ModelingFaceMaterialReplacementTarget> face_targets;
  if (scope == MaterialReferenceUsageScope::Selection &&
      modeling_->selection_mode() == ModelingSelectionMode::Face) {
    const ModelingSelectionExport selection = modeling_->export_selection();
    for (const ModelingSelectionExportObject &object : selection.objects) {
      for (const ModelingSelectionExportFace &face : object.selected_faces) {
        const std::optional<MaterialReference> material =
            face_material_reference(*scene_, *assets_, object.entity, face.id);
        if (!material.has_value() || *material != source) {
          continue;
        }
        if (scene_->is_effectively_locked(object.entity) ||
            linked_occurrence_is_locked(*scene_, object.entity) ||
            material_target_is_effectively_unselectable(*scene_,
                                                        object.entity)) {
          return {
              .ok = false,
              .message =
                  "A matching material target is locked or read-only.",
          };
        }
        face_targets.push_back({
            .modeling_object_id = object.modeling_object_id,
            .face_id = static_cast<int>(face.id),
        });
      }
    }
  } else {
    for (const ScopedMeshTarget &target :
         scoped_mesh_targets(*scene_, *modeling_, scope)) {
      const MaterialComponent *existing = scene_->material(target.entity);
      std::optional<MaterialComponent> desired = replaced_material_component(
          existing, source, replacement);
      if (!desired.has_value()) {
        continue;
      }
      if (target.context_locked ||
          scene_->is_effectively_locked(target.entity) ||
          material_target_is_effectively_unselectable(*scene_,
                                                      target.entity)) {
        return {
            .ok = false,
            .message = "A matching material target is locked or read-only.",
        };
      }
      component_edits.push_back({
          .entity = target.entity,
          .before = *existing,
          .after = std::move(*desired),
      });
    }
  }

  if (component_edits.empty() && face_targets.empty()) {
    return {.ok = true, .changed = false};
  }
  return {
      .ok = true,
      .changed = true,
      .command = std::make_unique<ReplaceMaterialReferencesCommand>(
          *scene_, *modeling_, std::move(component_edits),
          std::move(face_targets), source, replacement),
  };
}

} // namespace quader
