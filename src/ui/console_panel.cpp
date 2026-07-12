////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/console_panel.hpp"

#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "ui/console_model.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_panel.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr int kConsoleLevelRole = Qt::UserRole + 1;

QString qt_text(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

const char *level_key(spdlog::level::level_enum level) {
  switch (level) {
  case spdlog::level::trace:
    return "ui.console.level.trace";
  case spdlog::level::debug:
    return "ui.console.level.debug";
  case spdlog::level::info:
    return "ui.console.level.info";
  case spdlog::level::warn:
    return "ui.console.level.warn";
  case spdlog::level::err:
    return "ui.console.level.error";
  case spdlog::level::critical:
    return "ui.console.level.critical";
  default:
    return "ui.console.level.off";
  }
}

const char *level_fallback(spdlog::level::level_enum level) {
  switch (level) {
  case spdlog::level::trace:
    return "Trace";
  case spdlog::level::debug:
    return "Debug";
  case spdlog::level::info:
    return "Info";
  case spdlog::level::warn:
    return "Warn";
  case spdlog::level::err:
    return "Error";
  case spdlog::level::critical:
    return "Critical";
  default:
    return "Off";
  }
}

constexpr std::array<spdlog::level::level_enum, 7> kConsoleLevels{{
    spdlog::level::trace,
    spdlog::level::debug,
    spdlog::level::info,
    spdlog::level::warn,
    spdlog::level::err,
    spdlog::level::critical,
    spdlog::level::off,
}};

bool same_visible_log_entries(const std::vector<LogEntry> &left,
                              const std::vector<LogEntry> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].level != right[index].level ||
        left[index].message != right[index].message) {
      return false;
    }
  }
  return true;
}

} // namespace

/// Owns the retained controls and filtered log projection for ConsolePanel.
struct ConsolePanel::Impl {
  explicit Impl(ConsolePanel &panel) : panel_(panel) {
    panel_.setObjectName(
        QString::fromLatin1(ui_panel_stable_id(UIPanelId::Console)));
    panel_.setWindowTitle(
        QString::fromLatin1(ui_panel_key(UIPanelId::Console)));

    auto *root_layout = new QVBoxLayout(&panel_);
    auto *controls_layout = new QHBoxLayout();
    root_layout->addLayout(controls_layout);

    clear_button_ = new QPushButton(&panel_);
    clear_button_->setObjectName(QStringLiteral("console.clear"));
    clear_button_->setText(QStringLiteral("Clear"));
    controls_layout->addWidget(clear_button_);

    auto_scroll_box_ = new QCheckBox(&panel_);
    auto_scroll_box_->setObjectName(QStringLiteral("console.auto_scroll"));
    auto_scroll_box_->setChecked(auto_scroll_);
    auto_scroll_box_->setText(QStringLiteral("Auto-scroll"));
    controls_layout->addWidget(auto_scroll_box_);

    level_label_ = new QLabel(&panel_);
    level_label_->setObjectName(QStringLiteral("console.level.label"));
    level_label_->setText(QStringLiteral("Level"));
    controls_layout->addWidget(level_label_);

    level_combo_ = new QComboBox(&panel_);
    level_combo_->setObjectName(QStringLiteral("console.level"));
    for (const spdlog::level::level_enum level : kConsoleLevels) {
      level_combo_->addItem(QString::fromLatin1(level_fallback(level)),
                            static_cast<int>(level));
    }
    controls_layout->addWidget(level_combo_);

    search_edit_ = new QLineEdit(&panel_);
    search_edit_->setObjectName(QStringLiteral("console.search"));
    search_edit_->setClearButtonEnabled(true);
    search_edit_->setPlaceholderText(QStringLiteral("Filter logs"));
    controls_layout->addWidget(search_edit_, 1);

    entries_view_ = new QListView(&panel_);
    entries_view_->setObjectName(QStringLiteral("console.entries"));
    entries_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    entries_view_->setSelectionMode(QAbstractItemView::NoSelection);
    entries_view_->setUniformItemSizes(true);
    entries_view_->setWordWrap(false);
    entries_view_->setTextElideMode(Qt::ElideNone);
    entries_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    entries_model_ = new QStandardItemModel(entries_view_);
    entries_model_->setObjectName(QStringLiteral("console.entries.model"));
    entries_view_->setModel(entries_model_);
    root_layout->addWidget(entries_view_, 1);

    QObject::connect(clear_button_, &QPushButton::clicked, &panel_, [this]() {
      Logger::clear_memory();
      source_entries_ = Logger::entries_snapshot();
      rebuild_entries();
    });
    QObject::connect(auto_scroll_box_, &QCheckBox::toggled, &panel_,
                     [this](bool enabled) { auto_scroll_ = enabled; });
    QObject::connect(level_combo_, &QComboBox::currentIndexChanged, &panel_,
                     [this](int index) {
                       if (updating_controls_ || index < 0) {
                         return;
                       }
                       filter_.minimum_level =
                           static_cast<spdlog::level::level_enum>(
                               level_combo_->itemData(index).toInt());
                       rebuild_entries();
                     });
    QObject::connect(search_edit_, &QLineEdit::textChanged, &panel_,
                     [this](const QString &text) {
                       if (updating_controls_) {
                         return;
                       }
                       filter_.search_text = text.toUtf8().toStdString();
                       rebuild_entries();
                     });
  }

  void refresh(UIContext &context) {
    const bool localization_changed =
        localization_ != &context.localization ||
        localized_locale_ != context.localization.current_locale();
    localization_ = &context.localization;
    if (localization_changed) {
      localized_locale_ = localization_->current_locale();
      update_localization();
    }

    std::vector<LogEntry> entries = Logger::entries_snapshot();
    if (!same_visible_log_entries(source_entries_, entries)) {
      source_entries_ = std::move(entries);
      rebuild_entries();
    }
  }

private:
  void update_localization() {
    if (localization_ == nullptr) {
      return;
    }

    panel_.setWindowTitle(qt_text(
        localization_->text(ui_panel_localization_key(UIPanelId::Console),
                            ui_panel_key(UIPanelId::Console))));
    clear_button_->setText(
        qt_text(localization_->text("ui.console.clear", "Clear")));
    auto_scroll_box_->setText(
        qt_text(localization_->text("ui.console.auto_scroll", "Auto-scroll")));
    level_label_->setText(
        qt_text(localization_->text("ui.console.level", "Level")));
    search_edit_->setPlaceholderText(
        qt_text(localization_->text("ui.console.filter_logs", "Filter logs")));

    const QSignalBlocker level_blocker(level_combo_);
    updating_controls_ = true;
    level_combo_->clear();
    int selected_index = 0;
    for (std::size_t index = 0; index < kConsoleLevels.size(); ++index) {
      const spdlog::level::level_enum level = kConsoleLevels[index];
      level_combo_->addItem(
          qt_text(localization_->text(level_key(level), level_fallback(level))),
          static_cast<int>(level));
      if (level == filter_.minimum_level) {
        selected_index = static_cast<int>(index);
      }
    }
    level_combo_->setCurrentIndex(selected_index);
    updating_controls_ = false;

    for (int row = 0; row < entries_model_->rowCount(); ++row) {
      QStandardItem *item = entries_model_->item(row, 0);
      if (item == nullptr) {
        continue;
      }
      const auto level = static_cast<spdlog::level::level_enum>(
          item->data(kConsoleLevelRole).toInt());
      item->setData(
          qt_text(localization_->text(level_key(level), level_fallback(level))),
          Qt::AccessibleDescriptionRole);
    }
  }

  void rebuild_entries() {
    QScrollBar *vertical_scroll = entries_view_->verticalScrollBar();
    const bool was_at_bottom =
        vertical_scroll == nullptr ||
        vertical_scroll->value() >= vertical_scroll->maximum();

    const std::vector<LogEntry> filtered =
        ConsoleModel::filter(source_entries_, filter_);
    entries_model_->clear();
    entries_model_->setColumnCount(1);
    entries_model_->setRowCount(static_cast<int>(filtered.size()));
    for (std::size_t index = 0; index < filtered.size(); ++index) {
      const LogEntry &entry = filtered[index];
      auto *item = new QStandardItem(qt_text(entry.message));
      item->setEditable(false);
      item->setData(static_cast<int>(entry.level), kConsoleLevelRole);
      if (localization_ != nullptr) {
        item->setData(qt_text(localization_->text(level_key(entry.level),
                                                  level_fallback(entry.level))),
                      Qt::AccessibleDescriptionRole);
      }
      entries_model_->setItem(static_cast<int>(index), 0, item);
    }

    if (auto_scroll_ && was_at_bottom) {
      entries_view_->scrollToBottom();
    }
  }

  ConsolePanel &panel_;
  QPushButton *clear_button_ = nullptr;
  QCheckBox *auto_scroll_box_ = nullptr;
  QLabel *level_label_ = nullptr;
  QComboBox *level_combo_ = nullptr;
  QLineEdit *search_edit_ = nullptr;
  QListView *entries_view_ = nullptr;
  QStandardItemModel *entries_model_ = nullptr;
  LocalizationService *localization_ = nullptr;
  std::string localized_locale_;
  ConsoleFilter filter_;
  std::vector<LogEntry> source_entries_;
  bool auto_scroll_ = true;
  bool updating_controls_ = false;
};

ConsolePanel::ConsolePanel(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

ConsolePanel::~ConsolePanel() = default;

void ConsolePanel::refresh(UIContext &context) { impl_->refresh(context); }

} // namespace quader
