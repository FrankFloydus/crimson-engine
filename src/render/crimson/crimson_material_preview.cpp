////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_material_preview.hpp"
#include "render/crimson/crimson_environment.hpp"
#include "render/crimson/crimson_standard_material.hpp"
#include "render/crimson/crimson_copy_identity.hpp"
#include "core/logger.hpp"
#include "viewport/viewport_settings_defaults.hpp"
#include <crimson/renderer/forward/Environment.h>
#include <crimson/renderer/forward/MaterialPreviewGeometry.h>
#include <crimson/renderer/forward/ForwardTypes.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>

namespace quader {
namespace {
using crimson::Vec3;
constexpr float kRadiansToDegrees=180.f/std::numbers::pi_v<float>;

crimson::MaterialPreviewMesh mesh_kind(MaterialPreviewMeshKind kind) {
  switch(kind) {
  case MaterialPreviewMeshKind::Box:return crimson::MaterialPreviewMesh::Cube;
  case MaterialPreviewMeshKind::RoundedBox:return crimson::MaterialPreviewMesh::RoundedCube;
  case MaterialPreviewMeshKind::Cylinder:return crimson::MaterialPreviewMesh::Cylinder;
  case MaterialPreviewMeshKind::Plane:return crimson::MaterialPreviewMesh::Plane;
  default:return crimson::MaterialPreviewMesh::Sphere;
  }
}

std::shared_ptr<const crimson::CollisionOverlayGeometry> wire_geometry(crimson::MaterialPreviewMesh kind) {
  static std::map<crimson::MaterialPreviewMesh,std::shared_ptr<const crimson::CollisionOverlayGeometry>> cache;
  if(auto found=cache.find(kind);found!=cache.end())return found->second;
  auto result=std::make_shared<crimson::CollisionOverlayGeometry>();
  const auto model=crimson::materialPreviewGeometry(kind);
  const auto& primitive=model->primitives.front();
  for(const auto& vertex:primitive.vertices)result->vertices.push_back(vertex.position);
  std::set<std::pair<std::uint32_t,std::uint32_t>> edges;
  for(std::size_t i=0;i+2<primitive.indices.size();i+=3)for(unsigned e=0;e<3;++e) {
    const auto a=primitive.indices[i+e],b=primitive.indices[i+(e+1)%3];edges.emplace(std::min(a,b),std::max(a,b));
  }
  for(auto[a,b]:edges)result->edges.push_back({a,b,{},{},false,false,true});
  cache[kind]=result;return result;
}
}

struct CrimsonMaterialPreview::Impl {
  std::filesystem::path resources;
  MaterialPreviewState state;
  StandardMaterialPreviewSnapshot material;
  std::shared_ptr<const CrimsonStandardMaterial> textures;
  std::map<std::string,std::future<crimson::PreparedEnvironment>> jobs;
  std::map<std::string,std::shared_ptr<const crimson::PreparedEnvironment>> environments;
  std::set<std::string> failed_environments;
  CrimsonPreviewRecorder recorder;
  std::uint32_t width=1,height=1;
  std::uint64_t revision=1,submitted_revision=0;
  std::uint64_t source_owner=next_crimson_copy_identity();
  bool alive=true;
  bool material_dirty=true;
  std::uint64_t failed_material_revision=0;
  bool thumbnail=false;
  std::string thumbnail_id;
  std::uint64_t thumbnail_signature=0;
  std::shared_ptr<void> lifetime=std::make_shared<int>(0);
};
CrimsonMaterialPreview::CrimsonMaterialPreview(std::filesystem::path root,bool thumbnail):impl_(std::make_unique<Impl>()){impl_->resources=std::move(root);impl_->thumbnail=thumbnail;}
CrimsonMaterialPreview::~CrimsonMaterialPreview()=default;
void CrimsonMaterialPreview::shutdown(){impl_->alive=false;release_gpu();}
void CrimsonMaterialPreview::release_gpu(){impl_->textures.reset();impl_->submitted_revision=0;last_copy={};}
void CrimsonMaterialPreview::resize(std::uint32_t w,std::uint32_t h){w=std::max(1u,w);h=std::max(1u,h);if(impl_->width!=w||impl_->height!=h){impl_->width=w;impl_->height=h;mark_dirty();}}
void CrimsonMaterialPreview::set_preview_state(const MaterialPreviewState& state){if(impl_->state.revision()!=state.revision()){impl_->state=state;mark_dirty();}}
bool CrimsonMaterialPreview::set_material(StandardMaterialPreviewSnapshot material){if(!material.standard_material())return false;if(impl_->material==material)return true;if(impl_->material.project_material_id!=material.project_material_id)impl_->source_owner=next_crimson_copy_identity();impl_->material=std::move(material);impl_->material_dirty=true;mark_dirty();return true;}
bool CrimsonMaterialPreview::initialized()const noexcept{return impl_->alive;}
std::uint32_t CrimsonMaterialPreview::width()const noexcept{return impl_->width;}
std::uint32_t CrimsonMaterialPreview::height()const noexcept{return impl_->height;}
bool CrimsonMaterialPreview::pending()const noexcept{return dirty()&&impl_->failed_material_revision!=impl_->revision&&!impl_->failed_environments.contains(std::string(impl_->state.environment_id()));}
std::weak_ptr<void> CrimsonMaterialPreview::lifetime()const{return impl_->lifetime;}
std::uint64_t CrimsonMaterialPreview::source_owner()const noexcept{return impl_->source_owner;}
void CrimsonMaterialPreview::set_recorder(CrimsonPreviewRecorder recorder){impl_->source_owner=next_crimson_copy_identity();impl_->recorder=std::move(recorder);mark_dirty();}
void CrimsonMaterialPreview::set_thumbnail_identity(std::string id,std::uint64_t signature){impl_->thumbnail_id=std::move(id);impl_->thumbnail_signature=signature;mark_dirty();}
void CrimsonMaterialPreview::mark_dirty(){++impl_->revision;}
bool CrimsonMaterialPreview::dirty()const{return impl_->revision!=impl_->submitted_revision;}
void CrimsonMaterialPreview::submitted(){impl_->submitted_revision=impl_->revision;}

bool CrimsonMaterialPreview::record(crimson::shared::Renderer& renderer) {
  auto& p=*impl_;if(!p.alive)return false;
  const std::string id(p.state.environment_id());
  if(p.failed_environments.contains(id))return false;
  auto environment=p.environments.find(id);
  if(environment==p.environments.end()) {
    auto job=p.jobs.find(id);
    if(job==p.jobs.end()){p.jobs.emplace(id,std::async(std::launch::async,[root=p.resources,id]{return prepare_crimson_packaged_environment(root,id);}));return false;}
    if(job->second.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
    try {environment=p.environments.emplace(id,std::make_shared<const crimson::PreparedEnvironment>(job->second.get())).first;}
    catch(const std::exception& error){LOG_ERROR("Material preview environment: {}",error.what());p.failed_environments.insert(id);p.jobs.erase(job);return false;}
    p.jobs.erase(id);
  }
  if(p.thumbnail?!renderer.publishThumbnailEnvironment(*environment->second):!renderer.publishMaterialPreviewEnvironment(*environment->second))return false;
  if(p.recorder)return p.recorder(p.state,p.width,p.height);
  const auto* material=p.material.standard_material();if(!material)return false;
  if(p.failed_material_revision==p.revision)return false;
  if(!p.textures||p.material_dirty) {
    try {p.textures=prepare_crimson_standard_material(*material,p.textures);p.material_dirty=false;}
    catch(const std::exception& error){LOG_ERROR("Standard material preview preparation failed: {}",error.what());p.failed_material_revision=p.revision;return false;}
  }
  crimson::SceneSettings settings;
  const auto pan=p.state.pan_offset();settings.camera.position={pan.x,pan.y,p.state.camera_distance()};settings.camera.target={pan.x,pan.y,0};
  settings.camera.nearPlane=.05f;settings.camera.farPlane=100;settings.camera.fov=45;settings.camera.fovAxis=crimson::CameraFovAxis::Vertical;
  settings.environment.rotation=p.state.environment_rotation_radians()*kRadiansToDegrees;
  settings.environment.background.show=p.state.show_environment_background();
  settings.environment.background.overrideColor=!settings.environment.background.show;settings.environment.background.color={.08f,.08f,.08f};
  settings.environment.toneMapping=crimson::ToneMapping::ACES;
  settings.antiAliasing=crimson::AntiAliasing::Smaa;
  const auto kind=mesh_kind(p.state.mesh_kind());
  const auto q=p.state.object_rotation();const auto transform=crimson::math::rotation(crimson::Quat{q.x,q.y,q.z,q.w});
  crimson::RenderScene scene{settings,{}};scene.sceneMode=true;scene.pinned=p.textures;scene.meshInstancingEnabled=false;scene.previewSeparateMetallicRoughness=true;
  settings.directional.direction=crimson::math::rotate({-.45f,-.75f,-.5f},{0,1,0},p.state.environment_rotation_radians());
  scene.directionalLights={&settings.directional,1};
  std::array<crimson::DirectionalLight,3> thumbnail_lights;
  if(p.thumbnail) {
    thumbnail_lights[0]=settings.directional;thumbnail_lights[0].color={1,.98f,.95f};
    thumbnail_lights[1].color={.82f,.90f,1};thumbnail_lights[1].intensity=2.f*18000.f/65000.f;thumbnail_lights[1].direction={.75f,-.2f,-.65f};
    thumbnail_lights[2].color={1,.92f,.82f};thumbnail_lights[2].intensity=2.f*45000.f/65000.f;thumbnail_lights[2].direction={.25f,-.55f,.8f};
    scene.directionalLights=thumbnail_lights;scene.transparentThumbnailBackground=true;
  }
  std::array<crimson::RenderCollisionOverlay,1> wires;
  if(p.state.shading_mode()==MaterialPreviewShadingMode::Wireframe) {
    wires[0]={wire_geometry(kind),transform,{.82f,.82f,.82f,1},0};scene.collisionOverlays=wires;
  } else {
    const auto& surface=p.textures->surface;
    const auto geometry=crimson::materialPreviewGeometry(kind);
    renderer.configureModelStream(geometry->id,1,*geometry->stream);
    if(!renderer.modelLodReady(geometry->id,1,0,0)&&!renderer.queueModelLod(geometry->id,1,crimson::materialPreviewGeometryLevel(kind)))return false;
    const std::vector<crimson::ModelMaterial> surfaces{surface};
    std::vector<crimson::ModelMaterialTextures> textures{p.textures->bindings};
    if(p.state.shading_mode()==MaterialPreviewShadingMode::Shaded) {
      settings.environment.toneMapping=crimson::ToneMapping::Linear;
      scene.previewShadedLighting={true,{kDefaultViewportShadedKeyDirectionX,kDefaultViewportShadedKeyDirectionY,kDefaultViewportShadedKeyDirectionZ},
          kDefaultViewportShadedKeyIntensity,kDefaultViewportShadedDiffuseWrap,kDefaultViewportShadedGroundFill,kDefaultViewportShadedSkyFill};
    }
    crimson::RenderModelInstance instance{0,{geometry,transform},&textures,1,true,false,&surfaces};
    scene.instances={&instance,1};scene.previewBurleyDiffuse=material->diffuse_model==MaterialDiffuseModel::Burley;
    if(p.thumbnail)return renderer.renderThumbnail(p.thumbnail_id,p.thumbnail_signature,scene,static_cast<std::uint16_t>(p.width),p.textures,"quader-standard-material");
    renderer.resizeMaterialPreview(p.width,p.height);renderer.renderMaterialPreview(scene);return true;
  }
  renderer.resizeMaterialPreview(p.width,p.height);renderer.renderMaterialPreview(scene);return true;
}
}
