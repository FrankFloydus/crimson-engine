#pragma once
#include "crimson-renderer/RenderTypes.h"
#include "input/InputEvent.h"
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace crimson {
// Relative flight owns these native events before they reach editor widgets. Releases
// and interruption events remain visible to clear any pre-acquisition state.
bool navigationBlocksEditorEvent(const InputEvent&,InputSurfaceId owner,bool flying);
class Navigation {
public:
    explicit Navigation(InputSurfaceId defaultOwner=0);
    ~Navigation() { cancel(); }
    using MouseCapture=std::function<bool(InputSurfaceId,bool)>;
    void setMouseCapture(MouseCapture);
    void handleEvent(const InputEvent& event);
    bool blocksEditorEvent(const InputEvent& event) const { return navigationBlocksEditorEvent(event,ownerWindowId_,flying()); }
    void update(Camera& camera, const ViewportRegion& region, float dt, bool sphereVisible = true, bool preserveRoll = false);
    void cancel();
    bool active() const { return mode_ != Mode::None; }
    bool flying() const { return mode_ == Mode::Fly; }
    float flySpeed() const { return flySpeed_; }
    // A stationary RMB release retains the original image point, independent
    // of native relative-mode cursor warps. Uses the current coordinate space.
    std::optional<Vec2> consumeContextClick();
private:
    enum class Mode { None, Orbit, Pan, Fly };
    struct Event { InputEvent value{}; InputModifiers modifiers{}; };
    void releaseCapture();
    void motion(Camera& camera, const ViewportRegion& region, float x, float y, bool preserveRoll);
    InputSurfaceId baseWindowId_{}, ownerWindowId_{}, captureWindowId_{};
    std::vector<Event> events_;
    std::array<bool, physicalKeyIndex(PhysicalKeyCode::Count)> keys_{};
    Mode mode_{Mode::None};
    bool captureActive_{false}, relativeActive_{false}, anchorPending_{false};
    float lastX_{}, lastY_{}, restoreX_{}, restoreY_{}, flySpeed_{25.0f};
    bool contextClickCandidate_{};
    float contextClickMotion_{};
    std::optional<Vec2> contextClick_;
    MouseCapture mouseCapture_;
    Vec3 pivot_{};
    Vec3 anchor_{};
};
} // namespace crimson
