#pragma once

#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace crimson::fog_aerial_fixture {
inline void require(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
inline Vec3 displayCodes(Vec3 linear) {
    for(auto& channel:linear) {
        channel=std::clamp(channel,0.f,1.f);
        channel=255*(channel<=.0031308f?12.92f*channel:1.055f*std::pow(channel,1.f/2.4f)-.055f);
    }
    return linear;
}
inline void expectSolid(const DecodedImage& image,const Vec3& linear,const std::string& label) {
    require(image.width&&image.height&&image.rgba.size()==std::size_t(image.width)*image.height*4,
        label+": invalid readback.");
    const auto expected=displayCodes(linear);
    for(std::size_t pixel=0;pixel<image.rgba.size()/4;++pixel) {
        for(unsigned channel=0;channel<3;++channel)
            require(std::abs(float(image.rgba[pixel*4+channel])-expected[channel])<=3,
                label+": pixel="+std::to_string(pixel)+" channel="+std::to_string(channel)+
                " GPU="+std::to_string(image.rgba[pixel*4+channel])+" CPU="+std::to_string(expected[channel]));
        require(image.rgba[pixel*4+3]==255,label+": opaque wall changed alpha.");
    }
}
inline std::shared_ptr<ModelAsset> inwardWall() {
    auto mesh=std::make_shared<ModelAsset>();mesh->id="fog-aerial-inward-wall";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-20,-20,0},{20,-20,0},{20,20,0},{-20,20,0}}};
    for(const auto& position:positions) {
        ModelVertex vertex;vertex.position=position;vertex.normal={0,0,-1};vertex.tangent={1,0,0,1};
        primitive.vertices.push_back(vertex);math::include(mesh->bounds,position);
    }
    // Deliberately face away from the eye, matching a two-sided inward room wall.
    primitive.indices={0,2,1,0,3,2};mesh->primitives.push_back(std::move(primitive));
    mesh->draws.push_back({0,{}});mesh->materials.emplace_back();return mesh;
}
template<class Capture>
void exercise(Renderer& renderer,SceneSettings& sceneSettings,SceneSettings& gameSettings,
    RenderScene& scene,RenderScene& game,const PreparedEnvironment& originalEnvironment,Capture&& capture) {
    const auto savedScene=sceneSettings,savedGame=gameSettings;
    const auto savedSceneInstances=scene.instances,savedGameInstances=game.instances;
    const auto savedSceneSuns=scene.directionalLights,savedGameSuns=game.directionalLights;
    const auto savedScenePoints=scene.pointLights,savedGamePoints=game.pointLights;
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Lit);
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);

    // Publish a deterministic test cube through the real environment owner.
    // Mip zero has a bright solar patch. The roughness levels carry distinct,
    // uniform sentinel colors, avoiding a reference based on stochastic GGX bake.
    constexpr std::array<Vec3,5> mipColors{{{.006f,.008f,.01f},{.18f,.07f,.035f},
        {.025f,.19f,.05f},{.08f,.035f,.22f},{.03f,.09f,.16f}}};
    PreparedEnvironment environment;environment.settings={2,16,8,16,16,32};environment.key=0x464f474145524941ull;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,mipColors[0])},environment.settings);
    require(environment.data.specular.size()==mipColors.size(),"Aerial fixture roughness mip count changed.");
    for(std::size_t mip=0;mip<environment.data.specular.size();++mip) {
        auto& level=environment.data.specular[mip];
        for(unsigned face=0;face<6;++face)for(unsigned y=0;y<level.size;++y)for(unsigned x=0;x<level.size;++x) {
            const auto direction=ibl::cubeDirection(static_cast<std::uint8_t>(face),
                (float(x)+.5f)/float(level.size),(float(y)+.5f)/float(level.size));
            const Vec3 color=mip==0&&direction[2]<-.97f?Vec3{8,4,1}:mipColors[mip];
            const auto offset=((std::size_t(face)*level.size+y)*level.size+x)*4;
            for(unsigned channel=0;channel<3;++channel)level.rgba[offset+channel]=color[channel];
            level.rgba[offset+3]=1;
        }
    }
    require(renderer.publishEnvironment(environment),renderer.environmentStatus());
    sceneSettings=SceneSettings{};
    sceneSettings.camera.position={0,0,3};sceneSettings.camera.target={};sceneSettings.camera.fov=60;
    sceneSettings.camera.nearPlane=2.9f;sceneSettings.camera.farPlane=102.9f;
    sceneSettings.environment.enabled=false;sceneSettings.environment.source=EnvironmentSource::Hdri;
    sceneSettings.environment.background.show=true;sceneSettings.environment.intensity=1;
    sceneSettings.environment.toneMapping=ToneMapping::Linear;sceneSettings.environment.glow.enabled=false;
    sceneSettings.environment.ambientLight.source=AmbientLightSource::Disabled;
    sceneSettings.environment.fog.color={};sceneSettings.environment.fog.sunScatter=0;
    sceneSettings.antiAliasing=AntiAliasing::Off;
    gameSettings=sceneSettings;gameSettings.camera.nearPlane=.1f;gameSettings.camera.farPlane=3.01f;
    scene.instances=game.instances={};scene.directionalLights=game.directionalLights={};scene.pointLights=game.pointLights={};
    const auto sky=capture();
    for(const auto& image:sky) {
        const auto center=(std::size_t(image.height/2)*image.width+image.width/2)*4;
        require(image.rgba[center]>240&&image.rgba[0]<60,
            "Aerial fixture did not expose its sharp solar patch against a dark sky.");
    }

    const auto mesh=inwardWall();require(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> materials(1);materials[0].baseColor={0,0,0,1};materials[0].doubleSided=true;
    RenderModelInstance wall;wall.entityId=0x464f474145524941ull;wall.model.asset=mesh;
    wall.materials=&materials;wall.shading=MaterialShading::Unlit;wall.castShadows=false;wall.receiveShadows=false;
    scene.instances=game.instances=std::span(&wall,1);
    const auto clearWall=capture();
    for(const auto& image:clearWall)expectSolid(image,{},"Inward opaque wall covers the actual sun disk");

    auto& fog=sceneSettings.environment.fog;
    fog.enabled=true;fog.mode=FogMode::Depth;fog.depthBegin=0;fog.depthEnd=1;fog.depthOpacity=1;
    fog.aerialPerspective=1;gameSettings.environment.fog=fog;
    // The wall has constant view depth 3. The two camera ranges deliberately
    // select opposite ends of Godot's [first roughness mip, coarsest mip] rule.
    // These explicit endpoint references would fail brightly with mip-zero AP.
    const Vec3 nearReference=mipColors[3]*.003f+mipColors[4]*.997f;
    const float farBlend=3.f*.01f/2.91f;
    const Vec3 farReference=mipColors[1]*(1-farBlend)+mipColors[2]*farBlend;
    const auto aerial=capture();
    expectSolid(aerial[0],nearReference,"Near wall uses coarse aerial sky radiance");
    expectSolid(aerial[1],farReference,"Far wall excludes the sharp sky mip");
    fog.aerialPerspective=0;const auto sceneOff=capture();
    expectSolid(sceneOff[0],{},"Scene Aerial Perspective zero");
    require(sceneOff[1].rgba==aerial[1].rgba,"Scene aerial edit leaked into Game.");
    fog.aerialPerspective=1;gameSettings.environment.fog.aerialPerspective=0;const auto gameOff=capture();
    require(gameOff[0].rgba==aerial[0].rgba,"Game aerial edit leaked into Scene.");
    expectSolid(gameOff[1],{},"Game Aerial Perspective zero");
    gameSettings.environment.fog.aerialPerspective=1;
    std::swap(sceneSettings.camera.nearPlane,gameSettings.camera.nearPlane);
    std::swap(sceneSettings.camera.farPlane,gameSettings.camera.farPlane);
    const auto swapped=capture();
    expectSolid(swapped[0],farReference,"Scene binds its changed aerial camera range");
    expectSolid(swapped[1],nearReference,"Game binds its changed aerial camera range");
    materials[0].receiveFog=false;const auto excluded=capture();
    for(const auto& image:excluded)expectSolid(image,{},"Receive Fog excludes aerial radiance");
    materials[0].receiveFog=true;fog.enabled=gameSettings.environment.fog.enabled=false;
    const auto disabled=capture();
    require(disabled[0].rgba==clearWall[0].rgba&&disabled[1].rgba==clearWall[1].rgba,
        "Disabling fog did not restore the exact opaque wall.");
    scene.instances=game.instances={};const auto restoredSky=capture();
    require(restoredSky[0].rgba==sky[0].rgba&&restoredSky[1].rgba==sky[1].rgba,
        "Surface aerial filtering changed the visible sky/sun disk.");

    renderer.evictModel(mesh->id);
    sceneSettings=savedScene;gameSettings=savedGame;
    scene.instances=savedSceneInstances;game.instances=savedGameInstances;
    scene.directionalLights=savedSceneSuns;game.directionalLights=savedGameSuns;
    scene.pointLights=savedScenePoints;game.pointLights=savedGamePoints;
    require(renderer.publishEnvironment(originalEnvironment),renderer.environmentStatus());
}
} // namespace crimson::fog_aerial_fixture
