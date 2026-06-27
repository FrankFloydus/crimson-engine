////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "behavior_scenario.hpp"

#include "behavior_expect.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "assets/asset_registry.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/scene_bounds.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace quader::test {
namespace {

QuaderApiStatus failedStatus(std::string message) {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
    };
}

BehaviorBounds toBehaviorBounds(const SceneBounds& bounds) {
    return {
            .min = bounds.min,
            .max = bounds.max,
    };
}

void addOverlayStats(BehaviorSnapshot& snapshot, const ViewportOverlayFrame& frame) {
    snapshot.overlay.lineCount = frame.lines.size();
    snapshot.overlay.triangleCount = frame.triangles.size();
    snapshot.overlay.pointCount = frame.points.size();
    for (const ViewportOverlayLine& line : frame.lines) {
        ++snapshot.overlay.linesByLayer[line.layer];
    }
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        ++snapshot.overlay.trianglesByLayer[triangle.layer];
    }
    for (const ViewportOverlayPoint& point : frame.points) {
        ++snapshot.overlay.pointsByLayer[point.layer];
    }
}

QuaderPickSelectRequest apiPickAt(QVec3 origin, PickOptions options) {
    const ModelingPickSelectRequest request = pickAt(origin, options);
    return {
            .ray = toApiRay(request.ray),
            .clickCount = request.clickCount,
            .controlPressed = request.controlPressed,
            .shiftPressed = request.shiftPressed,
            .commandPressed = request.commandPressed,
            .altPressed = request.altPressed,
            .preserveSelectionOnMiss = request.preserveSelectionOnMiss,
            .preserveSelectionOnSelectedHit = request.preserveSelectionOnSelectedHit,
            .vertexRadius = request.vertexRadius,
            .edgeRadius = request.edgeRadius,
    };
}

QuaderHoverRequest apiHoverAt(QVec3 origin, HoverOptions options) {
    const ModelingHoverRequest request = hoverAt(origin, options);
    return {
            .ray = toApiRay(request.ray),
            .selectedModifierPressed = request.selectedModifierPressed,
            .vertexRadius = request.vertexRadius,
            .edgeRadius = request.edgeRadius,
    };
}

QuaderTransformDragBeginRequest apiTransformDragBegin(DragBeginOptions options) {
    const ModelingTransformDragBeginRequest request = transformDragBegin(options);
    return {
            .tool = toApiTransformTool(request.tool),
            .operation = toApiTransformOperation(request.operation),
            .axis = toApiTransformAxis(request.axis),
            .pivot = request.pivot,
            .screenPosition = request.screenPosition,
            .screenPivot = request.screenPivot,
            .screenTip = request.screenTip,
            .hasWorldDirection = request.hasWorldDirection,
            .worldDirection = request.worldDirection,
            .ray = toApiRay(request.ray),
            .camera = toApiViewportCamera(request.camera),
            .gridSize = request.gridSize,
            .vertexSnapRadius = request.vertexSnapRadius,
            .duplicateSelected = request.duplicateSelected,
    };
}

QuaderTransformDragUpdateRequest apiTransformDragUpdate(QVec2 screenPosition) {
    const ModelingTransformDragUpdateRequest request = transformDragUpdate(screenPosition);
    return {
            .screenPosition = request.screenPosition,
            .ray = toApiRay(request.ray),
            .camera = toApiViewportCamera(request.camera),
            .gridSize = request.gridSize,
            .vertexSnapRadius = request.vertexSnapRadius,
            .precision = request.precision,
    };
}

} // namespace

BehaviorScenario::BehaviorScenario()
    : session_()
    , editor_(QuaderApiSessionInternalAccess::fromProjectSession(session_)) {
    lastStatus_.ok = true;
}

BehaviorScenario BehaviorScenario::newProject() {
    return BehaviorScenario();
}

BehaviorGiven BehaviorScenario::given() {
    return BehaviorGiven(*this);
}

BehaviorWhen BehaviorScenario::when() {
    return BehaviorWhen(*this);
}

BehaviorThen BehaviorScenario::then() {
    return BehaviorThen(*this);
}

EntityId BehaviorScenario::entity(std::string_view alias) const {
    return object(alias).entity;
}

QuaderModelingObjectRef BehaviorScenario::object(std::string_view alias) const {
    const auto found = aliases_.find(std::string(alias));
    if (found == aliases_.end()) {
        ADD_FAILURE() << "Unknown behavior alias: " << alias;
        return {};
    }
    return found->second;
}

ModelingObjectId BehaviorScenario::modelingObject(std::string_view alias) const {
    return object(alias).modelingObjectId;
}

BehaviorSnapshot BehaviorScenario::snapshot(std::string label) const {
    BehaviorSnapshot result;
    result.label = std::move(label);
    result.selection = session_.modeling().selectionSummary();
    result.toolStatus = session_.modeling().toolStatus();
    addOverlayStats(result, session_.modeling().selectionOverlayFrame());

    for (const auto& [alias, object] : aliases_) {
        EntitySnapshot entity;
        entity.id = object.entity;
        entity.alias = alias;
        entity.mesh = object.mesh;
        entity.modelingObjectId = object.modelingObjectId;
        if (const NameComponent* name = session_.scene().name(object.entity)) {
            entity.name = name->name;
        }
        if (const TransformComponent* transform = session_.scene().transform(object.entity)) {
            entity.transform = *transform;
        }
        if (const SelectionComponent* selection = session_.scene().selection(object.entity)) {
            entity.selected = selection->selected;
        }
        if (const MeshComponent* mesh = session_.scene().mesh(object.entity)) {
            entity.mesh = mesh->mesh;
        }
        result.entities.push_back(entity);

        if (const MeshAsset* mesh = session_.assets().mesh(entity.mesh)) {
            MeshSnapshot meshSnapshot;
            meshSnapshot.mesh = entity.mesh;
            meshSnapshot.vertexCount = mesh->vertices.size();
            meshSnapshot.indexCount = mesh->indices.size();
            meshSnapshot.positions.reserve(mesh->vertices.size());
            for (const MeshVertex& vertex : mesh->vertices) {
                meshSnapshot.positions.push_back(vertex.position);
            }
            if (const std::optional<SceneBounds> bounds = localMeshBounds(*mesh)) {
                meshSnapshot.bounds = toBehaviorBounds(*bounds);
            }
            result.meshes.push_back(meshSnapshot);
        }
    }

    if (const std::optional<SceneBounds> bounds = selectedWorldBounds(session_.scene(), session_.assets())) {
        result.selectedBounds = toBehaviorBounds(*bounds);
    }

    return result;
}

void BehaviorScenario::remember(std::string_view alias, QuaderModelingObjectRef object) {
    aliases_.insert_or_assign(std::string(alias), object);
}

void BehaviorScenario::remember(std::string_view alias, QuaderEntityRef entity) {
    remember(alias, entity.id);
}

void BehaviorScenario::remember(std::string_view alias, EntityId entity) {
    aliases_.insert_or_assign(std::string(alias), objectForEntity(entity));
}

void BehaviorScenario::rememberEntityNamed(std::string_view alias, std::string_view name) {
    for (const QuaderEntitySnapshot& snapshot : api().query().entities()) {
        if (snapshot.name == name) {
            remember(alias, snapshot.id);
            return;
        }
    }
    ADD_FAILURE() << "Missing entity named " << name;
}

void BehaviorScenario::recordStatus(const QuaderApiStatus& status) {
    lastStatus_ = status;
}

void BehaviorScenario::recordSelection(const QuaderApiResult<QuaderSelectionSummary>& selection) {
    recordStatus(selection);
    if (selection.ok) {
        lastSelection_ = selection.value;
    }
}

void BehaviorScenario::recordDrag(const QuaderTransformDragPreview& drag) {
    lastDrag_ = drag;
    lastStatus_ = {
            .ok = drag.ok,
            .changed = drag.changed,
            .message = drag.message,
            .mutationKind = QuaderMutationKind::Modeling,
            .revisions = api().revisions(),
    };
}

void BehaviorScenario::rememberSelectedCopyAlias() {
    const std::vector<EntityId> selected = session_.scene().selectedEntities();
    if (selected.size() != 1u) {
        return;
    }

    const EntityId selectedEntity = selected.front();
    const auto selectedAlreadyAliased = std::any_of(aliases_.begin(), aliases_.end(), [&](const auto& entry) {
        return entry.second.entity == selectedEntity;
    });
    if (selectedAlreadyAliased) {
        return;
    }

    for (const auto& [alias, object] : aliases_) {
        if (object.entity == selectedEntity) {
            continue;
        }
        const std::string copyAlias = alias + ".copy";
        if (!aliases_.contains(copyAlias)) {
            remember(copyAlias, objectForEntity(selectedEntity));
        }
        return;
    }
}

QuaderModelingObjectRef BehaviorScenario::objectForEntity(EntityId entity) const {
    QuaderModelingObjectRef result;
    result.entity = entity;
    result.editorObjectId = session_.scene().editorObjectId(entity);
    if (const ModelingObjectComponent* modeling = session_.scene().modelingObject(entity)) {
        result.modelingObjectId = modeling->objectId;
    }
    if (const MeshComponent* mesh = session_.scene().mesh(entity)) {
        result.mesh = mesh->mesh;
    }
    if (const MaterialComponent* material = session_.scene().material(entity)) {
        result.material = material->material;
    }
    return result;
}

BehaviorGiven& BehaviorGiven::unitBox(std::string_view alias, std::string name) {
    return box(alias, {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, std::move(name));
}

BehaviorGiven& BehaviorGiven::box(std::string_view alias, QuaderBounds bounds, std::string name) {
    QuaderCreateBoxArgs args;
    args.bounds = {
            .min = bounds.min,
            .max = bounds.max,
    };
    args.name = std::move(name);
    const QuaderApiResult<QuaderModelingObjectRef> result = scenario_.api().modeling().createBox(args);
    scenario_.recordStatus(result);
    if (result.ok) {
        scenario_.remember(alias, result.value);
    } else {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::mesh(std::string_view alias, QuaderPolygonDocument document, std::string name) {
    QuaderCreateMeshArgs args;
    args.name = std::move(name);
    const QuaderApiResult<QuaderModelingObjectRef> result = scenario_.api().modeling().createMesh(std::move(document), args);
    scenario_.recordStatus(result);
    if (result.ok) {
        scenario_.remember(alias, result.value);
    } else {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::entity(
        std::string_view alias,
        std::string name,
        QuaderEntityKind kind,
        QuaderTransform transform,
        std::string_view parentAlias) {
    QuaderCreateEntityArgs args;
    args.name = std::move(name);
    args.kind = kind;
    args.transform = transform;
    if (!parentAlias.empty()) {
        args.parent = scenario_.entity(parentAlias);
    }
    const QuaderApiResult<QuaderEntityRef> result = scenario_.api().scene().createEntity(args);
    scenario_.recordStatus(result);
    if (result.ok) {
        scenario_.remember(alias, result.value);
    } else {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::selectedAliases(
        std::initializer_list<std::string_view> aliases,
        std::string_view activeAlias) {
    std::vector<EntityId> entities;
    entities.reserve(aliases.size());
    for (const std::string_view alias : aliases) {
        entities.push_back(scenario_.entity(alias));
    }
    const QuaderApiStatus status = scenario_.api().selection().selectObjects(
            std::span<const EntityId>(entities.data(), entities.size()),
            scenario_.entity(activeAlias));
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::selected(std::string_view alias) {
    const QuaderApiStatus status = scenario_.api().selection().selectObject(scenario_.object(alias));
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::selectionMode(QuaderSelectionMode mode) {
    const QuaderApiStatus status = scenario_.api().selection().setMode(mode);
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::vertexSelected(std::string_view alias, int vertex) {
    const QuaderApiStatus status = scenario_.api().selection().selectVertex(scenario_.object(alias), vertex);
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::edgeSelected(std::string_view alias, int a, int b) {
    const QuaderApiStatus status = scenario_.api().selection().selectEdge(scenario_.object(alias), a, b);
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::faceSelected(std::string_view alias, int face) {
    const QuaderApiStatus status = scenario_.api().selection().selectFace(scenario_.object(alias), face);
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::allComponentsSelected(QuaderSelectionMode mode) {
    selectionMode(mode);
    const QuaderApiStatus status = scenario_.api().selection().selectAll();
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::transform(std::string_view alias, QuaderTransform transform) {
    const QuaderApiStatus status = scenario_.api().scene().setTransform(scenario_.entity(alias), transform);
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::material(std::string_view alias, AssetId material) {
    const QuaderApiStatus status = scenario_.api().assets().assignMaterialToObject(scenario_.object(alias), material);
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::activate(ModelingViewportTool) {
    scenario_.recordStatus(failedStatus("Viewport tool activation is not part of the current QuaderApi surface."));
    return *this;
}

BehaviorWhen& BehaviorWhen::pick(QVec3 rayOrigin, PickOptions options) {
    const QuaderApiResult<QuaderSelectionSummary> result =
            scenario_.api().selection().pickSelect(apiPickAt(rayOrigin, options));
    scenario_.recordSelection(result);
    return *this;
}

BehaviorWhen& BehaviorWhen::hover(QVec3 rayOrigin, HoverOptions options) {
    const QuaderApiResult<QuaderHoverSummary> result =
            scenario_.api().selection().hover(apiHoverAt(rayOrigin, options));
    scenario_.recordStatus(result);
    return *this;
}

BehaviorWhen& BehaviorWhen::hoverVertex(std::string_view alias, int vertex) {
    const QuaderApiResult<QuaderHoverSummary> result = scenario_.api().selection().hoverVertex(scenario_.object(alias), vertex);
    scenario_.recordStatus(result);
    return *this;
}

BehaviorWhen& BehaviorWhen::clearHover() {
    const QuaderApiStatus status = scenario_.api().selection().clearHover();
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::beginTransformPreview(bool duplicateSelected) {
    const QuaderApiStatus status = scenario_.api().modeling().beginTransformPreview(duplicateSelected);
    scenario_.recordStatus(status);
    if (status.ok && duplicateSelected) {
        scenario_.rememberSelectedCopyAlias();
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::translatePreview(QVec3 delta) {
    const QuaderApiStatus status = scenario_.api().modeling().updateTransformTranslate(delta);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::matrixPreview(const QuaderTransform3& transform, QVec3 pivot) {
    const QuaderApiStatus status = scenario_.api().modeling().updateTransformMatrix(transform, pivot);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::commitTransformPreview() {
    const QuaderApiStatus status = scenario_.api().modeling().commitTransformPreview();
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::cancelTransformPreview() {
    const QuaderApiStatus status = scenario_.api().modeling().cancelTransformPreview();
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::beginTransformDrag(DragBeginOptions options) {
    if (options.useSelectionCenter) {
        if (const std::optional<QVec3> center = scenario_.api().query().selectionCenter()) {
            options.pivot = *center;
        }
    }
    const QuaderApiResult<QuaderTransformDragPreview> result =
            scenario_.api().modeling().beginTransformDrag(apiTransformDragBegin(options));
    scenario_.recordDrag(result.value);
    if (!result.ok) {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::updateTransformDrag(QVec2 screenPosition) {
    const QuaderApiResult<QuaderTransformDragPreview> result =
            scenario_.api().modeling().updateTransformDrag(apiTransformDragUpdate(screenPosition));
    scenario_.recordDrag(result.value);
    if (!result.ok) {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::updateTransformDrag(float screenX) {
    return updateTransformDrag(QVec2{screenX, 0.0f});
}

BehaviorWhen& BehaviorWhen::beginXMoveDrag(DragBeginOptions options) {
    return beginTransformDrag(options);
}

BehaviorWhen& BehaviorWhen::updateXMoveDrag(float screenX) {
    return updateTransformDrag(screenX);
}

BehaviorWhen& BehaviorWhen::commitDrag() {
    const QuaderApiResult<QuaderTransformDragPreview> result =
            scenario_.api().modeling().commitTransformDrag();
    scenario_.recordDrag(result.value);
    if (!result.ok) {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::cancelDrag() {
    const QuaderApiStatus status = scenario_.api().modeling().cancelTransformDrag();
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::executeTool(std::string_view toolId) {
    const QuaderApiStatus status = scenario_.api().modeling().executeTool(toolId);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::execute(QuaderCommandId command) {
    const QuaderApiStatus status = scenario_.editor().commands().execute(command);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::execute(std::string_view commandId) {
    const QuaderApiStatus status = scenario_.editor().commands().execute(commandId);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::assignMaterialSlot(std::uint32_t materialSlot) {
    const QuaderApiStatus status = scenario_.api().modeling().assignMaterialSlotToSelection(materialSlot);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::clearSceneSelection() {
    const QuaderApiStatus status = scenario_.api().selection().selectObjects(std::span<const EntityId>());
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::groupSelection(std::string_view alias, std::string name, QuaderTransform transform) {
    const QuaderApiResult<QuaderEntityRef> result = scenario_.api().scene().groupSelection(std::move(name), transform);
    scenario_.recordStatus(result);
    if (result.ok) {
        scenario_.remember(alias, result.value);
    } else {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::ungroupSelection() {
    const QuaderApiStatus status = scenario_.api().scene().ungroupSelection();
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::duplicateSelectedHierarchy(std::string_view rootAlias) {
    const QuaderApiResult<QuaderDuplicateHierarchyResult> result = scenario_.api().scene().duplicateSelectedHierarchy();
    scenario_.recordStatus(result);
    if (result.ok && result.value.rootEntities.size() == 1u) {
        scenario_.remember(rootAlias, result.value.rootEntities.front());
    } else if (result.ok) {
        ADD_FAILURE() << "Expected exactly one duplicated root, got " << result.value.rootEntities.size();
    } else {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::destroy(std::string_view alias) {
    const QuaderApiStatus status = scenario_.api().scene().destroy(scenario_.entity(alias));
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::rename(std::string_view alias, std::string name) {
    const QuaderApiStatus status = scenario_.api().scene().rename(scenario_.entity(alias), std::move(name));
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::reparent(
        std::string_view alias,
        std::string_view parentAlias,
        bool preserveWorldTransform) {
    const QuaderApiStatus status = scenario_.api().scene().reparent(
            scenario_.entity(alias),
            scenario_.entity(parentAlias),
            preserveWorldTransform);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::setTransform(std::string_view alias, QuaderTransform transform) {
    const QuaderApiStatus status = scenario_.api().scene().setTransform(scenario_.entity(alias), transform);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::setVisible(std::string_view alias, bool visible) {
    const QuaderApiStatus status = scenario_.api().scene().setVisible(scenario_.entity(alias), visible);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::setLocked(std::string_view alias, bool locked) {
    const QuaderApiStatus status = scenario_.api().scene().setLocked(scenario_.entity(alias), locked);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::setGroupFlags(
        std::string_view alias,
        QuaderTransform transform,
        bool visible,
        bool locked) {
    const EntityId entity = scenario_.entity(alias);
    const QuaderApiStatus status = scenario_.api().scene().batch("Group Flags")
            .setTransform(entity, transform)
            .setVisible(entity, visible)
            .setLocked(entity, locked)
            .commit();
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::undo() {
    scenario_.recordStatus(scenario_.api().history().undo());
    return *this;
}

BehaviorWhen& BehaviorWhen::redo() {
    scenario_.recordStatus(scenario_.api().history().redo());
    return *this;
}

CommandExpect BehaviorThen::command() const {
    return CommandExpect(scenario_.lastStatus_);
}

SceneExpect BehaviorThen::scene() const {
    return SceneExpect(scenario_);
}

SelectionExpect BehaviorThen::selection() const {
    return SelectionExpect(scenario_);
}

MeshExpect BehaviorThen::mesh(std::string_view alias) const {
    return MeshExpect(scenario_, alias);
}

OverlayExpect BehaviorThen::overlay() const {
    return OverlayExpect(scenario_);
}

AssetExpect BehaviorThen::assets() const {
    return AssetExpect(scenario_);
}

ToolStatusExpect BehaviorThen::tool() const {
    return ToolStatusExpect(scenario_);
}

UndoRedoExpect BehaviorThen::history() const {
    return UndoRedoExpect(scenario_);
}

SnapshotExpect BehaviorThen::snapshot(const BehaviorSnapshot& snapshot) const {
    return SnapshotExpect(snapshot);
}

} // namespace quader::test
