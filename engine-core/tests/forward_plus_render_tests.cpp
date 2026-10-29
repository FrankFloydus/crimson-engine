#include "crimson-renderer/Renderer.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/RenderFrame.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "resources/ShaderCompiler.h"
#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
constexpr std::uint16_t ReadView=DiagnosticReadbackView;
void check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
struct Temporary {
    fs::path path=fs::temp_directory_path()/("crimson-forward-plus-"+std::to_string(Clock::now().time_since_epoch().count()));
    Temporary(){fs::create_directories(path);}
    ~Temporary(){std::error_code error;const auto target=fs::weakly_canonical(path,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-forward-plus-"))fs::remove_all(target,error);}
};
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};

std::shared_ptr<ModelAsset> quad(){
    auto asset=std::make_shared<ModelAsset>();asset->id="forward-plus-quad";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> p{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned i=0;i<4;++i){ModelVertex v;v.position=p[i];v.normal={0,0,1};v.uv0=uv[i];v.uv1=uv[i];v.tangent={1,0,0,-1};primitive.vertices.push_back(v);math::include(asset->bounds,p[i]);}
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.baseColor={.35f,.35f,.35f,1};material.metallic=0;material.roughness=.65f;asset->materials.push_back(material);return asset;
}

std::vector<PointLight> lights(unsigned count,bool overlapping=false){
    std::vector<PointLight> result(count);
    for(unsigned i=0;i<count;++i){auto& light=result[i];light.entityId=1000+i;light.color={.35f+float(i%3)*.25f,.35f+float((i+1)%3)*.25f,.35f+float((i+2)%3)*.25f};
        light.position=overlapping?Vec3{0,0,1.5f}:Vec3{(float(i%16)-7.5f)*1.25f,(float((i/16)%16)-7.5f)*1.25f,.3f+float((i/256)%4)*.6f};
        light.radius=overlapping?12.f:2.7f;light.intensity=overlapping?14.f/float(std::max(count,1u)):3.f;}
    return result;
}

struct Difference {double mean{};unsigned maximum{},different{};};
Difference difference(const DecodedImage& a,const DecodedImage& b){
    check(a.width==b.width&&a.height==b.height&&a.rgba.size()==b.rgba.size(),"Readback dimensions changed.");Difference result;
    for(std::size_t i=0;i<a.rgba.size();++i)if(i%4!=3){const auto d=unsigned(std::abs(int(a.rgba[i])-int(b.rgba[i])));result.mean+=d;result.maximum=std::max(result.maximum,d);result.different+=d>3;}
    result.mean/=double(a.width)*a.height*3;return result;
}
void equivalent(const DecodedImage& clustered,const DecodedImage& reference,const std::string& name){
    const auto error=difference(clustered,reference);
    check(error.mean<.15&&error.maximum<=4,name+": clustered/reference image mismatch: mean="+std::to_string(error.mean)+", maximum="+std::to_string(error.maximum));
}
double energy(const DecodedImage& image){double result{};for(std::size_t i=0;i<image.rgba.size();++i)if(i%4!=3)result+=image.rgba[i];return result;}
double centerBrightness(const DecodedImage& image){double result{};for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)for(unsigned c=0;c<3;++c)
    result+=image.rgba[(std::size_t(int(image.height/2)+y)*image.width+std::size_t(int(image.width/2)+x))*4+c];return result/27;}

template<class Read>
void verifyPassScheduling(Renderer& renderer,const ShaderCompileConfig& config,const RenderScene& scene,Read&& read) {
    struct Program {bgfx::ProgramHandle handle{BGFX_INVALID_HANDLE};~Program(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
    struct Uniform {bgfx::UniformHandle handle{BGFX_INVALID_HANDLE};~Uniform(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
    struct Target {bgfx::FrameBufferHandle handle{BGFX_INVALID_HANDLE};~Target(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
    const auto compile=[&](const char* name,ShaderStage stage,const char* source) {
        ShaderCompileRecipe recipe;recipe.name=name;recipe.stage=stage;recipe.source=source;
        recipe.varyingDefinition=config.sourceDirectory/"varying.def.sc";
        recipe.includeDirectories={config.sourceDirectory};recipe.contract="pass-scheduling-identity512-v1";
        return compileShader(config,recipe);
    };
    const auto vertex=compile("vs_pass_schedule",ShaderStage::Vertex,
        "$input a_position\n#include <bgfx_shader.sh>\nvoid main(){gl_Position=vec4(a_position,1.0);}\n");
    const auto producerFragment=compile("fs_pass_schedule_producer",ShaderStage::Fragment,
        "#include <bgfx_shader.sh>\nuniform vec4 u_passScheduleSeed;\nvoid main(){gl_FragColor=u_passScheduleSeed;}\n");
    const auto consumerFragment=compile("fs_pass_schedule_consumer",ShaderStage::Fragment,
        "#include <bgfx_shader.sh>\nSAMPLER2D(s_passScheduleInput,0);\n"
        "void main(){vec3 value=texelFetch(s_passScheduleInput,ivec2(gl_FragCoord.xy),0).rgb;"
        "gl_FragColor=vec4(value.r,1.0-value.g,value.b,1.0);}\n");
    const auto compute=compile("cs_pass_schedule",ShaderStage::Compute,
        "#include <bgfx_compute.sh>\nSAMPLER2D(s_passScheduleInput,0);\nIMAGE2D_WO(s_passScheduleOutput,rgba8,1);\n"
        "NUM_THREADS(8,8,1)\nvoid main(){ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);"
        "if(any(greaterThanEqual(pixel,ivec2(2,2))))return;"
        "vec4 value=texelFetch(s_passScheduleInput,pixel,0);imageStore(s_passScheduleOutput,pixel,vec4(value.brg,1.0));}\n");
    const auto shader=[](const ShaderBinary& binary) {
        const auto handle=bgfx::createShader(bgfx::copy(binary.bytes.data(),static_cast<std::uint32_t>(binary.bytes.size())));
        check(bgfx::isValid(handle),"Scheduling fixture shader publication failed.");return handle;
    };
    Program producer{bgfx::createProgram(shader(vertex),shader(producerFragment),true)};
    Program consumer{bgfx::createProgram(shader(vertex),shader(consumerFragment),true)};
    Program transform{bgfx::createProgram(shader(compute),true)};
    check(bgfx::isValid(producer.handle)&&bgfx::isValid(consumer.handle)&&bgfx::isValid(transform.handle),"Scheduling fixture program publication failed.");
    Uniform seed{bgfx::createUniform("u_passScheduleSeed",bgfx::UniformType::Vec4)};
    Uniform input{bgfx::createUniform("s_passScheduleInput",bgfx::UniformType::Sampler)};
    check(bgfx::isValid(seed.handle)&&bgfx::isValid(input.handle),"Scheduling fixture uniform allocation failed.");
    constexpr auto sampling=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
        BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
    Texture rasterSource{bgfx::createTexture2D(2,2,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT|sampling)};
    Texture computeOutput{bgfx::createTexture2D(2,2,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_COMPUTE_WRITE|sampling)};
    Texture rasterOutput{bgfx::createTexture2D(2,2,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT|sampling)};
    check(bgfx::isValid(rasterSource.handle)&&bgfx::isValid(computeOutput.handle)&&bgfx::isValid(rasterOutput.handle),"Scheduling fixture texture allocation failed.");
    Target first{bgfx::createFrameBuffer(1,&rasterSource.handle,false)},last{bgfx::createFrameBuffer(1,&rasterOutput.handle,false)};
    check(bgfx::isValid(first.handle)&&bgfx::isValid(last.handle),"Scheduling fixture framebuffer allocation failed.");
    bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).end();
    constexpr std::array<Vec3,3> corners{{{-1,-1,0},{3,-1,0},{-1,3,0}}};
    const auto raster=[&](bgfx::ViewId view,bgfx::FrameBufferHandle target,bgfx::ProgramHandle program) {
        bgfx::setViewMode(view,bgfx::ViewMode::Sequential);bgfx::setViewFrameBuffer(view,target);
        bgfx::setViewRect(view,0,0,2,2);bgfx::setViewClear(view,BGFX_CLEAR_COLOR,0xff00ffffu);
        bgfx::setViewTransform(view,nullptr,nullptr);
        bgfx::TransientVertexBuffer vertices;
        check(bgfx::getAvailTransientVertexBuffer(3,layout)==3,"Scheduling fixture transient geometry is unavailable.");
        bgfx::allocTransientVertexBuffer(&vertices,3,layout);std::memcpy(vertices.data,corners.data(),sizeof corners);
        bgfx::setVertexBuffer(0,&vertices);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(view,program);
    };
    check(bgfx::getCaps()->limits.maxViews==RendererViewCapacity,"Scheduling fixture received the wrong compiled view capacity.");
    constexpr std::array<std::array<std::uint8_t,3>,2> seeds{{{17,93,201},{231,52,86}}};
    for(const bool game:{false,true}) {
        const auto base=game?GameSceneView:SceneView,transparent=game?GameTransparentView:SceneTransparentView;
        const auto firstView=static_cast<bgfx::ViewId>(base+1),computeView=static_cast<bgfx::ViewId>(base+2),lastView=static_cast<bgfx::ViewId>(base+3);
        check(base<firstView&&firstView<computeView&&computeView<lastView&&lastView<transparent,
            "Raster/compute/raster insertion range overlaps an implemented view.");
        if(game)check(firstView>255,"The Game scheduling witness no longer exercises the high view bits.");
        for(const auto& color:seeds) {
            // Ordinary Scene and Game work coexist with the inserted dependent
            // stages. The second seed must replace the first in this same frame.
            renderer.render(scene);renderer.renderGame(scene);
            const Vec4 value{color[0]/255.f,color[1]/255.f,color[2]/255.f,1};
            bgfx::setUniform(seed.handle,value.data());raster(firstView,first.handle,producer.handle);
            bgfx::setViewMode(computeView,bgfx::ViewMode::Sequential);bgfx::setViewFrameBuffer(computeView,BGFX_INVALID_HANDLE);
            bgfx::setViewRect(computeView,0,0,2,2);bgfx::setViewClear(computeView,BGFX_CLEAR_NONE);
            bgfx::setTexture(0,input.handle,rasterSource.handle,static_cast<std::uint32_t>(sampling));
            bgfx::setImage(1,computeOutput.handle,0,bgfx::Access::Write,bgfx::TextureFormat::RGBA8);
            bgfx::dispatch(computeView,transform.handle,1,1,1);
            bgfx::setTexture(0,input.handle,computeOutput.handle,static_cast<std::uint32_t>(sampling));raster(lastView,last.handle,consumer.handle);
            const auto image=read(rasterOutput.handle,2,2);
            const std::array<int,4> expected{color[2],255-color[0],color[1],255};
            for(std::size_t pixel=0;pixel<4;++pixel)for(unsigned channel=0;channel<4;++channel)
                check(std::abs(int(image.rgba[pixel*4+channel])-expected[channel])<=1,
                    std::string(game?"Game":"Scene")+" raster/compute/raster scheduling lost its current-frame producer at channel "+std::to_string(channel));
        }
        for(const auto view:{firstView,computeView,lastView}){bgfx::resetView(view);bgfx::setViewMode(view,bgfx::ViewMode::Sequential);}
    }
    std::cout<<"Scene/Game raster -> compute -> raster dependencies and same-frame high-view readback passed.\n";
}

void exercise(SDL_Window* window,const ShaderCompileConfig& config,const fs::path& benchmarkDirectory,bool movingCamera){
    // Readback storage must outlive Renderer, including exceptional shutdown.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));MaterialGraphPrograms programs(resources,config);
    renderer.resizeViewport(160,128);renderer.resizeGameViewport(160,128);renderer.setGridVisible(false);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    bgfx::reset(960,540,BGFX_RESET_NONE);bgfx::setDebug(BGFX_DEBUG_PROFILER);
    TestGpuContext context;
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x46504c5553;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.12f,.16f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    check(renderer.publishThumbnailEnvironment(environment),renderer.thumbnailStatus());
    std::uint32_t frame{};
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle source,std::uint16_t width=160,std::uint16_t height=128){
        Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Forward+ readback allocation failed.");
        auto image=std::make_shared<DecodedImage>(DecodedImage{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)});destinations.push_back(image);
        bgfx::blit(ReadView,staging.handle,0,0,source,0,0,width,height);const auto ready=bgfx::readTexture(staging.handle,image->rgba.data());
        check(ready!=UINT32_MAX,"Forward+ readback unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *image;}
        throw std::runtime_error("Forward+ readback timed out.");
    };
    const auto prepare=[&](const MaterialAsset& source){
        const auto generated=generateMaterialGraphShader(source);MaterialGraphProgramResult published;const auto deadline=Clock::now()+std::chrono::seconds{90};
        do{tick();published=programs.request(generated);if(!published.current&&published.diagnostic!="Compiling material graph...")throw std::runtime_error(published.diagnostic);
            if(!published.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(!published.current&&Clock::now()<deadline);
        check(published.current&&bool(published.programs),"Forward+ generated material compilation timed out.");
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=published.programs;packet->uniforms=generated.uniforms;packet->sources=generated.textures;
        packet->shading=materialOutputNode(source.graph).output.shading;packet->blend=materialOutputNode(source.graph).output.blend;packet->signature=published.programs->signature;return packet;
    };
    auto mesh=quad();check(renderer.prepareModel(*mesh),renderer.modelStatus());std::vector<ModelMaterial> materials=mesh->materials;
    std::vector<ModelMaterialTextures> textures(1);RenderModelInstance instance;instance.entityId=1;instance.model.asset=mesh;instance.materials=&materials;instance.textures=&textures;
    instance.model.transform=math::scaling({5,4,1});instance.forceFullDetail=true;
    SceneSettings settings;settings.camera.position={0,0,10};settings.camera.target={};settings.camera.fov=55;settings.camera.nearPlane=.1f;settings.camera.farPlane=64;
    settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;settings.environment.background.color={};
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&instance,1);
    if(benchmarkDirectory.empty())verifyPassScheduling(renderer,config,scene,read);
    const auto capture=[&](LocalLightMode mode){scene.localLightMode=mode;renderer.renderGame(scene);check(renderer.clusteredLightingStatus().empty(),renderer.clusteredLightingStatus());return read(renderer.gameViewportTexture());};
    const auto compare=[&](const std::string& label){const auto reference=capture(LocalLightMode::AllLightsReference),clustered=capture(LocalLightMode::Clustered);equivalent(clustered,reference,label);return clustered;};

    // Empty and full lists, bitset word crossings, and lights beyond the former
    // sixteen-element array. Keep nonzero energy checks independent of the oracle.
    scene.pointLights={};const auto dark=compare("zero lights");
    for(unsigned count:{1u,16u,17u,31u,32u,33u,64u,256u,1024u}){
        auto points=lights(count,true);scene.pointLights=points;const auto image=compare(std::to_string(count)+" overlapping lights");
        check(energy(image)>energy(dark)+10000,"The all-light fixture rendered black.");
    }
    std::vector<PointLight> beyond(64);for(auto& light:beyond)light.enabled=false;
    beyond.back()=PointLight{};beyond.back().entityId=2064;beyond.back().position={0,0,2};beyond.back().color={1,0,0};beyond.back().radius=5;beyond.back().intensity=10;
    scene.pointLights=beyond;const auto lastLight=compare("last light after disabled prefix");
    check(energy(lastLight)>energy(dark)+10000,"A light beyond index sixteen was discarded.");

    // The response texel must stay attached to its own light across bitset
    // words, including enabled records culled from this receiver's clusters.
    // Only record 64 reaches the plane; editing its response changes no culling
    // volumes and must agree with the independent all-light traversal.
    auto responseLights=lights(65);
    for(auto& light:responseLights){light.position={200,200,200};light.radius=1;}
    auto& responseLight=responseLights.back();responseLight.position={0,0,.15f};
    responseLight.radius=8;responseLight.intensity=4;responseLight.color={1,1,1};
    scene.pointLights=responseLights;const auto punctualResponse=compare("punctual response beyond first two light words");
    responseLight.nearFalloffRadius=1.5f;responseLight.specularStrength=0;
    const auto softenedResponse=compare("diffuse-only softened response beyond first two light words");
    check(centerBrightness(softenedResponse)>30&&centerBrightness(punctualResponse)>centerBrightness(softenedResponse)+40,
        "The response texel did not soften the visible light after 64 offscreen records.");
    responseLight.attenuation=0;
    const auto flatDistance=compare("zero distance attenuation after source index 64");
    check(centerBrightness(flatDistance)>centerBrightness(softenedResponse)+20,
        "Distance attenuation was omitted from the late source record.");
    responseLight.attenuation=2;
    responseLight.nearFalloffRadius=0;responseLight.specularStrength=1;
    equivalent(compare("restored ordinary late-light response"),punctualResponse,
        "Local World response edits must not persist on an ordinary light");

    // Range response lives in the same record after two full light words.
    // Its finite sphere stays unchanged when only the profile is edited.
    responseLight.nearFalloffRadius=1.5f;responseLight.specularStrength=0;
    responseLight.position={0,0,2};responseLight.intensity=10;
    const auto quarticResponse=compare("default quartic response at record 64");
    responseLight.rangeFalloffExponent=1;responseLight.edgeSoftness=1;
    const auto broadResponse=compare("progressive linear range window at record 64");
    check(centerBrightness(broadResponse)>30&&centerBrightness(broadResponse)<centerBrightness(quarticResponse)-10,
        "Falloff exponent did not alter a late light's finite-range response.");
    responseLight.edgeSoftness=.25f;
    const auto narrowEdgeResponse=compare("narrow range transition at record 64");
    check(centerBrightness(narrowEdgeResponse)>centerBrightness(broadResponse)+10,
        "Edge Softness did not move the finite range transition.");
    responseLight.sourceRadius=1.5f;responseLight.specularStrength=1;
    compare("Source Radius at record 64");
    responseLight.position={0,0,8.01f};
    equivalent(compare("finite light beyond its authored radius"),dark,
        "Finite point influence escaped its range despite clustered/reference agreement");

    // The shadow-map allocation index and the light-data index are separate.
    // A shadow caster after sixty-three enabled, non-shadowed lights must still
    // sample its own tile; removing that shadow restores its light contribution.
    auto shadowPoints=lights(64);for(unsigned i=0;i+1<shadowPoints.size();++i){shadowPoints[i].position={200,200,200};shadowPoints[i].radius=1;}
    auto& shadowLight=shadowPoints.back();shadowLight.position={0,0,3};shadowLight.radius=8;shadowLight.intensity=20;shadowLight.color={1,1,1};shadowLight.castShadows=false;
    RenderModelInstance caster=instance;caster.entityId=2;caster.model.transform=math::translation({0,0,1.5f})*math::scaling({.7f,.7f,1});
    std::array<RenderModelInstance,2> shadowInstances{{instance,caster}};scene.shadowInstances=shadowInstances;scene.shadowWorldId=7;scene.shadowResolution=256;scene.pointLights=shadowPoints;
    const auto unshadowed=compare("shadow source index beyond sixteen, disabled");shadowLight.castShadows=true;
    const auto shadowed=compare("shadow source index beyond sixteen, enabled");
    check(renderer.shadowStatus().empty(),renderer.shadowStatus());
    check(centerBrightness(shadowed)<centerBrightness(unshadowed)*.4,"A shadow-casting light beyond source index sixteen sampled the wrong shadow slot.");
    shadowLight.castShadows=false;equivalent(compare("shadow source index beyond sixteen, removed"),unshadowed,"removing a late light shadow must restore its contribution");
    scene.shadowInstances={};scene.shadowWorldId=0;

    auto capacityLights=lights(4096);for(unsigned i=0;i+1<capacityLights.size();++i){capacityLights[i].position={200,200,200};capacityLights[i].radius=1;}
    capacityLights.back()=shadowLight;capacityLights.back().castShadows=false;scene.pointLights=capacityLights;
    const auto capacityImage=compare("last record at 4096-light capacity");check(energy(capacityImage)>energy(dark)+10000,"The final supported light record was truncated.");

    auto distributed=lights(1024);
    for(unsigned i=0;i<distributed.size();++i){
        distributed[i].nearFalloffRadius=i%4==0?1.5f:0.f;
        distributed[i].specularStrength=i%3==0?0.f:i%3==1?.35f:1.f;
        distributed[i].rangeFalloffExponent=i%2?1.f:4.f;
        distributed[i].edgeSoftness=i%5==0?.25f:1.f;
        distributed[i].sourceRadius=i%7==0?1.2f:0.f;
    }
    scene.pointLights=distributed;compare("distributed mixed finite point responses and soft source radii");
    // Non-square dimensions, near-plane crossing, eye inside a light, a light
    // with its center outside the frustum, and a tilted camera exercise the
    // conservative sphere/cluster assignment without an opaque depth shortcut.
    distributed.push_back(PointLight{});distributed.back().position={0,0,9.95f};distributed.back().radius=12;distributed.back().intensity=8;
    distributed.push_back(PointLight{});distributed.back().position={6,0,1};distributed.back().radius=5;distributed.back().intensity=20;
    scene.pointLights=distributed;compare("near-plane and offscreen light volumes");
    settings.camera.position={2.5f,1,9};settings.camera.target={-.4f,0,-1};compare("tilted camera");
    settings.camera.nearPlane=1;settings.camera.farPlane=128;instance.model.transform=math::translation({0,0,-12})*math::scaling({12,10,1});compare("different clip depth range");
    settings.camera.position={0,0,10};settings.camera.target={};settings.camera.nearPlane=.1f;settings.camera.farPlane=64;instance.model.transform=math::scaling({5,4,1});

    MaterialAsset authored;authored.id="forward-plus-generated";addMaterialNode(authored.graph,MaterialNodeKind::Output);auto& output=materialOutputNode(authored.graph).output;
    output.baseColor={.35f,.35f,.35f};output.roughness=.65f;output.metallic=0;
    for(auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}){
        instance.blend=blend;output.blend=blend;output.opacity=.65f;materials[0].baseColor[3]=.65f;
        textures[0].graph.reset();compare("fallback mesh blend "+std::to_string(unsigned(blend)));
        textures[0].graph=prepare(authored);compare("generated mesh blend "+std::to_string(unsigned(blend)));
    }
    textures[0].graph.reset();instance.blend=MaterialBlend::Opaque;materials[0].baseColor[3]=1;
    // A lit transparent particle without any opaque surface must receive the
    // same list. Front/back transparent depth must not be inferred from opaque depth.
    RenderParticle particle;particle.position={};particle.size=6;particle.right={1,0,0};particle.up={0,1,0};
    RenderParticleMaterial particleMaterial;particleMaterial.shading=MaterialShading::StandardPbr;particleMaterial.blend=MaterialBlend::Alpha;particleMaterial.baseColor={.35f,.35f,.35f,.7f};particleMaterial.roughness=.65f;
    scene.instances={};scene.particles=std::span(&particle,1);scene.particleMaterials=std::span(&particleMaterial,1);
    for(float z:{2.f,0.f,-3.f}){particle.position[2]=z;compare("fallback lit particle depth "+std::to_string(z));}
    output.blend=MaterialBlend::Alpha;output.opacity=.7f;particleMaterial.graph=prepare(authored);compare("generated lit transparent particle");
    scene.instances=std::span(&instance,1);compare("mixed lit opaque mesh and transparent particle");scene.particles={};scene.particleMaterials={};

    // Both view resources must survive same-frame submissions with different
    // cameras and light sets. Compare each against an isolated rendering.
    auto red=lights(64,true),blue=lights(64,true);for(auto& light:red)light.color={1,0,0};for(auto& light:blue){light.color={0,0,1};light.position[0]=1;}
    settings.camera.position={0,0,10};settings.camera.target={};scene.pointLights=red;const auto expectedGame=capture(LocalLightMode::Clustered);
    SceneSettings sceneSettings=settings;sceneSettings.camera.position={1,0,8};RenderScene editorScene{sceneSettings,{}};editorScene.sceneMode=true;editorScene.instances=std::span(&instance,1);editorScene.pointLights=blue;
    renderer.render(editorScene);const auto expectedEditor=read(renderer.viewportTexture());
    renderer.render(editorScene);renderer.renderGame(scene);
    equivalent(read(renderer.gameViewportTexture()),expectedGame,"independent Game cluster resources");
    equivalent(read(renderer.viewportTexture()),expectedEditor,"independent Scene cluster resources");
    check(difference(expectedEditor,expectedGame).mean>5,"Independent camera fixture lacks distinguishable output.");

    // The new shared-world entry point must preserve the immediate renderer's
    // image and own every borrowed receiver/caster binding through submission.
    // Mutating the producer's CPU values after freezing must affect neither view.
    {
        SceneSettings sourceGame=settings,sourceEditor=sceneSettings;
        sourceGame.environment.fog.enabled=true;sourceGame.environment.fog.density=.025f;
        sourceGame.environment.fog.color={.09f,.07f,.12f};
        sourceGame.environment.glow={true,.2f,1.f,.15f};
        sourceEditor.environment=sourceGame.environment;
        std::vector<ModelMaterial> sourceMaterials=materials;
        std::vector<ModelMaterialTextures> sourceTextures=textures;
        std::array<RenderModelInstance,2> sourceInstances{{instance,caster}};
        for(auto& value:sourceInstances){value.materials=&sourceMaterials;value.textures=&sourceTextures;}
        std::vector<PointLight> sourcePoints(1);
        sourcePoints[0].entityId=0x4652414d45;sourcePoints[0].position={0,0,3};sourcePoints[0].color={1,0,0};
        sourcePoints[0].intensity=1.5f;sourcePoints[0].radius=8;sourcePoints[0].castShadows=true;
        sourcePoints[0].nearFalloffRadius=1.5f;sourcePoints[0].specularStrength=0;
        sourcePoints[0].rangeFalloffExponent=1;sourcePoints[0].edgeSoftness=.75f;sourcePoints[0].sourceRadius=.5f;
        std::vector<DirectionalLight> sourceDirectionals(1);
        sourceDirectionals[0].entityId=0x4652414d46;sourceDirectionals[0].direction={.2f,-.5f,-1};
        sourceDirectionals[0].color={.15f,.2f,.4f};sourceDirectionals[0].intensity=.25f;
        sourceDirectionals[0].castShadows=true;sourceDirectionals[0].shadowDistance=32;
        std::vector<RenderParticle> sourceParticles(1);
        sourceParticles[0].position={1.1f,.5f,.7f};sourceParticles[0].size=1.2f;
        sourceParticles[0].right={1,0,0};sourceParticles[0].up={0,1,0};
        std::vector<RenderParticleMaterial> sourceParticleMaterials(1);
        sourceParticleMaterials[0].shading=MaterialShading::StandardPbr;
        sourceParticleMaterials[0].blend=MaterialBlend::Alpha;
        sourceParticleMaterials[0].baseColor={.35f,.35f,.35f,.65f};sourceParticleMaterials[0].roughness=.65f;
        const auto immediate=[&](const SceneSettings& cameraSettings) {
            RenderScene result{cameraSettings,{}};result.sceneMode=true;result.instances=sourceInstances;
            result.pointLights=sourcePoints;result.directionalLights=sourceDirectionals;
            result.particles=sourceParticles;result.particleMaterials=sourceParticleMaterials;
            result.shadowInstances=sourceInstances;result.shadowWorldId=0x4652414d45;result.shadowResolution=256;
            return result;
        };
        renderer.render(immediate(sourceEditor));renderer.renderGame(immediate(sourceGame));
        const auto immediateEditor=read(renderer.viewportTexture()),immediateGame=read(renderer.gameViewportTexture());
        check(energy(immediateGame)>energy(dark)+10000,"Prepared-frame fixture lacks a visible illuminated surface.");
        check(difference(immediateEditor,immediateGame).mean>1,"Prepared-frame cameras must produce distinguishable images.");

        struct FixturePins {std::vector<ModelMaterial> materials;std::vector<ModelMaterialTextures> textures;};
        auto fixturePins=std::make_shared<FixturePins>();fixturePins->materials=sourceMaterials;fixturePins->textures=sourceTextures;
        const auto freezeInstance=[&](RenderModelInstance value) {
            value.materials=&fixturePins->materials;value.textures=&fixturePins->textures;return value;
        };
        auto world=std::make_shared<RenderWorldSnapshot>();
        world->points=sourcePoints;world->directional=sourceDirectionals;
        world->particles=sourceParticles;world->particleMaterials=sourceParticleMaterials;
        for(const auto& value:sourceInstances)world->shadowInstances.push_back(freezeInstance(value));
        world->pinned=fixturePins;world->shadowWorldId=0x4652414d45;world->shadowResolution=256;
        auto producer=std::make_shared<PreparedRenderFrame>();producer->world=world;
        producer->appearance=appearance;producer->gridVisible=false;producer->resourceSignature=0x4652414d45;
        for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game}) {
            PreparedRenderView view;view.identity={purpose,0};view.width=160;view.height=128;
            view.settings=purpose==RenderViewPurpose::Scene?sourceEditor:sourceGame;
            for(const auto& value:sourceInstances)view.instances.push_back(freezeInstance(value));
            view.pinned=fixturePins;producer->views.push_back(std::move(view));
        }

        // These are the producer's original storage, not published immutable
        // resource versions. A borrowed frame would now render a different image.
        sourceGame.camera.position={0,0,40};sourceEditor.camera.position={-15,5,15};
        sourcePoints[0].color={0,0,1};sourcePoints[0].intensity=0;
        sourcePoints[0].rangeFalloffExponent=8;sourcePoints[0].edgeSoftness=.05f;sourcePoints[0].sourceRadius=0;
        sourceDirectionals[0].intensity=0;sourceMaterials[0].baseColor={0,1,0,1};
        sourceInstances[0].model.transform=math::translation({100,0,0});
        sourceParticles[0].position={100,0,0};sourceParticleMaterials[0].baseColor={0,0,0,0};
        std::shared_ptr<const PreparedRenderFrame> frozen=std::move(producer);
        std::weak_ptr<const PreparedRenderFrame> frameLease=frozen;
        std::weak_ptr<const FixturePins> bindingLease=fixturePins;
        renderer.renderFrame(frozen);
        frozen.reset();world.reset();fixturePins.reset();
        check(!frameLease.expired() && !bindingLease.expired(),"Prepared frame or borrowed bindings were released before queued draws completed.");
        equivalent(read(renderer.gameViewportTexture()),immediateGame,"immutable prepared Game image");
        equivalent(read(renderer.viewportTexture()),immediateEditor,"immutable prepared Scene image");
        for(unsigned i=0;i<4;++i)tick();
        check(frameLease.expired() && bindingLease.expired(),"Completed prepared frame retained its world/view binding storage indefinitely.");
        renderer.setExternalResourceSignature(0);
    }

    const auto thumbnail=[&](LocalLightMode mode,std::uint64_t signature){
        scene.localLightMode=mode;check(renderer.renderThumbnail("forward-plus-thumbnail",signature,scene,128),renderer.thumbnailStatus());
        for(unsigned i=0;i<120;++i){tick();if(auto result=renderer.pollThumbnail(frame)){check(result->signature==signature,"Thumbnail readback had a stale signature.");return result->image;}}
        throw std::runtime_error("Forward+ thumbnail readback timed out.");
    };
    const auto referenceThumbnail=thumbnail(LocalLightMode::AllLightsReference,1),clusteredThumbnail=thumbnail(LocalLightMode::Clustered,2);
    equivalent(clusteredThumbnail,referenceThumbnail,"thumbnail clustered lights");
    scene.localLightMode=LocalLightMode::Clustered;

    // Environment specular lookup shares its physical texture with light data.
    // Check that changing a local-light list cannot corrupt an unlit region's
    // ambient response on a metallic surface.
    settings.environment.enabled=true;materials[0].metallic=1;materials[0].roughness=.3f;scene.pointLights={};const auto environmentOnly=compare("packed BRDF lookup");
    auto remote=lights(256);for(auto& light:remote){light.position={200,200,200};light.radius=1;}scene.pointLights=remote;
    const auto withRemote=compare("packed BRDF with remote light records");equivalent(withRemote,environmentOnly,"light data must preserve BRDF rows");
    materials[0].metallic=0;materials[0].roughness=.65f;settings.environment.enabled=false;

    std::cout<<"Forward+ image equivalence passed for 0..1024 lights, bitset boundaries, clipped volumes, camera isolation, immutable prepared frames, fallback/generated meshes, transparent particles, and packed BRDF lookup.\n";

    if(!benchmarkDirectory.empty()){
        fs::create_directories(benchmarkDirectory);std::ofstream raw(benchmarkDirectory/"forward-plus-frames.csv"),summary(benchmarkDirectory/"forward-plus-summary.csv");
        check(bool(raw)&&bool(summary),"Cannot create Forward+ benchmark result files.");raw<<"phase,mode,lights,overlap,dual_view,moving_camera,submitted_frame,gpu_frame,cpu_submit_ms,gpu_ms,draws,active_lights,clusters,mask_words,lighting_bytes,cluster_cpu_setup_ms\n";
        summary<<"phase,mode,lights,overlap,dual_view,moving_camera,samples,cpu_mean_ms,cpu_p50_ms,cpu_p95_ms,cpu_p99_ms,cpu_max_ms,gpu_samples,gpu_mean_ms,gpu_p50_ms,gpu_p95_ms,gpu_p99_ms,gpu_max_ms,active_lights,clusters,mask_words,lighting_bytes\n";
        renderer.resizeViewport(960,540);renderer.resizeGameViewport(960,540);instance.model.transform=math::scaling({8,5,1});sceneSettings=settings;editorScene.pointLights={};
        std::ofstream hardware(benchmarkDirectory/"forward-plus-hardware.txt");const auto* caps=bgfx::getCaps();hardware<<"vendor="<<caps->vendorId<<"\ndevice="<<caps->deviceId<<"\nbackend="<<renderer.backendName()<<"\nviewport=960x540\nwarmup=300 frames\nminimum_measured=1000 frames and 5 seconds per phase\nno_shadows=1\nmoving_camera="<<movingCamera<<"\ntrajectory_period_frames=240\n";
        const Camera baseGameCamera=settings.camera,baseEditorCamera=sceneSettings.camera;
        unsigned phase{};
        for(bool dual:{false,true})for(bool overlap:{false,true})for(unsigned count:{64u,256u,1024u})for(auto mode:{LocalLightMode::AllLightsReference,LocalLightMode::Clustered}){
            auto points=lights(count,overlap);scene.pointLights=points;editorScene.pointLights=points;scene.localLightMode=editorScene.localLightMode=mode;
            ClusteredLightingStats lighting;unsigned trajectoryFrame{};
            const auto submit=[&]{
                settings.camera=baseGameCamera;sceneSettings.camera=baseEditorCamera;
                if(movingCamera){
                    // Reset the phase each time so reference and clustered modes
                    // replay the same pose sequence. Translation plus changing
                    // view direction invalidates culling for both camera owners.
                    const float phaseAngle=float(trajectoryFrame%240)*.02617993878f;
                    const Vec3 offset{.4f*std::sin(phaseAngle),.15f*std::cos(phaseAngle),.2f*std::sin(phaseAngle*2)};
                    settings.camera.position=settings.camera.position+offset;sceneSettings.camera.position=sceneSettings.camera.position+offset;
                }
                ++trajectoryFrame;renderer.advanceStreaming();renderer.renderGame(scene);if(dual)renderer.render(editorScene);lighting=renderer.clusteredLightingStats();frame=renderer.advance_frame();context.flush();};
            for(unsigned warm=0;warm<300;++warm)submit();const auto firstFrame=frame+1;auto start=Clock::now();std::vector<double> cpu,gpu;std::uint32_t lastGpu{};
            const auto name=mode==LocalLightMode::Clustered?"clustered":"reference";
            do{const auto before=Clock::now();submit();const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-before).count();cpu.push_back(elapsed);
                const auto* stats=bgfx::getStats();double gpuMs=-1;
                if(stats->gpuFrameNum>=firstFrame&&stats->gpuFrameNum!=lastGpu&&stats->gpuTimerFreq>0&&stats->gpuTimeEnd>=stats->gpuTimeBegin){gpuMs=double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000./double(stats->gpuTimerFreq);gpu.push_back(gpuMs);lastGpu=stats->gpuFrameNum;}
                raw<<phase<<','<<name<<','<<count<<','<<overlap<<','<<dual<<','<<movingCamera<<','<<frame<<','<<stats->gpuFrameNum<<','<<std::setprecision(9)<<elapsed<<','<<gpuMs<<','<<stats->numDraw<<','<<lighting.activeLights<<','<<lighting.clusterCount<<','<<lighting.wordCount<<','<<lighting.allocatedBytes<<','<<lighting.buildCpuMs<<'\n';
            }while(cpu.size()<1000||Clock::now()-start<std::chrono::seconds{5});
            const auto mean=[](const std::vector<double>& values){return values.empty()?-1.:std::accumulate(values.begin(),values.end(),0.)/double(values.size());};
            const auto percentile=[](std::vector<double> values,unsigned p){if(values.empty())return -1.;std::sort(values.begin(),values.end());return values[(values.size()-1)*p/100];};
            summary<<phase<<','<<name<<','<<count<<','<<overlap<<','<<dual<<','<<movingCamera<<','<<cpu.size()<<','<<mean(cpu)<<','<<percentile(cpu,50)<<','<<percentile(cpu,95)<<','<<percentile(cpu,99)<<','<<percentile(cpu,100)<<','<<gpu.size()<<','<<mean(gpu)<<','<<percentile(gpu,50)<<','<<percentile(gpu,95)<<','<<percentile(gpu,99)<<','<<percentile(gpu,100)<<','<<lighting.activeLights<<','<<lighting.clusterCount<<','<<lighting.wordCount<<','<<lighting.allocatedBytes<<'\n';summary.flush();raw.flush();
            std::cout<<"Benchmark "<<phase++<<' '<<name<<" lights="<<count<<" overlap="<<overlap<<" dual="<<dual<<" moving="<<movingCamera<<" CPU="<<mean(cpu)<<" GPU="<<mean(gpu)<<" ms\n";
        }
    }
    resources.stop();renderer.drainPending(frame);
}
}
int main(int argc,char** argv){try{
    check(argc==4||(argc==6&&(std::string(argv[4])=="--benchmark"||std::string(argv[4])=="--benchmark-moving")),"Pass shaderc, shader source, shader include directories; optionally --benchmark or --benchmark-moving output-directory.");
    Temporary temporary;ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary.path};Sdl sdl;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson Forward+ hidden checks",960,540,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);check(bool(window),SDL_GetError());
    exercise(window.get(),config,argc==6?fs::path(argv[5]):fs::path{},argc==6&&std::string(argv[4])=="--benchmark-moving");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
