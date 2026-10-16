#pragma once
#include "assets/BrowserCatalog.h"
#include <filesystem>
#include <string>

namespace crimson {
struct BrowserAssetLocation {
    std::filesystem::path file,directory;
    std::string name;
    bool available{};
};
// Stable asset identities resolve through the physical Content/Engine index.
// Models reveal their retained source document rather than manifest.json.
BrowserAssetLocation resolveBrowserAssetLocation(const std::filesystem::path& projectRoot,const AssetEntry&);
bool openBrowserAssetLocation(const BrowserAssetLocation&,std::string& diagnostic);
} // namespace crimson
