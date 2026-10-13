#pragma once

#include "Tools.h"
#include <array>
#include <vector>

namespace crimson {
class AssetBrowserPanel {
public:
    BrowserActions draw(AssetLibrary& assets, SDL_Window* window);
private:
    const AssetLibrary* library_{};
    std::string folder_, renameId_, message_, deferredClick_, dragPrimary_;
    std::array<char,512> renameText_{};
    std::vector<std::string> dragIds_;
    std::vector<char> dragPayload_;
    bool focusRename_{}, dragModelsBatch_{}, deferredControl_{}, deferredShift_{};
    void beginRename(const std::string& id, const std::string& name);
};
} // namespace crimson
