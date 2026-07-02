////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "behavior_scenario.hpp"

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
    CommandExpect &ok_changed();
    CommandExpect& unchanged();
    CommandExpect& failed();
    CommandExpect &message_contains(std::string_view text);

  private:
    const QuaderApiStatus& status_;
};

/**
 * Owns Scene Expect behavior within the behavior test DSL.
 */
class SceneExpect final {
public:
    explicit SceneExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    SceneExpect &entity_count(std::size_t count);
    SceneExpect &has_alias(std::string_view alias);
    SceneExpect &missing_alias(std::string_view alias);
    SceneExpect &named_exists(std::string_view name);
    SceneExpect &named_missing(std::string_view name);
    SceneExpect &alias_name(std::string_view alias, std::string_view name);
    SceneExpect &alias_kind(std::string_view alias, QuaderEntityKind kind);
    SceneExpect &parent(std::string_view alias, std::string_view parent_alias);
    SceneExpect &no_parent(std::string_view alias);
    SceneExpect &child_count(std::string_view alias, std::size_t count);
    SceneExpect &
    selected_aliases(std::initializer_list<std::string_view> aliases);
    SceneExpect &active_alias(std::string_view alias);
    SceneExpect &selected_alias_is(std::string_view alias);
    SceneExpect& visible(std::string_view alias, bool expected);
    SceneExpect& locked(std::string_view alias, bool expected);
    SceneExpect &has_mesh(std::string_view alias, AssetId mesh);
    SceneExpect &has_material(std::string_view alias, AssetId material);
    SceneExpect &world_position_near(std::string_view alias, QVec3 expected,
                                     float epsilon = 0.001f);
    SceneExpect &selected_bounds_center_near(QVec3 expected,
                                             float epsilon = 0.001f);
    SceneExpect &entity_transform_near(std::string_view alias,
                                       TransformComponent expected,
                                       float epsilon = 0.001f);
    SceneExpect &entity_transform_near(std::string_view alias,
                                       QuaderTransform expected,
                                       float epsilon = 0.001f);

  private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Asset Expect behavior within the behavior test DSL.
 */
class AssetExpect final {
public:
    explicit AssetExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    AssetExpect &texture_count(std::size_t count);

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
    SelectionExpect &object_count(std::uint32_t count);
    SelectionExpect &vertex_count(std::uint32_t count);
    SelectionExpect &edge_count(std::uint32_t count);
    SelectionExpect &face_count(std::uint32_t count);
    SelectionExpect &active_alias(std::string_view alias);
    SelectionExpect &active_vertex(int vertex);
    SelectionExpect &active_edge(int a, int b);
    SelectionExpect &active_face(int face);
    SelectionExpect &
    revision_changed_from(const ModelingSelectionSummary &before);
    SelectionExpect &revision_same_as(const ModelingSelectionSummary &before);

  private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Mesh Expect behavior within the behavior test DSL.
 */
class MeshExpect final {
public:
    MeshExpect(const BehaviorScenario& scenario, std::string_view alias);

    MeshExpect &vertex_count(std::size_t count);
    MeshExpect &index_count(std::size_t count);
    MeshExpect &vertices_greater_than(const MeshSnapshot &before);
    MeshExpect &vertices_differ_from(const MeshSnapshot &before,
                                     float epsilon = 0.001f);
    MeshExpect &bounds_min_near(QVec3 expected, float epsilon = 0.001f);
    MeshExpect &bounds_max_near(QVec3 expected, float epsilon = 0.001f);
    MeshExpect &bounds_center_near(QVec3 expected, float epsilon = 0.001f);

  private:
    std::optional<MeshSnapshot> mesh_;
};

/**
 * Owns Overlay Expect behavior within the behavior test DSL.
 */
class OverlayExpect final {
public:
    explicit OverlayExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    OverlayExpect &has_line(ViewportOverlayLayer layer);
    OverlayExpect &line_count(ViewportOverlayLayer layer, std::size_t count);
    OverlayExpect &has_point(ViewportOverlayLayer layer);
    OverlayExpect &point_count(ViewportOverlayLayer layer, std::size_t count);
    OverlayExpect &has_triangle(ViewportOverlayLayer layer);
    OverlayExpect &triangle_count(ViewportOverlayLayer layer,
                                  std::size_t count);
    OverlayExpect &point_bounds_center_near(ViewportOverlayLayer layer,
                                            QVec3 expected,
                                            float epsilon = 0.001f);
    OverlayExpect &line_bounds_within(ViewportOverlayLayer layer,
                                      QuaderBounds bounds,
                                      float epsilon = 0.001f);
    OverlayExpect &triangle_bounds_within(ViewportOverlayLayer layer,
                                          QuaderBounds bounds,
                                          float epsilon = 0.001f);
    OverlayExpect &finite_coordinates(float max_abs_coordinate = 1000.0f);

  private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Tool Status Expect behavior within the behavior test DSL.
 */
class ToolStatusExpect final {
public:
    explicit ToolStatusExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    ToolStatusExpect &active_tool(ModelingViewportTool tool);
    ToolStatusExpect &modal_owns_viewport(bool expected = true);
    ToolStatusExpect &cut_active(bool expected = true);
    ToolStatusExpect &mirror_active(bool expected = true);
    ToolStatusExpect &poly_active(bool expected = true);

  private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Undo Redo Expect behavior within the behavior test DSL.
 */
class UndoRedoExpect final {
public:
    explicit UndoRedoExpect(const BehaviorScenario& scenario) : scenario_(scenario) {}

    UndoRedoExpect &can_undo(bool expected = true);
    UndoRedoExpect &can_redo(bool expected = true);
    UndoRedoExpect &undo_label(std::string_view expected);
    UndoRedoExpect &redo_label(std::string_view expected);

  private:
    const BehaviorScenario& scenario_;
};

/**
 * Owns Snapshot Expect behavior within the behavior test DSL.
 */
class SnapshotExpect final {
public:
    explicit SnapshotExpect(const BehaviorSnapshot& snapshot) : snapshot_(snapshot) {}

    SnapshotExpect &selected_bounds_center_near(QVec3 expected,
                                                float epsilon = 0.001f);
    SnapshotExpect &entity_count(std::size_t count);
    SnapshotExpect &mesh_vertex_count(std::string_view alias,
                                      std::size_t count);

  private:
    const BehaviorSnapshot& snapshot_;
};

} // namespace quader::test
