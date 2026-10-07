////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_material_presentation.hpp"

#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QEvent;

namespace quader {

class QtUiScaleMetrics;
struct UIContext;

/**
 * Event-driven material presentation supplied by UISystem.
 *
 * The Inspector deliberately receives owner-projected identities, names, and
 * cached thumbnails. It never traverses modeling topology or enumerates asset
 * catalogs while refreshing retained controls.
 */
struct InspectorMaterialPresentationSnapshot final {
  std::uint64_t revision = 0;
  bool section_visible = false;
  std::vector<QtMaterialPresentationItem> materials;
  std::function<void(QuaderMaterialReference)> edit_material;
  std::function<void(QuaderMaterialReference)> replace_material;
};

/**
 * Presents object, component-selection, transform, and component properties
 * while routing every mutation through editor-owned APIs and services.
 */
class InspectorPanel final : public QWidget {
public:
  explicit InspectorPanel(QWidget *parent = nullptr);
  ~InspectorPanel() override;

  void set_material_presentation(
      InspectorMaterialPresentationSnapshot snapshot);
  void abandon_runtime() noexcept;
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  void refresh(UIContext &context);
  /// Presents live transform previews without rebuilding unrelated properties.
  void refresh_transform_preview(UIContext &context);
  /// Accepts current property drafts before an editor mode transition.
  [[nodiscard]] bool settle_pending_edits();

protected:
  bool event(QEvent *event) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
