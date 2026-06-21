#include "api/QuaderQueryApi.h"

#include "document/ProjectSession.h"
#include "modeling/ModelingSessionService.h"
#include "scene/SceneBounds.h"

#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

QuaderBounds apiBounds(const SceneBounds& bounds) {
    return {
            .min = bounds.min,
            .max = bounds.max,
    };
}

QuaderModelingObjectRef objectRefForEntity(const ProjectSession& session, EntityId entity) {
    QuaderModelingObjectRef ref;
    ref.entity = entity;
    if (const ModelingObjectComponent* object = session.scene().modelingObject(entity)) {
        ref.sdkObjectId = object->objectId;
    }
    if (const MeshComponent* mesh = session.scene().mesh(entity)) {
        ref.mesh = mesh->mesh;
    }
    if (const MaterialComponent* material = session.scene().material(entity)) {
        ref.material = material->material;
    }
    return ref;
}

QuaderEntitySnapshot entitySnapshotFor(const ProjectSession& session, EntityId entity) {
    const SceneDocument& scene = session.scene();
    QuaderEntitySnapshot snapshot;
    snapshot.id = entity;
    snapshot.kind = scene.kind(entity);
    snapshot.parent = scene.parent(entity);
    snapshot.active = scene.primarySelection() == entity;

    if (const NameComponent* name = scene.name(entity)) {
        snapshot.name = name->name;
    }
    if (const TransformComponent* transform = scene.transform(entity)) {
        snapshot.transform = *transform;
    }
    if (const VisibilityComponent* visibility = scene.visibility(entity)) {
        snapshot.visible = visibility->visible;
        snapshot.selectable = visibility->selectable;
    }
    if (const LockComponent* lock = scene.lock(entity)) {
        snapshot.locked = lock->locked;
    }
    if (const SelectionComponent* selection = scene.selection(entity)) {
        snapshot.selected = selection->selected;
    }
    if (const MeshComponent* mesh = scene.mesh(entity)) {
        snapshot.hasMesh = true;
        snapshot.mesh = mesh->mesh;
    }
    if (const MaterialComponent* material = scene.material(entity)) {
        snapshot.hasMaterial = true;
        snapshot.material = material->material;
    }
    if (const ModelingObjectComponent* object = scene.modelingObject(entity)) {
        snapshot.hasModelingObject = true;
        snapshot.sdkObjectId = object->objectId;
    }
    return snapshot;
}

QuaderMeshSnapshot meshSnapshotFor(AssetId id, const MeshAsset& mesh) {
    QuaderMeshSnapshot snapshot;
    snapshot.mesh = id;
    snapshot.revision = mesh.revision;
    snapshot.vertexCount = mesh.vertices.size();
    snapshot.indexCount = mesh.indices.size();
    snapshot.primitiveCount = mesh.primitives.size();
    snapshot.stableCpuTopology = mesh.hasStableCpuTopology;
    if (const std::optional<SceneBounds> bounds = localMeshBounds(mesh)) {
        snapshot.hasBounds = true;
        snapshot.bounds = apiBounds(*bounds);
    }
    return snapshot;
}

} // namespace

QuaderQueryApi::QuaderQueryApi(ProjectSession& session)
    : session_(session) {}

std::vector<QuaderEntitySnapshot> QuaderQueryApi::entities() const {
    std::vector<QuaderEntitySnapshot> result;
    const std::vector<EntityId> ids = session_.scene().entities();
    result.reserve(ids.size());
    for (const EntityId id : ids) {
        result.push_back(entitySnapshotFor(session_, id));
    }
    return result;
}

std::vector<QuaderEntitySnapshot> QuaderQueryApi::entityTree() const {
    std::vector<QuaderEntitySnapshot> result;
    const std::vector<EntityId> ids = session_.scene().entityTreeOrder();
    result.reserve(ids.size());
    for (const EntityId id : ids) {
        result.push_back(entitySnapshotFor(session_, id));
    }
    return result;
}

std::optional<QuaderEntitySnapshot> QuaderQueryApi::entity(EntityId id) const {
    if (!session_.scene().exists(id)) {
        return std::nullopt;
    }
    return entitySnapshotFor(session_, id);
}

std::optional<QuaderMeshSnapshot> QuaderQueryApi::mesh(AssetId id) const {
    const MeshAsset* mesh = session_.assets().mesh(id);
    if (mesh == nullptr) {
        return std::nullopt;
    }
    return meshSnapshotFor(id, *mesh);
}

std::vector<ModelingObjectRecord> QuaderQueryApi::modelingObjects() const {
    return session_.modeling().objects();
}

std::optional<QuaderModelingObjectRef> QuaderQueryApi::modelingObjectForEntity(EntityId entity) const {
    if (!session_.scene().exists(entity)) {
        return std::nullopt;
    }
    QuaderModelingObjectRef ref = objectRefForEntity(session_, entity);
    if (ref.sdkObjectId == InvalidModelingObjectId) {
        return std::nullopt;
    }
    return ref;
}

std::optional<QuaderModelingObjectRef> QuaderQueryApi::modelingObjectForSdkId(ModelingObjectId sdkObjectId) const {
    const std::optional<EntityId> entity = session_.modeling().entityForSdkObject(sdkObjectId);
    if (!entity.has_value()) {
        return std::nullopt;
    }
    return objectRefForEntity(session_, *entity);
}

std::optional<MeshAsset> QuaderQueryApi::compiledObjectMesh(QuaderModelingObjectRef object) const {
    if (object.sdkObjectId == InvalidModelingObjectId) {
        return std::nullopt;
    }
    return session_.modeling().compileObjectMesh(object.sdkObjectId);
}

std::optional<Vec3> QuaderQueryApi::selectionCenter() const {
    return session_.modeling().selectionCenter();
}

ModelingSelectionSummary QuaderQueryApi::selection() const {
    return session_.modeling().selectionSummary();
}

ModelingToolStatus QuaderQueryApi::toolStatus() const {
    return session_.modeling().toolStatus();
}

QuaderAssetSnapshot QuaderQueryApi::assets() const {
    return {
            .meshCount = session_.assets().meshCount(),
            .materialCount = session_.assets().materialCount(),
            .textureCount = session_.assets().textureCount(),
            .revision = session_.assets().revision(),
    };
}

QuaderHistorySnapshot QuaderQueryApi::history() const {
    QuaderHistorySnapshot result;
    result.canUndo = session_.undoRedo().canUndo() || session_.modeling().canUndo();
    result.canRedo = session_.undoRedo().canRedo() || session_.modeling().canRedo();
    if (const std::string* label = session_.undoRedo().undoLabel()) {
        result.undoLabel = *label;
    }
    if (const std::string* label = session_.undoRedo().redoLabel()) {
        result.redoLabel = *label;
    }
    return result;
}

ViewportOverlayFrame QuaderQueryApi::selectionOverlay() const {
    return session_.modeling().selectionOverlayFrame();
}

QuaderRevisionStamp QuaderQueryApi::revisions() const {
    const ModelingSelectionSummary summary = session_.modeling().selectionSummary();
    return {
            .projectRevision = session_.document().revision(),
            .sceneRevision = session_.scene().revision(),
            .assetRevision = session_.assets().revision(),
            .sdkContentRevision = summary.contentRevision,
            .sdkSelectionRevision = summary.selectionRevision,
    };
}

QuaderApiSnapshot QuaderQueryApi::snapshot() const {
    return {
            .revisions = revisions(),
            .entities = entityTree(),
            .modelingObjects = modelingObjects(),
            .selection = selection(),
            .toolStatus = toolStatus(),
            .assets = assets(),
            .history = history(),
            .selectionOverlay = selectionOverlay(),
    };
}

} // namespace quader
