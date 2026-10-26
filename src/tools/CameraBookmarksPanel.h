#pragma once

#include "editor/CameraBookmarks.h"
#include <array>

namespace crimson {
class CameraBookmarksPanel {
public:
    std::optional<Camera> draw(CameraBookmarks&,const std::string& scene,const Camera& current,bool enabled,bool& visible);
private:
    std::array<char,CameraBookmarks::MaximumNameLength+1> name_{},rename_{};
    std::string scene_;
    std::uint64_t renameId_{};
};
}
