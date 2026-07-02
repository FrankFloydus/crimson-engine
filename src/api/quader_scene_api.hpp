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
 * Owns Quader Scene Batch behavior within the public Quader API surface.
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
        std::string name;
    };

    QuaderSceneApi* scene_ = nullptr;
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

    QuaderApiVoidResult commit_batch(const QuaderSceneBatch &batch);

    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
