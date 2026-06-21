#pragma once

#include "render/overlay/OverlayRenderableBuilder.h"
#include "render/overlay/OverlayRenderableProxy.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace filament {
class Engine;
class Material;
class MaterialInstance;
class Scene;
} // namespace filament

namespace quader {

class OverlayProxyFactory {
public:
    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            filament::Material& overlayMaterial,
            filament::Material& overlayLineMaterial,
            filament::Material& overlayPointMaterial);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr &&
                scene_ != nullptr &&
                overlayMaterial_ != nullptr &&
                overlayLineMaterial_ != nullptr &&
                overlayPointMaterial_ != nullptr;
    }

    std::vector<OverlayRenderableProxy> createLayerProxies(const OverlayLayerBuildPlan& plan, std::uint8_t layerMask);
    void destroy(OverlayRenderableProxy& proxy);

    static std::size_t plannedProxyCount(const OverlayLayerBuildPlan& plan);

private:
    void createTriangleProxy(const OverlayFaceFillBatch& batch, std::uint8_t layerMask, std::vector<OverlayRenderableProxy>& proxies);
    void createLineProxy(const OverlayLineBatch& batch, std::uint8_t layerMask, std::vector<OverlayRenderableProxy>& proxies);
    void createPointProxy(const OverlayPointBatch& batch, std::uint8_t layerMask, std::vector<OverlayRenderableProxy>& proxies);
    void configureOverlayMaterial(filament::MaterialInstance& material, const OverlayRenderState& state) const;

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Material* overlayMaterial_ = nullptr;
    filament::Material* overlayLineMaterial_ = nullptr;
    filament::Material* overlayPointMaterial_ = nullptr;
};

} // namespace quader
