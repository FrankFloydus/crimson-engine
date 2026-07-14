////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_browser_window.hpp"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QPixmap>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace quader {
namespace {

constexpr int kMinimumTileSize = 64;
constexpr int kMaximumTileSize = 256;
constexpr int kDefaultTileSize = 128;
constexpr int kTileSizeStep = 16;
constexpr int kGridLabelHeight = 34;
constexpr int kGridSpacing = 8;

QString normalized_search_text(QString text) {
  return text.trimmed().toCaseFolded();
}

} // namespace

QtMaterialBrowserModel::QtMaterialBrowserModel(QObject *parent)
    : QAbstractTableModel(parent) {}

int QtMaterialBrowserModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int QtMaterialBrowserModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant QtMaterialBrowserModel::data(const QModelIndex &index,
                                      int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(items_.size())) {
    return {};
  }
  const QtMaterialBrowserItem &item =
      items_[static_cast<std::size_t>(index.row())];
  if (role == StableIdRole) {
    return item.stable_id;
  }
  if (role == SearchTextRole) {
    return item.name + QLatin1Char('\n') + item.project_relative_path;
  }
  if (role == SizeBytesRole) {
    return item.qmat_size_bytes;
  }
  if (role == RelativePathRole || role == Qt::ToolTipRole) {
    return item.project_relative_path;
  }
  if (role == Qt::DecorationRole && index.column() == NameColumn) {
    return preview_icon(index.row());
  }
  if (role == Qt::TextAlignmentRole && index.column() == SizeColumn) {
    return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
  }
  if (role != Qt::DisplayRole) {
    return {};
  }
  switch (index.column()) {
  case NameColumn:
    return item.name;
  case TypeColumn:
    return item.type_display_name.isEmpty() ? labels_.material_type
                                            : item.type_display_name;
  case SizeColumn:
    return QLocale().formattedDataSize(
        std::max<qint64>(0, item.qmat_size_bytes), 1,
        QLocale::DataSizeIecFormat);
  default:
    return {};
  }
}

QVariant QtMaterialBrowserModel::headerData(int section,
                                            Qt::Orientation orientation,
                                            int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  switch (section) {
  case NameColumn:
    return labels_.name_column;
  case TypeColumn:
    return labels_.type_column;
  case SizeColumn:
    return labels_.size_column;
  default:
    return {};
  }
}

bool QtMaterialBrowserModel::set_items(
    std::vector<QtMaterialBrowserItem> items) {
  if (items_ == items) {
    return false;
  }
  beginResetModel();
  items_ = std::move(items);
  preview_icons_.assign(items_.size(), {});
  preview_icons_ready_.assign(items_.size(), false);
  endResetModel();
  return true;
}

void QtMaterialBrowserModel::set_labels(QtMaterialBrowserLabels labels) {
  labels_ = std::move(labels);
  if (rowCount() > 0) {
    emit dataChanged(index(0, TypeColumn),
                     index(rowCount() - 1, SizeColumn),
                     {Qt::DisplayRole});
  }
  emit headerDataChanged(Qt::Horizontal, NameColumn, SizeColumn);
}

QIcon QtMaterialBrowserModel::preview_icon(int row) const {
  if (row < 0 || row >= static_cast<int>(items_.size())) {
    return {};
  }
  const std::size_t offset = static_cast<std::size_t>(row);
  if (preview_icons_ready_[offset]) {
    return preview_icons_[offset];
  }

  const QtMaterialBrowserItem &item = items_[offset];
  QImage image;
  if (!item.encoded_albedo.isEmpty()) {
    (void)image.loadFromData(item.encoded_albedo);
  }
  if (image.isNull()) {
    image = QImage(96, 96, QImage::Format_ARGB32_Premultiplied);
    image.fill(item.fallback_color);
  }
  preview_icons_[offset] = QIcon(QPixmap::fromImage(std::move(image)));
  preview_icons_ready_[offset] = true;
  return preview_icons_[offset];
}

QtMaterialBrowserProxyModel::QtMaterialBrowserProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
  setSortCaseSensitivity(Qt::CaseInsensitive);
}

void QtMaterialBrowserProxyModel::set_search_text(QString text) {
  text = normalized_search_text(std::move(text));
  if (search_text_ == text) {
    return;
  }
  search_text_ = std::move(text);
  invalidateRowsFilter();
}

bool QtMaterialBrowserProxyModel::filterAcceptsRow(
    int source_row, const QModelIndex &source_parent) const {
  if (search_text_.isEmpty()) {
    return true;
  }
  const QModelIndex source_index =
      sourceModel()->index(source_row, QtMaterialBrowserModel::NameColumn,
                           source_parent);
  return sourceModel()
      ->data(source_index, QtMaterialBrowserModel::SearchTextRole)
      .toString()
      .toCaseFolded()
      .contains(search_text_);
}

bool QtMaterialBrowserProxyModel::lessThan(
    const QModelIndex &source_left,
    const QModelIndex &source_right) const {
  const auto compare_name_then_path = [this](const QModelIndex &left,
                                              const QModelIndex &right) {
    const QModelIndex left_name = left.siblingAtColumn(
        QtMaterialBrowserModel::NameColumn);
    const QModelIndex right_name = right.siblingAtColumn(
        QtMaterialBrowserModel::NameColumn);
    const int name_order = QString::compare(
        sourceModel()->data(left_name).toString(),
        sourceModel()->data(right_name).toString(), Qt::CaseInsensitive);
    if (name_order != 0) {
      return name_order < 0;
    }
    return QString::compare(
               sourceModel()
                   ->data(left_name,
                          QtMaterialBrowserModel::RelativePathRole)
                   .toString(),
               sourceModel()
                   ->data(right_name,
                          QtMaterialBrowserModel::RelativePathRole)
                   .toString(),
               Qt::CaseInsensitive) < 0;
  };
  if (source_left.column() == QtMaterialBrowserModel::SizeColumn &&
      source_right.column() == QtMaterialBrowserModel::SizeColumn) {
    const qint64 left_size =
        sourceModel()
            ->data(source_left, QtMaterialBrowserModel::SizeBytesRole)
            .toLongLong();
    const qint64 right_size =
        sourceModel()
            ->data(source_right, QtMaterialBrowserModel::SizeBytesRole)
            .toLongLong();
    if (left_size != right_size) {
      return left_size < right_size;
    }
  }
  return compare_name_then_path(source_left, source_right);
}

QtMaterialBrowserWindow::QtMaterialBrowserWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool) {
  setObjectName(QStringLiteral("materialBrowser.window"));
  setAttribute(Qt::WA_DeleteOnClose, false);
  resize(760, 520);

  auto *root_layout = new QVBoxLayout(this);
  auto *toolbar = new QHBoxLayout();
  search_ = new QLineEdit(this);
  search_->setObjectName(QStringLiteral("materialBrowser.search"));
  search_->setClearButtonEnabled(true);
  toolbar->addWidget(search_, 1);

  grid_button_ = new QToolButton(this);
  grid_button_->setObjectName(QStringLiteral("materialBrowser.grid"));
  grid_button_->setCheckable(true);
  grid_button_->setIcon(style()->standardIcon(QStyle::SP_FileDialogListView));
  grid_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->addWidget(grid_button_);
  list_button_ = new QToolButton(this);
  list_button_->setObjectName(QStringLiteral("materialBrowser.list"));
  list_button_->setCheckable(true);
  list_button_->setIcon(
      style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  list_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->addWidget(list_button_);
  auto *view_group = new QButtonGroup(this);
  view_group->setExclusive(true);
  view_group->addButton(grid_button_);
  view_group->addButton(list_button_);

  tile_slider_ = new QSlider(Qt::Horizontal, this);
  tile_slider_->setObjectName(QStringLiteral("materialBrowser.tileSize"));
  tile_slider_->setRange(kMinimumTileSize, kMaximumTileSize);
  tile_slider_->setSingleStep(kTileSizeStep);
  tile_slider_->setPageStep(kTileSizeStep);
  tile_slider_->setValue(kDefaultTileSize);
  tile_slider_->setFixedWidth(150);
  toolbar->addWidget(tile_slider_);
  root_layout->addLayout(toolbar);

  model_ = new QtMaterialBrowserModel(this);
  proxy_ = new QtMaterialBrowserProxyModel(this);
  proxy_->setSourceModel(model_);
  proxy_->sort(QtMaterialBrowserModel::NameColumn, Qt::AscendingOrder);
  auto *selection = new QItemSelectionModel(proxy_, this);

  content_stack_ = new QStackedWidget(this);
  content_stack_->setObjectName(QStringLiteral("materialBrowser.content"));
  grid_view_ = new QListView(content_stack_);
  grid_view_->setObjectName(QStringLiteral("materialBrowser.gridView"));
  grid_view_->setModel(proxy_);
  grid_view_->setSelectionModel(selection);
  grid_view_->setViewMode(QListView::IconMode);
  grid_view_->setResizeMode(QListView::Adjust);
  grid_view_->setMovement(QListView::Static);
  grid_view_->setWrapping(true);
  grid_view_->setUniformItemSizes(true);
  grid_view_->setWordWrap(true);
  grid_view_->setSpacing(kGridSpacing);
  grid_view_->setSelectionMode(QAbstractItemView::SingleSelection);
  content_stack_->addWidget(grid_view_);

  list_view_ = new QTableView(content_stack_);
  list_view_->setObjectName(QStringLiteral("materialBrowser.listView"));
  list_view_->setModel(proxy_);
  list_view_->setSelectionModel(selection);
  list_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  list_view_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_view_->setSortingEnabled(true);
  list_view_->sortByColumn(QtMaterialBrowserModel::NameColumn,
                           Qt::AscendingOrder);
  list_view_->verticalHeader()->hide();
  list_view_->horizontalHeader()->setStretchLastSection(false);
  list_view_->horizontalHeader()->setSectionResizeMode(
      QtMaterialBrowserModel::NameColumn, QHeaderView::Stretch);
  list_view_->horizontalHeader()->setSectionResizeMode(
      QtMaterialBrowserModel::TypeColumn, QHeaderView::ResizeToContents);
  list_view_->horizontalHeader()->setSectionResizeMode(
      QtMaterialBrowserModel::SizeColumn, QHeaderView::ResizeToContents);
  content_stack_->addWidget(list_view_);

  empty_label_ = new QLabel(content_stack_);
  empty_label_->setObjectName(QStringLiteral("materialBrowser.empty"));
  empty_label_->setAlignment(Qt::AlignCenter);
  empty_label_->setWordWrap(true);
  content_stack_->addWidget(empty_label_);
  root_layout->addWidget(content_stack_, 1);

  QObject::connect(search_, &QLineEdit::textChanged, this,
                   [this](const QString &text) {
                     proxy_->set_search_text(text);
                     refresh_content_page();
                   });
  QObject::connect(grid_button_, &QToolButton::clicked, this, [this]() {
    set_view_mode(QtMaterialBrowserViewMode::Grid, true);
  });
  QObject::connect(list_button_, &QToolButton::clicked, this, [this]() {
    set_view_mode(QtMaterialBrowserViewMode::List, true);
  });
  QObject::connect(tile_slider_, &QSlider::valueChanged, this,
                   [this](int value) {
                     set_tile_size(value);
                     if (presentation_state_changed_) {
                       presentation_state_changed_();
                     }
                   });
  QObject::connect(grid_view_, &QListView::clicked, this,
                   [this](const QModelIndex &index) { activate_index(index); });
  QObject::connect(list_view_, &QTableView::clicked, this,
                   [this](const QModelIndex &index) { activate_index(index); });
  QObject::connect(grid_view_, &QListView::doubleClicked, this,
                   [this](const QModelIndex &index) { open_index(index); });
  QObject::connect(list_view_, &QTableView::doubleClicked, this,
                   [this](const QModelIndex &index) { open_index(index); });

  set_labels(labels_);
  set_view_mode(QtMaterialBrowserViewMode::Grid, false);
  set_tile_size(kDefaultTileSize);
  refresh_content_page();
}

QtMaterialBrowserWindow::~QtMaterialBrowserWindow() = default;

void QtMaterialBrowserWindow::set_labels(QtMaterialBrowserLabels labels) {
  labels_ = std::move(labels);
  setWindowTitle(labels_.window_title);
  search_->setPlaceholderText(labels_.search_placeholder);
  grid_button_->setText(labels_.grid);
  grid_button_->setToolTip(labels_.grid);
  grid_button_->setAccessibleName(labels_.grid);
  list_button_->setText(labels_.list);
  list_button_->setToolTip(labels_.list);
  list_button_->setAccessibleName(labels_.list);
  tile_slider_->setToolTip(labels_.tile_size);
  tile_slider_->setAccessibleName(labels_.tile_size);
  model_->set_labels(labels_);
  refresh_content_page();
}

void QtMaterialBrowserWindow::set_materials(
    std::vector<QtMaterialBrowserItem> materials,
    QString active_material_id) {
  (void)model_->set_items(std::move(materials));
  proxy_->sort(proxy_->sortColumn(), proxy_->sortOrder());
  select_material(active_material_id);
  refresh_content_page();
}

void QtMaterialBrowserWindow::set_active_material(
    QString active_material_id) {
  select_material(active_material_id);
}

void QtMaterialBrowserWindow::clear_materials() {
  set_materials({});
}

void QtMaterialBrowserWindow::set_activate_material_callback(
    ActivateMaterialCallback callback) {
  activate_material_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_open_material_callback(
    OpenMaterialCallback callback) {
  open_material_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_presentation_state_changed_callback(
    PresentationStateChangedCallback callback) {
  presentation_state_changed_ = std::move(callback);
}

void QtMaterialBrowserWindow::restore_presentation_state(
    QtMaterialBrowserViewMode view_mode, int tile_size) {
  set_view_mode(view_mode, false);
  set_tile_size(tile_size);
}

QtMaterialBrowserViewMode QtMaterialBrowserWindow::view_mode() const {
  return view_mode_;
}

int QtMaterialBrowserWindow::tile_size() const {
  return tile_slider_->value();
}

void QtMaterialBrowserWindow::closeEvent(QCloseEvent *event) {
  if (presentation_state_changed_) {
    presentation_state_changed_();
  }
  QWidget::closeEvent(event);
}

void QtMaterialBrowserWindow::showEvent(QShowEvent *event) {
  // Search is deliberately session-transient: each reopening starts unfiltered.
  search_->clear();
  QWidget::showEvent(event);
}

void QtMaterialBrowserWindow::set_view_mode(QtMaterialBrowserViewMode mode,
                                            bool notify) {
  const bool changed = view_mode_ != mode;
  view_mode_ = mode;
  {
    const QSignalBlocker grid_blocker(grid_button_);
    const QSignalBlocker list_blocker(list_button_);
    grid_button_->setChecked(mode == QtMaterialBrowserViewMode::Grid);
    list_button_->setChecked(mode == QtMaterialBrowserViewMode::List);
  }
  tile_slider_->setVisible(mode == QtMaterialBrowserViewMode::Grid);
  refresh_content_page();
  if (notify && changed && presentation_state_changed_) {
    presentation_state_changed_();
  }
}

void QtMaterialBrowserWindow::set_tile_size(int tile_size) {
  const int normalized = std::clamp(tile_size, kMinimumTileSize,
                                    kMaximumTileSize);
  {
    const QSignalBlocker blocker(tile_slider_);
    tile_slider_->setValue(normalized);
  }
  grid_view_->setIconSize(QSize(normalized, normalized));
  grid_view_->setGridSize(
      QSize(normalized + kGridSpacing * 2,
            normalized + kGridLabelHeight + kGridSpacing * 2));
}

void QtMaterialBrowserWindow::refresh_content_page() {
  if (model_ == nullptr || proxy_ == nullptr || content_stack_ == nullptr) {
    return;
  }
  if (proxy_->rowCount() == 0) {
    empty_label_->setText(model_->rowCount() == 0 ? labels_.empty_catalog
                                                  : labels_.no_results);
    content_stack_->setCurrentWidget(empty_label_);
    return;
  }
  content_stack_->setCurrentWidget(
      view_mode_ == QtMaterialBrowserViewMode::Grid
          ? static_cast<QWidget *>(grid_view_)
          : static_cast<QWidget *>(list_view_));
}

void QtMaterialBrowserWindow::select_material(const QString &stable_id) {
  QItemSelectionModel *selection = grid_view_->selectionModel();
  selection->clearSelection();
  selection->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
  if (stable_id.isEmpty()) {
    return;
  }
  for (int row = 0; row < proxy_->rowCount(); ++row) {
    const QModelIndex index = proxy_->index(row, 0);
    if (index.data(QtMaterialBrowserModel::StableIdRole).toString() ==
        stable_id) {
      selection->setCurrentIndex(
          index, QItemSelectionModel::ClearAndSelect |
                     QItemSelectionModel::Rows);
      return;
    }
  }
}

void QtMaterialBrowserWindow::activate_index(const QModelIndex &index) {
  if (!index.isValid() || !activate_material_) {
    return;
  }
  activate_material_(
      index.siblingAtColumn(QtMaterialBrowserModel::NameColumn)
          .data(QtMaterialBrowserModel::StableIdRole)
          .toString());
}

void QtMaterialBrowserWindow::open_index(const QModelIndex &index) {
  if (!index.isValid() || !open_material_) {
    return;
  }
  open_material_(
      index.siblingAtColumn(QtMaterialBrowserModel::NameColumn)
          .data(QtMaterialBrowserModel::StableIdRole)
          .toString());
}

} // namespace quader
