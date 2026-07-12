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

struct UIContext;

/**
 * Presents scene, component-selection, transform, and component properties
 * while routing every mutation through editor-owned APIs and services.
 */
class InspectorPanel final : public QWidget {
public:
  explicit InspectorPanel(QWidget *parent = nullptr);
  ~InspectorPanel() override;

  void refresh(UIContext &context);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
