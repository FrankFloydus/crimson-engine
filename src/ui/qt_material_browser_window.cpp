////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_browser_window.hpp"

#include <QAction>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

namespace quader {
namespace {

constexpr std::array<int, 6> kTileSizes{16, 32, 64, 128, 256, 512};
constexpr int kDefaultTileSize = 128;
constexpr int kGridLabelHeight = 34;
constexpr int kGridSpacing = 8;

int tile_size_for_index(int index) {
  return kTileSizes[static_cast<std::size_t>(
      std::clamp(index, 0, static_cast<int>(kTileSizes.size()) - 1))];
}

int tile_index_for_size(int tile_size) {
  const auto upper = std::lower_bound(kTileSizes.begin(), kTileSizes.end(),
                                      tile_size);
  if (upper == kTileSizes.begin()) {
    return 0;
  }
  if (upper == kTileSizes.end()) {
    return static_cast<int>(kTileSizes.size()) - 1;
  }

  const auto lower = std::prev(upper);
  const int lower_distance = tile_size - *lower;
  const int upper_distance = *upper - tile_size;
  const auto nearest = lower_distance <= upper_distance ? lower : upper;
  return static_cast<int>(std::distance(kTileSizes.begin(), nearest));
}

/// Distinguishes Qt's keyboard activation from platform-dependent mouse
/// activation so a mouse click cannot dispatch the same intent twice.
template <typename View> class KeyboardActivationView final : public View {
public:
  using View::View;

  [[nodiscard]] bool keyboard_activation_in_progress() const noexcept {
    return keyboard_activation_in_progress_;
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    const bool activation_key =
        event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    const QScopedValueRollback<bool> guard(keyboard_activation_in_progress_,
                                           activation_key);
    View::keyPressEvent(event);
  }

private:
  bool keyboard_activation_in_progress_ = false;
};

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
  const QtMaterialBrowserItem &item = items_[offset];
  if (item.thumbnail) {
    return item.thumbnail->icon;
  }
  return make_qt_material_thumbnail(item)->icon;
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
    : QMainWindow(parent, Qt::Tool) {
  setObjectName(QStringLiteral("materialBrowser.window"));
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowFlag(Qt::WindowMaximizeButtonHint, false);
  resize(760, 520);

  file_menu_ = menuBar()->addMenu(QString{});
  file_menu_->setObjectName(QStringLiteral("materialBrowser.fileMenu"));
  import_action_ = file_menu_->addAction(QString{});
  import_action_->setObjectName(
      QStringLiteral("materialBrowser.file.import"));

  auto *central_widget = new QWidget(this);
  setCentralWidget(central_widget);
  auto *root_layout = new QVBoxLayout(central_widget);
  auto *toolbar = new QHBoxLayout();
  search_ = new QLineEdit(central_widget);
  search_->setObjectName(QStringLiteral("materialBrowser.search"));
  search_->setClearButtonEnabled(true);
  toolbar->addWidget(search_, 1);

  grid_button_ = new QToolButton(central_widget);
  grid_button_->setObjectName(QStringLiteral("materialBrowser.grid"));
  grid_button_->setCheckable(true);
  grid_button_->setIcon(style()->standardIcon(QStyle::SP_FileDialogListView));
  grid_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->addWidget(grid_button_);
  list_button_ = new QToolButton(central_widget);
  list_button_->setObjectName(QStringLiteral("materialBrowser.list"));
  list_button_->setCheckable(true);
  list_button_->setIcon(
      style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  list_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->addWidget(list_button_);
  auto *view_group = new QButtonGroup(central_widget);
  view_group->setExclusive(true);
  view_group->addButton(grid_button_);
  view_group->addButton(list_button_);

  tile_slider_ = new QSlider(Qt::Horizontal, central_widget);
  tile_slider_->setObjectName(QStringLiteral("materialBrowser.tileSize"));
  tile_slider_->setRange(0, static_cast<int>(kTileSizes.size()) - 1);
  tile_slider_->setSingleStep(1);
  tile_slider_->setPageStep(1);
  tile_slider_->setTickInterval(1);
  tile_slider_->setTickPosition(QSlider::TicksBelow);
  tile_slider_->setValue(tile_index_for_size(kDefaultTileSize));
  tile_slider_->setFixedWidth(150);
  toolbar->addWidget(tile_slider_);
  root_layout->addLayout(toolbar);

  model_ = new QtMaterialBrowserModel(central_widget);
  proxy_ = new QtMaterialBrowserProxyModel(central_widget);
  proxy_->setSourceModel(model_);
  proxy_->sort(QtMaterialBrowserModel::NameColumn, Qt::AscendingOrder);
  auto *selection = new QItemSelectionModel(proxy_, central_widget);

  content_stack_ = new QStackedWidget(central_widget);
  content_stack_->setObjectName(QStringLiteral("materialBrowser.content"));
  auto *keyboard_grid =
      new KeyboardActivationView<QListView>(content_stack_);
  grid_view_ = keyboard_grid;
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
  grid_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  grid_view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  grid_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  content_stack_->addWidget(grid_view_);

  auto *keyboard_list =
      new KeyboardActivationView<QTableView>(content_stack_);
  list_view_ = keyboard_list;
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

  auto *footer = new QHBoxLayout();
  footer->addStretch(1);
  accept_button_ = new QPushButton(central_widget);
  accept_button_->setObjectName(QStringLiteral("materialBrowser.accept"));
  accept_button_->setEnabled(false);
  footer->addWidget(accept_button_);
  root_layout->addLayout(footer);

  QObject::connect(search_, &QLineEdit::textChanged, this,
                   [this](const QString &text) {
                     proxy_->set_search_text(text);
                     refresh_content_page();
                     update_accept_enabled();
                   });
  QObject::connect(grid_button_, &QToolButton::clicked, this, [this]() {
    set_view_mode(QtMaterialBrowserViewMode::Grid, true);
  });
  QObject::connect(list_button_, &QToolButton::clicked, this, [this]() {
    set_view_mode(QtMaterialBrowserViewMode::List, true);
  });
  QObject::connect(tile_slider_, &QSlider::valueChanged, this,
                   [this](int value) {
                     const int tile_size = tile_size_for_index(value);
                     grid_view_->setIconSize(QSize(tile_size, tile_size));
                     grid_view_->setGridSize(
                         QSize(tile_size + kGridSpacing * 2,
                               tile_size + kGridLabelHeight +
                                   kGridSpacing * 2));
                     update_tile_size_text();
                     if (presentation_state_changed_) {
                       presentation_state_changed_();
                     }
                   });
  QObject::connect(import_action_, &QAction::triggered, this, [this]() {
    if (import_textures_) {
      import_textures_();
    }
  });
  QObject::connect(grid_view_, &QListView::clicked, this,
                   [this](const QModelIndex &index) { activate_index(index); });
  QObject::connect(list_view_, &QTableView::clicked, this,
                   [this](const QModelIndex &index) { activate_index(index); });
  QObject::connect(grid_view_, &QListView::activated, this,
                   [this, keyboard_grid](const QModelIndex &index) {
                     if (keyboard_grid->keyboard_activation_in_progress()) {
                       activate_index(index);
                     }
                   });
  QObject::connect(list_view_, &QTableView::activated, this,
                   [this, keyboard_list](const QModelIndex &index) {
                     if (keyboard_list->keyboard_activation_in_progress()) {
                       activate_index(index);
                     }
                   });
  QObject::connect(grid_view_, &QListView::doubleClicked, this,
                   [this](const QModelIndex &index) { open_index(index); });
  QObject::connect(list_view_, &QTableView::doubleClicked, this,
                   [this](const QModelIndex &index) { open_index(index); });
  QObject::connect(selection, &QItemSelectionModel::currentChanged, this,
                   [this]() { update_accept_enabled(); });
  QObject::connect(accept_button_, &QPushButton::clicked, this,
                   [this]() { accept_selected_material(); });

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
  file_menu_->setTitle(labels_.file);
  import_action_->setText(labels_.import_textures);
  accept_button_->setText(labels_.accept);
  accept_button_->setAccessibleName(labels_.accept);
  update_tile_size_text();
  model_->set_labels(labels_);
  update_purpose_presentation();
  refresh_content_page();
}

void QtMaterialBrowserWindow::set_materials(
    std::vector<QtMaterialBrowserItem> materials,
    QuaderMaterialReference selected_material) {
  // Picker search is session-transient. UISystem rebinds candidates before the
  // hidden window receives showEvent(), so clear the old session's filter now
  // or the requested preselection may not exist in the proxy model.
  if (purpose_ == QtMaterialBrowserPurpose::Picker && !isVisible() &&
      !search_->text().isEmpty()) {
    search_->clear();
  }
  (void)model_->set_items(std::move(materials));
  proxy_->sort(proxy_->sortColumn(), proxy_->sortOrder());
  select_material(selected_material);
  refresh_content_page();
  update_accept_enabled();
}

void QtMaterialBrowserWindow::set_active_material(
    QuaderMaterialReference active_material) {
  select_material(active_material);
}

void QtMaterialBrowserWindow::clear_materials() {
  set_materials({});
}

void QtMaterialBrowserWindow::set_purpose(QtMaterialBrowserPurpose purpose) {
  purpose_ = purpose;
  update_purpose_presentation();
}

QuaderMaterialReference QtMaterialBrowserWindow::selected_material() const {
  const QtMaterialBrowserItem *item =
      item_for_index(grid_view_->selectionModel()->currentIndex());
  return item != nullptr ? item->reference : QuaderMaterialReference{};
}

void QtMaterialBrowserWindow::set_activate_material_callback(
    ActivateMaterialCallback callback) {
  activate_material_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_open_material_callback(
    OpenMaterialCallback callback) {
  open_material_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_accept_material_callback(
    AcceptMaterialCallback callback) {
  accept_material_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_import_textures_callback(
    ImportTexturesCallback callback) {
  import_textures_ = std::move(callback);
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
  return tile_size_for_index(tile_slider_->value());
}

void QtMaterialBrowserWindow::closeEvent(QCloseEvent *event) {
  if (presentation_state_changed_) {
    presentation_state_changed_();
  }
  QMainWindow::closeEvent(event);
}

void QtMaterialBrowserWindow::showEvent(QShowEvent *event) {
  // Search is deliberately session-transient: each reopening starts unfiltered.
  search_->clear();
  QMainWindow::showEvent(event);
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
  const int normalized_index = tile_index_for_size(tile_size);
  const int normalized_size = tile_size_for_index(normalized_index);
  {
    const QSignalBlocker blocker(tile_slider_);
    tile_slider_->setValue(normalized_index);
  }
  grid_view_->setIconSize(QSize(normalized_size, normalized_size));
  grid_view_->setGridSize(
      QSize(normalized_size + kGridSpacing * 2,
            normalized_size + kGridLabelHeight + kGridSpacing * 2));
  update_tile_size_text();
}

void QtMaterialBrowserWindow::update_tile_size_text() {
  const QString value = QLocale().toString(tile_size());
  const QString text = labels_.tile_size_value.arg(value);
  tile_slider_->setToolTip(text);
  tile_slider_->setAccessibleName(text);
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

void QtMaterialBrowserWindow::update_purpose_presentation() {
  const bool normal =
      purpose_ == QtMaterialBrowserPurpose::ActiveMaterial;
  setWindowTitle(normal ? labels_.window_title : labels_.picker_window_title);
  file_menu_->menuAction()->setVisible(normal);
  update_accept_enabled();
}

void QtMaterialBrowserWindow::update_accept_enabled() {
  if (accept_button_ == nullptr || grid_view_ == nullptr) {
    return;
  }
  const QtMaterialBrowserItem *item =
      item_for_index(grid_view_->selectionModel()->currentIndex());
  accept_button_->setEnabled(item != nullptr && item->valid());
}

void QtMaterialBrowserWindow::select_material(
    const QuaderMaterialReference &reference) {
  QItemSelectionModel *selection = grid_view_->selectionModel();
  selection->clearSelection();
  selection->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
  if (!reference.valid()) {
    update_accept_enabled();
    return;
  }
  for (int row = 0; row < proxy_->rowCount(); ++row) {
    const QModelIndex index = proxy_->index(row, 0);
    const QtMaterialBrowserItem *item = item_for_index(index);
    if (item != nullptr && item->reference == reference) {
      selection->setCurrentIndex(
          index, QItemSelectionModel::ClearAndSelect |
                     QItemSelectionModel::Rows);
      return;
    }
  }
  update_accept_enabled();
}

const QtMaterialBrowserItem *QtMaterialBrowserWindow::item_for_index(
    const QModelIndex &proxy_index) const {
  if (!proxy_index.isValid() || proxy_ == nullptr || model_ == nullptr) {
    return nullptr;
  }
  const QModelIndex source_index = proxy_->mapToSource(
      proxy_index.siblingAtColumn(QtMaterialBrowserModel::NameColumn));
  if (!source_index.isValid() || source_index.row() < 0 ||
      source_index.row() >= static_cast<int>(model_->items().size())) {
    return nullptr;
  }
  return &model_->items()[static_cast<std::size_t>(source_index.row())];
}

void QtMaterialBrowserWindow::activate_index(const QModelIndex &index) {
  const QtMaterialBrowserItem *item = item_for_index(index);
  if (purpose_ != QtMaterialBrowserPurpose::ActiveMaterial ||
      item == nullptr || !item->valid() || !activate_material_) {
    return;
  }
  activate_material_(item->reference);
}

void QtMaterialBrowserWindow::open_index(const QModelIndex &index) {
  const QtMaterialBrowserItem *item = item_for_index(index);
  if (purpose_ != QtMaterialBrowserPurpose::ActiveMaterial ||
      item == nullptr || !item->valid() || !open_material_) {
    return;
  }
  open_material_(item->reference);
}

void QtMaterialBrowserWindow::accept_selected_material() {
  const QtMaterialBrowserItem *item =
      item_for_index(grid_view_->selectionModel()->currentIndex());
  if (item == nullptr || !item->valid()) {
    return;
  }
  if (purpose_ == QtMaterialBrowserPurpose::ActiveMaterial) {
    if (activate_material_) {
      activate_material_(item->reference);
    }
  } else if (accept_material_) {
    accept_material_(item->reference);
  }
  close();
}

} // namespace quader
