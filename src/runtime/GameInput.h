#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <SDL3/SDL_events.h>
#include <optional>

namespace crimson {
// No mouse capture or native control: this owner only routes received SDL
// events. A click in the Game image acquires input and records a destination;
// Escape or focus loss cancels ownership. Only a left-button gesture begun in
// the owned image may continuously retarget movement.
class GameInput {
public:
    void setViewport(const ViewportRegion&,bool enabled,bool keyboardBlocked);
    void setDesktopCoordinates(bool enabled) { desktopCoordinates_=enabled; }
    bool handleEvent(const SDL_Event&);
    void acquire(){if(enabled_ && !blocked_)armed_=true;}
    void cancel();
    bool ownsKeyboard() const { return armed_ && enabled_ && !blocked_; }
    const ViewportRegion& viewport() const { return viewport_; }
    std::optional<Vec2> pointerPosition() const;
    std::optional<Vec2> consumeMoveClick();
    // Capture against the camera and physical target which produced the visible
    // image, before simulation, follow, resize or another native window can move it.
    void setProjection(const Camera&,std::uint32_t pixelWidth,std::uint32_t pixelHeight);
    std::optional<Ray> consumeMoveRay(const Camera& fallback);
    std::optional<Ray> heldMoveRay(const Camera& fallback) const;
    std::uint64_t cancellationRevision() const { return cancellationRevision_; }
    // UI-resolved coordinates recover the first click that deactivates a text
    // field, when the earlier SDL event was still blocked by the prior frame.
    void submitPointer(Vec2 position,bool moveClick);
    void submitUiPointer(Vec2 position,bool moveClick,bool leftHeld=false);
private:
    bool contains(Vec2 position) const;
    std::optional<Ray> pointerRay(Vec2 position,const Camera& fallback) const;
    struct Projection { Camera camera;ViewportRegion viewport;std::uint32_t width{},height{}; };
    ViewportRegion viewport_;
    std::optional<Vec2> pointer_,moveClick_;
    std::optional<Ray> moveRay_;
    std::optional<Projection> projection_;
    std::optional<Vec2> nativeClick_;
    bool nativeClickSubmitted_{};
    bool nativeClickCanceled_{},nativeClickReleased_{};
    bool leftHeld_{};
    std::uint64_t cancellationRevision_{};
    bool enabled_{},blocked_{},armed_{},desktopCoordinates_{};
};
}
