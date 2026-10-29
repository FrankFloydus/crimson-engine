#pragma once
#include "render/crimson/crimson_d3d11_context_state.hpp"
#include <array>
#include <cmath>
#include <stdexcept>

namespace quader::tests {
// Executes on the fixture's actual two independent Qt devices. The outer owner
// restores the entire pre-test primary state, so the test cannot poison QRhi.
inline void verify_d3d11_producer_context_state(ID3D11Device* producer,ID3D11DeviceContext* context,
    ID3D11Device* consumer,ID3D11DeviceContext* consumer_context){
  const auto require=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
  require(producer&&context&&consumer&&consumer_context&&producer!=consumer,"Context fixture requires two actual Qt D3D11 devices.");
  CrimsonD3D11ContextState preservation;require(preservation.initialize(producer,context),"Cannot preserve primary Qt native context.");
  auto preserved=preservation.scope();require(bool(preserved),"Cannot enter isolated native state fixture.");
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> qt_raster,renderer_raster;
  D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_WIREFRAME;raster.CullMode=D3D11_CULL_FRONT;raster.DepthClipEnable=TRUE;
  require(SUCCEEDED(producer->CreateRasterizerState(&raster,&qt_raster)),"Cannot create primary raster witness.");
  raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;
  require(SUCCEEDED(producer->CreateRasterizerState(&raster,&renderer_raster)),"Cannot create renderer raster witness.");
  const D3D11_VIEWPORT qt_viewport{3,5,19,23,.2f,.8f},renderer_viewport{7,11,29,31,0,1};
  context->RSSetState(qt_raster.Get());context->RSSetViewports(1,&qt_viewport);
  CrimsonD3D11ContextState owner;require(owner.initialize(producer,context),"Cannot initialize production context-state guard.");
  {
    auto outer=owner.scope();require(bool(outer)&&owner.depth()==1,"Context guard did not acquire primary owner.");
    context->RSSetState(renderer_raster.Get());context->RSSetViewports(1,&renderer_viewport);
    {
      auto inner=owner.scope();require(bool(inner)&&owner.depth()==2,"Nested context guard did not retain primary ownership.");
    }
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> current;context->RSGetState(&current);
    require(current==renderer_raster&&owner.depth()==1,"Nested scope restored Qt state before producer work ended.");
    // Calling a detached-consumer scope must never mutate the consumer context.
    consumer_context->RSGetState(&current);
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> consumer_before=current;
    {auto nested=owner.scope();require(bool(nested),"Cannot acquire producer during detached callback.");context->RSSetState(renderer_raster.Get());}
    consumer_context->RSGetState(&current);require(current==consumer_before,"Producer scope changed detached Qt context state.");
  }
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> current;context->RSGetState(&current);
  require(current==qt_raster&&owner.depth()==0,"Outermost producer scope did not restore Qt raster state.");
  D3D11_VIEWPORT returned{};UINT viewport_count=1;context->RSGetViewports(&viewport_count,&returned);
  require(viewport_count==1&&returned.TopLeftX==qt_viewport.TopLeftX&&returned.TopLeftY==qt_viewport.TopLeftY&&returned.Width==qt_viewport.Width&&returned.Height==qt_viewport.Height&&returned.MinDepth==qt_viewport.MinDepth&&returned.MaxDepth==qt_viewport.MaxDepth,"Outermost producer scope did not restore Qt viewport state.");
  {
    auto retained=owner.scope();require(bool(retained),"Cannot re-enter retained renderer native state.");
    context->RSGetState(&current);require(current==renderer_raster,"Renderer native state was discarded between callbacks.");
    owner.request_reset();require(!owner.begin(),"Retired context owner admitted new work before its scope exited.");
    context->RSGetState(&current);require(current==renderer_raster,"Nested teardown restored native state before work ended.");
  }
  context->RSGetState(&current);require(current==qt_raster&&!owner.begin(),"Deferred teardown lost Qt state or retained a dead producer owner.");
  require(!owner.initialize(producer,consumer_context),"Mismatched Qt device/context identity passed native ownership validation.");
}
}
