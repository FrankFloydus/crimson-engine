#include "crimson-renderer/Renderer.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/DirectionalShadowMath.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Picking.h"
#include "assets/BuiltinGeometry.h"
#include "resources/ResourceManager.h"
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
#include <numbers>
#include <set>
#include <stdexcept>
#include <thread>

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
template<class Read> void contactEdgeFiltering(Renderer& renderer,const std::shared_ptr<const ModelAsset>& floor,
    const std::shared_ptr<const ModelAsset>& box,const std::vector<ModelMaterial>& materials,
    const std::vector<ModelMaterialTextures>& textures,const Read& read,const fs::path& capture){
    SceneSettings settings;settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.exposure=1;
    settings.camera.position={0,4,0};settings.camera.target={0,0,0};settings.camera.up={0,0,-1};
    settings.camera.fov=45;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.05f;
    DirectionalLight sun;sun.entityId=701;sun.direction={0,-1,0};sun.intensity=1;sun.castShadows=true;
    sun.shadowDistance=20;sun.shadowDepthBias=0;sun.shadowNormalBias=0;
    RenderModelInstance receiver;receiver.entityId=702;receiver.model.asset=floor;
    receiver.materials=&materials;receiver.textures=&textures;receiver.forceFullDetail=true;
    auto blocker=receiver;blocker.entityId=703;blocker.model.asset=box;
    std::array casters{receiver,blocker};
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&receiver,1);
    scene.shadowInstances=casters;scene.directionalLights=std::span(&sun,1);scene.shadowWorldId=704;
    constexpr unsigned SampleX=Size/2,SampleY=Size/2,StepsPerTexel=16,Steps=8*StepsPerTexel;
    constexpr unsigned sampleOffset=(SampleY*Size+SampleX)*4;
    const auto ray=settings.camera.screenRay(SampleX+.5f,SampleY+.5f,{0,0,Size,Size});
    require(bool(ray),"Contact edge pixel ray is invalid.");
    const auto sample=math::intersectPlane(*ray,{0,0,0},{0,1,0});
    require(bool(sample),"Contact edge pixel misses its receiver.");
    // A 17-degree edge has six row-intercept phases fract(row*tan(17deg)).
    // Their minimum circular spacing exceeds a 1/16-texel normal move divided
    // by cos(17deg), plus both D3D n.8 vertex-conversion uncertainties. Thus at
    // most ONE texel in the 6x6 bilinear footprint changes coverage per step.
    constexpr double angle=17*std::numbers::pi/180;
    const double cosine=std::cos(angle),sine=std::sin(angle);
    std::array<double,6> phases{};
    for(unsigned row=0;row<phases.size();++row){const double intercept=row*std::tan(angle);phases[row]=intercept-std::floor(intercept);}
    std::sort(phases.begin(),phases.end());double minimumPhaseGap=1+phases.front()-phases.back();
    for(unsigned row=1;row<phases.size();++row)minimumPhaseGap=std::min(minimumPhaseGap,phases[row]-phases[row-1]);
    require(1./(StepsPerTexel*cosine)+2*(.6/256)<minimumPhaseGap,"Contact edge step can change more than one filter texel.");
    const Vec3 normal{cosine,0,-sine};
    for(const unsigned resolution:{256u,1024u})for(const float diameter:{0.f,.53f}){
        scene.shadowResolution=static_cast<std::uint16_t>(resolution);sun.shadowAngularDiameter=diameter;
        const auto fit=directional_shadow::build(sun,{settings.camera,1},resolution);
        require(fit.valid,"Contact edge cascade fit failed.");
        const auto depth=math::dot(settings.camera.forward(),*sample-settings.camera.eye());
        unsigned cascade{};while(cascade+1<directional_shadow::CascadeCount&&depth>=fit.splits[cascade])++cascade;
        const auto& projection=fit.values[cascade];
        require(depth<projection.splitFar-(projection.splitFar-projection.splitNear)*.1,
            "Contact edge fixture unexpectedly entered a cascade overlap.");
        const double texel=projection.texelWorld;
        const auto pose=[&](double shift){
            const auto center=*sample+normal*((shift-8)*texel);
            casters[1].model.transform=math::translation({center[0],.3*texel,center[2]})*
                math::rotation(math::axisAngle({0,1,0},angle))*math::scaling({16*texel,.1*texel,16*texel});
        };
        const auto render=[&]{renderer.render(scene);require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(renderer.viewportTexture());};
        pose(-4);receiver.receiveShadows=false;const auto clear=render();receiver.receiveShadows=true;
        const double lit=linearBrightness(clear,sampleOffset);
        require(lit>.05&&lit<.9,"Contact edge reference is dark or display-clipped.");
        // The separable [1,3,4,3,1]/12 PCF kernel followed by bilinear sampling
        // never assigns one raw depth texel more than (4/12)^2 = 1/9 weight.
        // Add only the two RGBA8 readback quantization intervals to that bound.
        const auto encoded=*std::max_element(clear.rgba.begin()+sampleOffset,clear.rgba.begin()+sampleOffset+3);
        const double quantization=(linearByte(std::min(255u,unsigned(encoded)+1))-linearByte(encoded))/lit;
        const double maximumStep=1./9+2*quantization;
        double previous{},first{},last{},observedMaximum{};std::set<unsigned> partialLevels;
        const auto label="contact-edge-"+std::to_string(resolution)+"-angle"+std::to_string(diameter);
        for(unsigned step=0;step<=Steps;++step){
            pose(-4.+double(step)/StepsPerTexel);const auto image=render();
            const double visibility=linearBrightness(image,sampleOffset)/lit;
            if(!step)first=visibility;else observedMaximum=std::max(observedMaximum,std::abs(visibility-previous));
            if(visibility>.05&&visibility<.95)partialLevels.insert(image.rgba[sampleOffset]);
            previous=last=visibility;
            if(step==0||step==Steps/2||step==Steps)save(image,capture,label+"-step"+std::to_string(step));
        }
        std::cout<<label<<" maximum_step="<<observedMaximum<<" weighted_PCF_bound="<<maximumStep
            <<" partial_levels="<<partialLevels.size()<<std::endl;
        require(first>.95&&last<.05,label+": contact edge sweep did not bracket lit and shadowed controls.");
        require(partialLevels.size()>=4,label+": contact filtering collapsed to binary shadow-map coverage.");
        require(observedMaximum<=maximumStep,label+": contact edge discarded the minimum weighted PCF anti-aliasing footprint.");
    }
}
void exercise(SDL_Window* window,ShaderCompileConfig config,const fs::path& capture){
    // Readback storage survives exceptional renderer shutdown.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));MaterialGraphPrograms programs(resources,config);
    renderer.resizeViewport(Size,Size);renderer.resizeGameViewport(Size,Size);renderer.setGridVisible(false);
    bgfx::reset(Size,Size,BGFX_RESET_NONE);TestGpuContext context;
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x4453485155;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.1f,.1f})},environment.settings);
    require(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint32_t frame{};const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle source){
        Texture staging{bgfx::createTexture2D(Size,Size,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        require(bgfx::isValid(staging.handle),"Directional quality readback allocation failed.");
        auto image=std::make_shared<DecodedImage>(DecodedImage{Size,Size,std::vector<std::uint8_t>(Size*Size*4)});destinations.push_back(image);
        bgfx::blit(ReadView,staging.handle,0,0,source,0,0,Size,Size);const auto ready=bgfx::readTexture(staging.handle,image->rgba.data());
        require(ready!=UINT32_MAX,"Directional quality readback is unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *image;}
        throw std::runtime_error("Directional quality readback timed out.");
    };
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
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{45};
            do{tick();result=programs.request(shader);if(!result.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});}
            while(!result.current&&std::chrono::steady_clock::now()<deadline);
            require(result.current&&bool(result.programs),"Directional quality generated material failed: "+result.diagnostic);
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
    require(argc==4||(argc==6&&std::string(argv[4])=="--capture"),"Pass shaderc, source, include directories; optionally --capture output-directory.");
    Temporary temporary;ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary.path};Sdl sdl;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson directional shadow quality checks",Size,Size,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    require(bool(window),SDL_GetError());exercise(window.get(),config,argc==6?fs::path(argv[5]):fs::path{});
    std::cout<<"Directional Scene/Game oblique/grazing floor/wall acne, generated/fallback materials, contact edge filtering and near-contact checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
