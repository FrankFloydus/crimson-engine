////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_contextual_input_hints.hpp"
#include "ui/qt_viewport_control_strip.hpp"

#include <QApplication>
#include <QDropEvent>
#include <QEvent>
#include <QHash>
#include <QInputEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <memory>
#include <utility>

namespace quader {
namespace {

constexpr auto kRegistryName = "quader.contextual_input_hints";

class ContextualHintSource final : public QObject {
public:
  struct Callbacks {
    QtContextualInputHintObserver observe;
    QtContextualInputHintBuilder build;
  };

  ContextualHintSource(QObject &surface, QObject &lifetime, std::uint64_t identity,
                       QtContextualInputHintObserver observer,
                       QtContextualInputHintBuilder builder)
      : QObject(&lifetime), identity(identity), surface(&surface),
        callbacks(std::make_shared<Callbacks>(std::move(observer),
                                               std::move(builder))) {}

  std::uint64_t identity = 0;
  QPointer<QObject> surface;
  std::shared_ptr<const Callbacks> callbacks;
};

bool text_editor(QObject *object) {
  return qobject_cast<QLineEdit *>(object) ||
         qobject_cast<QTextEdit *>(object) ||
         qobject_cast<QPlainTextEdit *>(object);
}

bool visible_surface(const QObject *surface) {
  if (const auto *widget = qobject_cast<const QWidget *>(surface)) {
    return widget->isVisible() && widget->isEnabled();
  }
  if (const auto *window = qobject_cast<const QWindow *>(surface)) {
    return window->isVisible();
  }
  return false;
}

bool modal_contains_surface(const QWidget &modal, const QObject *surface) {
  if (const auto *widget = qobject_cast<const QWidget *>(surface)) {
    return widget == &modal || modal.isAncestorOf(widget);
  }
  const auto *window = qobject_cast<const QWindow *>(surface);
  const auto *modal_window = modal.windowHandle();
  return window && modal_window &&
         (window == modal_window ||
          modal_window->isAncestorOf(window, QWindow::IncludeTransients));
}

class ContextualHintRegistry final : public QObject {
public:
  explicit ContextualHintRegistry(QApplication &application)
      : QObject(&application) {
    setObjectName(QString::fromLatin1(kRegistryName));
    application.installEventFilter(this);
  }

  void add(QObject &surface, QObject &lifetime,
           QtContextualInputHintObserver observe,
           QtContextualInputHintBuilder build) {
    if (auto previous = sources_.value(&surface)) {
      delete previous;
    }
    auto *source = new ContextualHintSource(
        surface, lifetime, ++source_identity_, std::move(observe), std::move(build));
    sources_.insert(&surface, source);
    connect(source, &QObject::destroyed, this, [this, key = &surface]() {
      // QPointer is already clear when destroyed is emitted.
      if (!sources_.value(key)) {
        sources_.remove(key);
      }
    });
    if (&surface != &lifetime) {
      connect(&surface, &QObject::destroyed, source,
              [source]() { delete source; });
    }
  }

  void set_refresh_callback(QObject &lifetime, std::function<void()> refresh) {
    refresh_lifetime_ = &lifetime;
    refresh_ = std::move(refresh);
  }

  QtContextualInputHintState state() {
    QPointer<ContextualHintSource> candidate;
    QtContextualInputHintSourceState observation;
    bool overridden = false;
    const auto accept = [&](ContextualHintSource *source, bool active_only) {
      if (!source || !source->surface || !source->callbacks->observe ||
          !visible_surface(source->surface)) {
        return false;
      }
      const QPointer<ContextualHintSource> guard(source);
      const auto callbacks = source->callbacks;
      const auto next = callbacks->observe(modifiers_);
      if (!guard || !next.available || (active_only && !next.active)) {
        return false;
      }
      candidate = source;
      observation = next;
      overridden = true;
      return true;
    };

    const QWidget *popup = QApplication::activePopupWidget();
    const bool viewport_popup = popup != nullptr &&
        qt_viewport_control_strip_ancestor(popup) != nullptr;
    if (!application_inactive_ && (popup == nullptr || viewport_popup)) {
      if (viewport_popup) {
        // Viewport menus retain that pane's hints. Text editors and modal
        // dialogs still suppress viewport context, as elsewhere in the app.
        overridden = text_editor(QApplication::focusWidget());
      } else if (native_drag_) {
        // A native drag's receiving surface, not its source mouse capture,
        // owns drop intent while crossing browser/panel boundaries.
        captured_.clear();
        accept(source_for(pointer_target_), false);
        overridden = true;
      } else if (!accept(captured_, true)) {
        captured_.clear();
        QObject *focus = QApplication::focusWidget();
        if (!focus) {
          focus = QGuiApplication::focusWindow();
        }
        auto *focused = source_for(focus);
        if (!accept(focused, true)) {
          if (text_editor(focus)) {
            overridden = true;
          } else {
            accept(source_for(pointer_target_), false);
          }
        }
      }
      // A modal dialog can provide its own hints, but cannot expose the
      // viewport behind it through a stale hovered surface.
      if (auto *modal = QApplication::activeModalWidget()) {
        if (!candidate || !modal_contains_surface(*modal, candidate->surface)) {
          candidate.clear();
          observation = {};
        }
        overridden = true;
      }
    } else {
      overridden = true;
    }
    if (candidate && observation.active) {
      captured_ = candidate;
    }
    const Stamp next{candidate ? candidate->identity : 0,
                     observation.revision, modifiers_, overridden,
                     candidate && observation.active};
    if (!(next == stamp_)) {
      stamp_ = next;
      selected_ = candidate;
      ++revision_;
    }
    return {this, revision_, overridden, next.active, overridden && !candidate};
  }

  std::span<const QuaderOperationInputHint> hints() {
    state();
    if (built_revision_ != revision_) {
      hints_.clear();
      const QPointer<ContextualHintSource> source = selected_;
      const auto generation = revision_;
      if (source && source->callbacks->build) {
        const auto callbacks = source->callbacks;
        auto next = callbacks->build(modifiers_);
        if (source && generation == revision_) {
          hints_ = std::move(next);
        }
      }
      built_revision_ = generation;
    }
    return hints_;
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    bool relevant = true;
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonRelease:
    case QEvent::Wheel:
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
      native_drag_ = false;
      latch_active_source();
      route_pointer_event(watched, static_cast<QSinglePointEvent *>(event)
                                       ->globalPosition().toPoint());
      application_inactive_ = false;
      modifiers_ = static_cast<QInputEvent *>(event)->modifiers();
      break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
      modifiers_ = static_cast<QInputEvent *>(event)->modifiers();
      if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        native_drag_ = false;
      }
      break;
    case QEvent::Enter:
      latch_active_source();
      pointer_target_ = watched;
      break;
    case QEvent::DragEnter:
    case QEvent::DragMove:
      native_drag_ = true;
      captured_.clear();
      pointer_target_ = watched;
      modifiers_ = static_cast<QDropEvent *>(event)->modifiers();
      break;
    case QEvent::Drop:
      native_drag_ = false;
      pointer_target_ = watched;
      modifiers_ = static_cast<QDropEvent *>(event)->modifiers();
      break;
    case QEvent::Leave:
    case QEvent::DragLeave:
    case QEvent::Hide:
      if (pointer_target_ == watched) {
        pointer_target_.clear();
      }
      break;
    case QEvent::ApplicationDeactivate:
      application_inactive_ = true;
      pointer_target_.clear();
      captured_.clear();
      modifiers_ = Qt::NoModifier;
      native_drag_ = false;
      break;
    case QEvent::ApplicationActivate:
      application_inactive_ = false;
      break;
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::UngrabMouse:
    case QEvent::WindowDeactivate:
      break;
    default:
      relevant = false;
      break;
    }
    if (relevant && !refresh_queued_ && refresh_lifetime_ && refresh_) {
      refresh_queued_ = true;
      QTimer::singleShot(0, this, [this]() {
        refresh_queued_ = false;
        if (refresh_lifetime_ && refresh_) {
          const auto callback = refresh_;
          callback();
        }
      });
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void route_pointer_event(QObject *watched, QPoint global_position) {
    // Ignored child mouse moves propagate through parent widgets. That routing
    // must not replace the real hovered child with its enclosing dock surface.
    const auto *previous = qobject_cast<QWidget *>(pointer_target_.data());
    const auto *next = qobject_cast<QWidget *>(watched);
    if (previous && next && previous->isVisible() &&
        next->isAncestorOf(previous) &&
        previous->rect().contains(previous->mapFromGlobal(global_position))) {
      return;
    }
    pointer_target_ = watched;
  }

  void latch_active_source() {
    if (captured_) {
      return;
    }
    const QPointer<ContextualHintSource> source = source_for(pointer_target_);
    if (source && source->surface && visible_surface(source->surface) &&
        source->callbacks->observe) {
      const auto callbacks = source->callbacks;
      const auto state = callbacks->observe(modifiers_);
      if (source && state.available && state.active) {
        captured_ = source;
      }
    }
  }

  ContextualHintSource *source_for(QObject *object) const {
    for (auto *ancestor = object; ancestor; ancestor = ancestor->parent()) {
      if (auto source = sources_.value(ancestor)) {
        return source;
      }
    }
    return nullptr;
  }

  struct Stamp {
    std::uint64_t source = 0;
    std::uint64_t source_revision = 0;
    Qt::KeyboardModifiers modifiers;
    bool overridden = false;
    bool active = false;
    bool operator==(const Stamp &) const = default;
  };

  std::uint64_t source_identity_ = 0;
  QHash<QObject *, QPointer<ContextualHintSource>> sources_;
  QPointer<QObject> pointer_target_;
  QPointer<ContextualHintSource> captured_;
  QPointer<ContextualHintSource> selected_;
  Qt::KeyboardModifiers modifiers_;
  bool application_inactive_ = false;
  bool native_drag_ = false;
  QPointer<QObject> refresh_lifetime_;
  std::function<void()> refresh_;
  bool refresh_queued_ = false;
  Stamp stamp_;
  std::uint64_t revision_ = 1;
  std::uint64_t built_revision_ = 0;
  std::vector<QuaderOperationInputHint> hints_;
};

ContextualHintRegistry &registry(QApplication &application) {
  // QApplication is unique, while tests may destroy and recreate it. QPointer
  // makes that lifetime transition explicit without a per-frame QObject scan.
  static QPointer<ContextualHintRegistry> current;
  if (!current || current->parent() != &application) {
    current = new ContextualHintRegistry(application);
  }
  return *current;
}

} // namespace

void qt_register_contextual_input_hints(
    QObject &surface, QObject &lifetime, QtContextualInputHintObserver observe,
    QtContextualInputHintBuilder build) {
  if (auto *application =
          qobject_cast<QApplication *>(QCoreApplication::instance())) {
    registry(*application).add(surface, lifetime, std::move(observe),
                               std::move(build));
  }
}

QtContextualInputHintState
qt_contextual_input_hints_state(QApplication &application) {
  return registry(application).state();
}

void qt_set_contextual_input_hints_refresh_callback(
    QApplication &application, QObject &lifetime, std::function<void()> refresh) {
  registry(application).set_refresh_callback(lifetime, std::move(refresh));
}

std::span<const QuaderOperationInputHint>
qt_contextual_input_hints(QApplication &application) {
  return registry(application).hints();
}

} // namespace quader
