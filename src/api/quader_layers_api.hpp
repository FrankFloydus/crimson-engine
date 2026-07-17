////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_api_snapshot.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/**
 * Exposes undoable scene display-layer workflows through the public API.
 */
class QuaderLayersApi final {
public:
  QuaderLayersApi(QuaderApiSession session,
                  QuaderApiMutationRunner &mutations);

  [[nodiscard]] std::vector<QuaderLayerSnapshot> all() const;
  [[nodiscard]] std::optional<QuaderLayerSnapshot>
  layer(QuaderLayerId id) const;
  [[nodiscard]] std::uint64_t revision() const;

  [[nodiscard]] bool can_create_from_selection() const;
  [[nodiscard]] bool can_add_selection_to(QuaderLayerId id) const;
  [[nodiscard]] bool can_remove_selection(QuaderLayerId id) const;
  [[nodiscard]] bool can_remove_selection_from_all_layers() const;

  QuaderApiResult<QuaderLayerSnapshot>
  create(std::string name = {},
         QVec3 wireframe_color = kDefaultQuaderLayerWireframeColor);
  QuaderApiResult<QuaderLayerSnapshot>
  create_from_selection(
      std::string name = {},
      QVec3 wireframe_color = kDefaultQuaderLayerWireframeColor);
  QuaderApiVoidResult rename(QuaderLayerId id, std::string name);
  QuaderApiVoidResult set_visible(QuaderLayerId id, bool visible);
  QuaderApiVoidResult set_wireframe_color(QuaderLayerId id, QVec3 color);
  QuaderApiVoidResult set_locked(QuaderLayerId id, bool locked);
  QuaderApiVoidResult add_selection(QuaderLayerId id);
  QuaderApiVoidResult remove_selection(QuaderLayerId id);
  QuaderApiVoidResult remove_selection_from_all_layers();
  QuaderApiVoidResult empty(QuaderLayerId id);
  QuaderApiVoidResult destroy(QuaderLayerId id);
  QuaderApiVoidResult destroy(std::span<const QuaderLayerId> ids);

private:
  QuaderApiSession session_;
  QuaderApiMutationRunner &mutations_;
};

} // namespace quader
