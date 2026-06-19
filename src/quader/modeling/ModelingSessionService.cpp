#include "quader/modeling/ModelingSessionService.h"

#include "quader/assets/AssetRegistry.h"
#include "quader/render/ViewportOverlayPolicy.h"
#include "quader/scene/SceneDocument.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/runtime_command.h>
#include <quader_editor_core/session.h>
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

ViewportOverlayFrame fromSdk(const quader_editor_core::SelectionOverlayFrame& frame) {
    ViewportOverlayFrame result;
    result.triangles.reserve(frame.triangles.size());
    result.lines.reserve(frame.lines.size());
    result.points.reserve(frame.points.size());
    result.hoverTargetSelected = frame.hover_target_selected;

    for (const quader_editor_core::SelectionOverlayTriangle& triangle : frame.triangles) {
        result.triangles.push_back(ViewportOverlayTriangle{
                .layer = fromSdk(triangle.layer),
                .a = fromSdk(triangle.a),
                .b = fromSdk(triangle.b),
                .c = fromSdk(triangle.c),
                .color = viewportOverlayColorFor(fromSdk(triangle.layer), triangle.color_abgr),
        });
    }
    for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
        result.lines.push_back(ViewportOverlayLine{
                .layer = fromSdk(line.layer),
                .start = fromSdk(line.start),
                .end = fromSdk(line.end),
                .color = viewportOverlayColorFor(fromSdk(line.layer), line.color_abgr),
                .widthPixels = line.width_pixels,
        });
    }
    for (const quader_editor_core::SelectionOverlayPoint& point : frame.points) {
        result.points.push_back(ViewportOverlayPoint{
                .layer = fromSdk(point.layer),
                .position = fromSdk(point.position),
                .color = viewportOverlayColorFor(fromSdk(point.layer), point.color_abgr),
                .sizePixels = point.size_pixels,
        });
    }
    return result;
}

MeshAsset toMeshAsset(const quader_editor_core::PolygonObject& object) {
    const quader_poly::CompiledMesh compiled = quader_poly::compile_document(object.document);

    MeshAsset mesh;
    mesh.name = "SDK Object " + std::to_string(object.id);
    mesh.vertices.reserve(compiled.vertices.size());
    mesh.indices = compiled.indices;
    mesh.hasStableCpuTopology = true;

    for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
        mesh.vertices.push_back(MeshVertex{
                .position = fromSdk(vertex.position),
                .normal = fromSdk(vertex.normal),
                .uv = {vertex.uv0.x, vertex.uv0.y},
                .rgba = vertex.color,
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
        const std::optional<quader_poly::Vec3> center = session_.selection_center();
        if (!center) {
            return std::nullopt;
        }
        return fromSdk(*center);
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
        return fromSdk(quader_editor_core::build_selection_overlay_frame(
                session_.objects(),
                session_.selection_mode(),
                runtime_.hover()));
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
        std::vector<int> selectedObjectIds = session_.selected_object_ids();
        const int activeObjectId = session_.active_object_id();
        scene_.clearSelection();
        for (const int objectId : selectedObjectIds) {
            const auto it = entityByObject_.find(objectId);
            if (it != entityByObject_.end()) {
                scene_.setSelected(it->second, true);
            }
        }
        const auto active = entityByObject_.find(activeObjectId);
        if (active != entityByObject_.end()) {
            const SelectionComponent* selection = scene_.selection(active->second);
            if (!selection || !selection->selected) {
                scene_.setSelected(active->second, true);
            }
            scene_.setActiveSelection(active->second);
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
