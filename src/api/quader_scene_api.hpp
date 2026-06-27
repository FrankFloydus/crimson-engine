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
    EntityId entity = InvalidEntityId;
    QuaderTransform before;
    QuaderTransform after;
};

/**
 * Represents a scene visibility edit used by the public Quader API surface.
 */
struct QuaderSceneVisibilityEdit {
    EntityId entity = InvalidEntityId;
    bool before = true;
    bool after = true;
};

/**
 * Represents a scene lock edit used by the public Quader API surface.
 */
struct QuaderSceneLockEdit {
    EntityId entity = InvalidEntityId;
    bool before = false;
    bool after = false;
};

/**
 * Owns Quader Scene Batch behavior within the public Quader API surface.
 */
class QuaderSceneBatch final {
public:
    QuaderSceneBatch(QuaderSceneApi& scene, std::string label);

    QuaderSceneBatch& rename(EntityId entity, std::string name);
    QuaderSceneBatch& reparent(EntityId entity, EntityId parent, bool preserveWorldTransform = true);
    QuaderSceneBatch& setTransform(EntityId entity, QuaderTransform transform);
    QuaderSceneBatch& setVisible(EntityId entity, bool visible);
    QuaderSceneBatch& setLocked(EntityId entity, bool locked);
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
        EntityId entity = InvalidEntityId;
        EntityId parent = InvalidEntityId;
        bool preserveWorldTransform = true;
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

    QuaderApiResult<QuaderEntityRef> createEntity(const QuaderCreateEntityArgs& args);
    QuaderApiVoidResult destroy(EntityId entity);
    QuaderApiVoidResult rename(EntityId entity, std::string name);
    QuaderApiVoidResult reparent(EntityId entity, EntityId parent, bool preserveWorldTransform = true);
    QuaderApiVoidResult setTransform(EntityId entity, QuaderTransform transform);
    QuaderApiVoidResult setTransforms(std::span<const QuaderSceneTransformEdit> edits);
    QuaderApiVoidResult setVisible(EntityId entity, bool visible);
    QuaderApiVoidResult setVisibility(std::span<const QuaderSceneVisibilityEdit> edits);
    QuaderApiVoidResult setLocked(EntityId entity, bool locked);
    QuaderApiVoidResult setLocks(std::span<const QuaderSceneLockEdit> edits);
    QuaderApiVoidResult setMeshComponent(EntityId entity, QuaderMeshComponent mesh);
    QuaderApiVoidResult select(std::span<const EntityId> entities, EntityId active = InvalidEntityId);
    QuaderApiVoidResult select(std::initializer_list<EntityId> entities, EntityId active = InvalidEntityId);
    QuaderApiVoidResult select(EntityId entity);

    QuaderApiResult<QuaderEntityRef> duplicate(EntityId entity);
    QuaderApiResult<QuaderDuplicateHierarchyResult> duplicateSelectedHierarchy();
    QuaderApiResult<QuaderEntityRef> groupSelection(std::string groupName = {}, QuaderTransform transform = {});
    QuaderApiVoidResult ungroupSelection();
    [[nodiscard]] QuaderSceneBatch batch(std::string label = "Scene Batch");

private:
    friend class QuaderSceneBatch;

    QuaderApiVoidResult commitBatch(const QuaderSceneBatch& batch);

    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
