#pragma once

#include "CaptureController.h"
#include "crimson-renderer/GraphicsBackend.h"
#include <array>
#include <limits>

namespace crimson {
struct SceneSettings;
struct ResourceStats;
class SceneDocument;
namespace profiling {
struct EngineProfileOptions {
    std::filesystem::path projectRoot,toolsDirectory,outputDirectory;
    std::string mode;
    std::uint32_t durationSeconds{15};
};

// Host metadata and capture policy. The controller owns collector/process
// lifetime; this adapter never changes simulation, cache contents or rendering.
class EngineProfile final {
public:
    explicit EngineProfile(const EngineProfileOptions&);
    const std::filesystem::path& projectRoot() const noexcept { return root_; }
    CaptureController& captures() noexcept { return captures_; }
    void projectChanged(const std::filesystem::path&);
    void backendInitialized(GraphicsBackend);
    void backendRetired()noexcept{backendReady_=false;}
    bool observing(bool playing=false) const;
    void frame(CaptureFrame,const SceneSettings*,const ResourceStats*,const SceneDocument* = nullptr);
private:
    std::filesystem::path root_;
    CaptureContext context_;
    CaptureController captures_;
    bool gameplayArmed_{},editorArmed_{};
    std::uint32_t captureDuration_{10};
    bool outputOverridden_{},backendReady_{};
    std::uint32_t lastGpuFrame_{std::numeric_limits<std::uint32_t>::max()};
    std::chrono::steady_clock::time_point lastContext_{};
};
} // namespace profiling
} // namespace crimson
