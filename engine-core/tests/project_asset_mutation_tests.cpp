#include "integration/project_asset_mutations.hpp"
#include "assets/AssetLibrary.h"
#include "editor/assets/project_asset_deletion_service.hpp"
#include "editor/session/project_session.hpp"
#include "scene/SceneDocument.h"

#include <QApplication>
#include <QTemporaryDir>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
using namespace crimson;
using quader::ProjectRelativePath;
void require(bool value,const std::string &message) {if(!value)throw std::runtime_error(message);}
ProjectRelativePath relative(std::string_view path) {
    auto parsed=ProjectRelativePath::parse_portable(path);require(parsed.ok(),parsed.error().message);return parsed.take_value();
}
std::string read(const fs::path &file) {
    std::ifstream input(file,std::ios::binary);require(bool(input),"Fixture file is missing: "+file.string());
    return {std::istreambuf_iterator<char>(input),{}};
}
struct Fixture final {
    QTemporaryDir temporary;
    fs::path root;
    quader::ProjectSession project;
    std::unique_ptr<AssetLibrary> assets;
    SceneDocument *active{};
    Fixture():root(temporary.path().toStdWString()) {
        require(temporary.isValid(),"Cannot create isolated project storage.");
        project.set_resource_root(fs::path(QUADER_SOURCE_DIR)/"resources/modeling_assets");
        const auto created=project.create(root);require(created.ok,created.message);
        assets=std::make_unique<AssetLibrary>(root);
        project.project_assets().set_mutation_prepare([this](const auto &mutation){
            return integration::prepare_project_asset_mutation(*assets,active,mutation);
        });
    }
    ~Fixture() {project.project_assets().set_mutation_prepare({});(void)project.close(true);}
};

void folder_move_and_copy() {
    Fixture fixture;auto &assets=*fixture.assets;auto &physical=fixture.project.project_assets();
    const auto material=assets.createMaterial("Stone");
    const auto original_material=resolveAssetPath(fixture.root,material->id);
    const auto uppercase=original_material.parent_path()/(original_material.stem().string()+".CRIMSON-MATERIAL");
    fs::rename(original_material,uppercase);registerAssetPath(fixture.root,material->id,uppercase);assets.refreshMaterials();
    auto particle=defaultParticleEffect("Dust");particle.id=newAssetId();particle.emitter.material.id=material->id;
    saveParticleAsset(fixture.root,particle);assets.refreshParticles();
    auto &browser=assets.browserCatalog();const auto folder=browser.createFolder(BrowserCatalog::RootFolder,"Art");
    browser.move(std::array{material->id,particle.id},folder);
    const auto material_path=resolveAssetPath(fixture.root,material->id);
    const auto particle_path=resolveAssetPath(fixture.root,particle.id);
    const auto material_bytes=read(material_path),particle_bytes=read(particle_path);
    const std::string binary{"\0source\xff",8};
    std::ofstream(fixture.root/"Art/source.bin",std::ios::binary).write(binary.data(),binary.size());
    const auto &observed=physical.refresh_if_changed();require(observed.ok(),observed.error().message);
    const auto renamed=physical.rename(relative("Art"),"Renamed");require(renamed.ok,renamed.message);
    require(!fs::exists(fixture.root/"Art")&&read(fixture.root/"Renamed/source.bin")==binary,"Folder move lost unrelated physical files.");
    require(resolveAssetPath(fixture.root,material->id).parent_path()==fixture.root/"Renamed","Native material UUID retained a stale location.");
    require(read(resolveAssetPath(fixture.root,material->id))==material_bytes&&read(resolveAssetPath(fixture.root,particle.id))==particle_bytes,
            "Physical move rewrote authored native bytes.");
    require(assets.browserCatalog().folderPath(folder)==fixture.root/"Renamed","Folder move lost its retained metadata identity.");
    require(physical.create_folder({},"Copies").ok,"Cannot create the physical copy destination.");
    const auto copied=physical.copy(std::array{relative("Renamed")},"Copies");require(copied.ok,copied.message);
    const auto copy_root=fixture.root/"Copies/Renamed";
    const auto copied_material=assetIdAtPath(fixture.root,copy_root/material_path.filename());
    const auto copied_particle=assetIdAtPath(fixture.root,copy_root/particle_path.filename());
    require(copied_material!=material->id&&copied_particle!=particle.id,"Folder copy reused native asset UUIDs.");
    require(loadMaterialAsset(fixture.root,copied_material).id==copied_material,"Mixed-case material copy was not reidentified.");
    const auto copied_effect=loadParticleAsset(fixture.root,copied_particle);
    require(copied_effect.id==copied_particle&&copied_effect.emitter.material.id==copied_material,"Folder copy did not remap its internal typed dependency.");
    require(loadParticleAsset(fixture.root,particle.id).emitter.material.id==material->id&&read(resolveAssetPath(fixture.root,material->id))==material_bytes,
            "Copy changed its source asset or references.");
    require(read(copy_root/"source.bin")==binary,"Folder copy changed arbitrary file bytes.");
    AssetLibrary reopened(fixture.root);
    require(reopened.findMaterial(copied_material)&&reopened.findParticle(copied_particle),"Copied native identities did not survive reopening the chosen project.");
}

void guarded_folder_deletion() {
    Fixture fixture;auto &assets=*fixture.assets;
    const auto material=assets.createMaterial("Guarded");
    auto &browser=assets.browserCatalog();const auto folder=browser.createFolder(BrowserCatalog::RootFolder,"DeleteMe");
    browser.move(std::array{material->id},folder);
    auto particle=defaultParticleEffect("Surviving");particle.id=newAssetId();particle.emitter.material.id=material->id;
    saveParticleAsset(fixture.root,particle);assets.refreshParticles();
    std::size_t recycle_calls{};
    quader::ProjectAssetDeletionService deletion(fixture.project,[&](std::span<const fs::path> paths){
        ++recycle_calls;quader::ProjectAssetRecycleResult result{.ok=true};
        for(const auto &path:paths){fs::remove_all(path);result.items.push_back({.original_path=path,.recycled=true});}
        return result;
    });
    auto preview=deletion.preview(std::array{relative("DeleteMe")});
    require(!preview.complete&&!preview.errors.empty()&&recycle_calls==0,"A surviving Engine reference did not guard mixed-folder deletion.");
    require(fs::is_regular_file(resolveAssetPath(fixture.root,material->id)),"Rejected deletion mutated the source asset.");
    particle.emitter.material.id=DefaultParticleMaterialId;saveParticleAsset(fixture.root,particle);assets.refreshParticles();
    assets.protectMaterialDocument(material->id);
    preview=deletion.preview(std::array{relative("DeleteMe")});
    require(!preview.complete,"An open native document did not protect its containing folder.");
    assets.protectMaterialDocument({});
    preview=deletion.preview(std::array{relative("DeleteMe")});require(preview.complete,preview.errors.empty()?"No deletion preview.":preview.errors.front());
    const auto erased=deletion.erase(std::array{relative("DeleteMe")},preview.revision);require(erased.ok,erased.message);
    require(recycle_calls==1&&!fs::exists(fixture.root/"DeleteMe"),"Mixed folder was not removed by the single physical transaction.");
    require(assets.references().deleted(material->id),"Physical folder deletion lost the asset tombstone.");
    require(std::ranges::none_of(assets.catalog(),[&](const auto &entry){return entry.id==material->id;}),"Deleted native material remained in the live catalog.");
    for(const auto &entry:storedAssetPaths(fixture.root))require(entry.id!=material->id,"Deleted asset retained an indexed location.");
    AssetLibrary reopened(fixture.root);require(reopened.references().deleted(material->id),"Asset deletion policy did not survive reopening.");
}
}
int main(int argc,char **argv) {
    QApplication application(argc,argv);
    try {folder_move_and_copy();guarded_folder_deletion();std::cout<<"Project asset mutation checks passed.\n";return 0;}
    catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
