////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QSortFilterProxyModel>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <vector>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QListView;
class QShowEvent;
class QSlider;
class QStackedWidget;
class QTableView;
class QToolButton;

namespace quader {

/// Immutable project-material presentation data supplied by the public API.
struct QtMaterialBrowserItem final {
  QString stable_id;
  QString name;
  QString project_relative_path;
  QString type_display_name;
  qint64 qmat_size_bytes = 0;
  QByteArray encoded_albedo;
  QColor fallback_color = Qt::white;
  std::uint64_t content_revision = 0;

  bool operator==(const QtMaterialBrowserItem &) const = default;
};

/// Localized text used by the material-browser presentation.
struct QtMaterialBrowserLabels final {
  QString window_title = QStringLiteral("Material Browser");
  QString search_placeholder = QStringLiteral("Search materials");
  QString grid = QStringLiteral("Grid");
  QString list = QStringLiteral("List");
  QString tile_size = QStringLiteral("Preview size");
  QString name_column = QStringLiteral("Name");
  QString type_column = QStringLiteral("Type");
  QString size_column = QStringLiteral("Size");
  QString material_type = QStringLiteral("Standard Material");
  QString empty_catalog = QStringLiteral("No materials in this project");
  QString no_results = QStringLiteral("No materials match this search");
};

enum class QtMaterialBrowserViewMode : std::uint8_t { Grid, List };

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

  bool set_items(std::vector<QtMaterialBrowserItem> items);
  void set_labels(QtMaterialBrowserLabels labels);
  [[nodiscard]] const std::vector<QtMaterialBrowserItem> &items() const {
    return items_;
  }

private:
  [[nodiscard]] QIcon preview_icon(int row) const;

  std::vector<QtMaterialBrowserItem> items_;
  QtMaterialBrowserLabels labels_;
  mutable std::vector<QIcon> preview_icons_;
  mutable std::vector<bool> preview_icons_ready_;
};

/// Case-insensitive name/path filter with deterministic typed sorting.
class QtMaterialBrowserProxyModel final : public QSortFilterProxyModel {
public:
  explicit QtMaterialBrowserProxyModel(QObject *parent = nullptr);

  void set_search_text(QString text);

protected:
  [[nodiscard]] bool
  filterAcceptsRow(int source_row,
                   const QModelIndex &source_parent) const override;
  [[nodiscard]] bool lessThan(const QModelIndex &source_left,
                              const QModelIndex &source_right) const override;

private:
  QString search_text_;
};

/**
 * Retained stock-Qt material browser. It presents immutable API snapshots and
 * emits only stable material-ID activation intent.
 */
class QtMaterialBrowserWindow final : public QWidget {
public:
  using ActivateMaterialCallback = std::function<void(QString)>;
  using OpenMaterialCallback = std::function<void(QString)>;
  using PresentationStateChangedCallback = std::function<void()>;

  explicit QtMaterialBrowserWindow(QWidget *parent = nullptr);
  ~QtMaterialBrowserWindow() override;

  QtMaterialBrowserWindow(const QtMaterialBrowserWindow &) = delete;
  QtMaterialBrowserWindow &operator=(const QtMaterialBrowserWindow &) = delete;

  void set_labels(QtMaterialBrowserLabels labels);
  void set_materials(std::vector<QtMaterialBrowserItem> materials,
                     QString active_material_id = {});
  void set_active_material(QString active_material_id);
  void clear_materials();
  void set_activate_material_callback(ActivateMaterialCallback callback);
  void set_open_material_callback(OpenMaterialCallback callback);
  void set_presentation_state_changed_callback(
      PresentationStateChangedCallback callback);

  void restore_presentation_state(QtMaterialBrowserViewMode view_mode,
                                  int tile_size);
  [[nodiscard]] QtMaterialBrowserViewMode view_mode() const;
  [[nodiscard]] int tile_size() const;

protected:
  void closeEvent(QCloseEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  void set_view_mode(QtMaterialBrowserViewMode mode, bool notify);
  void set_tile_size(int tile_size);
  void refresh_content_page();
  void select_material(const QString &stable_id);
  void activate_index(const QModelIndex &index);
  void open_index(const QModelIndex &index);

  QtMaterialBrowserModel *model_ = nullptr;
  QtMaterialBrowserProxyModel *proxy_ = nullptr;
  QLineEdit *search_ = nullptr;
  QToolButton *grid_button_ = nullptr;
  QToolButton *list_button_ = nullptr;
  QSlider *tile_slider_ = nullptr;
  QStackedWidget *content_stack_ = nullptr;
  QListView *grid_view_ = nullptr;
  QTableView *list_view_ = nullptr;
  QLabel *empty_label_ = nullptr;
  QtMaterialBrowserLabels labels_;
  QtMaterialBrowserViewMode view_mode_ = QtMaterialBrowserViewMode::Grid;
  ActivateMaterialCallback activate_material_;
  OpenMaterialCallback open_material_;
  PresentationStateChangedCallback presentation_state_changed_;
};

} // namespace quader
