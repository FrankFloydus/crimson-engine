////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "core/types.hpp"
#include "io/project/project_types.hpp"

#include <cstdint>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/// Selects the semantic material-use universe queried or mutated by the API.
enum class QuaderMaterialUsageScope : std::uint8_t {
  Selection,
  ActiveScene,
};

/** Public material-reference intent surface for the active editor scene. */
class QuaderMaterialsApi final {
public:
  QuaderMaterialsApi(QuaderApiSession session,
                     QuaderApiMutationRunner &mutations);

  /** Returns distinct references resolved by live faces in the requested scope. */
  [[nodiscard]] QuaderApiResult<std::vector<QuaderMaterialReference>>
  used_materials(QuaderMaterialUsageScope scope) const;

  /** Rewrites matching references as one atomic, undoable editor operation. */
  QuaderApiVoidResult replace_material_references(
      QuaderMaterialReference source, QuaderMaterialReference replacement,
      QuaderMaterialUsageScope scope);

  void rebind_session(QuaderApiSession session) { session_ = session; }

private:
  QuaderApiSession session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
};

} // namespace quader
