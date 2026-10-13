#include "editor/AssetPreviews.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        crimson::PreviewDependencies sources;
        sources.files={"assets/textures/albedo.png","assets/textures/normal.png"};
        sources.sourceFingerprints={11,22};
        require(sources.sourceMatches("assets/textures/albedo.png",11),"Current texture source was rejected.");
        require(sources.sourceMatches("assets/textures/folder/../normal.png",22),"Normalized source identity was rejected.");
        require(!sources.sourceMatches("assets/textures/albedo.png",10),"Old GPU pixels matched a newly hashed source.");
        require(!sources.sourceMatches("assets/textures/missing.png",11),"Missing source matched another image's fingerprint.");
        require(!sources.sourceMatches("assets/textures/albedo.png",0),"An unpublished GPU source was accepted.");
        sources.sourceFingerprints.pop_back();
        require(!sources.sourceMatches("assets/textures/albedo.png",11),"Incomplete source metadata was accepted.");
        std::cout << "Preview source coherence passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
