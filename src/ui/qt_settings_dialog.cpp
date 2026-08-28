////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_settings_dialog.hpp"

#include "api/quader_commands_api.hpp"
#include "core/localization_service.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_retained_message_box.hpp"
#include "ui/qt_settings_controller.hpp"
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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

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
  const QString opacity = color_opacity_text(localization, swatch_color);
  QString details =
      QStringLiteral("%1 | %2")
          .arg(swatch_color.name(QColor::HexArgb).toUpper(), opacity);
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
  /// Holds one viewport category item and its right-hand options form.
  struct SectionWidgets {
    const ViewportSettingsSectionDescriptor *descriptor = nullptr;
    QListWidgetItem *category_item = nullptr;
    QFormLayout *form = nullptr;
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

  QtSettingsDialog &owner;
  UISettings *target_ui_settings = nullptr;
  ViewportSettings *target_viewport_settings = nullptr;
  UISettings draft_ui_settings = UISettings::defaults();
  ViewportSettings draft_viewport_settings = ViewportSettings::defaults();
  UISettings *ui_settings = nullptr;
  ViewportSettings *viewport_settings = nullptr;
  LocalizationService *localization = nullptr;
  QuaderCommandsApi *commands = nullptr;
  QtSettingsController controller;
  std::function<void(QtSettingsChangeKind)> settings_changed;
  std::array<bool, kViewportColorSettingCount> dirty_colors{};
  std::array<bool, kViewportFloatSettingCount> dirty_floats{};
  std::array<bool, kViewportBoolSettingCount> dirty_bools{};
  bool locale_dirty = false;
  bool transaction_active = false;
  bool refreshing = false;
  bool keybinding_message_box_prewarm_scheduled = false;

  QtRetainedMessageBox *keybinding_message_box = nullptr;
  QListWidget *category_list = nullptr;
  QStackedWidget *page_stack = nullptr;
  QListWidgetItem *interface_category_item = nullptr;
  QListWidgetItem *keybindings_category_item = nullptr;
  QLabel *locale_label = nullptr;
  QComboBox *locale_combo = nullptr;
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
  std::vector<SectionWidgets> sections;
  std::vector<ColorWidgets> color_controls;
  std::vector<FloatWidgets> float_controls;
  std::vector<BoolWidgets> bool_controls;
  std::vector<KeybindingTableRow> keybinding_rows;
  QString keybinding_shortcut_text;

  [[nodiscard]] bool has_context() const {
    return target_ui_settings != nullptr &&
           target_viewport_settings != nullptr && localization != nullptr &&
           commands != nullptr;
  }

  void clear_dirty() {
    dirty_colors.fill(false);
    dirty_floats.fill(false);
    dirty_bools.fill(false);
    locale_dirty = false;
  }

  [[nodiscard]] bool has_pending_changes() const {
    return locale_dirty ||
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

    const bool interface_changed =
        locale_dirty &&
        target_ui_settings->locale() != localization->current_locale();
    if (locale_dirty) {
      target_ui_settings->set_locale(localization->current_locale());
    }

    clear_dirty();
    synchronize_draft_from_targets(true);
    if (viewport_changed && settings_changed) {
      settings_changed(QtSettingsChangeKind::ViewportCommit);
    }
    if (interface_changed && settings_changed) {
      settings_changed(QtSettingsChangeKind::InterfaceCommit);
    }
    refresh();
    return true;
  }

  void discard_changes() {
    if (!transaction_active) {
      return;
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
    auto *content_layout = new QHBoxLayout();
    category_list = new QListWidget(&owner);
    category_list->setObjectName(QStringLiteral("settingsCategories"));
    category_list->setMinimumWidth(160);
    category_list->setMaximumWidth(220);
    content_layout->addWidget(category_list);

    auto *options_container = new QWidget(&owner);
    options_container->setObjectName(QStringLiteral("settingsOptions"));
    auto *options_layout = new QVBoxLayout(options_container);
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

    create_interface_page();
    create_keybindings_page();

    std::array<QFormLayout *, kViewportSettingsSectionCount> section_forms{};
    sections.reserve(kViewportSettingsSectionCount);
    for (const ViewportSettingsSectionDescriptor &descriptor :
         viewport_settings_sections()) {
      sections.push_back(create_viewport_section(descriptor));
      section_forms[static_cast<std::size_t>(descriptor.id)] =
          sections.back().form;
    }

    color_controls.reserve(kViewportColorSettingCount);
    for (const ViewportColorSettingDescriptor &descriptor :
         viewport_color_settings()) {
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
      QFormLayout *form =
          section_forms[static_cast<std::size_t>(descriptor.section)];
      auto *label = new QLabel(form->parentWidget());
      auto *spin_box = new QDoubleSpinBox(form->parentWidget());
      spin_box->setObjectName(setting_object_name(descriptor.key));
      spin_box->setDecimals(3);
      spin_box->setRange(static_cast<double>(descriptor.minimum),
                         static_cast<double>(descriptor.maximum));
      spin_box->setSingleStep(0.001);
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

    bool_controls.reserve(kViewportBoolSettingCount);
    for (const ViewportBoolSettingDescriptor &descriptor :
         viewport_bool_settings()) {
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

    category_list->setCurrentRow(0);
    refresh();
  }

  OptionsPageWidgets create_options_page(std::string_view stable_id) {
    const QString id = QString::fromUtf8(
        stable_id.data(), static_cast<qsizetype>(stable_id.size()));
    auto *page = new QScrollArea(page_stack);
    page->setObjectName(QStringLiteral("settingsPage_%1").arg(id));
    page->setWidgetResizable(true);

    auto *content = new QWidget(page);
    content->setObjectName(QStringLiteral("settingsPageContent_%1").arg(id));
    auto *content_layout = new QVBoxLayout(content);
    auto *form = new QFormLayout();
    content_layout->addLayout(form);
    content_layout->addStretch(1);
    page->setWidget(content);
    page_stack->addWidget(page);
    return {page, content, form};
  }

  void create_interface_page() {
    interface_category_item = new QListWidgetItem(category_list);
    interface_category_item->setData(Qt::UserRole,
                                     QStringLiteral("interface"));
    const OptionsPageWidgets page = create_options_page("interface");
    locale_label = new QLabel(page.content);
    locale_combo = new QComboBox(page.content);
    locale_combo->setObjectName(QStringLiteral("settingsLocale"));
    locale_label->setBuddy(locale_combo);
    page.form->addRow(locale_label, locale_combo);
    QObject::connect(locale_combo, &QComboBox::currentIndexChanged, &owner,
                     [this](int index) { select_locale(index); });
  }

  void create_keybindings_page() {
    keybindings_category_item = new QListWidgetItem(category_list);
    keybindings_category_item->setData(Qt::UserRole,
                                       QStringLiteral("keybindings"));

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
      settings_changed(QtSettingsChangeKind::KeybindingsCommit);
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
      settings_changed(QtSettingsChangeKind::KeybindingsCommit);
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

  SectionWidgets
  create_viewport_section(const ViewportSettingsSectionDescriptor &descriptor) {
    const std::string_view stable_id = section_stable_id(descriptor.id);
    auto *category_item = new QListWidgetItem(category_list);
    category_item->setData(
        Qt::UserRole,
        QString::fromUtf8(stable_id.data(),
                          static_cast<qsizetype>(stable_id.size())));
    const OptionsPageWidgets page = create_options_page(stable_id);
    return {&descriptor, category_item, page.form};
  }

  void set_context(UISettings &new_ui_settings,
                   ViewportSettings &new_viewport_settings,
                   LocalizationService &new_localization,
                   QuaderCommandsApi &new_commands,
                   std::function<void(QtSettingsChangeKind)>
                       new_settings_changed) {
    target_ui_settings = &new_ui_settings;
    target_viewport_settings = &new_viewport_settings;
    ui_settings = &draft_ui_settings;
    viewport_settings = &draft_viewport_settings;
    localization = &new_localization;
    commands = &new_commands;
    settings_changed = std::move(new_settings_changed);
    controller.bind(draft_ui_settings, draft_viewport_settings,
                    new_localization, [](QtSettingsChangeKind) {});
    transaction_active = false;
    clear_dirty();
    synchronize_draft_from_targets(true);
    refresh();
  }

  void refresh() {
    QScopedValueRollback<bool> refresh_guard(refreshing, true);

    synchronize_draft_from_targets(!transaction_active);

    owner.setWindowTitle(qtext(localization, "ui.panel.settings", "Settings"));
    interface_category_item->setText(
        qtext(localization, "ui.settings.interface", "Interface"));
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
    apply_button->setText(qtext(localization, "ui.action.apply", "Apply"));
    ok_button->setText(qtext(localization, "ui.action.ok", "OK"));
    cancel_button->setText(
        qtext(localization, "ui.action.cancel", "Cancel"));

    refresh_locale_combo();
    refresh_keybindings();
    refresh_keybinding_editor();

    for (const SectionWidgets &section : sections) {
      const std::string key = section_localization_key(section.descriptor->id);
      section.category_item->setText(
          qtext(localization, key, section.descriptor->label));
    }

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
    dialog.setOption(QColorDialog::ShowAlphaChannel, true);

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
                                   std::function<void(QtSettingsChangeKind)>
                                       settings_changed) {
  impl_->set_context(ui_settings, viewport_settings, localization, commands,
                     std::move(settings_changed));
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
