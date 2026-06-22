#include "api/QuaderCommandsApi.h"

#include "api/QuaderApi.h"
#include "api/QuaderToolsApi.h"
#include "api/QuaderViewportApi.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"
#include "viewport/tools/ViewportToolCatalog.h"

#include <array>
#include <string>

namespace quader {
namespace {

bool consumePrefix(std::string_view& value, std::string_view prefix) {
    if (!value.starts_with(prefix)) {
        return false;
    }
    value.remove_prefix(prefix.size());
    return true;
}

QuaderCommandDescriptor descriptor(std::string id, std::string label, std::string shortcut = {}, bool enabled = true) {
    return {
            .id = std::move(id),
            .label = std::move(label),
            .shortcut = std::move(shortcut),
            .enabled = enabled,
    };
}

} // namespace

QuaderCommandsApi::QuaderCommandsApi(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools)
    : api_(api)
    , viewports_(viewports)
    , tools_(tools) {}

QuaderApiVoidResult QuaderCommandsApi::execute(QuaderCommandId commandId) {
    return execute(commandId.string());
}

QuaderApiVoidResult QuaderCommandsApi::execute(std::string_view commandId) {
    if (commandId == "history.undo") {
        return api_.history().undo();
    }
    if (commandId == "history.redo") {
        return api_.history().redo();
    }
    if (commandId == "history.repeat_last_action") {
        return api_.history().repeatLastAction();
    }
    if (commandId == "selection.clear") {
        return api_.selection().clear();
    }
    if (commandId == "selection.select_all") {
        return api_.selection().selectAll();
    }
    if (commandId == "selection.invert") {
        return api_.selection().invert();
    }
    if (commandId == "viewport.frame_selection") {
        return viewports_.frameSelection();
    }
    if (commandId == "viewport.focus_origin") {
        return viewports_.focusOrigin();
    }

    std::string_view routed = commandId;
    if (consumePrefix(routed, "modeling.action.")) {
        return api_.modeling().executeAction(routed);
    }
    routed = commandId;
    if (consumePrefix(routed, "modeling.")) {
        return api_.modeling().executeTool(routed);
    }
    routed = commandId;
    if (consumePrefix(routed, "tools.activate.")) {
        return tools_.activate(routed);
    }
    return failure("Unknown command id.");
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::available() const {
    std::vector<QuaderCommandDescriptor> result{
            descriptor("history.undo", "Undo", shortcutFor("history.undo"), api_.history().canUndo()),
            descriptor("history.redo", "Redo", shortcutFor("history.redo"), api_.history().canRedo()),
            descriptor("history.repeat_last_action", "Repeat Last Action", shortcutFor("history.repeat_last_action")),
            descriptor("selection.clear", "Clear Selection"),
            descriptor("selection.select_all", "Select All", shortcutFor("selection.select_all")),
            descriptor("selection.invert", "Invert Selection", shortcutFor("selection.invert")),
            descriptor("viewport.frame_selection", "Frame Selection", "Home"),
            descriptor("viewport.focus_origin", "Focus Origin", "Shift+Home"),
    };

    for (const CommandTool* tool : modelingCommandTools()) {
        if (tool == nullptr) {
            continue;
        }
        std::string id = "modeling.";
        id.append(tool->id());
        result.push_back(descriptor(std::move(id), std::string(tool->label())));
    }
    for (const ViewportTool* tool : viewportToolCatalog()) {
        if (tool == nullptr) {
            continue;
        }
        std::string id = "tools.activate.";
        id.append(tool->id());
        result.push_back(descriptor(
                std::move(id),
                std::string(tool->label()),
                std::string(tool->shortcut()),
                viewportToolAvailable(tool->viewportTool())));
    }
    return result;
}

std::string QuaderCommandsApi::shortcutFor(std::string_view commandId) const {
    if (commandId == "history.undo") {
        return "Ctrl+Z";
    }
    if (commandId == "history.redo") {
        return "Ctrl+Y";
    }
    if (commandId == "history.repeat_last_action") {
        return "Ctrl+R";
    }
    if (commandId == "selection.select_all") {
        return "Ctrl+A";
    }
    if (commandId == "selection.invert") {
        return "Ctrl+I";
    }
    return {};
}

std::string QuaderCommandsApi::shortcutFor(QuaderCommandId commandId) const {
    return shortcutFor(commandId.string());
}

QuaderApiVoidResult QuaderCommandsApi::failure(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

} // namespace quader
