#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <optional>
#include <span>

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
    GameViewFrame draw(SceneDocument&,Renderer&,bool& open,bool playing,bool focus=false);
    void drawRoute(const Camera&,const std::optional<Vec3>& clickedGround,std::span<const Vec3> route,
                   const std::optional<Vec3>& characterPosition);
    void drawCollisions(const Camera&,const Scene&,bool enabled);
private:
    ImDrawList* imageDrawList_{};
    ViewportRegion imageRegion_;
    std::uint32_t imageWidth_{},imageHeight_{};
};
}
