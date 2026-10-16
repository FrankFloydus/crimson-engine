#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/PointShadowMath.h"
#include "resources/ShaderCompiler.h"
#include "resources/ShaderReloadSession.h"
#include "ShaderSourceBaseline.h"
#include "assets/AtomicFile.h"
#include "assets/ModelAnimation.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <nlohmann/json.hpp>
#include <numeric>
#include <limits>
#include <set>
#include "crimson-renderer/shaders/point_light.sh"

namespace {
using namespace crimson;
constexpr std::uint16_t ImageSize=128,ReadbackView=254;
void check(bool condition,const std::string& message) {if (!condition) throw std::runtime_error(message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("crimson-point-shadows-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){std::filesystem::create_directories(path);}
    ~Temp(){std::error_code error;const auto target=std::filesystem::weakly_canonical(path,error);
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if (!error && target.parent_path()==parent && target.filename().string().starts_with("crimson-point-shadows-")) std::filesystem::remove_all(target,error);}
};
struct SdlOwner {SdlOwner(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~SdlOwner(){SDL_Quit();}};
struct ImGuiOwner {ImGuiOwner(){ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;}~ImGuiOwner(){ImGui::DestroyContext();}};
struct TextureOwner {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~TextureOwner(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
std::shared_ptr<ModelAsset> quad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="point-shadow-test-quad";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> coordinates{{{0,1},{1,1},{1,0},{0,0}}};
    for (unsigned i=0;i<4;++i) {ModelVertex vertex;vertex.position=positions[i];vertex.normal={0,0,1};
        vertex.uv0=coordinates[i];vertex.uv1={1-coordinates[i][0],coordinates[i][1]};primitive.vertices.push_back(vertex);math::include(asset->bounds,vertex.position);}
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.metallic=0;material.roughness=1;asset->materials.push_back(material);return asset;
}
Mat4 plane(const Camera& camera,const Vec3& position,float extent) {
    const auto right=camera.right()*extent,up=math::cross(camera.right(),camera.forward())*extent,normal=-camera.forward();
    return {right[0],right[1],right[2],0,up[0],up[1],up[2],0,normal[0],normal[1],normal[2],0,position[0],position[1],position[2],1};
}
double brightness(const DecodedImage& image,const Camera& camera,const Vec3& point) {
    const auto pixel=math::project(point,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),{0,0,ImageSize,ImageSize},ClipDepth::ZeroToOne);
    check(bool(pixel),"Fixture sample is not projectable.");
    const int x=static_cast<int>((*pixel)[0]),y=static_cast<int>((*pixel)[1]);
    check(x>1 && x<ImageSize-2 && y>1 && y<ImageSize-2,"Fixture sample escaped its image.");
    double sum=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)for(unsigned channel=0;channel<3;++channel)
        sum+=image.rgba[(std::size_t(y+dy)*image.width+std::size_t(x+dx))*4+channel];
    return sum/27;
}
double linearSample(const DecodedImage& image,const Camera& camera,const Vec3& point) {
    const auto pixel=math::project(point,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),
        {0,0,ImageSize,ImageSize},ClipDepth::ZeroToOne);
    check(bool(pixel),"Soft-shadow sample is not projectable.");
    const int x=static_cast<int>((*pixel)[0]),y=static_cast<int>((*pixel)[1]);
    check(x>=0 && x<ImageSize && y>=0 && y<ImageSize,"Soft-shadow sample escaped its image.");
    double result=0;
    for (unsigned channel=0;channel<3;++channel) {
        const double encoded=image.rgba[(std::size_t(y)*image.width+std::size_t(x))*4+channel]/255.0;
        result+=encoded<=.04045 ? encoded/12.92:std::pow((encoded+.055)/1.055,2.4);
    }
    return result/3;
}
struct ShadowEdge {double width{},midpoint{-1};};
ShadowEdge shadowEdge(const DecodedImage& shadow,const DecodedImage& clear,const Camera& camera,Vec3 center,Vec3 right) {
    ShadowEdge result;
    // This fixture uses Linear tone mapping and unexposed values below one.
    // Decode the sole final sRGB encoding before dividing out the receiver's
    // diffuse lighting. Highlight size and HDR clipping cannot mimic a PCSS edge.
    for (unsigned sample=0;sample<=84;++sample) {
        const double offset=.1+.025*sample;
        const auto point=center+right*offset;
        const auto lit=linearSample(clear,camera,point);
        check(lit>.015 && lit<.9,"Soft-shadow reference is dark or display-clipped.");
        const auto visibility=linearSample(shadow,camera,point)/lit;
        if (visibility>.12 && visibility<.88) result.width+=.025;
        if (result.midpoint<0 && visibility>=.5) result.midpoint=offset;
    }
    return result;
}
unsigned maximumDifference(const DecodedImage& a,const DecodedImage& b) {
    check(a.width==b.width && a.height==b.height && a.rgba.size()==b.rgba.size(),"Shadow images have different extents.");
    unsigned result=0;
    for (std::size_t i=0;i<a.rgba.size();++i)
        result=std::max(result,static_cast<unsigned>(std::abs(int(a.rgba[i])-int(b.rgba[i]))));
    return result;
}
// Diagnostics replace only a private copy of this helper, never project shader
// sources or authored assets. Keep the old filter here as a regression control,
// rather than retaining an alternate rendering mode in the engine.
constexpr const char* LegacyCornerFilter=R"(
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 samplePixel = clamp(pixel + vec2(float(x), float(y)), origin + vec2_splat(0.5), origin + vec2_splat(u_shadowAtlas.w - 0.5));
            vec2 lowerCenter = floor(samplePixel - vec2_splat(0.5)) + vec2_splat(0.5);
            vec2 shallowCenter = lowerCenter + vec2(depthGradient.x < 0.0 ? 1.0 : 0.0, depthGradient.y < 0.0 ? 1.0 : 0.0);
            float sampleDepth = depth + dot(depthGradient, shallowCenter - pixel);
            visibility += shadow2D(s_pointShadow, vec3(samplePixel * u_shadowAtlas.xy, sampleDepth));
        }
    }
    return visibility / 9.0;
)";
constexpr const char* UnfilteredCornerFilter=R"(
    vec2 center = clamp(floor(pixel) + vec2_splat(0.5), origin + vec2_splat(0.5), origin + vec2_splat(u_shadowAtlas.w - 0.5));
    float reference = depth + dot(depthGradient, center - pixel);
    float stored = s_pointShadow.m_texture.Load(ivec3(ivec2(center), 0)).x;
    return reference <= stored + comparisonTolerance ? 1.0 : 0.0;
)";
struct ShaderHelperRestore {
    std::filesystem::path path;
    std::string original;
    ~ShaderHelperRestore(){try {writeFileAtomically(path,original);}catch(...) {}}
};
template<class Read> void touchingCorner(Renderer& renderer,const ShaderCompileConfig& config,
    const std::shared_ptr<ModelAsset>& mesh,const Read& read) {
    const auto path=config.sourceDirectory/"point_shadow.sh";
    std::string original;
    {std::ifstream input(path,std::ios::binary);check(bool(input),"Cannot read private point-shadow helper.");
        original.assign(std::istreambuf_iterator<char>(input),{});}
    ShaderHelperRestore restore{path,original};
    const auto function=original.find("float pointShadowVisibilityPcf(");
    const auto begin=original.find("    float visibility = 0.0;",function);
    const auto end=original.find("\n}",begin);
    check(function!=std::string::npos && begin!=std::string::npos && end!=std::string::npos,
        "Cannot locate the private PCF diagnostic boundary.");
    const auto vertex=compileShader(config,"vs_model");
    const auto compile=[&](const char* replacement) {
        const auto source=replacement ? original.substr(0,begin)+replacement+original.substr(end):original;
        writeFileAtomically(path,source);
        return RendererShaderPair{RendererProgram::Model,vertex.bytes,compileShader(config,"fs_model").bytes};
    };
    const auto legacy=compile(LegacyCornerFilter);
    const auto unfiltered=compile(UnfilteredCornerFilter);
    const auto repaired=compile(nullptr);
    const auto publish=[&](const RendererShaderPair& pair){check(renderer.reloadPrograms(std::span(&pair,1)),renderer.shaderStatus());};

    std::vector<ModelMaterial> materials=mesh->materials;materials[0].doubleSided=true;
    RenderModelInstance receiver;receiver.entityId=9101;receiver.model.asset=mesh;
    receiver.materials=&materials;receiver.forceFullDetail=true;
    receiver.model.transform=math::translation({2,1.5f,0})*math::scaling({2,1.5f,1});
    auto blocker=receiver;blocker.entityId=9102;
    // Exact columns avoid a trigonometric epsilon opening the shared vertical
    // edge: receiver z=0, x=[0,4]; blocker x=0, z=[0,4]; both y=[0,3].
    blocker.model.transform={0,0,-2,0, 0,1.5f,0,0, 1,0,0,0, 0,1.5f,2,1};
    const std::array casters{receiver,blocker};
    SceneSettings settings;settings.environment.enabled=false;settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    settings.environment.ambientVisibility.enabled=false;settings.environment.glow.enabled=false;
    settings.environment.toneMapping=ToneMapping::Linear;settings.exposure=1;
    settings.camera.position={.0035f,1.5f,.012f};settings.camera.target={.0035f,1.5f,0};
    settings.camera.nearPlane=.0001f;settings.camera.fov=45;
    PointLight light;light.entityId=9103;light.position={-.3f,1.5f,.3f};light.radius=7.12f;
    light.intensity=4;light.specularStrength=0;light.sourceRadius=0;light.nearFalloffRadius=1.5f;
    light.rangeFalloffExponent=1;light.edgeSoftness=.93f;light.castShadows=true;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&receiver,1);
    scene.shadowInstances=casters;scene.pointLights=std::span(&light,1);scene.shadowWorldId=9100;scene.shadowResolution=1024;
    const auto capture=[&](bool game) {
        if(game)renderer.renderGame(scene);else renderer.render(scene);
        check(renderer.shadowStatus().empty(),renderer.shadowStatus());
        return read(game ? renderer.gameViewportTexture():renderer.viewportTexture());
    };
    struct Case {
        bool game{},savedBias{},litSide{};
        float lightX{};
        DecodedImage clear,old,hard,fixed;
    };
    std::vector<Case> cases;
    for(bool game:{false,true})for(bool savedBias:{false,true})for(float lightX:{-.3f,-.301f,.3f})
        cases.push_back({game,savedBias,lightX>0,lightX});
    const auto configure=[&](const Case& test) {
        light.position[0]=test.lightX;
        light.shadowDepthBias=test.savedBias ? .005f:0;
        light.shadowNormalBias=test.savedBias ? 1.f:0;
    };
    // Shader variants share identical captured depth and lighting equations;
    // only the hard-PCF query changes. The unfiltered control also retains the
    // selected authored biases, so a bias-created gap cannot be called filtering.
    publish(legacy);
    for(auto& test:cases){configure(test);light.castShadows=false;test.clear=capture(test.game);
        light.castShadows=true;test.old=capture(test.game);}
    publish(unfiltered);
    for(auto& test:cases){configure(test);test.hard=capture(test.game);}
    publish(repaired);
    for(auto& test:cases){configure(test);test.fixed=capture(test.game);}
    bool reproduced=false;
    constexpr std::array distances{.00015f,.0003f,.0005f,.0008f,.0012f,.002f,.003f,
        .00325f,.0035f,.00375f,.004f,.00425f,.0045f,.006f};
    constexpr auto sampleCount=3*distances.size();
    for(const auto& test:cases) {
        double oldExcess=0,fixedExcess=0,hardLight=0,maxFixed=0;
        unsigned blocked=0;
        for(float y:{1.499f,1.5f,1.501f})for(float x:distances) {
            const Vec3 point{x,y,0};const auto clear=linearSample(test.clear,settings.camera,point);
            check(clear>.03 && clear<.9,"Corner reference is dark or display-clipped.");
            const auto old=linearSample(test.old,settings.camera,point)/clear;
            const auto hard=linearSample(test.hard,settings.camera,point)/clear;
            const auto fixed=linearSample(test.fixed,settings.camera,point)/clear;
            const auto context=std::string(test.game ? "Game":"Scene")+", bias="+(test.savedBias ? ".005/1":"0/0")+
                ", lightX="+std::to_string(test.lightX)+", receiverX="+std::to_string(x)+
                ", old="+std::to_string(old)+", unfiltered="+std::to_string(hard)+", repaired="+std::to_string(fixed);
            if(test.litSide) {
                check(std::abs(fixed-1)<.035,"Contact correction self-shadowed the physically lit adjacent side: "+context);
                continue;
            }
            oldExcess+=std::max(old-hard,0.0);fixedExcess+=std::max(fixed-hard,0.0);hardLight+=hard;
            maxFixed=std::max(maxFixed,fixed);
            check(fixed<=old+.035,"Corner correction increased the old light leak: "+context);
            if(hard<.05){++blocked;check(fixed<.05,"Filtered corner leaked across a confirmed central blocker: "+context);}
        }
        if(test.litSide)continue;
        std::cout<<"Touching-corner "<<(test.game ? "Game":"Scene")<<" bias="<<(test.savedBias ? ".005/1":"0/0")
            <<" lightX="<<test.lightX<<" old/filter excess="<<oldExcess<<'/'<<fixedExcess
            <<" unfiltered mean="<<hardLight/sampleCount<<" repaired max="<<maxFixed<<" central blockers="<<blocked<<'/'<<sampleCount<<"\n";
        if(!test.savedBias && test.lightX==-.3f) {
            check(blocked==sampleCount,"Unfiltered exact-touching corner did not provide the expected opaque control.");
            check(oldExcess>.4,"Old PCF control did not reproduce the corner halo.");
            check(fixedExcess<oldExcess*.1+.03,"Repaired filter retained the touching-corner halo.");
            reproduced=true;
        }
        if(test.savedBias && test.lightX==-.3f) {
            check(blocked>=6 && hardLight>3,"Saved biases did not produce both an offset gap and valid farther occlusion.");
            check(oldExcess>.2,"Saved-bias control did not isolate the additional PCF contact halo.");
            check(fixedExcess<oldExcess*.1+.03,"Saved-bias contact retained filter-added light beyond the unfiltered gap.");
        }
    }
    check(reproduced,"No exact-touching corner control ran.");

    // This finite, grazing caster stops several metres before the receiver.
    // Its infinite plane would reach z=0 close to the projected silhouette;
    // completing that plane must not grow the real, separated shadow edge.
    struct SeparatedCase {
        bool game{},savedBias{};float intercept{};
        DecodedImage clear,old,hard,fixed;
    };
    std::vector<SeparatedCase> separated;
    for(bool game:{false,true})for(bool savedBias:{false,true})for(float intercept:{.0035f,.004f,.0045f})
        separated.push_back({game,savedBias,intercept});
    std::array<RenderModelInstance,2> separatedCasters;
    constexpr float casterLow=.5f,casterHigh=3.2f,casterSlope=.0002f,lightZ=4;
    const auto configureSeparated=[&](const SeparatedCase& test) {
        const float half=(casterHigh-casterLow)*.5f,middle=(casterHigh+casterLow)*.5f;
        const float edgeX=lightZ*(test.intercept+casterSlope*casterHigh)/(lightZ-casterHigh);
        receiver.model.transform=math::translation({0,1.5f,0})*math::scaling({1,1.5f,1});
        blocker.model.transform={casterSlope*half,0,half,0, 0,1.4f,0,0, -1,0,casterSlope,0,
            test.intercept+casterSlope*middle,1.5f,middle,1};
        separatedCasters={receiver,blocker};scene.shadowInstances=separatedCasters;
        scene.shadowWorldId=9150;
        settings.camera.position={edgeX,1.5f,.08f};settings.camera.target={edgeX,1.5f,0};
        light.position={0,1.5f,lightZ};light.color={1,1,1};
        // Calibrate a neutral, unclipped ~.2 linear reference using the same
        // finite-range attenuation as the renderer. The earlier orange light
        // produced only .0298 RGB mean at intensity20, below the .03 guard.
        const float attenuation=point_lighting::pointLightAttenuation(lightZ*lightZ,light.inverseRadiusSquared(),
            light.nearFalloffRadius*light.nearFalloffRadius,light.rangeFalloffExponent,light.edgeSoftness,light.attenuation);
        const float diffuse=(1-.08f*materials[0].specular)*materials[0].baseColor[0]/std::acos(-1.f);
        check(attenuation>0&&diffuse>0,"Separated silhouette has no reference light response.");
        light.intensity=.2f/(attenuation*diffuse);settings.exposure=1;
        light.shadowDepthBias=test.savedBias?.005f:0;light.shadowNormalBias=1;
        const Vec3 receiverEdge{edgeX,1.5f,0},casterEdge{test.intercept+casterSlope*casterHigh,1.5f,casterHigh};
        check(math::length(receiverEdge-casterEdge)>3 && casterLow>.4f,
            "Separated silhouette fixture did not retain a genuine physical caster/receiver gap.");
        return edgeX;
    };
    publish(legacy);
    for(auto& test:separated){configureSeparated(test);light.castShadows=false;test.clear=capture(test.game);
        light.castShadows=true;test.old=capture(test.game);}
    publish(unfiltered);
    for(auto& test:separated){configureSeparated(test);test.hard=capture(test.game);}
    publish(repaired);
    for(auto& test:separated){configureSeparated(test);test.fixed=capture(test.game);}
    for(const auto& test:separated) {
        const auto edgeX=configureSeparated(test);unsigned shadowed=0,lit=0,filtered=0,checkedOutside=0;
        double maximumGrowth=0;
        for(float y:{1.49f,1.5f,1.51f})for(int sample=-28;sample<=28;++sample) {
            const float x=edgeX+float(sample)*.0005f;const Vec3 point{x,y,0};
            const auto clear=linearSample(test.clear,settings.camera,point);
            check(clear>.03&&clear<.9,"Separated silhouette reference is dark or display-clipped: "+
                std::string(test.game?"Game":"Scene")+", bias="+(test.savedBias?".005/1":"0/1")+
                ", intercept="+std::to_string(test.intercept)+", x="+std::to_string(x)+", y="+std::to_string(y)+
                ", linear RGB mean="+std::to_string(clear)+", intensity="+std::to_string(light.intensity));
            const auto old=linearSample(test.old,settings.camera,point)/clear;
            const auto hard=linearSample(test.hard,settings.camera,point)/clear;
            const auto fixed=linearSample(test.fixed,settings.camera,point)/clear;
            shadowed+=hard<.05;lit+=hard>.95;filtered+=old>.1&&old<.9;
            if(x>edgeX&&hard>.95) {
                ++checkedOutside;maximumGrowth=std::max(maximumGrowth,old-fixed);
                const auto context=std::string(test.game?"Game":"Scene")+", bias="+(test.savedBias?".005/1":"0/1")+
                    ", intercept="+std::to_string(test.intercept)+", edge="+std::to_string(edgeX)+", x="+std::to_string(x)+
                    ", old="+std::to_string(old)+", unfiltered="+std::to_string(hard)+", repaired="+std::to_string(fixed);
                check(fixed>=old-.035,"Contact reconstruction grew a genuinely separated caster silhouette: "+context);
            }
        }
        check(shadowed>=6&&lit>=6&&filtered>=6&&checkedOutside>=6,
            "Separated silhouette control did not resolve opaque coverage, lit receiver and a filtered finite edge.");
        std::cout<<"Separated grazing silhouette "<<(test.game?"Game":"Scene")<<" bias="<<(test.savedBias?".005/1":"0/1")
            <<" intercept="<<test.intercept<<" gap=3.2m shadowed/lit/filtered="<<shadowed<<'/'<<lit<<'/'<<filtered
            <<" maximum shadow growth="<<maximumGrowth<<'\n';
    }
}
#include "point_shadow_room_fixture.h"
void exercise(SDL_Window* window,const ShaderCompileConfig& config) {
    // Destinations outlive Renderer even if a readback times out.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(window);ImGuiOwner context;ImGuiBgfx tools;
    renderer.resize(320,240);renderer.resizeViewport(ImageSize,ImageSize);renderer.resizeGameViewport(ImageSize,ImageSize);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=887;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.1f,.1f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    check(renderer.publishThumbnailEnvironment(environment),renderer.thumbnailStatus());
    std::uint32_t frame{};
    const auto tick=[&] {resources.pump();renderer.advanceStreaming();frame=bgfx::frame();tools.postFrame(frame);std::this_thread::sleep_for(std::chrono::milliseconds{1});};
    const auto read=[&](bgfx::TextureHandle source) {
        TextureOwner staging{bgfx::createTexture2D(ImageSize,ImageSize,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Shadow fixture readback allocation failed.");
        auto image=std::make_shared<DecodedImage>(DecodedImage{ImageSize,ImageSize,std::vector<std::uint8_t>(ImageSize*ImageSize*4)});
        destinations.push_back(image);bgfx::setViewMode(ReadbackView,bgfx::ViewMode::Sequential);
        bgfx::blit(ReadbackView,staging.handle,0,0,source,0,0,ImageSize,ImageSize);
        const auto ready=bgfx::readTexture(staging.handle,image->rgba.data());check(ready!=UINT32_MAX,"Shadow fixture readback unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *image;}
        throw std::runtime_error("Shadow fixture readback timed out.");
    };
    const auto mesh=quad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    touchingCorner(renderer,config,mesh,read);
    // The exposure-independent editor grid is not a shadow receiver. Isolate
    // the room's material pixels before comparing them with triangle rays.
    renderer.setGridVisible(false);
    room_shadow_fixture::exercise(renderer,config,read);
    renderer.setGridVisible(true);
    std::vector<ModelMaterial> receiverMaterials=mesh->materials,casterMaterials=mesh->materials;
    std::vector<ModelMaterialTextures> casterTextures(1);
    RenderModelInstance receiver;receiver.entityId=1;receiver.model.asset=mesh;receiver.materials=&receiverMaterials;receiver.forceFullDetail=true;
    RenderModelInstance caster;caster.entityId=2;caster.model.asset=mesh;caster.materials=&casterMaterials;caster.textures=&casterTextures;caster.forceFullDetail=true;
    std::array<RenderModelInstance,2> shadowInstances;
    std::array<PointLight,16> lights;
    for(unsigned i=0;i<lights.size();++i){lights[i].entityId=100+i;lights[i].position={};lights[i].color={1,1,1};
        lights[i].radius=20;lights[i].intensity=100;lights[i].castShadows=true;}
    SceneSettings settings;settings.environment.enabled=false;settings.exposure=1;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&receiver,1);scene.pointLights=std::span(lights.data(),1);
    scene.shadowWorldId=1;scene.shadowResolution=256;
    Vec3 center{},right{};PointShadowStats submittedStats;
    const auto orient=[&](unsigned face) {
        settings.camera=point_shadow::faceCamera({},20,face,256);const auto direction=settings.camera.forward();
        center=direction*4;settings.camera.position=direction*-2;settings.camera.target=center;settings.camera.fov=45;
        right=settings.camera.right();receiver.model.transform=plane(settings.camera,center,3);
        caster.model.transform=plane(settings.camera,direction*2,.5f);
    };
    const auto capture=[&] {
        shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;
        renderer.renderGame(scene);check(renderer.shadowStatus().empty(),renderer.shadowStatus());
        submittedStats=renderer.shadowStats(); // advanceStreaming resets per-frame counters during readback ticks.
        return read(renderer.gameViewportTexture());
    };
    for (unsigned face=0;face<6;++face) {
        orient(face);lights[0].castShadows=false;const auto clear=capture();lights[0].castShadows=true;const auto shadow=capture();
        const auto lit=brightness(clear,settings.camera,center),dark=brightness(shadow,settings.camera,center);
        check(lit>80 && dark<lit*.35,"A cube face failed to cast an opaque shadow: "+std::to_string(face));
        const auto outside=center+right*1.6;
        check(std::abs(brightness(clear,settings.camera,outside)-brightness(shadow,settings.camera,outside))<8,"Shadow leaked outside caster bounds or onto another tile.");
    }
    // Render receivers straddling exact cube-face edges/corners and compare
    // neighboring samples on both sides, including the guarded PCF footprint.
    for (const Vec3 diagonal:{Vec3{1,0,1},Vec3{1,1,1},Vec3{0,1,1}}) {
        const auto direction=math::normalized(diagonal);center=direction*4;
        settings.camera.position=direction*-2;settings.camera.target=center;settings.camera.up={0,1,0};settings.camera.fov=45;
        right=settings.camera.right();receiver.model.transform=plane(settings.camera,center,3);caster.model.transform=plane(settings.camera,direction*2,.5f);
        lights[0].castShadows=false;const auto clear=capture();lights[0].castShadows=true;const auto shadow=capture();
        for (float offset:{-.08f,0.f,.08f}) {
            const auto point=center+right*offset;
            check(brightness(shadow,settings.camera,point)<brightness(clear,settings.camera,point)*.35,"Cube seam or corner leaked light through an opaque caster.");
        }
    }
    // Equal hard projected extents separate genuine contact hardening from
    // changes in caster size or specular highlights. The far caster projects
    // half a metre to one metre; the near caster projects .95 to the same edge.
    orient(5);settings.environment.toneMapping=ToneMapping::Linear;
    lights[0].intensity=10;lights[0].specularStrength=0;
    lights[0].castShadows=false;const auto softClear=capture();lights[0].castShadows=true;
    const auto farHard=capture();
    const auto hardEdge=shadowEdge(farHard,softClear,settings.camera,center,right);
    lights[0].sourceRadius=.4f;const auto farSoftSmall=capture();
    check(submittedStats.renderedFaces==0 && submittedStats.cacheHits==1,
        "Source Radius edit rerendered six static shadow faces instead of changing the receiver filter.");
    const auto smallEdge=shadowEdge(farSoftSmall,softClear,settings.camera,center,right);
    lights[0].sourceRadius=.7f;const auto farSoft=capture();
    check(submittedStats.renderedFaces==0 && submittedStats.cacheHits==1,
        "Larger Source Radius invalidated the point-shadow projection.");
    const auto farEdge=shadowEdge(farSoft,softClear,settings.camera,center,right);
    caster.model.transform=plane(settings.camera,settings.camera.forward()*3.8f,.95f);
    const auto nearSoft=capture();
    check(submittedStats.renderedFaces==6,"A moved soft-shadow caster did not refresh its six faces.");
    const auto nearEdge=shadowEdge(nearSoft,softClear,settings.camera,center,right);
    lights[0].sourceRadius=0;const auto nearHard=capture();
    check(submittedStats.renderedFaces==0 && submittedStats.cacheHits==1,
        "Returning Source Radius to zero needlessly invalidated the caster map.");
    const auto nearHardEdge=shadowEdge(nearHard,softClear,settings.camera,center,right);
    const auto widths=" hard="+std::to_string(hardEdge.width)+", radius.4="+std::to_string(smallEdge.width)+
        ", radius.7="+std::to_string(farEdge.width)+", contact="+std::to_string(nearEdge.width);
    check(hardEdge.midpoint>.8 && hardEdge.midpoint<1.2 && nearHardEdge.midpoint>.8 && nearHardEdge.midpoint<1.2 &&
        std::abs(hardEdge.midpoint-nearHardEdge.midpoint)<.1,
        "Contact-hardening fixture does not preserve the same hard projected edge."+widths);
    check(smallEdge.width>hardEdge.width+.15 && farEdge.width>smallEdge.width+.1 && farEdge.width>.35,
        "Source Radius did not widen diffuse-only point-shadow penumbrae."+widths);
    check(farEdge.width>nearEdge.width+.2 && nearEdge.width<farEdge.width*.65,
        "Soft point shadows did not harden as the caster approached the receiver."+widths);
    check(farEdge.midpoint>.7 && farEdge.midpoint<1.3 && nearEdge.midpoint>.8 && nearEdge.midpoint<1.2,
        "PCSS enlarged the hard silhouette instead of filtering around its projected edge."+widths);
    std::cout<<"Diffuse-only point-shadow transition widths(m):"<<widths
        <<", hard/contact edge="<<hardEdge.midpoint<<'/'<<nearHardEdge.midpoint<<'\n';
    caster.model.transform=plane(settings.camera,settings.camera.forward()*2,.5f);
    const auto restoredHard=capture();
    check(maximumDifference(restoredHard,farHard)==0,"Source Radius zero did not restore the exact hard-filter image.");
    lights[0].sourceRadius=.7f;lights[0].castShadows=false;
    check(maximumDifference(capture(),softClear)==0,"Disabling Cast Shadows retained a soft occluder.");
    lights[0].castShadows=true;lights[0].sourceRadius=0;lights[0].specularStrength=1;
    lights[0].intensity=100;settings.environment.toneMapping=ToneMapping::Reinhard;
    orient(5);const auto opaque=capture();const auto dark=brightness(opaque,settings.camera,center);
    const auto smallBytes=point_shadow::atlasLayout(256,1).bytes(),defaultBytes=point_shadow::atlasLayout(1024,1).bytes();
    scene.shadowResolution=1024;const auto defaultResolution=capture();
    check(submittedStats.resolution==1024 && submittedStats.renderedFaces==6 && submittedStats.allocatedBytes>=defaultBytes+smallBytes,
        "Default resolution switch failed to recreate all faces or count the retired small atlas.");
    check(brightness(defaultResolution,settings.camera,center)<35,"Default 1024 resolution lost the shadow.");
    scene.shadowResolution=256;capture();
    check(submittedStats.resolution==256 && submittedStats.renderedFaces==6 && submittedStats.allocatedBytes>=defaultBytes+smallBytes,
        "Returning to 256 failed to invalidate faces or retain old-atlas accounting.");
    for(unsigned i=0;i<3;++i)tick();
    check(renderer.shadowResidentBytes()==smallBytes,"Retired shadow allocations did not drain after their frame fence.");
    capture();check(submittedStats.renderedFaces==0 && submittedStats.cacheHits==1,"Static shadow frame did not reuse its six cached faces.");
    const auto cached=[&](const char* reason){capture();check(submittedStats.renderedFaces==0 && submittedStats.cacheHits==1,reason);};
    receiverMaterials[0].baseColor[0]=.7f;cached("Receiver RGB edit invalidated shadow depth.");receiverMaterials[0].baseColor[0]=1;
    lights[0].color={.8f,.9f,1};cached("Light color edit invalidated shadow depth.");lights[0].color={1,1,1};
    lights[0].intensity=110;cached("Light intensity edit invalidated shadow depth.");lights[0].intensity=100;
    settings.exposure=2;cached("Exposure edit invalidated shadow depth.");settings.exposure=1;
    settings.camera.position=settings.camera.position+right*.1;cached("Camera movement invalidated shadow depth.");orient(5);
    const auto streamingSignature=renderer.streamingSignature();
    lights[0].position=right*.1;capture();check(submittedStats.renderedFaces==6,"Light movement did not invalidate all six faces.");
    check(renderer.streamingSignature()==streamingSignature,"Light shadow update changed the Scene picking resource signature.");lights[0].position={};capture();
    caster.model.transform=math::translation(right*.1)*caster.model.transform;capture();
    check(submittedStats.renderedFaces==6,"Caster movement did not invalidate all six faces.");
    check(renderer.streamingSignature()==streamingSignature,"Shadow-only caster pose changed the Scene picking resource signature.");orient(5);capture();
    check(renderer.requestPick({1,1,1,ImageSize/2,ImageSize/2,renderer.streamingSignature()}),renderer.pickingStatus());
    shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;renderer.render(scene);tick();
    const auto queuedSignature=renderer.streamingSignature();
    lights[0].position=right*.1;caster.model.transform=math::translation(right*.1)*caster.model.transform;capture();
    std::optional<PickResult> picked;
    for(unsigned i=0;i<120 && !picked;++i){picked=renderer.pollPick(frame);if(!picked)tick();}
    check(picked && picked->entityId==receiver.entityId && picked->resourceSignature==queuedSignature && renderer.streamingSignature()==queuedSignature,
        "Shadow-only light/caster updates invalidated or corrupted a queued Scene pick.");
    lights[0].position={};orient(5);capture();
    caster.castShadows=false;const auto noCast=capture();check(brightness(noCast,settings.camera,center)>dark+60,"Cast Shadows disabled retained a stale tile.");
    caster.castShadows=true;receiver.receiveShadows=false;const auto noReceive=capture();
    check(brightness(noReceive,settings.camera,center)>dark+60,"Receive Shadows disabled still sampled occlusion.");receiver.receiveShadows=true;
    caster.blend=MaterialBlend::Alpha;const auto blend=capture();check(brightness(blend,settings.camera,center)>dark+60,"Alpha-blended material unexpectedly cast an opaque shadow.");caster.blend=MaterialBlend::Opaque;
    caster.model.transform=math::translation(right*3)*caster.model.transform;const auto moved=capture();
    check(brightness(moved,settings.camera,center)>dark+60,"Moving a caster did not invalidate its shadow.");orient(5);
    caster.model.transform=caster.model.transform*math::scaling({-1,1,-1});const auto backface=capture();
    check(brightness(backface,settings.camera,center)>dark+60,"Single-sided backface unexpectedly cast a shadow.");
    casterMaterials[0].doubleSided=true;const auto doubleSided=capture();
    check(brightness(doubleSided,settings.camera,center)<dark+10,"Double-sided backface failed to cast a shadow.");casterMaterials[0].doubleSided=false;orient(5);
    receiverMaterials[0].emissive={.4f,.4f,.4f};settings.environment.enabled=true;lights[0].intensity=0;
    const auto indirectShadow=capture();lights[0].castShadows=false;const auto indirectClear=capture();
    check(brightness(indirectShadow,settings.camera,center)>40 &&
        std::abs(brightness(indirectShadow,settings.camera,center)-brightness(indirectClear,settings.camera,center))<=1,"Point shadows changed IBL or emissive lighting.");
    receiverMaterials[0].emissive={};settings.environment.enabled=false;lights[0].intensity=100;lights[0].castShadows=true;
    // A masked caster uses the same alpha UVs/threshold as the color pass.
    constexpr std::array<std::uint8_t,8> alphaPixels{{255,255,255,0,255,255,255,255}};
    TextureOwner alpha{bgfx::createTexture2D(2,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(alphaPixels.data(),static_cast<std::uint32_t>(alphaPixels.size())))};
    check(bgfx::isValid(alpha.handle),"Mask fixture texture allocation failed.");casterTextures[0].handles[0]=alpha.handle;
    casterTextures[0].flags[0]=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
    casterMaterials[0].alphaMask=true;casterMaterials[0].baseColorTexture.texCoord=1;
    const auto masked=capture();check(brightness(masked,settings.camera,center+right*.5)>brightness(masked,settings.camera,center-right*.5)+60,"Masked caster did not preserve its UV1 transparent silhouette.");
    casterMaterials[0].uvScale={-1,1,1};casterMaterials[0].uvOffset={1,0,0};const auto uvMirrored=capture();
    check(submittedStats.renderedFaces==6 && brightness(uvMirrored,settings.camera,center-right*.5)>brightness(uvMirrored,settings.camera,center+right*.5)+60,
        "Shared UV scale/offset did not invalidate and transform masked shadows.");
    casterMaterials[0].uvScale={1,1,1};casterMaterials[0].uvRotation={0,0,180};casterMaterials[0].uvOffset={1,1,0};const auto uvRotated=capture();
    check(submittedStats.renderedFaces==6 && brightness(uvRotated,settings.camera,center-right*.5)>brightness(uvRotated,settings.camera,center+right*.5)+60,
        "Shared UV rotation was absent from shadow alpha sampling.");
    casterMaterials[0].triplanar=true;casterMaterials[0].uvRotation={};casterMaterials[0].uvOffset={.5f,.5f,0};const auto triplanarMask=capture();
    check(submittedStats.renderedFaces==6 && brightness(triplanarMask,settings.camera,center-right*.5)>brightness(triplanarMask,settings.camera,center+right*.5)+60,
        "Triplanar shadows did not use local-space mask coordinates.");
    casterMaterials[0].uvRotation={0,0,180};const auto triplanarRotated=capture();
    check(brightness(triplanarRotated,settings.camera,center+right*.5)>brightness(triplanarRotated,settings.camera,center-right*.5)+60,
        "Triplanar rotation did not change the masked shadow silhouette.");
    casterMaterials[0].uvRotation={};casterMaterials[0].uvOffset={.35f,0,0};const auto localDomain=capture();
    casterMaterials[0].triplanarWorld=true;const auto worldDomain=capture();
    check(submittedStats.renderedFaces==6 && brightness(worldDomain,settings.camera,center+right*.2)>brightness(localDomain,settings.camera,center+right*.2)+60,
        "World triplanar shadow mode did not invalidate and use accumulated world-space mask coordinates.");
    caster.model.transform=math::translation(right*.3)*caster.model.transform;const auto worldMoved=capture();
    check(std::abs(brightness(worldMoved,settings.camera,center+right*.2)-brightness(worldDomain,settings.camera,center+right*.2))<5,
        "World triplanar shadow mask moved with the caster instead of remaining world-anchored.");
    orient(5);casterMaterials[0].triplanarWorld=false;
    casterMaterials[0].triplanar=false;casterMaterials[0].uvOffset={};casterMaterials[0].uvRotation={};
    caster.model.transform=caster.model.transform*math::scaling({-1,1,1});const auto mirrored=capture();
    check(brightness(mirrored,settings.camera,center-right*.5)>brightness(mirrored,settings.camera,center+right*.5)+60,"Mirrored caster lost alpha/culling parity.");
    casterMaterials[0].baseColor[3]=.25f;const auto factor=capture();
    check(brightness(factor,settings.camera,center)>dark+60,"Caster alpha factor was ignored by the mask threshold.");casterMaterials[0].baseColor[3]=1;
    casterTextures[0].handles[0]=BGFX_INVALID_HANDLE;casterMaterials[0].alphaMask=false;orient(5);
    // Sixteen resident slots fill through whole-light updates over several
    // frames. The per-world-frame budget limits updates, not illumination or
    // the eventual set of shadowed lights.
    scene.pointLights=lights;for(auto& light:lights)light.intensity=100.f/16;
    DecodedImage all;unsigned admitted{};
    for(unsigned attempt=0;attempt<8;++attempt) {
        all=capture();admitted+=submittedStats.renderedLights;
        check(submittedStats.activeLights==16&&submittedStats.renderedLights<=4&&submittedStats.renderedFaces<=24,
            "Point-shadow admission exceeded the four-light/twenty-four-face frame budget.");
        if(!submittedStats.pointDeferred)break;
    }
    check(admitted==16&&submittedStats.pointDeferred==0&&submittedStats.pointCriticalDeferred==0,
        "The sixteen resident lights did not all acquire coherent shadows within bounded warmup.");
    check(brightness(all,settings.camera,center)<35,"One or more of the sixteen point lights remained unshadowed.");
    check(renderer.shadowResidentBytes()>=point_shadow::atlasLayout(256,16).bytes(),"Atlas accounting omitted live shadow storage.");
    {
        const auto savedLights=lights;const auto savedSettings=settings;
        const auto savedReceiver=receiver.model.transform,savedCaster=caster.model.transform;
        const auto savedCenter=center,savedRight=right;
        settings.environment.toneMapping=ToneMapping::Linear;
        lights[0].intensity=10;lights[0].specularStrength=0;lights[0].sourceRadius=.7f;
        scene.pointLights=std::span(lights.data(),1);
        DecodedImage seamReference;
        for (const Vec3 diagonal:{Vec3{1,0,1},Vec3{1,1,1},Vec3{0,1,1}}) {
            const auto direction=math::normalized(diagonal);center=direction*4;
            settings.camera.position=direction*-2;settings.camera.target=center;settings.camera.up={0,1,0};settings.camera.fov=45;
            right=settings.camera.right();receiver.model.transform=plane(settings.camera,center,3);
            caster.model.transform=plane(settings.camera,direction*2,.5f);
            lights[0].castShadows=false;const auto clear=capture();lights[0].castShadows=true;seamReference=capture();
            for (const float offset:{-.08f,0.f,.08f}) {
                const auto point=center+right*offset;
                check(linearSample(seamReference,settings.camera,point)<linearSample(clear,settings.camera,point)*.15,
                    "PCSS cube edge/corner leaked light into an opaque umbra.");
            }
            for (const float offset:{-2.1f,2.1f}) {
                const auto point=center+right*offset;
                check(std::abs(linearSample(seamReference,settings.camera,point)-linearSample(clear,settings.camera,point))<.005,
                    "PCSS cube edge/corner leaked a shadow outside emitter/caster support.");
            }
            const auto edge=shadowEdge(seamReference,clear,settings.camera,center,right);
            check(edge.width>.25 && edge.midpoint>.65 && edge.midpoint<1.35,
                "PCSS face remapping lost its continuous soft edge at a cube seam/corner: width="+std::to_string(edge.width));
        }
        // Change atlas packing and fill every neighboring light block with
        // cleared depth. Those lights illuminate no receiver and cast no
        // geometry, so a correctly remapped kernel must reproduce one source.
        for (std::size_t i=1;i<lights.size();++i) {
            lights[i].roles.receiverMask=0;lights[i].roles.shadowCasterMask=0;
            lights[i].roles.ambientReveal=false;
        }
        scene.pointLights=lights;DecodedImage neighbors;
        for (unsigned attempt=0;attempt<8;++attempt) {
            neighbors=capture();if (!submittedStats.pointDeferred) break;
        }
        check(submittedStats.pointDeferred==0 && submittedStats.activeLights==16,
            "PCSS neighbor-tile fixture did not finish all sixteen shadow blocks.");
        check(maximumDifference(neighbors,seamReference)<=1,
            "PCSS sampled unrelated light tiles after atlas layout changed beside a cube seam.");
        lights=savedLights;settings=savedSettings;receiver.model.transform=savedReceiver;caster.model.transform=savedCaster;
        center=savedCenter;right=savedRight;
        for (unsigned attempt=0;attempt<8;++attempt) {capture();if (!submittedStats.pointDeferred) break;}
        check(submittedStats.pointDeferred==0,"PCSS fixture failed to restore the sixteen cached ordinary point lights.");
    }
    // Caster-only edits can retain the previous projection while waiting. Scene
    // and Game share one update decision and cannot each spend another budget.
    caster.model.transform=math::translation(right*.1f)*caster.model.transform;
    unsigned refreshed{};bool observedDeferred{};
    for(unsigned attempt=0;attempt<8;++attempt) {
        shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;
        renderer.render(scene);renderer.renderGame(scene);const auto stats=renderer.shadowStats();
        refreshed+=stats.renderedLights;observedDeferred|=stats.pointDeferred!=0;
        check(stats.renderedLights<=4&&stats.renderedFaces<=24,
            "Scene and Game spent separate point-shadow update budgets in one world frame.");
        check(stats.pointCriticalDeferred==0,"A caster-only edit incorrectly invalidated a coherent light projection.");
        tick();if(!stats.pointDeferred)break;
    }
    check(observedDeferred&&refreshed==16,"Deferred caster updates did not eventually refresh all resident lights.");
    // Every moved light requires a new projection. Equal-distance lights use
    // persistent identity for ties; the highest-ID dominant light is deferred
    // behind the first four and must illuminate without sampling its old tile.
    for(auto& light:lights){light.position=right*.1f;light.intensity=.0001f;}
    lights.back().intensity=100;
    const auto deferredProjection=capture();
    check(submittedStats.pointCriticalDeferred>0&&submittedStats.renderedFaces<=24,
        "Moved projections were not deferred within the shared frame budget.");
    check(brightness(deferredProjection,settings.camera,center)>80,
        "A deferred moved light sampled stale occlusion instead of disabling its old projection.");
    for(unsigned attempt=0;attempt<8&&submittedStats.pointDeferred;++attempt)all=capture();
    check(submittedStats.pointDeferred==0&&brightness(all,settings.camera,center)<35,
        "The deferred moved light did not publish a complete shadow projection.");
    for(auto& light:lights){light.position={};light.intensity=100.f/16;}
    orient(5);for(unsigned attempt=0;attempt<8;++attempt){capture();if(!submittedStats.pointDeferred)break;}
    check(submittedStats.pointDeferred==0,"Restored shadow fixture did not finish its budgeted updates.");
    // A separate saved-scene thumbnail is interleaved between Scene and Game.
    // It has no blocker and must neither overwrite nor invalidate active shadows.
    shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;RenderScene thumbnail=scene;
    thumbnail.shadowWorldId=77;thumbnail.shadowInstances=std::span(&receiver,1);
    renderer.render(scene);check(renderer.renderThumbnail("isolated-point-shadows",1,thumbnail,ImageSize),renderer.thumbnailStatus());renderer.renderGame(scene);
    check(renderer.shadowStats().renderedFaces==0 && renderer.shadowStats().cacheHits==16 && renderer.shadowStats().worldId==scene.shadowWorldId,
        "Scene, thumbnail and Game failed to retain the cached active-world shadow set.");
    const auto dual=read(renderer.gameViewportTexture());check(brightness(dual,settings.camera,center)<35,"Scene/Game sharing corrupted the Game shadows.");
    std::optional<ThumbnailResult> thumbnailImage;
    for(unsigned i=0;i<120 && !thumbnailImage;++i){thumbnailImage=renderer.pollThumbnail(frame);if(!thumbnailImage)tick();}
    check(thumbnailImage && brightness(thumbnailImage->image,settings.camera,center)>brightness(dual,settings.camera,center)+60,
        "Saved-scene thumbnail sampled the active-world blocker instead of its own shadow context.");
    // A broad floor crosses the downward cube face into the four side faces.
    // With no blocker, toggling either light's shadows must not draw a square
    // (or self-shadow stripes) along those face boundaries.
    scene.pointLights=std::span(lights.data(),2);caster.castShadows=false;
    settings.camera.position={10,14,10};settings.camera.target={};settings.camera.up={0,1,0};settings.camera.fov=65;
    const auto floorCamera=point_shadow::faceCamera({},20,3,256);
    lights[0].position={0,2,0};lights[1].position={2,3,1};
    lights[0].intensity=lights[1].intensity=100;
    for (const std::uint16_t resolution:std::array<std::uint16_t,2>{256,1024})
        for (const float slope:{0.f,.3f,-.3f}) for (const bool reflected:{false,true}) {
        const auto tilt=math::rotation(math::axisAngle({0,0,1},slope));
        receiver.model.transform=tilt*plane(floorCamera,{},12);
        if (reflected) receiver.model.transform=receiver.model.transform*math::scaling({-1,1,1});
        scene.shadowResolution=resolution;
        lights[0].castShadows=lights[1].castShadows=false;const auto unshadowedFloor=capture();
        for (unsigned mask:{1u,3u,2u,0u,3u}) {
            lights[0].castShadows=(mask&1)!=0;lights[1].castShadows=(mask&2)!=0;
            const auto shadowedFloor=capture();
            for (float x:{-6.f,-4.f,-2.1f,-1.9f,0.f,1.9f,2.1f,4.f,6.f}) for (float z:{-4.f,0.f,4.f}) {
                const auto point=math::transformPoint(tilt,{x,0,z});
                const auto lit=brightness(unshadowedFloor,settings.camera,point);
                const auto shadow=brightness(shadowedFloor,settings.camera,point);
                check(std::abs(lit-shadow)<4,"Unoccluded floor self-shadowed across a cube face: resolution="+
                    std::to_string(resolution)+", slope="+std::to_string(slope)+", reflected="+std::to_string(reflected)+
                    ", mask="+std::to_string(mask)+", x="+std::to_string(x)+", z="+
                    std::to_string(z)+", clear="+std::to_string(lit)+", shadow="+std::to_string(shadow));
            }
        }
    }
    // Positive source size must not turn the floor itself into a blocker when
    // the larger search/filter crosses face seams on sloped/reflected geometry.
    scene.shadowResolution=256;lights[0].sourceRadius=lights[1].sourceRadius=.7f;
    for(float slope:{-.3f,.3f})for(bool reflected:{false,true}) {
        const auto tilt=math::rotation(math::axisAngle({0,0,1},slope));
        receiver.model.transform=tilt*plane(floorCamera,{},12);
        if(reflected)receiver.model.transform=receiver.model.transform*math::scaling({-1,1,1});
        lights[0].castShadows=lights[1].castShadows=false;const auto clear=capture();
        lights[0].castShadows=lights[1].castShadows=true;const auto soft=capture();
        for(float x:{-4.f,-2.1f,-1.9f,0.f,1.9f,2.1f,4.f})for(float z:{-4.f,0.f,4.f}) {
            const auto point=math::transformPoint(tilt,{x,0,z});
            check(std::abs(brightness(clear,settings.camera,point)-brightness(soft,settings.camera,point))<4,
                "Nonzero Source Radius introduced self-shadowing on an unoccluded sloped/reflected receiver.");
        }
    }
    lights[0].sourceRadius=lights[1].sourceRadius=0;
    // Preserve nearby real occlusion while correcting the receiver plane. Both
    // samples lie on a side face, where a larger global bias could hide it.
    scene.pointLights=std::span(lights.data(),1);lights[0].castShadows=true;
    receiver.model.transform=plane(floorCamera,{},12);caster.castShadows=true;
    caster.model.transform=plane(floorCamera,{4,.2f,0},.3f);
    const auto contactShadow=capture();caster.castShadows=false;const auto contactClear=capture();
    check(brightness(contactShadow,settings.camera,{4.444444f,0,0})<brightness(contactClear,settings.camera,{4.444444f,0,0})*.5,
        "Receiver-plane correction erased a nearby side-face caster shadow.");
    scene.shadowResolution=256;caster.castShadows=true;
    // An occluder behind the camera is absent from visible instances but remains
    // in the independently prepared caster set and must affect visible geometry.
    scene.pointLights=std::span(lights.data(),1);lights[0].intensity=1000;lights[0].position={0,0,10};
    settings.camera.position={0,0,6};settings.camera.target={};center={};right={1,0,0};
    receiver.model.transform=plane(settings.camera,{},3);caster.model.transform=plane(settings.camera,{0,0,8},.2f);
    const auto offscreen=capture();lights[0].castShadows=false;const auto offscreenClear=capture();lights[0].castShadows=true;
    check(brightness(offscreenClear,settings.camera,center)>brightness(offscreen,settings.camera,center)+60,"Off-camera caster did not shadow the visible receiver.");
    // Shader publication is atomic: a failing caster must preserve the entire
    // current renderer+ImGui group and its rendered shadow, not just its handle.
    const auto compileBeforeStartup=shaderCompilerMetrics();
    ShaderReloadSession reload(resources,renderer,tools,config,EmbeddedShaderSourceBaseline);
    check(!reload.pending()&&reload.revision()==0&&shaderCompilerMetrics().compilerInvocations==compileBeforeStartup.compilerInvocations,
        "Matching embedded shader sources queued an unnecessary startup compilation.");
    const auto pump=[&] {const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{45};
        do{tick();reload.pump();}while(reload.pending() && std::chrono::steady_clock::now()<deadline);
        check(!reload.pending(),"Shadow shader reload timed out.");};
    const auto source=config.sourceDirectory/"fs_point_shadow.sc";std::ifstream input(source);
    const std::string original{std::istreambuf_iterator<char>(input),{}};input.close();
    writeFileAtomically(source,original+"\n// Actual source edit after embedded startup.\n");
    const std::array changed{source};reload.changed(changed);
    pump();check(reload.revision()==1,"Valid shadow shader reload failed: "+reload.status());
    const auto healthySignature=renderer.shaderSignature();const auto healthy=capture();
    writeFileAtomically(source,original+"\nINVALID_POINT_SHADOW_SHADER\n");reload.changed(changed);pump();
    check(reload.revision()==1 && renderer.shaderSignature()==healthySignature,"Failed caster reload replaced part of the live program group.");
    check(capture().rgba==healthy.rgba,"Failed caster reload changed the rendered shadow.");
    // The Game view uses one bounded world projection. A focused character map
    // can add detail independently; the Scene view retains four inspection
    // cascades in a separate camera block of the same comparison resource.
    scene.pointLights={};
    DirectionalLight sun;sun.entityId=900;sun.direction={0,0,-1};sun.color={1,1,1};sun.intensity=4;sun.castShadows=true;
    // This is a six-metre gameplay fixture. Keep its bounded coverage tight
    // enough to resolve the 1.5-metre caster with a single 256-pixel map;
    // inherited 100-metre/FOV65 inspection coverage tests a different quality.
    sun.shadowDistance=15;
    scene.directionalLights=std::span(&sun,1);
    settings.camera.position={0,0,6};settings.camera.target={};settings.camera.fov=45;settings.camera.nearPlane=.01f;settings.camera.farPlane=2048;
    receiver.model.transform=plane(settings.camera,{},3);caster.model.transform=plane(settings.camera,{0,0,2},.75f);
    receiver.receiveShadows=true;caster.castShadows=true;casterMaterials[0].alphaMask=false;
    const auto sunShadow=capture();
    check(submittedStats.directionalRendered==1&&submittedStats.directionalCascades==1&&
        submittedStats.directionalWorldProjections==1&&submittedStats.characterProjections==0,
        "Game directional shadows did not use one bounded world projection.");
    const auto sunCached=capture();check(submittedStats.directionalCacheHits==1&&submittedStats.directionalRendered==0,"Static Game directional projection did not remain cached.");
    check(sunCached.rgba==sunShadow.rgba,"Static directional cache changed pixels.");
    sun.castShadows=false;const auto sunClear=capture();sun.castShadows=true;
    check(brightness(sunClear,settings.camera,{})>brightness(sunShadow,settings.camera,{})+60,
        "Directional shadow did not occlude its direct light: clear="+std::to_string(brightness(sunClear,settings.camera,{}))+
        ", shadow="+std::to_string(brightness(sunShadow,settings.camera,{})));
    receiver.receiveShadows=false;const auto sunUnreceived=capture();receiver.receiveShadows=true;
    check(std::abs(brightness(sunClear,settings.camera,{})-brightness(sunUnreceived,settings.camera,{}))<=1,"Directional receiving flag changed unshadowed direct light.");
    const std::array<std::uint64_t,1> focusEntities{caster.entityId};scene.shadowFocusEntities=focusEntities;
    scene.shadowFocusBounds=math::transformBounds(mesh->bounds,caster.model.transform);
    const auto focused=capture();
    check(submittedStats.characterProjections==1&&submittedStats.directionalCascades<=2&&
        submittedStats.directionalWorldProjections<=1,
        "Game character focus did not add exactly one focused projection.");
    check(brightness(focused,settings.camera,{})<brightness(sunClear,settings.camera,{})*.5,
        "Character focus lost the shadow under the selected caster.");
    capture();check(submittedStats.directionalRendered==0&&submittedStats.characterProjections==0,
        "Static world and character projections did not remain cached.");
    caster.model.transform=math::translation({.3f,0,0})*caster.model.transform;
    scene.shadowFocusBounds=math::transformBounds(mesh->bounds,caster.model.transform);
    const auto followedFocus=capture();
    check(submittedStats.directionalWorldProjections==0&&submittedStats.characterProjections==1&&
        submittedStats.directionalCascades==1,
        "A character-only pose edit rewrote the stationary Game world projection.");
    check(brightness(followedFocus,settings.camera,{.3f,0,0})<brightness(sunClear,settings.camera,{})*.5,
        "The focused shadow did not follow the selected character's presented position.");
    // Only the selected character contributes to its focus map. A second
    // overlapping non-character caster draws in the world map once, while the
    // focus map draws the selected caster once. Each changed projection also
    // draws one explicit tile clear: four total depth submissions.
    receiver.castShadows=false;caster.model.transform=math::translation({.05f,0,0})*caster.model.transform;
    scene.shadowFocusBounds=math::transformBounds(mesh->bounds,caster.model.transform);
    auto nonCharacter=caster;nonCharacter.entityId=3;
    nonCharacter.model.transform=math::translation({.1f,0,0})*caster.model.transform;
    std::array<RenderModelInstance,3> focusCasters{receiver,caster,nonCharacter};scene.shadowInstances=focusCasters;
    renderer.renderGame(scene);const auto focusStats=renderer.shadowStats();
    check(focusStats.directionalWorldProjections==1&&focusStats.characterProjections==1&&focusStats.drawCalls==4,
        "The character focus pass included a non-character caster or lost its world projection.");
    read(renderer.gameViewportTexture());receiver.castShadows=true;
    shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;
    renderer.render(scene);
    check(renderer.shadowStats().directionalWorldProjections==4&&renderer.shadowStats().characterProjections==0,
        "The free Scene camera did not retain four world cascades independently of Game focus.");
    auto otherSettings=settings;otherSettings.camera.position={30,0,6};otherSettings.camera.target={30,0,0};
    RenderScene other{otherSettings,{}};other.sceneMode=true;other.instances=scene.instances;other.shadowInstances=shadowInstances;
    other.directionalLights=scene.directionalLights;other.shadowWorldId=scene.shadowWorldId;other.shadowResolution=scene.shadowResolution;
    renderer.renderGame(other);
    const auto isolated=read(renderer.viewportTexture());
    check(brightness(isolated,settings.camera,{})<brightness(sunClear,settings.camera,{})*.5,"Game projection overwrote Scene directional depth.");
    sun.direction={.3f,0,-1};const auto movedSun=capture();check(submittedStats.directionalRendered==1,"Moving a sun failed to invalidate its projections.");
    check(movedSun.rgba!=sunShadow.rgba,"Moving a sun left its old shadow projection.");
    // GPU deformation must agree with the CPU reference through visible color,
    // readable depth, picking, point shadows and the dedicated player silhouette.
    renderer.setGridVisible(false);
    auto animated=quad();animated->id="animated-gpu-parity";animated->draws[0].node=0;
    animated->nodes.resize(3);animated->nodes[0].mesh=0;animated->nodes[0].skin=0;
    animated->skins.resize(1);animated->skins[0].joints={1,2};animated->skins[0].inverseBindMatrices.resize(2);
    auto& animatedPrimitive=animated->primitives[0];animatedPrimitive.hasSkinning=true;animatedPrimitive.skinWeights.resize(4);
    for(auto& weights:animatedPrimitive.skinWeights)for(unsigned i=0;i<8;++i){weights.joints[i]=static_cast<std::uint16_t>(i%2);weights.weights[i]=float(i+1)/36.f;}
    ModelMorphTarget morph;morph.positions={{-.1f,.2f,0},{.2f,.1f,0},{.1f,-.1f,.1f},{-.2f,-.2f,0}};
    morph.normals.assign(4,{.1f,.05f,0});morph.tangents.assign(4,{0,.1f,.05f});animatedPrimitive.morphTargets.push_back(morph);
    auto animatedPose=std::make_shared<ModelPose>();animatedPose->localTransforms.resize(3);animatedPose->worldTransforms.resize(3);
    animatedPose->skinPalettes.resize(3);animatedPose->nodeWeights.resize(3);animatedPose->nodeWeights[0]={.6f};animatedPose->drawTransforms.resize(1);
    animatedPose->skinPalettes[0]={math::translation({.6f,.15f,0})*math::scaling({.85f,1.1f,1}),
        math::translation({.3f,-.1f,.1f})*math::scaling({.7f,.8f,1.2f})};
    auto reference=quad();reference->id="animated-cpu-reference";
    reference->primitives[0]=deformModelPrimitive(*animated,0,0,*animatedPose);
    reference->primitives[0].skinWeights.clear();reference->primitives[0].morphTargets.clear();reference->primitives[0].hasSkinning=false;
    reference->bounds={};for(const auto& vertex:reference->primitives[0].vertices)math::include(reference->bounds,vertex.position);
    animatedPose->bounds=reference->bounds;
    animated->materials[0].doubleSided=true;reference->materials[0].doubleSided=true;
    check(renderer.prepareModel(*animated),renderer.modelStatus());check(renderer.prepareModel(*reference),renderer.modelStatus());
    RenderModelInstance animatedReceiver;animatedReceiver.entityId=700;animatedReceiver.model.asset=animated;animatedReceiver.pose=animatedPose;
    RenderModelInstance referenceReceiver=animatedReceiver;referenceReceiver.model.asset=reference;referenceReceiver.pose.reset();
    SceneSettings animationSettings;animationSettings.camera.position={0,0,6};animationSettings.camera.target={};animationSettings.camera.fov=45;
    animationSettings.environment.enabled=false;animationSettings.environment.ambientVisibility.enabled=true;
    animationSettings.environment.ambientVisibility.darknessFloor=1;
    // Illuminate the two-sided fixture from both directions. Lit parity uses
    // an orientation-preserving nonuniform blend; reflected geometry is tested
    // separately below without depending on the PBR backface convention.
    std::array<PointLight,2> animationLights;auto& animationLight=animationLights[0];
    animationLight.entityId=701;animationLight.position={0,0,4};animationLight.radius=12;animationLight.intensity=60;
    animationLights[1]=animationLight;animationLights[1].entityId=702;animationLights[1].position={0,0,-4};
    CharacterShadowSource animationActorShadow;animationActorShadow.owner=99;
    RenderScene animationScene{animationSettings,{}};animationScene.actorShadows=std::span(&animationActorShadow,1);animationScene.sceneMode=true;animationScene.pointLights=animationLights;
    const auto renderAnimated=[&](const RenderModelInstance& instance){animationScene.instances=std::span(&instance,1);renderer.render(animationScene);return read(renderer.viewportTexture());};
    const auto equivalentAnimation=[&](const DecodedImage& gpu,const DecodedImage& cpu,const char* reason){
        std::size_t changed{};unsigned maximum{};std::uint64_t total{};
        for(std::size_t i=0;i<gpu.rgba.size();++i){const auto error=unsigned(std::abs(int(gpu.rgba[i])-int(cpu.rgba[i])));maximum=std::max(maximum,error);total+=error;if(error>3)++changed;}
        check(changed<32 && total<gpu.rgba.size()/10,reason);
        std::cout<<"GPU deformation parity: max="<<maximum<<", differing components="<<changed<<", total="<<total<<'\n';
    };
    const auto referenceColor=renderAnimated(referenceReceiver);const auto gpuColor=renderAnimated(animatedReceiver);
    if(brightness(referenceColor,animationSettings.camera,(reference->bounds.minimum+reference->bounds.maximum)*.5)<=30 ||
        brightness(gpuColor,animationSettings.camera,(reference->bounds.minimum+reference->bounds.maximum)*.5)<=30) {
    const auto diagnosticCenter=(reference->bounds.minimum+reference->bounds.maximum)*.5;
    const auto diagnosticPixel=math::project(diagnosticCenter,animationSettings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*animationSettings.camera.viewMatrix(),{0,0,ImageSize,ImageSize},ClipDepth::ZeroToOne);
    const auto imageStatistics=[&](const char* name,const DecodedImage& image){
        unsigned maximum{};std::size_t lit{};for(std::size_t pixel=0;pixel<image.rgba.size();pixel+=4){
            const auto value=std::max({image.rgba[pixel],image.rgba[pixel+1],image.rgba[pixel+2]});maximum=std::max(maximum,unsigned(value));if(value>30)++lit;
        }
        std::cout<<"Animation diagnostic "<<name<<": maximum="<<maximum<<", lit pixels="<<lit<<", center brightness="<<brightness(image,animationSettings.camera,diagnosticCenter)<<'\n';
        if(diagnosticPixel){const auto offset=(std::size_t((*diagnosticPixel)[1])*image.width+std::size_t((*diagnosticPixel)[0]))*4;
            std::cout<<"Projected center="<<(*diagnosticPixel)[0]<<','<<(*diagnosticPixel)[1]<<" RGB="<<unsigned(image.rgba[offset])<<','<<unsigned(image.rgba[offset+1])<<','<<unsigned(image.rgba[offset+2])<<'\n';}
    };
    imageStatistics("CPU",referenceColor);imageStatistics("GPU",gpuColor);
    std::cout<<"Reference bounds center="<<diagnosticCenter[0]<<','<<diagnosticCenter[1]<<','<<diagnosticCenter[2]
        <<"; material RGB="<<reference->materials[0].baseColor[0]<<','<<reference->materials[0].baseColor[1]<<','<<reference->materials[0].baseColor[2]
        <<"; metallic="<<reference->materials[0].metallic<<", double sided="<<reference->materials[0].doubleSided
        <<"; drawable="<<renderer.drawable()<<", model status="<<renderer.modelStatus()<<", shader status="<<renderer.shaderStatus()
        <<", active cluster lights="<<renderer.clusteredLightingStats().activeLights<<'\n';
    for(const auto& vertex:reference->primitives[0].vertices)std::cout<<"Reference vertex: P="<<vertex.position[0]<<','<<vertex.position[1]<<','<<vertex.position[2]
        <<" N="<<vertex.normal[0]<<','<<vertex.normal[1]<<','<<vertex.normal[2]<<" tangent W="<<vertex.tangent[3]<<'\n';
    const auto& diagnosticVertices=reference->primitives[0].vertices;
    const auto windingNormal=math::normalized(math::cross(diagnosticVertices[1].position-diagnosticVertices[0].position,diagnosticVertices[2].position-diagnosticVertices[0].position));
    std::cout<<"Reference triangle normal dot view="<<math::dot(windingNormal,-animationSettings.camera.forward())
        <<", shading normal dot view="<<math::dot(diagnosticVertices[0].normal,-animationSettings.camera.forward())<<'\n';
    if(brightness(referenceColor,animationSettings.camera,diagnosticCenter)<=30||brightness(gpuColor,animationSettings.camera,diagnosticCenter)<=30){
        const auto folder=std::filesystem::temp_directory_path()/("crimson-animation-diagnostic-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(folder);
        const auto save=[&](const char* name,const DecodedImage& image){std::vector<unsigned char> encoded;check(lodepng::encode(encoded,image.rgba,image.width,image.height)==0,"Cannot encode animation diagnostic.");
            writeFileAtomically(folder/name,{reinterpret_cast<const char*>(encoded.data()),encoded.size()});};
        save("cpu.png",referenceColor);save("gpu.png",gpuColor);std::cout<<"Animation diagnostics: "<<folder.string()<<'\n';
    }
    }
    check(brightness(referenceColor,animationSettings.camera,(reference->bounds.minimum+reference->bounds.maximum)*.5)>30,"Animated CPU/GPU parity fixture is not visible.");
    check(brightness(gpuColor,animationSettings.camera,(reference->bounds.minimum+reference->bounds.maximum)*.5)>30,"GPU-deformed parity fixture is not visible.");
    equivalentAnimation(gpuColor,referenceColor,"GPU morph/skin color and readable depth disagree with CPU deformation.");
    // An immutable alternate pose must not overwrite a buffer already queued
    // by the other camera in this frame.
    auto alternatePose=std::make_shared<ModelPose>(*animatedPose);for(auto& matrix:alternatePose->skinPalettes[0])matrix=math::translation({.7f,0,0})*matrix;
    auto alternateReference=quad();alternateReference->id="animated-cpu-alternate";alternateReference->primitives[0]=deformModelPrimitive(*animated,0,0,*alternatePose);
    alternateReference->primitives[0].skinWeights.clear();alternateReference->primitives[0].morphTargets.clear();alternateReference->primitives[0].hasSkinning=false;
    alternateReference->materials[0].doubleSided=true;
    alternateReference->bounds={};for(const auto& vertex:alternateReference->primitives[0].vertices)math::include(alternateReference->bounds,vertex.position);alternatePose->bounds=alternateReference->bounds;
    check(renderer.prepareModel(*alternateReference),renderer.modelStatus());auto alternateCpu=referenceReceiver;alternateCpu.model.asset=alternateReference;
    const auto alternateColor=renderAnimated(alternateCpu);auto alternateGpu=animatedReceiver;alternateGpu.pose=alternatePose;
    animationScene.instances=std::span(&alternateGpu,1);renderer.renderGame(animationScene);animationScene.instances=std::span(&animatedReceiver,1);renderer.render(animationScene);
    equivalentAnimation(read(renderer.gameViewportTexture()),alternateColor,"Scene pose overwrote another camera's queued GPU vertices.");
    equivalentAnimation(read(renderer.viewportTexture()),referenceColor,"Game pose overwrote Scene's immutable GPU vertices.");
    const auto centerPoint=(reference->bounds.minimum+reference->bounds.maximum)*.5;
    const auto pickPixel=math::project(centerPoint,animationSettings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*animationSettings.camera.viewMatrix(),{0,0,ImageSize,ImageSize},ClipDepth::ZeroToOne);
    check(bool(pickPixel),"Animated picking fixture has no projected center.");
    check(renderer.requestPick({1,1,700,static_cast<std::uint32_t>((*pickPixel)[0]),static_cast<std::uint32_t>((*pickPixel)[1]),renderer.streamingSignature()}),renderer.pickingStatus());
    renderer.render(animationScene);tick();std::optional<PickResult> animationPick;
    for(unsigned i=0;i<120&&!animationPick;++i){animationPick=renderer.pollPick(frame);if(!animationPick)tick();}
    check(animationPick&&animationPick->entityId==animatedReceiver.entityId,"Picking did not use the animated visible silhouette.");
    auto ground=receiver;ground.model.asset=mesh;ground.model.transform=math::scaling({3,3,1});ground.materials=nullptr;
    animatedReceiver.model.transform=math::translation({0,0,1.5f})*math::scaling({.5f,.5f,.5f});referenceReceiver.model.transform=animatedReceiver.model.transform;
    std::array<RenderModelInstance,2> animationCasters;animationScene.instances=std::span(&ground,1);animationScene.shadowWorldId=700;animationScene.shadowResolution=256;animationScene.pointLights=std::span(&animationLight,1);animationLight.castShadows=true;
    const auto animationShadow=[&](const RenderModelInstance& blocker){animationCasters={ground,blocker};animationScene.shadowInstances=animationCasters;renderer.renderGame(animationScene);return read(renderer.gameViewportTexture());};
    referenceReceiver.castShadows=false;const auto animatedClear=animationShadow(referenceReceiver);referenceReceiver.castShadows=true;
    const auto cpuPoint=animationShadow(referenceReceiver);
    const auto blockerCenter=math::transformPoint(referenceReceiver.model.transform,centerPoint);
    const auto floorSample=Vec3{blockerCenter[0]*4/(4-blockerCenter[2]),blockerCenter[1]*4/(4-blockerCenter[2]),0};
    check(brightness(cpuPoint,animationSettings.camera,floorSample)<brightness(animatedClear,animationSettings.camera,floorSample)*.5,
        "Animated point-shadow parity fixture has no visible ground silhouette.");
    equivalentAnimation(animationShadow(animatedReceiver),cpuPoint,"Point shadow caster did not use GPU-deformed vertices.");
    animatedReceiver.actorOwner=referenceReceiver.actorOwner=99;
    animatedReceiver.lightingCategory=referenceReceiver.lightingCategory=lighting_scope::Character;
    animatedReceiver.lightingScopes.shadowCasterMask=referenceReceiver.lightingScopes.shadowCasterMask=lighting_scope::Character;
    animationLight.castShadows=false;animationActorShadow.enabled=true;animationActorShadow.strength=1;
    animationActorShadow.direction={0,0,-1};animationActorShadow.distance=10;
    const auto cpuCharacter=animationShadow(referenceReceiver);
    check(brightness(cpuCharacter,animationSettings.camera,{blockerCenter[0],blockerCenter[1],0})<brightness(animatedClear,animationSettings.camera,{blockerCenter[0],blockerCenter[1],0})*.5,
        "Animated dedicated-shadow parity fixture has no visible ground silhouette.");
    equivalentAnimation(animationShadow(animatedReceiver),cpuCharacter,"Dedicated player shadow caster did not use GPU-deformed vertices.");
    // A reflected skin blend reverses winding. Test its geometry with Unlit
    // receivers, so the established two-sided PBR NoV rejection cannot turn a
    // correct CPU reference black. Depth, picking and both shadow types still
    // consume this same reflected GPU buffer and are compared independently.
    auto reflectedPose=std::make_shared<ModelPose>(*animatedPose);
    for(auto& matrix:reflectedPose->skinPalettes[0])matrix.at(0,0)=-std::abs(matrix.at(0,0));
    auto reflectedReference=quad();reflectedReference->id="animated-cpu-reflected";
    reflectedReference->primitives[0]=deformModelPrimitive(*animated,0,0,*reflectedPose);
    reflectedReference->materials[0].doubleSided=true;reflectedReference->bounds={};
    for(const auto& vertex:reflectedReference->primitives[0].vertices)math::include(reflectedReference->bounds,vertex.position);
    reflectedPose->bounds=reflectedReference->bounds;check(renderer.prepareModel(*reflectedReference),renderer.modelStatus());
    auto reflectedGpu=animatedReceiver;reflectedGpu.pose=reflectedPose;reflectedGpu.shading=MaterialShading::Unlit;reflectedGpu.model.transform={};
    auto reflectedCpu=referenceReceiver;reflectedCpu.model.asset=reflectedReference;reflectedCpu.shading=MaterialShading::Unlit;reflectedCpu.model.transform={};
    animationScene.shadowWorldId=0;animationScene.shadowInstances={};animationActorShadow={};animationActorShadow.owner=99;animationLight.castShadows=false;
    const auto reflectedCpuColor=renderAnimated(reflectedCpu);const auto reflectedGpuColor=renderAnimated(reflectedGpu);
    const auto reflectedCenter=(reflectedReference->bounds.minimum+reflectedReference->bounds.maximum)*.5;
    check(brightness(reflectedCpuColor,animationSettings.camera,reflectedCenter)>30&&brightness(reflectedGpuColor,animationSettings.camera,reflectedCenter)>30,
        "Reflected CPU/GPU geometry fixture is not visible.");
    equivalentAnimation(reflectedGpuColor,reflectedCpuColor,"Reflected GPU morph/skin geometry or readable depth differs from the CPU reference.");
    const auto reflectedPixel=math::project(reflectedCenter,animationSettings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*animationSettings.camera.viewMatrix(),{0,0,ImageSize,ImageSize},ClipDepth::ZeroToOne);
    check(bool(reflectedPixel),"Reflected picking fixture has no projected center.");
    check(renderer.requestPick({1,1,701,static_cast<std::uint32_t>((*reflectedPixel)[0]),static_cast<std::uint32_t>((*reflectedPixel)[1]),renderer.streamingSignature()}),renderer.pickingStatus());
    renderer.render(animationScene);tick();std::optional<PickResult> reflectedPick;
    for(unsigned i=0;i<120&&!reflectedPick;++i){reflectedPick=renderer.pollPick(frame);if(!reflectedPick)tick();}
    check(reflectedPick&&reflectedPick->entityId==reflectedGpu.entityId,"Picking did not use reflected GPU geometry.");
    reflectedGpu.model.transform=reflectedCpu.model.transform=math::translation({0,0,1.5f})*math::scaling({.5f,.5f,.5f});
    animationScene.instances=std::span(&ground,1);animationScene.shadowWorldId=701;animationLight.castShadows=true;
    reflectedCpu.castShadows=false;const auto reflectedClear=animationShadow(reflectedCpu);reflectedCpu.castShadows=true;
    const auto reflectedCpuPoint=animationShadow(reflectedCpu);const auto reflectedBlocker=math::transformPoint(reflectedCpu.model.transform,reflectedCenter);
    const auto reflectedFloor=Vec3{reflectedBlocker[0]*4/(4-reflectedBlocker[2]),reflectedBlocker[1]*4/(4-reflectedBlocker[2]),0};
    check(brightness(reflectedCpuPoint,animationSettings.camera,reflectedFloor)<brightness(reflectedClear,animationSettings.camera,reflectedFloor)*.5,
        "Reflected point-shadow parity fixture has no visible silhouette.");
    equivalentAnimation(animationShadow(reflectedGpu),reflectedCpuPoint,"Reflected GPU point shadow differs from CPU geometry.");
    animationLight.castShadows=false;animationActorShadow.enabled=true;animationActorShadow.strength=1;
    animationActorShadow.direction={0,0,-1};animationActorShadow.distance=10;
    const auto reflectedCpuKey=animationShadow(reflectedCpu);
    check(brightness(reflectedCpuKey,animationSettings.camera,{reflectedBlocker[0],reflectedBlocker[1],0})<brightness(reflectedClear,animationSettings.camera,{reflectedBlocker[0],reflectedBlocker[1],0})*.5,
        "Reflected dedicated-shadow parity fixture has no visible silhouette.");
    equivalentAnimation(animationShadow(reflectedGpu),reflectedCpuKey,"Reflected GPU dedicated silhouette differs from CPU geometry.");
    resources.stop();frame=renderer.drainPending(frame);
}
}
int main(int argc,char** argv) {
    try {
        check(argc==4,"Pass shaderc, engine shader directory and shader include directory.");Temp temp;
        ShaderCompileConfig config{std::filesystem::path(argv[1]),temp.path/"shaders",temp.path/"includes",temp.path/"cache"};
        std::filesystem::copy(std::filesystem::path(argv[2]),config.sourceDirectory,std::filesystem::copy_options::recursive);
        std::filesystem::copy(std::filesystem::path(argv[3]),config.includeDirectory,std::filesystem::copy_options::recursive);
        SdlOwner sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson point-shadow checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),config);
        std::cout<<"Hidden D3D11 point-shadow faces, masks, flags, budgeted admission/fairness, stale-projection rejection, focused Game/Scene projections, shared views and reload checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
