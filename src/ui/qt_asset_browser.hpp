////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QIcon>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QMimeData;

namespace quader {

/** Public asset categories and navigable physical folders. */
enum class QtAssetBrowserKind : std::uint8_t { Folder, Scene, Material, Texture };
/** Native collection layouts sharing one selection model. */
enum class QtAssetBrowserViewMode : std::uint8_t { Grid, List };

/** Immutable presentation of an owner asset; identity remains opaque to Qt. */
struct QtAssetBrowserItem final {
  QString id;
  QString relative_path;
  QString name;
  QtAssetBrowserKind kind = QtAssetBrowserKind::Scene;
  QIcon thumbnail;
  std::uint64_t content_revision = 0;
  bool editable = true;
};

/** The public asset owner supplies folders and assets in one revisioned view. */
struct QtAssetBrowserSnapshot final {
  std::uint64_t generation = 0;
  std::uint64_t revision = 0;
  std::uint64_t dependency_revision = 0;
  std::vector<QtAssetBrowserItem> items;
};

/** User intent delegated to the project owner; Qt never changes asset files. */
enum class QtAssetBrowserAction : std::uint8_t {
  Open,
  CreateScene,
  CreateMaterial,
  CreateFolder,
  Rename,
  Duplicate,
  Cut,
  Copy,
  Paste,
  Delete,
  Import,
  Move,
  CopyTo,
};

/** Semantic intent fenced to the catalog that supplied its selected items. */
struct QtAssetBrowserActionRequest final {
  QtAssetBrowserAction action = QtAssetBrowserAction::Open;
  std::vector<QtAssetBrowserItem> selected_items;
  QString destination_folder;
  QString proposed_name;
  QStringList source_files;
  std::uint64_t generation = 0;
  std::uint64_t catalog_revision = 0;
  std::uint64_t dependency_revision = 0;
};

/** Decodes exactly one texture dragged by the browser for a material slot. */
[[nodiscard]] bool qt_asset_browser_texture_drag(const QMimeData *data,
                                                QString &texture_id,
                                                std::uint64_t &generation);

/** Localized browser, filter, and context-menu presentation text. */
struct QtAssetBrowserLabels final {
  QString title = QStringLiteral("Asset Browser");
  QString project = QStringLiteral("Project");
  QString search = QStringLiteral("Search assets");
  QString all_types = QStringLiteral("All assets");
  QString scenes = QStringLiteral("Scenes");
  QString materials = QStringLiteral("Materials");
  QString textures = QStringLiteral("Textures");
  QString scene = QStringLiteral("Scene");
  QString material = QStringLiteral("Material");
  QString texture = QStringLiteral("Texture");
  QString folder = QStringLiteral("Folder");
  QString name = QStringLiteral("Name");
  QString type = QStringLiteral("Type");
  QString path = QStringLiteral("Path");
  QString folders = QStringLiteral("Folders");
  QString grid = QStringLiteral("Grid");
  QString list = QStringLiteral("List");
  QString preview_size = QStringLiteral("Preview size");
  QString open = QStringLiteral("Open");
  QString create_scene = QStringLiteral("Create Scene");
  QString create_material = QStringLiteral("Create Material");
  QString new_folder = QStringLiteral("New Folder");
  QString rename = QStringLiteral("Rename");
  QString duplicate = QStringLiteral("Duplicate");
  QString cut = QStringLiteral("Cut");
  QString copy = QStringLiteral("Copy");
  QString paste = QStringLiteral("Paste");
  QString delete_asset = QStringLiteral("Delete");
  QString import = QStringLiteral("Import…");
  QString empty = QStringLiteral("No assets in this folder");
  QString no_results = QStringLiteral("No assets match this search");
};

/** Retained browser presentation. Mutations are exclusively semantic callbacks. */
class QtAssetBrowser final : public QWidget {
public:
  using ActionCallback = std::function<void(QtAssetBrowserActionRequest)>;
  using ThumbnailProvider =
      std::function<QIcon(const QtAssetBrowserItem &, QSize, qreal)>;

  explicit QtAssetBrowser(QWidget *parent = nullptr);
  ~QtAssetBrowser() override;

  void set_labels(QtAssetBrowserLabels labels);
  /** Returns false before model work when all owner stamps are unchanged. */
  bool set_snapshot(QtAssetBrowserSnapshot snapshot);
  [[nodiscard]] const QtAssetBrowserSnapshot &snapshot() const;
  void clear();
  void set_action_callback(ActionCallback callback);
  void set_selection_changed_callback(std::function<void()> callback);
  void set_presentation_state_changed_callback(std::function<void()> callback);
  void set_thumbnail_provider(ThumbnailProvider provider);
  /** Reports painted identities after scroll/filter changes, never a catalog scan. */
  void set_visible_items_changed_callback(
      std::function<void(std::vector<QString>)> callback);
  void invalidate_thumbnail(const QString &id = {});
  void set_material_authoring_available(bool available);
  void set_paste_available(bool available);
  /** Picker mode keeps navigation and folder creation, with no asset mutation. */
  void set_picker_mode(bool picker);
  void set_type_filter(std::optional<QtAssetBrowserKind> kind);
  void set_search_text(QString text);
  void set_selected_folder(QString folder);
  [[nodiscard]] QString selected_folder() const;
  [[nodiscard]] std::vector<QtAssetBrowserItem> selected_items() const;
  [[nodiscard]] bool reveal_item(const QString &id);
  /** Selects and edits the basename; its owner retains the extension. */
  bool begin_rename(const QString &id);
  /** Routes canonical clipboard/rename commands without a private shortcut map. */
  bool handle_action(QtAssetBrowserAction action);
  void focus_browser();
  [[nodiscard]] bool owns_browser_focus() const;
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void restore_presentation_state(QtAssetBrowserViewMode view_mode, int tile_size,
                                  bool sidebar_visible = true,
                                  int sidebar_width = 200);
  [[nodiscard]] QtAssetBrowserViewMode view_mode() const;
  [[nodiscard]] int tile_size() const;
  [[nodiscard]] bool sidebar_visible() const;
  [[nodiscard]] int sidebar_width() const;

protected:
  bool event(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
