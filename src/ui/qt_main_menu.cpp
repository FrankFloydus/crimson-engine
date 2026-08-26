////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_main_menu.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_mesh_arrangement_catalog.hpp"
#include "core/localization_service.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "viewport/viewport_layout.hpp"

#include <QAction>
#include <QActionGroup>
#include <QByteArray>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QPointer>
#include <QSignalBlocker>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace quader {
namespace {

QString qtext(const LocalizationService &localization, std::string_view key,
              std::string_view fallback) {
  return QString::fromStdString(localization.text(key, fallback));
}

QString action_text(const LocalizationService &localization,
                    std::string_view key, std::string_view fallback,
                    std::string_view shortcut = {}) {
  QString text = qtext(localization, key, fallback);
  if (!shortcut.empty()) {
    text.append(QLatin1Char('\t'));
    text.append(QString::fromUtf8(shortcut.data(),
                                  static_cast<qsizetype>(shortcut.size())));
  }
  return text;
}

std::string stable_object_suffix(std::string_view value) {
  std::string result(value);
  for (char &character : result) {
    if (character == '.' || character == '-' || character == ' ') {
      character = '_';
    }
  }
  return result;
}

std::string layout_localization_key(ViewportLayoutId layout) {
  std::string key = "viewport.layout.";
  key.append(viewport_layout_stable_id(layout));
  key.append(".label");
  return key;
}

std::optional<QuaderCommandId>
layout_command_id(ViewportLayoutId layout) {
  switch (layout) {
  case ViewportLayoutId::Single:
    return quader_command::kViewportLayoutSingle;
  case ViewportLayoutId::TwoHorizontal:
    return quader_command::kViewportLayoutTwoHorizontal;
  case ViewportLayoutId::TwoVertical:
    return quader_command::kViewportLayoutTwoVertical;
  case ViewportLayoutId::FourQuad:
    return quader_command::kViewportLayoutFourQuad;
  case ViewportLayoutId::ThreePrimaryRight:
  case ViewportLayoutId::ThreePrimaryLeft:
  case ViewportLayoutId::ThreePrimaryBottom:
  case ViewportLayoutId::ThreePrimaryTop:
    return std::nullopt;
  }
  return std::nullopt;
}

std::string tool_localization_key(const Tool &tool) {
  std::string key = "viewport.tool.";
  key.append(tool.id());
  key.append(".label");
  return key;
}

const QuaderMeshArrangementDescriptor *arrangement_descriptor(
    QuaderMeshArrangementKind kind, QuaderTransformAxis axis,
    QuaderMeshAlignment alignment = QuaderMeshAlignment::Center,
    QuaderMeshDistribution distribution = QuaderMeshDistribution::Centers) {
  for (const QuaderMeshArrangementDescriptor &descriptor :
       quader_mesh_arrangement_catalog()) {
    if (descriptor.kind == kind && descriptor.axis == axis &&
        (kind != QuaderMeshArrangementKind::Alignment ||
         descriptor.alignment == alignment) &&
        (kind != QuaderMeshArrangementKind::Distribution ||
         descriptor.distribution == distribution)) {
      return &descriptor;
    }
  }
  return nullptr;
}

using SelectionModeMask = std::uint8_t;

constexpr SelectionModeMask
selection_mode_bit(ModelingSelectionMode mode) noexcept {
  return static_cast<SelectionModeMask>(
      SelectionModeMask{1} << static_cast<unsigned>(mode));
}

constexpr SelectionModeMask kVertexSelectionMode =
    selection_mode_bit(ModelingSelectionMode::Vertex);
constexpr SelectionModeMask kEdgeSelectionMode =
    selection_mode_bit(ModelingSelectionMode::Edge);
constexpr SelectionModeMask kFaceSelectionMode =
    selection_mode_bit(ModelingSelectionMode::Face);
constexpr SelectionModeMask kObjectSelectionMode =
    selection_mode_bit(ModelingSelectionMode::Object);
constexpr SelectionModeMask kComponentSelectionModes =
    kVertexSelectionMode | kEdgeSelectionMode | kFaceSelectionMode;
constexpr SelectionModeMask kNonGroupSelectionModes =
    kComponentSelectionModes | kObjectSelectionMode;
constexpr SelectionModeMask kEdgeOrFaceSelectionModes =
    kEdgeSelectionMode | kFaceSelectionMode;

} // namespace

/// Owns the retained menu hierarchy and its editor-action bindings.
struct QtMainMenu::Impl {
  /// Associates a QAction with a typed editor command and localized label.
  struct CommandAction {
    QAction *action = nullptr;
    QMenu *owner_menu = nullptr;
    QuaderCommandId command_id;
    std::string localization_key;
    std::string fallback;
    std::function<bool()> checked;
  };

  /// Associates a checkable QAction with one viewport boolean setting.
  struct BoolSettingAction {
    QAction *action = nullptr;
    QMenu *owner_menu = nullptr;
    ViewportBoolSettingId setting = ViewportBoolSettingId::Count;
  };

  /// Associates a QAction with a viewport workspace layout.
  struct LayoutAction {
    QAction *action = nullptr;
    QMenu *owner_menu = nullptr;
    ViewportLayoutId layout = ViewportLayoutId::Single;
    std::optional<QuaderCommandId> command_id;
  };

  /// Associates a QAction with a stable dock-panel identifier.
  struct PanelAction {
    QAction *action = nullptr;
    QMenu *owner_menu = nullptr;
    UIPanelId panel = UIPanelId::Viewport;
  };

  /// Associates a QAction with a viewport tool identifier.
  struct ToolAction {
    QAction *action = nullptr;
    QMenu *owner_menu = nullptr;
    ViewportToolId tool = ViewportToolId::Select;
    std::string command_id;
    std::string localization_key;
    std::string fallback;
  };

  /// Associates a Select-menu action or separator with the modes that show it.
  struct SelectionModeAction {
    QAction *action = nullptr;
    QMenu *owner_menu = nullptr;
    SelectionModeMask modes = 0;
  };

  explicit Impl(QtMainMenuContext value) : context(std::move(value)) {}

  ~Impl() { release_menu_hierarchy(); }

  void release_menu_hierarchy() {
    // The functors capture Impl (and, for aboutToShow, QtMainMenu). Destroy the
    // exact-lifetime context before deleting any sender so no direct or queued
    // callback can observe a partially released hierarchy.
    connection_context.reset();
    for (const QPointer<QMenu> &menu : top_level_menus) {
      if (menu_bar != nullptr && menu != nullptr) {
        menu_bar->removeAction(menu->menuAction());
      }
      delete menu.data();
    }
    top_level_menus.clear();
    command_actions.clear();
    bool_setting_actions.clear();
    layout_actions.clear();
    panel_actions.clear();
    tool_actions.clear();
    selection_mode_actions.clear();
    view_menu = nullptr;
    navigation_gizmo_action = nullptr;
    material_browser_action = nullptr;
    selection_toolbar_action = nullptr;
    snapping_toolbar_action = nullptr;
    viewport_display_toolbar_action = nullptr;
    tool_toolbar_action = nullptr;
  }

  void prepare_rebuild() {
    release_menu_hierarchy();
    connection_context = std::make_unique<QObject>();
  }

  QMenu *add_top_level_menu(std::string_view key,
                            std::string_view fallback) {
    QMenu *menu =
        menu_bar->addMenu(qtext(context.localization, key, fallback));
    top_level_menus.emplace_back(menu);
    return menu;
  }

  static QMenu *top_level_owner(QMenu &menu) {
    QMenu *owner = &menu;
    while (QMenu *parent = qobject_cast<QMenu *>(owner->parentWidget())) {
      owner = parent;
    }
    return owner;
  }

  QPointer<QMenuBar> menu_bar;
  QtMainMenuContext context;
  std::unique_ptr<QObject> connection_context = std::make_unique<QObject>();
  std::vector<QPointer<QMenu>> top_level_menus;
  QMenu *view_menu = nullptr;
  QAction *navigation_gizmo_action = nullptr;
  QAction *material_browser_action = nullptr;
  QAction *selection_toolbar_action = nullptr;
  QAction *snapping_toolbar_action = nullptr;
  QAction *viewport_display_toolbar_action = nullptr;
  QAction *tool_toolbar_action = nullptr;
  std::vector<CommandAction> command_actions;
  std::vector<BoolSettingAction> bool_setting_actions;
  std::vector<LayoutAction> layout_actions;
  std::vector<PanelAction> panel_actions;
  std::vector<ToolAction> tool_actions;
  std::vector<SelectionModeAction> selection_mode_actions;

  QAction *add_command_action(QMenu &menu, QuaderCommandId command_id,
                              std::string localization_key,
                              std::string fallback,
                              std::function<bool()> checked = {}) {
    QAction *action = menu.addAction(QString::fromStdString(fallback));
    action->setObjectName(
        QStringLiteral("command_%1")
            .arg(QString::fromStdString(
                stable_object_suffix(command_id.string()))));
    action->setCheckable(static_cast<bool>(checked));
    QObject::connect(action, &QAction::triggered, connection_context.get(),
                     [this, action, command_id, checked]() {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch command action");
                       (void)context.editor.commands().execute(command_id);
                       if (checked) {
                         const bool checked_value = checked();
                         if (action->isChecked() != checked_value) {
                           const QSignalBlocker blocker(action);
                           action->setChecked(checked_value);
                         }
                         const std::optional<QuaderCommandDescriptor>
                             descriptor =
                                 context.editor.commands().descriptor(
                                     command_id);
                         const bool enabled =
                             descriptor.has_value() && descriptor->enabled;
                         if (action->isEnabled() != enabled) {
                           action->setEnabled(enabled);
                         }
                       }
                     });
    command_actions.push_back(CommandAction{
        .action = action,
        .owner_menu = top_level_owner(menu),
        .command_id = command_id,
        .localization_key = std::move(localization_key),
        .fallback = std::move(fallback),
        .checked = std::move(checked),
    });
    return action;
  }

  QAction *add_selection_mode_action(QMenu &menu,
                                     QuaderCommandId command_id,
                                     std::string localization_key,
                                     std::string fallback,
                                     SelectionModeMask modes) {
    QAction *action = add_command_action(menu, command_id,
                                         std::move(localization_key),
                                         std::move(fallback));
    selection_mode_actions.push_back({action, top_level_owner(menu), modes});
    return action;
  }

  QAction *add_selection_mode_separator(QMenu &menu,
                                        SelectionModeMask modes) {
    QAction *separator = menu.addSeparator();
    selection_mode_actions.push_back(
        {separator, top_level_owner(menu), modes});
    return separator;
  }

  QAction *add_bool_setting_action(QMenu &menu,
                                   ViewportBoolSettingId setting,
                                   std::string_view key,
                                   std::string_view fallback) {
    QAction *action = menu.addAction(qtext(context.localization, key, fallback));
    action->setObjectName(
        QStringLiteral("viewportSetting_%1")
            .arg(QString::fromUtf8(key.data(),
                                   static_cast<qsizetype>(key.size()))));
    action->setCheckable(true);
    QObject::connect(action, &QAction::toggled, connection_context.get(),
                     [this, setting](bool checked) {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch bool setting");
                       if (context.viewport_settings.set_bool_value(setting,
                                                                    checked)) {
                         if (context.viewport_settings_changed) {
                           context.viewport_settings_changed();
                         }
                       }
                     });
    bool_setting_actions.push_back(
        {action, top_level_owner(menu), setting});
    return action;
  }

  void add_arrangement_action(QMenu &menu,
                              const QuaderMeshArrangementDescriptor *descriptor) {
    if (descriptor == nullptr) {
      return;
    }
    add_command_action(menu, descriptor->command_id,
                       std::string(descriptor->localization_key),
                       std::string(descriptor->fallback_label));
  }

  void build_file_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.file", "&File");
    menu->setObjectName(QStringLiteral("menu_file"));
    add_command_action(*menu, quader_command::kSceneDocumentNew,
                       "ui.menu.file.new_scene", "New Scene");
    add_command_action(*menu, quader_command::kSceneDocumentOpen,
                       "ui.menu.file.open_scene", "Open Scene...");
    add_command_action(*menu, quader_command::kSceneDocumentSave,
                       "ui.menu.file.save_scene", "Save Scene");
    add_command_action(*menu, quader_command::kSceneDocumentSaveAs,
                       "ui.menu.file.save_scene_as", "Save Scene As...");
    add_command_action(*menu, quader_command::kSceneDocumentSaveAll,
                       "ui.menu.file.save_all_scenes", "Save All Scenes");
    add_command_action(*menu, quader_command::kSceneDocumentClose,
                       "ui.menu.file.close_scene", "Close Scene");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kSceneDocumentNext,
                       "ui.menu.file.next_scene", "Next Scene");
    add_command_action(*menu, quader_command::kSceneDocumentPrevious,
                       "ui.menu.file.previous_scene", "Previous Scene");
    menu->addSeparator();
    QMenu *project_menu = menu->addMenu(
        qtext(context.localization, "ui.menu.file.project", "Project"));
    project_menu->setObjectName(QStringLiteral("menu_file_project"));
    add_command_action(*project_menu, quader_command::kProjectNew,
                       "ui.menu.file.new_project", "New Project...");
    add_command_action(*project_menu, quader_command::kProjectOpen,
                       "ui.menu.file.open_project", "Open Project...");
    add_command_action(*project_menu, quader_command::kProjectClose,
                       "ui.menu.file.close_project", "Close Project");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kProjectExportSelectionObj,
                       "ui.menu.file.export_obj", "Export OBJ...");
    menu->addSeparator();
    QAction *exit_action =
        menu->addAction(qtext(context.localization, "ui.menu.file.exit", "Exit"));
    exit_action->setObjectName(QStringLiteral("action_exit"));
    QObject::connect(exit_action, &QAction::triggered,
                     connection_context.get(),
                     [this]() {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch presentation action");
                       if (context.request_quit) {
                         context.request_quit();
                       }
                     });
  }

  void build_edit_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.edit", "&Edit");
    menu->setObjectName(QStringLiteral("menu_edit"));
    add_command_action(*menu, quader_command::kEditUndo, "ui.menu.edit.undo",
                       "Undo");
    add_command_action(*menu, quader_command::kEditRedo, "ui.menu.edit.redo",
                       "Redo");
    add_command_action(*menu, quader_command::kEditRepeatLastAction,
                       "ui.menu.edit.repeat_last_action",
                       "Repeat Last Action");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kCutSelection,
                       "ui.menu.edit.cut", "Cut");
    add_command_action(*menu, quader_command::kCopySelection,
                       "ui.menu.edit.copy", "Copy");
    add_command_action(*menu, quader_command::kPasteSelection,
                       "ui.menu.edit.paste", "Paste");
    add_command_action(*menu, quader_command::kDuplicateSelection,
                       "ui.menu.edit.duplicate", "Duplicate");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kEditCreateInstance,
                       "ui.menu.edit.create_instance", "Create Instance");
    add_command_action(*menu, quader_command::kEditFlattenInstance,
                       "ui.menu.edit.flatten_instance", "Flatten Instance");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kModelingFlipHorizontal,
                       "ui.menu.edit.flip_horizontal", "Flip Horizontal");
    add_command_action(*menu, quader_command::kModelingFlipVertical,
                       "ui.menu.edit.flip_vertical", "Flip Vertical");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kEditClearSelection,
                       "ui.menu.edit.clear_selection", "Clear Selection");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kDeleteSelection,
                       "ui.menu.edit.delete", "Delete");
    menu->addSeparator();
    add_command_action(*menu, quader_command::kGroupSelection,
                       "ui.menu.edit.group", "Group");
    add_command_action(*menu, quader_command::kUngroupSelection,
                       "ui.menu.edit.ungroup", "Ungroup");
    menu->addSeparator();
    QMenu *snap = menu->addMenu(
        qtext(context.localization, "ui.menu.edit.snap", "Snap"));
    snap->setObjectName(QStringLiteral("menu_snap"));
    add_bool_setting_action(*snap, ViewportBoolSettingId::GridSnap,
                            "ui.menu.edit.snap.grid", "Grid");
    add_bool_setting_action(*snap, ViewportBoolSettingId::VertexSnap,
                            "ui.menu.edit.snap.vertex", "Vertex");
    menu->addSeparator();
    QAction *settings = menu->addAction(qtext(
        context.localization, "ui.menu.edit.settings", "Settings"));
    settings->setObjectName(QStringLiteral("action_settings"));
    QObject::connect(settings, &QAction::triggered, connection_context.get(),
                     [this]() {
      QDR_PROFILE_SCOPE("Qt main menu dispatch presentation action");
      if (context.show_settings) {
        context.show_settings();
      }
    });
  }

  void build_select_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.select", "&Select");
    menu->setObjectName(QStringLiteral("menu_select"));
    add_command_action(*menu, quader_command::kSelectAll,
                       "ui.menu.select.all", "Select All");
    add_command_action(*menu, quader_command::kSelectInverse,
                       "ui.menu.select.inverse", "Select Inverse");
    add_command_action(
        *menu, quader_command::kSelectToggleBackfaceSelection,
        "ui.menu.select.toggle_backface_selection",
        "Toggle Backface Selection", [this]() {
          return context.editor.preferences().backface_selection_enabled();
        });
    add_selection_mode_separator(*menu, kNonGroupSelectionModes);
    add_selection_mode_action(
        *menu, quader_command::kSelectEdgeDirection,
        "ui.menu.select.edge.direction", "Select Direction",
        kEdgeSelectionMode);
    add_selection_mode_action(
        *menu, quader_command::kSelectEdgeLoops,
        "ui.menu.select.edge.loops", "Select Edge Loops",
        kEdgeSelectionMode);
    add_selection_mode_action(
        *menu, quader_command::kSelectEdgeRing,
        "ui.menu.select.edge.ring", "Select Edge Ring",
        kEdgeSelectionMode);
    add_selection_mode_action(
        *menu, quader_command::kSelectFacesWithSameMaterial,
        "ui.menu.select.face.same_material",
        "Select Faces With Same Material", kFaceSelectionMode);
    add_selection_mode_action(
        *menu, quader_command::kSelectCoplanarFaces,
        "ui.menu.select.face.coplanar", "Select Co-planar Faces",
        kFaceSelectionMode);
    add_selection_mode_action(
        *menu, quader_command::kSelectFaceNormal,
        "ui.menu.select.face.normal", "Select Normal", kFaceSelectionMode);
    add_selection_mode_action(
        *menu, quader_command::kSelectMeshNGons,
        "ui.menu.select.mesh.ngons", "Select N-Gons",
        kObjectSelectionMode);
    add_selection_mode_action(
        *menu, quader_command::kSelectSimilarMeshes,
        "ui.menu.select.mesh.similar", "Select Similar",
        kObjectSelectionMode);
    add_selection_mode_separator(*menu, kEdgeOrFaceSelectionModes);
    add_selection_mode_action(*menu, quader_command::kSelectGrow,
                              "ui.menu.select.grow", "Grow Selection",
                              kComponentSelectionModes);
    add_selection_mode_action(*menu, quader_command::kSelectShrink,
                              "ui.menu.select.shrink", "Shrink Selection",
                              kComponentSelectionModes);
  }

  void build_align_menu() {
    QMenu *align_menu = add_top_level_menu("ui.menu.align", "&Align");
    align_menu->setObjectName(QStringLiteral("menu_align"));
    QAction *snap = align_menu->addAction(qtext(
        context.localization, "ui.menu.align.snap_to_grid", "Snap to Grid"));
    snap->setObjectName(QStringLiteral("action_align_snap_to_grid"));
    snap->setCheckable(true);
    QObject::connect(
        snap, &QAction::toggled, connection_context.get(),
        [this](bool checked) {
          QDR_PROFILE_SCOPE("Qt main menu dispatch bool setting");
          (void)context.editor.preferences().set_grid_snap_enabled(checked);
        });
    bool_setting_actions.push_back(
        {snap, top_level_owner(*align_menu),
         ViewportBoolSettingId::GridSnap});
    align_menu->addSeparator();
    constexpr std::array<
        std::tuple<QuaderTransformAxis, std::string_view, std::string_view>, 3>
        kAxes{{
            {QuaderTransformAxis::X, "ui.menu.align.axis_x", "X"},
            {QuaderTransformAxis::Y, "ui.menu.align.axis_y", "Y"},
            {QuaderTransformAxis::Z, "ui.menu.align.axis_z", "Z"},
        }};
    for (const auto &[axis, key, fallback] : kAxes) {
      QMenu *axis_menu =
          align_menu->addMenu(qtext(context.localization, key, fallback));
      axis_menu->setObjectName(
          QStringLiteral("menu_align_%1")
              .arg(QString::fromUtf8(fallback.data()).toLower()));
      for (QuaderMeshAlignment alignment :
           {QuaderMeshAlignment::Minimum, QuaderMeshAlignment::Center,
            QuaderMeshAlignment::Maximum}) {
        add_arrangement_action(
            *axis_menu,
            arrangement_descriptor(QuaderMeshArrangementKind::Alignment,
                                   axis, alignment));
      }
    }
    align_menu->addSeparator();
    for (const auto &[distribution, key, fallback] :
         {std::tuple{QuaderMeshDistribution::Centers,
                     std::string_view("ui.menu.align.distribute_centers"),
                     std::string_view("Distribute Centers")},
          std::tuple{QuaderMeshDistribution::Gaps,
                     std::string_view("ui.menu.align.distribute_gaps"),
                     std::string_view("Distribute Gaps")}}) {
      QMenu *distribution_menu = align_menu->addMenu(
          qtext(context.localization, key, fallback));
      distribution_menu->setObjectName(
          QStringLiteral("menu_%1")
              .arg(QString::fromUtf8(key.data(),
                                     static_cast<qsizetype>(key.size()))));
      for (const auto &[axis, key, fallback] : kAxes) {
        (void)key;
        (void)fallback;
        add_arrangement_action(
            *distribution_menu,
            arrangement_descriptor(QuaderMeshArrangementKind::Distribution,
                                   axis, QuaderMeshAlignment::Center,
                                   distribution));
      }
    }
  }

  void build_cleanup_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.cleanup", "&Clean Up");
    menu->setObjectName(QStringLiteral("menu_cleanup"));
    add_command_action(*menu, quader_command::kModelingRemoveDoubles,
                       "ui.menu.cleanup.remove_doubles", "Remove Doubles");
  }

  void build_tools_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.tools", "&Tools");
    menu->setObjectName(QStringLiteral("menu_tools"));
    ToolMenuGroup current_group = ToolMenuGroup::Navigation;
    bool first = true;
    for (const Tool *tool : viewport_tool_catalog()) {
      if (tool == nullptr) {
        continue;
      }
      if (!first && tool->menu_group() != current_group) {
        menu->addSeparator();
      }
      first = false;
      current_group = tool->menu_group();
      QAction *action = menu->addAction(
          action_text(context.localization, tool_localization_key(*tool),
                      tool->label(), tool->shortcut()));
      action->setObjectName(
          QStringLiteral("tool_%1")
              .arg(QString::fromUtf8(tool->id().data(),
                                     static_cast<qsizetype>(tool->id().size()))));
      action->setCheckable(true);
      const ViewportToolId tool_id = tool->viewport_tool();
      const std::string command_id =
          context.editor.commands().command_id_for_tool_activation(tool->id());
      QObject::connect(action, &QAction::triggered, connection_context.get(),
                       [this, command_id]() {
                         QDR_PROFILE_SCOPE(
                             "Qt main menu dispatch tool action");
                         (void)context.editor.commands().execute(command_id);
                       });
      tool_actions.push_back({
          .action = action,
          .owner_menu = top_level_owner(*menu),
          .tool = tool_id,
          .command_id = command_id,
          .localization_key = tool_localization_key(*tool),
          .fallback = std::string(tool->label()),
      });
    }
  }

  void build_create_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.create", "C&reate");
    menu->setObjectName(QStringLiteral("menu_create"));
    add_command_action(*menu, quader_command::kSceneCreatePointLight,
                       "ui.menu.create.point_light", "Point Light");
    add_command_action(*menu, quader_command::kSceneCreateDirectionalLight,
                       "ui.menu.create.directional_light",
                       "Directional Light");
  }

  void build_view_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.view", "&View");
    view_menu = menu;
    menu->setObjectName(QStringLiteral("menu_view"));
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowGrid,
                            "ui.menu.view.grid", "Grid");
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowMeshGrid,
                            "ui.menu.view.mesh_grid", "Mesh Grid");
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowGizmos,
                            "ui.menu.view.gizmos", "Gizmos");
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowOverlays,
                            "ui.menu.view.overlays", "Overlays");
    add_bool_setting_action(*menu, ViewportBoolSettingId::ShowInfo,
                            "ui.menu.view.show_info", "Show Info");
    menu->addSeparator();
    add_command_action(
        *menu, quader_command::kViewportToggleLocalVisibility,
        "ui.menu.view.toggle_local_visibility", "Toggle Local Visibility",
        [this]() {
          return context.editor.viewports().local_visibility_active();
        });
    add_command_action(
        *menu, quader_command::kViewportToggleTransformSpace,
        "ui.menu.view.toggle_transform_space", "Local Transforms",
        [this]() {
          return context.editor.viewports().transform_space() ==
                 QuaderTransformSpace::Local;
        });
    menu->addSeparator();
    add_command_action(*menu, quader_command::kViewportFocusAll,
                       "ui.menu.view.focus_all", "Focus All");
    add_command_action(*menu, quader_command::kViewportFrameSelection,
                       "ui.menu.view.focus_selection", "Focus Selection");
    menu->addSeparator();
    navigation_gizmo_action = menu->addAction(qtext(
        context.localization, "ui.menu.show.navigation_gizmo",
        "Navigation Gizmo"));
    navigation_gizmo_action->setObjectName(
        QStringLiteral("action_navigation_gizmo"));
    navigation_gizmo_action->setCheckable(true);
    QObject::connect(navigation_gizmo_action, &QAction::toggled,
                     connection_context.get(),
                     [this](bool visible) {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch bool setting");
                       (void)context.editor.preferences()
                           .set_navigation_gizmo_visible(visible);
                     });
    menu->addSeparator();
    material_browser_action = menu->addAction(qtext(
        context.localization, "ui.menu.view.material_browser",
        "Material Browser"));
    material_browser_action->setObjectName(
        QStringLiteral("action_material_browser"));
    QObject::connect(material_browser_action, &QAction::triggered,
                     connection_context.get(), [this]() {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch presentation action");
                       if (context.show_material_browser) {
                         context.show_material_browser();
                       }
                     });
    menu->addSeparator();
    QMenu *panels = menu->addMenu(
        qtext(context.localization, "ui.menu.view.toolbars", "Toolbars"));
    panels->setObjectName(QStringLiteral("menu_panels"));
    selection_toolbar_action = panels->addAction(qtext(
        context.localization, "ui.toolbar.selection.title", "Selection"));
    selection_toolbar_action->setObjectName(
        QStringLiteral("toolbar_selection"));
    selection_toolbar_action->setCheckable(true);
    QObject::connect(selection_toolbar_action, &QAction::toggled,
                     connection_context.get(), [this](bool visible) {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch toolbar visibility");
                       if (context.set_selection_toolbar_visible) {
                         context.set_selection_toolbar_visible(visible);
                       }
                     });
    snapping_toolbar_action = panels->addAction(qtext(
        context.localization, "ui.toolbar.snapping.title", "Snapping"));
    snapping_toolbar_action->setObjectName(
        QStringLiteral("toolbar_snapping"));
    snapping_toolbar_action->setCheckable(true);
    QObject::connect(snapping_toolbar_action, &QAction::toggled,
                     connection_context.get(), [this](bool visible) {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch toolbar visibility");
                       if (context.set_snapping_toolbar_visible) {
                         context.set_snapping_toolbar_visible(visible);
                       }
                     });
    viewport_display_toolbar_action = panels->addAction(qtext(
        context.localization, "ui.toolbar.viewport_display.title",
        "Viewport Display"));
    viewport_display_toolbar_action->setObjectName(
        QStringLiteral("toolbar_viewport_display"));
    viewport_display_toolbar_action->setCheckable(true);
    QObject::connect(viewport_display_toolbar_action, &QAction::toggled,
                     connection_context.get(), [this](bool visible) {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch toolbar visibility");
                       if (context.set_viewport_display_toolbar_visible) {
                         context.set_viewport_display_toolbar_visible(visible);
                       }
                     });
    tool_toolbar_action = panels->addAction(qtext(
        context.localization, "ui.toolbar.tools.title", "Tools"));
    tool_toolbar_action->setObjectName(QStringLiteral("toolbar_tools"));
    tool_toolbar_action->setCheckable(true);
    QObject::connect(tool_toolbar_action, &QAction::toggled,
                     connection_context.get(), [this](bool visible) {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch toolbar visibility");
                       if (context.set_tool_toolbar_visible) {
                         context.set_tool_toolbar_visible(visible);
                       }
                     });
    panels->addSeparator();
    for (UIPanelId id : kUiDockPanelIds) {
      QAction *action = panels->addAction(qtext(
          context.localization, ui_panel_localization_key(id),
          ui_panel_key(id)));
      action->setObjectName(
          QStringLiteral("panel_%1")
              .arg(QString::fromUtf8(ui_panel_stable_id(id))));
      action->setCheckable(true);
      QObject::connect(action, &QAction::toggled, connection_context.get(),
                       [this, id](bool visible) {
                         QDR_PROFILE_SCOPE(
                             "Qt main menu dispatch panel visibility");
                         if (context.set_panel_visible) {
                           context.set_panel_visible(id, visible);
                         }
                       });
      panel_actions.push_back({action, top_level_owner(*panels), id});
    }
  }

  void build_layout_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.layout", "&Layout");
    menu->setObjectName(QStringLiteral("menu_layout"));
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    for (const ViewportLayoutOption &option : viewport_layout_options()) {
      QAction *action = menu->addAction(qtext(
          context.localization, layout_localization_key(option.id),
          option.label));
      action->setObjectName(
          QStringLiteral("layout_%1")
              .arg(QString::fromUtf8(viewport_layout_stable_id(option.id).data())));
      action->setCheckable(true);
      group->addAction(action);
      const std::optional<QuaderCommandId> command_id =
          layout_command_id(option.id);
      QObject::connect(action, &QAction::triggered, connection_context.get(),
                       [this, layout = option.id, command_id]() {
                         QDR_PROFILE_SCOPE(
                             "Qt main menu dispatch layout action");
                         if (command_id.has_value()) {
                           (void)context.editor.commands().execute(
                               *command_id);
                         } else {
                           (void)context.editor.viewports().set_layout(layout);
                         }
                       });
      layout_actions.push_back(
          {action, top_level_owner(*menu), option.id, command_id});
    }
  }

  static void present_text(QAction &action, const QString &text) {
    if (action.text() != text) {
      action.setText(text);
    }
  }

  static void present_enabled(QAction &action, bool enabled) {
    if (action.isEnabled() != enabled) {
      action.setEnabled(enabled);
    }
  }

  static void present_checked(QAction &action, bool checked) {
    if (action.isChecked() == checked) {
      return;
    }
    const QSignalBlocker blocker(&action);
    action.setChecked(checked);
  }

  [[nodiscard]] ModelingSelectionMode current_selection_mode() const {
    return context.modeling.selection_mode();
  }

  void refresh_menu(QMenu &menu, ModelingSelectionMode selection_mode) {
    QDR_PROFILE_SCOPE("Qt main menu refresh owning menu");

    {
      QDR_PROFILE_SCOPE("Qt main menu sync command actions");
      for (CommandAction &entry : command_actions) {
        if (entry.owner_menu != &menu) {
          continue;
        }
        const std::optional<QuaderCommandDescriptor> command =
            context.editor.commands().descriptor(entry.command_id);
        const std::string shortcut =
            command.has_value() ? command->shortcut : std::string{};
        const bool checked = entry.checked ? entry.checked() : false;
        present_text(*entry.action,
                     action_text(context.localization, entry.localization_key,
                                 entry.fallback, shortcut));
        present_enabled(*entry.action,
                        command.has_value() && command->enabled);
        if (entry.checked) {
          present_checked(*entry.action, checked);
        }
      }
    }

    {
      QDR_PROFILE_SCOPE("Qt main menu sync selection-mode visibility");
      for (const SelectionModeAction &entry : selection_mode_actions) {
        if (entry.owner_menu != &menu) {
          continue;
        }
        const bool visible =
            (entry.modes & selection_mode_bit(selection_mode)) != 0;
        if (entry.action->isVisible() != visible) {
          entry.action->setVisible(visible);
        }
      }
    }

    {
      QDR_PROFILE_SCOPE("Qt main menu sync bool settings");
      for (const BoolSettingAction &entry : bool_setting_actions) {
        if (entry.owner_menu != &menu) {
          continue;
        }
        present_checked(
            *entry.action,
            context.viewport_settings.bool_value(entry.setting));
      }
    }

    {
      QDR_PROFILE_SCOPE("Qt main menu sync layout actions");
      std::optional<ViewportLayoutId> current_layout;
      for (const LayoutAction &entry : layout_actions) {
        if (entry.owner_menu != &menu) {
          continue;
        }
        if (!current_layout.has_value()) {
          current_layout = context.editor.viewports().layout();
        }
        const std::optional<QuaderCommandDescriptor> command =
            entry.command_id.has_value()
                ? context.editor.commands().descriptor(*entry.command_id)
                : std::nullopt;
        const std::string shortcut =
            command.has_value() ? command->shortcut : std::string{};
        present_text(
            *entry.action,
            action_text(context.localization,
                        layout_localization_key(entry.layout),
                        viewport_layout_label(entry.layout), shortcut));
        present_checked(*entry.action, *current_layout == entry.layout);
      }
    }

    {
      QDR_PROFILE_SCOPE("Qt main menu sync panel actions");
      for (const PanelAction &entry : panel_actions) {
        if (entry.owner_menu != &menu) {
          continue;
        }
        const bool visible =
            context.panel_visible ? context.panel_visible(entry.panel) : false;
        present_checked(*entry.action, visible);
      }
    }

    if (&menu == view_menu) {
      QDR_PROFILE_SCOPE("Qt main menu sync view actions");
      if (selection_toolbar_action != nullptr) {
        present_text(*selection_toolbar_action,
                     qtext(context.localization,
                           "ui.toolbar.selection.title", "Selection"));
        present_checked(*selection_toolbar_action,
                        context.selection_toolbar_visible
                            ? context.selection_toolbar_visible()
                            : false);
      }
      if (tool_toolbar_action != nullptr) {
        present_text(*tool_toolbar_action,
                     qtext(context.localization, "ui.toolbar.tools.title",
                           "Tools"));
        present_checked(*tool_toolbar_action,
                        context.tool_toolbar_visible
                            ? context.tool_toolbar_visible()
                            : false);
      }
      if (snapping_toolbar_action != nullptr) {
        present_text(*snapping_toolbar_action,
                     qtext(context.localization, "ui.toolbar.snapping.title",
                           "Snapping"));
        present_checked(*snapping_toolbar_action,
                        context.snapping_toolbar_visible
                            ? context.snapping_toolbar_visible()
                            : false);
      }
      if (viewport_display_toolbar_action != nullptr) {
        present_text(
            *viewport_display_toolbar_action,
            qtext(context.localization,
                  "ui.toolbar.viewport_display.title", "Viewport Display"));
        present_checked(*viewport_display_toolbar_action,
                        context.viewport_display_toolbar_visible
                            ? context.viewport_display_toolbar_visible()
                            : false);
      }
      if (navigation_gizmo_action != nullptr) {
        present_checked(
            *navigation_gizmo_action,
            context.editor.preferences().navigation_gizmo_visible());
      }
      if (material_browser_action != nullptr) {
        present_text(*material_browser_action,
                     qtext(context.localization,
                           "ui.menu.view.material_browser",
                           "Material Browser"));
        present_enabled(*material_browser_action,
                        context.material_browser_available
                            ? context.material_browser_available()
                            : false);
      }
    }

    {
      QDR_PROFILE_SCOPE("Qt main menu sync tool actions");
      std::optional<std::string> active_tool;
      for (const ToolAction &entry : tool_actions) {
        if (entry.owner_menu != &menu) {
          continue;
        }
        if (!active_tool.has_value()) {
          active_tool = context.editor.tools().active_tool();
        }
        const std::optional<QuaderCommandDescriptor> command =
            context.editor.commands().descriptor(entry.command_id);
        const std::string shortcut =
            command.has_value() ? command->shortcut : std::string{};
        present_text(*entry.action,
                     action_text(context.localization, entry.localization_key,
                                 entry.fallback, shortcut));
        present_checked(*entry.action,
                        *active_tool == viewport_tool_id(entry.tool));
        present_enabled(*entry.action,
                        command.has_value() && command->enabled);
      }
    }
  }
};

QtMainMenu::QtMainMenu() = default;
QtMainMenu::~QtMainMenu() = default;

void QtMainMenu::initialize(QMenuBar &menu_bar, QtMainMenuContext context) {
  impl_ = std::make_unique<Impl>(std::move(context));
  impl_->menu_bar = &menu_bar;
  rebuild();
}

void QtMainMenu::rebuild() {
  QDR_PROFILE_SCOPE("Qt main menu rebuild");
  if (!impl_ || impl_->menu_bar == nullptr) {
    return;
  }
  {
    QDR_PROFILE_SCOPE("Qt main menu rebuild hierarchy");
    impl_->prepare_rebuild();
    impl_->build_file_menu();
    impl_->build_edit_menu();
    impl_->build_select_menu();
    impl_->build_align_menu();
    impl_->build_cleanup_menu();
    impl_->build_tools_menu();
    impl_->build_create_menu();
    impl_->build_view_menu();
    impl_->build_layout_menu();
  }
  {
    QDR_PROFILE_SCOPE("Qt main menu connect aboutToShow");
    for (QAction *top_level_action : impl_->menu_bar->actions()) {
      if (QMenu *menu = top_level_action->menu()) {
        QObject::connect(
            menu, &QMenu::aboutToShow, impl_->connection_context.get(),
            [this, menu]() {
              QDR_PROFILE_SCOPE("Qt main menu aboutToShow");
              [[maybe_unused]] const QByteArray menu_identity =
                  menu->objectName().isEmpty() ? menu->title().toUtf8()
                                               : menu->objectName().toUtf8();
              QDR_PROFILE_TEXT(
                  menu_identity.constData(),
                  static_cast<std::size_t>(menu_identity.size()));
              const ModelingSelectionMode selection_mode =
                  impl_->current_selection_mode();
              impl_->refresh_menu(*menu, selection_mode);
            });
      }
    }
  }
}

void QtMainMenu::refresh() {
  QDR_PROFILE_SCOPE("Qt main menu refresh");
  if (!impl_ || impl_->menu_bar == nullptr) {
    return;
  }
  const ModelingSelectionMode selection_mode = impl_->current_selection_mode();
  for (const QPointer<QMenu> &menu : impl_->top_level_menus) {
    if (menu != nullptr) {
      impl_->refresh_menu(*menu.data(), selection_mode);
    }
  }
}

} // namespace quader
