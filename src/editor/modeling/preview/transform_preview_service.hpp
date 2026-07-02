////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <editor/modeling/runtime/session.hpp>

namespace quader {

/**
 * Coordinates Transform Preview Service responsibilities for the modeling domain.
 */
class TransformPreviewService {
public:
  [[nodiscard]] static bool is_transform(ModelingTransformOperation operation);

  [[nodiscard]] static bool
  has_transform_preview(const quader_editor_core::EditorSession &session);
  [[nodiscard]] static ModelingCommandResult finish_result(
      const quader_editor_core::PreviewTransactionFinishResult &finish,
      bool cancel);
};

} // namespace quader
