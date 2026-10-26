#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/DirectionalShadowMath.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include <crimson/renderer/forward/ModelAnimation.h>
#include "crimson-renderer/Picking.h"
#include "assets/BuiltinGeometry.h"
#include "resources/ResourceManager.h"
#include "resources/ShaderCompiler.h"
#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
constexpr std::uint16_t Size=256,ReadView=DiagnosticReadbackView;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Sdl {Sdl(){require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
struct Temporary {
    fs::path path=fs::temp_directory_path()/("crimson-directional-quality-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary(){fs::create_directories(path);}
    ~Temporary(){std::error_code error;const auto resolved=fs::weakly_canonical(path,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-directional-quality-"))fs::remove_all(resolved,error);}
};
std::shared_ptr<ModelAsset> floorGeometry(){
    auto mesh=std::make_shared<ModelAsset>();mesh->id="directional-quality-floor";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-18,0,18},{18,0,18},{18,0,-18},{-18,0,-18}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned index=0;index<4;++index){ModelVertex v;v.position=positions[index];v.normal={0,1,0};v.tangent={1,0,0,-1};v.uv0=v.uv1=uv[index];primitive.vertices.push_back(v);math::include(mesh->bounds,v.position);}
    primitive.indices={0,1,2,0,2,3};mesh->primitives.push_back(std::move(primitive));mesh->draws.push_back({0,{}});mesh->materials.emplace_back();return mesh;
}
Camera gameCamera(){
    Camera camera;camera.position={8,3.2f,6};camera.target={1,1.5f,-3};
    camera.fov=75;camera.fovAxis=CameraFovAxis::Vertical;camera.nearPlane=.05f;camera.farPlane=2048;return camera;
}
Camera sceneCamera(){
    Camera camera;camera.position={8.6003008f,10.6881323f,4.6002536f};camera.target={.6423655f,1.6077204f,-1.1630378f};
    camera.fov=45;camera.nearPlane=.01f;camera.farPlane=2048;return camera;
}
std::optional<unsigned> pixel(const Camera& camera,Vec3 world){
    const auto projected=math::project(world,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
    if(!projected||(*projected)[0]<3||(*projected)[1]<3||(*projected)[0]>=Size-3||(*projected)[1]>=Size-3)return {};
    return (static_cast<unsigned>((*projected)[1])*Size+static_cast<unsigned>((*projected)[0]))*4;
}
double brightness(const DecodedImage& image,unsigned offset){return (double(image.rgba[offset])+image.rgba[offset+1]+image.rgba[offset+2])/3;}
void save(const DecodedImage& image,const fs::path& directory,const std::string& name){
    if(directory.empty())return;fs::create_directories(directory);std::vector<std::uint8_t> png;
    require(lodepng::encode(png,image.rgba,image.width,image.height)==0,"Cannot encode directional quality evidence.");
    std::ofstream out(directory/(name+".png"),std::ios::binary);out.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));require(bool(out),"Cannot write directional quality evidence.");
}
void litSamples(const DecodedImage& reference,const DecodedImage& shadowed,const Camera& camera,
    std::span<const Vec3> points,const std::string& label,unsigned minimum){
    unsigned samples{},maximumLoss{},bad{};double lossSum{};
    for(const auto point:points)if(const auto offset=pixel(camera,point)){
        if(brightness(reference,*offset)<25)continue;
        ++samples;bool reported{};for(unsigned channel=0;channel<3;++channel){const auto loss=static_cast<unsigned>(std::max(0,int(reference.rgba[*offset+channel])-int(shadowed.rgba[*offset+channel])));
            maximumLoss=std::max(maximumLoss,loss);lossSum+=loss;bad+=loss>3;
            if(loss>3&&!reported){std::cout<<label<<" bad world="<<point[0]<<","<<point[1]<<","<<point[2]
                <<" pixel="<<(*offset/4)%Size<<","<<(*offset/4)/Size<<" loss="<<loss<<std::endl;reported=true;}}

    }
    const auto meanLoss=samples?lossSum/(samples*3):0;
    std::cout<<label<<" samples="<<samples<<" maximum_loss="<<maximumLoss<<" mean_loss="<<meanLoss<<" bad_channels="<<bad<<std::endl;
    require(samples>=minimum,label+": insufficient visible lit interior samples.");
    require(maximumLoss<=3&&bad==0,label+": directional self-shadow acne: maximum_loss="+std::to_string(maximumLoss)+", mean_loss="+std::to_string(meanLoss));
}
double linearByte(unsigned value){
    const double encoded=value/255.;return encoded<=.04045?encoded/12.92:std::pow((encoded+.055)/1.055,2.4);
}
double linearBrightness(const DecodedImage& image,unsigned offset){
    return (linearByte(image.rgba[offset])+linearByte(image.rgba[offset+1])+linearByte(image.rgba[offset+2]))/3;
}
// Separate, zero-thickness faces reproduce an imported inward-normal room.
// Every edge is an exact floor/wall or wall/wall join, with no authored gaps.
constexpr Vec3 RoomMinimum{-3,0,-3},RoomMaximum{3,3,3};
std::array<std::shared_ptr<ModelAsset>,6> closedRoomGeometry(){
    std::array<std::shared_ptr<ModelAsset>,6> result;
    const auto center=(RoomMinimum+RoomMaximum)*.5,extent=(RoomMaximum-RoomMinimum)*.5;
    for(unsigned axis=0;axis<3;++axis)for(unsigned side=0;side<2;++side){
        const unsigned face=axis*2+side;auto mesh=std::make_shared<ModelAsset>();
        mesh->id="directional-closed-room-face-"+std::to_string(face);
        Vec3 normal{},u{};normal[axis]=side?-1.f:1.f;u[(axis+1)%3]=1;
        const auto v=math::cross(normal,u);auto origin=center;origin[axis]=side?RoomMaximum[axis]:RoomMinimum[axis];
        ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
        constexpr std::array<Vec2,4> corners{{{-1,-1},{1,-1},{1,1},{-1,1}}};
        for(const auto corner:corners){
            ModelVertex vertex;vertex.position=origin+u*(corner[0]*extent[(axis+1)%3])+v*(corner[1]*extent[(axis+2)%3]);
            vertex.normal=normal;vertex.tangent={u[0],u[1],u[2],1};vertex.uv0=vertex.uv1={(corner[0]+1)*.5f,(corner[1]+1)*.5f};
            primitive.vertices.push_back(vertex);math::include(mesh->bounds,vertex.position);
        }
        primitive.indices={0,1,2,0,2,3};mesh->primitives.push_back(std::move(primitive));
        mesh->draws.push_back({0,{}});mesh->materials.emplace_back();result[face]=std::move(mesh);
    }
    return result;
}
struct RoomHit {Vec3 position{},normal{};double edgeDistance{};};
std::optional<RoomHit> closedRoomHit(const Ray& ray){
    std::optional<RoomHit> result;double nearest=1e30;
    for(unsigned axis=0;axis<3;++axis)for(unsigned side=0;side<2;++side){
        if(std::abs(ray.direction[axis])<1e-9)continue;
        const double distance=((side?RoomMaximum[axis]:RoomMinimum[axis])-ray.origin[axis])/ray.direction[axis];
        if(distance<=0||distance>=nearest)continue;
        const auto point=ray.origin+ray.direction*distance;double edge=1e30;bool inside=true;
        for(unsigned other=0;other<3;++other)if(other!=axis){
            const double margin=std::min(point[other]-RoomMinimum[other],RoomMaximum[other]-point[other]);
            inside&=margin>=-1e-5;edge=std::min(edge,margin);
        }
        if(!inside)continue;Vec3 normal{};normal[axis]=side?-1.f:1.f;
        result=RoomHit{point,normal,edge};nearest=distance;
    }
    return result;
}
// Independent finite analytic box intersection in local space. Preserve the
// inverse-transformed direction's length: the resulting t remains world t.
// This oracle never uses uploaded triangles, BLAS bounds or shader helpers.
std::optional<RoomHit> affineClosedRoomHit(const Ray& ray,const Mat4& inverse){
    std::array<double,3> origin{},direction{};
    for(unsigned row=0;row<3;++row){
        origin[row]=inverse.at(row,3);
        for(unsigned column=0;column<3;++column){
            origin[row]+=double(inverse.at(row,column))*ray.origin[column];
            direction[row]+=double(inverse.at(row,column))*ray.direction[column];
        }
    }
    std::optional<RoomHit> result;double nearest=1e30;
    for(unsigned axis=0;axis<3;++axis)for(unsigned side=0;side<2;++side){
        if(std::abs(direction[axis])<1e-12)continue;
        const double distance=((side?RoomMaximum[axis]:RoomMinimum[axis])-origin[axis])/direction[axis];
        if(distance<=0||distance>=nearest)continue;
        bool inside=true;double edge=1e30;
        for(unsigned other=0;other<3;++other)if(other!=axis){
            const double coordinate=origin[other]+direction[other]*distance;
            inside&=coordinate>=RoomMinimum[other]-1e-8&&coordinate<=RoomMaximum[other]+1e-8;
            double inverseLength{};for(unsigned column=0;column<3;++column)inverseLength+=double(inverse.at(other,column))*inverse.at(other,column);
            edge=std::min(edge,std::min(coordinate-RoomMinimum[other],RoomMaximum[other]-coordinate)/std::sqrt(inverseLength));
        }
        if(!inside)continue;
        Vec3 normal{};
        for(unsigned row=0;row<3;++row)normal[row]=inverse.at(axis,row)*(side?-1.f:1.f);
        result=RoomHit{ray.origin+ray.direction*distance,math::normalized(normal),edge};nearest=distance;
    }
    return result;
}
template<class Read,class Tick> void closedRoomLeakChecks(Renderer& renderer,const Read& read,const Tick& tick,
    std::uint32_t& frame,const fs::path& capture,bool profile,bool exactFilter){
    const auto meshes=closedRoomGeometry();
    std::vector<ModelMaterial> materials(1);materials[0].baseColor={1,1,1,1};materials[0].roughness=1;
    materials[0].metallic=materials[0].specular=0;materials[0].doubleSided=true;
    std::vector<ModelMaterialTextures> bindings(1);std::array<RenderModelInstance,6> instances;
    std::array<Aabb,6> bounds;
    for(unsigned face=0;face<instances.size();++face){
        require(renderer.prepareModel(*meshes[face]),renderer.modelStatus());
        instances[face].entityId=810+face;instances[face].model.asset=meshes[face];instances[face].materials=&materials;
        instances[face].textures=&bindings;instances[face].forceFullDetail=true;bounds[face]=meshes[face]->bounds;
    }
    SceneSettings settings;settings.environment.enabled=false;settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.fog.enabled=settings.environment.glow.enabled=settings.environment.ssgi.enabled=false;
    settings.environment.ssao.enabled=settings.environment.ssr.enabled=false;settings.antiAliasing=AntiAliasing::Off;settings.exposure=1;
    settings.camera.position={0,1.4f,0};settings.camera.fov=75;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.05f;
    DirectionalLight sun;sun.entityId=820;sun.color={1,1,1};sun.intensity=1;sun.shadowDistance=100;
    sun.shadowNormalBias=1;sun.shadowAngularDiameter=0;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=instances;scene.shadowInstances=instances;
    scene.directionalLights=std::span(&sun,1);scene.shadowWorldId=821;
    const auto previousScene=renderer.debugView(RenderViewPurpose::Scene),previousGame=renderer.debugView(RenderViewPurpose::Game);
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::DirectLighting);
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);
    // First is the saved Main.crimson-scene sun; the controls cover a roof
    // blocker and the opposite wall, all outside the interior camera frustum.
    constexpr std::array<Vec3,3> directions{{{-.835363f,-.384680f,.392668f},{-.15f,-.98f,-.12f},{.7f,-.2f,-.6f}}};
    for(const unsigned resolution:{256u,1024u})for(unsigned direction=0;direction<directions.size();++direction)
        for(const float bias:{.005f,0.f})for(bool game:{false,true}){
        sun.direction=math::normalized(directions[direction]);sun.shadowDepthBias=bias;scene.shadowResolution=static_cast<std::uint16_t>(resolution);
        settings.camera.target={sun.direction[0]<0?-3.f:3.f,.65f,.7f};
        const auto label=std::string("closed-room-")+(game?"game-":"scene-")+std::to_string(resolution)+"-sun"+std::to_string(direction)+"-bias"+std::to_string(bias);
        const auto render=[&](bool shadows){sun.castShadows=shadows;if(game)renderer.renderGame(scene);else renderer.render(scene);
            require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());};
        const auto clear=render(false),shadowed=render(true);save(clear,capture,label+"-shadows-off");save(shadowed,capture,label+"-shadowed");
        const auto fit=directional_shadow::build(sun,{settings.camera,1},resolution,bounds);require(fit.valid,label+": invalid cascade fit.");
        unsigned interior{},interiorLeaks{},seam{},seamLeaks{},interiorMaximumByte{},seamMaximumByte{},seamByteLeaks{};
        double interiorMaximum{},interiorSum{},seamMaximum{},farthestLeak{};
        RoomHit worstJoin;unsigned worstX{},worstY{},worstCascade{};double worstReference{};
        auto mask=shadowed;
        for(unsigned y=3;y<Size-3;++y)for(unsigned x=3;x<Size-3;++x){
            const unsigned offset=(y*Size+x)*4;const auto ray=settings.camera.screenRay(x+.5f,y+.5f,{0,0,Size,Size});
            require(bool(ray),label+": invalid interior camera ray.");const auto hit=closedRoomHit(*ray);
            require(bool(hit),label+": a camera ray escaped the watertight CPU room.");
            const double reference=linearBrightness(clear,offset),incidence=math::dot(hit->normal,sun.direction*-1);
            if(reference<.02||incidence<=.05)continue;
            // Every exact sun ray from this surface is blocked by another
            // room face. Exclude only a conservative six-texel projected PCF
            // footprint around joins from the strict fully occluded oracle.
            // Report that band separately instead of treating it as a hole.
            const double depth=math::dot(settings.camera.forward(),hit->position-settings.camera.eye());
            unsigned cascade{};while(cascade+1<directional_shadow::CascadeCount&&depth>=fit.splits[cascade])++cascade;
            const auto& projection=fit.values[cascade];double texel=projection.texelWorld;
            if(cascade+1<directional_shadow::CascadeCount&&depth>projection.splitFar-(projection.splitFar-projection.splitNear)*.1)
                texel=std::max(texel,double(fit.values[cascade+1].texelWorld));
            const double margin=(6/incidence+sun.shadowNormalBias)*texel+sun.shadowDepthBias;
            const bool strict=hit->edgeDistance>margin;const double visibility=linearBrightness(shadowed,offset)/reference;
            const unsigned maximumByte=std::max({shadowed.rgba[offset],shadowed.rgba[offset+1],shadowed.rgba[offset+2]});
            if(strict){++interior;interiorSum+=visibility;interiorMaximum=std::max(interiorMaximum,visibility);interiorLeaks+=visibility>.08;interiorMaximumByte=std::max(interiorMaximumByte,maximumByte);}
            else{++seam;seamByteLeaks+=maximumByte>1;seamMaximumByte=std::max(seamMaximumByte,maximumByte);if(visibility>seamMaximum){seamMaximum=visibility;worstJoin=*hit;worstX=x;worstY=y;worstCascade=cascade;worstReference=reference;}seamLeaks+=visibility>.08;}
            if(visibility>.08)farthestLeak=std::max(farthestLeak,hit->edgeDistance);
            mask.rgba[offset]=visibility>.08?255:0;mask.rgba[offset+1]=strict?255:100;mask.rgba[offset+2]=strict?0:255;
        }
        save(mask,capture,label+"-classification");
        std::cout<<label<<" interior_samples="<<interior<<" interior_leaks="<<interiorLeaks
            <<" interior_max_visibility="<<interiorMaximum<<" interior_mean_visibility="<<(interior?interiorSum/interior:0)<<" interior_max_byte="<<interiorMaximumByte
            <<" join_samples="<<seam<<" join_leaks="<<seamLeaks<<" join_byte_leaks="<<seamByteLeaks<<" join_max_visibility="<<seamMaximum<<" join_max_byte="<<seamMaximumByte
            <<" farthest_leak_from_join_m="<<farthestLeak<<std::endl;
        if(seamMaximumByte>1){
            const auto& projection=fit.values[worstCascade];const auto towardLight=sun.direction*-1;
            const double depth=math::dot(settings.camera.forward(),worstJoin.position-settings.camera.eye());
            const double begin=worstCascade?fit.splits[worstCascade-1]:0,end=fit.splits[worstCascade];
            const double ramp=std::clamp((depth-(end-(end-begin)*.1))/((end-begin)*.1),0.,1.);
            const double blend=worstCascade+1<directional_shadow::CascadeCount?ramp*ramp*(3-2*ramp):0;
            double filterTexel=projection.texelWorld,normalTexel=projection.texelWorld,pixelRadius{};
            if(blend>0){const double next=fit.values[worstCascade+1].texelWorld;filterTexel=std::max(filterTexel,next);normalTexel=normalTexel*(1-blend)+next*blend;}
            for(unsigned corner=0;corner<4;++corner){
                const auto ray=settings.camera.screenRay(float(worstX+(corner&1)),float(worstY+((corner>>1)&1)),{0,0,Size,Size});
                if(ray)if(const auto point=math::intersectPlane(*ray,worstJoin.position,worstJoin.normal))pixelRadius=std::max(pixelRadius,math::length(*point-worstJoin.position));
            }
            const double incidence=math::dot(worstJoin.normal,towardLight);
            const double range=(6*filterTexel+sun.shadowNormalBias*normalTexel+sun.shadowDepthBias+pixelRadius)/std::max(incidence,1e-4);
            unsigned axis{};while(axis<2&&std::abs(worstJoin.normal[axis])<.5f)++axis;
            // The six-plane room oracle is independent of raster depth/BVH.
            // Start strictly inside its source face to identify the actual
            // next finite blocker, rather than intersecting the receiver again.
            const auto blocker=closedRoomHit({worstJoin.position+worstJoin.normal*1e-4,towardLight});
            std::cout<<label<<" worst_join_pixel="<<worstX<<","<<worstY<<" visibility="<<seamMaximum<<" reference_linear="<<worstReference
                <<" receiver_world="<<worstJoin.position[0]<<","<<worstJoin.position[1]<<","<<worstJoin.position[2]
                <<" receiver_axis="<<axis<<" receiver_normal="<<worstJoin.normal[0]<<","<<worstJoin.normal[1]<<","<<worstJoin.normal[2]
                <<" edge_distance_m="<<worstJoin.edgeDistance<<" light_direction="<<sun.direction[0]<<","<<sun.direction[1]<<","<<sun.direction[2]
                <<" shadow_distance_m="<<sun.shadowDistance<<" cascade="<<worstCascade<<" filter_texel_world="<<filterTexel
                <<" normal_offset_m="<<sun.shadowNormalBias*normalTexel<<" pixel_radius_m="<<pixelRadius<<" cpu_contact_range_estimate_m="<<range;
            if(blocker)std::cout<<" finite_blocker_world="<<blocker->position[0]<<","<<blocker->position[1]<<","<<blocker->position[2]
                <<" finite_blocker_normal="<<blocker->normal[0]<<","<<blocker->normal[1]<<","<<blocker->normal[2]
                <<" finite_blocker_distance_m="<<math::length(blocker->position-worstJoin.position);
            else std::cout<<" finite_blocker=missing";
            std::cout<<std::endl;
            if(!capture.empty()){
                const auto purpose=game?RenderViewPurpose::Game:RenderViewPurpose::Scene;
                renderer.setDebugView(purpose,RenderDebugView::Normal);const auto normalImage=render(true);
                renderer.setDebugView(purpose,RenderDebugView::DirectLighting);save(normalImage,capture,label+"-worst-join-normal");
                const unsigned offset=(worstY*Size+worstX)*4;
                std::cout<<label<<" worst_join_normal_encoded_rgb="<<unsigned(normalImage.rgba[offset])<<","
                    <<unsigned(normalImage.rgba[offset+1])<<","<<unsigned(normalImage.rgba[offset+2])<<std::endl;
            }
        }
        require(interior>=100,label+": insufficient sunlit-reference samples outside the finite PCF join footprint.");
        require(seam>=100,label+": insufficient sunlit-reference samples at the finite room joins.");
        require(interiorLeaks==0,label+": direct sunlight crossed a closed double-sided wall outside the finite filter footprint.");
        // Keep every interior sample occluded. The accepted numerical limit is
        // one join pixel per case above one encoding byte, with peak at most16.
        // Private CSM controls retain diagnostics and the strict interior guard.
        if(exactFilter)require(interiorMaximumByte<=1&&seamByteLeaks<=1&&seamMaximumByte<=16,
            label+": closed-room leakage exceeded one bounded join pixel (peak16), or reached the strict interior.");
    }
    {
        // A reflected, rotated, nonuniform room exercises generic plane and
        // inverse-affine traversal paths beyond the exact-axis shortcut.
        const auto transform=math::translation({1.7f,-.6f,2.3f})*
            math::eulerRotationXYZ({.23f,.41f,-.17f})*math::scaling({-1.1f,.8f,1.3f});
        const auto inverse=math::inverse(transform);
        require(bool(inverse)&&math::handedness(transform)<0,"Affine closed-room fixture lost its invertible reflection.");
        const auto previousCamera=settings.camera;const auto previousSun=sun;
        for(unsigned face=0;face<instances.size();++face){instances[face].model.transform=transform;bounds[face]=math::transformBounds(meshes[face]->bounds,transform);}
        settings.camera.position=math::transformPoint(transform,{0,1.4f,0});
        settings.camera.target=math::transformPoint(transform,{-3,.65f,.7f});
        settings.camera.up=math::normalized(math::transformDirection(transform,{0,1,0}));
        sun.direction=math::normalized(math::transformDirection(transform,directions[1]));
        sun.shadowDepthBias=.005f;sun.shadowNormalBias=1;scene.shadowResolution=1024;
        for(bool game:{false,true}){
            const auto label=std::string("closed-room-affine-")+(game?"game":"scene");
            const auto render=[&](bool shadows){sun.castShadows=shadows;if(game)renderer.renderGame(scene);else renderer.render(scene);
                require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());};
            const auto clear=render(false),shadowed=render(true);save(clear,capture,label+"-shadows-off");save(shadowed,capture,label+"-shadowed");
            const auto fit=directional_shadow::build(sun,{settings.camera,1},1024,bounds);require(fit.valid,label+": invalid affine cascade fit.");
            unsigned samples{},maximumByte{},byteLeaks{},interiorByteLeaks{};double maximumVisibility{};
            for(unsigned y=3;y<Size-3;++y)for(unsigned x=3;x<Size-3;++x){
                const unsigned offset=(y*Size+x)*4;const auto ray=settings.camera.screenRay(x+.5f,y+.5f,{0,0,Size,Size});
                require(bool(ray),label+": invalid camera ray.");const auto hit=affineClosedRoomHit(*ray,*inverse);
                require(bool(hit),label+": a camera ray escaped the finite affine box oracle.");
                const double reference=linearBrightness(clear,offset),incidence=math::dot(hit->normal,sun.direction*-1);
                if(reference<.02||incidence<=.05)continue;
                require(bool(affineClosedRoomHit({hit->position+hit->normal*1e-4,sun.direction*-1},*inverse)),
                    label+": the independent finite box did not block the direct sun ray.");
                ++samples;const unsigned value=std::max({shadowed.rgba[offset],shadowed.rgba[offset+1],shadowed.rgba[offset+2]});
                maximumByte=std::max(maximumByte,value);byteLeaks+=value>1;
                const double depth=math::dot(settings.camera.forward(),hit->position-settings.camera.eye());unsigned cascade{};
                while(cascade+1<directional_shadow::CascadeCount&&depth>=fit.splits[cascade])++cascade;
                const auto& projection=fit.values[cascade];double texel=projection.texelWorld;
                if(cascade+1<directional_shadow::CascadeCount&&depth>projection.splitFar-(projection.splitFar-projection.splitNear)*.1)
                    texel=std::max(texel,double(fit.values[cascade+1].texelWorld));
                const double margin=(6/incidence+sun.shadowNormalBias)*texel+sun.shadowDepthBias;
                interiorByteLeaks+=value>1&&hit->edgeDistance>margin;
                maximumVisibility=std::max(maximumVisibility,linearBrightness(shadowed,offset)/reference);
            }
            std::cout<<label<<" reference_samples="<<samples<<" maximum_byte="<<maximumByte<<" byte_leaks="<<byteLeaks
                <<" interior_byte_leaks="<<interiorByteLeaks<<" maximum_visibility="<<maximumVisibility<<" reflected=true nonuniform=true"<<std::endl;
            require(samples>=100,label+": insufficient visible sunlit-reference samples.");
            if(exactFilter)require(maximumByte<=16&&byteLeaks<=1&&interiorByteLeaks==0,
                label+": affine leakage exceeded one bounded join pixel (peak16), or reached the strict interior.");
        }
        for(unsigned face=0;face<instances.size();++face){instances[face].model.transform={};bounds[face]=meshes[face]->bounds;}
        settings.camera=previousCamera;sun=previousSun;
    }
    if(profile){
        constexpr std::uint16_t ProfileWidth=1280,ProfileHeight=720;
        renderer.resizeGameViewport(ProfileWidth,ProfileHeight);
        scene.shadowResolution=1024;sun.direction=math::normalized(directions[0]);sun.shadowDepthBias=.005f;sun.castShadows=true;
        settings.camera.target={-3,.65f,.7f};
        const auto submit=[&]{renderer.renderGame(scene);tick();};
        for(unsigned index=0;index<60;++index)submit();
        constexpr unsigned Samples=240;const std::uint32_t first=frame+1,last=first+Samples-1;
        std::set<std::uint32_t> observed;std::vector<double> gpu;
        unsigned minimumDraws=UINT32_MAX,maximumDraws{};
        const auto collect=[&]{const auto* stats=bgfx::getStats();
            if(stats->gpuFrameNum<first||stats->gpuFrameNum>last||stats->gpuTimerFreq<=0||
                stats->gpuTimeEnd<=stats->gpuTimeBegin||!observed.insert(stats->gpuFrameNum).second)return;
            gpu.push_back(double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000./stats->gpuTimerFreq);
        };
        // Render-only cadence: no texture readback or explicit GPU fence in
        // timed frames. tick only pumps publication/streaming and bgfx frame.
        for(unsigned index=0;index<Samples;++index){submit();collect();const auto draws=bgfx::getStats()->numDraw;
            minimumDraws=std::min(minimumDraws,draws);maximumDraws=std::max(maximumDraws,draws);}
        for(unsigned index=0;index<16;++index){submit();collect();}
        std::sort(gpu.begin(),gpu.end());const auto percentile=[&](double fraction){
            if(gpu.empty())return 0.;const double position=(gpu.size()-1)*fraction;const auto lower=static_cast<std::size_t>(position);
            return gpu[lower]+(gpu[std::min(lower+1,gpu.size()-1)]-gpu[lower])*(position-lower);
        };
        std::cout<<"closed-room-profile width="<<ProfileWidth<<" height="<<ProfileHeight<<" warmup_frames=60 measured_frames="<<Samples<<" first_measured_gpu_frame="<<first
            <<" last_measured_gpu_frame="<<last<<" unique_gpu_samples="<<gpu.size()<<" gpu_p50_ms="<<percentile(.5)
            <<" gpu_p95_ms="<<percentile(.95)<<" draws_min="<<minimumDraws<<" draws_max="<<maximumDraws<<std::endl;
        require(gpu.size()>=Samples/2,"Closed-room GPU profile did not publish enough unique in-window queries.");
        require(renderer.shadowStatus().empty(),renderer.shadowStatus());
        renderer.resizeGameViewport(Size,Size);
    }
    renderer.setDebugView(RenderViewPurpose::Scene,previousScene);renderer.setDebugView(RenderViewPurpose::Game,previousGame);
}
template<class Read> void inclinedLocalSelfShadowCheck(Renderer& renderer,const Read& read,const fs::path& capture){
    auto mesh=floorGeometry();mesh->id="directional-inclined-local-self-caster";
    const auto rotation=math::eulerRotationXYZ({.31f,-.27f,.43f});const Vec3 center{.3f,1.8f,-.4f};
    const auto normal=math::normalized(math::transformDirection(rotation,{0,1,0}));
    const auto tangent=math::normalized(math::transformDirection(rotation,{1,0,0}));
    const auto up=math::normalized(math::transformDirection(rotation,{0,0,-1}));mesh->bounds={};
    // Bake the non-axis plane into LOCAL vertex positions and normals. Merely
    // rotating an instance would leave its BLAS triangle/plane arithmetic axial.
    for(auto& vertex:mesh->primitives[0].vertices){
        vertex.position=math::transformPoint(rotation,vertex.position)+center;vertex.normal=normal;
        vertex.tangent={tangent[0],tangent[1],tangent[2],-1};math::include(mesh->bounds,vertex.position);
    }
    require(std::abs(normal[0])>.1&&std::abs(normal[1])>.1&&std::abs(normal[2])>.1,"Inclined local fixture became axial.");
    require(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> materials(1);materials[0].baseColor={1,1,1,1};materials[0].roughness=1;
    materials[0].metallic=materials[0].specular=0;materials[0].doubleSided=true;
    std::vector<ModelMaterialTextures> bindings(1);RenderModelInstance instance;instance.entityId=861;
    instance.model.asset=mesh;instance.materials=&materials;instance.textures=&bindings;instance.forceFullDetail=true;
    SceneSettings settings;settings.environment.enabled=false;settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.fog.enabled=settings.environment.glow.enabled=settings.environment.ssgi.enabled=false;
    settings.environment.ssao.enabled=settings.environment.ssr.enabled=false;settings.antiAliasing=AntiAliasing::Off;settings.exposure=1;
    settings.camera.position=center+normal*6+tangent*2.5+up*.8;settings.camera.target=center;settings.camera.up=up;
    settings.camera.fov=75;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.0006994245f;settings.camera.farPlane=2048;
    DirectionalLight sun;sun.entityId=862;sun.color={1,1,1};sun.intensity=1;sun.shadowDistance=100;
    sun.shadowNormalBias=sun.shadowDepthBias=sun.shadowAngularDiameter=0;
    sun.direction=math::normalized(normal*.45+tangent*.85+up*.25)*-1;
    require(math::dot(normal,sun.direction*-1)>.4,"Inclined local fixture has insufficient direct reference lighting.");
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&instance,1);scene.shadowInstances=scene.instances;
    scene.directionalLights=std::span(&sun,1);scene.shadowWorldId=863;scene.shadowResolution=1024;
    std::vector<Vec3> points;for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x)points.push_back(center+tangent*(x*.5)+up*(y*.5));
    for(bool game:{false,true}){
        const auto purpose=game?RenderViewPurpose::Game:RenderViewPurpose::Scene;const auto previous=renderer.debugView(purpose);
        renderer.setDebugView(purpose,RenderDebugView::DirectLighting);
        const auto label=std::string("inclined-local-self-")+(game?"game":"scene");
        const auto render=[&](bool shadows){sun.castShadows=shadows;if(game)renderer.renderGame(scene);else renderer.render(scene);
            // Streaming/frame advancement during asynchronous readback resets
            // per-submission counters. Prove admission before that drain.
            if(shadows)require(renderer.shadowStats().rayInstances==1&&renderer.shadowStats().rayTriangles==2,
                label+": the original local self-caster was not admitted to exact geometry queries.");
            require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());};
        const auto clear=render(false),shadowed=render(true);
        save(clear,capture,label+"-reference");save(shadowed,capture,label+"-shadowed");
        litSamples(clear,shadowed,settings.camera,points,label,36);renderer.setDebugView(purpose,previous);
    }
}
// Keep the broad PCF contract on a CSM-only identity-pose
// control; check contact visibility against independently unprojected finite
// receiving-pixel rays. No production toggle or BVH reference is introduced.
struct ContactRayD { std::array<double,3> origin{},direction{}; };
struct ContactCoverage { double value{},lower{},upper{}; };
std::array<ContactRayD,32> contactPixelRays(const Camera& camera,unsigned x,unsigned y,
    Vec3 receiver,Vec3 receiverNormal,Vec3 towardLight,double angularDiameter){
    std::array<ContactRayD,32> result;
    const auto lightRight=math::normalized(math::cross(towardLight,std::abs(towardLight[1])<.99?Vec3{0,1,0}:Vec3{1,0,0}));
    const auto lightUp=math::cross(lightRight,towardLight);
    const double tangent=std::tan(angularDiameter*std::numbers::pi/360);
    for(unsigned index=0;index<result.size();++index){
        // The public quadrature has one Latinized sample in each 8x4 cell.
        // Visibility itself comes from independent finite analytic OBB slabs,
        // without calling the production BVH, triangle or plane helpers.
        const unsigned ix=index&7,iy=index>>3;
        const double sx=(ix+(iy+.5)/4)/8-.5,sy=(iy+(ix+.5)/8)/4-.5;
        const auto ray=camera.screenRay(float(x+.5+sx),float(y+.5+sy),{0,0,Size,Size});
        require(bool(ray),"Independent contact subpixel has no camera ray.");
        const auto point=math::intersectPlane(*ray,receiver,receiverNormal);
        require(bool(point),"Independent contact subpixel misses the finite receiver plane.");
        const double radius=std::sqrt((index+.5)/32)*tangent,angle=index*2.399963230;
        const auto direction=math::normalized(towardLight+lightRight*(std::cos(angle)*radius)+lightUp*(std::sin(angle)*radius));
        for(unsigned axis=0;axis<3;++axis){result[index].origin[axis]=(*point)[axis];result[index].direction[axis]=direction[axis];}
    }
    return result;
}
ContactCoverage finiteBoxCoverage(std::span<const ContactRayD> rays,std::span<const Mat4> boxes,double maximum=1e100){
    std::vector<Mat4> inverses;for(const auto& box:boxes){const auto inverse=math::inverse(box);require(bool(inverse),"Contact reference OBB is singular.");inverses.push_back(*inverse);}
    unsigned hits{},expandedHits{},contractedHits{};
    for(const auto& ray:rays){bool hit{},expanded{},contracted{};
        for(const auto& inverse:inverses){
            std::array<double,3> origin{},direction{};
            for(unsigned row=0;row<3;++row){origin[row]=inverse.at(row,3);
                for(unsigned column=0;column<3;++column){origin[row]+=double(inverse.at(row,column))*ray.origin[column];direction[row]+=double(inverse.at(row,column))*ray.direction[column];}}
            double scale=1;for(double value:origin)scale=std::max(scale,std::abs(value));
            // Bound a few float affine/unprojection ULPs, in local units. Only
            // an actual finite-box boundary receives this uncertainty interval.
            const double error=32*std::numeric_limits<float>::epsilon()*scale;
            const auto intersects=[&](double margin){double nearDistance=-1e100,farDistance=1e100;
                for(unsigned axis=0;axis<3;++axis){
                    if(std::abs(direction[axis])<1e-30){if(std::abs(origin[axis])>.5+margin)return false;continue;}
                    const double a=(-.5-margin-origin[axis])/direction[axis],b=(.5+margin-origin[axis])/direction[axis];
                    nearDistance=std::max(nearDistance,std::min(a,b));farDistance=std::min(farDistance,std::max(a,b));if(nearDistance>farDistance)return false;
                }
                // A closed outward-wound box presents its exit face to the
                // receiver-to-sun ray (N.direction > 0), matching sun culling.
                return farDistance>1e-8&&nearDistance<maximum;
            };
            hit|=intersects(0);expanded|=intersects(error);contracted|=intersects(-error);
        }
        hits+=hit;expandedHits+=expanded;contractedHits+=contracted;
    }
    const double count=double(rays.size());require(count>0,"Finite coverage reference has no rays.");
    return {1-hits/count,1-expandedHits/count,1-contractedHits/count};
}
std::array<double,2> contactLinearInterval(const DecodedImage& image,unsigned offset){
    double low{},high{};for(unsigned channel=0;channel<3;++channel){const unsigned value=image.rgba[offset+channel];
        low+=linearByte(value?value-1:0);high+=linearByte(std::min(255u,value+1));}
    return {low/3,high/3};
}
template<class Read> void contactEdgeFiltering(Renderer& renderer,const std::shared_ptr<const ModelAsset>& floor,
    const std::shared_ptr<const ModelAsset>& box,const std::vector<ModelMaterial>& materials,
    const std::vector<ModelMaterialTextures>& textures,const Read& read,const fs::path& capture){
    SceneSettings settings;settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.exposure=1;settings.antiAliasing=AntiAliasing::Off;
    settings.environment.ambientLight.source=AmbientLightSource::Disabled;settings.environment.fog.enabled=settings.environment.glow.enabled=false;
    settings.environment.ssgi.enabled=settings.environment.ssao.enabled=settings.environment.ssr.enabled=false;
    settings.camera.position={0,4,0};settings.camera.target={0,0,0};settings.camera.up={0,0,-1};
    settings.camera.fov=45;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.05f;
    DirectionalLight sun;sun.entityId=701;sun.direction={0,-1,0};sun.intensity=1;sun.castShadows=true;
    sun.shadowDistance=20;sun.shadowDepthBias=0;sun.shadowNormalBias=0;
    RenderModelInstance receiver;receiver.entityId=702;receiver.model.asset=floor;
    receiver.materials=&materials;receiver.textures=&textures;receiver.forceFullDetail=true;
    auto blocker=receiver;blocker.entityId=703;blocker.model.asset=box;std::array casters{receiver,blocker};
    const auto floorPose=std::make_shared<ModelPose>(evaluateModelPose(*floor)),boxPose=std::make_shared<ModelPose>(evaluateModelPose(*box));
    require(floorPose->drawTransforms.size()==floor->draws.size()&&boxPose->drawTransforms.size()==box->draws.size(),"CSM control has different draw count.");
    for(unsigned draw=0;draw<floor->draws.size();++draw)require(floorPose->drawTransforms[draw]==floor->draws[draw].transform,"CSM control changed a receiver draw transform.");
    for(unsigned draw=0;draw<box->draws.size();++draw)require(boxPose->drawTransforms[draw]==box->draws[draw].transform,"CSM control changed a caster draw transform.");
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&receiver,1);
    scene.shadowInstances=casters;scene.directionalLights=std::span(&sun,1);scene.shadowWorldId=704;
    constexpr unsigned SampleX=Size/2,SampleY=Size/2,StepsPerTexel=16,Steps=8*StepsPerTexel,PhysicalSteps=64;
    constexpr unsigned sampleOffset=(SampleY*Size+SampleX)*4;
    const auto ray=settings.camera.screenRay(SampleX+.5f,SampleY+.5f,{0,0,Size,Size});require(bool(ray),"Contact edge pixel ray is invalid.");
    const auto sample=math::intersectPlane(*ray,{0,0,0},{0,1,0});require(bool(sample),"Contact edge pixel misses its receiver.");
    constexpr double angle=17*std::numbers::pi/180;const double cosine=std::cos(angle),sine=std::sin(angle);const Vec3 normal{cosine,0,-sine};
    std::array<double,6> phases{};for(unsigned row=0;row<phases.size();++row){const double intercept=row*std::tan(angle);phases[row]=intercept-std::floor(intercept);}
    std::sort(phases.begin(),phases.end());double minimumPhaseGap=1+phases.front()-phases.back();
    for(unsigned row=1;row<phases.size();++row)minimumPhaseGap=std::min(minimumPhaseGap,phases[row]-phases[row-1]);
    require(1./(StepsPerTexel*cosine)+2*(.6/256)<minimumPhaseGap,"Contact baseline step can change more than one filter texel.");
    double pixelLower=1e100,pixelUpper=-1e100;
    for(unsigned corner=0;corner<4;++corner){const auto cornerRay=settings.camera.screenRay(float(SampleX+(corner&1)),float(SampleY+((corner>>1)&1)),{0,0,Size,Size});
        require(bool(cornerRay),"Contact pixel corner has no ray.");const auto point=math::intersectPlane(*cornerRay,{0,0,0},{0,1,0});require(bool(point),"Contact pixel corner misses floor.");
        const double distance=math::dot(*point-*sample,normal);pixelLower=std::min(pixelLower,distance);pixelUpper=std::max(pixelUpper,distance);}
    const auto render=[&](bool baseline){receiver.pose=baseline?floorPose:nullptr;casters[0].pose=baseline?floorPose:nullptr;casters[1].pose=baseline?boxPose:nullptr;
        renderer.render(scene);require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(renderer.viewportTexture());};
    for(const unsigned resolution:{256u,1024u})for(const float diameter:{0.f,.53f}){
        scene.shadowResolution=static_cast<std::uint16_t>(resolution);sun.shadowAngularDiameter=diameter;
        const auto fit=directional_shadow::build(sun,{settings.camera,1},resolution);require(fit.valid,"Contact edge cascade fit failed.");
        const auto depth=math::dot(settings.camera.forward(),*sample-settings.camera.eye());unsigned cascade{};
        while(cascade+1<directional_shadow::CascadeCount&&depth>=fit.splits[cascade])++cascade;
        const auto& projection=fit.values[cascade];require(depth<projection.splitFar-(projection.splitFar-projection.splitNear)*.1,"Contact fixture entered cascade overlap.");
        const double texel=projection.texelWorld;
        const auto pose=[&](double edge){const auto center=*sample+normal*(edge-8*texel);
            casters[1].model.transform=math::translation({center[0],.3*texel,center[2]})*math::rotation(math::axisAngle({0,1,0},angle))*math::scaling({16*texel,.1*texel,16*texel});};
        pose(-4*texel);receiver.receiveShadows=false;const auto clear=render(false),controlClear=render(true);receiver.receiveShadows=true;
        require(clear.rgba==controlClear.rgba,"Identity-pose CSM control changed shadow-off pixels.");
        const double lit=linearBrightness(clear,sampleOffset);require(lit>.05&&lit<.9,"Contact edge reference is dark or clipped.");
        const auto encoded=*std::max_element(clear.rgba.begin()+sampleOffset,clear.rgba.begin()+sampleOffset+3);
        const double quantization=(linearByte(std::min(255u,unsigned(encoded)+1))-linearByte(encoded))/lit;
        const double maximumPcfStep=1./9+2*quantization;double previous{},first{},last{},observedMaximum{};std::set<unsigned> partialLevels;
        const auto label="contact-edge-"+std::to_string(resolution)+"-angle"+std::to_string(diameter);
        // Retain the original world-PCF proof unchanged on identical geometry
        // excluded from the optional contact path by an explicit bind pose.
        for(unsigned step=0;step<=Steps;++step){pose((-4.+double(step)/StepsPerTexel)*texel);const auto image=render(true);
            const double visibility=linearBrightness(image,sampleOffset)/lit;
            if(!step)first=visibility;else observedMaximum=std::max(observedMaximum,std::abs(visibility-previous));
            if(visibility>.05&&visibility<.95)partialLevels.insert(image.rgba[sampleOffset]);previous=last=visibility;}
        std::cout<<label<<" baseline_maximum_step="<<observedMaximum<<" weighted_PCF_bound="<<maximumPcfStep<<" baseline_partial_levels="<<partialLevels.size()<<std::endl;
        require(first>.95&&last<.05,label+": broad CSM sweep did not bracket controls.");require(partialLevels.size()>=4,label+": baseline PCF became binary.");
        require(observedMaximum<=maximumPcfStep,label+": CSM baseline discarded weighted PCF filtering.");
        const auto rays=contactPixelRays(settings.camera,SampleX,SampleY,*sample,{0,1,0},{0,1,0},diameter);
        const auto clearInterval=contactLinearInterval(clear,sampleOffset);
        std::set<unsigned> physicalLevels;ContactCoverage previousCoverage;double previousBaseline{},previousRefined{},maximumMinError{},maximumPhysicalStep{};
        for(unsigned step=0;step<=PhysicalSteps;++step){pose(pixelLower+(pixelUpper-pixelLower)*step/PhysicalSteps);
            const std::array boxes{casters[1].model.transform};const auto coverage=finiteBoxCoverage(rays,boxes);
            const auto baseline=render(true),refined=render(false);const auto baselineInterval=contactLinearInterval(baseline,sampleOffset),refinedInterval=contactLinearInterval(refined,sampleOffset);
            const double b=linearBrightness(baseline,sampleOffset)/lit,v=linearBrightness(refined,sampleOffset)/lit;
            // CSM and exact rays see the same blocker. Take the smaller
            // visibility; multiplying would count its occlusion twice.
            const double expectedLower=std::min(baselineInterval[0],clearInterval[0]*coverage.lower);
            const double expectedUpper=std::min(baselineInterval[1],clearInterval[1]*coverage.upper);
            require(refinedInterval[0]<=expectedUpper+1e-7&&refinedInterval[1]>=expectedLower-1e-7,label+": contact disagrees with captured CSM / independent finite-box visibility minimum.");
            maximumMinError=std::max(maximumMinError,std::abs(v-std::min(b,coverage.value)));
            if(v>.05&&v<.95)physicalLevels.insert(refined.rgba[sampleOffset]);
            if(step){const double coverageDelta=std::max(std::abs(coverage.upper-previousCoverage.lower),std::abs(coverage.lower-previousCoverage.upper));
                // Each move spans 1/64 of one physical receiver-pixel footprint.
                // Use its independently measured finite coverage change rather
                // than importing a shadow-map texel movement bound.
                const double bound=std::abs(b-previousBaseline)+coverageDelta+4*quantization;
                maximumPhysicalStep=std::max(maximumPhysicalStep,std::abs(v-previousRefined));
                require(std::abs(v-previousRefined)<=bound,label+": contact coverage exceeded its physical subpixel movement bound.");}
            else require(coverage.lower==1&&v>.1,label+": physical lit endpoint is invalid.");
            if(step==PhysicalSteps)require(coverage.upper==0&&v<.02,label+": physical occluded endpoint is invalid.");
            previousCoverage=coverage;previousBaseline=b;previousRefined=v;
            if(step==0||step==PhysicalSteps/2||step==PhysicalSteps)save(refined,capture,label+"-physical-step"+std::to_string(step));
        }
        std::cout<<label<<" physical_maximum_step="<<maximumPhysicalStep<<" physical_partial_levels="<<physicalLevels.size()
            <<" min_max_error="<<maximumMinError<<std::endl;
        require(physicalLevels.size()>=4,label+": real pixel-footprint contact coverage became binary.");
    }
    // Four finite roof pieces surround a real opening. The fifth piece is an
    // independent closed control; no extrapolated plane may seal the open gap.
    sun.shadowAngularDiameter=0;scene.shadowResolution=1024;
    const auto fit=directional_shadow::build(sun,{settings.camera,1},1024);require(fit.valid,"Light-opening fit failed.");
    unsigned cascade{};const auto depth=math::dot(settings.camera.forward(),*sample-settings.camera.eye());
    while(cascade+1<directional_shadow::CascadeCount&&depth>=fit.splits[cascade])++cascade;
    const double texel=fit.values[cascade].texelWorld,gap=std::max(1.75*texel,1.5*(pixelUpper-pixelLower)),outer=32*texel,half=gap*.5;
    std::array<RenderModelInstance,6> opening;opening[0]=receiver;
    for(unsigned index=1;index<opening.size();++index){opening[index]=blocker;opening[index].entityId=730+index;}
    std::array<Mat4,5> windowBoxes;
    const auto windowBox=[&](double x,double z,double width,double length){return math::translation(*sample)*math::rotation(math::axisAngle({0,1,0},angle))*
        math::translation({x,.3*texel,z})*math::scaling({width,.1*texel,length});};
    windowBoxes[0]=windowBox(-(outer+half)*.5,0,outer-half,2*outer);windowBoxes[1]=windowBox((outer+half)*.5,0,outer-half,2*outer);
    windowBoxes[2]=windowBox(0,-(outer+half)*.5,gap,outer-half);windowBoxes[3]=windowBox(0,(outer+half)*.5,gap,outer-half);windowBoxes[4]=windowBox(0,0,gap,gap);
    for(unsigned index=0;index<windowBoxes.size();++index)opening[index+1].model.transform=windowBoxes[index];
    const auto rays=contactPixelRays(settings.camera,SampleX,SampleY,*sample,{0,1,0},{0,1,0},0);
    const auto openCoverage=finiteBoxCoverage(rays,std::span(windowBoxes).first(4)),closedCoverage=finiteBoxCoverage(rays,windowBoxes);
    require(openCoverage.lower==1&&closedCoverage.upper==0,"Finite roof opening reference did not distinguish real light gap and closed control.");
    const auto windowRender=[&](bool baseline,bool closed){receiver.pose=baseline?floorPose:nullptr;scene.shadowInstances=std::span(opening).first(closed?6:5);
        for(unsigned index=0;index<opening.size();++index)opening[index].pose=baseline?(index?boxPose:floorPose):nullptr;
        renderer.render(scene);require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(renderer.viewportTexture());};
    receiver.receiveShadows=false;const auto clear=windowRender(false,false),controlClear=windowRender(true,false);receiver.receiveShadows=true;
    require(clear.rgba==controlClear.rgba,"Opening CSM control changed shadow-off pixels.");
    const double lit=linearBrightness(clear,sampleOffset);const auto baseline=windowRender(true,false),open=windowRender(false,false),closed=windowRender(false,true);
    const auto baselineInterval=contactLinearInterval(baseline,sampleOffset),openInterval=contactLinearInterval(open,sampleOffset);
    require(linearBrightness(baseline,sampleOffset)/lit>.03,"True light opening was unresolved by the baseline shadow map.");
    require(openInterval[0]<=baselineInterval[1]&&openInterval[1]>=baselineInterval[0],"Contact refinement sealed or dimmed a fully visible finite light opening.");
    require(linearBrightness(closed,sampleOffset)/lit<.02,"Opaque finite opening-fill control leaked direct sunlight.");
    std::cout<<"finite-light-opening width_m="<<gap<<" baseline_visibility="<<linearBrightness(baseline,sampleOffset)/lit
        <<" refined_open_visibility="<<linearBrightness(open,sampleOffset)/lit<<" closed_visibility="<<linearBrightness(closed,sampleOffset)/lit<<std::endl;
    save(open,capture,"finite-light-opening-open");save(closed,capture,"finite-light-opening-closed");

    // A visible finite corner covers some subpixels of the floor plane while
    // leaving the actual center sample on the lit floor. Hidden floor samples
    // must be removed from both halves of contact's visibility fraction.
    // Its tiny map footprint keeps CSM sufficiently bright to expose a biased
    // denominator; a large half-plane caster would mask that error through MIN.
    sun.shadowDistance=500;scene.shadowResolution=256;
    const auto cornerFit=directional_shadow::build(sun,{settings.camera,1},256);require(cornerFit.valid,"Visible-corner fit failed.");
    unsigned cornerCascade{};while(cornerCascade+1<directional_shadow::CascadeCount&&depth>=cornerFit.splits[cornerCascade])++cornerCascade;
    const double footprint=pixelUpper-pixelLower,cornerSize=1.5*footprint;
    require(cornerSize*2<cornerFit.values[cornerCascade].texelWorld,"Visible-corner caster is not confined to one raw shadow texel per axis.");
    double pixelRadius{};for(unsigned corner=0;corner<4;++corner){
        const auto cameraRay=settings.camera.screenRay(float(SampleX+(corner&1)),float(SampleY+((corner>>1)&1)),{0,0,Size,Size});
        require(bool(cameraRay),"Visible-corner pixel corner has no camera ray.");const auto point=math::intersectPlane(*cameraRay,*sample,{0,1,0});
        require(bool(point),"Visible-corner pixel corner misses its floor.");pixelRadius=std::max(pixelRadius,double(math::length(*point-*sample)));}
    const Vec3 tangent{sine,0,cosine};const auto cornerCenter=*sample+normal*(-.05*footprint-cornerSize*.5)+tangent*(.2*footprint-cornerSize*.5);
    auto visibleBlocker=blocker;visibleBlocker.entityId=760;
    visibleBlocker.model.transform=math::translation({cornerCenter[0],.2*pixelRadius,cornerCenter[2]})*
        math::rotation(math::axisAngle({0,1,0},angle))*math::scaling({cornerSize,.2*pixelRadius,cornerSize});
    auto redMaterials=materials;redMaterials[0].baseColor={1,.02f,.02f,1};visibleBlocker.materials=&redMaterials;
    std::array visible{receiver,visibleBlocker};scene.instances=scene.shadowInstances=visible;
    const std::array cornerBox{visibleBlocker.model.transform};
    const auto cornerRays=contactPixelRays(settings.camera,SampleX,SampleY,*sample,{0,1,0},{0,1,0},0);
    const auto cameraVisibility=[&](const ContactRayD& sunRay){ContactRayD cameraRay;cameraRay.origin=sunRay.origin;double distanceSquared{};
        for(unsigned axis=0;axis<3;++axis){cameraRay.direction[axis]=double(settings.camera.eye()[axis])-cameraRay.origin[axis];distanceSquared+=cameraRay.direction[axis]*cameraRay.direction[axis];}
        const double distance=std::sqrt(distanceSquared);for(auto& component:cameraRay.direction)component/=distance;
        return finiteBoxCoverage(std::span(&cameraRay,1),cornerBox,distance);};
    ContactRayD centerRay;for(unsigned axis=0;axis<3;++axis){centerRay.origin[axis]=(*sample)[axis];centerRay.direction[axis]=axis==1?1:0;}
    require(cameraVisibility(centerRay).lower==1,"Visible-corner center is not an actual floor receiver.");
    unsigned hiddenSunHits{},validLit{};
    for(const auto& sunRay:cornerRays){const auto camera=cameraVisibility(sunRay),sunlight=finiteBoxCoverage(std::span(&sunRay,1),cornerBox);
        require(camera.lower==camera.upper&&sunlight.lower==sunlight.upper,"Visible-corner analytic reference straddles an uncertain finite boundary.");
        if(camera.value==0){hiddenSunHits+=sunlight.value==0;}else{require(sunlight.value==1,"A valid visible floor stratum is not sunlit.");++validLit;}}
    require(hiddenSunHits>=6&&validLit>=6,"Visible-corner reference did not exercise hidden sun hits and visible lit floor strata.");
    const auto cornerRender=[&](bool csm,bool shadows){visible[0].pose=csm?floorPose:nullptr;visible[1].pose=csm?boxPose:nullptr;
        visible[0].receiveShadows=visible[1].receiveShadows=shadows;renderer.render(scene);require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(renderer.viewportTexture());};
    const auto cornerClear=cornerRender(false,false),cornerControlClear=cornerRender(true,false);
    require(cornerClear.rgba==cornerControlClear.rgba,"Visible-corner posed control changed shadow-off pixels.");
    scene.instances=std::span(visible).subspan(1,1);renderer.render(scene);const auto cornerBoxOnly=read(renderer.viewportTexture());
    scene.instances=std::span(visible).first(1);renderer.render(scene);const auto cornerFloorOnly=read(renderer.viewportTexture());scene.instances=visible;
    Vec2 projectedMinimum{float(Size),float(Size)},projectedMaximum{};
    for(const float x:{-.5f,.5f})for(const float y:{-.5f,.5f})for(const float z:{-.5f,.5f}){
        const auto world=math::transformPoint(visibleBlocker.model.transform,{x,y,z});
        const auto projected=math::project(world,settings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*settings.camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
        require(bool(projected),"Visible-corner projection has no finite bounds.");for(unsigned axis=0;axis<2;++axis){projectedMinimum[axis]=std::min(projectedMinimum[axis],(*projected)[axis]);projectedMaximum[axis]=std::max(projectedMaximum[axis],(*projected)[axis]);}}
    unsigned predictedPixels{},boxPixels{},coverageMismatches{};
    const auto pixelHitsBox=[&](float x,float y){const auto ray=settings.camera.screenRay(x,y,{0,0,Size,Size});require(bool(ray),"Finite corner coverage has no camera ray.");
        ContactRayD reference;for(unsigned axis=0;axis<3;++axis){reference.origin[axis]=ray->origin[axis];reference.direction[axis]=ray->direction[axis];}
        return finiteBoxCoverage(std::span(&reference,1),cornerBox,settings.camera.farPlane).value==0;};
    for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x){const auto offset=(y*Size+x)*4;
        const bool actual=brightness(cornerBoxOnly,offset)>4;boxPixels+=actual;
        if(x+1<projectedMinimum[0]||x>projectedMaximum[0]+1||y+1<projectedMinimum[1]||y>projectedMaximum[1]+1)continue;
        const bool expected=pixelHitsBox(x+.5f,y+.5f);predictedPixels+=expected;bool stable=true;
        for(const float ox:{-.005f,.005f})for(const float oy:{-.005f,.005f})stable&=pixelHitsBox(x+.5f+ox,y+.5f+oy)==expected;
        coverageMismatches+=stable&&actual!=expected;
    }
    unsigned redPixels{};for(unsigned offset=0;offset<cornerClear.rgba.size();offset+=4)redPixels+=cornerClear.rgba[offset]>cornerClear.rgba[offset+1]+20;
    unsigned centerFloorDifference{};for(unsigned channel=0;channel<3;++channel)centerFloorDifference=std::max(centerFloorDifference,
        unsigned(std::abs(int(cornerClear.rgba[sampleOffset+channel])-int(cornerFloorOnly.rgba[sampleOffset+channel]))));
    std::cout<<"visible-finite-corner-precondition projected_bounds="<<projectedMinimum[0]<<","<<projectedMinimum[1]<<".."<<projectedMaximum[0]<<","<<projectedMaximum[1]
        <<" predicted_pixels="<<predictedPixels<<" box_only_pixels="<<boxPixels<<" stable_coverage_mismatches="<<coverageMismatches<<" red_pixels="<<redPixels
        <<" center_combined_rgb="<<unsigned(cornerClear.rgba[sampleOffset])<<","<<unsigned(cornerClear.rgba[sampleOffset+1])<<","<<unsigned(cornerClear.rgba[sampleOffset+2])
        <<" center_floor_rgb="<<unsigned(cornerFloorOnly.rgba[sampleOffset])<<","<<unsigned(cornerFloorOnly.rgba[sampleOffset+1])<<","<<unsigned(cornerFloorOnly.rgba[sampleOffset+2])
        <<" center_box_rgb="<<unsigned(cornerBoxOnly.rgba[sampleOffset])<<","<<unsigned(cornerBoxOnly.rgba[sampleOffset+1])<<","<<unsigned(cornerBoxOnly.rgba[sampleOffset+2])
        <<" debug_mode="<<int(renderer.debugView(RenderViewPurpose::Scene))<<" scene_instances="<<scene.instances.size()<<" model_status="<<renderer.modelStatus()<<std::endl;
    if(!predictedPixels||!boxPixels||coverageMismatches||brightness(cornerBoxOnly,sampleOffset)>4||centerFloorDifference>1){
        const auto evidence=capture.empty()?fs::path(__FILE__).parent_path().parent_path()/"build/diagnostics/light-leaks"/(std::string("contact-corner-failure-")+renderer.backendName()):capture;
        save(cornerClear,evidence,"corner-clear");save(cornerControlClear,evidence,"corner-control-clear");save(cornerBoxOnly,evidence,"corner-box-only");save(cornerFloorOnly,evidence,"corner-floor-only");
        throw std::runtime_error("Finite corner raster coverage disagrees with its independent projected box/floor oracle; evidence="+evidence.string());
    }
    const auto cornerBaseline=cornerRender(true,true),cornerRefined=cornerRender(false,true);
    const double cornerLit=linearBrightness(cornerClear,sampleOffset),baselineVisibility=linearBrightness(cornerBaseline,sampleOffset)/cornerLit;
    require(cornerLit>.05&&baselineVisibility>1-hiddenSunHits/32.+.04,"Visible-corner CSM control cannot distinguish a biased contact denominator.");
    const auto expected=contactLinearInterval(cornerBaseline,sampleOffset),actual=contactLinearInterval(cornerRefined,sampleOffset);
    require(actual[0]<=expected[1]&&actual[1]>=expected[0],"Hidden sun-hit strata darkened an independently sunlit finite receiver corner.");
    std::cout<<"visible-finite-corner hidden_sun_hits="<<hiddenSunHits<<" valid_lit="<<validLit<<" visible_red_pixels="<<redPixels
        <<" csm_visibility="<<baselineVisibility<<" refined_visibility="<<linearBrightness(cornerRefined,sampleOffset)/cornerLit<<std::endl;
    save(cornerClear,capture,"visible-finite-corner-clear");save(cornerBaseline,capture,"visible-finite-corner-csm");save(cornerRefined,capture,"visible-finite-corner-refined");
}

// A full-screen curved receiver exposed the angular packet regression. The
// dense inclined plane adds capture-sized paint-style geometry at a grazing
// camera angle. Only bounded production/private shaders run on that fixture;
// the formerly unbounded packet is never used as a performance control.
template<class Read> void contactPerformanceChecks(Renderer& renderer,const ShaderCompileConfig& config,
    const Read& read,std::uint32_t& frame,const fs::path& capture){
    Temporary reference;
    const std::array current{
        RendererShaderPair{RendererProgram::ShadowContactTrace,{},compileShader(config,"cs_shadow_contact_trace").bytes},
        RendererShaderPair{RendererProgram::ShadowContactPublish,{},compileShader(config,"cs_shadow_contact_publish").bytes}};
    const auto privatePrograms=[&](const char* name,const char* definitions){
        const auto sources=reference.path/name/"sources";fs::create_directories(sources.parent_path());
        fs::copy(config.sourceDirectory,sources,fs::copy_options::recursive);
        const auto entry=sources/"cs_shadow_contact_trace.sc";
        std::string text;{std::ifstream input(entry,std::ios::binary);require(bool(input),"Contact reference source is missing.");text.assign(std::istreambuf_iterator<char>(input),{});}
        {std::ofstream output(entry,std::ios::binary|std::ios::trunc);output<<definitions<<text;require(bool(output),"Cannot prepare private contact shader.");}
        ShaderCompileConfig privateConfig{config.compiler,sources,config.includeDirectory,reference.path/name/"cache"};
        return std::array{RendererShaderPair{RendererProgram::ShadowContactTrace,{},compileShader(privateConfig,"cs_shadow_contact_trace").bytes},current[1]};
    };
    // These overrides exist only in private test compilations. Ample allowance
    // is used on the tiny floor/box oracle, never on dense capture geometry.
    const auto independent=privatePrograms("independent","#define CC_SHADOW_SOURCE_SINGLE_RAY_REFERENCE 1\n");
    const auto amplePacket=privatePrograms("ample-packet","#define CC_SHADOW_CONTACT_WORK_LIMIT_OVERRIDE 65536u\n#define CC_SHADOW_CONTACT_PACKET_WORK_LIMIT_OVERRIDE 65536u\n");
    const auto ampleFallback=privatePrograms("ample-fallback","#define CC_SHADOW_CONTACT_WORK_LIMIT_OVERRIDE 65536u\n#define CC_SHADOW_CONTACT_PACKET_WORK_LIMIT_OVERRIDE 1u\n");
    const auto ampleIndependent=privatePrograms("ample-independent","#define CC_SHADOW_CONTACT_WORK_LIMIT_OVERRIDE 65536u\n#define CC_SHADOW_SOURCE_SINGLE_RAY_REFERENCE 1\n");
    const auto exhausted=privatePrograms("exhausted","#define CC_SHADOW_CONTACT_WORK_LIMIT_OVERRIDE 64u\n#define CC_SHADOW_CONTACT_PACKET_WORK_LIMIT_OVERRIDE 1u\n");
    const auto sphere=builtinSphereGeometry(),box=builtinBoxGeometry();
    require(renderer.prepareModel(*sphere)&&renderer.prepareModel(*box),renderer.modelStatus());
    std::vector<ModelMaterial> white(1);white[0].baseColor={1,1,1,1};white[0].roughness=1;white[0].metallic=white[0].specular=0;
    std::vector<ModelMaterialTextures> textures(1);std::array<RenderModelInstance,3> instances;
    for(unsigned i=0;i<instances.size();++i){auto& instance=instances[i];instance.entityId=910+i;
        instance.model.asset=i==0?sphere:box;instance.materials=&white;instance.textures=&textures;instance.forceFullDetail=true;}
    instances[0].model.transform=math::translation({0,1.5417464f,1.9092153f});
    instances[1].model.transform=math::translation({0,.99876356f,0});
    SceneSettings settings;settings.environment.enabled=false;settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.fog.enabled=settings.environment.glow.enabled=settings.environment.ssgi.enabled=false;
    settings.environment.ssao.enabled=settings.environment.ssr.enabled=false;settings.antiAliasing=AntiAliasing::Off;settings.exposure=1;
    settings.camera.position={-.48f,1.60f,2.45f};settings.camera.target={0,1.5417464f,1.9092153f};
    settings.camera.fov=45;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.01f;
    const auto closeCamera=settings.camera;
    DirectionalLight sun;sun.entityId=920;sun.direction={-.3145876f,-.7120456f,.6277145f};sun.color={1,1,1};sun.intensity=2;
    sun.castShadows=true;sun.shadowAngularDiameter=.53f;sun.shadowDistance=100;sun.shadowDepthBias=.005f;sun.shadowNormalBias=1;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=instances;scene.shadowInstances=instances;
    scene.directionalLights=std::span(&sun,1);scene.shadowWorldId=921;scene.shadowResolution=1024;
    bgfx::setDebug(BGFX_DEBUG_PROFILER);
    const auto contactView=renderViewPassSlots(RenderViewPurpose::Game).lightingEffects;
    require(contactView!=InvalidRenderView,"Contact performance fixture has no native compute view.");
    std::uint16_t width=Size,height=Size;
    bool checkDenseAdmission{};unsigned denseSubmissions{};std::uint32_t reportedDenseSize{};
    const auto submit=[&]{
        renderer.renderGame(scene);
        // These are per-submission counters. Asynchronous readback pumps
        // advanceStreaming(), which clears them before the image returns.
        if(checkDenseAdmission&&++denseSubmissions>12){
            const auto& stats=renderer.shadowStats();
            require(stats.rayInstances==1&&stats.rayTriangles==192200,
                "Dense contact owner admitted instances="+std::to_string(stats.rayInstances)+" triangles="+std::to_string(stats.rayTriangles));
            const auto sizeKey=(std::uint32_t(width)<<16)|height;
            if(reportedDenseSize!=sizeKey){reportedDenseSize=sizeKey;std::cout<<"contact-dense-admission size="<<width<<"x"<<height
                <<" instances="<<stats.rayInstances<<" triangles="<<stats.rayTriangles<<std::endl;}
        }
        bgfx::touch(ToolsView);frame=renderer.advance_frame();
    };
    const auto measureGpu=[&](const auto& programs,const std::string& label,unsigned warmup=24,unsigned samples=24){
        require(renderer.reloadPrograms(programs),renderer.shaderStatus());
        for(unsigned i=0;i<warmup;++i)submit();
        require(renderer.shadowStatus().empty(),renderer.shadowStatus());
        const auto first=frame+1,last=first+samples-1;
        std::set<std::uint32_t> frames;std::set<std::pair<std::int64_t,std::int64_t>> intervals;std::vector<double> values;
        const auto compilerBefore=shaderCompilerMetrics();
        const auto collect=[&]{const auto* stats=bgfx::getStats();if(stats->gpuTimerFreq<=0)return;
            for(unsigned i=0;i<stats->numViews;++i){const auto& view=stats->viewStats[i];
                if(view.view!=contactView||view.gpuFrameNum<first||view.gpuFrameNum>last||view.gpuTimeEnd<=view.gpuTimeBegin||
                    !frames.insert(view.gpuFrameNum).second||!intervals.emplace(view.gpuTimeBegin,view.gpuTimeEnd).second)continue;
                values.push_back(double(view.gpuTimeEnd-view.gpuTimeBegin)*1000./stats->gpuTimerFreq);}
        };
        // No readback, explicit fence, compiler, or CPU-duration assertion in
        // the measured window. The native contact trace+publish view is alone:
        // GI/AO and unrelated compute effects are disabled above.
        for(unsigned i=0;i<samples;++i){submit();collect();}
        for(unsigned i=0;i<16;++i){submit();collect();}
        require(values.size()>=samples/2,"Too few distinct native contact GPU queries.");
        require(shaderCompilerMetrics().compilerInvocations==compilerBefore.compilerInvocations,"Compilation entered contact GPU measurement.");
        std::sort(values.begin(),values.end());const auto median=(values[(values.size()-1)/2]+values[values.size()/2])*.5;
        std::cout<<"contact-performance "<<label<<" size="<<width<<"x"<<height<<" gpu_samples="<<values.size()<<" median_ms="<<median
            <<" maximum_ms="<<values.back()<<" shader_bytes="<<programs[0].fragment.size()<<std::endl;
        // Native query time includes trace and publication. This intentionally
        // loose ceiling catches the historical hundreds-of-ms dispatches while
        // allowing slower adapters and timing noise.
        require(median<100.,label+": contact native GPU median exceeded 100 ms.");
        require(values.back()<250.,label+": a contact native GPU interval exceeded 250 ms.");
        return median;
    };
    const auto measure=[&](const auto& programs,const char* label){
        settings.camera=closeCamera;const auto median=measureGpu(programs,label);
        const auto close=read(renderer.gameViewportTexture());
        settings.camera.position={-1.25f,2.f,3.65f};settings.camera.target={0,1.54f,1.91f};
        for(unsigned i=0;i<8;++i)submit();const auto edge=read(renderer.gameViewportTexture());
        save(close,capture,std::string("contact-performance-")+label+"-close");save(edge,capture,std::string("contact-performance-")+label+"-edges");
        return std::tuple{median,close,edge};
    };
    const auto [currentMs,currentClose,currentEdges]=measure(current,"production");
    const auto [referenceMs,referenceClose,referenceEdges]=measure(independent,"reference");
    require(currentClose.rgba==referenceClose.rgba&&currentEdges.rgba==referenceEdges.rgba,
        "Contact traversal changed deterministic 32-ray image coverage.");
    unsigned lit{},dark{};for(unsigned i=0;i<referenceEdges.rgba.size();i+=4){lit+=brightness(referenceEdges,i)>30;dark+=brightness(referenceEdges,i)<5;}
    require(lit>=100&&dark>=100,"Contact image parity fixture lost its lit cap and shadow/silhouette coverage.");
    require(currentMs<=referenceMs*3.+2.,"Angular contact traversal exceeded the independent-ray GPU budget: production="+
        std::to_string(currentMs)+"ms, reference="+std::to_string(referenceMs)+"ms.");

    const auto floor=floorGeometry();require(renderer.prepareModel(*floor),renderer.modelStatus());
    settings.camera.position={0,4,0};settings.camera.target={0,0,0};settings.camera.up={0,0,-1};settings.camera.nearPlane=.05f;
    settings.camera.fov=45;sun.direction={0,-1,0};sun.intensity=1;sun.shadowAngularDiameter=0;sun.shadowDistance=20;
    sun.shadowDepthBias=sun.shadowNormalBias=0;scene.shadowResolution=256;
    RenderModelInstance receiver;receiver.entityId=930;receiver.model.asset=floor;receiver.materials=&white;receiver.textures=&textures;receiver.forceFullDetail=true;
    auto blocker=receiver;blocker.entityId=931;blocker.model.asset=box;
    blocker.model.transform=math::translation({.06f,.03f,0})*math::eulerRotationXYZ({0,.29670597f,0})*math::scaling({.55f,.01f,.55f});
    std::array finiteCasters{receiver,blocker};scene.instances=std::span(&receiver,1);scene.shadowInstances=finiteCasters;
    const auto floorPose=std::make_shared<ModelPose>(evaluateModelPose(*floor)),boxPose=std::make_shared<ModelPose>(evaluateModelPose(*box));
    const auto finiteImage=[&](const auto& programs,bool csmOnly=false){
        require(renderer.reloadPrograms(programs),renderer.shaderStatus());
        receiver.pose=csmOnly?floorPose:nullptr;finiteCasters[0].pose=receiver.pose;finiteCasters[1].pose=csmOnly?boxPose:nullptr;
        for(unsigned i=0;i<6;++i)submit();require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(renderer.gameViewportTexture(),width,height);
    };
    const auto csm=finiteImage(current,true),packet=finiteImage(amplePacket),fallback=finiteImage(ampleFallback),exact=finiteImage(ampleIndependent),limited=finiteImage(exhausted);
    require(packet.rgba==exact.rgba,"Ample-budget hard packet changed deterministic finite coverage.");
    require(fallback.rgba==exact.rgba,"Exhausted hard packet failed to restart exact independent-ray refinement.");
    require(limited.rgba==csm.rgba,"Total-work exhaustion published partial contact coverage instead of the CSM baseline.");
    unsigned refinedPixels{};for(unsigned i=0;i<exact.rgba.size();i+=4)refinedPixels+=exact.rgba[i]!=csm.rgba[i];
    require(refinedPixels>=10,"Finite work-exhaustion fixture lost observable contact refinement.");
    std::cout<<"contact-work-fallback refined_pixels="<<refinedPixels<<" packet_limit=1 total_limit=64 csm_exact=1"<<std::endl;
    save(csm,capture,"contact-work-csm");save(exact,capture,"contact-work-exact");save(limited,capture,"contact-work-exhausted");

    // A capture-sized viewport receives a smaller per-pixel allowance. Compare
    // its useful refinement against both CSM alone and completed finite rays;
    // ordered results prove fallback cannot introduce extra dark coverage.
    width=914;height=600;renderer.resizeGameViewport(width,height);
    const auto nativeCsm=finiteImage(current,true),nativeExact=finiteImage(ampleIndependent),nativeProduction=finiteImage(current);
    unsigned nativeExactPixels{},nativeCorrectedPixels{},nativeOrderedViolations{};
    for(unsigned i=0;i<nativeCsm.rgba.size();i+=4){
        nativeExactPixels+=int(nativeExact.rgba[i])+1<int(nativeCsm.rgba[i]);
        nativeCorrectedPixels+=int(nativeProduction.rgba[i])+1<int(nativeCsm.rgba[i]);
        for(unsigned channel=0;channel<3;++channel)
            nativeOrderedViolations+=int(nativeProduction.rgba[i+channel])>int(nativeCsm.rgba[i+channel])+1||
                int(nativeProduction.rgba[i+channel])+1<int(nativeExact.rgba[i+channel]);
    }
    std::cout<<"contact-native-quality size="<<width<<"x"<<height<<" pixels="<<unsigned(width)*height
        <<" ample_corrected="<<nativeExactPixels<<" production_corrected="<<nativeCorrectedPixels
        <<" ordered_violations="<<nativeOrderedViolations<<std::endl;
    require(nativeExactPixels>=10,"Capture-sized finite fixture lost completed contact refinement.");
    require(nativeCorrectedPixels>=100 && nativeCorrectedPixels>=nativeExactPixels/8,
        "Capture-sized production allowance removed useful finite contact refinement.");
    require(nativeOrderedViolations==0,"Budgeted native contacts escaped the CSM/completed-refinement visibility bounds.");
    // Exercise both triangles of the box's flat face. Counting corrected pixels
    // alone allowed one triangle to sharpen while its neighbor stayed blurred.
    unsigned nativeInteriorCorrections{};
    constexpr std::array<Vec2,6> facePoints{{{-.40f,-.30f},{.40f,.30f},{-.30f,.40f},{.30f,-.40f},{-.40f,-.40f},{.40f,.40f}}};
    for(const auto local:facePoints){
        auto world=math::transformPoint(blocker.model.transform,{local[0],0,local[1]});world[1]=0;
        const auto projected=math::project(world,settings.camera.projectionMatrix(float(width)/height,ClipDepth::ZeroToOne)*
            settings.camera.viewMatrix(),{0,0,float(width),float(height)},ClipDepth::ZeroToOne);
        require(projected&&(*projected)[0]>=0&&(*projected)[1]>=0&&(*projected)[0]<width&&(*projected)[1]<height,
            "Native hard-shadow face probe left its viewport.");
        const auto offset=(std::size_t((*projected)[1])*width+std::size_t((*projected)[0]))*4;
        require(nativeExact.rgba[offset]<=1&&nativeProduction.rgba[offset]<=1,
            "Native hard contact failed at box-face point "+std::to_string(local[0])+","+std::to_string(local[1])+
            ": exact="+std::to_string(nativeExact.rgba[offset])+" production="+std::to_string(nativeProduction.rgba[offset]));
        nativeInteriorCorrections+=nativeCsm.rgba[offset]>1;
    }
    require(nativeInteriorCorrections>=4,"Native hard-shadow face probes lost their observable CSM correction.");
    std::cout<<"contact-native-face completed_probes="<<facePoints.size()<<" corrected_probes="<<nativeInteriorCorrections<<std::endl;
    save(nativeCsm,capture,"contact-native-csm");save(nativeExact,capture,"contact-native-ample");save(nativeProduction,capture,"contact-native-production");
    measureGpu(current,"finite-hard-capture-size",8,8);
    width=height=Size;renderer.resizeGameViewport(Size,Size);

    constexpr unsigned Cells=310;
    auto dense=std::make_shared<ModelAsset>();dense->id="directional-contact-dense-painted-plane";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;primitive.vertices.reserve((Cells+1)*(Cells+1));
    primitive.indices.reserve(Cells*Cells*6);primitive.sourceFaces.reserve(Cells*Cells);
    const auto rotation=math::eulerRotationXYZ({.31f,-.27f,.43f});
    const auto normal=math::normalized(math::transformDirection(rotation,{0,1,0})),tangent=math::normalized(math::transformDirection(rotation,{1,0,0}));
    for(unsigned z=0;z<=Cells;++z)for(unsigned x=0;x<=Cells;++x){
        const Vec2 uv{float(x)/Cells,float(z)/Cells};ModelVertex vertex;
        vertex.position=math::transformPoint(rotation,{uv[0]*36-18,0,18-uv[1]*36});vertex.normal=normal;
        vertex.tangent={tangent[0],tangent[1],tangent[2],-1};vertex.uv0=vertex.uv1=uv;
        primitive.vertices.push_back(vertex);math::include(dense->bounds,vertex.position);
    }
    for(unsigned z=0;z<Cells;++z)for(unsigned x=0;x<Cells;++x){
        const auto a=z*(Cells+1)+x,b=a+1,c=a+Cells+2,d=a+Cells+1,triangle=static_cast<std::uint32_t>(primitive.indices.size()/3);
        primitive.indices.insert(primitive.indices.end(),{a,b,c,a,c,d});primitive.sourceFaces.push_back({{a,b,c,d},{triangle,triangle+1},4});
    }
    require(primitive.indices.size()/3==192200,"Dense contact fixture lost its capture-sized triangle population.");
    dense->primitives.push_back(std::move(primitive));dense->draws.push_back({0,{}});dense->materials.emplace_back();
    require(renderer.prepareModel(*dense),renderer.modelStatus());
    RenderModelInstance denseReceiver=receiver;denseReceiver.entityId=940;denseReceiver.pose.reset();denseReceiver.model.asset=dense;
    scene.instances=scene.shadowInstances=std::span(&denseReceiver,1);scene.shadowWorldId=941;scene.shadowResolution=1024;
    settings.camera.position=math::transformPoint(rotation,{.08f,.025f,1});settings.camera.target=math::transformPoint(rotation,{0,0,-.5f});
    settings.camera.up=normal;settings.camera.nearPlane=.0006994245f;settings.camera.fov=75;
    sun.direction=math::normalized(math::transformDirection(rotation,{-.85f,-.45f,.25f}));sun.intensity=2;sun.shadowDistance=100;
    sun.shadowAngularDiameter=0;sun.shadowDepthBias=.005f;sun.shadowNormalBias=1;
    checkDenseAdmission=true;
    const auto denseMs=measureGpu(current,"dense-hard-production",12,16);const auto denseImage=read(renderer.gameViewportTexture());
    const auto denseReferenceMs=measureGpu(independent,"dense-hard-independent",12,16);const auto denseReference=read(renderer.gameViewportTexture());
    require(denseImage.rgba==denseReference.rgba,"Bounded dense hard-source result disagrees with the same-budget independent control.");
    require(denseMs<=denseReferenceMs*3.+2.,"Dense hard packet exceeded its bounded independent-ray timing control.");
    unsigned denseLit{};for(unsigned i=0;i<denseImage.rgba.size();i+=4)denseLit+=brightness(denseImage,i)>30;
    require(denseLit>=100,"Dense grazing fixture lost its visible receiving plane.");save(denseImage,capture,"contact-dense-hard");
    width=914;height=600;renderer.resizeGameViewport(width,height);
    measureGpu(current,"dense-hard-capture-size",8,8);save(read(renderer.gameViewportTexture(),width,height),capture,"contact-dense-hard-capture-size");
    renderer.resizeGameViewport(Size,Size);
    require(renderer.reloadPrograms(current),renderer.shaderStatus());bgfx::setDebug(BGFX_DEBUG_NONE);
}

void exercise(SDL_Window* window,ShaderCompileConfig config,const fs::path& capture,bool closedRoomOnly,bool compiledModel,bool profileClosedRoom,bool contactOnly,bool contactPerformanceOnly){
    // Readback storage survives exceptional renderer shutdown.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));MaterialGraphPrograms programs(resources,config);
    renderer.resizeViewport(Size,Size);renderer.resizeGameViewport(Size,Size);renderer.setGridVisible(false);
    bgfx::reset(Size,Size,BGFX_RESET_NONE);TestGpuContext context;
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x4453485155;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.1f,.1f})},environment.settings);
    require(renderer.publishEnvironment(environment),renderer.environmentStatus());
    if(compiledModel){
        const RendererShaderPair pair{RendererProgram::Model,compileShader(config,"vs_model").bytes,compileShader(config,"fs_model").bytes};
        require(renderer.reloadPrograms(std::span(&pair,1)),renderer.shaderStatus());
        std::cout<<"compiled-model shader_status="<<(renderer.shaderStatus().empty()?"ok":renderer.shaderStatus())
            <<" shader_sources="<<config.sourceDirectory.string()<<std::endl;
    }
    std::uint32_t frame{};const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle source,std::uint16_t width=Size,std::uint16_t height=Size){
        Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        require(bgfx::isValid(staging.handle),"Directional quality readback allocation failed.");
        auto image=std::make_shared<DecodedImage>(DecodedImage{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)});destinations.push_back(image);
        bgfx::blit(ReadView,staging.handle,0,0,source,0,0,width,height);const auto ready=bgfx::readTexture(staging.handle,image->rgba.data());
        require(ready!=UINT32_MAX,"Directional quality readback is unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *image;}
        throw std::runtime_error("Directional quality readback timed out.");
    };
    if(contactPerformanceOnly){contactPerformanceChecks(renderer,config,read,frame,capture);resources.stop();renderer.drainPending(frame);return;}
    if(contactOnly){const auto floor=floorGeometry();const auto box=builtinBoxGeometry();
        require(renderer.prepareModel(*floor)&&renderer.prepareModel(*box),renderer.modelStatus());
        std::vector<ModelMaterial> white(1);white[0].baseColor={1,1,1,1};white[0].roughness=1;white[0].metallic=white[0].specular=0;
        std::vector<ModelMaterialTextures> bindings(1);contactEdgeFiltering(renderer,floor,box,white,bindings,read,capture);
        resources.stop();renderer.drainPending(frame);return;}
    closedRoomLeakChecks(renderer,read,tick,frame,capture,profileClosedRoom,!compiledModel);
    if(closedRoomOnly){resources.stop();renderer.drainPending(frame);return;}
    inclinedLocalSelfShadowCheck(renderer,read,capture);
    const auto floor=floorGeometry();const auto pillar=builtinBoxGeometry();
    require(renderer.prepareModel(*floor)&&renderer.prepareModel(*pillar),renderer.modelStatus());
    std::vector<ModelMaterial> white(1);white[0].baseColor={1,1,1,1};white[0].roughness=1;white[0].metallic=0;white[0].specular=0;
    std::vector<ModelMaterialTextures> bindings(1);
    RenderModelInstance ground;ground.entityId=1;ground.model.asset=floor;ground.materials=&white;ground.textures=&bindings;ground.forceFullDetail=true;
    auto column=ground;column.entityId=2;column.model.asset=pillar;column.model.transform=math::translation({1,2,-3})*math::scaling({1,4,1});
    auto wall=ground;wall.entityId=3;
    wall.model.transform={0,-1,0,0,1,0,0,0,0,0,1,0,1.5f,2,-3,1};
    std::array<RenderModelInstance,2> instances;
    DirectionalLight sun;sun.entityId=100;sun.color={1,1,1};sun.intensity=2;sun.castShadows=true;
    sun.shadowDistance=100;sun.shadowDepthBias=.005f;
    SceneSettings settings;settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.exposure=1;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.directionalLights=std::span(&sun,1);scene.shadowWorldId=42;
    std::vector<Vec3> floorPoints;
    for(float z=-12;z<=8;z+=.3f)for(float x=-10;x<=10;x+=.3f)floorPoints.push_back({x,0,z});
    // A broad isolated wall tests self-shadowing without mixing real PCF
    // coverage from the small pillar's adjoining floor and corner silhouettes
    // into the lit-interior acne oracle. Its sample region is far from edges.
    std::vector<Vec3> wallPoints;
    for(float y=-3;y<=7;y+=.3f)for(float z=-9;z<=3;z+=.3f)wallPoints.push_back({1.5f,y,z});
    const auto render=[&](bool game,bool includePillar,bool receiving){
        ground.receiveShadows=column.receiveShadows=receiving;instances={ground,column};scene.instances=std::span(instances).first(includePillar?2:1);scene.shadowInstances=scene.instances;
        if(game)renderer.renderGame(scene);else renderer.render(scene);
        require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());
    };
    const auto renderWall=[&](bool game,bool receiving){
        wall.receiveShadows=receiving;scene.instances=std::span(&wall,1);scene.shadowInstances=scene.instances;
        if(game)renderer.renderGame(scene);else renderer.render(scene);
        require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());
    };
    const std::array<Vec3,3> directions{{{-.354550123f,-.611820817f,-.707084596f},{-.7f,-.2f,-.6f},{-.55f,-.75f,-.4f}}};
    for(bool generated:{false,true}){
        if(generated){
            MaterialAsset material;addMaterialNode(material.graph,MaterialNodeKind::Output);auto& output=materialOutputNode(material.graph).output;
            output.baseColor={1,1,1};output.roughness=1;output.metallic=0;output.specular=0;
            const auto shader=generateMaterialGraphShader(material);MaterialGraphProgramResult result;
            const auto started=std::chrono::steady_clock::now(),deadline=started+std::chrono::seconds{360};
            auto nextProgress=started;const auto initial=resources.stats();const auto compilerBefore=shaderCompilerMetrics();
            bool optimizedReady{};
            do{tick();result=programs.request(shader);const auto state=resources.stats();
                require(state.failures==initial.failures,"Directional quality shader compilation failed: "+result.diagnostic);
                require(result.compiling||result.diagnostic.empty(),"Directional quality shader publication failed: "+result.diagnostic);
                optimizedReady=result.current&&result.programs&&result.programs->optimized&&!state.queued&&!state.running;
                const auto now=std::chrono::steady_clock::now();
                if(now>=nextProgress){const auto metrics=shaderCompilerMetrics();
                    std::cout<<"directional-generated elapsed_s="<<std::chrono::duration_cast<std::chrono::seconds>(now-started).count()
                        <<" compiler_processes="<<metrics.compilerInvocations-compilerBefore.compilerInvocations
                        <<" queued="<<state.queued<<" running="<<state.running<<" failures="<<state.failures-initial.failures
                        <<" current="<<result.current<<" optimized="<<(result.programs&&result.programs->optimized)<<std::endl;
                    nextProgress=now+std::chrono::seconds{5};}
                if(!optimizedReady)std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }while(!optimizedReady&&std::chrono::steady_clock::now()<deadline);
            require(optimizedReady,"Directional quality generated material did not reach optimized idle publication within 360s: "+result.diagnostic);
            auto graph=std::make_shared<PreparedGraphMaterial>();graph->programs=result.programs;graph->uniforms=shader.uniforms;graph->sources=shader.textures;graph->signature=result.programs->signature;bindings[0].graph=std::move(graph);
        }
        for(const unsigned resolution:{256u,1024u})for(const float normalBias:{1.f,0.f})for(unsigned direction=0;direction<directions.size();++direction)for(bool game:{false,true}){
            settings.camera=game?gameCamera():sceneCamera();sun.direction=math::normalized(directions[direction]);sun.shadowNormalBias=normalBias;scene.shadowResolution=static_cast<std::uint16_t>(resolution);
            const auto label=std::string(generated?"graph-":"fallback-")+(game?"game-":"scene-")+std::to_string(resolution)+"-bias"+std::to_string(int(normalBias))+"-sun"+std::to_string(direction);
            const auto floorClear=render(game,false,false),floorShadow=render(game,false,true);
            save(floorClear,capture,label+"-floor-reference");save(floorShadow,capture,label+"-floor-shadowed");
            litSamples(floorClear,floorShadow,settings.camera,floorPoints,label+" floor",100);
            const auto wallClear=renderWall(game,false),wallShadow=renderWall(game,true);
            save(wallClear,capture,label+"-wall-reference");save(wallShadow,capture,label+"-wall-shadowed");
            litSamples(wallClear,wallShadow,settings.camera,wallPoints,label+" wall",100);
        }
    }
    // A real near-contact caster must stay occluding after acne correction.
    bindings[0].graph.reset();sun.direction=math::normalized(Vec3{1,-2,.3f});
    for(const unsigned resolution:{256u,1024u})for(const float normalBias:{1.f,0.f})for(bool game:{false,true}){
        scene.shadowResolution=static_cast<std::uint16_t>(resolution);sun.shadowNormalBias=normalBias;settings.camera=game?gameCamera():sceneCamera();
        const auto clear=render(game,true,false),shadow=render(game,true,true);unsigned visible{},occluded{};
        for(float z=-3.2f;z<=-2.8f;z+=.1f)if(const auto offset=pixel(settings.camera,{1.65f,0,z})){
            const auto reference=brightness(clear,*offset);if(reference<30)continue;++visible;
            if(brightness(shadow,*offset)<reference*.75)++occluded;
        }
        require(visible>=3&&occluded>=3,"Directional receiver correction detached the near-contact pillar shadow.");
    }
    contactEdgeFiltering(renderer,floor,pillar,white,bindings,read,capture);
    // A suspended blocker isolates angular softness from the anti-aliasing
    // footprint. Its ground shadow stays in view while the caster is offscreen.
    settings.camera.position={3,6,3};settings.camera.target={1,0,-3};settings.camera.fov=45;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.05f;
    scene.shadowResolution=1024;sun.direction={0,-1,0};sun.shadowNormalBias=0;
    const auto partialWidth=[&](const DecodedImage& clear,const DecodedImage& image,double dark){
        std::set<unsigned> samples;unsigned partial{};
        for(float x=-.5f;x<=2.5f;x+=.01f)if(const auto offset=pixel(settings.camera,{x,0,-3}))samples.insert(*offset);
        for(const auto offset:samples){const auto span=brightness(clear,offset)-dark;if(span<30)continue;
            const auto visibility=(brightness(image,offset)-dark)/span;
            partial+=visibility>.15&&visibility<.85;}
        return partial;
    };
    for(bool game:{false,true}){
        column.model.transform=math::translation({1,12,-3})*math::scaling({1,.1f,1});
        sun.shadowAngularDiameter=0;const auto clear=render(game,true,false),hard=render(game,true,true);
        const auto center=pixel(settings.camera,{1,0,-3});require(bool(center),"Soft-shadow ground profile is outside its camera.");
        const auto dark=brightness(hard,*center);
        require(dark<brightness(clear,*center)*.75,"An offscreen suspended caster lost its hard ground shadow.");
        sun.shadowAngularDiameter=.53f;const auto solar=render(game,true,true);
        sun.shadowAngularDiameter=2;const auto softFar=render(game,true,true);
        column.model.transform=math::translation({1,3,-3})*math::scaling({1,.1f,1});
        const auto softNear=render(game,true,true);
        const auto hardWidth=partialWidth(clear,hard,dark),solarWidth=partialWidth(clear,solar,dark);
        const auto farWidth=partialWidth(clear,softFar,dark),nearWidth=partialWidth(clear,softNear,dark);
        std::cout<<(game?"Game":"Scene")<<" PCSS profile widths hard="<<hardWidth<<" solar="<<solarWidth
            <<" near="<<nearWidth<<" far="<<farWidth<<std::endl;
        require(farWidth>hardWidth+1 && farWidth>nearWidth,
            "Directional PCSS does not widen the penumbra with source angle and caster separation.");
        require(solarWidth+1>=hardWidth,"Default solar diameter sharpened the deterministic contact filter.");
        const auto name=std::string(game?"game":"scene")+"-angular-profile";
        save(hard,capture,name+"-hard");save(solar,capture,name+"-solar");save(softNear,capture,name+"-near");save(softFar,capture,name+"-far");
        // The sparse search must retain a thin beam's umbra. PCF fallback is
        // essential where no search disk point lands on the sub-texel edge.
        column.model.transform=math::translation({1,4,-3})*math::scaling({4,.08f,.04f});
        sun.shadowAngularDiameter=.53f;const auto thin=render(game,true,true);
        unsigned visible{},occluded{};
        for(float x=-.5f;x<=2.5f;x+=.1f)if(const auto offset=pixel(settings.camera,{x,0,-3})){
            const auto reference=brightness(clear,*offset);if(reference<30)continue;++visible;
            occluded+=brightness(thin,*offset)<reference*.95;
        }
        require(visible>=20&&occluded>=visible/2,"Sparse PCSS blocker search erased a thin offscreen caster.");
        save(thin,capture,name+"-thin");
    }
    bindings[0].graph.reset();resources.stop();renderer.drainPending(frame);
}
}
int main(int argc,char** argv){try{
    require(argc>=4,"Pass shaderc, source, include directories; optionally --closed-room-only, --contact-only, --contact-performance-only, --compiled-model, --profile-closed-room and --capture output-directory.");
    bool closedRoomOnly{},compiledModel{},profileClosedRoom{},contactOnly{},contactPerformanceOnly{};fs::path capture;
    for(int index=4;index<argc;++index){const std::string argument=argv[index];
        if(argument=="--closed-room-only")closedRoomOnly=true;
        else if(argument=="--contact-only")contactOnly=true;
        else if(argument=="--contact-performance-only")contactPerformanceOnly=true;
        else if(argument=="--compiled-model")compiledModel=true;
        else if(argument=="--profile-closed-room")profileClosedRoom=true;
        else if(argument=="--capture"&&index+1<argc)capture=argv[++index];
        else throw std::runtime_error("Unknown directional quality argument: "+argument);
    }
    Temporary temporary;ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary.path};Sdl sdl;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson directional shadow quality checks",Size,Size,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    require(!(contactOnly&&(closedRoomOnly||profileClosedRoom)),"--contact-only cannot be combined with closed-room diagnostics.");
    require(!(contactPerformanceOnly&&(contactOnly||closedRoomOnly||profileClosedRoom||compiledModel)),"--contact-performance-only is an isolated GPU comparison.");
    require(bool(window),SDL_GetError());exercise(window.get(),config,capture,closedRoomOnly,compiledModel,profileClosedRoom,contactOnly,contactPerformanceOnly);
    std::cout<<(contactPerformanceOnly?"Directional contact native GPU budget and exact 32-ray image parity passed.\n":contactOnly?"Directional physical contact edge, finite opening and visible-corner checks passed.\n":closedRoomOnly?"Directional closed inward-normal double-sided room checks passed.\n":
        "Directional closed-room leaks, Scene/Game oblique/grazing floor/wall acne, generated/fallback materials, contact edge filtering and near-contact checks passed.\n");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
