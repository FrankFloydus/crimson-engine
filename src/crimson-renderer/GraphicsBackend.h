#pragma once

#include <optional>
#include <string_view>

namespace crimson {
enum class GraphicsBackend { Direct3D11, Direct3D12 };

constexpr bool validGraphicsBackend(GraphicsBackend backend) {
    return backend==GraphicsBackend::Direct3D11 || backend==GraphicsBackend::Direct3D12;
}
constexpr const char* graphicsBackendName(GraphicsBackend backend) {
    return backend==GraphicsBackend::Direct3D11 ? "Direct3D 11" : "Direct3D 12";
}
constexpr const char* graphicsBackendKey(GraphicsBackend backend) {
    return backend==GraphicsBackend::Direct3D11 ? "dx11" : "dx12";
}
constexpr std::optional<GraphicsBackend> parseGraphicsBackend(std::string_view value) {
    if(value=="dx11")return GraphicsBackend::Direct3D11;
    if(value=="dx12")return GraphicsBackend::Direct3D12;
    return std::nullopt;
}
} // namespace crimson
