#include "tools/AssetBrowserPaths.h"
#include "resources/Paths.h"
#include "resources/AssetId.h"
#include "quader_bundle_fixture.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-browser-paths-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto resolved=fs::weakly_canonical(root,error),temporary=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&resolved.parent_path()==temporary&&resolved.filename().string().starts_with("crimson-browser-paths-"))fs::remove_all(resolved,error);}
    void write(const std::string& id,const std::string& contents="fixture") {const auto path=resolveAssetPath(root,id);fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output<<contents;require(bool(output),"Could not write the disposable asset path fixture.");output.close();registerAssetPath(root,id,path);}
};
void pathsAndNames() {
    Project project;project.write("assets/textures/Stone Floor.png");
    const auto image=resolveBrowserAssetLocation(project.root,{"assets/textures/Stone Floor.png","Stone Floor.png",AssetKind::Texture});
    require(image.available&&image.file==fs::weakly_canonical(project.root/"Content/textures/Stone Floor.png")&&
        image.directory==image.file.parent_path()&&image.directory.filename()=="textures"&&image.name=="Stone Floor",
        "Texture actions did not resolve its actual file, parent-only path and extensionless name.");
    const auto alias=resolveBrowserAssetLocation(project.root,{"assets/textures/Stone Floor.png","Renamed stone.png",AssetKind::Texture});
    require(alias.file==image.file&&alias.directory==image.directory&&alias.name=="Renamed stone","A browser alias changed the physical asset location or was ignored by Copy Name.");
    const auto colonAlias=resolveBrowserAssetLocation(project.root,{"assets/textures/Stone Floor.png","A:Brick.png",AssetKind::Texture});
    require(colonAlias.available&&colonAlias.name=="A:Brick","Copy Name interpreted a display alias as a drive-relative filesystem path.");
    for(const auto& asset:{AssetEntry{"assets/materials/opaque-id.crimson-material","Warm Stone",AssetKind::Material},
        AssetEntry{"assets/scenes/scene-id.crimson-scene","Courtyard.crimson-scene",AssetKind::Scene},
        AssetEntry{"assets/particles/effect.crimson-particle","Embers.crimson-particle",AssetKind::Particle},
        AssetEntry{"assets/textures/studio.hdr","Studio.hdr",AssetKind::Environment}}) {
        project.write(asset.id);const auto location=resolveBrowserAssetLocation(project.root,asset);
        require(location.available&&location.directory==location.file.parent_path()&&!location.name.empty(),"A native asset/environment did not expose its real managed location.");
    }
    const auto unicodeId=std::string("assets/textures/")+"muro "+"\xc3\xa8"+".png";project.write(unicodeId);
    const auto unicode=resolveBrowserAssetLocation(project.root,{unicodeId,"muro \xc3\xa8.png",AssetKind::Texture});
    require(unicode.available&&unicode.name=="muro \xc3\xa8","Asset path/name conversion damaged Unicode.");
    const auto renamed=project.root/"Content/Organized/Renamed.png";fs::create_directories(renamed.parent_path());
    relocateContentPaths(project.root,std::array{ContentPathMove{image.file,renamed}});
    const auto moved=resolveBrowserAssetLocation(project.root,{"assets/textures/Stone Floor.png","Stone Floor.png",AssetKind::Texture});
    require(moved.available&&moved.file==fs::weakly_canonical(renamed)&&moved.directory==renamed.parent_path()&&moved.name=="Stone Floor",
        "Asset context actions used the opaque identity as a path after physical movement.");
    const auto engine=resolveBrowserAssetLocation(project.root,{canonicalAssetId("builtin:texture/gray"),"Gray.png",AssetKind::Texture});
    require(engine.available&&engine.file==resolveAssetPath(project.root,"builtin:texture/gray")&&engine.directory.filename()=="textures"&&engine.name=="Gray",
        "A materialized protected Engine asset did not expose its real location.");
}
void modelDocumentsAndUnavailableAssets() {
    Project project;project.write("assets/models/tree/manifest.json",R"({"original":"source.glb","document":"model.gltf"})");
    project.write("assets/models/tree/source.glb");project.write("assets/models/tree/model.gltf");
    auto model=resolveBrowserAssetLocation(project.root,{"assets/models/tree/manifest.json","Ancient Tree.glb",AssetKind::Model});
    require(model.available&&model.file.filename()=="source.glb"&&model.name=="Ancient Tree","Model actions selected a manifest instead of the managed source document.");
    project.write("assets/models/normalized/manifest.json","{}");project.write("assets/models/normalized/model.gltf");
    model=resolveBrowserAssetLocation(project.root,{"assets/models/normalized/manifest.json","Normalized",AssetKind::Model});
    require(model.available&&model.file.filename()=="model.gltf","A normalized model package had no Explorer location.");
    project.write("assets/models/unsafe/manifest.json",R"({"original":"../../textures/external.png"})");project.write("assets/textures/external.png");
    model=resolveBrowserAssetLocation(project.root,{"assets/models/unsafe/manifest.json","Unsafe",AssetKind::Model});
    require(model.available&&model.file.filename()=="manifest.json","A model manifest escaped its own package when resolving the original document.");
    for(const auto* id:{"builtin:material-default","folder:art","../outside.png","assets/../outside.png","C:/outside.png","assets/textures/missing.png"}) {
        const auto missing=resolveBrowserAssetLocation(project.root,{id,"Visible name.png",AssetKind::Texture});
        require(!missing.available&&missing.file.empty()&&missing.directory.empty()&&missing.name=="Visible name","A virtual/missing/outside asset fabricated a filesystem location or lost its copyable name.");
    }
}
void bundleSourceLocations() {
    Project project;const auto fixture=bundle_test::writeFixture(project.root);
    const auto location=resolveBrowserAssetLocation(project.root,{fixture.modelId,"Triangle",AssetKind::Model});
    require(location.available&&location.file==fs::weakly_canonical(fixture.file)&&location.directory==fixture.file.parent_path()&&location.name=="Triangle",
        "A Quader submodel exposed a disposable cache package instead of its tracked bundle.");
    const auto cache=resolveAssetPath(project.root,fixture.modelId).parent_path();
    const auto cacheRoot=fs::weakly_canonical(project.root/".crimson/cache/quader-imports");
    require(!cache.lexically_relative(cacheRoot).empty()&&*cache.lexically_relative(cacheRoot).begin()!="..","Bundle path fixture escaped its generated cache owner.");
    fs::remove_all(cache);
    const auto missing=resolveBrowserAssetLocation(project.root,{fixture.modelId,"Triangle",AssetKind::Model});
    require(missing.available&&missing.file==location.file,"Missing generated data hid the existing tracked bundle location.");
}
}
int main(){try{pathsAndNames();modelDocumentsAndUnavailableAssets();bundleSourceLocations();std::cout<<"Asset browser locations, names and managed model documents passed. Explorer and clipboard were not invoked.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
