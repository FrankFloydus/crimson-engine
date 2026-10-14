#include "editor/MaterialDocument.h"
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
    require(document.begin("Color gesture") && document.update([](auto& a){a.surface.baseColor={.1f,.2f,.3f,.5f};}) &&
        document.update([](auto& a){a.surface.baseColor={.2f,.3f,.4f,.6f};}),"Valid color gesture failed.");
    require(document.dirty() && document.committed()==committed && document.revision()==revision && same(*committed,original),"Intermediate material edits leaked to shared users.");
    require(document.commit() && document.revision()>revision && document.committed()!=committed,"Completed gesture did not publish an immutable shared revision.");
    require(same(*committed,original),"Shared publication mutated a retained old snapshot.");
    require(document.undo() && same(document.definition(),original) && !document.dirty() && !document.canUndo(),"Color drag was not one command or clean state was lost.");
    require(document.redo() && document.dirty(),"Redo did not restore the material gesture.");
    require(document.save() && !document.dirty(),"Material Save did not mark its state clean.");
    require(document.undo() && document.dirty() && document.redo() && !document.dirty(),"Saved-state tokens did not follow history.");
    const auto valid=document.definition();const auto version=document.revision();
    require(!document.edit("Invalid color",[](auto& a){a.surface.baseColor[0]=std::numeric_limits<float>::quiet_NaN();}),"Nonfinite material edit was accepted.");
    require(document.revision()==version && same(document.definition(),valid) && !document.dirty(),"Rejected material edit changed its working/committed state.");
    require(!document.edit("Identity",[](auto& a){a.id="assets/materials/other.crimson-material";}),"Material edit changed managed identity.");
    require(!document.edit("Incompatible shading",[](auto& a){a.shading=MaterialShading::Unlit;}),"Unlit with opaque blending was accepted.");
    require(document.begin("Cancel") && document.update([](auto& a){a.surface.baseColor[3]=.1f;}),"Could not prepare cancel.");
    document.cancel();require(same(document.definition(),valid) && document.revision()==version && !document.dirty(),"Cancel published a shared revision or changed data.");
    require(!document.load("assets/materials/missing.crimson-material") && same(document.definition(),valid),"Failed load replaced the working material.");
}
void identityAndExternalAuthority(){
    Project project;MaterialDocument document(project.root);require(document.create("Original"),"Create failed.");
    const auto sourceId=document.assetId(),sourceText=text(materialAssetPath(project.root,sourceId));
    require(document.edit("Texture",[](auto& a){a.surface.baseColorTexture.image.id="assets/textures/first.png";}),"Texture edit failed.");
    require(document.edit("Replace texture",[](auto& a){a.surface.baseColorTexture.image.id="assets/textures/second.png";}),"Texture replacement failed.");
    const auto refs=document.historicalTextureReferences();
    require(std::find(refs.begin(),refs.end(),"assets/textures/first.png")!=refs.end() &&
        std::find(refs.begin(),refs.end(),"assets/textures/second.png")!=refs.end(),"Historical texture usages omit an undo/redo dependency.");
    require(document.save("Copy",true) && document.assetId()!=sourceId && !document.dirty(),"Save As did not create an independent clean identity.");
    const auto copyId=document.assetId();
    require(document.undo() && document.assetId()==copyId && document.dirty(),"Save As Undo resurrected the original identity.");
    require(document.redo() && document.assetId()==copyId && document.definition().name=="Copy" && !document.dirty(),"Save As Redo lost copied identity or saved state.");
    require(text(materialAssetPath(project.root,sourceId))==sourceText,"Save As modified the original file.");
    require(document.edit("Unsaved blend",[](auto& a){a.blend=MaterialBlend::Mask;}),"Blend edit failed.");
    auto external=loadMaterialAsset(project.root,copyId);external.surface.baseColor[3]=.15f;saveMaterialAsset(project.root,external);
    require(document.externalFileChanged() && document.definition().blend==MaterialBlend::Mask && document.dirty(),"External write replaced the current document authority.");
    require(document.load(copyId) && !document.dirty() && same(document.definition(),external) && !document.externalFileChanged(),"Explicit external reload failed.");
    require(document.edit("Unsaved",[](auto& a){a.surface.baseColor[3]=.75f;}) && document.discard() && !document.dirty() && same(document.definition(),external),"Discard did not restore last saved values.");
    const auto before=document.definition();std::ofstream bad(materialAssetPath(project.root,copyId));bad<<"{}";bad.close();
    require(document.externalFileChanged() && !document.load(copyId) && same(document.definition(),before),"Malformed external reload destroyed a valid working revision.");
    require(document.save() && !document.externalFileChanged(),"Explicit overwrite did not recover the saved material.");
#ifdef _WIN32
    require(document.edit("Write failure",[](auto& a){a.surface.baseColor[3]=.8f;}),"Could not prepare failed save.");
    const auto file=materialAssetPath(project.root,copyId);const auto disk=text(file);const auto snapshot=document.committed();
    require(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"Could not make the save fixture read-only.");
    const bool failed=!document.save(),unchanged=text(file)==disk;
    SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL);
    require(failed && unchanged && document.assetId()==copyId && document.dirty() && document.committed()==snapshot,"Failed atomic save changed data, identity, dirty state or authority.");
    require(document.save() && !document.dirty(),"Save did not recover after failure.");
#endif
}
void boundedHistoryAndRedo() {
    Project project;MaterialDocument document(project.root);require(document.create(),"Create failed.");
    for(size_t edit=0;edit<MaterialDocument::MaximumHistoryEntries+12;++edit)
        require(document.edit("Opacity",[&](auto& a){a.surface.baseColor[3]=static_cast<float>(edit+1)/400.f;}),"Bounded history edit failed.");
    size_t undone{};while(document.undo())++undone;
    require(undone==MaterialDocument::MaximumHistoryEntries && document.canRedo(),"Material history retained more than its bounded limit.");
    const auto revision=document.revision();
    require(!document.edit("Invalid replacement",[](auto& a){a.surface.baseColor[3]=-1;}) && document.canRedo() && document.revision()==revision,"Rejected edit destroyed redo.");
    require(document.edit("New branch",[](auto& a){a.surface.baseColor[3]=.75f;}) && !document.canRedo(),"A valid new command did not replace redo history.");
}
void readOnlyAndCopy(){
    Project project;MaterialDocument document(project.root);
    require(document.load(DefaultParticleMaterialId) && !document.editable() && !document.externalFileChanged(),"Built-in material did not open read-only.");
    require(!document.begin("Edit builtin") && !document.save(),"Built-in material accepted edits or overwrite.");
    require(document.save("Saved sprite",true) && document.editable() && !document.dirty() && document.assetId()!=DefaultParticleMaterialId,"Save As did not create an editable native asset.");
    auto imported=createMaterialAsset(project.root,"Imported fixture");imported.originModel="assets/models/fixture/manifest.json";
    imported.shading=MaterialShading::StandardPbr;imported.blend=MaterialBlend::Opaque;saveMaterialAsset(project.root,imported);
    require(document.load(imported.id) && !document.editable() && !document.begin("Imported edit") && !document.save(),"Imported original accepted an overwrite.");
    const auto original=text(materialAssetPath(project.root,imported.id));
    require(document.save("Native PBR",true) && document.editable() && document.definition().originModel.empty() && document.assetId()!=imported.id,"Save As did not create an editable native PBR asset.");
    require(document.edit("PBR appearance",[](auto& a){a.surface.roughness=.23f;a.surface.metallic=.75f;a.surface.normalTexture.image.id="assets/textures/normal.png";}) && document.save(),"Native PBR editing or storage failed.");
    const auto saved=document.definition();
    require(document.undo() && document.redo() && same(document.definition(),saved) && !document.dirty(),"PBR material undo/redo lost its saved state.");
    require(document.load(saved.id) && same(document.definition(),saved) && text(materialAssetPath(project.root,imported.id))==original,"Native PBR roundtrip changed the imported original.");
}
void mappingAndSamplerTransactions() {
    Project project;MaterialDocument document(project.root);require(document.create("Mapped"),"Mapped material create failed.");
    const auto before=document.definition();const auto published=document.committed();const auto revision=document.revision();
    require(document.begin("UV gesture") && document.update([](auto& a){a.surface.uvScale={2,3,4};a.surface.uvOffset={-.2f,.3f,1};a.surface.uvRotation={10,20,45};a.surface.triplanar=true;a.surface.uvScaleLinked=false;}),
        "UV gesture failed.");
    require(document.committed()==published && document.revision()==revision && same(*published,before),"Working UV values leaked to shared material users.");
    require(document.commit() && document.undo() && same(document.definition(),before) && document.redo(),"UV gesture did not form one undoable command.");
    const auto local=document.definition();
    require(!local.surface.triplanarWorld && document.edit("World Triplanar",[](auto& a){a.surface.triplanarWorld=true;}) &&
        document.definition().surface.triplanarWorld && document.committed()->surface.triplanarWorld,
        "World Triplanar was not an independently committed material edit.");
    require(document.undo() && same(document.definition(),local) && document.redo() && document.definition().surface.triplanarWorld,
        "World Triplanar undo/redo changed other mapping settings or lost its projection space.");
    require(document.edit("Shared sampler",[](auto& a){a.surface.sharedSampler=ModelSampler{ModelWrap::Mirror,ModelWrap::Clamp,ModelFilter::NearestMipmapNearest,ModelFilter::Nearest};}),
        "Shared sampler edit failed.");
    const auto mapped=document.definition();require(document.save() && document.load(mapped.id) && same(document.definition(),mapped),"Mapping and shared sampler did not survive Save/reopen.");
    const auto savedRevision=document.revision();
    require(document.begin("Cancel UV") && document.update([](auto& a){a.surface.uvOffset[0]=5;a.surface.uvScaleLinked=true;a.surface.triplanarWorld=false;}),"UV cancellation setup failed.");
    document.cancel();require(same(document.definition(),mapped) && document.revision()==savedRevision && !document.dirty(),"Cancel changed committed UV mapping or link state.");
    require(!document.edit("Invalid mapping",[](auto& a){a.surface.uvRotation[1]=std::numeric_limits<float>::infinity();}) && same(document.definition(),mapped),"Invalid UV mapping replaced the last valid document.");
    require(document.edit("Unlit transition",[](auto& a){a.shading=MaterialShading::Unlit;a.blend=MaterialBlend::Alpha;}) && particleMaterialCompatible(document.definition()),
        "New Lit default could not become a sprite material through an explicit mode change.");
    require(document.undo() && same(document.definition(),mapped),"Shading transition lost UV mapping or sampler on undo.");
}
}
int main(){try{publicationAndHistory();identityAndExternalAuthority();boundedHistoryAndRedo();readOnlyAndCopy();mappingAndSamplerTransactions();std::cout<<"Material document transactions, history, storage and authority checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
