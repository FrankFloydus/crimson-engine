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
    static BehaviorScenario newProject();

    BehaviorScenario(const BehaviorScenario&) = delete;
    BehaviorScenario& operator=(const BehaviorScenario&) = delete;
    BehaviorScenario(BehaviorScenario&&) = delete;
    BehaviorScenario& operator=(BehaviorScenario&&) = delete;

    [[nodiscard]] BehaviorGiven given();
    [[nodiscard]] BehaviorWhen when();
    [[nodiscard]] BehaviorThen then();

    [[nodiscard]] QuaderEditorApi& editor() { return editor_; }
    [[nodiscard]] const QuaderEditorApi& editor() const { return editor_; }
    [[nodiscard]] QuaderApi& api() { return editor_.api(); }
    [[nodiscard]] const QuaderApi& api() const { return editor_.api(); }

    [[nodiscard]] EntityId entity(std::string_view alias) const;
    [[nodiscard]] QuaderModelingObjectRef object(std::string_view alias) const;
    [[nodiscard]] ModelingObjectId modelingObject(std::string_view alias) const;
    [[nodiscard]] BehaviorSnapshot snapshot(std::string label = {}) const;
    void remember(std::string_view alias, QuaderModelingObjectRef object);
    void remember(std::string_view alias, QuaderEntityRef entity);
    void remember(std::string_view alias, EntityId entity);
    void rememberEntityNamed(std::string_view alias, std::string_view name);

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

    void recordStatus(const QuaderApiStatus& status);
    void recordSelection(const QuaderApiResult<QuaderSelectionSummary>& selection);
    void recordDrag(const QuaderTransformDragPreview& drag);
    void rememberSelectedCopyAlias();
    [[nodiscard]] QuaderModelingObjectRef objectForEntity(EntityId entity) const;
    [[nodiscard]] QVec3 worldPosition(EntityId entity) const;
    [[nodiscard]] ViewportOverlayFrame selectionOverlayFrame() const;

    ProjectSession session_;
    QuaderEditorApi editor_;
    std::unordered_map<std::string, QuaderModelingObjectRef> aliases_;
    QuaderApiStatus lastStatus_;
    QuaderSelectionSummary lastSelection_;
    QuaderTransformDragPreview lastDrag_;
};

/**
 * Owns Behavior Given behavior within the behavior test DSL.
 */
class BehaviorGiven final {
public:
    explicit BehaviorGiven(BehaviorScenario& scenario) : scenario_(scenario) {}

    BehaviorGiven& unitBox(std::string_view alias, std::string name = "Box");
    BehaviorGiven& box(std::string_view alias, QuaderBounds bounds, std::string name = "Box");
    BehaviorGiven& mesh(std::string_view alias, QuaderPolygonDocument document, std::string name = "Mesh");
    BehaviorGiven& entity(
            std::string_view alias,
            std::string name,
            QuaderEntityKind kind = QuaderEntityKind::Generic,
            QuaderTransform transform = {},
            std::string_view parentAlias = {});
    BehaviorGiven& selectedAliases(std::initializer_list<std::string_view> aliases, std::string_view activeAlias);
    BehaviorGiven& selected(std::string_view alias);
    BehaviorGiven& selectionMode(QuaderSelectionMode mode);
    BehaviorGiven& vertexSelected(std::string_view alias, int vertex);
    BehaviorGiven& edgeSelected(std::string_view alias, int a, int b);
    BehaviorGiven& faceSelected(std::string_view alias, int face);
    BehaviorGiven& allComponentsSelected(QuaderSelectionMode mode);
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
    BehaviorWhen& pick(QVec3 rayOrigin, PickOptions options = {});
    BehaviorWhen& hover(QVec3 rayOrigin, HoverOptions options = {});
    BehaviorWhen& hoverVertex(std::string_view alias, int vertex);
    BehaviorWhen& clearHover();

    BehaviorWhen& beginTransformPreview(bool duplicateSelected = false);
    BehaviorWhen& translatePreview(QVec3 delta);
    BehaviorWhen& matrixPreview(const QuaderTransform3& transform, QVec3 pivot);
    BehaviorWhen& commitTransformPreview();
    BehaviorWhen& cancelTransformPreview();

    BehaviorWhen& beginTransformDrag(DragBeginOptions options = {});
    BehaviorWhen& updateTransformDrag(QVec2 screenPosition);
    BehaviorWhen& updateTransformDrag(float screenX);
    BehaviorWhen& beginXMoveDrag(DragBeginOptions options = {});
    BehaviorWhen& updateXMoveDrag(float screenX);
    BehaviorWhen& commitDrag();
    BehaviorWhen& cancelDrag();

    BehaviorWhen& executeTool(std::string_view toolId);
    BehaviorWhen& dispatchViewportShortcut(const ViewportShortcut& shortcut);
    BehaviorWhen& execute(QuaderCommandId command);
    BehaviorWhen& execute(std::string_view commandId);
    BehaviorWhen& assignMaterialSlot(std::uint32_t materialSlot);
    BehaviorWhen& clearSceneSelection();
    BehaviorWhen& groupSelection(std::string_view alias, std::string name, QuaderTransform transform = {});
    BehaviorWhen& ungroupSelection();
    BehaviorWhen& duplicateSelectedHierarchy(std::string_view rootAlias);
    BehaviorWhen& destroy(std::string_view alias);
    BehaviorWhen& rename(std::string_view alias, std::string name);
    BehaviorWhen& reparent(std::string_view alias, std::string_view parentAlias, bool preserveWorldTransform = true);
    BehaviorWhen& setTransform(std::string_view alias, QuaderTransform transform);
    BehaviorWhen& setVisible(std::string_view alias, bool visible);
    BehaviorWhen& setLocked(std::string_view alias, bool locked);
    BehaviorWhen& setGroupFlags(std::string_view alias, QuaderTransform transform, bool visible, bool locked);
    BehaviorWhen& undo();
    BehaviorWhen& redo();

    [[nodiscard]] const QuaderApiStatus& lastStatus() const { return scenario_.lastStatus_; }
    [[nodiscard]] const QuaderSelectionSummary& lastSelection() const { return scenario_.lastSelection_; }
    [[nodiscard]] const QuaderTransformDragPreview& lastDrag() const { return scenario_.lastDrag_; }

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
