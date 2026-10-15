#include "ModelDeformationGpu.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>
#include <unordered_map>

namespace crimson {
namespace {
constexpr auto ReadFlags=BGFX_BUFFER_COMPUTE_READ|BGFX_BUFFER_COMPUTE_FORMAT_32X1|BGFX_BUFFER_COMPUTE_TYPE_FLOAT;
constexpr auto WriteFlags=BGFX_BUFFER_COMPUTE_WRITE|BGFX_BUFFER_COMPUTE_FORMAT_32X1|BGFX_BUFFER_COMPUTE_TYPE_FLOAT;
template<class T> void destroyHandle(T& handle){if(bgfx::isValid(handle))bgfx::destroy(handle);handle=BGFX_INVALID_HANDLE;}
bgfx::VertexLayout scalarLayout(){bgfx::VertexLayout value;value.begin().add(bgfx::Attrib::Position,1,bgfx::AttribType::Float).end();return value;}
bgfx::VertexLayout modelLayout(){bgfx::VertexLayout value;value.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
    .add(bgfx::Attrib::Normal,3,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float)
    .add(bgfx::Attrib::TexCoord1,2,bgfx::AttribType::Float).add(bgfx::Attrib::Tangent,4,bgfx::AttribType::Float)
    .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Float).end();return value;}
bgfx::VertexBufferHandle input(std::span<const float> values){
    if(values.empty() || values.size()>UINT32_MAX/sizeof(float))throw std::length_error("Model deformation input exceeds GPU buffer limits.");
    const auto result=bgfx::createVertexBuffer(bgfx::copy(values.data(),static_cast<std::uint32_t>(values.size_bytes())),scalarLayout(),ReadFlags);
    if(!bgfx::isValid(result))throw std::runtime_error("Model deformation input allocation failed.");return result;
}
}
ModelDeformationSource::~ModelDeformationSource(){destroyHandle(skin);destroyHandle(morph);}
std::shared_ptr<ModelDeformationSource> createModelDeformationSource(const ModelPrimitive& primitive){
    if(primitive.skinWeights.empty() && primitive.morphTargets.empty())return {};
    if(primitive.vertices.empty() || primitive.vertices.size()>16777216u || primitive.morphTargets.size()>256u)
        throw std::length_error("Model deformation counts exceed exact GPU integer transport.");
    auto result=std::make_shared<ModelDeformationSource>();result->vertexCount=static_cast<std::uint32_t>(primitive.vertices.size());
    result->morphCount=static_cast<std::uint32_t>(primitive.morphTargets.size());result->skinned=!primitive.skinWeights.empty();
    if(result->skinned && primitive.skinWeights.size()!=primitive.vertices.size())throw std::runtime_error("Model skin influence count differs from vertices.");
    for(const auto& target:primitive.morphTargets)for(const auto* values:{&target.positions,&target.normals,&target.tangents})
        if(!values->empty() && values->size()!=primitive.vertices.size())throw std::runtime_error("Model morph count differs from vertices.");
    const auto skin=std::uint64_t(result->vertexCount)*(result->skinned?16u:0u),morph=std::uint64_t(result->vertexCount)*result->morphCount*9;
    if(skin>UINT32_MAX/sizeof(float) || morph>UINT32_MAX/sizeof(float))throw std::length_error("Model deformation inputs exceed GPU buffer limits.");
    result->skinFloats=static_cast<std::size_t>(skin);result->morphFloats=static_cast<std::size_t>(morph);
    if(skin)result->skin=bgfx::createDynamicVertexBuffer(static_cast<std::uint32_t>(skin),scalarLayout(),ReadFlags);
    if(morph)result->morph=bgfx::createDynamicVertexBuffer(static_cast<std::uint32_t>(morph),scalarLayout(),ReadFlags);
    if((skin&&!bgfx::isValid(result->skin))||(morph&&!bgfx::isValid(result->morph)))throw std::runtime_error("Model deformation input allocation failed.");
    result->bytes=((skin?skin+1:0)+(morph?morph+1:0))*sizeof(float);return result;
}
std::size_t uploadModelDeformationSource(ModelDeformationSource& target,const ModelPrimitive& primitive,std::size_t byteBudget) {
    const auto upload=[&](bool skin){
        auto& offset=skin?target.skinUploaded:target.morphUploaded;const auto total=skin?target.skinFloats:target.morphFloats;
        const auto count=std::min({total-offset,byteBudget/sizeof(float),std::size_t{16384}});
        if(!count)return std::size_t{};
        std::vector<float> data;data.reserve(count);
        for(std::size_t i=offset;i<offset+count;++i) {
            if(skin){const auto& influence=primitive.skinWeights[i/16];const auto component=i%16;data.push_back(component<8?float(influence.joints[component]):influence.weights[component-8]);}
            else {
                const auto targetIndex=i/(std::size_t(target.vertexCount)*9),vertex=(i/9)%target.vertexCount,component=i%9;
                const auto& morph=primitive.morphTargets[targetIndex];const auto& values=component<3?morph.positions:component<6?morph.normals:morph.tangents;
                data.push_back(values.empty()?0.f:values[vertex][component%3]);
            }
        }
        bgfx::update(skin?target.skin:target.morph,static_cast<std::uint32_t>(offset),bgfx::copy(data.data(),static_cast<std::uint32_t>(count*sizeof(float))));
        offset+=count;return count*sizeof(float);
    };
    if(target.skinUploaded<target.skinFloats)return upload(true);
    return upload(false);
}
DeformedModelVertices::~DeformedModelVertices(){destroyHandle(vertices);destroyHandle(poseInput);}
void DeformedModelVertices::bind() const {bgfx::setVertexBuffer(0,vertices,0,vertexCount);}
struct ModelDeformationGpu::Impl {
    bgfx::ProgramHandle program{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle parameters{BGFX_INVALID_HANDLE};
    bgfx::VertexBufferHandle zero{BGFX_INVALID_HANDLE};
    struct Entry {std::shared_ptr<DeformedModelVertices> output;std::int32_t node{};std::uint64_t used{};};
    struct Key {
        const ModelDeformationSource* source{};const ModelPose* pose{};std::int32_t node{};
        bool operator==(const Key&) const=default;
    };
    struct KeyHash {std::size_t operator()(const Key& key) const noexcept {
        return (std::hash<const void*>{}(key.source)*1099511628211ull)^std::hash<const void*>{}(key.pose)^std::size_t(key.node);
    }};
    std::unordered_map<Key,Entry,KeyHash> entries;
    std::vector<std::pair<std::shared_ptr<DeformedModelVertices>,std::uint64_t>> available;
    std::vector<std::weak_ptr<DeformedModelVertices>> allocations;
    std::uint64_t frame{};
};
ModelDeformationGpu::ModelDeformationGpu(bgfx::ProgramHandle program):impl_(std::make_unique<Impl>()) {
    impl_->program=program;impl_->parameters=bgfx::createUniform("u_modelDeformation",bgfx::UniformType::Vec4);
    const float zero{};impl_->zero=input(std::span(&zero,1));
}
ModelDeformationGpu::~ModelDeformationGpu(){impl_->entries.clear();impl_->available.clear();destroyHandle(impl_->parameters);destroyHandle(impl_->zero);}
void ModelDeformationGpu::setProgram(bgfx::ProgramHandle program){impl_->program=program;impl_->entries.clear();}
void ModelDeformationGpu::advance(std::uint64_t frame){
    impl_->frame=frame;
    // Keep only the last submitted generations; renderer leases retain in-flight
    // outputs independently. Active immutable poses remain reusable across views.
    for(auto& [key,entry]:impl_->entries)if(frame>entry.used+2 && entry.output.use_count()==1) {
        (void)key;entry.output->source.reset();entry.output->pose.reset();entry.output->geometryLease.reset();
        impl_->available.emplace_back(std::move(entry.output),frame);
    }
    std::erase_if(impl_->entries,[&](const auto& value){return !value.second.output || frame>value.second.used+2;});
    std::erase_if(impl_->available,[&](const auto& entry){return frame>entry.second+120;});
    std::erase_if(impl_->allocations,[](const auto& value){return value.expired();});
}
std::shared_ptr<const DeformedModelVertices> ModelDeformationGpu::prepare(std::shared_ptr<const ModelDeformationSource> source,
    bgfx::DynamicVertexBufferHandle dynamic,bgfx::VertexBufferHandle fixed,std::shared_ptr<const void> geometryLease,
    std::shared_ptr<const ModelPose> pose,std::int32_t node,bgfx::ViewId view){
    if(!source || !pose || node<0)return {};
    if(!source->ready())throw std::logic_error("Incomplete deformation source was published.");
    const Impl::Key key{source.get(),pose.get(),node};
    if(const auto found=impl_->entries.find(key);found!=impl_->entries.end()){found->second.used=impl_->frame;return found->second.output;}
    if(!bgfx::isValid(impl_->program) || !(bgfx::getCaps()->supported&BGFX_CAPS_COMPUTE))throw std::runtime_error("Animated model requires compute deformation support.");
    if(static_cast<std::size_t>(node)>=pose->nodeWeights.size() || static_cast<std::size_t>(node)>=pose->skinPalettes.size())
        throw std::runtime_error("Model pose is missing a mesh node.");
    const auto& palette=pose->skinPalettes[node];const auto& weights=pose->nodeWeights[node];
    if(palette.size()>65536u || source->morphCount>UINT32_MAX-palette.size()*16)throw std::length_error("Model pose exceeds GPU buffer limits.");
    std::vector<float> data;data.reserve(palette.size()*16+source->morphCount);
    for(const auto& matrix:palette)data.insert(data.end(),matrix.begin(),matrix.end());
    for(std::size_t i=0;i<source->morphCount;++i)data.push_back(i<weights.size()?weights[i]:0.f);
    if(data.empty())data.push_back(0);
    if(data.size()>UINT32_MAX/sizeof(float))throw std::length_error("Model pose exceeds GPU buffer limits.");
    auto reusable=std::find_if(impl_->available.begin(),impl_->available.end(),[&](const auto& entry){
        return entry.first->vertexCapacity>=source->vertexCount && entry.first->poseCapacity>=data.size();
    });
    const bool allocate=reusable==impl_->available.end();
    auto output=allocate?std::make_shared<DeformedModelVertices>():std::move(reusable->first);
    if(!allocate)impl_->available.erase(reusable);
    output->source=std::move(source);output->pose=std::move(pose);output->geometryLease=std::move(geometryLease);
    output->vertexCount=output->source->vertexCount;
    if(allocate) {
        output->vertexCapacity=output->vertexCount;output->poseCapacity=static_cast<std::uint32_t>(data.size());
        output->vertices=bgfx::createDynamicVertexBuffer(output->vertexCapacity,modelLayout(),WriteFlags);
        output->poseInput=bgfx::createDynamicVertexBuffer(output->poseCapacity,scalarLayout(),ReadFlags);
        output->bytes=(std::size_t(output->vertexCapacity)+1)*sizeof(ModelVertex)+(std::size_t(output->poseCapacity)+1)*sizeof(float);
        impl_->allocations.push_back(output);
    }
    if(!bgfx::isValid(output->vertices)||!bgfx::isValid(output->poseInput))throw std::runtime_error("Animated model output allocation failed.");
    bgfx::update(output->poseInput,0,bgfx::copy(data.data(),static_cast<std::uint32_t>(data.size()*sizeof(float))));
    const Vec4 parameters{float(output->vertexCount),float(output->source->morphCount),float(palette.size()),output->source->skinned?1.f:0.f};
    bgfx::setUniform(impl_->parameters,parameters.data());
    if(bgfx::isValid(dynamic))bgfx::setBuffer(0,dynamic,bgfx::Access::Read);else bgfx::setBuffer(0,fixed,bgfx::Access::Read);
    if(bgfx::isValid(output->source->skin))bgfx::setBuffer(1,output->source->skin,bgfx::Access::Read);else bgfx::setBuffer(1,impl_->zero,bgfx::Access::Read);
    if(bgfx::isValid(output->source->morph))bgfx::setBuffer(2,output->source->morph,bgfx::Access::Read);else bgfx::setBuffer(2,impl_->zero,bgfx::Access::Read);
    bgfx::setBuffer(3,output->poseInput,bgfx::Access::Read);bgfx::setBuffer(4,output->vertices,bgfx::Access::Write);
    const auto groups=(output->vertexCount+63u)/64u;
    if(groups>65535u*65535u)throw std::length_error("Animated model dispatch exceeds GPU limits.");
    bgfx::dispatch(view,impl_->program,std::min(groups,65535u),(groups+65534u)/65535u,1);
    impl_->entries.emplace(key,Impl::Entry{output,node,impl_->frame});return output;
}
std::size_t ModelDeformationGpu::bytes() const {std::size_t result=sizeof(float);for(const auto& weak:impl_->allocations)if(const auto output=weak.lock())result+=output->bytes;return result;}
void ModelDeformationGpu::trim(std::size_t targetBytes) {
    while(!impl_->available.empty() && bytes()>targetBytes)impl_->available.erase(impl_->available.begin());
}

}
