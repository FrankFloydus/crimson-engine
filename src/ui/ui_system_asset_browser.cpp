////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_system.hpp"

#include "api/quader_editor_api.hpp"
#include "core/release_features.hpp"
#include "platform/platform_file_dialog.hpp"
#include "ui/qt_asset_browser.hpp"
#include "ui/qt_asset_thumbnail_cache.hpp"
#include "ui/qt_project_asset_dialog.hpp"
#include "ui/qt_retained_message_box.hpp"
#include "ui/qt_standard_material_editor_window.hpp"
#include "ui/qt_texture_info_window.hpp"
#include "ui/qt_unsaved_confirmation_dialog.hpp"
#include "ui/qt_viewport_panel.hpp"

#include <QCache>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QPixmap>
#include <QPushButton>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

QtAssetBrowserLabels asset_labels(const LocalizationService &localization) {
  const auto text = [&](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization.text(key, fallback));
  };
  return {
      .title = text("ui.panel.asset_browser", "Asset Browser"),
      .project = text("ui.asset_browser.project", "Project"),
      .search = text("ui.asset_browser.search", "Search assets"),
      .all_types = text("ui.asset_browser.all", "All assets"),
      .scenes = text("ui.asset_browser.scenes", "Scenes"),
      .materials = text("ui.asset_browser.materials", "Materials"),
      .textures = text("ui.asset_browser.textures", "Textures"),
      .scene = text("ui.asset_browser.scene", "Scene"),
      .material = text("ui.asset_browser.material", "Material"),
      .texture = text("ui.asset_browser.texture", "Texture"),
      .folder = text("ui.asset_browser.folder", "Folder"),
      .name = text("ui.material_browser.name", "Name"),
      .type = text("ui.material_browser.type", "Type"),
      .path = text("ui.asset_browser.path", "Path"),
      .folders = text("ui.material_browser.folders", "Folders"),
      .grid = text("ui.material_browser.grid", "Grid"),
      .list = text("ui.material_browser.list", "List"),
      .preview_size = text("ui.material_browser.tile_size", "Preview size"),
      .open = text("ui.asset_browser.open", "Open"),
      .create_scene = text("ui.asset_browser.create_scene", "Create Scene"),
      .create_material = text("ui.asset_browser.create_material", "Create Material"),
      .new_folder = text("ui.material_browser.new_folder", "New Folder"),
      .rename = text("ui.material_browser.context.rename", "Rename"),
      .duplicate = text("ui.material_browser.context.duplicate", "Duplicate"),
      .cut = text("ui.material_browser.context.cut", "Cut"),
      .copy = text("ui.material_browser.context.copy", "Copy"),
      .paste = text("ui.material_browser.context.paste", "Paste"),
      .delete_asset = text("ui.material_browser.context.delete", "Delete"),
      .import = text("ui.asset_browser.import", "Import…"),
      .empty = text("ui.asset_browser.empty", "No assets in this folder"),
      .no_results = text("ui.asset_browser.no_results", "No assets match this search"),
  };
}

QtAssetBrowserKind browser_kind(QuaderProjectAssetKind kind) {
  switch (kind) {
  case QuaderProjectAssetKind::Folder: return QtAssetBrowserKind::Folder;
  case QuaderProjectAssetKind::Scene: return QtAssetBrowserKind::Scene;
  case QuaderProjectAssetKind::Material: return QtAssetBrowserKind::Material;
  case QuaderProjectAssetKind::Texture: return QtAssetBrowserKind::Texture;
  }
  return QtAssetBrowserKind::Folder;
}

QString native_path_text(const std::filesystem::path &path) {
  return QString::fromStdWString(path.wstring());
}

QIcon asset_preview_icon(const QPixmap &pixmap) {
  QIcon icon(pixmap);
  // Keep authored texture/material colors while the item view paints the
  // selection background; QStyle otherwise colorizes Selected icon pixels.
  icon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
  icon.addPixmap(pixmap, QIcon::Selected, QIcon::On);
  return icon;
}

} // namespace

void UISystem::configure_asset_browser() {
  if (asset_browser_ == nullptr || render_system_ == nullptr) {
    return;
  }
  asset_browser_->set_labels(asset_labels(localization_));
  asset_thumbnails_ = std::make_unique<QtAssetThumbnailCache>();
  asset_thumbnails_->initialize(*render_system_, [this](ProjectAssetId id)
      -> std::optional<RenderMaterialEvaluationSnapshot> {
    if (editor_ == nullptr || !editor_->project().has_project()) {
      return std::nullopt;
    }
    const auto material = editor_->project_assets().material_thumbnail_info(id);
    return material.ok()
        ? std::optional(asset_material_thumbnail_render_snapshot(material.value()))
        : std::nullopt;
  });
  auto textures = std::make_shared<QCache<QString, QIcon>>(32 * 1024);
  asset_thumbnail_provider_ =
      [this, textures](const QtAssetBrowserItem &item, QSize logical_size,
                       qreal dpr) -> QIcon {
        if (editor_ == nullptr || !editor_->project().has_project()) {
          return {};
        }
        const auto id = ProjectAssetId::parse(item.id.toStdString());
        if (!id) {
          return {};
        }
        if (item.kind == QtAssetBrowserKind::Material && asset_thumbnails_) {
          const QImage image = asset_thumbnails_->request(
              *id, item.content_revision, logical_size, dpr);
          return image.isNull() ? QIcon{} : asset_preview_icon(QPixmap::fromImage(image));
        }
        if (item.kind != QtAssetBrowserKind::Texture) {
          return {};
        }
        const auto revision = editor_->project_assets().asset_revision();
        if (!revision.ok()) return {};
        const QSize size = logical_size * dpr;
        const QString key = QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
            .arg(QString::fromStdString(std::string(revision.value().project_id.str())))
            .arg(revision.value().project_generation)
            .arg(item.id).arg(item.content_revision)
            .arg(size.width()).arg(size.height()).arg(dpr, 0, 'g', 17);
        if (const QIcon *cached = textures->object(key)) {
          return *cached;
        }
        const auto texture = editor_->project_assets().texture_info(*id);
        QIcon icon;
        if (texture.ok() && texture.value().rgba8 &&
            texture.value().width > 0 && texture.value().height > 0) {
          const auto &source = texture.value();
          const QImage image(
              reinterpret_cast<const uchar *>(source.rgba8->data()),
              static_cast<int>(source.width), static_cast<int>(source.height),
              QImage::Format_RGBA8888);
          QPixmap pixmap = QPixmap::fromImage(image.scaled(
              size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
          pixmap.setDevicePixelRatio(dpr);
          icon = asset_preview_icon(pixmap);
        }
        textures->insert(key, new QIcon(icon),
                         std::max(1, size.width() * size.height() / 256));
        return icon;
      };
  asset_browser_->set_thumbnail_provider(asset_thumbnail_provider_);
  asset_browser_->set_visible_items_changed_callback(
      [this](const std::vector<QString> &ids) {
        asset_browser_visible_thumbnail_ids_.clear();
        asset_browser_visible_thumbnail_ids_.reserve(ids.size());
        for (const auto &id : ids) {
          if (const auto parsed = ProjectAssetId::parse(id.toStdString())) {
            asset_browser_visible_thumbnail_ids_.push_back(*parsed);
          }
        }
        retain_visible_asset_thumbnails();
      });
}

void UISystem::retain_visible_asset_thumbnails() {
  if (!asset_thumbnails_) return;
  std::vector<ProjectAssetId> visible;
  if (panel_presented(UIPanelId::AssetBrowser)) {
    visible = asset_browser_visible_thumbnail_ids_;
  }
  if (asset_thumbnail_picker_ && asset_thumbnail_picker_->isVisible()) {
    visible.insert(visible.end(), asset_picker_visible_thumbnail_ids_.begin(),
                   asset_picker_visible_thumbnail_ids_.end());
  }
  asset_thumbnails_->retain_visible_requests(visible);
}

void UISystem::refresh_asset_thumbnails() {
  if (!asset_thumbnails_) return;
  if (asset_thumbnail_picker_ && asset_thumbnail_picker_current_ &&
      !asset_thumbnail_picker_current_()) {
    asset_thumbnail_picker_->window()->close();
    asset_thumbnail_picker_.clear();
  }
  const bool dock_visible = panel_presented(UIPanelId::AssetBrowser);
  const bool picker_visible = asset_thumbnail_picker_ &&
                             asset_thumbnail_picker_->isVisible();
  const bool visibility_changed = asset_thumbnail_dock_presented_ != dock_visible ||
                                 asset_thumbnail_picker_presented_ != picker_visible;
  asset_thumbnail_dock_presented_ = dock_visible;
  asset_thumbnail_picker_presented_ = picker_visible;
  const bool visible = (dock_visible || picker_visible) && editor_ != nullptr &&
                       editor_->project().has_project();
  asset_thumbnails_->set_visible(visible);
  if (!visible) return;
  const auto revision = editor_->project_assets().asset_revision();
  if (!revision.ok()) return;
  asset_thumbnails_->set_project_generation(revision.value().project_generation);
  asset_thumbnails_->set_catalog_revision(revision.value().revision);
  if (visibility_changed) retain_visible_asset_thumbnails();
  for (const ProjectAssetId id : asset_thumbnails_->poll()) {
    const QString identity = QString::fromStdString(std::string(id.str()));
    if (dock_visible && asset_browser_) asset_browser_->invalidate_thumbnail(identity);
    if (picker_visible && asset_thumbnail_picker_) {
      asset_thumbnail_picker_->invalidate_thumbnail(identity);
    }
  }
}

void UISystem::refresh_asset_browser_labels() {
  if (asset_browser_ != nullptr) asset_browser_->set_labels(asset_labels(localization_));
}

QtAssetBrowserSnapshot UISystem::asset_browser_snapshot(bool refresh_files) {
  QtAssetBrowserSnapshot snapshot;
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return snapshot;
  }
  const auto catalog = editor_->project_assets().assets(refresh_files);
  if (!catalog.ok()) {
    present_status_result(catalog);
    return snapshot;
  }
  snapshot.generation = catalog.value().project_generation;
  snapshot.revision = catalog.value().revision;
  QSet<QString> folders;
  for (const auto &entry : catalog.value().assets) {
    if (!release_has_feature(ReleaseFeature::Materials) &&
        (entry.kind == QuaderProjectAssetKind::Material ||
         entry.kind == QuaderProjectAssetKind::Texture)) {
      continue;
    }
    const QString path = QString::fromStdString(entry.project_relative_path);
    QString identity;
    if (entry.kind == QuaderProjectAssetKind::Scene) {
      // Physical copies of a registered scene deliberately have no scene UUID.
      // Their paths must distinguish the browser rows until opening registers
      // them; the nil UUID is not a shared presentation identity.
      identity = entry.scene_id.valid()
          ? QString::fromStdString(std::string(entry.scene_id.str()))
          : QStringLiteral("scene:") + path;
    } else {
      identity = entry.asset_id.valid()
          ? QString::fromStdString(std::string(entry.asset_id.str()))
          : QStringLiteral("asset:") + path;
    }
    snapshot.items.push_back({
        .id = entry.kind == QuaderProjectAssetKind::Folder
            ? path : identity,
        .relative_path = path,
        .name = QString::fromStdString(entry.name),
        .kind = browser_kind(entry.kind),
        .content_revision = entry.content_revision,
    });
    if (entry.kind == QuaderProjectAssetKind::Folder) {
      folders.insert(path);
    }
  }
  for (const std::string &folder : catalog.value().folders) {
    const QString path = QString::fromStdString(folder);
    if (!path.isEmpty() && !folders.contains(path)) {
      snapshot.items.push_back({.id = path, .relative_path = path,
          .name = path.section(QLatin1Char('/'), -1),
          .kind = QtAssetBrowserKind::Folder});
    }
  }
  return snapshot;
}

void UISystem::refresh_asset_browser(bool force, bool prepare_reveal) {
  if (asset_browser_ == nullptr) {
    return;
  }
  const bool visible = panel_presented(UIPanelId::AssetBrowser);
  refresh_asset_thumbnails();
  if (editor_ == nullptr || !editor_->project().has_project()) {
    if (asset_browser_generation_ != 0u) {
      asset_browser_->clear();
      asset_browser_revision_ = 0u;
      asset_browser_generation_ = 0u;
      asset_browser_discovery_pending_ = false;
      asset_clipboard_paths_.clear();
      asset_browser_->set_paste_available(false);
      if (asset_thumbnails_) asset_thumbnails_->set_project_generation(0u);
    }
    return;
  }
  if (!visible && !force && !prepare_reveal) {
    return;
  }
  const auto revision = editor_->project_assets().asset_revision();
  if (!revision.ok()) {
    return;
  }
  const bool reveal_ready = !prepare_reveal && visible &&
      asset_browser_discovery_pending_ && asset_browser_->has_completed_visible_paint();
  const bool refresh_files = force || reveal_ready;
  if (reveal_ready) asset_browser_discovery_pending_ = false;
  const bool changed = refresh_files || asset_browser_revision_ != revision.value().revision ||
      asset_browser_generation_ != revision.value().project_generation;
  if (changed) {
    // The pending reveal was consumed before discovery, including failed
    // attempts. Unchanged frames never repeat physical catalog work.
    auto snapshot = asset_browser_snapshot(refresh_files);
    if (snapshot.generation == 0u) return;
    if (asset_browser_generation_ != snapshot.generation) {
      asset_clipboard_paths_.clear();
      asset_browser_->set_paste_available(false);
    }
    asset_browser_generation_ = snapshot.generation;
    asset_browser_revision_ = snapshot.revision;
    if (asset_thumbnails_) {
      asset_thumbnails_->set_project_generation(snapshot.generation);
      asset_thumbnails_->set_catalog_revision(snapshot.revision);
    }
    (void)asset_browser_->set_snapshot(std::move(snapshot));
  }
}

void UISystem::toggle_asset_browser() {
  if (asset_browser_ == nullptr || home_visible_ || editor_ == nullptr ||
      !editor_->project().has_project()) {
    return;
  }
  if (panel_presented(UIPanelId::AssetBrowser) && asset_browser_->owns_browser_focus()) {
    set_panel_visible(UIPanelId::AssetBrowser, false);
    if (viewport_panel_ != nullptr) (void)viewport_panel_->focus_active_pane();
    return;
  }
  set_panel_visible(UIPanelId::AssetBrowser, true);
  (void)panels_.focus(UIPanelId::AssetBrowser);
  asset_browser_->focus_browser();
}

QuaderSceneSavePathResult UISystem::choose_project_scene_path(
    bool save, const QuaderSceneDocumentInfo *scene,
    std::span<const std::string> excluded_paths) {
  if (editor_ == nullptr || !editor_->project().has_project()) {
    return {.error_message = "Open a project before choosing a scene asset."};
  }
  const auto root = editor_->project().root();
  const auto binding = runtime_binding_generation_;
  const auto project_id = editor_->project_assets().asset_revision();
  if (!project_id.ok()) return {.error_message = project_id.error_message()};
  const auto snapshot = asset_browser_snapshot(true);
  const auto generation = snapshot.generation;
  const auto current_project = [this, binding, generation,
                                id = project_id.value().project_id]() {
    if (editor_ == nullptr || runtime_binding_generation_ != binding ||
        !editor_->project().has_project()) return false;
    const auto current = editor_->project_assets().asset_revision();
    return current.ok() && current.value().project_generation == generation &&
           current.value().project_id == id;
  };
  QtProjectAssetDialog dialog(save ? QtProjectAssetDialogMode::SaveScene
                                   : QtProjectAssetDialogMode::OpenScene,
                              native_path_text(root), snapshot, main_window_.get());
  const auto text = [this](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization_.text(key, fallback));
  };
  dialog.set_labels({
      .open_title = text("ui.asset_browser.open_scene", "Open Scene"),
      .save_title = text("ui.asset_browser.save_scene", "Save Scene"),
      .name = text("ui.material_browser.name", "Name"),
      .open = text("ui.asset_browser.open", "Open"),
      .save = text("ui.scene.unsaved.save", "Save"),
      .cancel = text("ui.action.cancel", "Cancel"),
      .invalid_path = text("ui.asset_browser.invalid_path", "Choose a scene location inside this project."),
      .browser = asset_labels(localization_),
  });
  QStringList excluded;
  for (const auto &path : excluded_paths) excluded.push_back(QString::fromStdString(path));
  dialog.set_excluded_paths(excluded);
  if (scene && scene->path) {
    dialog.set_initial_path(QString::fromStdString(scene->path->portable_string()));
  } else {
    const QString folder = asset_browser_ ? asset_browser_->selected_folder() : QString{};
    dialog.set_initial_path(save
        ? (folder.isEmpty() ? QString{} : folder + QLatin1Char('/')) +
              (scene ? QString::fromStdString(scene->name) : text("ui.asset_browser.new_scene", "New Scene")) + QStringLiteral(".qdr")
        : folder);
  }
  dialog.apply_ui_scale(ui_scale_controller_->metrics());
  dialog.set_create_folder_callback([this, &dialog, &current_project](const auto &request) {
    if (!current_project()) {
      dialog.reject();
      return;
    }
    QString name = request.proposed_name;
    if (name.isEmpty()) {
      bool accepted = false;
      name = QInputDialog::getText(&dialog,
          QString::fromStdString(localization_.text("ui.material_browser.new_folder", "New Folder")),
          QString::fromStdString(localization_.text("ui.material_browser.folder_name", "Folder name")),
          QLineEdit::Normal,
          QString::fromStdString(localization_.text("ui.material_browser.new_folder", "New Folder")),
          &accepted);
      if (!accepted || name.isEmpty() || !current_project()) return;
    }
    const auto result = editor_->project_assets().create_folder(
        request.destination_folder.toStdString(), name.toStdString());
    present_lifecycle_result(result);
    if (result.ok() && current_project()) dialog.set_snapshot(asset_browser_snapshot(false));
  });
  if (dialog.exec() != QDialog::Accepted) return {};
  if (!current_project()) {
    return {.error_message = "The project changed while choosing a scene asset."};
  }
  return {.path = root / std::filesystem::path(dialog.selected_relative_path().toStdWString())};
}

std::optional<ProjectRelativePath> UISystem::choose_project_material_path(
    const QuaderStandardMaterialDocumentInfo &material) {
  if (editor_ == nullptr || !editor_->project().has_project()) return std::nullopt;
  const auto binding = runtime_binding_generation_;
  const auto project_revision = editor_->project_assets().asset_revision();
  if (!project_revision.ok()) return std::nullopt;
  const auto snapshot = asset_browser_snapshot(true);
  const auto current_document = [this, binding, generation = snapshot.generation,
                                 project_id = project_revision.value().project_id,
                                 session = material.session_id]() {
    if (editor_ == nullptr || runtime_binding_generation_ != binding ||
        !editor_->project().has_project()) return false;
    const auto project = editor_->project_assets().asset_revision();
    if (!project.ok() || project.value().project_generation != generation ||
        project.value().project_id != project_id) return false;
    const auto document = editor_->standard_material_documents().current_revision();
    return document.ok() && document.value().session_id == session;
  };
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveMaterial,
      native_path_text(editor_->project().root()), snapshot,
      standard_material_editor_window_);
  const auto text = [this](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization_.text(key, fallback));
  };
  dialog.set_labels({
      .save_material_title = text("ui.asset_browser.save_material", "Save Material"),
      .name = text("ui.material_browser.name", "Name"),
      .save = text("ui.scene.unsaved.save", "Save"),
      .cancel = text("ui.action.cancel", "Cancel"),
      .invalid_material_path = text("ui.standard_material_editor.path_outside_project",
                                   "Choose a material inside the current project."),
      .browser = asset_labels(localization_),
  });
  dialog.browser().set_thumbnail_provider(
      [this, current_document](const auto &item, QSize size, qreal dpr) {
        return current_document() && asset_thumbnail_provider_
            ? asset_thumbnail_provider_(item, size, dpr) : QIcon{};
      });
  // The modal browser shares the paint-demand cache without depending on the
  // management dock being open. Restore its previous consumer on every exit.
  const auto refresh_after_picker = qScopeGuard([this]() { refresh_asset_thumbnails(); });
  const QScopedValueRollback picker_scope(asset_thumbnail_picker_,
      QPointer<QtAssetBrowser>(&dialog.browser()));
  const QScopedValueRollback picker_context_scope(asset_thumbnail_picker_current_,
      std::function<bool()>(current_document));
  const QScopedValueRollback picker_tiles_scope(asset_picker_visible_thumbnail_ids_,
      std::vector<ProjectAssetId>{});
  dialog.browser().set_visible_items_changed_callback(
      [this, current_document](const std::vector<QString> &ids) {
        if (!current_document()) return;
        asset_picker_visible_thumbnail_ids_.clear();
        asset_picker_visible_thumbnail_ids_.reserve(ids.size());
        for (const auto &id : ids) {
          if (const auto parsed = ProjectAssetId::parse(id.toStdString())) {
            asset_picker_visible_thumbnail_ids_.push_back(*parsed);
          }
        }
        retain_visible_asset_thumbnails();
      });
  if (material.path) {
    const auto path = material.path->native_path();
    dialog.set_initial_path(native_path_text(path.parent_path() /
        (path.stem().wstring() + L"_copy.qmat")));
  } else {
    const auto proposed = ProjectRelativePath::parse_portable(material.name + ".qmat");
    const QString name = proposed.ok() && !proposed.value().native_path().has_parent_path()
        ? QString::fromStdString(proposed.value().portable_string())
        : QStringLiteral("Material.qmat");
    const auto folder = std::ranges::find_if(snapshot.items, [](const auto &item) {
      return item.kind == QtAssetBrowserKind::Folder &&
             item.relative_path == QStringLiteral("Materials");
    });
    dialog.set_initial_path(folder != snapshot.items.end()
        ? QStringLiteral("Materials/") + name : name);
  }
  dialog.apply_ui_scale(ui_scale_controller_->metrics());
  dialog.set_create_folder_callback([this, &dialog, &current_document](const auto &request) {
    if (!current_document()) { dialog.reject(); return; }
    bool accepted = false;
    const QString name = QInputDialog::getText(&dialog,
        QString::fromStdString(localization_.text("ui.material_browser.new_folder", "New Folder")),
        QString::fromStdString(localization_.text("ui.material_browser.folder_name", "Folder name")),
        QLineEdit::Normal,
        QString::fromStdString(localization_.text("ui.material_browser.new_folder", "New Folder")),
        &accepted);
    if (!accepted || name.isEmpty() || !current_document()) return;
    const auto result = editor_->project_assets().create_folder(
        request.destination_folder.toStdString(), name.toStdString());
    present_lifecycle_result(result);
    if (result.ok() && current_document()) dialog.set_snapshot(asset_browser_snapshot(false));
  });
  dialog.show();
  refresh_asset_thumbnails();
  if (dialog.exec() != QDialog::Accepted || !current_document()) return std::nullopt;
  const auto relative = ProjectRelativePath::parse_portable(
      dialog.selected_relative_path().toStdString());
  return relative.ok() ? std::optional(relative.value()) : std::nullopt;
}

void UISystem::handle_asset_browser_action(QtAssetBrowserActionRequest request) {
  if (editor_ == nullptr || asset_browser_ == nullptr || !editor_->project().has_project()) return;
  const auto observed = editor_->project_assets().asset_revision();
  if (!observed.ok() || request.generation != observed.value().project_generation ||
      request.catalog_revision != observed.value().revision) {
    refresh_asset_browser(true);
    return;
  }
  const auto generation = request.generation;
  const auto binding = runtime_binding_generation_;
  const auto still_current = [this, generation, binding,
                              id = observed.value().project_id]() {
    if (editor_ == nullptr || runtime_binding_generation_ != binding ||
        !editor_->project().has_project()) return false;
    const auto current = editor_->project_assets().asset_revision();
    return current.ok() && current.value().project_generation == generation &&
           current.value().project_id == id;
  };
  std::vector<std::string> paths;
  for (const auto &item : request.selected_items) paths.push_back(item.relative_path.toStdString());
  const std::string folder = request.destination_folder.toStdString();
  const auto finish = [this, &still_current](const QuaderApiStatus &result) {
    present_lifecycle_result(result);
    if (still_current()) {
      // Successful mutations already publish the owner catalog. Re-reading
      // the project here repeats filesystem work for every drag and rename.
      refresh_asset_browser(!result.ok());
      if (result.ok() && result.changed()) {
        // Workspace events and material-owner revisions already identify
        // changed presentations; do not add a second forced invalidation.
        refresh_project_workspace();
        refresh_standard_material_editor();
      }
    }
  };
  const auto project_id = observed.value().project_id;
  const auto revision = request.catalog_revision;
  switch (request.action) {
  case QtAssetBrowserAction::Open:
    if (request.selected_items.size() != 1u) return;
    if (request.selected_items.front().kind == QtAssetBrowserKind::Scene) {
      const auto path = ProjectRelativePath::parse_portable(paths.front());
      if (path.ok()) {
        const auto result = editor_->scene_documents().open(path.value());
        present_lifecycle_result(result);
        refresh_project_workspace();
      }
    } else if (const auto id = ProjectAssetId::parse(request.selected_items.front().id.toStdString())) {
      if (request.selected_items.front().kind == QtAssetBrowserKind::Material) open_standard_material_document(*id);
      else if (request.selected_items.front().kind == QtAssetBrowserKind::Texture) show_asset_texture_info(*id);
    }
    return;
  case QtAssetBrowserAction::CreateScene:
  case QtAssetBrowserAction::CreateMaterial: {
    const auto result = request.action == QtAssetBrowserAction::CreateScene
        ? editor_->project_assets().create_scene_asset(folder, localization_.text("ui.asset_browser.new_scene", "New Scene"))
        : editor_->project_assets().create_material_asset(folder, localization_.text("ui.asset_browser.new_material", "New Material"));
    finish(result);
    if (result.ok() && still_current()) (void)asset_browser_->begin_rename(QString::fromStdString(result.value().project_relative_path));
    return;
  }
  case QtAssetBrowserAction::CreateFolder: {
    QSet<QString> previous_folders;
    for (const auto &item : asset_browser_->snapshot().items) {
      if (item.kind == QtAssetBrowserKind::Folder) previous_folders.insert(item.relative_path);
    }
    const auto name = request.proposed_name.isEmpty()
        ? localization_.text("ui.material_browser.new_folder", "New Folder")
        : request.proposed_name.toStdString();
    const auto result = editor_->project_assets().create_folder(folder, name);
    finish(result);
    if (result.ok() && still_current()) {
      for (const auto &item : asset_browser_->snapshot().items) {
        if (item.kind == QtAssetBrowserKind::Folder &&
            !previous_folders.contains(item.relative_path)) {
          (void)asset_browser_->begin_rename(item.relative_path);
          break;
        }
      }
    }
    return;
  }
  case QtAssetBrowserAction::Rename:
    if (paths.size() == 1u) finish(editor_->project_assets().rename_asset(paths.front(), request.proposed_name.toStdString(), project_id, revision));
    return;
  case QtAssetBrowserAction::Duplicate:
    finish(editor_->project_assets().duplicate_assets(paths, project_id, revision));
    return;
  case QtAssetBrowserAction::Cut:
  case QtAssetBrowserAction::Copy: {
    const auto catalog = editor_->project_assets().assets();
    if (!catalog.ok()) { present_lifecycle_result(catalog); return; }
    std::vector<QuaderProjectAssetInfo> sources;
    for (const auto &path : paths) {
      const auto entry = std::ranges::find(catalog.value().assets, path,
          &QuaderProjectAssetInfo::project_relative_path);
      if (entry == catalog.value().assets.end()) return;
      sources.push_back(*entry);
    }
    asset_clipboard_paths_ = std::move(paths);
    asset_clipboard_sources_ = std::move(sources);
    asset_clipboard_generation_ = generation;
    asset_clipboard_cut_ = request.action == QtAssetBrowserAction::Cut;
    asset_browser_->set_paste_available(!asset_clipboard_paths_.empty());
    return;
  }
  case QtAssetBrowserAction::Paste: {
    if (asset_clipboard_generation_ != generation || asset_clipboard_paths_.empty()) return;
    const auto catalog = editor_->project_assets().assets();
    if (!catalog.ok()) { present_lifecycle_result(catalog); return; }
    for (const auto &source : asset_clipboard_sources_) {
      const auto entry = std::ranges::find(catalog.value().assets,
          source.project_relative_path, &QuaderProjectAssetInfo::project_relative_path);
      if (entry == catalog.value().assets.end() || entry->kind != source.kind ||
          entry->asset_id != source.asset_id || entry->scene_id != source.scene_id ||
          entry->content_revision != source.content_revision) {
        asset_clipboard_paths_.clear();
        asset_clipboard_sources_.clear();
        asset_browser_->set_paste_available(false);
        present_status_result({.ok = false, .message = localization_.text(
            "ui.asset_browser.clipboard_changed", "The copied assets changed. Copy them again before pasting.")});
        return;
      }
    }
    if (asset_clipboard_cut_) {
      const auto result = editor_->project_assets().move_assets(asset_clipboard_paths_, folder, project_id, revision);
      if (result.ok()) {
        asset_clipboard_paths_.clear();
        asset_clipboard_sources_.clear();
        asset_browser_->set_paste_available(false);
      }
      finish(result);
    } else finish(editor_->project_assets().copy_assets(asset_clipboard_paths_, folder, project_id, revision));
    return;
  }
  case QtAssetBrowserAction::Move:
    finish(editor_->project_assets().move_assets(paths, folder, project_id, revision));
    return;
  case QtAssetBrowserAction::CopyTo:
    finish(editor_->project_assets().copy_assets(paths, folder, project_id, revision));
    return;
  case QtAssetBrowserAction::Import: {
    std::vector<std::filesystem::path> sources;
    for (const auto &source : request.source_files) sources.emplace_back(source.toStdWString());
    if (sources.empty()) {
      PlatformFileDialogOptions options;
      options.owner_window_handle = native_window_handle();
      options.initial_directory = editor_->project().root();
      if (auto selected = platform_open_texture_files_dialog(options)) {
        sources = std::move(*selected);
      }
    }
    if (still_current() && !sources.empty()) finish(editor_->project_assets().import_textures(sources, folder));
    return;
  }
  case QtAssetBrowserAction::Delete:
    break;
  }

  const auto preview = editor_->project_assets().preview_delete_assets(paths);
  if (!preview.ok() || !preview.value().complete) {
    if (!preview.ok()) present_lifecycle_result(preview);
    else {
      std::string error;
      for (const auto &issue : preview.value().errors) error += issue + "\n";
      present_status_result({.ok = false, .message = std::move(error)});
    }
    return;
  }
  const auto text = [this](std::string_view key, std::string_view fallback) {
    return QString::fromStdString(localization_.text(key, fallback));
  };
  const auto deletion_details = [&](const QuaderProjectAssetDeletionPreview &impact) {
  QStringList details;
  for (const auto &path : impact.paths) details.push_back(QString::fromStdString(path));
  if (!impact.affected_scenes.empty()) {
    details.push_back(text("ui.asset_browser.delete.scene_dependencies", "Material references in these scenes will become Default Material:"));
    for (const auto &scene : impact.affected_scenes) details.push_back(QString::fromStdString(scene.name));
  }
  if (!impact.affected_materials.empty()) {
    details.push_back(text("ui.asset_browser.delete.texture_dependencies", "Texture slots in these materials will be cleared:"));
    for (const auto &material : impact.affected_materials) details.push_back(QString::fromStdString(material.name));
  }
  if (impact.discards_material_draft) details.push_back(text("ui.material_browser.delete.draft", "The open material draft will be closed and its unsaved changes discarded."));
  details.push_back(text("ui.asset_browser.delete.restore", "Assets move to the Recycle Bin. Restoring files does not restore cleared assignments."));
  return details;
  };
  const auto confirm_deletion = [&](const QStringList &details) {
  return material_browser_import_message_box_->present({
      .icon = QMessageBox::Warning,
      .object_name = QStringLiteral("asset_browser_delete_confirmation"),
      .title = text("ui.asset_browser.delete.title", "Delete Assets"),
      .text = text("ui.asset_browser.delete.question", "Move the selected assets to the Recycle Bin?"),
      .informative_text = details.join(QLatin1Char('\n')),
      .buttons = {
          {.button = QtRetainedMessageBoxButton::Primary, .label = text("ui.material_browser.context.delete", "Delete"), .role = QMessageBox::DestructiveRole},
          {.button = QtRetainedMessageBoxButton::Secondary, .label = text("ui.action.cancel", "Cancel"), .role = QMessageBox::RejectRole}},
      .default_button = QtRetainedMessageBoxButton::Secondary,
      .escape_button = QtRetainedMessageBoxButton::Secondary,
      .close_button = QtRetainedMessageBoxButton::Secondary,
  });
  };
  const auto details = deletion_details(preview.value());
  const auto decision = confirm_deletion(details);
  if (decision != QtRetainedMessageBoxButton::Primary || !still_current()) return;
  struct SaveCopy final { SceneSessionId session; ProjectRelativePath path; };
  std::vector<SaveCopy> copies;
  std::vector<SceneSessionId> approved;
  const auto scenes = editor_->scene_documents().open_scenes();
  for (SceneSessionId id : preview.value().dirty_scene_sessions) {
    const auto scene = std::ranges::find(scenes, id, &QuaderSceneDocumentInfo::session_id);
    if (scene == scenes.end()) return;
    const auto answer = unsaved_confirmation_->present({
        .object_name = QStringLiteral("asset_browser_delete_dirty_scene"),
        .title = text("ui.asset_browser.delete.dirty_title", "Unsaved Scene"),
        .message = text("ui.asset_browser.delete.dirty", "Save a copy of \"%1\" before deleting its asset?").arg(QString::fromStdString(scene->name)),
        .save_label = text("ui.asset_browser.save_copy", "Save Copy"),
        .discard_label = text("ui.scene.unsaved.discard", "Discard"),
        .cancel_label = text("ui.action.cancel", "Cancel"),
    });
    if (!still_current() || answer == QtUnsavedConfirmationDecision::Cancel) return;
    if (answer == QtUnsavedConfirmationDecision::Save) {
      const auto selected = choose_project_scene_path(true, &*scene, preview.value().paths);
      if (!selected.path || !still_current()) return;
      const auto relative = ProjectRelativePath::parse(selected.path->lexically_relative(editor_->project().root()));
      if (!relative.ok()) return;
      copies.push_back({id, relative.value()});
    }
    approved.push_back(id);
  }
  const auto reviewed = editor_->project_assets().preview_delete_assets(paths);
  if (!reviewed.ok() || !reviewed.value().complete ||
      reviewed.value().revision != preview.value().revision) {
    present_status_result({.ok = false, .message = localization_.text(
        "ui.asset_browser.delete.changed",
        "The deletion dependencies changed. Review the assets again before deleting.")});
    return;
  }
  bool saved_copy = false;
  for (const auto &copy : copies) {
    const auto result = editor_->scene_documents().save_copy(copy.session, copy.path);
    if (!result.ok()) {
      finish(result);
      if (saved_copy) present_status_result({.ok = false,
          .message = result.error_message() + "\n" + localization_.text(
              "ui.asset_browser.delete.copies_preserved",
              "Deletion failed. The scene copies you saved remain in the project.")});
      return;
    }
    saved_copy = true;
  }
  // Save Copy publishes independent assets and can change the reviewed revision.
  // Reinspect before deletion; any new dirty session requires a fresh prompt.
  const auto current_preview = editor_->project_assets().preview_delete_assets(paths);
  if (!current_preview.ok() || !current_preview.value().complete) {
    if (!current_preview.ok()) present_lifecycle_result(current_preview);
    else {
      std::string message;
      for (const auto &error : current_preview.value().errors) message += error + "\n";
      present_status_result({.ok = false, .message = std::move(message)});
    }
    return;
  }
  const auto updated_details = deletion_details(current_preview.value());
  if (updated_details != details &&
      (confirm_deletion(updated_details) != QtRetainedMessageBoxButton::Primary ||
       !still_current())) return;
  if (std::ranges::any_of(current_preview.value().dirty_scene_sessions,
                         [&](SceneSessionId id) { return std::ranges::find(approved, id) == approved.end(); })) return;
  const auto deleted = editor_->project_assets().delete_assets(
      paths, current_preview.value().revision, approved);
  finish(deleted);
  if (!deleted.ok() && !copies.empty()) {
    present_status_result({.ok = false, .message = deleted.error_message() + "\n" +
        localization_.text("ui.asset_browser.delete.copies_preserved",
            "Deletion failed. The scene copies you saved remain in the project.")});
  }
}

void UISystem::show_asset_texture_info(ProjectAssetId texture_id, bool activate) {
  if (editor_ == nullptr || material_texture_info_window_ == nullptr) return;
  const auto result = editor_->project_assets().texture_info(texture_id);
  if (!result.ok() || !result.value().rgba8) {
    material_texture_info_window_->clear();
    material_texture_info_window_->hide();
    present_lifecycle_result(result);
    return;
  }
  const auto &texture = result.value();
  const auto revision = editor_->project_assets().asset_revision();
  if (!revision.ok()) return;
  if (!material_texture_info_window_->present({
      .context = {.project_identity = std::string(revision.value().project_id.str()),
                  .activation_generation = revision.value().project_generation,
                  .dependency_revision = revision.value().revision,
                  .texture_identity = std::string(texture_id.str())},
      .name = texture.name,
      .location = native_path_text(texture.absolute_path).toStdString(),
      .format = texture.format,
      .width = texture.width,
      .height = texture.height,
      .encoded_byte_size = texture.encoded_size_bytes,
      .source_has_alpha = texture.has_alpha_channel,
      .rgba8 = *texture.rgba8,
      .content_revision = texture.content_revision,
  })) return;
  material_texture_info_catalog_revision_ = material_presentation_catalog_revision_;
  material_texture_info_window_->show();
  if (activate) { material_texture_info_window_->raise(); material_texture_info_window_->activateWindow(); }
}

void UISystem::select_project_material_texture(QtStandardMaterialTextureSlot slot) {
  if (editor_ == nullptr || standard_material_editor_window_ == nullptr) return;
  auto snapshot = asset_browser_snapshot(true);
  const auto generation = snapshot.generation;
  const auto binding = runtime_binding_generation_;
  const auto project_revision = editor_->project_assets().asset_revision();
  if (!project_revision.ok()) return;
  const auto material_session = editor_->standard_material_documents().current_revision();
  if (!material_session.ok()) return;
  QDialog dialog(standard_material_editor_window_);
  dialog.setWindowTitle(QString::fromStdString(localization_.text("ui.asset_browser.select_texture", "Select Texture")));
  auto *layout = new QVBoxLayout(&dialog);
  auto *browser = new QtAssetBrowser(&dialog);
  browser->set_labels(asset_labels(localization_));
  browser->set_picker_mode(true);
  browser->set_type_filter(QtAssetBrowserKind::Texture);
  browser->set_thumbnail_provider(asset_thumbnail_provider_);
  (void)browser->set_snapshot(std::move(snapshot));
  layout->addWidget(browser);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  browser->set_action_callback([&dialog](const auto &request) {
    if (request.action == QtAssetBrowserAction::Open) dialog.accept();
  });
  dialog.resize(ui_scale_controller_->metrics().size(QSize(760, 460)));
  browser->apply_ui_scale(ui_scale_controller_->metrics());
  if (dialog.exec() != QDialog::Accepted) return;
  if (editor_ == nullptr || runtime_binding_generation_ != binding ||
      !editor_->project().has_project()) return;
  const auto current = editor_->project_assets().asset_revision();
  if (!current.ok() || current.value().project_generation != generation ||
      current.value().project_id != project_revision.value().project_id) return;
  const auto current_material = editor_->standard_material_documents().current_revision();
  if (!current_material.ok() ||
      current_material.value().session_id != material_session.value().session_id) return;
  const auto selected = browser->selected_items();
  if (selected.size() != 1u || selected.front().kind != QtAssetBrowserKind::Texture) return;
  const auto id = ProjectAssetId::parse(selected.front().id.toStdString());
  if (!id) return;
  const auto domain_slot = static_cast<QuaderStandardMaterialTextureSlot>(slot);
  const auto result = editor_->standard_material_documents().set_texture(domain_slot, *id);
  present_lifecycle_result(result);
  refresh_standard_material_editor();
}

} // namespace quader
