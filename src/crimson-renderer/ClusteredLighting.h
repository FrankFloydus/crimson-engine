#pragma once

#include "RenderTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace crimson::clustered_lighting {

// The current Camera is perspective. Tiles use a top-left origin and the last
// tile can be smaller than tileSize. Depth is positive distance along forward.
struct Config {
    std::uint32_t tileSize{32};
    std::uint32_t depthSlices{24};
    std::uint32_t maxLights{4096};
    std::uint32_t maxClusters{262144};
    std::uint32_t maxIndices{4194304};
};

struct Header {
    std::uint32_t offset{},count{};
};

enum class Status {
    Ready,
    InvalidConfiguration,
    LightCapacityExceeded,
    ClusterCapacityExceeded,
    IndexCapacityExceeded,
    AllocationFailed
};

struct Stats {
    std::uint32_t inputLights{},activeLights{},visibleLights{},occupiedClusters{},maxLightsPerCluster{};
    std::uint64_t candidateTests{},lightReferences{};
};

struct Grid {
    std::uint32_t width{},height{},tileSize{},tilesX{},tilesY{},depthSlices{};
    float nearPlane{},farPlane{};
    // slice = clamp(floor(log2(positiveViewDepth)*depthScale+depthBias),0,slices-1).
    float depthScale{},depthBias{};
    std::vector<Header> headers;
    // Source indices refer directly to the caller's PointLight span, retaining
    // persistent-light/shadow correspondence through culling and compaction.
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> visibleLightIndices;
    Stats stats{};
    Status status{Status::InvalidConfiguration};

    bool ready() const noexcept { return status==Status::Ready; }
    std::uint32_t clusterIndex(std::uint32_t x,std::uint32_t y,std::uint32_t z) const noexcept;
    std::uint32_t depthSlice(float positiveViewDepth) const noexcept;
    std::uint32_t clusterForPixelDepth(float pixelX,float pixelY,float positiveViewDepth) const noexcept;
    std::span<const std::uint32_t> lightsForCluster(std::uint32_t cluster) const noexcept;

private:
    struct Bounds {
        double x{},y{},z{},radius{};
        std::uint32_t source{},minX{},maxX{},minY{},maxY{},minZ{},maxZ{};
    };
    struct TilePlanes { double low{},high{},lowLength{},highLength{}; };
    std::vector<Bounds> bounds_;
    std::vector<TilePlanes> columns_,rows_;
    std::vector<double> depths_;
    std::vector<std::uint32_t> cursors_;
    friend bool build(Grid&,const Camera&,std::uint32_t,std::uint32_t,std::span<const PointLight>,const Config&);
};

// Reuses all output/scratch vector capacities. No opaque-depth reduction is
// applied, so transparent geometry and particles use the same conservative
// lists. A failed build clears all publishable lists and reports its reason;
// capacity exhaustion never publishes a silently truncated lighting result.
bool build(Grid&,const Camera&,std::uint32_t width,std::uint32_t height,
           std::span<const PointLight>,const Config& = {});
const char* statusText(Status) noexcept;

} // namespace crimson::clustered_lighting
