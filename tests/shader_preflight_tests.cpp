#include "crimson-renderer/ShaderPreflight.h"
#include "vs_model.h"
#include "fs_model.h"
#include "vs_pick.h"
#include "fs_pick.h"
#include "vs_present.h"
#include "fs_present.h"
#include "fs_glow_extract.h"
#include "fs_glow_blur.h"
#include "vs_grid.h"
#include "fs_grid.h"
#include "fs_encode.h"
#include "vs_imgui.h"
#include "fs_imgui.h"
#include "vs_particle.h"
#include "fs_particle.h"
#include "fs_sky.h"
#include "vs_point_shadow.h"
#include "fs_point_shadow.h"
#include "cs_procedural_filter.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        const auto check=[](bool success,const std::string& message){if (!success) throw std::runtime_error(message);}; std::string diagnostic;
        const auto pair=[&](auto& vertex,auto& fragment){check(crimson::preflightShaderPair(vertex,fragment,vertex,fragment,diagnostic),diagnostic);};
        pair(cc_vs_model,cc_fs_model); pair(cc_vs_pick,cc_fs_pick); pair(cc_vs_present,cc_fs_present); pair(cc_vs_grid,cc_fs_grid); pair(cc_vs_present,cc_fs_encode); pair(cc_vs_imgui,cc_fs_imgui);
        pair(cc_vs_particle,cc_fs_particle);
        pair(cc_vs_present,cc_fs_sky);
        pair(cc_vs_point_shadow,cc_fs_point_shadow);
        pair(cc_vs_present,cc_fs_glow_extract);pair(cc_vs_present,cc_fs_glow_blur);
        check(!crimson::preflightShaderPair(cc_vs_present,cc_fs_glow_extract,cc_vs_present,cc_fs_present,diagnostic),"Glow extraction must not replace the tone-map sampler contract.");
        check(!crimson::preflightShaderPair(cc_vs_pick,cc_fs_pick,cc_vs_point_shadow,cc_fs_point_shadow,diagnostic),"Picking must not replace the shadow caster contract.");
        std::vector<std::uint8_t> truncated(std::begin(cc_fs_model),std::end(cc_fs_model)); truncated.resize(20);
        check(!crimson::preflightShaderPair(cc_vs_model,truncated,cc_vs_model,cc_fs_model,diagnostic),"Truncated shader should fail safely.");
        check(!crimson::preflightShaderPair(cc_vs_model,cc_fs_imgui,cc_vs_model,cc_fs_model,diagnostic),"Changed shader interface should fail before GPU creation.");
        check(!crimson::preflightShaderPair(cc_vs_particle,cc_fs_particle,cc_vs_model,cc_fs_model,diagnostic),"Instanced particle inputs must not replace the model program contract.");
        auto wrong=std::vector<std::uint8_t>(std::begin(cc_vs_model),std::end(cc_vs_model)); wrong[0]='F';
        check(!crimson::preflightShaderPair(wrong,cc_fs_model,cc_vs_model,cc_fs_model,diagnostic),"Wrong shader stage should fail safely.");
        check(crimson::preflightComputeShader(cc_cs_procedural_filter,cc_cs_procedural_filter,diagnostic),diagnostic);
        std::vector<std::uint8_t> compute(std::begin(cc_cs_procedural_filter),std::end(cc_cs_procedural_filter));
        auto cutCompute=compute;cutCompute.pop_back();
        check(!crimson::preflightComputeShader(cutCompute,cc_cs_procedural_filter,diagnostic),"Truncated compute trailer should fail safely.");
        auto wrongCompute=compute;wrongCompute[0]='F';
        check(!crimson::preflightComputeShader(wrongCompute,cc_cs_procedural_filter,diagnostic),"Wrong compute envelope stage was accepted.");
        auto forgedCompute=std::vector<std::uint8_t>(std::begin(cc_fs_sky),std::end(cc_fs_sky));forgedCompute[0]='C';
        check(!crimson::preflightComputeShader(forgedCompute,cc_cs_procedural_filter,diagnostic),"Pixel bytecode disguised as compute was accepted.");
        const std::string uniform="u_proceduralSky";
        const auto name=std::search(compute.begin(),compute.end(),uniform.begin(),uniform.end());
        check(name!=compute.end(),"Compute fixture is missing its shared sky uniforms.");
        *name='q';
        check(!crimson::preflightComputeShader(compute,cc_cs_procedural_filter,diagnostic),"Changed compute uniform name was accepted.");
        std::cout<<"Shader container/reflection/interface checks passed.\n"; return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
