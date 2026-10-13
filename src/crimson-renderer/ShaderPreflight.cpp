#include "ShaderPreflight.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dxguid.lib")

namespace crimson {
namespace {
struct Binding { std::string name; std::uint8_t type{},count{}; bool operator==(const Binding&) const=default; };
struct Interface { std::uint32_t input{},output{}; std::vector<Binding> bindings; std::vector<std::uint16_t> attributes; };
struct Reader {
    std::span<const std::uint8_t> bytes; std::size_t cursor{};
    std::span<const std::uint8_t> take(std::size_t count) {
        if (count>bytes.size()-cursor) throw std::runtime_error("Shader binary is truncated.");
        const auto result=bytes.subspan(cursor,count); cursor+=count; return result;
    }
    template<class T> T value() { const auto block=take(sizeof(T)); T result; std::memcpy(&result,block.data(),sizeof result); return result; }
};
Interface inspect(std::span<const std::uint8_t> bytes,char stage) {
    Reader reader{bytes}; const auto magic=reader.take(4);
    if (magic[0]!=stage || magic[1]!='S' || magic[2]!='H' || magic[3]<10 || magic[3]>11) throw std::runtime_error("Shader binary stage/version does not match the pinned bgfx format.");
    Interface result; result.input=reader.value<std::uint32_t>(); result.output=reader.value<std::uint32_t>();
    const auto uniforms=reader.value<std::uint16_t>();
    for (std::uint32_t i=0;i<uniforms;++i) {
        const auto size=reader.value<std::uint8_t>(); if (!size) throw std::runtime_error("Shader uniform has no name.");
        const auto name=reader.take(size); Binding binding{{name.begin(),name.end()},reader.value<std::uint8_t>(),reader.value<std::uint8_t>()};
        binding.type&=0x0f;
        reader.take(2+2+2+2); // Register range and texture metadata for format v10+.
        result.bindings.push_back(std::move(binding));
    }
    const auto size=reader.value<std::uint32_t>(); const auto code=reader.take(size); reader.take(1);
    if (code.size()<32 || std::memcmp(code.data(),"DXBC",4)!=0) throw std::runtime_error("Shader has no Direct3D 11 bytecode.");
    ID3D11ShaderReflection* raw{};
    if (FAILED(D3DReflect(code.data(),code.size(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(&raw))) || !raw) throw std::runtime_error("Direct3D shader reflection rejected the bytecode.");
    const auto release=[](ID3D11ShaderReflection* ptr){ptr->Release();}; std::unique_ptr<ID3D11ShaderReflection,decltype(release)> reflection(raw,release);
    D3D11_SHADER_DESC description{};
    if (FAILED(reflection->GetDesc(&description)) || static_cast<std::uint32_t>(D3D11_SHVER_GET_TYPE(description.Version))!=
        static_cast<std::uint32_t>(stage=='V' ? D3D11_SHVER_VERTEX_SHADER:D3D11_SHVER_PIXEL_SHADER)) throw std::runtime_error("Shader bytecode stage is invalid.");
    const auto attributes=reader.value<std::uint8_t>();
    for (std::uint32_t i=0;i<attributes;++i) result.attributes.push_back(reader.value<std::uint16_t>());
    reader.take(2); // Constant buffer byte size.
    if (reader.cursor!=reader.bytes.size()) throw std::runtime_error("Shader binary contains unexpected trailing bytes.");
    std::sort(result.bindings.begin(),result.bindings.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    std::sort(result.attributes.begin(),result.attributes.end()); return result;
}
}
bool preflightShaderPair(std::span<const std::uint8_t> vertex,std::span<const std::uint8_t> fragment,
    std::span<const std::uint8_t> contractVertex,std::span<const std::uint8_t> contractFragment,std::string& diagnostic) {
    try {
        const auto v=inspect(vertex,'V'),f=inspect(fragment,'F'),originalV=inspect(contractVertex,'V'),originalF=inspect(contractFragment,'F');
        if (v.output!=f.input) throw std::runtime_error("Vertex/fragment varying interfaces differ.");
        if (v.bindings!=originalV.bindings || f.bindings!=originalF.bindings || v.attributes!=originalV.attributes || f.attributes!=originalF.attributes ||
            v.input!=originalV.input || v.output!=originalV.output || f.input!=originalF.input || f.output!=originalF.output)
            throw std::runtime_error("Hot reload changed shader uniforms, vertex attributes or varyings; restart with a rebuilt engine for interface changes.");
        diagnostic.clear(); return true;
    } catch (const std::exception& error) { diagnostic=error.what(); return false; }
}
}
