#pragma once

#include "assets/AssetRegistry.h"
#include "core/UndoRedoManager.h"
#include "modeling/ModelingSessionService.h"
#include "scene/SceneDocument.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportSettings.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace quader {

enum class ToolKind {
    Selection,
    Transform,
    Modal,
};

enum class ToolMenuGroup {
    Navigation,
    Modeling,
};

enum class ViewportToolEventKind {
    Activation,
    Frame,
    Cancel,
};

struct ViewportPointerToolEvent {
    bool hovered = false;
    bool leftClicked = false;
    bool leftDown = false;
    bool leftReleased = false;
    bool canInteract = false;
    bool finalizePressed = false;
    bool modeCyclePressed = false;
    bool suppressHoverWork = false;
    bool gizmoConsumed = false;
    bool navigationActive = false;
    bool textInputActive = false;
    Vec2 mouseLocal = {};
    Vec2 leftClickLocal = {};
    Vec2 viewportSize = {};
    ViewportModifiers modifiers = {};
};

struct ViewportToolEvent {
    ViewportToolEventKind kind = ViewportToolEventKind::Activation;
    ViewportToolId tool = ViewportToolId::Select;
    ViewportShortcut shortcut = {};
    bool fromShortcut = false;
    bool exitActiveModalTool = false;
    ViewportPointerToolEvent pointer = {};
};

struct ViewportModalToolState {
    ModelingBoxPreview boxPreview = {};
    bool insertEdgeLoopDragActive = false;
    bool cutDragActive = false;
    bool mirrorDragActive = false;
};

struct ViewportToolResult {
    bool handled = false;
    bool consumed = false;
    bool modalStateChanged = false;
    bool activeToolChanged = false;
    bool selectionModeChanged = false;
};

struct ViewportToolContext {
    ViewportToolState& state;
    ViewportCameraController& camera;
    ModelingSessionService& modeling;
    UndoRedoManager* undoRedo = nullptr;
    SceneDocument* document = nullptr;
    AssetId defaultMaterial = InvalidAssetId;
    const ViewportSettings* settings = nullptr;
    ViewportModalToolState* modalState = nullptr;

    std::function<ModelingBoxToolEvent()> makeBoxEvent;
    std::function<ModelingToolRayEvent(Vec2, bool)> makeToolEventAt;
    std::function<bool(ModelingBoxDragState)> claimHover;
    std::function<void()> invalidateHover;
    std::function<void()> markOverlayDirty;
    std::function<void(ViewportToolId)> setActiveTool;
    std::function<void(ViewportSelectionMode)> setSelectionMode;
    std::function<std::string()> nextMeshName;
    std::function<void(std::string_view, const ModelingCommandResult&)> handleToolCommandResult;
    std::function<std::optional<int>(const std::array<Vec3, 3>&, int)> hitDrawnPlaneCapturedPoint;
    std::function<bool(const std::array<Vec3, 3>&, int, bool)> hitDrawnPlaneLine;
};

struct ToolDescriptor {
    ViewportToolId tool = ViewportToolId::Select;
    ModelingViewportTool modelingTool = ModelingViewportTool::Select;
    std::string_view id = "select";
    std::string_view label = "Select";
    std::string_view shortcut;
    ToolKind kind = ToolKind::Selection;
    ToolMenuGroup menuGroup = ToolMenuGroup::Navigation;
    bool ownsViewport = false;
    bool blocksViewportFocusShortcuts = false;
};

class Tool {
public:
    explicit Tool(ToolDescriptor descriptor);
    virtual ~Tool();

    Tool(const Tool&) = delete;
    Tool& operator=(const Tool&) = delete;
    Tool(Tool&&) = delete;
    Tool& operator=(Tool&&) = delete;

    [[nodiscard]] const ToolDescriptor& descriptor() const;
    [[nodiscard]] ViewportToolId viewportTool() const;
    [[nodiscard]] ModelingViewportTool modelingTool() const;
    [[nodiscard]] std::string_view id() const;
    [[nodiscard]] std::string_view label() const;
    [[nodiscard]] std::string_view shortcut() const;
    [[nodiscard]] ToolKind kind() const;
    [[nodiscard]] ToolMenuGroup menuGroup() const;
    [[nodiscard]] virtual bool available() const;
    [[nodiscard]] virtual bool ownsViewport() const;
    [[nodiscard]] virtual bool blocksViewportFocusShortcuts() const;
    [[nodiscard]] virtual ModelingTransformTool transformTool() const;

    virtual ModelingCommandResult activate(ModelingSessionService& modeling) const;

private:
    ToolDescriptor descriptor_;
};

class ViewportTool : public Tool {
public:
    static constexpr ViewportToolId Select = ViewportToolId::Select;
    static constexpr ViewportToolId Move = ViewportToolId::Move;
    static constexpr ViewportToolId Extend = ViewportToolId::Extend;
    static constexpr ViewportToolId Extrude = ViewportToolId::Extrude;
    static constexpr ViewportToolId Rotate = ViewportToolId::Rotate;
    static constexpr ViewportToolId Scale = ViewportToolId::Scale;
    static constexpr ViewportToolId Box = ViewportToolId::Box;
    static constexpr ViewportToolId InsertEdgeLoop = ViewportToolId::InsertEdgeLoop;
    static constexpr ViewportToolId Knife = ViewportToolId::Knife;
    static constexpr ViewportToolId Cut = ViewportToolId::Cut;
    static constexpr ViewportToolId Poly = ViewportToolId::Poly;
    static constexpr ViewportToolId Mirror = ViewportToolId::Mirror;

    explicit ViewportTool(ToolDescriptor descriptor);
    ViewportTool(ViewportToolId tool);

    [[nodiscard]] operator ViewportToolId() const;
    [[nodiscard]] virtual bool allowsSelectionFallback() const;
    [[nodiscard]] virtual ViewportToolResult handleEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event) const;

protected:
    [[nodiscard]] ViewportToolResult activateById(
            ViewportToolContext& context,
            ViewportToolId tool) const;
};

} // namespace quader
