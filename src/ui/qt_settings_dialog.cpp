////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_settings_dialog.hpp"

#include "core/localization_service.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_settings_controller.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
  case ViewportSettingsSectionId::Selection:
    return "selection";
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
  UISettings *ui_settings = nullptr;
  ViewportSettings *viewport_settings = nullptr;
  LocalizationService *localization = nullptr;
  QtSettingsController controller;
  bool refreshing = false;

  QListWidget *category_list = nullptr;
  QStackedWidget *page_stack = nullptr;
  QListWidgetItem *interface_category_item = nullptr;
  QLabel *locale_label = nullptr;
  QComboBox *locale_combo = nullptr;
  QPushButton *reset_button = nullptr;
  std::vector<SectionWidgets> sections;
  std::vector<ColorWidgets> color_controls;
  std::vector<FloatWidgets> float_controls;
  std::vector<BoolWidgets> bool_controls;

  void build() {
    owner.setObjectName(QStringLiteral("settingsDialog"));
    owner.setModal(false);
    owner.setWindowModality(Qt::NonModal);
    owner.setAttribute(Qt::WA_DeleteOnClose, false);
    owner.setSizeGripEnabled(true);
    owner.resize(720, 560);

    auto *dialog_layout = new QHBoxLayout(&owner);
    category_list = new QListWidget(&owner);
    category_list->setObjectName(QStringLiteral("settingsCategories"));
    category_list->setSelectionMode(QAbstractItemView::SingleSelection);
    category_list->setMinimumWidth(160);
    category_list->setMaximumWidth(220);
    dialog_layout->addWidget(category_list);

    auto *options_container = new QWidget(&owner);
    options_container->setObjectName(QStringLiteral("settingsOptions"));
    auto *options_layout = new QVBoxLayout(options_container);
    page_stack = new QStackedWidget(options_container);
    page_stack->setObjectName(QStringLiteral("settingsPages"));
    options_layout->addWidget(page_stack, 1);

    reset_button = new QPushButton(options_container);
    reset_button->setObjectName(QStringLiteral("settingsResetDefaults"));
    options_layout->addWidget(reset_button);
    dialog_layout->addWidget(options_container, 1);

    QObject::connect(category_list, &QListWidget::currentRowChanged, &owner,
                     [this](int row) {
                       if (row >= 0 && row < page_stack->count()) {
                         page_stack->setCurrentIndex(row);
                       }
                       reset_button->setVisible(row > 0);
                     });

    QObject::connect(reset_button, &QPushButton::clicked, &owner, [this]() {
      if (viewport_settings == nullptr) {
        return;
      }
      (void)controller.reset_viewport_to_defaults();
      refresh();
    });

    create_interface_page();

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
                         (void)controller.set_float(
                             id, static_cast<float>(value));
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
                         (void)controller.set_bool(id, checked);
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
                   std::function<void(QtSettingsChangeKind)>
                       new_settings_changed) {
    ui_settings = &new_ui_settings;
    viewport_settings = &new_viewport_settings;
    localization = &new_localization;
    controller.bind(new_ui_settings, new_viewport_settings, new_localization,
                    std::move(new_settings_changed));
    refresh();
  }

  void refresh() {
    QScopedValueRollback<bool> refresh_guard(refreshing, true);

    owner.setWindowTitle(qtext(localization, "ui.panel.settings", "Settings"));
    interface_category_item->setText(
        qtext(localization, "ui.settings.interface", "Interface"));
    locale_label->setText(
        qtext(localization, "ui.settings.locale", "Language"));
    reset_button->setText(qtext(localization,
                                "ui.settings.viewport.reset_to_defaults",
                                "Reset to Defaults"));

    refresh_locale_combo();

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

    reset_button->setEnabled(viewport_settings != nullptr);
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
    if (refreshing || ui_settings == nullptr || localization == nullptr ||
        index < 0) {
      return;
    }

    const std::string locale_id =
        locale_combo->itemData(index).toString().toStdString();
    if (controller.select_locale(locale_id)) {
      refresh();
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
          (void)controller.preview_color(id, from_q_color(color));
          if (guarded_button != nullptr && viewport_settings != nullptr) {
            set_color_button_presentation(
                *guarded_button, viewport_settings->color(id), localization);
          }
        });

    const int result = dialog.exec();
    if (result == QDialog::Accepted) {
      if (dialog.currentColor().isValid()) {
        (void)controller.preview_color(
            descriptor.id, from_q_color(dialog.currentColor()));
      }
      (void)controller.commit_color(descriptor.id, initial);
    } else {
      (void)controller.rollback_color(descriptor.id, initial);
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
                                   std::function<void(QtSettingsChangeKind)>
                                       settings_changed) {
  impl_->set_context(ui_settings, viewport_settings, localization,
                     std::move(settings_changed));
}

void QtSettingsDialog::refresh() { impl_->refresh(); }

} // namespace quader
