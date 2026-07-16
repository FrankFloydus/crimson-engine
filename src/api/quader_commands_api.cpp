////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_commands_api.hpp"

#include "api/quader_api.hpp"
#include "api/quader_mesh_arrangement_catalog.hpp"
#ifndef NDEBUG
#include "api/quader_dev_tools_api.hpp"
#endif
#include "api/quader_project_api.hpp"
#include "api/quader_scene_documents_api.hpp"
#include "api/quader_preferences_api.hpp"
#include "api/quader_tools_api.hpp"
#include "api/quader_viewport_api.hpp"
#include "core/public_command_targets.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/editor_command_registry.hpp"
#include "editor/commands/modeling_command_tool_catalog.hpp"
#include "editor/input/quader_keybinding_service.hpp"
#include "editor/input/viewport_input_binding_adapter.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_input_types.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

std::string command_id_for_modeling_action(std::string_view action_id) {
  if (const CommandTool *tool = find_modeling_command_tool(action_id)) {
    return tool->public_command_target_id();
  }

  return quader_modeling_command_target_id(action_id);
}

bool modeling_action_registers_dedicated_command(const CommandTool &tool) {
  return tool.registers_public_command();
}

bool duplicate_selection_available(const QuaderApi &api) {
  QDR_PROFILE_FUNCTION();
  return api.modeling().can_duplicate_selected_entity_hierarchy();
}

std::string command_id_for_tool_activation(std::string_view tool_id) {
  return quader_tool_activation_command_target_id(tool_id);
}

std::filesystem::path default_keybindings_path() {
  if (const char *local_app_data = std::getenv("LOCALAPPDATA")) {
    return std::filesystem::path(local_app_data) / "Quader" /
           "keybindings.json";
  }
  return std::filesystem::path("keybindings.json");
}

constexpr std::string_view global_command_keybinding_context() {
  return "!textInputActive";
}

constexpr std::string_view undo_command_keybinding_context() {
  return "!textInputActive && canUndo";
}

constexpr std::string_view redo_command_keybinding_context() {
  return "!textInputActive && canRedo";
}

constexpr std::string_view viewport_command_keybinding_context() {
  return "viewportFocus && !textInputActive && !navigation.active";
}

std::string_view
default_keybinding_context_for_command(std::string_view command_id) {
  if (command_id == quader_command::kEditUndo.string()) {
    return undo_command_keybinding_context();
  }
  if (command_id == quader_command::kEditRedo.string()) {
    return redo_command_keybinding_context();
  }
  ViewportShortcut shortcut;
  if (viewport_shortcut_for_command_target_id(command_id, shortcut) &&
      shortcut.context == ViewportShortcutContext::Viewport) {
    return viewport_command_keybinding_context();
  }
  return global_command_keybinding_context();
}

int default_keybinding_weight_for_command(std::string_view command_id) {
#ifndef NDEBUG
  if (command_id == quader_command::kDevCopySelectionJson.string()) {
    return 2000;
  }
#else
  (void)command_id;
#endif
  return 0;
}

std::string_view arrangement_axis_label(QuaderTransformAxis axis) {
  switch (axis) {
  case QuaderTransformAxis::X:
    return "X";
  case QuaderTransformAxis::Y:
    return "Y";
  case QuaderTransformAxis::Z:
    return "Z";
  case QuaderTransformAxis::None:
  case QuaderTransformAxis::XY:
  case QuaderTransformAxis::XZ:
  case QuaderTransformAxis::YZ:
  case QuaderTransformAxis::All:
    return "";
  }
  return "";
}

std::string arrangement_command_label(
    const QuaderMeshArrangementDescriptor &descriptor) {
  std::string label;
  if (descriptor.kind == QuaderMeshArrangementKind::Alignment) {
    label = "Align ";
    label.append(arrangement_axis_label(descriptor.axis));
    label.push_back(' ');
    switch (descriptor.alignment) {
    case QuaderMeshAlignment::Minimum:
      label.append("Min");
      break;
    case QuaderMeshAlignment::Center:
      label.append("Center");
      break;
    case QuaderMeshAlignment::Maximum:
      label.append("Max");
      break;
    }
    return label;
  }
  label = descriptor.distribution == QuaderMeshDistribution::Centers
              ? "Distribute Centers "
              : "Distribute Gaps ";
  label.append(arrangement_axis_label(descriptor.axis));
  return label;
}

std::string_view
default_keybinding_context_for_contract_target(std::string_view target_id) {
  const std::optional<std::string> command_target =
      viewport_command_target_id_for_contract_target(target_id);
  if (command_target) {
    const std::string_view command_target_view(command_target->data(),
                                               command_target->size());
    if (command_target_view == quader_command::kEditUndo.string()) {
      return undo_command_keybinding_context();
    }
    if (command_target_view == quader_command::kEditRedo.string()) {
      return redo_command_keybinding_context();
    }
  }
  ViewportShortcut shortcut;
  if (viewport_shortcut_from_contract_target(target_id, shortcut) &&
      shortcut.context == ViewportShortcutContext::Global) {
    return global_command_keybinding_context();
  }
  return viewport_command_keybinding_context();
}

QuaderKeybindingContext
viewport_keybinding_context(const ViewportToolState &state,
                            bool viewport_focused, bool text_input_active,
                            QuaderViewportKeybindingContext input_context,
                            bool can_undo, bool can_redo) {
  QuaderKeybindingContext context;
  context.set("viewportFocus", viewport_focused);
  context.set("viewport.hovered", input_context.viewport_hovered);
  context.set("textInputActive", text_input_active);
  context.set("editorTextFocus", text_input_active);
  context.set("canUndo", can_undo);
  context.set("canRedo", can_redo);
  context.set("navigation.active", input_context.navigation_active);
  context.set("navigation.flyActive", input_context.fly_navigation_active);
  context.set("navigation.orbitActive", input_context.orbit_navigation_active);
  context.set("navigation.panActive", input_context.pan_navigation_active);
  context.set("modalTool.active", input_context.modal_tool_active);
  context.set("activeTool.ownsViewport",
              input_context.active_tool_owns_viewport);
  context.set("operationPreview.active",
              input_context.operation_preview_active);
  context.set("activeTool." + std::string(viewport_tool_id(state.active_tool)),
              true);
  context.set("selectionMode." +
                  std::string(viewport_selection_mode_id(state.selection_mode)),
              true);
  return context;
}

} // namespace

/**
 * Owns command registration, dispatch, and keybinding adaptation for QuaderCommandsApi.
 */
class QuaderCommandsApi::Impl final {
public:
#ifndef NDEBUG
    Impl(QuaderApi& api, QuaderProjectApi& project,
         QuaderSceneDocumentsApi &scene_documents,
         QuaderViewportApi& viewports, QuaderToolsApi& tools,
         QuaderPreferencesApi& preferences, QuaderDevToolsApi& dev_tools);
#else
    Impl(QuaderApi& api, QuaderProjectApi& project,
         QuaderSceneDocumentsApi &scene_documents,
         QuaderViewportApi& viewports, QuaderToolsApi& tools,
         QuaderPreferencesApi& preferences);
#endif

    QuaderApiVoidResult execute(std::string_view command_id);
    QuaderApiVoidResult execute(QuaderCommandId command_id);
    [[nodiscard]] std::optional<QuaderApiStatus> last_status() const;
    [[nodiscard]] std::optional<QuaderCommandDescriptor>
    descriptor(std::string_view command_id) const;
    [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
    [[nodiscard]] std::string
    command_id_for_modeling_action(std::string_view action_id) const;
    [[nodiscard]] std::string
    command_id_for_tool_activation(std::string_view tool_id) const;
    [[nodiscard]] std::string shortcut_for(std::string_view command_id) const;
    [[nodiscard]] std::string shortcut_for(QuaderCommandId command_id) const;
    [[nodiscard]] std::vector<QuaderKeybindingRule> keybindings() const;
    [[nodiscard]] std::vector<QuaderKeybindingConflict>
    conflicts_for_shortcut(std::string_view shortcut,
                           std::string_view when = {}) const;
    [[nodiscard]] bool resolve_viewport_shortcut(
        ViewportKey key, ViewportModifiers modifiers,
        const ViewportToolState &state, bool viewport_focused,
        bool text_input_active, ViewportShortcut &shortcut,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] bool resolve_viewport_shortcut(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        ViewportShortcut &shortcut,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] bool resolve_viewport_input_action(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        ViewportInputActionKind &action,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch dispatch_viewport_keybinding(
        ViewportKey key, ViewportModifiers modifiers,
        const ViewportToolState &state, bool viewport_focused,
        bool text_input_active,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch
    dispatch_viewport_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        QuaderViewportKeybindingContext input_context = {});
    void reset_pending_keybinding_chord();
    [[nodiscard]] bool has_pending_keybinding_chord() const;
    QuaderApiVoidResult set_shortcut(std::string_view command_id,
                                     std::string shortcut);
    QuaderApiVoidResult set_shortcut(QuaderCommandId command_id,
                                     std::string shortcut);
    QuaderApiVoidResult remove_shortcut(std::string_view command_id);
    QuaderApiVoidResult remove_shortcut(QuaderCommandId command_id);
    QuaderApiVoidResult reset_shortcut(std::string_view command_id);
    QuaderApiVoidResult reset_shortcut(QuaderCommandId command_id);
    QuaderApiVoidResult set_keybinding_storage_path(std::filesystem::path path);

  private:
    void register_commands();
    void register_default_keybindings();
    void register_viewport_default_keybindings();
    void register_viewport_view_command(QuaderCommandId id, std::string label,
                                        ViewportView view,
                                        std::string shortcut = {});
    void register_command(QuaderCommandId id, std::string label,
                          std::function<QuaderApiVoidResult()> execute,
                          std::string shortcut = {},
                          std::function<bool()> enabled = {},
                          bool visible = true);
    void register_command(std::string id, std::string label,
                          std::function<QuaderApiVoidResult()> execute,
                          std::string shortcut = {},
                          std::function<bool()> enabled = {},
                          bool visible = true);
    [[nodiscard]] QuaderApiVoidResult failure(std::string message) const;

    QuaderApi& api_;
    QuaderProjectApi& project_;
    QuaderSceneDocumentsApi &scene_documents_;
    QuaderViewportApi& viewports_;
    QuaderToolsApi& tools_;
    QuaderPreferencesApi& preferences_;
#ifndef NDEBUG
    QuaderDevToolsApi& dev_tools_;
#endif
    EditorCommandRegistry registry_;
    QuaderKeybindingService keybindings_;
    std::optional<QuaderApiStatus> last_status_;
};

QuaderCommandsApi::Impl::Impl(
    QuaderApi &api, QuaderProjectApi &project,
    QuaderSceneDocumentsApi &scene_documents, QuaderViewportApi &viewports,
    QuaderToolsApi &tools, QuaderPreferencesApi &preferences
#ifndef NDEBUG
    ,
    QuaderDevToolsApi &dev_tools
#endif
    )
    : api_(api)
    , project_(project)
    , scene_documents_(scene_documents)
    , viewports_(viewports)
    , tools_(tools)
    , preferences_(preferences)
#ifndef NDEBUG
    , dev_tools_(dev_tools)
#endif
{
  keybindings_.set_storage_path(default_keybindings_path());
  register_commands();
  register_default_keybindings();
  (void)keybindings_.load_user_keybindings();
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::execute(QuaderCommandId command_id) {
  QDR_PROFILE_FUNCTION();
  const std::string_view id = command_id.string();
  QDR_PROFILE_NAME(id.data(), id.size());
  return execute(command_id.string());
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::execute(std::string_view command_id) {
  QDR_PROFILE_FUNCTION();
  QDR_PROFILE_NAME(command_id.data(), command_id.size());
  QDR_PROFILE_MESSAGE_DYNAMIC(command_id.data(), command_id.size());
  if (std::optional<QuaderApiVoidResult> result =
          registry_.execute(command_id)) {
    result->revisions = api_.revisions();
    last_status_ = *result;
    return *result;
  }
  QuaderApiVoidResult result = failure("Unknown command id.");
  last_status_ = result;
  return result;
}

std::optional<QuaderApiStatus>
QuaderCommandsApi::Impl::last_status() const {
  return last_status_;
}

std::optional<QuaderCommandDescriptor>
QuaderCommandsApi::Impl::descriptor(std::string_view command_id) const {
  std::optional<QuaderCommandDescriptor> command =
      registry_.descriptor(command_id);
  if (command.has_value()) {
    command->shortcut = keybindings_.shortcut_for(command->id);
  }
  return command;
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::Impl::available() const {
    std::vector<QuaderCommandDescriptor> commands = registry_.available();
    for (QuaderCommandDescriptor& command : commands) {
      command.shortcut = keybindings_.shortcut_for(command.id);
    }
    return commands;
}

std::string QuaderCommandsApi::Impl::command_id_for_modeling_action(
    std::string_view action_id) const {
  return quader::command_id_for_modeling_action(action_id);
}

std::string QuaderCommandsApi::Impl::command_id_for_tool_activation(
    std::string_view tool_id) const {
  return quader::command_id_for_tool_activation(tool_id);
}

std::string
QuaderCommandsApi::Impl::shortcut_for(std::string_view command_id) const {
  return keybindings_.shortcut_for(std::string(command_id));
}

std::string
QuaderCommandsApi::Impl::shortcut_for(QuaderCommandId command_id) const {
  return shortcut_for(command_id.string());
}

std::vector<QuaderKeybindingRule> QuaderCommandsApi::Impl::keybindings() const {
    return keybindings_.keybindings();
}

std::vector<QuaderKeybindingConflict>
QuaderCommandsApi::Impl::conflicts_for_shortcut(std::string_view shortcut,
                                                std::string_view when) const {
  return keybindings_.conflicts_for(shortcut, when);
}

bool QuaderCommandsApi::Impl::resolve_viewport_shortcut(
    ViewportKey key, ViewportModifiers modifiers,
    const ViewportToolState &state, bool viewport_focused,
    bool text_input_active, ViewportShortcut &shortcut,
    QuaderViewportKeybindingContext input_context) {
  const std::optional<QuaderInputChord> chord =
      quader_input_chord_for_viewport_key(key, modifiers);
  return chord ? resolve_viewport_shortcut(*chord, state, viewport_focused,
                                           text_input_active, shortcut,
                                           input_context)
               : false;
}

bool QuaderCommandsApi::Impl::resolve_viewport_shortcut(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active, ViewportShortcut &shortcut,
    QuaderViewportKeybindingContext input_context) {
  const QuaderViewportKeybindingDispatch dispatch =
      dispatch_viewport_input_binding(chord, state, viewport_focused,
                                      text_input_active, input_context);
  if (dispatch.kind == QuaderViewportKeybindingDispatch::Kind::PendingChord) {
    shortcut.kind = ViewportShortcutKind::None;
    return true;
  }
  if (dispatch.kind !=
      QuaderViewportKeybindingDispatch::Kind::ViewportShortcut) {
    return false;
  }
  shortcut = dispatch.shortcut;
  return true;
}

bool QuaderCommandsApi::Impl::resolve_viewport_input_action(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active,
    ViewportInputActionKind &action,
    QuaderViewportKeybindingContext input_context) {
  action = ViewportInputActionKind::None;
  if (chord.empty()) {
    return false;
  }

  const QuaderKeybindingContext context = viewport_keybinding_context(
      state, viewport_focused, text_input_active, input_context,
      api_.history().can_undo(), api_.history().can_redo());
  const QuaderKeybindingDispatch dispatch =
      keybindings_.resolve_sequence({chord}, context);
  return dispatch.kind == QuaderKeybindingDispatch::Kind::ExactMatch &&
         viewport_input_action_for_target_id(dispatch.target_id, action);
}

QuaderViewportKeybindingDispatch
QuaderCommandsApi::Impl::dispatch_viewport_keybinding(
    ViewportKey key, ViewportModifiers modifiers,
    const ViewportToolState &state, bool viewport_focused,
    bool text_input_active, QuaderViewportKeybindingContext input_context) {
  const std::optional<QuaderInputChord> chord =
      quader_input_chord_for_viewport_key(key, modifiers);
  return chord
             ? dispatch_viewport_input_binding(*chord, state, viewport_focused,
                                               text_input_active, input_context)
             : QuaderViewportKeybindingDispatch{};
}

QuaderViewportKeybindingDispatch
QuaderCommandsApi::Impl::dispatch_viewport_input_binding(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active,
    QuaderViewportKeybindingContext input_context) {
  if (chord.empty()) {
    keybindings_.reset_pending_chord();
    return {};
  }

  const QuaderKeybindingContext context = viewport_keybinding_context(
      state, viewport_focused, text_input_active, input_context,
      api_.history().can_undo(), api_.history().can_redo());
  const QuaderKeybindingDispatch dispatch =
      keybindings_.dispatch(chord, context);
  if (dispatch.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::PendingChord,
    };
  }
  if (dispatch.kind != QuaderKeybindingDispatch::Kind::ExactMatch) {
    return {};
  }

  if (registry_.contains(dispatch.target_id) &&
      !viewport_target_requires_router_dispatch(dispatch.target_id)) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::Command,
        .command_id = dispatch.target_id,
    };
  }

  ViewportShortcut shortcut;
  if (viewport_router_shortcut_for_target_id(dispatch.target_id, shortcut)) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::ViewportShortcut,
        .shortcut = shortcut,
    };
  }

  if (registry_.contains(dispatch.target_id)) {
    return {
        .kind = QuaderViewportKeybindingDispatch::Kind::Command,
        .command_id = dispatch.target_id,
    };
  }

  return {};
}

void QuaderCommandsApi::Impl::reset_pending_keybinding_chord() {
  keybindings_.reset_pending_chord();
}

bool QuaderCommandsApi::Impl::has_pending_keybinding_chord() const {
  return keybindings_.has_pending_chord();
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::set_shortcut(std::string_view command_id,
                                      std::string shortcut) {
  const std::string target_id(command_id);
  std::string when;
  for (const QuaderKeybindingRule &rule : keybindings_.keybindings()) {
    if (rule.target_id == target_id) {
      when = rule.when;
      break;
    }
  }
  if (when.empty()) {
    when = std::string(default_keybinding_context_for_command(target_id));
  }
  const QuaderKeybindingMutationResult mutation =
      keybindings_.set_user_shortcut(target_id, std::move(shortcut), when);
  if (!mutation.ok) {
    return failure(mutation.message);
  }
  return {
      .ok = true,
      .changed = mutation.changed,
      .message = mutation.message,
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::set_shortcut(QuaderCommandId command_id,
                                      std::string shortcut) {
  return set_shortcut(command_id.string(), std::move(shortcut));
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::remove_shortcut(std::string_view command_id) {
  const std::string target_id(command_id);
  const QuaderKeybindingMutationResult mutation =
      keybindings_.remove_user_shortcut(target_id);
  if (!mutation.ok) {
    return failure(mutation.message);
  }
  return {
      .ok = true,
      .changed = mutation.changed,
      .message = mutation.message,
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::remove_shortcut(QuaderCommandId command_id) {
  return remove_shortcut(command_id.string());
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::reset_shortcut(std::string_view command_id) {
  const std::string target_id(command_id);
  const QuaderKeybindingMutationResult mutation =
      keybindings_.reset_user_shortcut(target_id);
  if (!mutation.ok) {
    return failure(mutation.message);
  }
  return {
      .ok = true,
      .changed = mutation.changed,
      .message = mutation.message,
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

QuaderApiVoidResult
QuaderCommandsApi::Impl::reset_shortcut(QuaderCommandId command_id) {
  return reset_shortcut(command_id.string());
}

QuaderApiVoidResult QuaderCommandsApi::Impl::set_keybinding_storage_path(
    std::filesystem::path path) {
  keybindings_.set_storage_path(std::move(path));
  keybindings_.clear_user_keybindings();
  if (const std::optional<std::string> error =
          keybindings_.load_user_keybindings()) {
    return failure(*error);
  }
  return {
      .ok = true,
      .changed = false,
      .message = "Shortcut storage path updated.",
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

void QuaderCommandsApi::Impl::register_commands() {
  auto register_modeling_tool_public_command =
      [this](QuaderCommandId command_id, std::string_view tool_id,
             std::string fallback_label, std::string shortcut = {}) {
        const CommandTool *tool = find_modeling_command_tool(tool_id);
        const std::string label = tool != nullptr && !tool->label().empty()
                                      ? std::string(tool->label())
                                      : std::move(fallback_label);
        register_command(
            command_id, label,
            [this, target_tool_id = std::string(tool_id)] {
              return api_.modeling().execute_tool(target_tool_id);
            },
            std::move(shortcut),
            [this, target_tool_id = std::string(tool_id)] {
              return api_.modeling().can_execute_tool(target_tool_id);
            });
      };

  register_command(
      quader_command::kEditUndo, "Undo",
      [this] { return api_.history().undo(); }, "Ctrl+Z",
      [this] { return api_.history().can_undo(); });
  register_command(
      quader_command::kEditRedo, "Redo",
      [this] { return api_.history().redo(); }, "Ctrl+Y",
      [this] { return api_.history().can_redo(); });
  register_command(
      quader_command::kEditRepeatLastAction, "Repeat Last Action",
      [this] { return api_.history().repeat_last_action(); }, "Ctrl+R");
  register_command(quader_command::kEditClearSelection, "Clear Selection",
                   [this] { return api_.selection().clear(); });
  register_command(
      quader_command::kEditSelectAll, "Select All",
      [this] { return api_.selection().select_all(); }, "Ctrl+A");
  register_command(
      quader_command::kEditInvertSelection, "Invert Selection",
      [this] { return api_.selection().invert(); }, "Ctrl+I");
  register_modeling_tool_public_command(quader_command::kDeleteSelection,
                                        "delete_selection", "Delete Selection");
  register_modeling_tool_public_command(quader_command::kCopySelection,
                                        "copy_selection", "Copy Selection",
                                        "Ctrl+C");
  register_modeling_tool_public_command(quader_command::kPasteSelection,
                                        "paste_selection", "Paste Selection",
                                        "Ctrl+V");
  register_command(
      quader_command::kDuplicateSelection, "Duplicate Selection",
      [this] { return api_.modeling().duplicate_selected_entity_hierarchy(); },
      "Ctrl+D", [this] { return duplicate_selection_available(api_); });
  register_command(
      quader_command::kEditCreateInstance, "Create Instance",
      [this] { return api_.modeling().create_instance(); }, {},
      [this] { return api_.modeling().can_create_instance(); });
  register_command(
      quader_command::kEditFlattenInstance, "Flatten Instance",
      [this] { return api_.modeling().flatten_instance(); }, {},
      [this] { return api_.modeling().can_flatten_instance(); });
  register_command(
      quader_command::kEditExitInstanceEdit, "Exit Instance Edit",
      [this] { return api_.modeling().exit_instance_edit(); }, {},
      [this] { return api_.modeling().can_exit_instance_edit(); });
  register_command(
      quader_command::kGroupSelection, "Group Selection",
      [this] {
        const QuaderApiResult<QuaderEntityRef> result =
            api_.scene().group_selection();
        return quader_api_status_from_result(result);
      },
      "Ctrl+G");
  register_command(
      quader_command::kUngroupSelection, "Ungroup Selection",
      [this] { return api_.scene().ungroup_selection(); }, "Shift+Ctrl+G");
  register_modeling_tool_public_command(quader_command::kApplyMaterial,
                                        "assign_material",
                                        "Apply Material");
  register_command(
      quader_command::kModelingCopyPivotFromActive, "Copy Pivot From Active",
      [this] { return api_.modeling().copy_pivot_from_active(); }, {},
      [this] { return api_.modeling().can_copy_pivot_from_active(); });
#ifndef NDEBUG
  register_command(quader_command::kDevCopySelectionJson,
                   "Dev: Copy Selection JSON",
                   [this] {
                     return dev_tools_.copy_selection_json_to_clipboard();
                   },
                   "Ctrl+Shift+C");
#endif
  register_command(
      quader_command::kModelingApplyActivePreview, "Apply Active Preview",
      [this] { return api_.modeling().commit_preview("Apply Tool Preview"); });
  register_command(quader_command::kModelingCancelActivePreview,
                   "Cancel Active Preview",
                   [this] { return api_.modeling().cancel_preview(); });
  register_command(quader_command::kToolsApplyPrimitiveCreation,
                   "Apply Primitive Creation",
                   [this] {
                     return tools_.has_active_primitive_creation()
                                ? tools_.commit_active_tool_preview()
                                : failure(
                                      "No primitive creation session is active.");
                   },
                   {}, [this] {
                     return tools_.has_active_primitive_creation();
                   });
  register_command(quader_command::kToolsCancelPrimitiveCreation,
                   "Cancel Primitive Creation",
                   [this] {
                     return tools_.has_active_primitive_creation()
                                ? tools_.cancel_active_tool_preview()
                                : failure(
                                      "No primitive creation session is active.");
                   },
                   {}, [this] {
                     return tools_.has_active_primitive_creation();
                   });
  register_command(
      quader_command::kProjectNew, "New Project",
      [this] { return project_.new_project(); });
  register_command(
      quader_command::kProjectOpen, "Open Project",
      [this] { return project_.open({}); });
  register_command(
      quader_command::kProjectClose, "Close Project",
      [this] { return project_.close(); }, {},
      [this] { return project_.has_project(); });
  register_command(
      quader_command::kSceneDocumentNew, "New Scene",
      [this] {
        return quader_api_status_from_result(scene_documents_.new_scene());
      },
      "Ctrl+N", [this] { return project_.has_project(); });
  register_command(
      quader_command::kSceneDocumentOpen, "Open Scene",
      [this] {
        return quader_api_status_from_result(scene_documents_.open());
      },
      "Ctrl+O", [this] { return project_.has_project(); });
  register_command(
      quader_command::kSceneDocumentSave, "Save Scene",
      [this] { return scene_documents_.save_active(); }, "Ctrl+S",
      [this] { return scene_documents_.has_active_scene(); });
  register_command(
      quader_command::kSceneDocumentSaveAs, "Save Scene As",
      [this] {
        const std::optional<SceneSessionId> id =
            scene_documents_.active_scene_id();
        return id.has_value()
                   ? scene_documents_.save_as(*id)
                   : failure("No scene is active.");
      },
      "Ctrl+Alt+S", [this] { return scene_documents_.has_active_scene(); });
  register_command(
      quader_command::kSceneDocumentSaveAll, "Save All Scenes",
      [this] { return scene_documents_.save_all(); }, "Ctrl+Shift+S",
      [this] { return scene_documents_.has_dirty_scenes(); });
  register_command(
      quader_command::kSceneDocumentClose, "Close Scene",
      [this] { return scene_documents_.close_active(); }, "Ctrl+W",
      [this] { return scene_documents_.has_active_scene(); });
  register_command(quader_command::kSceneDocumentNext, "Next Scene",
                   [this] { return scene_documents_.activate_next(); },
                   "Ctrl+Tab",
                   [this] { return scene_documents_.open_scenes().size() > 1u; });
  register_command(
      quader_command::kSceneDocumentPrevious, "Previous Scene",
      [this] { return scene_documents_.activate_previous(); },
      "Ctrl+Shift+Tab",
      [this] { return scene_documents_.open_scenes().size() > 1u; });
  register_command(
      quader_command::kProjectExportSelectionObj, "Export Selection OBJ",
      [this] {
        return quader_api_status_from_result(
            project_.export_selected_meshes_as_obj({}));
      },
      {}, [this] { return project_.can_export_selected_meshes_as_obj(); });
  register_command(quader_command::kSceneCreatePointLight, "Create Point Light",
                   [this] {
                     QuaderCreatePointLightArgs args;
                     args.select_created = true;
                     const QuaderApiResult<QuaderEntityRef> result =
                         api_.scene().create_point_light(args);
                     return quader_api_status_from_result(result);
                   });
  register_command(quader_command::kSceneCreateDirectionalLight,
                   "Create Directional Light", [this] {
                     QuaderCreateDirectionalLightArgs args;
                     args.select_created = true;
                     const QuaderApiResult<QuaderEntityRef> result =
                         api_.scene().create_directional_light(args);
                     return quader_api_status_from_result(result);
                   });
  register_command(
      quader_command::kViewportFrameSelection, "Focus Selection",
      [this] { return viewports_.frame_selection(viewports_.focused_pane()); });
  register_command(quader_command::kViewportFocusAll, "Focus All",
                   [this] {
                     return viewports_.frame_all(viewports_.focused_pane());
                   },
                   "Home");
  register_command(
      quader_command::kViewportFocusOrigin, "Focus Origin",
      [this] { return viewports_.focus_origin(viewports_.focused_pane()); });
  register_command(
      quader_command::kViewportSpawnWindow, "New Viewport Window",
      [this] { return viewports_.spawn_window(); }, {},
      [this] { return viewports_.can_spawn_window(); });
  register_viewport_view_command(quader_command::kViewportViewPerspective,
                                 "Perspective View",
                                 ViewportView::Perspective);
  register_viewport_view_command(quader_command::kViewportViewTop, "Top View",
                                 ViewportView::Top);
  register_viewport_view_command(quader_command::kViewportViewBottom,
                                 "Bottom View", ViewportView::Bottom);
  register_viewport_view_command(quader_command::kViewportViewLeft, "Left View",
                                 ViewportView::Left);
  register_viewport_view_command(quader_command::kViewportViewRight,
                                 "Right View", ViewportView::Side);
  register_viewport_view_command(quader_command::kViewportViewFront,
                                 "Front View", ViewportView::Front);
  register_viewport_view_command(quader_command::kViewportViewBack, "Back View",
                                 ViewportView::Back);

  for (const QuaderMeshArrangementDescriptor &descriptor :
       quader_mesh_arrangement_catalog()) {
    register_command(
        descriptor.command_id, arrangement_command_label(descriptor),
        [this, descriptor] {
          const QuaderMeshArrangementOptions options{
              .snap_to_grid = preferences_.grid_snap_enabled(),
              .grid_size = viewport_grid_world_size_for_preset(
                  viewports_.grid_preset()),
          };
          return descriptor.kind == QuaderMeshArrangementKind::Alignment
                     ? api_.modeling().align_selected_meshes(
                           descriptor.axis, descriptor.alignment, options)
                     : api_.modeling().distribute_selected_meshes(
                           descriptor.axis, descriptor.distribution, options);
        },
        {}, [this, kind = descriptor.kind] {
          return kind == QuaderMeshArrangementKind::Alignment
                     ? api_.modeling().can_align_selected_meshes()
                     : api_.modeling().can_distribute_selected_meshes();
        });
  }

  for (const CommandTool *tool : modeling_command_tools()) {
    if (tool == nullptr) {
      continue;
    }
    const std::string command_id = command_id_for_modeling_action(tool->id());
    if (!modeling_action_registers_dedicated_command(*tool)) {
      continue;
    }
    register_command(
        command_id, std::string(tool->label()),
        [this, tool_id = std::string(tool->id())] {
          return api_.modeling().execute_tool(tool_id);
        },
        {}, [this, tool_id = std::string(tool->id())] {
          return api_.modeling().can_execute_tool(tool_id);
        });
  }
  for (const ViewportTool *tool : viewport_tool_catalog()) {
    if (tool == nullptr) {
      continue;
    }
    register_command(
        command_id_for_tool_activation(tool->id()), std::string(tool->label()),
        [this, tool_id = std::string(tool->id())] {
          return tools_.activate(tool_id);
        },
        std::string(tool->shortcut()),
        [this, tool_kind = tool->viewport_tool()] {
          return viewport_tool_available(tool_kind) &&
                 tools_.can_activate(tool_kind);
        });
  }
}

void QuaderCommandsApi::Impl::register_viewport_view_command(
    QuaderCommandId id, std::string label, ViewportView view,
    std::string shortcut) {
  register_command(
      id, std::move(label),
      [this, view] {
        return viewports_.set_view(viewports_.focused_pane(), view);
      },
      std::move(shortcut));
}

void QuaderCommandsApi::Impl::register_default_keybindings() {
  for (const QuaderCommandDescriptor &command : registry_.available()) {
    keybindings_.register_target(command.id);
    if (!command.shortcut.empty()) {
      keybindings_.add_default_keybinding(
          command.id, command.shortcut,
          std::string(default_keybinding_context_for_command(command.id)),
          default_keybinding_weight_for_command(command.id));
    }
  }

  register_viewport_default_keybindings();
}

void QuaderCommandsApi::Impl::register_viewport_default_keybindings() {
  constexpr std::string_view kViewportContext =
      "viewportFocus && !textInputActive";
  constexpr std::string_view kOperationPreviewContext =
      "viewportFocus && !textInputActive && !navigation.active && "
      "operationPreview.active";
  constexpr std::string_view kPrimitiveToolContext =
      "viewportFocus && !textInputActive && !navigation.active && "
      "activeTool.create_primitive";

  keybindings_.add_default_keybinding(
      std::string(quader_command::kModelingApplyActivePreview.string()),
      "Space", std::string(kOperationPreviewContext), 20);
  keybindings_.add_default_keybinding(
      std::string(quader_command::kModelingCancelActivePreview.string()),
      "Escape", std::string(kOperationPreviewContext), 20);
  keybindings_.add_default_keybinding(
      std::string(quader_command::kToolsApplyPrimitiveCreation.string()),
      "Enter", std::string(kPrimitiveToolContext), 50);
  keybindings_.add_default_keybinding(
      std::string(quader_command::kToolsCancelPrimitiveCreation.string()),
      "Escape", std::string(kPrimitiveToolContext), 50);

  keybindings_.add_default_keybinding(
      viewport_input_action_target_id("cancel_active_tool"), "Escape",
      std::string(viewport_command_keybinding_context()), 10);
  keybindings_.add_default_keybinding(
      viewport_input_action_target_id("toggle_maximized"), "Space",
      std::string(viewport_command_keybinding_context()), 10);
  keybindings_.add_default_keybinding(
      viewport_input_action_target_id("delete_popup"), "X",
      std::string(viewport_command_keybinding_context()), 10);
  for (const ViewportInputActionBindingDescriptor &descriptor :
       viewport_input_action_binding_catalog()) {
    keybindings_.add_default_keybinding(
        descriptor.target_id, descriptor.shortcut,
        descriptor.when.empty() ? std::string(kViewportContext)
                                : std::string(descriptor.when),
        descriptor.weight);
  }

  for (const ViewportShortcutBindingDescriptor &descriptor :
       viewport_shortcut_binding_catalog()) {
    const std::optional<std::string> target_id =
        viewport_command_target_id_for_contract_target(descriptor.target_id);
    if (!target_id) {
      continue;
    }
    keybindings_.add_default_keybinding(
        *target_id, descriptor.shortcut,
        std::string(default_keybinding_context_for_contract_target(
            descriptor.target_id)),
        10);
  }

  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("vertex"), "1",
      std::string(viewport_command_keybinding_context()), 5);
  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("edge"), "2",
      std::string(viewport_command_keybinding_context()), 5);
  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("face"), "3",
      std::string(viewport_command_keybinding_context()), 5);
  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("object"), "4",
      std::string(viewport_command_keybinding_context()), 5);
  keybindings_.add_default_keybinding(
      viewport_selection_mode_target_id("group"), "5",
      std::string(viewport_command_keybinding_context()), 5);

  const std::vector<ViewportGridShortcutBindingDescriptor> presets =
      viewport_grid_shortcut_binding_catalog();
  for (const ViewportGridShortcutBindingDescriptor &preset : presets) {
    keybindings_.add_default_keybinding(
        viewport_grid_preset_target_id(preset.preset), preset.shortcut,
        std::string(viewport_command_keybinding_context()), 10);
  }

  if (presets.empty()) {
    for (int preset = 1; preset <= 10; ++preset) {
      keybindings_.add_default_keybinding(
          viewport_grid_preset_target_id(preset), "F" + std::to_string(preset),
          std::string(viewport_command_keybinding_context()), 10);
    }
  }
}

void QuaderCommandsApi::Impl::register_command(
    QuaderCommandId id, std::string label,
    std::function<QuaderApiVoidResult()> execute, std::string shortcut,
    std::function<bool()> enabled, bool visible) {
  register_command(std::string(id.string()), std::move(label),
                   std::move(execute), std::move(shortcut), std::move(enabled),
                   visible);
}

void QuaderCommandsApi::Impl::register_command(
    std::string id, std::string label,
    std::function<QuaderApiVoidResult()> execute, std::string shortcut,
    std::function<bool()> enabled, bool visible) {
  registry_.register_command(EditorCommandRegistration{
      .id = std::move(id),
      .label = std::move(label),
      .shortcut = std::move(shortcut),
      .visible = visible,
      .execute = std::move(execute),
      .enabled = std::move(enabled),
  });
}

QuaderApiVoidResult QuaderCommandsApi::Impl::failure(std::string message) const {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
      .revisions = api_.revisions(),
  };
}

#ifndef NDEBUG
QuaderCommandsApi::QuaderCommandsApi(QuaderApi &api,
                                     QuaderProjectApi &project,
                                     QuaderSceneDocumentsApi &scene_documents,
                                     QuaderViewportApi &viewports,
                                     QuaderToolsApi &tools,
                                     QuaderPreferencesApi &preferences,
                                     QuaderDevToolsApi &dev_tools)
    : impl_(std::make_unique<Impl>(api, project, scene_documents, viewports,
                                   tools, preferences, dev_tools)) {}
#else
QuaderCommandsApi::QuaderCommandsApi(QuaderApi &api,
                                     QuaderProjectApi &project,
                                     QuaderSceneDocumentsApi &scene_documents,
                                     QuaderViewportApi &viewports,
                                     QuaderToolsApi &tools,
                                     QuaderPreferencesApi &preferences)
    : impl_(std::make_unique<Impl>(api, project, scene_documents, viewports,
                                   tools, preferences)) {}
#endif

QuaderCommandsApi::~QuaderCommandsApi() = default;

QuaderApiVoidResult QuaderCommandsApi::execute(QuaderCommandId command_id) {
  return impl_->execute(command_id);
}

QuaderApiVoidResult QuaderCommandsApi::execute(std::string_view command_id) {
  return impl_->execute(command_id);
}

std::optional<QuaderApiStatus> QuaderCommandsApi::last_status() const {
    return impl_->last_status();
}

std::optional<QuaderCommandDescriptor>
QuaderCommandsApi::descriptor(std::string_view command_id) const {
  return impl_->descriptor(command_id);
}

std::optional<QuaderCommandDescriptor>
QuaderCommandsApi::descriptor(QuaderCommandId command_id) const {
  return impl_->descriptor(command_id.string());
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::available() const {
    return impl_->available();
}

std::string QuaderCommandsApi::command_id_for_modeling_action(
    std::string_view action_id) const {
  return impl_->command_id_for_modeling_action(action_id);
}

std::string QuaderCommandsApi::command_id_for_tool_activation(
    std::string_view tool_id) const {
  return impl_->command_id_for_tool_activation(tool_id);
}

std::string QuaderCommandsApi::shortcut_for(std::string_view command_id) const {
  return impl_->shortcut_for(command_id);
}

std::string QuaderCommandsApi::shortcut_for(QuaderCommandId command_id) const {
  return impl_->shortcut_for(command_id);
}

std::vector<QuaderKeybindingRule> QuaderCommandsApi::keybindings() const {
    return impl_->keybindings();
}

std::vector<QuaderKeybindingConflict>
QuaderCommandsApi::conflicts_for_shortcut(std::string_view shortcut,
                                          std::string_view when) const {
  return impl_->conflicts_for_shortcut(shortcut, when);
}

bool QuaderCommandsApi::resolve_viewport_shortcut(
    ViewportKey key, ViewportModifiers modifiers,
    const ViewportToolState &state, bool viewport_focused,
    bool text_input_active, ViewportShortcut &shortcut,
    QuaderViewportKeybindingContext input_context) {
  return impl_->resolve_viewport_shortcut(key, modifiers, state,
                                          viewport_focused, text_input_active,
                                          shortcut, input_context);
}

bool QuaderCommandsApi::resolve_viewport_shortcut(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active, ViewportShortcut &shortcut,
    QuaderViewportKeybindingContext input_context) {
  return impl_->resolve_viewport_shortcut(chord, state, viewport_focused,
                                          text_input_active, shortcut,
                                          input_context);
}

bool QuaderCommandsApi::resolve_viewport_input_action(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active,
    ViewportInputActionKind &action,
    QuaderViewportKeybindingContext input_context) {
  return impl_->resolve_viewport_input_action(
      chord, state, viewport_focused, text_input_active, action, input_context);
}

QuaderViewportKeybindingDispatch
QuaderCommandsApi::dispatch_viewport_keybinding(
    ViewportKey key, ViewportModifiers modifiers,
    const ViewportToolState &state, bool viewport_focused,
    bool text_input_active, QuaderViewportKeybindingContext input_context) {
  return impl_->dispatch_viewport_keybinding(key, modifiers, state,
                                             viewport_focused,
                                             text_input_active, input_context);
}

QuaderViewportKeybindingDispatch
QuaderCommandsApi::dispatch_viewport_input_binding(
    const QuaderInputChord &chord, const ViewportToolState &state,
    bool viewport_focused, bool text_input_active,
    QuaderViewportKeybindingContext input_context) {
  return impl_->dispatch_viewport_input_binding(
      chord, state, viewport_focused, text_input_active, input_context);
}

void QuaderCommandsApi::reset_pending_keybinding_chord() {
  impl_->reset_pending_keybinding_chord();
}

bool QuaderCommandsApi::has_pending_keybinding_chord() const {
  return impl_->has_pending_keybinding_chord();
}

QuaderApiVoidResult QuaderCommandsApi::set_shortcut(std::string_view command_id,
                                                    std::string shortcut) {
  return impl_->set_shortcut(command_id, std::move(shortcut));
}

QuaderApiVoidResult QuaderCommandsApi::set_shortcut(QuaderCommandId command_id,
                                                    std::string shortcut) {
  return impl_->set_shortcut(command_id, std::move(shortcut));
}

QuaderApiVoidResult
QuaderCommandsApi::remove_shortcut(std::string_view command_id) {
  return impl_->remove_shortcut(command_id);
}

QuaderApiVoidResult
QuaderCommandsApi::remove_shortcut(QuaderCommandId command_id) {
  return impl_->remove_shortcut(command_id);
}

QuaderApiVoidResult
QuaderCommandsApi::reset_shortcut(std::string_view command_id) {
  return impl_->reset_shortcut(command_id);
}

QuaderApiVoidResult
QuaderCommandsApi::reset_shortcut(QuaderCommandId command_id) {
  return impl_->reset_shortcut(command_id);
}

QuaderApiVoidResult
QuaderCommandsApi::set_keybinding_storage_path(std::filesystem::path path) {
  return impl_->set_keybinding_storage_path(std::move(path));
}

} // namespace quader
