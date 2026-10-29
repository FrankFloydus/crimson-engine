#include "render/crimson/crimson_rich_scene.hpp"
#include "render/crimson/crimson_rich_geometry.hpp"
#include "render/crimson/crimson_standard_material.hpp"
#include "render/crimson/crimson_environment.hpp"
#include "assets/material_document.hpp"
#include "resources/ResourceManager.h"
#include "resources/ImageDecoder.h"
#include "scene/light_properties.hpp"
#include "core/color_space.hpp"
#include "math/glm_bridge.hpp"
#include <QImage>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>

namespace quader {
namespace {
std::string geometry_key(const crimson::shared::MeshUpdate& mesh,const crimson::shared::Matrix4& sampling){std::ostringstream value;value<<"quader-rendered:"<<std::hex<<mesh.id<<':'<<mesh.positions.revision<<':'<<mesh.attributes.revision<<':'<<mesh.indices.revision<<':'<<mesh.ranges.revision;for(const auto component:sampling.values)value<<':'<<std::bit_cast<std::uint32_t>(component);return value.str();}
std::uint64_t new_world_identity(){static std::atomic_uint64_t sequence{1};return 0x5155414400000000ull|sequence.fetch_add(1);}
std::uint64_t hash(std::string_view value){std::uint64_t result=14695981039346656037ull;for(const unsigned char c:value){result^=c;result*=1099511628211ull;}return result?result:1;}
crimson::Vec3 vector(QVec3 value){return {value.x,value.y,value.z};}
crimson::Vec3 display_color(QVec3 value){return {linear_to_srgb(value.x),linear_to_srgb(value.y),linear_to_srgb(value.z)};}
struct RichOccurrence {
    std::shared_ptr<const CrimsonRichGeometry> geometry;
    std::shared_ptr<const void> geometry_lease;
    std::vector<std::shared_ptr<const CrimsonStandardMaterial>> material_pins;
    std::vector<crimson::ModelMaterial> materials;
    std::vector<crimson::ModelMaterialTextures> textures;
    crimson::Mat4 world;
    std::uint64_t identity{},resource_revision{};
    bool double_sided{},cast_shadows{},context_dimmed{};
};
crimson::SceneSettings presentation_settings(const SceneSettings& input,const ViewportCameraState& camera,const ViewportCameraPose& pose,crimson::AntiAliasing aa){
    crimson::SceneSettings out;out.camera.position=vector(pose.eye);out.camera.target=vector(pose.target);out.camera.up=vector(pose.up);out.camera.nearPlane=camera.near_plane;out.camera.farPlane=camera.far_plane;out.camera.fov=camera.fov_degrees;out.camera.fovAxis=crimson::CameraFovAxis::Vertical;
    // Quader's actual preview owner fixes its camera exposure at 16,1/125,100.
    // LightEngineValues already includes that calibration; HDR transport here
    // uses the corresponding exposure-normalized radiance with exposure one.
    out.exposure=1;auto& environment=out.environment;environment.source=crimson::EnvironmentSource::Hdri;environment.enabled=input.environment.enabled;environment.intensity=input.environment.enabled?input.environment.intensity*(30000.f/38400.f):0;environment.rotation=input.environment.rotation_y*180.f/std::numbers::pi_v<float>;environment.background.show=input.environment.enabled&&input.environment.show_background&&input.environment.background_intensity>0;environment.background.overrideColor=true;environment.background.color=display_color(input.environment.clear_color);

    environment.debanding=input.post_process.enabled&&input.post_process.dithering;
    const bool post=input.post_process.enabled;out.antiAliasing=post&&input.post_process.anti_aliasing?aa:crimson::AntiAliasing::Off;
    switch(post?input.post_process.tone_mapping:SceneToneMapping::Linear){case SceneToneMapping::Linear:environment.toneMapping=crimson::ToneMapping::Linear;break;case SceneToneMapping::Aces:case SceneToneMapping::AcesLegacy:case SceneToneMapping::DisplayRange:case SceneToneMapping::Filmic:environment.toneMapping=crimson::ToneMapping::ACES;break;case SceneToneMapping::Agx:environment.toneMapping=crimson::ToneMapping::AgX;break;}
    environment.ssao.enabled=post&&input.post_process.ambient_occlusion;environment.ssao.strength=input.post_process.ambient_occlusion_intensity;environment.ssao.radius=input.post_process.ambient_occlusion_radius;environment.glow.enabled=post&&input.post_process.bloom;environment.glow.strength=input.post_process.bloom_strength;

    environment.fog.enabled=false;return out;
}
}
struct CrimsonRichWorld {
    std::shared_ptr<const CrimsonSceneProjection> source;
    std::vector<std::shared_ptr<const RichOccurrence>> owners;
    std::vector<crimson::RenderModelInstance> instances;
    std::vector<crimson::DirectionalLight> directional;
    std::vector<crimson::PointLight> locals;
    std::uint64_t revision{},world_id{};
};
crimson::RenderScene CrimsonRichFrame::scene()const{
    if(!world_)return {settings,{}};
    crimson::RenderScene result{settings,{},nullptr,nullptr,world_->instances,world_->directional,world_->locals,true,world_};result.shadowInstances=world_->instances;result.shadowWorldId=world_->world_id;result.shadowResolution=1024;result.meshPopulationRevision=world_->revision;result.giWorldId=world_->world_id;result.giCameraId=camera_id;result.giEnvironmentId=world_->world_id;result.modelingPresentation=presentation;return result;
}
struct CrimsonRichScene::Impl {
    crimson::shared::Renderer& renderer;std::filesystem::path root;crimson::ResourceManager resources;
    struct Geometry {crimson::ResourceHandle<crimson::ModelAsset> request;std::shared_ptr<const void> lease=std::make_shared<const int>(0);std::string id;bool wanted{};};
    struct Image {crimson::ResourceHandle<crimson::DecodedImage> request;std::shared_ptr<const std::vector<std::byte>> rgba;std::uint64_t revision{};bool wanted{};};
    std::map<std::string,Geometry> geometry;
    std::map<std::string,Image> images;
    std::map<std::uint64_t,std::shared_ptr<const CrimsonStandardMaterial>> materials;
    std::map<std::uint64_t,std::shared_ptr<const RichOccurrence>> occurrences;
    std::shared_ptr<const CrimsonRichWorld> accepted;
    crimson::ResourceHandle<crimson::PreparedEnvironment> environment;
    crimson::ResourceHandle<crimson::DecodedImage> uv_debug_image;
    std::string environment_id,message,domain;std::uint64_t environment_revision{},revision{},world_id{};
    RenderSceneBindingKey binding;bool has_binding{},pending{},environment_pending{};
    explicit Impl(crimson::shared::Renderer& owner,std::filesystem::path path):renderer(owner),root(std::move(path)),world_id(new_world_identity()){}
    ~Impl(){resources.stop();clear();}
    void clear(){accepted.reset();occurrences.clear();materials.clear();images.clear();for(auto& [key,entry]:geometry){(void)key;if(!entry.id.empty())renderer.evictModel(entry.id);}geometry.clear();environment={};environment_revision=0;environment_id.clear();has_binding=false;domain.clear();pending=environment_pending=false;}
    void bind(const RenderSceneBindingKey& next){
        if(has_binding&&binding==next)return;
        binding=next;has_binding=true;world_id=new_world_identity();
        domain=std::to_string(world_id)+":"+std::to_string(next.scene_session_id)+":"+
            std::to_string(next.activation_generation)+":"+std::to_string(next.binding_revision)+":";
        accepted.reset();occurrences.clear();materials.clear();images.clear();
        // Frozen frames retain their immutable occurrence/geometry leases.
        // The normal prune boundary evicts only after the final frame releases.
        for(auto& [key,entry]:geometry){(void)key;entry.wanted=false;entry.request={};}
    }
    bool texture(const RenderTextureAssetSnapshot& source,RenderMaterialTextureEvaluationSnapshot& destination){
        destination.project_texture_id=source.project_texture_id;destination.identity_revision=source.project_texture_id.valid()?render_project_asset_identity_revision(source.project_texture_id):std::max<std::uint64_t>(1,source.texture_id);destination.content_revision=source.content_revision?source.content_revision:source.texture.revision.value;destination.color_space=source.texture.color_space;
        if(!source.present)return true;
        if(source.decoded_rgba8&&source.width&&source.height&&source.decoded_rgba8->size()==std::size_t(source.width)*source.height*4){destination.decoded_rgba8=source.decoded_rgba8;destination.width=source.width;destination.height=source.height;return true;}
        auto image_path=source.texture.source_path;if(!image_path.empty()&&image_path.is_relative())image_path=root/image_path;
        const auto key=domain+(source.encoded_file_bytes?"pinned:"+std::to_string(reinterpret_cast<std::uintptr_t>(source.encoded_file_bytes.get())):crimson::resourcePathKey(image_path))+":"+std::to_string(destination.content_revision);
        auto& entry=images[key];entry.wanted=true;
        if(!entry.request){crimson::ResourceRequestOptions options;options.priority=crimson::ResourcePriority::Visible;options.retainLoader=false;options.cancelWhenUnused=true;if(!image_path.empty())options.dependencies={image_path};const auto encoded=source.encoded_file_bytes;const auto file=image_path;entry.request=resources.requestDerived<crimson::DecodedImage>("quader-standard:"+key,[encoded,file](std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Rendered material image canceled.");auto image=std::make_shared<crimson::DecodedImage>();if(encoded&&!encoded->empty()){auto qimage=QImage::fromData(reinterpret_cast<const uchar*>(encoded->data()),int(encoded->size())).convertToFormat(QImage::Format_RGBA8888);if(qimage.isNull()||qimage.width()>16384||qimage.height()>16384)throw std::runtime_error("Rendered material image is invalid.");image->width=qimage.width();image->height=qimage.height();image->rgba.resize(std::size_t(image->width)*image->height*4);for(unsigned y=0;y<image->height;++y)std::memcpy(image->rgba.data()+std::size_t(y)*image->width*4,qimage.constScanLine(int(y)),std::size_t(image->width)*4);}else{if(file.empty())throw std::runtime_error("Rendered material image has no pinned pixels or source.");*image=crimson::decodeManagedImage(file);}if(stop.stop_requested())throw std::runtime_error("Rendered material image canceled.");const auto bytes=image->rgba.size();return crimson::ResourceLoadResult<crimson::DecodedImage>{image,bytes,0,{}};},std::move(options));}
        const auto image=entry.request.snapshot();if(!image){const bool failed=entry.request.state()==crimson::ResourceState::Failed;pending=pending||!failed;if(failed)message=entry.request.diagnostic();return false;}
        if(image->format!=crimson::ImagePixelFormat::RGBA8)throw std::runtime_error("Rendered Standard material requires an ordinary RGBA8 image.");
        if(entry.revision!=image.revision()){auto pixels=std::make_shared<std::vector<std::byte>>(image->rgba.size());std::memcpy(pixels->data(),image->rgba.data(),image->rgba.size());entry.rgba=std::move(pixels);entry.revision=image.revision();}
        destination.width=image->width;destination.height=image->height;destination.decoded_rgba8=entry.rgba;return true;
    }
    std::shared_ptr<const CrimsonStandardMaterial> material(std::uint64_t id,const RenderMaterialAssetSnapshot& source){
        auto& previous=materials[id];RenderStandardMaterialEvaluationSnapshot value;value.base_color=source.document.base_color;value.roughness=source.document.roughness;value.metallic=source.document.metallic;value.diffuse_model=source.document.diffuse_model;value.normal_strength=source.normal_strength;value.directx_normal=source.directx_normal;value.texture_transform=source.texture_transform;
        constexpr std::array<const char*,4> kMaterialSlots{material_texture_slot::kBaseColor,material_texture_slot::kRoughness,material_texture_slot::kMetallic,material_texture_slot::kNormal};bool ready=true;
        for(unsigned slot=0;slot<kMaterialSlots.size();++slot)if(const auto* image=source.texture_for_slot(kMaterialSlots[slot]))ready=texture(*image,value.textures[slot])&&ready;
        if(!ready)return previous;
        if(previous&&previous->source==value)return previous;
        try{previous=prepare_crimson_standard_material(value,previous);}catch(const std::exception& error){message=error.what();pending=true;}
        return previous;
    }
    void prune(const std::set<std::uint64_t>& live_instances,const std::set<std::uint64_t>& live_materials){
        std::erase_if(occurrences,[&](const auto& entry){return !live_instances.contains(entry.first);});
        std::erase_if(materials,[&](const auto& entry){return !live_materials.contains(entry.first);});
        std::erase_if(images,[](const auto& entry){return !entry.second.wanted;});
        for(auto it=geometry.begin();it!=geometry.end();){auto& entry=it->second;if(entry.wanted){++it;continue;}entry.request={};if(entry.lease.use_count()==1){if(!entry.id.empty())renderer.evictModel(entry.id);it=geometry.erase(it);}else ++it;}
        resources.trim();
    }
};
CrimsonRichScene::CrimsonRichScene(crimson::shared::Renderer& renderer,std::filesystem::path root):impl_(std::make_unique<Impl>(renderer,std::move(root))){}
CrimsonRichScene::~CrimsonRichScene()=default;
bool CrimsonRichScene::prepare(std::shared_ptr<const CrimsonSceneProjection> source){
    auto& state=*impl_;state.resources.pump();state.pending=false;state.message.clear();if(!source||!source->source)return false;state.bind(source->source->scene_binding);
    for(auto& [key,entry]:state.geometry){(void)key;entry.wanted=false;}for(auto& [key,entry]:state.images){(void)key;entry.wanted=false;}
    auto candidate=std::make_shared<CrimsonRichWorld>();candidate->source=source;candidate->world_id=state.world_id;std::set<std::uint64_t> live_instances,live_materials;bool ready=true;
    try {
        std::map<std::uint64_t,std::shared_ptr<const CrimsonStandardMaterial>> prepared_materials;
        for(const auto& [id,material]:source->source_materials){live_materials.insert(id);prepared_materials[id]=state.material(id,material);if(!prepared_materials[id])ready=false;}
        for(const auto& instance:source->instances){live_instances.insert(instance.id);if(!instance.visible)continue;const auto mesh=std::ranges::find(source->meshes,instance.mesh,&crimson::shared::MeshUpdate::id);if(mesh==source->meshes.end())throw std::runtime_error("Rendered modeling instance has no pinned geometry.");const auto key=state.domain+geometry_key(*mesh,instance.sampling_from_local);auto& entry=state.geometry[key];entry.wanted=true;entry.id=key;
            if(!entry.request){crimson::ResourceRequestOptions options;options.priority=crimson::ResourcePriority::Visible;options.retainLoader=false;options.cancelWhenUnused=true;const auto pinned=*mesh;const auto sampling=instance.sampling_from_local;entry.request=state.resources.requestDerived<crimson::ModelAsset>(key,[pinned,sampling,key](std::stop_token stop){const auto value=build_crimson_rich_geometry(pinned,sampling,key,stop);return crimson::ResourceLoadResult<crimson::ModelAsset>{value,value->byte_size,hash(key),{}};},std::move(options));}
            const auto resource=entry.request.snapshot();if(!resource){const bool failed=entry.request.state()==crimson::ResourceState::Failed;state.pending=state.pending||!failed;ready=false;if(failed)state.message=entry.request.diagnostic();continue;}const auto geometry=std::static_pointer_cast<const CrimsonRichGeometry>(resource.shared());state.renderer.configureModelStream(geometry->id,resource.revision(),*geometry->stream);bool resident=true;for(const auto& upload:geometry->uploads)if(!state.renderer.modelLodReady(geometry->id,resource.revision(),upload->primitive,0)){state.renderer.queueModelLod(geometry->id,resource.revision(),upload);resident=false;}if(!resident){state.pending=true;ready=false;continue;}
            std::vector<std::shared_ptr<const CrimsonStandardMaterial>> material_pins;for(const auto slot:geometry->source_slots){std::uint64_t material=instance.surface;for(const auto& binding:instance.surface_bindings)if(binding.slot==slot){material=binding.surface;break;}const auto found=prepared_materials.find(material);if(found==prepared_materials.end()||!found->second){ready=false;material_pins.clear();break;}material_pins.push_back(found->second);}if(material_pins.size()!=geometry->source_slots.size())continue;
            bool cast=true;for(const auto& [id,value]:source->instance_cast_shadows)if(id==instance.id){cast=value;break;}crimson::Mat4 world;world.values=instance.world_from_local.values;
            auto& previous=state.occurrences[instance.id];if(!previous||previous->geometry!=geometry||previous->world!=world||previous->material_pins!=material_pins||previous->double_sided!=instance.double_sided||previous->cast_shadows!=cast||previous->context_dimmed!=instance.context_dimmed){auto occurrence=std::make_shared<RichOccurrence>();occurrence->geometry=geometry;occurrence->geometry_lease=entry.lease;occurrence->identity=instance.id;occurrence->resource_revision=resource.revision();occurrence->world=world;occurrence->double_sided=instance.double_sided;occurrence->cast_shadows=cast;occurrence->context_dimmed=instance.context_dimmed;occurrence->material_pins=std::move(material_pins);for(const auto& material:occurrence->material_pins){auto surface=material->surface;surface.doubleSided=instance.double_sided;surface.receiveFog=true;occurrence->materials.push_back(std::move(surface));occurrence->textures.push_back(material->bindings);}previous=std::move(occurrence);}candidate->owners.push_back(previous);
        }
        if(ready){for(const auto& owner:candidate->owners){candidate->instances.push_back({owner->identity,{owner->geometry,owner->world},&owner->textures,owner->resource_revision,true,false,&owner->materials});candidate->instances.back().castShadows=owner->cast_shadows;candidate->instances.back().receiveShadows=true;candidate->instances.back().modelingContextDimmed=owner->context_dimmed;}
            for(const auto& source_light:source->lights){const auto& light=source_light.light;const auto values=light_engine_values(light);const auto energy=values.preview_energy*std::numbers::pi_v<float>;const auto color=display_color(light.color);const auto position=transform_point(source_light.world_transform,{});auto direction=transform_vector(source_light.world_transform,{0,-1,0});direction=length(direction)>1e-6f?normalize(direction):QVec3{0,-1,0};if(light.type==LightType::Directional){crimson::DirectionalLight value;value.entityId=source_light.entity_id;value.direction=vector(direction);value.color=color;value.intensity=energy;value.enabled=energy>0;value.castShadows=light.casts_shadows;candidate->directional.push_back(value);}else{crimson::PointLight value;value.entityId=source_light.entity_id;value.position=vector(position);value.direction=vector(direction);value.color=color;value.intensity=energy;value.enabled=energy>0;value.radius=std::max(.01f,light.range);value.attenuation=light.distance_attenuation;value.castShadows=light.casts_shadows;value.type=light.type==LightType::Spot?crimson::LocalLightType::Spot:crimson::LocalLightType::Point;candidate->locals.push_back(value);}}
            if(candidate->directional.size()>crimson::MaxDirectionalLights||candidate->locals.size()>crimson::MaximumClusteredPointLights)throw std::runtime_error("Rendered modeling lights exceed the canonical Crimson lighting budget.");
            const bool changed=!state.accepted||state.accepted->source!=source||state.accepted->owners!=candidate->owners;if(changed){candidate->revision=++state.revision;state.accepted=std::move(candidate);}
        }
    }catch(const std::exception& error){state.message=error.what();ready=false;}
    state.prune(live_instances,live_materials);if(!ready&&state.message.empty())state.message="Preparing Rendered geometry and materials…";return ready&&state.accepted&&state.accepted->source==source;
}
bool CrimsonRichScene::prepare_environment(const std::string& id){auto& state=*impl_;if(!state.environment||state.environment_id!=id){state.environment_id=id;state.environment_revision=0;crimson::ResourceRequestOptions options;options.priority=crimson::ResourcePriority::Visible;options.retainLoader=false;options.cancelWhenUnused=true;const auto root=state.root;state.environment=state.resources.requestDerived<crimson::PreparedEnvironment>("quader-ibl:"+id,[root,id](std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Rendered environment canceled.");auto value=std::make_shared<const crimson::PreparedEnvironment>(prepare_crimson_packaged_environment(root,id));if(stop.stop_requested())throw std::runtime_error("Rendered environment canceled.");std::size_t bytes=value->data.diffuse.rgba.size()*sizeof(float)+value->data.brdf.size()*sizeof(float);for(const auto& mip:value->data.specular)bytes+=mip.rgba.size()*sizeof(float);return crimson::ResourceLoadResult<crimson::PreparedEnvironment>{value,bytes,value->key,{}};},std::move(options));}const auto ready=state.environment.snapshot();if(!ready){state.environment_pending=state.environment.state()!=crimson::ResourceState::Failed;if(!state.environment_pending)state.message=state.environment.diagnostic();return false;}if(state.environment_revision!=ready.revision()){const auto result=state.renderer.publish_modeling_environment(*ready);if(!result.ok()){state.message=std::string(result.error_message());return false;}state.environment_revision=ready.revision();}state.environment_pending=false;return true;}
std::shared_ptr<const CrimsonRichFrame> CrimsonRichScene::view(const SceneSettings& settings,const ViewportCameraState& camera,const ViewportCameraPose& pose,crimson::AntiAliasing aa,std::uint64_t camera_id,const crimson::ModelingPresentation& controls,bool scene_lighting){
    auto& state=*impl_;
    if(!state.accepted||!prepare_environment(settings.environment.environment_id))return {};
    try{
        auto frame=std::make_shared<CrimsonRichFrame>();
        frame->settings=presentation_settings(settings,camera,pose,aa);
        auto packet=std::make_shared<crimson::ModelingPresentation>(controls);
        packet->postProcessing=scene_lighting&&settings.post_process.enabled;
        // Quader's retired serialized ACESLegacy / DisplayRange names both
        // resolve to its current ACES owner, as in scene_tone_mapper.cpp.
        switch(packet->postProcessing?settings.post_process.tone_mapping:SceneToneMapping::Linear){
        case SceneToneMapping::Linear:packet->toneMapper=crimson::ModelingToneMapper::Linear;break;
        case SceneToneMapping::Aces:case SceneToneMapping::AcesLegacy:case SceneToneMapping::DisplayRange:packet->toneMapper=crimson::ModelingToneMapper::ACES;break;
        case SceneToneMapping::Filmic:packet->toneMapper=crimson::ModelingToneMapper::Filmic;break;
        case SceneToneMapping::Agx:packet->toneMapper=crimson::ModelingToneMapper::AgX;break;
        }
        const auto& source_fog=settings.fog;auto& fog=packet->fog;
        fog.enabled=scene_lighting&&source_fog.enabled;fog.colorFromEnvironment=source_fog.color_from_environment;
        fog.distance=source_fog.distance;fog.cutOffDistance=source_fog.cut_off_distance;fog.maximumOpacity=source_fog.maximum_opacity;
        fog.height=source_fog.height;fog.heightFalloff=source_fog.height_falloff;fog.density=source_fog.density;
        fog.inScatteringStart=source_fog.in_scattering_start;fog.inScatteringSize=source_fog.in_scattering_size;fog.color=vector(source_fog.color);
        packet->skyIntensity=settings.environment.enabled?settings.environment.background_intensity*(30000.f/38400.f):0.f;
        if(!scene_lighting){frame->settings.environment.enabled=false;frame->settings.environment.intensity=0;frame->settings.environment.background.show=false;frame->settings.environment.glow.enabled=false;frame->settings.environment.ssao.enabled=false;frame->settings.environment.debanding=false;frame->settings.antiAliasing=aa;}
        if(packet->materialDebug==crimson::ModelingMaterialDebug::UV){
            if(!state.uv_debug_image){
                const auto path=state.root/"textures"/"uv_checker.png";crimson::ResourceRequestOptions options;options.dependencies={path};options.priority=crimson::ResourcePriority::Visible;options.retainLoader=false;
                state.uv_debug_image=state.resources.requestDerived<crimson::DecodedImage>("quader-modeling-uv-checker:"+crimson::resourcePathKey(path),[path](std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("UV checker preparation canceled.");auto image=std::make_shared<const crimson::DecodedImage>(crimson::decodeManagedImage(path));return crimson::ResourceLoadResult<crimson::DecodedImage>{image,image->rgba.size(),0,{}};},std::move(options));
            }
            const auto image=state.uv_debug_image.snapshot();if(!image){const bool failed=state.uv_debug_image.state()==crimson::ResourceState::Failed;state.pending|=!failed;if(failed)state.message=state.uv_debug_image.diagnostic();return {};}
            packet->uvDebugImage=image.shared();
        }
        frame->presentation=std::move(packet);frame->world_=state.accepted;frame->source_generation=state.accepted->source->generation;frame->camera_id=camera_id;return frame;
    }catch(const std::exception& error){state.message=error.what();return {};}
}
bool CrimsonRichScene::pending()const{return impl_->pending||impl_->environment_pending;}
const std::string& CrimsonRichScene::status()const{return impl_->message;}
void CrimsonRichScene::clear(){impl_->clear();}
}
