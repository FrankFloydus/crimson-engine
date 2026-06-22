#include "ui/ViewportShortcutController.h"

#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"

#include <optional>
#include <string>
#include <string_view>

namespace quader {
namespace {

ModelingMirrorAxis viewportMirrorAxisToModeling(ViewportMirrorAxis axis) {
    switch (axis) {
    case ViewportMirrorAxis::X:
        return ModelingMirrorAxis::X;
    case ViewportMirrorAxis::Y:
        return ModelingMirrorAxis::Y;
    case ViewportMirrorAxis::Z:
        return ModelingMirrorAxis::Z;
    }
    return ModelingMirrorAxis::X;
}

std::optional<QuaderApiVoidResult> executeViewportModelingShortcut(
        QuaderEditorApi& editor,
        const ViewportShortcut& shortcut) {
    switch (shortcut.kind) {
    case ViewportShortcutKind::DeleteSelection:
        return editor.commands().execute("modeling.delete_selection");
    case ViewportShortcutKind::SelectAll:
        return editor.commands().execute("selection.select_all");
    case ViewportShortcutKind::InvertSelection:
        return editor.commands().execute("selection.invert");
    case ViewportShortcutKind::Copy:
        return editor.commands().execute("modeling.copy_selection");
    case ViewportShortcutKind::Paste:
        return editor.commands().execute("modeling.paste_selection");
    case ViewportShortcutKind::Duplicate:
        return editor.commands().execute("modeling.duplicate_meshes");
    case ViewportShortcutKind::Undo:
        return editor.commands().execute("history.undo");
    case ViewportShortcutKind::Redo:
        return editor.commands().execute("history.redo");
    case ViewportShortcutKind::RepeatLastAction:
        return editor.commands().execute("history.repeat_last_action");
    case ViewportShortcutKind::MergeActive:
        return editor.commands().execute("modeling.merge_vertices_to_active");
    case ViewportShortcutKind::MergeCenter:
        return editor.commands().execute("modeling.merge_vertices_to_center");
    case ViewportShortcutKind::MirrorAxis:
        {
            std::string command = "modeling.";
            command.append(modelingMirrorSelectionToolId(viewportMirrorAxisToModeling(shortcut.mirrorAxis)));
            return editor.commands().execute(command);
        }
    case ViewportShortcutKind::FlipMeshNormals:
        return editor.commands().execute("modeling.invert_mesh");
    case ViewportShortcutKind::FlipHorizontal:
        return editor.commands().execute("modeling.flip_horizontal");
    case ViewportShortcutKind::FlipVertical:
        return editor.commands().execute("modeling.flip_vertical");
    case ViewportShortcutKind::ApplyMaterial:
        return editor.commands().execute("modeling.assign_material");
    case ViewportShortcutKind::None:
    case ViewportShortcutKind::ActivateTool:
    case ViewportShortcutKind::SetSelectionMode:
    case ViewportShortcutKind::FocusDocument:
    case ViewportShortcutKind::FocusOrigin:
    case ViewportShortcutKind::ToggleMaximized:
    case ViewportShortcutKind::SetGridPreset:
    case ViewportShortcutKind::CancelActiveTool:
    case ViewportShortcutKind::DeletePopup:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

ViewportShortcutControllerResult ViewportShortcutController::handleOperationPreviewShortcut(
        const ViewportInputResult& inputResult,
        const ModelingToolSettingsTarget& toolSettingsTarget,
        ViewportInputRouter& inputRouter,
        QuaderEditorApi& editor,
        ModelingSessionService& modeling) const {
    if (!inputResult.shortcutHandled || !toolSettingsTarget.operationPreviewActive) {
        return {};
    }

    if (inputResult.shortcut.kind == ViewportShortcutKind::ToggleMaximized) {
        inputRouter.setMaximized(!inputRouter.state().maximized);
        const QuaderApiVoidResult command = editor.modeling().commitPreview("Apply Tool Preview");
        if (!command.ok) {
            LOG_ERROR("Tool preview apply failed: {}", command.message.empty() ? "Command failed." : command.message);
        }
        return {
                .consumed = true,
                .commandExecuted = true,
                .invalidateHover = true,
                .markOverlayDirty = true,
        };
    }

    if (inputResult.shortcut.kind == ViewportShortcutKind::CancelActiveTool) {
        const QuaderApiVoidResult command = editor.modeling().cancelPreview();
        if (!command.ok) {
            LOG_ERROR("Tool preview cancel failed: {}", command.message.empty() ? "Command failed." : command.message);
        }
        return {
                .consumed = true,
                .commandExecuted = true,
                .invalidateHover = true,
                .markOverlayDirty = true,
        };
    }

    return {};
}

ViewportShortcutControllerResult ViewportShortcutController::handleModelingShortcut(
        const ViewportInputResult& inputResult,
        QuaderEditorApi& editor,
        ModelingSessionService& modeling) const {
    if (!inputResult.shortcutHandled) {
        return {};
    }

    const std::optional<QuaderApiVoidResult> command =
            executeViewportModelingShortcut(editor, inputResult.shortcut);
    if (!command) {
        return {};
    }

    if (!command->ok) {
        LOG_ERROR("Viewport shortcut failed: {}", command->message.empty() ? "Command failed." : command->message);
    }
    return {
            .commandExecuted = true,
            .invalidateHover = true,
            .markOverlayDirty = true,
    };
}

} // namespace quader
