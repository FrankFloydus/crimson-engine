#include "ImGuiBgfx.h"
#include "Renderer.h"

#include "math/Math.h"
#include "ShaderPreflight.h"
#include "PlatformSurface.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

#include "vs_imgui.h"
#include "fs_imgui.h"

namespace crimson {
namespace {

constexpr uint32_t LinearSampler = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr uint32_t NearestSampler = LinearSampler | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
constexpr ImTextureID RepeatTexture = static_cast<ImTextureID>(1ull << 32);
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
    id &= ~RepeatTexture;
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
    bgfx::FrameBufferHandle framebuffer{BGFX_INVALID_HANDLE};
};

struct ImGuiBgfx::ViewportResource {
    ImGuiBgfx* renderer{};
    ImGuiViewport* viewport{};
    bgfx::ViewId view{};
    uint16_t width{},height{};
    std::unique_ptr<PlatformSurface> surface;
};

ImGuiBgfx::ImGuiBgfx(shared::Renderer& renderer,std::function<void(const std::string&)> diagnostic)
    :frame_owner_(&renderer),diagnostic_(std::move(diagnostic)) {
    if (!ImGui::GetCurrentContext()) throw std::runtime_error("Create the ImGui context before its bgfx renderer.");
    ImGuiIO& io = ImGui::GetIO();
    if (io.BackendRendererUserData) throw std::runtime_error("An ImGui renderer backend is already installed.");
    const bool platformWindowsSupported=(bgfx::getCaps()->supported & BGFX_CAPS_SWAP_CHAIN) &&
        platformViewLimit()>FirstPlatformView;
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) && !platformWindowsSupported)
        throw std::runtime_error("This GPU backend cannot render Dear ImGui platform windows.");
    if(platformWindowsSupported) {
        const auto capacity=platformViewLimit()-FirstPlatformView;
        viewports_.reserve(capacity);retiredSurfaces_.reserve(capacity);
    }
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
    if(platformWindowsSupported && bgfx::getRendererType()==bgfx::RendererType::Direct3D12) {
        platformTerminalTexture_=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        if(bgfx::isValid(platformTerminalTexture_))platformTerminalFramebuffer_=bgfx::createFrameBuffer(1,&platformTerminalTexture_,false);
        if(!bgfx::isValid(platformTerminalFramebuffer_)) {
            if(bgfx::isValid(platformTerminalTexture_))bgfx::destroy(platformTerminalTexture_);
            bgfx::destroy(program_);bgfx::destroy(sampler_);
            throw std::runtime_error("Could not initialize the DX12 platform terminal framebuffer.");
        }
        bgfx::setName(platformTerminalFramebuffer_,"ImGui platform terminal framebuffer");
        platformTerminalBytes_=PlatformSurface::colorTargetAllocationBytes(1,1);
    }
    io.BackendRendererName = "crimson-renderer-imgui-bgfx";
    io.BackendRendererUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Renderer_TextureMaxWidth = platform.Renderer_TextureMaxHeight = bgfx::getCaps()->limits.maxTextureSize;
    platform.DrawCallback_ResetRenderState = resetRenderState;
    platform.DrawCallback_SetSamplerLinear = setSamplerLinear;
    platform.DrawCallback_SetSamplerNearest = setSamplerNearest;
    if (platformWindowsSupported) {
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
        platform.Renderer_CreateWindow = createPlatformWindow;
        platform.Renderer_DestroyWindow = destroyPlatformWindow;
        platform.Renderer_SetWindowSize = resizePlatformWindow;
        platform.Renderer_RenderWindow = renderPlatformWindow;
        platform.Renderer_SwapBuffers = swapPlatformWindow;
    }
    initialized_ = true;
}

ImGuiBgfx::~ImGuiBgfx() {
    shutdown();
    // App explicitly drains these before context teardown. The bounded fallback
    // also protects exception unwinding: imported COM pointers must survive the
    // queued framebuffer/wrapper commands. Ordinary frame callbacks never advance bgfx.
    for(unsigned i=0;i<8 && hasPendingRetirements();++i)postFrame(frame_owner_->advance_frame());
}

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

ImTextureID ImGuiBgfx::textureId(bgfx::TextureHandle texture,bool repeat) {
    return bgfx::isValid(texture) ? (static_cast<ImTextureID>(texture.idx) + 1) | (repeat?RepeatTexture:0) : ImTextureID_Invalid;
}

void ImGuiBgfx::newFrame(std::uint32_t completedBgfxFrame) {
    completedFrame_=completedBgfxFrame;
    collectRetiredSurfaces(completedFrame_);
    for(auto* viewport:viewports_)viewport->surface->beginFrame(completedFrame_);
}

bool ImGuiBgfx::postFrame(std::uint32_t completedBgfxFrame,bool synchronizeNative) {
    completedFrame_=completedBgfxFrame;
    bool synchronized=false;
    for(auto* viewport:viewports_)if(viewport->surface->postFrame(completedFrame_,synchronizeNative && !synchronized))synchronized=true;
    collectRetiredSurfaces(completedFrame_);
    return synchronized;
}

void ImGuiBgfx::collectRetiredSurfaces(std::uint32_t frame) {
    for(auto& surface:retiredSurfaces_)surface->postFrame(frame);
    std::erase_if(retiredSurfaces_,[](const auto& surface){return !surface->pendingRetirements();});
}

bool ImGuiBgfx::hasPendingRetirements() const noexcept {return !retiredSurfaces_.empty();}

std::uint32_t ImGuiBgfx::platformViewLimit() const noexcept {
    const auto limit=std::min<std::uint32_t>(bgfx::getCaps()->limits.maxViews,UINT16_MAX);
    return limit-(bgfx::getRendererType()==bgfx::RendererType::Direct3D12?1u:0u);
}
std::uint64_t ImGuiBgfx::platformResidentBytes() const noexcept {
    std::uint64_t bytes=platformTerminalBytes_+PlatformSurface::quarantinedResidentBytes();
    for(const auto* viewport:viewports_)bytes+=viewport->surface->residentBytes();
    for(const auto& surface:retiredSurfaces_)bytes+=surface->residentBytes();
    return bytes;
}

bgfx::FrameBufferHandle ImGuiBgfx::platformViewportFramebuffer(ImGuiID id) const noexcept {
    const auto found=std::find_if(viewports_.begin(),viewports_.end(),[id](const auto* value){return value->viewport->ID==id;});
    if (found==viewports_.end()) return BGFX_INVALID_HANDLE;
    return (*found)->surface->framebuffer();
}

std::uintptr_t ImGuiBgfx::platformViewportSwapChainIdentity(ImGuiID id) const noexcept {
    const auto found=std::find_if(viewports_.begin(),viewports_.end(),[id](const auto* value){return value->viewport->ID==id;});
    return found==viewports_.end() ? 0 : (*found)->surface->swapChainIdentity();
}

bool ImGuiBgfx::requestPlatformViewportCapture(ImGuiID id,std::function<void(const DecodedImage&)> callback) {
    const auto found=std::find_if(viewports_.begin(),viewports_.end(),[id](const auto* value){return value->viewport->ID==id;});
    return found!=viewports_.end() && (*found)->surface->requestCapture(std::move(callback));
}

void ImGuiBgfx::resizeViewport(ViewportResource& resource,ImVec2 size,ImVec2 scale) {
    const float pixelWidth=size.x*scale.x,pixelHeight=size.y*scale.y;
    const auto limit=std::min<std::uint32_t>(bgfx::getCaps()->limits.maxTextureSize,UINT16_MAX);
    if (!std::isfinite(pixelWidth) || !std::isfinite(pixelHeight) || pixelWidth>limit || pixelHeight>limit)
        throw std::runtime_error("Dear ImGui platform window exceeds the GPU framebuffer size limit.");
    if (pixelWidth<=0 || pixelHeight<=0) return;
    const auto width=static_cast<uint16_t>(std::max(1.f,std::round(pixelWidth)));
    const auto height=static_cast<uint16_t>(std::max(1.f,std::round(pixelHeight)));
    resource.width=width; resource.height=height;
}

void ImGuiBgfx::createPlatformWindow(ImGuiViewport* viewport) {
    auto* renderer=static_cast<ImGuiBgfx*>(ImGui::GetIO().BackendRendererUserData);
    if (!renderer || viewport->RendererUserData) throw std::runtime_error("Dear ImGui platform framebuffer ownership is invalid.");
    const auto maxViews=renderer->platformViewLimit();
    if(renderer->viewports_.size()+renderer->retiredSurfaces_.size()+PlatformSurface::quarantinedSurfaceCount()>=maxViews-FirstPlatformView)
        throw std::runtime_error("Dear ImGui platform windows exhausted the pending surface capacity.");
    std::uint32_t view=FirstPlatformView;
    while (view<maxViews && std::any_of(renderer->viewports_.begin(),renderer->viewports_.end(),[view](const auto* value){return value->view==view;})) ++view;
    if (view==maxViews) throw std::runtime_error("Dear ImGui platform windows exhausted the GPU view capacity.");
    auto resource=std::make_unique<ViewportResource>();
    resource->renderer=renderer; resource->viewport=viewport; resource->view=static_cast<bgfx::ViewId>(view);
    resource->surface=std::make_unique<PlatformSurface>(viewport->PlatformHandleRaw,renderer->diagnostic_);
    renderer->resizeViewport(*resource,viewport->Size,viewport->FramebufferScale);
    renderer->viewports_.push_back(resource.get());
    viewport->RendererUserData=resource.release();
}

void ImGuiBgfx::destroyViewport(ViewportResource* resource) {
    bgfx::resetView(resource->view);
    resource->surface->close(completedFrame_);
    if(resource->surface->pendingRetirements())retiredSurfaces_.push_back(std::move(resource->surface));
    resource->viewport->RendererUserData=nullptr;
    const auto found=std::find(viewports_.begin(),viewports_.end(),resource);
    if (found!=viewports_.end()) viewports_.erase(found);
    delete resource;
}

void ImGuiBgfx::destroyPlatformWindow(ImGuiViewport* viewport) {
    if (auto* resource=static_cast<ViewportResource*>(viewport->RendererUserData)) resource->renderer->destroyViewport(resource);
}

void ImGuiBgfx::resizePlatformWindow(ImGuiViewport* viewport,ImVec2 size) {
    if (auto* resource=static_cast<ViewportResource*>(viewport->RendererUserData))
        resource->renderer->resizeViewport(*resource,size,viewport->FramebufferScale);
}

void ImGuiBgfx::renderPlatformWindow(ImGuiViewport* viewport,void*) {
    auto* resource=static_cast<ViewportResource*>(viewport->RendererUserData);
    if (!resource || !viewport->DrawData || (viewport->Flags & ImGuiViewportFlags_IsMinimized)) return;
    auto* renderer=resource->renderer;
    renderer->resizeViewport(*resource,viewport->DrawData->DisplaySize,viewport->DrawData->FramebufferScale);
    const auto framebuffer=resource->surface->prepare(resource->width,resource->height,renderer->completedFrame_);
    if (!bgfx::isValid(framebuffer)) return;
    bgfx::setViewClear(resource->view,(viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? BGFX_CLEAR_NONE : BGFX_CLEAR_COLOR,0x000000ff);
    renderer->renderTo(viewport->DrawData,resource->view,framebuffer);
    resource->surface->rendered(renderer->completedFrame_+1);
    if(bgfx::isValid(renderer->platformTerminalFramebuffer_)) {
        // Pinned D3D12 setFrameBuffer transitions the preceding ordinary color
        // framebuffer to PIXEL_SHADER_RESOURCE. Reserve the final view so all
        // native copy barriers can use and restore that known state. A private
        // target also works while the main native window is minimized.
        const auto terminal=static_cast<bgfx::ViewId>(renderer->platformViewLimit());
        bgfx::setViewName(terminal,"ImGui platform copy boundary");
        bgfx::setViewFrameBuffer(terminal,renderer->platformTerminalFramebuffer_);
        bgfx::setViewRect(terminal,0,0,1,1);bgfx::setViewClear(terminal,BGFX_CLEAR_NONE);
        bgfx::touch(terminal);
    }
}

void ImGuiBgfx::swapPlatformWindow(ImGuiViewport*,void*) {
    // These callbacks run before bgfx executes the offscreen draws. App calls
    // postFrame after its one bgfx::frame to copy and present native surfaces.
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
    bgfx::setViewFrameBuffer(state.viewId,state.framebuffer);
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
    renderTo(drawData,viewId,BGFX_INVALID_HANDLE);
}

void ImGuiBgfx::renderTo(ImDrawData* drawData,uint16_t viewId,bgfx::FrameBufferHandle framebuffer) {
    if (!initialized_ || !drawData) return;
    if (drawData->Textures) for (ImTextureData* texture : *drawData->Textures)
        if (texture->Status != ImTextureStatus_OK) updateTexture(texture);
    const int width = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int height = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX) return;
    RenderState state{this, drawData, viewId, static_cast<uint16_t>(width), static_cast<uint16_t>(height), LinearSampler,framebuffer};
    setupView(state);
    bgfx::touch(viewId);
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
            const auto flags=(command.GetTexID() & RepeatTexture)?state.samplerFlags & ~LinearSampler:state.samplerFlags;
            bgfx::setTexture(0, sampler_, texture, flags);
            bgfx::submit(viewId, program_);
        }
    }
    platform.Renderer_RenderState = nullptr;
}

void ImGuiBgfx::shutdown() {
    if (!initialized_) return;
    // Renderer_DestroyWindow runs before SDL's Platform_DestroyWindow, while
    // all three owners (this renderer, the SDL backend, and context) are alive.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData==this) ImGui::DestroyPlatformWindows();
    while (!viewports_.empty()) destroyViewport(viewports_.back());
    while (!textures_.empty()) destroyTexture(textures_.back());
    if(bgfx::isValid(platformTerminalFramebuffer_)) {
        bgfx::resetView(static_cast<bgfx::ViewId>(platformViewLimit()));
        bgfx::destroy(platformTerminalFramebuffer_);
    }
    if(bgfx::isValid(platformTerminalTexture_))bgfx::destroy(platformTerminalTexture_);
    platformTerminalFramebuffer_=BGFX_INVALID_HANDLE;platformTerminalTexture_=BGFX_INVALID_HANDLE;platformTerminalBytes_=0;
    if (bgfx::isValid(sampler_)) bgfx::destroy(sampler_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    sampler_ = BGFX_INVALID_HANDLE; program_ = BGFX_INVALID_HANDLE;
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.BackendRendererUserData == this) {
            io.BackendRendererUserData = nullptr; io.BackendRendererName = nullptr;
            io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports);
            ImGui::GetPlatformIO().ClearRendererHandlers();
        }
    }
    initialized_ = false;
}

} // namespace crimson
