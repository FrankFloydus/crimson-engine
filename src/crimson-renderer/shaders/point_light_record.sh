#ifndef CC_POINT_LIGHT_RECORD_SH
#define CC_POINT_LIGHT_RECORD_SH
// The CPU packs this layout and publishes its stride through the reflected
// u_clusterRecordLayoutV6 binding used by compute and material record indexing.
// Position/intensity, color/range, cull sphere, shadow/source-radius/roles/reveal,
// then near-radius squared/specular scale/range exponent/edge softness,
// and distance attenuation exponent / GI energy minus one / reserved ZW.
// Default GI energy one therefore preserves the former zero payload.
#define CC_POINT_LIGHT_RECORD_TEXELS 6
#endif
