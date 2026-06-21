#pragma once

#include "BehaviorScenario.h"

#if QUADER_TEST_HAS_PROGRAMMATIC_API

#include <cstddef>
#include <optional>
#include <string_view>

namespace quader::test {

class CommandExpect final {
public:
    explicit CommandExpect(const QuaderApiStatus& status) : status_(status) {}

    CommandExpect& ok();
    CommandExpect& okChanged();
    CommandExpect& unchanged();
    CommandExpect& failed();
    CommandExpect& messageContains(std::string_view text);

private:
    const QuaderApiStatus& status_;
};

class SceneExpect final {
public:
    explicit SceneExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    SceneExpect& entityCount(std::size_t count);
    SceneExpect& hasAlias(std::string_view alias);
    SceneExpect& selectedAliasIs(std::string_view alias);
    SceneExpect& selectedBoundsCenterNear(Vec3 expected, float epsilon = 0.001f);
    SceneExpect& entityTransformNear(std::string_view alias, TransformComponent expected, float epsilon = 0.001f);

private:
    const BehaviorScenario& scenario_;
};

class SelectionExpect final {
public:
    explicit SelectionExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    SelectionExpect& mode(ModelingSelectionMode mode);
    SelectionExpect& objectCount(std::uint32_t count);
    SelectionExpect& vertexCount(std::uint32_t count);
    SelectionExpect& edgeCount(std::uint32_t count);
    SelectionExpect& faceCount(std::uint32_t count);
    SelectionExpect& activeAlias(std::string_view alias);
    SelectionExpect& activeVertex(int vertex);
    SelectionExpect& activeFace(int face);
    SelectionExpect& revisionChangedFrom(const ModelingSelectionSummary& before);
    SelectionExpect& revisionSameAs(const ModelingSelectionSummary& before);

private:
    const BehaviorScenario& scenario_;
};

class MeshExpect final {
public:
    MeshExpect(const BehaviorScenario& scenario, std::string_view alias);

    MeshExpect& vertexCount(std::size_t count);
    MeshExpect& indexCount(std::size_t count);
    MeshExpect& verticesGreaterThan(const MeshSnapshot& before);
    MeshExpect& boundsCenterNear(Vec3 expected, float epsilon = 0.001f);

private:
    std::optional<MeshSnapshot> mesh_;
};

class OverlayExpect final {
public:
    explicit OverlayExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    OverlayExpect& hasLine(ViewportOverlayLayer layer);
    OverlayExpect& lineCount(ViewportOverlayLayer layer, std::size_t count);
    OverlayExpect& hasPoint(ViewportOverlayLayer layer);
    OverlayExpect& pointCount(ViewportOverlayLayer layer, std::size_t count);
    OverlayExpect& hasTriangle(ViewportOverlayLayer layer);
    OverlayExpect& triangleCount(ViewportOverlayLayer layer, std::size_t count);
    OverlayExpect& pointBoundsCenterNear(ViewportOverlayLayer layer, Vec3 expected, float epsilon = 0.001f);

private:
    const BehaviorScenario& scenario_;
};

class ToolStatusExpect final {
public:
    explicit ToolStatusExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    ToolStatusExpect& activeTool(ModelingViewportTool tool);
    ToolStatusExpect& modalOwnsViewport(bool expected = true);
    ToolStatusExpect& cutActive(bool expected = true);
    ToolStatusExpect& mirrorActive(bool expected = true);
    ToolStatusExpect& polyActive(bool expected = true);

private:
    const BehaviorScenario& scenario_;
};

class UndoRedoExpect final {
public:
    explicit UndoRedoExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    UndoRedoExpect& canUndo(bool expected = true);
    UndoRedoExpect& canRedo(bool expected = true);

private:
    const BehaviorScenario& scenario_;
};

class SnapshotExpect final {
public:
    explicit SnapshotExpect(const BehaviorSnapshot& snapshot) : snapshot_(snapshot) {}

    SnapshotExpect& selectedBoundsCenterNear(Vec3 expected, float epsilon = 0.001f);
    SnapshotExpect& entityCount(std::size_t count);
    SnapshotExpect& meshVertexCount(std::string_view alias, std::size_t count);

private:
    const BehaviorSnapshot& snapshot_;
};

} // namespace quader::test

#endif
