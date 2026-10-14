uniform vec4 u_materialUvScale;
uniform vec4 u_materialUvOffset;
uniform vec4 u_materialUvRotation;
#include "material_mapping_functions.sh"
vec3 mappingNormalize(vec3 value, vec3 fallback) { return ccMappingNormalize(value, fallback); }
vec3 mappingRotate(vec3 value) { return ccMappingRotate(value, u_materialUvRotation); }
vec3 mappingInverseRotate(vec3 value) { return ccMappingInverseRotate(value, u_materialUvRotation); }
vec3 mappingScaleSigns() { return ccMappingScaleSigns(u_materialUvScale); }
vec2 materialUv(vec2 uv) { return ccMaterialUv(uv, u_materialUvScale, u_materialUvOffset, u_materialUvRotation); }
vec3 materialMappingPosition(vec3 position) { return ccMappingPosition(position, u_materialUvScale, u_materialUvOffset, u_materialUvRotation); }
vec3 materialMappingNormal(vec3 normal) { return ccMappingNormal(normal, u_materialUvScale, u_materialUvRotation); }
vec3 materialMappingWeights(vec3 normal) { return ccMappingWeights(normal); }
vec4 materialSample(sampler2D image, vec2 uv, vec3 position, vec3 normal)
{ return ccMaterialSample(image, uv, position, normal, u_materialUvScale, u_materialUvOffset, u_materialUvRotation); }
vec3 materialDecodeNormal(vec3 color, float strength) { return ccDecodeNormal(color, strength); }
vec3 materialUvNormal(vec3 normal) { return ccUvNormal(normal, u_materialUvScale, u_materialUvRotation); }
vec3 materialTriplanarNormal(sampler2D image, vec3 position, vec3 normal, float strength)
{ return ccTriplanarNormal(image, position, normal, strength, u_materialUvScale, u_materialUvOffset, u_materialUvRotation); }
