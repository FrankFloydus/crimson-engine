////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_d3d11_context_state.hpp"
#include <limits>

namespace quader {
bool CrimsonD3D11ContextState::initialize(ID3D11Device* device,ID3D11DeviceContext* context)noexcept{
  if(depth_||!device||!context)return false;
  reset();
  Microsoft::WRL::ComPtr<ID3D11Device1> native_device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext1> native_context;
  Microsoft::WRL::ComPtr<ID3D11Device> context_device;
  context->GetDevice(&context_device);
  Microsoft::WRL::ComPtr<IUnknown> device_identity,context_identity;
  if(FAILED(device->QueryInterface(IID_PPV_ARGS(&device_identity)))||FAILED(context_device.As(&context_identity))||device_identity!=context_identity)return false;
  if(FAILED(device->QueryInterface(IID_PPV_ARGS(&native_device)))||FAILED(context->QueryInterface(IID_PPV_ARGS(&native_context))))return false;
  const auto level=device->GetFeatureLevel();D3D_FEATURE_LEVEL selected{};
  Microsoft::WRL::ComPtr<ID3DDeviceContextState> state;
  const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
  if(FAILED(native_device->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&state)))return false;
  device_=std::move(native_device);context_=std::move(native_context);renderer_state_=std::move(state);return true;
}
bool CrimsonD3D11ContextState::begin()noexcept{
  if(!context_||!renderer_state_||reset_pending_||depth_==(std::numeric_limits<std::uint32_t>::max)())return false;
  if(depth_++==0)context_->SwapDeviceContextState(renderer_state_.Get(),prior_state_.ReleaseAndGetAddressOf());
  return true;
}
void CrimsonD3D11ContextState::end()noexcept{
  if(!depth_)return;
  if(--depth_==0){
    context_->SwapDeviceContextState(prior_state_.Get(),renderer_state_.ReleaseAndGetAddressOf());
    prior_state_.Reset();
    if(reset_pending_)reset();
  }
}
void CrimsonD3D11ContextState::request_reset()noexcept{if(depth_)reset_pending_=true;else reset();}
void CrimsonD3D11ContextState::reset()noexcept{prior_state_.Reset();renderer_state_.Reset();context_.Reset();device_.Reset();reset_pending_=false;}
}
