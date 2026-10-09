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
#include "scene/scene_entity_naming.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace quader {

class SceneDocument;
class AssetRegistry;

/** Carries one validated document command before history executes it. */
struct PreparedDocumentCommand final {
  DocumentCommandResult status;
  std::unique_ptr<IUndoableCommand> command;
};

/// Lets the session track clean content across a live document-property preview.
struct DocumentPropertyPreviewCheckpointHooks {
  std::function<void()> begin;
  std::function<void()> observe_before_projection;
  std::function<void()> record_after_projection;
  std::function<void(bool restored_by_cancel)> finish;
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
    /** Native sessions restrict whole-object authoring to Object Mode. */
    void set_object_mode_provider(std::function<bool()> provider) {
      object_mode_provider_ = std::move(provider);
    }
    [[nodiscard]] bool object_mode_active() const {
      return !object_mode_provider_ || object_mode_provider_();
    }
    void set_texture_lock_settings_provider(
        std::function<TextureLockSettings()> provider);
    [[nodiscard]] TextureLockSettings texture_lock_settings() const;

    DocumentCreateEntityResult
    create_entity(std::string name, EntityKind kind = EntityKind::Generic,
                  EntityId parent = kInvalidEntityId,
                  TransformComponent transform = TransformComponent{},
                  bool select_created = false);
    DocumentCommandResult destroy_entity(EntityId entity);
    DocumentCommandResult reparent_entity(EntityId entity, EntityId parent,
                                          bool preserve_world_transform = true);
    DocumentCommandResult move_entities(
        std::span<const EntityId> entities, EntityId parent,
        EntityId before_sibling = kInvalidEntityId);
    DocumentCommandResult parent_selection_to_active();
    DocumentCommandResult unparent_selection();
    [[nodiscard]] bool can_parent_selection_to_active() const;
    [[nodiscard]] bool can_unparent_selection() const;
    [[nodiscard]] bool can_group_selection() const;
    [[nodiscard]] bool can_ungroup_selection() const;
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
        std::unordered_map<EntityId, EntityId> existing_clones_by_source,
        SceneEntityNamePolicy name_policy = SceneEntityNamePolicy::DuplicateFamily);
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
                                              publication = {},
                                          UndoRedoExecuteOptions options = {},
                                          std::optional<std::uint64_t>
                                              expected_history_revision = {});
    [[nodiscard]] std::uint64_t history_revision() const;
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
    /// A range preview publishes live light values and records only its finish.
    DocumentCommandResult begin_point_light_range_edit(EntityId entity);
    DocumentCommandResult update_point_light_range_edit(EntityId entity, float range);
    DocumentCommandResult finish_point_light_range_edit(EntityId entity, bool cancel);
    DocumentCommandResult begin_light_color_edit(EntityId entity);
    DocumentCommandResult update_light_color_edit(EntityId entity, std::uint64_t generation, QVec3 color);
    DocumentCommandResult finish_light_color_edit(EntityId entity, std::uint64_t generation, bool cancel);
    DocumentCommandResult begin_environment_color_edit();
    DocumentCommandResult update_environment_color_edit(std::uint64_t generation, QVec3 color);
    DocumentCommandResult finish_environment_color_edit(std::uint64_t generation, bool cancel);
    DocumentCommandResult begin_fog_color_edit();
    DocumentCommandResult update_fog_color_edit(std::uint64_t generation, QVec3 color);
    DocumentCommandResult finish_fog_color_edit(std::uint64_t generation, bool cancel);
    /// Accepts a valid pending property edit; rejection retains its draft.
    DocumentCommandResult accept_active_property_edit();
    DocumentCommandResult cancel_active_property_edit();
    DocumentCommandResult cancel_active_point_light_range_edit();
    void abandon_property_edit() { property_edit_.reset(); }
    [[nodiscard]] bool property_edit_active() const { return property_edit_.has_value(); }
    [[nodiscard]] std::uint64_t property_edit_generation() const {
      return property_edit_ ? property_edit_generation_ : 0u;
    }
    [[nodiscard]] bool point_light_range_edit_active() const;
    [[nodiscard]] bool entity_property_edit_active() const {
      return property_edit_ && property_edit_->entity != kInvalidEntityId;
    }
    void set_property_preview_hooks(DocumentPropertyPreviewCheckpointHooks hooks) {
      property_preview_hooks_ = std::move(hooks);
    }
    DocumentCommandResult
    set_scene_settings(SceneSettings settings,
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
    DocumentCommandResult move_entities_impl(
        std::span<const EntityId> entities, EntityId parent,
        EntityId before_sibling, bool retain_subtrees,
        bool leave_direct_children, std::string label);
    [[nodiscard]] bool mutation_blocked(EntityId entity) const;
    [[nodiscard]] bool selected_mutation_blocked() const;
    [[nodiscard]] bool
    entities_mutation_blocked(std::span<const EntityId> entities) const;

    SceneDocument& scene_;
    EditorHistoryService& history_;
    const AssetRegistry* assets_ = nullptr;
    EntityMutationBlocker entity_mutation_blocker_;
    CheckedSelectionPublication selection_publication_;
    std::function<bool()> object_mode_provider_;
    std::function<TextureLockSettings()> texture_lock_settings_provider_;
    /// Each preview owns only its named fields; unrelated edits remain intact.
    struct PointLightRangeEdit {
      float before = 0.0f;
      float expected = 0.0f;
    };
    /// Fields changed by a manual RGB edit, including its authored mode.
    struct LightColorValue {
      QVec3 color;
      LightColorMode mode = LightColorMode::Color;
      LightSourcePreset preset = LightSourcePreset::Custom;
    };
    /// Captures initial and most recently published light-color properties.
    struct LightColorEdit {
      LightColorValue before;
      LightColorValue expected;
    };
    /// Captures only the environment clear color.
    struct EnvironmentColorEdit {
      QVec3 before;
      QVec3 expected;
    };
    /// Captures only the fog color; other settings remain independently authored.
    struct FogColorEdit {
      QVec3 before;
      QVec3 expected;
    };
    /// Pins entity previews to their identity; document previews carry only typed values.
    struct PropertyEdit {
      EntityId entity = kInvalidEntityId;
      std::string uuid;
      std::variant<PointLightRangeEdit, LightColorEdit, EnvironmentColorEdit,
                   FogColorEdit> value;
    };
    DocumentCommandResult finish_property_edit(bool cancel);
    DocumentCommandResult settle_property_edit(DocumentCommandResult result, bool restored);
    void advance_property_edit_generation();
    std::optional<PropertyEdit> property_edit_;
    std::uint64_t property_edit_generation_ = 0u;
    DocumentPropertyPreviewCheckpointHooks property_preview_hooks_;
};

} // namespace quader
