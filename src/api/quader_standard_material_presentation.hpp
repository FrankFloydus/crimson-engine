////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_standard_material_documents_api.hpp"

namespace quader {
struct ProjectStandardMaterialDefinition;

/// Projects saved and draft Standard Materials through one public mapping.
[[nodiscard]] QuaderStandardMaterialDefinition
quader_standard_material_definition(const ProjectStandardMaterialDefinition &material);
} // namespace quader
