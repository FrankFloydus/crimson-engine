#pragma once

#include "CaptureController.h"
#include "crimson-renderer/GraphicsBackend.h"
#include <array>
#include <limits>

namespace crimson {
struct AppOptions;
struct SceneSettings;
struct ResourceStats;
class SceneDocument;
namespace profiling {

// Host metadata and capture policy. The controller owns collector/process
// lifetime; this adapter never changes simulation, cache contents or rendering.
class EngineProfile final {
public:
    explicit EngineProfile(const AppOptions&);
    const std::filesystem::path& projectRoot() const noexcept { return root_; }
    CaptureController& captures() noexcept { return captures_; }
    void backendInitialized(GraphicsBackend);
    bool observing(bool playing=false) const;
    void frame(CaptureFrame,const SceneSettings*,const ResourceStats*,const SceneDocument* = nullptr);
private:
    std::filesystem::path root_;
    CaptureContext context_;
    CaptureController captures_;
    bool gameplayArmed_{},editorArmed_{};
    std::uint32_t captureDuration_{10};
    std::uint32_t lastGpuFrame_{std::numeric_limits<std::uint32_t>::max()};
    std::chrono::steady_clock::time_point lastContext_{};
};
} // namespace profiling
} // namespace crimson
