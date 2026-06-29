////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "behavior_expect.hpp"

#include "assets/asset_registry.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace quader::test {
namespace {

QVec3 center(const BehaviorBounds& bounds) {
    return (bounds.min + bounds.max) * 0.5f;
}

void expectVec3Near(QVec3 actual, QVec3 expected, float epsilon) {
    EXPECT_NEAR(actual.x, expected.x, epsilon);
    EXPECT_NEAR(actual.y, expected.y, epsilon);
    EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expectTransformNear(const TransformComponent& actual, const TransformComponent& expected, float epsilon) {
    expectVec3Near(actual.position, expected.position, epsilon);
    expectVec3Near(actual.rotationEuler, expected.rotationEuler, epsilon);
    expectVec3Near(actual.scale, expected.scale, epsilon);
}

void expectTransformNear(const QuaderTransform& actual, const TransformComponent& expected, float epsilon) {
    expectVec3Near(actual.position, expected.position, epsilon);
    expectVec3Near(actual.rotationEuler, expected.rotationEuler, epsilon);
    expectVec3Near(actual.scale, expected.scale, epsilon);
}

void expectTransformNear(const QuaderTransform& actual, const QuaderTransform& expected, float epsilon) {
    expectVec3Near(actual.position, expected.position, epsilon);
    expectVec3Near(actual.rotationEuler, expected.rotationEuler, epsilon);
    expectVec3Near(actual.scale, expected.scale, epsilon);
}

ModelingSelectionMode modelingModeFromApi(QuaderSelectionMode mode) {
    switch (mode) {
    case QuaderSelectionMode::Vertex:
        return ModelingSelectionMode::Vertex;
    case QuaderSelectionMode::Edge:
        return ModelingSelectionMode::Edge;
    case QuaderSelectionMode::Face:
        return ModelingSelectionMode::Face;
    case QuaderSelectionMode::Object:
        return ModelingSelectionMode::Object;
    case QuaderSelectionMode::Group:
        return ModelingSelectionMode::Group;
    }
    return ModelingSelectionMode::Object;
}

std::size_t countForLayer(const std::unordered_map<ViewportOverlayLayer, std::size_t>& counts, ViewportOverlayLayer layer) {
    const auto found = counts.find(layer);
    return found == counts.end() ? 0u : found->second;
}

std::optional<MeshSnapshot> findMeshSnapshot(const BehaviorSnapshot& snapshot, AssetId mesh) {
    const auto found = std::find_if(snapshot.meshes.begin(), snapshot.meshes.end(), [&](const MeshSnapshot& value) {
        return value.mesh == mesh;
    });
    if (found == snapshot.meshes.end()) {
        return std::nullopt;
    }
    return *found;
}

std::optional<EntitySnapshot> findEntitySnapshot(const BehaviorSnapshot& snapshot, std::string_view alias) {
    const auto found = std::find_if(snapshot.entities.begin(), snapshot.entities.end(), [&](const EntitySnapshot& value) {
        return value.alias == alias;
    });
    if (found == snapshot.entities.end()) {
        return std::nullopt;
    }
    return *found;
}

std::optional<QuaderEntitySnapshot> findEntityNamed(const BehaviorScenario& scenario, std::string_view name) {
    const std::vector<QuaderEntitySnapshot> entities = scenario.api().query().entities();
    const auto found = std::find_if(entities.begin(), entities.end(), [&](const QuaderEntitySnapshot& value) {
        return value.name == name;
    });
    if (found == entities.end()) {
        return std::nullopt;
    }
    return *found;
}

std::optional<QuaderEntitySnapshot> findEntityAlias(const BehaviorScenario& scenario, std::string_view alias) {
    return scenario.api().query().entity(scenario.entity(alias));
}

void includePoint(BehaviorBounds& bounds, QVec3 point) {
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

} // namespace

CommandExpect& CommandExpect::ok() {
    EXPECT_TRUE(status_.ok) << status_.message;
    return *this;
}

CommandExpect& CommandExpect::okChanged() {
    ok();
    EXPECT_TRUE(status_.changed) << status_.message;
    return *this;
}

CommandExpect& CommandExpect::unchanged() {
    ok();
    EXPECT_FALSE(status_.changed) << status_.message;
    return *this;
}

CommandExpect& CommandExpect::failed() {
    EXPECT_FALSE(status_.ok);
    return *this;
}

CommandExpect& CommandExpect::messageContains(std::string_view text) {
    EXPECT_NE(status_.message.find(std::string(text)), std::string::npos) << status_.message;
    return *this;
}

SceneExpect& SceneExpect::entityCount(std::size_t count) {
    EXPECT_EQ(scenario_.snapshot().entities.size(), count);
    return *this;
}

SceneExpect& SceneExpect::hasAlias(std::string_view alias) {
    EXPECT_TRUE(findEntityAlias(scenario_, alias).has_value()) << alias;
    return *this;
}

SceneExpect& SceneExpect::missingAlias(std::string_view alias) {
    EXPECT_FALSE(findEntityAlias(scenario_, alias).has_value()) << alias;
    return *this;
}

SceneExpect& SceneExpect::namedExists(std::string_view name) {
    EXPECT_TRUE(findEntityNamed(scenario_, name).has_value()) << name;
    return *this;
}

SceneExpect& SceneExpect::namedMissing(std::string_view name) {
    EXPECT_FALSE(findEntityNamed(scenario_, name).has_value()) << name;
    return *this;
}

SceneExpect& SceneExpect::aliasName(std::string_view alias, std::string_view name) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    EXPECT_EQ(snapshot->name, name);
    return *this;
}

SceneExpect& SceneExpect::aliasKind(std::string_view alias, QuaderEntityKind kind) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    EXPECT_EQ(snapshot->kind, kind);
    return *this;
}

SceneExpect& SceneExpect::parent(std::string_view alias, std::string_view parentAlias) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    EXPECT_EQ(snapshot->parent, scenario_.entity(parentAlias));
    return *this;
}

SceneExpect& SceneExpect::noParent(std::string_view alias) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    EXPECT_EQ(snapshot->parent, InvalidEntityId);
    return *this;
}

SceneExpect& SceneExpect::childCount(std::string_view alias, std::size_t count) {
    const EntityId parent = scenario_.entity(alias);
    std::size_t actual = 0;
    for (const QuaderEntitySnapshot& snapshot : scenario_.api().query().entities()) {
        if (snapshot.parent == parent) {
            ++actual;
        }
    }
    EXPECT_EQ(actual, count) << alias;
    return *this;
}

SceneExpect& SceneExpect::selectedAliases(std::initializer_list<std::string_view> aliases) {
    std::vector<EntityId> expected;
    expected.reserve(aliases.size());
    for (const std::string_view alias : aliases) {
        expected.push_back(scenario_.entity(alias));
    }
    std::sort(expected.begin(), expected.end());

    std::vector<EntityId> actual;
    for (const QuaderEntitySnapshot& snapshot : scenario_.api().query().entities()) {
        if (snapshot.selected) {
            actual.push_back(snapshot.id);
        }
    }
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);
    return *this;
}

SceneExpect& SceneExpect::activeAlias(std::string_view alias) {
    const EntityId expected = scenario_.entity(alias);
    bool found = false;
    for (const QuaderEntitySnapshot& snapshot : scenario_.api().query().entities()) {
        if (snapshot.active) {
            found = true;
            EXPECT_EQ(snapshot.id, expected);
        }
    }
    EXPECT_TRUE(found) << alias;
    return *this;
}

SceneExpect& SceneExpect::selectedAliasIs(std::string_view alias) {
    return activeAlias(alias);
}

SceneExpect& SceneExpect::visible(std::string_view alias, bool expected) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    EXPECT_EQ(snapshot->visible, expected);
    return *this;
}

SceneExpect& SceneExpect::locked(std::string_view alias, bool expected) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    EXPECT_EQ(snapshot->locked, expected);
    return *this;
}

SceneExpect& SceneExpect::hasMesh(std::string_view alias, AssetId mesh) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    EXPECT_TRUE(snapshot->hasMesh) << alias;
    EXPECT_EQ(snapshot->mesh, mesh) << alias;
    return *this;
}

SceneExpect& SceneExpect::hasMaterial(std::string_view alias, AssetId material) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    EXPECT_TRUE(snapshot->hasMaterial) << alias;
    EXPECT_EQ(snapshot->material, material) << alias;
    return *this;
}

SceneExpect& SceneExpect::worldPositionNear(std::string_view alias, QVec3 expected, float epsilon) {
    expectVec3Near(scenario_.worldPosition(scenario_.entity(alias)), expected, epsilon);
    return *this;
}

SceneExpect& SceneExpect::selectedBoundsCenterNear(QVec3 expected, float epsilon) {
    const BehaviorSnapshot current = scenario_.snapshot();
    EXPECT_TRUE(current.selectedBounds.has_value());
    if (current.selectedBounds.has_value()) {
        expectVec3Near(center(*current.selectedBounds), expected, epsilon);
    }
    return *this;
}

SceneExpect& SceneExpect::entityTransformNear(std::string_view alias, TransformComponent expected, float epsilon) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    expectTransformNear(snapshot->transform, expected, epsilon);
    return *this;
}

SceneExpect& SceneExpect::entityTransformNear(std::string_view alias, QuaderTransform expected, float epsilon) {
    const std::optional<QuaderEntitySnapshot> snapshot = findEntityAlias(scenario_, alias);
    EXPECT_TRUE(snapshot.has_value()) << alias;
    if (!snapshot.has_value()) {
        return *this;
    }
    expectTransformNear(snapshot->transform, expected, epsilon);
    return *this;
}

AssetExpect& AssetExpect::textureCount(std::size_t count) {
    EXPECT_EQ(scenario_.api().query().assets().textureCount, count);
    return *this;
}

SelectionExpect& SelectionExpect::mode(ModelingSelectionMode mode) {
    EXPECT_EQ(scenario_.snapshot().selection.mode, mode);
    return *this;
}

SelectionExpect& SelectionExpect::mode(QuaderSelectionMode mode) {
    return this->mode(modelingModeFromApi(mode));
}

SelectionExpect& SelectionExpect::objectCount(std::uint32_t count) {
    EXPECT_EQ(scenario_.snapshot().selection.selectedObjectCount, count);
    return *this;
}

SelectionExpect& SelectionExpect::vertexCount(std::uint32_t count) {
    EXPECT_EQ(scenario_.snapshot().selection.selectedVertexCount, count);
    return *this;
}

SelectionExpect& SelectionExpect::edgeCount(std::uint32_t count) {
    EXPECT_EQ(scenario_.snapshot().selection.selectedEdgeCount, count);
    return *this;
}

SelectionExpect& SelectionExpect::faceCount(std::uint32_t count) {
    EXPECT_EQ(scenario_.snapshot().selection.selectedFaceCount, count);
    return *this;
}

SelectionExpect& SelectionExpect::activeAlias(std::string_view alias) {
    EXPECT_EQ(scenario_.snapshot().selection.activeObjectId, scenario_.modelingObject(alias));
    return *this;
}

SelectionExpect& SelectionExpect::activeVertex(int vertex) {
    EXPECT_EQ(scenario_.snapshot().selection.activeVertexId, vertex);
    return *this;
}

SelectionExpect& SelectionExpect::activeEdge(int a, int b) {
    const ModelingSelectionSummary selection = scenario_.snapshot().selection;
    const bool sameOrder = selection.activeEdgeA == a && selection.activeEdgeB == b;
    const bool reversed = selection.activeEdgeA == b && selection.activeEdgeB == a;
    EXPECT_TRUE(sameOrder || reversed)
            << "active edge was (" << selection.activeEdgeA << ", " << selection.activeEdgeB << ")";
    return *this;
}

SelectionExpect& SelectionExpect::activeFace(int face) {
    EXPECT_EQ(scenario_.snapshot().selection.activeFaceId, face);
    return *this;
}

SelectionExpect& SelectionExpect::revisionChangedFrom(const ModelingSelectionSummary& before) {
    const ModelingSelectionSummary current = scenario_.snapshot().selection;
    EXPECT_TRUE(current.contentRevision != before.contentRevision ||
            current.selectionRevision != before.selectionRevision);
    return *this;
}

SelectionExpect& SelectionExpect::revisionSameAs(const ModelingSelectionSummary& before) {
    const ModelingSelectionSummary current = scenario_.snapshot().selection;
    EXPECT_EQ(current.contentRevision, before.contentRevision);
    EXPECT_EQ(current.selectionRevision, before.selectionRevision);
    return *this;
}

MeshExpect::MeshExpect(const BehaviorScenario& scenario, std::string_view alias) {
    const BehaviorSnapshot snapshot = scenario.snapshot();
    const std::optional<EntitySnapshot> entity = findEntitySnapshot(snapshot, alias);
    if (entity.has_value()) {
        mesh_ = findMeshSnapshot(snapshot, entity->mesh);
    }
    EXPECT_TRUE(mesh_.has_value()) << "Missing mesh snapshot for alias " << alias;
}

MeshExpect& MeshExpect::vertexCount(std::size_t count) {
    if (mesh_.has_value()) {
        EXPECT_EQ(mesh_->vertexCount, count);
    }
    return *this;
}

MeshExpect& MeshExpect::indexCount(std::size_t count) {
    if (mesh_.has_value()) {
        EXPECT_EQ(mesh_->indexCount, count);
    }
    return *this;
}

MeshExpect& MeshExpect::verticesGreaterThan(const MeshSnapshot& before) {
    if (mesh_.has_value()) {
        EXPECT_GT(mesh_->vertexCount, before.vertexCount);
    }
    return *this;
}

MeshExpect& MeshExpect::verticesDifferFrom(const MeshSnapshot& before, float epsilon) {
    if (mesh_.has_value()) {
        EXPECT_EQ(mesh_->positions.size(), before.positions.size());
        if (mesh_->positions.size() != before.positions.size()) {
            return *this;
        }
        bool differs = false;
        for (std::size_t index = 0; index < mesh_->positions.size(); ++index) {
            const QVec3 delta = mesh_->positions[index] - before.positions[index];
            if (std::abs(delta.x) > epsilon || std::abs(delta.y) > epsilon || std::abs(delta.z) > epsilon) {
                differs = true;
                break;
            }
        }
        EXPECT_TRUE(differs);
    }
    return *this;
}

MeshExpect& MeshExpect::boundsMinNear(QVec3 expected, float epsilon) {
    if (mesh_.has_value()) {
        expectVec3Near(mesh_->bounds.min, expected, epsilon);
    }
    return *this;
}

MeshExpect& MeshExpect::boundsMaxNear(QVec3 expected, float epsilon) {
    if (mesh_.has_value()) {
        expectVec3Near(mesh_->bounds.max, expected, epsilon);
    }
    return *this;
}

MeshExpect& MeshExpect::boundsCenterNear(QVec3 expected, float epsilon) {
    if (mesh_.has_value()) {
        expectVec3Near(center(mesh_->bounds), expected, epsilon);
    }
    return *this;
}

OverlayExpect& OverlayExpect::hasLine(ViewportOverlayLayer layer) {
    EXPECT_GT(countForLayer(scenario_.snapshot().overlay.linesByLayer, layer), 0u);
    return *this;
}

OverlayExpect& OverlayExpect::lineCount(ViewportOverlayLayer layer, std::size_t count) {
    EXPECT_EQ(countForLayer(scenario_.snapshot().overlay.linesByLayer, layer), count);
    return *this;
}

OverlayExpect& OverlayExpect::hasPoint(ViewportOverlayLayer layer) {
    EXPECT_GT(countForLayer(scenario_.snapshot().overlay.pointsByLayer, layer), 0u);
    return *this;
}

OverlayExpect& OverlayExpect::pointCount(ViewportOverlayLayer layer, std::size_t count) {
    EXPECT_EQ(countForLayer(scenario_.snapshot().overlay.pointsByLayer, layer), count);
    return *this;
}

OverlayExpect& OverlayExpect::hasTriangle(ViewportOverlayLayer layer) {
    EXPECT_GT(countForLayer(scenario_.snapshot().overlay.trianglesByLayer, layer), 0u);
    return *this;
}

OverlayExpect& OverlayExpect::triangleCount(ViewportOverlayLayer layer, std::size_t count) {
    EXPECT_EQ(countForLayer(scenario_.snapshot().overlay.trianglesByLayer, layer), count);
    return *this;
}

OverlayExpect& OverlayExpect::pointBoundsCenterNear(ViewportOverlayLayer layer, QVec3 expected, float epsilon) {
    const ViewportOverlayFrame frame = scenario_.selectionOverlayFrame();
    std::optional<BehaviorBounds> bounds;
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer != layer) {
            continue;
        }
        if (!bounds.has_value()) {
            bounds = BehaviorBounds{.min = point.position, .max = point.position};
        } else {
            includePoint(*bounds, point.position);
        }
    }
    EXPECT_TRUE(bounds.has_value());
    if (bounds.has_value()) {
        expectVec3Near(center(*bounds), expected, epsilon);
    }
    return *this;
}

OverlayExpect& OverlayExpect::lineBoundsWithin(ViewportOverlayLayer layer, QuaderBounds bounds, float epsilon) {
    const ViewportOverlayFrame frame = scenario_.selectionOverlayFrame();
    bool found = false;
    for (const ViewportOverlayLine& line : frame.lines) {
        if (line.layer != layer) {
            continue;
        }
        found = true;
        for (const QVec3 point : {line.start, line.end}) {
            EXPECT_GE(point.x, bounds.min.x - epsilon);
            EXPECT_GE(point.y, bounds.min.y - epsilon);
            EXPECT_GE(point.z, bounds.min.z - epsilon);
            EXPECT_LE(point.x, bounds.max.x + epsilon);
            EXPECT_LE(point.y, bounds.max.y + epsilon);
            EXPECT_LE(point.z, bounds.max.z + epsilon);
        }
    }
    EXPECT_TRUE(found);
    return *this;
}

OverlayExpect& OverlayExpect::triangleBoundsWithin(ViewportOverlayLayer layer, QuaderBounds bounds, float epsilon) {
    const ViewportOverlayFrame frame = scenario_.selectionOverlayFrame();
    bool found = false;
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        if (triangle.layer != layer) {
            continue;
        }
        found = true;
        for (const QVec3 point : {triangle.a, triangle.b, triangle.c}) {
            EXPECT_GE(point.x, bounds.min.x - epsilon);
            EXPECT_GE(point.y, bounds.min.y - epsilon);
            EXPECT_GE(point.z, bounds.min.z - epsilon);
            EXPECT_LE(point.x, bounds.max.x + epsilon);
            EXPECT_LE(point.y, bounds.max.y + epsilon);
            EXPECT_LE(point.z, bounds.max.z + epsilon);
        }
    }
    EXPECT_TRUE(found);
    return *this;
}

OverlayExpect& OverlayExpect::finiteCoordinates(float maxAbsCoordinate) {
    const ViewportOverlayFrame frame = scenario_.selectionOverlayFrame();
    auto expectFinite = [&](QVec3 point) {
        EXPECT_TRUE(std::isfinite(point.x));
        EXPECT_TRUE(std::isfinite(point.y));
        EXPECT_TRUE(std::isfinite(point.z));
        EXPECT_LE(std::abs(point.x), maxAbsCoordinate);
        EXPECT_LE(std::abs(point.y), maxAbsCoordinate);
        EXPECT_LE(std::abs(point.z), maxAbsCoordinate);
    };
    for (const ViewportOverlayLine& line : frame.lines) {
        expectFinite(line.start);
        expectFinite(line.end);
    }
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        expectFinite(triangle.a);
        expectFinite(triangle.b);
        expectFinite(triangle.c);
    }
    for (const ViewportOverlayPoint& point : frame.points) {
        expectFinite(point.position);
    }
    return *this;
}

ToolStatusExpect& ToolStatusExpect::activeTool(ModelingViewportTool tool) {
    EXPECT_EQ(scenario_.snapshot().toolStatus.activeTool, tool);
    return *this;
}

ToolStatusExpect& ToolStatusExpect::modalOwnsViewport(bool expected) {
    EXPECT_EQ(scenario_.snapshot().toolStatus.modalToolOwnsViewport, expected);
    return *this;
}

ToolStatusExpect& ToolStatusExpect::cutActive(bool expected) {
    EXPECT_EQ(scenario_.snapshot().toolStatus.cutActive, expected);
    return *this;
}

ToolStatusExpect& ToolStatusExpect::mirrorActive(bool expected) {
    EXPECT_EQ(scenario_.snapshot().toolStatus.mirrorActive, expected);
    return *this;
}

ToolStatusExpect& ToolStatusExpect::polyActive(bool expected) {
    EXPECT_EQ(scenario_.snapshot().toolStatus.polyActive, expected);
    return *this;
}

UndoRedoExpect& UndoRedoExpect::canUndo(bool expected) {
    EXPECT_EQ(scenario_.api().history().canUndo(), expected);
    return *this;
}

UndoRedoExpect& UndoRedoExpect::canRedo(bool expected) {
    EXPECT_EQ(scenario_.api().history().canRedo(), expected);
    return *this;
}

UndoRedoExpect& UndoRedoExpect::undoLabel(std::string_view expected) {
    EXPECT_EQ(scenario_.api().query().history().undoLabel, expected);
    return *this;
}

UndoRedoExpect& UndoRedoExpect::redoLabel(std::string_view expected) {
    EXPECT_EQ(scenario_.api().query().history().redoLabel, expected);
    return *this;
}

SnapshotExpect& SnapshotExpect::selectedBoundsCenterNear(QVec3 expected, float epsilon) {
    EXPECT_TRUE(snapshot_.selectedBounds.has_value()) << snapshot_.label;
    if (snapshot_.selectedBounds.has_value()) {
        expectVec3Near(center(*snapshot_.selectedBounds), expected, epsilon);
    }
    return *this;
}

SnapshotExpect& SnapshotExpect::entityCount(std::size_t count) {
    EXPECT_EQ(snapshot_.entities.size(), count) << snapshot_.label;
    return *this;
}

SnapshotExpect& SnapshotExpect::meshVertexCount(std::string_view alias, std::size_t count) {
    const auto entity = std::find_if(snapshot_.entities.begin(), snapshot_.entities.end(), [&](const EntitySnapshot& value) {
        return value.alias == alias;
    });
    EXPECT_NE(entity, snapshot_.entities.end()) << alias;
    if (entity == snapshot_.entities.end()) {
        return *this;
    }

    const std::optional<MeshSnapshot> mesh = findMeshSnapshot(snapshot_, entity->mesh);
    EXPECT_TRUE(mesh.has_value()) << alias;
    if (mesh.has_value()) {
        EXPECT_EQ(mesh->vertexCount, count) << alias;
    }
    return *this;
}

} // namespace quader::test
