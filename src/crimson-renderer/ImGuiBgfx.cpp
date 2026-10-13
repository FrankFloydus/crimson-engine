#include "ImGuiBgfx.h"

#include "math/Math.h"
#include "ShaderPreflight.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "vs_imgui.h"
#include "fs_imgui.h"

namespace crimson {
namespace {

constexpr uint32_t LinearSampler = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr uint32_t NearestSampler = LinearSampler | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
constexpr uint64_t UiState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
    BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                  BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

// Pack rows rather than passing the atlas pitch to bgfx. This supports small
// dirty rectangles, Alpha8 atlases, and texture rows wider than uint16_t pitch.
const bgfx::Memory* copyPixels(ImTextureData* texture, int x, int y, int width, int height) {
    const uint64_t size = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;
    if (size == 0 || size > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("ImGui texture upload has an invalid size.");
    const bgfx::Memory* memory = bgfx::alloc(static_cast<uint32_t>(size));
    const auto* pixels = static_cast<const uint8_t*>(texture->GetPixels());
    const size_t destinationPitch = static_cast<size_t>(width) * 4;
    for (int row = 0; row < height; ++row) {
        const uint8_t* source = pixels + (static_cast<size_t>(y + row) * texture->Width + x) * texture->BytesPerPixel;
        uint8_t* destination = memory->data + static_cast<size_t>(row) * destinationPitch;
        if (texture->Format == ImTextureFormat_RGBA32) {
            std::memcpy(destination, source, destinationPitch);
        } else {
            for (int column = 0; column < width; ++column) {
                destination[column * 4 + 0] = 255;
                destination[column * 4 + 1] = 255;
                destination[column * 4 + 2] = 255;
                destination[column * 4 + 3] = source[column];
            }
        }
    }
    return memory;
}

bgfx::TextureHandle decodeTexture(ImTextureID id) {
    if (id == ImTextureID_Invalid || id > static_cast<ImTextureID>(UINT16_MAX))
        return BGFX_INVALID_HANDLE;
    return bgfx::TextureHandle{static_cast<uint16_t>(id - 1)};
}

} // namespace

struct ImGuiBgfx::TextureResource {
    ImTextureData* data{};
    bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};
};

struct ImGuiBgfx::RenderState {
    ImGuiBgfx* renderer{};
    ImDrawData* drawData{};
    uint16_t viewId{};
    uint16_t width{};
    uint16_t height{};
    uint32_t samplerFlags{LinearSampler};
};

ImGuiBgfx::ImGuiBgfx() {
    if (!ImGui::GetCurrentContext()) throw std::runtime_error("Create the ImGui context before its bgfx renderer.");
    ImGuiIO& io = ImGui::GetIO();
    if (io.BackendRendererUserData) throw std::runtime_error("An ImGui renderer backend is already installed.");
    static_assert(sizeof(ImDrawVert) == 20 && offsetof(ImDrawVert, pos) == 0 &&
                  offsetof(ImDrawVert, uv) == 8 && offsetof(ImDrawVert, col) == 16,
                  "ImGuiBgfx expects the default ImDrawVert layout.");
    layout_.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
    const bgfx::ShaderHandle vertex = bgfx::createShader(bgfx::copy(cc_vs_imgui, sizeof(cc_vs_imgui)));
    const bgfx::ShaderHandle fragment = bgfx::createShader(bgfx::copy(cc_fs_imgui, sizeof(cc_fs_imgui)));
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
        if (bgfx::isValid(vertex)) bgfx::destroy(vertex);
        if (bgfx::isValid(fragment)) bgfx::destroy(fragment);
        throw std::runtime_error("Could not create the ImGui shaders.");
    }
    program_ = bgfx::createProgram(vertex, fragment, true);
    sampler_ = bgfx::createUniform("s_texture", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(program_) || !bgfx::isValid(sampler_)) {
        if (bgfx::isValid(program_)) bgfx::destroy(program_);
        if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
        throw std::runtime_error("Could not initialize the ImGui renderer resources.");
    }
    io.BackendRendererName = "crimson-renderer-imgui-bgfx";
    io.BackendRendererUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Renderer_TextureMaxWidth = platform.Renderer_TextureMaxHeight = bgfx::getCaps()->limits.maxTextureSize;
    platform.DrawCallback_ResetRenderState = resetRenderState;
    platform.DrawCallback_SetSamplerLinear = setSamplerLinear;
    platform.DrawCallback_SetSamplerNearest = setSamplerNearest;
    initialized_ = true;
}

ImGuiBgfx::~ImGuiBgfx() { shutdown(); }

bgfx::ProgramHandle ImGuiBgfx::prepareProgramReload(std::span<const std::uint8_t> vertexCode,std::span<const std::uint8_t> fragmentCode,std::string& diagnostic) {
    if (!preflightShaderPair(vertexCode,fragmentCode,cc_vs_imgui,cc_fs_imgui,diagnostic)) return BGFX_INVALID_HANDLE;
    const auto vertex=bgfx::createShader(bgfx::copy(vertexCode.data(),static_cast<std::uint32_t>(vertexCode.size())));
    const auto fragment=bgfx::createShader(bgfx::copy(fragmentCode.data(),static_cast<std::uint32_t>(fragmentCode.size())));
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
        if (bgfx::isValid(vertex)) bgfx::destroy(vertex); if (bgfx::isValid(fragment)) bgfx::destroy(fragment);
        diagnostic="ImGui shader candidate allocation failed."; return BGFX_INVALID_HANDLE;
    }
    const auto candidate=bgfx::createProgram(vertex,fragment,true);
    if (!bgfx::isValid(candidate)) diagnostic="ImGui shader candidate link failed.";
    return candidate;
}
void ImGuiBgfx::publishProgramReload(bgfx::ProgramHandle candidate) {
    if (!bgfx::isValid(candidate)) return;
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    program_=candidate;
}

ImTextureID ImGuiBgfx::textureId(bgfx::TextureHandle texture) {
    return bgfx::isValid(texture) ? static_cast<ImTextureID>(texture.idx) + 1 : ImTextureID_Invalid;
}

void ImGuiBgfx::destroyTexture(TextureResource* resource) {
    if (bgfx::isValid(resource->handle)) bgfx::destroy(resource->handle);
    if (ImGui::GetCurrentContext() && resource->data) {
        resource->data->SetTexID(ImTextureID_Invalid);
        resource->data->BackendUserData = nullptr;
        resource->data->SetStatus(ImTextureStatus_Destroyed);
    }
    const auto position = std::find(textures_.begin(), textures_.end(), resource);
    if (position != textures_.end()) textures_.erase(position);
    delete resource;
}

void ImGuiBgfx::updateTexture(ImTextureData* texture) {
    if (texture->Status == ImTextureStatus_WantCreate) {
        const int maxTextureSize = static_cast<int>(bgfx::getCaps()->limits.maxTextureSize);
        if (texture->Width <= 0 || texture->Height <= 0 ||
            texture->Width > maxTextureSize || texture->Height > maxTextureSize ||
            (texture->Format != ImTextureFormat_RGBA32 && texture->Format != ImTextureFormat_Alpha8))
            throw std::runtime_error("ImGui requested an unsupported texture.");
        auto* resource = new TextureResource{};
        resource->data = texture;
        resource->handle = bgfx::createTexture2D(static_cast<uint16_t>(texture->Width), static_cast<uint16_t>(texture->Height),
            false, 1, bgfx::TextureFormat::RGBA8, LinearSampler);
        if (!bgfx::isValid(resource->handle)) { delete resource; throw std::runtime_error("Could not upload an ImGui texture."); }
        // Supplying pixels to createTexture2D makes this texture immutable on
        // bgfx/DX11. Allocate without pixels, then upload through the mutable
        // update path used by subsequent dynamic-font atlas rectangles.
        bgfx::updateTexture2D(resource->handle, 0, 0, 0, 0, static_cast<uint16_t>(texture->Width),
            static_cast<uint16_t>(texture->Height), copyPixels(texture, 0, 0, texture->Width, texture->Height));
        textures_.push_back(resource);
        texture->BackendUserData = resource;
        texture->SetTexID(textureId(resource->handle));
        texture->SetStatus(ImTextureStatus_OK);
    } else if (texture->Status == ImTextureStatus_WantUpdates) {
        auto* resource = static_cast<TextureResource*>(texture->BackendUserData);
        if (!resource || !bgfx::isValid(resource->handle)) throw std::runtime_error("ImGui texture update has no GPU resource.");
        for (const ImTextureRect& rectangle : texture->Updates) {
            if (!rectangle.w || !rectangle.h) continue;
            bgfx::updateTexture2D(resource->handle, 0, 0, rectangle.x, rectangle.y, rectangle.w, rectangle.h,
                copyPixels(texture, rectangle.x, rectangle.y, rectangle.w, rectangle.h));
        }
        texture->SetStatus(ImTextureStatus_OK);
    } else if (texture->Status == ImTextureStatus_WantDestroy) {
        if (auto* resource = static_cast<TextureResource*>(texture->BackendUserData)) destroyTexture(resource);
        else texture->SetStatus(ImTextureStatus_Destroyed);
    }
}

void ImGuiBgfx::setupView(const RenderState& state) {
    const ImDrawData& data = *state.drawData;
    // ImGui uses the legacy left-handed screen-space Z convention. Reflect only
    // that adapter axis; engine world/camera matrices remain right-handed.
    const auto projection=math::orthographic(data.DisplayPos.x,data.DisplayPos.x+data.DisplaySize.x,
        data.DisplayPos.y+data.DisplaySize.y,data.DisplayPos.y,0,100,
        bgfx::getCaps()->homogeneousDepth ? ClipDepth::NegativeOneToOne : ClipDepth::ZeroToOne)*math::scaling({1,1,-1});
    bgfx::setViewName(state.viewId, "ImGui tools");
    bgfx::setViewMode(state.viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(state.viewId, 0, 0, state.width, state.height);
    bgfx::setViewTransform(state.viewId, nullptr, projection.data());
}

void ImGuiBgfx::resetRenderState(const ImDrawList*, const ImDrawCmd*) {
    auto* state = static_cast<RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
    if (state) { state->samplerFlags = LinearSampler; state->renderer->setupView(*state); }
}

void ImGuiBgfx::setSamplerLinear(const ImDrawList*, const ImDrawCmd*) {
    if (auto* state = static_cast<RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState)) state->samplerFlags = LinearSampler;
}

void ImGuiBgfx::setSamplerNearest(const ImDrawList*, const ImDrawCmd*) {
    if (auto* state = static_cast<RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState)) state->samplerFlags = NearestSampler;
}

void ImGuiBgfx::render(ImDrawData* drawData, uint16_t viewId) {
    if (!initialized_ || !drawData) return;
    if (drawData->Textures) for (ImTextureData* texture : *drawData->Textures)
        if (texture->Status != ImTextureStatus_OK) updateTexture(texture);
    const int width = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int height = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX) return;
    RenderState state{this, drawData, viewId, static_cast<uint16_t>(width), static_cast<uint16_t>(height), LinearSampler};
    setupView(state);
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Renderer_RenderState = &state;
    for (const ImDrawList* list : drawData->CmdLists) {
        const uint32_t vertices = static_cast<uint32_t>(list->VtxBuffer.Size);
        const uint32_t indices = static_cast<uint32_t>(list->IdxBuffer.Size);
        if (vertices == 0 || indices == 0) continue;
        const bool index32 = sizeof(ImDrawIdx) == 4;
        if (bgfx::getAvailTransientVertexBuffer(vertices, layout_) < vertices ||
            bgfx::getAvailTransientIndexBuffer(indices, index32) < indices) {
            std::fprintf(stderr, "ImGui transient buffer capacity exceeded; tools rendering skipped.\n");
            break;
        }
        bgfx::TransientVertexBuffer vertexBuffer;
        bgfx::TransientIndexBuffer indexBuffer;
        bgfx::allocTransientVertexBuffer(&vertexBuffer, vertices, layout_);
        bgfx::allocTransientIndexBuffer(&indexBuffer, indices, index32);
        std::memcpy(vertexBuffer.data, list->VtxBuffer.Data, vertices * sizeof(ImDrawVert));
        std::memcpy(indexBuffer.data, list->IdxBuffer.Data, indices * sizeof(ImDrawIdx));
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback) {
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
                if (command.UserCallback == ImDrawCallback_ResetRenderState) resetRenderState(list, &command);
                else
#endif
                    command.UserCallback(list, &command);
                continue;
            }
            if (!command.ElemCount) continue;
            const float left = (command.ClipRect.x - drawData->DisplayPos.x) * drawData->FramebufferScale.x;
            const float top = (command.ClipRect.y - drawData->DisplayPos.y) * drawData->FramebufferScale.y;
            const float right = (command.ClipRect.z - drawData->DisplayPos.x) * drawData->FramebufferScale.x;
            const float bottom = (command.ClipRect.w - drawData->DisplayPos.y) * drawData->FramebufferScale.y;
            const int x0 = static_cast<int>(std::floor(std::clamp(left, 0.0f, static_cast<float>(width))));
            const int y0 = static_cast<int>(std::floor(std::clamp(top, 0.0f, static_cast<float>(height))));
            const int x1 = static_cast<int>(std::ceil(std::clamp(right, 0.0f, static_cast<float>(width))));
            const int y1 = static_cast<int>(std::ceil(std::clamp(bottom, 0.0f, static_cast<float>(height))));
            if (x1 <= x0 || y1 <= y0 || command.VtxOffset >= vertices || command.IdxOffset + command.ElemCount > indices) continue;
            const bgfx::TextureHandle texture = decodeTexture(command.GetTexID());
            if (!bgfx::isValid(texture)) continue;
            bgfx::setScissor(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), static_cast<uint16_t>(x1 - x0), static_cast<uint16_t>(y1 - y0));
            bgfx::setState(UiState);
            bgfx::setVertexBuffer(0, &vertexBuffer, command.VtxOffset, vertices - command.VtxOffset);
            bgfx::setIndexBuffer(&indexBuffer, command.IdxOffset, command.ElemCount);
            bgfx::setTexture(0, sampler_, texture, state.samplerFlags);
            bgfx::submit(viewId, program_);
        }
    }
    platform.Renderer_RenderState = nullptr;
}

void ImGuiBgfx::shutdown() {
    if (!initialized_) return;
    while (!textures_.empty()) destroyTexture(textures_.back());
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    sampler_ = BGFX_INVALID_HANDLE; program_ = BGFX_INVALID_HANDLE;
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.BackendRendererUserData == this) {
            io.BackendRendererUserData = nullptr; io.BackendRendererName = nullptr;
            io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset);
            ImGui::GetPlatformIO().ClearRendererHandlers();
        }
    }
    initialized_ = false;
}

} // namespace crimson
