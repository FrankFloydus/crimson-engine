#include "api/QuaderSceneApi.h"

#include "api/QuaderApiMutationRunner.h"
#include "document/ProjectSession.h"

#include <utility>
#include <vector>

namespace quader {

QuaderSceneApi::QuaderSceneApi(ProjectSession& session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::createEntity(const QuaderCreateEntityArgs& args) {
    return mutations_.createEntity(args);
}

QuaderApiVoidResult QuaderSceneApi::destroy(EntityId entity) {
    return mutations_.runDocumentResult(session_.documentCommands().destroyEntity(entity), "Destroy Entity");
}

QuaderApiVoidResult QuaderSceneApi::rename(EntityId entity, std::string name) {
    return mutations_.runDocumentResult(
            session_.documentCommands().renameEntity(entity, std::move(name)),
            "Rename Entity");
}

QuaderApiVoidResult QuaderSceneApi::reparent(
        EntityId entity,
        EntityId parent,
        bool preserveWorldTransform) {
    return mutations_.runDocumentResult(
            session_.documentCommands().reparentEntity(entity, parent, preserveWorldTransform),
            "Reparent Entity");
}

QuaderApiVoidResult QuaderSceneApi::setTransform(EntityId entity, TransformComponent transform) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setTransform(entity, transform),
            "Set Transform");
}

QuaderApiVoidResult QuaderSceneApi::setTransforms(std::span<const DocumentTransformEdit> edits) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setTransforms(std::vector<DocumentTransformEdit>(edits.begin(), edits.end())),
            "Set Transform");
}

QuaderApiVoidResult QuaderSceneApi::setVisible(EntityId entity, bool visible) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setVisible(entity, visible),
            "Set Visibility");
}

QuaderApiVoidResult QuaderSceneApi::setVisibility(std::span<const DocumentVisibilityEdit> edits) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setVisibility(std::vector<DocumentVisibilityEdit>(edits.begin(), edits.end())),
            "Set Visibility");
}

QuaderApiVoidResult QuaderSceneApi::setLocked(EntityId entity, bool locked) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setLocked(entity, locked),
            "Set Lock");
}

QuaderApiVoidResult QuaderSceneApi::setLocks(std::span<const DocumentLockEdit> edits) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setLocks(std::vector<DocumentLockEdit>(edits.begin(), edits.end())),
            "Set Lock");
}

QuaderApiVoidResult QuaderSceneApi::setMeshComponent(EntityId entity, MeshComponent mesh) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setMeshComponent(entity, std::move(mesh)),
            "Set Mesh Component");
}

QuaderApiVoidResult QuaderSceneApi::select(std::span<const EntityId> entities, EntityId active) {
    return mutations_.runDocumentResult(
            session_.documentCommands().selectEntities(std::vector<EntityId>(entities.begin(), entities.end()), active),
            "Select Entities");
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::duplicate(EntityId entity) {
    EntityId duplicateEntity = InvalidEntityId;
    DocumentCommandResult command = session_.documentCommands().duplicateEntity(entity, &duplicateEntity);

    QuaderApiResult<QuaderEntityRef> result;
    const QuaderApiVoidResult status = mutations_.runDocumentResult(std::move(command), "Duplicate Entity");
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = status.message;
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{.id = duplicateEntity};
    }
    return result;
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::groupSelection(std::string groupName, TransformComponent transform) {
    EntityId groupEntity = InvalidEntityId;
    DocumentCommandResult command = session_.documentCommands().groupSelection(
            std::move(groupName),
            transform,
            &groupEntity);

    QuaderApiResult<QuaderEntityRef> result;
    const QuaderApiVoidResult status = mutations_.runDocumentResult(std::move(command), "Group Selection");
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = status.message;
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{.id = groupEntity};
    }
    return result;
}

QuaderApiVoidResult QuaderSceneApi::ungroupSelection() {
    return mutations_.runDocumentResult(session_.documentCommands().ungroupSelection(), "Ungroup Selection");
}

} // namespace quader
