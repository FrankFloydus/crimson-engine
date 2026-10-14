#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <optional>

struct ImDrawList;
namespace crimson {
class SceneDocument;
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
    void drawClickMarker(const Camera&,const Vec3& groundPoint);
private:
    bool clickMarker_{true};
    ImDrawList* imageDrawList_{};
    ViewportRegion imageRegion_;
    std::uint32_t imageWidth_{},imageHeight_{};
};
}
