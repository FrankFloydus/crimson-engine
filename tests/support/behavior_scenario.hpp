////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "behavior_snapshot.hpp"
#include "modeling_test_events.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "scene/document/project_session.hpp"
#include "scene/components.hpp"

#include <mesh/polygon/document.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>

namespace quader::test {

class BehaviorGiven;
class BehaviorWhen;
class BehaviorThen;
class CommandExpect;
class MeshExpect;
class OverlayExpect;
class SceneExpect;
class SelectionExpect;
class SnapshotExpect;
class AssetExpect;
class ToolStatusExpect;
class UndoRedoExpect;

/**
 * Owns Behavior Scenario behavior within the behavior test DSL.
 */
class BehaviorScenario final {
public:
  static BehaviorScenario new_project();

  BehaviorScenario(const BehaviorScenario &) = delete;
  BehaviorScenario &operator=(const BehaviorScenario &) = delete;
  BehaviorScenario(BehaviorScenario &&) = delete;
  BehaviorScenario &operator=(BehaviorScenario &&) = delete;

  [[nodiscard]] BehaviorGiven given();
  [[nodiscard]] BehaviorWhen when();
  [[nodiscard]] BehaviorThen then();

  [[nodiscard]] QuaderEditorApi &editor() { return editor_; }
  [[nodiscard]] const QuaderEditorApi &editor() const { return editor_; }
  [[nodiscard]] QuaderApi &api() { return editor_.api(); }
  [[nodiscard]] const QuaderApi &api() const { return editor_.api(); }

  [[nodiscard]] EntityId entity(std::string_view alias) const;
  [[nodiscard]] QuaderModelingObjectRef object(std::string_view alias) const;
  [[nodiscard]] ModelingObjectId modeling_object(std::string_view alias) const;
  [[nodiscard]] BehaviorSnapshot snapshot(std::string label = {}) const;
  void remember(std::string_view alias, QuaderModelingObjectRef object);
  void remember(std::string_view alias, QuaderEntityRef entity);
  void remember(std::string_view alias, EntityId entity);
  void remember_entity_named(std::string_view alias, std::string_view name);

private:
    BehaviorScenario();

    friend class BehaviorGiven;
    friend class BehaviorWhen;
    friend class BehaviorThen;
    friend class SceneExpect;
    friend class SelectionExpect;
    friend class MeshExpect;
    friend class OverlayExpect;
    friend class AssetExpect;
    friend class ToolStatusExpect;
    friend class UndoRedoExpect;

    void record_status(const QuaderApiStatus &status);
    void
    record_selection(const QuaderApiResult<QuaderSelectionSummary> &selection);
    void record_drag(const QuaderTransformDragPreview &drag);
    void remember_selected_copy_alias();
    [[nodiscard]] QuaderModelingObjectRef
    object_for_entity(EntityId entity) const;
    [[nodiscard]] QVec3 world_position(EntityId entity) const;
    [[nodiscard]] ViewportOverlayFrame selection_overlay_frame() const;

    ProjectSession session_;
    QuaderEditorApi editor_;
    std::unordered_map<std::string, QuaderModelingObjectRef> aliases_;
    QuaderApiStatus last_status_;
    QuaderSelectionSummary last_selection_;
    QuaderTransformDragPreview last_drag_;
};

/**
 * Owns Behavior Given behavior within the behavior test DSL.
 */
class BehaviorGiven final {
public:
    explicit BehaviorGiven(BehaviorScenario& scenario) : scenario_(scenario) {}

    BehaviorGiven &unit_box(std::string_view alias, std::string name = "Box");
    BehaviorGiven& box(std::string_view alias, QuaderBounds bounds, std::string name = "Box");
    BehaviorGiven& mesh(std::string_view alias, QuaderPolygonDocument document, std::string name = "Mesh");
    BehaviorGiven &entity(std::string_view alias, std::string name,
                          QuaderEntityKind kind = QuaderEntityKind::Generic,
                          QuaderTransform transform = {},
                          std::string_view parent_alias = {});
    BehaviorGiven &
    selected_aliases(std::initializer_list<std::string_view> aliases,
                     std::string_view active_alias);
    BehaviorGiven& selected(std::string_view alias);
    BehaviorGiven &selection_mode(QuaderSelectionMode mode);
    BehaviorGiven &vertex_selected(std::string_view alias, int vertex);
    BehaviorGiven &edge_selected(std::string_view alias, int a, int b);
    BehaviorGiven &face_selected(std::string_view alias, int face);
    BehaviorGiven &all_components_selected(QuaderSelectionMode mode);
    BehaviorGiven& transform(std::string_view alias, QuaderTransform transform);
    BehaviorGiven& material(std::string_view alias, AssetId material);

private:
    BehaviorScenario& scenario_;
};

/**
 * Owns Behavior When behavior within the behavior test DSL.
 */
class BehaviorWhen final {
public:
    explicit BehaviorWhen(BehaviorScenario& scenario) : scenario_(scenario) {}

    BehaviorWhen& activate(ModelingViewportTool tool);
    BehaviorWhen &pick(QVec3 ray_origin, PickOptions options = {});
    BehaviorWhen &hover(QVec3 ray_origin, HoverOptions options = {});
    BehaviorWhen &hover_vertex(std::string_view alias, int vertex);
    BehaviorWhen &clear_hover();

    BehaviorWhen &begin_transform_preview(bool duplicate_selected = false);
    BehaviorWhen &translate_preview(QVec3 delta);
    BehaviorWhen &matrix_preview(const QuaderTransform3 &transform,
                                 QVec3 pivot);
    BehaviorWhen &commit_transform_preview();
    BehaviorWhen &cancel_transform_preview();

    BehaviorWhen &begin_transform_drag(DragBeginOptions options = {});
    BehaviorWhen &update_transform_drag(QVec2 screen_position);
    BehaviorWhen &update_transform_drag(float screen_x);
    BehaviorWhen &begin_x_move_drag(DragBeginOptions options = {});
    BehaviorWhen &update_x_move_drag(float screen_x);
    BehaviorWhen &commit_drag();
    BehaviorWhen &cancel_drag();

    BehaviorWhen &execute_tool(std::string_view tool_id);
    BehaviorWhen &dispatch_viewport_shortcut(const ViewportShortcut &shortcut);
    BehaviorWhen& execute(QuaderCommandId command);
    BehaviorWhen &execute(std::string_view command_id);
    BehaviorWhen &assign_material_slot(std::uint32_t material_slot);
    BehaviorWhen &clear_scene_selection();
    BehaviorWhen &group_selection(std::string_view alias, std::string name,
                                  QuaderTransform transform = {});
    BehaviorWhen &ungroup_selection();
    BehaviorWhen &duplicate_selected_hierarchy(std::string_view root_alias);
    BehaviorWhen& destroy(std::string_view alias);
    BehaviorWhen& rename(std::string_view alias, std::string name);
    BehaviorWhen &reparent(std::string_view alias,
                           std::string_view parent_alias,
                           bool preserve_world_transform = true);
    BehaviorWhen &set_transform(std::string_view alias,
                                QuaderTransform transform);
    BehaviorWhen &set_visible(std::string_view alias, bool visible);
    BehaviorWhen &set_locked(std::string_view alias, bool locked);
    BehaviorWhen &set_group_flags(std::string_view alias,
                                  QuaderTransform transform, bool visible,
                                  bool locked);
    BehaviorWhen& undo();
    BehaviorWhen& redo();

    [[nodiscard]] const QuaderApiStatus &last_status() const {
      return scenario_.last_status_;
    }
    [[nodiscard]] const QuaderSelectionSummary &last_selection() const {
      return scenario_.last_selection_;
    }
    [[nodiscard]] const QuaderTransformDragPreview &last_drag() const {
      return scenario_.last_drag_;
    }

private:
    BehaviorScenario& scenario_;
};

/**
 * Owns Behavior Then behavior within the behavior test DSL.
 */
class BehaviorThen final {
public:
    explicit BehaviorThen(const BehaviorScenario& scenario) : scenario_(scenario) {}

    [[nodiscard]] CommandExpect command() const;
    [[nodiscard]] SceneExpect scene() const;
    [[nodiscard]] SelectionExpect selection() const;
    [[nodiscard]] MeshExpect mesh(std::string_view alias) const;
    [[nodiscard]] OverlayExpect overlay() const;
    [[nodiscard]] AssetExpect assets() const;
    [[nodiscard]] ToolStatusExpect tool() const;
    [[nodiscard]] UndoRedoExpect history() const;
    [[nodiscard]] SnapshotExpect snapshot(const BehaviorSnapshot& snapshot) const;

private:
    const BehaviorScenario& scenario_;
};

} // namespace quader::test
