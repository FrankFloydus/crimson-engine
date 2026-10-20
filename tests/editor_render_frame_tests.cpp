#include "editor/EditorRenderFrame.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ShadowLightSelection.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
}

int main() {
    try {
        using namespace crimson;
        static_assert(std::is_const_v<typename decltype(PreparedRenderFrame::world)::element_type>);
        EditorRenderFrameInput admission;
        admission.sceneCamera.position={3,4,5};admission.sceneCamera.target={20,30,40};
        admission.gameCamera=Camera{};admission.gameCamera->position={11,12,13};
        admission.gameWidth=960;admission.gameHeight=540;admission.gameVisible=true;
        require(admission.shadowPriorityOrigin()==Vec3{11,12,13},"Visible Game admission did not use its presented eye.");
        admission.gameVisible=false;
        require(admission.shadowPriorityOrigin()==Vec3{3,4,5},"Hidden Game admission did not use the Scene eye.");
        admission.gameVisible=true;admission.gameWidth=0;
        require(admission.shadowPriorityOrigin()==Vec3{3,4,5},"Collapsed Game retained point-shadow priority.");
        admission.gameWidth=960;admission.gameCamera.reset();
        require(admission.shadowPriorityOrigin()==Vec3{3,4,5},"Missing Game camera retained point-shadow priority.");

        // A first-person player can have no body mesh. A distant editor camera
        // must not steal any of the sixteen resident maps from its Game view.
        admission.sceneCamera.position={0,0,0};admission.gameCamera=Camera{};
        admission.gameCamera->position={1000,0,0};
        std::array<PointLight,32> admittedLights;
        for(unsigned i=0;i<admittedLights.size();++i) {
            auto& light=admittedLights[i];light.entityId=i+1;light.castShadows=true;
            light.position={float(i%16)+(i>=16?1000.f:0.f),0,0};light.radius=20;
        }
        const auto nearGame=selectPointShadowLights(admittedLights,admission.shadowPriorityOrigin());
        require(nearGame.count==16 && nearGame.unadmittedCount==16,"Point-shadow residency budget changed.");
        for(const auto source:nearGame.selectedSources())require(source>=16,"Bodyless Game lost a nearby shadow to the Scene camera.");
        admission.sceneCamera.position={-10000,1000,0};
        const auto movedScene=selectPointShadowLights(admittedLights,admission.shadowPriorityOrigin());
        require(nearGame.sourceIndices==movedScene.sourceIndices,"Moving only Scene changed visible Game shadow admission.");
        admission.gameVisible=false;admission.sceneCamera.position={0,0,0};
        const auto nearScene=selectPointShadowLights(admittedLights,admission.shadowPriorityOrigin());
        for(const auto source:nearScene.selectedSources())require(source<16,"Closing Game did not restore Scene-oriented shadow admission.");

        auto hover=std::make_shared<PreparedEditorModel>();
        hover->materials.resize(2);hover->textures.resize(2);
        require(!materialSlotPreviewReady(hover),"Unpublished hover candidate replaced the authored mesh.");
        hover->ready=true;hover->textures[0].complete=false;
        require(materialSlotPreviewReady(hover),"Coherent coarse hover textures unnecessarily waited for refinement.");
        hover->textures[1].graphPending=true;
        require(!materialSlotPreviewReady(hover),"Partial shader hover candidate replaced the authored material.");
        hover->textures[1].graphPending=false;hover->textures[1].sourceReady=false;
        require(!materialSlotPreviewReady(hover),"Stale texture hover candidate replaced the authored material.");
        hover->textures[1].sourceReady=true;hover->textures[1].flags[0]=0;
        require(!materialSlotPreviewReady(hover),"Missing hover texture was treated as drawable.");
        hover->textures[1].flags[0]=UINT32_MAX;hover->textures.pop_back();
        require(!materialSlotPreviewReady(hover),"Incomplete slot bindings replaced the authored mesh.");

        ExtractedScene extracted;
        extracted.models.push_back({71,"model-a",math::translation({2,3,4}),{AssetRef<MaterialAsset>{"material-a"}},true,false});
        extracted.models.front().lightingCategory=lighting_scope::Character;
        extracted.directional.push_back({});extracted.directional.front().intensity=2.25f;
        extracted.points.push_back({});extracted.points.front().position={8,9,10};
        extracted.points.front().entityId=UINT64_C(0xfedcba9876543210);
        const auto* pointStorage=extracted.points.data();
        std::vector<RenderParticle> particles{{{5,6,7},.75f,{2,3,4,.4f},1}};
        const auto* particleStorage=particles.data();
        auto particleMaterial=std::make_shared<PreparedParticleMaterial>();
        particleMaterial->render.receiveFog=false;particleMaterial->signature=42;
        std::weak_ptr<const PreparedParticleMaterial> weakParticle=particleMaterial;
        std::vector<RenderParticleMaterial> materials{{},particleMaterial->render};

        auto model=std::make_shared<PreparedEditorModel>();
        auto asset=std::make_shared<ModelAsset>();asset->id="model-a";
        model->asset=asset;model->revision=19;model->materials.push_back({});
        model->materials.front().receiveFog=false;model->textures.resize(1);
        std::weak_ptr<const PreparedEditorModel> weakModel=model;
        auto casters=std::make_shared<PreparedEditorShadows>();
        casters->models.push_back(model);
        casters->instances.push_back({71,{model->asset,extracted.models.front().world},&model->textures,model->revision,
            false,true,&model->materials});
        casters->instances.back().lightingCategory=extracted.models.front().lightingCategory;
        casters->instances.back().actorOwner=71;
        auto pins=std::make_shared<EditorRenderWorldPins>();pins->particles.push_back(particleMaterial);pins->shadows=casters;

        const MaterialEvaluationContext clock{12.5,91.75};
        auto frozen=freezeEditorRenderWorld(extracted,std::move(particles),std::move(materials),pins,903,512,clock,
            Vec3{3,4,5});
        require(frozen->points.data()==pointStorage && frozen->particles.data()==particleStorage,
            "World publication copied packet arrays instead of transferring their storage.");
        require(extracted.models.size()==1 && extracted.models.front().materialOverrides==ModelMaterialOverrides{AssetRef<MaterialAsset>{"material-a"}},
            "Freezing world lighting consumed receiver metadata before view preparation.");
        require(frozen->shadowWorldId==903 && frozen->shadowResolution==512,
            "World identity or shadow quality was lost at the prepared-frame boundary.");
        require(frozen->materialContext.gameSeconds==12.5 && frozen->materialContext.realSeconds==91.75,
            "Frame publication changed material clocks.");
        require(frozen->particles.front().position==Vec3{5,6,7} && frozen->particles.front().materialIndex==1 &&
            !frozen->particleMaterials.at(1).receiveFog,"Particle presentation/material indexing changed during publication.");
        require(frozen->shadowInstances.front().entityId==71 && frozen->shadowInstances.front().resourceRevision==19 &&
            frozen->shadowInstances.front().materials==&model->materials && !frozen->shadowInstances.front().materials->front().receiveFog,
            "Caster packets no longer refer to their exact prepared material version.");

        // Advancing simulation/extraction and publishing replacement assets must
        // not rewrite the values or borrowed arrays of an already queued frame.
        extracted.points.push_back({});extracted.points.front().position={-1,-2,-3};
        extracted.directional.push_back({});extracted.directional.front().intensity=100;
        extracted.models.clear();
        require(frozen->points.front().position==Vec3{8,9,10} && frozen->points.front().entityId==UINT64_C(0xfedcba9876543210) &&
            frozen->directional.front().intensity==2.25f,
            "A later world mutation changed a published frame.");
        auto frame=std::make_shared<PreparedRenderFrame>();frame->world=frozen;
        PreparedRenderView scene;scene.identity={RenderViewPurpose::Scene,0};scene.width=1280;scene.height=720;
        scene.settings.camera.position={4,5,6};scene.settings.environment.fog.enabled=true;
        scene.settings.environment.ssgi={true,1.5f,4.f,.12f,.8f,SsgiQuality::High,2};
        scene.settings.environment.ssao={true,1.2f,.7f,SsaoQuality::High};
        scene.settings.antiAliasing=AntiAliasing::Smaa;
        scene.instances.push_back(frozen->shadowInstances.front());
        PreparedRenderView game;game.identity={RenderViewPurpose::Game,0};game.width=960;game.height=540;
        game.settings=scene.settings;game.settings.camera.position={11,12,13};
        game.settings.antiAliasing=AntiAliasing::Off;
        game.instances.push_back(frozen->shadowInstances.front());game.instances.front().entityId=72;
        game.instances.front().opacity=.25f;
        frame->views.push_back(std::move(scene));frame->views.push_back(std::move(game));
        std::shared_ptr<const PreparedRenderFrame> queued=frame;
        require(queued->views[0].settings.camera.position==Vec3{4,5,6} && queued->views[1].settings.camera.position==Vec3{11,12,13} &&
            queued->views[0].settings.environment.fog==queued->views[1].settings.environment.fog,
            "Cameras or environment state leaked between prepared views.");
        require(validatePreparedRenderFrame(*queued)==PreparedFrameError::None,
            "A valid Scene/Game frame failed validation.");
        {
            const auto sceneAdapter=preparedRenderScene(*queued,queued->views[0]);
            const auto gameAdapter=preparedRenderScene(*queued,queued->views[1]);
            require(sceneAdapter.settings.antiAliasing==AntiAliasing::Smaa&&gameAdapter.settings.antiAliasing==AntiAliasing::Off,
                "Prepared Scene/Game adapters lost independent antialiasing settings.");
            require(sceneAdapter.settings.environment.ssgi==gameAdapter.settings.environment.ssgi&&
                sceneAdapter.settings.environment.ssao==gameAdapter.settings.environment.ssao&&sceneAdapter.settings.environment.ssao.enabled,
                "Prepared Scene/Game adapters lost authored screen-space lighting controls.");
            require(&sceneAdapter.settings==&queued->views[0].settings && &gameAdapter.settings==&queued->views[1].settings &&
                sceneAdapter.settings.camera.position==Vec3{4,5,6} && gameAdapter.settings.camera.position==Vec3{11,12,13},
                "Execution adapters lost their owning view's settings or camera.");
            require(sceneAdapter.instances.data()==queued->views[0].instances.data() &&
                gameAdapter.instances.data()==queued->views[1].instances.data() &&
                sceneAdapter.instances.front().entityId==71 && gameAdapter.instances.front().entityId==72,
                "Execution adapters mixed receiver arrays between cameras.");
            require(sceneAdapter.instances.front().opacity==1 && gameAdapter.instances.front().opacity==.25f &&
                sceneAdapter.shadowInstances.front().opacity==1 && gameAdapter.shadowInstances.front().opacity==1,
                "Generic instance opacity changed another view or the full-opacity shadow snapshot.");
            require(sceneAdapter.directionalLights.data()==queued->world->directional.data() &&
                gameAdapter.directionalLights.data()==sceneAdapter.directionalLights.data() &&
                sceneAdapter.pointLights.data()==queued->world->points.data() &&
                gameAdapter.pointLights.data()==sceneAdapter.pointLights.data() &&
                sceneAdapter.particles.data()==queued->world->particles.data() &&
                gameAdapter.particles.data()==sceneAdapter.particles.data() &&
                sceneAdapter.particleMaterials.data()==queued->world->particleMaterials.data() &&
                gameAdapter.particleMaterials.data()==sceneAdapter.particleMaterials.data(),
                "Scene/Game adapters no longer share one frozen lighting/particle world.");
            require(sceneAdapter.sceneMode && gameAdapter.sceneMode && !sceneAdapter.model && !gameAdapter.model &&
                !sceneAdapter.modelTextures && !gameAdapter.modelTextures,
                "Prepared world execution unexpectedly entered a legacy preview path.");
            require(sceneAdapter.shadowInstances.data()==queued->world->shadowInstances.data() &&
                gameAdapter.shadowInstances.data()==sceneAdapter.shadowInstances.data() &&
                sceneAdapter.shadowWorldId==903 && gameAdapter.shadowWorldId==903 &&
                sceneAdapter.shadowResolution==512 && gameAdapter.shadowResolution==512 &&
                sceneAdapter.materialContext==clock && gameAdapter.materialContext==clock,
                "Scene/Game adapters diverged in caster ownership, shadow policy or material clocks.");
            require(sceneAdapter.shadowPriorityOrigin==std::optional<Vec3>{Vec3{3,4,5}} &&
                gameAdapter.shadowPriorityOrigin==sceneAdapter.shadowPriorityOrigin,
                "Scene/Game adapters diverged in the frozen shadow admission origin.");
        }

        // Reject malformed requests before any camera submits work; an empty
        // list with a valid world remains a legal hidden/collapsed editor frame.
        PreparedRenderFrame invalid;
        require(validatePreparedRenderFrame(invalid)==PreparedFrameError::MissingWorld,
            "A frame without a world snapshot passed validation.");
        invalid.world=queued->world;
        require(validatePreparedRenderFrame(invalid)==PreparedFrameError::None,
            "A world frame without visible camera requests was rejected.");
        invalid.views=queued->views;
        invalid.views[1].identity=invalid.views[0].identity;
        require(validatePreparedRenderFrame(invalid)==PreparedFrameError::DuplicateView,
            "Duplicate active-world view identities passed validation.");
        invalid.views=queued->views;invalid.views[0].identity.instanceId=7;
        require(validatePreparedRenderFrame(invalid)==PreparedFrameError::DuplicateView,
            "Unsupported active-world view instance IDs passed validation.");
        for(const auto purpose:{RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail}) {
            invalid.views=queued->views;invalid.views[0].identity={purpose,0};
            require(validatePreparedRenderFrame(invalid)==PreparedFrameError::UnsupportedView,
                "An isolated preview was accepted as a shared active-world view.");
        }
        for(const bool width:{false,true})for(const auto extent:{0u,65536u,std::numeric_limits<std::uint32_t>::max()}) {
            invalid.views=queued->views;
            (width?invalid.views[0].width:invalid.views[0].height)=extent;
            require(validatePreparedRenderFrame(invalid)==PreparedFrameError::InvalidExtent,
                "An empty or unrepresentable physical viewport extent passed validation.");
        }
        invalid.views=queued->views;invalid.views[0].width=65535;invalid.views[0].height=65535;
        require(validatePreparedRenderFrame(invalid)==PreparedFrameError::None,
            "The representable physical viewport extent boundary was rejected.");
        require(*preparedFrameErrorText(PreparedFrameError::None)=='\0' &&
            *preparedFrameErrorText(PreparedFrameError::MissingWorld)!='\0' &&
            *preparedFrameErrorText(PreparedFrameError::DuplicateView)!='\0' &&
            *preparedFrameErrorText(PreparedFrameError::UnsupportedView)!='\0' &&
            *preparedFrameErrorText(PreparedFrameError::InvalidExtent)!='\0',
            "Prepared-frame validation lost its actionable diagnostic.");
        invalid.world.reset();invalid.views.clear();
        frozen.reset();pins.reset();casters.reset();particleMaterial.reset();model.reset();asset.reset();frame.reset();
        require(!weakParticle.expired() && !weakModel.expired(),"Queued frames released borrowed particle/caster resource versions early.");
        require(queued->world->shadowInstances.front().materials->front().receiveFog==false,
            "A queued caster's material pointer outlived its version owner.");
        queued.reset();
        require(weakParticle.expired() && weakModel.expired(),"Retired frames retained resource versions after their last owner released them.");

        ExtractedScene empty;
        const auto emptyWorld=freezeEditorRenderWorld(empty,{},{},{},0,1024,{});
        require(emptyWorld->points.empty() && emptyWorld->particles.empty() && emptyWorld->shadowInstances.empty() && !emptyWorld->pinned,
            "An empty world retained packets or phantom version owners.");
        std::cout<<"Editor render frame ownership checks passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
