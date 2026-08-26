////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_material_presentation.hpp"

#include <QAbstractTableModel>
#include <QIcon>
#include <QMainWindow>
#include <QSortFilterProxyModel>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

class QAction;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QPushButton;
class QShowEvent;
class QSlider;
class QStackedWidget;
class QTableView;
class QToolButton;

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
  QString file = QStringLiteral("File");
  QString import_textures = QStringLiteral("Import…");
  QString name_column = QStringLiteral("Name");
  QString type_column = QStringLiteral("Type");
  QString size_column = QStringLiteral("Size");
  QString material_type = QStringLiteral("Standard Material");
  QString empty_catalog = QStringLiteral("No materials in this project");
  QString no_results = QStringLiteral("No materials match this search");
  QString accept = QStringLiteral("Accept");
  QString picker_window_title = QStringLiteral("Replace Material Browser");
};

enum class QtMaterialBrowserViewMode : std::uint8_t { Grid, List };
enum class QtMaterialBrowserPurpose : std::uint8_t {
  ActiveMaterial,
  Picker,
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

  bool set_items(std::vector<QtMaterialBrowserItem> items);
  void set_labels(QtMaterialBrowserLabels labels);
  [[nodiscard]] const std::vector<QtMaterialBrowserItem> &items() const {
    return items_;
  }

private:
  [[nodiscard]] QIcon preview_icon(int row) const;

  std::vector<QtMaterialBrowserItem> items_;
  QtMaterialBrowserLabels labels_;
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
 * emits only semantic material activation, opening, and import intent.
 */
class QtMaterialBrowserWindow final : public QMainWindow {
public:
  using ActivateMaterialCallback =
      std::function<void(QuaderMaterialReference)>;
  using OpenMaterialCallback = std::function<void(QuaderMaterialReference)>;
  using AcceptMaterialCallback = std::function<void(QuaderMaterialReference)>;
  using ImportTexturesCallback = std::function<void()>;
  using PresentationStateChangedCallback = std::function<void()>;

  explicit QtMaterialBrowserWindow(QWidget *parent = nullptr);
  ~QtMaterialBrowserWindow() override;

  QtMaterialBrowserWindow(const QtMaterialBrowserWindow &) = delete;
  QtMaterialBrowserWindow &operator=(const QtMaterialBrowserWindow &) = delete;

  void set_labels(QtMaterialBrowserLabels labels);
  void set_materials(std::vector<QtMaterialBrowserItem> materials,
                     QuaderMaterialReference selected_material = {});
  void set_active_material(QuaderMaterialReference active_material);
  void clear_materials();
  void set_purpose(QtMaterialBrowserPurpose purpose);
  [[nodiscard]] QtMaterialBrowserPurpose purpose() const noexcept {
    return purpose_;
  }
  [[nodiscard]] QuaderMaterialReference selected_material() const;
  void set_activate_material_callback(ActivateMaterialCallback callback);
  void set_open_material_callback(OpenMaterialCallback callback);
  void set_accept_material_callback(AcceptMaterialCallback callback);
  void set_import_textures_callback(ImportTexturesCallback callback);
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
  void update_tile_size_text();
  void refresh_content_page();
  void update_purpose_presentation();
  void update_accept_enabled();
  void select_material(const QuaderMaterialReference &reference);
  [[nodiscard]] const QtMaterialBrowserItem *
  item_for_index(const QModelIndex &proxy_index) const;
  void activate_index(const QModelIndex &index);
  void open_index(const QModelIndex &index);
  void accept_selected_material();

  QtMaterialBrowserModel *model_ = nullptr;
  QtMaterialBrowserProxyModel *proxy_ = nullptr;
  QLineEdit *search_ = nullptr;
  QToolButton *grid_button_ = nullptr;
  QToolButton *list_button_ = nullptr;
  QSlider *tile_slider_ = nullptr;
  QMenu *file_menu_ = nullptr;
  QAction *import_action_ = nullptr;
  QStackedWidget *content_stack_ = nullptr;
  QListView *grid_view_ = nullptr;
  QTableView *list_view_ = nullptr;
  QLabel *empty_label_ = nullptr;
  QPushButton *accept_button_ = nullptr;
  QtMaterialBrowserLabels labels_;
  QtMaterialBrowserViewMode view_mode_ = QtMaterialBrowserViewMode::Grid;
  QtMaterialBrowserPurpose purpose_ =
      QtMaterialBrowserPurpose::ActiveMaterial;
  ActivateMaterialCallback activate_material_;
  OpenMaterialCallback open_material_;
  AcceptMaterialCallback accept_material_;
  ImportTexturesCallback import_textures_;
  PresentationStateChangedCallback presentation_state_changed_;
};

} // namespace quader
