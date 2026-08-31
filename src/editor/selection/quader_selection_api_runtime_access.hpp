////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

class EditorSelectionService;
class QuaderSelectionApi;

/**
 * Gives native editor owners access to the canonical selection authority
 * without adding editor-only hit or baseline types to the public API surface.
 */
class QuaderSelectionApiRuntimeAccess final {
public:
  [[nodiscard]] static EditorSelectionService &
  service(QuaderSelectionApi &api);
  [[nodiscard]] static const EditorSelectionService &
  service(const QuaderSelectionApi &api);
};

} // namespace quader
