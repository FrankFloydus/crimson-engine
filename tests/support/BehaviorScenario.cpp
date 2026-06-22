////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "BehaviorScenario.h"

#if QUADER_TEST_HAS_PROGRAMMATIC_API

#include "BehaviorExpect.h"
#include "assets/AssetRegistry.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "scene/SceneBounds.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

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

} // namespace

BehaviorScenario::BehaviorScenario()
    : session_()
    , api_(session_) {
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

void BehaviorScenario::recordStatus(const QuaderApiStatus& status) {
    lastStatus_ = status;
}

void BehaviorScenario::recordSelection(const QuaderApiResult<ModelingSelectionSummary>& selection) {
    recordStatus(selection);
    if (selection.ok) {
        lastSelection_ = selection.value;
    }
}

void BehaviorScenario::recordDrag(const ModelingTransformDragPreview& drag) {
    lastDrag_ = drag;
    lastStatus_ = {
            .ok = drag.ok,
            .changed = drag.changed,
            .message = drag.message,
            .mutationKind = QuaderMutationKind::Modeling,
            .revisions = api_.revisions(),
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

BehaviorGiven& BehaviorGiven::box(std::string_view alias, ModelingBounds bounds, std::string name) {
    QuaderCreateBoxArgs args;
    args.bounds = bounds;
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

BehaviorGiven& BehaviorGiven::selected(std::string_view alias) {
    const QuaderApiStatus status = scenario_.api().selection().selectObject(scenario_.object(alias));
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::selectionMode(ModelingSelectionMode mode) {
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

BehaviorGiven& BehaviorGiven::allComponentsSelected(ModelingSelectionMode mode) {
    selectionMode(mode);
    const QuaderApiStatus status = scenario_.api().selection().selectAll();
    scenario_.recordStatus(status);
    if (!status.ok) {
        ADD_FAILURE() << status.message;
    }
    return *this;
}

BehaviorGiven& BehaviorGiven::transform(std::string_view alias, TransformComponent transform) {
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

BehaviorWhen& BehaviorWhen::pick(Vec3 rayOrigin, PickOptions options) {
    const QuaderApiResult<ModelingSelectionSummary> result = scenario_.api().selection().pickSelect(pickAt(rayOrigin, options));
    scenario_.recordSelection(result);
    return *this;
}

BehaviorWhen& BehaviorWhen::hover(Vec3 rayOrigin, HoverOptions options) {
    const QuaderApiResult<ModelingHoverSummary> result = scenario_.api().selection().hover(hoverAt(rayOrigin, options));
    scenario_.recordStatus(result);
    return *this;
}

BehaviorWhen& BehaviorWhen::hoverVertex(std::string_view alias, int vertex) {
    const QuaderApiResult<ModelingHoverSummary> result = scenario_.api().selection().hoverVertex(scenario_.object(alias), vertex);
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

BehaviorWhen& BehaviorWhen::translatePreview(Vec3 delta) {
    const QuaderApiStatus status = scenario_.api().modeling().updateTransformTranslate(delta);
    scenario_.recordStatus(status);
    return *this;
}

BehaviorWhen& BehaviorWhen::matrixPreview(const ModelingTransform3& transform, Vec3 pivot) {
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
        if (const std::optional<Vec3> center = scenario_.api().query().selectionCenter()) {
            options.pivot = *center;
        }
    }
    const QuaderApiResult<ModelingTransformDragPreview> result =
            scenario_.api().modeling().beginTransformDrag(transformDragBegin(options));
    scenario_.recordDrag(result.value);
    if (!result.ok) {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::updateTransformDrag(Vec2 screenPosition) {
    const QuaderApiResult<ModelingTransformDragPreview> result =
            scenario_.api().modeling().updateTransformDrag(transformDragUpdate(screenPosition));
    scenario_.recordDrag(result.value);
    if (!result.ok) {
        ADD_FAILURE() << result.message;
    }
    return *this;
}

BehaviorWhen& BehaviorWhen::updateTransformDrag(float screenX) {
    return updateTransformDrag(Vec2{screenX, 0.0f});
}

BehaviorWhen& BehaviorWhen::beginXMoveDrag(DragBeginOptions options) {
    return beginTransformDrag(options);
}

BehaviorWhen& BehaviorWhen::updateXMoveDrag(float screenX) {
    return updateTransformDrag(screenX);
}

BehaviorWhen& BehaviorWhen::commitDrag() {
    const QuaderApiResult<ModelingTransformDragPreview> result =
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

BehaviorWhen& BehaviorWhen::assignMaterialSlot(std::uint32_t materialSlot) {
    const QuaderApiStatus status = scenario_.api().modeling().assignMaterialSlotToSelection(materialSlot);
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

#endif
