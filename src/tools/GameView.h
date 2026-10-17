#pragma once
#include "crimson-renderer/RenderTypes.h"
#include "editor/FrameMetrics.h"
#include "assets/NavigationAgentProfile.h"
#include <optional>
#include <span>
#include <string>

struct ImDrawList;
namespace crimson {
class SceneDocument;
class Scene;
class Renderer;
struct GameViewFrame {
    ViewportRegion viewport;
    uint32_t pixelWidth{},pixelHeight{};
    bool focused{},closed{},clicked{},leftHeld{};
    std::optional<Vec2> pointerPosition;
};
class GameView {
public:
    GameViewFrame draw(SceneDocument&,Renderer&,bool& open,bool playing,bool focus=false,std::uint64_t textureAndProgramBytes=0);
    void setFrameTiming(FrameTiming timing) {frameTiming_=timing;newFrameTiming_=true;}
    void drawRoute(const Camera&,const std::optional<Vec3>& clickedGround,std::span<const Vec3> route,
                   const std::optional<Vec3>& characterPosition);
    void drawActorLightRigs(const Camera&,const Scene&,const NavigationAgentProfileQuery& profiles={});
private:
    ImDrawList* imageDrawList_{};
    ViewportRegion imageRegion_;
    std::uint32_t imageWidth_{},imageHeight_{};
    FrameTiming frameTiming_;
    FrameMetricsSampler metrics_;
    std::string metricsText_;
    bool metricsVisible_{true},wasPlaying_{},newFrameTiming_{};
    void updateMetrics(Renderer&,std::uint64_t textureAndProgramBytes);
    void drawMetrics(Vec2 origin,Vec2 size) const;
};
}
