////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"

#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;
class QuaderSceneApi;
class SceneTransformDragService;
class SceneTransformPreviewTransaction;

/// Pins a color preview to both the scene entity and one edit lifetime.
struct QuaderScenePropertyEditRef {
  QuaderEntityRef entity;
  std::uint64_t generation = 0u;
};

/**
 * Represents a scene transform edit used by the public Quader API surface.
 */
struct QuaderSceneTransformEdit {
  EntityId entity = kInvalidEntityId;
  QuaderTransform before;
  QuaderTransform after;
};

/**
 * Represents a scene visibility edit used by the public Quader API surface.
 */
struct QuaderSceneVisibilityEdit {
  EntityId entity = kInvalidEntityId;
  bool before = true;
  bool after = true;
};

/**
 * Describes optional history behavior for live scene edits.
 */
struct QuaderSceneEditOptions {
  std::string merge_key;
};

/**
 * Represents a scene lock edit used by the public Quader API surface.
 */
struct QuaderSceneLockEdit {
  EntityId entity = kInvalidEntityId;
  bool before = false;
  bool after = false;
};

/**
 * Groups scene mutations into one public API transaction.
 *
 * Use editor.scene().batch("Label") when a user action or script should rename,
 * reparent, transform, hide, or lock several entities and commit them as one
 * undoable history entry. The batch records requested editor intent only; the
 * actual validation, ownership invariants, history mutation, and selection of
 * low-level services remain behind QuaderSceneApi::commit_batch().
 *
 * A batch is single-use: build the operation list, call commit(), and then
 * discard it. Individual calls are intentionally chainable so public examples
 * can express grouped scene edits without reaching into document-owner internals.
 */
class QuaderSceneBatch final {
public:
    QuaderSceneBatch(QuaderSceneApi& scene, std::string label);
    QuaderSceneBatch(const QuaderSceneBatch&) = delete;
    QuaderSceneBatch& operator=(const QuaderSceneBatch&) = delete;
    QuaderSceneBatch(QuaderSceneBatch&& other) noexcept;
    QuaderSceneBatch& operator=(QuaderSceneBatch&& other) noexcept;

    QuaderSceneBatch& rename(EntityId entity, std::string name);
    QuaderSceneBatch &reparent(EntityId entity, EntityId parent,
                               bool preserve_world_transform = true);
    QuaderSceneBatch &set_transform(EntityId entity, QuaderTransform transform);
    QuaderSceneBatch &set_visible(EntityId entity, bool visible);
    QuaderSceneBatch &set_locked(EntityId entity, bool locked);
    QuaderSceneBatch &set_mesh_component(EntityId entity,
                                         QuaderMeshComponent mesh);
    QuaderApiVoidResult commit();

private:
    friend class QuaderSceneApi;

    /**
     * Enumerates OperationKind values used by the api layer.
     */
    enum class OperationKind {
        Rename,
        Reparent,
        SetTransform,
        SetVisible,
        SetLocked,
        SetMeshComponent,
    };

    /**
     * Implements the Operation modeling operation data used by the public Quader API surface.
     */
    struct Operation {
        OperationKind kind = OperationKind::Rename;
        EntityId entity = kInvalidEntityId;
        EntityId parent = kInvalidEntityId;
        bool preserve_world_transform = true;
        bool flag = false;
        QuaderTransform transform;
        QuaderMeshComponent mesh;
        std::string name;
    };

    QuaderSceneApi* scene_ = nullptr;
    SceneSessionId scene_session_id_;
    std::string label_;
    std::vector<Operation> operations_;
    bool committed_ = false;
};

/**
 * Exposes Quader Scene API operations through the public Quader API surface.
 */
class QuaderSceneApi final {
public:
    QuaderSceneApi(QuaderApiSession session, QuaderApiMutationRunner& mutations);

    QuaderApiResult<QuaderEntityRef>
    create_entity(const QuaderCreateEntityArgs &args);
    QuaderApiResult<QuaderEntityRef> create_point_light(
        const QuaderCreatePointLightArgs &args = QuaderCreatePointLightArgs{});
    QuaderApiResult<QuaderEntityRef>
    create_directional_light(const QuaderCreateDirectionalLightArgs &args =
                                 QuaderCreateDirectionalLightArgs{});
    QuaderApiVoidResult destroy(EntityId entity);
    QuaderApiVoidResult rename(EntityId entity, std::string name);
    QuaderApiVoidResult reparent(EntityId entity, EntityId parent,
                                 bool preserve_world_transform = true);
    /**
     * Moves the supplied hierarchy roots in authored tree order as one undo step.
     * Descendants of supplied roots are deduplicated. An invalid parent means
     * the scene root; an invalid insertion sibling appends to the destination.
     * World transforms and selection are preserved; invalid moves are atomic.
     */
    QuaderApiVoidResult move_entities(
        std::span<const EntityId> entities, EntityId parent,
        EntityId before_sibling = kInvalidEntityId);
    QuaderApiVoidResult set_transform(EntityId entity,
                                      QuaderTransform transform);
    QuaderApiVoidResult
    set_transforms(std::span<const QuaderSceneTransformEdit> edits);
    QuaderApiVoidResult set_visible(EntityId entity, bool visible);
    QuaderApiVoidResult
    set_visibility(std::span<const QuaderSceneVisibilityEdit> edits);
    QuaderApiVoidResult set_locked(EntityId entity, bool locked);
    QuaderApiVoidResult set_locks(std::span<const QuaderSceneLockEdit> edits);
    QuaderApiVoidResult set_mesh_component(EntityId entity,
                                           QuaderMeshComponent mesh);
    QuaderApiVoidResult
    set_light_component(EntityId entity, QuaderLightComponent light,
                        QuaderSceneEditOptions options = {});
    /// Starts a live point-light range gesture, pinned to the current scene.
    QuaderApiResult<QuaderEntityRef> begin_point_light_range_edit(EntityId entity);
    QuaderApiVoidResult update_point_light_range_edit(QuaderEntityRef light, float range);
    /// Cancel restores the original range; commit adds one undo step if changed.
    QuaderApiVoidResult finish_point_light_range_edit(QuaderEntityRef light, bool cancel = false);
    /// Previews linear RGB and restores only color, color mode and preset on cancel.
    QuaderApiResult<QuaderScenePropertyEditRef> begin_light_color_edit(EntityId entity);
    QuaderApiVoidResult update_light_color_edit(QuaderScenePropertyEditRef light, QVec3 color);
    QuaderApiVoidResult finish_light_color_edit(QuaderScenePropertyEditRef light, bool cancel = false);
    /// Previews the scene-root clear color; acceptance records one undo step.
    QuaderApiResult<QuaderScenePropertyEditRef> begin_environment_color_edit(EntityId entity);
    QuaderApiVoidResult update_environment_color_edit(QuaderScenePropertyEditRef environment, QVec3 color);
    QuaderApiVoidResult finish_environment_color_edit(QuaderScenePropertyEditRef environment, bool cancel = false);
    QuaderApiVoidResult set_scene_settings(EntityId entity,
                                           QuaderSceneSettings settings,
                                           QuaderSceneEditOptions options = {});
    QuaderApiVoidResult select(std::span<const EntityId> entities,
                               EntityId active = kInvalidEntityId);
    QuaderApiVoidResult select(std::initializer_list<EntityId> entities,
                               EntityId active = kInvalidEntityId);
    QuaderApiVoidResult select(EntityId entity);

    QuaderApiResult<QuaderEntityRef> duplicate(EntityId entity);
    QuaderApiResult<QuaderDuplicateHierarchyResult>
    duplicate_selected_hierarchy();
    QuaderApiResult<QuaderEntityRef>
    group_selection(std::string group_name = {},
                    QuaderTransform transform = {});
    QuaderApiVoidResult ungroup_selection();
    [[nodiscard]] QuaderSceneBatch batch(std::string label = "Scene Batch");

private:
    friend class QuaderSceneBatch;
    friend class SceneTransformDragService;

    QuaderApiVoidResult commit_transform_preview(
        SceneTransformPreviewTransaction &preview, const std::string &label);

    QuaderApiVoidResult commit_batch(const QuaderSceneBatch &batch);

    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
