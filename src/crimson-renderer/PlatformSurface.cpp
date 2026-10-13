#include "PlatformSurface.h"
#include "resources/TextureMipCache.h"

#include <bgfx/platform.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace crimson {
namespace {
using Microsoft::WRL::ComPtr;
bool completed(std::uint32_t frame,std::uint32_t after) {return frame-after<UINT32_C(0x80000000);}
// Pinned bgfx's ordinary color-framebuffer postReset requires an SRV even
// when the UI never samples this target. Only this offscreen texture is readable.
constexpr auto TargetFlags=BGFX_TEXTURE_RT;
}

struct PlatformSurface::Impl {
    struct Target {
        std::uint16_t width{},height{};
        std::uint32_t readyAfter{},releaseAfter{};
        bool imported{};
        ComPtr<ID3D11Texture2D> native;
        bgfx::TextureHandle texture{BGFX_INVALID_HANDLE};
        bgfx::FrameBufferHandle framebuffer{BGFX_INVALID_HANDLE};
        void releaseGpu() {
            if(bgfx::isValid(framebuffer))bgfx::destroy(framebuffer);
            if(bgfx::isValid(texture))bgfx::destroy(texture);
            framebuffer=BGFX_INVALID_HANDLE;texture=BGFX_INVALID_HANDLE;
        }
    };
    HWND window{};
    Diagnostic diagnostic;
    std::string lastDiagnostic;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGISwapChain1> swapChain;
    std::uint16_t nativeWidth{},nativeHeight{};
    std::unique_ptr<Target> current,candidate;
    // At most two ordinary retirements, reserving two more slots for closing
    // the current target and candidate. COM owners survive bgfx cmdPost release.
    std::array<std::unique_ptr<Target>,4> retired;
    Capture capture;
    std::uint32_t renderedAfter{};
    bool submitted{},closed{};

    void report(const char* operation,HRESULT result,std::uint16_t width,std::uint16_t height) {
        RECT client{};const bool valid=IsWindow(window)!=FALSE;
        if(valid)GetClientRect(window,&client);
        char text[512]{};
        std::snprintf(text,sizeof text,
            "ImGui platform surface: %s failed HRESULT=0x%08lX HWND=%p valid=%d target=%ux%u client=%ldx%ld chain=%p native=%ux%u device=0x%08lX",
            operation,static_cast<unsigned long>(result),static_cast<void*>(window),valid?1:0,
            static_cast<unsigned>(width),static_cast<unsigned>(height),client.right-client.left,client.bottom-client.top,
            static_cast<void*>(swapChain.Get()),static_cast<unsigned>(nativeWidth),static_cast<unsigned>(nativeHeight),
            static_cast<unsigned long>(device ? device->GetDeviceRemovedReason() : S_OK));
        if(lastDiagnostic!=text) {
            lastDiagnostic=text;
            if(diagnostic)diagnostic(lastDiagnostic);else std::fprintf(stderr,"%s\n",text);
        }
    }
    bool ensureDevice() {
        if(device)return true;
        if(bgfx::getRendererType()!=bgfx::RendererType::Direct3D11 ||
            (bgfx::getCaps()->supported & BGFX_CAPS_RENDERER_MULTITHREADED)) {
            report("render-thread contract",E_NOTIMPL,0,0);return false;
        }
        const auto* internal=bgfx::getInternalData();
        if(!internal || !internal->context){report("D3D11 device",E_POINTER,0,0);return false;}
        // Own these COM references under the bgfx lifetime. getInternalData's
        // borrowed device is only accessed on Crimson's established render thread.
        ComPtr<ID3D11Device> nextDevice=static_cast<ID3D11Device*>(internal->context);
        ComPtr<ID3D11DeviceContext> nextContext;nextDevice->GetImmediateContext(&nextContext);
        ComPtr<IDXGIDevice> dxgi;auto result=nextDevice.As(&dxgi);
        ComPtr<IDXGIAdapter> adapter;
        if(SUCCEEDED(result))result=dxgi->GetAdapter(&adapter);
        ComPtr<IDXGIFactory2> nextFactory;
        if(SUCCEEDED(result))result=adapter->GetParent(IID_PPV_ARGS(&nextFactory));
        if(FAILED(result) || !nextContext){report("DXGI factory",FAILED(result)?result:E_POINTER,0,0);return false;}
        device=std::move(nextDevice);context=std::move(nextContext);factory=std::move(nextFactory);return true;
    }
    bool ensureChain(std::uint16_t width,std::uint16_t height) {
        if(!IsWindow(window)){report("native window",E_HANDLE,width,height);return false;}
        RECT client{};
        if(!GetClientRect(window,&client) || client.right<=client.left || client.bottom<=client.top || IsIconic(window))return false;
        if(swapChain && width==nativeWidth && height==nativeHeight)return true;
        HRESULT result;
        if(!swapChain) {
            DXGI_SWAP_CHAIN_DESC1 description{};
            description.Width=width;description.Height=height;description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count=1;description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount=2;description.Scaling=DXGI_SCALING_STRETCH;
            description.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;description.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
            result=factory->CreateSwapChainForHwnd(device.Get(),window,&description,nullptr,nullptr,&swapChain);
            if(FAILED(result)){swapChain.Reset();report("CreateSwapChainForHwnd",result,width,height);return false;}
            factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
        } else {
            // No backbuffer RTV/SRV or imported bgfx reference exists. GetBuffer
            // references are scoped to postFrame and released before this call.
            context->Flush();
            result=swapChain->ResizeBuffers(2,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,0);
            if(FAILED(result)){report("ResizeBuffers",result,width,height);return false;}
        }
        nativeWidth=width;nativeHeight=height;lastDiagnostic.clear();return true;
    }
    std::size_t retiredCount() const {
        return std::count_if(retired.begin(),retired.end(),[](const auto& value){return bool(value);});
    }
    void collect(std::uint32_t frame) {
        for(auto& value:retired)if(value && completed(frame,value->releaseAfter))value.reset();
    }
    bool retire(std::unique_ptr<Target>& target,std::uint32_t frame) {
        if(!target)return true;
        const auto slot=std::find_if(retired.begin(),retired.end(),[](const auto& value){return !value;});
        if(slot==retired.end())return false;
        target->releaseGpu();target->releaseAfter=frame+1;*slot=std::move(target);return true;
    }
    void retireClosed(std::uint32_t frame) {
        retire(current,frame);retire(candidate,frame);
    }
    std::unique_ptr<Target> allocate(std::uint16_t width,std::uint16_t height,std::uint32_t frame) {
        auto target=std::make_unique<Target>();target->width=width;target->height=height;target->readyAfter=frame+1;
        D3D11_TEXTURE2D_DESC description{};
        description.Width=width;description.Height=height;description.MipLevels=1;description.ArraySize=1;
        description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;
        description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        const auto result=device->CreateTexture2D(&description,nullptr,&target->native);
        if(FAILED(result)){report("offscreen CreateTexture2D",result,width,height);return {};}
        // This placeholder must first pass through bgfx cmdPre. A pointer import
        // is only legal once its texture wrapper actually exists on the GPU.
        target->texture=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,TargetFlags);
        if(!bgfx::isValid(target->texture)){report("bgfx texture wrapper",E_OUTOFMEMORY,width,height);return {};}
        return target;
    }
    bgfx::FrameBufferHandle prepare(std::uint16_t width,std::uint16_t height,std::uint32_t frame) {
        collect(frame);
        if(closed || !width || !height || !ensureDevice())return BGFX_INVALID_HANDLE;
        if(candidate && (candidate->width!=width || candidate->height!=height)) {
            if(retiredCount()>=2 || !retire(candidate,frame))return BGFX_INVALID_HANDLE;
        }
        if(current && current->width==width && current->height==height && swapChain && nativeWidth==width && nativeHeight==height)
            return current->framebuffer;
        if(!candidate) {
            if(retiredCount()>=2)return BGFX_INVALID_HANDLE;
            candidate=allocate(width,height,frame);return BGFX_INVALID_HANDLE;
        }
        if(!completed(frame,candidate->readyAfter))return BGFX_INVALID_HANDLE;
        if(!candidate->imported) {
            const auto pointer=bgfx::overrideInternal(candidate->texture,reinterpret_cast<std::uintptr_t>(candidate->native.Get()));
            if(!pointer){report("bgfx texture import",E_PENDING,width,height);return BGFX_INVALID_HANDLE;}
            candidate->imported=true;
        }
        if(!bgfx::isValid(candidate->framebuffer)) {
            candidate->framebuffer=bgfx::createFrameBuffer(1,&candidate->texture,false);
            if(!bgfx::isValid(candidate->framebuffer)){report("bgfx offscreen framebuffer",E_OUTOFMEMORY,width,height);return BGFX_INVALID_HANDLE;}
            bgfx::setName(candidate->framebuffer,"ImGui imported offscreen framebuffer");
        }
        if(retiredCount()>=2 || !ensureChain(width,height))return BGFX_INVALID_HANDLE;
        retire(current,frame);current=std::move(candidate);return current->framebuffer;
    }
    bool captureBackbuffer(ID3D11Texture2D* backbuffer) {
        D3D11_TEXTURE2D_DESC description{};backbuffer->GetDesc(&description);
        description.Usage=D3D11_USAGE_STAGING;description.BindFlags=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;auto result=device->CreateTexture2D(&description,nullptr,&staging);
        if(FAILED(result)){report("capture CreateTexture2D",result,nativeWidth,nativeHeight);return false;}
        context->CopyResource(staging.Get(),backbuffer);
        D3D11_MAPPED_SUBRESOURCE mapping{};result=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapping);
        if(FAILED(result)){report("capture Map",result,nativeWidth,nativeHeight);return false;}
        struct Unmap {
            ID3D11DeviceContext* context;
            ID3D11Texture2D* texture;
            ~Unmap(){context->Unmap(texture,0);}
        } unmap{context.Get(),staging.Get()};
        DecodedImage image{nativeWidth,nativeHeight,std::vector<std::uint8_t>(std::size_t(nativeWidth)*nativeHeight*4)};
        for(std::uint32_t y=0;y<nativeHeight;++y)
            std::memcpy(image.rgba.data()+std::size_t(y)*nativeWidth*4,static_cast<const std::uint8_t*>(mapping.pData)+std::size_t(y)*mapping.RowPitch,std::size_t(nativeWidth)*4);
        auto request=std::move(capture);capture={};request(image);return true;
    }
    bool present(std::uint32_t frame,bool synchronize) {
        collect(frame);
        if(closed){retireClosed(frame);return false;}
        if(!submitted || !completed(frame,renderedAfter))return false;
        submitted=false;
        if(!current || !swapChain || current->width!=nativeWidth || current->height!=nativeHeight || IsIconic(window))return false;
        ComPtr<ID3D11Texture2D> backbuffer;
        auto result=swapChain->GetBuffer(0,IID_PPV_ARGS(&backbuffer));
        if(FAILED(result)){report("GetBuffer",result,nativeWidth,nativeHeight);return false;}
        D3D11_TEXTURE2D_DESC description{};backbuffer->GetDesc(&description);
        if(description.Width!=nativeWidth || description.Height!=nativeHeight || description.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
            description.SampleDesc.Count!=1 || description.ArraySize!=1 || description.MipLevels!=1) {
            report("CopyResource dimensions/format",E_INVALIDARG,nativeWidth,nativeHeight);return false;
        }
        context->CopyResource(backbuffer.Get(),current->native.Get());
        // Flip-discard may invalidate or rotate buffers at Present. Diagnostic
        // requests inspect the actual native pixels between CopyResource/Present.
        if(capture) {
            try {captureBackbuffer(backbuffer.Get());}
            catch(...) {report("capture callback/allocation",E_FAIL,nativeWidth,nativeHeight);}
            // A diagnostic failure must not block normal window presentation or
            // leave an unbounded retry request behind. Mapping uses an Unmap guard.
            capture={};
        }
        backbuffer.Reset();
        const bool synchronized=synchronize && IsWindowVisible(window);
        result=swapChain->Present(synchronized ? 1 : 0,0);
        if(FAILED(result))report("Present",result,nativeWidth,nativeHeight);
        return synchronized && result==S_OK;
    }
};

PlatformSurface::PlatformSurface(void* window,Diagnostic diagnostic):impl_(std::make_unique<Impl>()) {
    impl_->window=static_cast<HWND>(window);impl_->diagnostic=std::move(diagnostic);
}
PlatformSurface::~PlatformSurface()=default;
void PlatformSurface::beginFrame(std::uint32_t frame){impl_->collect(frame);if(impl_->closed)impl_->retireClosed(frame);}
bgfx::FrameBufferHandle PlatformSurface::prepare(std::uint16_t width,std::uint16_t height,std::uint32_t frame){return impl_->prepare(width,height,frame);}
void PlatformSurface::rendered(std::uint32_t after){impl_->renderedAfter=after;impl_->submitted=true;}
bool PlatformSurface::postFrame(std::uint32_t frame,bool synchronize){return impl_->present(frame,synchronize);}
void PlatformSurface::close(std::uint32_t frame) {
    if(impl_->closed)return;
    impl_->closed=true;impl_->submitted=false;impl_->capture={};impl_->retireClosed(frame);
    impl_->swapChain.Reset();impl_->nativeWidth=impl_->nativeHeight=0;
    // The backbuffer was never bound as an output/SRV. Flush the released native
    // chain immediately, before SDL destroys this HWND or can reuse its value.
    if(impl_->context)impl_->context->Flush();
}
bool PlatformSurface::pendingRetirements() const noexcept{return impl_->retiredCount()!=0 || (impl_->closed && (impl_->current || impl_->candidate));}
bgfx::FrameBufferHandle PlatformSurface::framebuffer() const noexcept{return impl_->current ? impl_->current->framebuffer : bgfx::FrameBufferHandle{bgfx::kInvalidHandle};}
std::uintptr_t PlatformSurface::swapChainIdentity() const noexcept{return reinterpret_cast<std::uintptr_t>(impl_->swapChain.Get());}
bool PlatformSurface::requestCapture(Capture capture) {
    if(impl_->closed || impl_->capture || !capture)return false;
    impl_->capture=std::move(capture);return true;
}
}
