#include <crimson/renderer/BorrowedD3D12.hpp>
#include <crimson/renderer/core/DeviceSession.hpp>
#include <bgfx/d3d12.h>
#include <bgfx/platform.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !defined(BGFX_CRIMSON_D3D12_ASYNC_READBACK_VERSION) || BGFX_CRIMSON_D3D12_ASYNC_READBACK_VERSION != 1
#error The maintained bgfx D3D12 asynchronous readback overlay is required.
#endif

namespace {
using Microsoft::WRL::ComPtr;
using namespace crimson::shared;
using Clock=std::chrono::steady_clock;
using Readback=bgfx::D3D12ReadbackStatus;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void hr(HRESULT value,const char* message){require(SUCCEEDED(value),message);}
void status(Status value){if(!value.ok())throw std::runtime_error(std::string(value.error_message()));}
struct Host {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12InfoQueue> messages;
    Host(){
        ComPtr<ID3D12Debug> debug;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))){
            debug->EnableDebugLayer();ComPtr<ID3D12Debug1> extended;
            if(SUCCEEDED(debug.As(&extended)))extended->SetEnableGPUBasedValidation(TRUE);
            std::cout<<"D3D12 validation enabled"<<(extended?" with GPU validation":"")<<'\n';
        }else std::cout<<"D3D12 validation unavailable; native contract checks remain active.\n";
        ComPtr<IDXGIFactory6> factory;hr(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Cannot create DXGI factory.");
        for(UINT index=0;;++index){
            ComPtr<IDXGIAdapter1> adapter;
            const auto result=factory->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter));
            if(result==DXGI_ERROR_NOT_FOUND)break;hr(result,"Cannot enumerate native adapter.");
            DXGI_ADAPTER_DESC1 desc{};hr(adapter->GetDesc1(&desc),"Cannot inspect native adapter.");
            if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))break;
        }
        require(bool(device),"Native readback regression requires a hardware DX12 device.");
        D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)),"Cannot create exact DIRECT queue.");
        if(SUCCEEDED(device.As(&messages))){
            messages->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,FALSE);
            messages->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION,FALSE);
            messages->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,FALSE);
        }
    }
    void validate(){
        if(!messages)return;
        for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
            SIZE_T size{};hr(messages->GetMessage(i,nullptr,&size),"Cannot size validation message.");
            std::vector<std::uint8_t> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            hr(messages->GetMessage(i,message,&size),"Cannot read validation message.");
            if(message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)
                throw std::runtime_error(std::string("DX12 validation: ")+message->pDescription);
        }
        messages->ClearStoredMessages();
    }
};
// Only this test thread signals the CPU fence. A failed assertion/destructor
// always releases the queue; production readback contains no watchdog thread.
struct QueueGate {
    ComPtr<ID3D12Fence> fence;std::atomic_bool released{},watchdogReleased{};
    std::mutex mutex;std::condition_variable condition;std::jthread watchdog;
    explicit QueueGate(Host& host){
        hr(host.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Cannot create queue gate.");
        hr(host.queue->Wait(fence.Get(),1),"Cannot enqueue queue gate.");
        watchdog=std::jthread([this]{
            std::unique_lock lock(mutex);
            if(!condition.wait_for(lock,std::chrono::milliseconds{150},[&]{return released.load();})){
                watchdogReleased.store(true);release();
            }
        });
    }
    void release(){if(!released.exchange(true)){(void)fence->Signal(1);condition.notify_all();}}
    ~QueueGate(){release();}
};
void drain(core::DeviceSession& session){
    session.advance_frame();session.advance_frame();require(bgfx::drainD3D12(),"Cannot drain the prepared DX12 fixture.");
}
struct TextureFixture {
    bgfx::TextureHandle source{BGFX_INVALID_HANDLE},target{BGFX_INVALID_HANDLE},spare{BGFX_INVALID_HANDLE};
    std::vector<std::uint8_t> expected;
    explicit TextureFixture(core::DeviceSession& session,bool floating){
        constexpr std::uint16_t width=7,height=5;
        if(floating){
            std::vector<float> pixels(width*height*4);
            for(std::size_t i=0;i<pixels.size();++i)pixels[i]=(static_cast<float>(i%29)-14.f)*.125f;
            expected.resize(pixels.size()*sizeof(float));std::memcpy(expected.data(),pixels.data(),expected.size());
        }else{expected.resize(width*height*4);for(std::size_t i=0;i<expected.size();++i)expected[i]=static_cast<std::uint8_t>((i*37+11)&255);}
        const auto format=floating?bgfx::TextureFormat::RGBA32F:bgfx::TextureFormat::RGBA8;
        source=bgfx::createTexture2D(width,height,false,1,format,0,bgfx::copy(expected.data(),static_cast<std::uint32_t>(expected.size())));
        target=bgfx::createTexture2D(width,height,false,1,format,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        spare=bgfx::createTexture2D(width,height,false,1,format,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        require(bgfx::isValid(source)&&bgfx::isValid(target)&&bgfx::isValid(spare),"Cannot allocate native readback textures.");
        bgfx::blit(0,target,0,0,source,0,0,width,height);bgfx::blit(1,spare,0,0,source,0,0,width,height);drain(session);
    }
    void destroy(){for(auto* handle:{&source,&target,&spare})if(bgfx::isValid(*handle)){bgfx::destroy(*handle);*handle=BGFX_INVALID_HANDLE;}}
    ~TextureFixture(){destroy();}
};
void untouched(std::uint64_t ticket,Readback expected){
    std::array<std::uint8_t,1024> bytes;bytes.fill(0x5a);
    require(bgfx::pollD3D12TextureReadback(ticket,bytes.data(),static_cast<std::uint32_t>(bytes.size()))==expected,"Unexpected readback ticket status.");
    require(std::all_of(bytes.begin(),bytes.end(),[](auto value){return value==0x5a;}),"A non-ready ticket modified its CPU destination.");
}
void awaitReady(core::DeviceSession& session,std::uint64_t ticket){
    const auto deadline=Clock::now()+std::chrono::seconds{5};
    for(;;){
        const auto result=bgfx::pollD3D12TextureReadback(ticket,nullptr,0);
        if(result==Readback::Ready)return;
        require(result==Readback::Pending,"Native asynchronous readback failed before completion.");
        require(Clock::now()<deadline,"Native asynchronous readback did not complete.");
        session.advance_frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}
void consume(core::DeviceSession& session,std::uint64_t ticket,const std::vector<std::uint8_t>& expected){
    awaitReady(session,ticket);std::uint32_t required{};
    require(bgfx::pollD3D12TextureReadback(ticket,nullptr,0,&required)==Readback::Ready&&required==expected.size(),"Ready size query consumed or mis-sized a ticket.");
    std::vector<std::uint8_t> bytes(expected.size(),0x5a);
    require(bgfx::pollD3D12TextureReadback(ticket,bytes.data(),required-1)==Readback::Failed,"An undersized CPU buffer was accepted.");
    require(std::all_of(bytes.begin(),bytes.end(),[](auto value){return value==0x5a;}),"An undersized CPU buffer was modified.");
    require(bgfx::pollD3D12TextureReadback(ticket,nullptr,0)==Readback::Ready,"An undersized poll consumed a ready ticket.");
    require(bgfx::pollD3D12TextureReadback(ticket,bytes.data(),required)==Readback::Ready&&bytes==expected,"Native readback bytes or tightly packed rows differ.");
    untouched(ticket,Readback::Invalid);
}
std::uint64_t asynchronousControl(Host& host,core::DeviceSession& session,TextureFixture& texture){
    drain(session);std::array<std::uint64_t,32> tickets{};
    QueueGate gate(host);const auto begin=Clock::now();
    for(auto& ticket:tickets){ticket=bgfx::requestD3D12TextureReadback(texture.target);require(ticket!=0,"A bounded readback slot was unexpectedly refused.");}
    require(bgfx::requestD3D12TextureReadback(texture.spare)==0,"The readback pool exceeded32 live tickets.");
    bgfx::cancelD3D12TextureReadback(tickets[0]);untouched(tickets[0],Readback::Invalid);
    const auto cancelled=tickets[0];tickets[0]=bgfx::requestD3D12TextureReadback(texture.target);
    require(tickets[0]>tickets.back(),"Queued-only cancellation did not release its slot or ticket serial repeated.");
    // Pending polls may use ephemeral CPU buffers. None survives the gate.
    untouched(tickets[0],Readback::Pending);
    bgfx::destroy(texture.target);texture.target=BGFX_INVALID_HANDLE;
    session.advance_frame(); // Record the post-render copies and source destruction.
    std::uint32_t required{};
    require(bgfx::pollD3D12TextureReadback(tickets[0],nullptr,0,&required)==Readback::Pending&&required==texture.expected.size(),"First native frame did not record the pending copy.");
    bgfx::cancelD3D12TextureReadback(tickets[1]);untouched(tickets[1],Readback::Invalid);
    require(bgfx::requestD3D12TextureReadback(texture.spare)==0,"An in-flight cancellation recycled staging before its copy-list fence.");
    session.advance_frame(); // Submit that exact copy list through the normal frame path.
    require(!gate.released.load(),"Normal asynchronous frame submission waited for the blocked GPU queue.");
    untouched(tickets[0],Readback::Pending);
    const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();gate.release();
    for(std::size_t i=0;i<tickets.size();++i)if(i!=1)consume(session,tickets[i],texture.expected);
    untouched(cancelled,Readback::Invalid);untouched(tickets[1],Readback::Invalid);
    const auto reused=bgfx::requestD3D12TextureReadback(texture.spare);
    require(reused>tickets[0],"Completed/cancelled slots did not reuse storage with a fresh ticket.");consume(session,reused,texture.expected);
    std::cout<<"Asynchronous request/record/normal submit while gate remained closed: "<<elapsed<<" ms.\n";
    return reused;
}
void synchronousControl(Host& host,core::DeviceSession& session,TextureFixture& texture){
    drain(session);std::vector<std::uint8_t> bytes(texture.expected.size(),0x5a);QueueGate gate(host);
    const auto begin=Clock::now();(void)bgfx::readTexture(texture.spare,bytes.data());session.advance_frame();
    require(gate.watchdogReleased.load(),"The legacy synchronous readback control did not encounter the gated queue.");
    require(bytes==texture.expected,"Legacy readback control did not observe the same source bytes.");
    std::cout<<"Legacy readTexture backend frame waited for gate watchdog: "<<std::chrono::duration<double,std::milli>(Clock::now()-begin).count()<<" ms.\n";
}
}
int main(){try{
    Host host;std::uint64_t previousSessionTicket{};
    for(unsigned iteration=0;iteration<2;++iteration){
        core::DeviceSession session;status(session.initialize(BorrowedD3D12Context{host.device.Get(),host.queue.Get(),8,8}));
        require(bgfx::supportsD3D12AsyncReadback(),"Borrowed DX12 does not expose asynchronous readback.");
        if(previousSessionTicket)untouched(previousSessionTicket,Readback::Invalid);
        TextureFixture rgba(session,false),floating(session,true);
        require(bgfx::requestD3D12TextureReadback(bgfx::TextureHandle{bgfx::kInvalidHandle})==0,"An invalid texture handle was accepted.");
        require(bgfx::requestD3D12TextureReadback(rgba.source)==0,"A texture without READ_BACK was accepted.");
        require(bgfx::requestD3D12TextureReadback(rgba.target,1)==0,"An invalid mip was accepted.");
        const auto latest=asynchronousControl(host,session,rgba);require(latest>previousSessionTicket,"A new session aliased an earlier ticket.");
        synchronousControl(host,session,rgba);
        const auto floats=bgfx::requestD3D12TextureReadback(floating.target);require(floats>latest,"RGBA32F ticket allocation failed.");
        consume(session,floats,floating.expected);drain(session);
        {
            QueueGate gate(host);previousSessionTicket=bgfx::requestD3D12TextureReadback(floating.spare);
            require(previousSessionTicket>floats,"Cannot allocate shutdown cancellation fixture.");
            session.advance_frame();untouched(previousSessionTicket,Readback::Pending);
            bgfx::cancelD3D12TextureReadback(previousSessionTicket);untouched(previousSessionTicket,Readback::Invalid);
            rgba.destroy();floating.destroy();status(session.shutdown());
        }
        untouched(previousSessionTicket,Readback::Invalid);host.validate();
    }
    std::cout<<"Native DX12 byte-exact readback, deterministic queue isolation, bounded tickets, cancellation, source retirement and repeated sessions passed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"Asynchronous readback regression failed: "<<error.what()<<'\n';return 1;}}
