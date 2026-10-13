#include "crimson-renderer/ShaderPreflight.h"
#include "vs_model.h"
#include "fs_model.h"
#include "vs_pick.h"
#include "fs_pick.h"
#include "vs_present.h"
#include "fs_present.h"
#include "vs_grid.h"
#include "fs_grid.h"
#include "fs_encode.h"
#include "vs_imgui.h"
#include "fs_imgui.h"
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        const auto check=[](bool success,const std::string& message){if (!success) throw std::runtime_error(message);}; std::string diagnostic;
        const auto pair=[&](auto& vertex,auto& fragment){check(crimson::preflightShaderPair(vertex,fragment,vertex,fragment,diagnostic),diagnostic);};
        pair(cc_vs_model,cc_fs_model); pair(cc_vs_pick,cc_fs_pick); pair(cc_vs_present,cc_fs_present); pair(cc_vs_grid,cc_fs_grid); pair(cc_vs_present,cc_fs_encode); pair(cc_vs_imgui,cc_fs_imgui);
        std::vector<std::uint8_t> truncated(std::begin(cc_fs_model),std::end(cc_fs_model)); truncated.resize(20);
        check(!crimson::preflightShaderPair(cc_vs_model,truncated,cc_vs_model,cc_fs_model,diagnostic),"Truncated shader should fail safely.");
        check(!crimson::preflightShaderPair(cc_vs_model,cc_fs_imgui,cc_vs_model,cc_fs_model,diagnostic),"Changed shader interface should fail before GPU creation.");
        auto wrong=std::vector<std::uint8_t>(std::begin(cc_vs_model),std::end(cc_vs_model)); wrong[0]='F';
        check(!crimson::preflightShaderPair(wrong,cc_fs_model,cc_vs_model,cc_fs_model,diagnostic),"Wrong shader stage should fail safely.");
        std::cout<<"Shader container/reflection/interface checks passed.\n"; return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
