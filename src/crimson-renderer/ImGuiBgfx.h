#pragma once

#include "RenderTypes.h"

#include <bgfx/bgfx.h>
#include <imgui.h>
#include <vector>
#include <span>
#include <string>
#include <functional>
#include <memory>

namespace crimson {
struct DecodedImage;
class PlatformSurface;

// The application owns the ImGui context and SDL platform backend. Construct
// this after bgfx/context initialization and shut it down before either owner.
class ImGuiBgfx final {
public:
    explicit ImGuiBgfx(std::function<void(const std::string&)> diagnostic = {});
    ~ImGuiBgfx();
    ImGuiBgfx(const ImGuiBgfx&) = delete;
    ImGuiBgfx& operator=(const ImGuiBgfx&) = delete;

    // Called before the ImGui frame and after the application's one bgfx::frame.
    // Secondary UI renders offscreen; postFrame copies/presents native surfaces.
    void newFrame(std::uint32_t completedBgfxFrame);
    // Synchronize at most one visible child when the main window cannot pace
    // rendering. False means no visible child performed synchronized Present.
    bool postFrame(std::uint32_t completedBgfxFrame,bool synchronizeNative=false);
    void render(ImDrawData* drawData, uint16_t viewId = ToolsView);
    void shutdown();
    bool hasPendingRetirements() const noexcept;
    std::uint64_t platformResidentBytes() const noexcept;
    bgfx::ProgramHandle prepareProgramReload(std::span<const std::uint8_t>,std::span<const std::uint8_t>,std::string& diagnostic);
    void publishProgramReload(bgfx::ProgramHandle candidate);

    // ImGui's invalid ID is zero; bgfx texture handle zero remains usable.
    static ImTextureID textureId(bgfx::TextureHandle texture,bool repeat=false);
    // Borrowed ordinary offscreen framebuffer. Native chain buffers are never
    // imported into bgfx; native captures run after copy and before Present.
    bgfx::FrameBufferHandle platformViewportFramebuffer(ImGuiID viewportId) const noexcept;
    std::uintptr_t platformViewportSwapChainIdentity(ImGuiID viewportId) const noexcept;
    bool requestPlatformViewportCapture(ImGuiID,std::function<void(const DecodedImage&)>);

private:
    struct TextureResource;
    struct RenderState;
    struct ViewportResource;
    void updateTexture(ImTextureData* texture);
    void destroyTexture(TextureResource* resource);
    void renderTo(ImDrawData*, uint16_t, bgfx::FrameBufferHandle);
    void resizeViewport(ViewportResource&, ImVec2 size, ImVec2 framebufferScale);
    void destroyViewport(ViewportResource*);
    void collectRetiredSurfaces(std::uint32_t);
    std::uint32_t platformViewLimit() const noexcept;
    void setupView(const RenderState& state);
    static void resetRenderState(const ImDrawList*, const ImDrawCmd*);
    static void setSamplerLinear(const ImDrawList*, const ImDrawCmd*);
    static void setSamplerNearest(const ImDrawList*, const ImDrawCmd*);
    static void createPlatformWindow(ImGuiViewport*);
    static void destroyPlatformWindow(ImGuiViewport*);
    static void resizePlatformWindow(ImGuiViewport*, ImVec2);
    static void renderPlatformWindow(ImGuiViewport*, void*);
    static void swapPlatformWindow(ImGuiViewport*, void*);

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle sampler_{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle platformTerminalTexture_{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle platformTerminalFramebuffer_{BGFX_INVALID_HANDLE};
    std::vector<TextureResource*> textures_;
    std::vector<ViewportResource*> viewports_;
    std::vector<std::unique_ptr<PlatformSurface>> retiredSurfaces_;
    std::function<void(const std::string&)> diagnostic_;
    std::uint32_t completedFrame_{};
    std::uint64_t platformTerminalBytes_{};
    bool initialized_{false};
};

} // namespace crimson
