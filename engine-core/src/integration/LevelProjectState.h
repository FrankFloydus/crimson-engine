#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace crimson::integration {
// Project discovery never creates a modeler manifest or modifies Engine data.
std::optional<std::filesystem::path> findLevelProjectRoot(const std::filesystem::path& start);
std::optional<std::filesystem::path> readLevelProjectSelection(const std::filesystem::path& file);
void writeLevelProjectSelection(const std::filesystem::path& file,const std::filesystem::path& root);

// Retains the original Engine v1 editor-session scene and free-camera map.
// Construction and lookups are read-only; accepted user commands save explicitly.
class LevelProjectState final {
public:
    explicit LevelProjectState(const std::filesystem::path& root);
    const std::string& scene()const{return scene_;}
    std::optional<Camera> camera(const std::string&)const;
    void remember(const std::string&,const Camera&);
    bool save(const std::string& activeScene);
    const std::string& status()const{return status_;}
private:
    std::filesystem::path path_;
    nlohmann::json saved_=nlohmann::json::object();
    std::map<std::string,Camera> cameras_;
    std::string scene_,status_;
    bool writable_{true},changed_{};
};
}
