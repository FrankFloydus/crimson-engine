#pragma once

#include "crimson-renderer/GraphicsBackend.h"
#include <bgfx/platform.h>
#include <d3d11.h>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace crimson {
inline GraphicsBackend selectedTestGraphicsBackend() {
    char* raw{};std::size_t length{};
    if(_dupenv_s(&raw,&length,"CRIMSON_TEST_RENDERER")!=0)
        throw std::runtime_error("Cannot read CRIMSON_TEST_RENDERER.");
    const std::unique_ptr<char,decltype(&std::free)> value(raw,&std::free);
    if(!value || !*value)return GraphicsBackend::Direct3D12;
    const auto backend=parseGraphicsBackend(value.get());
    if(!backend)throw std::runtime_error("CRIMSON_TEST_RENDERER must be dx11 or dx12.");
    return *backend;
}
inline bgfx::RendererType::Enum selectedTestBgfxRenderer() {
    return selectedTestGraphicsBackend()==GraphicsBackend::Direct3D11 ?
        bgfx::RendererType::Direct3D11 : bgfx::RendererType::Direct3D12;
}
// bgfx readback readiness remains the synchronization contract. The legacy
// DX11 tests additionally flush their immediate context; DX12 owns submission.
class TestGpuContext {
public:
    TestGpuContext() {
        if(bgfx::getRendererType()!=bgfx::RendererType::Direct3D11)return;
        const auto* internal=bgfx::getInternalData();
        device_=internal ? static_cast<ID3D11Device*>(internal->context) : nullptr;
        if(!device_)throw std::runtime_error("Direct3D 11 test device is unavailable.");
        device_->GetImmediateContext(&context_);
        if(!context_)throw std::runtime_error("Direct3D 11 test context is unavailable.");
    }
    ~TestGpuContext() {if(context_)context_->Release();}
    TestGpuContext(const TestGpuContext&)=delete;
    TestGpuContext& operator=(const TestGpuContext&)=delete;
    void flush() const {if(context_)context_->Flush();}
    ID3D11Device* d3d11Device() const {return device_;}
private:
    ID3D11Device* device_{}; // Borrowed under the surrounding renderer lifetime.
    ID3D11DeviceContext* context_{};
};
} // namespace crimson
