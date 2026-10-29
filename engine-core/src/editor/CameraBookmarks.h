#pragma once

#include "crimson-renderer/RenderTypes.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace crimson {
struct CameraBookmark {
    std::uint64_t id{};
    std::string name;
    Camera camera;
};

// Project-local editor views, keyed by saved scene identity. Loading never
// writes a scene or this metadata; mutations publish one atomic replacement.
class CameraBookmarks {
public:
    static constexpr std::size_t MaximumPerScene=64;
    static constexpr std::size_t MaximumNameLength=96;
    explicit CameraBookmarks(const std::filesystem::path& projectRoot);
    std::span<const CameraBookmark> views(const std::string& scene) const;
    std::optional<Camera> recall(const std::string& scene,std::uint64_t id) const;
    bool add(const std::string& scene,std::string name,const Camera&);
    bool update(const std::string& scene,std::uint64_t id,const Camera&);
    bool rename(const std::string& scene,std::uint64_t id,std::string name);
    bool remove(const std::string& scene,std::uint64_t id);
    bool reload();
    bool writable() const noexcept{return writable_;}
    const std::string& status() const noexcept{return status_;}
    static bool validCamera(const Camera&);
private:
    using Scenes=std::map<std::string,std::vector<CameraBookmark>>;
    std::filesystem::path path_;
    Scenes scenes_;
    std::uint64_t nextId_{1};
    std::optional<std::string> publishedText_;
    bool writable_{true};
    std::string status_;
    bool publish(Scenes,std::uint64_t next);
    bool checkScene(const std::string&);
    bool checkName(const std::vector<CameraBookmark>&,std::uint64_t,std::string&);
};
}
