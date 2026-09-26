////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_settings_dialog.hpp"

#include "api/quader_commands_api.hpp"
#include "ui/qt_apply_cancel_input_filter.hpp"
#include "core/localization_service.hpp"
#include "core/release_features.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/material/default_material_texture_store.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_material_presentation.hpp"
#include "ui/qt_retained_message_box.hpp"
#include "ui/qt_settings_controller.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include "ui/qt_spin_box.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QStringList>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace quader {
namespace {

class QtSettingsCategoryDelegate final : public QStyledItemDelegate {
public:
  explicit QtSettingsCategoryDelegate(QObject *parent)
      : QStyledItemDelegate(parent) {}

protected:
  void initStyleOption(QStyleOptionViewItem *option,
                       const QModelIndex &index) const override {
    QStyledItemDelegate::initStyleOption(option, index);
    const bool selected = option->state.testFlag(QStyle::State_Selected);
    for (const QPalette::ColorGroup group :
         {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
      option->palette.setColor(
          group, QPalette::Text,
          option->palette.color(group, selected ? QPalette::Text
                                                 : QPalette::WindowText));
    }
    option->displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
  }
};

class QtSettingsCategoryList final : public QListWidget {
public:
  explicit QtSettingsCategoryList(QWidget *parent) : QListWidget(parent) {
    setFrameShape(QFrame::NoFrame);
    setUniformItemSizes(true);
    setItemDelegate(new QtSettingsCategoryDelegate(this));
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    metrics_ = metrics;
    update_item_metrics();
  }

protected:
  void changeEvent(QEvent *event) override {
    QListWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange ||
        event->type() == QEvent::StyleChange) {
      update_item_metrics();
    }
  }

private:
  void update_item_metrics() {
    const int padding = metrics_.px(8);
    setViewportMargins(padding, padding, padding, padding);
    setSpacing(metrics_.px(2));
    const int row_height = std::max(metrics_.px(32),
                                    fontMetrics().height() + metrics_.px(12));
    for (int row = 0; row < count(); ++row) {
      const QSize hint(0, row_height);
      if (item(row)->sizeHint() != hint) {
        item(row)->setSizeHint(hint);
      }
    }
  }

  QtUiScaleMetrics metrics_;
};

QString qtext(const LocalizationService *localization, std::string_view key,
              std::string_view fallback) {
  if (localization == nullptr) {
    return QString::fromUtf8(fallback.data(),
                             static_cast<qsizetype>(fallback.size()));
  }
  return QString::fromStdString(localization->text(key, fallback));
}

std::string_view section_stable_id(ViewportSettingsSectionId section) {
  switch (section) {
  case ViewportSettingsSectionId::Grid:
    return "grid";
  case ViewportSettingsSectionId::Navigation:
    return "navigation";
  case ViewportSettingsSectionId::Shading:
    return "shading";
  case ViewportSettingsSectionId::Selection:
    return "selection";
  case ViewportSettingsSectionId::InstanceEditing:
    return "instance_editing";
  case ViewportSettingsSectionId::Overlay:
    return "overlay";
  case ViewportSettingsSectionId::Vertices:
    return "vertices";
  case ViewportSettingsSectionId::Picking:
    return "picking";
  case ViewportSettingsSectionId::Gizmo:
    return "gizmo";
  case ViewportSettingsSectionId::Texturing:
    return "texturing";
  case ViewportSettingsSectionId::Count:
    break;
  }
  return "unknown";
}

std::string section_localization_key(ViewportSettingsSectionId section) {
  std::string key = "viewport.settings.section.";
  key.append(section_stable_id(section));
  key.append(".label");
  return key;
}

std::string setting_localization_key(std::string_view descriptor_key,
                                     std::string_view field = "label") {
  std::string key = "viewport.settings.";
  key.append(descriptor_key);
  key.push_back('.');
  key.append(field);
  return key;
}

QString setting_help_text(const LocalizationService *localization,
                          std::string_view descriptor_key,
                          std::string_view fallback) {
  if (fallback.empty()) {
    return {};
  }
  return qtext(localization, setting_localization_key(descriptor_key, "help"),
               fallback);
}

QString setting_object_name(std::string_view descriptor_key) {
  return QStringLiteral("viewportSetting_%1")
      .arg(QString::fromUtf8(descriptor_key.data(),
                             static_cast<qsizetype>(descriptor_key.size())));
}

QColor to_q_color(ViewportSettingColor color) {
  return QColor::fromRgbF(
      std::clamp(color.r, 0.0f, 1.0f), std::clamp(color.g, 0.0f, 1.0f),
      std::clamp(color.b, 0.0f, 1.0f), std::clamp(color.a, 0.0f, 1.0f));
}

ViewportSettingColor from_q_color(const QColor &color) {
  return {
      .r = static_cast<float>(color.redF()),
      .g = static_cast<float>(color.greenF()),
      .b = static_cast<float>(color.blueF()),
      .a = static_cast<float>(color.alphaF()),
  };
}

QString color_opacity_text(const LocalizationService *localization,
                           const QColor &color) {
  const int percent = static_cast<int>(std::lround(
      std::clamp(static_cast<double>(color.alphaF()), 0.0, 1.0) * 100.0));
  return qtext(localization, "ui.settings.color.opacity", "Opacity: %1%")
      .arg(percent);
}

void set_color_button_presentation(
    QtColorSwatchButton &button, ViewportSettingColor color,
    const LocalizationService *localization,
    const ViewportColorSettingDescriptor &descriptor) {
  const QColor swatch_color = to_q_color(color);
  button.set_swatch_color(swatch_color);
  QString details = descriptor.id == ViewportColorSettingId::Background
                        ? swatch_color.name(QColor::HexRgb).toUpper()
                        : QStringLiteral("%1 | %2")
                              .arg(swatch_color.name(QColor::HexArgb).toUpper(),
                                   color_opacity_text(localization, swatch_color));
  const QString help =
      setting_help_text(localization, descriptor.key, descriptor.help);
  if (!help.isEmpty()) {
    details.append(QLatin1Char('\n'));
    details.append(help);
  }
  button.setToolTip(details);
  button.setAccessibleDescription(details);
}

/// Stores one fully localized row before retained filtering and sorting.
struct KeybindingTableRow {
  QuaderKeybindingActionKey key;
  QString target_id;
  QString command_name;
  QString context;
  QString input_mapping;
  QString shortcut;
  QString default_mapping;
  QuaderKeybindingSourceKind source = QuaderKeybindingSourceKind::Default;
  QString when;
  bool customized = false;
  std::size_t mapping_order = 0u;

  bool operator==(const KeybindingTableRow &other) const {
    return key == other.key && target_id == other.target_id &&
           command_name == other.command_name &&
           context == other.context && input_mapping == other.input_mapping &&
           shortcut == other.shortcut &&
           default_mapping == other.default_mapping && source == other.source &&
           when == other.when &&
           customized == other.customized &&
           mapping_order == other.mapping_order;
  }
};

/// Compares localized UI text without consulting the process locale.
int natural_numeric_compare(const QString &left, const QString &right) {
  qsizetype left_index = 0;
  qsizetype right_index = 0;
  while (left_index < left.size() && right_index < right.size()) {
    const bool left_is_digit = left.at(left_index).isDigit();
    const bool right_is_digit = right.at(right_index).isDigit();
    if (left_is_digit && right_is_digit) {
      const qsizetype left_run_start = left_index;
      const qsizetype right_run_start = right_index;
      while (left_index < left.size() && left.at(left_index).isDigit()) {
        ++left_index;
      }
      while (right_index < right.size() && right.at(right_index).isDigit()) {
        ++right_index;
      }

      qsizetype left_significant = left_run_start;
      qsizetype right_significant = right_run_start;
      while (left_significant < left_index &&
             left.at(left_significant).digitValue() == 0) {
        ++left_significant;
      }
      while (right_significant < right_index &&
             right.at(right_significant).digitValue() == 0) {
        ++right_significant;
      }

      const qsizetype left_digits = left_index - left_significant;
      const qsizetype right_digits = right_index - right_significant;
      if (left_digits != right_digits) {
        return left_digits < right_digits ? -1 : 1;
      }
      for (qsizetype offset = 0; offset < left_digits; ++offset) {
        const int left_digit =
            left.at(left_significant + offset).digitValue();
        const int right_digit =
            right.at(right_significant + offset).digitValue();
        if (left_digit != right_digit) {
          return left_digit < right_digit ? -1 : 1;
        }
      }

      const qsizetype left_run_length = left_index - left_run_start;
      const qsizetype right_run_length = right_index - right_run_start;
      if (left_run_length != right_run_length) {
        return left_run_length < right_run_length ? -1 : 1;
      }
      continue;
    }

    const ushort left_character = left.at(left_index).toCaseFolded().unicode();
    const ushort right_character =
        right.at(right_index).toCaseFolded().unicode();
    if (left_character != right_character) {
      return left_character < right_character ? -1 : 1;
    }
    ++left_index;
    ++right_index;
  }
  if (left_index == left.size() && right_index == right.size()) {
    return 0;
  }
  return left_index == left.size() ? -1 : 1;
}

/// Retained catalog model with natural numeric sorting for all visible columns.
class QtKeybindingTableModel final : public QAbstractTableModel {
public:
  /// Identifies the fixed presentation columns.
  enum Column {
    CommandNameColumn = 0,
    ContextColumn,
    InputMappingColumn,
    ColumnCount,
  };

  explicit QtKeybindingTableModel(QObject *parent = nullptr)
      : QAbstractTableModel(parent) {}

  int rowCount(const QModelIndex &parent = {}) const override {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
  }

  int columnCount(const QModelIndex &parent = {}) const override {
    return parent.isValid() ? 0 : ColumnCount;
  }

  QVariant data(const QModelIndex &index, int role) const override {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(rows_.size()) || index.column() < 0 ||
        index.column() >= ColumnCount) {
      return {};
    }

    const KeybindingTableRow &row =
        rows_[static_cast<std::size_t>(index.row())];
    if (role == Qt::UserRole) {
      return row.target_id;
    }
    if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) {
      return {};
    }

    switch (index.column()) {
    case CommandNameColumn:
      return row.command_name;
    case ContextColumn:
      return row.context;
    case InputMappingColumn:
      return row.input_mapping;
    default:
      return {};
    }
  }

  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole ||
        section < 0 || section >= ColumnCount) {
      return {};
    }
    return headers_[static_cast<std::size_t>(section)];
  }

  void set_headers(std::array<QString, ColumnCount> headers) {
    if (headers_ == headers) {
      return;
    }
    headers_ = std::move(headers);
    emit headerDataChanged(Qt::Horizontal, CommandNameColumn,
                           InputMappingColumn);
  }

  void set_rows(std::vector<KeybindingTableRow> rows) {
    sort_rows(rows, sort_column_, sort_order_);
    if (rows_ == rows) {
      return;
    }
    if (!has_same_row_membership(rows_, rows)) {
      beginResetModel();
      rows_ = std::move(rows);
      endResetModel();
      return;
    }
    if (has_same_row_order(rows_, rows)) {
      rows_ = std::move(rows);
      if (!rows_.empty()) {
        emit dataChanged(index(0, CommandNameColumn),
                         index(static_cast<int>(rows_.size()) - 1,
                               InputMappingColumn),
                         {Qt::DisplayRole, Qt::AccessibleTextRole,
                          Qt::UserRole});
      }
      return;
    }
    replace_rows_preserving_persistent_indexes(std::move(rows));
  }

  void sort(int column, Qt::SortOrder order) override {
    if (column < CommandNameColumn || column >= ColumnCount) {
      return;
    }
    sort_column_ = column;
    sort_order_ = order;
    std::vector<KeybindingTableRow> rows = rows_;
    sort_rows(rows, sort_column_, sort_order_);
    if (rows == rows_) {
      return;
    }

    replace_rows_preserving_persistent_indexes(std::move(rows));
  }

  [[nodiscard]] const KeybindingTableRow *row_at(int row) const {
    return row < 0 || row >= static_cast<int>(rows_.size())
               ? nullptr
               : &rows_[static_cast<std::size_t>(row)];
  }

private:
  static bool has_same_row_membership(
      const std::vector<KeybindingTableRow> &left,
      const std::vector<KeybindingTableRow> &right) {
    return left.size() == right.size() &&
           std::is_permutation(
               left.begin(), left.end(), right.begin(), right.end(),
               [](const KeybindingTableRow &left_row,
                  const KeybindingTableRow &right_row) {
                 return left_row.key == right_row.key;
               });
  }

  static bool has_same_row_order(
      const std::vector<KeybindingTableRow> &left,
      const std::vector<KeybindingTableRow> &right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](const KeybindingTableRow &left_row,
                         const KeybindingTableRow &right_row) {
                        return left_row.key == right_row.key;
                      });
  }

  void replace_rows_preserving_persistent_indexes(
      std::vector<KeybindingTableRow> rows) {
    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    const QModelIndexList from = persistentIndexList();
    /// Identifies one persistent table cell across a row reorder.
    struct PersistentCell {
      QuaderKeybindingActionKey key;
      int column = 0;
    };
    std::vector<PersistentCell> cells;
    cells.reserve(static_cast<std::size_t>(from.size()));
    for (const QModelIndex &index : from) {
      cells.push_back({rows_[static_cast<std::size_t>(index.row())].key,
                       index.column()});
    }

    rows_ = std::move(rows);
    QModelIndexList to;
    to.reserve(from.size());
    for (const PersistentCell &cell : cells) {
      const auto found = std::find_if(
          rows_.begin(), rows_.end(), [&cell](const KeybindingTableRow &row) {
            return row.key == cell.key;
          });
      to.push_back(found != rows_.end()
                       ? index(static_cast<int>(found - rows_.begin()),
                               cell.column)
                       : QModelIndex{});
    }
    changePersistentIndexList(from, to);
    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
  }
  static const QString &column_text(const KeybindingTableRow &row,
                                    int column) {
    switch (column) {
    case ContextColumn:
      return row.context;
    case InputMappingColumn:
      return row.input_mapping;
    case CommandNameColumn:
    default:
      return row.command_name;
    }
  }

  static void sort_rows(std::vector<KeybindingTableRow> &rows, int column,
                        Qt::SortOrder order) {
    std::stable_sort(
        rows.begin(), rows.end(),
        [column, order](const KeybindingTableRow &left,
                        const KeybindingTableRow &right) {
          const int compared = natural_numeric_compare(
              column_text(left, column), column_text(right, column));
          if (compared != 0) {
            return order == Qt::AscendingOrder ? compared < 0 : compared > 0;
          }
          return left.mapping_order < right.mapping_order;
        });
  }

  std::array<QString, ColumnCount> headers_{};
  std::vector<KeybindingTableRow> rows_;
  int sort_column_ = CommandNameColumn;
  Qt::SortOrder sort_order_ = Qt::AscendingOrder;
};

/// Table view that treats Enter as the standard rebind action for its row.
class QtKeybindingTableView final : public QTableView {
public:
  explicit QtKeybindingTableView(QWidget *parent = nullptr)
      : QTableView(parent) {}

  void set_rebind_handler(std::function<void()> handler) {
    rebind_handler_ = std::move(handler);
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event != nullptr && !event->isAutoRepeat() &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        currentIndex().isValid() && rebind_handler_) {
      rebind_handler_();
      event->accept();
      return;
    }
    QTableView::keyPressEvent(event);
  }

private:
  std::function<void()> rebind_handler_;
};

/// Read-only field that translates pressed Qt input into canonical chords.
class QtKeybindingCaptureEdit final : public QLineEdit {
public:
  explicit QtKeybindingCaptureEdit(QWidget *parent = nullptr)
      : QLineEdit(parent) {
    setReadOnly(true);
  }

  void set_chord_handler(
      std::function<void(const QuaderInputChord &)> handler) {
    chord_handler_ = std::move(handler);
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event != nullptr) {
      const std::optional<QuaderInputChord> chord =
          quader_pressed_input_chord_from_qt_event(*event);
      if (chord.has_value() && chord_handler_) {
        chord_handler_(*chord);
        event->accept();
        return;
      }
    }
    QLineEdit::keyPressEvent(event);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event != nullptr) {
      const std::optional<QuaderInputChord> chord =
          quader_mouse_input_chord_from_qt_event(
              *event, QuaderInputGesture::Press);
      if (chord && chord_handler_) {
        chord_handler_(*chord);
        event->accept();
        return;
      }
    }
    QLineEdit::mousePressEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override {
    if (event != nullptr) {
      const std::optional<QuaderInputChord> chord =
          quader_mouse_input_chord_from_qt_event(
              *event, QuaderInputGesture::DoubleClick);
      if (chord && chord_handler_) {
        chord_handler_(*chord);
        event->accept();
        return;
      }
    }
    QLineEdit::mouseDoubleClickEvent(event);
  }

  void wheelEvent(QWheelEvent *event) override {
    if (event != nullptr) {
      const std::optional<QuaderInputChord> chord =
          quader_wheel_input_chord_from_qt_event(*event);
      if (chord && chord_handler_) {
        chord_handler_(*chord);
        event->accept();
        return;
      }
    }
    QLineEdit::wheelEvent(event);
  }

private:
  std::function<void(const QuaderInputChord &)> chord_handler_;
};

/**
 * Captures one explicit sequence while preserving event-time mouse modifiers.
 */
class QtKeybindingRecorder final : public QLineEdit {
public:
  explicit QtKeybindingRecorder(
      std::function<std::string(const QuaderInputChord &)> formatter,
      QWidget *parent = nullptr)
      : QLineEdit(parent), formatter_(std::move(formatter)) {
    setReadOnly(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
  }

  void set_changed_handler(std::function<void()> handler) {
    changed_handler_ = std::move(handler);
  }

  [[nodiscard]] const std::vector<QuaderInputChord> &sequence() const {
    return sequence_;
  }

  [[nodiscard]] bool ready_to_use() const {
    return !sequence_.empty() && !pending_mouse_ && !pending_click_;
  }

  [[nodiscard]] bool has_capture() const {
    return !sequence_.empty() || pending_mouse_.has_value() ||
           pending_click_.has_value();
  }

  void finish_sequence() { flush_pending_click(); }

  void clear_sequence() {
    ++mouse_generation_;
    pending_mouse_.reset();
    pending_click_.reset();
    dragging_ = false;
    press_committed_ = false;
    sequence_.clear();
    clear();
    notify_changed();
  }

protected:
  bool event(QEvent *event) override {
    if (event != nullptr && event->type() == QEvent::KeyPress) {
      auto *key_event = static_cast<QKeyEvent *>(event);
      const std::optional<QuaderInputChord> chord =
          quader_pressed_input_chord_from_qt_event(*key_event);
      if (chord) {
        append(*chord);
        event->accept();
        return true;
      }
    }
    return QLineEdit::event(event);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event != nullptr) {
      const std::optional<QuaderInputChord> chord =
          quader_mouse_input_chord_from_qt_event(
              *event, QuaderInputGesture::Press);
      if (chord) {
        flush_pending_click();
        ++mouse_generation_;
        pending_mouse_ = *chord;
        mouse_press_position_ = event->position();
        dragging_ = false;
        press_committed_ = false;
        notify_changed();
        const std::size_t generation = mouse_generation_;
        QTimer::singleShot(QApplication::doubleClickInterval(), this,
                           [this, generation]() {
                             if (generation != mouse_generation_ ||
                                 !pending_mouse_ || dragging_) {
                               return;
                             }
                             QuaderInputChord held = *pending_mouse_;
                             pending_mouse_.reset();
                             press_committed_ = true;
                             append(held);
                           });
        event->accept();
        return;
      }
    }
    QLineEdit::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (event != nullptr && pending_mouse_) {
      const QPointF delta = event->position() - mouse_press_position_;
      if (delta.manhattanLength() >= QApplication::startDragDistance()) {
        dragging_ = true;
      }
      event->accept();
      return;
    }
    QLineEdit::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event != nullptr && press_committed_) {
      press_committed_ = false;
      event->accept();
      return;
    }
    if (event != nullptr && pending_mouse_) {
      QuaderInputChord chord = *pending_mouse_;
      pending_mouse_.reset();
      if (dragging_) {
        ++mouse_generation_;
        chord.gesture = QuaderInputGesture::Drag;
        append(chord);
      } else {
        chord.gesture = QuaderInputGesture::Click;
        const std::size_t generation = mouse_generation_;
        pending_click_ = chord;
        notify_changed();
        QTimer::singleShot(QApplication::doubleClickInterval(), this,
                           [this, generation]() {
                             if (generation == mouse_generation_) {
                               flush_pending_click();
                             }
                           });
      }
      dragging_ = false;
      event->accept();
      return;
    }
    QLineEdit::mouseReleaseEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override {
    if (event != nullptr) {
      const std::optional<QuaderInputChord> chord =
          quader_mouse_input_chord_from_qt_event(
              *event, QuaderInputGesture::DoubleClick);
      if (chord) {
        ++mouse_generation_;
        pending_mouse_.reset();
        pending_click_.reset();
        dragging_ = false;
        press_committed_ = false;
        append(*chord);
        event->accept();
        return;
      }
    }
    QLineEdit::mouseDoubleClickEvent(event);
  }

  void wheelEvent(QWheelEvent *event) override {
    if (event != nullptr) {
      const std::optional<QuaderInputChord> chord =
          quader_wheel_input_chord_from_qt_event(*event);
      if (chord) {
        append(*chord);
        event->accept();
        return;
      }
    }
    QLineEdit::wheelEvent(event);
  }

private:
  void append(const QuaderInputChord &chord) {
    flush_pending_click();
    append_now(chord);
  }

  void append_now(const QuaderInputChord &chord) {
    if (chord.empty()) {
      return;
    }
    sequence_.push_back(chord);
    QString presentation;
    for (const QuaderInputChord &captured : sequence_) {
      if (!presentation.isEmpty()) {
        presentation.append(QLatin1Char(' '));
      }
      presentation.append(QString::fromStdString(formatter_(captured)));
    }
    setText(presentation);
    notify_changed();
  }

  void flush_pending_click() {
    if (!pending_click_) {
      return;
    }
    QuaderInputChord click = *pending_click_;
    pending_click_.reset();
    append_now(click);
  }

  void notify_changed() const {
    if (changed_handler_) {
      changed_handler_();
    }
  }

  std::function<std::string(const QuaderInputChord &)> formatter_;
  std::function<void()> changed_handler_;
  std::vector<QuaderInputChord> sequence_;
  std::optional<QuaderInputChord> pending_mouse_;
  std::optional<QuaderInputChord> pending_click_;
  QPointF mouse_press_position_;
  std::size_t mouse_generation_ = 0u;
  bool dragging_ = false;
  bool press_committed_ = false;
};

/**
 * Records a complete keyboard/mouse sequence before conflict analysis.
 */
class QtKeybindingCaptureDialog final : public QDialog {
public:
  QtKeybindingCaptureDialog(const QuaderCommandsApi &commands,
                            const LocalizationService *localization,
                            bool has_mapping, QWidget *parent)
      : QDialog(parent), commands_(commands) {
    setObjectName(QStringLiteral("settingsKeybindingCaptureDialog"));
    setWindowTitle(qtext(localization,
                         "ui.settings.keybindings.capture.title",
                         "Record Input Mapping"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(
        qtext(localization, "ui.settings.keybindings.capture.hint",
              "Press keys, click, double-click, drag, scroll, or hold a "
              "mouse button to record Press. Add more inputs for a sequence, "
              "then choose Use Mapping."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    recorder_ = new QtKeybindingRecorder(
        [&commands](const QuaderInputChord &chord) {
          return commands.format_input_mapping(chord);
        },
        this);
    recorder_->setObjectName(
        QStringLiteral("settingsKeybindingCaptureRecorder"));
    recorder_->setPlaceholderText(qtext(
        localization, "ui.settings.keybindings.capture.placeholder",
        "Press input to record"));
    layout->addWidget(recorder_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    clear_button_ = buttons->addButton(
        qtext(localization, "ui.settings.keybindings.capture.clear",
              "Clear Capture"),
        QDialogButtonBox::ResetRole);
    unassign_button_ = buttons->addButton(
        qtext(localization, "ui.settings.keybindings.capture.unassign",
              "Unassign"),
        QDialogButtonBox::DestructiveRole);
    use_button_ = buttons->addButton(
        qtext(localization, "ui.settings.keybindings.capture.use",
              "Use Mapping"),
        QDialogButtonBox::AcceptRole);
    clear_button_->setObjectName(
        QStringLiteral("settingsKeybindingCaptureClear"));
    unassign_button_->setObjectName(
        QStringLiteral("settingsKeybindingCaptureUnassign"));
    use_button_->setObjectName(
        QStringLiteral("settingsKeybindingCaptureUse"));
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
      cancel->setObjectName(QStringLiteral("settingsKeybindingCaptureCancel"));
    }
    clear_button_->setEnabled(false);
    unassign_button_->setEnabled(has_mapping);
    use_button_->setEnabled(false);
    layout->addWidget(buttons);

    recorder_->set_changed_handler([this]() {
      use_button_->setEnabled(recorder_->ready_to_use());
      clear_button_->setEnabled(recorder_->has_capture());
    });
    QObject::connect(clear_button_, &QPushButton::clicked, this,
                     [this]() { recorder_->clear_sequence(); });
    QObject::connect(unassign_button_, &QPushButton::clicked, this, [this]() {
      unassign_requested_ = true;
      accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, this,
                     [this]() {
                       unassign_requested_ = false;
                       accept();
                     });
    QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                     &QDialog::reject);
  }

  [[nodiscard]] std::string shortcut() {
    recorder_->finish_sequence();
    std::string result;
    for (const QuaderInputChord &chord : recorder_->sequence()) {
      if (!result.empty()) {
        result.push_back(' ');
      }
      result.append(commands_.format_input_mapping(chord));
    }
    return result;
  }

  [[nodiscard]] bool unassign_requested() const {
    return unassign_requested_;
  }

protected:
  void showEvent(QShowEvent *event) override {
    QDialog::showEvent(event);
    recorder_->setFocus(Qt::OtherFocusReason);
  }

private:
  const QuaderCommandsApi &commands_;
  QtKeybindingRecorder *recorder_ = nullptr;
  QPushButton *clear_button_ = nullptr;
  QPushButton *unassign_button_ = nullptr;
  QPushButton *use_button_ = nullptr;
  bool unassign_requested_ = false;
};

} // namespace

/// Owns the retained settings controls and descriptor-to-widget bindings.
struct QtSettingsDialog::Impl {
  /// Retains one user-facing category or group and its localization source.
  struct CategoryWidgets {
    QListWidgetItem *item = nullptr;
    std::string localization_key;
    std::string label;
  };

  struct GroupWidgets {
    qt_settings_detail::QtSettingsSection *section = nullptr;
    QWidget *page = nullptr;
    QFormLayout *form = nullptr;
    std::string localization_key;
    std::string label;
  };

  /// Holds the scrollable stock-Qt page created for one settings category.
  struct OptionsPageWidgets {
    QScrollArea *page = nullptr;
    QWidget *content = nullptr;
    QFormLayout *form = nullptr;
  };

  /// Binds a viewport color descriptor to its label and picker button.
  struct ColorWidgets {
    const ViewportColorSettingDescriptor *descriptor = nullptr;
    QLabel *label = nullptr;
    QtColorSwatchButton *button = nullptr;
  };

  /// Binds a viewport float descriptor to its label and stock spin box.
  struct FloatWidgets {
    const ViewportFloatSettingDescriptor *descriptor = nullptr;
    QLabel *label = nullptr;
    QDoubleSpinBox *spin_box = nullptr;
  };

  /// Binds a viewport boolean descriptor to its stock check box.
  struct BoolWidgets {
    const ViewportBoolSettingDescriptor *descriptor = nullptr;
    QCheckBox *check_box = nullptr;
  };

  explicit Impl(QtSettingsDialog &dialog) : owner(dialog) { build(); }
  ~Impl() {
    if (lifecycle_input) {
      lifecycle_input->abandon();
    }
  }

  QtSettingsDialog &owner;
  UISettings *target_ui_settings = nullptr;
  ViewportSettings *target_viewport_settings = nullptr;
  UISettings draft_ui_settings = UISettings::defaults();
  ViewportSettings draft_viewport_settings = ViewportSettings::defaults();
  UISettings *ui_settings = nullptr;
  ViewportSettings *viewport_settings = nullptr;
  LocalizationService *localization = nullptr;
  QuaderCommandsApi *commands = nullptr;
  QPointer<QtApplyCancelInputFilter> lifecycle_input;
  QtSettingsController controller;
  QtSettingsController::ChangeCallback settings_changed;
  std::function<bool()> persist_interface_settings;
  std::filesystem::path settings_directory;
  std::filesystem::path built_in_default_texture;
  std::function<std::optional<std::filesystem::path>()> default_texture_picker;
  std::uint64_t context_generation = 0;
  bool default_material_dirty = false;
  std::filesystem::path default_material_target_baseline;
  std::filesystem::path default_material_preview_path;
  QtMaterialPresentationItem default_material_preview_item;
  std::array<bool, kViewportColorSettingCount> dirty_colors{};
  std::array<bool, kViewportFloatSettingCount> dirty_floats{};
  std::array<bool, kViewportBoolSettingCount> dirty_bools{};
  bool interface_scale_dirty = false;
  float interface_scale_target_baseline = 1.0f;
  bool vsync_dirty = false;
  bool vsync_target_baseline = true;
  bool locale_dirty = false;
  std::array<bool, 4> session_dirty{};
  SessionPreferences session_target_baseline;
  bool transaction_active = false;
  bool refreshing = false;
  bool keybinding_message_box_prewarm_scheduled = false;

  QtRetainedMessageBox *keybinding_message_box = nullptr;
  QListWidget *category_list = nullptr;
  QStackedWidget *page_stack = nullptr;
  QListWidgetItem *keybindings_category_item = nullptr;
  QLabel *locale_label = nullptr;
  QComboBox *locale_combo = nullptr;
  QLabel *interface_scale_label = nullptr;
  QSpinBox *interface_scale_spin_box = nullptr;
  QCheckBox *vsync_check_box = nullptr;
  QLabel *default_material_preview = nullptr;
  QLabel *default_material_path = nullptr;
  QPushButton *default_material_browse = nullptr;
  QLabel *undo_steps_label = nullptr;
  QSpinBox *undo_steps_spin_box = nullptr;
  QLabel *undo_help_label = nullptr;
  QCheckBox *autosave_check_box = nullptr;
  QLabel *autosave_interval_label = nullptr;
  QSpinBox *autosave_interval_spin_box = nullptr;
  QLabel *autosave_max_files_label = nullptr;
  QSpinBox *autosave_max_files_spin_box = nullptr;
  QLabel *autosave_help_label = nullptr;
  QLabel *keybindings_search_label = nullptr;
  QLineEdit *keybindings_search = nullptr;
  QLabel *keybindings_shortcut_label = nullptr;
  QtKeybindingCaptureEdit *keybindings_shortcut_filter = nullptr;
  QPushButton *keybindings_shortcut_clear = nullptr;
  QtKeybindingTableView *keybindings_table = nullptr;
  QtKeybindingTableModel *keybindings_model = nullptr;
  QLabel *keybindings_status = nullptr;
  QPushButton *keybindings_reset_action = nullptr;
  QPushButton *keybindings_reset_all = nullptr;
  QDialogButtonBox *button_box = nullptr;
  QPushButton *apply_button = nullptr;
  QPushButton *ok_button = nullptr;
  QPushButton *cancel_button = nullptr;
  std::vector<CategoryWidgets> categories;
  std::vector<GroupWidgets> groups;
  std::vector<ColorWidgets> color_controls;
  std::vector<FloatWidgets> float_controls;
  std::vector<BoolWidgets> bool_controls;
  std::vector<KeybindingTableRow> keybinding_rows;
  QString keybinding_shortcut_text;
  std::array<double, 3> keybinding_column_design_widths{
      320.0, 180.0, 180.0};
  float applied_ui_scale_factor = 1.0f;
  bool applying_ui_scale_to_keybinding_columns = false;
  QtUiScaleMetrics layout_metrics;
  const LocalizationService *measured_localization = nullptr;
  std::uint64_t measured_localization_revision = 0u;
  QFont measured_font;
  float measured_scale = 0.0f;
  bool layout_measurement_queued = false;

  [[nodiscard]] bool has_context() const {
    return target_ui_settings != nullptr &&
           target_viewport_settings != nullptr && localization != nullptr &&
           commands != nullptr;
  }

  void clear_dirty() {
    dirty_colors.fill(false);
    dirty_floats.fill(false);
    dirty_bools.fill(false);
    interface_scale_dirty = false;
    vsync_dirty = false;
    default_material_dirty = false;
    locale_dirty = false;
    session_dirty.fill(false);
  }

  [[nodiscard]] bool has_pending_changes() const {
    return interface_scale_dirty || vsync_dirty || locale_dirty ||
           default_material_dirty ||
           std::any_of(session_dirty.begin(), session_dirty.end(),
                       [](bool dirty) { return dirty; }) ||
           std::any_of(dirty_colors.begin(), dirty_colors.end(),
                       [](bool dirty) { return dirty; }) ||
           std::any_of(dirty_floats.begin(), dirty_floats.end(),
                       [](bool dirty) { return dirty; }) ||
           std::any_of(dirty_bools.begin(), dirty_bools.end(),
                       [](bool dirty) { return dirty; });
  }

  void refresh_action_state() const {
    if (apply_button != nullptr) {
      apply_button->setEnabled(transaction_active && has_pending_changes());
    }
  }

  void synchronize_draft_from_targets(bool force) {
    if (!has_context()) {
      clear_dirty();
      refresh_action_state();
      return;
    }

    for (const ViewportColorSettingDescriptor &descriptor :
         viewport_color_settings()) {
      const std::size_t id = static_cast<std::size_t>(descriptor.id);
      if (!force && dirty_colors[id]) {
        dirty_colors[id] = !viewport_setting_colors_approximately(
            draft_viewport_settings.color(descriptor.id),
            target_viewport_settings->color(descriptor.id));
      }
      if (force || !dirty_colors[id]) {
        (void)draft_viewport_settings.restore_color(
            descriptor.id, target_viewport_settings->color(descriptor.id));
        dirty_colors[id] = false;
      }
    }
    for (const ViewportFloatSettingDescriptor &descriptor :
         viewport_float_settings()) {
      const std::size_t id = static_cast<std::size_t>(descriptor.id);
      if (!force && dirty_floats[id]) {
        dirty_floats[id] =
            std::fabs(draft_viewport_settings.float_value(descriptor.id) -
                      target_viewport_settings->float_value(descriptor.id)) >
            0.0001f;
      }
      if (force || !dirty_floats[id]) {
        (void)draft_viewport_settings.set_float_value(
            descriptor.id,
            target_viewport_settings->float_value(descriptor.id));
        dirty_floats[id] = false;
      }
    }
    for (const ViewportBoolSettingDescriptor &descriptor :
         viewport_bool_settings()) {
      const std::size_t id = static_cast<std::size_t>(descriptor.id);
      if (!force && dirty_bools[id]) {
        dirty_bools[id] =
            draft_viewport_settings.bool_value(descriptor.id) !=
            target_viewport_settings->bool_value(descriptor.id);
      }
      if (force || !dirty_bools[id]) {
        (void)draft_viewport_settings.set_bool_value(
            descriptor.id,
            target_viewport_settings->bool_value(descriptor.id));
        dirty_bools[id] = false;
      }
    }

    if (!force && locale_dirty) {
      locale_dirty =
          draft_ui_settings.locale() != target_ui_settings->locale();
    }
    if (force || !locale_dirty) {
      draft_ui_settings.set_locale(target_ui_settings->locale());
      locale_dirty = false;
    }
    const float target_interface_scale =
        target_ui_settings->interface_scale();
    const bool interface_scale_changed_externally =
        !force && transaction_active &&
        std::fabs(target_interface_scale - interface_scale_target_baseline) >
            0.0001f;
    if (!force && !interface_scale_changed_externally &&
        interface_scale_dirty) {
      interface_scale_dirty =
          std::fabs(draft_ui_settings.interface_scale() -
                    target_interface_scale) >
          0.0001f;
    }
    if (force || interface_scale_changed_externally ||
        !interface_scale_dirty) {
      draft_ui_settings.set_interface_scale(target_interface_scale);
      interface_scale_dirty = false;
      interface_scale_target_baseline = target_interface_scale;
    }
    const bool target_vsync = target_ui_settings->vsync_enabled();
    const bool vsync_changed_externally =
        !force && transaction_active && target_vsync != vsync_target_baseline;
    if (!force && !vsync_changed_externally && vsync_dirty) {
      vsync_dirty = draft_ui_settings.vsync_enabled() != target_vsync;
    }
    if (force || vsync_changed_externally || !vsync_dirty) {
      draft_ui_settings.set_vsync_enabled(target_vsync);
      vsync_dirty = false;
      vsync_target_baseline = target_vsync;
    }
    synchronize_session_draft(force);
    synchronize_default_material_draft(force);
    refresh_action_state();
  }

  void synchronize_default_material_draft(bool force) {
    const std::filesystem::path &target = target_ui_settings->default_material_texture();
    const bool externally_changed = !force && transaction_active &&
        target != default_material_target_baseline;
    if (!force && default_material_dirty && !externally_changed) {
      default_material_dirty = draft_ui_settings.default_material_texture() != target;
    } else {
      draft_ui_settings.set_default_material_texture(target);
      default_material_dirty = false;
    }
    default_material_target_baseline = target;
  }

  void synchronize_session_draft(bool force) {
    SessionPreferences draft = draft_ui_settings.session_settings();
    const SessionPreferences &target = target_ui_settings->session_settings();
    const auto synchronize_field =
        [this, force](auto &value, const auto target_value,
                      const auto baseline_value, std::size_t index) {
          const bool changed_externally = !force && transaction_active &&
                                          target_value != baseline_value;
          if (!force && !changed_externally && session_dirty[index]) {
            session_dirty[index] = value != target_value;
          }
          if (force || changed_externally || !session_dirty[index]) {
            value = target_value;
            session_dirty[index] = false;
          }
        };
    synchronize_field(draft.undo_steps, target.undo_steps,
                      session_target_baseline.undo_steps, 0);
    synchronize_field(draft.autosave_enabled, target.autosave_enabled,
                      session_target_baseline.autosave_enabled, 1);
    synchronize_field(draft.autosave_interval_minutes,
                      target.autosave_interval_minutes,
                      session_target_baseline.autosave_interval_minutes, 2);
    synchronize_field(draft.autosave_max_files, target.autosave_max_files,
                      session_target_baseline.autosave_max_files, 3);
    (void)draft_ui_settings.set_session_settings(draft);
    session_target_baseline = target;
  }

  template <typename Value>
  void stage_session_field(Value SessionPreferences::*field, Value value,
                           std::size_t index) {
    if (refreshing || !has_context()) {
      return;
    }
    SessionPreferences draft = draft_ui_settings.session_settings();
    draft.*field = value;
    if (!draft_ui_settings.set_session_settings(draft)) {
      return;
    }
    const SessionPreferences &target = target_ui_settings->session_settings();
    session_dirty[index] = draft.*field != target.*field;
    refresh_action_state();
  }

  void mark_color_dirty(ViewportColorSettingId id) {
    if (!has_context()) {
      return;
    }
    dirty_colors[static_cast<std::size_t>(id)] =
        !viewport_setting_colors_approximately(
            draft_viewport_settings.color(id),
            target_viewport_settings->color(id));
    refresh_action_state();
  }

  void mark_float_dirty(ViewportFloatSettingId id) {
    if (!has_context()) {
      return;
    }
    dirty_floats[static_cast<std::size_t>(id)] =
        std::fabs(draft_viewport_settings.float_value(id) -
                  target_viewport_settings->float_value(id)) >
        0.0001f;
    refresh_action_state();
  }

  void mark_bool_dirty(ViewportBoolSettingId id) {
    if (!has_context()) {
      return;
    }
    dirty_bools[static_cast<std::size_t>(id)] =
        draft_viewport_settings.bool_value(id) !=
        target_viewport_settings->bool_value(id);
    refresh_action_state();
  }

  void mark_interface_scale_dirty() {
    if (!has_context()) {
      return;
    }
    interface_scale_dirty =
        std::fabs(draft_ui_settings.interface_scale() -
                  target_ui_settings->interface_scale()) >
        0.0001f;
    refresh_action_state();
  }

  void begin_transaction() {
    if (!has_context() || transaction_active) {
      return;
    }
    clear_dirty();
    synchronize_draft_from_targets(true);
    transaction_active = true;
    refresh();
  }

  void schedule_keybinding_message_box_prewarm() {
    if (keybinding_message_box == nullptr ||
        keybinding_message_box_prewarm_scheduled) {
      return;
    }
    keybinding_message_box_prewarm_scheduled = true;
    QTimer::singleShot(0, &owner, [this]() {
      keybinding_message_box_prewarm_scheduled = false;
      if (keybinding_message_box != nullptr && owner.isVisible()) {
        keybinding_message_box->prewarm();
      }
    });
  }

  bool apply_changes() {
    if (!transaction_active || !has_context()) {
      return true;
    }
    synchronize_draft_from_targets(false);

    const UISettings previous_ui_settings = *target_ui_settings;
    const ViewportSettings previous_viewport_settings =
        *target_viewport_settings;
    std::filesystem::path imported_default_material =
        target_ui_settings->default_material_texture();
    if (default_material_dirty) {
      const auto imported = DefaultMaterialTextureStore::import_image(
          draft_ui_settings.default_material_texture(), settings_directory);
      if (!imported.ok()) {
        show_default_material_error();
        return false;
      }
      imported_default_material = imported.value();
    }
    if (locale_dirty &&
        (!localization->has_locale(draft_ui_settings.locale()) ||
         !localization->select_locale(draft_ui_settings.locale()))) {
      return false;
    }

    bool viewport_changed = false;
    for (const ViewportColorSettingDescriptor &descriptor :
         viewport_color_settings()) {
      const std::size_t id = static_cast<std::size_t>(descriptor.id);
      if (dirty_colors[id]) {
        viewport_changed = target_viewport_settings->restore_color(
                               descriptor.id,
                               draft_viewport_settings.color(descriptor.id)) ||
                           viewport_changed;
      }
    }
    for (const ViewportFloatSettingDescriptor &descriptor :
         viewport_float_settings()) {
      const std::size_t id = static_cast<std::size_t>(descriptor.id);
      if (dirty_floats[id]) {
        viewport_changed = target_viewport_settings->set_float_value(
                               descriptor.id,
                               draft_viewport_settings.float_value(
                                   descriptor.id)) ||
                           viewport_changed;
      }
    }
    for (const ViewportBoolSettingDescriptor &descriptor :
         viewport_bool_settings()) {
      const std::size_t id = static_cast<std::size_t>(descriptor.id);
      if (dirty_bools[id]) {
        viewport_changed = target_viewport_settings->set_bool_value(
                               descriptor.id,
                               draft_viewport_settings.bool_value(
                                   descriptor.id)) ||
                           viewport_changed;
      }
    }

    const bool locale_changed =
        locale_dirty &&
        target_ui_settings->locale() != localization->current_locale();
    const bool scale_changed =
        interface_scale_dirty &&
        std::fabs(target_ui_settings->interface_scale() -
                  draft_ui_settings.interface_scale()) >
            0.0001f;
    const bool vsync_changed =
        vsync_dirty && target_ui_settings->vsync_enabled() !=
                           draft_ui_settings.vsync_enabled();
    const bool default_material_changed =
        imported_default_material != target_ui_settings->default_material_texture();
    if (default_material_changed) {
      target_ui_settings->set_default_material_texture(imported_default_material);
    }
    SessionPreferences merged_session = target_ui_settings->session_settings();
    const SessionPreferences &draft_session = draft_ui_settings.session_settings();
    if (session_dirty[0]) {
      merged_session.undo_steps = draft_session.undo_steps;
    }
    if (session_dirty[1]) {
      merged_session.autosave_enabled = draft_session.autosave_enabled;
    }
    if (session_dirty[2]) {
      merged_session.autosave_interval_minutes =
          draft_session.autosave_interval_minutes;
    }
    if (session_dirty[3]) {
      merged_session.autosave_max_files = draft_session.autosave_max_files;
    }
    const bool session_changed =
        merged_session != target_ui_settings->session_settings();
    if (session_changed) {
      (void)target_ui_settings->set_session_settings(merged_session);
    }
    if (locale_dirty) {
      target_ui_settings->set_locale(localization->current_locale());
    }
    if (interface_scale_dirty) {
      target_ui_settings->set_interface_scale(
          draft_ui_settings.interface_scale());
    }
    if (vsync_dirty) {
      target_ui_settings->set_vsync_enabled(draft_ui_settings.vsync_enabled());
    }
    const float committed_interface_scale =
        target_ui_settings->interface_scale();

    if ((locale_changed || scale_changed || vsync_changed || session_changed ||
         default_material_changed) &&
        persist_interface_settings &&
        !persist_interface_settings()) {
      *target_ui_settings = previous_ui_settings;
      *target_viewport_settings = previous_viewport_settings;
      (void)localization->select_locale(previous_ui_settings.locale());
      if (settings_changed) {
        if (locale_changed || scale_changed) {
          settings_changed({.kind = QtSettingsChangeKind::InterfaceCommit,
                            .interface_scale =
                                previous_ui_settings.interface_scale()});
        }
        if (vsync_changed) {
          settings_changed({.kind = QtSettingsChangeKind::RenderingCommit,
                            .vsync_enabled =
                                previous_ui_settings.vsync_enabled()});
        }
      }
      if (keybinding_message_box->set_presentation(
              QtRetainedMessageBoxPresentation{
              .icon = QMessageBox::Critical,
              .object_name =
                  QStringLiteral("settingsPersistenceErrorDialog"),
              .title = qtext(localization, "ui.panel.settings", "Settings"),
              .text = qtext(localization,
                            "ui.runtime.preferences_save_failed",
                            "Preferences could not be saved."),
              .buttons =
                  {{
                      .button = QtRetainedMessageBoxButton::Primary,
                      .object_name = QStringLiteral(
                          "settingsPersistenceErrorDialog.ok"),
                      .label = qtext(localization, "ui.action.ok", "OK"),
                      .role = QMessageBox::AcceptRole,
                  }},
              .default_button = QtRetainedMessageBoxButton::Primary,
              .escape_button = QtRetainedMessageBoxButton::Primary,
                  .close_button = QtRetainedMessageBoxButton::Primary,
              })) {
        keybinding_message_box->prewarm();
        keybinding_message_box->open();
      }
      refresh();
      return false;
    }

    if (viewport_changed && settings_changed) {
      settings_changed({.kind = QtSettingsChangeKind::ViewportCommit});
    }
    if ((locale_changed || scale_changed) && settings_changed) {
      settings_changed({.kind = QtSettingsChangeKind::InterfaceCommit,
                        .interface_scale = committed_interface_scale});
    }
    if (vsync_changed && settings_changed) {
      settings_changed({.kind = QtSettingsChangeKind::RenderingCommit,
                        .vsync_enabled = target_ui_settings->vsync_enabled()});
    }
    if (session_changed && settings_changed) {
      settings_changed({.kind = QtSettingsChangeKind::SessionCommit,
                        .session_settings = merged_session});
    }
    if (default_material_changed && settings_changed) {
      settings_changed({.kind = QtSettingsChangeKind::DefaultMaterialCommit,
                        .default_material_texture = imported_default_material});
    }

    clear_dirty();
    synchronize_draft_from_targets(true);
    refresh();
    return true;
  }

  void discard_changes() {
    if (!transaction_active) {
      return;
    }
    if (interface_scale_dirty && settings_changed &&
        std::fabs(draft_ui_settings.interface_scale() -
                  target_ui_settings->interface_scale()) >
            0.0001f) {
      settings_changed({.kind = QtSettingsChangeKind::InterfacePreview,
                        .interface_scale =
                            target_ui_settings->interface_scale()});
    }
    if (vsync_dirty && settings_changed &&
        draft_ui_settings.vsync_enabled() !=
            target_ui_settings->vsync_enabled()) {
      settings_changed({.kind = QtSettingsChangeKind::RenderingPreview,
                        .vsync_enabled = target_ui_settings->vsync_enabled()});
    }
    transaction_active = false;
    clear_dirty();
    synchronize_draft_from_targets(true);
  }

  void build() {
    owner.setObjectName(QStringLiteral("settingsDialog"));
    owner.setWindowModality(Qt::WindowModal);
    owner.setAttribute(Qt::WA_DeleteOnClose, false);
    owner.setSizeGripEnabled(true);
    owner.resize(720, 560);

    keybinding_message_box = new QtRetainedMessageBox(&owner);

    auto *dialog_layout = new QVBoxLayout(&owner);
    qt_settings_detail::set_scalable_margins(
        *dialog_layout, QMargins(12, 12, 12, 12));
    qt_settings_detail::set_scalable_spacing(*dialog_layout, 10);
    auto *content_layout = new QHBoxLayout();
    qt_settings_detail::set_scalable_spacing(*content_layout, 12);
    category_list = new QtSettingsCategoryList(&owner);
    category_list->setObjectName(QStringLiteral("settingsCategories"));
    category_list->setMinimumWidth(160);
    category_list->setMaximumWidth(220);
    content_layout->addWidget(category_list);

    auto *options_container = new QWidget(&owner);
    options_container->setObjectName(QStringLiteral("settingsOptions"));
    auto *options_layout = new QVBoxLayout(options_container);
    qt_settings_detail::set_scalable_margins(
        *options_layout, QMargins(0, 0, 0, 0));
    page_stack = new QStackedWidget(options_container);
    page_stack->setObjectName(QStringLiteral("settingsPages"));
    options_layout->addWidget(page_stack, 1);

    content_layout->addWidget(options_container, 1);
    dialog_layout->addLayout(content_layout, 1);

    button_box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
            QDialogButtonBox::Apply,
        &owner);
    button_box->setObjectName(QStringLiteral("settingsButtonBox"));
    apply_button = button_box->button(QDialogButtonBox::Apply);
    ok_button = button_box->button(QDialogButtonBox::Ok);
    cancel_button = button_box->button(QDialogButtonBox::Cancel);
    apply_button->setObjectName(QStringLiteral("settingsApply"));
    ok_button->setObjectName(QStringLiteral("settingsOk"));
    cancel_button->setObjectName(QStringLiteral("settingsCancel"));
    apply_button->setAutoDefault(false);
    apply_button->setDefault(false);
    cancel_button->setAutoDefault(false);
    cancel_button->setDefault(false);
    ok_button->setAutoDefault(true);
    ok_button->setDefault(true);
    dialog_layout->addWidget(button_box);

    QObject::connect(category_list, &QListWidget::currentRowChanged, &owner,
                     [this](int row) {
                       if (row >= 0 && row < page_stack->count()) {
                         page_stack->setCurrentIndex(row);
                       }
                     });
    QObject::connect(apply_button, &QPushButton::clicked, &owner,
                     [this]() { (void)apply_changes(); });
    QObject::connect(button_box, &QDialogButtonBox::accepted, &owner,
                     [this]() { owner.accept(); });
    QObject::connect(button_box, &QDialogButtonBox::rejected, &owner,
                     [this]() { owner.reject(); });
    lifecycle_input = new QtApplyCancelInputFilter(
        owner, *apply_button, *cancel_button, [this]() { return commands; },
        [](const QWidget *widget) {
          return dynamic_cast<const QtKeybindingCaptureEdit *>(widget) != nullptr;
        }, ok_button);

    create_interface_page();
    const OptionsPageWidgets navigation_page = create_category(
        "navigation", "ui.settings.navigation", "Navigation");
    create_keybindings_page();
    const OptionsPageWidgets viewport_page = create_category(
        "viewport", "ui.settings.viewport", "Viewport");
    create_display_group(viewport_page);
    create_default_material_group(viewport_page);
    const OptionsPageWidgets appearance_page = create_category(
        "appearance", "ui.settings.appearance", "Colors & Overlays");
    const OptionsPageWidgets editing_page = create_category(
        "editing", "ui.settings.editing", "Editing");
    create_undo_group(editing_page);
    const OptionsPageWidgets recovery_page = create_category(
        "save_recovery", "ui.settings.save_recovery", "Save & Recovery");
    create_autosave_group(recovery_page);

    std::array<QFormLayout *, kViewportSettingsSectionCount> section_forms{};
    for (const ViewportSettingsSectionDescriptor &descriptor :
         viewport_settings_sections()) {
      const OptionsPageWidgets *page = &appearance_page;
      switch (descriptor.id) {
      case ViewportSettingsSectionId::Navigation:
        page = &navigation_page;
        break;
      case ViewportSettingsSectionId::Shading:
      case ViewportSettingsSectionId::Picking:
      case ViewportSettingsSectionId::Gizmo:
        page = &viewport_page;
        break;
      case ViewportSettingsSectionId::Texturing:
        continue;
      default:
        break;
      }
      section_forms[static_cast<std::size_t>(descriptor.id)] =
          create_viewport_section(*page, descriptor);
    }

    bool_controls.reserve(1);
    for (const ViewportBoolSettingDescriptor &descriptor :
         viewport_bool_settings()) {
      // Other toggles are workspace controls exposed by menus and toolbars.
      if (descriptor.id != ViewportBoolSettingId::ShowHoverDimensions) {
        continue;
      }
      QFormLayout *form =
          section_forms[static_cast<std::size_t>(descriptor.section)];
      auto *check_box = new QCheckBox(form->parentWidget());
      check_box->setObjectName(setting_object_name(descriptor.key));
      form->addRow(check_box);
      bool_controls.push_back({&descriptor, check_box});

      QObject::connect(check_box, &QCheckBox::toggled, &owner,
                       [this, id = descriptor.id](bool checked) {
                         if (refreshing || viewport_settings == nullptr) {
                           return;
                         }
                         if (controller.set_bool(id, checked)) {
                           mark_bool_dirty(id);
                         }
                       });
    }

    color_controls.reserve(kViewportColorSettingCount);
    for (const ViewportColorSettingDescriptor &descriptor :
         viewport_color_settings()) {
      if (!release_has_feature(ReleaseFeature::MeshGrid) &&
          (descriptor.id == ViewportColorSettingId::MeshGridMinor ||
           descriptor.id == ViewportColorSettingId::MeshGridMajor)) {
        continue;
      }
      QFormLayout *form =
          section_forms[static_cast<std::size_t>(descriptor.section)];
      auto *label = new QLabel(form->parentWidget());
      auto *button = new QtColorSwatchButton(form->parentWidget());
      button->setObjectName(setting_object_name(descriptor.key));
      button->setAutoDefault(false);
      button->setDefault(false);
      label->setBuddy(button);
      form->addRow(label, button);
      color_controls.push_back({&descriptor, label, button});

      QObject::connect(button, &QPushButton::clicked, &owner,
                       [this, descriptor_ptr = &descriptor, button]() {
                         choose_color(*descriptor_ptr, *button);
                       });
    }

    float_controls.reserve(kViewportFloatSettingCount);
    for (const ViewportFloatSettingDescriptor &descriptor :
         viewport_float_settings()) {
      if (descriptor.id == ViewportFloatSettingId::RotationSnapDegrees) {
        continue;
      }
      QFormLayout *form =
          section_forms[static_cast<std::size_t>(descriptor.section)];
      auto *label = new QLabel(form->parentWidget());
      auto *spin_box = new QtDoubleSpinBox(form->parentWidget());
      spin_box->setObjectName(setting_object_name(descriptor.key));
      spin_box->setDecimals(3);
      spin_box->setRange(static_cast<double>(descriptor.minimum),
                         static_cast<double>(descriptor.maximum));
      spin_box->setSingleStep(0.1);
      auto &numeric = qt_install_numeric_spin_box(*spin_box);
      numeric.set_drag_sensitivity(0.1);
      numeric.set_drag_snap_increment(1.0);
      spin_box->setAccelerated(true);
      spin_box->setKeyboardTracking(true);
      label->setBuddy(spin_box);
      form->addRow(label, spin_box);
      float_controls.push_back({&descriptor, label, spin_box});

      QObject::connect(spin_box, &QDoubleSpinBox::valueChanged, &owner,
                       [this, id = descriptor.id](double value) {
                         if (refreshing || viewport_settings == nullptr) {
                           return;
                         }
                         if (controller.set_float(
                                 id, static_cast<float>(value))) {
                           mark_float_dirty(id);
                         }
                       });
    }

    category_list->setCurrentRow(0);
    refresh();
  }

  OptionsPageWidgets create_options_page(std::string_view stable_id) {
    const QString id = QString::fromUtf8(
        stable_id.data(), static_cast<qsizetype>(stable_id.size()));
    auto *page = new QScrollArea(page_stack);
    page->setObjectName(QStringLiteral("settingsPage_%1").arg(id));
    page->setFrameShape(QFrame::NoFrame);
    page->setWidgetResizable(true);

    auto *content = new QWidget(page);
    content->setObjectName(QStringLiteral("settingsPageContent_%1").arg(id));
    auto *content_layout = new QVBoxLayout(content);
    qt_settings_detail::set_scalable_margins(
        *content_layout, QMargins(0, 0, 0, 0));
    qt_settings_detail::set_scalable_spacing(*content_layout, 0);
    auto *form = new QFormLayout();
    qt_settings_detail::set_scalable_margins(
        *form, QMargins(0, 0, 0, 0));
    qt_settings_detail::set_scalable_spacing(*form, 10);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    content_layout->addLayout(form);
    content_layout->addStretch(1);
    page->setWidget(content);
    page_stack->addWidget(page);
    return {page, content, form};
  }

  OptionsPageWidgets create_category(std::string_view stable_id,
                                     std::string_view localization_key,
                                     std::string_view label) {
    auto *item = new QListWidgetItem(category_list);
    item->setData(Qt::UserRole, QString::fromUtf8(
        stable_id.data(), static_cast<qsizetype>(stable_id.size())));
    categories.push_back(
        {item, std::string(localization_key), std::string(label)});
    return create_options_page(stable_id);
  }

  QFormLayout *create_group(const OptionsPageWidgets &page,
                           std::string_view stable_id,
                           std::string_view localization_key,
                           std::string_view label) {
    auto *section = new qt_settings_detail::QtSettingsSection(
        QStringLiteral("settingsGroup_%1").arg(
            QString::fromUtf8(stable_id.data(),
                              static_cast<qsizetype>(stable_id.size()))),
        page.content);
    auto *form = new QFormLayout(section->content());
    qt_settings_detail::set_scalable_margins(
        *form, QMargins(8, 8, 8, 10));
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    page.form->addRow(section);
    groups.push_back(
        {section, page.content, form, std::string(localization_key),
         std::string(label)});
    return form;
  }

  void create_default_material_group(const OptionsPageWidgets &page) {
    QFormLayout *form = create_group(page, "default_material",
        "ui.settings.group.default_material", "Default Material");
    auto *row = new QWidget(form->parentWidget());
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    default_material_preview = new QLabel(row);
    default_material_preview->setObjectName(
        QStringLiteral("settingsDefaultMaterialPreview"));
    default_material_preview->setAlignment(Qt::AlignCenter);
    default_material_preview->setFixedSize(64, 64);
    default_material_path = new QLabel(row);
    default_material_path->setObjectName(
        QStringLiteral("settingsDefaultMaterialPath"));
    default_material_path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    default_material_path->setMinimumWidth(40);
    default_material_browse = new QPushButton(row);
    default_material_browse->setObjectName(
        QStringLiteral("settingsDefaultMaterialBrowse"));
    layout->addWidget(default_material_preview);
    layout->addWidget(default_material_path, 1);
    layout->addWidget(default_material_browse);
    form->addRow(row);
    QObject::connect(default_material_browse, &QPushButton::clicked, &owner,
                     [this]() { choose_default_material(); });
  }

  void show_default_material_error() {
    if (keybinding_message_box->set_presentation({
            .icon = QMessageBox::Critical,
            .object_name = QStringLiteral("settingsDefaultMaterialErrorDialog"),
            .title = qtext(localization, "ui.panel.settings", "Settings"),
            .text = qtext(localization,
                "ui.settings.default_material.import_failed",
                "The default material image could not be imported. Choose a valid PNG or JPEG image."),
            .buttons = {{
                .button = QtRetainedMessageBoxButton::Primary,
                .object_name = QStringLiteral("settingsDefaultMaterialErrorDialog.ok"),
                .label = qtext(localization, "ui.action.ok", "OK"),
                .role = QMessageBox::AcceptRole}},
            .default_button = QtRetainedMessageBoxButton::Primary,
            .escape_button = QtRetainedMessageBoxButton::Primary,
            .close_button = QtRetainedMessageBoxButton::Primary})) {
      keybinding_message_box->prewarm();
      keybinding_message_box->open();
    }
  }

  void choose_default_material() {
    if (!transaction_active || !has_context()) {
      return;
    }
    const std::uint64_t generation = context_generation;
    std::optional<std::filesystem::path> selected;
    if (default_texture_picker) {
      const auto picker = default_texture_picker;
      selected = picker();
    } else {
      const QString path = QFileDialog::getOpenFileName(
          &owner, qtext(localization, "ui.settings.group.default_material",
                         "Default Material"),
          QString::fromStdWString(draft_ui_settings.default_material_texture().wstring()),
          qtext(localization, "ui.settings.default_material.image_filter",
                "Images (*.png *.jpg *.jpeg)"));
      if (!path.isEmpty()) {
        selected = std::filesystem::path(path.toStdWString());
      }
    }
    if (generation != context_generation || !transaction_active ||
        !selected.has_value()) {
      return;
    }
    if (!selected->empty() &&
        !DefaultMaterialTextureStore::validate_image(*selected).ok()) {
      show_default_material_error();
      return;
    }
    draft_ui_settings.set_default_material_texture(*selected);
    default_material_dirty = *selected != target_ui_settings->default_material_texture();
    refresh_default_material();
    refresh_action_state();
  }

  void refresh_default_material(bool force_pixmap = false) {
    if (default_material_preview == nullptr) {
      return;
    }
    default_material_browse->setText(
        qtext(localization, "ui.action.browse", "Browse..."));
    const std::filesystem::path configured = ui_settings != nullptr
        ? ui_settings->default_material_texture() : std::filesystem::path{};
    const std::filesystem::path path = configured.empty()
        ? built_in_default_texture : configured;
    const bool path_changed = path != default_material_preview_path;
    if (path_changed) {
      default_material_preview_path = path;
      default_material_preview_item = {};
      QFile file(QString::fromStdWString(path.wstring()));
      if (file.open(QIODevice::ReadOnly) && file.size() > 0 &&
          file.size() <= static_cast<qint64>(kMaximumProjectTextureEncodedBytes)) {
        default_material_preview_item.encoded_albedo =
            file.read(static_cast<qint64>(kMaximumProjectTextureEncodedBytes));
        default_material_preview_item.has_valid_albedo =
            !default_material_preview_item.encoded_albedo.isEmpty();
      }
      default_material_preview_item.thumbnail =
          make_qt_material_thumbnail(default_material_preview_item);
    }
    default_material_path->setText(configured.empty()
        ? qtext(localization, "ui.settings.default_material.built_in", "Default")
        : QString::fromStdWString(configured.filename().wstring()));
    default_material_path->setToolTip(QString::fromStdWString(path.wstring()));
    if (path_changed || force_pixmap) {
      default_material_preview->setPixmap(qt_material_preview_pixmap(
          default_material_preview_item,
          default_material_preview->size() - QSize(8, 8),
          default_material_preview->devicePixelRatioF()));
    }
  }

  void create_interface_page() {
    const OptionsPageWidgets page = create_category(
        "interface", "ui.settings.interface", "Interface");
    QFormLayout *language_form = create_group(page, "language",
        "ui.settings.group.language", "Language");
    locale_label = new QLabel(language_form->parentWidget());
    locale_combo = new QComboBox(language_form->parentWidget());
    locale_combo->setObjectName(QStringLiteral("settingsLocale"));
    locale_label->setBuddy(locale_combo);
    language_form->addRow(locale_label, locale_combo);
    QObject::connect(locale_combo, &QComboBox::currentIndexChanged, &owner,
                     [this](int index) { select_locale(index); });

    QFormLayout *scale_form = create_group(page, "interface_scale",
        "ui.settings.group.interface_scale", "Interface Scaling");
    interface_scale_label = new QLabel(scale_form->parentWidget());
    interface_scale_spin_box = new QtSpinBox(scale_form->parentWidget());
    interface_scale_spin_box->setObjectName(
        QStringLiteral("settingsInterfaceScale"));
    interface_scale_spin_box->setRange(50, 300);
    interface_scale_spin_box->setSingleStep(5);
    qt_install_numeric_spin_box(*interface_scale_spin_box)
        .set_drag_snap_increment(1.0);
    interface_scale_spin_box->setSuffix(QStringLiteral("%"));
    interface_scale_spin_box->setAccelerated(true);
    interface_scale_spin_box->setKeyboardTracking(true);
    interface_scale_label->setBuddy(interface_scale_spin_box);
    scale_form->addRow(interface_scale_label, interface_scale_spin_box);
    QObject::connect(interface_scale_spin_box, &QSpinBox::valueChanged,
                     &owner, [this](int percent) {
                       if (refreshing || ui_settings == nullptr) {
                         return;
                       }
                       (void)controller.preview_interface_scale(
                           static_cast<float>(percent) / 100.0f);
                     });

  }

  void create_display_group(const OptionsPageWidgets &page) {
    QFormLayout *form = create_group(page, "display",
        "ui.settings.group.display", "Display");
    vsync_check_box = new QCheckBox(form->parentWidget());
    vsync_check_box->setObjectName(QStringLiteral("settingsVSync"));
    form->addRow(vsync_check_box);
    QObject::connect(vsync_check_box, &QCheckBox::toggled, &owner,
                     [this](bool enabled) {
                       if (!refreshing && ui_settings != nullptr) {
                         (void)controller.preview_vsync_enabled(enabled);
                       }
                     });
  }

  void create_undo_group(const OptionsPageWidgets &page) {
    QFormLayout *form = create_group(page, "undo",
        "ui.settings.group.undo", "Undo History");
    undo_steps_label = new QLabel(form->parentWidget());
    undo_steps_spin_box = new QtSpinBox(form->parentWidget());
    undo_steps_spin_box->setObjectName(QStringLiteral("settingsUndoSteps"));
    undo_steps_spin_box->setRange(SessionPreferences::kMinimumUndoSteps,
                                  SessionPreferences::kMaximumUndoSteps);
    undo_steps_spin_box->setAccelerated(true);
    qt_install_numeric_spin_box(*undo_steps_spin_box)
        .set_drag_snap_increment(1.0);
    undo_steps_label->setBuddy(undo_steps_spin_box);
    form->addRow(undo_steps_label, undo_steps_spin_box);
    undo_help_label = new QLabel(form->parentWidget());
    undo_help_label->setWordWrap(true);
    form->addRow(undo_help_label);
    QObject::connect(undo_steps_spin_box, &QSpinBox::valueChanged, &owner,
                     [this](int steps) {
                       stage_session_field(&SessionPreferences::undo_steps,
                                           steps, 0);
                     });
  }

  void create_autosave_group(const OptionsPageWidgets &page) {
    QFormLayout *form = create_group(page, "autosave",
        "ui.settings.group.autosave", "Autosave");
    autosave_check_box = new QCheckBox(form->parentWidget());
    autosave_check_box->setObjectName(QStringLiteral("settingsAutosaveEnabled"));
    form->addRow(autosave_check_box);
    autosave_interval_label = new QLabel(form->parentWidget());
    autosave_interval_spin_box = new QtSpinBox(form->parentWidget());
    autosave_interval_spin_box->setObjectName(
        QStringLiteral("settingsAutosaveInterval"));
    autosave_interval_spin_box->setRange(
        SessionPreferences::kMinimumAutoSaveIntervalMinutes,
        SessionPreferences::kMaximumAutoSaveIntervalMinutes);
    qt_install_numeric_spin_box(*autosave_interval_spin_box)
        .set_drag_snap_increment(1.0);
    autosave_interval_label->setBuddy(autosave_interval_spin_box);
    form->addRow(autosave_interval_label, autosave_interval_spin_box);
    autosave_max_files_label = new QLabel(form->parentWidget());
    autosave_max_files_spin_box = new QtSpinBox(form->parentWidget());
    autosave_max_files_spin_box->setObjectName(
        QStringLiteral("settingsAutosaveMaxFiles"));
    autosave_max_files_spin_box->setRange(
        SessionPreferences::kMinimumAutoSaveMaxFiles,
        SessionPreferences::kMaximumAutoSaveMaxFiles);
    qt_install_numeric_spin_box(*autosave_max_files_spin_box)
        .set_drag_snap_increment(1.0);
    autosave_max_files_label->setBuddy(autosave_max_files_spin_box);
    form->addRow(autosave_max_files_label, autosave_max_files_spin_box);
    autosave_help_label = new QLabel(form->parentWidget());
    autosave_help_label->setWordWrap(true);
    form->addRow(autosave_help_label);
    QObject::connect(autosave_check_box, &QCheckBox::toggled, &owner,
                     [this](bool enabled) {
                       stage_session_field(&SessionPreferences::autosave_enabled,
                                           enabled, 1);
                       refresh_session_controls();
                     });
    QObject::connect(autosave_interval_spin_box, &QSpinBox::valueChanged, &owner,
                     [this](int minutes) {
                       stage_session_field(
                           &SessionPreferences::autosave_interval_minutes,
                           minutes, 2);
                     });
    QObject::connect(autosave_max_files_spin_box, &QSpinBox::valueChanged, &owner,
                     [this](int files) {
                       stage_session_field(&SessionPreferences::autosave_max_files,
                                           files, 3);
                     });

  }

  void create_keybindings_page() {
    keybindings_category_item = new QListWidgetItem(category_list);
    keybindings_category_item->setData(Qt::UserRole,
                                       QStringLiteral("keybindings"));
    categories.push_back({keybindings_category_item, "ui.settings.keybindings",
                          "Keybindings"});

    auto *page = new QWidget(page_stack);
    page->setObjectName(QStringLiteral("settingsPage_keybindings"));
    auto *layout = new QVBoxLayout(page);

    auto *filters = new QFormLayout();
    keybindings_search_label = new QLabel(page);
    keybindings_search = new QLineEdit(page);
    keybindings_search->setObjectName(
        QStringLiteral("settingsKeybindingsSearch"));
    keybindings_search_label->setBuddy(keybindings_search);
    filters->addRow(keybindings_search_label, keybindings_search);

    keybindings_shortcut_label = new QLabel(page);
    auto *shortcut_row = new QWidget(page);
    auto *shortcut_layout = new QHBoxLayout(shortcut_row);
    shortcut_layout->setContentsMargins(0, 0, 0, 0);
    keybindings_shortcut_filter = new QtKeybindingCaptureEdit(shortcut_row);
    keybindings_shortcut_filter->setObjectName(
        QStringLiteral("settingsKeybindingsShortcutFilter"));
    keybindings_shortcut_clear = new QPushButton(shortcut_row);
    keybindings_shortcut_clear->setObjectName(
        QStringLiteral("settingsKeybindingsShortcutClear"));
    keybindings_shortcut_label->setBuddy(keybindings_shortcut_filter);
    shortcut_layout->addWidget(keybindings_shortcut_filter, 1);
    shortcut_layout->addWidget(keybindings_shortcut_clear);
    filters->addRow(keybindings_shortcut_label, shortcut_row);
    layout->addLayout(filters);

    keybindings_table = new QtKeybindingTableView(page);
    keybindings_table->setObjectName(
        QStringLiteral("settingsKeybindingsTable"));
    keybindings_table->setAlternatingRowColors(true);
    keybindings_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    keybindings_table->setSelectionMode(QAbstractItemView::SingleSelection);
    keybindings_table->setShowGrid(false);
    keybindings_table->setWordWrap(false);
    QHeaderView *vertical_header = keybindings_table->verticalHeader();
    vertical_header->hide();
    vertical_header->setSectionResizeMode(QHeaderView::Fixed);
    vertical_header->setMinimumSectionSize(18);
    vertical_header->setDefaultSectionSize(20);

    keybindings_model = new QtKeybindingTableModel(keybindings_table);
    keybindings_table->setModel(keybindings_model);

    QHeaderView *header = keybindings_table->horizontalHeader();
    header->setSectionsMovable(false);
    header->setSectionsClickable(true);
    header->setStretchLastSection(true);
    header->setMinimumSectionSize(80);
    header->setSectionResizeMode(QHeaderView::Interactive);
    keybindings_table->setSortingEnabled(true);
    keybindings_table->sortByColumn(QtKeybindingTableModel::CommandNameColumn,
                                    Qt::AscendingOrder);
    header->resizeSection(QtKeybindingTableModel::CommandNameColumn, 320);
    header->resizeSection(QtKeybindingTableModel::ContextColumn, 180);
    header->resizeSection(QtKeybindingTableModel::InputMappingColumn, 180);
    QObject::connect(
        header, &QHeaderView::sectionResized, &owner,
        [this](int logical_index, int, int new_size) {
          if (applying_ui_scale_to_keybinding_columns || logical_index < 0 ||
              logical_index >=
                  static_cast<int>(keybinding_column_design_widths.size())) {
            return;
          }
          const double factor = std::max(
              0.0001, static_cast<double>(applied_ui_scale_factor));
          keybinding_column_design_widths[static_cast<std::size_t>(
              logical_index)] = static_cast<double>(new_size) / factor;
        });

    QObject::connect(keybindings_search, &QLineEdit::textChanged, &owner,
                     [this]() { apply_keybinding_filters(); });
    keybindings_shortcut_filter->set_chord_handler(
        [this](const QuaderInputChord &chord) {
          if (commands == nullptr) {
            return;
          }
          keybinding_shortcut_text = QString::fromStdString(
              commands->format_input_mapping(chord));
          keybindings_shortcut_filter->setText(keybinding_shortcut_text);
          apply_keybinding_filters();
        });
    QObject::connect(keybindings_shortcut_clear, &QPushButton::clicked,
                     &owner, [this]() {
                       keybinding_shortcut_text.clear();
                       keybindings_shortcut_filter->clear();
                       apply_keybinding_filters();
                     });

    layout->addWidget(keybindings_table, 1);

    auto *actions = new QHBoxLayout();
    keybindings_reset_action = new QPushButton(page);
    keybindings_reset_all = new QPushButton(page);
    keybindings_reset_action->setObjectName(
        QStringLiteral("settingsKeybindingsResetAction"));
    keybindings_reset_all->setObjectName(
        QStringLiteral("settingsKeybindingsResetAll"));
    actions->addStretch(1);
    actions->addWidget(keybindings_reset_action);
    actions->addWidget(keybindings_reset_all);
    layout->addLayout(actions);

    keybindings_status = new QLabel(page);
    keybindings_status->setObjectName(
        QStringLiteral("settingsKeybindingsStatus"));
    keybindings_status->setWordWrap(true);
    layout->addWidget(keybindings_status);

    QObject::connect(keybindings_table->selectionModel(),
                     &QItemSelectionModel::selectionChanged, &owner,
                     [this]() { refresh_keybinding_editor(); });
    QObject::connect(keybindings_table, &QTableView::doubleClicked, &owner,
                     [this](const QModelIndex &index) {
                       if (index.column() ==
                           QtKeybindingTableModel::InputMappingColumn) {
                         rebind_keybinding();
                       }
                     });
    keybindings_table->set_rebind_handler(
        [this]() { rebind_keybinding(); });
    QObject::connect(keybindings_reset_action, &QPushButton::clicked, &owner,
                     [this]() { reset_selected_keybinding(); });
    QObject::connect(keybindings_reset_all, &QPushButton::clicked, &owner,
                     [this]() { reset_all_keybindings(); });
    page_stack->addWidget(page);
  }

  [[nodiscard]] const KeybindingTableRow *selected_keybinding_row() const {
    if (keybindings_model == nullptr || keybindings_table == nullptr ||
        keybindings_table->selectionModel() == nullptr) {
      return nullptr;
    }
    const QModelIndexList selected =
        keybindings_table->selectionModel()->selectedRows(
            QtKeybindingTableModel::CommandNameColumn);
    return selected.size() == 1
               ? keybindings_model->row_at(selected.front().row())
               : nullptr;
  }

  [[nodiscard]] std::optional<QuaderKeybindingActionKey>
  selected_keybinding_key() const {
    const KeybindingTableRow *row = selected_keybinding_row();
    return row == nullptr
               ? std::nullopt
               : std::optional<QuaderKeybindingActionKey>{row->key};
  }

  void select_keybinding_action(const QuaderKeybindingActionKey &key) const {
    if (keybindings_model == nullptr || keybindings_table == nullptr) {
      return;
    }
    for (int row = 0; row < keybindings_model->rowCount(); ++row) {
      const KeybindingTableRow *candidate = keybindings_model->row_at(row);
      if (candidate != nullptr && candidate->key == key) {
        keybindings_table->selectRow(row);
        return;
      }
    }
  }

  void refresh_keybinding_editor_actions() const {
    const bool has_row = selected_keybinding_row() != nullptr;
    keybindings_reset_action->setEnabled(has_row && commands != nullptr);
    keybindings_reset_all->setEnabled(commands != nullptr);
  }

  void refresh_keybinding_editor() {
    refresh_keybinding_editor_actions();
  }

  void show_keybinding_error(std::string_view message) const {
    const QString detail = QString::fromUtf8(
        message.data(), static_cast<qsizetype>(message.size()));
    const QString body =
        qtext(localization, "ui.settings.keybindings.error.message",
              "Unable to save the keybinding change.\n\n%1")
            .arg(detail);
    (void)keybinding_message_box->present(QtRetainedMessageBoxPresentation{
        .icon = QMessageBox::Critical,
        .object_name = QStringLiteral("settingsKeybindingErrorDialog"),
        .title = qtext(localization,
                       "ui.settings.keybindings.error.title",
                       "Keybinding Error"),
        .text = body,
        .buttons =
            {
                {
                    .button = QtRetainedMessageBoxButton::Primary,
                    .object_name = {},
                    .label = qtext(localization, "ui.action.ok", "OK"),
                    .role = QMessageBox::AcceptRole,
                },
            },
        .default_button = QtRetainedMessageBoxButton::Primary,
        .escape_button = QtRetainedMessageBoxButton::Primary,
        .close_button = QtRetainedMessageBoxButton::Primary,
    });
  }

  [[nodiscard]] std::optional<QuaderKeybindingConflictPolicy>
  choose_conflict_policy(
      const std::vector<QuaderKeybindingCollision> &conflicts) const {
    if (conflicts.empty()) {
      return QuaderKeybindingConflictPolicy::Reject;
    }

    QStringList action_names;
    for (const QuaderKeybindingCollision &conflict : conflicts) {
      const QString action =
          qtext(localization, conflict.localization_key, conflict.label);
      if (!action_names.contains(action)) {
        action_names.push_back(action);
      }
    }
    const QString details =
        qtext(localization, "ui.settings.keybindings.conflict.message",
              "This input mapping clashes with “%1”.")
            .arg(action_names.join(QStringLiteral("”, “")));

    const QtRetainedMessageBoxButton selected =
        keybinding_message_box->present(QtRetainedMessageBoxPresentation{
            .icon = QMessageBox::Warning,
            .object_name =
                QStringLiteral("settingsKeybindingConflictDialog"),
            .title = qtext(localization,
                           "ui.settings.keybindings.conflict.title",
                           "Keybinding Conflict"),
            .text = details,
            .buttons =
                {
                    {
                        .button = QtRetainedMessageBoxButton::Primary,
                        .object_name = QStringLiteral(
                            "settingsKeybindingConflictReplace"),
                        .label = qtext(
                            localization,
                            "ui.settings.keybindings.conflict.replace",
                            "Replace"),
                        .role = QMessageBox::AcceptRole,
                    },
                    {
                        .button = QtRetainedMessageBoxButton::Tertiary,
                        .object_name = QStringLiteral(
                            "settingsKeybindingConflictCancel"),
                        .label =
                            qtext(localization, "ui.action.cancel", "Cancel"),
                        .role = QMessageBox::RejectRole,
                    },
                },
            .default_button = QtRetainedMessageBoxButton::Tertiary,
            .escape_button = QtRetainedMessageBoxButton::Tertiary,
            .close_button = QtRetainedMessageBoxButton::Tertiary,
        });
    if (selected == QtRetainedMessageBoxButton::Primary) {
      return QuaderKeybindingConflictPolicy::Replace;
    }
    return std::nullopt;
  }

  [[nodiscard]] bool
  finish_keybinding_mutation(const QuaderKeybindingActionKey &key,
                             const QuaderApiVoidResult &result,
                             bool compatible_reuse) {
    if (!result.ok()) {
      show_keybinding_error(result.error_message());
      return false;
    }
    if (result.changed() && settings_changed) {
      settings_changed({.kind = QtSettingsChangeKind::KeybindingsCommit});
    }
    refresh_keybindings();
    select_keybinding_action(key);
    keybindings_status->setText(
        compatible_reuse
            ? qtext(localization,
                    "ui.settings.keybindings.compatible_reuse",
                    "Saved. The mapping is also used in a non-overlapping "
                    "context.")
            : qtext(localization,
                    result.changed()
                        ? "ui.settings.keybindings.status.saved"
                        : "ui.settings.keybindings.status.unchanged",
                    result.changed() ? "Keybindings saved."
                                     : "Keybindings unchanged."));
    return true;
  }

  enum class CapturedCommitOutcome { Completed, Cancelled, Failed };

  [[nodiscard]] CapturedCommitOutcome
  commit_captured_keybinding(const KeybindingTableRow &row,
                             std::string shortcut) {
    if (commands == nullptr) {
      return CapturedCommitOutcome::Failed;
    }
    const QuaderKeybindingAssignmentAnalysis analysis =
        commands->analyze_shortcut_assignment(row.key, shortcut);
    if (!analysis.ok) {
      show_keybinding_error(analysis.message);
      return CapturedCommitOutcome::Failed;
    }
    const std::optional<QuaderKeybindingConflictPolicy> policy =
        choose_conflict_policy(analysis.conflicts);
    if (!policy) {
      return CapturedCommitOutcome::Cancelled;
    }
    return finish_keybinding_mutation(
               row.key,
               commands->set_action_shortcut(row.key, std::move(shortcut),
                                             *policy),
               !analysis.compatible_reuses.empty())
               ? CapturedCommitOutcome::Completed
               : CapturedCommitOutcome::Failed;
  }

  void rebind_keybinding() {
    const KeybindingTableRow *row = selected_keybinding_row();
    if (row == nullptr || commands == nullptr) {
      return;
    }
    const KeybindingTableRow selected = *row;
    QtKeybindingCaptureDialog dialog(*commands, localization,
                                     !selected.shortcut.isEmpty(), &owner);
    while (dialog.exec() == QDialog::Accepted) {
      if (dialog.unassign_requested()) {
        if (finish_keybinding_mutation(
                selected.key,
                commands->remove_action_shortcut(selected.key), false)) {
          return;
        }
        continue;
      }
      const CapturedCommitOutcome outcome =
          commit_captured_keybinding(selected, dialog.shortcut());
      if (outcome == CapturedCommitOutcome::Completed) {
        return;
      }
    }
  }

  void reset_selected_keybinding() {
    const KeybindingTableRow *row = selected_keybinding_row();
    if (row == nullptr || commands == nullptr) {
      return;
    }
    const KeybindingTableRow selected = *row;
    std::vector<QuaderKeybindingCollision> conflicts;
    if (!selected.default_mapping.isEmpty()) {
      const QuaderKeybindingAssignmentAnalysis analysis =
          commands->analyze_shortcut_assignment(selected.key,
                                                selected.default_mapping.toStdString());
      if (!analysis.ok) {
        show_keybinding_error(analysis.message);
        return;
      }
      conflicts = analysis.conflicts;
    }
    const std::optional<QuaderKeybindingConflictPolicy> policy =
        choose_conflict_policy(conflicts);
    if (!policy) {
      return;
    }
    (void)finish_keybinding_mutation(
        selected.key, commands->reset_action_shortcut(selected.key, *policy),
        false);
  }

  void reset_all_keybindings() {
    if (commands == nullptr) {
      return;
    }
    const QtRetainedMessageBoxButton decision =
        keybinding_message_box->present(QtRetainedMessageBoxPresentation{
            .icon = QMessageBox::Question,
            .object_name =
                QStringLiteral("settingsKeybindingResetAllDialog"),
            .title = qtext(localization,
                           "ui.settings.keybindings.reset_all.title",
                           "Reset All Keybindings"),
            .text = qtext(
                localization, "ui.settings.keybindings.reset_all.confirm",
                "Reset every keybinding to its default mapping?"),
            .buttons =
                {
                    {
                        .button = QtRetainedMessageBoxButton::Primary,
                        .object_name = QStringLiteral(
                            "settingsKeybindingResetAllConfirm"),
                        .label = qtext(
                            localization,
                            "ui.settings.keybindings.action.reset_all",
                            "Reset All"),
                        .role = QMessageBox::AcceptRole,
                    },
                    {
                        .button = QtRetainedMessageBoxButton::Tertiary,
                        .object_name = QStringLiteral(
                            "settingsKeybindingResetAllCancel"),
                        .label =
                            qtext(localization, "ui.action.cancel", "Cancel"),
                        .role = QMessageBox::RejectRole,
                    },
                },
            .default_button = QtRetainedMessageBoxButton::Tertiary,
            .escape_button = QtRetainedMessageBoxButton::Tertiary,
            .close_button = QtRetainedMessageBoxButton::Tertiary,
        });
    if (decision != QtRetainedMessageBoxButton::Primary) {
      return;
    }
    const std::optional<QuaderKeybindingActionKey> selected =
        selected_keybinding_key();
    const QuaderApiVoidResult result = commands->reset_all_shortcuts();
    if (!result.ok()) {
      show_keybinding_error(result.error_message());
      return;
    }
    if (result.changed() && settings_changed) {
      settings_changed({.kind = QtSettingsChangeKind::KeybindingsCommit});
    }
    refresh_keybindings();
    if (selected) {
      select_keybinding_action(*selected);
    }
    keybindings_status->setText(qtext(
        localization,
        result.changed() ? "ui.settings.keybindings.status.reset_all"
                         : "ui.settings.keybindings.status.unchanged",
        result.changed() ? "All keybindings reset."
                         : "Keybindings unchanged."));
  }

  QFormLayout *create_viewport_section(
      const OptionsPageWidgets &page,
      const ViewportSettingsSectionDescriptor &descriptor) {
    switch (descriptor.id) {
    case ViewportSettingsSectionId::Navigation:
      return create_group(page, "navigation",
          "ui.settings.group.fly_navigation", "Fly Navigation");
    case ViewportSettingsSectionId::Selection:
      return create_group(page, "selection",
          "ui.settings.group.selection_appearance", "Selection Appearance");
    case ViewportSettingsSectionId::Gizmo:
      return create_group(page, "gizmo",
          "ui.settings.group.gizmo", "Transform Gizmos");
    case ViewportSettingsSectionId::Overlay:
      return create_group(page, "overlay",
          "ui.settings.group.overlay", "Mesh Overlays");
    default:
      break;
    }
    return create_group(page, section_stable_id(descriptor.id),
                        section_localization_key(descriptor.id),
                        descriptor.label);
  }

  void set_context(UISettings &new_ui_settings,
                   ViewportSettings &new_viewport_settings,
                   LocalizationService &new_localization,
                   QuaderCommandsApi &new_commands,
                   QtSettingsController::ChangeCallback new_settings_changed,
                   std::function<bool()> new_persist_interface_settings,
                   std::filesystem::path new_settings_directory,
                   std::filesystem::path new_built_in_default_texture,
                   std::function<std::optional<std::filesystem::path>()> new_picker) {
    ++context_generation;
    const bool restart_visible_transaction = owner.isVisible();
    const bool preserve_transaction =
        transaction_active && target_ui_settings == &new_ui_settings &&
        target_viewport_settings == &new_viewport_settings &&
        localization == &new_localization;
    if (transaction_active && !preserve_transaction) {
      discard_changes();
    }
    target_ui_settings = &new_ui_settings;
    target_viewport_settings = &new_viewport_settings;
    ui_settings = &draft_ui_settings;
    viewport_settings = &draft_viewport_settings;
    localization = &new_localization;
    commands = &new_commands;
    settings_changed = std::move(new_settings_changed);
    persist_interface_settings =
        std::move(new_persist_interface_settings);
    const std::filesystem::path executable_directory(
        QCoreApplication::applicationDirPath().toStdWString());
    settings_directory = new_settings_directory.empty()
        ? UISettings::resolve_paths(executable_directory).directory
        : std::move(new_settings_directory);
    built_in_default_texture = new_built_in_default_texture.empty()
        ? executable_directory / "materials" / "default" / "default_albedo.png"
        : std::move(new_built_in_default_texture);
    default_texture_picker = std::move(new_picker);
    controller.bind(
        draft_ui_settings, draft_viewport_settings, new_localization,
        [this](const QtSettingsChange &change) {
          if (change.kind == QtSettingsChangeKind::InterfacePreview) {
            mark_interface_scale_dirty();
            if (settings_changed) {
              settings_changed(change);
            }
          } else if (change.kind == QtSettingsChangeKind::RenderingPreview) {
            vsync_dirty = draft_ui_settings.vsync_enabled() !=
                          target_ui_settings->vsync_enabled();
            refresh_action_state();
            if (settings_changed) {
              settings_changed(change);
            }
          }
        });
    if (preserve_transaction) {
      refresh();
      return;
    }
    transaction_active = false;
    clear_dirty();
    synchronize_draft_from_targets(true);
    refresh();
    if (restart_visible_transaction) {
      begin_transaction();
    }
  }

  void refresh() {
    QScopedValueRollback<bool> refresh_guard(refreshing, true);

    synchronize_draft_from_targets(!transaction_active);

    owner.setWindowTitle(qtext(localization, "ui.panel.settings", "Settings"));
    for (const CategoryWidgets &category : categories) {
      category.item->setText(qtext(localization, category.localization_key,
                                  category.label));
    }
    for (const GroupWidgets &group : groups) {
      group.section->set_title(qtext(localization, group.localization_key,
                                    group.label));
    }
    const QString keybindings_text =
        qtext(localization, "ui.settings.keybindings", "Keybindings");
    keybindings_category_item->setText(keybindings_text);
    keybindings_table->setAccessibleName(keybindings_text);
    keybindings_search_label->setText(qtext(
        localization, "ui.settings.keybindings.search.label", "Search"));
    keybindings_search->setPlaceholderText(qtext(
        localization, "ui.settings.keybindings.search.placeholder",
        "Search commands"));
    keybindings_shortcut_label->setText(qtext(
        localization, "ui.settings.keybindings.shortcut_filter.label",
        "Input Mapping"));
    keybindings_shortcut_filter->setPlaceholderText(qtext(
        localization, "ui.settings.keybindings.shortcut_filter.placeholder",
        "Press a shortcut"));
    keybindings_shortcut_clear->setText(qtext(
        localization, "ui.settings.keybindings.shortcut_filter.clear",
        "Clear"));
    keybindings_reset_action->setText(qtext(
        localization, "ui.settings.keybindings.action.reset", "Reset Action"));
    keybindings_reset_all->setText(qtext(
        localization, "ui.settings.keybindings.action.reset_all",
        "Reset All"));
    locale_label->setText(
        qtext(localization, "ui.settings.locale", "Language"));
    interface_scale_label->setText(qtext(
        localization, "ui.settings.interface_scale", "Interface Scale"));
    const QString interface_scale_help = qtext(
        localization, "ui.settings.interface_scale.help",
        "Scales editor text, controls, icons, and interaction targets.");
    interface_scale_label->setToolTip(interface_scale_help);
    interface_scale_spin_box->setToolTip(interface_scale_help);
    vsync_check_box->setText(qtext(
        localization, "ui.settings.vsync", "VSync (synchronize with display)"));
    vsync_check_box->setToolTip(qtext(
        localization, "ui.settings.vsync.help",
        "Synchronizes presentation with the display refresh rate. Disable to "
        "allow faster rendering; tearing may occur. Drivers without support "
        "for immediate presentation keep synchronization enabled."));
    undo_steps_label->setText(qtext(
        localization, "ui.settings.undo_steps", "Undo Steps"));
    undo_steps_spin_box->setSpecialValueText(qtext(
        localization, "ui.settings.undo_disabled", "Disabled"));
    undo_help_label->setText(qtext(
        localization, "ui.settings.undo_steps.help",
        "Keep up to this many undo steps. Applying a lower limit removes "
        "older history. Set to 0 to disable undo history."));
    autosave_check_box->setText(qtext(
        localization, "ui.settings.autosave_enabled", "Enable Autosave"));
    autosave_interval_label->setText(qtext(
        localization, "ui.settings.autosave_interval", "Save Every (minutes)"));
    autosave_max_files_label->setText(qtext(
        localization, "ui.settings.autosave_max_files",
        "Recovery Copies per Scene"));
    autosave_help_label->setText(qtext(
        localization, "ui.settings.autosave.help",
        "Saves recovery copies of changed scenes without overwriting the "
        "scene files. Choose File > Recover > Auto Save to recover a copy, then "
        "use Save As to keep it."));
    for (const auto &[label, control, help] :
         {std::tuple<QLabel *, QSpinBox *, QLabel *>{
              undo_steps_label, undo_steps_spin_box, undo_help_label},
          {autosave_interval_label, autosave_interval_spin_box,
           autosave_help_label},
          {autosave_max_files_label, autosave_max_files_spin_box,
           autosave_help_label}}) {
      control->setAccessibleName(label->text());
      control->setToolTip(help->text());
      control->setAccessibleDescription(help->text());
    }
    autosave_check_box->setToolTip(autosave_help_label->text());
    autosave_check_box->setAccessibleDescription(autosave_help_label->text());
    apply_button->setText(qtext(localization, "ui.action.apply", "Apply"));
    ok_button->setText(qtext(localization, "ui.action.ok", "OK"));
    cancel_button->setText(
        qtext(localization, "ui.action.cancel", "Cancel"));

    refresh_interface_scale();
    refresh_vsync_enabled();
    refresh_session_controls();
    refresh_default_material();
    refresh_locale_combo();
    refresh_keybindings();
    refresh_keybinding_editor();

    for (const ColorWidgets &control : color_controls) {
      const std::string key = setting_localization_key(control.descriptor->key);
      const QString label = qtext(localization, key, control.descriptor->label);
      control.label->setText(label);
      control.label->setToolTip(setting_help_text(
          localization, control.descriptor->key, control.descriptor->help));
      control.button->setAccessibleName(label);
      control.button->setEnabled(viewport_settings != nullptr);
      if (viewport_settings != nullptr) {
        set_color_button_presentation(
            *control.button,
            viewport_settings->color(control.descriptor->id), localization,
            *control.descriptor);
      } else {
        set_color_button_presentation(*control.button,
                                      control.descriptor->default_value,
                                      localization, *control.descriptor);
      }
    }

    for (const FloatWidgets &control : float_controls) {
      const std::string key = setting_localization_key(control.descriptor->key);
      const QString label = qtext(localization, key, control.descriptor->label);
      const QString help = setting_help_text(
          localization, control.descriptor->key, control.descriptor->help);
      control.label->setText(label);
      control.label->setToolTip(help);
      control.spin_box->setAccessibleName(label);
      control.spin_box->setAccessibleDescription(help);
      control.spin_box->setToolTip(help);
      control.spin_box->setEnabled(viewport_settings != nullptr);
      const QSignalBlocker blocker(control.spin_box);
      control.spin_box->setValue(
          viewport_settings != nullptr
              ? static_cast<double>(
                    viewport_settings->float_value(control.descriptor->id))
              : static_cast<double>(control.descriptor->default_value));
    }

    for (const BoolWidgets &control : bool_controls) {
      const std::string key = setting_localization_key(control.descriptor->key);
      control.check_box->setText(
          qtext(localization, key, control.descriptor->label));
      control.check_box->setEnabled(viewport_settings != nullptr);
      const QSignalBlocker blocker(control.check_box);
      control.check_box->setChecked(
          viewport_settings != nullptr
              ? viewport_settings->bool_value(control.descriptor->id)
              : control.descriptor->default_value);
    }

    ok_button->setEnabled(has_context());
    cancel_button->setEnabled(has_context());
    refresh_action_state();
    update_form_metrics();
  }

  /// Measures retained label columns only when localization or scale changes.
  void update_form_metrics() {
    const std::uint64_t locale_revision =
        localization != nullptr ? localization->revision() : 0u;
    const QFont font = owner.font();
    if (measured_localization == localization &&
        measured_localization_revision == locale_revision &&
        measured_font == font && measured_scale == layout_metrics.factor()) {
      return;
    }
    measured_localization = localization;
    measured_localization_revision = locale_revision;
    measured_font = font;
    measured_scale = layout_metrics.factor();
    static_cast<QtSettingsCategoryList *>(category_list)->apply_ui_scale(
        layout_metrics);
    for (const GroupWidgets &group : groups) {
      int label_width = layout_metrics.px(140);
      for (const GroupWidgets &peer : groups) {
        if (peer.page != group.page) {
          continue;
        }
        for (int row = 0; row < peer.form->rowCount(); ++row) {
          QLayoutItem *item = peer.form->itemAt(row, QFormLayout::LabelRole);
          auto *label = item != nullptr
              ? qobject_cast<QLabel *>(item->widget()) : nullptr;
          if (label != nullptr) {
            label_width = std::max(label_width,
                label->fontMetrics().horizontalAdvance(label->text()));
          }
        }
      }
      label_width = std::min(label_width, layout_metrics.px(200));
      group.form->setHorizontalSpacing(layout_metrics.px(10));
      group.form->setVerticalSpacing(layout_metrics.px(6));
      for (int row = 0; row < group.form->rowCount(); ++row) {
        QLayoutItem *item = group.form->itemAt(row, QFormLayout::LabelRole);
        auto *label = item != nullptr
            ? qobject_cast<QLabel *>(item->widget()) : nullptr;
        if (label != nullptr) {
          label->setWordWrap(true);
          label->setFixedWidth(label_width);
        }
      }
    }
  }

  void refresh_keybindings() {
    const std::optional<QuaderKeybindingActionKey> selected =
        selected_keybinding_key();
    keybindings_model->set_headers({
        qtext(localization,
              "ui.settings.keybindings.column.command_name", "Command Name"),
        qtext(localization, "ui.settings.keybindings.column.context",
              "Context"),
        qtext(localization,
              "ui.settings.keybindings.column.input_mapping",
              "Input Mapping"),
    });

    std::vector<KeybindingTableRow> rows;
    if (commands != nullptr) {
      const std::vector<QuaderKeybindingCatalogEntry> catalog =
          commands->keybinding_catalog();
      rows.reserve(catalog.size());
      for (std::size_t index = 0u; index < catalog.size(); ++index) {
        const QuaderKeybindingCatalogEntry &entry = catalog[index];
        if (!entry.editable ||
            !release_allows_input_target(entry.action_id) ||
            !release_presents_command(entry.action_id)) {
          continue;
        }
        const QString shortcut = QString::fromStdString(entry.shortcut);
        rows.push_back({
            .key = entry.key,
            .target_id = QString::fromStdString(entry.action_id),
            .command_name = qtext(localization, entry.localization_key,
                                  entry.label),
            .context = qtext(localization, entry.scope_localization_key,
                             entry.scope_label),
            .input_mapping =
                shortcut.isEmpty()
                    ? qtext(localization,
                            "ui.settings.keybindings.unassigned",
                            "Unassigned")
                    : shortcut,
            .shortcut = shortcut,
            .default_mapping = QString::fromStdString(entry.default_shortcut),
            .source = entry.source,
            .when = QString::fromStdString(entry.when),
            .customized = entry.customized,
            .mapping_order = index,
        });
      }
    }
    keybinding_rows = std::move(rows);
    apply_keybinding_filters();
    if (selected) {
      select_keybinding_action(*selected);
    } else if (keybindings_model->rowCount() > 0) {
      keybindings_table->selectRow(0);
    }
    refresh_keybinding_editor();
  }

  void apply_keybinding_filters() {
    const std::optional<QuaderKeybindingActionKey> selected =
        selected_keybinding_key();
    const QString command_filter = keybindings_search->text().trimmed();
    std::vector<KeybindingTableRow> filtered;
    filtered.reserve(keybinding_rows.size());
    for (const KeybindingTableRow &row : keybinding_rows) {
      if (!command_filter.isEmpty() &&
          !row.command_name.contains(command_filter, Qt::CaseInsensitive)) {
        continue;
      }
      if (!keybinding_shortcut_text.isEmpty() &&
          QString::compare(row.shortcut, keybinding_shortcut_text,
                           Qt::CaseInsensitive) != 0) {
        continue;
      }
      filtered.push_back(row);
    }
    keybindings_model->set_rows(std::move(filtered));
    if (selected) {
      select_keybinding_action(*selected);
    }
    refresh_keybinding_editor();
  }

  void refresh_locale_combo() {
    const QSignalBlocker blocker(locale_combo);
    locale_combo->clear();

    if (localization == nullptr) {
      locale_combo->setEnabled(false);
      return;
    }

    const std::string selected_locale = ui_settings != nullptr
                                            ? ui_settings->locale()
                                            : localization->current_locale();
    int selected_index = -1;
    for (const LocaleInfo &locale : localization->available_locales()) {
      std::string locale_key = "locale.";
      locale_key.append(locale.id);
      locale_key.append(".name");
      const QString label =
          qtext(localization, locale_key, locale.display_name);
      locale_combo->addItem(label, QString::fromStdString(locale.id));
      if (locale.id == selected_locale) {
        selected_index = locale_combo->count() - 1;
      }
    }

    if (selected_index < 0 && !selected_locale.empty()) {
      locale_combo->addItem(
          QString::fromStdString(
              localization->locale_display_name(selected_locale)),
          QString::fromStdString(selected_locale));
      selected_index = locale_combo->count() - 1;
    }
    locale_combo->setCurrentIndex(selected_index);
    locale_combo->setEnabled(ui_settings != nullptr &&
                             locale_combo->count() > 0);
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    layout_metrics = metrics;
    qt_settings_detail::apply_scalable_layout_metrics(owner, metrics);
    qt_settings_detail::apply_scalable_widget_metrics(owner, metrics);
    update_form_metrics();
    if (!layout_measurement_queued) {
      layout_measurement_queued = true;
      QTimer::singleShot(0, &owner, [this]() {
        layout_measurement_queued = false;
        // Application font propagation can finish after the scale callback.
        measured_scale = 0.0f;
        update_form_metrics();
      });
    }
    default_material_preview->setFixedSize(metrics.px(64), metrics.px(64));
    refresh_default_material(true);
    category_list->setMinimumWidth(metrics.px(160));
    category_list->setMaximumWidth(metrics.px(220));

    QHeaderView *vertical_header = keybindings_table->verticalHeader();
    vertical_header->setMinimumSectionSize(metrics.px(18));
    vertical_header->setDefaultSectionSize(metrics.px(20));

    QHeaderView *header = keybindings_table->horizontalHeader();
    const float next_factor = metrics.factor();
    if (std::fabs(next_factor - applied_ui_scale_factor) > 0.0001f) {
      applying_ui_scale_to_keybinding_columns = true;
      for (const int column : {
               QtKeybindingTableModel::CommandNameColumn,
               QtKeybindingTableModel::ContextColumn,
               QtKeybindingTableModel::InputMappingColumn,
           }) {
        header->resizeSection(
            column,
            std::max(1, static_cast<int>(std::lround(
                            keybinding_column_design_widths[
                                static_cast<std::size_t>(column)] *
                            static_cast<double>(next_factor)))));
      }
      applying_ui_scale_to_keybinding_columns = false;
      applied_ui_scale_factor = next_factor;
    }
    header->setMinimumSectionSize(metrics.px(80));
  }

  void refresh_interface_scale() {
    const QSignalBlocker blocker(interface_scale_spin_box);
    const float scale = ui_settings != nullptr
                            ? ui_settings->interface_scale()
                            : 1.0f;
    interface_scale_spin_box->setValue(
        static_cast<int>(std::lround(scale * 100.0f)));
    interface_scale_spin_box->setEnabled(ui_settings != nullptr);
  }

  void synchronize_interface_scale() {
    if (!has_context()) {
      return;
    }
    const float target_scale = target_ui_settings->interface_scale();
    draft_ui_settings.set_interface_scale(target_scale);
    interface_scale_target_baseline = target_scale;
    interface_scale_dirty = false;
    refresh_interface_scale();
    refresh_action_state();
  }

  void refresh_vsync_enabled() {
    const QSignalBlocker blocker(vsync_check_box);
    vsync_check_box->setChecked(ui_settings == nullptr ||
                               ui_settings->vsync_enabled());
    vsync_check_box->setEnabled(ui_settings != nullptr);
  }

  void refresh_session_controls() {
    const SessionPreferences values = ui_settings != nullptr
        ? ui_settings->session_settings() : SessionPreferences{};
    const QSignalBlocker undo_blocker(undo_steps_spin_box);
    const QSignalBlocker autosave_blocker(autosave_check_box);
    const QSignalBlocker interval_blocker(autosave_interval_spin_box);
    const QSignalBlocker files_blocker(autosave_max_files_spin_box);
    undo_steps_spin_box->setValue(values.undo_steps);
    autosave_check_box->setChecked(values.autosave_enabled);
    autosave_interval_spin_box->setValue(values.autosave_interval_minutes);
    autosave_max_files_spin_box->setValue(values.autosave_max_files);
    undo_steps_spin_box->setEnabled(ui_settings != nullptr);
    autosave_check_box->setEnabled(ui_settings != nullptr);
    const bool autosave_active = ui_settings != nullptr && values.autosave_enabled;
    autosave_interval_label->setEnabled(autosave_active);
    autosave_interval_spin_box->setEnabled(autosave_active);
    autosave_max_files_label->setEnabled(autosave_active);
    autosave_max_files_spin_box->setEnabled(autosave_active);
  }

  void synchronize_session_settings() {
    if (!has_context()) {
      return;
    }
    synchronize_session_draft(false);
    refresh_session_controls();
    refresh_action_state();
  }

  void synchronize_default_material_texture() {
    if (!has_context()) {
      return;
    }
    synchronize_default_material_draft(true);
    refresh_default_material();
    refresh_action_state();
  }

  void synchronize_vsync_enabled() {
    if (!has_context()) {
      return;
    }
    const bool target_vsync = target_ui_settings->vsync_enabled();
    draft_ui_settings.set_vsync_enabled(target_vsync);
    vsync_target_baseline = target_vsync;
    vsync_dirty = false;
    refresh_vsync_enabled();
    refresh_action_state();
  }

  void select_locale(int index) {
    if (refreshing || ui_settings == nullptr || target_ui_settings == nullptr ||
        localization == nullptr || index < 0) {
      return;
    }

    const std::string locale_id =
        locale_combo->itemData(index).toString().toStdString();
    if (controller.select_locale(locale_id)) {
      locale_dirty = ui_settings->locale() != target_ui_settings->locale();
      refresh_action_state();
    }
  }

  void choose_color(const ViewportColorSettingDescriptor &descriptor,
                    QtColorSwatchButton &button) {
    if (viewport_settings == nullptr) {
      return;
    }

    const ViewportSettingColor initial =
        viewport_settings->color(descriptor.id);
    const std::string key = setting_localization_key(descriptor.key);
    QColorDialog dialog(to_q_color(initial), &owner);
    dialog.setObjectName(
        setting_object_name(descriptor.key) + QStringLiteral("_dialog"));
    dialog.setWindowTitle(qtext(localization, key, descriptor.label));
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);
    dialog.setOption(QColorDialog::ShowAlphaChannel,
                     descriptor.id != ViewportColorSettingId::Background);

    QPointer<QtColorSwatchButton> guarded_button(&button);
    QObject::connect(
        &dialog, &QColorDialog::currentColorChanged, &dialog,
        [this, descriptor_ptr = &descriptor,
         guarded_button](const QColor &color) {
          const ViewportColorSettingId id = descriptor_ptr->id;
          if (!color.isValid()) {
            return;
          }
          if (controller.preview_color(id, from_q_color(color))) {
            mark_color_dirty(id);
          }
          if (guarded_button != nullptr && viewport_settings != nullptr) {
            set_color_button_presentation(
                *guarded_button, viewport_settings->color(id), localization,
                *descriptor_ptr);
          }
        });

    const int result = dialog.exec();
    if (result == QDialog::Accepted) {
      if (dialog.currentColor().isValid()) {
        if (controller.preview_color(
                descriptor.id, from_q_color(dialog.currentColor()))) {
          mark_color_dirty(descriptor.id);
        }
      }
      (void)controller.commit_color(descriptor.id, initial);
      mark_color_dirty(descriptor.id);
    } else {
      (void)controller.rollback_color(descriptor.id, initial);
      mark_color_dirty(descriptor.id);
    }
    if (guarded_button != nullptr) {
      set_color_button_presentation(
          *guarded_button, viewport_settings->color(descriptor.id),
          localization, descriptor);
    }
  }
};

QtSettingsDialog::QtSettingsDialog(QWidget *parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this)) {}

QtSettingsDialog::~QtSettingsDialog() = default;

void QtSettingsDialog::set_context(UISettings &ui_settings,
                                   ViewportSettings &viewport_settings,
                                   LocalizationService &localization,
                                   QuaderCommandsApi &commands,
                                   QtSettingsController::ChangeCallback
                                       settings_changed,
                                   std::function<bool()>
                                       persist_interface_settings,
                                   std::filesystem::path settings_directory,
                                   std::filesystem::path built_in_default_texture,
                                   std::function<std::optional<std::filesystem::path>()>
                                       default_texture_picker) {
  impl_->set_context(ui_settings, viewport_settings, localization, commands,
                     std::move(settings_changed),
                     std::move(persist_interface_settings),
                     std::move(settings_directory),
                     std::move(built_in_default_texture),
                     std::move(default_texture_picker));
}

void QtSettingsDialog::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  impl_->apply_ui_scale(metrics);
}

void QtSettingsDialog::synchronize_interface_scale() {
  impl_->synchronize_interface_scale();
}

void QtSettingsDialog::synchronize_vsync_enabled() {
  impl_->synchronize_vsync_enabled();
}

void QtSettingsDialog::synchronize_session_settings() {
  impl_->synchronize_session_settings();
}

void QtSettingsDialog::synchronize_default_material_texture() {
  impl_->synchronize_default_material_texture();
}

void QtSettingsDialog::refresh() { impl_->refresh(); }

void QtSettingsDialog::accept() {
  if (impl_->apply_changes()) {
    impl_->transaction_active = false;
    QDialog::accept();
  }
}

void QtSettingsDialog::reject() {
  impl_->discard_changes();
  QDialog::reject();
}

void QtSettingsDialog::showEvent(QShowEvent *event) {
  impl_->begin_transaction();
  QDialog::showEvent(event);
  impl_->schedule_keybinding_message_box_prewarm();
}

} // namespace quader
