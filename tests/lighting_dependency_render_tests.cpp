#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "assets/MaterialPreviewGeometry.h"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
constexpr std::uint16_t ReadView=254;
void check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
struct Temporary {
    fs::path path=fs::temp_directory_path()/("crimson-lighting-dependency-"+std::to_string(Clock::now().time_since_epoch().count()));
    Temporary(){fs::create_directories(path);}
    ~Temporary(){std::error_code error;const auto target=fs::weakly_canonical(path,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-lighting-dependency-"))fs::remove_all(target,error);}
};
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
struct Context {ID3D11DeviceContext* value{};~Context(){if(value)value->Release();}};
std::shared_ptr<ModelAsset> quad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="lighting-dependency-quad";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned index=0;index<4;++index){ModelVertex v;v.position=positions[index];v.normal={0,0,1};v.uv0=v.uv1=uv[index];v.tangent={1,0,0,-1};
        primitive.vertices.push_back(v);math::include(asset->bounds,v.position);}
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.baseColor={.35f,.35f,.35f,1};material.metallic=0;material.roughness=.65f;asset->materials.push_back(material);return asset;
}
double brightness(const DecodedImage& image,const Camera& camera,const Vec3& world={}) {
    const auto projected=math::project(world,camera.projectionMatrix(float(image.width)/image.height,ClipDepth::ZeroToOne)*camera.viewMatrix(),
        {0,0,float(image.width),float(image.height)},ClipDepth::ZeroToOne);
    check(bool(projected),"Lighting fixture sample is not projectable.");
    const auto x=static_cast<int>((*projected)[0]),y=static_cast<int>((*projected)[1]);
    check(x>1&&y>1&&x<int(image.width)-2&&y<int(image.height)-2,"Lighting fixture sample escaped its image.");
    double value{};for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)for(unsigned channel=0;channel<3;++channel)
        value+=image.rgba[(std::size_t(y+dy)*image.width+std::size_t(x+dx))*4+channel];return value/27;
}
void equivalent(const DecodedImage& a,const DecodedImage& b,const char* reason,unsigned tolerance=1) {
    check(a.width==b.width&&a.height==b.height&&a.rgba.size()==b.rgba.size(),"Lighting readback sizes differ.");
    unsigned maximum{};for(std::size_t index=0;index<a.rgba.size();++index)if(index%4!=3)
        maximum=std::max(maximum,unsigned(std::abs(int(a.rgba[index])-int(b.rgba[index]))));
    check(maximum<=tolerance,std::string(reason)+" maximum RGB error="+std::to_string(maximum));
}
void exercise(SDL_Window* window,const ShaderCompileConfig& config) {
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(window);MaterialGraphPrograms programs(resources,config);
    std::uint16_t width=129,height=97;renderer.resizeViewport(width,height);renderer.resizeGameViewport(width,height);renderer.setGridVisible(false);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    bgfx::reset(320,240,BGFX_RESET_NONE);
    Context context;static_cast<ID3D11Device*>(bgfx::getInternalData()->context)->GetImmediateContext(&context.value);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x414d4249454e54;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());check(renderer.publishThumbnailEnvironment(environment),renderer.thumbnailStatus());
    check(renderer.publishMaterialPreviewEnvironment(environment),"Material preview environment publication failed.");
    std::uint32_t frame{};const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=bgfx::frame();context.value->Flush();};
    const auto read=[&](bgfx::TextureHandle source) {
        Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Lighting readback allocation failed.");
        auto result=std::make_shared<DecodedImage>(DecodedImage{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)});destinations.push_back(result);
        bgfx::blit(ReadView,staging.handle,0,0,source,0,0,width,height);const auto ready=bgfx::readTexture(staging.handle,result->rgba.data());
        check(ready!=UINT32_MAX,"Lighting readback is unsupported.");
        for(unsigned attempt=0;attempt<120;++attempt){tick();if(picking::frameReady(frame,ready))return *result;}
        throw std::runtime_error("Lighting readback timed out.");
    };
    const auto prepare=[&](const MaterialAsset& material) {
        auto shader=generateMaterialGraphShader(material);
        MaterialGraphProgramResult result;const auto deadline=Clock::now()+std::chrono::seconds{90};
        do{tick();result=programs.request(shader);if(!result.current&&result.diagnostic!="Compiling material graph...")throw std::runtime_error(result.diagnostic);
            if(!result.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(!result.current&&Clock::now()<deadline);
        check(result.current&&bool(result.programs),"Lighting graph compilation timed out.");
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=result.programs;packet->uniforms=shader.uniforms;packet->sources=shader.textures;
        packet->runtimeGraph=shader.runtimeGraph;packet->runtimeParameters=shader.runtimeParameters;packet->dynamicCoverage=shader.dynamicCoverage;
        packet->shading=materialOutputNode(material.graph).output.shading;packet->blend=materialOutputNode(material.graph).output.blend;
        packet->signature=result.programs->signature;return packet;
    };
    const auto mesh=quad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> materials=mesh->materials;std::vector<ModelMaterialTextures> textures(1);
    RenderModelInstance receiver;receiver.entityId=1;receiver.model.asset=mesh;receiver.materials=&materials;receiver.textures=&textures;
    receiver.forceFullDetail=true;receiver.model.transform=math::scaling({5,5,1});
    SceneSettings settings;settings.camera.position={0,0,6};settings.camera.target={};settings.camera.fov=60;settings.camera.fovAxis=CameraFovAxis::Vertical;
    settings.camera.nearPlane=.1f;settings.camera.farPlane=32;settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&receiver,1);scene.shadowResolution=256;
    PointShadowStats submittedShadows;ParticleRenderStats submittedParticles;
    const auto capture=[&] {renderer.renderGame(scene);submittedShadows=renderer.shadowStats();submittedParticles=renderer.gameParticleStats();return read(renderer.gameViewportTexture());};
    const auto center=[&](const DecodedImage& image,const Vec3& point=Vec3{}){return brightness(image,settings.camera,point);};
    PointLight point;point.entityId=100;point.position={0,0,2};point.color={1,1,1};point.intensity=10;point.radius=8;
    point.roles.receiverMask=lighting_scope::World;scene.pointLights=std::span(&point,1);
    receiver.lightingScopes.directReceiverMask=lighting_scope::World;const auto worldLit=capture();
    receiver.lightingScopes.directReceiverMask=lighting_scope::Character;const auto worldExcluded=capture();
    check(center(worldLit)>center(worldExcluded)+40,"Point receiver masks did not exclude world-only light from a character surface.");
    point.roles.receiverMask=lighting_scope::Character;const auto characterLit=capture();
    check(center(characterLit)>center(worldExcluded)+40,"Editing only point light roles did not republish its GPU record.");
    std::cout<<"Point scopes: World="<<center(worldLit)<<", excluded="<<center(worldExcluded)<<", Character="<<center(characterLit)<<'\n';
    point.roles.receiverMask=lighting_scope::All;receiver.lightingScopes.directReceiverMask=lighting_scope::All;
    equivalent(capture(),worldLit,"Default all-surface point scopes changed original lighting");

    // Keep ordinary punctual lights unchanged while bounding the artificial
    // Local World source near architecture. The center pixel lies exactly on
    // the normal-facing plane at these odd image dimensions, so its diffuse
    // radiance has an independent analytic value before final sRGB encoding.
    {
        const ModelMaterial originalMaterial=materials[0];
        PointLight response=point;scene.pointLights=std::span(&response,1);
        check(response.nearFalloffRadius==0&&response.specularStrength==1,
            "Ordinary point lights acquired nondefault Local World response.");
        equivalent(capture(),worldLit,"Explicit ordinary point response changed original lighting");
        response.position={0,0,.15f};response.intensity=4;response.specularStrength=0;
        const auto punctualHotspot=capture();response.nearFalloffRadius=1.5f;
        const auto softenedHotspot=capture();
        check(center(punctualHotspot)>240&&center(softenedHotspot)>30&&
            center(softenedHotspot)<180&&center(punctualHotspot)>center(softenedHotspot)+50,
            "Local World near falloff failed to reduce the close-wall hotspot while preserving illumination.");
        const auto centerPixel=[](const DecodedImage& image) {
            const auto offset=(std::size_t(image.height/2)*image.width+image.width/2)*4;
            return (double(image.rgba[offset])+image.rgba[offset+1]+image.rgba[offset+2])/3;
        };
        const auto diffuseReference=[&](float distance) {
            constexpr double pi=3.14159265358979323846;
            const double relative=double(distance)/response.radius;
            const double window=std::max(1-std::pow(relative,4),0.);
            const double attenuation=window*window/std::max(double(distance)*distance+
                double(response.nearFalloffRadius)*response.nearFalloffRadius,.01);
            // At N=V=L, F is the dielectric F0=.04. Disabling the
            // light's specular response must retain this same diffuse lobe.
            const double linear=.96*originalMaterial.baseColor[0]/pi*response.intensity*attenuation;
            return 255*std::clamp(linear<=.0031308?12.92*linear:1.055*std::pow(linear,1/2.4)-.055,0.,1.);
        };
        check(std::abs(centerPixel(softenedHotspot)-diffuseReference(.15f))<=3,
            "Softened Local World diffuse radiance disagrees with the linear-light reference: actual="+
            std::to_string(centerPixel(softenedHotspot))+", expected="+std::to_string(diffuseReference(.15f)));
        response.nearFalloffRadius=0;response.position={0,0,2};
        const auto punctualDiffuse=capture();
        check(std::abs(centerPixel(punctualDiffuse)-diffuseReference(2))<=3,
            "Zero near falloff failed to retain ordinary inverse-square diffuse radiance.");

        response.position={0,0,1};response.nearFalloffRadius=1.5f;response.intensity=.5f;
        materials[0].metallic=1;materials[0].roughness=.4f;response.specularStrength=1;
        const auto metallicSpecular=capture();response.specularStrength=0;
        const auto metallicNoSpecular=capture();
        check(center(metallicSpecular)>40&&center(metallicNoSpecular)<5,
            "Local World Specular zero failed to suppress only the metallic specular lobe.");
        materials[0]=originalMaterial;const auto dielectricDiffuse=capture();
        check(center(dielectricDiffuse)>25&&std::abs(centerPixel(dielectricDiffuse)-diffuseReference(1))<=3,
            "Local World Specular zero removed or rescaled dielectric diffuse illumination.");
        scene.pointLights=std::span(&point,1);
        equivalent(capture(),worldLit,"Changing Local World response contaminated an ordinary point light");
        std::cout<<"Local World response: near-wall punctual/soft="<<center(punctualHotspot)<<'/'<<center(softenedHotspot)
            <<", analytic dielectric="<<diffuseReference(1)<<", metallic specular/off="<<center(metallicSpecular)<<'/'<<center(metallicNoSpecular)
            <<", retained dielectric diffuse="<<center(dielectricDiffuse)<<'\n';
    }

    // The player source is a finite shadowed point with an independently
    // shaped range window. Lambert diffuse stays intact; source radius can
    // broaden highlights/shadow edges without extending the influence sphere.
    {
        const auto originalReceiver=receiver;const auto originalMaterial=materials[0];
        PointLight local=point;local.position={0,0,2};local.intensity=10;local.radius=8;
        local.nearFalloffRadius=1.5f;local.specularStrength=0;local.rangeFalloffExponent=1;
        local.edgeSoftness=1;local.roles={lighting_scope::World,lighting_scope::World,lighting_scope::World,false,1};
        scene.pointLights=std::span(&local,1);
        const auto progressive=capture();check(center(progressive)>40,"Progressive finite-point fixture rendered no light.");
        const auto reference=[&](double distance) {
            constexpr double pi=3.14159265358979323846;
            const double relative=distance/local.radius;
            const double t=std::clamp((relative-(1-local.edgeSoftness))/local.edgeSoftness,0.,1.);
            const double window=1-std::pow(t,local.rangeFalloffExponent);
            const double softened=std::max(distance*distance+double(local.nearFalloffRadius)*local.nearFalloffRadius,.01);
            const double linear=.96*originalMaterial.baseColor[0]/pi*local.intensity*window*window/
                std::pow(softened,double(local.attenuation)*.5);
            return 255*std::clamp(linear<=.0031308?12.92*linear:1.055*std::pow(linear,1/2.4)-.055,0.,1.);
        };
        const auto pixel=[](const DecodedImage& image) {
            const auto offset=(std::size_t(image.height/2)*image.width+image.width/2)*4;
            return (double(image.rgba[offset])+image.rgba[offset+1]+image.rgba[offset+2])/3;
        };
        for(float distance:{1.f,2.f,4.f,7.f,8.f,8.01f}) {
            local.position[2]=distance;const auto image=capture();
            check(std::abs(pixel(image)-reference(distance))<=3,
                "Finite point diffuse profile disagrees with the independent linear-HDR reference at distance="+std::to_string(distance));
            if(distance>=8)check(center(image)<1,"Finite point lighting escaped its authored radius.");
        }
        for (const float attenuation:{0.f,1.f,2.f,3.f}) {
            local.attenuation=attenuation;
            for (const float distance:{1.f,2.f,4.f,7.f}) {
                local.position[2]=distance;const auto image=capture();
                check(std::abs(pixel(image)-reference(distance))<=3,
                    "Point distance attenuation disagrees with the independent linear-HDR reference: exponent="+
                    std::to_string(attenuation)+", distance="+std::to_string(distance)+", actual="+
                    std::to_string(pixel(image))+", expected="+std::to_string(reference(distance)));
            }
        }
        local.attenuation=2;
        local.position[2]=2;equivalent(capture(),progressive,"Attenuation two failed to restore exact original inverse-square lighting");
        local.position[2]=2;local.rangeFalloffExponent=4;const auto quartic=capture();
        local.rangeFalloffExponent=1;local.edgeSoftness=.25f;const auto narrowEdge=capture();
        check(center(quartic)>center(progressive)+10&&center(narrowEdge)>center(progressive)+10,
            "Range exponent/Edge Softness failed to tune the finite transition.");
        local.edgeSoftness=1;
        local.position={2,0,0};check(center(capture())<1,
            "A light parallel to the wall retained obsolete normal-independent illumination.");
        local.position={0,0,-2};check(center(capture())<1,
            "A point source behind the wall illuminated its front hemisphere.");
        local.position={0,0,2};
        local.radius=4;local.castShadows=false;const auto finiteClear=capture();
        auto finiteCaster=receiver;finiteCaster.entityId=0x46494e495445;
        finiteCaster.model.transform=math::translation({0,0,-3})*math::scaling({.6f,.6f,1});
        scene.shadowInstances=std::span(&finiteCaster,1);scene.shadowWorldId=0x46494e495445;
        local.castShadows=true;equivalent(capture(),finiteClear,"Geometry beyond Radius changed finite-point illumination");
        check(submittedShadows.drawCalls==0,"A caster entirely outside Radius was submitted to the finite point shadow map.");
        finiteCaster.model.transform=math::translation({0,0,1})*math::scaling({.6f,.6f,1});
        const auto finiteBlocked=capture();
        check(center(finiteBlocked)<center(finiteClear)*.35&&submittedShadows.drawCalls>0,
            "An in-range World blocker failed to cast a real finite-point shadow.");
        scene.shadowInstances={};equivalent(capture(),finiteClear,"Removing a finite-point blocker retained its shadow");
        local.castShadows=false;local.radius=8;scene.shadowWorldId=0;
        MaterialAsset localMaterial;localMaterial.id="finite-point-generated";addMaterialNode(localMaterial.graph,MaterialNodeKind::Output);
        auto& output=materialOutputNode(localMaterial.graph).output;
        output.baseColor={.35f,.35f,.35f};output.metallic=0;output.roughness=.65f;
        textures[0].graph=prepare(localMaterial);
        equivalent(capture(),progressive,"Generated finite-point shading differs from fallback");textures[0].graph.reset();
        receiver.blend=MaterialBlend::Alpha;
        equivalent(capture(),progressive,"Transparent finite-point shading differs from opaque");receiver.blend=MaterialBlend::Opaque;
        RenderParticle sprite;sprite.position={};sprite.size=10;sprite.right={1,0,0};sprite.up={0,1,0};
        RenderParticleMaterial spriteMaterial;spriteMaterial.shading=MaterialShading::StandardPbr;spriteMaterial.blend=MaterialBlend::Alpha;
        spriteMaterial.baseColor={.35f,.35f,.35f,1};spriteMaterial.roughness=.65f;
        scene.instances={};scene.particles=std::span(&sprite,1);scene.particleMaterials=std::span(&spriteMaterial,1);
        equivalent(capture(),progressive,"Lit particle finite-point response differs from a mesh");
        scene.instances=std::span(&receiver,1);scene.particles={};scene.particleMaterials={};
        auto& darkness=settings.environment.ambientVisibility;darkness.enabled=true;darkness.shadowAware=false;
        check(center(capture())<1,"Finite player point escaped Ambient Darkness.");
        scene.playerReveal={{},100000,true};equivalent(capture(),progressive,"Full player reveal changed finite-point lighting");
        scene.playerReveal={};darkness={};

        materials[0].metallic=1;materials[0].roughness=.25f;local.intensity=.3f;
        local.specularStrength=1;local.rangeFalloffExponent=4;
        const auto punctualSpecular=capture();local.sourceRadius=1;
        const auto broadSpecular=capture();
        check(center(punctualSpecular)>center(broadSpecular)+35&&
            center(broadSpecular,{.8f,0,0})>center(punctualSpecular,{.8f,0,0})+3,
            "Source Radius did not reduce the specular peak and broaden its highlight.");
        localMaterial.graph.nodes[0].output.metallic=1;localMaterial.graph.nodes[0].output.roughness=.25f;
        textures[0].graph=prepare(localMaterial);equivalent(capture(),broadSpecular,
            "Generated and fallback Source Radius responses disagree");textures[0].graph.reset();
        local.sourceRadius=0;equivalent(capture(),punctualSpecular,"Zero source radius failed to restore the punctual lobe");
        local.specularStrength=0;const auto noSpecular=capture();local.sourceRadius=100;
        equivalent(capture(),noSpecular,"Specular Scale zero retained a soft-source highlight");
        check(center(noSpecular)<1,"Specular Scale zero retained metallic reflected light.");
        materials[0]=originalMaterial;local.intensity=10;local.rangeFalloffExponent=1;
        const auto noSpecularDiffuse=capture();local.sourceRadius=0;
        equivalent(capture(),noSpecularDiffuse,"Source Radius changed diffuse lighting at Specular Scale zero");
        equivalent(noSpecularDiffuse,progressive,"Specular Scale zero removed ordinary dielectric diffuse");
        receiver=originalReceiver;materials[0]=originalMaterial;scene.pointLights=std::span(&point,1);
        equivalent(capture(),worldLit,"Finite player-light response contaminated ordinary point lighting");
        std::cout<<"Finite point profile: progressive/quartic/narrow-edge="<<center(progressive)<<'/'<<center(quartic)<<'/'<<center(narrowEdge)
            <<", specular punctual/soft="<<center(punctualSpecular)<<'/'<<center(broadSpecular)<<'\n';
    }

    DirectionalLight sun;sun.entityId=200;sun.direction={0,0,-1};sun.color={1,1,1};sun.intensity=2;
    sun.roles.receiverMask=lighting_scope::World;scene.pointLights={};scene.directionalLights=std::span(&sun,1);
    receiver.lightingScopes.directReceiverMask=lighting_scope::World;const auto sunLit=capture();
    receiver.lightingScopes.directReceiverMask=lighting_scope::Character;const auto sunExcluded=capture();
    check(center(sunLit)>center(sunExcluded)+40,"Directional receiver scopes did not exclude world-only lighting.");
    sun.roles.receiverMask=lighting_scope::Character;check(center(capture())>center(sunExcluded)+40,"Directional role edits did not bind new scopes.");
    scene.directionalLights={};receiver.lightingScopes={};settings.environment.enabled=true;
    const auto ambientOriginal=capture();check(center(ambientOriginal)>30,"Ambient fixture lacks visible IBL.");
    // Color ambient works without sky lighting. Its source blend is performed
    // in linear HDR; reflections retain the independent environment intensity.
    {
        const auto savedEnvironment=settings.environment;
        const auto savedMaterial=materials[0];
        auto& light=settings.environment.ambientLight;
        light.source=AmbientLightSource::Color;light.color={.5f,.25f,.75f};light.energy=.8f;
        settings.environment.enabled=false;
        const auto colored=capture();
        const auto channel=[](const DecodedImage& image,unsigned component) {
            return double(image.rgba[(std::size_t(image.height/2)*image.width+image.width/2)*4+component]);
        };
        const auto linear=[](double value) {return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);};
        const auto encoded=[](double value) {return 255*(value<=.0031308?12.92*value:1.055*std::pow(value,1/2.4)-.055);};
        for(unsigned component=0;component<3;++component) {
            const double expected=encoded(linear(light.color[component])*light.energy*.96*savedMaterial.baseColor[component]);
            check(std::abs(channel(colored,component)-expected)<=2,"Color ambient disagrees with independent linear-light/sRGB reference.");
        }
        light.skyContribution=.2f;equivalent(capture(),colored,"Color source unexpectedly included sky contribution");
        light.energy=0;check(center(capture())<1,"Zero Color ambient energy left diffuse illumination.");
        light.energy=.8f;settings.environment.enabled=true;
        // One and zero contribution are the exact sky/color endpoints. Energy
        // controls only the custom-color endpoint, following Godot's contract.
        light.source=AmbientLightSource::Sky;light.skyContribution=1;
        const auto sky=capture();equivalent(sky,ambientOriginal,"Default sky ambient changed original diffuse/reflection lighting");
        light.energy=5;equivalent(capture(),sky,"Custom Color Energy changed full-sky ambient or reflections");light.energy=.8f;
        light.skyContribution=0;const auto colorAndReflection=capture();
        light.skyContribution=.5f;const auto blended=capture();
        for(unsigned component=0;component<3;++component) {
            const double expected=encoded((linear(channel(sky,component)/255)+linear(channel(colorAndReflection,component)/255))*.5);
            check(std::abs(channel(blended,component)-expected)<=2,"Ambient sky contribution blended after tone mapping or sRGB encoding.");
        }
        materials[0].metallic=1;
        light.source=AmbientLightSource::Disabled;const auto reflectionOnly=capture();
        check(center(reflectionOnly)>25,"Ambient reflection isolation fixture is dark.");
        light.source=AmbientLightSource::Color;light.color={1,0,1};light.energy=8;
        equivalent(capture(),reflectionOnly,"Color ambient changed metallic sky reflections");
        materials[0]=savedMaterial;light.color={.5f,.25f,.75f};light.energy=.8f;settings.environment.enabled=false;
        MaterialAsset ambientMaterial;ambientMaterial.id="environment-color-ambient";
        addMaterialNode(ambientMaterial.graph,MaterialNodeKind::Output);
        auto& output=materialOutputNode(ambientMaterial.graph).output;
        output.baseColor={.35f,.35f,.35f};output.metallic=0;output.roughness=.65f;
        textures[0].graph=prepare(ambientMaterial);
        equivalent(capture(),colored,"Generated material lost the Environment color ambient binding");
        textures[0].graph.reset();receiver.blend=MaterialBlend::Alpha;
        equivalent(capture(),colored,"Transparent fallback ambient differs from opaque");receiver.blend=MaterialBlend::Opaque;
        RenderParticle sprite;sprite.size=10;sprite.right={1,0,0};sprite.up={0,1,0};
        RenderParticleMaterial spriteMaterial;spriteMaterial.shading=MaterialShading::StandardPbr;
        spriteMaterial.blend=MaterialBlend::Alpha;spriteMaterial.baseColor={.35f,.35f,.35f,1};spriteMaterial.roughness=.65f;
        scene.instances={};scene.particles=std::span(&sprite,1);scene.particleMaterials=std::span(&spriteMaterial,1);
        equivalent(capture(),colored,"Lit particles lost independent Environment color ambient");
        scene.instances=std::span(&receiver,1);scene.particles={};scene.particleMaterials={};
        settings.environment.ambientVisibility.enabled=true;settings.environment.ambientVisibility.shadowAware=false;
        check(center(capture())<5,"Ambient Darkness failed to attenuate custom Color ambient.");
        receiver.lightingScopes.ambientReceiverMask=0;
        equivalent(capture(),colored,"Ambient receiver opt-out failed to preserve custom Color ambient");receiver.lightingScopes={};
        std::cout<<"Environment Ambient Light: Color RGB="<<channel(colored,0)<<'/'<<channel(colored,1)<<'/'<<channel(colored,2)
            <<", sky/color/mix="<<center(sky)<<'/'<<center(colorAndReflection)<<'/'<<center(blended)
            <<", metallic reflections="<<center(reflectionOnly)<<'\n';
        settings.environment=savedEnvironment;materials[0]=savedMaterial;
        equivalent(capture(),ambientOriginal,"Restoring default Environment ambient changed original image");
    }
    auto& ambient=settings.environment.ambientVisibility;ambient.enabled=true;ambient.shadowAware=false;
    const auto ambientDark=capture();check(center(ambientDark)<5,"Ambient visibility did not darken unrevealed IBL.");
    receiver.lightingScopes.ambientReceiverMask=0;
    equivalent(capture(),ambientOriginal,"Ambient receiver None did not exclude an opaque surface from attenuation");
    receiver.lightingScopes.ambientReceiverMask=lighting_scope::All;
    ambient.darknessFloor=.25f;const auto ambientFloor=capture();
    check(center(ambientFloor)>center(ambientDark)+10&&center(ambientFloor)<center(ambientOriginal)-10,"Ambient darkness floor has no graded response.");
    std::cout<<"Ambient response: original="<<center(ambientOriginal)<<", dark="<<center(ambientDark)<<", floor.25="<<center(ambientFloor)<<'\n';
    ambient.darknessFloor=1;equivalent(capture(),ambientOriginal,"Floor one failed exact original IBL/odd-size packing");
    ambient.darknessFloor=0;ambient.strength=0;equivalent(capture(),ambientOriginal,"Strength zero changed original IBL");ambient.strength=1;
    ambient.enabled=false;equivalent(capture(),ambientOriginal,"Disabling ambient visibility failed to restore original visuals");ambient.enabled=true;
    // Darkness attenuates all reflected illumination. A local source can reveal
    // its own contribution, or a separate player radius can reveal that source;
    // neither path depends on the already-attenuated surface lighting result.
    settings.environment.enabled=false;scene.pointLights=std::span(&point,1);ambient.enabled=false;point.roles.ambientReveal=false;
    const auto directReference=capture();ambient.enabled=true;const auto directDark=capture();
    check(center(directReference)>40&&center(directDark)<5,"Unrevealed darkness failed to attenuate direct point lighting.");
    receiver.lightingScopes.ambientReceiverMask=0;
    equivalent(capture(),directReference,"Ambient receiver None did not exempt local reflected illumination");
    receiver.lightingScopes.ambientReceiverMask=lighting_scope::All;
    point.roles.ambientReveal=true;const auto directSelfRevealed=capture();
    check(std::abs(center(directSelfRevealed)-center(directReference))<=1,"A local light failed to reveal its own direct illumination.");
    point.roles.ambientReveal=false;scene.playerReveal={{},100000,true};const auto directPlayerRevealed=capture();
    check(std::abs(center(directPlayerRevealed)-center(directReference))<=1,"Player reveal failed to restore existing local illumination.");
    scene.playerReveal={};
    scene.pointLights={};materials[0].emissive={.2f,.1f,.05f};ambient.enabled=false;
    const auto emissionReference=capture();ambient.enabled=true;
    equivalent(capture(),emissionReference,"Ambient visibility attenuated material emission");
    check(center(emissionReference)>30,"Emission independence fixture is dark.");materials[0].emissive={};
    receiver.shading=MaterialShading::Unlit;ambient.enabled=false;const auto unlitReference=capture();ambient.enabled=true;
    equivalent(capture(),unlitReference,"Ambient visibility attenuated an Unlit material");
    check(center(unlitReference)>40,"Unlit independence fixture is dark.");receiver.shading=MaterialShading::StandardPbr;
    settings.environment.fog.enabled=true;settings.environment.fog.density=.1f;settings.environment.fog.color={.2f,.1f,.05f};
    ambient.enabled=false;const auto fogReference=capture();ambient.enabled=true;
    equivalent(capture(),fogReference,"Ambient visibility attenuated fog radiance");
    check(center(fogReference)>10,"Fog independence fixture is dark.");settings.environment.fog.enabled=false;settings.environment.enabled=true;
    std::cout<<"Direct darkness: original="<<center(directReference)<<", dark="<<center(directDark)
        <<", self revealed="<<center(directSelfRevealed)<<", player revealed="<<center(directPlayerRevealed)
        <<"; independent emission="<<center(emissionReference)
        <<", unlit="<<center(unlitReference)<<", fog="<<center(fogReference)<<'\n';
    ambient.threshold=.05f;ambient.transition=.05f;
    point.color={};point.intensity=1;point.roles.receiverMask=0;point.roles.ambientReveal=true;point.roles.ambientRevealStrength=1;
    point.roles.ambientRevealMask=lighting_scope::World;receiver.lightingScopes.ambientReceiverMask=lighting_scope::World;scene.pointLights=std::span(&point,1);
    const auto revealed=capture();check(center(revealed)>center(ambientDark)+30,"A black reveal-only light did not reveal IBL independently of direct light.");
    point.roles.ambientRevealMask=lighting_scope::Character;check(center(capture())<5,"Ambient light scopes leaked into an excluded world receiver.");
    point.roles.ambientRevealMask=lighting_scope::World;point.roles.ambientReveal=false;check(center(capture())<5,"Ambient reveal enable did not update cached point records.");
    point.roles.ambientReveal=true;point.roles.ambientRevealStrength=0;check(center(capture())<5,"Zero reveal strength still illuminated ambient.");
    point.roles.ambientRevealStrength=1;

    // Direct-light exclusion and shadow-caster eligibility are separate. The
    // invisible Character blocker must still shadow the World receiver.
    RenderModelInstance caster=receiver;caster.entityId=2;caster.model.transform=math::translation({0,0,1.5f})*math::scaling({.6f,.6f,1});
    caster.lightingScopes.directReceiverMask=lighting_scope::Character;caster.lightingScopes.shadowCasterMask=lighting_scope::Character;
    std::array<RenderModelInstance,2> shadowInstances{receiver,caster};scene.shadowInstances=shadowInstances;scene.shadowWorldId=7;
    point.position={0,0,3};point.radius=8;point.castShadows=true;point.roles.shadowCasterMask=lighting_scope::Character;
    ambient.threshold=.01f;ambient.transition=.02f;ambient.shadowAware=false;
    const auto throughWall=capture();ambient.shadowAware=true;const auto blockedReveal=capture();
    check(center(blockedReveal)<center(throughWall)*.5,"Shadow-aware ambient reveal passed through an eligible character blocker.");
    caster.lightingScopes.shadowCasterMask=lighting_scope::World;shadowInstances={receiver,caster};
    const auto excludedCaster=capture();check(center(excludedCaster)>center(blockedReveal)+30,"Caster scope exclusion retained stale ambient shadowing.");
    std::cout<<"Ambient shadows: through-wall="<<center(throughWall)<<", eligible-blocker="<<center(blockedReveal)<<", excluded-blocker="<<center(excludedCaster)<<'\n';
    caster.lightingScopes.shadowCasterMask=lighting_scope::Character;shadowInstances={receiver,caster};
    ambient.enabled=false;settings.environment.enabled=false;point.color={1,1,1};point.intensity=20;point.roles.receiverMask=lighting_scope::World;
    receiver.lightingScopes.directReceiverMask=lighting_scope::World;shadowInstances={receiver,caster};
    const auto directBlocked=capture();caster.lightingScopes.shadowCasterMask=lighting_scope::World;shadowInstances={receiver,caster};
    const auto directClear=capture();check(center(directBlocked)<center(directClear)*.5,"Direct scopes incorrectly removed a separately eligible shadow caster.");
    std::cout<<"Direct caster scopes: blocked="<<center(directBlocked)<<", excluded="<<center(directClear)<<", last faces="<<submittedShadows.renderedFaces<<'\n';

    // The emitter filter is shared by fallback/generated, opaque/transparent
    // receivers and the independent shadow-aware ambient-reveal evaluation.
    // Specular Scale stays zero throughout: these pixels measure real diffuse
    // occlusion, rather than a larger specular highlight hiding a hard edge.
    {
        const auto savedPoint=point;const auto savedReceiver=receiver;
        const auto savedEnvironment=settings.environment;
        const auto savedMaterial=materials[0];const auto savedGraph=textures[0].graph;
        const auto savedShadowInstances=scene.shadowInstances;
        point.position={0,0,3};point.color={1,1,1};point.intensity=20;point.radius=8;
        point.sourceRadius=0;point.specularStrength=0;point.roles={};
        point.roles.receiverMask=point.roles.shadowCasterMask=lighting_scope::World;
        receiver.lightingScopes={};receiver.receiveShadows=true;receiver.blend=MaterialBlend::Opaque;
        textures[0].graph.reset();materials[0]=mesh->materials[0];
        auto softCaster=receiver;softCaster.entityId=0x50435353;softCaster.textures=nullptr;
        softCaster.model.transform=math::translation({0,0,1.5f})*math::scaling({.6f,.6f,1});
        scene.shadowInstances=std::span(&softCaster,1);
        point.castShadows=false;const auto unshadowed=capture();point.castShadows=true;
        const auto hard=capture();point.sourceRadius=.7f;const auto soft=capture();
        const Vec3 innerEdge{1,0,0},outerEdge{1.4f,0,0};
        check(center(soft,innerEdge)>center(hard,innerEdge)+10 &&
            center(soft,outerEdge)<center(unshadowed,outerEdge)-10,
            "Diffuse-only point PCSS fixture has no partially occluded edge on both sides of the hard silhouette.");
        check(submittedShadows.renderedFaces==0 && submittedShadows.cacheHits==1,
            "Receiver-only Source Radius change invalidated the shared point depth map.");
        point.attenuation=1;const auto attenuationEdit=capture();
        check(submittedShadows.renderedFaces==0 && submittedShadows.cacheHits==1 &&
            center(attenuationEdit,{2,0,0})>center(soft,{2,0,0})+20,
            "Attenuation-only edit failed to change radiance while retaining the static PCSS depth map.");
        point.attenuation=2;equivalent(capture(),soft,"Restoring point attenuation changed the cached soft-shadow image");
        MaterialAsset softMaterial;softMaterial.id="point-pcss-shared-receiver";
        addMaterialNode(softMaterial.graph,MaterialNodeKind::Output);
        auto& softOutput=materialOutputNode(softMaterial.graph).output;
        softOutput.baseColor={.35f,.35f,.35f};softOutput.metallic=0;softOutput.roughness=.65f;
        textures[0].graph=prepare(softMaterial);
        equivalent(capture(),soft,"Generated opaque point PCSS disagrees with fallback");textures[0].graph.reset();
        receiver.blend=MaterialBlend::Alpha;
        equivalent(capture(),soft,"Transparent fallback point PCSS disagrees with opaque");
        softOutput.blend=MaterialBlend::Alpha;textures[0].graph=prepare(softMaterial);
        equivalent(capture(),soft,"Generated transparent point PCSS disagrees with opaque");textures[0].graph.reset();
        receiver.blend=MaterialBlend::Opaque;
        RenderParticle softSprite;softSprite.position={};softSprite.size=10;softSprite.right={1,0,0};softSprite.up={0,1,0};
        RenderParticleMaterial softSpriteMaterial;softSpriteMaterial.shading=MaterialShading::StandardPbr;
        softSpriteMaterial.blend=MaterialBlend::Alpha;softSpriteMaterial.baseColor={.35f,.35f,.35f,1};softSpriteMaterial.roughness=.65f;
        scene.instances={};scene.particles=std::span(&softSprite,1);scene.particleMaterials=std::span(&softSpriteMaterial,1);
        equivalent(capture(),soft,"Lit particle point PCSS disagrees with mesh receivers");
        scene.instances=std::span(&receiver,1);scene.particles={};scene.particleMaterials={};

        // A black reveal-only source has no direct RGB contribution. Partial
        // IBL visibility here must therefore come from PCSS inside the compute
        // mask (or the analytic transparent path), independently of highlights.
        settings.environment.enabled=true;point.color={};point.intensity=1;
        point.roles.receiverMask=0;point.roles.ambientReveal=true;
        point.roles.ambientRevealMask=lighting_scope::World;
        ambient={};ambient.enabled=true;ambient.threshold=0;ambient.transition=.1f;ambient.shadowAware=false;
        const auto revealThrough=capture();point.sourceRadius=0;
        equivalent(capture(),revealThrough,"Source Radius changed reveal with shadow awareness disabled");
        ambient.shadowAware=true;const auto revealHard=capture();point.sourceRadius=.7f;const auto revealSoft=capture();
        check(center(revealSoft,innerEdge)>center(revealHard,innerEdge)+8 &&
            center(revealSoft,outerEdge)<center(revealThrough,outerEdge)-8,
            "Shadow-aware ambient reveal ignored the emitter penumbra or lost partial occlusion.");
        receiver.blend=MaterialBlend::Alpha;
        equivalent(capture(),revealSoft,"Transparent analytic PCSS reveal disagrees with the opaque compute mask",2);
        textures[0].graph=prepare(softMaterial);
        equivalent(capture(),revealSoft,"Generated transparent PCSS reveal disagrees with the opaque compute mask",2);
        // Exercise actual non-axis receiving planes and translated light/caster
        // coordinates. The same partial edge must survive normal packing and
        // readable-depth reconstruction, with no relaxed image tolerance.
        for (const float slope:{-.2f,.2f}) {
            const auto world=math::translation({.25f,-.1f,.3f})*math::rotation(math::axisAngle({0,1,0},slope));
            receiver.model.transform=world*math::scaling({5,5,1});
            softCaster.model.transform=world*math::translation({0,0,1.5f})*math::scaling({.6f,.6f,1});
            point.position=math::transformPoint(world,{0,0,3});
            const auto inner=math::transformPoint(world,innerEdge),outer=math::transformPoint(world,outerEdge);
            receiver.blend=MaterialBlend::Opaque;textures[0].graph.reset();
            ambient.shadowAware=false;const auto slopedThrough=capture();ambient.shadowAware=true;
            point.sourceRadius=0;const auto slopedHard=capture();point.sourceRadius=.7f;const auto slopedSoft=capture();
            check(center(slopedSoft,inner)>center(slopedHard,inner)+8 &&
                center(slopedSoft,outer)<center(slopedThrough,outer)-8,
                "Sloped/translated PCSS reveal fixture lost its genuine partial shadow edge: slope="+std::to_string(slope));
            receiver.blend=MaterialBlend::Alpha;const auto slopedTransparent=capture();
            equivalent(slopedTransparent,slopedSoft,"Sloped/translated analytic PCSS reveal disagrees with opaque compute",2);
            textures[0].graph=prepare(softMaterial);const auto slopedGenerated=capture();
            equivalent(slopedGenerated,slopedSoft,"Sloped/translated generated PCSS reveal disagrees with opaque compute",2);
            std::cout<<"PCSS sloped/translated reveal: slope="<<slope<<", hard/opaque/transparent/generated inner="
                <<center(slopedHard,inner)<<'/'<<center(slopedSoft,inner)<<'/'<<center(slopedTransparent,inner)<<'/'
                <<center(slopedGenerated,inner)<<'\n';
        }
        std::cout<<"Diffuse PCSS inner edge hard/soft="<<center(hard,innerEdge)<<'/'<<center(soft,innerEdge)
            <<", outer edge clear/soft="<<center(unshadowed,outerEdge)<<'/'<<center(soft,outerEdge)
            <<"; IBL reveal hard/soft="<<center(revealHard,innerEdge)<<'/'<<center(revealSoft,innerEdge)<<'\n';
        point=savedPoint;receiver=savedReceiver;settings.environment=savedEnvironment;
        materials[0]=savedMaterial;textures[0].graph=savedGraph;scene.shadowInstances=savedShadowInstances;
    }

    // A torch may physically fill the directional silhouette when its caster
    // scope excludes the Character. Including that caster must instead produce
    // the torch's own crisp occlusion, without copying the sun's world shadows.
    // The key is deliberately dim so this checks the dominant local source.
    sun.direction={0,0,-1};sun.intensity=.15f;sun.castShadows=true;sun.roles={};sun.roles.shadowCasterMask=lighting_scope::Character;
    scene.directionalLights=std::span(&sun,1);scene.pointLights={};scene.shadowWorldId=47;
    caster.model.transform=math::translation({0,0,1.5f})*math::scaling({.6f,.6f,1});
    caster.lightingScopes.shadowCasterMask=lighting_scope::Character;shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;
    ambient.enabled=true;ambient.shadowAware=false;scene.playerReveal={{},100000,true};
    const auto directionalCharacterShadow=capture();
    point.roles={};point.roles.receiverMask=lighting_scope::World;point.roles.shadowCasterMask=lighting_scope::World;
    point.color={1,1,1};point.intensity=20;point.position={0,0,3};point.radius=8;point.castShadows=true;
    scene.pointLights=std::span(&point,1);const auto torchExcludesCharacter=capture();
    check(center(torchExcludesCharacter)>center(directionalCharacterShadow)+40,
        "A local light excluding Character casters incorrectly inherited the directional character shadow.");
    point.roles.shadowCasterMask=lighting_scope::All;const auto torchIncludesCharacter=capture();
    check(center(torchIncludesCharacter)<center(torchExcludesCharacter)*.35,
        "Bright torch illumination remained inside its own eligible character shadow: excluded="+
        std::to_string(center(torchExcludesCharacter))+", included="+std::to_string(center(torchIncludesCharacter)));
    check(submittedShadows.renderedLights==1&&submittedShadows.renderedFaces==6&&submittedShadows.pointDeferred==0&&submittedShadows.pointCriticalDeferred==0,
        "Changing the torch caster scope did not refresh its complete shadow map immediately.");
    const Vec3 movedTorchShadow{1.8f,0,0};
    caster.model.transform=math::translation({.9f,0,1.5f})*math::scaling({.6f,.6f,1});shadowInstances={receiver,caster};
    const auto movedCharacterShadow=capture();
    check(center(movedCharacterShadow)>center(torchIncludesCharacter)+40,
        "Moving the Character left a stale shadow at its previous position under a static torch.");
    check(center(movedCharacterShadow,movedTorchShadow)<center(torchIncludesCharacter,movedTorchShadow)*.55,
        "A static torch did not shadow the Character at its updated projected position: before="+
        std::to_string(center(torchIncludesCharacter,movedTorchShadow))+", after="+std::to_string(center(movedCharacterShadow,movedTorchShadow)));
    check(submittedShadows.renderedLights==1&&submittedShadows.renderedFaces==6&&submittedShadows.pointDeferred==0&&submittedShadows.pointCriticalDeferred==0,
        "A single static torch deferred its moving-character shadow update.");
    equivalent(capture(),movedCharacterShadow,"Reusing a static torch shadow changed its retained silhouette");
    check(submittedShadows.renderedLights==0&&submittedShadows.renderedFaces==0&&submittedShadows.cacheHits==1,
        "An unchanged torch and Character failed to reuse their coherent shadow map.");
    std::cout<<"Bright torch shadows: directional only="<<center(directionalCharacterShadow)
        <<", Character excluded="<<center(torchExcludesCharacter)<<", Character included="<<center(torchIncludesCharacter)
        <<", moved old/new="<<center(movedCharacterShadow)<<'/'<<center(movedCharacterShadow,movedTorchShadow)<<'\n';
    scene.directionalLights={};sun.castShadows=false;sun.intensity=2;scene.playerReveal={};
    scene.shadowWorldId=0;scene.shadowInstances={};point.castShadows=false;point.roles.ambientReveal=true;

    // Transparent receivers use their own world position. An opaque plane
    // behind this light's radius is dark and must not supply the foreground's mask.
    ambient.enabled=true;ambient.shadowAware=false;ambient.threshold=.01f;ambient.transition=.02f;settings.environment.enabled=true;
    point.position={0,0,2};point.radius=2;point.color={};point.intensity=1;point.roles.receiverMask=0;
    receiver.model.transform=math::translation({0,0,-3})*math::scaling({5,5,1});
    RenderModelInstance transparent=receiver;transparent.entityId=3;transparent.model.transform=math::translation({0,0,1})*math::scaling({1.5f,1.5f,1});transparent.blend=MaterialBlend::Alpha;
    std::array<RenderModelInstance,2> transparentInstances{receiver,transparent};scene.instances=transparentInstances;
    const auto frontLit=capture();point.roles.ambientReveal=false;const auto frontDark=capture();
    check(center(frontLit,{0,0,1})>center(frontDark,{0,0,1})+30,"Transparent mesh sampled the unrelated opaque-depth ambient mask.");point.roles.ambientReveal=true;
    transparentInstances[1].lightingScopes.ambientReceiverMask=0;point.roles.ambientReveal=false;
    const auto transparentUnrestricted=capture();
    check(std::abs(center(transparentUnrestricted,{0,0,1})-center(ambientOriginal))<=1,
        "Ambient receiver None did not exclude a transparent surface from attenuation.");
    transparentInstances[1].lightingScopes.ambientReceiverMask=lighting_scope::World;point.roles.ambientReveal=true;
    RenderParticle particle;particle.position={0,0,1};particle.size=3;particle.right={1,0,0};particle.up={0,1,0};
    RenderParticleMaterial particleMaterial;particleMaterial.shading=MaterialShading::StandardPbr;particleMaterial.blend=MaterialBlend::Alpha;particleMaterial.baseColor={.35f,.35f,.35f,1};
    scene.instances=std::span(&receiver,1);scene.particles=std::span(&particle,1);scene.particleMaterials=std::span(&particleMaterial,1);
    const auto particleLit=capture();point.roles.ambientReveal=false;const auto particleDark=capture();
    check(center(particleLit,{0,0,1})>center(particleDark,{0,0,1})+30,"Transparent particle reused opaque-depth ambient visibility.");
    std::cout<<"Transparent ambient: mesh="<<center(frontLit,{0,0,1})<<'/'<<center(frontDark,{0,0,1})
        <<", particle="<<center(particleLit,{0,0,1})<<'/'<<center(particleDark,{0,0,1})<<'\n';
    point.roles.ambientReveal=true;scene.particles={};scene.particleMaterials={};

    // A generated material exercises both its native scope filtering and its
    // masked prepass. Time coverage must use one frozen evaluation context.
    MaterialAsset authored;authored.id="lighting-dependency-generated";const auto outputId=addMaterialNode(authored.graph,MaterialNodeKind::Output).id;
    auto& output=materialOutputNode(authored.graph).output;output.baseColor={.35f,.35f,.35f};output.metallic=0;output.roughness=.65f;
    textures[0].graph=prepare(authored);scene.instances=transparentInstances;
    const auto generatedFront=capture();point.roles.ambientReveal=false;const auto generatedDark=capture();
    check(center(generatedFront,{0,0,1})>center(generatedDark,{0,0,1})+30,"Generated material did not apply independent ambient reveal.");
    output.blend=MaterialBlend::Alpha;textures[0].graph=prepare(authored);point.roles.ambientReveal=true;
    const auto generatedTransparent=capture();point.roles.ambientReveal=false;const auto generatedTransparentDark=capture();
    check(center(generatedTransparent,{0,0,1})>center(generatedTransparentDark,{0,0,1})+30,
        "Generated transparent material reused the opaque ambient mask.");
    point.roles.ambientReveal=true;scene.instances=std::span(&receiver,1);receiver.model.transform=math::scaling({5,5,1});point.radius=8;
    output.blend=MaterialBlend::Mask;const auto timeId=addMaterialNode(authored.graph,MaterialNodeKind::Time).id;
    connectMaterialNodes(authored.graph,timeId,MaterialPin::Value,outputId,MaterialPin::OpacityMask);textures[0].graph=prepare(authored);
    scene.materialContext={0,0};const auto hiddenMask=capture();scene.materialContext={1,1};const auto visibleMask=capture();
    check(center(hiddenMask)<5&&center(visibleMask)>30,"Generated masked color/depth coverage did not share the frozen material clock.");
    scene.materialContext={0,0};equivalent(capture(),hiddenMask,"Changing only material time retained stale readable depth or scope coverage");
    const auto timedParticleGraph=textures[0].graph;
    textures[0].graph.reset();scene.materialContext={};receiver.model.transform=math::translation({0,0,-3})*math::scaling({5,5,1});

    // Opaque/masked sprites take the new depth-prepass early-return branch.
    // Counters count the one visible packet once, although both submissions
    // retain the exact same transient instance allocation.
    const auto oneParticle=[&](const char* label) {
        check(submittedParticles.requested==1&&submittedParticles.submitted==1&&submittedParticles.drawCalls==1&&
            submittedParticles.invalid==0&&submittedParticles.dropped==0,
            std::string(label)+": prepass changed visible particle admission/counts: requested="+std::to_string(submittedParticles.requested)+
            ", submitted="+std::to_string(submittedParticles.submitted)+", draws="+std::to_string(submittedParticles.drawCalls));
    };
    particle.position={0,0,1};particle.size=1;particle.rotation=.37f;particleMaterial.graph.reset();particleMaterial.blend=MaterialBlend::Opaque;
    particleMaterial.baseColor[3]=0;scene.instances={};scene.particles=std::span(&particle,1);scene.particleMaterials=std::span(&particleMaterial,1);
    point.position={0,0,-2};point.radius=2;point.roles.ambientReveal=true;
    ambient.enabled=false;const auto opaqueParticleOriginal=capture();oneParticle("Opaque sprite, original");
    check(center(opaqueParticleOriginal,{0,0,1})>30,"Opaque sprite lost visible coverage because its unused opacity is zero.");
    ambient.enabled=true;ambient.darknessFloor=1;equivalent(capture(),opaqueParticleOriginal,"Opaque particle prepass changed neutral visible coverage");
    oneParticle("Opaque sprite, prepassed");

    // Behind a discarded foreground sprite, the opaque wall lies inside the
    // reveal radius. The foreground lies outside it. A masked prepass that
    // writes through its holes would therefore make the visible wall dark.
    ambient.darknessFloor=0;scene.instances=std::span(&receiver,1);scene.particles={};
    const auto backgroundOnly=capture();check(center(backgroundOnly,{0,0,-3})>30,"Particle coverage fixture's revealed wall is dark.");
    scene.particles=std::span(&particle,1);particleMaterial.blend=MaterialBlend::Mask;particleMaterial.alphaCutoff=.5f;
    const auto discardedParticle=capture();oneParticle("Fallback Mask sprite, discarded");
    equivalent(discardedParticle,backgroundOnly,"Masked particle prepass covered discarded visible pixels");
    particleMaterial.baseColor[3]=1;const auto coveredParticle=capture();oneParticle("Fallback Mask sprite, covered");
    check(center(coveredParticle,{0,0,1})<5,"An opaque/masked sprite failed to cover the revealed wall with its own dark surface.");
    check(center(backgroundOnly,{1.4f,0,-3})>30,"Particle coverage fixture lacks a revealed wall sample outside the rotated quad.");
    check(std::abs(center(coveredParticle,{1.4f,0,-3})-center(backgroundOnly,{1.4f,0,-3}))<=1,
        "A masked particle prepass changed wall lighting outside its visible quad.");
    ambient.enabled=false;const auto maskedParticleOriginal=capture();oneParticle("Fallback Mask sprite, original");
    ambient.enabled=true;ambient.darknessFloor=1;equivalent(capture(),maskedParticleOriginal,"Masked particle prepass changed neutral visible coverage");
    oneParticle("Fallback Mask sprite, neutral prepass");

    // Reuse the real generated Time->OpacityMask packet. Its Particle variant
    // must discard in both passes at t=0 and retain the identical quad at t=1.
    particleMaterial.graph=timedParticleGraph;ambient.darknessFloor=0;scene.materialContext={0,0};
    const auto generatedParticleHidden=capture();oneParticle("Generated Mask sprite, t0");
    equivalent(generatedParticleHidden,backgroundOnly,"Generated particle prepass ignored frozen Time mask holes");
    scene.materialContext={1,1};const auto generatedParticleVisible=capture();oneParticle("Generated Mask sprite, t1");
    check(center(generatedParticleVisible,{0,0,1})<5,"Generated masked particle lost its retained t1 visible coverage.");
    ambient.enabled=false;const auto generatedParticleOriginal=capture();oneParticle("Generated Mask sprite, original t1");
    check(center(generatedParticleOriginal,{0,0,1})>30,"Generated masked particle fixture has no retained t1 surface.");
    ambient.enabled=true;ambient.darknessFloor=1;
    equivalent(capture(),generatedParticleOriginal,"Generated masked particle prepass changed neutral t1 pixels");oneParticle("Generated Mask sprite, neutral t1");
    ambient.darknessFloor=0;scene.materialContext={0,0};
    equivalent(capture(),generatedParticleHidden,"Generated masked particle prepass retained stale t1 scope/depth");oneParticle("Generated Mask sprite, restored t0");
    std::cout<<"Particle prepasses: Opaque="<<center(opaqueParticleOriginal,{0,0,1})
        <<", Mask holes="<<center(discardedParticle,{0,0,-3})<<", covered="<<center(coveredParticle,{0,0,1})
        <<", generated t0/t1="<<center(generatedParticleHidden,{0,0,-3})<<'/'<<center(generatedParticleVisible,{0,0,1})
        <<", submitted="<<submittedParticles.submitted<<", draws="<<submittedParticles.drawCalls<<'\n';
    particleMaterial.graph.reset();scene.particles={};scene.particleMaterials={};scene.materialContext={};
    receiver.model.transform=math::scaling({5,5,1});point.position={0,0,2};point.radius=8;

    // Both contexts must retain their own depth, visibility region and settings.
    point.roles.ambientReveal=false;const auto expectedGame=capture();SceneSettings editorSettings=settings;editorSettings.environment.ambientVisibility.darknessFloor=1;
    RenderScene editorScene{editorSettings,{}};editorScene.sceneMode=true;editorScene.instances=std::span(&receiver,1);
    renderer.render(editorScene);const auto expectedEditor=read(renderer.viewportTexture());
    check(center(expectedEditor)>center(expectedGame)+30,"Scene/Game ownership fixture lacks distinguishable ambient responses.");
    renderer.render(editorScene);renderer.renderGame(scene);
    equivalent(read(renderer.gameViewportTexture()),expectedGame,"Scene overwrote Game ambient resources");
    equivalent(read(renderer.viewportTexture()),expectedEditor,"Game overwrote Scene ambient resources");
    // Recreate both views after visibility has been published. Toggle across
    // differently sized odd extents so stale UV dimensions, tail offsets or
    // an old receiver attachment cannot hide behind a constant center sample.
    for(const auto extent:std::array<std::array<std::uint16_t,2>,2>{{{133,101},{65,49}}}) {
        width=extent[0];height=extent[1];renderer.resizeViewport(width,height);renderer.resizeGameViewport(width,height);
        ambient.enabled=false;ambient.darknessFloor=0;const auto resizedOriginal=capture();
        ambient.enabled=true;ambient.darknessFloor=1;equivalent(capture(),resizedOriginal,"Resized enabled visibility changed a neutral response");
        ambient.darknessFloor=0;const auto resizedDark=capture();check(center(resizedDark)<5,"Resized visibility retained an old reveal or viewport extent.");
        ambient.enabled=false;equivalent(capture(),resizedOriginal,"Disabling resized visibility retained stale packed data");
        ambient.enabled=true;ambient.darknessFloor=1;equivalent(capture(),resizedOriginal,"Re-enabling resized visibility reused incompatible attachments");
        std::cout<<"Ambient resize "<<width<<'x'<<height<<": original="<<center(resizedOriginal)<<", dark="<<center(resizedDark)<<'\n';
    }

    // The world key and all local reflected lighting use the same darkness
    // result. A black reveal source exercises the mask without adding light.
    width=129;height=97;renderer.resizeViewport(width,height);renderer.resizeGameViewport(width,height);
    settings.camera.position={0,0,6};settings.camera.target={};settings.environment.enabled=false;ambient={};
    scene.pointLights={};scene.directionalLights={};scene.shadowWorldId=0;scene.shadowInstances={};
    receiver.model.transform=math::scaling({5,5,1});receiver.lightingScopes={};scene.instances=std::span(&receiver,1);
    materials[0]=mesh->materials[0];receiver.lightingScopes.directReceiverMask=lighting_scope::World;
    sun.roles={};scene.directionalLights=std::span(&sun,1);const auto worldKeyOriginal=capture();
    check(center(worldKeyOriginal)>40,"World key fixture has no visible directional lighting.");
    ambient.enabled=true;const auto worldKeyDark=capture();
    check(center(worldKeyDark)<5,"World directional lighting ignored the darkness mask.");
    settings.environment.enabled=true;const auto worldKeyAndIblDark=capture();
    check(center(worldKeyAndIblDark)<5,"Unrevealed World directional plus IBL escaped darkness composition.");
    settings.environment.enabled=false;scene.pointLights=std::span(&point,1);point.position={0,0,2};point.intensity=10;point.radius=8;
    point.color={};point.roles={};point.roles.receiverMask=0;point.roles.ambientReveal=true;
    const auto worldKeyRevealed=capture();
    check(std::abs(center(worldKeyRevealed)-center(worldKeyOriginal))<=1,
        "Local light failed to fully reveal the fixed world key at its center: revealed="+
        std::to_string(center(worldKeyRevealed))+", original="+std::to_string(center(worldKeyOriginal)));
    point.roles.ambientReveal=false;point.roles.receiverMask=lighting_scope::World;point.color={1,1,1};
    const auto unrevealedCombined=capture();check(center(unrevealedCombined)<5,"Unrevealed local plus directional lighting escaped darkness.");
    ambient.enabled=false;const auto combinedReference=capture();ambient.enabled=true;point.roles.ambientReveal=true;
    const auto combinedRevealed=capture();
    check(std::abs(center(combinedRevealed)-center(combinedReference))<=1,"Full reveal changed the sum of local and directional lighting.");
    std::cout<<"Directional darkness: lit="<<center(worldKeyOriginal)<<", dark="<<center(worldKeyDark)
        <<", IBL dark="<<center(worldKeyAndIblDark)<<", revealed="<<center(worldKeyRevealed)
        <<", combined unrevealed/revealed="<<center(unrevealedCombined)<<'/'<<center(combinedRevealed)<<'\n';

    // Generated and fallback PBR share the complete reflected-light darkness
    // composition. Unrelated views must not inherit point lights or reveal.
    MaterialAsset lightingMaterial;lightingMaterial.id="lighting-dependency-composition";addMaterialNode(lightingMaterial.graph,MaterialNodeKind::Output);
    auto& lightingOutput=materialOutputNode(lightingMaterial.graph).output;
    lightingOutput.baseColor={.35f,.35f,.35f};lightingOutput.metallic=0;lightingOutput.roughness=.65f;
    textures[0].graph=prepare(lightingMaterial);equivalent(capture(),combinedRevealed,"Generated and fallback reflected-light composition disagree");
    point.roles.ambientReveal=false;check(center(capture())<5,"Generated local lighting ignored unrevealed darkness.");
    textures[0].graph.reset();scene.pointLights={};scene.directionalLights={};const auto isolatedDark=capture();
    scene.pointLights=std::span(&point,1);point.roles.ambientReveal=true;capture();
    SceneSettings isolatedSettings=settings;RenderScene isolatedScene{isolatedSettings,{}};isolatedScene.sceneMode=true;
    isolatedScene.instances=std::span(&receiver,1);renderer.render(isolatedScene);
    equivalent(read(renderer.viewportTexture()),isolatedDark,"An unrelated Scene view inherited Game lighting or reveal");
    scene.pointLights={};ambient.enabled=false;materials[0].emissive={.2f,.1f,.05f};const auto emissionDefault=capture();
    ambient.enabled=true;equivalent(capture(),emissionDefault,"Default darkness policy changed existing emission");
    ambient.attenuateEmission=true;check(center(capture())<5,"Emission darkness opt-in did not attenuate fallback emission.");
    lightingOutput.emission=materials[0].emissive;textures[0].graph=prepare(lightingMaterial);
    check(center(capture())<5,"Emission darkness opt-in did not attenuate generated emission.");
    ambient.attenuateEmission=false;equivalent(capture(),emissionDefault,"Generated default emission policy disagrees with fallback");
    textures[0].graph.reset();materials[0].emissive={};lightingOutput.emission={};ambient={};

    // Player distance reveal and independently lit islands combine with max,
    // rather than summing light energy or spending a shadowed point-light slot.
    scene.directionalLights=std::span(&sun,1);scene.pointLights={};
    sun.roles={};settings.environment.enabled=true;ambient={};ambient.enabled=true;
    receiver.lightingScopes={lighting_scope::World,lighting_scope::World,lighting_scope::World};
    receiver.model.transform=math::scaling({5,5,1});scene.instances=std::span(&receiver,1);
    scene.playerReveal.position={};scene.playerReveal.spread=.35f;scene.playerReveal.enabled=true;
    const auto playerIsland=capture();
    check(center(playerIsland)>40&&center(playerIsland,{1.25f,0,0})<5,
        "Player distance reveal failed to create a bounded bright area with a dark exterior.");
    scene.playerReveal.enabled=false;const auto playerRevealDisabled=capture();
    check(center(playerRevealDisabled)<5,"Disabling player distance reveal retained a bright area.");
    scene.playerReveal.enabled=true;scene.playerReveal.spread=0;
    equivalent(capture(),playerRevealDisabled,"Zero player reveal spread retained stale reveal influence");
    scene.playerReveal.spread=.35f;point.position={2.5f,0,.4f};point.radius=.8f;point.intensity=10;point.color={};
    point.roles={};point.roles.receiverMask=0;point.roles.ambientReveal=true;scene.pointLights=std::span(&point,1);
    const auto separateIslands=capture();
    check(center(separateIslands)>40&&center(separateIslands,{2.5f,0,0})>40&&center(separateIslands,{1.25f,0,0})<5,
        "A distant torch reveal failed to remain an independent bright island separated from the player.");
    scene.playerReveal.enabled=false;const auto torchOnlyIsland=capture();
    check(center(torchOnlyIsland)<5&&center(torchOnlyIsland,{2.5f,0,0})>40,
        "Disabling player reveal removed independent torch reveal or left the player area visible.");
    scene.pointLights={};scene.directionalLights={};scene.playerReveal.position={0,0,1};scene.playerReveal.enabled=true;
    receiver.model.transform=math::translation({0,0,-3})*math::scaling({5,5,1});
    transparent=receiver;transparent.entityId=3;transparent.model.transform=math::translation({0,0,1})*math::scaling({1.5f,1.5f,1});
    transparent.blend=MaterialBlend::Alpha;transparentInstances={receiver,transparent};scene.instances=transparentInstances;
    const auto playerTransparentLit=capture();scene.playerReveal.enabled=false;const auto playerTransparentDark=capture();
    check(center(playerTransparentLit,{0,0,1})>40&&center(playerTransparentDark,{0,0,1})<5,
        "A transparent surface sampled an unrelated opaque player-reveal position.");
    scene.instances=std::span(&receiver,1);particle.position={0,0,1};particle.size=3;particleMaterial.graph.reset();
    particleMaterial.baseColor={.35f,.35f,.35f,1};particleMaterial.blend=MaterialBlend::Alpha;
    scene.particles=std::span(&particle,1);scene.particleMaterials=std::span(&particleMaterial,1);scene.playerReveal.enabled=true;
    const auto playerParticleLit=capture();scene.playerReveal.enabled=false;const auto playerParticleDark=capture();
    check(center(playerParticleLit,{0,0,1})>40&&center(playerParticleDark,{0,0,1})<5,
        "A transparent particle failed to evaluate the player reveal at its own position.");
    std::cout<<"Reveal islands: player="<<center(playerIsland)<<", distant torch="<<center(separateIslands,{2.5f,0,0})
        <<", between="<<center(separateIslands,{1.25f,0,0})<<", transparent="<<center(playerTransparentLit,{0,0,1})
        <<", particle="<<center(playerParticleLit,{0,0,1})<<'\n';

    // Character readability, local World illumination and the player silhouette
    // are independent packets. No world point/directional source is necessary
    // for the character-only rig; ordinary world blockers cannot occlude it.
    ambient={};settings.environment.enabled=false;settings.environment.fog.enabled=false;
    scene.playerReveal={};scene.characterShadow={};scene.characterLights={};
    scene.pointLights={};scene.directionalLights={};scene.particles={};scene.particleMaterials={};
    scene.shadowWorldId=81;scene.shadowResolution=256;scene.materialContext={};
    receiver.model.transform=math::scaling({5,5,1});receiver.lightingScopes={};receiver.receiveShadows=true;
    scene.instances=std::span(&receiver,1);textures[0].graph.reset();materials[0]=mesh->materials[0];
    caster=receiver;caster.entityId=2;caster.model.transform=math::translation({0,0,1.5f})*math::scaling({.6f,.6f,1});
    caster.lightingScopes={lighting_scope::World,lighting_scope::World,lighting_scope::World};
    shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;
    auto& characterKey=scene.characterLights[0];characterKey.enabled=true;characterKey.direction={0,0,-1};
    characterKey.color={1,1,1};characterKey.intensity=2;characterKey.diffuse=1;characterKey.specular=1;
    receiver.lightingScopes.directReceiverMask=lighting_scope::World;
    const auto characterRigWorld=capture();
    receiver.lightingScopes.directReceiverMask=lighting_scope::All;
    equivalent(capture(),characterRigWorld,"Raw All mesh default incorrectly received Character-only lighting");
    receiver.lightingCategory=lighting_scope::Character;receiver.lightingScopes.directReceiverMask=lighting_scope::Character;
    const auto separatedCharacter=capture();
    check(center(separatedCharacter)>40&&center(characterRigWorld)<5,"Character-only Key leaked into World or failed to illuminate Character.");
    scene.shadowInstances={};equivalent(capture(),separatedCharacter,"World geometry occluded unshadowed Character-only lighting");
    scene.shadowInstances=shadowInstances;
    point.roles={};point.roles.receiverMask=lighting_scope::World;point.roles.shadowCasterMask=lighting_scope::World;
    point.position={0,0,3};point.intensity=20;point.radius=8;point.color={1,1,1};point.castShadows=true;scene.pointLights=std::span(&point,1);
    equivalent(capture(),separatedCharacter,"Local World source illuminated or occluded Character-only lighting");
    point.nearFalloffRadius=1.5f;point.specularStrength=0;
    equivalent(capture(),separatedCharacter,"Softened diffuse-only Local World source affected Character-only Key lighting");
    point.rangeFalloffExponent=1;point.edgeSoftness=.5f;point.sourceRadius=2;
    equivalent(capture(),separatedCharacter,"Finite player response illuminated or occluded Character-only Key lighting");
    point.nearFalloffRadius=0;point.specularStrength=1;point.rangeFalloffExponent=4;point.edgeSoftness=1;point.sourceRadius=0;
    characterKey.diffuse=0;characterKey.specular=0;
    check(center(capture())<5,"Disabled diffuse/specular Character responses retained illumination.");
    characterKey.diffuse=1;characterKey.specular=0;const auto characterDiffuse=capture();
    characterKey.diffuse=0;characterKey.specular=1;const auto characterSpecular=capture();
    check(center(characterDiffuse)>30&&center(characterSpecular)>2,"Character diffuse/specular controls failed to independently contribute.");
    characterKey.diffuse=characterKey.specular=1;
    ambient.enabled=true;const auto characterDark=capture();check(center(characterDark)<5,"Character-only lighting escaped Ambient Darkness.");
    scene.playerReveal={{},100000,true};equivalent(capture(),separatedCharacter,"Full reveal failed to restore Character-only lighting");
    scene.playerReveal={};ambient={};scene.pointLights={};

    // World point light and IBL are independent from the character-only rig.
    // The dedicated player silhouette applies to their reflected sum, so even
    // a bright unshadowed torch cannot fill it. World pillars never cast here.
    receiver.lightingCategory=lighting_scope::World;receiver.lightingScopes={lighting_scope::World,lighting_scope::World,lighting_scope::World};
    point.castShadows=false;scene.pointLights=std::span(&point,1);const auto localWorldClear=capture();
    characterKey.intensity=20;equivalent(capture(),localWorldClear,"Character intensity changed World architecture illumination");
    characterKey.enabled=false;equivalent(capture(),localWorldClear,"Disabling Character-only rig changed World illumination");
    ambient.enabled=true;check(center(capture())<5,"Local World illumination escaped Ambient Darkness.");ambient={};
    caster.lightingCategory=lighting_scope::Character;caster.lightingScopes={lighting_scope::Character,lighting_scope::Character,lighting_scope::Character};
    shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;
    const std::array<std::uint64_t,1> characterIds{caster.entityId};scene.shadowFocusEntities=characterIds;
    scene.shadowFocusBounds=math::transformBounds(mesh->bounds,caster.model.transform);
    scene.characterShadow.position={0,0,1.5f};scene.characterShadow.direction={0,0,-1};
    scene.characterShadow.strength=1;scene.characterShadow.distance=10;scene.characterShadow.enabled=true;
    const auto dedicatedSilhouette=capture();
    check(center(dedicatedSilhouette)<center(localWorldClear)*.35,"Dedicated Character silhouette failed without an authored directional light or ordinary point shadows.");
    check(submittedShadows.dedicatedCharacterUpdates==1,"Dedicated Character shadow did not render its independent atlas tile.");
    equivalent(capture(),dedicatedSilhouette,"Retained Character shadow changed its silhouette");
    check(submittedShadows.dedicatedCharacterUpdates==0&&submittedShadows.dedicatedCharacterCacheHits==1,
        "An unchanged dedicated Character projection failed cache reuse.");
    caster.lightingCategory=lighting_scope::World;caster.lightingScopes.shadowCasterMask=lighting_scope::All;shadowInstances={receiver,caster};
    equivalent(capture(),localWorldClear,"World-category pillar with All caster mask cast a dedicated Character shadow");
    check(submittedShadows.dedicatedCharacterUpdates==0,"World-only pillars submitted an empty dedicated Character shadow update.");
    caster.lightingCategory=lighting_scope::Character;caster.lightingScopes.shadowCasterMask=lighting_scope::World;shadowInstances={receiver,caster};
    equivalent(capture(),localWorldClear,"World-only caster mask cast a dedicated Character shadow");
    check(submittedShadows.dedicatedCharacterUpdates==0,"Excluded Character caster mask submitted an empty dedicated shadow update.");
    caster.lightingScopes.shadowCasterMask=lighting_scope::Character;caster.blend=MaterialBlend::Alpha;shadowInstances={receiver,caster};
    equivalent(capture(),localWorldClear,"Translucent-only Character caster produced a dedicated silhouette");
    check(submittedShadows.dedicatedCharacterUpdates==0,"Translucent-only Character geometry submitted an empty dedicated shadow update.");
    caster.blend=MaterialBlend::Opaque;
    caster.lightingScopes.shadowCasterMask=lighting_scope::Character;shadowInstances={receiver,caster};
    point.intensity=100;scene.characterShadow.enabled=false;const auto brightTorchClear=capture();
    scene.characterShadow.enabled=true;const auto brightTorchSilhouette=capture();
    check(center(brightTorchClear)>center(localWorldClear)+15&&center(brightTorchSilhouette)<center(brightTorchClear)*.35,
        "Bright torch filled the dedicated Character silhouette.");
    {
        // Response shaping changes the World light's radiance, never the Key
        // projection or which architecture/Character meshes cast each shadow.
        const auto originalLight=point;const auto originalCaster=caster;
        const auto originalFocus=scene.shadowFocusBounds;const auto originalCharacterShadow=scene.characterShadow;
        point.nearFalloffRadius=1.5f;point.specularStrength=0;point.rangeFalloffExponent=1;point.edgeSoftness=1;point.intensity=20;point.radius=8;
        const auto softKeySilhouette=capture();
        check(submittedShadows.dedicatedCharacterUpdates==0&&submittedShadows.dedicatedCharacterCacheHits==1,
            "Changing Local World response unnecessarily rebuilt the unchanged Key silhouette.");
        scene.characterShadow.enabled=false;const auto softWorldClear=capture();
        check(center(softWorldClear)>40&&center(softKeySilhouette)<center(softWorldClear)*.35,
            "Local World point lost the independent Character Key silhouette.");
        caster.lightingCategory=lighting_scope::World;caster.lightingScopes.shadowCasterMask=lighting_scope::World;
        shadowInstances={receiver,caster};point.castShadows=true;
        const auto softWorldPillarShadow=capture();
        check(center(softWorldPillarShadow)<center(softWorldClear)*.4,
            "Local World point stopped World architecture casting shadows.");
        caster.lightingCategory=lighting_scope::Character;caster.lightingScopes.shadowCasterMask=lighting_scope::Character;
        shadowInstances={receiver,caster};
        equivalent(capture(),softWorldClear,"Local World point source acquired Character casters");
        point.castShadows=false;scene.characterShadow.enabled=true;
        equivalent(capture(),softKeySilhouette,"Restoring Character Key silhouette changed Local World point");
        caster.model.transform=math::translation({.9f,0,1.5f})*math::scaling({.6f,.6f,1});
        shadowInstances={receiver,caster};scene.shadowFocusBounds=math::transformBounds(mesh->bounds,caster.model.transform);
        scene.characterShadow.position={.9f,0,1.5f};point.position[0]=.9f;
        const auto movedLocalShadow=capture();
        check(center(movedLocalShadow)>center(softKeySilhouette)+40&&
            center(movedLocalShadow,{.9f,0,0})<center(softWorldClear,{.9f,0,0})*.35&&
            submittedShadows.dedicatedCharacterUpdates==1,
            "Moving Local World point and Character retained the old Key silhouette or lost its new projection.");
        caster=originalCaster;shadowInstances={receiver,caster};scene.shadowFocusBounds=originalFocus;
        scene.characterShadow=originalCharacterShadow;point.position=originalLight.position;
        equivalent(capture(),softKeySilhouette,"Restoring the player changed Local World point or its Key silhouette");
        ambient.enabled=true;check(center(capture(),{1.25f,0,0})<5,
            "Local World point escaped Ambient Darkness.");
        scene.playerReveal={{},100000,true};equivalent(capture(),softKeySilhouette,
            "Full reveal changed Local World point or the Character Key silhouette");
        scene.playerReveal={};ambient={};point=originalLight;
        equivalent(capture(),brightTorchSilhouette,"Local World response edits changed ordinary torch or Key shadow rendering");
        std::cout<<"Local World point shadows: clear/Key/pillar="<<center(softWorldClear)<<'/'<<center(softKeySilhouette)
            <<'/'<<center(softWorldPillarShadow)<<", moved old/new="<<center(movedLocalShadow)<<'/'<<center(movedLocalShadow,{.9f,0,0})<<'\n';
    }
    receiver.lightingCategory=lighting_scope::Character;receiver.lightingScopes.directReceiverMask=lighting_scope::Character;
    characterKey.enabled=true;characterKey.intensity=2;const auto characterWithoutSelfShadow=capture();
    scene.characterShadow.enabled=false;equivalent(capture(),characterWithoutSelfShadow,"Dedicated World silhouette attenuated Character readability");
    scene.characterShadow.enabled=true;characterKey.enabled=false;receiver.lightingCategory=lighting_scope::World;receiver.lightingScopes.directReceiverMask=lighting_scope::World;
    receiver.receiveShadows=false;equivalent(capture(),brightTorchClear,"Receive Shadows off retained the dedicated Character silhouette");receiver.receiveShadows=true;
    scene.characterShadow.strength=0;equivalent(capture(),brightTorchClear,"Character shadow strength zero changed reflected lighting");
    scene.characterShadow.strength=1;scene.characterShadow.enabled=false;
    equivalent(capture(),brightTorchClear,"Disabling dedicated Character shadow retained stale attenuation");scene.characterShadow.enabled=true;
    ambient.enabled=true;check(center(capture(),{1.25f,0,0})<5,"Ambient Darkness failed to attenuate the dedicated-shadow World result.");
    scene.playerReveal={{},100000,true};equivalent(capture(),brightTorchSilhouette,"Full reveal changed dedicated Character shadow composition");
    scene.playerReveal={};ambient={};
    settings.environment.enabled=true;scene.pointLights={};receiver.lightingScopes.directReceiverMask=0;
    scene.characterShadow.enabled=false;const auto shadowIblClear=capture();
    scene.characterShadow.enabled=true;check(center(capture())<center(shadowIblClear)*.35,"Dedicated Character silhouette failed to attenuate IBL on a World surface with direct lighting disabled.");
    receiver.lightingScopes.directReceiverMask=lighting_scope::World;
    settings.environment.enabled=false;scene.pointLights=std::span(&point,1);

    // Generated color, the shadow caster pass, transparency and Lit particles
    // must evaluate this same shadow at their own position and frozen clock.
    textures[0].graph=prepare(lightingMaterial);equivalent(capture(),brightTorchSilhouette,"Generated and fallback dedicated Character shadows disagree");textures[0].graph.reset();
    std::vector<ModelMaterialTextures> casterTextures(1);casterTextures[0].graph=timedParticleGraph;caster.textures=&casterTextures;caster.blend=MaterialBlend::Mask;
    shadowInstances={receiver,caster};scene.materialContext={0,0};equivalent(capture(),brightTorchClear,"Discarded generated Character caster retained silhouette coverage");
    scene.materialContext={1,1};equivalent(capture(),brightTorchSilhouette,"Visible generated Character caster disagreed with fallback coverage");
    scene.materialContext={0,0};equivalent(capture(),brightTorchClear,"Changing caster time retained a stale dedicated silhouette");
    scene.materialContext={};caster.textures=&textures;caster.blend=MaterialBlend::Opaque;shadowInstances={receiver,caster};
    receiver.blend=MaterialBlend::Alpha;const auto transparentSilhouette=capture();
    check(center(transparentSilhouette)<center(brightTorchClear)*.35,"Transparent World receiver lost dedicated Character shadowing.");receiver.blend=MaterialBlend::Opaque;
    transparent=receiver;transparent.entityId=3;transparent.model.transform=math::translation({0,0,2})*math::scaling({.5f,.5f,1});transparent.blend=MaterialBlend::Alpha;
    transparentInstances={receiver,transparent};scene.instances=transparentInstances;
    const auto transparentAheadOfCaster=capture();
    check(center(transparentAheadOfCaster,{0,0,2})>40,"Transparent foreground inherited the shadow of the opaque receiver behind it.");
    particle.position={};particle.size=3;particle.right={1,0,0};particle.up={0,1,0};particle.rotation=0;
    particleMaterial.graph.reset();particleMaterial.shading=MaterialShading::StandardPbr;particleMaterial.blend=MaterialBlend::Alpha;
    particleMaterial.baseColor={.35f,.35f,.35f,1};scene.instances={};scene.particles=std::span(&particle,1);scene.particleMaterials=std::span(&particleMaterial,1);
    scene.characterShadow.enabled=false;const auto particleShadowClear=capture();scene.characterShadow.enabled=true;const auto particleSilhouette=capture();
    check(center(particleShadowClear)>30&&center(particleSilhouette)<center(particleShadowClear)*.35,"Lit sprite did not receive the dedicated shadow at its own surface.");
    scene.instances=std::span(&receiver,1);particle.position={0,0,2};particle.size=1;
    const auto particleAheadOfCaster=capture();
    check(center(particleAheadOfCaster,{0,0,2})>40,"Lit foreground sprite inherited an opaque receiver's dedicated shadow.");
    scene.instances=std::span(&receiver,1);scene.particles={};scene.particleMaterials={};

    // Moving the player replaces the old projection immediately. Independent
    // Scene/Game packets reset both character uniforms and shadow bindings.
    caster.model.transform=math::translation({.9f,0,1.5f})*math::scaling({.6f,.6f,1});shadowInstances={receiver,caster};
    scene.shadowFocusBounds=math::transformBounds(mesh->bounds,caster.model.transform);scene.characterShadow.position={.9f,0,1.5f};
    const auto movedDedicated=capture();
    check(center(movedDedicated)>center(brightTorchSilhouette)+40&&center(movedDedicated,{.9f,0,0})<center(brightTorchClear,{.9f,0,0})*.35,
        "Moving Character retained its old dedicated shadow or failed to project at the new position.");
    check(submittedShadows.dedicatedCharacterUpdates==1,"Moving Character failed an immediate dedicated shadow update.");
    RenderScene noCharacterView{settings,{}};noCharacterView.sceneMode=true;noCharacterView.instances=std::span(&receiver,1);noCharacterView.pointLights=std::span(&point,1);
    renderer.render(noCharacterView);equivalent(read(renderer.viewportTexture()),brightTorchClear,"Scene view inherited Game Character lighting/shadow bindings");
    renderer.render(noCharacterView);renderer.renderGame(scene);
    equivalent(read(renderer.gameViewportTexture()),movedDedicated,"Rendering Scene changed Game dedicated Character projection");
    equivalent(read(renderer.viewportTexture()),brightTorchClear,"Rendering Game changed an unrelated Scene's shadow policy");
    renderer.resizeMaterialPreview(width,height);settings.environment.enabled=true;
    const auto previewGeometry=materialPreviewGeometry(MaterialPreviewMesh::Sphere);
    const auto drawIsolatedPreview=[&] {
        renderer.renderMaterialPreview(settings,lightingMaterial,textures[0],MaterialPreviewMesh::Sphere,Mat4{},false);
    };
    // The preview queues its own streamed sphere on first submission. Complete
    // that upload before comparing rendered lighting rather than reading the
    // deliberately empty first frame while the geometry is still pending.
    for(unsigned attempt=0;attempt<120&&!renderer.modelLodReady(previewGeometry->id,1,0,0);++attempt) {
        drawIsolatedPreview();tick();
    }
    check(renderer.modelLodReady(previewGeometry->id,1,0,0),"Material preview isolation geometry did not become resident.");
    drawIsolatedPreview();
    const auto previewWithoutRig=read(renderer.materialPreviewTexture());
    check(center(previewWithoutRig)>20,"Material preview isolation fixture has no visible illumination.");
    characterKey.enabled=true;characterKey.intensity=20;capture();
    drawIsolatedPreview();
    equivalent(read(renderer.materialPreviewTexture()),previewWithoutRig,"Material preview inherited Game Character lights or silhouette bindings");
    settings.environment.enabled=false;
    std::cout<<"Separated lighting: Character="<<center(separatedCharacter)<<", World="<<center(characterRigWorld)
        <<", diffuse/specular="<<center(characterDiffuse)<<'/'<<center(characterSpecular)<<", Character dark="<<center(characterDark)
        <<"; World clear/shadow="<<center(localWorldClear)<<'/'<<center(dedicatedSilhouette)
        <<", bright torch clear/shadow="<<center(brightTorchClear)<<'/'<<center(brightTorchSilhouette)
        <<", moved old/new="<<center(movedDedicated)<<'/'<<center(movedDedicated,{.9f,0,0})
        <<", transparent/sprite="<<center(transparentSilhouette)<<'/'<<center(particleSilhouette)<<'\n';

    resources.stop();renderer.drainPending(frame);
    std::cout<<"Lighting dependency checks passed: scopes, reveal/casters, bright torch and moving Character shadows, reflected-light darkness, opt-in emission, directional darkness, odd dimensions, transparent receivers, generated/time masks, separated Character/World lighting, dedicated Character shadows and Scene/Game ownership.\n";
}
}
int main(int argc,char** argv) {
    try {check(argc==4,"Pass shaderc, shader source and shader include directories.");Temporary temporary;
        ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary.path};Sdl sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson lighting dependency checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),config);return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
