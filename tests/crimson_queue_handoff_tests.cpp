#include "render/crimson/crimson_native_queue_handoff.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void success(HRESULT value,const char* message){check(SUCCEEDED(value),message);}
}
int main(){try{
    ComPtr<ID3D12Device> device;
    success(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Cannot create handoff fixture device.");
    D3D12_COMMAND_QUEUE_DESC descriptor{};descriptor.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> producer,consumer;
    success(device->CreateCommandQueue(&descriptor,IID_PPV_ARGS(&producer)),"Cannot create producer queue.");
    success(device->CreateCommandQueue(&descriptor,IID_PPV_ARGS(&consumer)),"Cannot create Qt consumer queue.");
    quader::CrimsonNativeQueueHandoff handoff;
    check(handoff.initialize(device.Get(),producer.Get())&&handoff.healthy(),"Handoff rejected its native owner.");
    check(handoff.handoff(producer.Get()),"The primary queue rejected its own handoff.");
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width=64;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;
    buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> upload,nextUpload,shared,readback,nextReadback;
    success(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"Cannot allocate fixture upload.");
    success(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&nextUpload)),"Cannot allocate next fixture upload.");
    heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    success(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&shared)),"Cannot allocate shared destination.");
    heap.Type=D3D12_HEAP_TYPE_READBACK;
    success(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Cannot allocate fixture readback.");
    success(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&nextReadback)),"Cannot allocate next fixture readback.");
    std::array<unsigned,16> expected{},nextExpected{};
    for(unsigned index=0;index<expected.size();++index){expected[index]=0x49a1u+index*7919u;nextExpected[index]=0xf39du+index*239u;}
    void* bytes{};const D3D12_RANGE empty{0,0};
    success(upload->Map(0,&empty,&bytes),"Cannot map fixture upload.");
    std::memcpy(bytes,expected.data(),sizeof(expected));upload->Unmap(0,nullptr);
    success(nextUpload->Map(0,&empty,&bytes),"Cannot map next fixture upload.");
    std::memcpy(bytes,nextExpected.data(),sizeof(nextExpected));nextUpload->Unmap(0,nullptr);
    std::array<ComPtr<ID3D12CommandAllocator>,4> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>,4> commands;
    for(unsigned index=0;index<commands.size();++index){
        success(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[index])),"Cannot allocate fixture commands.");
        success(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[index].Get(),nullptr,IID_PPV_ARGS(&commands[index])),"Cannot record fixture commands.");
    }
    const auto transition=[&](ID3D12GraphicsCommandList* list,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=shared.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=before;barrier.Transition.StateAfter=after;list->ResourceBarrier(1,&barrier);
    };
    const auto execute=[&](ID3D12CommandQueue* queue,unsigned index){
        success(commands[index]->Close(),"Cannot close fixture commands.");ID3D12CommandList* submitted[]{commands[index].Get()};queue->ExecuteCommandLists(1,submitted);
    };
    check(handoff.acquire(consumer.Get()),"Cannot acquire initial Qt destination prefix.");
    commands[0]->CopyBufferRegion(shared.Get(),0,upload.Get(),0,64);
    transition(commands[0].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);execute(producer.Get(),0);
    check(handoff.handoff(consumer.Get()),"The secondary Qt queue did not accept the GPU handoff.");
    commands[1]->CopyBufferRegion(readback.Get(),0,shared.Get(),0,64);execute(consumer.Get(),1);
    // Qt's queued read must finish before the next renderer write, even when
    // both directions are submitted without an intervening CPU/GPU wait.
    check(handoff.acquire(consumer.Get()),"Cannot acquire the real queued Qt read.");
    transition(commands[2].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    commands[2]->CopyBufferRegion(shared.Get(),0,nextUpload.Get(),0,64);
    transition(commands[2].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);execute(producer.Get(),2);
    check(handoff.handoff(consumer.Get()),"Cannot return the updated image to Qt.");
    commands[3]->CopyBufferRegion(nextReadback.Get(),0,shared.Get(),0,64);execute(consumer.Get(),3);
    check(handoff.prepare_shutdown(),"Cannot capture final real Qt queue prefixes.");
    ComPtr<ID3D12Fence> complete;
    success(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&complete)),"Cannot create completion marker.");
    success(consumer->Signal(complete.Get(),1),"Cannot signal consumer completion.");
    const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(event!=nullptr,"Cannot create completion event.");
    success(complete->SetEventOnCompletion(1,event),"Cannot observe consumer completion.");
    const auto waited=WaitForSingleObject(event,3000);CloseHandle(event);
    check(waited==WAIT_OBJECT_0&&complete->GetCompletedValue()>=1,"Consumer completion did not cover producer copy.");
    const D3D12_RANGE contents{0,64};success(readback->Map(0,&contents,&bytes),"Cannot map completed fixture readback.");
    const bool equal=std::memcmp(bytes,expected.data(),sizeof(expected))==0;readback->Unmap(0,&empty);
    check(equal,"Qt consumer completion exposed incomplete producer pixels/data.");
    success(nextReadback->Map(0,&contents,&bytes),"Cannot map next completed fixture readback.");
    const bool nextEqual=std::memcmp(bytes,nextExpected.data(),sizeof(nextExpected))==0;nextReadback->Unmap(0,&empty);
    check(nextEqual,"Qt did not observe the updated producer image.");
    success(producer->Signal(complete.Get(),2),"Cannot signal shutdown completion.");
    const auto retired=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(retired!=nullptr,"Cannot create retirement event.");
    success(complete->SetEventOnCompletion(2,retired),"Cannot observe shutdown completion.");
    const auto drained=WaitForSingleObject(retired,3000);CloseHandle(retired);check(drained==WAIT_OBJECT_0,"The final Qt prefix did not retire.");
    check(!handoff.handoff(nullptr)&&!handoff.healthy(),"Invalid queue failed to latch unsafe ownership.");
    handoff.reset();
    std::cout<<"Detached Qt queue handoff preserves both queued reads and subsequent writes, retires real prefixes, and rejects unknown queues.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
