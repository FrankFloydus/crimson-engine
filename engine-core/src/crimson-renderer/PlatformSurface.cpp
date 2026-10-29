#include "PlatformSurface.h"
#include "resources/TextureMipCache.h"

#include <bgfx/platform.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <utility>
#include <chrono>

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
        ComPtr<ID3D12Resource> native12;
        std::uint64_t bytes{},retirementFence{};
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
    bool direct12{};
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent{};
    std::uint64_t nextFence{};
    bool failed{},unfencedWork{},chainRetiring{},stalled{};
    std::chrono::steady_clock::time_point recoverAfter{};
    Impl* nextQuarantined{};
    // A driver timeout is not proof that submitted GPU references are gone.
    // Exceptional shutdown keeps these owners until a later poll observes a
    // completed marker or confirmed device removal. Raw intrusive storage also
    // avoids allocating from a noexcept destructor/failure path.
    static Impl* quarantined;
    static void collectQuarantined() {
        auto** cursor=&quarantined;
        while(auto* value=*cursor) {
            value->recoverUnfencedWork();
            if(value->canDestroyNativeOwner()) {*cursor=value->nextQuarantined;delete value;}
            else cursor=&value->nextQuarantined;
        }
    }
    static std::size_t quarantinedCount() {
        std::size_t count{};for(auto* value=quarantined;value;value=value->nextQuarantined)++count;return count;
    }
    static std::uint64_t quarantinedBytes() {
        std::uint64_t bytes{};for(auto* value=quarantined;value;value=value->nextQuarantined)bytes+=value->residentBytes();return bytes;
    }
    struct CopySlot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Resource> backbuffer,readback;
        std::uint64_t done{},readbackBytes{};
    };
    std::array<CopySlot,3> copies;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGISwapChain1> swapChain;
    std::uint16_t nativeWidth{},nativeHeight{};
    std::uint64_t swapChainBytes{};
    std::unique_ptr<Target> current,candidate;
    // At most two ordinary retirements, reserving two more slots for closing
    // the current target and candidate. COM owners survive bgfx cmdPost release.
    std::array<std::unique_ptr<Target>,4> retired;
    Capture capture;
    std::uint32_t renderedAfter{};
    bool submitted{},closed{};
    ~Impl() {if(fenceEvent)CloseHandle(fenceEvent);}
    bool deviceRemoved() const {return device12 && FAILED(device12->GetDeviceRemovedReason());}
    bool nativeWorkCompleted() const {
        if(!direct12 || !queue || deviceRemoved())return true;
        const auto done=fence->GetCompletedValue();
        return !unfencedWork && done!=UINT64_MAX && done>=nextFence;
    }
    bool canDestroyNativeOwner() const {
        if(!direct12 || deviceRemoved())return true;
        if(!nativeWorkCompleted() || current || candidate)return false;
        // A marked retirement proves bgfx cmdPost consumed its wrappers before
        // the GPU marker was queued. Without that proof, retain the owner for
        // process lifetime rather than guessing across a torn-down context.
        for(const auto& target:retired)if(target && !target->retirementFence)return false;
        return true;
    }
    void recoverUnfencedWork() {
        if(!unfencedWork || deviceRemoved() || std::chrono::steady_clock::now()<recoverAfter)return;
        recoverAfter=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        std::uint64_t marker{};signal(marker);
    }
    bool waitNativeWork() {
        recoverUnfencedWork();
        if(nativeWorkCompleted())return true;
        if(unfencedWork)return false;
        return waitFence(nextFence);
    }
    void releaseChainIfSafe() {
        if(!chainRetiring || !nativeWorkCompleted())return;
        for(auto& slot:copies) {slot.backbuffer.Reset();slot.readback.Reset();slot.readbackBytes=0;}
        swapChain.Reset();nativeWidth=nativeHeight=0;swapChainBytes=0;chainRetiring=false;
    }
    HRESULT removedReason() const {
        return device12 ? device12->GetDeviceRemovedReason() : (device ? device->GetDeviceRemovedReason() : S_OK);
    }
    bool waitFence(std::uint64_t value) {
        if(!value || deviceRemoved())return true;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        HRESULT result=S_OK;
        for(;;) {
            const auto completedValue=fence->GetCompletedValue();
            if(completedValue!=UINT64_MAX && completedValue>=value)return true;
            if(deviceRemoved())return true;
            const auto now=std::chrono::steady_clock::now();if(now>=deadline)break;
            ResetEvent(fenceEvent);
            result=fence->SetEventOnCompletion(value,fenceEvent);if(FAILED(result))break;
            const auto remaining=std::max<std::int64_t>(1,std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now).count());
            const auto wait=WaitForSingleObject(fenceEvent,static_cast<DWORD>(remaining));
            if(wait!=WAIT_OBJECT_0)break;
            // A previous timed-out marker can also signal this reusable event.
            // Only the fence value establishes completion of the requested work.
        }
        if(deviceRemoved())return true;
        stalled=true;
        report("D3D12 fence wait; resources retained",FAILED(result)?result:DXGI_ERROR_WAIT_TIMEOUT,nativeWidth,nativeHeight);return false;
    }
    bool signal(std::uint64_t& value) {
        const auto next=nextFence+1;
        const auto result=queue->Signal(fence.Get(),next);
        if(FAILED(result)) {
            // ExecuteCommandLists may already have consumed a command allocator
            // and texture. Never reuse or release either on an old fence value.
            failed=true;unfencedWork=true;
            report("D3D12 queue Signal; surface quarantined",result,nativeWidth,nativeHeight);return false;
        }
        nextFence=next;value=next;unfencedWork=false;return true;
    }

    void report(const char* operation,HRESULT result,std::uint16_t width,std::uint16_t height) {
        RECT client{};const bool valid=IsWindow(window)!=FALSE;
        if(valid)GetClientRect(window,&client);
        char text[512]{};
        std::snprintf(text,sizeof text,
            "ImGui platform surface: %s failed HRESULT=0x%08lX HWND=%p valid=%d target=%ux%u client=%ldx%ld chain=%p native=%ux%u device=0x%08lX",
            operation,static_cast<unsigned long>(result),static_cast<void*>(window),valid?1:0,
            static_cast<unsigned>(width),static_cast<unsigned>(height),client.right-client.left,client.bottom-client.top,
            static_cast<void*>(swapChain.Get()),static_cast<unsigned>(nativeWidth),static_cast<unsigned>(nativeHeight),
            static_cast<unsigned long>(removedReason()));
        if(lastDiagnostic!=text) {
            lastDiagnostic=text;
            if(diagnostic)diagnostic(lastDiagnostic);else std::fprintf(stderr,"%s\n",text);
        }
    }
    bool ensureDevice() {
        if(device || device12)return true;
        const auto type=bgfx::getRendererType();
        if((type!=bgfx::RendererType::Direct3D11 && type!=bgfx::RendererType::Direct3D12) ||
            (bgfx::getCaps()->supported & BGFX_CAPS_RENDERER_MULTITHREADED)) {
            report("render-thread contract",E_NOTIMPL,0,0);return false;
        }
        const auto* internal=bgfx::getInternalData();
        if(!internal || !internal->context){report("D3D device",E_POINTER,0,0);return false;}
        direct12=type==bgfx::RendererType::Direct3D12;
        ComPtr<IDXGIFactory2> nextFactory;
        if(direct12) {
            ComPtr<ID3D12Device> nextDevice=static_cast<ID3D12Device*>(internal->context);
            ComPtr<ID3D12CommandQueue> nextQueue;
            UINT size=sizeof(ID3D12CommandQueue*);
            // The pinned bgfx backend deliberately publishes its direct queue on
            // the device under this interface GUID. Sharing that queue orders
            // our copies after bgfx without a frame-wide CPU/GPU synchronization.
            auto result=nextDevice->GetPrivateData(__uuidof(ID3D12CommandQueue),&size,nextQueue.GetAddressOf());
            if(FAILED(result) || size!=sizeof(ID3D12CommandQueue*) || !nextQueue || nextQueue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT) {
                report("D3D12 bgfx direct queue contract",FAILED(result)?result:E_NOINTERFACE,0,0);return false;
            }
            ComPtr<IDXGIFactory4> factory4;
            result=CreateDXGIFactory1(IID_PPV_ARGS(&factory4));
            ComPtr<IDXGIAdapter> adapter;
            if(SUCCEEDED(result))result=factory4->EnumAdapterByLuid(nextDevice->GetAdapterLuid(),IID_PPV_ARGS(&adapter));
            if(SUCCEEDED(result))result=adapter->GetParent(IID_PPV_ARGS(&nextFactory));
            ComPtr<ID3D12Fence> nextFenceObject;
            if(SUCCEEDED(result))result=nextDevice->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&nextFenceObject));
            HANDLE event=SUCCEEDED(result)?CreateEventW(nullptr,FALSE,FALSE,nullptr):nullptr;
            if(FAILED(result) || !event){report("D3D12 surface factory/fence",FAILED(result)?result:HRESULT_FROM_WIN32(GetLastError()),0,0);return false;}
            device12=std::move(nextDevice);queue=std::move(nextQueue);fence=std::move(nextFenceObject);fenceEvent=event;
            queue->SetName(L"Crimson shared bgfx graphics queue");fence->SetName(L"Crimson platform presentation fence");
        } else {
            // Borrowed bgfx devices are accessed only on Crimson's established
            // initialization/render thread; the owner keeps COM references.
            ComPtr<ID3D11Device> nextDevice=static_cast<ID3D11Device*>(internal->context);
            ComPtr<ID3D11DeviceContext> nextContext;nextDevice->GetImmediateContext(&nextContext);
            ComPtr<IDXGIDevice> dxgi;auto result=nextDevice.As(&dxgi);
            ComPtr<IDXGIAdapter> adapter;
            if(SUCCEEDED(result))result=dxgi->GetAdapter(&adapter);
            if(SUCCEEDED(result))result=adapter->GetParent(IID_PPV_ARGS(&nextFactory));
            if(FAILED(result) || !nextContext){report("DXGI factory",FAILED(result)?result:E_POINTER,0,0);return false;}
            device=std::move(nextDevice);context=std::move(nextContext);
        }
        factory=std::move(nextFactory);return true;
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
            result=factory->CreateSwapChainForHwnd(direct12?static_cast<IUnknown*>(queue.Get()):static_cast<IUnknown*>(device.Get()),window,&description,nullptr,nullptr,&swapChain);
            if(FAILED(result)){swapChain.Reset();report("CreateSwapChainForHwnd",result,width,height);return false;}
            factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
        } else {
            // No backbuffer RTV/SRV or imported bgfx reference exists. GetBuffer
            // references are scoped to postFrame and released before this call.
            if(direct12) {
                if(!waitNativeWork())return false;
                for(auto& slot:copies)slot.backbuffer.Reset();
            } else context->Flush();
            result=swapChain->ResizeBuffers(2,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,0);
            if(FAILED(result)){report("ResizeBuffers",result,width,height);return false;}
        }
        nativeWidth=width;nativeHeight=height;swapChainBytes=std::uint64_t(width)*height*4*2;
        if(direct12)for(UINT index=0;index<2;++index) {
            ComPtr<ID3D12Resource> buffer;
            if(SUCCEEDED(swapChain->GetBuffer(index,IID_PPV_ARGS(&buffer)))) {
                wchar_t name[160]{};std::swprintf(name,std::size(name),L"Crimson platform swap-chain buffer %u HWND=%p %ux%u",index,static_cast<void*>(window),static_cast<unsigned>(width),static_cast<unsigned>(height));
                buffer->SetName(name);
            }
        }
        if(direct12) {
            D3D12_RESOURCE_DESC description{};description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width=width;description.Height=height;description.DepthOrArraySize=1;description.MipLevels=1;
            description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;description.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            swapChainBytes=device12->GetResourceAllocationInfo(0,1,&description).SizeInBytes*2;
        }
        lastDiagnostic.clear();return true;
    }
    std::size_t retiredCount() const {
        return std::count_if(retired.begin(),retired.end(),[](const auto& value){return bool(value);});
    }
    void collect(std::uint32_t frame) {
        recoverUnfencedWork();
        if(stalled && nativeWorkCompleted())stalled=false;
        releaseChainIfSafe();
        for(auto& slot:copies)if(fence && !unfencedWork && (deviceRemoved() || (fence->GetCompletedValue()!=UINT64_MAX && fence->GetCompletedValue()>=slot.done))) {
            slot.backbuffer.Reset();slot.readback.Reset();slot.readbackBytes=0;
        }
        for(auto& value:retired)if(value && completed(frame,value->releaseAfter)) {
            if(direct12 && !deviceRemoved()) {
                // The release command has now passed through bgfx cmdPost. A
                // queue fence also covers its pending native graphics accesses.
                if(unfencedWork)continue;
                if(!value->retirementFence && !signal(value->retirementFence))continue;
                if(unfencedWork || fence->GetCompletedValue()==UINT64_MAX || fence->GetCompletedValue()<value->retirementFence)continue;
            }
            value.reset();
        }
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
        if(direct12) {
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC description{};description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width=width;description.Height=height;description.DepthOrArraySize=1;description.MipLevels=1;
            description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;
            description.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            const auto result=device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
                D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&target->native12));
            if(FAILED(result)){report("offscreen D3D12 CreateCommittedResource",result,width,height);return {};}
            wchar_t name[160]{};std::swprintf(name,std::size(name),L"Crimson platform imported offscreen target HWND=%p %ux%u",static_cast<void*>(window),static_cast<unsigned>(width),static_cast<unsigned>(height));
            target->native12->SetName(name);
            target->bytes=device12->GetResourceAllocationInfo(0,1,&description).SizeInBytes;
        } else {
            D3D11_TEXTURE2D_DESC description{};
            description.Width=width;description.Height=height;description.MipLevels=1;description.ArraySize=1;
            description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;
            description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            const auto result=device->CreateTexture2D(&description,nullptr,&target->native);
            if(FAILED(result)){report("offscreen CreateTexture2D",result,width,height);return {};}
            target->bytes=std::uint64_t(width)*height*4;
        }
        // This placeholder must first pass through bgfx cmdPre. A pointer import
        // is only legal once its texture wrapper actually exists on the GPU.
        target->texture=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,TargetFlags);
        if(!bgfx::isValid(target->texture)){report("bgfx texture wrapper",E_OUTOFMEMORY,width,height);return {};}
        bgfx::setName(target->texture,"Crimson platform offscreen import placeholder");
        return target;
    }
    bgfx::FrameBufferHandle prepare(std::uint16_t width,std::uint16_t height,std::uint32_t frame) {
        collect(frame);
        if(closed || failed || stalled || !width || !height || !ensureDevice())return BGFX_INVALID_HANDLE;
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
            const auto pointer=bgfx::overrideInternal(candidate->texture,direct12?reinterpret_cast<std::uintptr_t>(candidate->native12.Get()):reinterpret_cast<std::uintptr_t>(candidate->native.Get()));
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
    static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};return barrier;
    }
    bool present12(bool synchronize) {
        if(failed || stalled || unfencedWork || deviceRemoved())return false;
        auto found=std::find_if(copies.begin(),copies.end(),[this](const auto& slot){return !slot.done || (fence->GetCompletedValue()!=UINT64_MAX && fence->GetCompletedValue()>=slot.done);});
        if(found==copies.end())return false; // bounded queue: keep the last displayed frame instead of allocating.
        auto& slot=*found;slot.backbuffer.Reset();slot.readback.Reset();slot.readbackBytes=0;
        HRESULT result=S_OK;
        if(!slot.allocator)result=device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator));
        if(SUCCEEDED(result))result=slot.allocator->Reset();
        if(SUCCEEDED(result)) {
            if(slot.commands)result=slot.commands->Reset(slot.allocator.Get(),nullptr);
            else result=device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.commands));
        }
        ComPtr<IDXGISwapChain3> chain;
        if(SUCCEEDED(result))result=swapChain.As(&chain);
        if(SUCCEEDED(result))result=chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&slot.backbuffer));
        if(FAILED(result)){if(slot.commands)slot.commands->Close();report("D3D12 copy allocator/list/backbuffer",result,nativeWidth,nativeHeight);return false;}
        const auto description=slot.backbuffer->GetDesc();
        if(description.Width!=nativeWidth || description.Height!=nativeHeight || description.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) {
            slot.commands->Close();report("D3D12 CopyResource dimensions/format",E_INVALIDARG,nativeWidth,nativeHeight);return false;
        }
        // ImGui's reserved terminal view switches away from all platform
        // framebuffers, leaving every imported color target in this state.
        std::array barriers{transition(current->native12.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition(slot.backbuffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST)};
        slot.commands->ResourceBarrier(static_cast<UINT>(barriers.size()),barriers.data());
        slot.commands->CopyResource(slot.backbuffer.Get(),current->native12.Get());
        ComPtr<ID3D12Resource> readback;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        std::uint64_t readbackBytes{};
        if(capture) {
            device12->GetCopyableFootprints(&description,0,1,0,&footprint,nullptr,nullptr,&readbackBytes);
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=readbackBytes;
            buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            result=device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback));
            if(SUCCEEDED(result)) {
                readback->SetName(L"Crimson platform diagnostic backbuffer readback");
                const auto readable=transition(slot.backbuffer.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
                slot.commands->ResourceBarrier(1,&readable);
                D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=slot.backbuffer.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;
                slot.commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
            } else report("D3D12 capture readback allocation",result,nativeWidth,nativeHeight);
        }
        std::array restore{transition(current->native12.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            transition(slot.backbuffer.Get(),readback?D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT)};
        slot.commands->ResourceBarrier(static_cast<UINT>(restore.size()),restore.data());
        slot.readback=readback;
        if(readback) {const auto allocation=readback->GetDesc();slot.readbackBytes=device12->GetResourceAllocationInfo(0,1,&allocation).SizeInBytes;}
        result=slot.commands->Close();
        if(FAILED(result)){report("D3D12 copy Close",result,nativeWidth,nativeHeight);capture={};return false;}
        ID3D12CommandList* lists[]{slot.commands.Get()};queue->ExecuteCommandLists(1,lists);
        unfencedWork=true;slot.done=UINT64_MAX;
        if(!signal(slot.done)){capture={};return false;}
        if(capture) {
            try {
                if(readback && waitFence(slot.done)) {
                    void* pixels{};D3D12_RANGE read{0,static_cast<SIZE_T>(readbackBytes)};
                    result=readback->Map(0,&read,&pixels);
                    if(SUCCEEDED(result)) {
                        struct Unmap {
                            ID3D12Resource* resource;
                            ~Unmap(){D3D12_RANGE written{0,0};resource->Unmap(0,&written);}
                        } unmap{readback.Get()};
                        DecodedImage image{nativeWidth,nativeHeight,std::vector<std::uint8_t>(std::size_t(nativeWidth)*nativeHeight*4)};
                        for(std::uint32_t y=0;y<nativeHeight;++y)
                            std::memcpy(image.rgba.data()+std::size_t(y)*nativeWidth*4,static_cast<const std::uint8_t*>(pixels)+footprint.Offset+std::size_t(y)*footprint.Footprint.RowPitch,std::size_t(nativeWidth)*4);
                        auto request=std::move(capture);capture={};request(image);
                    } else report("D3D12 capture Map",result,nativeWidth,nativeHeight);
                }
            } catch(...) {report("D3D12 capture callback/allocation",E_FAIL,nativeWidth,nativeHeight);}
            capture={};
        }
        const bool synchronized=synchronize && IsWindowVisible(window);
        // Present enqueues its own graphics-queue accesses to the chain buffer.
        // The earlier marker is sufficient for diagnostic readback, but cannot
        // establish safe ResizeBuffers/final-release or copy-slot retirement.
        unfencedWork=true;slot.done=UINT64_MAX;
        result=swapChain->Present(synchronized?1:0,0);
        if(FAILED(result))report("D3D12 Present",result,nativeWidth,nativeHeight);
        if(!signal(slot.done))return false;
        return synchronized && result==S_OK;
    }
    std::uint64_t residentBytes() const {
        std::uint64_t total=swapChainBytes;
        for(const auto& slot:copies)total+=slot.readbackBytes;
        if(current)total+=current->bytes;
        if(candidate)total+=candidate->bytes;
        for(const auto& value:retired)if(value)total+=value->bytes;
        return total;
    }
    bool present(std::uint32_t frame,bool synchronize) {
        collect(frame);
        if(closed){retireClosed(frame);return false;}
        if(!submitted || !completed(frame,renderedAfter))return false;
        submitted=false;
        if(!current || !swapChain || current->width!=nativeWidth || current->height!=nativeHeight || IsIconic(window))return false;
        if(direct12)return present12(synchronize);
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

PlatformSurface::Impl* PlatformSurface::Impl::quarantined{};
PlatformSurface::PlatformSurface(void* window,Diagnostic diagnostic):impl_(std::make_unique<Impl>()) {
    Impl::collectQuarantined();
    impl_->window=static_cast<HWND>(window);impl_->diagnostic=std::move(diagnostic);
}
PlatformSurface::~PlatformSurface() {
    if(impl_ && !impl_->canDestroyNativeOwner()) {
        // The application already performed its bounded close/drain. Keep the
        // native references alive if the driver still owns them, including when
        // Signal failed and no trustworthy completion marker exists yet.
        // Diagnostic callbacks may borrow the editor/test owner being torn
        // down. The quarantined owner reports only to stderr from this point.
        impl_->diagnostic={};impl_->capture={};
        auto* retained=impl_.release();retained->nextQuarantined=Impl::quarantined;Impl::quarantined=retained;
    }
}
void PlatformSurface::beginFrame(std::uint32_t frame){Impl::collectQuarantined();impl_->collect(frame);if(impl_->closed)impl_->retireClosed(frame);}
bgfx::FrameBufferHandle PlatformSurface::prepare(std::uint16_t width,std::uint16_t height,std::uint32_t frame){return impl_->prepare(width,height,frame);}
void PlatformSurface::rendered(std::uint32_t after){impl_->renderedAfter=after;impl_->submitted=true;}
bool PlatformSurface::postFrame(std::uint32_t frame,bool synchronize){return impl_->present(frame,synchronize);}
void PlatformSurface::close(std::uint32_t frame) {
    if(impl_->closed)return;
    impl_->closed=true;impl_->submitted=false;impl_->capture={};impl_->retireClosed(frame);
    if(impl_->direct12 && impl_->queue) {
        impl_->chainRetiring=true;
        impl_->waitNativeWork();impl_->releaseChainIfSafe();
    } else {impl_->swapChain.Reset();impl_->nativeWidth=impl_->nativeHeight=0;impl_->swapChainBytes=0;}
    // The backbuffer was never bound as an output/SRV. Flush the released native
    // chain immediately, before SDL destroys this HWND or can reuse its value.
    if(impl_->context)impl_->context->Flush();
}
bool PlatformSurface::pendingRetirements() const noexcept{return impl_->retiredCount()!=0 || impl_->chainRetiring || (impl_->closed && (impl_->current || impl_->candidate || !impl_->nativeWorkCompleted()));}
bgfx::FrameBufferHandle PlatformSurface::framebuffer() const noexcept{return impl_->current ? impl_->current->framebuffer : bgfx::FrameBufferHandle{bgfx::kInvalidHandle};}
std::uint64_t PlatformSurface::residentBytes() const noexcept{return impl_->residentBytes();}
std::uint64_t PlatformSurface::quarantinedResidentBytes() noexcept{return Impl::quarantinedBytes();}
std::size_t PlatformSurface::quarantinedSurfaceCount() noexcept{return Impl::quarantinedCount();}
std::uint64_t PlatformSurface::colorTargetAllocationBytes(std::uint16_t width,std::uint16_t height) noexcept {
    if(bgfx::getRendererType()!=bgfx::RendererType::Direct3D12)return std::uint64_t(width)*height*4;
    const auto* internal=bgfx::getInternalData();if(!internal || !internal->context)return 0;
    D3D12_RESOURCE_DESC description{};description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width=width;description.Height=height;description.DepthOrArraySize=1;description.MipLevels=1;
    description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;description.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    return static_cast<ID3D12Device*>(internal->context)->GetResourceAllocationInfo(0,1,&description).SizeInBytes;
}
std::uintptr_t PlatformSurface::swapChainIdentity() const noexcept{return reinterpret_cast<std::uintptr_t>(impl_->swapChain.Get());}
bool PlatformSurface::requestCapture(Capture capture) {
    if(impl_->closed || impl_->capture || !capture)return false;
    impl_->capture=std::move(capture);return true;
}
}
