////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_texture_toolbar.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"

#include <QAction>
#include <QIcon>
#include <QSignalBlocker>
#include <QSize>
#include <QStyle>
#include <QToolButton>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace quader {
namespace {

constexpr int kTopToolbarButtonHeight = 30;

/// Defines the two stable texture-lock actions and their original icon assets.
struct TextureSpec {
  QuaderCommandId command_id;
  const char *icon_path;
  const char *object_name;
  std::string_view localization_key;
  std::string_view fallback;
};

constexpr std::array<TextureSpec, 2> kTextureSpecs{{
    {quader_command::kViewportToggleTextureLock,
     ":/quader/icons/texture_lock.png", "textureLock",
     "ui.toolbar.texture.lock", "Texture Lock"},
    {quader_command::kViewportToggleTextureLockScale,
     ":/quader/icons/texture_lock_scale.png", "textureLockScale",
     "ui.toolbar.texture.lock_scale", "Texture Lock Scale"},
}};

QString localized_text(const LocalizationService &localization,
                       std::string_view key, std::string_view fallback) {
  return QString::fromStdString(localization.text(key, fallback));
}

} // namespace

QtTextureToolbar::QtTextureToolbar(QWidget *parent) : QToolBar(parent) {
  setObjectName(QStringLiteral("textureToolbar"));
  setWindowTitle(QStringLiteral("Texturing"));
  setAllowedAreas(Qt::AllToolBarAreas);
  setMovable(true);
  setFloatable(true);
  setIconSize(QSize(32, 32));
  setToolButtonStyle(Qt::ToolButtonIconOnly);
  addSeparator()->setObjectName(QStringLiteral("textureToolbarDivider"));

  for (std::size_t index = 0; index < kTextureSpecs.size(); ++index) {
    const TextureSpec &spec = kTextureSpecs[index];
    QAction *action = addAction(
        QIcon(QString::fromUtf8(spec.icon_path)),
        QString::fromUtf8(spec.fallback.data(),
                          static_cast<qsizetype>(spec.fallback.size())));
    action->setObjectName(QString::fromUtf8(spec.object_name));
    action->setCheckable(true);
    QObject::connect(action, &QAction::triggered, this,
                     [this, command_id = spec.command_id](bool) {
                       presented_state_.reset();
                       if (command_requested_ != nullptr) {
                         command_requested_(command_id);
                       }
                     });
    texture_actions_[index] = {
        .action = action,
        .command_id = spec.command_id,
        .localization_key = spec.localization_key,
        .fallback = spec.fallback,
    };
  }

  QStyle &button_presentation =
      install_native_toolbar_button_presentation(*this);
  for (const TextureAction &entry : texture_actions_) {
    configure_native_toolbar_button_face(*this, *entry.action,
                                         button_presentation);
    if (auto *button =
            qobject_cast<QToolButton *>(widgetForAction(entry.action))) {
      button->setFixedHeight(kTopToolbarButtonHeight);
    }
  }
}

QSize QtTextureToolbar::minimumSizeHint() const {
  // Qt's stock minimum reserves only the first item, which is our divider.
  // Keep both locks visible when a restored neighbor prefers a wider row.
  return QToolBar::sizeHint();
}

void QtTextureToolbar::set_localization(
    const LocalizationService &localization) {
  setWindowTitle(localized_text(localization, "ui.toolbar.texture.title",
                                "Texturing"));
  for (const TextureAction &entry : texture_actions_) {
    const QString text = localized_text(localization, entry.localization_key,
                                        entry.fallback);
    if (entry.action->text() != text) {
      entry.action->setText(text);
    }
    if (entry.action->toolTip() != text) {
      entry.action->setToolTip(text);
    }
  }
  presented_state_.reset();
}

void QtTextureToolbar::synchronize(const QuaderEditorApi &editor, bool force) {
  const auto &preferences = editor.preferences();
  const std::array<bool, 2> state{
      preferences.texture_lock_enabled(),
      preferences.texture_lock_scale_enabled(),
  };
  const std::uint64_t keybindings_revision = preferences.keybindings_revision();
  // The frame path reads only two owner booleans and one binding stamp.
  // Descriptor resolution, shortcut formatting and Qt setters follow this gate.
  if (!force && presented_state_.has_value() && *presented_state_ == state &&
      presented_keybindings_revision_ == keybindings_revision) {
    return;
  }

  for (std::size_t index = 0; index < texture_actions_.size(); ++index) {
    const TextureAction &entry = texture_actions_[index];
    if (entry.action->isChecked() != state[index]) {
      const QSignalBlocker blocker(entry.action);
      entry.action->setChecked(state[index]);
    }
    const std::optional<QuaderCommandDescriptor> command =
        editor.commands().descriptor(entry.command_id);
    const bool enabled = command.has_value() && command->enabled;
    if (entry.action->isEnabled() != enabled) {
      entry.action->setEnabled(enabled);
    }
    QString tooltip = entry.action->text();
    const std::string shortcut =
        editor.commands().shortcut_for(entry.command_id);
    if (!shortcut.empty()) {
      tooltip.append(QStringLiteral(" [%1]")
                         .arg(QString::fromStdString(shortcut)));
    }
    if (entry.action->toolTip() != tooltip) {
      entry.action->setToolTip(tooltip);
    }
  }
  presented_state_ = state;
  presented_keybindings_revision_ = keybindings_revision;
}

void QtTextureToolbar::set_command_requested_callback(
    std::function<void(QuaderCommandId)> callback) {
  command_requested_ = std::move(callback);
}

} // namespace quader
