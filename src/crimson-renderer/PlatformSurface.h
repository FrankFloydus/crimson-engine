#pragma once

#include <bgfx/bgfx.h>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace crimson {
struct DecodedImage;

// Renderer-private DX11/DX12 secondary-window presentation. Native COM resources
// remain behind this owner; bgfx only sees an imported offscreen texture, never
// a swap-chain buffer. All methods run on the initialization/render thread.
class PlatformSurface final {
public:
    using Diagnostic = std::function<void(const std::string&)>;
    using Capture = std::function<void(const DecodedImage&)>;
    explicit PlatformSurface(void* nativeWindow,Diagnostic diagnostic);
    ~PlatformSurface();
    PlatformSurface(const PlatformSurface&)=delete;
    PlatformSurface& operator=(const PlatformSurface&)=delete;

    void beginFrame(std::uint32_t completedFrame);
    bgfx::FrameBufferHandle prepare(std::uint16_t width,std::uint16_t height,std::uint32_t completedFrame);
    void rendered(std::uint32_t submittedAfter);
    bool postFrame(std::uint32_t completedFrame,bool synchronize=false);
    void close(std::uint32_t completedFrame);
    bool pendingRetirements() const noexcept;
    bgfx::FrameBufferHandle framebuffer() const noexcept;
    std::uintptr_t swapChainIdentity() const noexcept;
    // Native texture allocations, including candidate/retired targets and both
    // swap-chain buffers. Driver/command-list bookkeeping is not queryable.
    std::uint64_t residentBytes() const noexcept;
    static std::uint64_t quarantinedResidentBytes() noexcept;
    static std::size_t quarantinedSurfaceCount() noexcept;
    static std::uint64_t colorTargetAllocationBytes(std::uint16_t width,std::uint16_t height) noexcept;
    bool requestCapture(Capture);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
