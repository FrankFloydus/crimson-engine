#pragma once
#include <functional>
#include <string>
struct ImGuiTextFilter;
namespace crimson {
class AssetLibrary;
// Shared regular-image choices and strict drag delivery for texture documents.
void drawTextureAssetChoices(AssetLibrary&,ImGuiTextFilter&,const std::string& selected,
    const std::function<void(const std::string&)>& assign);
bool drawTextureAssetField(AssetLibrary&,const char* label,std::string& id,std::string& diagnostic,bool normal=false);
}
