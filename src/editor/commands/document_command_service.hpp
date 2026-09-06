////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "editor/commands/document/scene_texture_mapping_plan.hpp"
#include "scene/components.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

class SceneDocument;
class AssetRegistry;

/** Carries one validated document command before history executes it. */
struct PreparedDocumentCommand final {
  DocumentCommandResult status;
  std::unique_ptr<IUndoableCommand> command;
};

/**
 * Coordinates Document Command Service responsibilities for the editor document ownership layer.
 */
class DocumentCommandService {
public:
    using EntityMutationBlocker = std::function<bool(EntityId)>;
    using CheckedSelectionPublication = std::function<CommandResult()>;

    DocumentCommandService(SceneDocument& scene, EditorHistoryService& history, const AssetRegistry* assets = nullptr);
    void set_entity_mutation_blocker(EntityMutationBlocker blocker);
    void set_selection_publication(CheckedSelectionPublication publication);
    void set_texture_lock_settings_provider(
        std::function<TextureLockSettings()> provider);
    [[nodiscard]] TextureLockSettings texture_lock_settings() const;

    DocumentCreateEntityResult
    create_entity(std::string name, EntityKind kind = EntityKind::Generic,
                  EntityId parent = kInvalidEntityId,
                  TransformComponent transform = TransformComponent{});
    DocumentCommandResult destroy_entity(EntityId entity);
    DocumentCommandResult reparent_entity(EntityId entity, EntityId parent,
                                          bool preserve_world_transform = true);
    DocumentCommandResult move_entities(
        std::span<const EntityId> entities, EntityId parent,
        EntityId before_sibling = kInvalidEntityId);
    DocumentGroupSelectionResult
    group_selection(std::string group_name = {},
                    TransformComponent group_transform = TransformComponent{});
    DocumentCommandResult ungroup_selection();
    [[nodiscard]] PreparedDocumentCommand prepare_group_selection_command(
        std::string group_name = {},
        TransformComponent group_transform = TransformComponent{});
    [[nodiscard]] PreparedDocumentCommand
    prepare_ungroup_selection_command();
    DocumentDuplicateEntityResult duplicate_entity(EntityId entity);
    DocumentDuplicateHierarchyResult duplicate_selected_hierarchy();
    DocumentDuplicateHierarchyResult duplicate_hierarchy_with_existing_clones(
        std::vector<EntityId> roots,
        std::unordered_map<EntityId, EntityId> existing_clones_by_source);
    DocumentCreateEntityResult
    create_point_light(std::string name, LightComponent light,
                       EntityId parent = kInvalidEntityId,
                       TransformComponent transform = TransformComponent{},
                       bool select_created = false);
    DocumentCreateEntityResult create_directional_light(
        std::string name, LightComponent light,
        EntityId parent = kInvalidEntityId,
        TransformComponent transform = TransformComponent{},
        bool select_created = false);
    DocumentCommandResult select_entities(std::vector<EntityId> entities,
                                          EntityId active = kInvalidEntityId,
                                          SceneSelectionApplicationMode mode =
                                              SceneSelectionApplicationMode::
                                                  UndoableCommand,
                                          CheckedSelectionPublication
                                              publication = {});
    DocumentCommandResult apply_entity_transform(EntityId entity,
                                                 TransformComponent transform);
    DocumentCommandResult rename_entity(EntityId entity, std::string name);
    DocumentCommandResult set_visible(EntityId entity, bool visible);
    DocumentCommandResult
    set_visibility(std::vector<DocumentVisibilityEdit> edits);
    DocumentCommandResult set_locked(EntityId entity, bool locked);
    DocumentCommandResult set_locks(std::vector<DocumentLockEdit> edits);
    DocumentCommandResult set_material(EntityId entity, AssetId material);
    DocumentCommandResult set_mesh_component(EntityId entity,
                                             MeshComponent mesh);
    DocumentCommandResult
    set_curve_component(EntityId entity, CurveComponent curve,
                        UndoRedoExecuteOptions options = {});
    DocumentCommandResult
    set_light_component(EntityId entity, LightComponent light,
                        UndoRedoExecuteOptions options = {});
    DocumentCommandResult
    set_scene_settings(EntityId entity, SceneSettingsComponent settings,
                       UndoRedoExecuteOptions options = {});
    DocumentCommandResult set_transform(EntityId entity,
                                        TransformComponent transform);
    DocumentCommandResult
    set_transforms(std::vector<DocumentTransformEdit> edits,
                   std::string label = "Set Transform",
                   SceneTextureTransformKind kind = SceneTextureTransformKind::Combined);
    DocumentCreateLayerResult
    create_layer(std::string name, QVec3 wireframe_color,
                 bool add_selection = false);
    DocumentCommandResult rename_layer(SceneLayerId layer_id,
                                       std::string name);
    DocumentCommandResult set_layer_visible(SceneLayerId layer_id,
                                            bool visible);
    DocumentCommandResult set_layer_wireframe_color(SceneLayerId layer_id,
                                                    QVec3 color);
    DocumentCommandResult set_layer_locked(SceneLayerId layer_id,
                                           bool locked);
    DocumentCommandResult add_selection_to_layer(SceneLayerId layer_id);
    DocumentCommandResult
    remove_selection_from_layer(SceneLayerId layer_id);
    DocumentCommandResult remove_selection_from_all_layers();
    DocumentCommandResult empty_layer(SceneLayerId layer_id);
    DocumentCommandResult delete_layers(std::vector<SceneLayerId> layer_ids);
    DocumentCommandResult
    apply_scene_batch(std::string label,
                      std::vector<DocumentSceneBatchOperation> operations);

  private:
    [[nodiscard]] bool mutation_blocked(EntityId entity) const;
    [[nodiscard]] bool selected_mutation_blocked() const;
    [[nodiscard]] bool
    entities_mutation_blocked(std::span<const EntityId> entities) const;

    SceneDocument& scene_;
    EditorHistoryService& history_;
    const AssetRegistry* assets_ = nullptr;
    EntityMutationBlocker entity_mutation_blocker_;
    CheckedSelectionPublication selection_publication_;
    std::function<TextureLockSettings()> texture_lock_settings_provider_;
};

} // namespace quader
