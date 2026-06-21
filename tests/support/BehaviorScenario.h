#pragma once

#include "BehaviorSnapshot.h"
#include "ModelingTestEvents.h"

#if QUADER_TEST_HAS_PROGRAMMATIC_API
#include "document/ProjectSession.h"
#include "scene/Components.h"

#include <quader_poly/document.h>

#include <string>
#include <string_view>
#include <unordered_map>
#endif

namespace quader::test {

#if QUADER_TEST_HAS_PROGRAMMATIC_API

class BehaviorGiven;
class BehaviorWhen;
class BehaviorThen;
class CommandExpect;
class MeshExpect;
class OverlayExpect;
class SceneExpect;
class SelectionExpect;
class SnapshotExpect;
class ToolStatusExpect;
class UndoRedoExpect;

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

    [[nodiscard]] ProjectSession& session() { return session_; }
    [[nodiscard]] const ProjectSession& session() const { return session_; }
    [[nodiscard]] QuaderApi& api() { return api_; }
    [[nodiscard]] const QuaderApi& api() const { return api_; }

    [[nodiscard]] EntityId entity(std::string_view alias) const;
    [[nodiscard]] QuaderModelingObjectRef object(std::string_view alias) const;
    [[nodiscard]] ModelingObjectId sdkObject(std::string_view alias) const;
    [[nodiscard]] BehaviorSnapshot snapshot(std::string label = {}) const;
    void remember(std::string_view alias, QuaderModelingObjectRef object);

private:
    BehaviorScenario();

    friend class BehaviorGiven;
    friend class BehaviorWhen;
    friend class BehaviorThen;
    friend class SceneExpect;
    friend class SelectionExpect;
    friend class MeshExpect;
    friend class OverlayExpect;
    friend class ToolStatusExpect;
    friend class UndoRedoExpect;

    void recordStatus(const QuaderApiStatus& status);
    void recordSelection(const QuaderApiResult<ModelingSelectionSummary>& selection);
    void recordDrag(const ModelingTransformDragPreview& drag);
    void rememberSelectedCopyAlias();
    [[nodiscard]] QuaderModelingObjectRef objectForEntity(EntityId entity) const;

    ProjectSession session_;
    QuaderApi api_;
    std::unordered_map<std::string, QuaderModelingObjectRef> aliases_;
    QuaderApiStatus lastStatus_;
    ModelingSelectionSummary lastSelection_;
    ModelingTransformDragPreview lastDrag_;
};

class BehaviorGiven final {
public:
    explicit BehaviorGiven(BehaviorScenario& scenario) : scenario_(scenario) {}

    BehaviorGiven& unitBox(std::string_view alias, std::string name = "Box");
    BehaviorGiven& box(std::string_view alias, ModelingBounds bounds, std::string name = "Box");
    BehaviorGiven& mesh(std::string_view alias, quader_poly::Document document, std::string name = "Mesh");
    BehaviorGiven& selected(std::string_view alias);
    BehaviorGiven& selectionMode(ModelingSelectionMode mode);
    BehaviorGiven& vertexSelected(std::string_view alias, int vertex);
    BehaviorGiven& edgeSelected(std::string_view alias, int a, int b);
    BehaviorGiven& faceSelected(std::string_view alias, int face);
    BehaviorGiven& allComponentsSelected(ModelingSelectionMode mode);
    BehaviorGiven& transform(std::string_view alias, TransformComponent transform);
    BehaviorGiven& material(std::string_view alias, AssetId material);

private:
    BehaviorScenario& scenario_;
};

class BehaviorWhen final {
public:
    explicit BehaviorWhen(BehaviorScenario& scenario) : scenario_(scenario) {}

    BehaviorWhen& activate(ModelingViewportTool tool);
    BehaviorWhen& pick(Vec3 rayOrigin, PickOptions options = {});
    BehaviorWhen& hover(Vec3 rayOrigin, HoverOptions options = {});
    BehaviorWhen& hoverVertex(std::string_view alias, int vertex);
    BehaviorWhen& clearHover();

    BehaviorWhen& beginTransformPreview(bool duplicateSelected = false);
    BehaviorWhen& translatePreview(Vec3 delta);
    BehaviorWhen& matrixPreview(const ModelingTransform3& transform, Vec3 pivot);
    BehaviorWhen& commitTransformPreview();
    BehaviorWhen& cancelTransformPreview();

    BehaviorWhen& beginXMoveDrag(DragBeginOptions options = {});
    BehaviorWhen& updateXMoveDrag(float screenX);
    BehaviorWhen& commitDrag();
    BehaviorWhen& cancelDrag();

    BehaviorWhen& executeTool(std::string_view toolId);
    BehaviorWhen& executeAction(std::string_view actionId);
    BehaviorWhen& assignMaterialSlot(std::uint32_t materialSlot);
    BehaviorWhen& undo();
    BehaviorWhen& redo();

    [[nodiscard]] const QuaderApiStatus& lastStatus() const { return scenario_.lastStatus_; }
    [[nodiscard]] const ModelingSelectionSummary& lastSelection() const { return scenario_.lastSelection_; }
    [[nodiscard]] const ModelingTransformDragPreview& lastDrag() const { return scenario_.lastDrag_; }

private:
    BehaviorScenario& scenario_;
};

class BehaviorThen final {
public:
    explicit BehaviorThen(const BehaviorScenario& scenario) : scenario_(scenario) {}

    [[nodiscard]] CommandExpect command() const;
    [[nodiscard]] SceneExpect scene() const;
    [[nodiscard]] SelectionExpect selection() const;
    [[nodiscard]] MeshExpect mesh(std::string_view alias) const;
    [[nodiscard]] OverlayExpect overlay() const;
    [[nodiscard]] ToolStatusExpect tool() const;
    [[nodiscard]] UndoRedoExpect history() const;
    [[nodiscard]] SnapshotExpect snapshot(const BehaviorSnapshot& snapshot) const;

private:
    const BehaviorScenario& scenario_;
};

#else

class BehaviorScenario final {
public:
    static BehaviorScenario newProject() { return {}; }
    [[nodiscard]] bool programmaticApiAvailable() const { return false; }
};

#endif

} // namespace quader::test
