////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QObject>
#include <QPointer>

#include <cstdint>
#include <vector>

class QApplication;
class QEvent;
class QWidget;

namespace quader {

/// Prevents native viewport submission from competing with the first paint of
/// a newly shown Qt dialog or popup. Rendering resumes immediately after that
/// paint, including while a modal nested event loop remains active.
class AppFramePresentationGate final : public QObject {
public:
  explicit AppFramePresentationGate(QApplication &application,
                                    QObject *parent = nullptr);
  ~AppFramePresentationGate() override;

  [[nodiscard]] bool blocks_frame_submission() const noexcept;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  /**
   * Tracks one transient that has been shown but has not completed its first
   * paint event.
   */
  struct PendingTransient final {
    QPointer<QWidget> widget;
    std::uint64_t generation = 0u;
    bool release_queued = false;
  };

  [[nodiscard]] static bool is_top_level_transient(const QWidget &widget);
  void begin_first_paint_wait(QWidget &widget);
  void queue_first_paint_release(QWidget &widget);
  void release_if_current(QWidget *widget, std::uint64_t generation);
  void forget_transient(const QObject *widget);

  QPointer<QApplication> application_;
  std::vector<PendingTransient> pending_transients_;
  std::uint64_t next_generation_ = 1u;
};

} // namespace quader
