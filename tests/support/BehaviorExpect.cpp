#include "BehaviorExpect.h"

#if QUADER_TEST_HAS_PROGRAMMATIC_API

#include "assets/AssetRegistry.h"
#include "scene/SceneBounds.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace quader::test {
namespace {

Vec3 center(const BehaviorBounds& bounds) {
    return (bounds.min + bounds.max) * 0.5f;
}

void expectVec3Near(Vec3 actual, Vec3 expected, float epsilon) {
    EXPECT_NEAR(actual.x, expected.x, epsilon);
    EXPECT_NEAR(actual.y, expected.y, epsilon);
    EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expectTransformNear(const TransformComponent& actual, const TransformComponent& expected, float epsilon) {
    expectVec3Near(actual.position, expected.position, epsilon);
    expectVec3Near(actual.rotationEuler, expected.rotationEuler, epsilon);
    expectVec3Near(actual.scale, expected.scale, epsilon);
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

void includePoint(BehaviorBounds& bounds, Vec3 point) {
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
    EXPECT_NE(scenario_.entity(alias), InvalidEntityId);
    return *this;
}

SceneExpect& SceneExpect::selectedAliasIs(std::string_view alias) {
    EXPECT_EQ(scenario_.session().scene().primarySelection(), scenario_.entity(alias));
    return *this;
}

SceneExpect& SceneExpect::selectedBoundsCenterNear(Vec3 expected, float epsilon) {
    const BehaviorSnapshot current = scenario_.snapshot();
    EXPECT_TRUE(current.selectedBounds.has_value());
    if (current.selectedBounds.has_value()) {
        expectVec3Near(center(*current.selectedBounds), expected, epsilon);
    }
    return *this;
}

SceneExpect& SceneExpect::entityTransformNear(std::string_view alias, TransformComponent expected, float epsilon) {
    const TransformComponent* transform = scenario_.session().scene().transform(scenario_.entity(alias));
    EXPECT_NE(transform, nullptr);
    if (transform != nullptr) {
        expectTransformNear(*transform, expected, epsilon);
    }
    return *this;
}

SelectionExpect& SelectionExpect::mode(ModelingSelectionMode mode) {
    EXPECT_EQ(scenario_.snapshot().selection.mode, mode);
    return *this;
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
    EXPECT_EQ(scenario_.snapshot().selection.activeObjectId, scenario_.sdkObject(alias));
    return *this;
}

SelectionExpect& SelectionExpect::activeVertex(int vertex) {
    EXPECT_EQ(scenario_.snapshot().selection.activeVertexId, vertex);
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
    mesh_ = findMeshSnapshot(snapshot, scenario.object(alias).mesh);
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

MeshExpect& MeshExpect::boundsCenterNear(Vec3 expected, float epsilon) {
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

OverlayExpect& OverlayExpect::pointBoundsCenterNear(ViewportOverlayLayer layer, Vec3 expected, float epsilon) {
    const ViewportOverlayFrame frame = scenario_.session().modeling().selectionOverlayFrame();
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
    EXPECT_EQ(scenario_.session().undoRedo().canUndo() || scenario_.session().modeling().canUndo(), expected);
    return *this;
}

UndoRedoExpect& UndoRedoExpect::canRedo(bool expected) {
    EXPECT_EQ(scenario_.session().undoRedo().canRedo() || scenario_.session().modeling().canRedo(), expected);
    return *this;
}

SnapshotExpect& SnapshotExpect::selectedBoundsCenterNear(Vec3 expected, float epsilon) {
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

#endif
