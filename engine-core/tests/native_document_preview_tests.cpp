#include "integration/LevelDocumentPreviews.h"
#include "integration/AssetDocuments.h"
#include "integration/LevelSession.h"
#include "integration/widgets/QtDocumentProperties.h"
#include "integration/widgets/QtMaterialGraph.h"
#include "editor/MaterialDocument.h"
#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include "editor/RagdollDocument.h"
#include "editor/ParticleDocument.h"
#include "editor/DecalDocument.h"
#include "assets/AnimationAsset.h"
#include "assets/ModelAnimation.h"
#include "assets/AssetLibrary.h"
#include "resources/ResourceManager.h"
#include "render/render_host.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "ragdoll_fixture.h"
#include <QApplication>
#include <QComboBox>
#include <QMainWindow>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using crimson::ragdoll_test::require;
using crimson::AssetKind;
using quader::EnginePreviewKind;

// Records the production adapter's publications without initializing a device,
// painting QRhi widgets, creating SDL windows or advancing a renderer frame.
class Host final:public quader::RenderHost {
public:
    struct Binding{std::weak_ptr<const void> lifetime;std::function<bool()> record;unsigned publications{},clears{};};
    std::array<Binding,std::size_t(EnginePreviewKind::Count)> bindings;
    unsigned renderCalls{};
    void publish_engine_preview(EnginePreviewKind kind,std::weak_ptr<const void> lifetime,std::function<bool()> record)override{auto& value=bindings[std::size_t(kind)];value.lifetime=std::move(lifetime);value.record=std::move(record);++value.publications;}
    void clear_engine_preview(EnginePreviewKind kind)override{auto& value=bindings[std::size_t(kind)];value.lifetime.reset();value.record={};++value.clears;}
    bool render_engine_preview(EnginePreviewKind,const quader::RenderVulkanCompositionTarget&)override{++renderCalls;return false;}
    bool initialize(void*,const std::filesystem::path&,quader::RenderConfig,const quader::RenderVulkanSharedContext*)override{return false;}
    void shutdown()override{}
    bool initialized()const override{return false;}
    bool supports_hidden_native_pixels()const override{return true;}
    bool uses_composited_vulkan()const override{return true;}
    std::unique_ptr<quader::RenderSceneSink> create_scene_sink(const std::filesystem::path&)override{return {};}
    std::unique_ptr<quader::RenderView> create_view(std::uint8_t,quader::ViewportRenderSurface)override{return {};}
    std::unique_ptr<quader::RenderMaterialPreviewView> create_material_preview_view()override{return {};}
    std::unique_ptr<quader::RenderSurface> create_composited_surface(std::uint32_t,std::uint32_t)override{return {};}
    std::unique_ptr<quader::RenderSurface> create_native_surface(void*,quader::SwapChainPresentationColorSpace)override{return {};}
    bool begin_frame(quader::RenderSurface&)override{return false;}
    void end_frame(quader::RenderSurface&)override{}
    void render_viewport(quader::RenderView&,quader::RenderSurface&)override{}
    bool render_composited_viewport(quader::RenderView&,quader::RenderSurface&,const quader::RenderVulkanCompositionTarget&)override{return false;}
    void render_material_preview(quader::RenderMaterialPreviewView&,quader::RenderSurface&)override{}
    void begin_presentation_frame()override{}
    void complete_presentation_frame(std::size_t)override{}
    void complete_composited_presentation_frame()override{}
    bool finish_external_queue_submission()override{return true;}
    void begin_shared_command_batch()override{}
    void end_shared_command_batch()override{}
    void mark_shared_commands_pending()override{}
    void set_composited_present_mode(VkPresentModeKHR)override{}
    void set_display_refresh_rate_hz(float)override{}
    void set_vsync_enabled(bool)override{}
    bool vsync_enabled()const override{return false;}
    bool presentation_is_synchronized()const override{return false;}
    bool ready_for_next_frame()const override{return true;}
    quader::RenderFramePerformanceStats frame_performance_stats()const override{return {};}
    void apply_scene_settings(const quader::SceneSettings&)override{}
    std::shared_ptr<const quader::MaterialThumbnailReadback> submit_material_thumbnail(quader::RenderMaterialEvaluationSnapshot,std::uint32_t)override{return {};}
    void poll_material_thumbnails()override{}
};
std::string bytes(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
QPushButton* button(QWidget& parent,const char* label){for(auto* value:parent.findChildren<QPushButton*>())if(value->text()==QLatin1String(label))return value;std::string diagnostic;for(auto* value:parent.findChildren<QLabel*>())if(!value->text().isEmpty())diagnostic+=" "+value->text().toStdString();throw std::runtime_error(std::string("Missing native preview button: ")+label+diagnostic);}
QWidget* viewport(QWidget& parent){for(auto* value:parent.findChildren<QWidget*>())if(dynamic_cast<quader::QtVulkanCompositionWidget*>(value))return value->parentWidget();throw std::runtime_error("Typed document has no canonical composed viewport.");}

void material_graph_preview(){
    crimson::ragdoll_test::Project project;
    crimson::integration::LevelSession session(project.root);
    auto material=crimson::createMaterialAsset(project.root,"Graph preview");
    const auto scalar=crimson::addMaterialNode(material.graph,crimson::MaterialNodeKind::Scalar,{-300,0}).id;
    const auto other=crimson::addMaterialNode(material.graph,crimson::MaterialNodeKind::UV,{-300,240}).id;
    const auto animated=crimson::addMaterialNode(material.graph,crimson::MaterialNodeKind::Time,{-300,480}).id;
    crimson::saveMaterialAsset(project.root,material);
    QMainWindow main;main.setAttribute(Qt::WA_DontShowOnScreen);unsigned wakes{};
    crimson::integration::AssetDocuments documents(main,session,[&]{++wakes;});
    require(documents.open(AssetKind::Material,material.id),"Production material document failed to open.");
    crimson::QtMaterialGraph* graph{};
    for(auto* widget:documents.window(AssetKind::Material)->findChildren<QWidget*>())if(auto* candidate=dynamic_cast<crimson::QtMaterialGraph*>(widget))graph=candidate;
    require(graph&&bool(graph->previewChanged)&&bool(graph->nodeImage)&&bool(graph->releaseImageCache),"Production graph omitted its preview publication or asynchronous image owner.");
    using crimson::integration::MaterialPreviewMode;
    require(documents.materialPreviewMode(material.id)==MaterialPreviewMode::ThreeD,"A newly opened graph did not default to 3D preview.");
    require(documents.materialNodeImageSupported(material.id,{scalar,crimson::MaterialPin::Value})&&!documents.materialNodeImageSupported(material.id,{animated,crimson::MaterialPin::Value}),"Static 2D image classification did not preserve the animated node shader fallback.");
    require(documents.setMaterialPreviewMode(material.id,MaterialPreviewMode::TwoD),"The native graph rejected its 2D presentation mode.");
    const auto before=crimson::serializeMaterialAsset(documents.materialDocument(material.id)->definition());
    const std::array<crimson::MaterialNodeId,1> selected{scalar};graph->selectNodes(selected);
    const auto previous_wakes=wakes;
    QKeyEvent preview(QEvent::KeyPress,Qt::Key_P,Qt::ControlModifier);QApplication::sendEvent(graph,&preview);
    require(documents.materialPreviewTarget(material.id)==crimson::MaterialGraphPreviewTarget{scalar,crimson::MaterialPin::Value}&&wakes>previous_wakes,"Production node preview did not reach its render owner or request a frame.");
    const std::array<crimson::MaterialNodeId,1> changed_selection{other};graph->selectNodes(changed_selection);documents.update();
    const auto retained=documents.materialPreviewTarget(material.id);
    require(documents.materialPreviewMode(material.id)==MaterialPreviewMode::TwoD,"Changing node selection reset the document's 2D preview mode.");
    require(retained&&retained->node==scalar&&crimson::serializeMaterialAsset(documents.materialDocument(material.id)->definition())==before&&!documents.materialDocument(material.id)->dirty(),"Graph selection retargeted a pinned output or preview state edited the material.");
    graph->selectNodes(selected);QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(graph,&remove);
    require(!documents.materialPreviewTarget(material.id),"Production renderer retained a deleted node output.");
    require(documents.materialPreviewMode(material.id)==MaterialPreviewMode::TwoD,"Stopping a node preview discarded the retained document mode.");
    require(documents.materialDocument(material.id)->undo(),"Deleting a previewed node was not undoable.");documents.update();
    graph->selectNodes(selected);QApplication::sendEvent(graph,&preview);
    QPointer<crimson::QtMaterialGraph> old_graph=graph;
    auto replacement=material;replacement.id=crimson::newAssetId();replacement.name="Replacement graph";crimson::saveMaterialAsset(project.root,replacement);
    require(documents.open(AssetKind::Material,replacement.id),"Clean graph document replacement failed.");
    require(old_graph.isNull()&&!documents.materialPreviewTarget(material.id)&&!documents.materialPreviewTarget(replacement.id),"A replaced graph leaked its old output target or image callbacks.");
    require(documents.materialPreviewMode(replacement.id)==MaterialPreviewMode::ThreeD&&!documents.setMaterialPreviewMode(material.id,MaterialPreviewMode::TwoD),"A replacement document inherited transient preview mode or accepted the closed owner's mode command.");
    require(documents.confirmClose()&&!documents.materialPreviewTarget(replacement.id),"Closed graph retained a live preview output.");
}
void blend_material_preview(){
    using namespace crimson;
    using integration::MaterialPreviewMode;
    ragdoll_test::Project project;integration::LevelSession session(project.root);
    auto source=*session.assets().createMaterial("Existing Blend source");
    const auto set_roughness=[&](float value){const auto* link=materialInputLink(source.graph,materialOutputNode(source.graph).id,MaterialPin::Roughness);require(link,"Blend source lost its Roughness parameter.");findMaterialNode(source.graph,link->fromNode)->scalar=value;canonicalizeMaterialAsset(source);};
    set_roughness(.25f);saveMaterialAsset(project.root,source);session.assets().publishMaterial(std::make_shared<const MaterialAsset>(source));
    auto blend=createBlendMaterialAsset(project.root,"Blend channel fixture");
    auto& base=blend.blendDefinition->layers[0];base.channels[std::size_t(BlendMaterialChannel::Roughness)].value={.2f,.2f,.2f,.2f};base.adjustments.roughnessBrightness=2;
    auto& red=blend.blendDefinition->layers[1];red.source=BlendMaterialSource::Existing;red.material.id=source.id;red.adjustments.roughnessBrightness=2;
    canonicalizeMaterialAsset(blend);saveMaterialAsset(project.root,blend);const auto original=bytes(materialAssetPath(project.root,blend.id));
    QMainWindow main;main.setAttribute(Qt::WA_DontShowOnScreen);unsigned wakes{};integration::AssetDocuments documents(main,session,[&]{++wakes;});
    require(documents.open(AssetKind::Material,blend.id),"Native Blend document failed to open.");
    require(documents.blendMaterialPreview(blend.id)&&!documents.blendMaterialPreview(blend.id)->channel&&!documents.materialPreviewTarget(blend.id)&&!documents.materialPreviewMaterial(blend.id),"A Blend document did not default to Full material.");
    const auto before=serializeMaterialAsset(documents.materialDocument(blend.id)->definition());
    const auto roughness=[&]{const auto preview=documents.materialPreviewMaterial(blend.id);const auto target=documents.materialPreviewTarget(blend.id);require(preview&&target&&!preview->blendDefinition&&preview->surface.graph,"Adjusted channel preview did not publish one immutable derived graph.");return bakeMaterialGraphImagePreview(preview->graph,*target,{},MaterialImagePreviewOptions{2,2}).rgba.front();};
    for(unsigned channel=0;channel<7;++channel){const auto previous=wakes;require(documents.setBlendMaterialPreview(blend.id,{0,static_cast<BlendMaterialChannel>(channel)}),"Native Blend rejected a supported channel.");const auto target=documents.materialPreviewTarget(blend.id);require(target==blend_material_channel_preview_target(*blend.blendDefinition,0,static_cast<BlendMaterialChannel>(channel))&&documents.materialPreviewMaterial(blend.id)&&wakes>previous,"Blend selector did not publish the canonical adjusted channel endpoint or request a frame.");}
    require(documents.setBlendMaterialPreview(blend.id,{0,BlendMaterialChannel::Roughness})&&std::abs(roughness()-.4f)<1.e-5f,"Inline Blend preview omitted its channel adjustment.");
    const auto stable=documents.materialPreviewMaterial(blend.id);for(unsigned frame=0;frame<10;++frame)require(documents.materialPreviewMaterial(blend.id)==stable,"Stable Blend presentation rebuilt its immutable diagnostic graph.");
    require(documents.materialPreviewMode(blend.id)==MaterialPreviewMode::TwoD&&documents.materialNodeImageSupported(blend.id,*documents.materialPreviewTarget(blend.id))&&documents.setMaterialPreviewMode(blend.id,MaterialPreviewMode::ThreeD),"Blend channels lost their 2D image or existing 3D preview transport.");
    require(documents.setBlendMaterialPreview(blend.id,{1,BlendMaterialChannel::Roughness})&&std::abs(roughness()-.5f)<1.e-5f,"Existing-material Blend preview lost its inherited raw channel or adjustment.");
    const auto previous=documents.materialPreviewMaterial(blend.id);set_roughness(.4f);session.assets().publishMaterial(std::make_shared<const MaterialAsset>(source));
    require(documents.materialPreviewMaterial(blend.id)!=previous&&std::abs(roughness()-.8f)<1.e-5f,"An updated committed layer source did not invalidate its adjusted Blend channel preview.");
    require(!documents.setBlendMaterialPreview(blend.id,{4,BlendMaterialChannel::Roughness})&&documents.setBlendMaterialPreview(blend.id,{1,{}})&&!documents.materialPreviewTarget(blend.id)&&!documents.materialPreviewMaterial(blend.id),"Full material did not release the transient Blend diagnostic endpoint.");
    require(serializeMaterialAsset(documents.materialDocument(blend.id)->definition())==before&&!documents.materialDocument(blend.id)->dirty()&&!documents.materialDocument(blend.id)->canUndo()&&bytes(materialAssetPath(project.root,blend.id))==original,"Blend preview selection, mode or source refresh edited authored recipe, history or storage.");
    require(documents.confirmClose()&&!documents.blendMaterialPreview(blend.id),"Closed Blend document retained its selector owner.");
}
void lifecycle(){
    crimson::ragdoll_test::Project project;
    crimson::integration::LevelSession session(project.root);
    auto source=std::make_shared<crimson::ModelAsset>(crimson::ragdoll_test::source());
    crimson::ModelAnimationChannel idle;
    idle.node=0;idle.components=3;idle.times={0,1};idle.values={0,0,0,0,0,0};
    source->animations.push_back({"Idle",0,1,{idle}});
    crimson::validateModelAnimationData(*source);
    const auto sourcePath=project.root/"Content"/"Models"/"fixture.crimson-model";
    std::filesystem::create_directories(sourcePath.parent_path());
    crimson::registerAssetPath(project.root,source->id,sourcePath);
    auto seed=session.resources().request<crimson::ModelAsset>(crimson::AssetRef<crimson::ModelAsset>{source->id},[source](std::stop_token){return crimson::ResourceLoadResult<crimson::ModelAsset>{source,sizeof(*source),1,{}};});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!seed.snapshot()&&std::chrono::steady_clock::now()<deadline){session.resources().pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(bool(seed.snapshot()),"CPU source model did not publish.");
    auto ragdoll=crimson::makeRagdollAsset(*source);crimson::generateRagdollBodies(ragdoll,*source,{3,1});crimson::saveRagdollAsset(project.root,ragdoll);
    const auto animation=crimson::importedAnimationAsset(*source,0);crimson::saveAnimationAsset(project.root,animation);
    const auto particle=crimson::createParticleAsset(project.root,"Native particles");
    const auto decal=crimson::createDecalAsset(project.root,"Native decal");
    const auto ragdollBytes=bytes(crimson::ragdollAssetPath(project.root,ragdoll.id));
    const auto animationBytes=bytes(crimson::animationAssetPath(project.root,animation.id));
    const auto particleBytes=bytes(crimson::particleAssetPath(project.root,particle.id));
    const auto decalBytes=bytes(crimson::decalAssetPath(project.root,decal.id));
    Host host;QMainWindow main;main.setAttribute(Qt::WA_DontShowOnScreen);
    crimson::integration::AssetDocuments documents(main,session,[]{});
    auto previews=std::make_unique<crimson::integration::LevelDocumentPreviews>(documents,session,host,[]{});
    require(documents.open(AssetKind::Ragdoll,ragdoll.id),"Native Ragdoll document did not open.");
    require(documents.open(AssetKind::Animation,animation.id),"Native Animation document did not open.");
    require(documents.open(AssetKind::Particle,particle.id),"Native Particle document did not open.");
    require(documents.open(AssetKind::Decal,decal.id),"Native Decal document did not open.");
    previews->update(0,false);
    auto* rig=documents.window(AssetKind::Ragdoll);auto* bones=rig->findChild<QTreeWidget*>(QStringLiteral("CrimsonRagdollBones"));
    require(bones&&bones->topLevelItemCount()>0,"The Ragdoll Bones sidebar did not bind the accepted source model.");
    require(documents.properties(AssetKind::Ragdoll)->parentWidget()!=rig->centralWidget(),"Ragdoll properties were not moved into the separate right sidebar.");
    auto* scope=rig->findChild<QComboBox*>();require(scope&&scope->count()==3,"The native Ragdoll test scopes are absent.");
    const auto before=documents.ragdollDocument(ragdoll.id)->definition();
    button(*rig,"Simulate")->click();previews->update(1./60,true);button(*rig,"Pause")->click();previews->update(0,false);
    button(*rig,"Reset")->click();previews->update(0,false);
    require(documents.ragdollDocument(ragdoll.id)->definition()==before&&!documents.ragdollDocument(ragdoll.id)->dirty(),"Preview simulation or controls changed the authored Ragdoll/history.");
    auto* decalWindow=documents.window(AssetKind::Decal);
    auto* decalMeshes=decalWindow->findChild<QComboBox*>(QStringLiteral("CrimsonDecalPreviewMesh"));
    require(decalMeshes&&decalMeshes->count()==5,"The Decal document lost its five canonical receiver shapes.");
    const auto decalBefore=documents.decalDocument(decal.id)->definition();
    for(int shape=0;shape<decalMeshes->count();++shape){decalMeshes->setCurrentIndex(shape);button(*decalWindow,"Reset view")->click();previews->update(0,false);}
    require(documents.decalDocument(decal.id)->definition()==decalBefore&&!documents.decalDocument(decal.id)->dirty(),"Decal receiver selection or camera reset edited the authored decal/history.");
    for(const auto kind:{EnginePreviewKind::Ragdoll,EnginePreviewKind::Animation,EnginePreviewKind::Particle,EnginePreviewKind::Decal}){const auto& binding=host.bindings[std::size_t(kind)];require(binding.publications>0&&!binding.lifetime.expired()&&bool(binding.record),"A typed preview failed to publish its own live host binding.");}
    QPointer<QWidget> rigViewport=viewport(*rig);QPointer<crimson::QtDocumentProperties> retainedFields=documents.properties(AssetKind::Ragdoll);
    const auto oldToken=host.bindings[std::size_t(EnginePreviewKind::Ragdoll)].lifetime;
    auto replacement=ragdoll;replacement.id=crimson::newAssetId();replacement.name="Other rig";crimson::saveRagdollAsset(project.root,replacement);
    require(documents.open(AssetKind::Ragdoll,replacement.id),"Replacing a clean typed document failed.");previews->update(0,false);
    require(rigViewport.isNull()&&retainedFields.isNull()&&oldToken.expired(),"A replaced document retained its old preview, controls or callable lifetime.");
    auto* replacementWindow=documents.window(AssetKind::Ragdoll);QPointer<QWidget> replacementViewport=viewport(*replacementWindow);QPointer<crimson::QtDocumentProperties> restoredFields=documents.properties(AssetKind::Ragdoll);
    const auto replacementToken=host.bindings[std::size_t(EnginePreviewKind::Ragdoll)].lifetime;
    previews.reset();
    require(replacementViewport.isNull()&&!restoredFields.isNull()&&restoredFields->parentWidget()==replacementWindow->centralWidget(),"Preview teardown did not restore the canonical document properties owner.");
    require(replacementToken.expired()&&!host.bindings[std::size_t(EnginePreviewKind::Ragdoll)].record,"Preview teardown retained a callable into destroyed document UI.");
    require(host.bindings[std::size_t(EnginePreviewKind::Decal)].lifetime.expired()&&!host.bindings[std::size_t(EnginePreviewKind::Decal)].record,"Decal preview teardown retained a callback into its destroyed pane.");
    require(documents.confirmClose(),"Clean native documents failed their ordinary close flow.");
    require(host.renderCalls==0,"A minimal Qt lifecycle fixture unexpectedly attempted a GPU composition.");
    require(bytes(crimson::ragdollAssetPath(project.root,ragdoll.id))==ragdollBytes&&bytes(crimson::animationAssetPath(project.root,animation.id))==animationBytes&&bytes(crimson::particleAssetPath(project.root,particle.id))==particleBytes,"Opening, previewing or closing native documents rewrote authored assets.");
    require(bytes(crimson::decalAssetPath(project.root,decal.id))==decalBytes,"Previewing canonical decal receiver shapes rewrote its authored asset.");
}
}
int main(int argc,char** argv){try{std::cerr<<"Native document preview: QApplication\n";QApplication application(argc,argv);application.setQuitOnLastWindowClosed(false);material_graph_preview();blend_material_preview();lifecycle();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);std::cout<<"Native typed previews preserve layout, simulation isolation, document replacement and callback lifetime without a renderer device.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
