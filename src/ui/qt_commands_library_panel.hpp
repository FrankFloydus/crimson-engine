////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <cstdint>
#include <memory>

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class QtUiScaleMetrics;

/** Bounded presentation work, including the retained unchanged-frame gate. */
struct QtCommandsLibraryWorkStats final {
  std::uint64_t catalog_projection_count = 0u;
  std::uint64_t descriptor_query_count = 0u;
  std::uint64_t presentation_count = 0u;
  friend bool operator==(const QtCommandsLibraryWorkStats &,
                         const QtCommandsLibraryWorkStats &) = default;
};

/** Projects mode-compatible commands through the canonical editor dispatcher. */
class QtCommandsLibraryPanel final : public QWidget {
public:
  explicit QtCommandsLibraryPanel(QWidget *parent = nullptr);
  ~QtCommandsLibraryPanel() override;

  QtCommandsLibraryPanel(const QtCommandsLibraryPanel &) = delete;
  QtCommandsLibraryPanel &operator=(const QtCommandsLibraryPanel &) = delete;

  void bind(QuaderEditorApi &editor);
  void unbind();
  void set_localization(const LocalizationService &localization);
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void refresh();
  [[nodiscard]] const QtCommandsLibraryWorkStats &work_stats() const;
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
