#include "ShaderPreflight.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dxguid.lib")

namespace crimson {
namespace {
struct Binding {
    std::string name; std::uint8_t type{},count{};
    std::uint16_t samplerSlot{},samplerCount{},textureComponent{},textureDimension{},constantRegisters{};
    bool operator==(const Binding&) const=default;
};
struct ResourceBinding {
    std::string name;D3D_SHADER_INPUT_TYPE type{};UINT slot{},count{},flags{};
    D3D_RESOURCE_RETURN_TYPE returnType{};D3D_SRV_DIMENSION dimension{};UINT samples{};
    bool operator==(const ResourceBinding&) const=default;
};
struct Interface {
    std::uint32_t input{},output{};std::vector<Binding> bindings;
    std::vector<ResourceBinding> resources;std::vector<std::uint16_t> attributes;
    std::vector<std::string> executableResources;
    std::array<UINT,3> groupSize{};
};
struct Reader {
    std::span<const std::uint8_t> bytes; std::size_t cursor{};
    std::span<const std::uint8_t> take(std::size_t count) {
        if (count>bytes.size()-cursor) throw std::runtime_error("Shader binary is truncated.");
        const auto result=bytes.subspan(cursor,count); cursor+=count; return result;
    }
    template<class T> T value() { const auto block=take(sizeof(T)); T result; std::memcpy(&result,block.data(),sizeof result); return result; }
};
std::vector<std::string> executableResourceBindings(std::span<const std::uint8_t> code) {
    // Pinned shaderc strips RDEF. D3DReflect still reports the stage and thread
    // group, but BoundResources can then be zero even when the bytecode reads
    // textures or writes UAVs. Validate the executable SM5 declarations too.
    ID3DBlob* raw{};
    if (FAILED(D3DDisassemble(code.data(),code.size(),0,nullptr,&raw)) || !raw)
        throw std::runtime_error("Direct3D shader resource disassembly failed.");
    const auto release=[](ID3DBlob* ptr){ptr->Release();};
    std::unique_ptr<ID3DBlob,decltype(release)> assembly(raw,release);
    std::istringstream lines(std::string(static_cast<const char*>(raw->GetBufferPointer()),raw->GetBufferSize()));
    std::vector<std::string> result;
    std::string line;
    while (std::getline(lines,line)) {
        if (const auto comment=line.find("//");comment!=std::string::npos) line.resize(comment);
        std::erase_if(line,[](unsigned char c){return std::isspace(c)!=0;});
        // Preserve slots, dimensions, return types, access modes and structure
        // strides. Registers/temporaries in ordinary instructions may change.
        if (line.starts_with("dcl_resource") || line.starts_with("dcl_sampler") || line.starts_with("dcl_uav"))
            result.push_back(std::move(line));
    }
    std::sort(result.begin(),result.end());return result;
}
Interface inspect(std::span<const std::uint8_t> bytes,char stage) {
    Reader reader{bytes}; const auto magic=reader.take(4);
    if (magic[0]!=stage || magic[1]!='S' || magic[2]!='H' || magic[3]<10 || magic[3]>11) throw std::runtime_error("Shader binary stage/version does not match the pinned bgfx format.");
    Interface result; result.input=reader.value<std::uint32_t>(); result.output=reader.value<std::uint32_t>();
    const auto uniforms=reader.value<std::uint16_t>();
    for (std::uint32_t i=0;i<uniforms;++i) {
        const auto size=reader.value<std::uint8_t>(); if (!size) throw std::runtime_error("Shader uniform has no name.");
        const auto name=reader.take(size); Binding binding{{name.begin(),name.end()},reader.value<std::uint8_t>(),reader.value<std::uint8_t>()};
        binding.type&=0x0f;
        const auto index=reader.value<std::uint16_t>(),registerCount=reader.value<std::uint16_t>();
        binding.textureComponent=reader.value<std::uint16_t>();binding.textureDimension=reader.value<std::uint16_t>();
        // Constant offsets may legitimately move when a shader body changes;
        // texture slots are fixed by renderer submission and must remain exact.
        if (binding.type==0) {binding.samplerSlot=index;binding.samplerCount=registerCount;}
        else binding.constantRegisters=registerCount;
        result.bindings.push_back(std::move(binding));
    }
    const auto size=reader.value<std::uint32_t>(); const auto code=reader.take(size); reader.take(1);
    if (code.size()<32 || std::memcmp(code.data(),"DXBC",4)!=0) throw std::runtime_error("Shader has no Direct3D 11 bytecode.");
    ID3D11ShaderReflection* raw{};
    if (FAILED(D3DReflect(code.data(),code.size(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(&raw))) || !raw) throw std::runtime_error("Direct3D shader reflection rejected the bytecode.");
    const auto release=[](ID3D11ShaderReflection* ptr){ptr->Release();}; std::unique_ptr<ID3D11ShaderReflection,decltype(release)> reflection(raw,release);
    D3D11_SHADER_DESC description{};
    const auto expectedStage=stage=='C' ? D3D11_SHVER_COMPUTE_SHADER : stage=='V' ? D3D11_SHVER_VERTEX_SHADER : D3D11_SHVER_PIXEL_SHADER;
    if (FAILED(reflection->GetDesc(&description)) || static_cast<std::uint32_t>(D3D11_SHVER_GET_TYPE(description.Version))!=
        static_cast<std::uint32_t>(expectedStage)) throw std::runtime_error("Shader bytecode stage is invalid.");
    if (stage=='C') {
        const auto count=reflection->GetThreadGroupSize(&result.groupSize[0],&result.groupSize[1],&result.groupSize[2]);
        if (!count || count>1024 || !result.groupSize[0] || !result.groupSize[1] || !result.groupSize[2] ||
            result.groupSize[0]>1024 || result.groupSize[1]>1024 || result.groupSize[2]>64 ||
            count!=result.groupSize[0]*result.groupSize[1]*result.groupSize[2])
            throw std::runtime_error("Compute shader thread-group dimensions are invalid.");
    }
    for (UINT i=0;i<description.BoundResources;++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        if (FAILED(reflection->GetResourceBindingDesc(i,&binding))) throw std::runtime_error("Shader resource reflection failed.");
        if (binding.Type==D3D_SIT_CBUFFER) continue;
        result.resources.push_back({binding.Name,binding.Type,binding.BindPoint,binding.BindCount,binding.uFlags,
            binding.ReturnType,binding.Dimension,binding.NumSamples});
    }
    result.executableResources=executableResourceBindings(code);
    const auto attributes=reader.value<std::uint8_t>();
    for (std::uint32_t i=0;i<attributes;++i) result.attributes.push_back(reader.value<std::uint16_t>());
    reader.take(2); // Constant buffer byte size.
    if (stage=='C' && !result.attributes.empty()) throw std::runtime_error("Compute shader contains vertex attributes.");
    if (reader.cursor!=reader.bytes.size()) throw std::runtime_error("Shader binary contains unexpected trailing bytes.");
    std::sort(result.bindings.begin(),result.bindings.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    std::sort(result.resources.begin(),result.resources.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    std::sort(result.attributes.begin(),result.attributes.end()); return result;
}
}
bool preflightComputeShader(std::span<const std::uint8_t> candidate,
    std::span<const std::uint8_t> contract,std::string& diagnostic) {
    try {
        const auto c=inspect(candidate,'C'),original=inspect(contract,'C');
        if (c.input!=original.input || c.output!=original.output || c.bindings!=original.bindings ||
            c.resources!=original.resources || c.executableResources!=original.executableResources ||
            c.attributes!=original.attributes || c.groupSize!=original.groupSize)
            throw std::runtime_error("Hot reload changed compute uniforms, image bindings or thread-group dimensions; restart with a rebuilt engine for interface changes.");
        diagnostic.clear();return true;
    } catch (const std::exception& error) {diagnostic=error.what();return false;}
}
bool preflightShaderPair(std::span<const std::uint8_t> vertex,std::span<const std::uint8_t> fragment,
    std::span<const std::uint8_t> contractVertex,std::span<const std::uint8_t> contractFragment,std::string& diagnostic) {
    try {
        const auto v=inspect(vertex,'V'),f=inspect(fragment,'F'),originalV=inspect(contractVertex,'V'),originalF=inspect(contractFragment,'F');
        if (v.output!=f.input) throw std::runtime_error("Vertex/fragment varying interfaces differ.");
        if (v.bindings!=originalV.bindings || f.bindings!=originalF.bindings || v.resources!=originalV.resources || f.resources!=originalF.resources ||
            v.executableResources!=originalV.executableResources || f.executableResources!=originalF.executableResources ||
            v.attributes!=originalV.attributes || f.attributes!=originalF.attributes ||
            v.input!=originalV.input || v.output!=originalV.output || f.input!=originalF.input || f.output!=originalF.output)
            throw std::runtime_error("Hot reload changed shader uniforms, sampler bindings, vertex attributes or varyings; restart with a rebuilt engine for interface changes.");
        diagnostic.clear(); return true;
    } catch (const std::exception& error) { diagnostic=error.what(); return false; }
}
}
