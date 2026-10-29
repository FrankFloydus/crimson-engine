#include "render/crimson/crimson_native_queue_handoff.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>
#include <limits>
#include <algorithm>
#include <vector>

namespace quader {
using Microsoft::WRL::ComPtr;
struct CrimsonNativeQueueHandoff::Impl {
  struct Consumer {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    UINT64 serial{},last_primary{},last_use{};
  };
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> primary;
  ComPtr<ID3D12Fence> fence;
  UINT64 serial{};
  UINT64 use{};
  std::vector<Consumer> consumers;
  bool failed{};

  bool validate(ID3D12CommandQueue* queue) {
    if(!queue||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT){failed=true;return false;}
    ComPtr<ID3D12Device> actual;
    if(FAILED(queue->GetDevice(IID_PPV_ARGS(&actual)))||actual.Get()!=device.Get()){failed=true;return false;}
    return true;
  }
  Consumer* find(ID3D12CommandQueue* queue) {
    const auto found=std::ranges::find_if(consumers,[queue](const auto& value){return value.queue.Get()==queue;});
    return found==consumers.end()?nullptr:&*found;
  }
  Consumer* obtain(ID3D12CommandQueue* queue) {
    if(auto* found=find(queue))return found;
    // Only this GUI-thread owner records handoffs. Eviction proves both sides'
    // references completed; a later use of that queue starts with a fresh fence
    // after its then-current prefix, even if Qt still owns the native queue.
    constexpr std::size_t kMaximumConsumers=16;
    if(consumers.size()==kMaximumConsumers) {
      auto oldest=consumers.end();const auto primary_done=fence->GetCompletedValue();
      if(primary_done==std::numeric_limits<UINT64>::max()){failed=true;return nullptr;}
      for(auto at=consumers.begin();at!=consumers.end();++at) {
        const auto done=at->fence->GetCompletedValue();
        if(done==std::numeric_limits<UINT64>::max()){failed=true;return nullptr;}
        if(done>=at->serial&&primary_done>=at->last_primary&&(oldest==consumers.end()||at->last_use<oldest->last_use))oldest=at;
      }
      if(oldest==consumers.end())return nullptr; // Bounded busy state, no allocation or wait.
      consumers.erase(oldest);
    }
    Consumer value;value.queue=queue;
    if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&value.fence)))){failed=true;return nullptr;}
    value.fence->SetName(L"Crimson Qt consumer-prefix handoff");consumers.push_back(std::move(value));return &consumers.back();
  }
  bool signal_primary(Consumer& consumer) {
    if(serial==std::numeric_limits<UINT64>::max()){failed=true;return false;}
    consumer.last_primary=++serial;
    if(FAILED(primary->Signal(fence.Get(),serial))){failed=true;return false;}return true;
  }
};
CrimsonNativeQueueHandoff::CrimsonNativeQueueHandoff():impl_(std::make_unique<Impl>()){}
CrimsonNativeQueueHandoff::~CrimsonNativeQueueHandoff()=default;
bool CrimsonNativeQueueHandoff::initialize(ID3D12Device* device,ID3D12CommandQueue* primary) {
  if(impl_->fence)return impl_->device.Get()==device&&impl_->primary.Get()==primary&&healthy();
  if(!device||!primary||primary->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
  ComPtr<ID3D12Device> actual;
  if(FAILED(primary->GetDevice(IID_PPV_ARGS(&actual)))||actual.Get()!=device)return false;
  ComPtr<ID3D12Fence> fence;
  if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return false;
  fence->SetName(L"Crimson Qt detached composition handoff");
  impl_->device=device;impl_->primary=primary;impl_->fence=std::move(fence);return true;
}
bool CrimsonNativeQueueHandoff::healthy() const {
  return !impl_->failed&&impl_->device&&impl_->primary&&impl_->fence&&SUCCEEDED(impl_->device->GetDeviceRemovedReason());
}
bool CrimsonNativeQueueHandoff::acquire(void* destination_queue) {
  if(!healthy())return false;
  auto* queue=static_cast<ID3D12CommandQueue*>(destination_queue);
  if(!impl_->validate(queue))return false;
  if(queue==impl_->primary.Get())return true;
  auto* consumer=impl_->obtain(queue);if(!consumer)return false;
  if(consumer->serial==std::numeric_limits<UINT64>::max()){impl_->failed=true;return false;}
  const auto serial=++consumer->serial;consumer->last_use=++impl_->use;
  if(FAILED(queue->Signal(consumer->fence.Get(),serial))||FAILED(impl_->primary->Wait(consumer->fence.Get(),serial))) {
    impl_->failed=true;return false;
  }
  // A rejected/busy copy need not call handoff. This real primary marker proves
  // its preceding consumer-fence wait completed, so the entry remains evictable.
  return impl_->signal_primary(*consumer);
}
bool CrimsonNativeQueueHandoff::handoff(void* destination_queue) {
  if(!healthy())return false;
  auto* destination=static_cast<ID3D12CommandQueue*>(destination_queue);
  if(!impl_->validate(destination))return false;
  if(destination==impl_->primary.Get())return true;
  auto* consumer=impl_->find(destination);
  if(!consumer){impl_->failed=true;return false;}
  if(!impl_->signal_primary(*consumer))return false;
  if(consumer->serial==std::numeric_limits<UINT64>::max()){impl_->failed=true;return false;}
  // This consumer-only fence has one signal owner. A marker after its Wait
  // proves the shared primary fence is no longer referenced before eviction.
  const auto acknowledgement=++consumer->serial;
  if(FAILED(destination->Wait(impl_->fence.Get(),consumer->last_primary))||
     FAILED(destination->Signal(consumer->fence.Get(),acknowledgement))) {
    impl_->failed=true;return false;
  }
  consumer->last_use=++impl_->use;
  return true;
}
bool CrimsonNativeQueueHandoff::prepare_shutdown() {
  if(!healthy())return false;
  // No Qt frame is being recorded now. The caller's following renderer drain
  // confirms these actual consumer prefixes before any fence owner is released.
  for(auto& consumer:impl_->consumers)if(!acquire(consumer.queue.Get()))return false;
  return true;
}
void CrimsonNativeQueueHandoff::reset(){impl_=std::make_unique<Impl>();}
}
