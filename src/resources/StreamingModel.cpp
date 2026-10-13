#include "StreamingModel.h"
#include "CacheIO.h"
#include <meshoptimizer.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace crimson::streaming {
namespace {
constexpr std::array<char,8> Magic{'C','R','L','O','D','0','0','1'};
std::uint64_t primitiveKey(const ModelPrimitive& primitive) {
    auto key=detail::HashSeed; detail::hash(key,ModelCacheVersion); detail::hash(key,MESHOPTIMIZER_VERSION);
    detail::hash(key,primitive.material); detail::hash(key,primitive.uvMask); detail::hash(key,primitive.tangentUv);
    detail::hash(key,primitive.vertices.data(),primitive.vertices.size()*sizeof(ModelVertex));
    detail::hash(key,primitive.indices.data(),primitive.indices.size()*sizeof(std::uint32_t)); return key;
}
void validate(const ModelPrimitive& primitive) {
    if (primitive.vertices.empty() || primitive.indices.empty() || primitive.indices.size()%3 ||
        primitive.vertices.size()>UINT32_MAX || primitive.indices.size()>UINT32_MAX)
        throw std::runtime_error("Cannot stream malformed model geometry.");
    for (const auto& vertex : primitive.vertices)
        if (!math::finite(vertex.position) || !math::finite(vertex.normal) || !math::finite(vertex.uv0) || !math::finite(vertex.uv1) ||
            !math::finite(vertex.tangent) || !math::finite(vertex.color)) throw std::runtime_error("Model geometry contains nonfinite attributes.");
    for (const auto index : primitive.indices) if (index>=primitive.vertices.size()) throw std::runtime_error("Model index is out of bounds.");
}
ModelLod writeLevel(const std::filesystem::path& file,const ModelPrimitive& primitive,float error) {
    validate(primitive); const auto key=primitiveKey(primitive);
    const auto vertices=static_cast<std::uint32_t>(primitive.vertices.size()),indices=static_cast<std::uint32_t>(primitive.indices.size());
    std::string bytes;
    detail::append(bytes,Magic); detail::append(bytes,ModelCacheVersion); detail::append(bytes,key);
    detail::append(bytes,vertices); detail::append(bytes,indices); detail::append(bytes,primitive.material);
    detail::append(bytes,primitive.uvMask); detail::append(bytes,primitive.tangentUv); detail::append(bytes,error);
    detail::appendBytes(bytes,primitive.vertices.data(),primitive.vertices.size()*sizeof(ModelVertex));
    detail::appendBytes(bytes,primitive.indices.data(),primitive.indices.size()*sizeof(std::uint32_t));
    writeFileAtomically(file,bytes);
    return {file,key,vertices,indices,error};
}
Aabb primitiveBounds(const ModelPrimitive& primitive) {
    Aabb result;
    for (const auto& vertex : primitive.vertices) math::include(result,vertex.position);
    return result;
}
}
std::uint64_t modelContentKey(const ModelAsset& asset) {
    auto key=detail::HashSeed; detail::hash(key,ModelCacheVersion); detail::hash(key,MESHOPTIMIZER_VERSION);
    for (const auto& primitive : asset.primitives) { const auto value=primitiveKey(primitive); detail::hash(key,value); }
    return key;
}
ModelStream buildModelCache(const ModelAsset& asset,const std::filesystem::path& root,bool writeMetadata) {
    ModelStream result{asset.id,modelContentKey(asset),{}};
    const auto folder=root/"assets"/"cache"/"mesh-lods"/detail::hex(result.key);
    std::filesystem::create_directories(folder);
    for (std::size_t p=0;p<asset.primitives.size();++p) {
        const auto& original=asset.primitives[p]; validate(original);
        PrimitiveLods descriptor{original.material,primitiveBounds(original),{}};
        for (std::size_t set=0;set<2;++set) if (original.uvMask&(1u<<set)) {
            auto minimum=set ? original.vertices.front().uv1:original.vertices.front().uv0,maximum=minimum;
            for (const auto& vertex:original.vertices) {
                const auto& uv=set ? vertex.uv1:vertex.uv0;
                for (std::size_t axis=0;axis<2;++axis) { minimum[axis]=std::min(minimum[axis],uv[axis]);maximum[axis]=std::max(maximum[axis],uv[axis]); }
            }
            descriptor.uvSpan[set]={maximum[0]-minimum[0],maximum[1]-minimum[1]};
        }
        auto current=original; float accumulated=0;
        const float maximumError=.01f*meshopt_simplifyScale(original.vertices.front().position.data(),original.vertices.size(),sizeof(ModelVertex));
        for (std::uint32_t level=0;level<4;++level) {
            const auto file=folder/(std::to_string(p)+"-"+std::to_string(level)+".lod");
            descriptor.levels.push_back(writeLevel(file,current,accumulated));
            if (current.indices.size()<24 || level==3) break;
            const float currentScale=meshopt_simplifyScale(current.vertices.front().position.data(),current.vertices.size(),sizeof(ModelVertex));
            const float remainingError=maximumError-accumulated;
            if (!(remainingError>0) || !(currentScale>0)) break;
            std::vector<unsigned int> simplified(current.indices.size());
            std::array<float,15> weights{.5f,.5f,.5f,10,10,10,10,.5f,.5f,.5f,1,1,1,1,1};
            float error=0;
            const auto count=meshopt_simplifyWithAttributes(simplified.data(),current.indices.data(),current.indices.size(),
                current.vertices.front().position.data(),current.vertices.size(),sizeof(ModelVertex),
                current.vertices.front().normal.data(),sizeof(ModelVertex),weights.data(),weights.size(),nullptr,
                (current.indices.size()/6)*3,remainingError/currentScale,meshopt_SimplifyLockBorder,&error);
            if (!count || count>=current.indices.size()*9/10) break;
            simplified.resize(count);
            ModelPrimitive next; next.material=current.material; next.uvMask=current.uvMask; next.tangentUv=current.tangentUv;
            next.indices=std::move(simplified); next.vertices.resize(current.vertices.size());
            meshopt_optimizeVertexCache(next.indices.data(),next.indices.data(),next.indices.size(),current.vertices.size());
            const auto vertexCount=meshopt_optimizeVertexFetch(next.vertices.data(),next.indices.data(),next.indices.size(),
                current.vertices.data(),current.vertices.size(),sizeof(ModelVertex));
            next.vertices.resize(vertexCount);
            const float nextError=accumulated+error*currentScale;
            if (!std::isfinite(nextError) || nextError>maximumError) break;
            accumulated=nextError;
            current=std::move(next);
        }
        result.primitives.push_back(std::move(descriptor));
    }
    if (writeMetadata) writeModelMetadataCache(asset,result,root);
    return result;
}
std::shared_ptr<const ModelGeometryLevel> loadModelLod(const ModelLod& info,std::uint32_t primitive,std::uint32_t level) {
    std::ifstream input(info.file,std::ios::binary);
    if (detail::read<std::array<char,8>>(input)!=Magic || detail::read<std::uint32_t>(input)!=ModelCacheVersion ||
        detail::read<std::uint64_t>(input)!=info.key) throw std::runtime_error("Stale or damaged model LOD cache.");
    const auto vertices=detail::read<std::uint32_t>(input),indices=detail::read<std::uint32_t>(input);
    if (!vertices || !indices || indices%3 || vertices!=info.vertexCount || indices!=info.indexCount)
        throw std::runtime_error("Model LOD cache counts are invalid.");
    auto result=std::make_shared<ModelGeometryLevel>(); result->primitive=primitive; result->lodIndex=level;
    result->geometry.material=detail::read<std::uint32_t>(input);
    result->geometry.uvMask=detail::read<std::uint8_t>(input); result->geometry.tangentUv=detail::read<std::uint8_t>(input);
    result->error=detail::read<float>(input);
    const auto expected=static_cast<std::uint64_t>(vertices)*sizeof(ModelVertex)+static_cast<std::uint64_t>(indices)*4;
    const auto remaining=std::filesystem::file_size(info.file)-static_cast<std::uint64_t>(input.tellg());
    if (remaining!=expected || !std::isfinite(result->error) || result->error<0 || result->error!=info.error)
        throw std::runtime_error("Model LOD cache payload is invalid.");
    result->geometry.vertices.resize(vertices); result->geometry.indices.resize(indices);
    result->geometry.sourceVertexCount=vertices; result->geometry.sourceIndexCount=indices;
    detail::readBytes(input,result->geometry.vertices.data(),static_cast<std::size_t>(vertices)*sizeof(ModelVertex));
    detail::readBytes(input,result->geometry.indices.data(),static_cast<std::size_t>(indices)*4);
    validate(result->geometry);
    if (primitiveKey(result->geometry)!=info.key) throw std::runtime_error("Model LOD cache checksum mismatch.");
    result->wideIndices=*std::max_element(result->geometry.indices.begin(),result->geometry.indices.end())>UINT16_MAX;
    if (!result->wideIndices)
        result->indices16.assign(result->geometry.indices.begin(),result->geometry.indices.end());
    result->validated=true;
    return result;
}
std::uint32_t chooseModelLod(std::span<const ModelLod> levels,const Mat4& world,const Aabb& bounds,
    const Vec3& camera,float fov,std::uint32_t height,float pixels) {
    if (levels.empty() || !bounds.valid || !height || !std::isfinite(fov) || fov<=0 || pixels<=0) return 0;
    const auto transformed=math::transformBounds(bounds,world);
    if (!transformed.valid) return 0;
    const auto center=(transformed.minimum+transformed.maximum)*.5f;
    const auto radius=math::length(transformed.maximum-transformed.minimum)*.5f;
    const double distance=std::max(0.0,math::length(camera-center)-radius);
    if (distance<=0) return 0;
    double scaleSquared=0;
    for (std::size_t column=0;column<3;++column) for (std::size_t row=0;row<3;++row) scaleSquared+=static_cast<double>(world[column*4+row])*world[column*4+row];
    const double scale=std::sqrt(scaleSquared);
    const double maximumError=distance*(2*std::tan(fov*.5f)/height)*pixels;
    for (std::size_t level=levels.size();level-->0;) if (levels[level].error*scale<=maximumError) return static_cast<std::uint32_t>(level);
    return 0;
}
}
