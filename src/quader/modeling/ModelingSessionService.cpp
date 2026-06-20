#include "quader/modeling/ModelingSessionService.h"

#include "quader/assets/AssetRegistry.h"
#include "quader/render/ViewportOverlayPolicy.h"
#include "quader/scene/SceneBounds.h"
#include "quader/scene/SceneDocument.h"
#include "quader/scene/SceneEntityNaming.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/operation_command.h>
#include <quader_editor_core/repeat_last_action.h>
#include <quader_editor_core/runtime_command.h>
#include <quader_editor_core/session.h>
#include <quader_editor_core/tool/mirror_tool.h>
#include <quader_host_contract/host_contract.h>
#include <quader_poly/document.h>
#endif

namespace quader {
namespace {

#if QUADER_HAS_MODELING_SDK
quader_poly::Vec3 toSdk(Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_poly::Vec2 toSdk(Vec2 value) {
    return {value.x, value.y};
}

quader_poly::Ray toSdk(const ModelingRay& ray) {
    return {
            .origin = toSdk(ray.origin),
            .direction = toSdk(normalize(ray.direction)),
    };
}

quader_poly::Transform3 toSdk(const ModelingTransform3& transform) {
    return {
            .x_axis = toSdk(transform.xAxis),
            .y_axis = toSdk(transform.yAxis),
            .z_axis = toSdk(transform.zAxis),
            .origin = toSdk(transform.origin),
    };
}

Vec3 fromSdk(quader_poly::Vec3 value) {
    return {value.x, value.y, value.z};
}

ModelingSelectionMode fromSdk(quader_editor_core::SelectionMode mode) {
    switch (mode) {
    case quader_editor_core::SelectionMode::vertex:
        return ModelingSelectionMode::Vertex;
    case quader_editor_core::SelectionMode::edge:
        return ModelingSelectionMode::Edge;
    case quader_editor_core::SelectionMode::face:
        return ModelingSelectionMode::Face;
    case quader_editor_core::SelectionMode::object:
        return ModelingSelectionMode::Object;
    }
    return ModelingSelectionMode::Object;
}

quader_editor_core::SelectionMode toSdk(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return quader_editor_core::SelectionMode::vertex;
    case ModelingSelectionMode::Edge:
        return quader_editor_core::SelectionMode::edge;
    case ModelingSelectionMode::Face:
        return quader_editor_core::SelectionMode::face;
    case ModelingSelectionMode::Object:
        return quader_editor_core::SelectionMode::object;
    }
    return quader_editor_core::SelectionMode::object;
}

quader_editor_core::TransformAxis toSdk(ModelingMirrorAxis axis) {
    switch (axis) {
    case ModelingMirrorAxis::X:
        return quader_editor_core::TransformAxis::x;
    case ModelingMirrorAxis::Y:
        return quader_editor_core::TransformAxis::y;
    case ModelingMirrorAxis::Z:
        return quader_editor_core::TransformAxis::z;
    }
    return quader_editor_core::TransformAxis::none;
}

quader_editor_core::TransformAxis toSdk(ModelingTransformAxis axis) {
    switch (axis) {
    case ModelingTransformAxis::X:
        return quader_editor_core::TransformAxis::x;
    case ModelingTransformAxis::Y:
        return quader_editor_core::TransformAxis::y;
    case ModelingTransformAxis::Z:
        return quader_editor_core::TransformAxis::z;
    case ModelingTransformAxis::XY:
        return quader_editor_core::TransformAxis::xy;
    case ModelingTransformAxis::XZ:
        return quader_editor_core::TransformAxis::xz;
    case ModelingTransformAxis::YZ:
        return quader_editor_core::TransformAxis::yz;
    case ModelingTransformAxis::All:
        return quader_editor_core::TransformAxis::all;
    case ModelingTransformAxis::None:
        return quader_editor_core::TransformAxis::none;
    }
    return quader_editor_core::TransformAxis::none;
}

quader_editor_core::ViewportInteractionTool toSdk(ModelingTransformTool tool) {
    switch (tool) {
    case ModelingTransformTool::Move:
        return quader_editor_core::ViewportInteractionTool::move;
    case ModelingTransformTool::Rotate:
        return quader_editor_core::ViewportInteractionTool::rotate;
    case ModelingTransformTool::Scale:
        return quader_editor_core::ViewportInteractionTool::scale;
    }
    return quader_editor_core::ViewportInteractionTool::unknown;
}

quader_editor_core::TransformDragOperation toSdk(ModelingTransformOperation operation) {
    switch (operation) {
    case ModelingTransformOperation::Transform:
        return quader_editor_core::TransformDragOperation::transform;
    case ModelingTransformOperation::Extrude:
        return quader_editor_core::TransformDragOperation::extrude;
    case ModelingTransformOperation::Inset:
        return quader_editor_core::TransformDragOperation::inset;
    }
    return quader_editor_core::TransformDragOperation::transform;
}

quader_editor_core::ViewportFlipOrientation toSdk(ModelingFlipOrientation orientation) {
    switch (orientation) {
    case ModelingFlipOrientation::Horizontal:
        return quader_editor_core::ViewportFlipOrientation::horizontal;
    case ModelingFlipOrientation::Vertical:
        return quader_editor_core::ViewportFlipOrientation::vertical;
    }
    return quader_editor_core::ViewportFlipOrientation::horizontal;
}

ModelingActiveSelectionKind fromSdk(quader_editor_core::ActiveSelectionKind kind) {
    switch (kind) {
    case quader_editor_core::ActiveSelectionKind::none:
        return ModelingActiveSelectionKind::None;
    case quader_editor_core::ActiveSelectionKind::object:
        return ModelingActiveSelectionKind::Object;
    case quader_editor_core::ActiveSelectionKind::vertex:
        return ModelingActiveSelectionKind::Vertex;
    case quader_editor_core::ActiveSelectionKind::edge:
        return ModelingActiveSelectionKind::Edge;
    case quader_editor_core::ActiveSelectionKind::face:
        return ModelingActiveSelectionKind::Face;
    }
    return ModelingActiveSelectionKind::None;
}

ViewportOverlayLayer fromSdk(quader_editor_core::SelectionOverlayLayer layer) {
    return static_cast<ViewportOverlayLayer>(static_cast<int>(layer));
}

ModelingBoxDragState fromSdk(quader_editor_core::BoxDragState state) {
    switch (state) {
    case quader_editor_core::BoxDragState::idle:
        return ModelingBoxDragState::Idle;
    case quader_editor_core::BoxDragState::footprint:
        return ModelingBoxDragState::Footprint;
    case quader_editor_core::BoxDragState::height:
        return ModelingBoxDragState::Height;
    }
    return ModelingBoxDragState::Idle;
}

Vec3 transformObjectPoint(
        int objectId,
        Vec3 point,
        const std::unordered_map<int, EntityId>& entityByObject,
        const SceneDocument& scene) {
    const auto entity = entityByObject.find(objectId);
    if (entity == entityByObject.end()) {
        return point;
    }
    const TransformComponent* transform = scene.transform(entity->second);
    if (transform == nullptr) {
        return point;
    }
    return transformPoint(composeTransform(transform->spatialTransform()), point);
}

ViewportOverlayFrame fromSdk(
        const quader_editor_core::SelectionOverlayFrame& frame,
        const std::unordered_map<int, EntityId>& entityByObject,
        const SceneDocument& scene) {
    ViewportOverlayFrame result;
    result.triangles.reserve(frame.triangles.size());
    result.lines.reserve(frame.lines.size());
    result.points.reserve(frame.points.size());
    result.hoverTargetSelected = frame.hover_target_selected;

    for (const quader_editor_core::SelectionOverlayTriangle& triangle : frame.triangles) {
        result.triangles.push_back(ViewportOverlayTriangle{
                .layer = fromSdk(triangle.layer),
                .a = transformObjectPoint(triangle.object_id, fromSdk(triangle.a), entityByObject, scene),
                .b = transformObjectPoint(triangle.object_id, fromSdk(triangle.b), entityByObject, scene),
                .c = transformObjectPoint(triangle.object_id, fromSdk(triangle.c), entityByObject, scene),
                .color = viewportOverlayColorFor(fromSdk(triangle.layer), triangle.color_abgr),
        });
    }
    for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
        result.lines.push_back(ViewportOverlayLine{
                .layer = fromSdk(line.layer),
                .start = transformObjectPoint(line.object_id, fromSdk(line.start), entityByObject, scene),
                .end = transformObjectPoint(line.object_id, fromSdk(line.end), entityByObject, scene),
                .color = viewportOverlayColorFor(fromSdk(line.layer), line.color_abgr),
                .widthPixels = line.width_pixels,
        });
    }
    for (const quader_editor_core::SelectionOverlayPoint& point : frame.points) {
        result.points.push_back(ViewportOverlayPoint{
                .layer = fromSdk(point.layer),
                .position = transformObjectPoint(point.object_id, fromSdk(point.position), entityByObject, scene),
                .color = viewportOverlayColorFor(fromSdk(point.layer), point.color_abgr),
                .sizePixels = point.size_pixels,
        });
    }
    return result;
}

MeshAsset toMeshAsset(const quader_editor_core::PolygonObject& object) {
    const quader_poly::CompiledMesh compiled = quader_poly::compile_document(object.document);

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

const quader_editor_core::PolygonObject* findObject(
        const quader_editor_core::EditorSession& session,
        int sdkObjectId) {
    const auto& objects = session.objects();
    const auto it = std::find_if(objects.begin(), objects.end(), [sdkObjectId](const auto& object) {
        return object.id == sdkObjectId;
    });
    return it == objects.end() ? nullptr : &(*it);
}

ModelingBoxPreview fromSdk(
        const quader_editor_core::BoxPreviewDescriptor& preview,
        const std::vector<quader_poly::Vec3>& corners = {}) {
    ModelingBoxPreview result;
    result.ok = preview.ok;
    result.valid = preview.ok && preview.footprint.footprint.size() >= 4U && !preview.bounds.empty;
    result.dragState = fromSdk(preview.drag_state);
    result.message = preview.message;
    result.normal = fromSdk(preview.normal);
    result.distance = preview.distance;
    result.boundsEmpty = preview.bounds.empty;
    result.bounds = {
            .min = fromSdk(preview.bounds.min),
            .max = fromSdk(preview.bounds.max),
    };

    result.footprintCount = std::min<std::size_t>(result.footprint.size(), preview.footprint.footprint.size());
    for (std::size_t index = 0; index < result.footprintCount; ++index) {
        result.footprint[index] = fromSdk(preview.footprint.footprint[index]);
    }

    result.cornerCount = std::min<std::size_t>(result.corners.size(), corners.size());
    for (std::size_t index = 0; index < result.cornerCount; ++index) {
        result.corners[index] = fromSdk(corners[index]);
    }
    return result;
}
#endif

ModelingCommandDescriptor makeBoxToolDescriptor() {
    return ModelingCommandDescriptor{
            .command = ModelingCommandId::ActivateBoxTool,
            .id = "box",
            .label = "Box",
            .shortcut = "B",
            .viewportScoped = true,
            .rebuildsMeshOnSuccess = true,
    };
}

ModelingCommandDescriptor makeCreateBoxDescriptor() {
    return ModelingCommandDescriptor{
            .command = ModelingCommandId::CreateBoxFromBounds,
            .id = "create_box_from_bounds",
            .label = "Create Box From Bounds",
            .shortcut = "B",
            .viewportScoped = true,
            .rebuildsMeshOnSuccess = true,
    };
}

#if QUADER_HAS_MODELING_SDK
quader_editor_core::EditorRuntimeCommand noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId commandId) {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = commandId;
    return command;
}

#endif

} // namespace

class ModelingSessionService::Impl {
public:
    Impl(SceneDocument& scene, AssetRegistry& assets) : scene_(scene), assets_(assets) {}

    [[nodiscard]] bool sdkAvailable() const {
#if QUADER_HAS_MODELING_SDK
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] ModelingCommandDescriptor boxToolCommand() const {
        return makeBoxToolDescriptor();
    }

    [[nodiscard]] ModelingCommandDescriptor createBoxFromBoundsCommand() const {
        return makeCreateBoxDescriptor();
    }

    [[nodiscard]] std::vector<ModelingObjectRecord> objects() const {
        std::vector<ModelingObjectRecord> records;
        records.reserve(entityByObject_.size());
        for (const auto& [sdkObjectId, entity] : entityByObject_) {
            ModelingObjectRecord record;
            record.sdkObjectId = sdkObjectId;
            record.entity = entity;
            if (const MeshComponent* mesh = scene_.mesh(entity)) {
                record.mesh = mesh->mesh;
            }
            if (const NameComponent* name = scene_.name(entity)) {
                record.name = name->name;
            }
            records.push_back(std::move(record));
        }
        std::sort(records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.entity < rhs.entity;
        });
        return records;
    }

    [[nodiscard]] std::optional<EntityId> entityForSdkObject(int sdkObjectId) const {
        const auto it = entityByObject_.find(sdkObjectId);
        if (it == entityByObject_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::optional<int> sdkObjectForEntity(EntityId entity) const {
        const auto it = objectByEntity_.find(entity);
        if (it == objectByEntity_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::optional<MeshAsset> compileObjectMesh(int sdkObjectId) const {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PolygonObject* object = findObject(session_, sdkObjectId);
        if (object == nullptr) {
            return std::nullopt;
        }
        return toMeshAsset(*object);
#else
        (void)sdkObjectId;
        return std::nullopt;
#endif
    }

    [[nodiscard]] std::optional<Vec3> selectionCenter() const {
#if QUADER_HAS_MODELING_SDK
        if (session_.selection_mode() == quader_editor_core::SelectionMode::object) {
            if (const std::optional<SceneBounds> bounds = selectedWorldBounds(scene_, assets_)) {
                return boundsCenter(*bounds);
            }
        }

        std::optional<SceneBounds> bounds;
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            if (!object.selected || object.selection.empty()) {
                continue;
            }
            const std::vector<quader_poly::ElementId> vertexIds =
                    quader_poly::selected_vertex_ids(object.document, object.selection);
            for (const quader_poly::ElementId vertexId : vertexIds) {
                const quader_poly::Vertex* vertex = quader_poly::find_vertex(object.document, vertexId);
                if (vertex == nullptr) {
                    continue;
                }
                const Vec3 world = transformObjectPoint(object.id, fromSdk(vertex->position), entityByObject_, scene_);
                if (!bounds) {
                    bounds = SceneBounds{.min = world, .max = world};
                } else {
                    includePoint(*bounds, world);
                }
            }
        }
        if (bounds) {
            return boundsCenter(*bounds);
        }
        const std::optional<quader_poly::Vec3> center = session_.selection_center();
        return center ? std::optional<Vec3>{fromSdk(*center)} : std::nullopt;
#else
        return std::nullopt;
#endif
    }

    [[nodiscard]] ModelingSelectionMode selectionMode() const {
#if QUADER_HAS_MODELING_SDK
        return fromSdk(session_.selection_mode());
#else
        return ModelingSelectionMode::Object;
#endif
    }

    [[nodiscard]] ModelingSelectionSummary selectionSummary() const {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::ActiveSelection active = session_.active_selection();
        const std::vector<int> selectedObjects = session_.selected_object_ids();
        std::uint32_t selectedVertexCount = 0;
        std::uint32_t selectedEdgeCount = 0;
        std::uint32_t selectedFaceCount = 0;
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            if (!object.selected) {
                continue;
            }
            selectedVertexCount += static_cast<std::uint32_t>(object.selection.vertices.size());
            selectedEdgeCount += static_cast<std::uint32_t>(object.selection.edges.size());
            selectedFaceCount += static_cast<std::uint32_t>(object.selection.faces.size());
        }

        return ModelingSelectionSummary{
                .mode = fromSdk(session_.selection_mode()),
                .hasSelection = session_.has_selection(),
                .activeKind = fromSdk(active.kind),
                .activeObjectId = active.object_id,
                .activeVertexId = static_cast<int>(active.vertex_id),
                .activeEdgeA = static_cast<int>(active.edge.a),
                .activeEdgeB = static_cast<int>(active.edge.b),
                .activeFaceId = static_cast<int>(active.face_id),
                .selectedObjectCount = static_cast<std::uint32_t>(selectedObjects.size()),
                .selectedVertexCount = selectedVertexCount,
                .selectedEdgeCount = selectedEdgeCount,
                .selectedFaceCount = selectedFaceCount,
                .contentRevision = session_.state().content_revision,
                .selectionRevision = session_.state().selection_revision,
        };
#else
        return {};
#endif
    }

    [[nodiscard]] ViewportOverlayFrame selectionOverlayFrame() const {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::SelectionHoverState& hover = runtime_.hover();
        const std::uint64_t contentRevision = session_.state().content_revision;
        const std::uint64_t selectionRevision = session_.state().selection_revision;
        const std::uint64_t sceneRevision = scene_.revision();
        const quader_editor_core::SelectionMode selectionMode = session_.selection_mode();
        if (selectionOverlayCacheValid_ &&
                selectionOverlayCacheContentRevision_ == contentRevision &&
                selectionOverlayCacheSelectionRevision_ == selectionRevision &&
                selectionOverlayCacheSceneRevision_ == sceneRevision &&
                selectionOverlayCacheSelectionMode_ == selectionMode &&
                hoverMatches(selectionOverlayCacheHover_, hover)) {
            return selectionOverlayCacheFrame_;
        }

        selectionOverlayCacheFrame_ = fromSdk(quader_editor_core::build_selection_overlay_frame(
                    session_.objects(),
                    selectionMode,
                    hover),
                    entityByObject_,
                    scene_);
        selectionOverlayCacheValid_ = true;
        selectionOverlayCacheContentRevision_ = contentRevision;
        selectionOverlayCacheSelectionRevision_ = selectionRevision;
        selectionOverlayCacheSceneRevision_ = sceneRevision;
        selectionOverlayCacheSelectionMode_ = selectionMode;
        selectionOverlayCacheHover_ = hover;
        return selectionOverlayCacheFrame_;
#else
        return {};
#endif
    }

    void syncObjectSelectionFromScene() {
#if QUADER_HAS_MODELING_SDK
        if (session_.selection_mode() != quader_editor_core::SelectionMode::object) {
            return;
        }

        std::vector<int> objectIds;
        objectIds.reserve(scene_.selectedEntities().size());
        for (const EntityId entity : scene_.selectedEntities()) {
            const auto it = objectByEntity_.find(entity);
            if (it != objectByEntity_.end()) {
                objectIds.push_back(it->second);
            }
        }

        int activeObjectId = -1;
        const auto primary = objectByEntity_.find(scene_.primarySelection());
        if (primary != objectByEntity_.end()) {
            activeObjectId = primary->second;
        }

        session_.set_selected_object_ids(objectIds, activeObjectId);
#endif
    }

    void syncSceneSelectionFromSdk() {
#if QUADER_HAS_MODELING_SDK
        if (!sceneObjectsSyncedForCurrentSdkContent()) {
            syncSceneObjectsFromSdk();
        }
        std::vector<int> selectedObjectIds = session_.selected_object_ids();
        const int activeObjectId = session_.active_object_id();
        std::unordered_set<EntityId> desiredSelection;
        desiredSelection.reserve(selectedObjectIds.size());
        for (const int objectId : selectedObjectIds) {
            const auto it = entityByObject_.find(objectId);
            if (it != entityByObject_.end()) {
                desiredSelection.insert(it->second);
            }
        }

        for (const EntityId entity : scene_.selectedEntities()) {
            if (!desiredSelection.contains(entity)) {
                scene_.setSelected(entity, false);
            }
        }
        for (const EntityId entity : desiredSelection) {
            const SelectionComponent* selection = scene_.selection(entity);
            if (selection == nullptr || !selection->selected) {
                scene_.setSelected(entity, true);
            }
        }
        const auto active = entityByObject_.find(activeObjectId);
        if (active != entityByObject_.end()) {
            const SelectionComponent* selection = scene_.selection(active->second);
            if (selection && selection->selected) {
                scene_.setActiveSelection(active->second);
            }
        }
#endif
    }

    ModelingCommandResult setSelectionMode(ModelingSelectionMode mode) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::SetSelectionModeRuntimeCommandSettings settings;
        settings.selection_mode = static_cast<int>(toSdk(mode));

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_selection_mode;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)mode;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult selectAll() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::select_all, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult invertSelection() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::invert_selection, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult clearSelection() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::clear_selection, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult deleteSelection() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::delete_selection, true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult copySelection() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::copy_selected_meshes, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult pasteSelection() {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::paste_copied_meshes;
        command.settings = quader_editor_core::PasteCopiedMeshesRuntimeCommandSettings {};
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult undo() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::undo, true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult redo() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::redo, true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult repeatLastAction() {
#if QUADER_HAS_MODELING_SDK
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::CommandResult result =
                quader_editor_core::execute_repeat_last_action(session_, repeatLastAction_);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult mirrorSelection(ModelingMirrorAxis axis) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::MirrorAxisRuntimeCommandSettings settings;
        settings.axis = toSdk(axis);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::mirror_axis;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)axis;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult flipSelection(ModelingFlipOrientation orientation) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::ViewFlipRuntimeCommandSettings settings;
        settings.orientation = toSdk(orientation);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::flip_view;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)orientation;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult applyActiveMaterial(std::uint32_t materialSlot) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::AssignMaterialOperationSettings settings;
        settings.material_slot = materialSlot;

        quader_editor_core::EditorOperationCommand command;
        command.operation_id = quader_editor_core::EditorOperationId::assign_material;
        command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorOperationCommandResult result =
                quader_editor_core::execute_command(session_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        quader_editor_core::record_repeatable_operation(repeatLastAction_, command, result.command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)materialSlot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    [[nodiscard]] std::vector<ModelingInstantOperationDescriptor> instantOperations() const {
        const ModelingSelectionSummary summary = selectionSummary();
        const bool hasSelection = summary.hasSelection;
        const bool objectMode = summary.mode == ModelingSelectionMode::Object;
        const bool vertexMode = summary.mode == ModelingSelectionMode::Vertex;
        const bool edgeMode = summary.mode == ModelingSelectionMode::Edge;
        const bool faceMode = summary.mode == ModelingSelectionMode::Face;
        const bool hasVertices = summary.selectedVertexCount > 0;
        const bool hasEdges = summary.selectedEdgeCount > 0;
        const bool hasFaces = summary.selectedFaceCount > 0;

        return {
                {ModelingInstantOperationId::DeleteSelection, "delete_selection", "Delete", hasSelection},
                {ModelingInstantOperationId::DuplicateMeshes, "duplicate_meshes", "Duplicate", objectMode && summary.selectedObjectCount > 0},
                {ModelingInstantOperationId::CombineMeshes, "combine_meshes", "Combine Meshes", objectMode && summary.selectedObjectCount > 1},
                {ModelingInstantOperationId::ExtractFaces, "extract_faces", "Extract Faces", faceMode && hasFaces},
                {ModelingInstantOperationId::DetachFaces, "detach_faces", "Detach Faces", faceMode && hasFaces},
                {ModelingInstantOperationId::Bridge, "bridge", "Bridge", (edgeMode && hasEdges) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::SnapVerticesToActive, "snap_vertices_to_active", "Snap to Vertex", vertexMode && summary.selectedVertexCount > 1 && summary.activeKind == ModelingActiveSelectionKind::Vertex},
                {ModelingInstantOperationId::ConnectVertices, "connect_vertices", "Connect Vertices", vertexMode && hasVertices},
                {ModelingInstantOperationId::DissolveVertices, "dissolve_vertices", "Dissolve Vertices", vertexMode && hasVertices},
                {ModelingInstantOperationId::BevelVertices, "bevel_vertices", "Bevel Vertex", vertexMode && hasVertices},
                {ModelingInstantOperationId::MergeVerticesToActive, "merge_vertices_to_active", "Merge Vertices To Active", vertexMode && summary.selectedVertexCount > 1},
                {ModelingInstantOperationId::MergeVerticesToCenter, "merge_vertices_to_center", "Merge Vertices To Center", vertexMode && summary.selectedVertexCount > 1},
                {ModelingInstantOperationId::SnapEdgesToActive, "snap_edges_to_active", "Snap to Edge", edgeMode && summary.selectedEdgeCount > 1 && summary.activeKind == ModelingActiveSelectionKind::Edge},
                {ModelingInstantOperationId::ConnectEdges, "connect_edges", "Connect Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::SplitEdges, "split_edges", "Split Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::DissolveEdges, "dissolve_edges", "Dissolve Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::MergeEdges, "merge_edges", "Merge Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::CollapseEdges, "collapse_edges", "Collapse Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::FillHole, "fill_hole", "Fill Hole", edgeMode && hasEdges},
                {ModelingInstantOperationId::CombineFaces, "combine_faces", "Combine Faces", faceMode && hasFaces},
                {ModelingInstantOperationId::CollapseFaces, "collapse_faces", "Collapse Faces", faceMode && hasFaces},
                {ModelingInstantOperationId::RadialAlign, "radial_align", "Radial Align", hasSelection && !objectMode},
                {ModelingInstantOperationId::FlipFaceNormals, "flip_face_normals", "Flip Face Normals", (objectMode && hasSelection) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::RecalculateFaceNormals, "recalculate_face_normals", "Recalculate Face Normals", (objectMode && hasSelection) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::ShadeFacesSmooth, "shade_faces_smooth", "Shade Smooth", (objectMode && hasSelection) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::ShadeFacesFlat, "shade_faces_flat", "Shade Flat", (objectMode && hasSelection) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::HardenEdgeNormals, "harden_edge_normals", "Harden Edge Normals", edgeMode && hasEdges},
                {ModelingInstantOperationId::SoftenEdgeNormals, "soften_edge_normals", "Soften Edge Normals", edgeMode && hasEdges},
        };
    }

    ModelingCommandResult executeInstantOperation(ModelingInstantOperationId operation) {
#if QUADER_HAS_MODELING_SDK
        if (operation == ModelingInstantOperationId::DeleteSelection) {
            return deleteSelection();
        }

        quader_editor_core::EditorOperationId sdkOperation = quader_editor_core::EditorOperationId::delete_selection;
        quader_editor_core::EditorOperationSettings settings = quader_editor_core::SelectionOperationSettings{};
        switch (operation) {
        case ModelingInstantOperationId::DuplicateMeshes:
            sdkOperation = quader_editor_core::EditorOperationId::duplicate_meshes;
            settings = quader_editor_core::DuplicateMeshesOperationSettings{};
            break;
        case ModelingInstantOperationId::CombineMeshes:
            sdkOperation = quader_editor_core::EditorOperationId::combine_meshes;
            break;
        case ModelingInstantOperationId::ExtractFaces:
            sdkOperation = quader_editor_core::EditorOperationId::extract_faces;
            break;
        case ModelingInstantOperationId::DetachFaces:
            sdkOperation = quader_editor_core::EditorOperationId::detach_faces;
            break;
        case ModelingInstantOperationId::Bridge:
            sdkOperation = quader_editor_core::EditorOperationId::bridge;
            settings = quader_editor_core::BridgeOperationSettings{};
            break;
        case ModelingInstantOperationId::SnapVerticesToActive:
            sdkOperation = quader_editor_core::EditorOperationId::snap_vertices_to_active;
            break;
        case ModelingInstantOperationId::ConnectVertices:
            sdkOperation = quader_editor_core::EditorOperationId::connect_vertices;
            break;
        case ModelingInstantOperationId::DissolveVertices:
            sdkOperation = quader_editor_core::EditorOperationId::dissolve_vertices;
            break;
        case ModelingInstantOperationId::BevelVertices:
            sdkOperation = quader_editor_core::EditorOperationId::bevel_vertices;
            settings = quader_editor_core::BevelVerticesOperationSettings{};
            break;
        case ModelingInstantOperationId::MergeVerticesToActive:
            sdkOperation = quader_editor_core::EditorOperationId::merge_vertices_to_active;
            break;
        case ModelingInstantOperationId::MergeVerticesToCenter:
            sdkOperation = quader_editor_core::EditorOperationId::merge_vertices_to_center;
            break;
        case ModelingInstantOperationId::SnapEdgesToActive:
            sdkOperation = quader_editor_core::EditorOperationId::snap_edges_to_active;
            break;
        case ModelingInstantOperationId::ConnectEdges:
            sdkOperation = quader_editor_core::EditorOperationId::connect_edges;
            break;
        case ModelingInstantOperationId::SplitEdges:
            sdkOperation = quader_editor_core::EditorOperationId::split_edges;
            break;
        case ModelingInstantOperationId::DissolveEdges:
            sdkOperation = quader_editor_core::EditorOperationId::dissolve_edges;
            break;
        case ModelingInstantOperationId::MergeEdges:
            sdkOperation = quader_editor_core::EditorOperationId::merge_edges;
            break;
        case ModelingInstantOperationId::CollapseEdges:
            sdkOperation = quader_editor_core::EditorOperationId::collapse_edges;
            break;
        case ModelingInstantOperationId::FillHole:
            sdkOperation = quader_editor_core::EditorOperationId::fill_hole;
            break;
        case ModelingInstantOperationId::CombineFaces:
            sdkOperation = quader_editor_core::EditorOperationId::combine_faces;
            break;
        case ModelingInstantOperationId::CollapseFaces:
            sdkOperation = quader_editor_core::EditorOperationId::collapse_faces;
            break;
        case ModelingInstantOperationId::RadialAlign:
            sdkOperation = quader_editor_core::EditorOperationId::radial_align;
            break;
        case ModelingInstantOperationId::FlipFaceNormals:
            sdkOperation = quader_editor_core::EditorOperationId::flip_face_normals;
            break;
        case ModelingInstantOperationId::RecalculateFaceNormals:
            sdkOperation = quader_editor_core::EditorOperationId::recalculate_face_normals;
            settings = quader_editor_core::RecalculateFaceNormalsOperationSettings{};
            break;
        case ModelingInstantOperationId::ShadeFacesSmooth:
            sdkOperation = quader_editor_core::EditorOperationId::shade_faces_smooth;
            break;
        case ModelingInstantOperationId::ShadeFacesFlat:
            sdkOperation = quader_editor_core::EditorOperationId::shade_faces_flat;
            break;
        case ModelingInstantOperationId::HardenEdgeNormals:
            sdkOperation = quader_editor_core::EditorOperationId::harden_edge_normals;
            break;
        case ModelingInstantOperationId::SoftenEdgeNormals:
            sdkOperation = quader_editor_core::EditorOperationId::soften_edge_normals;
            break;
        case ModelingInstantOperationId::DeleteSelection:
            break;
        }

        quader_editor_core::EditorOperationCommand command;
        command.operation_id = sdkOperation;
        command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorOperationCommandResult result =
                quader_editor_core::execute_command(session_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        quader_editor_core::record_repeatable_operation(repeatLastAction_, command, result.command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)operation;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult beginTransformPreview(bool duplicateSelected) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::CommandResult result = session_.begin_transform_preview(duplicateSelected);
        if (result.changed) {
            syncSceneObjectsFromSdk(true);
            syncSceneSelectionFromSdk();
        }
        return {
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
#else
        (void)duplicateSelected;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateTransformTranslatePreview(Vec3 delta) {
#if QUADER_HAS_MODELING_SDK
        return updateTransformPreviewResult(session_.update_transform_translate_preview(toSdk(delta)));
#else
        (void)delta;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateTransformMatrixPreview(const ModelingTransform3& transform, Vec3 pivot) {
#if QUADER_HAS_MODELING_SDK
        return updateTransformPreviewResult(session_.update_transform_matrix_preview(toSdk(transform), toSdk(pivot)));
#else
        (void)transform;
        (void)pivot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateTransformExtrudePreview(Vec3 offset, float closedEdgeLedgeSize) {
#if QUADER_HAS_MODELING_SDK
        return updateTransformPreviewResult(
                session_.update_transform_extrude_preview(toSdk(offset), closedEdgeLedgeSize));
#else
        (void)offset;
        (void)closedEdgeLedgeSize;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateTransformInsetPreview(const ModelingTransform3& transform, Vec3 pivot) {
#if QUADER_HAS_MODELING_SDK
        return updateTransformPreviewResult(session_.update_transform_inset_preview(toSdk(transform), toSdk(pivot)));
#else
        (void)transform;
        (void)pivot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult commitTransformPreview() {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PreviewTransactionFinishResult finish =
                session_.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
        syncSceneObjectsFromSdk(finish.before.active);
        syncSceneSelectionFromSdk();
        return {
                .ok = !finish.before.active || finish.before.kind == quader_editor_core::PreviewTransactionKind::transform,
                .changed = finish.before.changed,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult cancelTransformPreview() {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PreviewTransactionFinishResult finish =
                session_.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
        syncSceneObjectsFromSdk(finish.before.active);
        syncSceneSelectionFromSdk();
        return {
                .ok = !finish.before.active || finish.before.kind == quader_editor_core::PreviewTransactionKind::transform,
                .changed = finish.restored,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult recordRepeatableTransform(const ModelingRepeatTransform& transform) {
#if QUADER_HAS_MODELING_SDK
        if (!transform.active) {
            return {.ok = false, .message = "No active transform to record."};
        }

        quader_editor_core::TransformDragState drag;
        drag.active = true;
        drag.tool = toSdk(transform.tool);
        drag.operation = toSdk(transform.operation);
        drag.axis = toSdk(transform.axis);
        drag.move.active = transform.tool == ModelingTransformTool::Move;
        drag.move.axis = drag.axis;
        drag.move.preview_delta = toSdk(transform.moveDelta);
        drag.rotate.active = transform.tool == ModelingTransformTool::Rotate;
        drag.rotate.axis = drag.axis;
        drag.rotate.preview_angle_degrees = transform.rotateAngleDegrees;
        drag.scale.active = transform.tool == ModelingTransformTool::Scale;
        drag.scale.axis = drag.axis;
        drag.scale.preview_scale = toSdk(transform.scale);

        quader_editor_core::record_repeatable_transform(
                repeatLastAction_,
                session_,
                drag,
                transform.duplicateSelected);
        return {.ok = true};
#else
        (void)transform;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingSelectionSummary pickSelect(const ModelingPickSelectRequest& request) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::PickSelectRuntimeCommandSettings settings;
        settings.ray = toSdk(request.ray);
        settings.click_count = request.clickCount;
        settings.control_pressed = request.controlPressed;
        settings.shift_pressed = request.shiftPressed;
        settings.command_pressed = request.commandPressed;
        settings.alt_pressed = request.altPressed;
        settings.vertex_radius = request.vertexRadius;
        settings.edge_radius = request.edgeRadius;

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::pick_select;
        command.settings = settings;
        (void)quader_editor_core::execute_runtime_command(session_, runtime_, command);
        syncSceneSelectionFromSdk();
        return selectionSummary();
#else
        (void)request;
        return {};
#endif
    }

    ModelingCommandResult updateSelectionHover(const ModelingHoverRequest& request) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::SelectionHoverUpdateResult hover =
                quader_editor_core::update_selection_hover(
                        session_.objects(),
                        session_.selection_mode(),
                        runtime_.hover(),
                        toSdk(request.ray),
                        request.selectedModifierPressed,
                        quader_editor_core::selection_pick_options_from_radii(request.vertexRadius, request.edgeRadius));
        runtime_.apply_hover_update(hover);
        return {
                .ok = true,
                .changed = hover.changed,
        };
#else
        (void)request;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult clearSelectionHover() {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::clear_hover;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult activateBoxTool() {
#if QUADER_HAS_MODELING_SDK
        if (quader_host_contract::find_tool_descriptor("box") == nullptr) {
            return {.ok = false, .message = "Box tool is not present in the modeling host contract."};
        }

        quader_editor_core::SetActiveToolRuntimeCommandSettings settings;
        settings.tool = quader_editor_core::ViewportInteractionTool::box;

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_active_tool;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCreateBoxResult createBoxFromBounds(const ModelingCreateBoxRequest& request) {
#if QUADER_HAS_MODELING_SDK
        const std::size_t objectCountBefore = session_.objects().size();
        quader_editor_core::CreateBoxFromBoundsRuntimeCommandSettings settings;
        settings.min_bounds = toSdk(request.bounds.min);
        settings.max_bounds = toSdk(request.bounds.max);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::create_box_from_bounds;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult sdkResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!sdkResult.command.ok || !sdkResult.command.changed || session_.objects().size() <= objectCountBefore) {
            return {
                    .ok = sdkResult.command.ok,
                    .changed = sdkResult.command.changed,
                    .message = sdkResult.command.message.empty() ? "SDK did not create a box object." : sdkResult.command.message,
            };
        }

        return createSceneObjectFromLatest(request.name.empty() ? "Box" : request.name, sdkResult.command.message);
#else
        (void)request;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview updateBoxHover(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_hover, settings);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview beginBoxFootprint(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        ModelingBoxPreview preview =
                executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::begin_box_footprint, settings);
        if (preview.ok) {
            scene_.clearSelection();
        }
        return preview;
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview updateBoxFootprint(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_footprint, settings);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview beginBoxHeight(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxHeightRuntimeCommandSettings settings;
        settings.screen_position = toSdk(event.screenPosition);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::begin_box_height, settings);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview updateBoxHeight(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxHeightRuntimeCommandSettings settings;
        settings.screen_position = toSdk(event.screenPosition);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_height, settings);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCreateBoxResult commitBox(std::string name) {
#if QUADER_HAS_MODELING_SDK
        const std::size_t objectCountBefore = session_.objects().size();
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::commit_box;

        const quader_editor_core::EditorRuntimeCommandResult sdkResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!sdkResult.command.ok || !sdkResult.command.changed || session_.objects().size() <= objectCountBefore) {
            return {
                    .ok = sdkResult.command.ok,
                    .changed = sdkResult.command.changed,
                    .message = sdkResult.command.message.empty() ? "SDK did not commit a box object." : sdkResult.command.message,
            };
        }

        return createSceneObjectFromLatest(name.empty() ? "Box" : name, sdkResult.command.message);
#else
        (void)name;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult cancelBox() {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::cancel_box;

        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

private:
#if QUADER_HAS_MODELING_SDK
    static bool hoverMatches(
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

    [[nodiscard]] bool sceneObjectsSyncedForCurrentSdkContent() const {
        if (!sceneObjectsSyncValid_ || syncedSdkContentRevision_ != session_.state().content_revision) {
            return false;
        }
        if (entityByObject_.size() != session_.objects().size()) {
            return false;
        }
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            const auto entity = entityByObject_.find(object.id);
            if (entity == entityByObject_.end() || !scene_.exists(entity->second)) {
                return false;
            }
            const ModelingObjectComponent* modelingObject = scene_.modelingObject(entity->second);
            const MeshComponent* mesh = scene_.mesh(entity->second);
            if (modelingObject == nullptr || mesh == nullptr || mesh->mesh == InvalidAssetId) {
                return false;
            }
        }
        return true;
    }

    ModelingCommandResult updateTransformPreviewResult(const quader_editor_core::CommandResult& result) {
        if (result.ok) {
            syncSceneObjectsFromSdk(true);
            syncSceneSelectionFromSdk();
        }
        return {
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
    }
#endif

    void syncSceneObjectsFromSdk(bool force = false) {
#if QUADER_HAS_MODELING_SDK
        if (!force && sceneObjectsSyncedForCurrentSdkContent()) {
            return;
        }

        std::unordered_map<std::uint32_t, AssetId> materialBySlot;
        std::optional<AssetId> firstModelingMaterial;
        for (const auto& [objectId, entity] : entityByObject_) {
            const ModelingObjectComponent* modelingObject = scene_.modelingObject(entity);
            const MaterialComponent* material = scene_.material(entity);
            if (modelingObject == nullptr || material == nullptr || material->material == InvalidAssetId) {
                continue;
            }
            materialBySlot.try_emplace(modelingObject->materialSlot, material->material);
            if (!firstModelingMaterial) {
                firstModelingMaterial = material->material;
            }
        }

        std::unordered_map<int, bool> liveObjects;
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            liveObjects[object.id] = true;
            const std::uint32_t materialSlot = static_cast<std::uint32_t>(object.material_id);
            const auto existing = entityByObject_.find(object.id);
            if (existing == entityByObject_.end()) {
                const std::string name = SceneEntityNaming::nextNameForKind(scene_, SceneEntityKind::Mesh);
                MeshAsset mesh = toMeshAsset(object);
                mesh.name = name;
                const AssetId meshId = assets_.addMesh(std::move(mesh));
                const EntityId entity = scene_.createEntity(name);
                scene_.setModelingObject(entity, ModelingObjectComponent{
                        .objectId = object.id,
                        .materialSlot = materialSlot,
                });
                scene_.setMesh(entity, MeshComponent{.mesh = meshId});
                const auto material = materialBySlot.find(materialSlot);
                if (material != materialBySlot.end()) {
                    scene_.setMaterial(entity, MaterialComponent{.material = material->second});
                } else if (firstModelingMaterial) {
                    scene_.setMaterial(entity, MaterialComponent{.material = *firstModelingMaterial});
                }
                entityByObject_[object.id] = entity;
                objectByEntity_[entity] = object.id;
                continue;
            }

            const EntityId entity = existing->second;
            MeshAsset mesh = toMeshAsset(object);
            if (const NameComponent* name = scene_.name(entity)) {
                mesh.name = name->name;
            }
            AssetId meshId = InvalidAssetId;
            if (const MeshComponent* meshComponent = scene_.mesh(entity)) {
                meshId = meshComponent->mesh;
            }
            bool meshContentsReplaced = false;
            if (meshId == InvalidAssetId) {
                meshId = assets_.addMesh(std::move(mesh));
            } else if (assets_.replaceMesh(meshId, std::move(mesh))) {
                meshContentsReplaced = true;
            } else {
                MeshAsset replacement = toMeshAsset(object);
                if (const NameComponent* name = scene_.name(entity)) {
                    replacement.name = name->name;
                }
                meshId = assets_.addMesh(std::move(replacement));
            }
            const ModelingObjectComponent nextObject{
                    .objectId = object.id,
                    .materialSlot = materialSlot,
            };
            const ModelingObjectComponent* currentObject = scene_.modelingObject(entity);
            if (currentObject == nullptr ||
                    currentObject->objectId != nextObject.objectId ||
                    currentObject->materialSlot != nextObject.materialSlot) {
                scene_.setModelingObject(entity, nextObject);
            }
            const MeshComponent* currentMesh = scene_.mesh(entity);
            if (currentMesh == nullptr || currentMesh->mesh != meshId || meshContentsReplaced) {
                scene_.setMesh(entity, MeshComponent{.mesh = meshId});
            }
        }

        std::vector<int> removedObjects;
        for (const auto& [objectId, entity] : entityByObject_) {
            if (!liveObjects.contains(objectId)) {
                removedObjects.push_back(objectId);
            }
        }
        for (const int objectId : removedObjects) {
            const EntityId entity = entityByObject_[objectId];
            if (scene_.exists(entity)) {
                scene_.destroyEntity(entity);
            }
            objectByEntity_.erase(entity);
            entityByObject_.erase(objectId);
        }
        syncedSdkContentRevision_ = session_.state().content_revision;
        sceneObjectsSyncValid_ = true;
#endif
    }

#if QUADER_HAS_MODELING_SDK
    ModelingCommandResult executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId commandId, bool mayChangeContent) {
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, noSettingsRuntimeCommand(commandId));
        if (mayChangeContent && session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }
#endif

    ModelingCreateBoxResult createSceneObjectFromLatest(const std::string& name, const std::string& message) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PolygonObject& object = session_.objects().back();
        MeshAsset mesh = toMeshAsset(object);
        mesh.name = name;
        const AssetId meshId = assets_.addMesh(std::move(mesh));

        const EntityId entity = scene_.createEntity(name);
        scene_.setModelingObject(entity, ModelingObjectComponent{
                .objectId = object.id,
                .materialSlot = static_cast<std::uint32_t>(object.material_id),
        });
        scene_.setMesh(entity, MeshComponent{.mesh = meshId});
        scene_.clearSelection();
        scene_.setSelected(entity, true);

        entityByObject_[object.id] = entity;
        objectByEntity_[entity] = object.id;

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
#else
        (void)name;
        (void)message;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

#if QUADER_HAS_MODELING_SDK
    template <typename Settings>
    ModelingBoxPreview executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId commandId, Settings settings) {
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = commandId;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult sdkResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!sdkResult.command.ok) {
            return {
                    .ok = false,
                    .message = sdkResult.command.message,
            };
        }

        quader_editor_core::BoxPreviewDescriptor preview = runtime_.box_preview_descriptor(true);
        std::vector<quader_poly::Vec3> corners;
        if (preview.drag_state == quader_editor_core::BoxDragState::height) {
            corners = quader_editor_core::box_preview_corners(preview);
        }

        ModelingBoxPreview result = fromSdk(preview, corners);
        result.ok = sdkResult.command.ok;
        if (!sdkResult.command.message.empty()) {
            result.message = sdkResult.command.message;
        }
        return result;
    }
#endif

    SceneDocument& scene_;
    AssetRegistry& assets_;
    std::unordered_map<int, EntityId> entityByObject_;
    std::unordered_map<EntityId, int> objectByEntity_;

#if QUADER_HAS_MODELING_SDK
    quader_editor_core::EditorSession session_;
    quader_editor_core::EditorRuntime runtime_;
    quader_editor_core::RepeatLastActionState repeatLastAction_;
    bool sceneObjectsSyncValid_ = false;
    std::uint64_t syncedSdkContentRevision_ = 0;
    mutable bool selectionOverlayCacheValid_ = false;
    mutable std::uint64_t selectionOverlayCacheContentRevision_ = 0;
    mutable std::uint64_t selectionOverlayCacheSelectionRevision_ = 0;
    mutable std::uint64_t selectionOverlayCacheSceneRevision_ = 0;
    mutable quader_editor_core::SelectionMode selectionOverlayCacheSelectionMode_ =
            quader_editor_core::SelectionMode::object;
    mutable quader_editor_core::SelectionHoverState selectionOverlayCacheHover_;
    mutable ViewportOverlayFrame selectionOverlayCacheFrame_;
#endif
};

ModelingSessionService::ModelingSessionService(SceneDocument& scene, AssetRegistry& assets)
    : impl_(std::make_unique<Impl>(scene, assets)) {}

ModelingSessionService::~ModelingSessionService() = default;
ModelingSessionService::ModelingSessionService(ModelingSessionService&&) noexcept = default;
ModelingSessionService& ModelingSessionService::operator=(ModelingSessionService&&) noexcept = default;

bool ModelingSessionService::sdkAvailable() const {
    return impl_->sdkAvailable();
}

ModelingCommandDescriptor ModelingSessionService::boxToolCommand() const {
    return impl_->boxToolCommand();
}

ModelingCommandDescriptor ModelingSessionService::createBoxFromBoundsCommand() const {
    return impl_->createBoxFromBoundsCommand();
}

std::vector<ModelingObjectRecord> ModelingSessionService::objects() const {
    return impl_->objects();
}

std::optional<EntityId> ModelingSessionService::entityForSdkObject(int sdkObjectId) const {
    return impl_->entityForSdkObject(sdkObjectId);
}

std::optional<int> ModelingSessionService::sdkObjectForEntity(EntityId entity) const {
    return impl_->sdkObjectForEntity(entity);
}

std::optional<MeshAsset> ModelingSessionService::compileObjectMesh(int sdkObjectId) const {
    return impl_->compileObjectMesh(sdkObjectId);
}

std::optional<Vec3> ModelingSessionService::selectionCenter() const {
    return impl_->selectionCenter();
}

ModelingSelectionMode ModelingSessionService::selectionMode() const {
    return impl_->selectionMode();
}

ModelingSelectionSummary ModelingSessionService::selectionSummary() const {
    return impl_->selectionSummary();
}

ViewportOverlayFrame ModelingSessionService::selectionOverlayFrame() const {
    return impl_->selectionOverlayFrame();
}

void ModelingSessionService::syncObjectSelectionFromScene() {
    impl_->syncObjectSelectionFromScene();
}

void ModelingSessionService::syncSceneSelectionFromSdk() {
    impl_->syncSceneSelectionFromSdk();
}

ModelingCommandResult ModelingSessionService::setSelectionMode(ModelingSelectionMode mode) {
    return impl_->setSelectionMode(mode);
}

ModelingCommandResult ModelingSessionService::selectAll() {
    return impl_->selectAll();
}

ModelingCommandResult ModelingSessionService::invertSelection() {
    return impl_->invertSelection();
}

ModelingCommandResult ModelingSessionService::clearSelection() {
    return impl_->clearSelection();
}

ModelingCommandResult ModelingSessionService::deleteSelection() {
    return impl_->deleteSelection();
}

ModelingCommandResult ModelingSessionService::copySelection() {
    return impl_->copySelection();
}

ModelingCommandResult ModelingSessionService::pasteSelection() {
    return impl_->pasteSelection();
}

ModelingCommandResult ModelingSessionService::undo() {
    return impl_->undo();
}

ModelingCommandResult ModelingSessionService::redo() {
    return impl_->redo();
}

ModelingCommandResult ModelingSessionService::repeatLastAction() {
    return impl_->repeatLastAction();
}

ModelingCommandResult ModelingSessionService::mirrorSelection(ModelingMirrorAxis axis) {
    return impl_->mirrorSelection(axis);
}

ModelingCommandResult ModelingSessionService::flipSelection(ModelingFlipOrientation orientation) {
    return impl_->flipSelection(orientation);
}

ModelingCommandResult ModelingSessionService::applyActiveMaterial(std::uint32_t materialSlot) {
    return impl_->applyActiveMaterial(materialSlot);
}

std::vector<ModelingInstantOperationDescriptor> ModelingSessionService::instantOperations() const {
    return impl_->instantOperations();
}

ModelingCommandResult ModelingSessionService::executeInstantOperation(ModelingInstantOperationId operation) {
    return impl_->executeInstantOperation(operation);
}

ModelingCommandResult ModelingSessionService::beginTransformPreview(bool duplicateSelected) {
    return impl_->beginTransformPreview(duplicateSelected);
}

ModelingCommandResult ModelingSessionService::updateTransformTranslatePreview(Vec3 delta) {
    return impl_->updateTransformTranslatePreview(delta);
}

ModelingCommandResult ModelingSessionService::updateTransformMatrixPreview(const ModelingTransform3& transform, Vec3 pivot) {
    return impl_->updateTransformMatrixPreview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::updateTransformExtrudePreview(Vec3 offset, float closedEdgeLedgeSize) {
    return impl_->updateTransformExtrudePreview(offset, closedEdgeLedgeSize);
}

ModelingCommandResult ModelingSessionService::updateTransformInsetPreview(const ModelingTransform3& transform, Vec3 pivot) {
    return impl_->updateTransformInsetPreview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::commitTransformPreview() {
    return impl_->commitTransformPreview();
}

ModelingCommandResult ModelingSessionService::cancelTransformPreview() {
    return impl_->cancelTransformPreview();
}

ModelingCommandResult ModelingSessionService::recordRepeatableTransform(const ModelingRepeatTransform& transform) {
    return impl_->recordRepeatableTransform(transform);
}

ModelingSelectionSummary ModelingSessionService::pickSelect(const ModelingPickSelectRequest& request) {
    return impl_->pickSelect(request);
}

ModelingCommandResult ModelingSessionService::updateSelectionHover(const ModelingHoverRequest& request) {
    return impl_->updateSelectionHover(request);
}

ModelingCommandResult ModelingSessionService::clearSelectionHover() {
    return impl_->clearSelectionHover();
}

ModelingCommandResult ModelingSessionService::activateBoxTool() {
    return impl_->activateBoxTool();
}

ModelingCreateBoxResult ModelingSessionService::createBoxFromBounds(const ModelingCreateBoxRequest& request) {
    return impl_->createBoxFromBounds(request);
}

ModelingBoxPreview ModelingSessionService::updateBoxHover(const ModelingBoxToolEvent& event) {
    return impl_->updateBoxHover(event);
}

ModelingBoxPreview ModelingSessionService::beginBoxFootprint(const ModelingBoxToolEvent& event) {
    return impl_->beginBoxFootprint(event);
}

ModelingBoxPreview ModelingSessionService::updateBoxFootprint(const ModelingBoxToolEvent& event) {
    return impl_->updateBoxFootprint(event);
}

ModelingBoxPreview ModelingSessionService::beginBoxHeight(const ModelingBoxToolEvent& event) {
    return impl_->beginBoxHeight(event);
}

ModelingBoxPreview ModelingSessionService::updateBoxHeight(const ModelingBoxToolEvent& event) {
    return impl_->updateBoxHeight(event);
}

ModelingCreateBoxResult ModelingSessionService::commitBox(std::string name) {
    return impl_->commitBox(std::move(name));
}

ModelingCommandResult ModelingSessionService::cancelBox() {
    return impl_->cancelBox();
}

} // namespace quader
