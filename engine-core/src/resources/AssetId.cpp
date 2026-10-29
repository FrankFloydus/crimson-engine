#include "AssetId.h"
#include <stduuid/uuid.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

namespace crimson {
namespace {
const uuids::uuid& assetNamespace() {
    static const auto id=uuids::uuid::from_string("8b206058-7512-4b89-a776-e89b2d921e62").value();return id;
}
std::string named(const uuids::uuid& ns,std::string_view value){return uuids::to_string(uuids::uuid_name_generator{ns}(std::string(value)));}
}
bool isAssetUuid(std::string_view value) {
    const auto parsed=uuids::uuid::from_string(value);
    return value.size()==36&&parsed&&!parsed->is_nil()&&parsed->variant()==uuids::uuid_variant::rfc;
}
std::string newAssetId(){return uuids::to_string(uuids::uuid_system_generator{}());}
std::string derivedAssetId(std::string_view owner,std::string_view role) {
    const auto id=uuids::uuid::from_string(canonicalAssetId(owner));
    if(!id||id->is_nil())throw std::runtime_error("Derived asset owner must have a UUID.");
    return named(*id,role);
}
std::string canonicalAssetId(std::string_view value) {
    if(value.empty())return {};
    if(isAssetUuid(value))return uuids::to_string(*uuids::uuid::from_string(value));
    // Old imported sidecars and image slots become children of the same model
    // UUID as new imports. This retains sidecar ownership across conversion.
    constexpr std::string_view models="assets/models/",materials="assets/materials/";
    if(value.starts_with(models)) {
        const auto slash=value.find('/',models.size());
        if(slash!=std::string_view::npos) {
            const auto member=value.substr(slash+1);
            if(member.starts_with("images/"))return derivedAssetId(named(assetNamespace(),std::string(value.substr(0,slash))+"/manifest.json"),"image:"+std::string(member));
        }
    }
    if(value.starts_with(materials)&&value.ends_with(".crimson-material")) {
        const auto token=value.substr(materials.size(),value.size()-materials.size()-17);const auto suffix=token.rfind("-m");
        if(suffix!=std::string_view::npos&&suffix+2<token.size()&&std::all_of(token.begin()+suffix+2,token.end(),[](unsigned char c){return std::isdigit(c)!=0;}))
            return derivedAssetId(named(assetNamespace(),"assets/models/"+std::string(token.substr(0,suffix))+"/manifest.json"),"material:"+std::string(token.substr(suffix+2)));
    }
    return named(assetNamespace(),value);
}
bool isEngineAssetId(std::string_view value) {
    static const std::array<std::string,16> protectedIds{
        canonicalAssetId("builtin:gameplay-effect/player-defaults"),canonicalAssetId("builtin:gameplay-effect/enemy-defaults"),
        canonicalAssetId("builtin:material/default"),canonicalAssetId("builtin:material/particle"),canonicalAssetId("builtin:navigation-agent/default"),
        canonicalAssetId("builtin:texture/gray"),canonicalAssetId("builtin:texture/normal"),
        canonicalAssetId("builtin:environment/studio"),canonicalAssetId("builtin:mesh-box-v1"),canonicalAssetId("builtin:mesh-sphere-v1"),
        canonicalAssetId("builtin:mesh-cylinder-v1"),
        canonicalAssetId("builtin:material-preview-cube-v1"),canonicalAssetId("builtin:material-preview-rounded-cube-v1"),
        canonicalAssetId("builtin:material-preview-cylinder-v1"),canonicalAssetId("builtin:material-preview-plane-v1"),
        canonicalAssetId("builtin:material-preview-sphere-v1")};
    return value.starts_with("builtin:")||std::find(protectedIds.begin(),protectedIds.end(),value)!=protectedIds.end();
}
} // namespace crimson
