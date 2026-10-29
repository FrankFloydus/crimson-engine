////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstdint>
#include <utility>

namespace quader {
// Qt's detached window can use another D3D11 device. Its ExternalContent scope
// invalidates that consumer's QRhi cache, not the producer's immediate context.
// Keep the producer's complete native state coherent with its own QRhi tracker.
class CrimsonD3D11ContextState final {
public:
  class Scope final {
  public:
    Scope()=default;
    explicit Scope(CrimsonD3D11ContextState& owner):owner_(owner.begin()?&owner:nullptr){}
    ~Scope(){reset();}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
    Scope(Scope&& other)noexcept:owner_(std::exchange(other.owner_,nullptr)){}
    Scope& operator=(Scope&& other)noexcept{if(this!=&other){reset();owner_=std::exchange(other.owner_,nullptr);}return *this;}
    explicit operator bool()const noexcept{return owner_!=nullptr;}
    void reset()noexcept{if(auto* owner=std::exchange(owner_,nullptr))owner->end();}
  private:
    CrimsonD3D11ContextState* owner_=nullptr;
  };
  CrimsonD3D11ContextState()=default;
  ~CrimsonD3D11ContextState(){request_reset();}
  CrimsonD3D11ContextState(const CrimsonD3D11ContextState&)=delete;
  CrimsonD3D11ContextState& operator=(const CrimsonD3D11ContextState&)=delete;
  bool initialize(ID3D11Device* device,ID3D11DeviceContext* context)noexcept;
  bool begin()noexcept;
  void end()noexcept;
  [[nodiscard]] Scope scope(){return Scope(*this);}
  // Shutdown can be nested inside the outer Qt callback. Keep both native
  // owners until that callback restores the producer's prior Qt state.
  void request_reset()noexcept;
  [[nodiscard]] std::uint32_t depth()const noexcept{return depth_;}
private:
  void reset()noexcept;
  Microsoft::WRL::ComPtr<ID3D11Device1> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
  Microsoft::WRL::ComPtr<ID3DDeviceContextState> renderer_state_,prior_state_;
  std::uint32_t depth_=0;
  bool reset_pending_=false;
};
}
