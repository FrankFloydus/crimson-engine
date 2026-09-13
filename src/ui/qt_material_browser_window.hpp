////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_material_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAbstractTableModel>
#include <QIcon>
#include <QMainWindow>
#include <QPointer>
#include <QSize>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QtTypes>

#include <cstdint>
#include <functional>
#include <vector>

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QMimeData;
class QPushButton;
class QShowEvent;
class QSlider;
class QStackedWidget;
class QSplitter;
class QTableView;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace quader {

using QtMaterialBrowserItem = QtMaterialPresentationItem;

/// Localized text used by the material-browser presentation.
struct QtMaterialBrowserLabels final {
  QString window_title = QStringLiteral("Material Browser");
  QString search_placeholder = QStringLiteral("Search materials");
  QString grid = QStringLiteral("Grid");
  QString list = QStringLiteral("List");
  QString tile_size = QStringLiteral("Preview size");
  QString tile_size_value = QStringLiteral("Preview size: %1 px");
  QString import_textures = QStringLiteral("Import…");
  QString name_column = QStringLiteral("Name");
  QString type_column = QStringLiteral("Type");
  QString size_column = QStringLiteral("Size");
  QString material_type = QStringLiteral("Standard Material");
  QString empty_catalog = QStringLiteral("No materials in this project");
  QString no_results = QStringLiteral("No materials match this search");
  QString accept = QStringLiteral("Accept");
  QString assign = QStringLiteral("Assign");
  QString picker_window_title = QStringLiteral("Replace Material Browser");
  QString folders = QStringLiteral("Folders");
  QString all = QStringLiteral("All");
  QString new_folder = QStringLiteral("New Folder");
  QString rename_folder = QStringLiteral("Rename");
  QString delete_folder = QStringLiteral("Delete");
  QString folder_name = QStringLiteral("Folder name");
  QString empty_folder = QStringLiteral("No materials in this folder");
  QString filter = QStringLiteral("Filter materials");
  QString filter_title = QStringLiteral("Filter Materials");
  QString filter_used = QStringLiteral("Used");
  QString filter_not_used = QStringLiteral("Not Used");
  QString filter_clear = QStringLiteral("Clear");
  QString filter_apply = QStringLiteral("Apply");
  QString filter_cancel = QStringLiteral("Cancel");
  QString filtered_empty =
      QStringLiteral("No materials match this usage filter");
  QString usage_inspection_unavailable =
      QStringLiteral("Material usage is currently unavailable");
  QString copy_texture_name = QStringLiteral("Copy Texture Name");
  QString copy_texture_path = QStringLiteral("Copy Texture Path");
  QString open_file_location = QStringLiteral("Open File Location");
  QString show_texture_info = QStringLiteral("Show Texture Info");
  QString rename_material = QStringLiteral("Rename");
  QString duplicate_material = QStringLiteral("Duplicate");
  QString cut_material = QStringLiteral("Cut");
  QString copy_material = QStringLiteral("Copy");
  QString paste_material = QStringLiteral("Paste");
  QString delete_material = QStringLiteral("Delete");
  QString rename_material_title = QStringLiteral("Rename Material");
  QString material_name = QStringLiteral("Material name");
  QString edit_material = QStringLiteral("Edit Material");
  QString usage_incomplete =
      QStringLiteral("Material usage is incomplete; Not Used is unavailable.");
  QString clear_usage_filter = QStringLiteral("Clear usage filter");
  QString filtered_by = QStringLiteral("Filtered by:");
  QString filter_active = QStringLiteral("%1 (1 active)");
  QString remove_usage_filter = QStringLiteral("Remove %1 filter");
};

enum class QtMaterialBrowserViewMode : std::uint8_t { Grid, List };
enum class QtMaterialBrowserUsageMode : std::uint8_t { All, Used, NotUsed };
/** Chooses window chrome without changing the shared browser presentation. */
enum class QtMaterialBrowserHost : std::uint8_t { Windowed, Embedded };
/** Separates local browsing, explicit face assignment, and modal picking. */
enum class QtMaterialBrowserPurpose : std::uint8_t {
  Management,
  FaceAssignment,
  Picker,
};

/** Immutable project material usage supplied only when its revision changes. */
struct QtMaterialBrowserUsageSnapshot final {
  std::uint64_t project_generation = 0;
  std::uint64_t revision = 0;
  bool complete = false;
  std::vector<QuaderMaterialReference> used_materials;
  QStringList errors;

  bool operator==(const QtMaterialBrowserUsageSnapshot &) const = default;
};

/** Enumerates semantic material and texture commands exposed by the card menu. */
enum class QtMaterialBrowserAction : std::uint8_t {
  CopyTextureName,
  CopyTexturePath,
  OpenFileLocation,
  ShowTextureInfo,
  Rename,
  Duplicate,
  Cut,
  Copy,
  Paste,
  Delete,
};

/** Stable semantic intent emitted after a context-menu nested-loop fence. */
struct QtMaterialBrowserActionRequest final {
  QtMaterialBrowserAction action = QtMaterialBrowserAction::CopyTextureName;
  QuaderMaterialReference target;
  std::vector<QuaderMaterialReference> selected_materials;
  QString destination_folder = QStringLiteral("Materials");
  QString proposed_name;
  std::uint64_t project_generation = 0;
  std::uint64_t dependency_revision = 0;
  std::uint64_t presentation_revision = 0;
};

/// Shared source model used by both material-browser views.
class QtMaterialBrowserModel final : public QAbstractTableModel {
public:
  enum Column : int { NameColumn = 0, TypeColumn, SizeColumn, ColumnCount };
  /// Supplies shared identity, filtering, sorting, and tooltip data.
  enum Role : int {
    StableIdRole = Qt::UserRole + 1,
    SearchTextRole,
    SizeBytesRole,
    RelativePathRole,
  };

  explicit QtMaterialBrowserModel(QObject *parent = nullptr);

  [[nodiscard]] int
  rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] int
  columnCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant
  headerData(int section, Qt::Orientation orientation,
             int role = Qt::DisplayRole) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;
  [[nodiscard]] QStringList mimeTypes() const override;
  [[nodiscard]] QMimeData *mimeData(const QModelIndexList &indexes) const override;
  [[nodiscard]] Qt::DropActions supportedDragActions() const override;
  void set_drag_context(std::uint64_t generation, bool enabled);

  bool set_items(std::vector<QtMaterialBrowserItem> items);
  void set_labels(QtMaterialBrowserLabels labels);
  /** Selects the logical tile extent and backing-pixel scale for icons. */
  bool set_preview_raster_context(QSize logical_bounds,
                                  qreal device_pixel_ratio);
  void invalidate_preview_rasters();
  [[nodiscard]] const std::vector<QtMaterialBrowserItem> &items() const {
    return items_;
  }

private:
  /// One size/DPR-specific icon retained under the browser's bounded LRU.
  struct PreviewIconCacheEntry final {
    int row = -1;
    QIcon icon;
    std::uint64_t cost_bytes = 0;
    std::uint64_t last_use = 0;
  };

  [[nodiscard]] QIcon preview_icon(int row) const;
  void clear_preview_icon_cache() const;

  std::vector<QtMaterialBrowserItem> items_;
  QtMaterialBrowserLabels labels_;
  QSize preview_logical_bounds_{128, 128};
  qreal preview_device_pixel_ratio_ = 1.0;
  mutable std::vector<PreviewIconCacheEntry> preview_icon_cache_;
  mutable std::uint64_t preview_icon_cache_cost_ = 0;
  mutable std::uint64_t preview_icon_cache_clock_ = 0;
  std::uint64_t project_generation_ = 0;
  bool organization_enabled_ = true;
};

/// Case-insensitive name/path filter with deterministic typed sorting.
class QtMaterialBrowserProxyModel final : public QSortFilterProxyModel {
public:
  explicit QtMaterialBrowserProxyModel(QObject *parent = nullptr);

  void set_search_text(QString text);
  void set_folder(QString project_relative_folder);
  void set_usage_mode(QtMaterialBrowserUsageMode mode);
  void set_used_material_ids(std::vector<QString> stable_ids, bool complete);
  [[nodiscard]] QtMaterialBrowserUsageMode usage_mode() const noexcept {
    return usage_mode_;
  }
  [[nodiscard]] bool usage_complete() const noexcept {
    return usage_complete_;
  }
  [[nodiscard]] bool has_non_usage_match() const;

protected:
  [[nodiscard]] bool
  filterAcceptsRow(int source_row,
                   const QModelIndex &source_parent) const override;
  [[nodiscard]] bool lessThan(const QModelIndex &source_left,
                              const QModelIndex &source_right) const override;

private:
  [[nodiscard]] bool accepts_row(int source_row,
                                 const QModelIndex &source_parent,
                                 bool include_usage) const;

  QString search_text_;
  QString folder_;
  std::vector<QString> used_material_ids_;
  QtMaterialBrowserUsageMode usage_mode_ = QtMaterialBrowserUsageMode::All;
  bool usage_complete_ = false;
};

/**
 * Retained stock-Qt material browser. It presents immutable API snapshots and
 * emits only semantic material assignment, opening, and import intent.
 */
class QtMaterialBrowserWindow final : public QMainWindow {
public:
  using AssignMaterialCallback =
      std::function<void(QuaderMaterialReference)>;
  using OpenMaterialCallback = std::function<void(QuaderMaterialReference)>;
  using AcceptMaterialCallback = std::function<void(QuaderMaterialReference)>;
  using ImportTexturesCallback = std::function<void(QString)>;
  using CreateFolderCallback = std::function<void(QString, QString)>;
  using RenameFolderCallback = std::function<void(QString, QString)>;
  using DeleteFolderCallback = std::function<void(QString)>;
  using MoveFolderCallback =
      std::function<void(QString, QString, std::uint64_t)>;
  using MoveMaterialsCallback = std::function<void(
      std::vector<QuaderMaterialReference>, QString, std::uint64_t)>;
  using MaterialActionCallback =
      std::function<void(QtMaterialBrowserActionRequest)>;
  using PresentationStateChangedCallback = std::function<void()>;

  explicit QtMaterialBrowserWindow(
      QWidget *parent = nullptr,
      QtMaterialBrowserHost host = QtMaterialBrowserHost::Windowed);
  ~QtMaterialBrowserWindow() override;

  QtMaterialBrowserWindow(const QtMaterialBrowserWindow &) = delete;
  QtMaterialBrowserWindow &operator=(const QtMaterialBrowserWindow &) = delete;

  void set_labels(QtMaterialBrowserLabels labels);
  void set_materials(std::vector<QtMaterialBrowserItem> materials,
                     QuaderMaterialReference selected_material = {});
  void set_selected_material(QuaderMaterialReference material);
  void set_assignment_available(bool available);
  [[nodiscard]] QtMaterialBrowserHost host() const noexcept { return host_; }
  void set_folders(std::vector<QString> project_relative_folders);
  void set_project_generation(std::uint64_t generation);
  void set_material_usage(QtMaterialBrowserUsageSnapshot snapshot);
  void set_dependency_revision(std::uint64_t revision);
  [[nodiscard]] QtMaterialBrowserUsageMode usage_mode() const noexcept {
    return usage_mode_;
  }
  void set_protected_material(QuaderMaterialReference material);
  void set_material_paste_available(bool available);
  [[nodiscard]] QString selected_folder() const;
  void set_selected_folder(QString project_relative_folder);
  void follow_moved_folder(const QString &source, const QString &destination);
  void clear_materials();
  void set_purpose(QtMaterialBrowserPurpose purpose);
  [[nodiscard]] QtMaterialBrowserPurpose purpose() const noexcept {
    return purpose_;
  }
  [[nodiscard]] QuaderMaterialReference selected_material() const;
  void set_assign_material_callback(AssignMaterialCallback callback);
  void set_open_material_callback(OpenMaterialCallback callback);
  void set_accept_material_callback(AcceptMaterialCallback callback);
  void set_import_textures_callback(ImportTexturesCallback callback);
  void set_create_folder_callback(CreateFolderCallback callback);
  void set_rename_folder_callback(RenameFolderCallback callback);
  void set_delete_folder_callback(DeleteFolderCallback callback);
  void set_move_folder_callback(MoveFolderCallback callback);
  void set_move_materials_callback(MoveMaterialsCallback callback);
  void set_material_action_callback(MaterialActionCallback callback);
  void set_presentation_state_changed_callback(
      PresentationStateChangedCallback callback);

  /// Reapplies explicit retained metrics from their authored values.
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  void restore_presentation_state(QtMaterialBrowserViewMode view_mode,
                                  int tile_size, bool sidebar_visible = true,
                                  int sidebar_width = 200);
  [[nodiscard]] QtMaterialBrowserViewMode view_mode() const;
  [[nodiscard]] int tile_size() const;
  [[nodiscard]] bool sidebar_visible() const;
  [[nodiscard]] int sidebar_width() const;

protected:
  bool event(QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  void set_view_mode(QtMaterialBrowserViewMode mode, bool notify);
  void set_tile_size(int tile_size);
  void apply_tile_metrics(int tile_size);
  void refresh_preview_raster_context(bool force_refresh = false);
  void refresh_view_mode_icons();
  void update_tile_size_text();
  void refresh_content_page();
  void update_purpose_presentation();
  void update_accept_enabled();
  void select_material(const QuaderMaterialReference &reference);
  [[nodiscard]] const QtMaterialBrowserItem *
  item_for_index(const QModelIndex &proxy_index) const;
  void assign_index(const QModelIndex &index);
  void open_index(const QModelIndex &index);
  void accept_selected_material();
  void rebuild_folder_tree();
  void show_folder_menu(const QPoint &position);
  void show_filter_popup();
  void show_thumbnail_size_popup();
  void refresh_usage_presentation();
  void refresh_usage_badge_style();
  void show_material_menu(const QModelIndex &target,
                          const QPoint &global_position);
  void set_sidebar_visible(bool visible, bool notify);
  void apply_usage_mode(QtMaterialBrowserUsageMode mode);
  [[nodiscard]] std::vector<QuaderMaterialReference>
  selected_materials() const;
  [[nodiscard]] QStringList selected_material_ids() const;
  [[nodiscard]] bool is_protected_material(
      const QtMaterialBrowserItem &item) const;
  [[nodiscard]] bool receive_folder_drop(const QMimeData *data,
                                         const QString &destination,
                                         bool execute);

  QtMaterialBrowserModel *model_ = nullptr;
  QtMaterialBrowserProxyModel *proxy_ = nullptr;
  QLineEdit *search_ = nullptr;
  QToolButton *filter_button_ = nullptr;
  QPointer<QWidget> filter_popup_;
  QPointer<QWidget> thumbnail_size_popup_;
  QToolButton *thumbnail_size_button_ = nullptr;
  QToolButton *import_tool_button_ = nullptr;
  QLabel *thumbnail_size_value_ = nullptr;
  QWidget *active_filters_ = nullptr;
  QLabel *filtered_by_ = nullptr;
  QWidget *usage_chip_ = nullptr;
  QLabel *usage_chip_label_ = nullptr;
  QToolButton *usage_chip_remove_ = nullptr;
  QLabel *usage_status_ = nullptr;
  QToolButton *grid_button_ = nullptr;
  QToolButton *list_button_ = nullptr;
  QSlider *tile_slider_ = nullptr;
  QToolButton *folders_button_ = nullptr;
  QSplitter *splitter_ = nullptr;
  QTreeWidget *folder_tree_ = nullptr;
  QPushButton *import_button_ = nullptr;
  QStackedWidget *content_stack_ = nullptr;
  QListView *grid_view_ = nullptr;
  QTableView *list_view_ = nullptr;
  QLabel *empty_label_ = nullptr;
  QPushButton *accept_button_ = nullptr;
  QtMaterialBrowserLabels labels_;
  QtMaterialBrowserViewMode view_mode_ = QtMaterialBrowserViewMode::Grid;
  QtMaterialBrowserHost host_ = QtMaterialBrowserHost::Windowed;
  QtMaterialBrowserPurpose purpose_ = QtMaterialBrowserPurpose::Management;
  bool assignment_available_ = false;
  AssignMaterialCallback assign_material_;
  OpenMaterialCallback open_material_;
  AcceptMaterialCallback accept_material_;
  ImportTexturesCallback import_textures_;
  CreateFolderCallback create_folder_;
  RenameFolderCallback rename_folder_;
  DeleteFolderCallback delete_folder_;
  MoveFolderCallback move_folder_;
  MoveMaterialsCallback move_materials_;
  MaterialActionCallback material_action_;
  std::vector<QString> folders_;
  QtMaterialBrowserUsageSnapshot usage_snapshot_;
  QuaderMaterialReference protected_material_;
  QtMaterialBrowserUsageMode usage_mode_ = QtMaterialBrowserUsageMode::All;
  std::uint64_t project_generation_ = 0;
  std::uint64_t material_presentation_revision_ = 0;
  std::uint64_t dependency_revision_ = 0;
  std::uint64_t action_eligibility_revision_ = 0;
  int sidebar_width_ = 200;
  bool material_paste_available_ = false;
  PresentationStateChangedCallback presentation_state_changed_;
  QtUiScaleMetrics ui_scale_metrics_;
};

} // namespace quader
