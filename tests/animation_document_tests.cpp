#include "editor/AnimationDocument.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("ce-animdoc-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error),parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("ce-animdoc-"))std::filesystem::remove_all(target,error);}
};
AnimationAsset seed() {
    AnimationAsset animation;animation.id=newAssetId();animation.name="Walk";animation.sourceModel.id=newAssetId();
    animation.sourceClipIdentity=newAssetId();animation.bindingSignature=newAssetId();animation.endTime=3;animation.rootNode=7;
    return animation;
}
void isolatedGesturesAndHistory() {
    Project project;const auto original=seed();saveAnimationAsset(project.root,original);
    const auto sourceBytes=read(animationAssetPath(project.root,original.id));
    AnimationDocument document(project.root);require(document.load(original.id),document.status());
    require(!document.dirty()&&document.definition()==original,"Opening an Animation changed authored fields or dirty state.");
    const auto revision=document.revision(),preview=document.previewRevision();
    require(document.begin("Root lock gesture"),"Could not begin root gesture.");
    require(document.update([](auto& animation){animation.forceRootLock=true;animation.rootLockReference=AnimationRootLockReference::ReferencePose;}),document.status());
    require(document.dirty()&&document.revision()==revision&&document.previewRevision()>preview&&*document.committed()==original,
        "An uncommitted root gesture escaped preview isolation.");
    require(read(animationAssetPath(project.root,original.id))==sourceBytes,"Preview edited the asset on disk.");
    document.cancel();require(!document.dirty()&&document.definition()==original&&!document.canUndo(),"Canceled gesture changed authoring or history.");
    require(document.begin("Trim animation"),"Could not begin trim.");
    require(document.update([](auto& animation){animation.startTime=.25f;}),document.status());
    require(document.update([](auto& animation){animation.startTime=.5f;animation.endTime=2.5f;}),document.status());
    require(document.commit()&&document.canUndo()&&document.revision()==revision+1,"A continuous gesture did not create one committed generation.");
    const auto trimmed=document.definition();require(document.undo()&&document.definition()==original&&!document.dirty()&&!document.canUndo(),"One gesture required multiple undos.");
    require(document.redo()&&document.definition()==trimmed,"Redo lost authored range.");
    require(document.save()&&!document.dirty()&&loadAnimationAsset(project.root,original.id)==trimmed,document.status());
    require(document.edit("Root lock",[](auto& animation){animation.forceRootLock=true;}),document.status());
    require(document.discard()&&!document.dirty()&&document.definition()==trimmed,"Discard lost saved range or retained root edit.");
    require(document.modelReferences()==std::vector<std::string>{original.sourceModel.id},"Open Animation did not retain its source model reference.");
}
void rejectedMutationAndSaveAs() {
    Project project;const auto original=seed();saveAnimationAsset(project.root,original);
    AnimationDocument document(project.root);require(document.load(original.id),document.status());
    const auto bytes=read(animationAssetPath(project.root,original.id));
    require(!document.edit("Change identity",[](auto& animation){animation.id=newAssetId();})&&document.definition()==original,
        "Document edit changed Animation identity.");
    require(!document.edit("Rebind source",[](auto& animation){animation.sourceModel.id=newAssetId();})&&document.definition()==original,
        "Ordinary property edit rebound imported source.");
    require(!document.edit("Invalid trim",[](auto& animation){animation.startTime=4;})&&document.definition()==original,
        "Invalid range escaped canceled transaction.");
    require(document.begin("Recover valid gesture"),"Could not begin gesture.");
    require(!document.update([](auto& animation){animation.playbackRate=std::numeric_limits<float>::infinity();})&&!document.commit(),
        "Invalid gesture committed.");
    require(document.update([](auto& animation){animation.playbackRate=2;})&&document.commit(),document.status());
    require(document.save("Run",true),document.status());const auto copyId=document.assetId();
    require(copyId!=original.id&&document.definition().sourceModel==original.sourceModel&&
        read(animationAssetPath(project.root,original.id))==bytes,"Save As replaced source Animation or changed imported track binding.");
    while(document.canUndo())require(document.undo(),"Undo failed after Save As.");
    require(document.assetId()==copyId,"Undo after Save As restored the source identity.");
    require(document.save(),document.status());
    auto external=loadAnimationAsset(project.root,copyId);external.loop=false;saveAnimationAsset(project.root,external);
    require(document.externalFileChanged(),"External edit was not detected.");
    require(document.load(copyId)&&!document.dirty()&&document.definition()==external&&!document.externalFileChanged(),document.status());
    writeFileAtomically(animationAssetPath(project.root,copyId),"{broken animation");
    require(document.externalFileChanged()&&!document.load(copyId)&&document.definition()==external,
        "Corrupt external replacement destroyed the open Animation.");
    document.close();require(!document.isOpen()&&document.modelReferences().empty(),"Closed document retained authoring references.");
}
}
int main(){try{isolatedGesturesAndHistory();rejectedMutationAndSaveAs();std::cout<<"Animation documents preserve gesture isolation, root/range history, source binding, Save As and external conflicts.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
