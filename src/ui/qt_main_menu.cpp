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
#include "core/release_features.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "diagnostics/profile.hpp"
#include "editor/metadata/command_presentation_catalog.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"

#include <QAction>
#include <QActionGroup>
#include <QByteArray>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QPointer>
#include <QSignalBlocker>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr char kCommandIdProperty[] = "quaderCommandId";

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

bool same_command(QuaderCommandId left, QuaderCommandId right) noexcept {
  return left.string() == right.string();
}

std::string tool_localization_key(const Tool &tool) {
  std::string key = "viewport.tool.";
  key.append(tool.id());
  key.append(".label");
  return key;
}

const QuaderMeshArrangementDescriptor *
arrangement_descriptor(QuaderCommandId command_id) {
  for (const QuaderMeshArrangementDescriptor &descriptor :
       quader_mesh_arrangement_catalog()) {
    if (same_command(descriptor.command_id, command_id)) {
      return &descriptor;
    }
  }
  return nullptr;
}

std::string command_localization_key(QuaderCommandId command_id) {
  std::string key = "keybindings.target.";
  key.append(command_id.string());
  key.append(".label");
  return key;
}

std::string menu_command_localization_key(QuaderCommandId command_id) {
  const auto is = [command_id](QuaderCommandId candidate) {
    return same_command(command_id, candidate);
  };
  if (is(quader_command::kProjectNew)) {
    return "ui.menu.file.new_project";
  }
  if (is(quader_command::kProjectOpen)) {
    return "ui.menu.file.open_project";
  }
  if (is(quader_command::kProjectClose)) {
    return "ui.menu.file.close_project";
  }
  if (is(quader_command::kProjectExportSelectionObj)) {
    return "keybindings.target.project.export_selection_obj.label";
  }
  if (is(quader_command::kSceneDocumentNew)) {
    return "ui.menu.file.new_scene";
  }
  if (is(quader_command::kSceneDocumentOpen)) {
    return "ui.menu.file.open_scene";
  }
  if (is(quader_command::kSceneDocumentSave)) {
    return "ui.menu.file.save_scene";
  }
  if (is(quader_command::kSceneDocumentSaveAs)) {
    return "ui.menu.file.save_scene_as";
  }
  if (is(quader_command::kSceneDocumentSaveAll)) {
    return "ui.menu.file.save_all_scenes";
  }
  if (is(quader_command::kSceneDocumentClose)) {
    return "ui.menu.file.close_scene";
  }
  if (is(quader_command::kSceneDocumentNext)) {
    return "ui.menu.file.next_scene";
  }
  if (is(quader_command::kSceneDocumentPrevious)) {
    return "ui.menu.file.previous_scene";
  }
  if (is(quader_command::kEditUndo)) {
    return "ui.menu.edit.undo";
  }
  if (is(quader_command::kEditRedo)) {
    return "ui.menu.edit.redo";
  }
  if (is(quader_command::kEditRepeatLastAction)) {
    return "ui.menu.edit.repeat_last_action";
  }
  if (is(quader_command::kCutSelection)) {
    return "ui.menu.edit.cut";
  }
  if (is(quader_command::kCopySelection)) {
    return "ui.menu.edit.copy";
  }
  if (is(quader_command::kPasteSelection)) {
    return "ui.menu.edit.paste";
  }
  if (is(quader_command::kEditClearSelection)) {
    return "ui.menu.edit.clear_selection";
  }
  if (is(quader_command::kDuplicateSelection)) {
    return "ui.menu.edit.duplicate";
  }
  if (is(quader_command::kEditCreateInstance)) {
    return "ui.menu.edit.create_instance";
  }
  if (is(quader_command::kEditFlattenInstance)) {
    return "ui.menu.edit.flatten_instance";
  }
  if (is(quader_command::kGroupSelection)) {
    return "ui.menu.edit.group";
  }
  if (is(quader_command::kUngroupSelection)) {
    return "ui.menu.edit.ungroup";
  }
  if (is(quader_command::kParentToActive)) {
    return "ui.menu.edit.parent_to_active";
  }
  if (is(quader_command::kUnparentSelection)) {
    return "ui.menu.edit.unparent";
  }
  if (is(quader_command::kSelectAll)) {
    return "ui.menu.select.all";
  }
  if (is(quader_command::kSelectInverse)) {
    return "ui.menu.select.inverse";
  }
  if (is(quader_command::kSelectToggleBackfaceSelection)) {
    return "ui.menu.select.toggle_backface_selection";
  }
  if (is(quader_command::kSelectEdgeDirection)) {
    return "ui.menu.select.edge.direction";
  }
  if (is(quader_command::kSelectEdgeLoops)) {
    return "ui.menu.select.edge.loops";
  }
  if (is(quader_command::kSelectEdgeRing)) {
    return "ui.menu.select.edge.ring";
  }
  if (is(quader_command::kSelectFacesWithSameMaterial)) {
    return "ui.menu.select.face.same_material";
  }
  if (is(quader_command::kSelectCoplanarFaces)) {
    return "ui.menu.select.face.coplanar";
  }
  if (is(quader_command::kSelectFaceNormal)) {
    return "ui.menu.select.face.normal";
  }
  if (is(quader_command::kSelectMeshNGons)) {
    return "ui.menu.select.mesh.ngons";
  }
  if (is(quader_command::kSelectSimilarMeshes)) {
    return "ui.menu.select.mesh.similar";
  }
  if (is(quader_command::kSelectGrow)) {
    return "ui.menu.select.grow";
  }
  if (is(quader_command::kSelectShrink)) {
    return "ui.menu.select.shrink";
  }
  if (is(quader_command::kSceneCreatePointLight)) {
    return "ui.menu.create.point_light";
  }
  if (is(quader_command::kSceneCreateDirectionalLight)) {
    return "ui.menu.create.directional_light";
  }
  if (is(quader_command::kViewportFrameSelection)) {
    return "ui.menu.view.focus_selection";
  }
  if (is(quader_command::kViewportFocusAll)) {
    return "ui.menu.view.focus_all";
  }
  if (is(quader_command::kViewportToggleLocalVisibility)) {
    return "ui.menu.view.toggle_local_visibility";
  }
  if (is(quader_command::kViewportToggleTransformSpace)) {
    return "ui.menu.view.toggle_transform_space";
  }
  if (is(quader_command::kViewportToggleGrid)) {
    return "ui.menu.view.grid";
  }
  if (is(quader_command::kViewportToggleMeshGrid)) {
    return "ui.menu.view.mesh_grid";
  }
  if (is(quader_command::kViewportToggleGizmos)) {
    return "ui.menu.view.gizmos";
  }
  if (is(quader_command::kViewportToggleOverlays)) {
    return "ui.menu.view.overlays";
  }
  if (is(quader_command::kViewportToggleDimensions)) {
    return "ui.menu.view.dimensions";
  }
  if (is(quader_command::kViewportToggleCollisions)) {
    return "ui.menu.view.collisions";
  }
  if (is(quader_command::kViewportToggleInfo)) {
    return "ui.menu.view.show_info";
  }
  if (is(quader_command::kViewportViewPerspective)) {
    return "viewport.view.perspective";
  }
  if (is(quader_command::kViewportViewTop)) {
    return "viewport.view.top";
  }
  if (is(quader_command::kViewportViewBottom)) {
    return "viewport.view.bottom";
  }
  if (is(quader_command::kViewportViewLeft)) {
    return "viewport.view.left";
  }
  if (is(quader_command::kViewportViewRight)) {
    return "viewport.view.right";
  }
  if (is(quader_command::kViewportViewFront)) {
    return "viewport.view.front";
  }
  if (is(quader_command::kViewportViewBack)) {
    return "viewport.view.back";
  }
  if (is(quader_command::kViewportShadingWireframe)) {
    return "viewport.shading.wireframe";
  }
  if (is(quader_command::kViewportShadingSolid)) {
    return "viewport.shading.solid";
  }
  if (is(quader_command::kViewportShadingShaded)) {
    return "viewport.shading.shaded";
  }
  if (is(quader_command::kViewportShadingRendered)) {
    return "viewport.shading.rendered";
  }
  if (is(quader_command::kModelingRemoveDoubles)) {
    return "ui.menu.cleanup.remove_doubles";
  }
  if (is(quader_command::kModelingFlipHorizontal)) {
    return "ui.menu.edit.flip_horizontal";
  }
  if (is(quader_command::kModelingFlipVertical)) {
    return "ui.menu.edit.flip_vertical";
  }
  if (is(quader_command::kModelingUvPeel)) {
    return "command.modeling.uv_peel";
  }
  struct UvMenuLabel final {
    QuaderCommandId command_id;
    std::string_view localization_key;
  };
  static constexpr std::array<UvMenuLabel, 18> kUvMenuLabels{{
      {quader_command::kModelingTextureProjectCube,
       "ui.menu.uv.projection.cube"},
      {quader_command::kModelingTextureProjectCylinder,
       "ui.menu.uv.projection.cylinder"},
      {quader_command::kModelingTextureProjectSphere,
       "ui.menu.uv.projection.sphere"},
      {quader_command::kModelingTextureAlignToGrid,
       "ui.menu.uv.align.to_grid"},
      {quader_command::kModelingTextureAlignToFace,
       "ui.menu.uv.align.to_face"},
      {quader_command::kModelingTextureAlignToView,
       "ui.menu.uv.align.to_view"},
      {quader_command::kModelingTextureRotateClockwise,
       "ui.menu.uv.rotate_cw"},
      {quader_command::kModelingTextureRotateCounterClockwise,
       "ui.menu.uv.rotate_ccw"},
      {quader_command::kModelingTextureFlipTextureHorizontal,
       "ui.menu.uv.flip.horizontal"},
      {quader_command::kModelingTextureFlipTextureVertical,
       "ui.menu.uv.flip.vertical"},
      {quader_command::kModelingTextureFitBoth, "ui.menu.uv.fit.both"},
      {quader_command::kModelingTextureFitX, "ui.menu.uv.fit.x"},
      {quader_command::kModelingTextureFitY, "ui.menu.uv.fit.y"},
      {quader_command::kModelingTextureJustifyLeft,
       "ui.menu.uv.justify.left"},
      {quader_command::kModelingTextureJustifyRight,
       "ui.menu.uv.justify.right"},
      {quader_command::kModelingTextureJustifyTop,
       "ui.menu.uv.justify.top"},
      {quader_command::kModelingTextureJustifyBottom,
       "ui.menu.uv.justify.bottom"},
      {quader_command::kModelingTextureJustifyCenter,
       "ui.menu.uv.justify.center"},
  }};
  for (const UvMenuLabel &entry : kUvMenuLabels) {
    if (is(entry.command_id)) {
      return std::string(entry.localization_key);
    }
  }
  if (is(quader_command::kModelingMirrorSelectionX) ||
      is(quader_command::kModelingMirrorSelectionY) ||
      is(quader_command::kModelingMirrorSelectionZ) ||
      is(quader_command::kModelingRecalculateFaceNormals) ||
      is(quader_command::kModelingShadeFacesSmooth) ||
      is(quader_command::kModelingShadeFacesFlat)) {
    return command_localization_key(command_id);
  }
  if (command_id.string().starts_with("modeling.texture.")) {
    std::string key = "command.";
    key.append(command_id.string());
    return key;
  }
  if (command_id.string().starts_with("modeling.")) {
    std::string key = "modeling.action.";
    key.append(
        command_id.string().substr(std::string_view{"modeling."}.size()));
    key.append(".label");
    return key;
  }
  return command_localization_key(command_id);
}

bool presentation_domain_matches_selection(
    CommandPresentationDomain domain,
    ModelingSelectionMode selection_mode) noexcept {
  switch (domain) {
  case CommandPresentationDomain::Vertex:
    return selection_mode == ModelingSelectionMode::Vertex;
  case CommandPresentationDomain::Edge:
    return selection_mode == ModelingSelectionMode::Edge;
  case CommandPresentationDomain::Face:
    return selection_mode == ModelingSelectionMode::Face;
  case CommandPresentationDomain::UV:
    return selection_mode == ModelingSelectionMode::Edge ||
           selection_mode == ModelingSelectionMode::Face ||
           selection_mode == ModelingSelectionMode::Object;
  default:
    return true;
  }
}

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
    std::optional<CommandPresentationDomain> presentation_domain;
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
    std::optional<CommandPresentationDomain> presentation_domain;
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
    panel_actions.clear();
    tool_actions.clear();
    object_menu = nullptr;
    file_menu = nullptr;
    window_menu = nullptr;
    autosave_recovery_action = nullptr;
    material_find_replace_action = nullptr;
    selection_toolbar_action = nullptr;
    snapping_toolbar_action = nullptr;
    viewport_display_toolbar_action = nullptr;
    texture_toolbar_action = nullptr;
    tool_toolbar_action = nullptr;
  }

  void prepare_rebuild() {
    release_menu_hierarchy();
    connection_context = std::make_unique<QObject>();
  }

  QMenu *add_top_level_menu(std::string_view key, std::string_view fallback) {
    QMenu *menu = menu_bar->addMenu(qtext(context.localization, key, fallback));
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
  QMenu *object_menu = nullptr;
  QMenu *file_menu = nullptr;
  QMenu *window_menu = nullptr;
  QAction *autosave_recovery_action = nullptr;
  QAction *material_find_replace_action = nullptr;
  QAction *selection_toolbar_action = nullptr;
  QAction *snapping_toolbar_action = nullptr;
  QAction *viewport_display_toolbar_action = nullptr;
  QAction *texture_toolbar_action = nullptr;
  QAction *tool_toolbar_action = nullptr;
  std::vector<CommandAction> command_actions;
  std::vector<PanelAction> panel_actions;
  std::vector<ToolAction> tool_actions;

  QAction *add_command_action(QMenu &menu, QuaderCommandId command_id,
                              std::string localization_key,
                              std::string fallback,
                              std::function<bool()> checked = {},
                              std::optional<CommandPresentationDomain>
                                  presentation_domain = std::nullopt) {
    QAction *action = menu.addAction(QString::fromStdString(fallback));
    action->setObjectName(QStringLiteral("command_%1")
                              .arg(QString::fromStdString(
                                  stable_object_suffix(command_id.string()))));
    action->setProperty(
        kCommandIdProperty,
        QString::fromUtf8(command_id.string().data(),
                          static_cast<qsizetype>(command_id.string().size())));
    action->setCheckable(static_cast<bool>(checked));
    QObject::connect(
        action, &QAction::triggered, connection_context.get(),
        [this, action, command_id, checked, presentation_domain]() {
          QDR_PROFILE_SCOPE("Qt main menu dispatch command action");
          (void)context.editor.get().commands().execute(command_id);
          if (checked) {
            const bool checked_value = checked();
            if (action->isChecked() != checked_value) {
              const QSignalBlocker blocker(action);
              action->setChecked(checked_value);
            }
            const std::optional<QuaderCommandDescriptor> descriptor =
                context.editor.get().commands().descriptor(command_id);
            const bool domain_enabled =
                !presentation_domain.has_value() ||
                presentation_domain_matches_selection(
                    *presentation_domain,
                    context.modeling.get().selection_mode());
            const bool enabled = descriptor.has_value() && descriptor->enabled &&
                                 domain_enabled;
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
        .presentation_domain = presentation_domain,
    });
    return action;
  }

  [[nodiscard]] std::function<bool()>
  command_checked_state(QuaderCommandId command_id) {
    if (same_command(command_id,
                     quader_command::kSelectToggleBackfaceSelection)) {
      return [this] {
        return context.editor.get().preferences().backface_selection_enabled();
      };
    }
    if (same_command(command_id,
                     quader_command::kViewportToggleLocalVisibility)) {
      return [this] {
        return context.editor.get().viewports().local_visibility_active();
      };
    }
    if (same_command(command_id,
                     quader_command::kViewportToggleTransformSpace)) {
      return [this] {
        return context.editor.get().viewports().transform_space() ==
               QuaderTransformSpace::Local;
      };
    }
    if (same_command(command_id, quader_command::kViewportToggleGrid)) {
      return [this] {
        return context.editor.get().viewports().control(
            QuaderViewportControl::ShowGrid);
      };
    }
    if (same_command(command_id, quader_command::kViewportToggleMeshGrid)) {
      return [this] {
        return context.editor.get().viewports().control(
            QuaderViewportControl::ShowMeshGrid);
      };
    }
    if (same_command(command_id, quader_command::kViewportToggleGizmos)) {
      return [this] {
        return context.editor.get().viewports().control(
            QuaderViewportControl::ShowGizmos);
      };
    }
    if (same_command(command_id, quader_command::kViewportToggleOverlays)) {
      return [this] {
        return context.editor.get().viewports().control(
            QuaderViewportControl::ShowOverlays);
      };
    }
    if (same_command(command_id, quader_command::kViewportToggleDimensions)) {
      return [this] {
        return context.editor.get().preferences().dimensions_visible();
      };
    }
    if (same_command(command_id, quader_command::kViewportToggleCollisions)) {
      return [this] {
        return context.editor.get().preferences().collisions_visible();
      };
    }
    if (same_command(command_id, quader_command::kViewportToggleInfo)) {
      return [this] {
        return context.editor.get().preferences().viewport_info_visible();
      };
    }
    if (same_command(command_id, quader_command::kViewportToggleTextureLock)) {
      return [this] {
        return context.editor.get().preferences().texture_lock_enabled();
      };
    }
    if (same_command(command_id,
                     quader_command::kViewportToggleTextureLockScale)) {
      return [this] {
        return context.editor.get().preferences().texture_lock_scale_enabled();
      };
    }

    const auto focused_view_is = [this](ViewportView view) {
      return [this, view] {
        const auto &viewports = context.editor.get().viewports();
        return viewports.view(viewports.focused_pane()) == view;
      };
    };
    if (same_command(command_id, quader_command::kViewportViewPerspective)) {
      return focused_view_is(ViewportView::Perspective);
    }
    if (same_command(command_id, quader_command::kViewportViewTop)) {
      return focused_view_is(ViewportView::Top);
    }
    if (same_command(command_id, quader_command::kViewportViewBottom)) {
      return focused_view_is(ViewportView::Bottom);
    }
    if (same_command(command_id, quader_command::kViewportViewLeft)) {
      return focused_view_is(ViewportView::Left);
    }
    if (same_command(command_id, quader_command::kViewportViewRight)) {
      return focused_view_is(ViewportView::Side);
    }
    if (same_command(command_id, quader_command::kViewportViewFront)) {
      return focused_view_is(ViewportView::Front);
    }
    if (same_command(command_id, quader_command::kViewportViewBack)) {
      return focused_view_is(ViewportView::Back);
    }

    const auto focused_shading_is = [this](QuaderShadingMode shading) {
      return [this, shading] {
        const auto &viewports = context.editor.get().viewports();
        return viewports.shading_mode(viewports.focused_pane()) == shading;
      };
    };
    if (same_command(command_id, quader_command::kViewportShadingWireframe)) {
      return focused_shading_is(QuaderShadingMode::Wireframe);
    }
    if (same_command(command_id, quader_command::kViewportShadingSolid)) {
      return focused_shading_is(QuaderShadingMode::Solid);
    }
    if (same_command(command_id, quader_command::kViewportShadingShaded)) {
      return focused_shading_is(QuaderShadingMode::Shaded);
    }
    if (same_command(command_id, quader_command::kViewportShadingRendered)) {
      return focused_shading_is(QuaderShadingMode::Rendered);
    }
    return {};
  }

  [[nodiscard]] const ViewportTool *
  tool_for_command(QuaderCommandId command_id) const {
    for (const ViewportTool *tool : viewport_tool_catalog()) {
      if (tool != nullptr &&
          context.editor.get().commands().command_id_for_tool_activation(
              tool->id()) == command_id.string()) {
        return tool;
      }
    }
    return nullptr;
  }

  QAction *add_tool_action(QMenu &menu, const ViewportTool &tool,
                           QuaderCommandId command_id,
                           std::optional<CommandPresentationDomain> presentation_domain) {
    QAction *action = menu.addAction(
        action_text(context.localization, tool_localization_key(tool),
                    tool.label(), tool.shortcut()));
    action->setObjectName(QStringLiteral("tool_%1").arg(QString::fromUtf8(
        tool.id().data(), static_cast<qsizetype>(tool.id().size()))));
    action->setProperty(
        kCommandIdProperty,
        QString::fromUtf8(command_id.string().data(),
                          static_cast<qsizetype>(command_id.string().size())));
    action->setCheckable(true);
    const ViewportToolId tool_id = tool.viewport_tool();
    const std::string stable_command_id(command_id.string());
    QObject::connect(action, &QAction::triggered, connection_context.get(),
                     [this, stable_command_id]() {
                       QDR_PROFILE_SCOPE("Qt main menu dispatch tool action");
                       (void)context.editor.get().commands().execute(
                           stable_command_id);
                     });
    tool_actions.push_back({
        .action = action,
        .owner_menu = top_level_owner(menu),
        .tool = tool_id,
        .command_id = stable_command_id,
        .localization_key = tool_localization_key(tool),
        .fallback = std::string(tool.label()),
        .presentation_domain = presentation_domain,
    });
    return action;
  }

  QAction *add_presented_command_action(
      QMenu &menu, const CommandPresentationDescriptor &presentation) {
    if (const ViewportTool *tool = tool_for_command(presentation.command_id)) {
      // These tools enter Edge mode themselves; their menu category is not an
      // operand restriction on activation.
      const std::optional<CommandPresentationDomain> operand_domain =
          tool->viewport_tool() == ViewportToolId::InsertEdgeLoop ||
                  tool->viewport_tool() == ViewportToolId::UvPeel
              ? std::nullopt
              : std::optional{presentation.domain};
      return add_tool_action(menu, *tool, presentation.command_id,
                             operand_domain);
    }
    const std::optional<QuaderCommandDescriptor> command =
        context.editor.get().commands().descriptor(presentation.command_id);
    const std::string fallback =
        command.has_value() ? command->label
                            : std::string(presentation.command_id.string());
    return add_command_action(
        menu, presentation.command_id,
        menu_command_localization_key(presentation.command_id), fallback,
        command_checked_state(presentation.command_id), presentation.domain);
  }

  template <typename Predicate>
  std::size_t add_presented_actions(QMenu &menu, const Predicate &predicate,
                                    QActionGroup *choice_group = nullptr) {
    std::size_t added = 0;
    for (const CommandPresentationDescriptor &presentation :
         command_presentation_catalog()) {
      if (!has_command_presentation_surface(
              presentation, CommandPresentationSurface::MainMenu) ||
          !predicate(presentation) ||
          !release_presents_command(presentation.command_id.string())) {
        continue;
      }
      QAction *action = add_presented_command_action(menu, presentation);
      if (choice_group != nullptr && action->isCheckable()) {
        choice_group->addAction(action);
      }
      ++added;
    }
    return added;
  }

  std::size_t add_presented_group(QMenu &menu, CommandPresentationDomain domain,
                                  CommandPresentationGroup group,
                                  QActionGroup *choice_group = nullptr) {
    return add_presented_actions(
        menu,
        [domain, group](const CommandPresentationDescriptor &presentation) {
          return presentation.domain == domain && presentation.group == group;
        },
        choice_group);
  }

  std::size_t add_presented_rank_range(
      QMenu &menu, CommandPresentationDomain domain,
      CommandPresentationGroup group, std::uint32_t first_rank,
      std::uint32_t last_rank) {
    return add_presented_actions(
        menu, [domain, group, first_rank,
               last_rank](const CommandPresentationDescriptor &presentation) {
          return presentation.domain == domain && presentation.group == group &&
                 presentation.rank >= first_rank &&
                 presentation.rank <= last_rank;
        });
  }

  QMenu *add_presented_rank_range_submenu(
      QMenu &parent, std::string_view key, std::string_view fallback,
      std::string_view object_name, CommandPresentationDomain domain,
      CommandPresentationGroup group, std::uint32_t first_rank,
      std::uint32_t last_rank) {
    QMenu *menu = parent.addMenu(qtext(context.localization, key, fallback));
    menu->setObjectName(QString::fromUtf8(
        object_name.data(), static_cast<qsizetype>(object_name.size())));
    if (add_presented_rank_range(*menu, domain, group, first_rank, last_rank) ==
        0u) {
      parent.removeAction(menu->menuAction());
      delete menu;
      return nullptr;
    }
    return menu;
  }

  [[nodiscard]] std::size_t
  presented_group_size(CommandPresentationDomain domain,
                       CommandPresentationGroup group) const {
    return static_cast<std::size_t>(std::ranges::count_if(
        command_presentation_catalog(),
        [domain, group](const CommandPresentationDescriptor &presentation) {
          return release_presents_command(presentation.command_id.string()) &&
                 presentation.domain == domain &&
                 presentation.group == group &&
                 has_command_presentation_surface(
                     presentation, CommandPresentationSurface::MainMenu);
        }));
  }

  std::size_t append_presented_group(QMenu &menu,
                                     CommandPresentationDomain domain,
                                     CommandPresentationGroup group,
                                     QActionGroup *choice_group = nullptr) {
    if (presented_group_size(domain, group) == 0u) {
      return 0u;
    }
    if (!menu.actions().isEmpty()) {
      menu.addSeparator();
    }
    return add_presented_group(menu, domain, group, choice_group);
  }

  QMenu *add_presented_submenu(QMenu &parent, std::string_view key,
                               std::string_view fallback,
                               std::string_view object_name,
                               CommandPresentationDomain domain,
                               CommandPresentationGroup group,
                               bool exclusive = false) {
    QMenu *menu = parent.addMenu(qtext(context.localization, key, fallback));
    menu->setObjectName(QString::fromUtf8(
        object_name.data(), static_cast<qsizetype>(object_name.size())));
    QActionGroup *choice_group = nullptr;
    if (exclusive) {
      choice_group = new QActionGroup(menu);
      choice_group->setExclusive(true);
    }
    if (add_presented_group(*menu, domain, group, choice_group) == 0u) {
      parent.removeAction(menu->menuAction());
      delete menu;
      return nullptr;
    }
    return menu;
  }

  QMenu *add_presented_family(QMenu &parent, std::string_view key,
                              std::string_view fallback,
                              std::string_view object_name,
                              CommandPresentationDomain domain,
                              CommandPresentationGroup group) {
    const std::size_t group_size = presented_group_size(domain, group);
    if (group_size == 0u) {
      return nullptr;
    }
    if (group_size == 1u) {
      (void)add_presented_group(parent, domain, group);
      return nullptr;
    }
    return add_presented_submenu(parent, key, fallback, object_name, domain,
                                 group);
  }

  void append_presented_family(QMenu &parent, std::string_view key,
                               std::string_view fallback,
                               std::string_view object_name,
                               CommandPresentationDomain domain,
                               CommandPresentationGroup group) {
    if (presented_group_size(domain, group) == 0u) {
      return;
    }
    if (!parent.actions().isEmpty()) {
      parent.addSeparator();
    }
    (void)add_presented_family(parent, key, fallback, object_name, domain,
                               group);
  }

  template <typename Matches>
  void add_arrangement_actions(QMenu &menu, Matches matches) {
    for (const CommandPresentationDescriptor &presentation :
         command_presentation_catalog()) {
      if (presentation.domain != CommandPresentationDomain::Object ||
          presentation.group != CommandPresentationGroup::Arrangement ||
          !has_command_presentation_surface(
              presentation, CommandPresentationSurface::MainMenu)) {
        continue;
      }
      const QuaderMeshArrangementDescriptor *descriptor =
          arrangement_descriptor(presentation.command_id);
      if (descriptor == nullptr || !matches(*descriptor)) {
        continue;
      }
      const auto command =
          context.editor.get().commands().descriptor(presentation.command_id);
      add_command_action(menu, presentation.command_id,
                         std::string(descriptor->keybinding_localization_key),
                         command.has_value() ? command->label
                                             : std::string(descriptor->fallback_label));
    }
  }

  void show_no_recent_scenes(QMenu &menu) {
    QAction *empty = menu.addAction(qtext(
        context.localization, "ui.menu.file.no_recent_scenes", "No recent scenes"));
    empty->setObjectName(QStringLiteral("action_no_recent_scenes"));
    empty->setEnabled(false);
  }

  void populate_recent_scenes(QMenu &menu) {
    // File existence checks and history reads belong to the menu-open event,
    // never the retained frame refresh.
    const std::vector<QuaderRecentScene> recent =
        context.editor.get().project().recent_scenes();
    menu.clear();
    const std::size_t count = std::min(recent.size(), std::size_t{5});
    if (count == 0u) {
      show_no_recent_scenes(menu);
      return;
    }
    for (std::size_t index = 0; index < count; ++index) {
      const QuaderRecentScene &entry = recent[index];
      QString label = entry.scene_name.empty()
                          ? QString::fromStdWString(
                                entry.scene_path.native_path().filename().wstring())
                          : QString::fromStdString(entry.scene_name);
      if (!entry.project_name.empty()) {
        label.append(QStringLiteral(" (%1)")
                         .arg(QString::fromStdString(entry.project_name)));
      }
      label.replace(QLatin1Char('&'), QStringLiteral("&&"));
      label.replace(QLatin1Char('\t'), QLatin1Char(' '));
      label.replace(QLatin1Char('\n'), QLatin1Char(' '));
      label.replace(QLatin1Char('\r'), QLatin1Char(' '));
      QAction *action = menu.addAction(label);
      action->setObjectName(QStringLiteral("action_open_recent_scene_%1")
                                .arg(static_cast<qulonglong>(index)));
      action->setEnabled(entry.available);
      const std::filesystem::path scene_path =
          entry.manifest_path.parent_path() / entry.scene_path.native_path();
      action->setToolTip(
          QDir::toNativeSeparators(QString::fromStdWString(scene_path.wstring())));
      QObject::connect(
          action, &QAction::triggered, connection_context.get(),
          [this, entry]() {
            // A project switch can synchronously rebind the retained menu or
            // destroy its hierarchy. Keep the selected value alive independently.
            const QuaderRecentScene selected = entry;
            const QPointer<QObject> lifetime(connection_context.get());
            const QuaderApiVoidResult result =
                context.editor.get().project().open_recent_scene(selected);
            if (lifetime.isNull() || result.ok()) {
              return;
            }
            (void)context.editor.get().ui().notify({
                .level = QuaderNotificationLevel::Error,
                .message = result.error_message(),
            });
          });
    }
  }

  void refresh_autosave_recovery() {
    if (autosave_recovery_action != nullptr) {
      present_enabled(*autosave_recovery_action,
                      context.show_autosave_recovery &&
                          context.autosave_recovery_available &&
                          context.autosave_recovery_available());
    }
  }

  void build_file_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.file", "&File");
    file_menu = menu;
    menu->setObjectName(QStringLiteral("menu_file"));
    (void)add_presented_group(*menu, CommandPresentationDomain::File,
                              CommandPresentationGroup::Project);
    (void)append_presented_group(*menu, CommandPresentationDomain::File,
                                 CommandPresentationGroup::SceneDocument);

    QMenu *recent_menu = menu->addMenu(
        qtext(context.localization, "ui.menu.file.open_recent", "Open Recent"));
    recent_menu->setObjectName(QStringLiteral("menu_file_open_recent"));
    recent_menu->setToolTipsVisible(true);
    // Place recent scenes directly after Open Scene while preserving the
    // metadata-owned order of every command-backed row.
    const QList<QAction *> file_actions = menu->actions();
    for (qsizetype index = 0; index + 1 < file_actions.size(); ++index) {
      if (file_actions[index]->objectName() ==
          QStringLiteral("command_scene_document_open")) {
        menu->insertMenu(file_actions[index + 1], recent_menu);
        break;
      }
    }
    show_no_recent_scenes(*recent_menu);
    QObject::connect(recent_menu, &QMenu::aboutToShow, connection_context.get(),
                     [this, recent_menu] { populate_recent_scenes(*recent_menu); });

    menu->addSeparator();
    QMenu *recover_menu = menu->addMenu(
        qtext(context.localization, "ui.menu.file.recover", "Recover"));
    recover_menu->setObjectName(QStringLiteral("menu_file_recover"));
    autosave_recovery_action = recover_menu->addAction(qtext(
        context.localization, "ui.menu.file.recover_autosave", "Auto Save..."));
    autosave_recovery_action->setObjectName(
        QStringLiteral("action_recover_autosave"));
    refresh_autosave_recovery();
    QObject::connect(recover_menu, &QMenu::aboutToShow, connection_context.get(),
                     [this] { refresh_autosave_recovery(); });
    QObject::connect(
        autosave_recovery_action, &QAction::triggered, connection_context.get(),
        [this] {
          if (context.autosave_recovery_available &&
              context.autosave_recovery_available()) {
            const std::function<void()> recover = context.show_autosave_recovery;
            if (recover) {
              recover();
            }
          }
        });
    if (!menu->actions().isEmpty()) {
      menu->addSeparator();
    }
    QAction *exit_action = menu->addAction(
        qtext(context.localization, "ui.menu.file.exit", "Exit"));
    exit_action->setObjectName(QStringLiteral("action_exit"));
    QObject::connect(
        exit_action, &QAction::triggered, connection_context.get(), [this]() {
          QDR_PROFILE_SCOPE("Qt main menu dispatch presentation action");
          if (context.request_quit) {
            context.request_quit();
          }
        });
  }

  void build_edit_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.edit", "&Edit");
    menu->setObjectName(QStringLiteral("menu_edit"));
    (void)add_presented_group(*menu, CommandPresentationDomain::Edit,
                              CommandPresentationGroup::History);
    (void)append_presented_group(*menu, CommandPresentationDomain::Edit,
                                 CommandPresentationGroup::Clipboard);
    if (!menu->actions().isEmpty()) {
      menu->addSeparator();
    }
    QAction *settings = menu->addAction(
        qtext(context.localization, "ui.menu.edit.settings", "Settings..."));
    settings->setObjectName(QStringLiteral("action_settings"));
    QObject::connect(
        settings, &QAction::triggered, connection_context.get(), [this]() {
          QDR_PROFILE_SCOPE("Qt main menu dispatch presentation action");
          if (context.show_settings) {
            context.show_settings();
          }
        });
  }

  void build_select_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.select", "&Select");
    menu->setObjectName(QStringLiteral("menu_select"));
    (void)add_presented_group(*menu, CommandPresentationDomain::Select,
                              CommandPresentationGroup::Selection);
    const auto actions = menu->actions();
    for (qsizetype index = 0; index + 1 < actions.size(); ++index) {
      const auto &name = actions[index]->objectName();
      if (name == QStringLiteral("command_edit_clear_selection") ||
          name == QStringLiteral("command_select_inverse") ||
          name == QStringLiteral("command_select_edge_ring")) {
        menu->insertSeparator(actions[index + 1]);
      }
    }
  }

  void append_arrangement_groups(QMenu &menu) {
    if (!release_has_feature(ReleaseFeature::Arrangement)) {
      return;
    }
    for (const auto axis : {QuaderTransformAxis::X, QuaderTransformAxis::Y,
                            QuaderTransformAxis::Z}) {
      if (!menu.actions().isEmpty()) {
        menu.addSeparator();
      }
      add_arrangement_actions(
          menu, [axis](const QuaderMeshArrangementDescriptor &entry) {
            return entry.kind == QuaderMeshArrangementKind::Alignment &&
                   entry.axis == axis;
          });
    }
    for (const auto distribution : {QuaderMeshDistribution::Centers,
                                    QuaderMeshDistribution::Gaps}) {
      menu.addSeparator();
      add_arrangement_actions(
          menu, [distribution](const QuaderMeshArrangementDescriptor &entry) {
            return entry.kind == QuaderMeshArrangementKind::Distribution &&
                   entry.distribution == distribution;
          });
    }
  }

  void build_create_menu() {
    if (presented_group_size(CommandPresentationDomain::Create,
                             CommandPresentationGroup::Creation) == 0u) {
      return;
    }
    QMenu *menu = add_top_level_menu("ui.menu.create", "C&reate");
    menu->setObjectName(QStringLiteral("menu_create"));
    (void)add_presented_group(*menu, CommandPresentationDomain::Create,
                              CommandPresentationGroup::Creation);
  }

  void build_object_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.object", "&Object");
    object_menu = menu;
    menu->setObjectName(QStringLiteral("menu_object"));

    (void)append_presented_group(*menu, CommandPresentationDomain::Object,
                                 CommandPresentationGroup::Instances);
    (void)append_presented_group(*menu, CommandPresentationDomain::Object,
                                 CommandPresentationGroup::Grouping);
    append_arrangement_groups(*menu);
    (void)append_presented_group(*menu, CommandPresentationDomain::Object,
                                 CommandPresentationGroup::Duplication);

    if (release_has_feature(ReleaseFeature::Materials)) {
      if (!menu->actions().isEmpty()) {
        menu->addSeparator();
      }
      (void)add_presented_group(*menu, CommandPresentationDomain::Object,
                                CommandPresentationGroup::Materials);
      material_find_replace_action = menu->addAction(
          qtext(context.localization, "ui.menu.object.materials.find_replace",
                "Find/Replace Materials..."));
      material_find_replace_action->setObjectName(
          QStringLiteral("action_material_find_replace"));
      material_find_replace_action->setEnabled(
          context.material_find_replace_available
              ? context.material_find_replace_available()
              : false);
      QObject::connect(material_find_replace_action, &QAction::triggered,
                       connection_context.get(), [this]() {
                         QDR_PROFILE_SCOPE(
                             "Qt main menu dispatch presentation action");
                         if (context.show_material_find_replace) {
                           context.show_material_find_replace();
                         }
                       });

    }
  }

  void build_mesh_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.mesh", "&Mesh");
    menu->setObjectName(QStringLiteral("menu_mesh"));
    (void)add_presented_group(*menu, CommandPresentationDomain::Mesh,
                              CommandPresentationGroup::Tools);
    (void)append_presented_group(*menu, CommandPresentationDomain::Mesh,
                                 CommandPresentationGroup::Duplication);
    (void)append_presented_group(*menu, CommandPresentationDomain::Mesh,
                                 CommandPresentationGroup::Topology);
    append_presented_family(
        *menu, "ui.menu.mesh.transform", "Transform", "menu_mesh_transform",
        CommandPresentationDomain::Mesh, CommandPresentationGroup::Transform);
    (void)add_presented_family(
        *menu, "ui.menu.mesh.merge", "Merge / Weld", "menu_mesh_merge",
        CommandPresentationDomain::Mesh, CommandPresentationGroup::Merge);
    (void)add_presented_family(
        *menu, "ui.menu.mesh.normals", "Normals", "menu_mesh_normals",
        CommandPresentationDomain::Mesh, CommandPresentationGroup::Normals);
    (void)add_presented_family(
        *menu, "ui.menu.mesh.shading", "Shading", "menu_mesh_shading",
        CommandPresentationDomain::Mesh, CommandPresentationGroup::Shading);
    (void)append_presented_group(*menu, CommandPresentationDomain::Mesh,
                                 CommandPresentationGroup::Cleanup);
  }

  void build_vertex_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.vertex", "&Vertex");
    menu->setObjectName(QStringLiteral("menu_vertex"));
    (void)add_presented_group(*menu, CommandPresentationDomain::Vertex,
                              CommandPresentationGroup::Topology);
    append_presented_family(
        *menu, "ui.menu.vertex.transform", "Transform",
        "menu_vertex_transform", CommandPresentationDomain::Vertex,
        CommandPresentationGroup::Transform);
    (void)add_presented_family(
        *menu, "ui.menu.vertex.merge", "Merge / Weld", "menu_vertex_merge",
        CommandPresentationDomain::Vertex, CommandPresentationGroup::Merge);
    (void)append_presented_group(*menu, CommandPresentationDomain::Vertex,
                                 CommandPresentationGroup::Cleanup);
  }

  void build_edge_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.edge", "Ed&ge");
    menu->setObjectName(QStringLiteral("menu_edge"));
    (void)add_presented_group(*menu, CommandPresentationDomain::Edge,
                              CommandPresentationGroup::Tools);
    (void)append_presented_group(*menu, CommandPresentationDomain::Edge,
                                 CommandPresentationGroup::Topology);
    append_presented_family(
        *menu, "ui.menu.edge.transform", "Transform", "menu_edge_transform",
        CommandPresentationDomain::Edge, CommandPresentationGroup::Transform);
    (void)add_presented_family(
        *menu, "ui.menu.edge.merge", "Merge / Weld", "menu_edge_merge",
        CommandPresentationDomain::Edge, CommandPresentationGroup::Merge);
    (void)add_presented_family(
        *menu, "ui.menu.edge.normals", "Normals", "menu_edge_normals",
        CommandPresentationDomain::Edge, CommandPresentationGroup::Normals);
    (void)append_presented_group(*menu, CommandPresentationDomain::Edge,
                                 CommandPresentationGroup::Cleanup);
  }

  void build_face_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.face", "Fa&ce");
    menu->setObjectName(QStringLiteral("menu_face"));
    (void)add_presented_group(*menu, CommandPresentationDomain::Face,
                              CommandPresentationGroup::Topology);
    append_presented_family(
        *menu, "ui.menu.face.transform", "Transform", "menu_face_transform",
        CommandPresentationDomain::Face, CommandPresentationGroup::Transform);
    (void)add_presented_family(
        *menu, "ui.menu.face.normals", "Normals", "menu_face_normals",
        CommandPresentationDomain::Face, CommandPresentationGroup::Normals);
    (void)add_presented_family(
        *menu, "ui.menu.face.shading", "Shading", "menu_face_shading",
        CommandPresentationDomain::Face, CommandPresentationGroup::Shading);
    (void)append_presented_group(*menu, CommandPresentationDomain::Face,
                                 CommandPresentationGroup::Cleanup);
  }

  void build_uv_menu() {
    if (!release_has_feature(ReleaseFeature::UvEditing)) {
      return;
    }
    QMenu *menu = add_top_level_menu("ui.menu.uv", "&UV");
    menu->setObjectName(QStringLiteral("menu_uv"));

    // Keep the top-level UV surface deliberately compact. The command catalog
    // still exposes less-common texture transforms through command search.
    (void)add_presented_rank_range(
        *menu, CommandPresentationDomain::UV,
        CommandPresentationGroup::Texture, 500u, 500u);
    (void)add_presented_rank_range_submenu(
        *menu, "ui.menu.uv.projection", "Projection", "menu_uv_projection",
        CommandPresentationDomain::UV, CommandPresentationGroup::Texture, 501u,
        503u);
    menu->addSeparator();
    (void)add_presented_rank_range_submenu(
        *menu, "ui.menu.uv.align", "Align", "menu_uv_align",
        CommandPresentationDomain::UV, CommandPresentationGroup::Texture, 510u,
        530u);
    (void)add_presented_rank_range(
        *menu, CommandPresentationDomain::UV,
        CommandPresentationGroup::Texture, 620u, 630u);
    menu->addSeparator();
    (void)add_presented_rank_range_submenu(
        *menu, "ui.menu.uv.flip", "Flip", "menu_uv_flip",
        CommandPresentationDomain::UV, CommandPresentationGroup::Texture, 640u,
        650u);
    (void)add_presented_rank_range_submenu(
        *menu, "ui.menu.uv.fit", "Fit", "menu_uv_fit",
        CommandPresentationDomain::UV, CommandPresentationGroup::Texture, 680u,
        700u);
    (void)add_presented_rank_range_submenu(
        *menu, "ui.menu.uv.justify", "Justify", "menu_uv_justify",
        CommandPresentationDomain::UV, CommandPresentationGroup::Texture, 710u,
        750u);
  }

  void build_view_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.view", "V&iew");
    menu->setObjectName(QStringLiteral("menu_view"));
    (void)add_presented_actions(
        *menu, [](const CommandPresentationDescriptor &presentation) {
          return same_command(presentation.command_id,
                              quader_command::kViewportToggleLocalVisibility);
        });
    (void)append_presented_group(*menu, CommandPresentationDomain::View,
                                 CommandPresentationGroup::Camera);
    (void)append_presented_group(*menu, CommandPresentationDomain::Object,
                                 CommandPresentationGroup::Visibility);
    menu->addSeparator();
    QMenu *display = menu->addMenu(
        qtext(context.localization, "ui.menu.view.display", "Display"));
    display->setObjectName(QStringLiteral("menu_view_display"));
    (void)add_presented_actions(
        *display, [](const CommandPresentationDescriptor &presentation) {
          return presentation.domain == CommandPresentationDomain::View &&
                 presentation.group == CommandPresentationGroup::Display &&
                 !same_command(presentation.command_id,
                               quader_command::kViewportToggleLocalVisibility);
        });
    (void)add_presented_submenu(
        *menu, "ui.menu.view.grid", "Grid", "menu_view_grid",
        CommandPresentationDomain::View, CommandPresentationGroup::Grid);
  }

  void build_toolbar_submenu(QMenu &parent) {
    QMenu *toolbars = parent.addMenu(
        qtext(context.localization, "ui.menu.window.toolbars", "Toolbars"));
    toolbars->setObjectName(QStringLiteral("menu_window_toolbars"));
    selection_toolbar_action = toolbars->addAction(
        qtext(context.localization, "ui.toolbar.selection.title", "Selection"));
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
    snapping_toolbar_action = toolbars->addAction(
        qtext(context.localization, "ui.toolbar.snapping.title", "Snapping"));
    snapping_toolbar_action->setObjectName(QStringLiteral("toolbar_snapping"));
    snapping_toolbar_action->setCheckable(true);
    QObject::connect(snapping_toolbar_action, &QAction::toggled,
                     connection_context.get(), [this](bool visible) {
                       QDR_PROFILE_SCOPE(
                           "Qt main menu dispatch toolbar visibility");
                       if (context.set_snapping_toolbar_visible) {
                         context.set_snapping_toolbar_visible(visible);
                       }
                     });
    viewport_display_toolbar_action = toolbars->addAction(
        qtext(context.localization, "ui.toolbar.viewport_display.title",
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
    if (release_has_feature(ReleaseFeature::UvEditing)) {
      texture_toolbar_action = toolbars->addAction(
          qtext(context.localization, "ui.toolbar.texture.title", "Texturing"));
      texture_toolbar_action->setObjectName(QStringLiteral("toolbar_texture"));
      texture_toolbar_action->setCheckable(true);
      QObject::connect(texture_toolbar_action, &QAction::toggled,
                       connection_context.get(), [this](bool visible) {
                         QDR_PROFILE_SCOPE(
                             "Qt main menu dispatch toolbar visibility");
                         if (context.set_texture_toolbar_visible) {
                           context.set_texture_toolbar_visible(visible);
                         }
                       });
    }
    tool_toolbar_action = toolbars->addAction(
        qtext(context.localization, "ui.toolbar.tools.title", "Tools"));
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
  }

  void build_panel_submenu(QMenu &parent) {
    QMenu *panels = parent.addMenu(
        qtext(context.localization, "ui.menu.window.panels", "Panels"));
    panels->setObjectName(QStringLiteral("menu_window_panels"));
    for (UIPanelId id : kUiDockPanelIds) {
      if (!ui_panel_available(id)) {
        continue;
      }
      const std::string shortcut = id == UIPanelId::AssetBrowser
          ? context.editor.get().commands().shortcut_for(quader_command::kUiAssetBrowser)
          : std::string{};
      QAction *action = panels->addAction(action_text(context.localization,
          ui_panel_localization_key(id), ui_panel_key(id), shortcut));
      action->setObjectName(
          QStringLiteral("panel_%1")
              .arg(QString::fromUtf8(ui_panel_stable_id(id))));
      if (id == UIPanelId::AssetBrowser) {
        const auto command_id = quader_command::kUiAssetBrowser.string();
        action->setProperty(
            kCommandIdProperty,
            QString::fromUtf8(command_id.data(),
                              static_cast<qsizetype>(command_id.size())));
      }
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

  void build_window_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.window", "&Window");
    window_menu = menu;
    menu->setObjectName(QStringLiteral("menu_window"));

    build_toolbar_submenu(*menu);
    build_panel_submenu(*menu);
  }

  void build_help_menu() {
    QMenu *menu = add_top_level_menu("ui.menu.help", "&Help");
    menu->setObjectName(QStringLiteral("menu_help"));
    (void)add_presented_group(*menu, CommandPresentationDomain::Help,
                              CommandPresentationGroup::Discovery);
    if (!menu->actions().isEmpty()) {
      menu->addSeparator();
    }
    QAction *about = menu->addAction(
        qtext(context.localization, "ui.menu.help.about", "About Quader"));
    about->setObjectName(QStringLiteral("action_about_quader"));
    about->setEnabled(static_cast<bool>(context.show_about));
    QObject::connect(
        about, &QAction::triggered, connection_context.get(), [this]() {
          QDR_PROFILE_SCOPE("Qt main menu dispatch presentation action");
          if (context.show_about) {
            context.show_about();
          }
        });
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

  void refresh_menu(QMenu &menu, ModelingSelectionMode selection_mode) {
    QDR_PROFILE_SCOPE("Qt main menu refresh owning menu");

    if (&menu == file_menu) {
      refresh_autosave_recovery();
    }

    {
      QDR_PROFILE_SCOPE("Qt main menu sync command actions");
      for (CommandAction &entry : command_actions) {
        if (entry.owner_menu != &menu) {
          continue;
        }
        const std::optional<QuaderCommandDescriptor> command =
            context.editor.get().commands().descriptor(entry.command_id);
        const std::string shortcut =
            command.has_value() ? command->shortcut : std::string{};
        const bool checked = entry.checked ? entry.checked() : false;
        present_text(*entry.action,
                     action_text(context.localization, entry.localization_key,
                                 entry.fallback, shortcut));
        const bool domain_enabled =
            !entry.presentation_domain.has_value() ||
            presentation_domain_matches_selection(
                *entry.presentation_domain, selection_mode);
        present_enabled(*entry.action, command.has_value() && command->enabled &&
                                           domain_enabled);
        if (entry.checked) {
          present_checked(*entry.action, checked);
        }
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
        if (entry.panel == UIPanelId::AssetBrowser) {
          present_text(*entry.action, action_text(context.localization,
              ui_panel_localization_key(entry.panel), ui_panel_key(entry.panel),
              context.editor.get().commands().shortcut_for(quader_command::kUiAssetBrowser)));
        }
      }
    }

    if (&menu == window_menu) {
      QDR_PROFILE_SCOPE("Qt main menu sync window actions");
      if (selection_toolbar_action != nullptr) {
        present_text(*selection_toolbar_action,
                     qtext(context.localization, "ui.toolbar.selection.title",
                           "Selection"));
        present_checked(*selection_toolbar_action,
                        context.selection_toolbar_visible
                            ? context.selection_toolbar_visible()
                            : false);
      }
      if (tool_toolbar_action != nullptr) {
        present_text(
            *tool_toolbar_action,
            qtext(context.localization, "ui.toolbar.tools.title", "Tools"));
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
        present_text(*viewport_display_toolbar_action,
                     qtext(context.localization,
                           "ui.toolbar.viewport_display.title",
                           "Viewport Display"));
        present_checked(*viewport_display_toolbar_action,
                        context.viewport_display_toolbar_visible
                            ? context.viewport_display_toolbar_visible()
                            : false);
      }
      if (texture_toolbar_action != nullptr) {
        present_text(*texture_toolbar_action,
                     qtext(context.localization, "ui.toolbar.texture.title",
                           "Texturing"));
        present_checked(*texture_toolbar_action,
                        context.texture_toolbar_visible
                            ? context.texture_toolbar_visible()
                            : false);
      }

    }

    if (&menu == object_menu && material_find_replace_action != nullptr) {
      QDR_PROFILE_SCOPE("Qt main menu sync object presentation actions");
      present_text(*material_find_replace_action,
                   qtext(context.localization,
                         "ui.menu.object.materials.find_replace",
                         "Find/Replace Materials..."));
      present_enabled(*material_find_replace_action,
                      context.material_find_replace_available
                          ? context.material_find_replace_available()
                          : false);
    }

    {
      QDR_PROFILE_SCOPE("Qt main menu sync tool actions");
      std::optional<ViewportToolId> selected_tool;
      for (const ToolAction &entry : tool_actions) {
        if (entry.owner_menu != &menu) {
          continue;
        }
        if (!selected_tool.has_value()) {
          selected_tool = context.editor.get().viewports().selected_tool();
        }
        const std::optional<QuaderCommandDescriptor> command =
            context.editor.get().commands().descriptor(entry.command_id);
        const std::string shortcut =
            command.has_value() ? command->shortcut : std::string{};
        present_text(*entry.action,
                     action_text(context.localization, entry.localization_key,
                                 entry.fallback, shortcut));
        present_checked(*entry.action,
                        *selected_tool == entry.tool);
        const bool domain_enabled =
            !entry.presentation_domain.has_value() ||
            presentation_domain_matches_selection(
                *entry.presentation_domain, selection_mode);
        present_enabled(*entry.action, command.has_value() && command->enabled &&
                                           domain_enabled);
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

void QtMainMenu::rebind_runtime(QuaderEditorApi &editor,
                                ModelingSessionService &modeling) noexcept {
  if (!impl_) {
    return;
  }
  impl_->context.editor = std::ref(editor);
  impl_->context.modeling = std::ref(modeling);
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
    impl_->build_view_menu();
    impl_->build_select_menu();
    impl_->build_create_menu();
    impl_->build_object_menu();
    impl_->build_mesh_menu();
    impl_->build_vertex_menu();
    impl_->build_edge_menu();
    impl_->build_face_menu();
    impl_->build_uv_menu();
    impl_->build_window_menu();
    impl_->build_help_menu();
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
              QDR_PROFILE_TEXT(menu_identity.constData(),
                               static_cast<std::size_t>(menu_identity.size()));
              impl_->refresh_menu(
                  *menu, impl_->context.modeling.get().selection_mode());
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
  const ModelingSelectionMode selection_mode =
      impl_->context.modeling.get().selection_mode();
  for (const QPointer<QMenu> &menu : impl_->top_level_menus) {
    if (menu != nullptr) {
      impl_->refresh_menu(*menu.data(), selection_mode);
    }
  }
}

} // namespace quader
