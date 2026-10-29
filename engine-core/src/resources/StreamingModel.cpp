#include "StreamingModel.h"
#include "assets/ContentStorage.h"
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

void validate(const ModelPrimitive& primitive) {
    if (primitive.vertices.empty() || primitive.indices.empty() || primitive.indices.size()%3 ||
        primitive.vertices.size()>UINT32_MAX || primitive.indices.size()>UINT32_MAX)
        throw std::runtime_error("Cannot stream malformed model geometry.");
    for (const auto& vertex : primitive.vertices)
        if (!math::finite(vertex.position) || !math::finite(vertex.normal) || !math::finite(vertex.uv0) || !math::finite(vertex.uv1) ||
            !math::finite(vertex.tangent) || !math::finite(vertex.color) || !math::finite(vertex.uvInterpolationCoordinates) ||
            !math::finite(vertex.uvInterpolationCorrection) || !std::isfinite(vertex.uvInterpolationMode)) throw std::runtime_error("Model geometry contains nonfinite attributes.");
    for (const auto index : primitive.indices) if (index>=primitive.vertices.size()) throw std::runtime_error("Model index is out of bounds.");
    for(const auto& vertex:primitive.vertices)if((vertex.uvInterpolationMode!=0&&vertex.uvInterpolationMode!=1&&vertex.uvInterpolationMode!=2)||
        (vertex.uvInterpolationMode==1&&vertex.uvInterpolationCoordinates[2]<=0)||(vertex.uvInterpolationMode==0&&
        (vertex.uvInterpolationCoordinates!=Vec4{}||vertex.uvInterpolationCorrection!=Vec4{})))throw std::runtime_error("Invalid model UV interpolation corner attributes.");
    for(std::size_t triangle=0;triangle<primitive.indices.size();triangle+=3){
        const auto& a=primitive.vertices[primitive.indices[triangle]];const auto& b=primitive.vertices[primitive.indices[triangle+1]];const auto& c=primitive.vertices[primitive.indices[triangle+2]];
        if(a.uvInterpolationMode!=b.uvInterpolationMode||a.uvInterpolationMode!=c.uvInterpolationMode||
            a.uvInterpolationCorrection!=b.uvInterpolationCorrection||a.uvInterpolationCorrection!=c.uvInterpolationCorrection)
            throw std::runtime_error("Model UV interpolation triangle spans different interpolation fields.");
    }
    if(!primitive.skinWeights.empty() && primitive.skinWeights.size()!=primitive.vertices.size())throw std::runtime_error("Model skin weights do not match vertices.");
    for(const auto& influence:primitive.skinWeights) {
        double total{};for(const auto weight:influence.weights){if(!std::isfinite(weight)||weight<0)throw std::runtime_error("Model skin weight is invalid.");total+=weight;}
        if(total<=1e-12)throw std::runtime_error("Model skin vertex has no positive influence.");
    }
    if(primitive.morphTargets.size()>256)throw std::runtime_error("Model morph target count exceeds the supported limit.");
    for(const auto& target:primitive.morphTargets)for(const auto* values:{&target.positions,&target.normals,&target.tangents}) {
        if(!values->empty() && values->size()!=primitive.vertices.size())throw std::runtime_error("Model morph target does not match vertices.");
        for(const auto& value:*values)if(!math::finite(value))throw std::runtime_error("Model morph target is nonfinite.");
    }
}
ModelLod writeLevel(const std::filesystem::path& file,const ModelPrimitive& primitive,float error) {
    validate(primitive); const auto key=model_primitive_content_key(primitive);
    const auto vertices=static_cast<std::uint32_t>(primitive.vertices.size()),indices=static_cast<std::uint32_t>(primitive.indices.size());
    std::string bytes;
    detail::append(bytes,Magic); detail::append(bytes,ModelCacheVersion); detail::append(bytes,key);
    detail::append(bytes,vertices); detail::append(bytes,indices); detail::append(bytes,primitive.material);
    detail::append(bytes,primitive.uvMask); detail::append(bytes,primitive.tangentUv); detail::append(bytes,error);
    detail::appendBytes(bytes,primitive.vertices.data(),primitive.vertices.size()*sizeof(ModelVertex));
    detail::appendBytes(bytes,primitive.indices.data(),primitive.indices.size()*sizeof(std::uint32_t));
    detail::append(bytes,static_cast<std::uint32_t>(primitive.skinWeights.size()));
    detail::appendBytes(bytes,primitive.skinWeights.data(),primitive.skinWeights.size()*sizeof(ModelSkinWeights));
    detail::append(bytes,static_cast<std::uint32_t>(primitive.morphTargets.size()));
    for(const auto& target:primitive.morphTargets) {
        const std::uint8_t flags=(target.positions.empty()?0:1)|(target.normals.empty()?0:2)|(target.tangents.empty()?0:4);detail::append(bytes,flags);
        for(const auto* values:{&target.positions,&target.normals,&target.tangents})detail::appendBytes(bytes,values->data(),values->size()*sizeof(Vec3));
    }
    detail::append(bytes,static_cast<std::uint32_t>(primitive.sourceFaces.size()));
    for(const auto& face:primitive.sourceFaces) {
        detail::append(bytes,face.cornerCount);detail::append(bytes,face.vertices);detail::append(bytes,face.triangles);
    }
    detail::writeCacheFileAtomically(file,bytes);
    return {file,key,vertices,indices,error};
}
Aabb primitiveBounds(const ModelPrimitive& primitive) {
    Aabb result;
    for (const auto& vertex : primitive.vertices) math::include(result,vertex.position);
    return result;
}
}

ModelStream buildModelCache(const ModelAsset& asset,const std::filesystem::path& root,bool writeMetadata) {
    ModelStream result{asset.id,modelContentKey(asset),{}};
    const auto folder=resolveAssetPath(root,".crimson/cache/mesh-lods")/detail::hex(result.key);
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
        const bool interpolated=std::any_of(original.vertices.begin(),original.vertices.end(),[](const auto& vertex){return vertex.uvInterpolationMode>0;});
        const float maximumError=.01f*meshopt_simplifyScale(original.vertices.front().position.data(),original.vertices.size(),sizeof(ModelVertex));
        for (std::uint32_t level=0;level<4;++level) {
            const auto file=folder/(std::to_string(p)+"-"+std::to_string(level)+".lod");
            descriptor.levels.push_back(writeLevel(file,current,accumulated));
            // Triangle simplification cannot preserve nonlinear interpolation
            // residuals or chart boundaries.
            if (interpolated || !original.skinWeights.empty() || !original.morphTargets.empty() || current.indices.size()<24 || level==3) break;
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
std::shared_ptr<const ModelGeometryLevel> loadModelLod(const ModelLod& info,std::uint32_t primitive,std::uint32_t level,bool prepareShadowRays,std::stop_token stop) {
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
    if (remaining<expected+8 || !std::isfinite(result->error) || result->error<0 || result->error!=info.error)
        throw std::runtime_error("Model LOD cache payload is invalid.");
    result->geometry.vertices.resize(vertices); result->geometry.indices.resize(indices);
    result->geometry.sourceVertexCount=vertices; result->geometry.sourceIndexCount=indices;
    detail::readBytes(input,result->geometry.vertices.data(),static_cast<std::size_t>(vertices)*sizeof(ModelVertex));
    detail::readBytes(input,result->geometry.indices.data(),static_cast<std::size_t>(indices)*4);
    const auto skinCount=detail::read<std::uint32_t>(input);
    if(skinCount && skinCount!=vertices)throw std::runtime_error("Model LOD skin count is invalid.");
    const auto bytesLeft=[&]{const auto offset=input.tellg();if(offset<0)throw std::runtime_error("Truncated model deformation payload.");return std::filesystem::file_size(info.file)-static_cast<std::uint64_t>(offset);};
    if(std::uint64_t(skinCount)*sizeof(ModelSkinWeights)>bytesLeft())throw std::runtime_error("Truncated model skin payload.");
    result->geometry.hasSkinning=skinCount!=0;
    result->geometry.skinWeights.resize(skinCount);detail::readBytes(input,result->geometry.skinWeights.data(),std::size_t(skinCount)*sizeof(ModelSkinWeights));
    const auto morphCount=detail::read<std::uint32_t>(input);
    if(morphCount>256 || morphCount>bytesLeft())throw std::runtime_error("Model LOD morph count is invalid.");
    result->geometry.morphTargets.resize(morphCount);
    for(auto& target:result->geometry.morphTargets) {
        const auto flags=detail::read<std::uint8_t>(input);if(flags&~7u)throw std::runtime_error("Invalid model morph flags.");
        unsigned attribute=0;for(auto* values:{&target.positions,&target.normals,&target.tangents}) {
            if(flags&(1u<<attribute)) {
                if(std::uint64_t(vertices)*sizeof(Vec3)>bytesLeft())throw std::runtime_error("Truncated model morph payload.");
                values->resize(vertices);detail::readBytes(input,values->data(),std::size_t(vertices)*sizeof(Vec3));
            }
            ++attribute;
        }
    }
    const auto faceCount=detail::read<std::uint32_t>(input);
    if(faceCount>indices/3||std::uint64_t(faceCount)*25>bytesLeft())throw std::runtime_error("Model source face count is invalid.");
    result->geometry.sourceFaces.resize(faceCount);
    for(auto& face:result->geometry.sourceFaces) {
        face.cornerCount=detail::read<std::uint8_t>(input);face.vertices=detail::read<std::array<std::uint32_t,4>>(input);
        face.triangles=detail::read<std::array<std::uint32_t,2>>(input);
        if(face.cornerCount!=3&&face.cornerCount!=4)throw std::runtime_error("Invalid model source face.");
        for(unsigned i=0;i<face.cornerCount;++i)if(face.vertices[i]>=vertices)throw std::runtime_error("Model source face vertex is invalid.");
        if(face.triangles[0]>=indices/3||(face.cornerCount==4&&face.triangles[1]>=indices/3))throw std::runtime_error("Model source face triangle is invalid.");
    }
    if(bytesLeft())throw std::runtime_error("Model LOD cache has trailing geometry data.");
    input.close();
    validate(result->geometry);
    if (model_primitive_content_key(result->geometry)!=info.key) throw std::runtime_error("Model LOD cache checksum mismatch.");
    result->wideIndices=*std::max_element(result->geometry.indices.begin(),result->geometry.indices.end())>UINT16_MAX;
    if (!result->wideIndices)
        result->indices16.assign(result->geometry.indices.begin(),result->geometry.indices.end());
    result->validated=true;
    if(prepareShadowRays&&level==0)result->shadowRays=build_shadow_ray_mesh(result->geometry,stop);
    return result;
}

}
