////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_asset_browser.hpp"

#include "ui/qt_application_style.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_spin_box.hpp"
#include "ui/qt_tree_relationship_presentation.hpp"

#include <QAbstractTableModel>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QIODevice>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPointer>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

namespace quader {
namespace {

constexpr auto kAssetMime = "application/x-quader-project-assets";
constexpr int kIdentityRole = Qt::UserRole + 1;
constexpr int kFolderRole = Qt::UserRole + 2;
constexpr quint32 kDragVersion = 1;

/** Settle readable toolbar minima after the native control receives its font. */
template <typename Base> class AssetToolbarField final : public Base {
public:
  using Base::Base;

  QSize minimumSizeHint() const override {
    if constexpr (std::is_same_v<Base, QComboBox>) {
      // Qt invalidates sizeHint on FontChange, but retains a separate cached
      // minimumSizeHint. AdjustToContents uses the same measure for both.
      return this->sizeHint();
    }
    return Base::minimumSizeHint();
  }

  void refresh_layout() {
    if (refreshing_) {
      return;
    }
    refreshing_ = true;
    this->setMinimumWidth(this->sizeHint().width());
    this->updateGeometry();
    if (auto *parent = this->parentWidget(); parent && parent->layout()) {
      parent->layout()->invalidate();
      parent->layout()->activate();
    }
    refreshing_ = false;
  }

protected:
  bool event(QEvent *event) override {
    const bool handled = Base::event(event);
    if (event->type() == QEvent::FontChange ||
        event->type() == QEvent::StyleChange) {
      refresh_layout();
    }
    return handled;
  }

private:
  bool refreshing_ = false;
};

QString parent_folder(const QString &path) {
  const auto separator = path.lastIndexOf(QLatin1Char('/'));
  return separator < 0 ? QString{} : path.left(separator);
}

QString clean_folder(QString path) {
  path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (path == QStringLiteral(".")) {
    return {};
  }
  while (path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  if (path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char(':'))) {
    return {};
  }
  for (const auto &part : path.split(QLatin1Char('/'))) {
    if (part == QStringLiteral("..") || part == QStringLiteral(".")) {
      return {};
    }
  }
  return path;
}

QString basename(const QtAssetBrowserItem &item) {
  const QString leaf = item.relative_path.section(QLatin1Char('/'), -1);
  return item.kind == QtAssetBrowserKind::Folder ? leaf
                                               : QFileInfo(leaf).completeBaseName();
}

bool valid_basename(const QString &name) {
  if (name.isEmpty() || name.trimmed() != name || name == QStringLiteral(".") ||
      name == QStringLiteral("..") || name.endsWith(QLatin1Char('.'))) {
    return false;
  }
  return std::none_of(name.begin(), name.end(), [](QChar c) {
    return c.unicode() < 32 || QStringLiteral("/\\:*?\"<>|").contains(c);
  });
}

QMimeData *asset_mime(const std::vector<QtAssetBrowserItem> &items,
                     std::uint64_t generation) {
  auto *mime = new QMimeData;
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream << kDragVersion << quint64(generation) << quint32(items.size());
  for (const auto &item : items) {
    stream << item.id << item.relative_path << quint8(item.kind);
  }
  mime->setData(QString::fromLatin1(kAssetMime), bytes);
  return mime;
}

bool read_asset_mime(const QMimeData *mime,
                     std::vector<QtAssetBrowserItem> &items,
                     std::uint64_t &generation) {
  if (mime == nullptr || !mime->hasFormat(QString::fromLatin1(kAssetMime))) {
    return false;
  }
  const QByteArray bytes = mime->data(QString::fromLatin1(kAssetMime));
  if (bytes.size() > 4 * 1024 * 1024) {
    return false;
  }
  QDataStream stream(bytes);
  quint32 version = 0;
  quint32 count = 0;
  quint64 source_generation = 0;
  stream >> version >> source_generation >> count;
  if (version != kDragVersion || count == 0 || count > 65536) {
    return false;
  }
  std::vector<QtAssetBrowserItem> decoded;
  decoded.reserve(count);
  for (quint32 i = 0; i < count; ++i) {
    QtAssetBrowserItem item;
    quint8 kind = 0;
    stream >> item.id >> item.relative_path >> kind;
    if (stream.status() != QDataStream::Ok || item.id.isEmpty() ||
        item.relative_path.isEmpty() ||
        kind > quint8(QtAssetBrowserKind::Texture)) {
      return false;
    }
    item.kind = static_cast<QtAssetBrowserKind>(kind);
    decoded.push_back(std::move(item));
  }
  if (!stream.atEnd()) {
    return false;
  }
  items = std::move(decoded);
  generation = source_generation;
  return true;
}

QIcon asset_icon(QtAssetBrowserKind kind, const QPalette &palette) {
  QImage image(192, 192, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(8.0, 8.0);
  QPen pen(palette.color(QPalette::Text), 1.0);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(palette.color(QPalette::AlternateBase));
  QPainterPath path;
  if (kind == QtAssetBrowserKind::Folder) {
    QStyleOption option;
    option.palette = palette;
    const bool themed = qt_application_style_active(&option);
    // Folder yellow is semantic artwork, with native text contrast as fallback.
    const QColor front = themed ? QColor(237, 187, 74) : palette.color(QPalette::Text);
    const QColor back = themed ? QColor(193, 143, 43) : palette.color(QPalette::Mid);
    painter.setPen(Qt::NoPen);
    painter.setBrush(back);
    path.moveTo(2, 7);
    path.quadTo(2, 5, 4, 5);
    path.lineTo(9, 5);
    path.lineTo(11.5, 7.5);
    path.lineTo(20, 7.5);
    path.quadTo(22, 7.5, 22, 9.5);
    path.lineTo(22, 18);
    path.quadTo(22, 20, 20, 20);
    path.lineTo(4, 20);
    path.quadTo(2, 20, 2, 18);
    path.closeSubpath();
    painter.drawPath(path);
    painter.setBrush(front);
    painter.drawRoundedRect(QRectF(2, 9, 20, 11), 1.6, 1.6);
  } else if (kind == QtAssetBrowserKind::Material) {
    painter.drawEllipse(QRectF(4, 4, 16, 16));
    painter.drawArc(QRectF(6, 6, 12, 12), 30 * 16, 90 * 16);
  } else {
    path.moveTo(5, 2);
    path.lineTo(14, 2);
    path.lineTo(19, 7);
    path.lineTo(19, 22);
    path.lineTo(5, 22);
    path.closeSubpath();
    if (kind == QtAssetBrowserKind::Scene) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(palette.color(QPalette::Text));
      painter.drawPath(path);
      QPainterPath fold(QPointF(14, 2));
      fold.lineTo(14, 7);
      fold.lineTo(19, 7);
      fold.closeSubpath();
      painter.setBrush(palette.color(QPalette::WindowText));
      painter.drawPath(fold);
      const QPixmap logo = QApplication::windowIcon().pixmap(96, 96);
      if (!logo.isNull()) {
        painter.drawPixmap(QRectF(8, 10, 8, 8), logo, logo.rect());
      } else {
        painter.setPen(QPen(palette.color(QPalette::Accent), 1.4));
        painter.drawRect(QRectF(9, 11, 6, 6));
        painter.drawLine(QPointF(13, 15), QPointF(16, 18));
      }
    } else {
      painter.drawPath(path);
      painter.drawLine(QPointF(14, 2), QPointF(14, 7));
      painter.drawLine(QPointF(14, 7), QPointF(19, 7));
      painter.drawEllipse(QRectF(8, 10, 2, 2));
      path = QPainterPath(QPointF(7, 19));
      path.lineTo(11, 14);
      path.lineTo(14, 17);
      path.lineTo(16, 15);
      painter.drawPath(path);
    }
  }
  painter.end();
  return qt_multiresolution_icon(image);
}

class AssetModel final : public QAbstractTableModel {
public:
  explicit AssetModel(QObject *parent) : QAbstractTableModel(parent) {}
  QtAssetBrowserSnapshot snapshot;
  QtAssetBrowserLabels labels;
  QtAssetBrowser::ThumbnailProvider thumbnail;
  std::function<void(const QtAssetBrowserItem &, QString)> rename;
  std::array<QIcon, 4> fallback;
  QSize preview_size{128, 128};
  qreal dpr = 1.0;
  bool picker = false;

  int rowCount(const QModelIndex &parent = {}) const override {
    return parent.isValid() ? 0 : static_cast<int>(snapshot.items.size());
  }
  int columnCount(const QModelIndex &parent = {}) const override {
    return parent.isValid() ? 0 : 3;
  }
  QVariant data(const QModelIndex &index, int role) const override {
    if (!index.isValid() || index.row() >= rowCount()) {
      return {};
    }
    const auto &item = snapshot.items[static_cast<std::size_t>(index.row())];
    if (role == kIdentityRole) {
      return item.id;
    }
    if (role == Qt::ToolTipRole) {
      return item.relative_path;
    }
    if (role == Qt::EditRole && index.column() == 0) {
      return basename(item);
    }
    if (role == Qt::DisplayRole) {
      if (index.column() == 0) {
        return item.name.isEmpty() ? basename(item) : item.name;
      }
      if (index.column() == 2) {
        return item.relative_path;
      }
      switch (item.kind) {
      case QtAssetBrowserKind::Folder: return labels.folder;
      case QtAssetBrowserKind::Scene: return labels.scene;
      case QtAssetBrowserKind::Material: return labels.material;
      case QtAssetBrowserKind::Texture: return labels.texture;
      }
    }
    if (role == Qt::DecorationRole && index.column() == 0) {
      return item.thumbnail.isNull()
                 ? fallback[static_cast<std::size_t>(item.kind)]
                 : item.thumbnail;
    }
    return {};
  }
  QVariant headerData(int section, Qt::Orientation orientation,
                       int role) const override {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
      return {};
    }
    switch (section) {
    case 0: return labels.name;
    case 1: return labels.type;
    case 2: return labels.path;
    default: return {};
    }
  }
  Qt::ItemFlags flags(const QModelIndex &index) const override {
    auto result = QAbstractTableModel::flags(index);
    if (index.isValid() && !picker) {
      result |= Qt::ItemIsDragEnabled;
      if (index.column() == 0 && snapshot.items[index.row()].editable) {
        result |= Qt::ItemIsEditable;
      }
    }
    return result;
  }
  bool setData(const QModelIndex &index, const QVariant &value,
                int role) override {
    if (role != Qt::EditRole || !index.isValid() || index.column() != 0 ||
        index.row() >= rowCount() || picker || !rename) {
      return false;
    }
    const auto item = snapshot.items[index.row()];
    const QString proposed = value.toString();
    if (!item.editable || !valid_basename(proposed)) {
      return false;
    }
    if (proposed != basename(item)) {
      rename(item, proposed);
    }
    return true;
  }
  QStringList mimeTypes() const override {
    return {QString::fromLatin1(kAssetMime)};
  }
  QMimeData *mimeData(const QModelIndexList &indexes) const override {
    std::set<int> rows;
    std::vector<QtAssetBrowserItem> items;
    for (const auto &index : indexes) {
      if (index.isValid() && rows.insert(index.row()).second) {
        items.push_back(snapshot.items[index.row()]);
      }
    }
    return asset_mime(items, snapshot.generation);
  }
  Qt::DropActions supportedDragActions() const override {
    return Qt::MoveAction | Qt::CopyAction;
  }
  void replace(QtAssetBrowserSnapshot value) {
    beginResetModel();
    snapshot = std::move(value);
    endResetModel();
  }
  bool update(QtAssetBrowserSnapshot value) {
    // Source rows keep their identity across filesystem sorting changes. The
    // proxy owns display order, so moving an asset never resets unrelated tiles.
    std::map<QString, QtAssetBrowserItem *> incoming;
    for (auto &item : value.items) {
      if (!incoming.emplace(item.id, &item).second) {
        // A malformed projection must not dereference an exhausted identity
        // lookup or publish a mismatched beginInsertRows range.
        replace(std::move(value));
        return true;
      }
    }
    std::set<QString> previous_ids;
    for (const auto &item : snapshot.items) {
      if (!previous_ids.insert(item.id).second) {
        replace(std::move(value));
        return true;
      }
    }
    bool folders_changed = false;
    for (int last = rowCount() - 1; last >= 0;) {
      if (incoming.contains(snapshot.items[last].id)) {
        --last;
        continue;
      }
      int first = last;
      while (first > 0 && !incoming.contains(snapshot.items[first - 1].id)) {
        --first;
      }
      for (int row = first; row <= last; ++row) {
        folders_changed |= snapshot.items[row].kind == QtAssetBrowserKind::Folder;
      }
      beginRemoveRows({}, first, last);
      snapshot.items.erase(snapshot.items.begin() + first,
                           snapshot.items.begin() + last + 1);
      endRemoveRows();
      last = first - 1;
    }
    for (int row = 0; row < rowCount(); ++row) {
      auto &previous = snapshot.items[row];
      const auto next = incoming.find(previous.id);
      auto &item = *next->second;
      const bool changed = previous.relative_path != item.relative_path ||
          previous.name != item.name || previous.kind != item.kind ||
          previous.content_revision != item.content_revision ||
          previous.editable != item.editable ||
          previous.thumbnail.cacheKey() != item.thumbnail.cacheKey();
      if (changed) {
        folders_changed |= (previous.kind == QtAssetBrowserKind::Folder ||
                            item.kind == QtAssetBrowserKind::Folder) &&
            (previous.relative_path != item.relative_path ||
             previous.name != item.name || previous.kind != item.kind);
        previous = std::move(item);
        emit dataChanged(index(row, 0), index(row, 2));
      }
      incoming.erase(next);
    }
    if (!incoming.empty()) {
      const int first = rowCount();
      beginInsertRows({}, first, first + static_cast<int>(incoming.size()) - 1);
      for (auto &item : value.items) {
        if (incoming.contains(item.id)) {
          folders_changed |= item.kind == QtAssetBrowserKind::Folder;
          snapshot.items.push_back(std::move(item));
        }
      }
      endInsertRows();
    }
    snapshot.generation = value.generation;
    snapshot.revision = value.revision;
    snapshot.dependency_revision = value.dependency_revision;
    return folders_changed;
  }
  void refresh_icons() {
    if (rowCount() > 0) {
      emit dataChanged(index(0, 0), index(rowCount() - 1, 0),
                       {Qt::DecorationRole});
    }
  }
};

class AssetProxy final : public QSortFilterProxyModel {
public:
  using QSortFilterProxyModel::QSortFilterProxyModel;
  QString folder;
  QString search;
  std::optional<QtAssetBrowserKind> kind;
  bool materials_available = true;
  void refresh() { invalidateFilter(); }

protected:
  bool filterAcceptsRow(int row, const QModelIndex &) const override {
    const auto *model = static_cast<const AssetModel *>(sourceModel());
    const auto &item = model->snapshot.items[row];
    if (!materials_available && (item.kind == QtAssetBrowserKind::Material ||
                                item.kind == QtAssetBrowserKind::Texture)) {
      return false;
    }
    if (search.isEmpty()) {
      if (parent_folder(item.relative_path) != folder) {
        return false;
      }
    } else {
      if (!folder.isEmpty() &&
          !item.relative_path.startsWith(folder + QLatin1Char('/'),
                                         Qt::CaseInsensitive)) {
        return false;
      }
      if (!item.name.contains(search, Qt::CaseInsensitive) &&
          !item.relative_path.contains(search, Qt::CaseInsensitive)) {
        return false;
      }
    }
    if (item.kind != QtAssetBrowserKind::Folder && kind && item.kind != *kind) {
      return false;
    }
    return true;
  }
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
    const auto *model = static_cast<const AssetModel *>(sourceModel());
    const auto &a = model->snapshot.items[left.row()];
    const auto &b = model->snapshot.items[right.row()];
    if ((a.kind == QtAssetBrowserKind::Folder) !=
        (b.kind == QtAssetBrowserKind::Folder)) {
      return a.kind == QtAssetBrowserKind::Folder;
    }
    const int comparison = QString::localeAwareCompare(
        left.data().toString().toCaseFolded(),
        right.data().toString().toCaseFolded());
    return comparison == 0 ? a.relative_path < b.relative_path : comparison < 0;
  }
};

/** Measures the live font instead of retaining a previous scale's row height. */
class AssetFolderDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    minimum_height_ = metrics.px(26);
    vertical_padding_ = metrics.px(8);
  }

  QSize sizeHint(const QStyleOptionViewItem &option,
                  const QModelIndex &index) const override {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max({size.height(), minimum_height_,
                             option.fontMetrics.height() + vertical_padding_}));
    return size;
  }

private:
  int minimum_height_ = 26;
  int vertical_padding_ = 8;
};

/** Only painted tiles request previews; model layout never queues GPU work. */
class AssetDelegate final : public QStyledItemDelegate {
public:
  AssetDelegate(AssetModel &model, AssetProxy &proxy, QObject *parent)
      : QStyledItemDelegate(parent), model_(model), proxy_(proxy) {}
  std::function<void(QString)> painted;

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
    QStyleOptionViewItem presented(option);
    initStyleOption(&presented, index);
    if (index.column() == 0 && painted) {
      painted(index.data(kIdentityRole).toString());
    }
    if (index.column() == 0 && model_.thumbnail) {
      const int row = proxy_.mapToSource(index).row();
      if (row >= 0 && row < model_.rowCount()) {
        const auto &item = model_.snapshot.items[row];
        if (item.kind == QtAssetBrowserKind::Material ||
            item.kind == QtAssetBrowserKind::Texture) {
          const auto preview = model_.thumbnail(item, model_.preview_size, model_.dpr);
          if (!preview.isNull()) {
            presented.icon = preview;
          }
        }
      }
    }
    const auto *widget = presented.widget;
    const auto *style = widget == nullptr ? QApplication::style() : widget->style();
    style->drawControl(QStyle::CE_ItemViewItem, &presented, painter, widget);
  }

private:
  AssetModel &model_;
  AssetProxy &proxy_;
};

using DropReceiver = std::function<bool(const QMimeData *, QPoint,
                                         Qt::KeyboardModifiers, bool)>;

/** Explicit semantic moves avoid the native view's trailing row removal. */
template <typename Base> class AssetView final : public Base {
public:
  using Base::Base;
  DropReceiver receive_drop;
  std::function<void(bool)> before_paint;
  std::function<void()> after_paint;

protected:
  void paintEvent(QPaintEvent *event) override {
    if (before_paint) {
      before_paint(event->region().boundingRect().contains(this->viewport()->rect()));
    }
    Base::paintEvent(event);
    if (after_paint) {
      after_paint();
    }
  }
  void startDrag(Qt::DropActions actions) override {
    const auto indexes = this->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
      return;
    }
    QDrag drag(this);
    drag.setMimeData(this->model()->mimeData(indexes));
    drag.exec(actions & (Qt::MoveAction | Qt::CopyAction), Qt::MoveAction);
  }
  void dragEnterEvent(QDragEnterEvent *event) override {
    if (receive_drop && receive_drop(event->mimeData(), event->position().toPoint(),
                                    event->modifiers(), false)) {
      event->acceptProposedAction();
    } else {
      event->ignore();
    }
  }
  void dragMoveEvent(QDragMoveEvent *event) override {
    if (receive_drop && receive_drop(event->mimeData(), event->position().toPoint(),
                                    event->modifiers(), false)) {
      event->acceptProposedAction();
    } else {
      event->ignore();
    }
  }
  void dropEvent(QDropEvent *event) override {
    if (receive_drop && receive_drop(event->mimeData(), event->position().toPoint(),
                                    event->modifiers(), true)) {
      event->acceptProposedAction();
    } else {
      event->ignore();
    }
  }
};

class AssetFolderTree final : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;
  DropReceiver receive_drop;
  std::function<QMimeData *()> create_drag;

protected:
  void startDrag(Qt::DropActions) override {
    if (!create_drag) {
      return;
    }
    QMimeData *mime = create_drag();
    if (mime == nullptr) {
      return;
    }
    QDrag drag(this);
    drag.setMimeData(mime);
    drag.exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
  }
  void dragEnterEvent(QDragEnterEvent *event) override {
    if (receive_drop && receive_drop(event->mimeData(), event->position().toPoint(),
                                    event->modifiers(), false)) {
      event->acceptProposedAction();
    } else {
      event->ignore();
    }
  }
  void dragMoveEvent(QDragMoveEvent *event) override {
    if (receive_drop && receive_drop(event->mimeData(), event->position().toPoint(),
                                    event->modifiers(), false)) {
      event->acceptProposedAction();
    } else {
      event->ignore();
    }
  }
  void dropEvent(QDropEvent *event) override {
    if (receive_drop && receive_drop(event->mimeData(), event->position().toPoint(),
                                    event->modifiers(), true)) {
      event->acceptProposedAction();
    } else {
      event->ignore();
    }
  }
};

} // namespace

bool qt_asset_browser_texture_drag(const QMimeData *data, QString &texture_id,
                                   std::uint64_t &generation) {
  std::vector<QtAssetBrowserItem> items;
  std::uint64_t source_generation = 0;
  if (!read_asset_mime(data, items, source_generation) || items.size() != 1 ||
      items.front().kind != QtAssetBrowserKind::Texture) {
    return false;
  }
  texture_id = items.front().id;
  generation = source_generation;
  return true;
}

struct QtAssetBrowser::Impl final {
  QtAssetBrowser &owner;
  AssetModel *model = nullptr;
  AssetProxy *proxy = nullptr;
  QLineEdit *search = nullptr;
  AssetToolbarField<QComboBox> *types = nullptr;
  QToolButton *folders_button = nullptr;
  QToolButton *grid_button = nullptr;
  QToolButton *list_button = nullptr;
  AssetToolbarField<QtSpinBox> *size_spin = nullptr;
  QSplitter *splitter = nullptr;
  AssetFolderTree *folders = nullptr;
  AssetView<QListView> *grid = nullptr;
  AssetView<QTableView> *list = nullptr;
  QStackedWidget *views = nullptr;
  QLabel *empty = nullptr;
  ActionCallback action;
  std::function<void()> selection_changed;
  std::function<void()> state_changed;
  std::function<void(std::vector<QString>)> visible_items_changed;
  std::set<QString> painted_ids;
  std::set<QString> previous_visible_ids;
  QtAssetBrowserLabels labels;
  QtUiScaleMetrics metrics;
  QtAssetBrowserViewMode view_mode = QtAssetBrowserViewMode::Grid;
  bool picker = false;
  bool paste_available = false;
  bool has_snapshot = false;
  bool rebuilding = false;
  int sidebar_width = 200;
  int wheel_remainder = 0;

  explicit Impl(QtAssetBrowser &widget) : owner(widget) {}

  const QtAssetBrowserItem *item(const QModelIndex &index) const {
    if (!index.isValid()) {
      return nullptr;
    }
    const int row = proxy->mapToSource(index).row();
    return row < 0 || row >= model->rowCount() ? nullptr
                                            : &model->snapshot.items[row];
  }
  const QtAssetBrowserItem *find(const QString &id) const {
    const auto &items = model->snapshot.items;
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto &v) {
      return v.id == id || v.relative_path == id;
    });
    return found == items.end() ? nullptr : &*found;
  }
  QAbstractItemView *active_view() const {
    return view_mode == QtAssetBrowserViewMode::Grid
               ? static_cast<QAbstractItemView *>(grid)
               : static_cast<QAbstractItemView *>(list);
  }
  void changed() {
    if (!rebuilding && state_changed) {
      state_changed();
    }
  }
  void update_empty() {
    empty->setText(search->text().isEmpty() && !proxy->kind
                       ? labels.empty : labels.no_results);
    empty->setVisible(proxy->rowCount() == 0);
  }
  void emit_action(QtAssetBrowserAction action_id,
                   std::vector<QtAssetBrowserItem> selected = {},
                   QString proposed_name = {},
                   std::optional<QString> destination = {},
                   QStringList sources = {}) {
    if (!action) {
      return;
    }
    QtAssetBrowserActionRequest request;
    request.action = action_id;
    request.selected_items = std::move(selected);
    request.destination_folder = destination ? *destination : proxy->folder;
    request.proposed_name = std::move(proposed_name);
    request.source_files = std::move(sources);
    request.generation = model->snapshot.generation;
    request.catalog_revision = model->snapshot.revision;
    request.dependency_revision = model->snapshot.dependency_revision;
    const ActionCallback callback = action;
    callback(std::move(request));
  }
  void activate(const QModelIndex &index) {
    const auto *entry = item(index);
    if (entry == nullptr) {
      return;
    }
    const auto selected = *entry;
    if (selected.kind == QtAssetBrowserKind::Folder) {
      owner.set_selected_folder(selected.relative_path);
    } else {
      emit_action(QtAssetBrowserAction::Open, {selected});
    }
  }
  void refresh_icons() {
    for (std::size_t i = 0; i < model->fallback.size(); ++i) {
      model->fallback[i] = asset_icon(static_cast<QtAssetBrowserKind>(i),
                                       owner.palette());
    }
    folders_button->setIcon(model->fallback[0]);
    grid_button->setIcon(owner.style()->standardIcon(QStyle::SP_FileDialogListView));
    list_button->setIcon(owner.style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    for (QTreeWidgetItemIterator it(folders); *it != nullptr; ++it) {
      (*it)->setIcon(0, model->fallback[0]);
    }
    model->refresh_icons();
  }
  void rebuild_folders() {
    const QSignalBlocker blocker(folders);
    std::set<QString> expanded;
    for (QTreeWidgetItemIterator it(folders); *it != nullptr; ++it) {
      if ((*it)->isExpanded()) {
        expanded.insert((*it)->data(0, kFolderRole).toString());
      }
    }
    folders->clear();
    auto *root = new QTreeWidgetItem(folders, {labels.project});
    root->setData(0, kFolderRole, QString{});
    root->setIcon(0, model->fallback[0]);
    root->setExpanded(true);
    std::map<QString, QTreeWidgetItem *> nodes{{QString{}, root}};
    std::vector<QString> paths;
    for (const auto &entry : model->snapshot.items) {
      if (entry.kind == QtAssetBrowserKind::Folder) {
        paths.push_back(entry.relative_path);
      }
    }
    std::sort(paths.begin(), paths.end());
    for (const auto &path : paths) {
      const auto parent = nodes.find(parent_folder(path));
      if (parent == nodes.end()) {
        continue;
      }
      auto *node = new QTreeWidgetItem(parent->second,
                                      {path.section(QLatin1Char('/'), -1)});
      node->setData(0, kFolderRole, path);
      node->setIcon(0, model->fallback[0]);
      node->setExpanded(expanded.contains(path));
      nodes.emplace(path, node);
    }
    QString selected = proxy->folder;
    while (!nodes.contains(selected) && !selected.isEmpty()) {
      selected = parent_folder(selected);
    }
    folders->setCurrentItem(nodes.at(selected));
    proxy->folder = selected;
    proxy->refresh();
  }
  void metrics_changed() {
    const int size = metrics.px(size_spin->value());
    grid->setIconSize(QSize(size, size));
    grid->setGridSize(QSize(size + metrics.px(24),
                            size + owner.fontMetrics().height() * 2 + metrics.px(16)));
    grid->setSpacing(metrics.px(4));
    const int row_icon = std::min(size, metrics.px(64));
    list->setIconSize(QSize(row_icon, row_icon));
    list->verticalHeader()->setDefaultSectionSize(
        std::max(row_icon + metrics.px(8), owner.fontMetrics().height() + metrics.px(12)));
    model->preview_size = view_mode == QtAssetBrowserViewMode::Grid
                              ? grid->iconSize() : list->iconSize();
    model->dpr = owner.devicePixelRatioF();
    model->refresh_icons();
  }
  bool receive_drop(const QMimeData *mime, QString destination,
                     Qt::KeyboardModifiers modifiers, bool execute) {
    if (picker || !action || mime == nullptr) {
      return false;
    }
    destination = clean_folder(std::move(destination));
    if (!destination.isEmpty()) {
      const auto *folder = find(destination);
      if (folder == nullptr || folder->kind != QtAssetBrowserKind::Folder) {
        return false;
      }
    }
    std::vector<QtAssetBrowserItem> dragged;
    std::uint64_t generation = 0;
    if (read_asset_mime(mime, dragged, generation)) {
      if (generation != model->snapshot.generation) {
        return false;
      }
      std::vector<QtAssetBrowserItem> selected;
      for (const auto &entry : dragged) {
        const auto *current = find(entry.id);
        if (current == nullptr || !current->editable ||
            current->relative_path != entry.relative_path || current->kind != entry.kind ||
            (entry.kind == QtAssetBrowserKind::Folder &&
             (destination == entry.relative_path ||
              destination.startsWith(entry.relative_path + QLatin1Char('/'),
                                      Qt::CaseInsensitive)))) {
          return false;
        }
        selected.push_back(*current);
      }
      if (execute) {
        emit_action(modifiers.testFlag(Qt::ControlModifier)
                        ? QtAssetBrowserAction::CopyTo : QtAssetBrowserAction::Move,
                    std::move(selected), {}, destination);
      }
      return true;
    }
    if (!proxy->materials_available || !mime->hasUrls()) {
      return false;
    }
    QStringList sources;
    const auto supported = QImageReader::supportedImageFormats();
    for (const auto &url : mime->urls()) {
      const auto path = url.toLocalFile();
      if (!url.isLocalFile() || path.isEmpty() ||
          !supported.contains(QFileInfo(path).suffix().toLower().toLatin1())) {
        return false;
      }
      sources.push_back(path);
    }
    if (sources.isEmpty()) {
      return false;
    }
    if (execute) {
      emit_action(QtAssetBrowserAction::Import, {}, {}, destination,
                  std::move(sources));
    }
    return true;
  }
  void menu(QPoint global_position, std::vector<QtAssetBrowserItem> selected,
             QString destination) {
    QPointer<QtAssetBrowser> lifetime(&owner);
    const auto generation = model->snapshot.generation;
    const auto revision = model->snapshot.revision;
    const auto dependency = model->snapshot.dependency_revision;
    QMenu popup(&owner);
    std::map<QAction *, QtAssetBrowserAction> actions;
    const auto add = [&](const QString &label, QtAssetBrowserAction id) {
      actions.emplace(popup.addAction(label), id);
    };
    if (!selected.empty()) {
      if (selected.size() == 1) {
        add(labels.open, QtAssetBrowserAction::Open);
      }
      if (!picker) {
        popup.addSeparator();
        if (std::all_of(selected.begin(), selected.end(),
                         [](const auto &entry) { return entry.editable; })) {
          if (selected.size() == 1) {
            add(labels.rename, QtAssetBrowserAction::Rename);
          }
          add(labels.duplicate, QtAssetBrowserAction::Duplicate);
          add(labels.cut, QtAssetBrowserAction::Cut);
          add(labels.copy, QtAssetBrowserAction::Copy);
          add(labels.delete_asset, QtAssetBrowserAction::Delete);
        }
      }
    } else {
      if (!picker) {
        add(labels.create_scene, QtAssetBrowserAction::CreateScene);
        if (proxy->materials_available) {
          add(labels.create_material, QtAssetBrowserAction::CreateMaterial);
        }
      }
      add(labels.new_folder, QtAssetBrowserAction::CreateFolder);
    }
    if (!picker) {
      if (paste_available) {
        popup.addSeparator();
        add(labels.paste, QtAssetBrowserAction::Paste);
      }
      if (proxy->materials_available) {
        popup.addSeparator();
        add(labels.import, QtAssetBrowserAction::Import);
      }
    }
    QAction *chosen = popup.exec(global_position);
    if (!lifetime || generation != model->snapshot.generation ||
        revision != model->snapshot.revision ||
        dependency != model->snapshot.dependency_revision) {
      return;
    }
    const auto found = actions.find(chosen);
    if (found == actions.end()) {
      return;
    }
    const auto id = found->second;
    if (id == QtAssetBrowserAction::Rename) {
      owner.begin_rename(selected.front().id);
    } else if (id == QtAssetBrowserAction::Open &&
               selected.front().kind == QtAssetBrowserKind::Folder) {
      owner.set_selected_folder(selected.front().relative_path);
    } else {
      emit_action(id, std::move(selected), {}, std::move(destination));
    }
  }
};

QtAssetBrowser::QtAssetBrowser(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {
  auto &s = *impl_;
  setObjectName(QStringLiteral("assetBrowser"));
  setProperty("quaderAssetBrowserInput", true);
  auto *layout = new QVBoxLayout(this);
  auto *toolbar = new QHBoxLayout;
  s.folders_button = new QToolButton(this);
  s.folders_button->setObjectName(QStringLiteral("assetBrowser.foldersButton"));
  s.folders_button->setCheckable(true);
  s.folders_button->setChecked(true);
  toolbar->addWidget(s.folders_button);
  s.search = new QLineEdit(this);
  s.search->setObjectName(QStringLiteral("assetBrowser.search"));
  s.search->setClearButtonEnabled(true);
  toolbar->addWidget(s.search, 1);
  s.types = new AssetToolbarField<QComboBox>(this);
  s.types->setObjectName(QStringLiteral("assetBrowser.typeFilter"));
  s.types->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  toolbar->addWidget(s.types);
  s.grid_button = new QToolButton(this);
  s.grid_button->setCheckable(true);
  s.grid_button->setChecked(true);
  s.grid_button->setObjectName(QStringLiteral("assetBrowser.gridButton"));
  toolbar->addWidget(s.grid_button);
  s.list_button = new QToolButton(this);
  s.list_button->setCheckable(true);
  s.list_button->setObjectName(QStringLiteral("assetBrowser.listButton"));
  toolbar->addWidget(s.list_button);
  s.size_spin = new AssetToolbarField<QtSpinBox>(this);
  s.size_spin->setObjectName(QStringLiteral("assetBrowser.previewSize"));
  s.size_spin->setRange(16, 512);
  s.size_spin->setSingleStep(16);
  s.size_spin->setValue(128);
  // The native hint includes the value and both shared scrub-arrow gutters.
  toolbar->addWidget(s.size_spin);
  layout->addLayout(toolbar);
  s.splitter = new QSplitter(this);
  s.splitter->setObjectName(QStringLiteral("assetBrowser.splitter"));
  s.folders = new AssetFolderTree(s.splitter);
  s.folders->setObjectName(QStringLiteral("assetBrowser.folderTree"));
  s.folders->setHeaderHidden(true);
  s.folders->setUniformRowHeights(true);
  s.folders->setItemDelegate(new AssetFolderDelegate(s.folders));
  s.folders->setEditTriggers(QAbstractItemView::NoEditTriggers);
  s.folders->setContextMenuPolicy(Qt::CustomContextMenu);
  s.folders->setDragDropMode(QAbstractItemView::DragDrop);
  s.folders->viewport()->setAcceptDrops(true);
  s.folders->setDefaultDropAction(Qt::MoveAction);
  s.folders->setSelectionMode(QAbstractItemView::SingleSelection);
  s.folders->setMinimumWidth(0);
  install_qt_tree_relationship_presentation(*s.folders);
  auto *body = new QWidget(s.splitter);
  auto *body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(0, 0, 0, 0);
  s.views = new QStackedWidget(body);
  s.grid = new AssetView<QListView>(s.views);
  s.grid->setObjectName(QStringLiteral("assetBrowser.grid"));
  s.grid->setViewMode(QListView::IconMode);
  s.grid->setResizeMode(QListView::Adjust);
  s.grid->setMovement(QListView::Static);
  s.grid->setWordWrap(true);
  s.grid->setUniformItemSizes(true);
  s.list = new AssetView<QTableView>(s.views);
  s.list->setObjectName(QStringLiteral("assetBrowser.list"));
  s.list->verticalHeader()->hide();
  s.list->setShowGrid(false);
  s.list->setSortingEnabled(true);
  s.model = new AssetModel(this);
  s.proxy = new AssetProxy(this);
  s.proxy->setSourceModel(s.model);
  s.proxy->setDynamicSortFilter(true);
  s.grid->setModel(s.proxy);
  s.list->setModel(s.proxy);
  s.list->setSelectionModel(s.grid->selectionModel());
  s.list->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
  s.list->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  s.list->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  for (QAbstractItemView *view : {static_cast<QAbstractItemView *>(s.grid),
                                  static_cast<QAbstractItemView *>(s.list)}) {
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setEditTriggers(QAbstractItemView::EditKeyPressed);
    auto *delegate = new AssetDelegate(*s.model, *s.proxy, view);
    delegate->painted = [&s](QString id) { s.painted_ids.insert(std::move(id)); };
    view->setItemDelegate(delegate);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->setDragDropMode(QAbstractItemView::DragDrop);
    // QListView::Static disables viewport drops separately from the view's
    // DragDrop flag. Our owner-dispatched moves keep static icon positioning.
    view->viewport()->setAcceptDrops(true);
    view->setDefaultDropAction(Qt::MoveAction);
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
    for (auto *scrollbar : {view->verticalScrollBar(), view->horizontalScrollBar()}) {
      QObject::connect(scrollbar, &QScrollBar::valueChanged, this,
                       [view] { view->viewport()->update(); });
    }
    QObject::connect(view, &QAbstractItemView::activated, this,
                     [&s](const QModelIndex &index) { s.activate(index); });
    QObject::connect(view, &QWidget::customContextMenuRequested, this,
                     [&s, view](QPoint position) {
      const auto index = view->indexAt(position);
      if (index.isValid() && !view->selectionModel()->isSelected(index)) {
        view->selectionModel()->setCurrentIndex(index,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      }
      auto selected = index.isValid() ? s.owner.selected_items()
                                      : std::vector<QtAssetBrowserItem>{};
      s.menu(view->viewport()->mapToGlobal(position), std::move(selected),
             s.proxy->folder);
    });
  }
  s.views->addWidget(s.grid);
  s.views->addWidget(s.list);
  body_layout->addWidget(s.views, 1);
  s.empty = new QLabel(body);
  s.empty->setObjectName(QStringLiteral("assetBrowser.empty"));
  s.empty->setAlignment(Qt::AlignCenter);
  body_layout->addWidget(s.empty);
  s.splitter->setStretchFactor(0, 0);
  s.splitter->setStretchFactor(1, 1);
  layout->addWidget(s.splitter, 1);
  setFocusProxy(s.grid);

  const auto drop_for_view = [&s](QAbstractItemView *view) -> DropReceiver {
    return [&s, view](const QMimeData *data, QPoint position,
                       Qt::KeyboardModifiers modifiers, bool execute) {
      QString destination = s.proxy->folder;
      if (const auto *entry = s.item(view->indexAt(position));
          entry != nullptr && entry->kind == QtAssetBrowserKind::Folder) {
        destination = entry->relative_path;
      }
      return s.receive_drop(data, destination, modifiers, execute);
    };
  };
  s.grid->receive_drop = drop_for_view(s.grid);
  s.list->receive_drop = drop_for_view(s.list);
  const auto before_paint = [&s](bool complete) {
    if (complete) {
      s.painted_ids.clear();
    }
  };
  const auto after_paint = [&s] {
    if (s.painted_ids != s.previous_visible_ids) {
      s.previous_visible_ids = s.painted_ids;
      if (s.visible_items_changed) {
        s.visible_items_changed({s.painted_ids.begin(), s.painted_ids.end()});
      }
    }
  };
  s.grid->before_paint = before_paint;
  s.list->before_paint = before_paint;
  s.grid->after_paint = after_paint;
  s.list->after_paint = after_paint;
  s.folders->receive_drop = [&s](const QMimeData *data, QPoint position,
                                  Qt::KeyboardModifiers modifiers, bool execute) {
    auto *target = s.folders->itemAt(position);
    return s.receive_drop(data,
        target == nullptr ? QString{} : target->data(0, kFolderRole).toString(),
        modifiers, execute);
  };
  s.folders->create_drag = [&s]() -> QMimeData * {
    if (s.picker) {
      return nullptr;
    }
    const auto *entry = s.find(s.proxy->folder);
    return entry == nullptr ? nullptr
                            : asset_mime({*entry}, s.model->snapshot.generation);
  };
  s.model->rename = [&s](const QtAssetBrowserItem &item, QString name) {
    s.emit_action(QtAssetBrowserAction::Rename, {item}, std::move(name));
  };
  QObject::connect(s.search, &QLineEdit::textChanged, this,
                   [&s](const QString &text) {
    s.proxy->search = text.trimmed();
    s.proxy->refresh();
    s.update_empty();
  });
  QObject::connect(s.types, &QComboBox::currentIndexChanged, this, [&s](int index) {
    s.owner.set_type_filter(index == 0 ? std::optional<QtAssetBrowserKind>{}
                                      : static_cast<QtAssetBrowserKind>(index));
  });
  QObject::connect(s.size_spin, qOverload<int>(&QSpinBox::valueChanged), this,
                   [&s](int) { s.metrics_changed(); s.changed(); });
  QObject::connect(s.folders_button, &QToolButton::toggled, this, [&s](bool visible) {
    s.folders->setVisible(visible);
    if (visible) {
      s.splitter->setSizes({s.metrics.px(s.sidebar_width),
                            std::max(1, s.splitter->width() - s.metrics.px(s.sidebar_width))});
    }
    s.changed();
  });
  QObject::connect(s.splitter, &QSplitter::splitterMoved, this, [&s](int, int) {
    if (s.folders_button->isChecked() && !s.splitter->sizes().isEmpty()) {
      s.sidebar_width = static_cast<int>(s.splitter->sizes().front() / s.metrics.factor());
      s.changed();
    }
  });
  QObject::connect(s.grid_button, &QToolButton::clicked, this, [&s] {
    s.owner.restore_presentation_state(QtAssetBrowserViewMode::Grid,
        s.size_spin->value(), s.folders_button->isChecked(), s.sidebar_width);
    s.changed();
  });
  QObject::connect(s.list_button, &QToolButton::clicked, this, [&s] {
    s.owner.restore_presentation_state(QtAssetBrowserViewMode::List,
        s.size_spin->value(), s.folders_button->isChecked(), s.sidebar_width);
    s.changed();
  });
  QObject::connect(s.folders, &QTreeWidget::currentItemChanged, this,
                   [&s](QTreeWidgetItem *item, QTreeWidgetItem *) {
    if (item != nullptr) {
      s.owner.set_selected_folder(item->data(0, kFolderRole).toString());
    }
  });
  QObject::connect(s.folders, &QWidget::customContextMenuRequested, this,
                   [&s](QPoint position) {
    auto *item = s.folders->itemAt(position);
    const QString path = item == nullptr ? QString{} : item->data(0, kFolderRole).toString();
    std::vector<QtAssetBrowserItem> selected;
    if (const auto *entry = s.find(path); entry != nullptr) {
      selected.push_back(*entry);
    }
    s.menu(s.folders->viewport()->mapToGlobal(position), std::move(selected), path);
  });
  QObject::connect(s.grid->selectionModel(), &QItemSelectionModel::selectionChanged,
                   this, [&s] {
    if (!s.rebuilding && s.selection_changed) {
      s.selection_changed();
    }
  });
  set_labels({});
  s.refresh_icons();
  s.rebuild_folders();
  apply_ui_scale(QtUiScaleMetrics{});
}

QtAssetBrowser::~QtAssetBrowser() = default;

void QtAssetBrowser::set_labels(QtAssetBrowserLabels labels) {
  auto &s = *impl_;
  s.labels = std::move(labels);
  s.model->labels = s.labels;
  setWindowTitle(s.labels.title);
  s.search->setPlaceholderText(s.labels.search);
  s.search->setAccessibleName(s.labels.search);
  s.folders->setAccessibleName(s.labels.folders);
  s.folders_button->setToolTip(s.labels.folders);
  s.folders_button->setAccessibleName(s.labels.folders);
  s.grid_button->setToolTip(s.labels.grid);
  s.grid_button->setAccessibleName(s.labels.grid);
  s.list_button->setToolTip(s.labels.list);
  s.list_button->setAccessibleName(s.labels.list);
  s.size_spin->setToolTip(s.labels.preview_size);
  s.size_spin->setAccessibleName(s.labels.preview_size);
  {
    const QSignalBlocker types(s.types);
    s.types->clear();
    s.types->addItems({s.labels.all_types, s.labels.scenes,
                       s.labels.materials, s.labels.textures});
    s.types->setCurrentIndex(s.proxy->kind ? static_cast<int>(*s.proxy->kind) : 0);
  }
  s.types->refresh_layout();
  emit s.model->headerDataChanged(Qt::Horizontal, 0, 2);
  if (s.model->rowCount() > 0) {
    emit s.model->dataChanged(s.model->index(0, 0),
                              s.model->index(s.model->rowCount() - 1, 2),
                              {Qt::DisplayRole});
  }
  if (s.folders->topLevelItemCount() > 0) {
    s.folders->topLevelItem(0)->setText(0, s.labels.project);
  }
  s.update_empty();
}

bool QtAssetBrowser::set_snapshot(QtAssetBrowserSnapshot snapshot) {
  auto &s = *impl_;
  auto &previous = s.model->snapshot;
  if (s.has_snapshot && previous.generation == snapshot.generation &&
      previous.revision == snapshot.revision) {
    const bool changed = previous.dependency_revision != snapshot.dependency_revision;
    previous.dependency_revision = snapshot.dependency_revision;
    return changed;
  }
  const bool new_project = previous.generation != snapshot.generation;
  std::set<QString> selected;
  if (!new_project) {
    for (const auto &index : s.grid->selectionModel()->selectedRows()) {
      selected.insert(index.data(kIdentityRole).toString());
    }
  }
  s.rebuilding = true;
  if (new_project) {
    s.proxy->folder.clear();
    s.search->clear();
    s.paste_available = false;
    const QSignalBlocker blocker(s.folders);
    s.folders->clear();
  }
  bool folders_changed = new_project || !s.has_snapshot;
  if (folders_changed) {
    s.model->replace(std::move(snapshot));
  } else {
    folders_changed = s.model->update(std::move(snapshot));
  }
  if (new_project) {
    s.painted_ids.clear();
    s.previous_visible_ids.clear();
  }
  s.has_snapshot = true;
  if (folders_changed) {
    s.rebuild_folders();
  }
  s.proxy->sort(0);
  if (folders_changed && !selected.empty()) {
    for (int row = 0; row < s.proxy->rowCount(); ++row) {
      const auto index = s.proxy->index(row, 0);
      if (selected.contains(index.data(kIdentityRole).toString())) {
        s.grid->selectionModel()->select(index,
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
      }
    }
  }
  s.update_empty();
  s.rebuilding = false;
  if (s.selection_changed) {
    s.selection_changed();
  }
  return true;
}

const QtAssetBrowserSnapshot &QtAssetBrowser::snapshot() const {
  return impl_->model->snapshot;
}

void QtAssetBrowser::clear() {
  impl_->has_snapshot = false;
  set_snapshot({});
}

void QtAssetBrowser::set_action_callback(ActionCallback callback) {
  impl_->action = std::move(callback);
}
void QtAssetBrowser::set_selection_changed_callback(std::function<void()> callback) {
  impl_->selection_changed = std::move(callback);
}
void QtAssetBrowser::set_presentation_state_changed_callback(std::function<void()> callback) {
  impl_->state_changed = std::move(callback);
}
void QtAssetBrowser::set_thumbnail_provider(ThumbnailProvider provider) {
  impl_->model->thumbnail = std::move(provider);
  impl_->model->refresh_icons();
}
void QtAssetBrowser::set_visible_items_changed_callback(
    std::function<void(std::vector<QString>)> callback) {
  impl_->visible_items_changed = std::move(callback);
  impl_->previous_visible_ids.clear();
  impl_->active_view()->viewport()->update();
}
void QtAssetBrowser::invalidate_thumbnail(const QString &id) {
  auto &model = *impl_->model;
  if (id.isEmpty()) {
    model.refresh_icons();
    return;
  }
  for (int row = 0; row < model.rowCount(); ++row) {
    if (model.snapshot.items[row].id == id) {
      emit model.dataChanged(model.index(row, 0), model.index(row, 0),
                              {Qt::DecorationRole});
      return;
    }
  }
}
void QtAssetBrowser::set_material_authoring_available(bool available) {
  auto &s = *impl_;
  s.proxy->materials_available = available;
  s.types->setVisible(available && !s.picker);
  if (!available) {
    set_type_filter({});
  }
  s.proxy->refresh();
  s.update_empty();
}
void QtAssetBrowser::set_paste_available(bool available) {
  impl_->paste_available = available;
}
void QtAssetBrowser::set_picker_mode(bool picker) {
  auto &s = *impl_;
  s.picker = picker;
  s.model->picker = picker;
  s.types->setVisible(!picker && s.proxy->materials_available);
  for (QAbstractItemView *view : {static_cast<QAbstractItemView *>(s.grid),
                                  static_cast<QAbstractItemView *>(s.list),
                                  static_cast<QAbstractItemView *>(s.folders)}) {
    view->setDragEnabled(!picker);
    view->setAcceptDrops(!picker);
    view->viewport()->setAcceptDrops(!picker);
    view->setSelectionMode(picker ? QAbstractItemView::SingleSelection
                                  : QAbstractItemView::ExtendedSelection);
  }
}
void QtAssetBrowser::set_type_filter(std::optional<QtAssetBrowserKind> kind) {
  auto &s = *impl_;
  if (kind == QtAssetBrowserKind::Folder) {
    kind.reset();
  }
  s.proxy->kind = kind;
  const QSignalBlocker blocker(s.types);
  s.types->setCurrentIndex(kind ? static_cast<int>(*kind) : 0);
  s.proxy->refresh();
  s.update_empty();
}
void QtAssetBrowser::set_search_text(QString text) {
  impl_->search->setText(std::move(text));
}
void QtAssetBrowser::set_selected_folder(QString folder) {
  auto &s = *impl_;
  folder = clean_folder(std::move(folder));
  while (!folder.isEmpty()) {
    const auto *entry = s.find(folder);
    if (entry != nullptr && entry->kind == QtAssetBrowserKind::Folder) {
      break;
    }
    folder = parent_folder(folder);
  }
  const bool changed = s.proxy->folder != folder;
  s.proxy->folder = std::move(folder);
  if (changed) {
    s.grid->selectionModel()->clear();
    s.proxy->refresh();
    s.update_empty();
    s.changed();
  }
  const QSignalBlocker blocker(s.folders);
  for (QTreeWidgetItemIterator it(s.folders); *it != nullptr; ++it) {
    if ((*it)->data(0, kFolderRole).toString() == s.proxy->folder) {
      s.folders->setCurrentItem(*it);
      break;
    }
  }
  if (changed && s.selection_changed) {
    s.selection_changed();
  }
}
QString QtAssetBrowser::selected_folder() const { return impl_->proxy->folder; }

std::vector<QtAssetBrowserItem> QtAssetBrowser::selected_items() const {
  const auto &s = *impl_;
  std::vector<QtAssetBrowserItem> result;
  for (const auto &index : s.grid->selectionModel()->selectedRows()) {
    if (const auto *entry = s.item(index); entry != nullptr) {
      result.push_back(*entry);
    }
  }
  return result;
}
bool QtAssetBrowser::reveal_item(const QString &id) {
  auto &s = *impl_;
  const auto *found = s.find(id);
  if (found == nullptr) {
    return false;
  }
  const auto entry = *found;
  set_search_text({});
  set_selected_folder(parent_folder(entry.relative_path));
  if (s.proxy->kind && entry.kind != *s.proxy->kind &&
      entry.kind != QtAssetBrowserKind::Folder && !s.picker) {
    set_type_filter({});
  }
  for (int row = 0; row < s.proxy->rowCount(); ++row) {
    const auto index = s.proxy->index(row, 0);
    if (index.data(kIdentityRole).toString() == entry.id) {
      s.grid->selectionModel()->setCurrentIndex(index,
          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      s.active_view()->scrollTo(index);
      return true;
    }
  }
  return false;
}
bool QtAssetBrowser::begin_rename(const QString &id) {
  auto &s = *impl_;
  const auto *entry = s.find(id);
  if (s.picker || entry == nullptr || !entry->editable || !reveal_item(id)) {
    return false;
  }
  focus_browser();
  s.active_view()->edit(s.grid->selectionModel()->currentIndex());
  return true;
}
bool QtAssetBrowser::handle_action(QtAssetBrowserAction action) {
  auto &s = *impl_;
  if (s.picker && action != QtAssetBrowserAction::CreateFolder &&
      action != QtAssetBrowserAction::Open) {
    return false;
  }
  auto selected = selected_items();
  const auto *focus = QApplication::focusWidget();
  if (focus != nullptr && (focus == s.folders || s.folders->isAncestorOf(focus))) {
    const auto *entry = s.find(s.proxy->folder);
    selected = entry == nullptr ? std::vector<QtAssetBrowserItem>{}
                                : std::vector<QtAssetBrowserItem>{*entry};
  }
  if (action == QtAssetBrowserAction::Rename) {
    return selected.size() == 1 && begin_rename(selected.front().id);
  }
  if ((action == QtAssetBrowserAction::Cut || action == QtAssetBrowserAction::Copy ||
       action == QtAssetBrowserAction::Delete || action == QtAssetBrowserAction::Duplicate ||
       action == QtAssetBrowserAction::Open) && selected.empty()) {
    return false;
  }
  if (action == QtAssetBrowserAction::Paste && !s.paste_available) {
    return false;
  }
  if ((action == QtAssetBrowserAction::CreateMaterial || action == QtAssetBrowserAction::Import) &&
      !s.proxy->materials_available) {
    return false;
  }
  if (action == QtAssetBrowserAction::Open && selected.size() == 1 &&
      selected.front().kind == QtAssetBrowserKind::Folder) {
    set_selected_folder(selected.front().relative_path);
    return true;
  }
  if (!s.action) {
    return false;
  }
  s.emit_action(action, std::move(selected));
  return true;
}
void QtAssetBrowser::focus_browser() {
  impl_->active_view()->setFocus(Qt::ShortcutFocusReason);
}
bool QtAssetBrowser::owns_browser_focus() const {
  const auto *focused = QApplication::focusWidget();
  return focused != nullptr && (focused == this || isAncestorOf(focused));
}
void QtAssetBrowser::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  auto &s = *impl_;
  s.metrics = metrics;
  layout()->setContentsMargins(metrics.margins(QMargins(8, 8, 8, 8)));
  layout()->setSpacing(metrics.px(6));
  s.search->setMinimumWidth(metrics.px(80));
  s.types->refresh_layout();
  s.size_spin->refresh_layout();
  static_cast<AssetFolderDelegate *>(s.folders->itemDelegate())->apply_ui_scale(metrics);
  s.folders->setIconSize(metrics.size(QSize(16, 16)));
  apply_ui_scale_to_qt_tree_relationship_presentation(*s.folders, metrics);
  s.folders->doItemsLayout();
  for (auto *button : {s.folders_button, s.grid_button, s.list_button}) {
    button->setIconSize(metrics.size(QSize(18, 18)));
  }
  s.list->setColumnWidth(0, metrics.px(220));
  s.splitter->setSizes({metrics.px(s.sidebar_width),
                        std::max(1, width() - metrics.px(s.sidebar_width))});
  s.metrics_changed();
}
void QtAssetBrowser::restore_presentation_state(QtAssetBrowserViewMode mode,
                                                int tile_size,
                                                bool sidebar_visible,
                                                int sidebar_width) {
  auto &s = *impl_;
  const bool was_rebuilding = s.rebuilding;
  s.rebuilding = true;
  s.view_mode = mode;
  s.sidebar_width = std::clamp(sidebar_width, 80, 2000);
  s.size_spin->setValue(std::clamp(tile_size, 16, 512));
  s.grid_button->setChecked(mode == QtAssetBrowserViewMode::Grid);
  s.list_button->setChecked(mode == QtAssetBrowserViewMode::List);
  s.views->setCurrentWidget(mode == QtAssetBrowserViewMode::Grid
                                ? static_cast<QWidget *>(s.grid)
                                : static_cast<QWidget *>(s.list));
  setFocusProxy(s.active_view());
  s.folders_button->setChecked(sidebar_visible);
  s.folders->setVisible(sidebar_visible);
  s.splitter->setSizes({s.metrics.px(s.sidebar_width),
                        std::max(1, width() - s.metrics.px(s.sidebar_width))});
  s.metrics_changed();
  s.rebuilding = was_rebuilding;
}
QtAssetBrowserViewMode QtAssetBrowser::view_mode() const { return impl_->view_mode; }
int QtAssetBrowser::tile_size() const { return impl_->size_spin->value(); }
bool QtAssetBrowser::sidebar_visible() const { return impl_->folders_button->isChecked(); }
int QtAssetBrowser::sidebar_width() const { return impl_->sidebar_width; }

bool QtAssetBrowser::event(QEvent *event) {
  const bool result = QWidget::event(event);
  if (impl_ && impl_->model != nullptr) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
      impl_->refresh_icons();
    }
    if (event->type() == QEvent::FontChange ||
        event->type() == QEvent::DevicePixelRatioChange) {
      impl_->metrics_changed();
    }
  }
  return result;
}
bool QtAssetBrowser::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::Wheel && owns_browser_focus()) {
    auto *wheel = static_cast<QWheelEvent *>(event);
    if (wheel->modifiers() == Qt::ControlModifier) {
      auto &s = *impl_;
      s.wheel_remainder += wheel->angleDelta().y();
      const int steps = s.wheel_remainder / 120;
      s.wheel_remainder %= 120;
      if (steps != 0) {
        s.size_spin->setValue(s.size_spin->value() + steps * 16);
      }
      wheel->accept();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

} // namespace quader
