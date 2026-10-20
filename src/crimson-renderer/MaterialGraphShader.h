#pragma once
#include <crimson/renderer/forward/MaterialGraphShader.h>
#include "resources/ShaderCompiler.h"
namespace crimson {
MaterialGraphShader generateMaterialGraphShader(const MaterialAsset&);
MaterialGraphShader generateMaterialGraphPreviewShader(const MaterialAsset&,MaterialGraphPreviewTarget);
ShaderCompileRecipe materialGraphShaderRecipe(const ShaderCompileConfig&,const MaterialGraphShader&,const MaterialGraphShaderVariant&);
ShaderCompileRecipe materialGraphDomainShaderRecipe(const ShaderCompileConfig&,const MaterialGraphShader&,const MaterialGraphShaderVariant&);
}
