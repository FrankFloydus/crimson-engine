////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_tree_relationship_presentation.hpp"

#include <QAccessibilityHints>
#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDialog>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScreen>
#include <QShowEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleHints>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace quader {
namespace {

constexpr std::array<int, 6> kTileSizes{16, 32, 64, 128, 256, 512};
constexpr int kDefaultTileSize = 128;
constexpr int kGridLabelHeight = 34;
constexpr int kGridSpacing = 8;
constexpr auto kMaterialDragMime = "application/x-quader-material-browser-materials";
constexpr auto kFolderDragMime = "application/x-quader-material-browser-folder";
constexpr int kFolderPathRole = Qt::UserRole;
constexpr std::uint64_t kPreviewIconCacheBudgetBytes =
    96ULL * 1024ULL * 1024ULL;

/** Keeps the current material visible even while focus belongs to the viewport. */
class MaterialGridDelegate final : public QStyledItemDelegate {
public:
  explicit MaterialGridDelegate(QListView &view)
      : QStyledItemDelegate(&view), view_(view) {}

  void set_border_width(qreal width) { border_width_ = width; }

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyledItemDelegate::paint(painter, option, index);
    if (index != view_.currentIndex() ||
        !view_.selectionModel()->isSelected(index)) {
      return;
    }
    QStyleOptionViewItem item(option);
    initStyleOption(&item, index);
    const QRect decoration = view_.style()->subElementRect(
        QStyle::SE_ItemViewItemDecoration, &item, &view_);
    const qreal inset = border_width_ * 0.5;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    // The orange authored in the face-selection toolbar icon.
    painter->setPen(QPen(QColor(255, 143, 24), border_width_, Qt::SolidLine,
                        Qt::SquareCap, Qt::MiterJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(decoration).adjusted(inset, inset, -inset, -inset));
    painter->restore();
  }

private:
  QListView &view_;
  qreal border_width_ = 2.0;
};

/// A passive condition badge; its child remove button owns all interaction.
class UsageFilterBadge final : public QWidget {
public:
  using QWidget::QWidget;

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    radius_ = metrics.px(5.0);
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    const QStyleHints *hints = QGuiApplication::styleHints();
    const QAccessibilityHints *accessibility =
        hints != nullptr ? hints->accessibility() : nullptr;
    // The application theme leaves its resolve mask empty for the live
    // platform palette; do not tint that accessibility fallback either.
    const bool high_contrast =
        QGuiApplication::palette().resolveMask() == 0 ||
        (accessibility != nullptr && accessibility->contrastPreference() ==
                                         Qt::ContrastPreference::HighContrast);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(high_contrast ? QPen(palette().color(QPalette::WindowText))
                                : QPen(Qt::NoPen));
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(bounds, radius_, radius_);
    if (!high_contrast) {
      QColor tint = palette().color(QPalette::Accent);
      tint.setAlphaF(0.16);
      painter.setBrush(tint);
      painter.drawRoundedRect(bounds, radius_, radius_);
    }
  }

private:
  qreal radius_ = 5.0;
};

void show_popup_below(QWidget &popup, QWidget &anchor) {
  popup.adjustSize();
  const QPoint below = anchor.mapToGlobal(QPoint(0, anchor.height()));
  QPoint position = below;
  QScreen *popup_screen = QGuiApplication::screenAt(below);
  if (popup_screen == nullptr) {
    popup_screen = anchor.screen();
  }
  if (popup_screen != nullptr) {
    const QRect available = popup_screen->availableGeometry();
    const int maximum_x =
        std::max(available.left(), available.right() - popup.width() + 1);
    position.setX(std::clamp(position.x(), available.left(), maximum_x));
    if (position.y() + popup.height() - 1 > available.bottom()) {
      position.setY(anchor.mapToGlobal(QPoint(0, 0)).y() - popup.height());
    }
    const int maximum_y =
        std::max(available.top(), available.bottom() - popup.height() + 1);
    position.setY(std::clamp(position.y(), available.top(), maximum_y));
  }
  popup.move(position);
  popup.show();
}

qreal normalized_device_pixel_ratio(qreal value) {
  return std::isfinite(value) && value >= 1.0 ? value : 1.0;
}

std::uint64_t pixmap_cost_bytes(const QPixmap &pixmap) {
  if (pixmap.isNull()) {
    return 0;
  }
  const std::uint64_t width =
      static_cast<std::uint64_t>(std::max(1, pixmap.width()));
  const std::uint64_t height =
      static_cast<std::uint64_t>(std::max(1, pixmap.height()));
  const std::uint64_t bytes_per_pixel = static_cast<std::uint64_t>(
      std::max(1, (pixmap.depth() + 7) / 8));
  return width * height * bytes_per_pixel;
}

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
  using ContextMenuHandler =
      std::function<void(const QModelIndex &, const QPoint &)>;

  ContextMenuHandler context_menu_handler;

  [[nodiscard]] bool keyboard_activation_in_progress() const noexcept {
    return keyboard_activation_in_progress_;
  }

protected:
  void startDrag(Qt::DropActions) override {
    QMimeData *data = this->model()->mimeData(this->selectionModel()->selectedIndexes());
    if (data == nullptr) {
      return;
    }
    QDrag drag(this);
    drag.setMimeData(data);
    // The owner publishes the move. Never remove source rows after a drop.
    (void)drag.exec(Qt::MoveAction, Qt::MoveAction);
  }

  void keyPressEvent(QKeyEvent *event) override {
    const bool activation_key =
        event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    const QScopedValueRollback<bool> guard(keyboard_activation_in_progress_,
                                           activation_key);
    View::keyPressEvent(event);
  }

  void contextMenuEvent(QContextMenuEvent *event) override {
    if (context_menu_handler == nullptr || this->selectionModel() == nullptr) {
      View::contextMenuEvent(event);
      return;
    }
    QModelIndex index = event->reason() == QContextMenuEvent::Keyboard
                            ? this->currentIndex()
                            : this->indexAt(event->pos());
    if (index.isValid()) {
      index = index.siblingAtColumn(QtMaterialBrowserModel::NameColumn);
      if (!this->selectionModel()->isRowSelected(index.row(),
                                                  index.parent())) {
        this->selectionModel()->setCurrentIndex(
            index, QItemSelectionModel::ClearAndSelect |
                       QItemSelectionModel::Rows);
      } else {
        this->selectionModel()->setCurrentIndex(
            index, QItemSelectionModel::NoUpdate);
      }
    }
    const QPoint global_position =
        event->reason() == QContextMenuEvent::Keyboard && index.isValid()
            ? this->viewport()->mapToGlobal(this->visualRect(index).center())
            : event->globalPos();
    context_menu_handler(index, global_position);
    event->accept();
  }

private:
  bool keyboard_activation_in_progress_ = false;
};

QString normalized_search_text(QString text) {
  return text.trimmed().toCaseFolded();
}

bool material_matches_folder(const QModelIndex &source_index,
                             const QString &folder) {
  return folder.isEmpty() || folder == QStringLiteral("Materials") ||
         source_index.data(QtMaterialBrowserModel::RelativePathRole)
             .toString().startsWith(folder + QLatin1Char('/'),
                                   Qt::CaseInsensitive);
}

bool material_matches_search(const QModelIndex &source_index,
                             const QString &search_text) {
  return search_text.isEmpty() ||
         source_index.data(QtMaterialBrowserModel::SearchTextRole)
             .toString().toCaseFolded().contains(search_text);
}

bool material_matches_usage(QtMaterialBrowserUsageMode mode,
                            bool complete, bool used) {
  switch (mode) {
  case QtMaterialBrowserUsageMode::Used:
    return used;
  case QtMaterialBrowserUsageMode::NotUsed:
    return complete && !used;
  case QtMaterialBrowserUsageMode::All:
    return true;
  }
  return false;
}

QString normalized_folder(QString folder) {
  folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (folder.endsWith(QLatin1Char('/'))) {
    folder.chop(1);
  }
  if (folder == QStringLiteral("Materials")) {
    return folder;
  }
  if (!folder.startsWith(QStringLiteral("Materials/"))) {
    return {};
  }
  const QStringList components = folder.split(QLatin1Char('/'));
  for (const QString &component : components) {
    if (component.isEmpty() || component == QStringLiteral(".") ||
        component == QStringLiteral("..")) {
      return {};
    }
  }
  return folder;
}

/// Tree drops dispatch semantic intent; immutable snapshots alone change rows.
class MaterialFolderTree final : public QTreeWidget {
public:
  explicit MaterialFolderTree(QWidget *parent = nullptr)
      : QTreeWidget(parent) {
    install_qt_tree_relationship_presentation(*this);
  }

  std::uint64_t generation = 0;
  bool organization_enabled = true;
  std::function<bool(const QMimeData *, const QString &, bool)> receive_drop;

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    apply_ui_scale_to_qt_tree_relationship_presentation(*this, metrics);
  }

protected:
  void startDrag(Qt::DropActions) override {
    if (!organization_enabled || currentItem() == nullptr) {
      return;
    }
    const QString source = currentItem()->data(0, kFolderPathRole).toString();
    if (source == QStringLiteral("Materials")) {
      return;
    }
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << static_cast<quint64>(generation) << source;
    auto *data = new QMimeData;
    data->setData(QString::fromLatin1(kFolderDragMime), bytes);
    QDrag drag(this);
    drag.setMimeData(data);
    (void)drag.exec(Qt::MoveAction, Qt::MoveAction);
  }

  void dragEnterEvent(QDragEnterEvent *event) override {
    if (organization_enabled &&
        (event->mimeData()->hasFormat(QString::fromLatin1(kMaterialDragMime)) ||
         event->mimeData()->hasFormat(QString::fromLatin1(kFolderDragMime)))) {
      event->setDropAction(Qt::MoveAction);
      event->accept();
    } else {
      event->ignore();
    }
  }

  void dragMoveEvent(QDragMoveEvent *event) override {
    QTreeWidgetItem *target = itemAt(event->position().toPoint());
    if (organization_enabled && target != nullptr && receive_drop &&
        receive_drop(event->mimeData(),
                     target->data(0, kFolderPathRole).toString(), false)) {
      QTreeWidget::dragMoveEvent(event);
      event->setDropAction(Qt::MoveAction);
      event->accept();
    } else {
      event->ignore();
    }
  }

  void dropEvent(QDropEvent *event) override {
    QTreeWidgetItem *target = itemAt(event->position().toPoint());
    if (organization_enabled && target != nullptr && receive_drop &&
        receive_drop(event->mimeData(),
                     target->data(0, kFolderPathRole).toString(), true)) {
      event->setDropAction(Qt::MoveAction);
      event->accept();
    } else {
      event->ignore();
    }
  }
};

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
  if (role == RelativePathRole) {
    return item.project_relative_path;
  }
  if (role == Qt::ToolTipRole) {
    return QFileInfo(item.project_relative_path).fileName();
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

Qt::ItemFlags QtMaterialBrowserModel::flags(const QModelIndex &index) const {
  Qt::ItemFlags result = QAbstractTableModel::flags(index);
  if (organization_enabled_ && index.isValid() && index.row() < rowCount()) {
    const auto &item = items_[static_cast<std::size_t>(index.row())];
    if (item.valid() && item.reference.project_owned() && item.available) {
      result |= Qt::ItemIsDragEnabled;
    }
  }
  return result;
}

QStringList QtMaterialBrowserModel::mimeTypes() const {
  return {QString::fromLatin1(kMaterialDragMime)};
}

QMimeData *QtMaterialBrowserModel::mimeData(const QModelIndexList &indexes) const {
  if (!organization_enabled_) {
    return nullptr;
  }
  std::set<int> rows;
  QStringList identities;
  QStringList paths;
  for (const QModelIndex &index : indexes) {
    if (!index.isValid() || index.row() >= rowCount() ||
        !rows.insert(index.row()).second) {
      continue;
    }
    if (!flags(index).testFlag(Qt::ItemIsDragEnabled)) {
      return nullptr;
    }
    const auto &item = items_[static_cast<std::size_t>(index.row())];
    identities.append(item.stable_id);
    paths.append(item.project_relative_path);
  }
  if (identities.isEmpty()) {
    return nullptr;
  }
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream << static_cast<quint64>(project_generation_) << identities << paths;
  auto *data = new QMimeData;
  data->setData(QString::fromLatin1(kMaterialDragMime), bytes);
  return data;
}

Qt::DropActions QtMaterialBrowserModel::supportedDragActions() const {
  return Qt::MoveAction;
}

void QtMaterialBrowserModel::set_drag_context(std::uint64_t generation,
                                             bool enabled) {
  project_generation_ = generation;
  organization_enabled_ = enabled;
}

bool QtMaterialBrowserModel::set_items(
    std::vector<QtMaterialBrowserItem> items) {
  if (items_ == items) {
    return false;
  }
  beginResetModel();
  clear_preview_icon_cache();
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

bool QtMaterialBrowserModel::set_preview_raster_context(
    QSize logical_bounds, qreal device_pixel_ratio) {
  logical_bounds.setWidth(std::max(1, logical_bounds.width()));
  logical_bounds.setHeight(std::max(1, logical_bounds.height()));
  device_pixel_ratio = normalized_device_pixel_ratio(device_pixel_ratio);
  if (preview_logical_bounds_ == logical_bounds &&
      qFuzzyCompare(preview_device_pixel_ratio_, device_pixel_ratio)) {
    return false;
  }

  preview_logical_bounds_ = logical_bounds;
  preview_device_pixel_ratio_ = device_pixel_ratio;
  invalidate_preview_rasters();
  return true;
}

void QtMaterialBrowserModel::invalidate_preview_rasters() {
  clear_preview_icon_cache();
  if (rowCount() > 0) {
    emit dataChanged(index(0, NameColumn), index(rowCount() - 1, NameColumn),
                     {Qt::DecorationRole});
  }
}

QIcon QtMaterialBrowserModel::preview_icon(int row) const {
  if (row < 0 || row >= static_cast<int>(items_.size())) {
    return {};
  }
  ++preview_icon_cache_clock_;
  const auto cached = std::find_if(
      preview_icon_cache_.begin(), preview_icon_cache_.end(),
      [row](const PreviewIconCacheEntry &entry) { return entry.row == row; });
  if (cached != preview_icon_cache_.end()) {
    cached->last_use = preview_icon_cache_clock_;
    return cached->icon;
  }

  const QtMaterialBrowserItem &item =
      items_[static_cast<std::size_t>(row)];
  const QPixmap pixmap = qt_material_preview_pixmap(
      item, preview_logical_bounds_, preview_device_pixel_ratio_);
  QIcon icon(pixmap);
  const std::uint64_t cost = pixmap_cost_bytes(pixmap);
  if (cost > kPreviewIconCacheBudgetBytes) {
    return icon;
  }

  while (!preview_icon_cache_.empty() &&
         preview_icon_cache_cost_ + cost >
             kPreviewIconCacheBudgetBytes) {
    const auto oldest = std::min_element(
        preview_icon_cache_.begin(), preview_icon_cache_.end(),
        [](const PreviewIconCacheEntry &left,
           const PreviewIconCacheEntry &right) {
          return left.last_use < right.last_use;
        });
    preview_icon_cache_cost_ -= oldest->cost_bytes;
    preview_icon_cache_.erase(oldest);
  }
  preview_icon_cache_.push_back(PreviewIconCacheEntry{
      .row = row,
      .icon = icon,
      .cost_bytes = cost,
      .last_use = preview_icon_cache_clock_,
  });
  preview_icon_cache_cost_ += cost;
  return icon;
}

void QtMaterialBrowserModel::clear_preview_icon_cache() const {
  preview_icon_cache_.clear();
  preview_icon_cache_cost_ = 0;
  preview_icon_cache_clock_ = 0;
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

void QtMaterialBrowserProxyModel::set_folder(QString project_relative_folder) {
  if (project_relative_folder == QStringLiteral("Materials")) {
    project_relative_folder.clear();
  }
  if (folder_ == project_relative_folder) {
    return;
  }
  folder_ = std::move(project_relative_folder);
  invalidateRowsFilter();
}

void QtMaterialBrowserProxyModel::set_usage_mode(
    QtMaterialBrowserUsageMode mode) {
  if (usage_mode_ == mode) {
    return;
  }
  usage_mode_ = mode;
  invalidateRowsFilter();
}

void QtMaterialBrowserProxyModel::set_used_material_ids(
    std::vector<QString> stable_ids, bool complete) {
  std::sort(stable_ids.begin(), stable_ids.end());
  stable_ids.erase(std::unique(stable_ids.begin(), stable_ids.end()),
                   stable_ids.end());
  if (used_material_ids_ == stable_ids && usage_complete_ == complete) {
    return;
  }
  used_material_ids_ = std::move(stable_ids);
  usage_complete_ = complete;
  invalidateRowsFilter();
}

bool QtMaterialBrowserProxyModel::has_non_usage_match() const {
  if (sourceModel() == nullptr) {
    return false;
  }
  for (int row = 0; row < sourceModel()->rowCount(); ++row) {
    if (accepts_row(row, QModelIndex{}, false)) {
      return true;
    }
  }
  return false;
}

bool QtMaterialBrowserProxyModel::filterAcceptsRow(
    int source_row, const QModelIndex &source_parent) const {
  return accepts_row(source_row, source_parent, true);
}

bool QtMaterialBrowserProxyModel::accepts_row(
    int source_row, const QModelIndex &source_parent,
    bool include_usage) const {
  const QModelIndex source_index =
      sourceModel()->index(source_row, QtMaterialBrowserModel::NameColumn,
                           source_parent);
  if (!material_matches_folder(source_index, folder_) ||
      !material_matches_search(source_index, search_text_)) {
    return false;
  }
  if (!include_usage || usage_mode_ == QtMaterialBrowserUsageMode::All) {
    return true;
  }
  const QString stable_id =
      source_index.data(QtMaterialBrowserModel::StableIdRole).toString();
  const bool used = std::binary_search(used_material_ids_.begin(),
                                       used_material_ids_.end(), stable_id);
  return material_matches_usage(usage_mode_, usage_complete_, used);
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

QtMaterialBrowserWindow::QtMaterialBrowserWindow(
    QWidget *parent, QtMaterialBrowserHost host)
    : QMainWindow(parent, host == QtMaterialBrowserHost::Windowed ? Qt::Tool
                                                                 : Qt::Widget),
      host_(host),
      purpose_(host == QtMaterialBrowserHost::Embedded
                   ? QtMaterialBrowserPurpose::FaceAssignment
                   : QtMaterialBrowserPurpose::Management) {
  // QMainWindow promotes its constructor flags to a top-level window. Clear
  // that promotion before constructing the embedded browser's child controls.
  if (host_ == QtMaterialBrowserHost::Embedded) {
    setWindowFlags(Qt::Widget);
  }
  setObjectName(host_ == QtMaterialBrowserHost::Embedded
                    ? QStringLiteral("materialBrowser.embedded")
                    : QStringLiteral("materialBrowser.window"));
  setAttribute(Qt::WA_DeleteOnClose, false);
  if (host_ == QtMaterialBrowserHost::Windowed) {
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    resize(760, 520);
  }

  auto *central_widget = new QWidget(this);
  setCentralWidget(central_widget);
  auto *root_layout = new QVBoxLayout(central_widget);
  if (host_ == QtMaterialBrowserHost::Embedded) {
    root_layout->setContentsMargins(6, 6, 6, 6);
  }
  auto *toolbar = new QHBoxLayout();
  auto *view_toolbar = toolbar;
  folders_button_ = new QToolButton(central_widget);
  folders_button_->setObjectName(QStringLiteral("materialBrowser.folders"));
  folders_button_->setCheckable(true);
  folders_button_->setChecked(true);
  folders_button_->setToolButtonStyle(
      host_ == QtMaterialBrowserHost::Embedded ? Qt::ToolButtonIconOnly
                                              : Qt::ToolButtonTextBesideIcon);
  folders_button_->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  toolbar->addWidget(folders_button_);
  search_ = new QLineEdit(central_widget);
  search_->setObjectName(QStringLiteral("materialBrowser.search"));
  search_->setClearButtonEnabled(true);
  search_->setMinimumWidth(0);
  toolbar->addWidget(search_, 1);

  filter_button_ = new QToolButton(central_widget);
  filter_button_->setObjectName(QStringLiteral("materialBrowser.filter"));
  filter_button_->setCheckable(true);
  filter_button_->setAutoRaise(true);
  filter_button_->setFocusPolicy(Qt::StrongFocus);
  filter_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  filter_button_->setIcon(
      QIcon(QStringLiteral(":/quader/icons/material_filter.png")));
  filter_button_->setIconSize(QSize(20, 20));
  filter_button_->setFixedSize(QSize(28, 28));
  toolbar->addWidget(filter_button_);

  grid_button_ = new QToolButton(central_widget);
  grid_button_->setObjectName(QStringLiteral("materialBrowser.grid"));
  grid_button_->setCheckable(true);
  grid_button_->setToolButtonStyle(
      host_ == QtMaterialBrowserHost::Embedded ? Qt::ToolButtonIconOnly
                                              : Qt::ToolButtonTextBesideIcon);
  if (host_ == QtMaterialBrowserHost::Windowed) {
    view_toolbar->addWidget(grid_button_);
  } else {
    grid_button_->hide();
  }
  list_button_ = new QToolButton(central_widget);
  list_button_->setObjectName(QStringLiteral("materialBrowser.list"));
  list_button_->setCheckable(true);
  list_button_->setToolButtonStyle(
      host_ == QtMaterialBrowserHost::Embedded ? Qt::ToolButtonIconOnly
                                              : Qt::ToolButtonTextBesideIcon);
  if (host_ == QtMaterialBrowserHost::Windowed) {
    view_toolbar->addWidget(list_button_);
  } else {
    list_button_->hide();
  }
  refresh_view_mode_icons();
  auto *view_group = new QButtonGroup(central_widget);
  view_group->setExclusive(true);
  view_group->addButton(grid_button_);
  view_group->addButton(list_button_);

  if (host_ == QtMaterialBrowserHost::Embedded) {
    import_tool_button_ = new QToolButton(central_widget);
    import_tool_button_->setObjectName(QStringLiteral("materialBrowser.importTool"));
    import_tool_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    import_tool_button_->setAutoRaise(true);
    import_tool_button_->setIcon(
        QIcon(QStringLiteral(":/quader/icons/material_import.png")));
    import_tool_button_->setIconSize(QSize(20, 20));
    import_tool_button_->setFixedSize(QSize(28, 28));
    toolbar->addWidget(import_tool_button_);
    thumbnail_size_button_ = new QToolButton(central_widget);
    thumbnail_size_button_->setObjectName(QStringLiteral("materialBrowser.thumbnailSize"));
    thumbnail_size_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    thumbnail_size_button_->setAutoRaise(true);
    thumbnail_size_button_->setIcon(
        QIcon(QStringLiteral(":/quader/icons/material_thumbnail_size.png")));
    thumbnail_size_button_->setIconSize(QSize(20, 20));
    thumbnail_size_button_->setFixedSize(QSize(28, 28));
    toolbar->addWidget(thumbnail_size_button_);
    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    thumbnail_size_popup_ = popup;
    popup->setObjectName(QStringLiteral("materialBrowser.thumbnailSizePopup"));
    popup->setFrameShape(QFrame::StyledPanel);
    popup->setFrameShadow(QFrame::Raised);
    auto *popup_layout = new QVBoxLayout(popup);
    thumbnail_size_value_ = new QLabel(popup);
    popup_layout->addWidget(thumbnail_size_value_);
    tile_slider_ = new QSlider(Qt::Horizontal, popup);
    popup_layout->addWidget(tile_slider_);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), popup);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(escape, &QShortcut::activated, popup, &QWidget::close);
    popup->hide();
  } else {
    tile_slider_ = new QSlider(Qt::Horizontal, central_widget);
  }
  tile_slider_->setObjectName(QStringLiteral("materialBrowser.tileSize"));
  tile_slider_->setRange(0, static_cast<int>(kTileSizes.size()) - 1);
  tile_slider_->setSingleStep(1);
  tile_slider_->setPageStep(1);
  tile_slider_->setTickInterval(1);
  tile_slider_->setTickPosition(QSlider::TicksBelow);
  tile_slider_->setValue(tile_index_for_size(kDefaultTileSize));
  tile_slider_->setFixedWidth(150);
  if (host_ == QtMaterialBrowserHost::Windowed) {
    view_toolbar->addWidget(tile_slider_);
  }
  if (host_ == QtMaterialBrowserHost::Windowed) {
    root_layout->addLayout(toolbar);
  }
  active_filters_ = new QWidget(central_widget);
  active_filters_->setObjectName(QStringLiteral("materialBrowser.activeFilters"));
  auto *active_filters_layout = new QHBoxLayout(active_filters_);
  filtered_by_ = new QLabel(active_filters_);
  filtered_by_->setObjectName(QStringLiteral("materialBrowser.filteredBy"));
  filtered_by_->setTextFormat(Qt::PlainText);
  active_filters_layout->addWidget(filtered_by_);
  usage_chip_ = new UsageFilterBadge(active_filters_);
  usage_chip_->setObjectName(QStringLiteral("materialBrowser.usageChip"));
  auto *usage_chip_layout = new QHBoxLayout(usage_chip_);
  usage_chip_label_ = new QLabel(usage_chip_);
  usage_chip_label_->setObjectName(QStringLiteral("materialBrowser.usageChipLabel"));
  usage_chip_label_->setTextFormat(Qt::PlainText);
  usage_chip_layout->addWidget(usage_chip_label_);
  usage_chip_remove_ = new QToolButton(usage_chip_);
  usage_chip_remove_->setObjectName(QStringLiteral("materialBrowser.usageChipRemove"));
  usage_chip_remove_->setAutoRaise(true);
  usage_chip_remove_->setFocusPolicy(Qt::StrongFocus);
  usage_chip_remove_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  usage_chip_layout->addWidget(usage_chip_remove_);
  active_filters_layout->addWidget(usage_chip_);
  active_filters_layout->addStretch(1);
  active_filters_->hide();
  if (host_ == QtMaterialBrowserHost::Windowed) {
    root_layout->addWidget(active_filters_);
  }
  refresh_usage_badge_style();
  usage_status_ = new QLabel(central_widget);
  usage_status_->setObjectName(QStringLiteral("materialBrowser.usageStatus"));
  usage_status_->setWordWrap(true);
  usage_status_->setTextFormat(Qt::PlainText);
  usage_status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  usage_status_->hide();
  if (host_ == QtMaterialBrowserHost::Windowed) {
    root_layout->addWidget(usage_status_);
  }

  model_ = new QtMaterialBrowserModel(central_widget);
  proxy_ = new QtMaterialBrowserProxyModel(central_widget);
  proxy_->setSourceModel(model_);
  proxy_->sort(QtMaterialBrowserModel::NameColumn, Qt::AscendingOrder);
  auto *selection = new QItemSelectionModel(proxy_, central_widget);

  splitter_ = new QSplitter(Qt::Horizontal, central_widget);
  splitter_->setObjectName(QStringLiteral("materialBrowser.splitter"));
  auto *folder_tree = new MaterialFolderTree(splitter_);
  folder_tree_ = folder_tree;
  folder_tree_->setObjectName(QStringLiteral("materialBrowser.folderTree"));
  folder_tree_->setHeaderHidden(true);
  folder_tree_->setRootIsDecorated(true);
  folder_tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  folder_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  folder_tree_->setDragDropMode(QAbstractItemView::DragDrop);
  folder_tree_->setDefaultDropAction(Qt::MoveAction);
  folder_tree_->setAutoExpandDelay(650);
  folder_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  folder_tree->receive_drop =
      [this](const QMimeData *data, const QString &destination, bool execute) {
        return receive_folder_drop(data, destination, execute);
      };
  content_stack_ = new QStackedWidget(splitter_);
  content_stack_->setObjectName(QStringLiteral("materialBrowser.content"));
  auto *keyboard_grid =
      new KeyboardActivationView<QListView>(content_stack_);
  grid_view_ = keyboard_grid;
  grid_view_->setObjectName(QStringLiteral("materialBrowser.gridView"));
  grid_view_->setModel(proxy_);
  grid_view_->setSelectionModel(selection);
  grid_view_->setItemDelegate(new MaterialGridDelegate(*grid_view_));
  grid_view_->setViewMode(QListView::IconMode);
  grid_view_->setResizeMode(QListView::Adjust);
  grid_view_->setMovement(QListView::Static);
  grid_view_->setWrapping(true);
  grid_view_->setUniformItemSizes(true);
  grid_view_->setWordWrap(true);
  grid_view_->setSpacing(kGridSpacing);
  grid_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  grid_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  grid_view_->setDragDropMode(QAbstractItemView::DragOnly);
  grid_view_->setDefaultDropAction(Qt::MoveAction);
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
  list_view_->setShowGrid(false);
  list_view_->setWordWrap(false);
  list_view_->setAlternatingRowColors(true);
  list_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  list_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list_view_->setDragDropMode(QAbstractItemView::DragOnly);
  list_view_->setDefaultDropAction(Qt::MoveAction);
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
  empty_label_->setContextMenuPolicy(Qt::CustomContextMenu);
  content_stack_->addWidget(empty_label_);
  splitter_->setStretchFactor(0, 0);
  splitter_->setStretchFactor(1, 1);
  splitter_->setCollapsible(1, false);
  splitter_->setSizes({sidebar_width_, 540});
  root_layout->addWidget(splitter_, 1);

  if (host_ == QtMaterialBrowserHost::Embedded) {
    root_layout->addLayout(toolbar);
    root_layout->addWidget(active_filters_);
    root_layout->addWidget(usage_status_);
  }

  if (host_ == QtMaterialBrowserHost::Windowed) {
    auto *footer = new QHBoxLayout();
    footer->addStretch(1);
    import_button_ = new QPushButton(central_widget);
    import_button_->setObjectName(QStringLiteral("materialBrowser.import"));
    footer->addWidget(import_button_);
    accept_button_ = new QPushButton(central_widget);
    accept_button_->setObjectName(QStringLiteral("materialBrowser.accept"));
    accept_button_->setEnabled(false);
    footer->addWidget(accept_button_);
    root_layout->addLayout(footer);
  }

  QObject::connect(search_, &QLineEdit::textChanged, this,
                   [this](const QString &text) {
                     proxy_->set_search_text(text);
                     refresh_content_page();
                     update_accept_enabled();
                   });
  QObject::connect(filter_button_, &QToolButton::clicked, this,
                   [this]() { show_filter_popup(); });
  QObject::connect(grid_button_, &QToolButton::clicked, this, [this]() {
    set_view_mode(QtMaterialBrowserViewMode::Grid, true);
  });
  QObject::connect(list_button_, &QToolButton::clicked, this, [this]() {
    set_view_mode(QtMaterialBrowserViewMode::List, true);
  });
  QObject::connect(tile_slider_, &QSlider::valueChanged, this,
                   [this](int value) {
                     const int tile_size = tile_size_for_index(value);
                     apply_tile_metrics(tile_size);
                     update_tile_size_text();
                     if (presentation_state_changed_) {
                       presentation_state_changed_();
                     }
                   });
  const auto import_textures = [this]() {
    if (purpose_ != QtMaterialBrowserPurpose::Picker && import_textures_) {
      import_textures_(selected_folder());
    }
  };
  if (import_button_ != nullptr) {
    QObject::connect(import_button_, &QPushButton::clicked, this, import_textures);
  }
  if (import_tool_button_ != nullptr) {
    QObject::connect(import_tool_button_, &QToolButton::clicked, this, import_textures);
    QObject::connect(thumbnail_size_button_, &QToolButton::clicked, this,
                     [this]() { show_thumbnail_size_popup(); });
  }
  QObject::connect(usage_chip_remove_, &QToolButton::clicked, this,
                   [this]() {
                     const QPointer<QtMaterialBrowserWindow> guard(this);
                     if (filter_popup_) {
                       filter_popup_->close();
                     }
                     apply_usage_mode(QtMaterialBrowserUsageMode::All);
                     if (guard) {
                       guard->filter_button_->setFocus(Qt::OtherFocusReason);
                     }
                   });
  QObject::connect(folders_button_, &QToolButton::toggled, this,
                   [this](bool visible) { set_sidebar_visible(visible, true); });
  QObject::connect(splitter_, &QSplitter::splitterMoved, this,
                   [this](int, int) {
    const int width = splitter_->sizes().value(0);
    if (width > 0) {
      sidebar_width_ = width;
    }
    const QSignalBlocker blocker(folders_button_);
    folders_button_->setChecked(width > 0);
    if (presentation_state_changed_) {
      presentation_state_changed_();
    }
  });
  QObject::connect(folder_tree_, &QTreeWidget::currentItemChanged, this,
                   [this](QTreeWidgetItem *, QTreeWidgetItem *) {
    ++action_eligibility_revision_;
    proxy_->set_folder(selected_folder());
    refresh_content_page();
    update_accept_enabled();
  });
  QObject::connect(folder_tree_, &QTreeWidget::customContextMenuRequested,
                   this, [this](const QPoint &position) {
    show_folder_menu(position);
  });
  QObject::connect(grid_view_, &QListView::clicked, this,
                   [this](const QModelIndex &index) { assign_index(index); });
  QObject::connect(list_view_, &QTableView::clicked, this,
                   [this](const QModelIndex &index) { assign_index(index); });
  QObject::connect(grid_view_, &QListView::activated, this,
                   [this, keyboard_grid](const QModelIndex &index) {
                     if (keyboard_grid->keyboard_activation_in_progress()) {
                       assign_index(index);
                     }
                   });
  QObject::connect(list_view_, &QTableView::activated, this,
                   [this, keyboard_list](const QModelIndex &index) {
                     if (keyboard_list->keyboard_activation_in_progress()) {
                       assign_index(index);
                     }
                   });
  QObject::connect(grid_view_, &QListView::doubleClicked, this,
                   [this](const QModelIndex &index) { open_index(index); });
  QObject::connect(list_view_, &QTableView::doubleClicked, this,
                   [this](const QModelIndex &index) { open_index(index); });
  keyboard_grid->context_menu_handler =
      [this](const QModelIndex &index, const QPoint &global_position) {
        show_material_menu(index, global_position);
      };
  keyboard_list->context_menu_handler =
      [this](const QModelIndex &index, const QPoint &global_position) {
        show_material_menu(index, global_position);
      };
  QObject::connect(empty_label_, &QLabel::customContextMenuRequested, this,
                   [this](const QPoint &position) {
                     show_material_menu({},
                                        empty_label_->mapToGlobal(position));
                   });
  QObject::connect(selection, &QItemSelectionModel::currentChanged, this,
                   [this](const QModelIndex &current, const QModelIndex &previous) {
                     ++action_eligibility_revision_;
                     update_accept_enabled();
                     grid_view_->update(previous);
                     grid_view_->update(current);
                   });
  QObject::connect(selection, &QItemSelectionModel::selectionChanged, this,
                   [this]() { ++action_eligibility_revision_; });
  if (accept_button_ != nullptr) {
    QObject::connect(accept_button_, &QPushButton::clicked, this,
                     [this]() { accept_selected_material(); });
  }

  rebuild_folder_tree();
  set_labels(labels_);
  set_view_mode(QtMaterialBrowserViewMode::Grid, false);
  set_tile_size(kDefaultTileSize);
  refresh_content_page();
}

QtMaterialBrowserWindow::~QtMaterialBrowserWindow() {
  // A Qt::Popup child owns a native window and can outlive ordinary child
  // teardown ordering. Retire it while the browser controls referenced by its
  // dismissal callback are still intact.
  if (filter_popup_) {
    delete filter_popup_.data();
  }
  if (thumbnail_size_popup_) {
    delete thumbnail_size_popup_.data();
  }
}

void QtMaterialBrowserWindow::set_labels(QtMaterialBrowserLabels labels) {
  labels_ = std::move(labels);
  setWindowTitle(labels_.window_title);
  search_->setPlaceholderText(labels_.search_placeholder);
  filter_button_->setToolTip(labels_.filter);
  filter_button_->setAccessibleName(labels_.filter);
  filter_button_->setAccessibleDescription(labels_.filter);
  grid_button_->setText(labels_.grid);
  grid_button_->setToolTip(labels_.grid);
  grid_button_->setAccessibleName(labels_.grid);
  list_button_->setText(labels_.list);
  list_button_->setToolTip(labels_.list);
  list_button_->setAccessibleName(labels_.list);
  folders_button_->setText(labels_.folders);
  folders_button_->setToolTip(labels_.folders);
  folders_button_->setAccessibleName(labels_.folders);
  folder_tree_->setAccessibleName(labels_.folders);
  if (folder_tree_->topLevelItemCount() > 0) {
    folder_tree_->topLevelItem(0)->setText(0, labels_.all);
  }
  if (import_button_ != nullptr) {
    import_button_->setText(labels_.import_textures);
    import_button_->setAccessibleName(labels_.import_textures);
  }
  if (import_tool_button_ != nullptr) {
    import_tool_button_->setToolTip(labels_.import_textures);
    import_tool_button_->setAccessibleName(labels_.import_textures);
    thumbnail_size_button_->setToolTip(labels_.tile_size);
    thumbnail_size_button_->setAccessibleName(labels_.tile_size);
  }
  if (accept_button_ != nullptr) {
    accept_button_->setText(labels_.accept);
    accept_button_->setAccessibleName(labels_.accept);
  }
  refresh_usage_presentation();
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
  const QuaderMaterialReference previous_current = this->selected_material();
  QStringList selected_ids;
  for (const QModelIndex &index : grid_view_->selectionModel()->selectedIndexes()) {
    if (index.column() == QtMaterialBrowserModel::NameColumn) {
      selected_ids.append(index.data(QtMaterialBrowserModel::StableIdRole).toString());
    }
  }
  const bool items_changed = model_->set_items(std::move(materials));
  if (items_changed) {
    ++material_presentation_revision_;
    rebuild_folder_tree();
    proxy_->sort(proxy_->sortColumn(), proxy_->sortOrder());
    auto *selection = grid_view_->selectionModel();
    for (int row = 0; row < proxy_->rowCount(); ++row) {
      const QModelIndex index = proxy_->index(row, 0);
      if (selected_ids.contains(index.data(QtMaterialBrowserModel::StableIdRole).toString())) {
        selection->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
      }
    }
  }
  const QuaderMaterialReference requested_current =
      selected_material.valid() ? selected_material : previous_current;
  bool restored_current_without_expanding_selection = false;
  if (items_changed && requested_current.valid() &&
      purpose_ != QtMaterialBrowserPurpose::Picker &&
      !grid_view_->selectionModel()
           ->selectedRows(QtMaterialBrowserModel::NameColumn)
           .isEmpty()) {
    for (int row = 0; row < proxy_->rowCount(); ++row) {
      const QModelIndex index = proxy_->index(row, 0);
      const QtMaterialBrowserItem *item = item_for_index(index);
      if (item != nullptr && item->reference == requested_current) {
        grid_view_->selectionModel()->setCurrentIndex(
            index, QItemSelectionModel::NoUpdate);
        restored_current_without_expanding_selection = true;
        break;
      }
    }
  }
  if (!restored_current_without_expanding_selection) {
    select_material(requested_current);
  }
  refresh_content_page();
  update_accept_enabled();
}

void QtMaterialBrowserWindow::set_selected_material(
    QuaderMaterialReference material) {
  select_material(material);
}

bool QtMaterialBrowserWindow::reveal_material(
    QuaderMaterialReference material) {
  if (!material.valid()) {
    return false;
  }
  const auto &items = model_->items();
  const auto found = std::find_if(items.begin(), items.end(),
      [&](const auto &item) { return item.reference == material; });
  if (found == items.end()) {
    return false;
  }
  const QModelIndex source_index = model_->index(
      static_cast<int>(std::distance(items.begin(), found)),
      QtMaterialBrowserModel::NameColumn);
  if (!material_matches_folder(source_index, selected_folder())) {
    set_selected_folder(QStringLiteral("Materials"));
  }
  if (!material_matches_search(source_index,
                               normalized_search_text(search_->text()))) {
    search_->clear();
  }
  const bool used = std::find(usage_snapshot_.used_materials.begin(),
                              usage_snapshot_.used_materials.end(), material) !=
                    usage_snapshot_.used_materials.end();
  if (purpose_ != QtMaterialBrowserPurpose::Picker &&
      !material_matches_usage(usage_mode_, usage_snapshot_.complete, used)) {
    apply_usage_mode(QtMaterialBrowserUsageMode::All);
  }
  const QModelIndex index = proxy_->mapFromSource(source_index);
  if (!index.isValid()) {
    return false;
  }
  grid_view_->selectionModel()->setCurrentIndex(
      index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  if (view_mode() == QtMaterialBrowserViewMode::List) {
    list_view_->scrollTo(index);
  } else {
    grid_view_->scrollTo(index);
  }
  return true;
}

void QtMaterialBrowserWindow::set_assignment_available(bool available) {
  if (assignment_available_ == available) {
    return;
  }
  assignment_available_ = available;
  update_accept_enabled();
}

void QtMaterialBrowserWindow::clear_materials() {
  folders_.clear();
  folder_tree_->clear();
  search_->clear();
  proxy_->set_folder({});
  set_materials({});
  rebuild_folder_tree();
}

void QtMaterialBrowserWindow::set_folders(std::vector<QString> folders) {
  std::vector<QString> normalized;
  for (QString &folder : folders) {
    folder = normalized_folder(std::move(folder));
    if (!folder.isEmpty() && folder != QStringLiteral("Materials")) {
      normalized.push_back(std::move(folder));
    }
  }
  std::sort(normalized.begin(), normalized.end());
  normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
  if (folders_ == normalized) {
    return;
  }
  folders_ = std::move(normalized);
  rebuild_folder_tree();
}

void QtMaterialBrowserWindow::set_project_generation(std::uint64_t generation) {
  if (project_generation_ == generation) {
    return;
  }
  if (filter_popup_) {
    filter_popup_->close();
  }
  project_generation_ = generation;
  assignment_available_ = false;
  dependency_revision_ = 0;
  ++action_eligibility_revision_;
  usage_snapshot_ = {
      .project_generation = generation,
  };
  usage_mode_ = QtMaterialBrowserUsageMode::All;
  material_paste_available_ = false;
  proxy_->set_used_material_ids({}, false);
  proxy_->set_usage_mode(QtMaterialBrowserUsageMode::All);
  filter_button_->setChecked(false);
  static_cast<MaterialFolderTree *>(folder_tree_)->generation = generation;
  model_->set_drag_context(generation,
      purpose_ != QtMaterialBrowserPurpose::Picker);
  refresh_usage_presentation();
  if (thumbnail_size_popup_) {
    thumbnail_size_popup_->close();
  }
  clear_materials();
}

void QtMaterialBrowserWindow::set_material_usage(
    QtMaterialBrowserUsageSnapshot snapshot) {
  if (snapshot.project_generation != project_generation_) {
    return;
  }
  QStringList errors;
  for (const QString &error : snapshot.errors) {
    const QString trimmed = error.trimmed();
    if (!trimmed.isEmpty() && !errors.contains(trimmed)) {
      errors.push_back(trimmed);
    }
  }
  snapshot.errors = std::move(errors);
  if (usage_snapshot_ == snapshot) {
    return;
  }
  if (filter_popup_) {
    filter_popup_->close();
  }
  std::vector<QString> stable_ids;
  stable_ids.reserve(snapshot.used_materials.size());
  for (const QuaderMaterialReference &reference : snapshot.used_materials) {
    if (reference.valid()) {
      stable_ids.push_back(qt_material_reference_stable_id(reference));
    }
  }
  usage_snapshot_ = std::move(snapshot);
  proxy_->set_used_material_ids(std::move(stable_ids),
                                usage_snapshot_.complete);
  refresh_content_page();
  update_accept_enabled();
  refresh_usage_presentation();
}

void QtMaterialBrowserWindow::set_dependency_revision(std::uint64_t revision) {
  if (dependency_revision_ == revision) {
    return;
  }
  dependency_revision_ = revision;
  ++action_eligibility_revision_;
}

void QtMaterialBrowserWindow::set_protected_material(
    QuaderMaterialReference material) {
  if (protected_material_ == material) {
    return;
  }
  protected_material_ = material;
  ++action_eligibility_revision_;
}

void QtMaterialBrowserWindow::set_material_paste_available(bool available) {
  if (material_paste_available_ == available) {
    return;
  }
  material_paste_available_ = available;
  ++action_eligibility_revision_;
}

QString QtMaterialBrowserWindow::selected_folder() const {
  const auto *item = folder_tree_->currentItem();
  return item == nullptr ? QStringLiteral("Materials")
                        : item->data(0, kFolderPathRole).toString();
}

void QtMaterialBrowserWindow::set_purpose(QtMaterialBrowserPurpose purpose) {
  if (purpose_ == purpose) {
    return;
  }
  if (filter_popup_) {
    filter_popup_->close();
  }
  purpose_ = purpose;
  ++action_eligibility_revision_;
  update_purpose_presentation();
  if (purpose == QtMaterialBrowserPurpose::Picker) {
    auto *selection = grid_view_->selectionModel();
    const QModelIndex current = selection->currentIndex();
    selection->clearSelection();
    if (current.isValid()) {
      selection->select(current, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
  }
}

QuaderMaterialReference QtMaterialBrowserWindow::selected_material() const {
  const QtMaterialBrowserItem *item =
      item_for_index(grid_view_->selectionModel()->currentIndex());
  return item != nullptr ? item->reference : QuaderMaterialReference{};
}

QuaderMaterialReference QtMaterialBrowserWindow::current_selected_material() const {
  const auto *selection = grid_view_->selectionModel();
  return selection->isSelected(selection->currentIndex())
      ? selected_material() : QuaderMaterialReference{};
}

void QtMaterialBrowserWindow::set_assign_material_callback(
    AssignMaterialCallback callback) {
  assign_material_ = std::move(callback);
  update_accept_enabled();
}

void QtMaterialBrowserWindow::set_open_material_callback(
    OpenMaterialCallback callback) {
  open_material_ = std::move(callback);
  ++action_eligibility_revision_;
}

void QtMaterialBrowserWindow::set_accept_material_callback(
    AcceptMaterialCallback callback) {
  accept_material_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_import_textures_callback(
    ImportTexturesCallback callback) {
  import_textures_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_create_folder_callback(CreateFolderCallback callback) {
  create_folder_ = std::move(callback);
  ++action_eligibility_revision_;
}

void QtMaterialBrowserWindow::set_rename_folder_callback(RenameFolderCallback callback) {
  rename_folder_ = std::move(callback);
  ++action_eligibility_revision_;
}

void QtMaterialBrowserWindow::set_delete_folder_callback(DeleteFolderCallback callback) {
  delete_folder_ = std::move(callback);
  ++action_eligibility_revision_;
}

void QtMaterialBrowserWindow::set_move_folder_callback(MoveFolderCallback callback) {
  move_folder_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_move_materials_callback(MoveMaterialsCallback callback) {
  move_materials_ = std::move(callback);
}

void QtMaterialBrowserWindow::set_material_action_callback(
    MaterialActionCallback callback) {
  material_action_ = std::move(callback);
  ++action_eligibility_revision_;
}

void QtMaterialBrowserWindow::set_presentation_state_changed_callback(
    PresentationStateChangedCallback callback) {
  presentation_state_changed_ = std::move(callback);
}

void QtMaterialBrowserWindow::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (ui_scale_metrics_.revision() == metrics.revision() &&
      ui_scale_metrics_.factor() == metrics.factor()) {
    return;
  }
  ui_scale_metrics_ = metrics;
  static_cast<MaterialGridDelegate *>(grid_view_->itemDelegate())
      ->set_border_width(metrics.px(2));
  static_cast<MaterialFolderTree *>(folder_tree_)->apply_ui_scale(metrics);
  filter_button_->setIconSize(metrics.size(QSize(20, 20)));
  filter_button_->setFixedSize(metrics.size(QSize(28, 28)));
  tile_slider_->setFixedWidth(metrics.px(150));
  refresh_usage_badge_style();
  for (QToolButton *button : {import_tool_button_, thumbnail_size_button_}) {
    if (button != nullptr) {
      button->setIconSize(metrics.size(QSize(20, 20)));
      button->setFixedSize(metrics.size(QSize(28, 28)));
    }
  }
  apply_tile_metrics(tile_size());
  grid_view_->doItemsLayout();
  grid_view_->viewport()->update();
}

void QtMaterialBrowserWindow::restore_presentation_state(
    QtMaterialBrowserViewMode view_mode, int tile_size, bool sidebar_visible,
    int sidebar_width) {
  set_view_mode(view_mode, false);
  set_tile_size(tile_size);
  sidebar_width_ = std::clamp(sidebar_width, 80, 2000);
  set_sidebar_visible(sidebar_visible, false);
}

QtMaterialBrowserViewMode QtMaterialBrowserWindow::view_mode() const {
  return view_mode_;
}

int QtMaterialBrowserWindow::tile_size() const {
  return tile_size_for_index(tile_slider_->value());
}

bool QtMaterialBrowserWindow::sidebar_visible() const {
  return folders_button_->isChecked();
}

int QtMaterialBrowserWindow::sidebar_width() const {
  return sidebar_width_;
}

void QtMaterialBrowserWindow::set_sidebar_visible(bool visible, bool notify) {
  const bool changed = sidebar_visible() != visible || folder_tree_->isHidden() == visible;
  const QSignalBlocker blocker(folders_button_);
  folders_button_->setChecked(visible);
  folder_tree_->setVisible(visible);
  if (visible) {
    splitter_->setSizes({sidebar_width_, std::max(1, splitter_->width() - sidebar_width_)});
  }
  if (notify && changed && presentation_state_changed_) {
    presentation_state_changed_();
  }
}

bool QtMaterialBrowserWindow::event(QEvent *event) {
  const bool handled = QMainWindow::event(event);
  if (event != nullptr) {
    if (event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::ScreenChangeInternal) {
      refresh_preview_raster_context(true);
    } else if (event->type() == QEvent::PaletteChange ||
               event->type() == QEvent::ApplicationPaletteChange) {
      refresh_view_mode_icons();
    } else if (event->type() == QEvent::ActivationChange) {
      refresh_usage_badge_style();
    } else if (event->type() == QEvent::Hide) {
      if (filter_popup_) {
        filter_popup_->close();
      }
      if (thumbnail_size_popup_) {
        thumbnail_size_popup_->close();
      }
    }
  }
  return handled;
}

void QtMaterialBrowserWindow::refresh_view_mode_icons() {
  if (filter_button_ != nullptr) {
    filter_button_->setIcon(
        QIcon(QStringLiteral(":/quader/icons/material_filter.png")));
  }
  if (import_tool_button_ != nullptr) {
    import_tool_button_->setIcon(
        QIcon(QStringLiteral(":/quader/icons/material_import.png")));
    thumbnail_size_button_->setIcon(
        QIcon(QStringLiteral(":/quader/icons/material_thumbnail_size.png")));
  }
  refresh_usage_badge_style();
  if (grid_button_ != nullptr) {
    grid_button_->setIcon(style()->standardIcon(
        QStyle::SP_FileDialogListView, nullptr, this));
  }
  if (list_button_ != nullptr) {
    list_button_->setIcon(style()->standardIcon(
        QStyle::SP_FileDialogDetailedView, nullptr, this));
  }
  if (folders_button_ != nullptr) {
    folders_button_->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
  }
  if (folder_tree_ != nullptr) {
    for (QTreeWidgetItemIterator it(folder_tree_); *it != nullptr; ++it) {
      (*it)->setIcon(0, style()->standardIcon(
          (*it)->data(0, kFolderPathRole).toString() == QStringLiteral("Materials")
              ? QStyle::SP_DirHomeIcon : QStyle::SP_DirIcon));
    }
  }
}

void QtMaterialBrowserWindow::closeEvent(QCloseEvent *event) {
  if (filter_popup_) {
    filter_popup_->close();
  }
  if (thumbnail_size_popup_) {
    thumbnail_size_popup_->close();
  }
  if (presentation_state_changed_) {
    presentation_state_changed_();
  }
  QMainWindow::closeEvent(event);
}

void QtMaterialBrowserWindow::showEvent(QShowEvent *event) {
  // Floating browser search is session-transient; the dock retains its filter
  // when a tab switch or layout restoration temporarily hides its host.
  if (host_ == QtMaterialBrowserHost::Windowed) {
    search_->clear();
  }
  QMainWindow::showEvent(event);
  refresh_preview_raster_context();
}

void QtMaterialBrowserWindow::set_view_mode(QtMaterialBrowserViewMode mode,
                                            bool notify) {
  if (host_ == QtMaterialBrowserHost::Embedded) {
    mode = QtMaterialBrowserViewMode::Grid;
  }
  const bool changed = view_mode_ != mode;
  view_mode_ = mode;
  {
    const QSignalBlocker grid_blocker(grid_button_);
    const QSignalBlocker list_blocker(list_button_);
    grid_button_->setChecked(mode == QtMaterialBrowserViewMode::Grid);
    list_button_->setChecked(mode == QtMaterialBrowserViewMode::List);
  }
  if (host_ == QtMaterialBrowserHost::Windowed) {
    tile_slider_->setVisible(mode == QtMaterialBrowserViewMode::Grid);
  }
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
  apply_tile_metrics(normalized_size);
  update_tile_size_text();
}

void QtMaterialBrowserWindow::apply_tile_metrics(int tile_size) {
  const int presented_tile_size = ui_scale_metrics_.px(tile_size);
  const int spacing = ui_scale_metrics_.px(kGridSpacing);
  grid_view_->setSpacing(spacing);
  grid_view_->setIconSize(
      QSize(presented_tile_size, presented_tile_size));
  grid_view_->setGridSize(
      QSize(presented_tile_size + spacing * 2,
            presented_tile_size + ui_scale_metrics_.px(kGridLabelHeight) +
                spacing * 2));
  refresh_preview_raster_context();
}

void QtMaterialBrowserWindow::refresh_preview_raster_context(
    bool force_refresh) {
  if (model_ == nullptr || grid_view_ == nullptr) {
    return;
  }
  const qreal device_pixel_ratio = normalized_device_pixel_ratio(
      grid_view_->viewport()->devicePixelRatioF());
  const bool context_changed = model_->set_preview_raster_context(
      grid_view_->iconSize(), device_pixel_ratio);
  if (force_refresh && !context_changed) {
    model_->invalidate_preview_rasters();
  }
  if (context_changed || force_refresh) {
    grid_view_->viewport()->update();
  }
}

void QtMaterialBrowserWindow::update_tile_size_text() {
  const QString value = QLocale().toString(tile_size());
  const QString text = labels_.tile_size_value.arg(value);
  tile_slider_->setToolTip(text);
  tile_slider_->setAccessibleName(text);
  if (thumbnail_size_value_ != nullptr) {
    thumbnail_size_value_->setText(text);
  }
}

void QtMaterialBrowserWindow::refresh_content_page() {
  if (model_ == nullptr || proxy_ == nullptr || content_stack_ == nullptr) {
    return;
  }
  if (proxy_->rowCount() == 0) {
    const bool usage_removed_results =
        purpose_ != QtMaterialBrowserPurpose::Picker &&
        usage_mode_ != QtMaterialBrowserUsageMode::All &&
        proxy_->has_non_usage_match();
    const QString text = usage_mode_ == QtMaterialBrowserUsageMode::NotUsed &&
                                 !usage_snapshot_.complete &&
                                 purpose_ != QtMaterialBrowserPurpose::Picker
                             ? labels_.usage_incomplete
                         : usage_removed_results
                             ? labels_.filtered_empty
                         : !search_->text().trimmed().isEmpty()
                             ? labels_.no_results
                         : selected_folder() != QStringLiteral("Materials")
                             ? labels_.empty_folder
                             : labels_.empty_catalog;
    empty_label_->setText(text);
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
      purpose_ != QtMaterialBrowserPurpose::Picker;
  setWindowTitle(normal ? labels_.window_title : labels_.picker_window_title);
  if (import_button_ != nullptr) {
    import_button_->setVisible(normal);
  }
  if (import_tool_button_ != nullptr) {
    import_tool_button_->setVisible(normal);
  }
  if (accept_button_ != nullptr) {
    accept_button_->setVisible(!normal);
    accept_button_->setText(labels_.accept);
    accept_button_->setAccessibleName(labels_.accept);
  }
  filter_button_->setVisible(normal);
  proxy_->set_usage_mode(normal ? usage_mode_
                                : QtMaterialBrowserUsageMode::All);
  const auto selection_mode = normal ? QAbstractItemView::ExtendedSelection
                                    : QAbstractItemView::SingleSelection;
  grid_view_->setSelectionMode(selection_mode);
  list_view_->setSelectionMode(selection_mode);
  grid_view_->setDragEnabled(normal);
  list_view_->setDragEnabled(normal);
  folder_tree_->setDragEnabled(normal);
  folder_tree_->setAcceptDrops(normal);
  static_cast<MaterialFolderTree *>(folder_tree_)->organization_enabled = normal;
  model_->set_drag_context(project_generation_, normal);
  refresh_usage_presentation();
  refresh_content_page();
  update_accept_enabled();
}

void QtMaterialBrowserWindow::update_accept_enabled() {
  if (accept_button_ == nullptr || grid_view_ == nullptr) {
    return;
  }
  const QtMaterialBrowserItem *item =
      item_for_index(grid_view_->selectionModel()->currentIndex());
  const bool accepts = purpose_ == QtMaterialBrowserPurpose::Picker;
  const bool enabled = accepts && item != nullptr && item->valid();
  if (accept_button_->isEnabled() != enabled) {
    accept_button_->setEnabled(enabled);
  }
}

void QtMaterialBrowserWindow::select_material(
    const QuaderMaterialReference &reference) {
  QItemSelectionModel *selection = grid_view_->selectionModel();
  if (selected_material() == reference) {
    update_accept_enabled();
    return;
  }
  if (!reference.valid()) {
    selection->clear();
    update_accept_enabled();
    return;
  }
  for (int row = 0; row < proxy_->rowCount(); ++row) {
    const QModelIndex index = proxy_->index(row, 0);
    const QtMaterialBrowserItem *item = item_for_index(index);
    if (item != nullptr && item->reference == reference) {
      selection->setCurrentIndex(
          index, (purpose_ == QtMaterialBrowserPurpose::Picker
                      ? QItemSelectionModel::ClearAndSelect
                      : QItemSelectionModel::Select) |
                     QItemSelectionModel::Rows);
      return;
    }
  }
  selection->clear();
  update_accept_enabled();
}

void QtMaterialBrowserWindow::set_selected_folder(QString folder) {
  folder = normalized_folder(std::move(folder));
  while (!folder.isEmpty()) {
    for (QTreeWidgetItemIterator it(folder_tree_); *it != nullptr; ++it) {
      if ((*it)->data(0, kFolderPathRole).toString() == folder) {
        folder_tree_->setCurrentItem(*it);
        return;
      }
    }
    const qsizetype separator = folder.lastIndexOf(QLatin1Char('/'));
    folder = separator < 0 ? QString{} : folder.left(separator);
  }
  folder_tree_->setCurrentItem(folder_tree_->topLevelItem(0));
}

void QtMaterialBrowserWindow::follow_moved_folder(const QString &source,
                                                  const QString &destination) {
  if (normalized_folder(source).isEmpty() ||
      normalized_folder(destination).isEmpty()) {
    return;
  }
  for (QTreeWidgetItemIterator it(folder_tree_); *it != nullptr; ++it) {
    const QString path = (*it)->data(0, kFolderPathRole).toString();
    if (path == source || path.startsWith(source + QLatin1Char('/'))) {
      const QString replacement = destination + path.mid(source.size());
      (*it)->setData(0, kFolderPathRole, replacement);
    }
  }
}

void QtMaterialBrowserWindow::rebuild_folder_tree() {
  QString selection = selected_folder();
  std::set<QString> expanded;
  for (QTreeWidgetItemIterator it(folder_tree_); *it != nullptr; ++it) {
    if ((*it)->isExpanded()) {
      expanded.insert((*it)->data(0, kFolderPathRole).toString());
    }
  }
  std::set<QString> paths(folders_.begin(), folders_.end());
  for (const auto &material : model_->items()) {
    QString path = material.project_relative_path;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const qsizetype separator = path.lastIndexOf(QLatin1Char('/'));
    path = normalized_folder(path.left(std::max<qsizetype>(0, separator)));
    if (!path.isEmpty() && path != QStringLiteral("Materials")) {
      paths.insert(std::move(path));
    }
  }
  for (const QString &path : std::vector<QString>(paths.begin(), paths.end())) {
    QString parent = path;
    while (parent.contains(QLatin1Char('/'))) {
      parent.truncate(parent.lastIndexOf(QLatin1Char('/')));
      if (parent != QStringLiteral("Materials")) {
        paths.insert(parent);
      }
    }
  }
  std::vector<QString> sorted(paths.begin(), paths.end());
  std::sort(sorted.begin(), sorted.end(), [](const QString &left, const QString &right) {
    const int order = QString::compare(left, right, Qt::CaseInsensitive);
    return order == 0 ? left < right : order < 0;
  });
  {
    const QSignalBlocker blocker(folder_tree_);
    folder_tree_->clear();
    auto *all = new QTreeWidgetItem(folder_tree_, {labels_.all});
    all->setData(0, kFolderPathRole, QStringLiteral("Materials"));
    all->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
    all->setIcon(0, style()->standardIcon(QStyle::SP_DirHomeIcon));
    std::map<QString, QTreeWidgetItem *> nodes;
    for (const QString &path : sorted) {
      const qsizetype separator = path.lastIndexOf(QLatin1Char('/'));
      const QString parent = path.left(separator);
      const QString name = path.mid(separator + 1);
      QTreeWidgetItem *item = nullptr;
      if (parent == QStringLiteral("Materials")) {
        item = new QTreeWidgetItem(folder_tree_, {name});
      } else {
        const auto found = nodes.find(parent);
        if (found == nodes.end()) {
          continue;
        }
        item = new QTreeWidgetItem(found->second, {name});
      }
      item->setData(0, kFolderPathRole, path);
      item->setToolTip(0, path);
      item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                     Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
      item->setExpanded(expanded.contains(path));
      nodes.emplace(path, item);
    }
    set_selected_folder(std::move(selection));
  }
  proxy_->set_folder(selected_folder());
  refresh_content_page();
  update_accept_enabled();
}

void QtMaterialBrowserWindow::apply_usage_mode(
    QtMaterialBrowserUsageMode mode) {
  usage_mode_ = mode;
  proxy_->set_usage_mode(purpose_ != QtMaterialBrowserPurpose::Picker
                             ? mode
                             : QtMaterialBrowserUsageMode::All);
  {
    const QSignalBlocker blocker(filter_button_);
    filter_button_->setChecked(mode != QtMaterialBrowserUsageMode::All);
  }
  refresh_usage_presentation();
  refresh_content_page();
  update_accept_enabled();
}

void QtMaterialBrowserWindow::show_thumbnail_size_popup() {
  if (!thumbnail_size_popup_ || thumbnail_size_button_ == nullptr) {
    return;
  }
  if (thumbnail_size_popup_->isVisible()) {
    thumbnail_size_popup_->close();
    return;
  }
  if (filter_popup_) {
    filter_popup_->close();
  }
  if (QLayout *layout = thumbnail_size_popup_->layout()) {
    layout->setContentsMargins(ui_scale_metrics_.margins(QMargins(8, 8, 8, 8)));
    layout->setSpacing(ui_scale_metrics_.px(6));
  }
  show_popup_below(*thumbnail_size_popup_, *thumbnail_size_button_);
  tile_slider_->setFocus(Qt::PopupFocusReason);
}

void QtMaterialBrowserWindow::refresh_usage_presentation() {
  const bool browsing = purpose_ != QtMaterialBrowserPurpose::Picker;
  const QString chip_text = usage_mode_ == QtMaterialBrowserUsageMode::Used
                                ? labels_.filter_used : labels_.filter_not_used;
  if (usage_chip_label_->text() != chip_text) {
    usage_chip_label_->setText(chip_text);
  }
  if (filtered_by_->text() != labels_.filtered_by) {
    filtered_by_->setText(labels_.filtered_by);
  }
  const QString remove_label = labels_.remove_usage_filter.arg(chip_text);
  if (usage_chip_remove_->toolTip() != remove_label) {
    usage_chip_remove_->setToolTip(remove_label);
  }
  if (usage_chip_remove_->accessibleName() != remove_label) {
    usage_chip_remove_->setAccessibleName(remove_label);
  }
  const bool show_chip = browsing && host_ == QtMaterialBrowserHost::Embedded &&
                         usage_mode_ != QtMaterialBrowserUsageMode::All;
  if (active_filters_->isHidden() == show_chip) {
    active_filters_->setVisible(show_chip);
  }
  const QString filter_tooltip =
      show_chip ? labels_.filter_active.arg(labels_.filter) : labels_.filter;
  if (filter_button_->toolTip() != filter_tooltip) {
    filter_button_->setToolTip(filter_tooltip);
  }
  if (filter_button_->accessibleDescription() != filter_tooltip) {
    filter_button_->setAccessibleDescription(filter_tooltip);
  }
  QStringList messages = usage_snapshot_.errors;
  if (!usage_snapshot_.complete &&
      (usage_snapshot_.revision != 0 || usage_mode_ != QtMaterialBrowserUsageMode::All)) {
    messages.prepend(labels_.usage_incomplete);
  }
  messages.removeDuplicates();
  const QString status = browsing ? messages.join(QLatin1Char('\n')) : QString{};
  if (usage_status_->text() != status) {
    usage_status_->setText(status);
  }
  if (usage_status_->isHidden() == !status.isEmpty()) {
    usage_status_->setVisible(!status.isEmpty());
  }
}

void QtMaterialBrowserWindow::refresh_usage_badge_style() {
  if (usage_chip_remove_ == nullptr) {
    return;
  }
  active_filters_->layout()->setContentsMargins(0, 0, 0, 0);
  active_filters_->layout()->setSpacing(ui_scale_metrics_.px(6));
  usage_chip_->layout()->setContentsMargins(
      ui_scale_metrics_.margins(QMargins(7, 2, 2, 2)));
  usage_chip_->layout()->setSpacing(ui_scale_metrics_.px(3));
  usage_chip_remove_->setFixedSize(ui_scale_metrics_.size(QSize(20, 20)));
  usage_chip_remove_->setIconSize(ui_scale_metrics_.size(QSize(12, 12)));
  usage_chip_remove_->setIcon(qt_scalable_glyph_icon(
      QtScalableGlyph::Close, palette().color(QPalette::WindowText)));
  static_cast<UsageFilterBadge *>(usage_chip_)->apply_ui_scale(ui_scale_metrics_);
}

void QtMaterialBrowserWindow::show_filter_popup() {
  QPointer<QtMaterialBrowserWindow> guard(this);
  const auto restore_button_state = [guard]() {
    if (!guard) {
      return;
    }
    const QSignalBlocker blocker(guard->filter_button_);
    guard->filter_button_->setChecked(
        guard->usage_mode_ != QtMaterialBrowserUsageMode::All);
  };
  if (purpose_ == QtMaterialBrowserPurpose::Picker) {
    restore_button_state();
    return;
  }
  if (filter_popup_) {
    if (filter_popup_->isVisible()) {
      filter_popup_->close();
      restore_button_state();
      return;
    }
    // A choice closes immediately, but Qt deletes the native popup later.
    // Keep it tracked until then so browser teardown retires it safely; an
    // immediate reopen can dispose it before constructing its replacement.
    delete filter_popup_.data();
    if (!guard) {
      return;
    }
  }

  if (thumbnail_size_popup_) {
    thumbnail_size_popup_->close();
  }
  const std::uint64_t generation = project_generation_;
  const std::uint64_t usage_revision = usage_snapshot_.revision;
  auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
  filter_popup_ = popup;
  popup->setObjectName(QStringLiteral("materialBrowser.filterPopup"));
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setFrameShape(QFrame::StyledPanel);
  popup->setFrameShadow(QFrame::Raised);

  auto *layout = new QVBoxLayout(popup);
  layout->setContentsMargins(ui_scale_metrics_.margins(QMargins(8, 8, 8, 8)));
  layout->setSpacing(ui_scale_metrics_.px(6));
  auto *title = new QLabel(labels_.filter_title, popup);
  title->setObjectName(QStringLiteral("materialBrowser.filterTitle"));
  layout->addWidget(title);

  auto *used = new QCheckBox(labels_.filter_used, popup);
  used->setObjectName(QStringLiteral("materialBrowser.filterUsed"));
  auto *not_used = new QCheckBox(labels_.filter_not_used, popup);
  not_used->setObjectName(QStringLiteral("materialBrowser.filterNotUsed"));
  not_used->setEnabled(usage_snapshot_.complete);
  if (!usage_snapshot_.complete) {
    not_used->setToolTip(labels_.usage_incomplete);
  }
  auto *group = new QButtonGroup(popup);
  group->setExclusive(true);
  group->addButton(used);
  group->addButton(not_used);
  used->setChecked(usage_mode_ == QtMaterialBrowserUsageMode::Used);
  not_used->setChecked(usage_mode_ == QtMaterialBrowserUsageMode::NotUsed);
  layout->addWidget(used);
  layout->addWidget(not_used);

  const QPointer<QWidget> popup_guard(popup);
  const auto dismiss_embedded_popup = [guard, popup_guard]() {
    if (popup_guard) {
      popup_guard->close();
    }
    if (guard && guard->filter_popup_ == popup_guard) {
      guard->filter_button_->setFocus(Qt::PopupFocusReason);
    }
  };
  const auto apply_choice =
      [guard, popup_guard, generation, usage_revision](QtMaterialBrowserUsageMode mode) {
        if (guard && popup_guard && guard->filter_popup_ == popup_guard &&
            popup_guard->isVisible() &&
            generation == guard->project_generation_ &&
            usage_revision == guard->usage_snapshot_.revision &&
            guard->purpose_ != QtMaterialBrowserPurpose::Picker) {
          guard->apply_usage_mode(mode);
        }
      };
  if (host_ == QtMaterialBrowserHost::Embedded) {
    QObject::connect(used, &QCheckBox::clicked, popup,
                     [apply_choice, dismiss_embedded_popup]() {
                       apply_choice(QtMaterialBrowserUsageMode::Used);
                       dismiss_embedded_popup();
                     });
    QObject::connect(not_used, &QCheckBox::clicked, popup,
                     [apply_choice, dismiss_embedded_popup]() {
                       apply_choice(QtMaterialBrowserUsageMode::NotUsed);
                       dismiss_embedded_popup();
                     });
  } else {
    auto *button_layout = new QHBoxLayout();
    button_layout->setContentsMargins(0, 0, 0, 0);
    auto *clear = new QPushButton(labels_.filter_clear, popup);
    auto *cancel = new QPushButton(labels_.filter_cancel, popup);
    auto *apply = new QPushButton(labels_.filter_apply, popup);
    clear->setObjectName(QStringLiteral("materialBrowser.filterClear"));
    apply->setObjectName(QStringLiteral("materialBrowser.filterApply"));
    cancel->setObjectName(QStringLiteral("materialBrowser.filterCancel"));
    button_layout->addWidget(clear);
    button_layout->addStretch(1);
    button_layout->addWidget(cancel);
    button_layout->addWidget(apply);
    layout->addLayout(button_layout);
    QObject::connect(clear, &QPushButton::clicked, popup,
                     [group, used, not_used]() {
                       group->setExclusive(false);
                       used->setChecked(false);
                       not_used->setChecked(false);
                       group->setExclusive(true);
                     });
    QObject::connect(cancel, &QPushButton::clicked, popup, &QWidget::close);
    QObject::connect(apply, &QPushButton::clicked, popup,
                     [apply_choice, popup_guard, used, not_used]() {
                       apply_choice(used->isChecked()
                                        ? QtMaterialBrowserUsageMode::Used
                                    : not_used->isChecked()
                                        ? QtMaterialBrowserUsageMode::NotUsed
                                        : QtMaterialBrowserUsageMode::All);
                       if (popup_guard) {
                         popup_guard->close();
                       }
                     });
  }
  auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), popup);
  escape->setContext(Qt::WidgetWithChildrenShortcut);
  if (host_ == QtMaterialBrowserHost::Embedded) {
    QObject::connect(escape, &QShortcut::activated, popup,
                     dismiss_embedded_popup);
  } else {
    QObject::connect(escape, &QShortcut::activated, popup, &QWidget::close);
  }
  QObject::connect(popup, &QObject::destroyed, this,
                   [restore_button_state]() { restore_button_state(); });

  restore_button_state();
  popup->setMinimumWidth(ui_scale_metrics_.px(240));
  show_popup_below(*popup, *filter_button_);
  (usage_mode_ == QtMaterialBrowserUsageMode::NotUsed ? not_used : used)
      ->setFocus(Qt::PopupFocusReason);
}

std::vector<QuaderMaterialReference>
QtMaterialBrowserWindow::selected_materials() const {
  std::vector<QuaderMaterialReference> selected;
  if (grid_view_ == nullptr || grid_view_->selectionModel() == nullptr) {
    return selected;
  }
  const QModelIndexList indexes = grid_view_->selectionModel()->selectedRows(
      QtMaterialBrowserModel::NameColumn);
  selected.reserve(static_cast<std::size_t>(indexes.size()));
  for (const QModelIndex &index : indexes) {
    const QtMaterialBrowserItem *item = item_for_index(index);
    if (item != nullptr && item->valid()) {
      selected.push_back(item->reference);
    }
  }
  return selected;
}

QStringList QtMaterialBrowserWindow::selected_material_ids() const {
  QStringList selected;
  if (grid_view_ == nullptr || grid_view_->selectionModel() == nullptr) {
    return selected;
  }
  for (const QModelIndex &index : grid_view_->selectionModel()->selectedRows(
           QtMaterialBrowserModel::NameColumn)) {
    selected.push_back(
        index.data(QtMaterialBrowserModel::StableIdRole).toString());
  }
  return selected;
}

bool QtMaterialBrowserWindow::is_protected_material(
    const QtMaterialBrowserItem &item) const {
  if (protected_material_.valid()) {
    return item.reference == protected_material_;
  }
  return item.project_relative_path.compare(
             QStringLiteral("Materials/default_material.qmat"),
             Qt::CaseInsensitive) == 0;
}

void QtMaterialBrowserWindow::show_material_menu(
    const QModelIndex &target_index, const QPoint &global_position) {
  if (purpose_ == QtMaterialBrowserPurpose::Picker) {
    return;
  }
  const QtMaterialBrowserItem *target_item = item_for_index(target_index);

  const QuaderMaterialReference target = target_item != nullptr
                                             ? target_item->reference
                                             : QuaderMaterialReference{};
  const QString target_id =
      target_item != nullptr ? target_item->stable_id : QString{};
  const QString target_name =
      target_item != nullptr ? target_item->name : QString{};
  std::vector<QuaderMaterialReference> selected = selected_materials();
  const QStringList selected_ids = selected_material_ids();
  const QString destination = selected_folder();
  const std::uint64_t generation = project_generation_;
  const std::uint64_t dependency_revision = dependency_revision_;
  const std::uint64_t presentation_revision = material_presentation_revision_;
  const std::uint64_t action_eligibility_revision =
      action_eligibility_revision_;
  const bool action_handler = static_cast<bool>(material_action_);
  const bool texture_available = target_item != nullptr &&
                                 target_item->available &&
                                 target_item->reference.project_owned() &&
                                 target_item->has_valid_albedo;
  const bool target_protected =
      target_item != nullptr && is_protected_material(*target_item);
  const auto valid_batch_item =
      [this](const QuaderMaterialReference &reference) {
        const auto found =
            std::find_if(model_->items().begin(), model_->items().end(),
                         [&reference](const QtMaterialBrowserItem &item) {
                           return item.reference == reference;
                         });
        return found != model_->items().end() && found->valid() &&
               found->available && found->reference.project_owned();
      };
  const bool valid_batch =
      !selected.empty() &&
      std::all_of(selected.begin(), selected.end(), valid_batch_item);
  const bool protected_batch =
      valid_batch &&
      std::any_of(
          selected.begin(), selected.end(), [this](const auto &reference) {
            const auto found =
                std::find_if(model_->items().begin(), model_->items().end(),
                             [&reference](const QtMaterialBrowserItem &item) {
                               return item.reference == reference;
                             });
            return found != model_->items().end() &&
                   is_protected_material(*found);
          });

  QPointer<QMenu> menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("materialBrowser.contextMenu"));
  QPointer<QAction> edit_material;
  QPointer<QAction> copy_texture_name;
  QPointer<QAction> copy_texture_path;
  QPointer<QAction> open_file_location;
  QPointer<QAction> show_texture_info;
  QPointer<QAction> rename;
  QPointer<QAction> duplicate;
  QPointer<QAction> cut;
  QPointer<QAction> copy;
  QPointer<QAction> paste;
  QPointer<QAction> remove;
  const auto add_action = [&menu](const QString &text,
                                  const QString &object_name) {
    QAction *action = menu->addAction(text);
    action->setObjectName(object_name);
    return action;
  };
  if (target_item != nullptr) {
    edit_material = add_action(labels_.edit_material,
                               QStringLiteral("materialBrowser.action.editMaterial"));
    edit_material->setEnabled(static_cast<bool>(open_material_) &&
                               target_item->valid() && target_item->available);
    copy_texture_name =
        add_action(labels_.copy_texture_name,
                   QStringLiteral("materialBrowser.action.copyTextureName"));
    copy_texture_path =
        add_action(labels_.copy_texture_path,
                   QStringLiteral("materialBrowser.action.copyTexturePath"));
    menu->addSeparator();
    open_file_location =
        add_action(labels_.open_file_location,
                   QStringLiteral("materialBrowser.action.openFileLocation"));
    show_texture_info =
        add_action(labels_.show_texture_info,
                   QStringLiteral("materialBrowser.action.showTextureInfo"));
    menu->addSeparator();
    rename = add_action(labels_.rename_material,
                        QStringLiteral("materialBrowser.action.rename"));
    duplicate = add_action(labels_.duplicate_material,
                           QStringLiteral("materialBrowser.action.duplicate"));
    cut = add_action(labels_.cut_material,
                     QStringLiteral("materialBrowser.action.cut"));
    copy = add_action(labels_.copy_material,
                      QStringLiteral("materialBrowser.action.copy"));
    paste = add_action(labels_.paste_material,
                       QStringLiteral("materialBrowser.action.paste"));
    menu->addSeparator();
    remove = add_action(labels_.delete_material,
                        QStringLiteral("materialBrowser.action.delete"));

    copy_texture_name->setEnabled(action_handler && texture_available);
    copy_texture_path->setEnabled(action_handler && texture_available);
    open_file_location->setEnabled(action_handler && texture_available);
    show_texture_info->setEnabled(action_handler && texture_available);
    rename->setEnabled(action_handler && target_item->valid() &&
                       target_item->available &&
                       target_item->reference.project_owned() &&
                       !target_protected);
    duplicate->setEnabled(action_handler && valid_batch);
    cut->setEnabled(action_handler && valid_batch && !protected_batch);
    copy->setEnabled(action_handler && valid_batch);
    remove->setEnabled(action_handler && valid_batch && !protected_batch);
  } else {
    paste = add_action(labels_.paste_material,
                       QStringLiteral("materialBrowser.action.paste"));
  }
  paste->setEnabled(action_handler && material_paste_available_);

  QPointer<QtMaterialBrowserWindow> guard(this);
  QAction *chosen = menu->exec(global_position);
  QtMaterialBrowserAction action = QtMaterialBrowserAction::CopyTextureName;
  bool recognized_action = true;
  bool edit_requested = false;
  if (!menu || chosen == nullptr) {
    recognized_action = false;
  } else if (chosen == edit_material.data()) {
    edit_requested = true;
  } else if (chosen == copy_texture_name.data()) {
    action = QtMaterialBrowserAction::CopyTextureName;
  } else if (chosen == copy_texture_path.data()) {
    action = QtMaterialBrowserAction::CopyTexturePath;
  } else if (chosen == open_file_location.data()) {
    action = QtMaterialBrowserAction::OpenFileLocation;
  } else if (chosen == show_texture_info.data()) {
    action = QtMaterialBrowserAction::ShowTextureInfo;
  } else if (chosen == rename.data()) {
    action = QtMaterialBrowserAction::Rename;
  } else if (chosen == duplicate.data()) {
    action = QtMaterialBrowserAction::Duplicate;
  } else if (chosen == cut.data()) {
    action = QtMaterialBrowserAction::Cut;
  } else if (chosen == copy.data()) {
    action = QtMaterialBrowserAction::Copy;
  } else if (chosen == paste.data()) {
    action = QtMaterialBrowserAction::Paste;
  } else if (chosen == remove.data()) {
    action = QtMaterialBrowserAction::Delete;
  } else {
    recognized_action = false;
  }
  if (menu) {
    delete menu.data();
  }

  const auto context_is_current =
      [&guard, generation, dependency_revision, presentation_revision,
       action_eligibility_revision, target_id, selected_ids, destination]() {
    if (!guard || generation != guard->project_generation_ ||
        dependency_revision != guard->dependency_revision_ ||
        presentation_revision != guard->material_presentation_revision_ ||
        action_eligibility_revision != guard->action_eligibility_revision_ ||
        guard->purpose_ == QtMaterialBrowserPurpose::Picker ||
        guard->selected_material_ids() != selected_ids ||
        guard->selected_folder() != destination) {
      return false;
    }
    if (target_id.isEmpty()) {
      return true;
    }
    const QtMaterialBrowserItem *current =
        guard->item_for_index(
            guard->grid_view_->selectionModel()->currentIndex());
    return current != nullptr && current->stable_id == target_id;
  };
  if (!recognized_action || !context_is_current()) {
    return;
  }
  if (edit_requested) {
    const OpenMaterialCallback callback = guard->open_material_;
    if (callback) {
      callback(target);
    }
    return;
  }
  if (!guard->material_action_) {
    return;
  }

  if (action == QtMaterialBrowserAction::Rename) {
    QPointer<QInputDialog> rename_dialog = new QInputDialog(guard.data());
    rename_dialog->setObjectName(
        QStringLiteral("materialBrowser.renameDialog"));
    rename_dialog->setWindowTitle(guard->labels_.rename_material_title);
    rename_dialog->setLabelText(guard->labels_.material_name);
    rename_dialog->setInputMode(QInputDialog::TextInput);
    rename_dialog->setTextEchoMode(QLineEdit::Normal);
    rename_dialog->setTextValue(target_name);
    const int outcome = rename_dialog->exec();
    const bool dialog_survived = !rename_dialog.isNull();
    const QString proposed_name =
        dialog_survived ? rename_dialog->textValue() : QString{};
    if (rename_dialog) {
      delete rename_dialog.data();
    }
    if (!dialog_survived || outcome != QDialog::Accepted ||
        !context_is_current() || !guard->material_action_) {
      return;
    }
    MaterialActionCallback callback = guard->material_action_;
    callback({
        .action = QtMaterialBrowserAction::Rename,
        .target = target,
        .selected_materials = std::move(selected),
        .destination_folder = destination,
        .proposed_name = proposed_name,
        .project_generation = generation,
        .dependency_revision = dependency_revision,
        .presentation_revision = presentation_revision,
    });
    return;
  }
  MaterialActionCallback callback = guard->material_action_;
  callback({
      .action = action,
      .target = target,
      .selected_materials = std::move(selected),
      .destination_folder = destination,
      .project_generation = generation,
      .dependency_revision = dependency_revision,
      .presentation_revision = presentation_revision,
  });
}

void QtMaterialBrowserWindow::show_folder_menu(const QPoint &position) {
  if (purpose_ == QtMaterialBrowserPurpose::Picker) {
    return;
  }
  if (auto *item = folder_tree_->itemAt(position)) {
    folder_tree_->setCurrentItem(item);
  }
  const QString folder = selected_folder();
  const std::uint64_t generation = project_generation_;
  const std::uint64_t action_eligibility_revision =
      action_eligibility_revision_;
  QPointer<QMenu> menu = new QMenu(this);
  QPointer<QAction> create = menu->addAction(labels_.new_folder);
  create->setEnabled(static_cast<bool>(create_folder_));
  QPointer<QAction> rename = menu->addAction(labels_.rename_folder);
  QPointer<QAction> remove = menu->addAction(labels_.delete_folder);
  const bool root = folder == QStringLiteral("Materials");
  rename->setEnabled(!root && static_cast<bool>(rename_folder_));
  remove->setEnabled(!root && static_cast<bool>(delete_folder_));
  QPointer<QtMaterialBrowserWindow> guard(this);
  QAction *chosen =
      menu->exec(folder_tree_->viewport()->mapToGlobal(position));
  enum class FolderAction : std::uint8_t { None, Create, Rename, Delete };
  FolderAction action = FolderAction::None;
  if (menu && chosen == create.data()) {
    action = FolderAction::Create;
  } else if (menu && chosen == rename.data()) {
    action = FolderAction::Rename;
  } else if (menu && chosen == remove.data()) {
    action = FolderAction::Delete;
  }
  if (menu) {
    delete menu.data();
  }
  const auto context_is_current =
      [&guard, generation, action_eligibility_revision, folder]() {
        return guard && generation == guard->project_generation_ &&
               action_eligibility_revision ==
                   guard->action_eligibility_revision_ &&
               guard->purpose_ != QtMaterialBrowserPurpose::Picker &&
               guard->selected_folder() == folder;
      };
  if (action == FolderAction::None || !context_is_current()) {
    return;
  }
  if (action == FolderAction::Delete && guard->delete_folder_ && !root) {
    DeleteFolderCallback callback = guard->delete_folder_;
    callback(folder);
    return;
  }
  if (action != FolderAction::Create && action != FolderAction::Rename) {
    return;
  }
  QPointer<QInputDialog> input = new QInputDialog(guard.data());
  input->setObjectName(QStringLiteral("materialBrowser.folderNameDialog"));
  input->setWindowTitle(action == FolderAction::Create
                            ? guard->labels_.new_folder
                            : guard->labels_.rename_folder);
  input->setLabelText(guard->labels_.folder_name);
  input->setInputMode(QInputDialog::TextInput);
  input->setTextEchoMode(QLineEdit::Normal);
  input->setTextValue(
      action == FolderAction::Rename
          ? folder.mid(folder.lastIndexOf(QLatin1Char('/')) + 1)
          : QString{});
  const int outcome = input->exec();
  const bool dialog_survived = !input.isNull();
  const QString name = dialog_survived ? input->textValue() : QString{};
  if (input) {
    delete input.data();
  }
  if (!dialog_survived || outcome != QDialog::Accepted ||
      !context_is_current()) {
    return;
  }
  if (action == FolderAction::Create && guard->create_folder_) {
    CreateFolderCallback callback = guard->create_folder_;
    callback(folder, name);
  } else if (action == FolderAction::Rename && guard->rename_folder_ &&
             !root) {
    RenameFolderCallback callback = guard->rename_folder_;
    callback(folder, name);
  }
}

bool QtMaterialBrowserWindow::receive_folder_drop(const QMimeData *data,
                                                  const QString &destination,
                                                  bool execute) {
  if (data == nullptr || purpose_ == QtMaterialBrowserPurpose::Picker ||
      normalized_folder(destination).isEmpty()) {
    return false;
  }
  if (data->hasFormat(QString::fromLatin1(kFolderDragMime))) {
    QDataStream stream(data->data(QString::fromLatin1(kFolderDragMime)));
    quint64 generation = 0;
    QString source;
    stream >> generation >> source;
    if (stream.status() != QDataStream::Ok || !stream.atEnd() ||
        generation != project_generation_ || !move_folder_ ||
        source == QStringLiteral("Materials") || normalized_folder(source).isEmpty() ||
        destination == source || destination.startsWith(source + QLatin1Char('/'))) {
      return false;
    }
    bool source_exists = false;
    for (QTreeWidgetItemIterator it(folder_tree_); *it != nullptr; ++it) {
      source_exists |= (*it)->data(0, kFolderPathRole).toString() == source;
    }
    if (!source_exists) {
      return false;
    }
    if (execute) {
      move_folder_(source, destination, generation);
    }
    return true;
  }
  if (!data->hasFormat(QString::fromLatin1(kMaterialDragMime)) || !move_materials_) {
    return false;
  }
  QDataStream stream(data->data(QString::fromLatin1(kMaterialDragMime)));
  quint64 generation = 0;
  QStringList identities;
  QStringList paths;
  stream >> generation >> identities >> paths;
  if (stream.status() != QDataStream::Ok || !stream.atEnd() ||
      generation != project_generation_ || identities.isEmpty() ||
      identities.size() != paths.size()) {
    return false;
  }
  std::vector<QuaderMaterialReference> references;
  std::set<QString> seen;
  for (qsizetype index = 0; index < identities.size(); ++index) {
    const auto found = std::find_if(model_->items().begin(), model_->items().end(),
        [&](const auto &item) {
          return item.stable_id == identities[index] &&
                 item.project_relative_path == paths[index] &&
                 item.valid() && item.available && item.reference.project_owned();
        });
    if (found == model_->items().end() || !seen.insert(identities[index]).second) {
      return false;
    }
    references.push_back(found->reference);
  }
  if (execute) {
    move_materials_(std::move(references), destination, generation);
  }
  return true;
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

void QtMaterialBrowserWindow::assign_index(const QModelIndex &index) {
  const QtMaterialBrowserItem *item = item_for_index(index);
  if (host_ != QtMaterialBrowserHost::Embedded ||
      purpose_ != QtMaterialBrowserPurpose::FaceAssignment ||
      !assignment_available_ || item == nullptr || !item->valid() ||
      !assign_material_) {
    return;
  }
  const QuaderMaterialReference reference = item->reference;
  const AssignMaterialCallback callback = assign_material_;
  callback(reference);
}

void QtMaterialBrowserWindow::open_index(const QModelIndex &index) {
  const QtMaterialBrowserItem *item = item_for_index(index);
  if (purpose_ == QtMaterialBrowserPurpose::Picker ||
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
  if (purpose_ == QtMaterialBrowserPurpose::FaceAssignment) {
    assign_index(grid_view_->selectionModel()->currentIndex());
  } else if (purpose_ == QtMaterialBrowserPurpose::Picker) {
    const QuaderMaterialReference reference = item->reference;
    const AcceptMaterialCallback callback = accept_material_;
    QPointer<QtMaterialBrowserWindow> guard(this);
    if (callback) {
      callback(reference);
    }
    if (guard) {
      guard->close();
    }
  }
}

} // namespace quader
