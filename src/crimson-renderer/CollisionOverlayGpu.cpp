#include "CollisionOverlayGpu.h"
#include "RenderTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <stdexcept>

namespace crimson {
namespace {
template<class T> void destroyHandle(T& handle){if(bgfx::isValid(handle))bgfx::destroy(handle);handle=BGFX_INVALID_HANDLE;}
struct OverlayVertex {Vec3 position,normalA,normalB;Vec4 midpoint;};
static_assert(sizeof(OverlayVertex)==13*sizeof(float));
float linear(float value){value=std::clamp(value,0.f,1.f);return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);}
constexpr std::size_t MaximumBytes=256u*1024u*1024u,MaximumEntries=4096;
struct Mesh {
    std::weak_ptr<const CollisionOverlayGeometry> source;
    bgfx::VertexBufferHandle vertices{BGFX_INVALID_HANDLE},wires{BGFX_INVALID_HANDLE};
    bgfx::IndexBufferHandle triangles{BGFX_INVALID_HANDLE};
    std::uint32_t triangleIndices{},wireCount{};
    std::size_t bytes{};std::uint64_t used{};
    ~Mesh(){destroyHandle(vertices);destroyHandle(wires);destroyHandle(triangles);}
};
}
struct CollisionOverlayGpu::Impl {
    std::unordered_map<const CollisionOverlayGeometry*,std::unique_ptr<Mesh>> meshes;
    bgfx::VertexLayout layout;
    bgfx::UniformHandle color{BGFX_INVALID_HANDLE},localEye{BGFX_INVALID_HANDLE},depthParams{BGFX_INVALID_HANDLE},normalView{BGFX_INVALID_HANDLE};
    std::size_t bytes{};std::uint64_t uploads{};std::string error;
    Impl() {
        layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0,3,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord1,3,bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2,4,bgfx::AttribType::Float).end();
        color=bgfx::createUniform("u_collisionColor",bgfx::UniformType::Vec4);
        localEye=bgfx::createUniform("u_collisionLocalEye",bgfx::UniformType::Vec4);
        depthParams=bgfx::createUniform("u_collisionDepth",bgfx::UniformType::Vec4);
        normalView=bgfx::createUniform("u_collisionNormalView",bgfx::UniformType::Mat4);
        if(!bgfx::isValid(color)||!bgfx::isValid(localEye)||!bgfx::isValid(depthParams)||!bgfx::isValid(normalView)) {
            destroyHandle(color);destroyHandle(localEye);destroyHandle(depthParams);destroyHandle(normalView);throw std::runtime_error("Collision overlay uniform allocation failed.");
        }
    }
    ~Impl(){meshes.clear();destroyHandle(color);destroyHandle(localEye);destroyHandle(depthParams);destroyHandle(normalView);}
    void erase(decltype(meshes)::iterator it){bytes-=it->second->bytes;meshes.erase(it);}
    Mesh& prepare(const std::shared_ptr<const CollisionOverlayGeometry>& source,std::uint64_t frame) {
        if(const auto found=meshes.find(source.get());found!=meshes.end()) {
            if(found->second->source.lock()==source){found->second->used=frame;return *found->second;}
            erase(found);
        }
        const auto& positions=source->wireVertices.empty()?source->vertices:source->wireVertices;
        if(source->triangles.size()%3 || source->vertices.size()>4'000'000 || source->triangles.size()>12'000'000 || source->edges.size()>4'000'000)
            throw std::runtime_error("Collision overlay geometry exceeds its bounded preparation limits.");
        const auto estimate=(source->triangles.empty()?0:source->vertices.size()*sizeof(OverlayVertex))+source->triangles.size()*sizeof(std::uint32_t)+source->edges.size()*2*sizeof(OverlayVertex);
        if(estimate>MaximumBytes)throw std::runtime_error("Collision overlay geometry exceeds its GPU cache budget.");
        while((bytes+estimate>MaximumBytes || meshes.size()>=MaximumEntries) && !meshes.empty()) {
            const auto oldest=std::min_element(meshes.begin(),meshes.end(),[](const auto& a,const auto& b){return a.second->used<b.second->used;});
            if(oldest->second->used==frame)throw std::runtime_error("Visible collision overlays exceed their GPU cache budget.");
            erase(oldest);
        }
        auto next=std::make_unique<Mesh>();next->source=source;next->used=frame;next->bytes=estimate;
        std::vector<OverlayVertex> fill;fill.reserve(source->vertices.size());
        for(const auto& point:source->vertices) {
            if(!math::finite(point))throw std::runtime_error("Collision overlay contains a nonfinite vertex.");
            fill.push_back({point,{},{},{}});
        }
        for(const auto index:source->triangles)if(index>=fill.size())throw std::runtime_error("Collision overlay triangle index is invalid.");
        std::vector<OverlayVertex> wires;wires.reserve(source->edges.size()*2);
        for(const auto& edge:source->edges) {
            if(edge.a>=positions.size() || edge.b>=positions.size() || !math::finite(edge.normalA) || !math::finite(edge.normalB))
                throw std::runtime_error("Collision overlay edge is invalid.");
            const auto a=positions[edge.a],b=positions[edge.b],middle=(a+b)*.5f;
            if(!math::finite(a)||!math::finite(b))throw std::runtime_error("Collision overlay edge contains nonfinite positions.");
            const Vec4 midpoint{middle[0],middle[1],middle[2],(edge.silhouetteOnly?1.f:0.f)+(edge.concave?2.f:0.f)+(edge.planeFallback?4.f:0.f)};
            wires.push_back({a,edge.normalA,edge.normalB,midpoint});wires.push_back({b,edge.normalA,edge.normalB,midpoint});
        }
        if(!fill.empty() && !source->triangles.empty()) {
            next->vertices=bgfx::createVertexBuffer(bgfx::copy(fill.data(),static_cast<std::uint32_t>(fill.size()*sizeof(OverlayVertex))),layout);
            next->triangles=bgfx::createIndexBuffer(bgfx::copy(source->triangles.data(),static_cast<std::uint32_t>(source->triangles.size()*sizeof(std::uint32_t))),BGFX_BUFFER_INDEX32);
            if(!bgfx::isValid(next->vertices)||!bgfx::isValid(next->triangles))throw std::runtime_error("Collision overlay fill allocation failed.");
            next->triangleIndices=static_cast<std::uint32_t>(source->triangles.size());
        }
        if(!wires.empty()) {
            next->wires=bgfx::createVertexBuffer(bgfx::copy(wires.data(),static_cast<std::uint32_t>(wires.size()*sizeof(OverlayVertex))),layout);
            if(!bgfx::isValid(next->wires))throw std::runtime_error("Collision overlay wire allocation failed.");
            next->wireCount=static_cast<std::uint32_t>(wires.size());
        }
        bytes+=estimate;++uploads;const auto [stored,inserted]=meshes.emplace(source.get(),std::move(next));(void)inserted;return *stored->second;
    }
};
CollisionOverlayGpu::CollisionOverlayGpu():impl_(std::make_unique<Impl>()){}
CollisionOverlayGpu::~CollisionOverlayGpu()=default;
void CollisionOverlayGpu::render(std::span<const RenderCollisionOverlay> overlays,const Camera& camera,bgfx::ViewId view,bgfx::ProgramHandle program,std::uint64_t frame) {
    auto& state=*impl_;state.error.clear();
    constexpr auto base=BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_LEQUAL|
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
    const auto viewMatrix=camera.viewMatrix();
    for(bool wire:{false,true})for(const auto& overlay:overlays) {
        if(!overlay.geometry || !math::isAffine(overlay.world) || !math::finite(overlay.color) || overlay.color[3]<=0)continue;
        const auto inverse=math::inverse(overlay.world);if(!inverse)continue;
        const auto normalView=math::normalMatrix(viewMatrix*overlay.world);if(!normalView)continue;
        try {
            auto& mesh=state.prepare(overlay.geometry,frame);
            if(wire?!mesh.wireCount:!mesh.triangleIndices)continue;
            const float alpha=std::clamp(overlay.color[3]*(wire?1.f:std::clamp(overlay.fillAlphaScale,0.f,1.f)),0.f,1.f);
            if(!(alpha>0))continue;
            const Vec4 color{linear(overlay.color[0]),linear(overlay.color[1]),linear(overlay.color[2]),alpha};
            const auto localEye=math::transformPoint(*inverse,camera.eye());const Vec4 local{localEye[0],localEye[1],localEye[2],0};
            // The shared scene target is D24S8. Bias in representable depth
            // units, not world meters, so far coplanar surfaces remain stable
            // even with a very small near plane. Fills include one extra unit
            // for reprojection rounding plus subpixel rasterization error in
            // the fragment stage. Native fallback line depth needs two units;
            // surface-plane wire depth uses the same reprojection budget as fill.
            const auto* caps=bgfx::getCaps();
            const Vec4 depth{wire?2.f:3.f,caps->homogeneousDepth?1.f:0.f,wire?0.f:1.f,caps->originBottomLeft?1.f:0.f};
            bgfx::setUniform(state.color,color.data());bgfx::setUniform(state.localEye,local.data());bgfx::setUniform(state.depthParams,depth.data());
            bgfx::setUniform(state.normalView,normalView->data());
            bgfx::setTransform(overlay.world.data());
            bgfx::setVertexBuffer(0,wire?mesh.wires:mesh.vertices);
            if(!wire)bgfx::setIndexBuffer(mesh.triangles,0,mesh.triangleIndices);
            const auto topology=wire?BGFX_STATE_PT_LINES:math::handedness(overlay.world)<0?BGFX_STATE_CULL_CCW:BGFX_STATE_CULL_CW;
            bgfx::setState(base|topology);bgfx::submit(view,program);
        }catch(const std::exception& error){state.error=error.what();}
    }
}
void CollisionOverlayGpu::trim(std::uint64_t frame) {
    for(auto it=impl_->meshes.begin();it!=impl_->meshes.end();) {
        if(it->second->source.expired() && it->second->used+2<frame){auto old=it++;impl_->erase(old);}else ++it;
    }
}
std::size_t CollisionOverlayGpu::residentBytes()const{return impl_->bytes;}
std::uint64_t CollisionOverlayGpu::uploads()const{return impl_->uploads;}
const std::string& CollisionOverlayGpu::status()const{return impl_->error;}
} // namespace crimson
