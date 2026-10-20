#include "crimson-renderer/ShaderPreflight.h"
#include "vs_model.h"
#include "vs_model_instanced.h"
#include "vs_pick_instanced.h"
#include "fs_pick_instanced.h"
#include "vs_point_shadow_instanced.h"
#include "vs_directional_shadow_instanced.h"
#include "fs_model.h"
#include "vs_pick.h"
#include "fs_pick.h"
#include "vs_present.h"
#include "fs_present.h"
#include "cs_glow_bloom_downsample.h"
#include "cs_glow_bloom_upsample.h"
#include "vs_smaa.h"
#include "fs_smaa_edges.h"
#include "fs_smaa_weights.h"
#include "fs_smaa_blend.h"
#include "vs_grid.h"
#include "fs_grid.h"
#include "vs_collision_overlay.h"
#include "fs_collision_overlay.h"
#include "fs_encode.h"
#include "vs_imgui.h"
#include "fs_imgui.h"
#include "vs_particle.h"
#include "fs_particle.h"
#include "fs_sky.h"
#include "vs_point_shadow.h"
#include "fs_point_shadow.h"
#include "vs_directional_shadow.h"
#include "fs_directional_shadow.h"
#include "cs_procedural_filter.h"
#include "cs_model_deform.h"
#include "cs_ssgi_trace.h"
#include "cs_ssgi_temporal.h"
#include "cs_ssgi_filter.h"
#include "cs_ssgi_publish.h"
#include "cs_ssao_evaluate.h"
#include "cs_ssao_publish.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        const auto check=[](bool success,const std::string& message){if (!success) throw std::runtime_error(message);}; std::string diagnostic;
        const auto pair=[&](auto& vertex,auto& fragment){check(crimson::preflightShaderPair(vertex,fragment,vertex,fragment,diagnostic),diagnostic);};
        pair(cc_vs_model,cc_fs_model); pair(cc_vs_pick,cc_fs_pick); pair(cc_vs_present,cc_fs_present); pair(cc_vs_grid,cc_fs_grid); pair(cc_vs_collision_overlay,cc_fs_collision_overlay); pair(cc_vs_present,cc_fs_encode); pair(cc_vs_imgui,cc_fs_imgui);
        pair(cc_vs_particle,cc_fs_particle);
        pair(cc_vs_smaa,cc_fs_smaa_edges);pair(cc_vs_smaa,cc_fs_smaa_weights);pair(cc_vs_smaa,cc_fs_smaa_blend);
        check(!crimson::preflightShaderPair(cc_vs_smaa,cc_fs_smaa_blend,cc_vs_smaa,cc_fs_smaa_edges,diagnostic),
            "SMAA neighborhood blending must not replace the edge-detection interface.");
        pair(cc_vs_model_instanced,cc_fs_model);pair(cc_vs_pick_instanced,cc_fs_pick_instanced);
        pair(cc_vs_point_shadow_instanced,cc_fs_point_shadow);pair(cc_vs_directional_shadow_instanced,cc_fs_directional_shadow);
        check(!crimson::preflightShaderPair(cc_vs_model_instanced,cc_fs_model,cc_vs_model,cc_fs_model,diagnostic),"Instance-token vertex inputs must not replace the per-object model contract.");
        check(!crimson::preflightShaderPair(cc_vs_pick_instanced,cc_fs_pick_instanced,cc_vs_pick,cc_fs_pick,diagnostic),"Per-instance identity must retain its own picking interface.");
        pair(cc_vs_present,cc_fs_sky);
        pair(cc_vs_point_shadow,cc_fs_point_shadow);
        pair(cc_vs_directional_shadow,cc_fs_directional_shadow);
        check(crimson::preflightComputeShader(cc_cs_glow_bloom_downsample,cc_cs_glow_bloom_downsample,diagnostic),diagnostic);
        check(crimson::preflightComputeShader(cc_cs_glow_bloom_upsample,cc_cs_glow_bloom_upsample,diagnostic),diagnostic);
        check(!crimson::preflightComputeShader(cc_cs_glow_bloom_downsample,cc_cs_glow_bloom_upsample,diagnostic),"Glare downsampling must retain its distinct reconstruction interface.");
        check(!crimson::preflightShaderPair(cc_vs_pick,cc_fs_pick,cc_vs_point_shadow,cc_fs_point_shadow,diagnostic),"Picking must not replace the shadow caster contract.");
        std::vector<std::uint8_t> truncated(std::begin(cc_fs_model),std::end(cc_fs_model)); truncated.resize(20);
        check(!crimson::preflightShaderPair(cc_vs_model,truncated,cc_vs_model,cc_fs_model,diagnostic),"Truncated shader should fail safely.");
        check(!crimson::preflightShaderPair(cc_vs_model,cc_fs_imgui,cc_vs_model,cc_fs_model,diagnostic),"Changed shader interface should fail before GPU creation.");
        check(!crimson::preflightShaderPair(cc_vs_particle,cc_fs_particle,cc_vs_model,cc_fs_model,diagnostic),"Instanced particle inputs must not replace the model program contract.");
        auto wrong=std::vector<std::uint8_t>(std::begin(cc_vs_model),std::end(cc_vs_model)); wrong[0]='F';
        check(!crimson::preflightShaderPair(wrong,cc_fs_model,cc_vs_model,cc_fs_model,diagnostic),"Wrong shader stage should fail safely.");
        check(crimson::preflightComputeShader(cc_cs_procedural_filter,cc_cs_procedural_filter,diagnostic),diagnostic);
        check(crimson::preflightComputeShader(cc_cs_model_deform,cc_cs_model_deform,diagnostic),diagnostic);
        check(crimson::preflightComputeShader(cc_cs_ssgi_trace,cc_cs_ssgi_trace,diagnostic),diagnostic);
        check(crimson::preflightComputeShader(cc_cs_ssgi_temporal,cc_cs_ssgi_temporal,diagnostic),diagnostic);
        check(crimson::preflightComputeShader(cc_cs_ssgi_filter,cc_cs_ssgi_filter,diagnostic),diagnostic);
        check(crimson::preflightComputeShader(cc_cs_ssgi_publish,cc_cs_ssgi_publish,diagnostic),diagnostic);
        check(crimson::preflightComputeShader(cc_cs_ssao_evaluate,cc_cs_ssao_evaluate,diagnostic),diagnostic);
        check(crimson::preflightComputeShader(cc_cs_ssao_publish,cc_cs_ssao_publish,diagnostic),diagnostic);
        check(!crimson::preflightComputeShader(cc_cs_ssao_publish,cc_cs_ssao_evaluate,diagnostic),"SSAO publication must retain its independent receiver/atlas contract.");
        check(!crimson::preflightComputeShader(cc_cs_ssgi_publish,cc_cs_ssgi_trace,diagnostic),"GI publication must retain its separate atlas-write contract.");
        check(!crimson::preflightComputeShader(cc_cs_model_deform,cc_cs_procedural_filter,diagnostic),"Deformation compute must retain its independent buffer/uniform contract.");
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
