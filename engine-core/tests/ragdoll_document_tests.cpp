#include "ragdoll_fixture.h"
#include "editor/RagdollDocument.h"
#include "assets/AtomicFile.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
using namespace crimson;using namespace crimson::ragdoll_test;
std::string read(const std::filesystem::path& file){std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void isolatedHistory(){
    Project project;const auto original=asset();saveRagdollAsset(project.root,original);const auto bytes=read(ragdollAssetPath(project.root,original.id));RagdollDocument document(project.root);
    require(document.load(original.id)&&!document.dirty(),"Loading Ragdoll changed dirty state.");const auto revision=document.revision();
    require(document.begin("Resize body")&&document.update([](auto& v){v.bodies[0].radius=.03f;}),"Could not start ragdoll body gesture.");
    require(document.dirty()&&document.revision()==revision&&*document.committed()==original&&read(ragdollAssetPath(project.root,original.id))==bytes,"Uncommitted collider gesture escaped preview isolation.");
    document.cancel();require(document.definition()==original&&!document.canUndo()&&!document.dirty(),"Cancel changed authored setup or history.");
    require(document.begin("Body mass")&&document.update([](auto& v){v.bodies[0].mass=5;})&&document.update([](auto& v){v.bodies[0].mass=6;})&&document.commit(),"Mass gesture failed.");
    require(document.undo()&&document.definition()==original&&!document.dirty()&&document.redo()&&document.definition().bodies[0].mass==6,"Single gesture undo/redo lost physical settings.");
    require(document.save()&&!document.dirty(),"Ragdoll save did not mark the committed state clean.");
    require(!document.edit("Invalid identity",[](auto& v){v.id=newAssetId();}),"Property gesture replaced Ragdoll identity.");
    require(!document.edit("Invalid source",[](auto& v){v.sourceModel.id=newAssetId();}),"Property gesture silently rebound Ragdoll source.");
    require(document.begin("Recover gesture")&&!document.update([](auto& v){v.bodies[0].mass=std::numeric_limits<float>::infinity();})&&!document.commit(),"Invalid ragdoll gesture committed.");
    require(document.update([](auto& v){v.bodies[0].mass=7;})&&document.commit(),"A corrected gesture could not commit.");
    require(document.discard()&&!document.dirty()&&document.definition().bodies[0].mass==6,"Discard did not restore saved physical setup.");
}
void sourceRemapAndConflicts(){
    Project project;const auto original=asset();RagdollDocument document(project.root);require(document.create(original)&&document.dirty()&&!document.externalFileChanged(),"New unfinished rig could not be authored before its first save.");
    require(document.save()&&!document.dirty(),"First save failed.");const auto originalBytes=read(ragdollAssetPath(project.root,original.id));auto next=source();next.id=newAssetId();
    require(!document.rebindSource(next,0,{3,3})&&document.definition()==original,"Invalid explicit remapping partly modified the document.");
    require(document.rebindSource(next,0,{3,4})&&document.definition().sourceModel.id==next.id,"Explicit remapping failed.");
    const auto refs=document.modelReferences(true);require(std::find(refs.begin(),refs.end(),original.sourceModel.id)!=refs.end()&&std::find(refs.begin(),refs.end(),next.id)!=refs.end(),"Remapping history lost retained source-model references.");
    require(document.undo()&&document.definition()==original&&document.redo(),"Source mapping did not undo/redo as one transaction.");
    require(document.save("Variant",true)&&document.assetId()!=original.id,"Save As failed.");const auto copyId=document.assetId();while(document.canUndo())require(document.undo(),"Undo failed after Save As.");require(document.assetId()==copyId&&read(ragdollAssetPath(project.root,original.id))==originalBytes,"Save As or undo changed original identity/data.");
    require(document.save(),"Saving copied document failed.");auto external=loadRagdollAsset(project.root,copyId);external.gravityFactor=.5f;saveRagdollAsset(project.root,external);
    require(document.externalFileChanged()&&!document.save()&&loadRagdollAsset(project.root,copyId)==external,"External conflict was silently overwritten.");
    const auto local=document.definition();require(document.save({},false,true)&&!document.externalFileChanged()&&!document.dirty()&&loadRagdollAsset(project.root,copyId)==local,"Explicit Overwrite did not replace the external revision or accept its new baseline.");
    saveRagdollAsset(project.root,external);
    require(document.load(copyId)&&document.definition()==external&&!document.dirty(),"Reload did not accept external rig safely.");writeFileAtomically(ragdollAssetPath(project.root,copyId),"{broken ragdoll");
    require(!document.load(copyId)&&document.definition()==external,"Corrupt external replacement destroyed the open rig.");document.close();require(!document.isOpen()&&document.modelReferences().empty(),"Closing retained authoring references.");
}
}
int main(){try{isolatedHistory();sourceRemapAndConflicts();std::cout<<"Ragdoll documents preserve preview isolation, transactions, source remapping, references, Save As and external conflicts.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
