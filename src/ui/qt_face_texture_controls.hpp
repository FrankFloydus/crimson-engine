////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <memory>

namespace quader {

class LocalizationService;
class QuaderEditorApi;
class QtUiScaleMetrics;

/** Presents retained face texture numeric fields and actions in Face Texture. */
class QtFaceTextureControls final : public QWidget {
public:
  explicit QtFaceTextureControls(QWidget *parent = nullptr);
  ~QtFaceTextureControls() override;

  QtFaceTextureControls(const QtFaceTextureControls &) = delete;
  QtFaceTextureControls &operator=(const QtFaceTextureControls &) = delete;

  void bind(QuaderEditorApi &editor);
  /** Drops pending edits without calling the outgoing runtime. */
  void unbind();
  void abandon_runtime_binding();
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void set_localization(const LocalizationService &localization);
  /** Checks owner revisions before querying or presenting face settings. */
  void refresh();
  [[nodiscard]] bool numeric_interaction_active() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
