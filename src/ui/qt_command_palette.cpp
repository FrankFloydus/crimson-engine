////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_command_palette.hpp"
#include "ui/qt_status_message.hpp"

#include "api/quader_commands_api.hpp"
#include "core/localization_service.hpp"
#include "editor/metadata/command_presentation_catalog.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr int kCommandIdRole = Qt::UserRole;
constexpr int kCommandEnabledRole = Qt::UserRole + 1;
constexpr int kDisabledReasonRole = Qt::UserRole + 2;
constexpr std::size_t kRecentCommandLimit = 12u;

QString qtext(const LocalizationService &localization, std::string_view key,
              std::string_view fallback) {
  return QString::fromStdString(localization.text(key, fallback));
}

struct LocalizedPresentationName final {
  std::string key;
  std::string_view fallback;
};

LocalizedPresentationName
domain_name(CommandPresentationDomain domain) {
  std::string key = domain == CommandPresentationDomain::Internal
                        ? "ui.command_palette.domain."
                        : "ui.menu.";
  key.append(command_presentation_domain_id(domain));
  return {std::move(key), command_presentation_domain_label(domain)};
}

LocalizedPresentationName
group_name(CommandPresentationGroup group) {
  std::string key = "ui.command_palette.group.";
  key.append(command_presentation_group_id(group));
  return {std::move(key), command_presentation_group_label(group)};
}

QString localized_presentation_name(
    const LocalizationService &localization,
    LocalizedPresentationName name) {
  QString result = qtext(localization, name.key, name.fallback);
  // Menu localization strings can contain mnemonic markers. Breadcrumbs do
  // not own keyboard mnemonics, so show the literal domain name.
  result.remove(QLatin1Char('&'));
  return result;
}

QString normalized_search_text(const QString &text) {
  QString normalized = text.toCaseFolded().trimmed();
  normalized.replace(QRegularExpression(QStringLiteral("[._\\-/]+")),
                     QStringLiteral(" "));
  normalized.replace(QRegularExpression(QStringLiteral("\\s+")),
                     QStringLiteral(" "));
  return normalized;
}

bool is_word_boundary(const QString &text, qsizetype position) {
  return position <= 0 || !text.at(position - 1).isLetterOrNumber();
}

int fuzzy_subsequence_score(const QString &needle, const QString &haystack) {
  if (needle.isEmpty()) {
    return 0;
  }

  const qsizetype substring_position = haystack.indexOf(needle);
  if (substring_position >= 0) {
    int score = 1000 - static_cast<int>(substring_position) * 3;
    if (substring_position == 0) {
      score += 220;
    } else if (is_word_boundary(haystack, substring_position)) {
      score += 120;
    }
    if (needle.size() == haystack.size()) {
      score += 320;
    }
    return score;
  }

  qsizetype haystack_position = 0;
  qsizetype previous_match = -2;
  int score = 220;
  for (const QChar character : needle) {
    const qsizetype match = haystack.indexOf(character, haystack_position);
    if (match < 0) {
      return -1;
    }
    if (match == previous_match + 1) {
      score += 28;
    }
    if (is_word_boundary(haystack, match)) {
      score += 36;
    }
    score -= static_cast<int>(match - haystack_position) * 2;
    previous_match = match;
    haystack_position = match + 1;
  }
  return score;
}

struct CommandPaletteCandidate final {
  std::string id;
  QString label;
  QString category;
  QString breadcrumb;
  QString shortcut;
  QStringList search_aliases;
  QString disabled_reason;
  std::uint32_t presentation_rank = 0u;
  bool enabled = true;
};

QString candidate_location(const CommandPaletteCandidate &candidate) {
  if (candidate.breadcrumb.isEmpty()) {
    return candidate.category;
  }
  if (candidate.category.isEmpty()) {
    return candidate.breadcrumb;
  }
  return candidate.category + QStringLiteral(" › ") +
         candidate.breadcrumb;
}

int query_score(const CommandPaletteCandidate &candidate,
                const QStringList &tokens) {
  const QString label = normalized_search_text(candidate.label);
  const QString id = normalized_search_text(QString::fromStdString(candidate.id));
  const QString location = normalized_search_text(candidate_location(candidate));
  QStringList aliases;
  aliases.reserve(candidate.search_aliases.size());
  for (const QString &alias : candidate.search_aliases) {
    aliases.push_back(normalized_search_text(alias));
  }

  int total = candidate.enabled ? 80 : 0;
  for (const QString &token : tokens) {
    int best = -1;
    const int label_score = fuzzy_subsequence_score(token, label);
    if (label_score >= 0) {
      best = std::max(best, label_score + 520);
    }
    const int id_score = fuzzy_subsequence_score(token, id);
    if (id_score >= 0) {
      best = std::max(best, id_score + 260);
    }
    const int location_score = fuzzy_subsequence_score(token, location);
    if (location_score >= 0) {
      best = std::max(best, location_score + 180);
    }
    for (const QString &alias : aliases) {
      const int alias_score = fuzzy_subsequence_score(token, alias);
      if (alias_score >= 0) {
        best = std::max(best, alias_score + 400);
      }
    }
    if (best < 0) {
      return -1;
    }
    total += best;
  }
  return total;
}

} // namespace

struct QtCommandPalette::Impl final {
  struct ScoredCandidate final {
    const CommandPaletteCandidate *candidate = nullptr;
    int score = 0;
  };

  QtCommandPalette &owner;
  QuaderCommandsApi *commands = nullptr;
  LocalizationService &localization;
  MetadataResolver metadata_resolver;
  ExecutionObserver execution_observer;
  QLineEdit *search = nullptr;
  QTreeWidget *results = nullptr;
  QtStatusMessage *status = nullptr;
  std::vector<CommandPaletteCandidate> snapshot;
  std::vector<std::string> recent_command_ids;
  bool open_pending = false;

  Impl(QtCommandPalette &owner_in, QuaderCommandsApi &commands_in,
      LocalizationService &localization_in)
      : owner(owner_in), commands(&commands_in), localization(localization_in) {}

  void build_widgets() {
    owner.setObjectName(QStringLiteral("commandPalette.dialog"));
    owner.setAttribute(Qt::WA_DeleteOnClose, false);
    owner.setWindowModality(Qt::WindowModal);
    owner.setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    owner.resize(760, 460);

    auto *layout = new QVBoxLayout(&owner);
    search = new QLineEdit(&owner);
    search->setObjectName(QStringLiteral("commandPalette.search"));
    search->setClearButtonEnabled(true);
    search->setAccessibleName(
        qtext(localization, "ui.command_palette.search_accessible_name",
              "Command search"));
    layout->addWidget(search);

    results = new QTreeWidget(&owner);
    results->setObjectName(QStringLiteral("commandPalette.results"));
    results->setColumnCount(3);
    results->setRootIsDecorated(false);
    results->setItemsExpandable(false);
    results->setUniformRowHeights(true);
    results->setAlternatingRowColors(true);
    results->setAllColumnsShowFocus(true);
    results->setSelectionBehavior(QAbstractItemView::SelectRows);
    results->setSelectionMode(QAbstractItemView::SingleSelection);
    results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results->header()->setStretchLastSection(false);
    results->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    results->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    results->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(results, 1);

    status = new QtStatusMessage(&owner);
    status->setObjectName(QStringLiteral("commandPalette.status"));
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status);

    search->installEventFilter(&owner);
    results->installEventFilter(&owner);

    QObject::connect(search, &QLineEdit::textChanged, &owner,
                     [this]() { rebuild_results(); });
    QObject::connect(search, &QLineEdit::returnPressed, &owner,
                     [this]() { execute_current(); });
    QObject::connect(results, &QTreeWidget::itemActivated, &owner,
                     [this](QTreeWidgetItem *, int) { execute_current(); });
    QObject::connect(results, &QTreeWidget::currentItemChanged, &owner,
                     [this]() { refresh_status(); });

    apply_localization();
  }

  void apply_localization() {
    owner.setWindowTitle(qtext(localization, "ui.command_palette.title",
                               "Search Commands"));
    search->setPlaceholderText(
        qtext(localization, "ui.command_palette.search_placeholder",
              "Search commands by name, shortcut, or keyword"));
    search->setAccessibleName(
        qtext(localization, "ui.command_palette.search_accessible_name",
              "Command search"));
    results->setHeaderLabels(
        {qtext(localization, "ui.command_palette.column.command", "Command"),
         qtext(localization, "ui.command_palette.column.location", "Location"),
         qtext(localization, "ui.command_palette.column.shortcut", "Shortcut")});
    results->setAccessibleName(
        qtext(localization, "ui.command_palette.results_accessible_name",
              "Matching commands"));
  }

  QString localized_command_label(
      const QuaderCommandDescriptor &command,
      const std::unordered_map<std::string, QuaderKeybindingCatalogEntry>
          &keybinding_by_target) const {
    const auto found = keybinding_by_target.find(command.id);
    if (found == keybinding_by_target.end() ||
        found->second.localization_key.empty()) {
      return QString::fromStdString(command.label);
    }
    return qtext(localization, found->second.localization_key, command.label);
  }

  void capture_snapshot() {
    snapshot.clear();

    const std::vector<QuaderKeybindingCatalogEntry> keybindings =
        commands->keybinding_catalog();
    std::unordered_map<std::string, QuaderKeybindingCatalogEntry>
        keybinding_by_target;
    keybinding_by_target.reserve(keybindings.size() * 2u);
    for (const QuaderKeybindingCatalogEntry &entry : keybindings) {
      if (!entry.action_id.empty()) {
        keybinding_by_target.try_emplace(entry.action_id, entry);
      }
      if (!entry.target_id.empty()) {
        keybinding_by_target.try_emplace(entry.target_id, entry);
      }
      for (const std::string &target_id : entry.target_ids) {
        keybinding_by_target.try_emplace(target_id, entry);
      }
    }

    const std::vector<QuaderCommandDescriptor> public_commands =
        commands->available();
    snapshot.reserve(public_commands.size());
    for (const QuaderCommandDescriptor &command : public_commands) {
      const CommandPresentationDescriptor *presentation =
          find_command_presentation(command.id);
      if (presentation == nullptr ||
          presentation->palette_policy != CommandPalettePolicy::Searchable ||
          !has_command_presentation_surface(
              *presentation, CommandPresentationSurface::CommandPalette)) {
        continue;
      }

      QtCommandPaletteMetadata metadata;
      if (metadata_resolver) {
        metadata = metadata_resolver(command.id);
      }
      if (!metadata.user_facing) {
        continue;
      }

      const LocalizedPresentationName domain =
          domain_name(presentation->domain);
      const LocalizedPresentationName group = group_name(presentation->group);
      CommandPaletteCandidate candidate{
          .id = command.id,
          .label = localized_command_label(command, keybinding_by_target),
          .category = localized_presentation_name(localization, domain),
          .breadcrumb = localized_presentation_name(localization, group),
          .shortcut = QString::fromStdString(command.shortcut),
          .disabled_reason = std::move(metadata.disabled_reason),
          .presentation_rank = presentation->rank,
          .enabled = command.enabled,
      };
      if (!metadata.label.isEmpty()) {
        candidate.label = std::move(metadata.label);
      }
      if (!metadata.category.isEmpty()) {
        candidate.category = std::move(metadata.category);
      }
      if (!metadata.breadcrumb.isEmpty()) {
        candidate.breadcrumb = std::move(metadata.breadcrumb);
      }
      candidate.search_aliases = std::move(metadata.aliases);
      for (const std::string_view alias : presentation->aliases) {
        candidate.search_aliases.push_back(
            QString::fromUtf8(alias.data(), static_cast<qsizetype>(alias.size())));
      }
      for (const std::string_view keyword : presentation->keywords) {
        candidate.search_aliases.push_back(QString::fromUtf8(
            keyword.data(), static_cast<qsizetype>(keyword.size())));
      }
      snapshot.push_back(std::move(candidate));
    }
  }

  [[nodiscard]] const CommandPaletteCandidate *
  find_candidate(std::string_view command_id) const {
    const auto found = std::find_if(
        snapshot.begin(), snapshot.end(),
        [command_id](const CommandPaletteCandidate &candidate) {
          return candidate.id == command_id;
        });
    return found == snapshot.end() ? nullptr : &(*found);
  }

  QTreeWidgetItem *append_candidate(const CommandPaletteCandidate &candidate,
                                    bool recent) {
    QString location = candidate_location(candidate);
    if (recent) {
      const QString recent_label =
          qtext(localization, "ui.command_palette.recent", "Recent");
      location = recent_label + QStringLiteral(" › ") + location;
    }
    if (!candidate.enabled) {
      const QString reason = candidate.disabled_reason.isEmpty()
                                 ? qtext(localization,
                                         "ui.command_palette.unavailable",
                                         "Unavailable in the current context")
                                 : candidate.disabled_reason;
      location.append(QStringLiteral(" — "));
      location.append(reason);
    }

    auto *item = new QTreeWidgetItem(
        results, {candidate.label, location, candidate.shortcut});
    item->setData(0, kCommandIdRole, QString::fromStdString(candidate.id));
    item->setData(0, kCommandEnabledRole, candidate.enabled);
    item->setData(0, kDisabledReasonRole, candidate.disabled_reason);
    const QString accessible =
        candidate.label + QStringLiteral(", ") + location;
    item->setData(0, Qt::AccessibleTextRole, accessible);
    item->setToolTip(0, QString::fromStdString(candidate.id));
    item->setToolTip(1, location);
    if (!candidate.enabled) {
      item->setDisabled(true);
    }
    return item;
  }

  void select_first_enabled() {
    results->setCurrentItem(nullptr);
    for (int row = 0; row < results->topLevelItemCount(); ++row) {
      QTreeWidgetItem *item = results->topLevelItem(row);
      if (item->data(0, kCommandEnabledRole).toBool()) {
        results->setCurrentItem(item);
        return;
      }
    }
  }

  void rebuild_results() {
    results->setUpdatesEnabled(false);
    results->clear();

    const QString query = normalized_search_text(search->text());
    if (query.isEmpty()) {
      for (const std::string &recent_id : recent_command_ids) {
        if (const CommandPaletteCandidate *candidate =
                find_candidate(recent_id)) {
          append_candidate(*candidate, true);
        }
      }
      results->setUpdatesEnabled(true);
      select_first_enabled();
      if (results->topLevelItemCount() == 0) {
        status->present(qtext(
            localization, "ui.command_palette.empty_recents",
            "Type to search commands. Recently used commands will appear "
            "here."), false);
      } else {
        refresh_status();
      }
      return;
    }

    const QStringList tokens = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    std::vector<ScoredCandidate> matches;
    matches.reserve(snapshot.size());
    for (const CommandPaletteCandidate &candidate : snapshot) {
      const int score = query_score(candidate, tokens);
      if (score >= 0) {
        matches.push_back({.candidate = &candidate, .score = score});
      }
    }
    std::stable_sort(
        matches.begin(), matches.end(),
        [](const ScoredCandidate &left, const ScoredCandidate &right) {
          if (left.score != right.score) {
            return left.score > right.score;
          }
          if (left.candidate->presentation_rank !=
              right.candidate->presentation_rank) {
            return left.candidate->presentation_rank <
                   right.candidate->presentation_rank;
          }
          return QString::compare(left.candidate->label,
                                  right.candidate->label,
                                  Qt::CaseInsensitive) < 0;
        });
    for (const ScoredCandidate &match : matches) {
      append_candidate(*match.candidate, false);
    }

    results->setUpdatesEnabled(true);
    select_first_enabled();
    if (matches.empty()) {
      status->present(qtext(localization, "ui.command_palette.no_matches",
                            "No matching commands"), false);
    } else {
      refresh_status();
    }
  }

  void refresh_status() {
    const QTreeWidgetItem *item = results->currentItem();
    if (item == nullptr) {
      if (results->topLevelItemCount() > 0) {
        status->present(qtext(localization,
                              "ui.command_palette.no_available_matches",
                              "No matching command is currently available"), false);
      }
      return;
    }

    const QString command_id = item->data(0, kCommandIdRole).toString();
    if (item->data(0, kCommandEnabledRole).toBool()) {
      status->present(command_id, false);
      return;
    }
    QString reason = item->data(0, kDisabledReasonRole).toString();
    if (reason.isEmpty()) {
      reason = qtext(localization, "ui.command_palette.unavailable",
                     "Unavailable in the current context");
    }
    status->present(command_id + QStringLiteral(" — ") + reason, false);
  }

  void move_current(int delta) {
    const int count = results->topLevelItemCount();
    if (count == 0 || delta == 0) {
      return;
    }
    int row = results->indexOfTopLevelItem(results->currentItem());
    if (row < 0) {
      row = delta > 0 ? -1 : count;
    }
    for (int step = 0; step < count; ++step) {
      row = (row + delta + count) % count;
      QTreeWidgetItem *item = results->topLevelItem(row);
      if (item->data(0, kCommandEnabledRole).toBool()) {
        results->setCurrentItem(item);
        results->scrollToItem(item);
        return;
      }
    }
  }

  void remember_recent(std::string command_id) {
    const auto found = std::find(recent_command_ids.begin(),
                                 recent_command_ids.end(), command_id);
    if (found != recent_command_ids.end()) {
      recent_command_ids.erase(found);
    }
    recent_command_ids.insert(recent_command_ids.begin(),
                              std::move(command_id));
    if (recent_command_ids.size() > kRecentCommandLimit) {
      recent_command_ids.resize(kRecentCommandLimit);
    }
  }

  void execute_current() {
    QTreeWidgetItem *item = results->currentItem();
    if (item == nullptr ||
        !item->data(0, kCommandEnabledRole).toBool()) {
      refresh_status();
      QApplication::beep();
      return;
    }

    const std::string command_id =
        item->data(0, kCommandIdRole).toString().toStdString();
    owner.accept();
    QTimer::singleShot(0, &owner, [this, command_id]() {
      const QuaderApiVoidResult result = commands->execute(command_id);
      if (result.ok()) {
        remember_recent(command_id);
        if (execution_observer) {
          execution_observer(command_id);
        }
        return;
      }

      // Execution is deliberately deferred until after the activation signal
      // stack unwinds. A failed request can safely restore the retained dialog
      // without taking another broad command snapshot.
      owner.open();
      owner.raise();
      owner.activateWindow();
      search->setFocus(Qt::ShortcutFocusReason);
      QString message =
          qtext(localization, "ui.command_palette.execution_failed",
                "Command could not be executed");
      if (!result.error_message().empty()) {
        message.append(QStringLiteral(": "));
        message.append(QString::fromStdString(result.error_message()));
      }
      // Command status owns the one failure announcement in the main window.
      status->present(message, true);
    });
  }

  bool filter_key_event(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress) {
      return false;
    }
    auto *key_event = static_cast<QKeyEvent *>(event);
    if (key_event->key() == Qt::Key_Escape) {
      owner.reject();
      return true;
    }
    if (watched == search) {
      if (key_event->key() == Qt::Key_Down) {
        move_current(1);
        return true;
      }
      if (key_event->key() == Qt::Key_Up) {
        move_current(-1);
        return true;
      }
      return false;
    }
    if (watched != results) {
      return false;
    }

    if (key_event->key() == Qt::Key_Backspace) {
      search->setFocus(Qt::ShortcutFocusReason);
      search->backspace();
      return true;
    }
    const QString text = key_event->text();
    if (!text.isEmpty() &&
        (key_event->modifiers() &
         (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) == 0) {
      search->setFocus(Qt::ShortcutFocusReason);
      search->insert(text);
      return true;
    }
    return false;
  }
};

QtCommandPalette::QtCommandPalette(QuaderCommandsApi &commands,
                                   LocalizationService &localization,
                                   QWidget *parent)
    : QDialog(parent, Qt::Dialog),
      impl_(std::make_unique<Impl>(*this, commands, localization)) {
  impl_->build_widgets();
}

QtCommandPalette::~QtCommandPalette() = default;

void QtCommandPalette::rebind_commands(QuaderCommandsApi &commands) noexcept {
  impl_->commands = &commands;
}

void QtCommandPalette::refresh_localization() {
  impl_->apply_localization();
  if (isVisible()) {
    impl_->capture_snapshot();
    impl_->rebuild_results();
  }
}

void QtCommandPalette::set_metadata_resolver(MetadataResolver resolver) {
  impl_->metadata_resolver = std::move(resolver);
}

void QtCommandPalette::set_execution_observer(ExecutionObserver observer) {
  impl_->execution_observer = std::move(observer);
}

void QtCommandPalette::open_palette() {
  if (impl_->open_pending) {
    return;
  }
  impl_->open_pending = true;
  QTimer::singleShot(0, this, [this]() {
    impl_->open_pending = false;
    impl_->capture_snapshot();
    {
      const QSignalBlocker blocker(impl_->search);
      impl_->search->clear();
    }
    impl_->rebuild_results();
    open();
    raise();
    activateWindow();
    impl_->search->setFocus(Qt::ShortcutFocusReason);
  });
}

bool QtCommandPalette::eventFilter(QObject *watched, QEvent *event) {
  if (impl_->filter_key_event(watched, event)) {
    return true;
  }
  return QDialog::eventFilter(watched, event);
}

} // namespace quader
