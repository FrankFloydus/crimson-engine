#include "modeling/ModelingSessionServices.h"

#include "assets/AssetRegistry.h"
#include "scene/SceneDocument.h"
#include "scene/SceneEntityNaming.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <span>
#include <unordered_map>
#include <utility>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/tool/transform_drag.h>
#include <quader_poly/document.h>
#endif

namespace quader {
namespace {

#if QUADER_HAS_MODELING_SDK
Vec3 fromSdk(quader_poly::Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_poly::Vec3 toSdk(Vec3 value) {
    return {value.x, value.y, value.z};
}

MeshAsset toMeshAsset(const quader_poly::CompiledMesh& compiled) {
    MeshAsset mesh;
    mesh.name = "Mesh";
    mesh.vertices.reserve(compiled.vertices.size());
    mesh.indices = compiled.indices;
    mesh.primitives.reserve(compiled.primitives.size());
    mesh.hasStableCpuTopology = true;

    for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
        mesh.vertices.push_back(MeshVertex{
                .position = fromSdk(vertex.position),
                .normal = fromSdk(vertex.normal),
                .uv = {vertex.uv0.x, vertex.uv0.y},
                .rgba = vertex.color,
        });
    }
    for (const quader_poly::CompiledPrimitive& primitive : compiled.primitives) {
        mesh.primitives.push_back(MeshPrimitiveSpan{
                .indexOffset = primitive.index_offset,
                .indexCount = primitive.index_count,
                .materialSlot = primitive.material_slot,
        });
    }

    return mesh;
}

MeshAsset toMeshAsset(const quader_poly::Document& document) {
    return toMeshAsset(quader_poly::compile_document(document));
}

MeshAsset toMeshAsset(const quader_editor_core::PolygonObject& object) {
    return toMeshAsset(object.document);
}

constexpr std::uint64_t kPickGeometryHashSeed = 1469598103934665603ull;
constexpr std::uint64_t kPickGeometryHashPrime = 1099511628211ull;

std::uint64_t mixPickGeometryHash(std::uint64_t hash, std::uint64_t value) {
    return (hash ^ value) * kPickGeometryHashPrime;
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, int value) {
    return mixPickGeometryHash(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, std::uint32_t value) {
    return mixPickGeometryHash(hash, static_cast<std::uint64_t>(value));
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return mixPickGeometryHash(hash, bits);
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, quader_poly::Vec3 value) {
    hash = mixPickGeometryHash(hash, value.x);
    hash = mixPickGeometryHash(hash, value.y);
    hash = mixPickGeometryHash(hash, value.z);
    return hash;
}

std::uint64_t objectPickFaceGeometrySignature(
        const quader_editor_core::PolygonObject& object,
        quader_poly::PickOptions pickOptions = {}) {
    (void)pickOptions;
    std::uint64_t hash = mixPickGeometryHash(kPickGeometryHashSeed, object.id);
    hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(object.document.vertices.size()));
    hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(object.document.faces.size()));
    for (const quader_poly::Vertex& vertex : object.document.vertices) {
        hash = mixPickGeometryHash(hash, vertex.id);
        hash = mixPickGeometryHash(hash, vertex.position);
    }
    for (const quader_poly::Face& face : object.document.faces) {
        hash = mixPickGeometryHash(hash, face.id);
        hash = mixPickGeometryHash(hash, face.material_slot);
        hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(face.vertices.size()));
        for (const quader_poly::ElementId vertexId : face.vertices) {
            hash = mixPickGeometryHash(hash, vertexId);
        }
    }
    return hash;
}

void includePickBoundsPoint(quader_editor_core::PickObjectBounds& bounds, quader_poly::Vec3 point) {
    if (bounds.empty) {
        bounds.min = point;
        bounds.max = point;
        bounds.empty = false;
        return;
    }
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

quader_editor_core::PickObjectBounds buildObjectPickBounds(const quader_editor_core::PolygonObject& object) {
    quader_editor_core::PickObjectBounds bounds;
    bounds.object_id = object.id;
    for (const quader_poly::Vertex& vertex : object.document.vertices) {
        includePickBoundsPoint(bounds, vertex.position);
    }
    return bounds;
}

void appendOverlayFrame(
        quader_editor_core::SelectionOverlayFrame& target,
        const quader_editor_core::SelectionOverlayFrame& source) {
    target.triangles.reserve(target.triangles.size() + source.triangles.size());
    target.lines.reserve(target.lines.size() + source.lines.size());
    target.points.reserve(target.points.size() + source.points.size());
    target.triangles.insert(target.triangles.end(), source.triangles.begin(), source.triangles.end());
    target.lines.insert(target.lines.end(), source.lines.begin(), source.lines.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.hover_target_selected = target.hover_target_selected || source.hover_target_selected;
}

bool edgesMatchUnordered(quader_poly::Edge left, quader_poly::Edge right) {
    return (left.a == right.a && left.b == right.b) || (left.a == right.b && left.b == right.a);
}

bool hoverFrameSuppressesTriangle(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayTriangle& triangle) {
    if (!suppressBaseUnderHover) {
        return false;
    }
    if (triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_visible_face &&
            triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_xray_face &&
            triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_face_mask) {
        return false;
    }
    return std::any_of(
            hoverFrame.triangles.begin(),
            hoverFrame.triangles.end(),
            [&](const quader_editor_core::SelectionOverlayTriangle& hoverTriangle) {
                return hoverTriangle.layer == quader_editor_core::SelectionOverlayLayer::hover_face &&
                        hoverTriangle.object_id == triangle.object_id &&
                        hoverTriangle.face_id == triangle.face_id;
            });
}

bool hoverFrameSuppressesLine(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayLine& line) {
    if (!suppressBaseUnderHover) {
        return false;
    }
    if (line.layer != quader_editor_core::SelectionOverlayLayer::selected_face_edge &&
            line.layer != quader_editor_core::SelectionOverlayLayer::selected_edge) {
        return false;
    }
    return std::any_of(
            hoverFrame.lines.begin(),
            hoverFrame.lines.end(),
            [&](const quader_editor_core::SelectionOverlayLine& hoverLine) {
                if (hoverLine.object_id != line.object_id) {
                    return false;
                }
                if (hoverLine.layer == quader_editor_core::SelectionOverlayLayer::hover_face_edge) {
                    return line.face_id == hoverLine.face_id && edgesMatchUnordered(line.edge, hoverLine.edge);
                }
                return hoverLine.layer == quader_editor_core::SelectionOverlayLayer::hover_edge &&
                        edgesMatchUnordered(line.edge, hoverLine.edge);
            });
}

bool hoverFrameSuppressesPoint(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayPoint& point) {
    if (!suppressBaseUnderHover || point.layer != quader_editor_core::SelectionOverlayLayer::selected_vertex) {
        return false;
    }
    return std::any_of(
            hoverFrame.points.begin(),
            hoverFrame.points.end(),
            [&](const quader_editor_core::SelectionOverlayPoint& hoverPoint) {
                return hoverPoint.layer == quader_editor_core::SelectionOverlayLayer::hover_vertex &&
                        hoverPoint.object_id == point.object_id &&
                        hoverPoint.vertex_id == point.vertex_id;
            });
}

void appendSelectedHoverFilteredBaseFrame(
        quader_editor_core::SelectionOverlayFrame& target,
        const quader_editor_core::SelectionOverlayFrame& base,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        bool suppressBaseUnderHover) {
    target.triangles.reserve(target.triangles.size() + base.triangles.size());
    target.lines.reserve(target.lines.size() + base.lines.size());
    target.points.reserve(target.points.size() + base.points.size());
    for (const quader_editor_core::SelectionOverlayTriangle& triangle : base.triangles) {
        if (!hoverFrameSuppressesTriangle(suppressBaseUnderHover, hoverFrame, triangle)) {
            target.triangles.push_back(triangle);
        }
    }
    for (const quader_editor_core::SelectionOverlayLine& line : base.lines) {
        if (!hoverFrameSuppressesLine(suppressBaseUnderHover, hoverFrame, line)) {
            target.lines.push_back(line);
        }
    }
    for (const quader_editor_core::SelectionOverlayPoint& point : base.points) {
        if (!hoverFrameSuppressesPoint(suppressBaseUnderHover, hoverFrame, point)) {
            target.points.push_back(point);
        }
    }
}
#endif

} // namespace

std::size_t ModelingObjectRegistry::size() const {
    return entitiesByObject_.size();
}

bool ModelingObjectRegistry::empty() const {
    return entitiesByObject_.empty();
}

const std::unordered_map<int, EntityId>& ModelingObjectRegistry::entitiesByObject() const {
    return entitiesByObject_;
}

std::unordered_map<int, EntityId>& ModelingObjectRegistry::entitiesByObject() {
    return entitiesByObject_;
}

const std::unordered_map<EntityId, int>& ModelingObjectRegistry::objectsByEntity() const {
    return objectsByEntity_;
}

std::unordered_map<EntityId, int>& ModelingObjectRegistry::objectsByEntity() {
    return objectsByEntity_;
}

std::optional<EntityId> ModelingObjectRegistry::entityForSdkObject(int sdkObjectId) const {
    const auto it = entitiesByObject_.find(sdkObjectId);
    if (it == entitiesByObject_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<int> ModelingObjectRegistry::sdkObjectForEntity(EntityId entity) const {
    const auto it = objectsByEntity_.find(entity);
    if (it == objectsByEntity_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ModelingObjectRecord> ModelingObjectRegistry::records(const SceneDocument& scene) const {
    std::vector<ModelingObjectRecord> records;
    records.reserve(entitiesByObject_.size());
    for (const auto& [sdkObjectId, entity] : entitiesByObject_) {
        ModelingObjectRecord record;
        record.sdkObjectId = sdkObjectId;
        record.entity = entity;
        if (const MeshComponent* mesh = scene.mesh(entity)) {
            record.mesh = mesh->mesh;
        }
        if (const NameComponent* name = scene.name(entity)) {
            record.name = name->name;
        }
        records.push_back(std::move(record));
    }
    std::sort(records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return records;
}

void ModelingObjectRegistry::bind(int sdkObjectId, EntityId entity) {
    entitiesByObject_[sdkObjectId] = entity;
    objectsByEntity_[entity] = sdkObjectId;
}

void ModelingObjectRegistry::unbindObject(int sdkObjectId) {
    const auto it = entitiesByObject_.find(sdkObjectId);
    if (it == entitiesByObject_.end()) {
        return;
    }
    objectsByEntity_.erase(it->second);
    entitiesByObject_.erase(it);
}

void ModelingObjectRegistry::clear() {
    entitiesByObject_.clear();
    objectsByEntity_.clear();
}

bool ModelingSelectionService::hasComponentSelection(const ModelingSelectionSummary& summary) {
    return summary.selectedVertexCount > 0 || summary.selectedEdgeCount > 0 || summary.selectedFaceCount > 0;
}

#if QUADER_HAS_MODELING_SDK
void ModelingSelectionService::invalidatePickingCache() const {
    pickingCache_ = {};
}

const std::vector<quader_editor_core::PolygonObject>&
ModelingSelectionService::cachedSceneTransformedObjectsForPicking(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRegistry& objectRegistry) const {
    const std::uint64_t contentRevision = session.state().content_revision;
    const std::uint64_t selectionRevision = session.state().selection_revision;
    const std::uint64_t sceneRevision = scene.revision();
    const bool geometryChanged =
            !pickingCache_.objectsValid ||
            pickingCache_.contentRevision != contentRevision ||
            pickingCache_.sceneRevision != sceneRevision;
    if (pickingCache_.objectsValid &&
            pickingCache_.contentRevision == contentRevision &&
            pickingCache_.selectionRevision == selectionRevision &&
            pickingCache_.sceneRevision == sceneRevision) {
        return pickingCache_.objects;
    }

    pickingCache_.objects.clear();
    const std::vector<quader_editor_core::PolygonObject> sdkObjects = session.objects();
    pickingCache_.objects.reserve(sdkObjects.size());
    for (quader_editor_core::PolygonObject object : sdkObjects) {
        const auto entity = objectRegistry.entitiesByObject().find(object.id);
        if (entity == objectRegistry.entitiesByObject().end()) {
            continue;
        }
        const VisibilityComponent* visibility = scene.visibility(entity->second);
        if (!scene.isEffectivelyVisible(entity->second) ||
                (visibility != nullptr && !visibility->selectable)) {
            continue;
        }
        const Mat4 worldFromObject = scene.worldTransformMatrix(entity->second);
        for (quader_poly::Vertex& vertex : object.document.vertices) {
            vertex.position = toSdk(transformPoint(worldFromObject, fromSdk(vertex.position)));
        }
        pickingCache_.objects.push_back(std::move(object));
    }
    pickingCache_.contentRevision = contentRevision;
    pickingCache_.selectionRevision = selectionRevision;
    pickingCache_.sceneRevision = sceneRevision;
    pickingCache_.objectsValid = true;
    if (geometryChanged) {
        pickingCache_.boundsValid = false;
        pickingCache_.faceGeometryValid = false;
    }
    return pickingCache_.objects;
}

std::vector<quader_editor_core::PolygonObject> ModelingSelectionService::sceneTransformedObjectsForPicking(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRegistry& objectRegistry) const {
    return cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
}

const std::vector<quader_editor_core::PickObjectBounds>& ModelingSelectionService::cachedPickObjectBounds(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRegistry& objectRegistry) const {
    const std::vector<quader_editor_core::PolygonObject>& objects =
            cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
    if (pickingCache_.boundsValid) {
        return pickingCache_.bounds;
    }

    pickingCache_.bounds.clear();
    pickingCache_.bounds.reserve(objects.size());
    for (const quader_editor_core::PolygonObject& object : objects) {
        pickingCache_.bounds.push_back(buildObjectPickBounds(object));
    }
    pickingCache_.boundsValid = true;
    return pickingCache_.bounds;
}

const std::vector<quader_editor_core::PickObjectFaceGeometry>& ModelingSelectionService::cachedPickFaceGeometry(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRegistry& objectRegistry,
        quader_poly::PickOptions pickOptions) const {
    const std::vector<quader_editor_core::PolygonObject>& objects =
            cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
    if (pickingCache_.faceGeometryValid) {
        bool signaturesMatch = pickingCache_.faceGeometryEntries.size() == objects.size();
        for (std::size_t index = 0; signaturesMatch && index < objects.size(); ++index) {
            signaturesMatch =
                    pickingCache_.faceGeometryEntries[index].objectId == objects[index].id &&
                    pickingCache_.faceGeometryEntries[index].geometrySignature ==
                            objectPickFaceGeometrySignature(objects[index], pickOptions);
        }
        if (signaturesMatch) {
            return pickingCache_.faceGeometry;
        }
    }

    std::vector<PickFaceGeometryCacheEntry> previousEntries = std::move(pickingCache_.faceGeometryEntries);
    std::vector<quader_editor_core::PickObjectFaceGeometry> previousGeometry = std::move(pickingCache_.faceGeometry);
    std::vector<PickFaceGeometryCacheEntry> nextEntries;
    std::vector<quader_editor_core::PickObjectFaceGeometry> nextGeometry;
    nextEntries.reserve(objects.size());
    nextGeometry.reserve(objects.size());

    for (const quader_editor_core::PolygonObject& object : objects) {
        const std::uint64_t signature = objectPickFaceGeometrySignature(object, pickOptions);
        auto reusable = previousEntries.end();
        for (auto it = previousEntries.begin(); it != previousEntries.end(); ++it) {
            if (it->objectId == object.id && it->geometrySignature == signature) {
                reusable = it;
                break;
            }
        }
        if (reusable != previousEntries.end()) {
            const std::size_t index = static_cast<std::size_t>(std::distance(previousEntries.begin(), reusable));
            nextGeometry.push_back(std::move(previousGeometry[index]));
            reusable->objectId = -1;
        } else {
            nextGeometry.push_back({
                    object.id,
                    quader_poly::build_face_pick_geometry(object.document, pickOptions),
            });
        }
        nextEntries.push_back({
                .objectId = object.id,
                .geometrySignature = signature,
        });
    }

    pickingCache_.faceGeometry = std::move(nextGeometry);
    pickingCache_.faceGeometryEntries = std::move(nextEntries);
    pickingCache_.faceGeometryValid = true;
    return pickingCache_.faceGeometry;
}

bool ModelingSelectionService::selectionMatches(
        const quader_poly::Selection& left,
        const quader_poly::Selection& right) {
    return left.mode == right.mode &&
            left.vertices == right.vertices &&
            left.edges == right.edges &&
            left.faces == right.faces &&
            left.has_active == right.has_active &&
            left.active_kind == right.active_kind &&
            left.active_vertex == right.active_vertex &&
            left.active_edge == right.active_edge &&
            left.active_face == right.active_face;
}
#endif

SceneModelingSyncService::SceneModelingSyncService(ModelingObjectRegistry& objectRegistry)
    : objectRegistry_(objectRegistry) {}

ModelingObjectRegistry& SceneModelingSyncService::objectRegistry() {
    return objectRegistry_;
}

const ModelingObjectRegistry& SceneModelingSyncService::objectRegistry() const {
    return objectRegistry_;
}

bool SceneModelingSyncService::sceneObjectsSyncValid() const {
    return sceneObjectsSyncValid_;
}

std::uint64_t SceneModelingSyncService::syncedSdkContentRevision() const {
    return syncedSdkContentRevision_;
}

void SceneModelingSyncService::invalidateSceneObjects() {
    sceneObjectsSyncValid_ = false;
}

void SceneModelingSyncService::markSceneObjectsSynced(std::uint64_t contentRevision) {
    syncedSdkContentRevision_ = contentRevision;
    sceneObjectsSyncValid_ = true;
}

#if QUADER_HAS_MODELING_SDK
bool SceneModelingSyncService::sceneObjectsSyncedForCurrentSdkContent(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene) const {
    if (!sceneObjectsSyncValid_ || syncedSdkContentRevision_ != session.state().content_revision) {
        return false;
    }
    if (objectRegistry_.size() != session.objects().size()) {
        return false;
    }
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        const auto entity = objectRegistry_.entitiesByObject().find(object.id);
        if (entity == objectRegistry_.entitiesByObject().end() || !scene.exists(entity->second)) {
            return false;
        }
        const ModelingObjectComponent* modelingObject = scene.modelingObject(entity->second);
        const MeshComponent* mesh = scene.mesh(entity->second);
        if (modelingObject == nullptr || mesh == nullptr || mesh->mesh == InvalidAssetId) {
            return false;
        }
    }
    return true;
}
#endif

bool ModelingMeshSyncService::shouldForceRefresh(bool previewActive, bool previewFinished) {
    return previewActive || previewFinished;
}

#if QUADER_HAS_MODELING_SDK
std::optional<MeshAsset> ModelingMeshSyncService::compileObjectMesh(
        const quader_editor_core::PolygonObject& object) const {
    return toMeshAsset(object);
}

void ModelingMeshSyncService::syncSceneObjectsFromSdk(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        AssetRegistry& assets,
        SceneModelingSyncService& sceneSync,
        bool force) const {
    if (!force && sceneSync.sceneObjectsSyncedForCurrentSdkContent(session, scene)) {
        return;
    }

    ModelingObjectRegistry& objectRegistry = sceneSync.objectRegistry();
    std::unordered_map<std::uint32_t, AssetId> materialBySlot;
    std::optional<AssetId> firstModelingMaterial;
    for (const auto& [objectId, entity] : objectRegistry.entitiesByObject()) {
        (void)objectId;
        const ModelingObjectComponent* modelingObject = scene.modelingObject(entity);
        const MaterialComponent* material = scene.material(entity);
        if (modelingObject == nullptr || material == nullptr || material->material == InvalidAssetId) {
            continue;
        }
        materialBySlot.try_emplace(modelingObject->materialSlot, material->material);
        if (!firstModelingMaterial) {
            firstModelingMaterial = material->material;
        }
    }

    std::unordered_map<int, bool> liveObjects;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        liveObjects[object.id] = true;
        const std::uint32_t materialSlot = static_cast<std::uint32_t>(object.material_id);
        const auto existing = objectRegistry.entitiesByObject().find(object.id);
        if (existing == objectRegistry.entitiesByObject().end()) {
            const std::string name = SceneEntityNaming::nextNameForKind(scene, SceneEntityKind::Mesh);
            MeshAsset mesh = toMeshAsset(object);
            mesh.name = name;
            const AssetId meshId = assets.addMesh(std::move(mesh));
            const EntityId entity = scene.createEntity(name);
            scene.setModelingObject(entity, ModelingObjectComponent{
                    .objectId = object.id,
                    .materialSlot = materialSlot,
            });
            scene.setMesh(entity, MeshComponent{.mesh = meshId});
            const auto material = materialBySlot.find(materialSlot);
            if (material != materialBySlot.end()) {
                scene.setMaterial(entity, MaterialComponent{.material = material->second});
            } else if (firstModelingMaterial) {
                scene.setMaterial(entity, MaterialComponent{.material = *firstModelingMaterial});
            }
            objectRegistry.bind(object.id, entity);
            continue;
        }

        const EntityId entity = existing->second;
        MeshAsset mesh = toMeshAsset(object);
        if (const NameComponent* name = scene.name(entity)) {
            mesh.name = name->name;
        }
        AssetId meshId = InvalidAssetId;
        if (const MeshComponent* meshComponent = scene.mesh(entity)) {
            meshId = meshComponent->mesh;
        }
        bool meshContentsReplaced = false;
        if (meshId == InvalidAssetId) {
            meshId = assets.addMesh(std::move(mesh));
        } else if (assets.replaceMesh(meshId, std::move(mesh))) {
            meshContentsReplaced = true;
        } else {
            MeshAsset replacement = toMeshAsset(object);
            if (const NameComponent* name = scene.name(entity)) {
                replacement.name = name->name;
            }
            meshId = assets.addMesh(std::move(replacement));
        }
        const ModelingObjectComponent nextObject{
                .objectId = object.id,
                .materialSlot = materialSlot,
        };
        const ModelingObjectComponent* currentObject = scene.modelingObject(entity);
        if (currentObject == nullptr ||
                currentObject->objectId != nextObject.objectId ||
                currentObject->materialSlot != nextObject.materialSlot) {
            scene.setModelingObject(entity, nextObject);
        }
        const MeshComponent* currentMesh = scene.mesh(entity);
        if (currentMesh == nullptr || currentMesh->mesh != meshId || meshContentsReplaced) {
            scene.setMesh(entity, MeshComponent{.mesh = meshId});
        }
    }

    std::vector<int> removedObjects;
    for (const auto& [objectId, entity] : objectRegistry.entitiesByObject()) {
        if (!liveObjects.contains(objectId)) {
            removedObjects.push_back(objectId);
        }
        (void)entity;
    }
    for (const int objectId : removedObjects) {
        const std::optional<EntityId> entity = objectRegistry.entityForSdkObject(objectId);
        if (!entity.has_value()) {
            continue;
        }
        if (scene.exists(*entity)) {
            scene.destroyEntity(*entity);
        }
        objectRegistry.unbindObject(objectId);
    }
    sceneSync.markSceneObjectsSynced(session.state().content_revision);
}

ModelingCreateBoxResult ModelingMeshSyncService::createSceneObjectFromLatest(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        AssetRegistry& assets,
        SceneModelingSyncService& sceneSync,
        const std::string& name,
        const std::string& message) const {
    const quader_editor_core::PolygonObject& object = session.objects().back();
    MeshAsset mesh = toMeshAsset(object);
    mesh.name = name;
    const AssetId meshId = assets.addMesh(std::move(mesh));

    const EntityId entity = scene.createEntity(name);
    scene.setModelingObject(entity, ModelingObjectComponent{
            .objectId = object.id,
            .materialSlot = static_cast<std::uint32_t>(object.material_id),
    });
    scene.setMesh(entity, MeshComponent{.mesh = meshId});
    scene.clearSelection();
    scene.setSelected(entity, true);

    sceneSync.objectRegistry().bind(object.id, entity);
    sceneSync.markSceneObjectsSynced(session.state().content_revision);

    return {
            .ok = true,
            .changed = true,
            .message = message,
            .object = {
                    .sdkObjectId = object.id,
                    .entity = entity,
                    .mesh = meshId,
                    .name = name,
            },
    };
}
#endif

bool ModelingOverlayService::hasModalOverlay(const ModelingToolStatus& status) {
    return status.insertEdgeLoopHasPreview ||
            status.knifeHasCurrent ||
            status.cutHasLine ||
            status.mirrorHasLine ||
            status.polyHasCurrent;
}

void ModelingOverlayService::invalidate() const {
#if QUADER_HAS_MODELING_SDK
    selectionOverlayCache_.invalidate();
    overlayBaseCache_.valid = false;
#endif
}

void SelectionOverlayCache::invalidate() const {
#if QUADER_HAS_MODELING_SDK
    valid_ = false;
#endif
}

#if QUADER_HAS_MODELING_SDK
bool SelectionOverlayCache::matches(
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t toolSignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    return valid_ &&
            contentRevision_ == contentRevision &&
            selectionRevision_ == selectionRevision &&
            sceneRevision_ == sceneRevision &&
            selectionMode_ == selectionMode &&
            toolSignature_ == toolSignature &&
            hoverMatches(hover_, hover);
}

const ViewportOverlayFrame& SelectionOverlayCache::frame() const {
    return frame_;
}

void SelectionOverlayCache::store(
        ViewportOverlayFrame frame,
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t toolSignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    frame_ = std::move(frame);
    contentRevision_ = contentRevision;
    selectionRevision_ = selectionRevision;
    sceneRevision_ = sceneRevision;
    selectionMode_ = selectionMode;
    toolSignature_ = toolSignature;
    hover_ = hover;
    valid_ = true;
}

bool SelectionOverlayCache::hoverMatches(
        const quader_editor_core::SelectionHoverState& left,
        const quader_editor_core::SelectionHoverState& right) {
    return left.object_id == right.object_id &&
            left.kind == right.kind &&
            left.vertex_id == right.vertex_id &&
            left.edge == right.edge &&
            left.face_id == right.face_id &&
            left.selected_modifier_pressed == right.selected_modifier_pressed &&
            left.candidate_object_id == right.candidate_object_id;
}

bool ModelingOverlayService::selectionFrameMatches(
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t toolSignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    return selectionOverlayCache_.matches(
            contentRevision,
            selectionRevision,
            sceneRevision,
            selectionMode,
            toolSignature,
            hover);
}

const ViewportOverlayFrame& ModelingOverlayService::selectionFrame() const {
    return selectionOverlayCache_.frame();
}

void ModelingOverlayService::storeSelectionFrame(
        ViewportOverlayFrame frame,
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t toolSignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    selectionOverlayCache_.store(
            std::move(frame),
            contentRevision,
            selectionRevision,
            sceneRevision,
            selectionMode,
            toolSignature,
            hover);
}

const quader_editor_core::SelectionOverlayFrame& ModelingOverlayService::selectionOverlayBaseFrame(
        const quader_editor_core::EditorSession& session) const {
    const std::uint64_t contentRevision = session.state().content_revision;
    const std::uint64_t selectionRevision = session.state().selection_revision;
    const quader_editor_core::SelectionMode selectionMode = session.selection_mode();
    if (overlayBaseCache_.valid &&
            overlayBaseCache_.contentRevision == contentRevision &&
            overlayBaseCache_.selectionRevision == selectionRevision &&
            overlayBaseCache_.selectionMode == selectionMode) {
        return overlayBaseCache_.frame;
    }

    std::vector<OverlayBaseObjectCacheEntry> previousEntries = std::move(overlayBaseCache_.objectEntries);
    OverlayBaseCache next;
    next.valid = true;
    next.contentRevision = contentRevision;
    next.selectionRevision = selectionRevision;
    next.selectionMode = selectionMode;
    next.objectEntries.reserve(session.objects().size());

    const quader_editor_core::SelectionHoverState emptyHover;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        const std::uint64_t signature = objectPickFaceGeometrySignature(object);
        auto reusable = previousEntries.end();
        for (auto it = previousEntries.begin(); it != previousEntries.end(); ++it) {
            if (it->objectId == object.id &&
                    it->geometrySignature == signature &&
                    it->selectionMode == selectionMode &&
                    it->selected == object.selected &&
                    ModelingSelectionService::selectionMatches(it->selection, object.selection)) {
                reusable = it;
                break;
            }
        }

        OverlayBaseObjectCacheEntry entry;
        if (reusable != previousEntries.end()) {
            entry = std::move(*reusable);
            reusable->objectId = -1;
        } else {
            entry.objectId = object.id;
            entry.geometrySignature = signature;
            entry.selectionMode = selectionMode;
            entry.selected = object.selected;
            entry.selection = object.selection;
            entry.frame = quader_editor_core::build_selection_overlay_frame(
                    std::span<const quader_editor_core::PolygonObject>(&object, 1U),
                    selectionMode,
                    emptyHover,
                    {});
        }
        appendOverlayFrame(next.frame, entry.frame);
        next.objectEntries.push_back(std::move(entry));
    }

    overlayBaseCache_ = std::move(next);
    return overlayBaseCache_.frame;
}

quader_editor_core::SelectionOverlayFrame ModelingOverlayService::selectionOverlayCurrentFrame(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::EditorRuntime& runtime,
        const quader_editor_core::SelectionHoverState& hover) const {
    const quader_editor_core::SelectionOverlayFrame& base = selectionOverlayBaseFrame(session);
    quader_editor_core::SelectionOverlayFrame hoverFrame =
            quader_editor_core::build_selection_hover_overlay_frame(
                    session.objects(),
                    session.selection_mode(),
                    hover,
                    {});
    const auto appendTransformDragSnapPoints = [&](quader_editor_core::SelectionOverlayFrame& frame) {
        if (!runtime.transform_drag_active()) {
            return;
        }
        std::vector<quader_editor_core::SelectionOverlayPoint> snapPoints =
                quader_editor_core::transform_drag_snap_overlay_points(runtime.transform_drag_state());
        frame.points.insert(frame.points.end(), snapPoints.begin(), snapPoints.end());
    };
    const auto appendRuntimeToolOverlay = [&](quader_editor_core::SelectionOverlayFrame& frame) {
        const quader_editor_core::InsertEdgeLoopToolState& insertLoop = runtime.insert_edge_loop_state();
        frame.lines.insert(frame.lines.end(), insertLoop.preview_lines.begin(), insertLoop.preview_lines.end());

        const quader_editor_core::KnifeToolState& knife = runtime.knife_state();
        frame.lines.insert(frame.lines.end(), knife.preview_lines.begin(), knife.preview_lines.end());
        frame.points.insert(frame.points.end(), knife.preview_points.begin(), knife.preview_points.end());

        const quader_editor_core::PolyToolState& poly = runtime.poly_state();
        frame.lines.insert(frame.lines.end(), poly.preview_lines.begin(), poly.preview_lines.end());
        frame.points.insert(frame.points.end(), poly.preview_points.begin(), poly.preview_points.end());
    };
    const bool suppressBaseUnderHover =
            quader_editor_core::selection_hover_target_matches_selection(session.objects(), session.selection_mode(), hover);
    if (suppressBaseUnderHover) {
        quader_editor_core::SelectionOverlayFrame frame;
        frame.hover_target_selected = hoverFrame.hover_target_selected;
        appendSelectedHoverFilteredBaseFrame(frame, base, hoverFrame, suppressBaseUnderHover);
        appendOverlayFrame(frame, hoverFrame);
        appendTransformDragSnapPoints(frame);
        appendRuntimeToolOverlay(frame);
        return frame;
    }

    quader_editor_core::SelectionOverlayFrame frame = base;
    appendOverlayFrame(frame, hoverFrame);
    appendTransformDragSnapPoints(frame);
    appendRuntimeToolOverlay(frame);
    return frame;
}
#endif

ModelingCommandResult ModelingCommandExecutor::sdkUnavailable() {
    return {.ok = false, .message = "Modeling SDK is not available in this build."};
}

#if QUADER_HAS_MODELING_SDK
ModelingCommandResult ModelingCommandExecutor::fromSdk(const quader_editor_core::CommandResult& result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message,
    };
}

ModelingCommandExecutor::SyncPlan ModelingCommandExecutor::runtimeSyncPlan(
        bool mayChangeContent,
        std::uint64_t contentBefore,
        std::uint64_t contentAfter,
        const quader_editor_core::CommandResult& result) {
    SyncPlan plan;
    plan.syncMeshes = mayChangeContent && (contentAfter != contentBefore || result.changed);
    plan.forceMeshSync = result.changed;
    plan.syncSelection = true;
    plan.invalidateOverlay = result.changed;
    return plan;
}

ModelingCommandExecutor::SyncPlan ModelingCommandExecutor::operationSyncPlan(
        quader_editor_core::EditorOperationCommandPhase phase,
        const quader_editor_core::PreviewTransactionStatus& previewBefore,
        const quader_editor_core::EditorOperationCommandResult& result,
        std::uint64_t contentBefore,
        std::uint64_t contentAfter) {
    const bool previewPhase = phase != quader_editor_core::EditorOperationCommandPhase::execute;
    const bool previewFinished = result.preview_finish.has_value() && result.preview_finish->before.active;
    const bool sceneChanged = contentAfter != contentBefore;

    SyncPlan plan;
    plan.syncMeshes = sceneChanged || previewPhase || previewBefore.active || previewFinished;
    plan.forceMeshSync = ModelingMeshSyncService::shouldForceRefresh(
            previewPhase || previewBefore.active,
            previewFinished);
    plan.syncSelection = result.command.ok || sceneChanged || previewPhase || previewBefore.active || previewFinished;
    plan.invalidateOverlay = plan.syncMeshes;
    return plan;
}

std::optional<SdkOperationExecutor::OperationCommand> SdkOperationExecutor::operationForInstantCommand(
        ModelingInstantOperationId operation,
        const ModelingInteractionSettings& interactionSettings) const {
    OperationCommand command;
    command.settings = quader_editor_core::SelectionOperationSettings{};

    switch (operation) {
    case ModelingInstantOperationId::DuplicateMeshes:
        command.operation = quader_editor_core::EditorOperationId::duplicate_meshes;
        command.settings = quader_editor_core::DuplicateMeshesOperationSettings{};
        break;
    case ModelingInstantOperationId::CombineMeshes:
        command.operation = quader_editor_core::EditorOperationId::combine_meshes;
        break;
    case ModelingInstantOperationId::ExtractFaces:
        command.operation = quader_editor_core::EditorOperationId::extract_faces;
        break;
    case ModelingInstantOperationId::DetachFaces:
        command.operation = quader_editor_core::EditorOperationId::detach_faces;
        break;
    case ModelingInstantOperationId::Bridge:
        command.operation = quader_editor_core::EditorOperationId::bridge;
        command.settings = quader_editor_core::BridgeOperationSettings{.steps = interactionSettings.bridgeSteps};
        break;
    case ModelingInstantOperationId::SnapVerticesToActive:
        command.operation = quader_editor_core::EditorOperationId::snap_vertices_to_active;
        break;
    case ModelingInstantOperationId::ConnectVertices:
        command.operation = quader_editor_core::EditorOperationId::connect_vertices;
        break;
    case ModelingInstantOperationId::DissolveVertices:
        command.operation = quader_editor_core::EditorOperationId::dissolve_vertices;
        break;
    case ModelingInstantOperationId::BevelVertices:
        command.operation = quader_editor_core::EditorOperationId::bevel_vertices;
        command.settings = quader_editor_core::BevelVerticesOperationSettings{};
        break;
    case ModelingInstantOperationId::MergeVerticesToActive:
        command.operation = quader_editor_core::EditorOperationId::merge_vertices_to_active;
        break;
    case ModelingInstantOperationId::MergeVerticesToCenter:
        command.operation = quader_editor_core::EditorOperationId::merge_vertices_to_center;
        break;
    case ModelingInstantOperationId::SnapEdgesToActive:
        command.operation = quader_editor_core::EditorOperationId::snap_edges_to_active;
        break;
    case ModelingInstantOperationId::ConnectEdges:
        command.operation = quader_editor_core::EditorOperationId::connect_edges;
        break;
    case ModelingInstantOperationId::SplitEdges:
        command.operation = quader_editor_core::EditorOperationId::split_edges;
        break;
    case ModelingInstantOperationId::DissolveEdges:
        command.operation = quader_editor_core::EditorOperationId::dissolve_edges;
        break;
    case ModelingInstantOperationId::MergeEdges:
        command.operation = quader_editor_core::EditorOperationId::merge_edges;
        break;
    case ModelingInstantOperationId::CollapseEdges:
        command.operation = quader_editor_core::EditorOperationId::collapse_edges;
        break;
    case ModelingInstantOperationId::FillHole:
        command.operation = quader_editor_core::EditorOperationId::fill_hole;
        break;
    case ModelingInstantOperationId::CombineFaces:
        command.operation = quader_editor_core::EditorOperationId::combine_faces;
        break;
    case ModelingInstantOperationId::CollapseFaces:
        command.operation = quader_editor_core::EditorOperationId::collapse_faces;
        break;
    case ModelingInstantOperationId::RadialAlign:
        command.operation = quader_editor_core::EditorOperationId::radial_align;
        break;
    case ModelingInstantOperationId::FlipFaceNormals:
        command.operation = quader_editor_core::EditorOperationId::flip_face_normals;
        break;
    case ModelingInstantOperationId::RecalculateFaceNormals:
        command.operation = quader_editor_core::EditorOperationId::recalculate_face_normals;
        command.settings = quader_editor_core::RecalculateFaceNormalsOperationSettings{};
        break;
    case ModelingInstantOperationId::ShadeFacesSmooth:
        command.operation = quader_editor_core::EditorOperationId::shade_faces_smooth;
        break;
    case ModelingInstantOperationId::ShadeFacesFlat:
        command.operation = quader_editor_core::EditorOperationId::shade_faces_flat;
        break;
    case ModelingInstantOperationId::HardenEdgeNormals:
        command.operation = quader_editor_core::EditorOperationId::harden_edge_normals;
        break;
    case ModelingInstantOperationId::SoftenEdgeNormals:
        command.operation = quader_editor_core::EditorOperationId::soften_edge_normals;
        break;
    case ModelingInstantOperationId::DeleteSelection:
        return std::nullopt;
    }

    return command;
}

quader_editor_core::EditorOperationCommand SdkOperationExecutor::makeCommand(
        const OperationCommand& operation) {
    quader_editor_core::EditorOperationCommand command;
    command.operation_id = operation.operation;
    command.phase = operation.phase;
    command.settings = operation.settings;
    return command;
}

quader_editor_core::EditorOperationCommandResult SdkOperationExecutor::execute(
        quader_editor_core::EditorSession& session,
        const OperationCommand& operation) {
    return quader_editor_core::execute_command(session, makeCommand(operation));
}
#endif

bool TransformPreviewService::isTransform(ModelingTransformOperation operation) {
    return operation == ModelingTransformOperation::Transform;
}

#if QUADER_HAS_MODELING_SDK
bool TransformPreviewService::hasTransformPreview(const quader_editor_core::EditorSession& session) {
    const quader_editor_core::PreviewTransactionStatus status = session.preview_transaction_status();
    return status.active && status.kind == quader_editor_core::PreviewTransactionKind::transform;
}

ModelingCommandResult TransformPreviewService::finishResult(
        const quader_editor_core::PreviewTransactionFinishResult& finish,
        bool cancel) {
    return {
            .ok = !finish.before.active || finish.before.kind == quader_editor_core::PreviewTransactionKind::transform,
            .changed = cancel ? finish.restored : finish.before.changed,
    };
}
#endif

void TransformDragController::setPreviewDriven(bool previewDriven, bool duplicateSelected) {
    previewDriven_ = previewDriven;
    duplicateSelected_ = duplicateSelected;
}

void TransformDragController::clear() {
    previewDriven_ = false;
    duplicateSelected_ = false;
}

bool TransformDragController::previewDriven() const {
    return previewDriven_;
}

bool TransformDragController::duplicateSelected() const {
    return duplicateSelected_;
}

bool ModalToolRuntime::ownsViewport(ModelingViewportTool tool) {
    switch (tool) {
    case ModelingViewportTool::Box:
    case ModelingViewportTool::InsertEdgeLoop:
    case ModelingViewportTool::Knife:
    case ModelingViewportTool::Cut:
    case ModelingViewportTool::Poly:
    case ModelingViewportTool::Mirror:
        return true;
    case ModelingViewportTool::Select:
    case ModelingViewportTool::Move:
    case ModelingViewportTool::Extend:
    case ModelingViewportTool::Extrude:
    case ModelingViewportTool::Rotate:
    case ModelingViewportTool::Scale:
        return false;
    }
    return false;
}

#if QUADER_HAS_MODELING_SDK
quader_editor_core::EditorRuntimeCommandId ModelingClipboardService::copyCommand() {
    return quader_editor_core::EditorRuntimeCommandId::copy_selected_meshes;
}

quader_editor_core::EditorRuntimeCommandId ModelingClipboardService::pasteCommand() {
    return quader_editor_core::EditorRuntimeCommandId::paste_copied_meshes;
}

quader_editor_core::EditorRuntimeCommandId ModelingHistoryBridge::undoCommand() {
    return quader_editor_core::EditorRuntimeCommandId::undo;
}

quader_editor_core::EditorRuntimeCommandId ModelingHistoryBridge::redoCommand() {
    return quader_editor_core::EditorRuntimeCommandId::redo;
}

quader_editor_core::RepeatLastActionState& ModelingHistoryBridge::repeatLastAction() {
    return repeatLastAction_;
}

const quader_editor_core::RepeatLastActionState& ModelingHistoryBridge::repeatLastAction() const {
    return repeatLastAction_;
}

quader_editor_core::CommandResult ModelingHistoryBridge::executeRepeatLastAction(
        quader_editor_core::EditorSession& session) {
    return quader_editor_core::execute_repeat_last_action(session, repeatLastAction_);
}

void ModelingHistoryBridge::recordOperation(
        const quader_editor_core::EditorOperationCommand& command,
        const quader_editor_core::CommandResult& result) {
    quader_editor_core::record_repeatable_operation(repeatLastAction_, command, result);
}

void ModelingHistoryBridge::recordTransform(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::TransformDragState& drag,
        bool duplicateSelected) {
    quader_editor_core::record_repeatable_transform(
            repeatLastAction_,
            session,
            drag,
            duplicateSelected);
}
#endif

ModelingInteractionSettingsService::ModelingInteractionSettingsService()
    : settings_(normalize(ModelingInteractionSettings{})) {}

ModelingInteractionSettings ModelingInteractionSettingsService::settings() const {
    return settings_;
}

ModelingInteractionSettings ModelingInteractionSettingsService::normalize(
        const ModelingInteractionSettings& settings) const {
#if QUADER_HAS_MODELING_SDK
    ModelingInteractionSettings normalized = settings;
    normalized.bridgeSteps = quader_editor_core::normalize_bridge_operation_settings(
            quader_editor_core::BridgeOperationSettings{.steps = normalized.bridgeSteps}).steps;
    const quader_editor_core::EdgeBevelOperationSettings edgeBevel =
            quader_editor_core::normalize_edge_bevel_operation_settings({
                    .width = normalized.edgeBevelWidth,
                    .profile = normalized.edgeBevelProfile,
                    .segments = normalized.edgeBevelSegments,
            });
    normalized.edgeBevelWidth = edgeBevel.width;
    normalized.edgeBevelProfile = edgeBevel.profile;
    normalized.edgeBevelSegments = edgeBevel.segments;
    const quader_editor_core::ThickenOperationSettings thicken =
            quader_editor_core::normalize_thicken_operation_settings({
                    .thickness = normalized.thickenThickness,
                    .from_center = normalized.thickenFromCenter,
            });
    normalized.thickenThickness = thicken.thickness;
    normalized.thickenFromCenter = thicken.from_center;
    const quader_editor_core::SliceQuadOperationSettings slice =
            quader_editor_core::normalize_slice_quad_operation_settings({
                    .x_slices = normalized.sliceQuadXSlices,
                    .y_slices = normalized.sliceQuadYSlices,
            });
    normalized.sliceQuadXSlices = slice.x_slices;
    normalized.sliceQuadYSlices = slice.y_slices;
    normalized.mergeByDistanceTolerance = quader_editor_core::normalize_merge_by_distance_operation_settings(
            quader_editor_core::MergeByDistanceOperationSettings{.tolerance = normalized.mergeByDistanceTolerance}).tolerance;
    return normalized;
#else
    return settings;
#endif
}

bool ModelingInteractionSettingsService::setNormalized(ModelingInteractionSettings settings) {
    settings = normalize(settings);
    const bool changed = settings.bridgeSteps != settings_.bridgeSteps ||
            settings.edgeBevelWidth != settings_.edgeBevelWidth ||
            settings.edgeBevelProfile != settings_.edgeBevelProfile ||
            settings.edgeBevelSegments != settings_.edgeBevelSegments ||
            settings.thickenThickness != settings_.thickenThickness ||
            settings.thickenFromCenter != settings_.thickenFromCenter ||
            settings.sliceQuadXSlices != settings_.sliceQuadXSlices ||
            settings.sliceQuadYSlices != settings_.sliceQuadYSlices ||
            settings.mergeByDistanceTolerance != settings_.mergeByDistanceTolerance ||
            settings.cutMode != settings_.cutMode;
    settings_ = settings;
    return changed;
}

void ModelingInteractionSettingsService::resetForPreviewAction(ModelingActionPreviewKind previewKind) {
    const ModelingInteractionSettings defaults = normalize(ModelingInteractionSettings{});
    switch (previewKind) {
    case ModelingActionPreviewKind::EdgeBevel:
        settings_.edgeBevelWidth = defaults.edgeBevelWidth;
        settings_.edgeBevelProfile = defaults.edgeBevelProfile;
        settings_.edgeBevelSegments = defaults.edgeBevelSegments;
        return;
    case ModelingActionPreviewKind::Thicken:
        settings_.thickenThickness = defaults.thickenThickness;
        settings_.thickenFromCenter = defaults.thickenFromCenter;
        return;
    case ModelingActionPreviewKind::MergeByDistance:
        settings_.mergeByDistanceTolerance = defaults.mergeByDistanceTolerance;
        return;
    case ModelingActionPreviewKind::Bridge:
    case ModelingActionPreviewKind::SliceQuad:
    case ModelingActionPreviewKind::None:
        return;
    }
}

void ModelingInteractionSettingsService::setCutMode(ModelingCutMode mode) {
    settings_.cutMode = mode;
}

void ModelingInteractionSettingsService::cycleCutModeWithoutSdk() {
    settings_.cutMode =
            settings_.cutMode == ModelingCutMode::DiscardLeft ? ModelingCutMode::DiscardRight :
            settings_.cutMode == ModelingCutMode::DiscardRight ? ModelingCutMode::KeepBoth :
            ModelingCutMode::DiscardLeft;
}

const std::string& ModelingToolSettingsService::activePreviewActionId() const {
    return activePreviewActionId_;
}

const std::string& ModelingToolSettingsService::activePreviewOperationId() const {
    return activePreviewOperationId_;
}

const std::string& ModelingToolSettingsService::activePreviewLabel() const {
    return activePreviewLabel_;
}

void ModelingToolSettingsService::setPreviewMetadata(std::string actionId, std::string operationId, std::string label) {
    activePreviewActionId_ = std::move(actionId);
    activePreviewOperationId_ = std::move(operationId);
    activePreviewLabel_ = std::move(label);
}

void ModelingToolSettingsService::clearPreviewMetadata() {
    activePreviewActionId_.clear();
    activePreviewOperationId_.clear();
    activePreviewLabel_.clear();
}

ModelingToolSettingsTarget ModelingToolSettingsService::operationPreviewTarget(
        ModelingToolSettingsTargetKind kind,
        std::string_view fallbackLabel,
        std::string_view fallbackOperationId) const {
    ModelingToolSettingsTarget target;
    target.kind = kind;
    target.active = true;
    target.operationPreviewActive = true;
    target.label = activePreviewLabel_.empty() ? std::string(fallbackLabel) : activePreviewLabel_;
    target.actionId = activePreviewActionId_;
    target.operationId = activePreviewOperationId_.empty() ? std::string(fallbackOperationId) : activePreviewOperationId_;
    return target;
}

ModelingActionQuery ModelingContextActionService::effectiveActionQuery(
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    query.modalToolOwnsViewport = query.modalToolOwnsViewport || modalToolOwnsViewport;
    return query;
}

ModelingActionSet ModelingContextActionService::actionSet(
        const ModelingSelectionSummary& summary,
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    return ModelingActionCatalog::actionSetFor(summary, effectiveActionQuery(query, modalToolOwnsViewport));
}

std::vector<ModelingActionItem> ModelingContextActionService::actions(
        const ModelingSelectionSummary& summary,
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    return ModelingActionCatalog::actionsFor(summary, effectiveActionQuery(query, modalToolOwnsViewport));
}

} // namespace quader
