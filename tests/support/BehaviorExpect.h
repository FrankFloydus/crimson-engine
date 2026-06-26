////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "BehaviorScenario.h"

#if QUADER_TEST_HAS_PROGRAMMATIC_API

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace quader::test {

/**
 * Owns Command Expect behavior within the behavior test DSL.
 */
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

/**
 * Owns Scene Expect behavior within the behavior test DSL.
 */
class SceneExpect final {
public:
    explicit SceneExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    SceneExpect& entityCount(std::size_t count);
    SceneExpect& hasAlias(std::string_view alias);
    SceneExpect& missingAlias(std::string_view alias);
    SceneExpect& namedExists(std::string_view name);
    SceneExpect& namedMissing(std::string_view name);
    SceneExpect& aliasName(std::string_view alias, std::string_view name);
    SceneExpect& aliasKind(std::string_view alias, QuaderEntityKind kind);
    SceneExpect& parent(std::string_view alias, std::string_view parentAlias);
    SceneExpect& noParent(std::string_view alias);
    SceneExpect& childCount(std::string_view alias, std::size_t count);
    SceneExpect& selectedAliases(std::initializer_list<std::string_view> aliases);
    SceneExpect& activeAlias(std::string_view alias);
    SceneExpect& selectedAliasIs(std::string_view alias);
    SceneExpect& visible(std::string_view alias, bool expected);
    SceneExpect& locked(std::string_view alias, bool expected);
    SceneExpect& hasMesh(std::string_view alias, AssetId mesh);
    SceneExpect& hasMaterial(std::string_view alias, AssetId material);
    SceneExpect& worldPositionNear(std::string_view alias, QVec3 expected, float epsilon = 0.001f);
    SceneExpect& selectedBoundsCenterNear(QVec3 expected, float epsilon = 0.001f);
    SceneExpect& entityTransformNear(std::string_view alias, TransformComponent expected, float epsilon = 0.001f);
    SceneExpect& entityTransformNear(std::string_view alias, QuaderTransform expected, float epsilon = 0.001f);

private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Asset Expect behavior within the behavior test DSL.
 */
class AssetExpect final {
public:
    explicit AssetExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    AssetExpect& textureCount(std::size_t count);

private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Selection Expect behavior within the behavior test DSL.
 */
class SelectionExpect final {
public:
    explicit SelectionExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    SelectionExpect& mode(ModelingSelectionMode mode);
    SelectionExpect& mode(QuaderSelectionMode mode);
    SelectionExpect& objectCount(std::uint32_t count);
    SelectionExpect& vertexCount(std::uint32_t count);
    SelectionExpect& edgeCount(std::uint32_t count);
    SelectionExpect& faceCount(std::uint32_t count);
    SelectionExpect& activeAlias(std::string_view alias);
    SelectionExpect& activeVertex(int vertex);
    SelectionExpect& activeEdge(int a, int b);
    SelectionExpect& activeFace(int face);
    SelectionExpect& revisionChangedFrom(const ModelingSelectionSummary& before);
    SelectionExpect& revisionSameAs(const ModelingSelectionSummary& before);

private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Mesh Expect behavior within the behavior test DSL.
 */
class MeshExpect final {
public:
    MeshExpect(const BehaviorScenario& scenario, std::string_view alias);

    MeshExpect& vertexCount(std::size_t count);
    MeshExpect& indexCount(std::size_t count);
    MeshExpect& verticesGreaterThan(const MeshSnapshot& before);
    MeshExpect& verticesDifferFrom(const MeshSnapshot& before, float epsilon = 0.001f);
    MeshExpect& boundsMinNear(QVec3 expected, float epsilon = 0.001f);
    MeshExpect& boundsMaxNear(QVec3 expected, float epsilon = 0.001f);
    MeshExpect& boundsCenterNear(QVec3 expected, float epsilon = 0.001f);

private:
    std::optional<MeshSnapshot> mesh_;
};

/**
 * Owns Overlay Expect behavior within the behavior test DSL.
 */
class OverlayExpect final {
public:
    explicit OverlayExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    OverlayExpect& hasLine(ViewportOverlayLayer layer);
    OverlayExpect& lineCount(ViewportOverlayLayer layer, std::size_t count);
    OverlayExpect& hasPoint(ViewportOverlayLayer layer);
    OverlayExpect& pointCount(ViewportOverlayLayer layer, std::size_t count);
    OverlayExpect& hasTriangle(ViewportOverlayLayer layer);
    OverlayExpect& triangleCount(ViewportOverlayLayer layer, std::size_t count);
    OverlayExpect& pointBoundsCenterNear(ViewportOverlayLayer layer, QVec3 expected, float epsilon = 0.001f);
    OverlayExpect& lineBoundsWithin(ViewportOverlayLayer layer, QuaderBounds bounds, float epsilon = 0.001f);
    OverlayExpect& triangleBoundsWithin(ViewportOverlayLayer layer, QuaderBounds bounds, float epsilon = 0.001f);
    OverlayExpect& finiteCoordinates(float maxAbsCoordinate = 1000.0f);

private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Tool Status Expect behavior within the behavior test DSL.
 */
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

/**
 * Owns Undo Redo Expect behavior within the behavior test DSL.
 */
class UndoRedoExpect final {
public:
    explicit UndoRedoExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    UndoRedoExpect& canUndo(bool expected = true);
    UndoRedoExpect& canRedo(bool expected = true);
    UndoRedoExpect& undoLabel(std::string_view expected);
    UndoRedoExpect& redoLabel(std::string_view expected);

private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Snapshot Expect behavior within the behavior test DSL.
 */
class SnapshotExpect final {
public:
    explicit SnapshotExpect(const BehaviorSnapshot& snapshot) : snapshot_(snapshot) {}

    SnapshotExpect& selectedBoundsCenterNear(QVec3 expected, float epsilon = 0.001f);
    SnapshotExpect& entityCount(std::size_t count);
    SnapshotExpect& meshVertexCount(std::string_view alias, std::size_t count);

private:
    const BehaviorSnapshot& snapshot_;
};

} // namespace quader::test

#endif
