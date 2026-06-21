#pragma once

#include "api/QuaderApiResult.h"
#include "document/commands/DocumentCommandEdits.h"

#include <span>
#include <string>

namespace quader {

class ProjectSession;
class QuaderApiMutationRunner;

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
    QuaderApiVoidResult select(std::span<const EntityId> entities, EntityId active = InvalidEntityId);

    QuaderApiResult<QuaderEntityRef> duplicate(EntityId entity);
    QuaderApiResult<QuaderEntityRef> groupSelection(std::string groupName = {});
    QuaderApiVoidResult ungroupSelection();

private:
    ProjectSession& session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
