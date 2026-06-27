////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_revision.hpp"
#include "core/types.hpp"

#include <string>

namespace quader {

template <typename Revision>
/**
 * Represents an Asset Document Base value used by the asset document and registry layer.
 */
struct AssetDocumentBase {
    AssetId id = InvalidAssetId;
    Revision revision;
    std::string name;
};

} // namespace quader
