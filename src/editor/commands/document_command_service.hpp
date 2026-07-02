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
#include "scene/components.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

class SceneDocument;
class AssetRegistry;

/**
 * Coordinates Document Command Service responsibilities for the editor document ownership layer.
 */
class DocumentCommandService {
public:
    DocumentCommandService(SceneDocument& scene, EditorHistoryService& history, const AssetRegistry* assets = nullptr);

    DocumentCreateEntityResult
    create_entity(std::string name, EntityKind kind = EntityKind::Generic,
                  EntityId parent = kInvalidEntityId,
                  TransformComponent transform = TransformComponent{});
    DocumentCommandResult destroy_entity(EntityId entity);
    DocumentCommandResult reparent_entity(EntityId entity, EntityId parent,
                                          bool preserve_world_transform = true);
    DocumentGroupSelectionResult
    group_selection(std::string group_name = {},
                    TransformComponent group_transform = TransformComponent{});
    DocumentCommandResult ungroup_selection();
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
                                          EntityId active = kInvalidEntityId);
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
    set_light_component(EntityId entity, LightComponent light,
                        UndoRedoExecuteOptions options = {});
    DocumentCommandResult
    set_scene_settings(EntityId entity, SceneSettingsComponent settings,
                       UndoRedoExecuteOptions options = {});
    DocumentCommandResult set_transform(EntityId entity,
                                        TransformComponent transform);
    DocumentCommandResult
    set_transforms(std::vector<DocumentTransformEdit> edits,
                   std::string label = "Set Transform");
    DocumentCommandResult
    apply_scene_batch(std::string label,
                      std::vector<DocumentSceneBatchOperation> operations);

  private:
    SceneDocument& scene_;
    EditorHistoryService& history_;
    const AssetRegistry* assets_ = nullptr;
};

} // namespace quader
