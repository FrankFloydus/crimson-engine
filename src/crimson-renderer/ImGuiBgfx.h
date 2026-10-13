#pragma once

#include "RenderTypes.h"

#include <bgfx/bgfx.h>
#include <imgui.h>
#include <vector>

namespace crimson {

// The application owns the ImGui context and SDL platform backend. Construct
// this after bgfx/context initialization and shut it down before either owner.
class ImGuiBgfx final {
public:
    ImGuiBgfx();
    ~ImGuiBgfx();
    ImGuiBgfx(const ImGuiBgfx&) = delete;
    ImGuiBgfx& operator=(const ImGuiBgfx&) = delete;

    void render(ImDrawData* drawData, uint16_t viewId = ToolsView);
    void shutdown();

    // ImGui's invalid ID is zero; bgfx texture handle zero remains usable.
    static ImTextureID textureId(bgfx::TextureHandle texture);

private:
    struct TextureResource;
    struct RenderState;
    void updateTexture(ImTextureData* texture);
    void destroyTexture(TextureResource* resource);
    void setupView(const RenderState& state);
    static void resetRenderState(const ImDrawList*, const ImDrawCmd*);
    static void setSamplerLinear(const ImDrawList*, const ImDrawCmd*);
    static void setSamplerNearest(const ImDrawList*, const ImDrawCmd*);

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle sampler_{BGFX_INVALID_HANDLE};
    std::vector<TextureResource*> textures_;
    bool initialized_{false};
};

} // namespace crimson
