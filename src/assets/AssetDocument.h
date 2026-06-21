#pragma once

#include "assets/AssetRevision.h"
#include "core/Types.h"

#include <string>

namespace quader {

template <typename Revision>
struct AssetDocumentBase {
    AssetId id = InvalidAssetId;
    Revision revision;
    std::string name;
};

} // namespace quader
