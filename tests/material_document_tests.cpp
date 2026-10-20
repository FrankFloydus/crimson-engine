#include "editor/MaterialDocument.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include "material_fixtures.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string text(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
bool same(const MaterialAsset& a,const MaterialAsset& b){return serializeMaterialAsset(a)==serializeMaterialAsset(b);}
bool sameTexture(const ModelTexture& a,const ModelTexture& b){return a.image==b.image&&a.texCoord==b.texCoord&&
    a.wrapU==b.wrapU&&a.wrapV==b.wrapV&&a.minFilter==b.minFilter&&a.magFilter==b.magFilter;}
MaterialNode& parameter(MaterialAsset& a,MaterialPin pin) {
    const auto* link=materialInputLink(a.graph,materialOutputNode(a.graph).id,pin);
    if(!link)throw std::runtime_error("Fixture is missing its starter parameter.");
    return *findMaterialNode(a.graph,link->fromNode);
}
void color(MaterialAsset& a,Vec4 value){parameter(a,MaterialPin::BaseColor).color=value;parameter(a,MaterialPin::OpacityMask).scalar=value[3];}
MaterialUvMapping& uv(MaterialAsset& a){for(auto& n:a.graph.nodes)if(n.kind==MaterialNodeKind::UV)return n.uv;throw std::runtime_error("Fixture UV missing.");}
MaterialTextureNode& texture(MaterialAsset& a,MaterialTextureRole role=MaterialTextureRole::Color) {
    for(auto& n:a.graph.nodes)if(n.kind==MaterialNodeKind::Texture2D&&n.texture.role==role)return n.texture;
    const auto id=addMaterialNode(a.graph,MaterialNodeKind::Texture2D).id;findMaterialNode(a.graph,id)->texture.role=role;
    connectMaterialNodes(a.graph,id,role==MaterialTextureRole::Normal?MaterialPin::Normal:MaterialPin::RGB,
        materialOutputNode(a.graph).id,role==MaterialTextureRole::Normal?MaterialPin::Normal:MaterialPin::BaseColor);
    return findMaterialNode(a.graph,id)->texture;
}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-material-document-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error && target.parent_path()==temp && target.filename().string().starts_with("crimson-material-document-"))fs::remove_all(target,error);
    }
};
void publicationAndHistory(){
    Project project;MaterialDocument document(project.root);
    require(document.create(),"Could not create a saved starter material.");
    require(document.isOpen() && document.editable() && !document.dirty(),"Starter is not a clean editable material.");
    const auto original=document.definition();const auto committed=document.committed();const auto revision=document.revision();
    require(document.begin("Color gesture") && document.update([](auto& a){color(a,{.1f,.2f,.3f,.5f});}) &&
        document.update([](auto& a){color(a,{.2f,.3f,.4f,.6f});}),"Valid color gesture failed.");
    require(document.dirty() && document.committed()==committed && document.revision()==revision && same(*committed,original),"Intermediate material edits leaked to shared users.");
    require(document.commit() && document.revision()>revision && document.committed()!=committed,"Completed gesture did not publish an immutable shared revision.");
    require(same(*committed,original),"Shared publication mutated a retained old snapshot.");
    require(document.undo() && same(document.definition(),original) && !document.dirty() && !document.canUndo(),"Color drag was not one command or clean state was lost.");
    require(document.redo() && document.dirty(),"Redo did not restore the material gesture.");
    require(document.save() && !document.dirty(),"Material Save did not mark its state clean.");
    require(document.undo() && document.dirty() && document.redo() && !document.dirty(),"Saved-state tokens did not follow history.");
    const auto valid=document.definition();const auto version=document.revision();
    require(!document.edit("Invalid color",[](auto& a){parameter(a,MaterialPin::BaseColor).color[0]=std::numeric_limits<float>::quiet_NaN();}),"Nonfinite material edit was accepted.");
    require(document.revision()==version && same(document.definition(),valid) && !document.dirty(),"Rejected material edit changed its working/committed state.");
    require(!document.edit("Identity",[](auto& a){a.id="assets/materials/other.crimson-material";}),"Material edit changed managed identity.");
    require(!document.edit("Invalid shading",[](auto& a){materialOutputNode(a.graph).output.shading=static_cast<MaterialShading>(99);}),"Invalid shader model was accepted.");
    require(document.begin("Cancel") && document.update([](auto& a){parameter(a,MaterialPin::OpacityMask).scalar=.1f;}),"Could not prepare cancel.");
    document.cancel();require(same(document.definition(),valid) && document.revision()==version && !document.dirty(),"Cancel published a shared revision or changed data.");
    require(!document.load("assets/materials/missing.crimson-material") && same(document.definition(),valid),"Failed load replaced the working material.");
}
void displacementTransactions() {
    Project project;MaterialDocument document(project.root);require(document.create("Displaced material"),"Could not create displacement material.");
    MaterialNodeId height{};
    require(document.edit("Enable displacement",[&](auto& asset) {
        auto& root=materialOutputNode(asset.graph);root.output.tessellation.enabled=true;root.output.height=.7f;
        height=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(asset.graph,height)->texture.role=MaterialTextureRole::Data;
        findMaterialNode(asset.graph,height)->texture.image.id="assets/textures/displacement.png";
        connectMaterialNodes(asset.graph,height,MaterialPin::R,materialOutputNode(asset.graph).id,MaterialPin::Height);
    }),"Could not author Height and enable tessellation in one document transaction.");
    const auto enabled=document.definition();const auto revision=document.revision();
    require(document.begin("Displacement gesture")&&document.update([](auto& asset){materialOutputNode(asset.graph).output.tessellation.amount=.4f;})&&
        document.update([](auto& asset){materialOutputNode(asset.graph).output.tessellation.amount=-.6f;}),"Displacement gesture could not update signed amount.");
    document.cancel();require(same(document.definition(),enabled)&&document.revision()==revision,"Canceled displacement gesture changed authored state or published a revision.");
    require(document.edit("Displacement controls",[](auto& asset) {
        auto& t=materialOutputNode(asset.graph).output.tessellation;t.amount=.25f;t.midpoint=.3f;t.maxFactor=12;t.heightLod=1.5f;
    }),"Displacement controls did not use the document command path.");
    const auto displaced=document.definition();
    require(document.undo()&&same(document.definition(),enabled)&&document.redo()&&same(document.definition(),displaced),"Displacement controls failed exact undo/redo.");
    require(!document.edit("Invalid subdivision",[](auto& asset){materialOutputNode(asset.graph).output.tessellation.maxFactor=65;})&&same(document.definition(),displaced),
        "Rejected displacement edit changed its working material.");
    require(document.save()&&document.load(document.assetId())&&same(document.definition(),displaced),"Displacement material failed save/reopen.");
    require(document.edit("Disable tessellation",[](auto& asset){materialOutputNode(asset.graph).output.tessellation.enabled=false;}),"Could not disable tessellation.");
    const auto& graph=document.definition().graph;const auto output=materialOutputNode(graph).id;const auto references=document.historicalTextureReferences();
    require(materialInputLink(graph,output,MaterialPin::Height)&&materialOutputNode(graph).output.height==.7f&&
        std::find(references.begin(),references.end(),canonicalAssetId("assets/textures/displacement.png"))!=references.end(),
        "Disabled tessellation discarded the Height connection or fallback.");
    require(document.undo()&&same(document.definition(),displaced),"Undoing disable failed to restore tessellation.");
}
void identityAndExternalAuthority(){
    Project project;MaterialDocument document(project.root);require(document.create("Original"),"Create failed.");
    const auto sourceId=document.assetId(),sourceText=text(materialAssetPath(project.root,sourceId));
    require(document.edit("Texture",[](auto& a){texture(a).image.id="assets/textures/first.png";}),"Texture edit failed.");
    require(document.edit("Replace texture",[](auto& a){texture(a).image.id="assets/textures/second.png";}),"Texture replacement failed.");
    const auto refs=document.historicalTextureReferences();
    require(std::find(refs.begin(),refs.end(),canonicalAssetId("assets/textures/first.png"))!=refs.end() &&
        std::find(refs.begin(),refs.end(),canonicalAssetId("assets/textures/second.png"))!=refs.end(),"Historical texture usages omit an undo/redo dependency.");
    require(document.save("Copy",true) && document.assetId()!=sourceId && !document.dirty(),"Save As did not create an independent clean identity.");
    const auto copyId=document.assetId();
    require(document.undo() && document.assetId()==copyId && document.dirty(),"Save As Undo resurrected the original identity.");
    require(document.redo() && document.assetId()==copyId && document.definition().name=="Copy" && !document.dirty(),"Save As Redo lost copied identity or saved state.");
    require(text(materialAssetPath(project.root,sourceId))==sourceText,"Save As modified the original file.");
    require(document.edit("Unsaved blend",[](auto& a){materialOutputNode(a.graph).output.blend=MaterialBlend::Mask;}),"Blend edit failed.");
    auto external=loadMaterialAsset(project.root,copyId);parameter(external,MaterialPin::OpacityMask).scalar=.15f;saveMaterialAsset(project.root,external);
    require(document.externalFileChanged() && document.definition().blend==MaterialBlend::Mask && document.dirty(),"External write replaced the current document authority.");
    require(document.load(copyId) && !document.dirty() && same(document.definition(),external) && !document.externalFileChanged(),"Explicit external reload failed.");
    require(document.edit("Unsaved",[](auto& a){parameter(a,MaterialPin::OpacityMask).scalar=.75f;}) && document.discard() && !document.dirty() && same(document.definition(),external),"Discard did not restore last saved values.");
    const auto before=document.definition();std::ofstream bad(materialAssetPath(project.root,copyId));bad<<"{}";bad.close();
    require(document.externalFileChanged() && !document.load(copyId) && same(document.definition(),before),"Malformed external reload destroyed a valid working revision.");
    require(document.save() && !document.externalFileChanged(),"Explicit overwrite did not recover the saved material.");
#ifdef _WIN32
    require(document.edit("Write failure",[](auto& a){parameter(a,MaterialPin::OpacityMask).scalar=.8f;}),"Could not prepare failed save.");
    const auto file=materialAssetPath(project.root,copyId);const auto disk=text(file);const auto snapshot=document.committed();
    require(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Could not make the save fixture read-only.");
    const bool failed=!document.save(),unchanged=text(file)==disk;
    SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL);
    require(failed && unchanged && document.assetId()==copyId && document.dirty() && document.committed()==snapshot,"Failed atomic save changed data, identity, dirty state or authority.");
    require(document.save() && !document.dirty(),"Save did not recover after failure.");
#endif
}
void dormantGraphTextureHistory() {
    Project project;MaterialDocument document(project.root);require(document.create("Graph history"),"Graph history fixture creation failed.");
    const std::string colorImage=canonicalAssetId("assets/textures/disconnected.png"),normalImage=canonicalAssetId("assets/textures/dormant-normal.png");
    MaterialNodeId colorNode{},normalNode{};
    require(document.edit("Texture branches",[&](auto& asset) {
        colorNode=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(asset.graph,colorNode)->texture.image.id=colorImage;
        normalNode=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
        auto& normal=findMaterialNode(asset.graph,normalNode)->texture;normal.role=MaterialTextureRole::Normal;normal.image.id=normalImage;
        connectMaterialNodes(asset.graph,normalNode,MaterialPin::Normal,materialOutputNode(asset.graph).id,MaterialPin::Normal);
    })&&document.save(),"Could not save graph texture dependencies.");
    require(document.edit("Unlit model",[](auto& asset){materialOutputNode(asset.graph).output.shading=MaterialShading::Unlit;}),"Model switch failed.");
    const auto active=materialGraphOrder(document.definition().graph,MaterialShading::Unlit,MaterialBlend::Opaque);
    require(std::find(active.begin(),active.end(),normalNode)==active.end()&&std::find(active.begin(),active.end(),colorNode)==active.end(),"Dormant texture branches were treated as active graph inputs.");
    require(document.edit("Remove dormant textures",[&](auto& asset){removeMaterialNode(asset.graph,colorNode);removeMaterialNode(asset.graph,normalNode);}),"Removing dormant texture nodes failed.");
    require(materialTextureReferences(document.definition()).empty(),"Deleted graph nodes remained in current authored references.");
    const auto history=document.historicalTextureReferences();
    require(std::find(history.begin(),history.end(),colorImage)!=history.end()&&std::find(history.begin(),history.end(),normalImage)!=history.end(),"Disconnected or inactive graph texture was lost from undo dependencies.");
    require(document.undo()&&materialTextureReferences(document.definition()).size()==2&&document.redo()&&materialTextureReferences(document.definition()).empty(),"Dormant node deletion did not restore complete references on undo/redo.");
    require(document.save()&&document.discard()&&document.historicalTextureReferences().empty(),"Discarded graph history retained obsolete deleted texture dependencies.");
}
void reconcileDeletedGraphTextureHistory() {
    Project project;MaterialDocument document(project.root);require(document.create("Reconcile graph"),"Reconciliation fixture creation failed.");
    const std::string deleted=canonicalAssetId("assets/textures/deleted-shared.png");
    require(document.edit("Shared texture roles",[&](auto& asset) {
        materialOutputNode(asset.graph).output.blend=MaterialBlend::Mask;
        texture(asset,MaterialTextureRole::Color).image.id=deleted;
        texture(asset,MaterialTextureRole::Normal).image.id=deleted;
        const auto dormant=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(asset.graph,dormant)->texture.image.id=deleted;
    })&&document.save(),"Reconciliation graph setup failed.");
    require(document.edit("Unrelated roughness",[](auto& asset){parameter(asset,MaterialPin::Roughness).scalar=.2f;})&&
        document.begin("In-progress opacity")&&document.update([](auto& asset){parameter(asset,MaterialPin::OpacityMask).scalar=.4f;}),"Reconciliation transaction setup failed.");
    const auto before=serializeMaterialAsset(document.definition());const auto committed=document.committed();const auto revision=document.revision();
    require(!document.reconcileTextureReferences([&](const auto& id,MaterialTextureRole){return id==deleted?"../invalid.png":id;})&&
        serializeMaterialAsset(document.definition())==before&&document.committed()==committed&&document.revision()==revision&&document.transactionActive()&&document.dirty(),
        "Invalid deletion reconciliation partially changed authored state or its in-progress transaction.");
    const auto replace=[&](const std::string& id,MaterialTextureRole role) {
        return id==deleted?std::string{role==MaterialTextureRole::Normal?NeutralNormalTextureId:GrayTextureId}:id;
    };
    auto saved=loadMaterialAsset(project.root,document.assetId());
    rewriteMaterialGraphTextures(saved.graph,[&](const auto& id,MaterialTextureRole role){return replace(id,role);});saveMaterialAsset(project.root,saved);
    require(document.externalFileChanged(),"Reconciliation did not detect the staged saved-file rewrite.");
    require(document.reconcileTextureReferences(replace)&&document.transactionActive()&&document.dirty()&&!document.externalFileChanged(),
        "Successful texture reconciliation lost the transaction, dirty state or matching saved-file authority.");
    require(document.definition().surface.roughness==.2f&&document.definition().surface.baseColor[3]==.4f&&
        document.committed()->surface.roughness==.2f&&document.committed()->surface.baseColor[3]==1,
        "Texture reconciliation overwrote unrelated edits or published an in-progress parameter value.");
    const auto ensureRewritten=[&] {
        const auto current=materialTextureReferences(document.definition()),history=document.historicalTextureReferences();
        require(std::find(current.begin(),current.end(),deleted)==current.end()&&std::find(history.begin(),history.end(),deleted)==history.end(),
            "Deleted texture identity survived graph/history reconciliation.");
        for(const auto& node:document.definition().graph.nodes)if(node.kind==MaterialNodeKind::Texture2D&&!node.texture.image.empty())
            require(node.texture.image.id==(node.texture.role==MaterialTextureRole::Normal?NeutralNormalTextureId:GrayTextureId),"Reconciled texture role received an incompatible protected fallback.");
    };
    ensureRewritten();document.cancel();ensureRewritten();
    require(document.definition().surface.roughness==.2f&&document.definition().surface.baseColor[3]==1&&document.dirty(),"Cancel resurrected pre-reconciliation references or discarded an unrelated committed edit.");
    require(document.undo()&&!document.dirty()&&document.definition().surface.roughness==1,"Reconciliation changed saved-state history tokens.");ensureRewritten();
    while(document.undo())ensureRewritten();while(document.redo())ensureRewritten();
    require(document.dirty()&&document.definition().surface.roughness==.2f,"Reconciled redo lost the unrelated material edit.");
    const auto stableRevision=document.revision();
    require(document.reconcileTextureReferences(replace)&&document.revision()==stableRevision,"A no-op reference reconciliation published another material version.");
    require(document.save()&&!document.dirty(),"Reconciled graph did not save normally.");
    require(document.edit("New authored texture",[&](auto& asset){for(auto& node:asset.graph.nodes)if(node.kind==MaterialNodeKind::Texture2D)node.texture.image.id=deleted;})&&document.save(),"External-conflict fixture setup failed.");
    auto external=loadMaterialAsset(project.root,document.assetId());
    rewriteMaterialGraphTextures(external.graph,[&](const auto& id,MaterialTextureRole role){return replace(id,role);});
    parameter(external,MaterialPin::Roughness).scalar=.9f;saveMaterialAsset(project.root,external);
    require(document.reconcileTextureReferences(replace)&&document.externalFileChanged()&&document.definition().surface.roughness==.2f,
        "Texture reconciliation accepted an unrelated external material change as the saved authority.");
    ensureRewritten();
}
void boundedHistoryAndRedo() {
    Project project;MaterialDocument document(project.root);require(document.create(),"Create failed.");
    for(size_t edit=0;edit<MaterialDocument::MaximumHistoryEntries+12;++edit)
        require(document.edit("Opacity",[&](auto& a){parameter(a,MaterialPin::OpacityMask).scalar=static_cast<float>(edit+1)/400.f;}),"Bounded history edit failed.");
    size_t undone{};while(document.undo())++undone;
    require(undone==MaterialDocument::MaximumHistoryEntries && document.canRedo(),"Material history retained more than its bounded limit.");
    const auto revision=document.revision();
    require(!document.edit("Invalid replacement",[](auto& a){parameter(a,MaterialPin::OpacityMask).scalar=std::numeric_limits<float>::quiet_NaN();}) && document.canRedo() && document.revision()==revision,"Rejected edit destroyed redo.");
    require(document.edit("New branch",[](auto& a){parameter(a,MaterialPin::OpacityMask).scalar=.75f;}) && !document.canRedo(),"A valid new command did not replace redo history.");
}
void protectedDefaultsAndImportedAuthoring(){
    Project project;MaterialDocument document(project.root);
    require(document.load(DefaultParticleMaterialId) && !document.editable() && !document.externalFileChanged(),"Built-in material did not open read-only.");
    require(!document.begin("Edit builtin") && !document.save(),"Built-in material accepted edits or overwrite.");
    require(document.save("Saved sprite",true) && document.editable() && !document.dirty() && document.assetId()!=DefaultParticleMaterialId,"Save As did not create an editable native asset.");
    auto imported=createMaterialAsset(project.root,"Imported fixture");imported.originModel=canonicalAssetId("assets/models/fixture/manifest.json");
    imported.shading=MaterialShading::StandardPbr;imported.blend=MaterialBlend::Opaque;saveMaterialAsset(project.root,imported);
    require(document.load(imported.id) && document.editable() && !document.dirty(),"Imported material did not open as an editable shared asset.");
    const auto initial=text(materialAssetPath(project.root,imported.id));
    require(document.edit("Imported appearance",[](auto& a){parameter(a,MaterialPin::Roughness).scalar=.42f;})&&document.save(),
        "Imported material could not save under its assigned identity.");
    require(document.assetId()==imported.id&&document.definition().originModel==imported.originModel&&
        !document.dirty()&&text(materialAssetPath(project.root,imported.id))!=initial,
        "Editing an imported material changed identity, provenance or saved-state tracking.");
    require(document.undo()&&document.dirty()&&document.redo()&&!document.dirty(),"Imported material undo/redo lost its saved state.");
    require(!document.edit("Rewrite provenance",[](auto& a){a.originModel.clear();}),"An ordinary material edit rewrote import provenance.");
    const auto original=text(materialAssetPath(project.root,imported.id));
    require(document.save("Native PBR",true) && document.editable() && document.definition().originModel.empty() && document.assetId()!=imported.id,"Save As did not create an editable native PBR asset.");
    require(document.edit("PBR appearance",[](auto& a){parameter(a,MaterialPin::Roughness).scalar=.23f;parameter(a,MaterialPin::Metallic).scalar=.75f;texture(a,MaterialTextureRole::Normal).image.id="assets/textures/normal.png";}) && document.save(),"Native PBR editing or storage failed.");
    const auto saved=document.definition();
    require(document.undo() && document.redo() && same(document.definition(),saved) && !document.dirty(),"PBR material undo/redo lost its saved state.");
    require(document.load(saved.id) && same(document.definition(),saved) && text(materialAssetPath(project.root,imported.id))==original,"Native PBR roundtrip changed the imported original.");
}
void importedPackedMapsAndPromotion(){
    Project project;ModelAsset model;model.id="assets/models/model-material-authoring/manifest.json";
    ModelMaterial surface;surface.name="tripo_mat_e2d9edc9-a284-40c0-a603-a929041cabe3";
    surface.baseColor={.7f,.5f,.3f,1};surface.roughness=.6f;surface.metallic=.8f;surface.occlusionStrength=.7f;
    surface.baseColorTexture.image.id="assets/models/model-material-authoring/images/color.png";
    surface.baseColorTexture.texCoord=1;surface.baseColorTexture.wrapU=ModelWrap::Mirror;
    surface.baseColorTexture.minFilter=ModelFilter::NearestMipmapLinear;surface.baseColorTexture.magFilter=ModelFilter::Nearest;
    surface.metallicRoughnessTexture.image.id="assets/models/model-material-authoring/images/packed.png";
    surface.metallicRoughnessTexture.wrapV=ModelWrap::Clamp;
    surface.occlusionTexture=surface.metallicRoughnessTexture;
    model.materials.push_back(surface);model.materialIds.push_back(importedMaterialId(model.id,0));
    promoteModelMaterials(project.root,model);
    const auto original=loadMaterialAsset(project.root,model.materialIds[0]);const auto disk=text(materialAssetPath(project.root,original.id));
    MaterialDocument document(project.root);
    require(document.load(original.id)&&document.editable()&&!document.dirty()&&text(materialAssetPath(project.root,original.id))==disk,
        "Opening an imported packed material rewrote or locked its native sidecar.");
    const auto retained=document.committed();
    require(document.edit("Edit imported packed material",[](auto& asset){
        for(auto& node:asset.graph.nodes)if(node.kind==MaterialNodeKind::Scalar&&node.name=="Roughness factor")node.scalar=.23f;
        auto& normal=texture(asset,MaterialTextureRole::Normal);normal.image.id="assets/textures/authored-normal.png";normal.normalStrength=.65f;
        materialOutputNode(asset.graph).output.receiveFog=false;
    })&&document.save(),"Imported packed material rejected factor/map edits or in-place Save.");
    const auto edited=document.definition();const auto saved=text(materialAssetPath(project.root,edited.id));
    require(edited.id==original.id&&edited.originModel==canonicalAssetId(model.id)&&edited.surface.roughness==.23f&&
        retained->surface.roughness==.6f&&!edited.surface.receiveFog,"Imported authoring changed identity/provenance or mutated the old revision.");
    require(sameTexture(edited.surface.baseColorTexture,original.surface.baseColorTexture)&&
        sameTexture(edited.surface.metallicRoughnessTexture,original.surface.metallicRoughnessTexture)&&
        sameTexture(edited.surface.occlusionTexture,original.surface.occlusionTexture),
        "Editing an imported factor or new normal map changed unrelated images, UVs or samplers.");
    for(const auto& link:original.graph.links)
        require(std::find(edited.graph.links.begin(),edited.graph.links.end(),link)!=edited.graph.links.end(),
            "Imported authoring replaced an existing packed-channel connection.");
    require(document.undo()&&same(document.definition(),original)&&document.dirty()&&document.redo()&&
        same(document.definition(),edited)&&!document.dirty(),"Imported graph undo/redo lost channels or saved state.");
    promoteModelMaterials(project.root,model);
    require(text(materialAssetPath(project.root,edited.id))==saved&&document.load(edited.id)&&same(document.definition(),edited),
        "Model promotion or reopening overwrote authored imported material settings.");
}
void verifyActualMaterial(const fs::path& sourceRoot,const std::string& id) {
    const auto source=materialAssetPath(sourceRoot,id);const auto sourceBytes=text(source);
    const auto original=loadMaterialAsset(sourceRoot,id);
    Project disposable;const auto target=materialAssetPath(disposable.root,id);
    fs::create_directories(target.parent_path());fs::copy_file(source,target);
    MaterialDocument document(disposable.root);
    require(document.load(id)&&document.editable()&&!document.dirty()&&same(document.definition(),original),
        "The actual imported material did not open editable with its complete existing graph.");
    const auto retained=document.committed();
    require(document.edit("Verify actual imported authoring",[](auto& asset) {
        materialOutputNode(asset.graph).output.receiveFog=!materialOutputNode(asset.graph).output.receiveFog;
        texture(asset,MaterialTextureRole::Normal).image.id="assets/textures/verification-normal.png";
    })&&document.save(),"The actual imported material could not edit and save in the disposable project.");
    const auto edited=document.definition();
    require(edited.id==original.id&&edited.originModel==original.originModel&&
        sameTexture(edited.surface.baseColorTexture,original.surface.baseColorTexture)&&
        sameTexture(edited.surface.metallicRoughnessTexture,original.surface.metallicRoughnessTexture)&&same(*retained,original),
        "Editing the actual material lost identity, packed/color maps or immutable snapshot ownership.");
    require(document.undo()&&same(document.definition(),original)&&document.redo()&&same(document.definition(),edited)&&
        document.load(id)&&same(document.definition(),edited)&&!document.dirty(),
        "The actual imported material lost edits, graph data or history during Save/reopen.");
    require(text(source)==sourceBytes,"Disposable verification modified the authored source material.");
    std::cout<<"Actual material verified in disposable storage: "<<original.name<<" ["<<id<<"]\n";
}
void sharedQuaderMaterialAuthoring(){
    Project project;auto material=createMaterialAsset(project.root,"Quader shared");
    material.id="assets/materials/quader-project-shared.crimson-material";
    parameter(material,MaterialPin::Roughness).scalar=.62f;texture(material).image.id="assets/textures/quader-color.png";
    canonicalizeMaterialAsset(material);saveMaterialAsset(project.root,material);
    const auto initial=text(materialAssetPath(project.root,material.id));MaterialDocument document(project.root);
    require(document.load(material.id)&&document.editable()&&!document.dirty()&&!document.externalFileChanged(),"Shared native Quader material opened read-only or dirty.");
    require(text(materialAssetPath(project.root,material.id))==initial,"Opening a Quader material rewrote its saved contents.");
    require(document.edit("Quader appearance",[](auto& asset){parameter(asset,MaterialPin::Roughness).scalar=.27f;})&&document.save(),"Quader material could not be edited and saved under its shared identity.");
    require(document.assetId()==material.id&&document.definition().originModel.empty()&&document.definition().surface.roughness==.27f&&
        materialTextureReferences(document.definition())==materialTextureReferences(material),"Editing a Quader material changed its shared ID, ownership or textures.");
    require(document.undo()&&document.dirty()&&document.definition().surface.roughness==.62f&&document.redo()&&!document.dirty(),"Quader material history lost its saved state.");
    require(document.load(material.id)&&document.editable()&&document.definition().surface.roughness==.27f,"Saved Quader material did not reopen editable with its authored values.");
    const auto prior=document.committed();const auto revision=document.revision();
    auto refreshed=loadMaterialAsset(project.root,material.id);parameter(refreshed,MaterialPin::Roughness).scalar=.44f;saveMaterialAsset(project.root,refreshed);
    require(document.externalFileChanged()&&!document.dirty()&&document.definition().surface.roughness==.27f,"A Quader source refresh silently mutated the clean open document before reconciliation.");
    require(document.load(material.id)&&document.revision()>revision&&!document.dirty()&&!document.externalFileChanged()&&document.editable()&&
        document.definition().surface.roughness==.44f&&prior->surface.roughness==.27f,"Clean Quader refresh failed to publish a new editable revision or mutated the previous snapshot.");
    require(document.load(DefaultMaterialId)&&!document.editable()&&!document.begin("Edit protected default")&&!document.save(),"Protected default material became editable through Quader authoring.");
}
void mappingAndSamplerTransactions() {
    Project project;MaterialDocument document(project.root);require(document.create("Mapped"),"Mapped material create failed.");
    const auto before=document.definition();const auto published=document.committed();const auto revision=document.revision();
    require(document.begin("UV gesture") && document.update([](auto& a){uv(a).scale={2,3,4};uv(a).offset={-.2f,.3f,1};uv(a).rotation={10,20,45};uv(a).mode=MaterialUvMode::LocalTriplanar;uv(a).scaleLinked=false;}),
        "UV gesture failed.");
    require(document.committed()==published && document.revision()==revision && same(*published,before),"Working UV values leaked to shared material users.");
    require(document.commit() && document.undo() && same(document.definition(),before) && document.redo(),"UV gesture did not form one undoable command.");
    const auto local=document.definition();
    require(!local.surface.triplanarWorld && document.edit("World Triplanar",[](auto& a){uv(a).mode=MaterialUvMode::WorldTriplanar;}) &&
        document.definition().surface.triplanarWorld && document.committed()->surface.triplanarWorld,
        "World Triplanar was not an independently committed material edit.");
    require(document.undo() && same(document.definition(),local) && document.redo() && document.definition().surface.triplanarWorld,
        "World Triplanar undo/redo changed other mapping settings or lost its projection space.");
    require(document.edit("Shared sampler",[](auto& a){texture(a).sampler=ModelSampler{ModelWrap::Mirror,ModelWrap::Clamp,ModelFilter::NearestMipmapNearest,ModelFilter::Nearest};}),
        "Shared sampler edit failed.");
    const auto mapped=document.definition();require(document.save() && document.load(mapped.id) && same(document.definition(),mapped),"Mapping and shared sampler did not survive Save/reopen.");
    const auto savedRevision=document.revision();
    require(document.begin("Cancel UV") && document.update([](auto& a){uv(a).offset[0]=5;uv(a).scaleLinked=true;uv(a).mode=MaterialUvMode::LocalTriplanar;}),"UV cancellation setup failed.");
    document.cancel();require(same(document.definition(),mapped) && document.revision()==savedRevision && !document.dirty(),"Cancel changed committed UV mapping or link state.");
    require(!document.edit("Invalid mapping",[](auto& a){uv(a).rotation[1]=std::numeric_limits<float>::infinity();}) && same(document.definition(),mapped),"Invalid UV mapping replaced the last valid document.");
    require(document.edit("Unlit transition",[](auto& a){materialOutputNode(a.graph).output.shading=MaterialShading::Unlit;materialOutputNode(a.graph).output.blend=MaterialBlend::Alpha;}) && particleMaterialCompatible(document.definition()),
        "New Lit default could not become a sprite material through an explicit mode change.");
    require(document.undo() && same(document.definition(),mapped),"Shading transition lost UV mapping or sampler on undo.");
}
void spriteMappingMigration() {
    Project project;auto legacy=defaultParticleMaterialAsset();
    legacy.id="assets/materials/legacy-sprite.crimson-material";legacy.name="Legacy sprite";
    legacy.surface.baseColorTexture.image.id="assets/textures/sprite.png";
    legacy.surface.uvScale={2,-3,4};legacy.surface.uvOffset={-.2f,.3f,1};legacy.surface.uvRotation={10,20,45};
    legacy.surface.uvScaleLinked=false;legacy.surface.triplanar=true;
    fs::create_directories(project.root/"assets/materials");
    for(const auto blend:{MaterialBlend::Alpha,MaterialBlend::Additive})for(const bool world:{false,true}) {
        legacy.blend=blend;legacy.surface.triplanarWorld=world;
        const auto bytes=material_test::legacyMaterialJson(legacy).dump(2);
        {std::ofstream output(project.root/legacy.id,std::ios::binary);output<<bytes;}
        MaterialDocument document(project.root);require(document.load(legacy.id),"Legacy sprite migration failed.");
        require(document.editable()&&!document.dirty()&&text(project.root/legacy.id)==bytes,"Sprite migration changed source bytes or edit authority.");
        const auto& migrated=document.definition();
        require(particleMaterialCompatible(migrated),"Migrated sprite became incompatible.");
        MaterialNodeId mapping{};
        for(const auto id:materialGraphOrder(migrated.graph,migrated.shading,migrated.blend)) {
            const auto& n=*findMaterialNode(migrated.graph,id);if(n.kind!=MaterialNodeKind::UV)continue;mapping=id;
            require(n.uv.mode==MaterialUvMode::UV0&&n.uv.scale==legacy.surface.uvScale&&n.uv.offset==legacy.surface.uvOffset&&
                n.uv.rotation==legacy.surface.uvRotation&&!n.uv.scaleLinked,"Sprite migration changed its sampling transform.");
        }
        require(mapping!=0,"Migrated sprite lost its active mapping node.");
        const auto original=document.definition();
        for(const auto mode:{MaterialUvMode::UV1,MaterialUvMode::LocalTriplanar,MaterialUvMode::WorldTriplanar}) {
            require(document.edit("Shared material projection",[&](auto& a){findMaterialNode(a.graph,mapping)->uv.mode=mode;})&&
                particleMaterialCompatible(document.definition())&&modelMaterialCompatible(document.definition()),"Unified material rejected a supported mapping mode.");
            require(document.undo()&&same(document.definition(),original),"Mapping undo changed the migrated graph.");
        }
        require(document.save()&&document.load(legacy.id)&&same(document.definition(),original),"Migrated sprite mapping did not survive a v6 save/reload.");
    }
}
void fogReceptionTransactions() {
    Project project;MaterialDocument document(project.root);
    require(document.create("Fog reception"),"Fog material creation failed.");
    for(const auto shading:{MaterialShading::StandardPbr,MaterialShading::Unlit}) {
        require(document.edit("Configure fog fixture",[&](auto& asset){auto& output=materialOutputNode(asset.graph).output;
            output.shading=shading;output.receiveFog=true;})&&document.save(),"Fog fixture configuration failed.");
        const auto original=document.definition();const auto committed=document.committed();
        const auto savedBytes=text(materialAssetPath(project.root,document.assetId()));
        require(materialOutputNode(original.graph).output.receiveFog&&original.surface.receiveFog,"New material did not receive fog by default.");
        require(document.begin("Toggle material fog")&&document.update([](auto& asset){materialOutputNode(asset.graph).output.receiveFog=false;}),
            "Receive Fog gesture failed.");
        require(!document.definition().surface.receiveFog&&document.committed()==committed&&committed->surface.receiveFog,
            "Working fog toggle changed the committed snapshot before finishing the gesture.");
        document.cancel();require(same(document.definition(),original)&&!document.dirty(),"Cancel failed to restore material fog and saved state.");
        require(document.edit("Toggle material fog",[](auto& asset){materialOutputNode(asset.graph).output.receiveFog=false;}),"Receive Fog edit failed.");
        require(document.dirty()&&!document.committed()->surface.receiveFog&&committed->surface.receiveFog&&
            text(materialAssetPath(project.root,document.assetId()))==savedBytes,"Fog commit mutated the old snapshot or saved file.");
        require(document.undo()&&same(document.definition(),original)&&!document.dirty(),"Fog undo lost original material state.");
        require(document.redo()&&!document.definition().surface.receiveFog&&document.dirty(),"Fog redo did not restore the authored exclusion.");
        const auto changed=document.definition();const auto id=document.assetId();
        require(document.save()&&document.load(id)&&same(document.definition(),changed)&&!document.dirty(),"Fog exclusion did not survive save/reopen.");
    }
}
void usageBlendAndHdrTransactions() {
    Project project;MaterialDocument document(project.root);require(document.create("Coverage history"),"Coverage fixture creation failed.");
    const auto original=document.definition();const auto output=materialOutputNode(original.graph).id;
    const auto maskLink=materialInputLink(original.graph,output,MaterialPin::OpacityMask)->id;
    require(document.edit("Lit Translucent",[](auto& asset){materialOutputNode(asset.graph).output.blend=MaterialBlend::Alpha;}),"Lit Translucent was rejected.");
    require(modelMaterialCompatible(document.definition())&&particleMaterialCompatible(document.definition()),"Shared translucent material was rejected by a geometry consumer.");
    MaterialNodeId coverage{};
    require(document.edit("Coverage",[&](auto& asset){coverage=addMaterialNode(asset.graph,MaterialNodeKind::Scalar).id;
        findMaterialNode(asset.graph,coverage)->scalar=.2f;connectMaterialNodes(asset.graph,coverage,MaterialPin::Value,output,MaterialPin::Opacity);}),"Could not connect separate alpha coverage.");
    const auto alpha=document.definition();
    require(document.edit("Masked",[](auto& asset){materialOutputNode(asset.graph).output.blend=MaterialBlend::Mask;})&&
        materialInputLink(document.definition().graph,output,MaterialPin::OpacityMask)->id==maskLink&&
        materialInputLink(document.definition().graph,output,MaterialPin::Opacity)->fromNode==coverage,"Blend switch lost inactive coverage connections.");
    require(document.undo()&&same(document.definition(),alpha)&&document.redo(),"Blend switch did not undo/redo atomically.");
    const auto masked=document.definition();
    require(document.edit("Unlit model",[](auto& asset){materialOutputNode(asset.graph).output.shading=MaterialShading::Unlit;})&&particleMaterialCompatible(document.definition()),"Shared Unlit model transition failed.");
    require(document.undo()&&same(document.definition(),masked),"Model transition lost former shader/blend or inactive connections on Undo.");
    require(document.begin("HDR value")&&document.update([](auto& asset){parameter(asset,MaterialPin::BaseColor).color={12.5f,6295.f,.25f,.8f};})&&document.commit(),"HDR value transaction rejected finite existing values.");
    const auto hdr=document.definition();require(document.save()&&document.load(document.assetId())&&same(document.definition(),hdr),"Save/reopen normalized HDR channels.");
}
}
int main(int argc,char** argv){try{if(argc==4&&std::string(argv[1])=="--verify-material"){verifyActualMaterial(argv[2],argv[3]);return 0;}
    require(argc==1,"Expected --verify-material <project root> <material id>, or no arguments.");
    publicationAndHistory();displacementTransactions();identityAndExternalAuthority();dormantGraphTextureHistory();reconcileDeletedGraphTextureHistory();boundedHistoryAndRedo();protectedDefaultsAndImportedAuthoring();importedPackedMapsAndPromotion();sharedQuaderMaterialAuthoring();mappingAndSamplerTransactions();spriteMappingMigration();fogReceptionTransactions();usageBlendAndHdrTransactions();std::cout<<"Material document transactions, history, storage and authority checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
