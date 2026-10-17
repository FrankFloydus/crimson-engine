#include "AssetBrowserPaths.h"
#include "assets/ContentStorage.h"
#include "assets/QuaderSceneBundle.h"
#include "resources/Paths.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdint>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace crimson {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
bool contained(const std::filesystem::path& root,const std::filesystem::path& file) {
    const auto relative=file.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||relative.has_root_path())return false;
    for(const auto& part:relative)if(part=="..")return false;
    return true;
}
std::filesystem::path checkedFile(const std::filesystem::path& root,const std::filesystem::path& file) {
    std::error_code error;const auto canonical=std::filesystem::weakly_canonical(file,error);
    if(error||!contained(root,canonical)||!std::filesystem::is_regular_file(canonical,error)||error)return {};
    return canonical;
}
}
BrowserAssetLocation resolveBrowserAssetLocation(const std::filesystem::path& projectRoot,const AssetEntry& asset) {
    BrowserAssetLocation result;
    try {
        // Copy Name follows the name the user sees, including a browser alias.
        result.name=asset.name;
        if(const auto slash=result.name.find_last_of("/\\");slash!=std::string::npos)result.name.erase(0,slash+1);
        if(const auto dot=result.name.find_last_of('.');dot!=std::string::npos&&dot!=0)result.name.resize(dot);
        if(asset.id.empty()||asset.id.starts_with("folder:"))return result;
        std::error_code error;const auto root=std::filesystem::weakly_canonical(projectRoot,error);
        if(error)return result;
        const auto bundle=asset.kind==AssetKind::Model?quaderBundleSource(root,asset.id):std::nullopt;
        auto file=checkedFile(root,bundle?*bundle:resolveAssetPath(root,asset.id));
        if(file.empty())return result;
        if(asset.kind==AssetKind::Model&&file.filename()=="manifest.json") {
            // The installed managed format retains source.glb/source.gltf and
            // a normalized model.gltf. Respect the manifest's original first.
            const auto package=file.parent_path();std::filesystem::path original;
            if(const auto bytes=std::filesystem::file_size(file,error);!error&&bytes<=1024*1024) {
                std::ifstream input(file,std::ios::binary);
                const auto manifest=nlohmann::json::parse(input,nullptr,false);
                if(manifest.is_object()&&manifest.contains("original")&&manifest["original"].is_string()) {
                    const auto member=pathFromUtf8(manifest["original"].get<std::string>());
                    if(!member.is_absolute()&&!member.has_root_path()) {
                        const auto candidate=checkedFile(root,package/member);
                        if(!candidate.empty()&&contained(package,candidate))original=candidate;
                    }
                }
            }
            if(original.empty())for(const auto* candidate:{"source.glb","source.gltf","model.gltf"}) {
                original=checkedFile(root,package/candidate);if(!original.empty())break;
            }
            if(!original.empty())file=std::move(original);
        }
        result.file=std::move(file);result.directory=result.file.parent_path();result.available=true;
        if(result.name.empty())result.name=utf8(result.file.stem());
    } catch(const std::exception&) {
        result.file.clear();result.directory.clear();result.available=false;
    }
    return result;
}
bool openBrowserAssetLocation(const BrowserAssetLocation& location,std::string& diagnostic) {
    diagnostic.clear();std::error_code error;
    if(!location.available||!std::filesystem::is_regular_file(location.file,error)||error) {
        diagnostic="The asset file is unavailable.";return false;
    }
#ifdef _WIN32
    // Pass an argument directly to Explorer, never through cmd/PowerShell. A
    // quoted path keeps spaces, Unicode and commas inside the selected filename.
    const auto parameters=L"/select,\""+location.file.native()+L"\"";
    const auto opened=ShellExecuteW(nullptr,L"open",L"explorer.exe",parameters.c_str(),location.directory.c_str(),SW_SHOWNORMAL);
    if(reinterpret_cast<INT_PTR>(opened)>32)return true;
    diagnostic="Windows Explorer could not open the asset location.";
#else
    diagnostic="Opening an asset location requires Windows Explorer.";
#endif
    return false;
}
} // namespace crimson
