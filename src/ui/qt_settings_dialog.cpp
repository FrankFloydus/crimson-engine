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
#include "ui/qt_settings_controller.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QVariant>
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

std::string setting_localization_key(std::string_view descriptor_key) {
  std::string key = "viewport.settings.";
  key.append(descriptor_key);
  key.append(".label");
  return key;
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
    const LocalizationService *localization) {
  const QColor swatch_color = to_q_color(color);
  button.set_swatch_color(swatch_color);
  const QString opacity = color_opacity_text(localization, swatch_color);
  const QString details =
      QStringLiteral("%1 | %2")
          .arg(swatch_color.name(QColor::HexArgb).toUpper(), opacity);
  button.setToolTip(details);
  button.setAccessibleDescription(details);
}

QString keybinding_context_text(const LocalizationService *localization,
                                QuaderKeybindingContextKind context) {
  switch (context) {
  case QuaderKeybindingContextKind::Global:
    return qtext(localization, "ui.settings.keybindings.context.global",
                 "Global");
  case QuaderKeybindingContextKind::Viewport:
    return qtext(localization, "ui.settings.keybindings.context.viewport",
                 "Viewport");
  case QuaderKeybindingContextKind::ViewportNavigation:
    return qtext(
        localization, "ui.settings.keybindings.context.viewport_navigation",
        "Viewport Navigation");
  case QuaderKeybindingContextKind::FlyNavigation:
    return qtext(localization,
                 "ui.settings.keybindings.context.fly_navigation",
                 "Fly Navigation");
  case QuaderKeybindingContextKind::OperationPreview:
    return qtext(localization,
                 "ui.settings.keybindings.context.operation_preview",
                 "Operation Preview");
  case QuaderKeybindingContextKind::ModalTool:
    return qtext(localization, "ui.settings.keybindings.context.modal_tool",
                 "Modal Tool");
  case QuaderKeybindingContextKind::LinearArrayTool:
    return qtext(localization,
                 "ui.settings.keybindings.context.linear_array_tool",
                 "Linear Array Tool");
  case QuaderKeybindingContextKind::RadialArrayTool:
    return qtext(localization,
                 "ui.settings.keybindings.context.radial_array_tool",
                 "Radial Array Tool");
  case QuaderKeybindingContextKind::Custom:
    return qtext(localization, "ui.settings.keybindings.context.custom",
                 "Custom");
  }
  return qtext(localization, "ui.settings.keybindings.context.custom",
               "Custom");
}

/// Stores one fully localized row before retained filtering and sorting.
struct KeybindingTableRow {
  QString target_id;
  QString command_name;
  QString context;
  QString input_mapping;
  std::vector<QString> input_mappings;
  std::size_t mapping_order = 0u;

  bool operator==(const KeybindingTableRow &other) const {
    return target_id == other.target_id && command_name == other.command_name &&
           context == other.context && input_mapping == other.input_mapping &&
           input_mappings == other.input_mappings &&
           mapping_order == other.mapping_order;
  }
};

/// Retained read-only model with lexical sorting for all visible columns.
class QtKeybindingTableModel final : public QAbstractTableModel {
public:
  /// Identifies the three fixed presentation columns.
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
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
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
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
  }

private:
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
          const int compared =
              QString::compare(column_text(left, column),
                               column_text(right, column), Qt::CaseInsensitive);
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

/// Read-only field that translates pressed Qt keys into canonical chords.
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

private:
  std::function<void(const QuaderInputChord &)> chord_handler_;
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
  const QuaderCommandsApi *commands = nullptr;
  QtSettingsController controller;
  std::function<void(QtSettingsChangeKind)> settings_changed;
  std::array<bool, kViewportColorSettingCount> dirty_colors{};
  std::array<bool, kViewportFloatSettingCount> dirty_floats{};
  std::array<bool, kViewportBoolSettingCount> dirty_bools{};
  bool locale_dirty = false;
  bool transaction_active = false;
  bool refreshing = false;

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
  QTableView *keybindings_table = nullptr;
  QtKeybindingTableModel *keybindings_model = nullptr;
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

    auto *dialog_layout = new QVBoxLayout(&owner);
    auto *content_layout = new QHBoxLayout();
    category_list = new QListWidget(&owner);
    category_list->setObjectName(QStringLiteral("settingsCategories"));
    category_list->setSelectionMode(QAbstractItemView::SingleSelection);
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

    keybindings_table = new QTableView(page);
    keybindings_table->setObjectName(
        QStringLiteral("settingsKeybindingsTable"));
    keybindings_table->setAlternatingRowColors(true);
    keybindings_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
    page_stack->addWidget(page);
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
                   const QuaderCommandsApi &new_commands,
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
    locale_label->setText(
        qtext(localization, "ui.settings.locale", "Language"));
    apply_button->setText(qtext(localization, "ui.action.apply", "Apply"));
    ok_button->setText(qtext(localization, "ui.action.ok", "OK"));
    cancel_button->setText(
        qtext(localization, "ui.action.cancel", "Cancel"));

    refresh_locale_combo();
    refresh_keybindings();

    for (const SectionWidgets &section : sections) {
      const std::string key = section_localization_key(section.descriptor->id);
      section.category_item->setText(
          qtext(localization, key, section.descriptor->label));
    }

    for (const ColorWidgets &control : color_controls) {
      const std::string key = setting_localization_key(control.descriptor->key);
      const QString label = qtext(localization, key, control.descriptor->label);
      control.label->setText(label);
      control.button->setAccessibleName(label);
      control.button->setEnabled(viewport_settings != nullptr);
      if (viewport_settings != nullptr) {
        set_color_button_presentation(
            *control.button,
            viewport_settings->color(control.descriptor->id), localization);
      } else {
        set_color_button_presentation(*control.button,
                                      control.descriptor->default_value,
                                      localization);
      }
    }

    for (const FloatWidgets &control : float_controls) {
      const std::string key = setting_localization_key(control.descriptor->key);
      control.label->setText(
          qtext(localization, key, control.descriptor->label));
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
        std::vector<QString> input_mappings;
        input_mappings.reserve(entry.shortcuts.size());
        for (const std::string &shortcut : entry.shortcuts) {
          input_mappings.push_back(QString::fromStdString(shortcut));
        }
        rows.push_back({
            .target_id = QString::fromStdString(entry.action_id),
            .command_name = qtext(localization, entry.localization_key,
                                  entry.label),
            .context = keybinding_context_text(localization, entry.context),
            .input_mapping = QString::fromStdString(entry.shortcut),
            .input_mappings = std::move(input_mappings),
            .mapping_order = index,
        });
      }
    }
    keybinding_rows = std::move(rows);
    apply_keybinding_filters();
  }

  void apply_keybinding_filters() {
    const QString command_filter = keybindings_search->text().trimmed();
    std::vector<KeybindingTableRow> filtered;
    filtered.reserve(keybinding_rows.size());
    for (const KeybindingTableRow &row : keybinding_rows) {
      if (!command_filter.isEmpty() &&
          !row.command_name.contains(command_filter, Qt::CaseInsensitive)) {
        continue;
      }
      if (!keybinding_shortcut_text.isEmpty() &&
          std::none_of(row.input_mappings.begin(), row.input_mappings.end(),
                       [&](const QString &mapping) {
                         return QString::compare(
                                    mapping, keybinding_shortcut_text,
                                    Qt::CaseInsensitive) == 0;
                       })) {
        continue;
      }
      filtered.push_back(row);
    }
    keybindings_model->set_rows(std::move(filtered));
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
        [this, id = descriptor.id, guarded_button](const QColor &color) {
          if (!color.isValid()) {
            return;
          }
          if (controller.preview_color(id, from_q_color(color))) {
            mark_color_dirty(id);
          }
          if (guarded_button != nullptr && viewport_settings != nullptr) {
            set_color_button_presentation(
                *guarded_button, viewport_settings->color(id), localization);
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
          localization);
    }
  }
};

QtSettingsDialog::QtSettingsDialog(QWidget *parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this)) {}

QtSettingsDialog::~QtSettingsDialog() = default;

void QtSettingsDialog::set_context(UISettings &ui_settings,
                                   ViewportSettings &viewport_settings,
                                   LocalizationService &localization,
                                   const QuaderCommandsApi &commands,
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
}

} // namespace quader
