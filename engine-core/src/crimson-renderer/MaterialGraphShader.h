#pragma once
#include <crimson/renderer/forward/MaterialGraphShader.h>
#include "resources/ShaderCompiler.h"
namespace crimson {
MaterialGraphShader generateMaterialGraphShader(const MaterialAsset&,const MaterialGraphSourceSet& = {});
MaterialGraphShader generateMaterialGraphPreviewShader(const MaterialAsset&,MaterialGraphPreviewTarget,const MaterialGraphSourceSet& = {});
MaterialGraphSourceSet resolveMaterialSources(const MaterialAsset&,
    const std::function<std::shared_ptr<const MaterialAsset>(const std::string&)>&);
ShaderCompileRecipe materialGraphShaderRecipe(const ShaderCompileConfig&,const MaterialGraphShader&,const MaterialGraphShaderVariant&);
ShaderCompileRecipe materialGraphDomainShaderRecipe(const ShaderCompileConfig&,const MaterialGraphShader&,const MaterialGraphShaderVariant&);
}
