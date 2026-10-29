////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/instances/project_clipboard_service.hpp"

#include "assets/default_material_catalog.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "core/release_features.hpp"
#include "editor/commands/asset_command_service.hpp"
#include "editor/commands/document/duplicate_selected_hierarchy_command.hpp"
#include "editor/commands/document/set_scene_layer_state_command.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "editor/session/project_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "io/project/project_material_reference_resolver.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

/// Selects which clipboard payload family currently owns paste behavior.
enum class ProjectClipboardKind {
  Empty,
  Ordinary,
  Instances,
};

/// Captures one canonical definition hierarchy and its referenced mesh assets.
struct ClipboardDefinition final {
  DocumentEntitySnapshot snapshot;
  std::unordered_map<EntityId, MeshAsset> meshes;
};

/// Captures one occurrence root and its definition index in the payload.
struct ClipboardOccurrence final {
  DocumentEntitySnapshot snapshot;
  QMat4 parent_world_transform;
  std::size_t definition_index = 0u;
};

/// Owns a self-contained project clipboard snapshot for linked occurrences.
struct InstanceClipboardPayload final {
  SceneSessionId source_scene;
  std::vector<ClipboardDefinition> definitions;
  std::vector<ClipboardOccurrence> occurrences;
  std::unordered_map<SceneLayerId, SceneLayerRecord> layers;
  std::unordered_map<AssetId, MaterialDocument> materials;
  std::unordered_map<AssetId, TextureAsset> textures;
};

/// Plans one deterministic source-layer-name to destination-layer-id import.
struct ClipboardLayerImportPlan final {
  SceneLayerState before;
  SceneLayerState after;
  std::unordered_map<SceneLayerId, SceneLayerId> remap;
};

CommandResult clipboard_failure(std::string message) {
  return command_invalid_result(std::move(message));
}

bool occurrence_is_valid(const SceneDocument &scene, EntityId entity) {
  if (!scene.exists(entity) || scene.kind(entity) != EntityKind::Instance ||
      scene.has_children(entity) || !scene.is_effectively_selectable(entity)) {
    return false;
  }
  const InstanceOccurrenceComponent *occurrence =
      scene.instance_occurrence(entity);
  return occurrence != nullptr &&
         scene.is_instance_definition_entity(occurrence->definition) &&
         !scene.children(occurrence->definition).empty();
}

bool collect_material_reference(const AssetRegistry &assets,
                                MaterialReference reference,
                                InstanceClipboardPayload &payload,
                                std::string &error) {
  if (!release_has_feature(ReleaseFeature::Materials) ||
      !reference.embedded()) {
    return true;
  }
  const AssetId material_id = reference.embedded_asset();
  if (payload.materials.contains(material_id)) {
    return true;
  }
  const MaterialDocument *material = assets.material(material_id);
  if (material == nullptr) {
    error = "Instance references a missing embedded material.";
    return false;
  }
  payload.materials.emplace(material_id, *material);
  for (const auto &[slot, texture_id] : material->texture_slots) {
    (void)slot;
    if (texture_id == kInvalidAssetId || payload.textures.contains(texture_id)) {
      continue;
    }
    const TextureAsset *texture = assets.texture(texture_id);
    if (texture == nullptr) {
      error = "Instance material references a missing embedded texture.";
      return false;
    }
    payload.textures.emplace(texture_id, *texture);
  }
  return true;
}

bool collect_snapshot_assets(const DocumentEntitySnapshot &snapshot,
                             const AssetRegistry &assets,
                             ClipboardDefinition &definition,
                             InstanceClipboardPayload &payload,
                             std::string &error) {
  if (snapshot.instance_occurrence.has_value()) {
    error = "Nested instances are not supported by the clipboard.";
    return false;
  }
  if (snapshot.kind == EntityKind::Light || snapshot.light.has_value()) {
    error = "Lights are not supported inside an instance definition.";
    return false;
  }
  if (snapshot.kind == EntityKind::Curve || snapshot.curve.has_value()) {
    error = "Curves are not supported inside an instance definition.";
    return false;
  }
  if (snapshot.mesh.has_value()) {
    if (!snapshot.modeling_object.has_value() ||
        snapshot.mesh->mesh == kInvalidAssetId) {
      error = "Instance mesh is missing its modeling identity.";
      return false;
    }
    const MeshAsset *mesh = assets.mesh(snapshot.mesh->mesh);
    if (mesh == nullptr || !mesh->has_authored_polygon_document ||
        !mesh->authored_polygon_document) {
      error = "Instance mesh has no portable authored topology.";
      return false;
    }
    definition.meshes.emplace(snapshot.id,
                              clone_mesh_asset_payload(*mesh));
  }
  if (snapshot.material.has_value()) {
    if (!collect_material_reference(assets, snapshot.material->material,
                                    payload, error)) {
      return false;
    }
    for (const auto &[slot, material] :
         snapshot.material->material_slots) {
      (void)slot;
      if (!collect_material_reference(assets, material, payload, error)) {
        return false;
      }
    }
  }
  for (const DocumentEntitySnapshot &child : snapshot.children) {
    if (!collect_snapshot_assets(child, assets, definition, payload, error)) {
      return false;
    }
  }
  return true;
}

bool collect_snapshot_layers(const DocumentEntitySnapshot &snapshot,
                             const SceneDocument &scene,
                             InstanceClipboardPayload &payload,
                             std::string &error) {
  if (snapshot.layer_id != kInvalidSceneLayerId) {
    if (!release_has_feature(ReleaseFeature::Layers)) {
      error = release_feature_unavailable_message();
      return false;
    }
    const SceneLayerRecord *record = scene.layer(snapshot.layer_id);
    if (record == nullptr) {
      error = "Instance references a missing scene layer.";
      return false;
    }
    const auto [existing, inserted] =
        payload.layers.emplace(snapshot.layer_id, *record);
    if (!inserted && !(existing->second == *record)) {
      error = "Instance scene layer payload is inconsistent.";
      return false;
    }
  }
  for (const DocumentEntitySnapshot &child : snapshot.children) {
    if (!collect_snapshot_layers(child, scene, payload, error)) {
      return false;
    }
  }
  return true;
}

std::vector<AssetId> sorted_asset_ids(const auto &assets) {
  std::vector<AssetId> ids;
  ids.reserve(assets.size());
  for (const auto &[id, asset] : assets) {
    (void)asset;
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

MaterialReference remap_material_reference(
    MaterialReference source,
    const std::unordered_map<AssetId, AssetId> &material_remap,
    const ProjectManifest *manifest) {
  if (source.project_owned() && manifest != nullptr) {
    return MaterialReference::project(
        ProjectMaterialReferenceResolver::canonical_material_id(
            *manifest, source.project_asset()));
  }
  if (!source.embedded()) {
    return source;
  }
  const auto remapped = material_remap.find(source.embedded_asset());
  return remapped == material_remap.end() ? MaterialReference{}
                                          : MaterialReference(remapped->second);
}

MaterialComponent remap_material_component(
    MaterialComponent source,
    const std::unordered_map<AssetId, AssetId> &material_remap,
    const ProjectManifest *manifest) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return {.material = manifest != nullptr
                            ? MaterialReference::project(
                                  *ProjectAssetId::parse(kDefaultProjectMaterialId))
                            : MaterialReference{}};
  }
  source.material =
      remap_material_reference(source.material, material_remap, manifest);
  for (auto &[slot, material] : source.material_slots) {
    (void)slot;
    material = remap_material_reference(material, material_remap, manifest);
  }
  return source;
}

void remap_snapshot_materials(
    DocumentEntitySnapshot &snapshot,
    const std::unordered_map<AssetId, AssetId> &material_remap,
    const ProjectManifest *manifest) {
  if (snapshot.material.has_value()) {
    snapshot.material =
        remap_material_component(*snapshot.material, material_remap, manifest);
  }
  for (DocumentEntitySnapshot &child : snapshot.children) {
    remap_snapshot_materials(child, material_remap, manifest);
  }
}

void collect_mesh_snapshots(
    DocumentEntitySnapshot &snapshot,
    std::unordered_map<EntityId, DocumentEntitySnapshot *> &by_source) {
  if (snapshot.mesh.has_value()) {
    by_source.emplace(snapshot.id, &snapshot);
  }
  for (DocumentEntitySnapshot &child : snapshot.children) {
    collect_mesh_snapshots(child, by_source);
  }
}

std::optional<ClipboardLayerImportPlan>
build_layer_import_plan(const InstanceClipboardPayload &payload,
                        const SceneDocument &destination,
                        std::string &error) {
  ClipboardLayerImportPlan plan;
  plan.before = destination.layer_state();
  plan.after = plan.before;

  std::unordered_map<std::string, SceneLayerId> destination_by_name;
  std::unordered_set<SceneLayerId> destination_ids;
  destination_by_name.reserve(plan.before.layers.size() +
                              payload.layers.size());
  destination_ids.reserve(plan.before.layers.size() + payload.layers.size());
  for (const SceneLayerRecord &record : plan.before.layers) {
    destination_by_name.emplace(record.name, record.id);
    destination_ids.emplace(record.id);
  }

  std::vector<SceneLayerId> source_ids;
  source_ids.reserve(payload.layers.size());
  for (const auto &[source_id, record] : payload.layers) {
    (void)record;
    source_ids.push_back(source_id);
  }
  std::sort(source_ids.begin(), source_ids.end());

  SceneLayerId next_id = destination.next_layer_id();
  for (const SceneLayerId source_id : source_ids) {
    const SceneLayerRecord &source = payload.layers.at(source_id);
    const auto same_name = destination_by_name.find(source.name);
    if (same_name != destination_by_name.end()) {
      plan.remap.emplace(source_id, same_name->second);
      continue;
    }

    while (next_id != std::numeric_limits<SceneLayerId>::max() &&
           (next_id == kInvalidSceneLayerId ||
            destination_ids.contains(next_id))) {
      ++next_id;
    }
    if (next_id == std::numeric_limits<SceneLayerId>::max()) {
      error = "Destination scene has no available layer identity.";
      return std::nullopt;
    }

    SceneLayerRecord imported = source;
    imported.id = next_id;
    plan.after.layers.push_back(imported);
    destination_by_name.emplace(imported.name, imported.id);
    destination_ids.emplace(imported.id);
    plan.remap.emplace(source_id, imported.id);
    ++next_id;
  }
  return plan;
}

bool normalize_imported_snapshot_layers(
    DocumentEntitySnapshot &snapshot,
    const std::unordered_map<SceneLayerId, SceneLayerId> &layer_remap) {
  if (snapshot.layer_id != kInvalidSceneLayerId) {
    const auto remapped = layer_remap.find(snapshot.layer_id);
    if (remapped == layer_remap.end()) {
      return false;
    }
    snapshot.layer_id = remapped->second;
  }
  for (DocumentEntitySnapshot &child : snapshot.children) {
    if (!normalize_imported_snapshot_layers(child, layer_remap)) {
      return false;
    }
  }
  return true;
}

bool destination_accepts_instance_paste(const SceneSession &session) {
  return session.modeling().selection_mode() == ModelingSelectionMode::Object &&
         !session.modeling().can_cancel_active_tool_or_preview() &&
         !session.native_tools().has_pending_tool() &&
         !session.instances().instance_edit_state().active;
}

} // namespace

/// Implements project-lifetime capture, linked paste, and cross-scene import.
class ProjectClipboardService::Impl final {
public:
  [[nodiscard]] bool
  selection_contains_instance(const ProjectSession &project) const {
    const OpenSceneSession *active = project.active_scene();
    if (active == nullptr || active->session == nullptr) {
      return false;
    }
    const SceneDocument &scene = active->session->scene();
    for (const EntityId selected : scene.selected_entities()) {
      if (scene.instance_occurrence(selected) != nullptr) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool
  can_copy_instance_selection(const ProjectSession &project) const {
    if (!release_has_feature(ReleaseFeature::Instancing)) {
      return false;
    }
    const OpenSceneSession *active = project.active_scene();
    if (active == nullptr || active->session == nullptr ||
        !destination_accepts_instance_paste(*active->session)) {
      return false;
    }
    const SceneDocument &scene = active->session->scene();
    const std::vector<EntityId> selected = scene.selected_entities();
    if (selected.empty()) {
      return false;
    }
    return std::all_of(selected.begin(), selected.end(), [&](EntityId entity) {
      return occurrence_is_valid(scene, entity);
    });
  }

  CommandResult copy_instance_selection(ProjectSession &project) {
    if (!release_has_feature(ReleaseFeature::Instancing)) {
      return clipboard_failure(release_feature_unavailable_message());
    }
    OpenSceneSession *active = project.active_scene();
    if (active == nullptr || active->session == nullptr) {
      return clipboard_failure("No scene is active.");
    }
    if (!can_copy_instance_selection(project)) {
      return clipboard_failure(
          "Copying instances requires only selectable occurrence roots in "
          "Object mode with no active tool or edit scope.");
    }

    SceneSession &session = *active->session;
    SceneDocument &scene = session.scene();
    InstanceClipboardPayload captured;
    captured.source_scene = active->session_id;
    std::unordered_map<EntityId, std::size_t> definition_indices;

    for (const EntityId occurrence_entity : scene.selected_entities()) {
      const InstanceOccurrenceComponent *occurrence =
          scene.instance_occurrence(occurrence_entity);
      if (occurrence == nullptr) {
        return clipboard_failure("Selected occurrence is no longer valid.");
      }

      std::size_t definition_index = 0u;
      const auto existing = definition_indices.find(occurrence->definition);
      if (existing != definition_indices.end()) {
        definition_index = existing->second;
      } else {
        std::optional<DocumentEntitySnapshot> definition_snapshot =
            document_command_capture_entity_snapshot(scene,
                                                     occurrence->definition);
        if (!definition_snapshot.has_value() ||
            !definition_snapshot->instance_definition ||
            definition_snapshot->children.empty()) {
          return clipboard_failure(
              "Instance definition could not be captured.");
        }
        ClipboardDefinition definition{
            .snapshot = std::move(*definition_snapshot),
        };
        std::string error;
        if (!collect_snapshot_assets(definition.snapshot, session.assets(),
                                     definition, captured, error)) {
          return clipboard_failure(std::move(error));
        }
        if (!collect_snapshot_layers(definition.snapshot, scene, captured,
                                     error)) {
          return clipboard_failure(std::move(error));
        }
        definition_index = captured.definitions.size();
        definition_indices.emplace(occurrence->definition, definition_index);
        captured.definitions.push_back(std::move(definition));
      }

      std::optional<DocumentEntitySnapshot> occurrence_snapshot =
          document_command_capture_entity_snapshot(scene, occurrence_entity);
      if (!occurrence_snapshot.has_value()) {
        return clipboard_failure("Instance occurrence could not be captured.");
      }
      occurrence_snapshot->children.clear();
      std::string error;
      if (!collect_snapshot_layers(*occurrence_snapshot, scene, captured,
                                   error)) {
        return clipboard_failure(std::move(error));
      }
      captured.occurrences.push_back(ClipboardOccurrence{
          .snapshot = std::move(*occurrence_snapshot),
          .parent_world_transform =
              scene.world_transform_matrix(scene.parent(occurrence_entity)),
          .definition_index = definition_index,
      });
    }

    if (captured.definitions.empty() || captured.occurrences.empty()) {
      return clipboard_failure("Instance clipboard payload is empty.");
    }
    payload_ = std::move(captured);
    kind_ = ProjectClipboardKind::Instances;
    ordinary_source_scene_ = {};
    return {
        .ok = true,
        .changed = false,
        .message = payload_.occurrences.size() == 1u ? "Instance copied."
                                                    : "Instances copied.",
    };
  }

  [[nodiscard]] bool has_instance_payload() const {
    return kind_ == ProjectClipboardKind::Instances &&
           !payload_.definitions.empty() && !payload_.occurrences.empty();
  }

  [[nodiscard]] bool
  can_paste_instance_selection(const ProjectSession &project) const {
    if (!release_has_feature(ReleaseFeature::Instancing)) {
      return false;
    }
    const OpenSceneSession *active = project.active_scene();
    if (!has_instance_payload() || active == nullptr ||
        active->session == nullptr ||
        !destination_accepts_instance_paste(*active->session)) {
      return false;
    }
    if (active->session_id != payload_.source_scene) {
      return true;
    }
    const SceneDocument &scene = active->session->scene();
    for (const ClipboardDefinition &definition : payload_.definitions) {
      const EntityId entity = definition.snapshot.id;
      if (!scene.is_instance_definition_entity(entity) ||
          scene.uuid(entity) != definition.snapshot.uuid) {
        return false;
      }
    }
    return true;
  }

  CommandResult paste_instance_selection(ProjectSession &project) {
    if (!release_has_feature(ReleaseFeature::Instancing)) {
      return clipboard_failure(release_feature_unavailable_message());
    }
    if (!release_has_feature(ReleaseFeature::Layers) && !payload_.layers.empty()) {
      return clipboard_failure(release_feature_unavailable_message());
    }
    OpenSceneSession *active = project.active_scene();
    if (active == nullptr || active->session == nullptr) {
      return clipboard_failure("No scene is active.");
    }
    if (!has_instance_payload()) {
      return clipboard_failure("The project instance clipboard is empty.");
    }
    if (!can_paste_instance_selection(project)) {
      return clipboard_failure(
          "The linked instance definition is unavailable, or the active "
          "scene cannot accept a paste right now.");
    }
    return active->session_id == payload_.source_scene
               ? paste_linked(*active->session)
               : paste_imported(*active->session, project.manifest());
  }

  void note_ordinary_copy(SceneSessionId source_scene) {
    kind_ = ProjectClipboardKind::Ordinary;
    ordinary_source_scene_ = source_scene;
    payload_ = {};
  }

  [[nodiscard]] bool has_ordinary_payload() const {
    return kind_ == ProjectClipboardKind::Ordinary &&
           ordinary_source_scene_.valid();
  }

  [[nodiscard]] bool
  ordinary_payload_belongs_to(SceneSessionId scene) const {
    return has_ordinary_payload() && scene == ordinary_source_scene_;
  }

  void clear() {
    kind_ = ProjectClipboardKind::Empty;
    ordinary_source_scene_ = {};
    payload_ = {};
  }

private:
  CommandResult paste_linked(SceneSession &session) {
    std::vector<DocumentEntitySnapshot> occurrences;
    occurrences.reserve(payload_.occurrences.size());
    for (const ClipboardOccurrence &occurrence : payload_.occurrences) {
      occurrences.push_back(occurrence.snapshot);
    }

    const SceneSelectionState selection_before =
        capture_scene_selection(session.scene());
    SceneScalarCompensationCheckpoint scalar_checkpoint =
        session.scene().capture_scalar_compensation_checkpoint();
    if (!scalar_checkpoint.valid()) {
      return clipboard_failure(
          "Scene scalar compensation scope is unavailable.");
    }
    EditorTransaction transaction =
        session.history().begin_transaction("Paste Instances");
    if (!transaction.active()) {
      return clipboard_failure(
          "Cannot paste while another editor transaction is active.");
    }
    const auto preserve_after_scope_failure =
        [&](CommandResult failure, std::string message) {
          (void)session.scene().discard_scalar_compensation_checkpoint(
              scalar_checkpoint);
          const CommandResult preserved =
              transaction.active() ? transaction.commit()
                                   : command_unchanged_result();
          failure.ok = false;
          failure.changed = true;
          if (!failure.message.empty()) {
            failure.message += "; ";
          }
          failure.message += std::move(message);
          if (!preserved.ok) {
            failure.message +=
                "; applied paste could not be retained in history: ";
            failure.message += preserved.message;
          }
          return failure;
        };
    const auto rollback = [&](CommandResult failure,
                              bool restore_started = false) {
      if (!restore_started &&
          (!session.scene().acknowledge_scalar_compensation_checkpoint(
               scalar_checkpoint) ||
           !session.scene().begin_scalar_compensation_restore(
               scalar_checkpoint))) {
        return preserve_after_scope_failure(
            std::move(failure),
            "Paste rollback was rejected because the Scene changed outside "
            "the compensation scope.");
      }
      const CommandResult canceled =
          transaction.active() ? transaction.cancel()
                               : command_unchanged_result();
      const bool selection_restored = same_scene_selection(
          selection_before, capture_scene_selection(session.scene()));
      if (!canceled.ok || !selection_restored) {
        (void)session.scene().discard_scalar_compensation_checkpoint(
            scalar_checkpoint);
        failure.ok = false;
        failure.changed = true;
        if (!failure.message.empty()) {
          failure.message += "; ";
        }
        failure.message += !canceled.ok
                               ? "paste transaction rollback failed: " +
                                     canceled.message
                               : "paste rollback did not restore the exact "
                                 "scene selection";
        return failure;
      }
      if (!session.scene().restore_scalar_compensation_checkpoint(
              scalar_checkpoint)) {
        failure.ok = false;
        failure.changed = true;
        if (!failure.message.empty()) {
          failure.message += "; ";
        }
        failure.message += "paste scalar compensation failed";
        return failure;
      }
      failure.changed = false;
      return failure;
    };
    auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(
        session.scene(), std::move(occurrences),
        std::unordered_map<EntityId, EntityId>{}, SceneEntityNamePolicy::Unique);
    const CommandResult duplicated = transaction.execute(std::move(command));
    if (!duplicated.ok || !duplicated.changed) {
      return rollback(duplicated);
    }
    if (!session.scene().acknowledge_scalar_compensation_checkpoint(
            scalar_checkpoint)) {
      return preserve_after_scope_failure(
          duplicated,
          "Paste scalar compensation boundary could not be acknowledged.");
    }
    const DocumentCommandResult projected =
        session.selection().publish_scene_selection();
    if (!session.scene().begin_scalar_compensation_restore(
            scalar_checkpoint)) {
      return preserve_after_scope_failure(
          CommandResult{.ok = projected.ok,
                        .changed = projected.changed,
                        .message = projected.message},
          "Scene changed during pasted-selection publication; rollback was "
          "rejected.");
    }
    if (!projected.ok) {
      return rollback(
          {
              .ok = false,
              .changed = projected.changed,
              .message = projected.message.empty()
                             ? "Pasted instance selection could not be "
                               "projected."
                             : projected.message,
          },
          true);
    }
    const CommandResult committed = transaction.commit();
    if (!session.scene().discard_scalar_compensation_checkpoint(
            scalar_checkpoint)) {
      return {.ok = false,
              .changed = true,
              .message = "Paste scalar compensation scope could not be "
                         "released."};
    }
    return committed.ok
               ? CommandResult{
                     .ok = true,
                     .changed = committed.changed,
                     .message = payload_.occurrences.size() == 1u
                                    ? "Linked instance pasted."
                                    : "Linked instances pasted.",
                 }
               : committed;
  }

  CommandResult paste_imported(SceneSession &session,
                               const ProjectManifest *manifest) {
    std::string layer_error;
    std::optional<ClipboardLayerImportPlan> layer_plan =
        build_layer_import_plan(payload_, session.scene(), layer_error);
    if (!layer_plan.has_value()) {
      return clipboard_failure(std::move(layer_error));
    }

    const SceneSelectionState selection_before =
        capture_scene_selection(session.scene());
    SceneScalarCompensationCheckpoint scalar_checkpoint =
        session.scene().capture_scalar_compensation_checkpoint();
    if (!scalar_checkpoint.valid()) {
      return clipboard_failure(
          "Scene scalar compensation scope is unavailable.");
    }
    EditorTransaction transaction =
        session.history().begin_transaction("Paste Instances");
    if (!transaction.active()) {
      return clipboard_failure(
          "Cannot paste while another editor transaction is active.");
    }
    const ModelingSessionMemento before_modeling =
        session.modeling().capture_history_memento();
    const auto preserve_after_scope_failure =
        [&](CommandResult failure, std::string message) {
          (void)session.scene().discard_scalar_compensation_checkpoint(
              scalar_checkpoint);
          const CommandResult preserved =
              transaction.active() ? transaction.commit()
                                   : command_unchanged_result();
          failure.ok = false;
          failure.changed = true;
          if (!failure.message.empty()) {
            failure.message += "; ";
          }
          failure.message += std::move(message);
          if (!preserved.ok) {
            failure.message +=
                "; applied paste could not be retained in history: ";
            failure.message += preserved.message;
          }
          return failure;
        };
    const auto rollback = [&](CommandResult failure,
                              bool restore_started = false) {
      if (!restore_started &&
          (!session.scene().acknowledge_scalar_compensation_checkpoint(
               scalar_checkpoint) ||
           !session.scene().begin_scalar_compensation_restore(
               scalar_checkpoint))) {
        return preserve_after_scope_failure(
            std::move(failure),
            "Paste rollback was rejected because the Scene changed outside "
            "the compensation scope.");
      }
      const CommandResult canceled =
          transaction.active() ? transaction.cancel()
                               : command_unchanged_result();
      const ModelingCommandResult restored =
          session.modeling().restore_history_memento(before_modeling);
      const bool selection_restored = same_scene_selection(
          selection_before, capture_scene_selection(session.scene()));
      if (!canceled.ok || !restored.ok || !selection_restored) {
        (void)session.scene().discard_scalar_compensation_checkpoint(
            scalar_checkpoint);
        failure.ok = false;
        failure.changed = true;
        if (!failure.message.empty()) {
          failure.message += "; ";
        }
        if (!canceled.ok) {
          failure.message += "paste transaction rollback failed: ";
          failure.message += canceled.message;
        } else if (!restored.ok) {
          failure.message += "paste modeling rollback failed: ";
          failure.message += restored.message;
        } else {
          failure.message +=
              "paste rollback did not restore the exact scene selection";
        }
        return failure;
      }
      if (!session.scene().restore_scalar_compensation_checkpoint(
              scalar_checkpoint)) {
        failure.ok = false;
        failure.changed = true;
        if (!failure.message.empty()) {
          failure.message += "; ";
        }
        failure.message += "paste scalar compensation failed";
        return failure;
      }
      failure.changed = false;
      return failure;
    };

    if (!(layer_plan->before == layer_plan->after)) {
      const CommandResult layers_imported = transaction.execute(
          std::make_unique<SetSceneLayerStateCommand>(
              session.scene(), "Import Instance Layers", layer_plan->before,
              layer_plan->after));
      if (!layers_imported.ok || !layers_imported.changed) {
        return rollback(layers_imported.ok
                            ? clipboard_failure(
                                  "Instance scene layers could not be imported.")
                            : layers_imported);
      }
    }

    std::unordered_map<AssetId, AssetId> texture_remap;
    for (const AssetId source_id : sorted_asset_ids(payload_.textures)) {
      TextureAsset texture = payload_.textures.at(source_id);
      texture.id = kInvalidAssetId;
      const AssetCreateResult added =
          session.asset_commands().add_texture(std::move(texture));
      if (!added.ok || !added.changed || added.asset == kInvalidAssetId) {
        return rollback(clipboard_failure(
            added.message.empty() ? "Embedded texture could not be imported."
                                  : added.message));
      }
      texture_remap.emplace(source_id, added.asset);
    }

    std::unordered_map<AssetId, AssetId> material_remap;
    for (const AssetId source_id : sorted_asset_ids(payload_.materials)) {
      MaterialDocument material = payload_.materials.at(source_id);
      material.id = kInvalidAssetId;
      for (auto &[slot, texture] : material.texture_slots) {
        (void)slot;
        const auto remapped = texture_remap.find(texture);
        if (remapped == texture_remap.end()) {
          return rollback(clipboard_failure(
              "Embedded material texture remap is incomplete."));
        }
        texture = remapped->second;
      }
      const AssetCreateResult added =
          session.asset_commands().add_material(std::move(material));
      if (!added.ok || !added.changed || added.asset == kInvalidAssetId) {
        return rollback(clipboard_failure(
            added.message.empty() ? "Embedded material could not be imported."
                                  : added.message));
      }
      material_remap.emplace(source_id, added.asset);
    }

    std::vector<EntityId> imported_definitions;
    std::vector<DuplicateSelectedHierarchyCommand *> imported_commands;
    std::vector<EngineEntityClone> engine_clones;
    imported_definitions.reserve(payload_.definitions.size());
    for (const ClipboardDefinition &source_definition :
         payload_.definitions) {
      DocumentEntitySnapshot definition = source_definition.snapshot;
      definition.parent = kInvalidEntityId;
      if (!normalize_imported_snapshot_layers(definition,
                                               layer_plan->remap)) {
        return rollback(clipboard_failure(
            "Instance definition layer remap is incomplete."));
      }
      remap_snapshot_materials(definition, material_remap, manifest);

      std::unordered_map<EntityId, DocumentEntitySnapshot *> mesh_snapshots;
      collect_mesh_snapshots(definition, mesh_snapshots);
      if (mesh_snapshots.size() != source_definition.meshes.size()) {
        return rollback(clipboard_failure(
            "Instance definition mesh payload is incomplete."));
      }

      std::unordered_map<EntityId, EntityId> imported_meshes;
      for (const auto &[source_entity, source_mesh] :
           source_definition.meshes) {
        const auto snapshot_found = mesh_snapshots.find(source_entity);
        if (snapshot_found == mesh_snapshots.end() ||
            !source_mesh.authored_polygon_document) {
          return rollback(clipboard_failure(
              "Instance mesh snapshot is unavailable."));
        }
        DocumentEntitySnapshot &snapshot = *snapshot_found->second;
        const MaterialReference material =
            snapshot.material.has_value() ? snapshot.material->material
                                          : MaterialReference{};
        const ModelingCreateBoxResult created = session.modeling().create_mesh(
            ModelingCreateMeshRequest{
                .document = *source_mesh.authored_polygon_document,
                .name = snapshot.name,
                .material_slot = snapshot.modeling_object->material_slot,
                .material = material,
            });
        if (!created.ok || !created.changed ||
            created.object.entity == kInvalidEntityId ||
            created.object.mesh == kInvalidAssetId) {
          return rollback(clipboard_failure(
              created.message.empty() ? "Instance mesh could not be imported."
                                      : created.message));
        }

        MeshAsset shared_mesh = clone_mesh_asset_payload(source_mesh);
        shared_mesh.id = created.object.mesh;
        // Remove the just-compiled projection before installing the captured
        // payload. Otherwise the destination registry's local intern bucket
        // prefers its already-live equivalent payload over the project-wide
        // immutable pointer retained by the clipboard.
        if (!session.assets().remove_mesh(created.object.mesh) ||
            session.assets().add_mesh(created.object.mesh,
                                      std::move(shared_mesh)) !=
                created.object.mesh) {
          return rollback(
              clipboard_failure("Shared mesh payload could not be imported."));
        }
        MeshComponent mesh = *snapshot.mesh;
        mesh.mesh = created.object.mesh;
        session.scene().set_mesh(created.object.entity, mesh);
        if (snapshot.material.has_value()) {
          session.scene().set_material(created.object.entity,
                                       *snapshot.material);
        } else {
          session.scene().clear_material(created.object.entity);
        }
        imported_meshes.emplace(source_entity, created.object.entity);
      }

      auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(
          session.scene(),
          std::vector<DocumentEntitySnapshot>{std::move(definition)},
          std::move(imported_meshes), SceneEntityNamePolicy::Unique);
      DuplicateSelectedHierarchyCommand *definition_command = command.get();
      const CommandResult duplicated = transaction.execute(std::move(command));
      if (!duplicated.ok || !duplicated.changed ||
          definition_command->duplicate_roots().size() != 1u) {
        return rollback(duplicated.ok
                            ? clipboard_failure(
                                  "Instance definition could not be imported.")
                            : duplicated);
      }
      imported_definitions.push_back(
          definition_command->duplicate_roots().front());
      imported_commands.push_back(definition_command);
      engine_clones.insert(engine_clones.end(), definition_command->engine_clones().begin(), definition_command->engine_clones().end());
    }

    std::vector<DocumentEntitySnapshot> occurrences;
    occurrences.reserve(payload_.occurrences.size());
    const EntityId destination_parent = kInvalidEntityId;
    const QMat4 parent_world =
        session.scene().world_transform_matrix(destination_parent);
    for (const ClipboardOccurrence &source : payload_.occurrences) {
      if (source.definition_index >= imported_definitions.size()) {
        return rollback(
            clipboard_failure("Occurrence definition remap is invalid."));
      }
      DocumentEntitySnapshot occurrence = source.snapshot;
      occurrence.parent = destination_parent;
      if (!normalize_imported_snapshot_layers(occurrence,
                                               layer_plan->remap)) {
        return rollback(clipboard_failure(
            "Instance occurrence layer remap is incomplete."));
      }
      occurrence.instance_occurrence = InstanceOccurrenceComponent{
          .definition = imported_definitions[source.definition_index],
      };
      const std::optional<TransformComponent> local =
          try_scene_reparented_transform_preserving_world(
              parent_world, source.parent_world_transform,
              occurrence.transform);
      if (!local.has_value()) {
        return rollback(clipboard_failure(
            "Occurrence world transform could not be preserved."));
      }
      occurrence.transform = *local;
      occurrences.push_back(std::move(occurrence));
    }

    auto occurrence_command =
        std::make_unique<DuplicateSelectedHierarchyCommand>(
            session.scene(), std::move(occurrences),
            std::unordered_map<EntityId, EntityId>{},
            SceneEntityNamePolicy::Unique);
    auto *occurrence_records = occurrence_command.get();
    const CommandResult occurrences_created =
        transaction.execute(std::move(occurrence_command));
    if (!occurrences_created.ok || !occurrences_created.changed) {
      return rollback(occurrences_created);
    }
    imported_commands.push_back(occurrence_records);
    engine_clones.insert(engine_clones.end(), occurrence_records->engine_clones().begin(), occurrence_records->engine_clones().end());
    if (!session.scene().remap_engine_entity_references(engine_clones))
      return rollback(clipboard_failure("Imported entity references could not be remapped."));
    for (auto *command : imported_commands)
      if (!command->retain_remapped_engine_components())
        return rollback(clipboard_failure("Imported component state could not be retained."));

    const CommandResult recorded = transaction.record_executed_before_existing(
        std::make_unique<RecordedModelingMementoCommand>(
            session.modeling(), "Paste Instances", before_modeling,
            session.modeling().capture_history_memento(), true));
    if (!recorded.ok) {
      return rollback(recorded);
    }
    if (!session.scene().acknowledge_scalar_compensation_checkpoint(
            scalar_checkpoint)) {
      return preserve_after_scope_failure(
          recorded,
          "Paste scalar compensation boundary could not be acknowledged.");
    }
    const DocumentCommandResult projected =
        session.selection().publish_scene_selection();
    if (!session.scene().begin_scalar_compensation_restore(
            scalar_checkpoint)) {
      return preserve_after_scope_failure(
          CommandResult{.ok = projected.ok,
                        .changed = projected.changed,
                        .message = projected.message},
          "Scene changed during imported-selection publication; rollback was "
          "rejected.");
    }
    if (!projected.ok) {
      return rollback(
          {
              .ok = false,
              .changed = projected.changed,
              .message = projected.message.empty()
                             ? "Imported instance selection could not be "
                               "projected."
                             : projected.message,
          },
          true);
    }
    const CommandResult committed = transaction.commit();
    if (!session.scene().discard_scalar_compensation_checkpoint(
            scalar_checkpoint)) {
      return {
          .ok = false,
          .changed = true,
          .message = "Paste scalar compensation scope could not be released.",
      };
    }
    if (!committed.ok || !committed.changed) {
      return committed;
    }
    return {
        .ok = true,
        .changed = true,
        .message = payload_.occurrences.size() == 1u
                       ? "Independent scene instance pasted."
                       : "Independent scene instances pasted.",
    };
  }

  ProjectClipboardKind kind_ = ProjectClipboardKind::Empty;
  SceneSessionId ordinary_source_scene_;
  InstanceClipboardPayload payload_;
};

ProjectClipboardService::ProjectClipboardService()
    : impl_(std::make_unique<Impl>()) {}

ProjectClipboardService::~ProjectClipboardService() = default;

ProjectClipboardService::ProjectClipboardService(
    ProjectClipboardService &&) noexcept = default;

ProjectClipboardService &ProjectClipboardService::operator=(
    ProjectClipboardService &&) noexcept = default;

bool ProjectClipboardService::selection_contains_instance(
    const ProjectSession &project) const {
  return impl_->selection_contains_instance(project);
}

bool ProjectClipboardService::can_copy_instance_selection(
    const ProjectSession &project) const {
  return impl_->can_copy_instance_selection(project);
}

CommandResult ProjectClipboardService::copy_instance_selection(
    ProjectSession &project) {
  return impl_->copy_instance_selection(project);
}

bool ProjectClipboardService::has_instance_payload() const {
  return impl_->has_instance_payload();
}

bool ProjectClipboardService::can_paste_instance_selection(
    const ProjectSession &project) const {
  return impl_->can_paste_instance_selection(project);
}

CommandResult ProjectClipboardService::paste_instance_selection(
    ProjectSession &project) {
  return impl_->paste_instance_selection(project);
}

void ProjectClipboardService::note_ordinary_copy(
    SceneSessionId source_scene) {
  impl_->note_ordinary_copy(source_scene);
}

bool ProjectClipboardService::has_ordinary_payload() const {
  return impl_->has_ordinary_payload();
}

bool ProjectClipboardService::ordinary_payload_belongs_to(
    SceneSessionId scene) const {
  return impl_->ordinary_payload_belongs_to(scene);
}

void ProjectClipboardService::clear() { impl_->clear(); }

} // namespace quader
