////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/console_panel.hpp"

#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "core/qstr.hpp"
#include "ui/console_model.hpp"
#include "ui/qt_application_style.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_panel.hpp"

#include <QAction>
#include <QCheckBox>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleOption>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

constexpr int kConsoleSearchDebounceMs = 120;
constexpr std::size_t kMaximumCommandReplies = 200;
constexpr std::array<spdlog::level::level_enum, 5> kConsoleLevels{
    spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
    spdlog::level::warn, spdlog::level::err};

QString qt_text(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

const char *level_key(spdlog::level::level_enum level) {
  switch (level) {
  case spdlog::level::trace:
    return "ui.console.level.trace";
  case spdlog::level::debug:
    return "ui.console.level.debug";
  case spdlog::level::warn:
    return "ui.console.level.warning";
  case spdlog::level::err:
  case spdlog::level::critical:
    return "ui.console.level.error";
  default:
    return "ui.console.level.info";
  }
}

const char *level_fallback(spdlog::level::level_enum level) {
  switch (level) {
  case spdlog::level::trace:
    return "Trace";
  case spdlog::level::debug:
    return "Debug";
  case spdlog::level::warn:
    return "Warning";
  case spdlog::level::err:
  case spdlog::level::critical:
    return "Error";
  default:
    return "Info";
  }
}

QtLogMessageSeverity message_severity(spdlog::level::level_enum level) {
  switch (level) {
  case spdlog::level::trace:
  case spdlog::level::debug:
    return QtLogMessageSeverity::Subdued;
  case spdlog::level::warn:
    return QtLogMessageSeverity::Warning;
  case spdlog::level::err:
  case spdlog::level::critical:
    return QtLogMessageSeverity::Error;
  default:
    return QtLogMessageSeverity::Information;
  }
}

QString compact_payload(const LogEntry &entry) {
  QString text = qt_text(entry.timestamp == spdlog::log_clock::time_point{}
                             ? entry.message
                             : entry.payload);
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QChar('\r'), QChar('\n'));
  while (text.endsWith(QChar('\n'))) {
    text.chop(1);
  }
  return text;
}

} // namespace

/// Retains log entries and native text controls without replacing editor state.
struct ConsolePanel::Impl {
  explicit Impl(ConsolePanel &panel) : panel_(panel) {
    panel_.setObjectName(
        QString::fromLatin1(ui_panel_stable_id(UIPanelId::Console)));
    panel_.setWindowTitle(
        QString::fromLatin1(ui_panel_key(UIPanelId::Console)));
    auto *root_layout = new QVBoxLayout(&panel_);
    qt_settings_detail::set_scalable_margins(*root_layout,
                                             QMargins(6, 6, 6, 6));
    qt_settings_detail::set_scalable_spacing(*root_layout, 6);
    auto *controls_layout = new QHBoxLayout();
    qt_settings_detail::set_scalable_spacing(*controls_layout, 6);
    root_layout->addLayout(controls_layout);

    level_button_ = new QToolButton(&panel_);
    level_button_->setObjectName(QStringLiteral("console.level"));
    level_button_->setText(QStringLiteral("Levels"));
    level_button_->setPopupMode(QToolButton::InstantPopup);
    level_menu_ = new QMenu(level_button_);
    level_menu_->setObjectName(QStringLiteral("console.level.menu"));
    level_button_->setMenu(level_menu_);
    for (std::size_t index = 0; index < kConsoleLevels.size(); ++index) {
      const auto level = kConsoleLevels[index];
      QAction *action =
          level_menu_->addAction(QString::fromLatin1(level_fallback(level)));
      action->setObjectName(
          QStringLiteral("console.level.%1")
              .arg(QString::fromLatin1(level_fallback(level)).toLower()));
      action->setCheckable(true);
      action->setChecked(true);
      level_actions_[index] = action;
      QObject::connect(
          action, &QAction::toggled, &panel_, [this, level](bool checked) {
            const ConsoleLevelMask mask = ConsoleModel::level_mask(level);
            filter_.level_mask =
                checked
                    ? static_cast<ConsoleLevelMask>(filter_.level_mask | mask)
                    : static_cast<ConsoleLevelMask>(filter_.level_mask & ~mask);
            (void)commit_pending_search_filter();
            rebuild_entries();
          });
    }
    controls_layout->addWidget(level_button_);

    search_edit_ = new QLineEdit(&panel_);
    search_edit_->setObjectName(QStringLiteral("console.search"));
    search_edit_->setClearButtonEnabled(true);
    search_edit_->setPlaceholderText(QStringLiteral("Filter logs"));
    controls_layout->addWidget(search_edit_, 1);

    clear_button_ = new QPushButton(QStringLiteral("Clear"), &panel_);
    clear_button_->setObjectName(QStringLiteral("console.clear"));
    controls_layout->addWidget(clear_button_);
    auto_scroll_box_ = new QCheckBox(QStringLiteral("Auto-scroll"), &panel_);
    auto_scroll_box_->setObjectName(QStringLiteral("console.auto_scroll"));
    auto_scroll_box_->setChecked(true);
    controls_layout->addWidget(auto_scroll_box_);

    entries_view_ = new QPlainTextEdit(&panel_);
    entries_view_->setObjectName(QStringLiteral("console.entries"));
    entries_view_->setReadOnly(true);
    entries_view_->setUndoRedoEnabled(false);
    entries_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    entries_view_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                           Qt::TextSelectableByKeyboard);
    entries_view_->installEventFilter(&panel_);
    root_layout->addWidget(entries_view_, 1);
    command_edit_ = new QLineEdit(&panel_);
    command_edit_->setObjectName(QStringLiteral("console.command"));
    command_edit_->setPlaceholderText(QStringLiteral("Enter a command"));
    root_layout->addWidget(command_edit_);
    panel_.setFocusProxy(command_edit_);

    search_filter_timer_ = new QTimer(&panel_);
    search_filter_timer_->setSingleShot(true);
    search_filter_timer_->setInterval(kConsoleSearchDebounceMs);
    QObject::connect(search_filter_timer_, &QTimer::timeout, &panel_, [this]() {
      if (commit_pending_search_filter()) {
        rebuild_entries();
      }
    });
    QObject::connect(search_edit_, &QLineEdit::textChanged, &panel_,
                     [this](const QString &text) {
                       pending_search_text_ = text.toUtf8().toStdString();
                       search_filter_timer_->start();
                     });
    QObject::connect(clear_button_, &QPushButton::clicked, &panel_, [this]() {
      source_entries_.clear();
      command_reply_count_ = 0;
      Logger::clear_memory();
      synchronize_source_entries();
    });
    QObject::connect(auto_scroll_box_, &QCheckBox::toggled, &panel_,
                     [this](bool enabled) { auto_scroll_ = enabled; });
    QObject::connect(
        command_edit_, &QLineEdit::returnPressed, &panel_, [this]() {
          const std::string command =
              command_edit_->text().trimmed().toUtf8().toStdString();
          if (command.empty()) {
            return;
          }
          command_edit_->clear();
          if (command_handler_) {
            const auto handler = command_handler_;
            handler(command);
          } else {
            const std::string error =
                localization_ != nullptr
                    ? localization_->text("ui.console.unknown_command",
                                          "Unknown command")
                    : "Unknown command";
            QString reply = qt_text(error);
            if (reply.contains(QStringLiteral("%1"))) {
              reply = reply.arg(qt_text(command));
            } else {
              reply += QStringLiteral(": ") + qt_text(command);
            }
            append_command_reply(spdlog::level::err,
                                 reply.toUtf8().toStdString());
          }
        });
    update_colors();
  }

  void refresh(UIContext &context) {
    const bool localization_changed =
        localization_ != &context.localization ||
        localized_locale_ != context.localization.current_locale() ||
        localized_revision_ != context.localization.revision();
    localization_ = &context.localization;
    if (localization_changed) {
      localized_locale_ = localization_->current_locale();
      localized_revision_ = localization_->revision();
      update_localization();
    }
    synchronize_source_entries();
  }

  void set_command_handler(std::function<void(std::string_view)> handler) {
    command_handler_ = std::move(handler);
  }

  void append_command_reply(spdlog::level::level_enum level,
                            std::string_view text) {
    synchronize_source_entries();
    ViewState state = capture_view_state();
    while (command_reply_count_ >= kMaximumCommandReplies) {
      const auto oldest =
          std::find_if(source_entries_.begin(), source_entries_.end(),
                       [](const Entry &entry) { return entry.command_reply; });
      if (oldest == source_entries_.end()) {
        break;
      }
      remove_visible_entry(oldest->id, state);
      source_entries_.erase(oldest);
      --command_reply_count_;
    }
    Entry entry{
        .log = {.level = level,
                .timestamp = spdlog::log_clock::now(),
                .payload = std::string(text)},
        .id = ++next_entry_id_,
        .command_reply = true,
    };
    append_visible_entry(entry);
    source_entries_.push_back(std::move(entry));
    ++command_reply_count_;
    restore_view_state(state);
  }

  void update_colors() {
    if (entries_view_ == nullptr) {
      return;
    }
    QStyleOption option;
    option.initFrom(entries_view_);
    QPalette palette = option.palette;
    palette.setCurrentColorGroup(!panel_.isEnabled() ? QPalette::Disabled
                                 : panel_.isActiveWindow()
                                     ? QPalette::Active
                                     : QPalette::Inactive);
    const bool themed = qt_application_style_active(&option);
    if (color_state_valid_ && render_palette_ == palette && themed_ == themed &&
        render_palette_.currentColorGroup() == palette.currentColorGroup()) {
      return;
    }
    render_palette_ = palette;
    themed_ = themed;
    color_state_valid_ = true;
    int start = 0;
    QTextCursor cursor(entries_view_->document());
    cursor.beginEditBlock();
    for (const VisibleEntry &entry : visible_entries_) {
      cursor.setPosition(start);
      cursor.setPosition(start + entry.length, QTextCursor::KeepAnchor);
      cursor.setCharFormat(entry_format(entry.level));
      start += entry.length + 1;
    }
    cursor.endEditBlock();
  }

private:
  struct Entry {
    LogEntry log;
    std::uint64_t id = 0;
    bool command_reply = false;
  };
  struct VisibleEntry {
    std::uint64_t id = 0;
    int length = 0;
    spdlog::level::level_enum level = spdlog::level::info;
  };
  struct ViewState {
    int anchor = 0;
    int position = 0;
    int top_position = 0;
    int top_offset = 0;
    int horizontal = 0;
    bool follow = false;
  };

  [[nodiscard]] ViewState capture_view_state() const {
    const QTextCursor cursor = entries_view_->textCursor();
    const QTextBlock top =
        entries_view_->cursorForPosition(QPoint(0, 0)).block();
    const QScrollBar *scroll = entries_view_->verticalScrollBar();
    return {.anchor = cursor.anchor(),
            .position = cursor.position(),
            .top_position = top.position(),
            .top_offset = scroll->value() - top.blockNumber(),
            .horizontal = entries_view_->horizontalScrollBar()->value(),
            .follow = auto_scroll_ && scroll->value() >= scroll->maximum()};
  }

  void restore_view_state(const ViewState &state) {
    QTextDocument *document = entries_view_->document();
    const int maximum_position = document->characterCount() - 1;
    QTextCursor cursor(document);
    cursor.setPosition(std::clamp(state.anchor, 0, maximum_position));
    cursor.setPosition(std::clamp(state.position, 0, maximum_position),
                       QTextCursor::KeepAnchor);
    entries_view_->setTextCursor(cursor);
    entries_view_->horizontalScrollBar()->setValue(state.horizontal);
    QScrollBar *scroll = entries_view_->verticalScrollBar();
    if (state.follow) {
      scroll->setValue(scroll->maximum());
    } else {
      const QTextBlock top = document->findBlock(
          std::clamp(state.top_position, 0, maximum_position));
      scroll->setValue(top.blockNumber() + state.top_offset);
    }
  }

  void synchronize_source_entries() {
    const std::uint64_t revision = Logger::memory_revision();
    if (source_revision_valid_ && source_revision_ == revision) {
      return;
    }
    LogEntriesDelta delta = Logger::entries_since(source_cursor_);
    source_revision_ = delta.revision;
    source_revision_valid_ = true;
    source_cursor_ = delta.cursor;
    ViewState state = capture_view_state();
    for (auto entry = source_entries_.begin();
         entry != source_entries_.end();) {
      if (!entry->command_reply &&
          (delta.reset_required ||
           (delta.first_sequence != 0u &&
            entry->log.sequence < delta.first_sequence))) {
        remove_visible_entry(entry->id, state);
        entry = source_entries_.erase(entry);
      } else {
        ++entry;
      }
    }
    if (delta.reset_required && source_entries_.empty()) {
      entries_view_->clear();
      visible_entries_.clear();
      state.anchor = state.position = state.top_position = state.top_offset = 0;
    }
    for (LogEntry &log : delta.entries) {
      Entry entry{.log = std::move(log), .id = ++next_entry_id_};
      if (entry_matches(entry)) {
        append_visible_entry(entry);
      }
      source_entries_.push_back(std::move(entry));
    }
    restore_view_state(state);
  }

  void update_localization() {
    panel_.setWindowTitle(qt_text(
        localization_->text(ui_panel_localization_key(UIPanelId::Console),
                            ui_panel_key(UIPanelId::Console))));
    clear_button_->setText(
        qt_text(localization_->text("ui.console.clear", "Clear")));
    auto_scroll_box_->setText(
        qt_text(localization_->text("ui.console.auto_scroll", "Auto-scroll")));
    level_button_->setText(
        qt_text(localization_->text("ui.console.levels", "Levels")));
    search_edit_->setPlaceholderText(
        qt_text(localization_->text("ui.console.filter_logs", "Filter logs")));
    command_edit_->setPlaceholderText(
        qt_text(localization_->text("ui.console.command", "Enter a command")));
    for (std::size_t index = 0; index < kConsoleLevels.size(); ++index) {
      const auto level = kConsoleLevels[index];
      level_actions_[index]->setText(qt_text(
          localization_->text(level_key(level), level_fallback(level))));
    }
    rebuild_entries();
  }

  [[nodiscard]] bool entry_matches(const Entry &entry) const {
    return entry.command_reply ||
           ConsoleModel::accepts_normalized(entry.log, filter_.level_mask,
                                            normalized_search_);
  }

  bool commit_pending_search_filter() {
    if (search_filter_timer_->isActive()) {
      search_filter_timer_->stop();
    }
    if (filter_.search_text == pending_search_text_) {
      return false;
    }
    filter_.search_text = pending_search_text_;
    normalized_search_ = QStr::to_ascii_lower(filter_.search_text);
    return true;
  }

  [[nodiscard]] QString entry_text(const LogEntry &entry) const {
    const QString payload = compact_payload(entry);
    if (entry.timestamp == spdlog::log_clock::time_point{}) {
      return payload;
    }
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.timestamp.time_since_epoch())
            .count();
    const QString time = QDateTime::fromMSecsSinceEpoch(milliseconds)
                             .toString(QStringLiteral("HH:mm:ss"));
    const QString level =
        localization_ != nullptr
            ? qt_text(localization_->text(level_key(entry.level),
                                          level_fallback(entry.level)))
            : QString::fromLatin1(level_fallback(entry.level));
    return QStringLiteral("[%1] [%2] %3").arg(time, level, payload);
  }

  [[nodiscard]] QTextCharFormat
  entry_format(spdlog::level::level_enum level) const {
    QTextCharFormat format;
    format.setForeground(qt_log_message_color(message_severity(level),
                                              render_palette_, themed_));
    return format;
  }

  void append_visible_entry(const Entry &entry) {
    const QString text = entry_text(entry.log);
    QTextCursor cursor(entries_view_->document());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat block_format;
    block_format.setTopMargin(0);
    block_format.setBottomMargin(0);
    if (!visible_entries_.empty()) {
      cursor.insertBlock(block_format, entry_format(entry.log.level));
    } else {
      cursor.setBlockFormat(block_format);
    }
    cursor.insertText(text, entry_format(entry.log.level));
    visible_entries_.push_back(
        {entry.id, static_cast<int>(text.size()), entry.log.level});
  }

  void remove_visible_entry(std::uint64_t id, ViewState &state) {
    int start = 0;
    for (auto entry = visible_entries_.begin(); entry != visible_entries_.end();
         ++entry) {
      if (entry->id != id) {
        start += entry->length + 1;
        continue;
      }
      int end = start + entry->length;
      if (std::next(entry) != visible_entries_.end()) {
        ++end;
      } else if (entry != visible_entries_.begin()) {
        --start;
      }
      const auto adjust_position = [start, end](int position) {
        return position < start ? position
               : position < end ? start
                                : position - (end - start);
      };
      state.anchor = adjust_position(state.anchor);
      state.position = adjust_position(state.position);
      state.top_position = adjust_position(state.top_position);
      QTextCursor cursor(entries_view_->document());
      cursor.setPosition(start);
      cursor.setPosition(end, QTextCursor::KeepAnchor);
      cursor.removeSelectedText();
      visible_entries_.erase(entry);
      return;
    }
  }

  void rebuild_entries() {
    const ViewState state = capture_view_state();
    entries_view_->clear();
    visible_entries_.clear();
    for (const Entry &entry : source_entries_) {
      if (entry_matches(entry)) {
        append_visible_entry(entry);
      }
    }
    restore_view_state(state);
  }

  ConsolePanel &panel_;
  QToolButton *level_button_ = nullptr;
  QMenu *level_menu_ = nullptr;
  std::array<QAction *, kConsoleLevels.size()> level_actions_{};
  QLineEdit *search_edit_ = nullptr;
  QPushButton *clear_button_ = nullptr;
  QCheckBox *auto_scroll_box_ = nullptr;
  QPlainTextEdit *entries_view_ = nullptr;
  QLineEdit *command_edit_ = nullptr;
  QTimer *search_filter_timer_ = nullptr;
  LocalizationService *localization_ = nullptr;
  std::string localized_locale_;
  std::uint64_t localized_revision_ = 0;
  ConsoleFilter filter_;
  std::string pending_search_text_;
  std::string normalized_search_;
  std::deque<Entry> source_entries_;
  std::deque<VisibleEntry> visible_entries_;
  LogEntriesCursor source_cursor_;
  std::uint64_t source_revision_ = 0;
  std::uint64_t next_entry_id_ = 0;
  std::size_t command_reply_count_ = 0;
  std::function<void(std::string_view)> command_handler_;
  QPalette render_palette_;
  bool themed_ = false;
  bool color_state_valid_ = false;
  bool auto_scroll_ = true;
  bool source_revision_valid_ = false;
};

ConsolePanel::ConsolePanel(QWidget *parent) : QWidget(parent) {
  impl_ = std::make_unique<Impl>(*this);
}

ConsolePanel::~ConsolePanel() = default;

void ConsolePanel::refresh(UIContext &context) { impl_->refresh(context); }

void ConsolePanel::set_command_handler(
    std::function<void(std::string_view)> handler) {
  impl_->set_command_handler(std::move(handler));
}

void ConsolePanel::append_command_reply(spdlog::level::level_enum level,
                                        std::string_view text) {
  impl_->append_command_reply(level, text);
}

void ConsolePanel::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (impl_ != nullptr && (event->type() == QEvent::PaletteChange ||
                           event->type() == QEvent::ApplicationPaletteChange ||
                           event->type() == QEvent::StyleChange ||
                           event->type() == QEvent::EnabledChange ||
                           event->type() == QEvent::ActivationChange)) {
    impl_->update_colors();
  }
}

bool ConsolePanel::eventFilter(QObject *watched, QEvent *event) {
  // Child palettes resolve after the containing widget's PaletteChange event.
  if (impl_ != nullptr && (event->type() == QEvent::PaletteChange ||
                           event->type() == QEvent::ApplicationPaletteChange ||
                           event->type() == QEvent::StyleChange ||
                           event->type() == QEvent::EnabledChange)) {
    impl_->update_colors();
  }
  return QWidget::eventFilter(watched, event);
}

} // namespace quader
