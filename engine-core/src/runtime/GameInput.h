#pragma once
#include "crimson-renderer/RenderTypes.h"
#include "input/InputEvent.h"
#include <array>
#include <functional>

namespace crimson {
// Pure routing gate shared by the native event loop and CPU ownership checks.
// Releases still reach editor widgets to clear keys/buttons pressed before acquisition.
bool gameInputBlocksEditorEvent(const InputEvent&,InputSurfaceId owner,bool gameplayOwned,bool relativeMouseCaptured);
// Owns the Game image's FPS actions and its native relative mouse capture.
// Current-frame UI ownership is resolved before syncMouseCapture(); cancellation
// releases native capture immediately and never preserves held gameplay input.
class GameInput {
public:
    GameInput()=default;
    ~GameInput();
    GameInput(const GameInput&)=delete;
    GameInput& operator=(const GameInput&)=delete;
    using MouseCapture=std::function<bool(InputSurfaceId,bool)>;
    void setMouseCapture(MouseCapture);
    void setViewport(const ViewportRegion&,bool enabled,bool keyboardBlocked);
    bool handleEvent(const InputEvent&);
    bool blocksEditorEvent(const InputEvent& event) const {
        return gameInputBlocksEditorEvent(event,viewport_.windowId,ownsKeyboard(),captureWindowId_!=0);
    }
    void acquire();
    // The UI may acknowledge a native press after a later Escape/focus event.
    // That acknowledgment cannot rearm an acquisition already canceled.
    void acquireFromImageClick();
    void cancel();
    void syncMouseCapture();
    bool ownsKeyboard() const { return armed_ && enabled_ && !blocked_; }
    const ViewportRegion& viewport() const { return viewport_; }
    Vec2 movement() const;
    // Raw logical-pixel relative deltas: right/down are positive. Runtime
    // sensitivity and inversion are applied by the player's look controller.
    Vec2 consumeLookDelta();
    bool consumeJump();
    // One owned LMB press edge; the click acquiring Game only transfers input.
    bool consumeTrace();
    std::uint64_t cancellationRevision() const { return cancellationRevision_; }
private:
    bool contains(Vec2 position) const;
    void releaseMouseCapture();
    enum class ImageAcquisition { None, Pending, Canceled };
    ViewportRegion viewport_;
    std::array<bool,5> held_{}; // W, A, S, D, Space.
    Vec2 lookDelta_{};
    std::uint64_t cancellationRevision_{};
    InputSurfaceId captureWindowId_{};
    MouseCapture mouseCapture_;
    ImageAcquisition imageAcquisition_{};
    bool jumpPending_{},tracePending_{},traceHeld_{},discardNextMotion_{};
    bool enabled_{},blocked_{},armed_{};
};
}
