#pragma once

#include "api/QuaderApiResult.h"
#include "document/commands/DocumentCommandEdits.h"

#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace quader {

class ProjectSession;
class QuaderApiMutationRunner;
class QuaderSceneApi;

class QuaderSceneBatch final {
public:
    QuaderSceneBatch(QuaderSceneApi& scene, std::string label);

    QuaderSceneBatch& rename(EntityId entity, std::string name);
    QuaderSceneBatch& reparent(EntityId entity, EntityId parent, bool preserveWorldTransform = true);
    QuaderSceneBatch& setTransform(EntityId entity, TransformComponent transform);
    QuaderSceneBatch& setVisible(EntityId entity, bool visible);
    QuaderSceneBatch& setLocked(EntityId entity, bool locked);
    QuaderApiVoidResult commit();

private:
    friend class QuaderSceneApi;

    enum class OperationKind {
        Rename,
        Reparent,
        SetTransform,
        SetVisible,
        SetLocked,
    };

    struct Operation {
        OperationKind kind = OperationKind::Rename;
        EntityId entity = InvalidEntityId;
        EntityId parent = InvalidEntityId;
        bool preserveWorldTransform = true;
        bool flag = false;
        TransformComponent transform;
        std::string name;
    };

    QuaderSceneApi* scene_ = nullptr;
    std::string label_;
    std::vector<Operation> operations_;
    bool committed_ = false;
};

class QuaderSceneApi final {
public:
    QuaderSceneApi(ProjectSession& session, QuaderApiMutationRunner& mutations);

    QuaderApiResult<QuaderEntityRef> createEntity(const QuaderCreateEntityArgs& args);
    QuaderApiVoidResult destroy(EntityId entity);
    QuaderApiVoidResult rename(EntityId entity, std::string name);
    QuaderApiVoidResult reparent(EntityId entity, EntityId parent, bool preserveWorldTransform = true);
    QuaderApiVoidResult setTransform(EntityId entity, TransformComponent transform);
    QuaderApiVoidResult setTransforms(std::span<const DocumentTransformEdit> edits);
    QuaderApiVoidResult setVisible(EntityId entity, bool visible);
    QuaderApiVoidResult setVisibility(std::span<const DocumentVisibilityEdit> edits);
    QuaderApiVoidResult setLocked(EntityId entity, bool locked);
    QuaderApiVoidResult setLocks(std::span<const DocumentLockEdit> edits);
    QuaderApiVoidResult setMeshComponent(EntityId entity, MeshComponent mesh);
    QuaderApiVoidResult select(std::span<const EntityId> entities, EntityId active = InvalidEntityId);
    QuaderApiVoidResult select(std::initializer_list<EntityId> entities, EntityId active = InvalidEntityId);
    QuaderApiVoidResult select(EntityId entity);

    QuaderApiResult<QuaderEntityRef> duplicate(EntityId entity);
    QuaderApiResult<QuaderEntityRef> groupSelection(std::string groupName = {}, TransformComponent transform = {});
    QuaderApiVoidResult ungroupSelection();
    [[nodiscard]] QuaderSceneBatch batch(std::string label = "Scene Batch");

private:
    friend class QuaderSceneBatch;

    QuaderApiVoidResult commitBatch(const QuaderSceneBatch& batch);

    ProjectSession& session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
