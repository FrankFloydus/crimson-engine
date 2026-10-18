#include "crimson-renderer/Renderer.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "resources/ImageDecoder.h"
#include "resources/TextureStreamer.h"
#include "assets/GrungeGenerator.h"
#include "editor/MaterialPreview.h"
#include <chrono>
#include <filesystem>
#include <thread>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
constexpr std::uint16_t Size=128,ReadView=254;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
std::shared_ptr<ModelAsset> quad(){
    auto asset=std::make_shared<ModelAsset>();asset->id="material-mapping-quad";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned i=0;i<4;++i){ModelVertex v;v.position=positions[i];v.normal={0,0,1};v.uv0=uv[i];v.uv1={1-uv[i][0],uv[i][1]};v.tangent={1,0,0,-1};primitive.vertices.push_back(v);math::include(asset->bounds,v.position);}
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.metallic=0;material.roughness=1;asset->materials.push_back(material);return asset;
}
std::array<int,3> sample(const DecodedImage& image,const Camera& camera,Vec3 world){
    const auto p=math::project(world,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
    check(bool(p),"Mapping fixture point is not projectable.");const int x=static_cast<int>((*p)[0]),y=static_cast<int>((*p)[1]);
    check(x>1 && y>1 && x<Size-2 && y<Size-2,"Mapping fixture point escaped the image.");
    std::array<int,3> result{};for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)for(unsigned c=0;c<3;++c)result[c]+=image.rgba[(std::size_t(y+dy)*Size+x+dx)*4+c];
    for(auto& value:result)value/=9;return result;
}
void disconnectMaterialInput(MaterialGraph& graph,MaterialNodeId node,MaterialPin pin){if(const auto* link=materialInputLink(graph,node,pin))removeMaterialLink(graph,link->id);}
int difference(std::array<int,3> a,std::array<int,3> b){return std::abs(a[0]-b[0])+std::abs(a[1]-b[1])+std::abs(a[2]-b[2]);}
template<class Read,class Prepare>
void exerciseMaterialFog(Renderer& renderer,const std::shared_ptr<ModelAsset>& mesh,Read&& read,Prepare&& prepare) {
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.fov=45;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.environment.enabled=false;
    settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;
    settings.environment.background.color={};settings.environment.background.show=false;

    RenderScene scene{settings,{}};scene.sceneMode=true;
    std::vector<ModelMaterial> frontSurface(1),neighborSurface(1),rearSurface(1);
    frontSurface[0].baseColor={0,0,0,1};neighborSurface[0]=frontSurface[0];rearSurface[0].baseColor={0,1,0,1};
    std::vector<ModelMaterialTextures> frontBindings(1),neighborBindings(1),rearBindings(1);
    RenderModelInstance front;front.model.asset=mesh;front.materials=&frontSurface;front.textures=&frontBindings;
    front.model.transform=math::translation({-.55f,0,0})*math::scaling({.4f,.4f,1});
    auto neighbor=front;neighbor.materials=&neighborSurface;neighbor.textures=&neighborBindings;
    neighbor.model.transform=math::translation({.55f,0,0})*math::scaling({.4f,.4f,1});
    auto rear=front;rear.materials=&rearSurface;rear.textures=&rearBindings;rear.shading=MaterialShading::Unlit;
    rear.model.transform=math::translation({0,0,-.3f});
    std::array<RenderParticleMaterial,2> sprites;auto& sprite=sprites[0];
    sprite.baseColor={0,0,0,1};sprite.blend=MaterialBlend::Opaque;sprites[1]=sprite;
    std::array<RenderParticle,2> particles;particles[0].position={-.55f,0,0};particles[0].size=.8f;
    particles[0].billboardMode=ParticleBillboardMode::Enabled;particles[1]=particles[0];
    particles[1].position={.55f,0,0};particles[1].materialIndex=1;scene.particleMaterials=sprites;
    const auto capture=[&]{renderer.renderGame(scene);return read(renderer.gameViewportTexture());};
    const Vec3 left{-.55f,0,0},right{.55f,0,0};
    const auto black=[](const auto& rgb){return rgb[0]<3 && rgb[1]<3 && rgb[2]<3;};
    std::array<FogSettings,3> fogModes;
    for(auto& fog:fogModes){fog.enabled=true;fog.color={0,0,1};}
    fogModes[0].mode=FogMode::Depth;fogModes[0].depthBegin=0;fogModes[0].depthEnd=1;fogModes[0].depthOpacity=.6f;
    fogModes[1].density=.5f;
    fogModes[2].density=0;fogModes[2].height=2;fogModes[2].heightDensity=.8f;
    // Cover packet and fallback authority, both shading models and both geometry
    // paths. The green rear surface proves disabling haze retains opaque depth.
    for(bool generated:{false,true})for(auto shading:{MaterialShading::StandardPbr,MaterialShading::Unlit}) {
        MaterialAsset authored;addMaterialNode(authored.graph,MaterialNodeKind::Output);
        auto& output=materialOutputNode(authored.graph).output;output.shading=shading;output.baseColor={0,0,0};
        std::shared_ptr<const PreparedGraphMaterial> receiving,excluded;
        if(generated) {
            receiving=prepare(authored);output.receiveFog=false;excluded=prepare(authored);
            check(receiving->programs==excluded->programs,"Receive Fog recompiled an unchanged material program.");
        }
        front.shading=shading;neighbor.shading=shading;sprite.shading=shading;sprites[1].shading=shading;
        neighborBindings[0].graph=receiving;sprites[1].graph=receiving;
        for(bool particleGeometry:{false,true}) {
            std::vector<RenderModelInstance> instances;
            if(!particleGeometry){instances.push_back(front);instances.push_back(neighbor);}
            instances.push_back(rear);scene.instances=instances;
            scene.particles=particleGeometry?std::span<const RenderParticle>(particles):std::span<const RenderParticle>{};
            const auto receive=[&](bool enabled) {
                frontBindings[0].graph=enabled?receiving:excluded;sprite.graph=frontBindings[0].graph;
                // An available graph is authoritative even if its fallback is stale.
                frontSurface[0].receiveFog=generated?!enabled:enabled;sprite.receiveFog=frontSurface[0].receiveFog;
            };
            settings.environment.fog={};receive(true);
            const auto baseline=capture();
            check(black(sample(baseline,settings.camera,left)) && black(sample(baseline,settings.camera,right)) &&
                sample(baseline,settings.camera,{0,-.8f,-.3f})[1]>250,"Fog fixture lost its opaque black foreground or green rear surface.");
            for(const auto& fog:fogModes) {
                settings.environment.fog=fog;receive(true);const auto fogged=capture();
                check(sample(fogged,settings.camera,left)[2]>150 && sample(fogged,settings.camera,right)[2]>150,
                    "An opted-in black material did not receive colored distance or height fog.");
                receive(false);const auto isolated=capture();
                check(black(sample(isolated,settings.camera,left)),"A fog-excluded black material received haze or lost opaque depth.");
                check(difference(sample(isolated,settings.camera,right),sample(fogged,settings.camera,right))<=3,
                    "Excluding one material disabled fog on its neighboring surface.");
                receive(true);
                check(capture().rgba==fogged.rgba,"Reusing the opted-in material packet did not restore fog exactly.");
            }
        }
    }
}
template<class Read,class Prepare,class Pick>
void exerciseInstanceOpacity(Renderer& renderer,const std::shared_ptr<ModelAsset>& mesh,Read&& read,Prepare&& prepare,Pick&& pick) {
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.fov=45;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.environment.enabled=false;
    settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;
    settings.environment.background.color={};settings.environment.background.show=false;

    RenderScene scene{settings,{}};scene.sceneMode=true;
    std::vector<ModelMaterial> frontMaterials(1),rearMaterials(1);
    rearMaterials[0].baseColor={0,1,0,1};
    std::vector<ModelMaterialTextures> frontBindings(1),rearBindings(1);
    RenderModelInstance front;front.entityId=301;front.model.asset=mesh;front.materials=&frontMaterials;front.textures=&frontBindings;
    auto rear=front;rear.entityId=302;rear.materials=&rearMaterials;rear.textures=&rearBindings;rear.shading=MaterialShading::Unlit;
    rear.model.transform=math::translation({0,0,-.25f});
    std::array instances{front,rear};scene.instances=instances;
    const auto capture=[&](bool game=true){if(game)renderer.renderGame(scene);else renderer.render(scene);return read(game?renderer.gameViewportTexture():renderer.viewportTexture());};
    const auto encoded=[](float linear){return int(std::lround(255*(linear<=.0031308f?12.92f*linear:1.055f*std::pow(linear,1/2.4f)-.055f)));};
    const auto expect=[&](const DecodedImage& image,Vec3 position,float coverage,bool additive,const std::string& context) {
        const auto actual=sample(image,settings.camera,position);
        const std::array expected{encoded(coverage),encoded(additive?1.f:1-coverage),0};
        check(difference(actual,expected)<=6,context+": RGB="+std::to_string(actual[0])+","+std::to_string(actual[1])+","+std::to_string(actual[2])+
            ", expected="+std::to_string(expected[0])+","+std::to_string(expected[1])+",0");
    };
    // Low authored Opaque alpha must remain irrelevant. Mask cutoff must run
    // before the camera fade, while Alpha/Additive multiply authored coverage
    // by that fade exactly once in the existing shared transparency queue.
    for(bool generated:{false,true})for(auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
        const float authoredAlpha=blend==MaterialBlend::Opaque?.25f:blend==MaterialBlend::Mask?.75f:.4f;
        const auto context=std::string(generated?"Generated":"Fallback")+" instance opacity, blend="+std::to_string(unsigned(blend));
        MaterialAsset authored;addMaterialNode(authored.graph,MaterialNodeKind::Output);
        auto& output=materialOutputNode(authored.graph).output;output.shading=MaterialShading::Unlit;output.blend=blend;
        output.baseColor={1,0,0};output.opacity=authoredAlpha;output.opacityMask=authoredAlpha;
        auto& material=frontMaterials[0];material=ModelMaterial{};material.baseColor={1,0,0,authoredAlpha};
        material.alphaMask=blend==MaterialBlend::Mask;material.alphaCutoff=.5f;
        instances[0]=front;instances[0].shading=MaterialShading::Unlit;instances[0].blend=blend;instances[1]=rear;
        frontBindings[0].graph=generated?prepare(authored):nullptr;
        DecodedImage full;
        for(float opacity:{1.f,.5f,.125f,0.f}) {
            instances[0].opacity=opacity;const auto image=capture();
            const float coverage=opacity*((blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive)?authoredAlpha:1.f);
            expect(image,{},coverage,blend==MaterialBlend::Additive,context+", fade="+std::to_string(opacity));
            if(opacity==1)full=image;
        }
        instances[0].opacity=1;check(capture().rgba==full.rgba,context+" did not restore the same full-opacity image.");
        instances[0].opacity=.5f;
        check(pick(scene,{},PickMode::Selection)==front.entityId,context+" lost visibly covered selection.");
        instances[0].opacity=.005f;
        check(pick(scene,{},PickMode::Selection)==rear.entityId,context+" retained selection below visible coverage threshold.");
        check(pick(scene,{},PickMode::MaterialSlot)==front.entityId,context+" changed neutral material-slot targeting.");
        instances[0].opacity=0;check(pick(scene,{},PickMode::Selection)==rear.entityId,context+" retained a hidden instance in selection.");
        if(blend==MaterialBlend::Mask) {
            material.baseColor[3]=.25f;output.opacityMask=.25f;
            if(generated)frontBindings[0].graph=prepare(authored);
            instances[0].opacity=.5f;expect(capture(),{},0,false,context+" lost authored mask cutout.");
            check(pick(scene,{},PickMode::Selection)==rear.entityId,context+" lost authored mask cutout in selection.");
        }
        if(blend!=MaterialBlend::Opaque)continue;
        for(float opacity:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),2.f}) {
            instances[0].opacity=opacity;check(capture().rgba==full.rgba,context+" did not safely normalize nonfinite or excessive opacity.");
        }
        instances[0].opacity=-1;expect(capture(),{},0,false,context+" did not clamp negative opacity.");
        // Lit emission provides a known radiance without depending on an IBL
        // bake or analytic light, covering the second fallback shader branch.
        material.baseColor={1,0,0,.25f};material.emissive={1,0,0};output.emission={1,0,0};output.shading=MaterialShading::StandardPbr;
        instances[0].shading=MaterialShading::StandardPbr;instances[0].opacity=.5f;
        if(generated)frontBindings[0].graph=prepare(authored);
        expect(capture(),{},.5f,false,context+" did not fade Lit radiance.");
        material.emissive={};output.emission={};output.shading=MaterialShading::Unlit;instances[0].shading=MaterialShading::Unlit;
        if(generated)frontBindings[0].graph=prepare(authored);
        // Both draws share exactly one material packet. The value is per
        // instance, and switching Scene/Game must not retain the previous draw.
        auto right=instances[0];right.entityId=303;
        instances[0].model.transform=math::translation({-.5f,0,0})*math::scaling({.35f,.35f,1});instances[0].opacity=.25f;
        right.model.transform=math::translation({.5f,0,0})*math::scaling({.35f,.35f,1});right.opacity=.75f;
        const std::array twins{instances[0],right,rear};scene.instances=twins;
        const auto game=capture();expect(game,{-.5f,0,0},.25f,false,context+" shared packet lost left opacity.");
        expect(game,{.5f,0,0},.75f,false,context+" shared packet lost right opacity.");
        auto opaqueTwins=twins;opaqueTwins[0].opacity=opaqueTwins[1].opacity=1;scene.instances=opaqueTwins;
        expect(capture(false),{-.5f,0,0},1,false,context+" Game fade leaked into the Scene view.");
        scene.instances=twins;check(capture().rgba==game.rgba,context+" view switching changed the Game fade.");
        scene.instances=instances;
    }
}
template<class Read,class Prepare>
void exerciseParticleFadeTransitions(Renderer& renderer,const std::shared_ptr<ModelAsset>& mesh,Read&& read,Prepare&& prepare) {
    // This target deliberately has no simulation dependency. Use presentation
    // packets for three particles already two seconds into distinct motion,
    // rather than newborns that could hide an accidental return to t=0.
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.fov=45;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.environment.enabled=false;
    settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;
    settings.environment.background.color={};settings.environment.background.show=false;
    std::vector<ModelMaterial> wallMaterials(1);wallMaterials[0].baseColor={1,0,0,1};
    RenderModelInstance wall;wall.model.asset=mesh;wall.materials=&wallMaterials;wall.shading=MaterialShading::Unlit;
    wall.model.transform=math::translation({.7f,0,0})*math::scaling({.22f,.7f,1});
    constexpr std::array<Vec3,3> births{{{-.92f,-.75f,-.15f},{-.64f,-.6f,0},{-.36f,-.5f,.15f}}};
    constexpr std::array<Vec3,3> velocities{{{.01f,.24f,0},{-.015f,.2f,0},{.02f,.16f,0}}};
    std::array<RenderParticle,3> particles;
    for(auto& particle:particles){particle.size=.14f;particle.billboardMode=ParticleBillboardMode::Enabled;}
    RenderParticleMaterial sprite;sprite.blend=MaterialBlend::Alpha;sprite.baseColor={.22f,.4f,.7f,1};

    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&wall,1);
    scene.particles=particles;scene.particleMaterials=std::span(&sprite,1);
    const auto capture=[&]{renderer.renderGame(scene);return read(renderer.gameViewportTexture());};
    const auto encoded=[](double linear){return int(std::lround(255*(linear<=.0031308?12.92*linear:1.055*std::pow(linear,1/2.4)-.055)));};
    constexpr std::array<float,7> opacities{1,.12f,1,.5f,1,.12f,1};
    Texture particleAlpha;
    for(unsigned materialMode=0;materialMode<4;++materialMode) {
        const auto mode=std::string(materialMode==0?"fallback":materialMode==1?"Game Time":materialMode==2?"Real Time":"Particle Color RGB/alpha");
        sprite.graph.reset();
        if(materialMode==3) {
            MaterialAsset authored;const auto output=addMaterialNode(authored.graph,MaterialNodeKind::Output).id;
            materialOutputNode(authored.graph).output.shading=MaterialShading::Unlit;
            materialOutputNode(authored.graph).output.blend=MaterialBlend::Alpha;
            const auto color=addMaterialNode(authored.graph,MaterialNodeKind::ParticleColor).id;
            const auto alpha=addMaterialNode(authored.graph,MaterialNodeKind::Texture2D).id;
            findMaterialNode(authored.graph,alpha)->texture.role=MaterialTextureRole::Data;
            findMaterialNode(authored.graph,alpha)->texture.image.id="assets/textures/particle-continuity-alpha.png";
            constexpr std::array<std::uint8_t,4> pixels{255,255,255,204};
            particleAlpha.handle=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(pixels.data(),4));
            check(bgfx::isValid(particleAlpha.handle),"Particle fade-transition alpha texture allocation failed.");
            const auto coverage=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
            connectMaterialNodes(authored.graph,color,MaterialPin::RGB,output,MaterialPin::BaseColor);
            connectMaterialNodes(authored.graph,color,MaterialPin::A,coverage,MaterialPin::Left);
            connectMaterialNodes(authored.graph,alpha,MaterialPin::A,coverage,MaterialPin::Right);
            connectMaterialNodes(authored.graph,coverage,MaterialPin::Value,output,MaterialPin::Opacity);
            sprite.graph=prepare(authored,particleAlpha.handle);
        } else if(materialMode) {
            MaterialAsset authored;const auto output=addMaterialNode(authored.graph,MaterialNodeKind::Output).id;
            materialOutputNode(authored.graph).output.shading=MaterialShading::Unlit;
            materialOutputNode(authored.graph).output.blend=MaterialBlend::Alpha;
            const auto time=addMaterialNode(authored.graph,MaterialNodeKind::Time).id;
            findMaterialNode(authored.graph,time)->time.ignorePause=materialMode==2;
            const auto factor=addMaterialNode(authored.graph,MaterialNodeKind::Scalar).id;
            findMaterialNode(authored.graph,factor)->scalar=.15f;
            const auto product=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
            connectMaterialNodes(authored.graph,time,MaterialPin::Value,product,MaterialPin::Left);
            connectMaterialNodes(authored.graph,factor,MaterialPin::Value,product,MaterialPin::Right);
            const auto white=addMaterialNode(authored.graph,MaterialNodeKind::Color).id;
            findMaterialNode(authored.graph,white)->color={1,1,1,1};
            const auto tint=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
            connectMaterialNodes(authored.graph,white,MaterialPin::RGB,tint,MaterialPin::Left);
            connectMaterialNodes(authored.graph,product,MaterialPin::Value,tint,MaterialPin::Right);
            connectMaterialNodes(authored.graph,tint,MaterialPin::Value,output,MaterialPin::BaseColor);
            const auto lowered=generateMaterialGraphShader(authored);auto packet=prepare(authored);
            check(lowered.runtimeGraph&&!lowered.runtimeParameters.empty(),"Particle fade-transition fixture did not lower a live Time branch.");
            packet->runtimeGraph=lowered.runtimeGraph;packet->runtimeParameters=lowered.runtimeParameters;sprite.graph=std::move(packet);
        }
        std::uint64_t signature{};
        for(std::size_t step=0;step<opacities.size();++step) {
            const auto age=2.f+static_cast<float>(step)*.125f;
            constexpr std::array<Vec3,3> agedColors{{{.8f,.1f,.05f},{.05f,.65f,.15f},{.1f,.05f,.7f}}};
            for(std::size_t i=0;i<particles.size();++i) {
                auto& particle=particles[i];particle.position=births[i]+velocities[i]*age;
                const auto brightness=.75f+static_cast<float>(step)*.025f;
                particle.color={agedColors[i][0]*brightness,agedColors[i][1]*brightness,agedColors[i][2]*brightness,
                    .3f+static_cast<float>(i)*.05f+static_cast<float>(step)*.02f};
                particle.size=.14f+static_cast<float>(i)*.01f+static_cast<float>(step)*.004f;
            }
            scene.materialContext={age,double(age)+1.25};
            wall.opacity=1;const auto control=capture();
            if(!step)signature=renderer.streamingSignature();
            wall.opacity=opacities[step];const auto image=capture();
            const auto context=mode+" particle continuity, step="+std::to_string(step)+", wall opacity="+std::to_string(wall.opacity);
            // Only the wall occupies the right half. Its changing visibility
            // must not disturb any particle or its material clock on the left.
            for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size/2;++x)for(unsigned c=0;c<4;++c) {
                const auto offset=(std::size_t(y)*Size+x)*4+c;
                if(image.rgba[offset]!=control.rgba[offset])
                    check(false,context+" changed unrelated particle pixels at "+std::to_string(x)+","+std::to_string(y)+", channel="+
                        std::to_string(c)+", actual="+std::to_string(image.rgba[offset])+", opaque control="+std::to_string(control.rgba[offset]));
            }
            const auto expectedTime=materialMode?std::array<int,3>{encoded(.15*(materialMode==1?scene.materialContext.gameSeconds:scene.materialContext.realSeconds)),
                encoded(.15*(materialMode==1?scene.materialContext.gameSeconds:scene.materialContext.realSeconds)),
                encoded(.15*(materialMode==1?scene.materialContext.gameSeconds:scene.materialContext.realSeconds))}:
                std::array<int,3>{encoded(.22),encoded(.4),encoded(.7)};
            for(std::size_t i=0;i<particles.size();++i) {
                auto expected=expectedTime;
                if(materialMode==3)for(unsigned c=0;c<3;++c)expected[c]=encoded(particles[i].color[c]*particles[i].color[3]*.8f);
                const auto actual=sample(image,settings.camera,particles[i].position);
                check(difference(actual,expected)<=6,context+" lost the aged position, material clock or color/alpha for particle "+std::to_string(i)+
                    ": RGB="+std::to_string(actual[0])+","+std::to_string(actual[1])+","+std::to_string(actual[2]));
                check(difference(sample(image,settings.camera,births[i]),{0,0,0})<=3,context+" rendered a particle back at its birth position.");
            }
            const auto& stats=renderer.gameParticleStats();
            check(stats.requested==particles.size()&&stats.submitted==particles.size()&&!stats.dropped&&!stats.invalid,
                context+" changed particle admission/submission counts.");
            check(renderer.streamingSignature()==signature,context+" invalidated renderer resource/program signatures.");
        }
    }
}
template<class Read>
void exerciseCoplanarCollisionOverlays(Renderer& renderer,const std::shared_ptr<ModelAsset>& mesh,Read&& read) {
    // Render the visual plane as many small, alternating triangles, while the
    // collider uses two different large triangles. Identical triangle depth
    // interpolation would hide the coplanar floor/wall regression.
    auto split=quad();split->id="collision-split-plane";split->bounds={};
    auto& primitive=split->primitives.front();primitive.vertices.clear();primitive.indices.clear();
    constexpr unsigned Columns=13,Rows=11;
    for(unsigned y=0;y<=Rows;++y)for(unsigned x=0;x<=Columns;++x) {
        ModelVertex vertex;vertex.position={2.f*x/Columns-1,2.f*y/Rows-1,0};
        vertex.normal={0,0,1};vertex.tangent={1,0,0,-1};primitive.vertices.push_back(vertex);math::include(split->bounds,vertex.position);
    }
    for(unsigned y=0;y<Rows;++y)for(unsigned x=0;x<Columns;++x) {
        const auto a=y*(Columns+1)+x,b=a+1,d=a+Columns+1,c=d+1;
        const std::array<unsigned,6> indices=(x+y)%2?std::array<unsigned,6>{a,b,d,b,c,d}:std::array<unsigned,6>{a,b,c,a,c,d};
        primitive.indices.insert(primitive.indices.end(),indices.begin(),indices.end());
    }
    check(renderer.prepareModel(*split),renderer.modelStatus());
    SceneSettings settings;settings.camera.target={};settings.camera.fov=45;settings.camera.fovAxis=CameraFovAxis::Vertical;
    settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;settings.exposure=1;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.environment.background.show=false;
    std::vector<ModelMaterial> materials(1);materials[0].baseColor={0,0,0,1};std::vector<ModelMaterialTextures> textures(1);
    RenderModelInstance surface;surface.model.asset=split;surface.materials=&materials;surface.textures=&textures;surface.shading=MaterialShading::Unlit;
    auto blocker=surface;blocker.model.asset=mesh;
    auto geometry=std::make_shared<CollisionOverlayGeometry>();
    geometry->vertices={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};geometry->triangles={0,1,3,1,2,3};
    geometry->wireVertices={{-1,0,0},{1,0,0}};geometry->edges={{0,1,{0,0,1},{0,0,1},false}};
    auto fillOnly=std::make_shared<CollisionOverlayGeometry>(*geometry);fillOnly->wireVertices.clear();fillOnly->edges.clear();
    RenderCollisionOverlay overlay;overlay.geometry=geometry;overlay.color={.35f,.65f,.8f,1};overlay.fillAlphaScale=1;

    RenderScene scene{settings,{}};scene.sceneMode=true;scene.collisionOverlays=std::span(&overlay,1);
    const auto capture=[&](bool game){if(game)renderer.renderGame(scene);else renderer.render(scene);return read(game?renderer.gameViewportTexture():renderer.viewportTexture());};
    const auto nearColor=[](const auto& rgb){return std::abs(rgb[0]-89)<=2&&std::abs(rgb[1]-166)<=2&&std::abs(rgb[2]-204)<=2;};
    for(bool distant:{false,true})for(bool floor:{false,true}) {
        const float distance=distant?25.f:3.f,span=distant?10.f:1.2f;
        settings.camera.nearPlane=distant?.0008660254f:.01f;settings.camera.farPlane=2048;
        surface.model.transform=math::eulerRotationXYZ({floor?-1.05f:.25f,.35f,.1f})*math::scaling({span,span,1});
        overlay.world=surface.model.transform;
        blocker.model.transform=math::translation({0,0,distant?.3f:.002f})*surface.model.transform*math::scaling({.18f,.18f,1});
        std::array instances{surface,blocker};
        for(bool game:{false,true})for(float orbit:{-.013f,.017f}) {
            const auto caseName=std::string(distant?"Distant":"Near")+(floor?" floor ":" wall ")+(game?"Game":"Scene")+", orbit="+std::to_string(orbit);
            const auto rgbString=[](const auto& rgb){return std::to_string(rgb[0])+","+std::to_string(rgb[1])+","+std::to_string(rgb[2]);};
            settings.camera.position={distance*orbit,0,distance};scene.instances=std::span(&surface,1);overlay.fillAlphaScale=1;overlay.geometry=fillOnly;
            const auto image=capture(game);unsigned checked{};
            for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
                const auto point=math::transformPoint(overlay.world,{x*.18f,y*.18f,0});
                const auto screen=math::project(point,settings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*settings.camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
                if(!screen || (*screen)[0]<5 || (*screen)[0]>Size-5 || (*screen)[1]<5 || (*screen)[1]>Size-5)continue;
                const auto actual=sample(image,settings.camera,point);
                check(nearColor(actual),std::string(distant?"Distant":"Near")+(floor?" floor":" wall")+" coplanar collision fill developed raster depth holes: "+
                    (game?"Game":"Scene")+", orbit="+std::to_string(orbit)+", local="+std::to_string(x*.18f)+","+std::to_string(y*.18f)+
                    ", pixel="+std::to_string((*screen)[0])+","+std::to_string((*screen)[1])+", RGB="+std::to_string(actual[0])+","+std::to_string(actual[1])+","+std::to_string(actual[2]));
                ++checked;
            }
            check(checked>=25,"Coplanar collision fixture sampled too little visible surface.");
            overlay.fillAlphaScale=0;overlay.geometry=geometry;
            const auto wireImage=capture(game);
            for(float x:{-.36f,0.f,.36f}) {
                const auto visibleWire=sample(wireImage,settings.camera,math::transformPoint(overlay.world,{x,0,0}));
                check(visibleWire[2]>20,"Coplanar collision wire disappeared with a very small near plane: "+caseName+", localX="+std::to_string(x)+", RGB="+rgbString(visibleWire));
            }
            // Native line Z is evaluated along the edge, while triangle Z is
            // evaluated at the covered pixel center. On a sloped plane, a
            // half-pixel perpendicular offset can exceed a 2 mm separation at
            // this 128px resolution even without any bias. Keep the sloped wire
            // blocker beyond that footprint; test a real 2 mm wire gap below.
            instances[1].model.transform=math::translation({0,0,distant?.3f:.025f})*surface.model.transform*math::scaling({.18f,.18f,1});
            scene.instances=instances;
            const auto hiddenWire=sample(capture(game),settings.camera,{});
            check(difference(hiddenWire,{0,0,0})<3,"Collision wire depth bias pierced a separately closer surface: "+caseName+", RGB="+rgbString(hiddenWire));
            overlay.fillAlphaScale=1;overlay.geometry=fillOnly;instances[1]=blocker;
            const auto hiddenFill=sample(capture(game),settings.camera,{});
            check(difference(hiddenFill,{0,0,0})<3,"Collision fill depth bias pierced a separately closer surface: "+caseName+", RGB="+rgbString(hiddenFill));
        }
    }
    // A front-facing plane has no depth gradient perpendicular to the wire, so
    // a 2 mm occluder here measures the bias rather than line raster coverage.
    settings.camera.nearPlane=.01f;surface.model.transform=math::scaling({1.2f,1.2f,1});overlay.world=surface.model.transform;
    blocker.model.transform=math::translation({0,0,.002f})*surface.model.transform*math::scaling({.18f,.18f,1});
    const std::array frontFacing{surface,blocker};overlay.geometry=geometry;overlay.fillAlphaScale=0;
    for(bool game:{false,true})for(float orbit:{-.013f,.017f}) {
        settings.camera.position={3*orbit,0,3};scene.instances=std::span(&surface,1);
        check(sample(capture(game),settings.camera,{})[2]>20,"Front-facing coplanar wire was not visible.");
        scene.instances=frontFacing;const auto hiddenWire=sample(capture(game),settings.camera,{});
        check(difference(hiddenWire,{0,0,0})<3,"Collision wire depth bias pierced a front-facing 2 mm occluder.");
    }
    // A reflected, sheared plane requires the inverse-transpose normal matrix;
    // transforming its local normal as a direction gives the wrong surface.
    surface.model.transform=math::eulerRotationXYZ({.2f,-.3f,.15f})*math::scaling({-1.2f,1.1f,.7f});
    surface.model.transform.at(2,0)+=.3f;overlay.world=surface.model.transform;
    blocker.model.transform=math::translation({0,0,.002f})*surface.model.transform*math::scaling({.18f,.18f,1});
    const std::array affineInstances{surface,blocker};settings.camera.position={0,0,3};
    for(bool game:{false,true}) {
        scene.instances=std::span(&surface,1);const auto visible=capture(game);
        for(float x:{-.36f,0.f,.36f})check(sample(visible,settings.camera,math::transformPoint(overlay.world,{x,0,0}))[2]>20,
            "Reflected affine collision wire lost its adjacent surface depth.");
        scene.instances=affineInstances;
        check(difference(sample(capture(game),settings.camera,{}),{0,0,0})<3,"Reflected affine wire pierced a 2 mm occluder.");
    }
    for(bool concave:{false,true}) {
        auto wedge=quad();wedge->id=concave?"collision-concave-wedge":"collision-convex-wedge";wedge->bounds={};
        auto& wedgePrimitive=wedge->primitives.front();wedgePrimitive.vertices.clear();wedgePrimitive.indices.clear();
        for(int y=-1;y<=1;++y)for(float x:{-1.f,1.f}) {
            ModelVertex vertex;vertex.position={x,float(y),(concave?1.f:-1.f)*std::abs(float(y))};vertex.normal={0,0,1};vertex.tangent={1,0,0,-1};
            wedgePrimitive.vertices.push_back(vertex);math::include(wedge->bounds,vertex.position);
        }
        wedgePrimitive.indices={0,1,3,0,3,2,2,3,5,2,5,4};check(renderer.prepareModel(*wedge),renderer.modelStatus());
        auto crease=std::make_shared<CollisionOverlayGeometry>(*geometry);crease->triangles.clear();
        crease->edges={{0,1,{0,1,1},{0,-1,1},false,concave}};overlay.geometry=crease;overlay.world={};
        surface.model.asset=wedge;surface.model.transform={};blocker=surface;blocker.model.transform=math::translation({0,0,.002f});
        const std::array creaseInstances{surface,blocker};
        for(bool game:{false,true}) {
            scene.instances=std::span(&surface,1);
            check(sample(capture(game),settings.camera,{})[2]>20,concave?"Concave collision crease selected a hidden plane extension.":"Convex collision crease disappeared on its source surface.");
            scene.instances=creaseInstances;
            check(difference(sample(capture(game),settings.camera,{}),{0,0,0})<3,
                concave?"Concave collision crease pierced its closer surface.":"Convex collision crease used a nearer infinite plane extension.");
        }
    }
}

template<class Read>
void exerciseCollisionOverlays(Renderer& renderer,const std::shared_ptr<ModelAsset>& mesh,Read&& read) {
    std::cerr<<"[material mapping] Collision overlays: depth, color and sparse wires.\n"<<std::flush;
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.fov=45;
    settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;settings.exposure=1;
    std::vector<ModelMaterial> materials(1);materials[0].baseColor={0,0,0,1};std::vector<ModelMaterialTextures> textures(1);
    RenderModelInstance wall;wall.model.asset=mesh;wall.model.transform=math::scaling({.4f,.4f,1});
    wall.materials=&materials;wall.textures=&textures;wall.shading=MaterialShading::Unlit;
    auto geometry=std::make_shared<CollisionOverlayGeometry>();
    geometry->vertices={{-.85f,-.85f,0},{.85f,-.85f,0},{.85f,.85f,0},{-.85f,.85f,0}};
    geometry->triangles={0,1,2,0,2,3};geometry->wireVertices={{-.85f,0,0},{.85f,0,0}};geometry->edges={{0,1,{0,0,1},{0,0,1},false}};
    RenderCollisionOverlay overlay;overlay.geometry=geometry;overlay.world=math::translation({0,0,-.3f});overlay.color={.35f,.65f,.8f,1};overlay.fillAlphaScale=1;

    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&wall,1);scene.collisionOverlays=std::span(&overlay,1);
    renderer.setGridVisible(false);
    const auto capture=[&](bool game){if(game)renderer.renderGame(scene);else renderer.render(scene);return read(game?renderer.gameViewportTexture():renderer.viewportTexture());};
    const auto nearColor=[&](const auto& rgb){return std::abs(rgb[0]-89)<=2&&std::abs(rgb[1]-166)<=2&&std::abs(rgb[2]-204)<=2;};
    for(bool game:{false,true}) {
        overlay.world=math::translation({0,0,-.3f});overlay.fillAlphaScale=1;
        auto image=capture(game);const auto covered=sample(image,settings.camera,{});
        check(covered[0]<3&&covered[1]<3&&covered[2]<3,"Collision overlay drew through an opaque wall.");
        check(nearColor(sample(image,settings.camera,{.7f,.4f,-.3f})),"Visible collision fill lost its display-referred color.");
        overlay.world=math::translation({0,0,.2f})*math::scaling({-1,1,1});
        image=capture(game);check(nearColor(sample(image,settings.camera,{})),"Reflected collision fill was culled or lost its foreground depth.");
        for(const auto tone:{ToneMapping::Linear,ToneMapping::Reinhard,ToneMapping::ACES,ToneMapping::AgX}) {
            settings.environment.toneMapping=tone;settings.exposure=tone==ToneMapping::Linear?0.f:100.f;
            settings.environment.fog.enabled=true;settings.environment.fog.color={1,0,0};settings.environment.fog.density=2;
            check(nearColor(sample(capture(game),settings.camera,{})),"Fog, exposure or tone mapping changed collision display color.");
        }
        settings.environment.fog.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;settings.exposure=1;
        overlay.world=math::translation({0,0,-.3f});overlay.fillAlphaScale=0;
        image=capture(game);check(difference(sample(image,settings.camera,{}),{0,0,0})<3,"Hidden collision wire drew through scene depth.");
        check(sample(image,settings.camera,{.7f,0,-.3f})[2]>20,"Visible sparse collision wire was omitted.");
        overlay.world={};image=capture(game);
        check(sample(image,settings.camera,{})[2]>20,"Coplanar visible collision wire was lost behind its source mesh.");
    }
    const auto uploads=renderer.collisionOverlayUploads();
    for(unsigned i=0;i<4;++i) {
        overlay.world=math::translation({.01f*i,0,.2f});overlay.color[3]=.2f+.1f*i;
        renderer.render(scene);renderer.renderGame(scene);(void)read(renderer.gameViewportTexture());
    }
    check(renderer.collisionOverlayUploads()==uploads,"Camera/pose/color or second-view collision drawing reuploaded immutable geometry.");
    // Smooth adjacency edges are hidden unless the two incident faces have
    // opposite facing signs. Classification runs on the GPU for each camera.
    auto smooth=std::make_shared<CollisionOverlayGeometry>(*geometry);smooth->triangles.clear();
    smooth->edges={{0,1,{0,0,1},{0,0,1},true}};overlay.geometry=smooth;overlay.color={.35f,.65f,.8f,1};overlay.world=math::translation({0,0,.2f});
    check(difference(sample(capture(true),settings.camera,{}),{0,0,0})<3,"Smooth same-facing mesh edge became a triangle cage.");
    auto silhouette=std::make_shared<CollisionOverlayGeometry>(*smooth);silhouette->edges[0].normalB={0,0,-1};overlay.geometry=silhouette;
    check(sample(capture(true),settings.camera,{})[2]>20,"View silhouette edge was discarded.");
    auto fallback=std::make_shared<CollisionOverlayGeometry>(*geometry);fallback->edges[0].normalA={};fallback->edges[0].normalB={};overlay.geometry=fallback;
    check(sample(capture(true),settings.camera,{})[2]>20,"Collision wire without adjacent planes lost native-depth fallback.");
    scene.collisionOverlays={};check(difference(sample(capture(true),settings.camera,{}),{0,0,0})<3,"Disabling collision overlays retained their prior draws.");
    std::cerr<<"[material mapping] Collision overlays: split coplanar surfaces and tiny-near depth.\n"<<std::flush;
    exerciseCoplanarCollisionOverlays(renderer,mesh,read);
    // Retire both filled and wire-only cached meshes here. A wire-only mesh has
    // no index buffer, so its cleanup must preserve the invalid-handle guard.
    overlay.geometry.reset();geometry.reset();smooth.reset();silhouette.reset();fallback.reset();
    for(unsigned i=0;i<4;++i){renderer.advanceStreaming();(void)capture(true);}
    std::cerr<<"[material mapping] Collision overlays: cache retirement passed.\n"<<std::flush;
    renderer.setGridVisible(true);
}

template<class Read,class Prepare>
void exerciseImageAdjustmentsAndGrunge(Renderer& renderer,const std::shared_ptr<ModelAsset>& mesh,Read&& read,Prepare&& prepare) {
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.fov=45;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.environment.enabled=false;
    settings.environment.fog.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.environment.background.show=false;

    RenderScene scene{settings,{}};scene.sceneMode=true;
    std::vector<ModelMaterial> surfaces(1);std::vector<ModelMaterialTextures> bindings(1);
    RenderModelInstance instance;instance.model.asset=mesh;instance.materials=&surfaces;instance.textures=&bindings;
    instance.shading=MaterialShading::Unlit;scene.instances=std::span(&instance,1);
    const auto capture=[&](const MaterialAsset& authored) {
        bindings[0].graph=prepare(authored);renderer.renderGame(scene);return read(renderer.gameViewportTexture());
    };
    const auto encoded=[](float linear) {
        linear=std::clamp(linear,0.f,1.f);
        const float srgb=linear<=.0031308f?linear*12.92f:1.055f*std::pow(linear,1.f/2.4f)-.055f;
        return static_cast<int>(std::lround(srgb*255.f));
    };
    for(const auto kind:{MaterialNodeKind::Desaturation,MaterialNodeKind::Contrast,MaterialNodeKind::ContrastRGB,MaterialNodeKind::HueShift}) {
        MaterialAsset authored;authored.id="native-image-adjustment-"+std::to_string(static_cast<int>(kind));
        const auto output=addMaterialNode(authored.graph,MaterialNodeKind::Output).id;
        materialOutputNode(authored.graph).output.shading=MaterialShading::Unlit;
        materialOutputNode(authored.graph).output.receiveFog=false;
        const auto adjustment=addMaterialNode(authored.graph,kind).id;
        findMaterialNode(authored.graph,adjustment)->left={MaterialValueType::Color,{.12f,.43f,.81f,1}};
        connectMaterialNodes(authored.graph,adjustment,MaterialPin::RGB,output,MaterialPin::BaseColor);
        const auto baseline=capture(authored);const auto program=bindings[0].graph->programs;
        const std::array<float,4> amounts=kind==MaterialNodeKind::HueShift?std::array<float,4>{0,120,-120,360}:std::array<float,4>{0,1,.5f,1.6f};
        for(const auto amount:amounts) {
            findMaterialNode(authored.graph,adjustment)->right.value={amount,amount,amount,amount};
            const auto image=capture(authored);const auto actual=sample(image,settings.camera,{});
            const auto expected=evaluateMaterialUniform(authored.graph,adjustment,MaterialPin::RGB).value;
            for(unsigned channel=0;channel<3;++channel)
                check(std::abs(actual[channel]-encoded(expected[channel]))<=3,
                    std::string(materialNodeKindName(kind))+" GPU color diverged from its CPU linear result and one sRGB encode.");
            check(bindings[0].graph->programs==program,"An image-adjustment parameter edit recompiled unchanged topology.");
            if(kind==MaterialNodeKind::HueShift && (amount==0||amount==360))
                check(image.rgba==baseline.rgba,"A full HueShift turn changed the source color.");
        }
        if(kind==MaterialNodeKind::Contrast) {
            const auto rgb=capture(authored);
            const auto color=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
            findMaterialNode(authored.graph,color)->right={MaterialValueType::Color,{1,1,1,1}};
            connectMaterialNodes(authored.graph,adjustment,MaterialPin::Value,color,MaterialPin::Left);
            connectMaterialNodes(authored.graph,color,MaterialPin::Value,output,MaterialPin::BaseColor);
            check(capture(authored).rgba==rgb.rgba,"Contrast Value and replicated RGB outputs produced different grayscale.");
        }
    }
    MaterialAsset authored;authored.id="native-procedural-grunge";
    const auto output=addMaterialNode(authored.graph,MaterialNodeKind::Output).id;
    materialOutputNode(authored.graph).output.shading=MaterialShading::Unlit;
    materialOutputNode(authored.graph).output.receiveFog=false;
    const auto grunge=addMaterialNode(authored.graph,MaterialNodeKind::Grunge).id;
    const auto mapping=addMaterialNode(authored.graph,MaterialNodeKind::UV).id;
    const auto color=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
    findMaterialNode(authored.graph,color)->right={MaterialValueType::Color,{1,1,1,1}};
    connectMaterialNodes(authored.graph,mapping,MaterialPin::UV,grunge,MaterialPin::UV);
    connectMaterialNodes(authored.graph,grunge,MaterialPin::Value,color,MaterialPin::Left);
    connectMaterialNodes(authored.graph,color,MaterialPin::Value,output,MaterialPin::BaseColor);
    const auto changedPixels=[](const DecodedImage& first,const DecodedImage& second,int tolerance=3) {
        std::size_t count{};
        for(std::size_t y=24;y<Size-24u;++y)for(std::size_t x=24;x<Size-24u;++x) {
            const auto at=(y*Size+x)*4;
            if(std::abs(int(first.rgba[at])-int(second.rgba[at]))>tolerance)++count;
        }
        return count;
    };
    const auto grayscale=[](const DecodedImage& image,bool varying) {
        int minimum=255,maximum{};
        for(std::size_t y=24;y<Size-24u;++y)for(std::size_t x=24;x<Size-24u;++x) {
            const auto at=(y*Size+x)*4;const auto red=int(image.rgba[at]);minimum=std::min(minimum,red);maximum=std::max(maximum,red);
            check(std::abs(red-int(image.rgba[at+1]))<=1 && std::abs(red-int(image.rgba[at+2]))<=1,
                "Grunge grayscale outputs acquired unequal color channels.");
        }
        if(varying)check(maximum-minimum>12,"Grunge did not produce a spatially varying pattern.");
        return maximum;
    };
    {
        auto combined=MaterialGrungeNode{};combined.leaksOpacity=.65f;combined.stainsOpacity=.55f;
        findMaterialNode(authored.graph,grunge)->grunge=combined;
        findMaterialNode(authored.graph,mapping)->uv={};
        const auto baseline=capture(authored);const auto program=bindings[0].graph->programs;
        grayscale(baseline,true);
        // All layers contribute to this single generated image and sampler.
        check(bindings[0].graph->sources.size()==1,"Combined Grunge required more than one generated texture binding.");
        for(const auto layer:{&MaterialGrungeNode::dirtSpecksOpacity,&MaterialGrungeNode::stainsOpacity,
            &MaterialGrungeNode::scratchesOpacity,&MaterialGrungeNode::leaksOpacity}) {
            auto reduced=combined;reduced.*layer=0;findMaterialNode(authored.graph,grunge)->grunge=reduced;
            check(changedPixels(baseline,capture(authored),1)>20,"Disabling a Grunge layer did not change the combined image.");
            check(bindings[0].graph->programs==program,"Editing a Grunge layer recompiled the material shader.");
            findMaterialNode(authored.graph,grunge)->grunge=combined;
            check(capture(authored).rgba==baseline.rgba,"Restoring a Grunge layer did not restore the combined image.");
        }
        findMaterialNode(authored.graph,grunge)->grunge.seed=37;const auto seeded=capture(authored);
        check(changedPixels(baseline,seeded)>20,"Changing Grunge Seed did not change the generated pattern.");
        findMaterialNode(authored.graph,grunge)->grunge.seed=0;
        check(capture(authored).rgba==baseline.rgba,"Restoring Grunge Seed failed to restore the deterministic pattern.");
        findMaterialNode(authored.graph,grunge)->grunge.coverage=0;
        check(grayscale(capture(authored),false)<=1,"Grunge Coverage zero did not produce black.");
        findMaterialNode(authored.graph,grunge)->grunge.coverage=.5f;
        for(const auto tiling:{1,64}) {
            findMaterialNode(authored.graph,grunge)->grunge.tiling=tiling;
            findMaterialNode(authored.graph,mapping)->uv.offset={};const auto repeated=capture(authored);
            for(const auto offset:{Vec3{1,0,0},Vec3{0,-1,0},Vec3{-2,3,0}}) {
                findMaterialNode(authored.graph,mapping)->uv.offset=offset;const auto translated=capture(authored);
                check(changedPixels(repeated,translated)==0,
                    "Grunge lost seamless integer repetition under positive/negative UV translation or maximum Tiling.");
            }
        }
        findMaterialNode(authored.graph,grunge)->grunge.tiling=1;
        findMaterialNode(authored.graph,mapping)->uv.offset={};
        for(const auto mode:{MaterialUvMode::UV0,MaterialUvMode::UV1,MaterialUvMode::LocalTriplanar,MaterialUvMode::WorldTriplanar}) {
            findMaterialNode(authored.graph,mapping)->uv.mode=mode;grayscale(capture(authored),true);
            check(bindings[0].graph->programs==program,"Grunge layer/settings/mapping edits recompiled unchanged topology.");
        }
        findMaterialNode(authored.graph,mapping)->uv={};
        const auto scalar=capture(authored);
        connectMaterialNodes(authored.graph,grunge,MaterialPin::RGB,output,MaterialPin::BaseColor);
        check(capture(authored).rgba==scalar.rgba,"Grunge Value and replicated RGB outputs produced different patterns.");
        connectMaterialNodes(authored.graph,color,MaterialPin::Value,output,MaterialPin::BaseColor);
    }
}
void exercise(SDL_Window* window,const ShaderCompileConfig& config){
    // Outstanding readback destinations must outlive Renderer on failure.
    std::vector<std::shared_ptr<DecodedImage>> destinations;ResourceManager resources;Renderer renderer(window,nullptr,selectedTestGraphicsBackend());MaterialGraphPrograms programs(resources,config);
    TextureStreamer grungeTextures(resources,{});renderer.resizeViewport(Size,Size);renderer.resizeGameViewport(Size,Size);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=902;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.5f,.5f,.5f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint32_t frame{};const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=bgfx::frame();};
    const auto read=[&](bgfx::TextureHandle target,std::uint16_t width=Size,std::uint16_t height=Size){
        Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Mapping readback allocation failed.");
        auto pixels=std::make_shared<DecodedImage>(DecodedImage{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)});destinations.push_back(pixels);
        bgfx::blit(ReadView,staging.handle,0,0,target,0,0,width,height);const auto ready=bgfx::readTexture(staging.handle,pixels->rgba.data());
        check(ready!=UINT32_MAX,"Mapping readback is unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *pixels;}
        throw std::runtime_error("Mapping readback timed out.");
    };
    auto mesh=quad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> surfaces=mesh->materials;auto& material=surfaces.front();std::vector<ModelMaterialTextures> bindings(1);
    RenderModelInstance instance;instance.entityId=11;instance.model.asset=mesh;instance.materials=&surfaces;instance.textures=&bindings;instance.shading=MaterialShading::Unlit;
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.fov=45;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.environment.enabled=false;

    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&instance,1);
    const auto capture=[&]{renderer.renderGame(scene);return read(renderer.gameViewportTexture());};
    const auto reset=[&]{material.uvScale={1,1,1};material.uvOffset={};material.uvRotation={};material.triplanar=false;material.triplanarWorld=false;};
    constexpr std::array<std::uint8_t,16> colors{{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255}};
    Texture color{bgfx::createTexture2D(2,2,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_SRGB,bgfx::copy(colors.data(),16))};check(bgfx::isValid(color.handle),"Mapping color texture allocation failed.");
    const std::uint32_t point=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
    bindings[0].handles[0]=color.handle;bindings[0].flags.fill(point);
    const Vec3 position{-.66f,.54f,0};
    const auto expect=[&](Vec3 p,unsigned channel,const char* reason){const auto rgb=sample(capture(),settings.camera,p);check(rgb[channel]>170 && rgb[(channel+1)%3]<5 && rgb[(channel+2)%3]<5,reason);};
    expect(position,0,"Identity mapping changed imported UV0.");
    material.uvScale={2,3,1};expect(position,2,"Material-wide UV scale was not applied.");
    reset();material.uvOffset={.5f,0,0};expect(position,1,"Material-wide UV offset was not applied.");
    reset();material.uvRotation={0,0,90};material.uvOffset={1,0,0};expect(position,1,"UV Z rotation did not precede offset.");
    reset();material.uvScale={-1,1,1};material.uvOffset={1,0,0};expect(position,1,"Negative UV scale did not mirror the texture.");
    reset();material.baseColorTexture.texCoord=1;expect(position,1,"Mapping lost the imported UV1 selection.");material.baseColorTexture.texCoord=0;
    material.triplanar=true;expect({.3f,.3f,0},2,"Triplanar projection did not use local mesh position.");
    instance.model.transform=math::translation({3,2,0});settings.camera.position={3,2,3};settings.camera.target={3,2,0};
    expect({3.3f,2.3f,0},2,"Triplanar texture moved when its entity translated.");
    instance.model.transform={};settings.camera.position={0,0,3};settings.camera.target={};
    material.uvRotation={0,0,180};expect({.3f,.3f,0},1,"Triplanar rotation was not shared by its projections.");
    reset();material.triplanar=true;material.triplanarWorld=true;
    const auto anchored=capture();
    const auto parent=math::translation({.2f,-.1f,0})*math::rotation(math::axisAngle({0,0,1},.7f))*math::scaling({1.7f,2.1f,1});
    const auto child=math::rotation(math::axisAngle({0,0,1},-.4f))*math::scaling({-1.2f,.9f,1});
    const std::array worldTransforms{math::scaling({2,3,.8f}),math::translation({.5f,.25f,0})*math::scaling({2,3,1}),parent*child};
    for(const auto& transform:worldTransforms) {
        instance.model.transform=transform;const auto moved=capture();
        for(const auto p:{Vec3{.3f,.3f,0},Vec3{.7f,.3f,0},Vec3{-.3f,-.3f,0}})
            check(difference(sample(anchored,settings.camera,p),sample(moved,settings.camera,p))<=3,
                "World triplanar tiling changed under scale, translation or accumulated parent rotation/reflection.");
    }
    instance.model.transform=math::translation({.5f,0,0});material.triplanarWorld=false;
    check(difference(sample(anchored,settings.camera,{.3f,.3f,0}),sample(capture(),settings.camera,{.3f,.3f,0}))>100,
        "Local triplanar unexpectedly became world-anchored.");
    instance.model.transform={};
    reset();bindings[0].handles[0]=BGFX_INVALID_HANDLE;instance.shading=MaterialShading::StandardPbr;
    std::array<DirectionalLight,1> lights{{{{-1,0,-1},{1,1,1},4,true}}};scene.directionalLights=lights;
    constexpr std::array<std::uint8_t,4> flatPixel{{128,128,255,255}},tiltedPixel{{255,128,255,255}};
    Texture flat{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(flatPixel.data(),4))};
    Texture tilted{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(tiltedPixel.data(),4))};
    const auto neutral=sample(capture(),settings.camera,{});bindings[0].handles[2]=flat.handle;
    material.uvScale={2,-3,4};material.uvRotation={27,42,73};material.triplanar=true;
    check(difference(neutral,sample(capture(),settings.camera,{}))<=3,"Flat triplanar normal changed the original smooth normal.");
    auto smooth=quad();smooth->id="material-mapping-smooth-normals";
    constexpr std::array<Vec3,4> smoothNormals{{{-.7f,-.2f,1},{.5f,-.3f,1},{.5f,.7f,1},{-.4f,.5f,1}}};
    for(unsigned i=0;i<4;++i)smooth->primitives[0].vertices[i].normal=math::normalized(smoothNormals[i]);
    check(renderer.prepareModel(*smooth),renderer.modelStatus());instance.model.asset=smooth;
    for(bool world:{false,true}) for(float reflection:{-1.f,1.f}) {
        material.triplanarWorld=world;
        instance.model.transform=math::scaling({.9f*reflection,1.2f,.7f});bindings[0].handles[2]=BGFX_INVALID_HANDLE;
        const auto smoothBase=capture();bindings[0].handles[2]=flat.handle;const auto smoothFlat=capture();
        for(const auto p:{Vec3{},Vec3{-.2f,.2f,0},Vec3{.2f,-.2f,0}})
            check(difference(sample(smoothBase,settings.camera,p),sample(smoothFlat,settings.camera,p))<=3,
                "Flat triplanar normal changed a curved smooth normal under nonuniform/reflected world scale.");
    }
    instance.model.asset=mesh;instance.model.transform={};
    reset();bindings[0].handles[2]=tilted.handle;const auto forward=sample(capture(),settings.camera,{});
    material.uvRotation[2]=180;const auto backward=sample(capture(),settings.camera,{});
    check(forward[0]>backward[0]+50,"Rotating ordinary UVs did not reorient the normal map.");
    reset();material.uvScale[0]=-1;check(difference(backward,sample(capture(),settings.camera,{}))<=3,"Mirrored UVs lost normal-map handedness.");
    reset();material.triplanar=true;check(difference(forward,sample(capture(),settings.camera,{}))<=3,"Triplanar Z normal was not reoriented into the model basis.");
    material.uvRotation[2]=180;check(difference(backward,sample(capture(),settings.camera,{}))<=3,"Triplanar normal rotation was not inverted back to local space.");
    reset();material.triplanar=true;material.triplanarWorld=true;
    for(const auto& transform:worldTransforms) {
        instance.model.transform=transform;
        check(difference(forward,sample(capture(),settings.camera,{}))<=3,
            "World triplanar normal rotated or reflected with the entity instead of retaining world axes.");
    }
    instance.model.transform=math::rotation(math::axisAngle({0,1,0},std::numbers::pi_v<float>*.5f))*math::scaling({2,3,.7f});
    settings.camera.position={3,0,0};lights[0].direction={-1,0,1};
    check(difference(forward,sample(capture(),settings.camera,{}))<=3,"World triplanar X projection normal has incorrect axis reorientation.");
    instance.model.transform={};settings.camera.position={0,0,3};lights[0].direction={-1,0,-1};
    // Each non-color slot must use the same transformed domain and retain its
    // own data role. Uniform two-column maps make an offset swap measurable.
    reset();bindings[0].handles.fill(BGFX_INVALID_HANDLE);scene.directionalLights={};settings.environment.enabled=true;
    constexpr std::array<std::uint8_t,8> values{{0,0,0,255,255,255,255,255}};
    Texture data{bgfx::createTexture2D(2,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(values.data(),8))};
    material.metallic=0;material.roughness=1;material.occlusionStrength=1;bindings[0].handles[3]=data.handle;
    const auto occluded=sample(capture(),settings.camera,position);material.uvOffset[0]=.5f;const auto open=sample(capture(),settings.camera,position);
    check(open[0]>occluded[0]+50,"AO map ignored the shared offset.");
    bindings[0].handles[3]=BGFX_INVALID_HANDLE;bindings[0].handles[4]=color.handle;material.emissive={1,1,1};settings.environment.enabled=false;
    reset();expect(position,0,"Emissive map did not use the standard transformed domain.");material.uvOffset[0]=.5f;expect(position,1,"Emissive map ignored the shared offset.");
    bindings[0].handles[4]=BGFX_INVALID_HANDLE;material.emissive={};scene.directionalLights=lights;material.metallic=1;bindings[0].handles[1]=data.handle;
    reset();const auto packedA=sample(capture(),settings.camera,position);material.uvOffset[0]=.5f;const auto packedB=sample(capture(),settings.camera,position);
    check(difference(packedA,packedB)>30,"Packed metallic/roughness map ignored the shared offset.");
    // Color and ID picking must agree on a transformed alpha silhouette.
    reset();bindings[0].handles.fill(BGFX_INVALID_HANDLE);material.metallic=0;instance.shading=MaterialShading::Unlit;
    constexpr std::array<std::uint8_t,8> alphaPixels{{255,255,255,0,255,255,255,255}};
    Texture alpha{bgfx::createTexture2D(2,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(alphaPixels.data(),8))};
    bindings[0].handles[0]=alpha.handle;material.alphaMask=true;material.alphaCutoff=.5f;
    std::uint64_t serial{};
    const auto picked=[&](Vec3 p){
        const auto screen=math::project(p,settings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*settings.camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
        check(bool(screen),"Mask pick point failed projection.");check(renderer.requestPick({1,1,++serial,static_cast<std::uint32_t>((*screen)[0]),static_cast<std::uint32_t>((*screen)[1]),renderer.streamingSignature()}),renderer.pickingStatus());
        renderer.render(scene);std::optional<PickResult> result;for(unsigned i=0;i<120 && !result;++i){tick();result=renderer.pollPick(frame);}check(bool(result),"Mask pick timed out.");return result->entityId;
    };
    check(picked(position)==0,"Identity alpha mask returned a transparent entity.");material.uvOffset[0]=.5f;
    check(picked(position)==instance.entityId && sample(capture(),settings.camera,position)[0]>170,"Transformed color/pick alpha masks disagree.");
    reset();material.triplanar=true;check(picked({.3f,.3f,0})==0,"Triplanar alpha pick did not use local projection.");
    material.uvOffset[0]=.5f;check(picked({.3f,.3f,0})==instance.entityId,"Triplanar alpha pick ignored mapping edits.");
    reset();material.triplanar=true;material.triplanarWorld=true;
    for(const auto& transform:worldTransforms) {
        instance.model.transform=transform;
        check(picked({.3f,.3f,0})==0 && picked({.7f,.3f,0})==instance.entityId,
            "World triplanar picking mask changed under accumulated world transforms.");
        const auto maskImage=capture();
        check(sample(maskImage,settings.camera,{.3f,.3f,0})[0]<5 && sample(maskImage,settings.camera,{.7f,.3f,0})[0]>170,
            "World triplanar color/picking mask parity was lost.");
    }
    instance.model.transform=math::scaling({2,3,1});material.triplanarWorld=false;
    check(picked({.7f,.3f,0})==0,"Local triplanar picking mask lost its authored local domain.");
    instance.model.transform={};
    // Sprite UV mapping shares the same scale/rotation/offset behavior.
    scene.instances={};scene.directionalLights={};settings.environment.enabled=false;
    RenderParticleMaterial sprite; sprite.colorTexture=color.handle;sprite.samplerFlags=point;
    std::array<RenderParticle,1> particles{{{{0,0,0},2,{1,1,1,1},0}}};particles[0].billboardMode=ParticleBillboardMode::Enabled;
    scene.particles=particles;scene.particleMaterials=std::span(&sprite,1);
    expect(position,0,"Sprite identity UV mapping changed.");sprite.uvOffset={.5f,0,0};
    expect(position,1,"Sprite material did not retain its shared 2D offset.");
    // A shared transparency queue must interleave meshes with particles rather
    // than submitting all sprites last. Linear presentation gives known OVER
    // channel values: half-opacity front=.5, half-opacity rear=.25.
    reset();bindings[0].handles.fill(BGFX_INVALID_HANDLE);material.alphaMask=false;
    material.baseColor={1,0,0,.5f};instance.shading=MaterialShading::Unlit;instance.blend=MaterialBlend::Alpha;
    instance.model.transform={};scene.instances=std::span(&instance,1);
    sprite=RenderParticleMaterial{};sprite.baseColor={0,0,1,.5f};sprite.blend=MaterialBlend::Alpha;
    settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;
    settings.environment.background.color={};settings.environment.background.show=false;
    particles[0].position={0,0,.5f};
    const auto particleFront=sample(capture(),settings.camera,{});
    check(std::abs(particleFront[0]-137)<=4 && std::abs(particleFront[2]-188)<=4,
        "Front particle did not composite once over the rear translucent mesh.");
    instance.model.transform=math::translation({0,0,.5f});particles[0].position={};
    const auto meshFront=sample(capture(),settings.camera,{});
    check(std::abs(meshFront[0]-188)<=4 && std::abs(meshFront[2]-137)<=4,
        "Transparent queue submitted particles after a nearer translucent mesh.");
    // Equal-depth stable ties place the mesh first. The particle must still
    // pass LESS: writing mesh depth would suppress the following blue sprite.
    instance.model.transform={};
    const auto equalDepth=sample(capture(),settings.camera,{});
    check(equalDepth[2]>equalDepth[0]+35,"A translucent mesh wrote visible depth and rejected an equal-depth particle.");
    material.baseColor[3]=.005f;
    check(picked({})==0,"Transparent mesh picking retained coverage below 0.01.");
    material.baseColor[3]=.5f;
    check(picked({})==instance.entityId,"Transparent mesh picking discarded visible coverage.");
    // Opaque geometry is submitted first even when extraction lists a near
    // transparent object before its opaque background.
    auto opaqueSurfaces=surfaces;opaqueSurfaces[0].baseColor={0,1,0,1};
    auto back=instance;back.materials=&opaqueSurfaces;back.blend=MaterialBlend::Opaque;back.model.transform=math::translation({0,0,-.5f});
    const std::array mixedInstances{instance,back};scene.instances=mixedInstances;scene.particles={};
    const auto overOpaque=sample(capture(),settings.camera,{});
    check(std::abs(overOpaque[0]-188)<=4 && std::abs(overOpaque[1]-188)<=4,
        "Opaque extraction order overwrote the queued transparent surface.");
    // Additive RGB must leave coverage alone. A green display matte reveals
    // accidental alpha accumulation without needing access to the HDR target.
    settings.environment.background.color={0,.5f,0};scene.instances={};
    const auto emptyMatte=sample(capture(),settings.camera,{});
    instance.blend=MaterialBlend::Additive;sprite.blend=MaterialBlend::Additive;
    scene.instances=std::span(&instance,1);scene.particles=particles;
    const auto additive=sample(capture(),settings.camera,{});
    check(std::abs(additive[1]-emptyMatte[1])<=2 && additive[0]>180 && additive[2]>180,
        "Additive mesh/particle blending changed destination coverage or premultiplied twice.");
    // RGB * a linear Constant with explicit partial alpha. Keep the regression
    // self-contained so authored texture imports/deletions cannot break it.
    DecodedImage smokeImage{8,8,std::vector<std::uint8_t>(8*8*4)};
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        const auto at=(y*8+x)*4;
        smokeImage.rgba[at]=static_cast<std::uint8_t>(200+x*7);
        smokeImage.rgba[at+1]=static_cast<std::uint8_t>(180+y*7);
        smokeImage.rgba[at+2]=static_cast<std::uint8_t>(170+x*6+y);
        smokeImage.rgba[at+3]=static_cast<std::uint8_t>((x+y)%3==0?0:48+(x*13+y*7)%81);
    }
    const auto maximumAlpha=[&]{unsigned result{};for(std::size_t i=3;i<smokeImage.rgba.size();i+=4)result=std::max(result,unsigned(smokeImage.rgba[i]));return result;}();
    check(maximumAlpha>0 && maximumAlpha<255,"Smoke regression image no longer has partial real alpha.");
    Texture smoke{bgfx::createTexture2D(static_cast<std::uint16_t>(smokeImage.width),static_cast<std::uint16_t>(smokeImage.height),false,1,
        bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_SRGB,bgfx::copy(smokeImage.rgba.data(),static_cast<std::uint32_t>(smokeImage.rgba.size())))};
    check(bgfx::isValid(smoke.handle),"Smoke regression texture allocation failed.");
    MaterialAsset authored;authored.id="native-explicit-sprite";
    const auto root=addMaterialNode(authored.graph,MaterialNodeKind::Output).id;
    auto& output=materialOutputNode(authored.graph).output;output.shading=MaterialShading::Unlit;output.blend=MaterialBlend::Alpha;
    const auto texture=addMaterialNode(authored.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(authored.graph,texture)->texture.image.id="assets/textures/partial-alpha-fixture.png";
    const auto factor=addMaterialNode(authored.graph,MaterialNodeKind::Color).id;
    findMaterialNode(authored.graph,factor)->color={.8355646729469299f,.3041378855705261f,.3041378855705261f,1};
    const auto product=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
    connectMaterialNodes(authored.graph,texture,MaterialPin::RGB,product,MaterialPin::Left);
    connectMaterialNodes(authored.graph,factor,MaterialPin::RGB,product,MaterialPin::Right);
    connectMaterialNodes(authored.graph,product,MaterialPin::Value,root,MaterialPin::BaseColor);
    const auto prepare=[&](const MaterialAsset& source,bgfx::TextureHandle image=BGFX_INVALID_HANDLE) {
        const auto generated=generateMaterialGraphShader(source);
        const bool generatedImage=std::any_of(generated.textures.begin(),generated.textures.end(),[](const auto& binding){return binding.grunge.has_value();});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
        MaterialGraphProgramResult published;
        // Recipe generation takes enough time for the ordinary interactive ->
        // optimized replacement to finish between captures. Stabilize that
        // expected publication before checking parameter-only program reuse.
        const auto shaderReady=[&]{return published.current&&published.programs&&(!generatedImage||published.programs->optimized);};
        do {
            tick();published=programs.request(generated);
            if(!published.current && published.diagnostic!="Compiling material graph...")throw std::runtime_error(published.diagnostic);
            if(!shaderReady())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }while(!shaderReady() && std::chrono::steady_clock::now()<deadline);
        check(shaderReady(),"Generated sprite shader timed out.");
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=published.programs;packet->uniforms=generated.uniforms;
        packet->sources=generated.textures;packet->shading=materialOutputNode(source.graph).output.shading;packet->blend=materialOutputNode(source.graph).output.blend;
        packet->receiveFog=materialOutputNode(source.graph).output.receiveFog;
        packet->signature=published.programs->signature;
        for(const auto& binding:generated.textures) {
            if(binding.grunge) {
                const auto settings=*binding.grunge;PreparedTexture texture;
                const auto key=std::to_string(grungeTextureKey(settings));
                do {
                    resources.pump();grungeTextures.beginFrame();
                    texture=grungeTextures.requestGenerated(key,[settings]{return generateGrungeTexture(settings);},GrungeTextureEdge,true,GrungeTextureEdge);
                    if(!texture.diagnostic.empty())throw std::runtime_error(texture.diagnostic);
                    grungeTextures.advance();tick();
                    if(!texture.complete)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }while(!texture.complete && std::chrono::steady_clock::now()<deadline);
                check(texture.complete&&texture.sourceReady&&bgfx::isValid(texture.handle),"Grunge Texture 2D preparation timed out.");
                packet->textures[binding.slot]=texture.handle;packet->leases[binding.slot]=texture.lease;
                packet->sourceFingerprints[binding.slot]=texture.sourceFingerprint;packet->textureSignatures[binding.slot]=texture.signature;
                packet->flags[binding.slot]=0;
            } else {
                check(bgfx::isValid(image),"Regression graph needs a supplied image.");
                packet->textures[binding.slot]=image;packet->flags[binding.slot]=point|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
            }
        }
        return packet;
    };
    std::cerr<<"[material mapping] Material fog.\n"<<std::flush;
    exerciseMaterialFog(renderer,mesh,read,prepare);

    std::cerr<<"[material mapping] Particle continuity across wall fade transitions.\n"<<std::flush;
    exerciseParticleFadeTransitions(renderer,mesh,read,prepare);
    const auto pickScene=[&](const RenderScene& target,Vec3 p,PickMode mode) {
        const auto screen=math::project(p,target.settings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*target.settings.camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
        check(bool(screen),"Instance-opacity pick point failed projection.");
        check(renderer.requestPick({1,1,++serial,static_cast<std::uint32_t>((*screen)[0]),static_cast<std::uint32_t>((*screen)[1]),renderer.pickResourceSignature(mode),mode}),renderer.pickingStatus());
        renderer.render(target);std::optional<PickResult> result;
        for(unsigned i=0;i<120 && !result;++i){tick();result=renderer.pollPick(frame);}
        check(bool(result),"Instance-opacity pick timed out.");return result->entityId;
    };
    std::cerr<<"[material mapping] Per-view model instance opacity.\n"<<std::flush;
    exerciseInstanceOpacity(renderer,mesh,read,prepare,pickScene);
    std::cerr<<"[material mapping] Image adjustments and generated Grunge.\n"<<std::flush;
    exerciseImageAdjustmentsAndGrunge(renderer,mesh,read,prepare);
    exerciseCollisionOverlays(renderer,mesh,read);
    std::cerr<<"[material mapping] Generated particle coverage and material preview.\n"<<std::flush;
    settings.environment.background.color={};settings.environment.enabled=false;scene.directionalLights={};scene.pointLights={};scene.instances={};
    particles[0].position={};particles[0].color={0,1,0,0};sprite=RenderParticleMaterial{};
    scene.particles=particles;scene.particleMaterials=std::span(&sprite,1);
    sprite.graph=prepare(authored,smoke.handle);const auto disconnected=capture();
    connectMaterialNodes(authored.graph,texture,MaterialPin::A,root,MaterialPin::Opacity);
    sprite.graph=prepare(authored,smoke.handle);const auto connected=capture();
    std::uint64_t disconnectedRed{},connectedRed{};
    for(std::size_t i=0;i<connected.rgba.size();i+=4){disconnectedRed+=disconnected.rgba[i];connectedRed+=connected.rgba[i];}
    check(disconnectedRed>10000 && connectedRed<disconnectedRed*.85,"Smoke texture alpha connection did not reduce generated sprite coverage.");
    disconnectMaterialInput(authored.graph,root,MaterialPin::Opacity);
    sprite.graph=prepare(authored,smoke.handle);check(capture().rgba==disconnected.rgba,"Disconnecting smoke alpha did not restore the same authored RGB-only material.");
    // Plain constants are complete quads and ignore instance color/opacity.
    disconnectMaterialInput(authored.graph,root,MaterialPin::BaseColor);
    materialOutputNode(authored.graph).output.baseColor={1,1,1};
    sprite.graph=prepare(authored);const auto plain=capture();
    const auto corner=sample(plain,settings.camera,{.7f,.7f,0});
    check(corner[0]>250 && corner[1]>250 && corner[2]>250,"Constant sprite retained implicit color, opacity or procedural-disc behavior.");
    sprite.graph.reset();sprite.baseColor={1,1,1,1};sprite.blend=MaterialBlend::Alpha;
    const auto fallbackCorner=sample(capture(),settings.camera,{.7f,.7f,0});
    check(fallbackCorner[0]>250 && fallbackCorner[1]>250 && fallbackCorner[2]>250,"Plain fallback implicitly culled or tinted zero-alpha particle input.");
    // The same Particle Color graph evaluates to white on ordinary geometry.
    const auto pc=addMaterialNode(authored.graph,MaterialNodeKind::ParticleColor).id;
    connectMaterialNodes(authored.graph,pc,MaterialPin::RGB,root,MaterialPin::BaseColor);
    connectMaterialNodes(authored.graph,pc,MaterialPin::A,root,MaterialPin::Opacity);
    const auto saturate=addMaterialNode(authored.graph,MaterialNodeKind::Clamp).id;
    findMaterialNode(authored.graph,saturate)->left.value[0]=2;
    const auto clampedAlpha=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
    connectMaterialNodes(authored.graph,saturate,MaterialPin::Value,clampedAlpha,MaterialPin::Left);
    connectMaterialNodes(authored.graph,pc,MaterialPin::A,clampedAlpha,MaterialPin::Right);
    connectMaterialNodes(authored.graph,clampedAlpha,MaterialPin::Value,root,MaterialPin::Opacity);
    sprite.graph=prepare(authored);particles[0].color={1,0,0,.25f};
    const auto explicitColor=sample(capture(),settings.camera,{});
    check(std::abs(explicitColor[0]-137)<=3 && explicitColor[1]<3 && explicitColor[2]<3,"Explicit Particle Color RGB/alpha did not produce a single premultiplication.");
    bindings[0].graph=sprite.graph;material.baseColor={1,1,1,1};instance.model.transform={};scene.particles={};scene.instances=std::span(&instance,1);
    const auto meshColor=sample(capture(),settings.camera,{});
    check(meshColor[0]>250 && meshColor[1]>250 && meshColor[2]>250,"Particle Color was not neutral white on a mesh.");
    bindings[0].graph.reset();scene.instances={};scene.particles=particles;
    disconnectMaterialInput(authored.graph,root,MaterialPin::BaseColor);disconnectMaterialInput(authored.graph,root,MaterialPin::Opacity);
    const auto radial=addMaterialNode(authored.graph,MaterialNodeKind::RadialGradient).id;
    materialOutputNode(authored.graph).output.blend=MaterialBlend::Mask;
    connectMaterialNodes(authored.graph,radial,MaterialPin::Value,root,MaterialPin::OpacityMask);
    sprite.graph=prepare(authored);const auto masked=capture();
    check(sample(masked,settings.camera,{})[0]>250 && sample(masked,settings.camera,{.7f,.7f,0})[0]<3,"Explicit radial Opacity Mask did not cut the sprite quad.");
    // Sparks keeps Lit/Masked coverage: its particle alpha modulates the radial
    // mask, and the cutoff discards the complete surface, including emission.
    // Reuse one prepared packet throughout so only instance alpha changes.
    {
        auto sparks=authored;
        auto& sparksOutput=materialOutputNode(sparks.graph).output;
        sparksOutput.shading=MaterialShading::StandardPbr;sparksOutput.alphaCutoff=.5f;sparksOutput.emission={2,.2f,0};
        connectMaterialNodes(sparks.graph,pc,MaterialPin::RGB,root,MaterialPin::BaseColor);
        const auto particleMask=addMaterialNode(sparks.graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(sparks.graph,radial,MaterialPin::Value,particleMask,MaterialPin::Left);
        connectMaterialNodes(sparks.graph,pc,MaterialPin::A,particleMask,MaterialPin::Right);
        connectMaterialNodes(sparks.graph,particleMask,MaterialPin::Value,root,MaterialPin::OpacityMask);
        const auto previousGraph=sprite.graph;const auto previousColor=particles[0].color;
        sprite.graph=prepare(sparks);const auto sparksProgram=sprite.graph->programs;
        scene.particles={};const auto empty=capture();scene.particles=particles;
        particles[0].color={1,.25f,.05f,0};
        check(capture().rgba==empty.rgba,"Zero particle alpha left visible Lit/Masked Sparks emission.");
        particles[0].color[3]=1;const auto visible=capture();
        check(sample(visible,settings.camera,{})[0]>250 && sample(visible,settings.camera,{.7f,.7f,0})[0]<3,
            "Full particle alpha did not preserve the Lit/Masked Sparks center and clipped corners.");
        particles[0].color[3]=.49f;
        check(capture().rgba==empty.rgba,"Particle alpha below the Sparks mask cutoff did not discard the entire sprite.");
        particles[0].color[3]=1;
        check(capture().rgba==visible.rgba && sprite.graph->programs==sparksProgram,
            "Changing Sparks particle alpha did not restore coverage using the same compiled program.");
        // Without the radial connection, particle alpha controls the entire
        // billboard. Its shape must remain the user's authored full quad.
        connectMaterialNodes(sparks.graph,pc,MaterialPin::A,root,MaterialPin::OpacityMask);
        sprite.graph=prepare(sparks);const auto directProgram=sprite.graph->programs;
        particles[0].color[3]=0;
        check(capture().rgba==empty.rgba,"Zero direct particle alpha left visible Lit/Masked Sparks emission.");
        particles[0].color[3]=1;const auto fullQuad=capture();
        check(sample(fullQuad,settings.camera,{})[0]>250 && sample(fullQuad,settings.camera,{.7f,.7f,0})[0]>250,
            "Direct particle alpha retained an unauthored radial sprite shape.");
        particles[0].color[3]=.49f;
        check(capture().rgba==empty.rgba,"Direct particle alpha below the mask cutoff did not discard the full quad.");
        particles[0].color[3]=1;
        check(capture().rgba==fullQuad.rgba && sprite.graph->programs==directProgram,
            "Direct particle alpha edits did not restore full coverage using the same compiled program.");
        sprite.graph=previousGraph;particles[0].color=previousColor;
    }
    materialOutputNode(authored.graph).output.blend=MaterialBlend::Opaque;
    sprite.graph=prepare(authored);check(sample(capture(),settings.camera,{.7f,.7f,0})[0]>250,"Opaque sprite incorrectly evaluated its inactive mask branch.");
    // Lit sprites use the same direct lighting and normal mapping as meshes.
    materialOutputNode(authored.graph).output.shading=MaterialShading::StandardPbr;
    sprite.graph=prepare(authored);const auto darkSprite=sample(capture(),settings.camera,{});
    scene.directionalLights=lights;
    const auto litSprite=sample(capture(),settings.camera,{});
    check(litSprite[0]>darkSprite[0]+30,"Lit sprite did not receive directional lighting.");
    const auto normalNode=addMaterialNode(authored.graph,MaterialNodeKind::Texture2D).id;
    auto* normalDefinition=findMaterialNode(authored.graph,normalNode);normalDefinition->texture.role=MaterialTextureRole::Normal;normalDefinition->texture.image.id="assets/textures/native-normal.png";
    connectMaterialNodes(authored.graph,normalNode,MaterialPin::Normal,root,MaterialPin::Normal);
    sprite.graph=prepare(authored,flat.handle);check(difference(litSprite,sample(capture(),settings.camera,{}))<=3,"Flat sprite normal map altered the facing normal.");
    sprite.graph=prepare(authored,tilted.handle);check(difference(litSprite,sample(capture(),settings.camera,{}))>20,"Sprite normal mapping did not affect lighting.");
    // Opaque sprite depth rejects later transparent meshes behind the billboard.
    scene.directionalLights={};materialOutputNode(authored.graph).output.shading=MaterialShading::Unlit;materialOutputNode(authored.graph).output.baseColor={0,0,1};
    sprite.graph=prepare(authored);instance.model.transform=math::translation({0,0,-.5f});instance.blend=MaterialBlend::Alpha;material.baseColor={1,0,0,.5f};
    scene.instances=std::span(&instance,1);const auto depth=sample(capture(),settings.camera,{});
    check(depth[2]>250 && depth[0]<3,"Opaque sprite did not write visible depth before transparent mesh submission.");
    // Facing is an instance geometry choice, independent of material program.
    // Exercise both embedded fallback and generated programs using the same
    // camera changes; Disabled must retain the world plane, not follow the eye.
    scene.instances={};scene.directionalLights={};scene.pointLights={};
    MaterialAsset oriented;oriented.id="native-oriented-sprite";
    const auto orientedRoot=addMaterialNode(oriented.graph,MaterialNodeKind::Output).id;
    auto& orientedOutput=materialOutputNode(oriented.graph).output;
    orientedOutput.shading=MaterialShading::Unlit;orientedOutput.blend=MaterialBlend::Opaque;orientedOutput.baseColor={1,1,1};
    sprite=RenderParticleMaterial{};sprite.blend=MaterialBlend::Opaque;sprite.baseColor={1,1,1,1};
    const auto covered=[](const DecodedImage& image){std::size_t result{};for(std::size_t i=0;i<image.rgba.size();i+=4)if(image.rgba[i]>8)++result;return result;};
    for(const bool generated:{false,true}) {
        sprite.graph=generated?prepare(oriented):nullptr;
        particles[0].position={};particles[0].right={1,0,0};particles[0].up={0,1,0};particles[0].rotation=0;
        particles[0].billboardMode=ParticleBillboardMode::Disabled;
        settings.camera.position={0,0,3};settings.camera.target={};
        const auto front=covered(capture());check(front>3000,"World-plane particle did not render from its front.");
        settings.camera.position={3,0,0};
        check(covered(capture())<front/100,"Disabled billboarding still turned the quad toward the camera.");
        particles[0].billboardMode=ParticleBillboardMode::Enabled;
        const auto facing=capture();check(covered(facing)>front*.95,"Enabled billboard failed to follow a side camera.");
        particles[0].right={0,1,0};particles[0].up={0,0,1};
        check(capture().rgba==facing.rgba,"Enabled billboard inherited an authored emitter orientation.");
        particles[0].billboardMode=ParticleBillboardMode::Disabled;particles[0].right={0,0,-1};particles[0].up={0,1,0};
        check(covered(capture())>front*.95,"Disabled quad ignored its explicit world-space orientation.");
        particles[0].billboardMode=ParticleBillboardMode::FixedY;
        check(covered(capture())>front*.95,"Fixed-Y billboard did not turn around the vertical axis.");
        settings.camera.position={3,3,0};
        const auto fixedSlope=covered(capture());particles[0].billboardMode=ParticleBillboardMode::Enabled;
        const auto fullSlope=covered(capture());
        check(fixedSlope>500 && fixedSlope<fullSlope*.85,"Fixed-Y billboard tilted with the camera instead of keeping world Y upright.");
        particles[0].billboardMode=ParticleBillboardMode::FixedY;settings.camera.position={0,3,0};
        check(covered(capture())<10,"Fixed-Y pole fallback produced nonfinite or expanded geometry.");
        settings.camera.position={3,0,0};
        check(covered(capture())>front*.95,"Fixed-Y billboard failed to recover after a vertical camera pole.");
    }
    // Rotate a textured Disabled quad in its own plane; UVs follow the same
    // rotation and local triplanar normals retain that rotated tangent frame.
    particles[0].billboardMode=ParticleBillboardMode::Disabled;particles[0].right={0,0,-1};particles[0].up={0,1,0};
    const auto orientedTexture=addMaterialNode(oriented.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(oriented.graph,orientedTexture)->texture.image.id="assets/textures/orientation-fixture.png";
    connectMaterialNodes(oriented.graph,orientedTexture,MaterialPin::RGB,orientedRoot,MaterialPin::BaseColor);
    for(const bool generated:{false,true}) {
        sprite.graph=generated?prepare(oriented,color.handle):nullptr;sprite.colorTexture=color.handle;sprite.samplerFlags=point;
        particles[0].rotation=0;expect({0,.54f,.66f},0,"Oriented sprite UVs lost the authored right/up basis.");
        particles[0].rotation=std::numbers::pi_v<float>*.5f;
        expect({0,.54f,.66f},1,"Rotation did not spin a Disabled sprite within its world plane.");
    }
    disconnectMaterialInput(oriented.graph,orientedRoot,MaterialPin::BaseColor);
    materialOutputNode(oriented.graph).output.shading=MaterialShading::StandardPbr;
    materialOutputNode(oriented.graph).output.baseColor={.5f,.5f,.5f};
    particles[0].rotation=0;lights[0].direction={-1,0,-1};lights[0].intensity=1;scene.directionalLights=lights;
    sprite.graph=prepare(oriented);const auto orientedLit=sample(capture(),settings.camera,{});
    check(orientedLit[0]>40,"Lit Disabled sprite did not use its world-facing surface normal.");
    const auto orientedUv=addMaterialNode(oriented.graph,MaterialNodeKind::UV).id;
    findMaterialNode(oriented.graph,orientedUv)->uv.mode=MaterialUvMode::LocalTriplanar;
    findMaterialNode(oriented.graph,orientedTexture)->texture.role=MaterialTextureRole::Normal;
    connectMaterialNodes(oriented.graph,orientedUv,MaterialPin::UV,orientedTexture,MaterialPin::UV);
    connectMaterialNodes(oriented.graph,orientedTexture,MaterialPin::Normal,orientedRoot,MaterialPin::Normal);
    sprite.graph=prepare(oriented,flat.handle);
    check(difference(orientedLit,sample(capture(),settings.camera,{}))<=3,"Neutral local-triplanar normal changed an oriented sprite's world normal.");
    sprite.graph=prepare(oriented,tilted.handle);const auto orientedTilt=sample(capture(),settings.camera,{});
    particles[0].rotation=std::numbers::pi_v<float>;
    check(difference(orientedTilt,sample(capture(),settings.camera,{}))>10,"Local-triplanar normal did not rotate with the Disabled sprite basis.");
    // The material editor may display a coherent coarse texture packet while
    // finer requested detail is pending. Exercise its dedicated view family and
    // streamed preview geometry rather than only the ordinary scene/game path.
    check(renderer.publishMaterialPreviewEnvironment(environment),"Material preview environment did not publish.");
    renderer.resizeMaterialPreview(Size,Size);
    MaterialAsset previewMaterial;previewMaterial.id="preview-readback";
    addMaterialNode(previewMaterial.graph,MaterialNodeKind::Output);
    auto& previewOutput=materialOutputNode(previewMaterial.graph).output;previewOutput.shading=MaterialShading::Unlit;previewOutput.baseColor={0,1,0};
    canonicalizeMaterialAsset(previewMaterial);ModelMaterialTextures previewBindings;previewBindings.graph=prepare(previewMaterial);previewBindings.complete=false;
    check(materialPreviewPacketReady(previewBindings),"Coherent preview packet was rejected while detail remained pending.");
    SceneSettings previewSettings;previewSettings.camera.position={0,0,3.2f};previewSettings.camera.target={};previewSettings.camera.nearPlane=.1f;previewSettings.camera.farPlane=30;
    previewSettings.environment.enabled=false;previewSettings.environment.toneMapping=ToneMapping::Linear;
    const auto drawPreview=[&]{renderer.renderMaterialPreview(previewSettings,previewMaterial,previewBindings,MaterialPreviewMesh::Sphere,{},false);};
    for(unsigned frameIndex=0;frameIndex<8;++frameIndex){drawPreview();tick();}
    drawPreview();const auto preview=read(renderer.materialPreviewTexture());const auto previewCenter=sample(preview,previewSettings.camera,{});
    check(previewCenter[1]>240&&previewCenter[0]<5&&previewCenter[2]<5,"Dedicated material preview was blank or lost its graph surface.");
    previewSettings.camera.position={100,0,3.2f};previewSettings.camera.target={100,0,0};drawPreview();const auto movedPreview=read(renderer.materialPreviewTexture());
    check(movedPreview.rgba!=preview.rgba,"Material preview camera movement did not affect its output.");
    previewSettings.camera.position={0,0,3.2f};previewSettings.camera.target={};drawPreview();
    check(read(renderer.materialPreviewTexture()).rgba==preview.rgba,"Restoring the default preview view did not restore its visible sphere.");
    renderer.releaseMaterialPreview();
    scene.instances={};scene.particles={};sprite.graph.reset();resources.stop();
    frame=renderer.drainPending(frame);
}
}
int main(int argc,char** argv){try{
    check(argc==4,"Pass shaderc, source and include directories.");
    const auto temporary=std::filesystem::temp_directory_path()/("crimson-mapping-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(path,error);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error && resolved.parent_path()==parent && resolved.filename().string().starts_with("crimson-mapping-"))std::filesystem::remove_all(resolved,error);}} cleanup{temporary};
    ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary};
    Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson material mapping checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);check(bool(window),SDL_GetError());exercise(window.get(),config);std::cout<<"Material mapping, coverage/picking, mixed mesh/particle ordering, depth and additive checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
