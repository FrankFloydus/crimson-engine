////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#ifndef QUADER_BRIDGE_READER_H
#define QUADER_BRIDGE_READER_H

#include <stdint.h>

#if defined(_WIN32) && defined(QUADER_BRIDGE_READER_EXPORTS)
#define QUADER_BRIDGE_EXPORT __declspec(dllexport)
#elif defined(_WIN32) && defined(QUADER_BRIDGE_READER_IMPORTS)
#define QUADER_BRIDGE_EXPORT __declspec(dllimport)
#else
#define QUADER_BRIDGE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define QUADER_BRIDGE_ABI_VERSION 4u
#define QUADER_BRIDGE_NO_MATERIAL UINT32_MAX
#define QUADER_BRIDGE_NO_SCENE UINT32_MAX
#define QUADER_BRIDGE_TEXTURE_COUNT 4u

typedef struct QuaderBridgeProject QuaderBridgeProject;
typedef struct QuaderBridgeScene QuaderBridgeScene;
typedef struct QuaderBridgeBlob QuaderBridgeBlob;

/* Every string is UTF-8, zero-terminated, and borrowed from its owning handle.
 * Every operation returns 1 on success and 0 on failure. Error may be null.
 * No exceptions, C++ objects, or allocator ownership cross this interface.
 * A handle must not be used concurrently; independent handles may be used on
 * independent threads. Releasing null is supported. */
typedef struct QuaderBridgeError {
  char message[1024];
} QuaderBridgeError;

typedef struct QuaderBridgeProjectView {
  const char *id;
  const char *name;
  uint32_t scene_count;
  uint32_t default_scene_index;
} QuaderBridgeProjectView;

typedef struct QuaderBridgeSceneEntry {
  const char *id;
  const char *relative_path;
} QuaderBridgeSceneEntry;

typedef struct QuaderBridgeSceneView {
  const char *id;
  const char *relative_path;
  uint32_t mesh_count;
  uint32_t material_count;
  uint32_t light_count;
  /* Effective exposure inputs, before renderer clamping. Defaults are 16,
   * 0.008 and 100 when the scene has no camera settings. Disabled camera
   * exposure yields 1, 1.2 and 100, equivalent to an exposure multiplier of 1.
   * Consumers that do not import lights need not interpret these values. */
  float camera_aperture;
  float camera_shutter_seconds;
  float camera_iso;
} QuaderBridgeSceneView;

/* FNV-1a64 of exactly the serialized bytes decoded into the owning handle.
 * These remain stable when the source file is subsequently replaced. */
typedef struct QuaderBridgeSourceFingerprint {
  uint64_t byte_count;
  uint64_t content_hash;
} QuaderBridgeSourceFingerprint;

typedef struct QuaderBridgeVertex {
  float position[3];
  float normal[3];
  /* Face and occurrence mappings are already applied. Apply the material's
   * shared UV transform in the importing application. */
  float uv[2];
} QuaderBridgeVertex;

typedef struct QuaderBridgeSurface {
  uint32_t material_index;
  uint32_t index_offset;
  uint32_t index_count;
} QuaderBridgeSurface;

/* Preserve published uppercase C ABI names for bridge source compatibility. */
// NOLINTBEGIN(readability-identifier-naming)
typedef enum QuaderBridgeCollisionStatus {
  QUADER_BRIDGE_COLLISION_DISABLED = 0,
  QUADER_BRIDGE_COLLISION_TRIMESH_STATIC = 1,
  QUADER_BRIDGE_COLLISION_UNSUPPORTED = 2
} QuaderBridgeCollisionStatus;
// NOLINTEND(readability-identifier-naming)

typedef struct QuaderBridgeCollisionPoint {
  float position[3];
} QuaderBridgeCollisionPoint;

typedef struct QuaderBridgeMeshView {
  const char *name;
  uint64_t display_entity;
  uint64_t geometry_entity;
  uint64_t definition_entity;
  /* Column-major affine world-from-geometry, right-handed Y-up, 1:1 units.
   * Vertices retain source CCW winding; Godot consumers reverse each triangle.
   * A negative determinant is represented by this transform, not vertex data. */
  float transform[16];
  uint32_t visible;
  const QuaderBridgeVertex *vertices;
  uint32_t vertex_count;
  const uint32_t *indices;
  uint32_t index_count;
  const QuaderBridgeSurface *surfaces;
  uint32_t surface_count;
  /* Collision eligibility ignores editor display preferences and visibility.
   * Unsupported saved shape/body combinations have no points. */
  uint32_t collision_status;
  /* Borrowed local CCW triangle triplets, snapped to Godot's 0.0001 grid.
   * Retains rendered triangle multiplicity; empty geometry has zero points.
   * The owning scene keeps these independent of material/UV projection. */
  const QuaderBridgeCollisionPoint *collision_points;
  uint32_t collision_point_count;
} QuaderBridgeMeshView;

/* Preserve published uppercase C ABI names for bridge source compatibility. */
// NOLINTBEGIN(readability-identifier-naming)
typedef enum QuaderBridgeLightType {
  QUADER_BRIDGE_LIGHT_DIRECTIONAL = 0,
  QUADER_BRIDGE_LIGHT_POINT = 1,
  QUADER_BRIDGE_LIGHT_SPOT = 2
} QuaderBridgeLightType;
// NOLINTEND(readability-identifier-naming)

typedef struct QuaderBridgeLightView {
  const char *name;
  /* Namespace this identity with the owning project UUID and scene UUID. */
  uint64_t entity;
  uint32_t type;
  /* Column-major affine world transform, right-handed Y-up, 1:1 units.
   * Directional and spot emission follows the transformed local -Y axis. */
  float transform[16];
  uint32_t visible;
  /* Linear RGB, authored intensity scalar and unscaled world-space range. */
  float color[3];
  float intensity;
  float range;
  float distance_attenuation;
  uint32_t casts_shadows;
} QuaderBridgeLightView;

/* Preserve published uppercase C ABI names for bridge source compatibility. */
// NOLINTBEGIN(readability-identifier-naming)
typedef enum QuaderBridgeMaterialKind {
  QUADER_BRIDGE_MATERIAL_PROJECT = 1,
  QUADER_BRIDGE_MATERIAL_EMBEDDED = 2
} QuaderBridgeMaterialKind;
// NOLINTEND(readability-identifier-naming)

/* Identity is available without reading QMAT or texture source files. IDs are
 * UUIDs for project materials, numeric IDs for scene-embedded materials. */
typedef struct QuaderBridgeMaterialIdentity {
  const char *id;
  uint32_t kind;
} QuaderBridgeMaterialIdentity;

/* Preserve published uppercase C ABI names for bridge source compatibility. */
// NOLINTBEGIN(readability-identifier-naming)
typedef enum QuaderBridgeTextureSlot {
  QUADER_BRIDGE_TEXTURE_ALBEDO = 0,
  QUADER_BRIDGE_TEXTURE_ROUGHNESS = 1,
  QUADER_BRIDGE_TEXTURE_METALLIC = 2,
  QUADER_BRIDGE_TEXTURE_NORMAL = 3
} QuaderBridgeTextureSlot;
// NOLINTEND(readability-identifier-naming)

typedef struct QuaderBridgeTextureInfo {
  uint32_t present;
  const char *id;
  const char *source_path;
  uint32_t srgb;
} QuaderBridgeTextureInfo;

typedef struct QuaderBridgeMaterialView {
  const char *name;
  /* Project-relative QMAT path, or the scene QDR path for embedded materials. */
  const char *source_relative_path;
  /* Linear RGB factors, as consumed by Quader's material renderer. */
  float base_color[3];
  float roughness;
  float metallic;
  float normal_strength;
  uint32_t normal_directx;
  /* UV = R(rotation) * ((uv - center) * scale) + center + offset.
   * Rotation is in degrees, positive counter-clockwise. */
  float uv_center[2];
  float uv_scale[2];
  float uv_offset[2];
  float uv_rotation_degrees;
  QuaderBridgeTextureInfo textures[QUADER_BRIDGE_TEXTURE_COUNT];
} QuaderBridgeMaterialView;

typedef struct QuaderBridgeBlobView {
  const uint8_t *bytes;
  uint64_t byte_count;
  /* Lowercase extension including dot, e.g. ".png". */
  const char *extension;
} QuaderBridgeBlobView;

typedef struct QuaderBridgeApi {
  uint32_t abi_version;
  uint32_t struct_size;
  int (*open_project)(const char *manifest_path, QuaderBridgeProject **out,
                      QuaderBridgeError *error);
  void (*release_project)(QuaderBridgeProject *project);
  int (*project_view)(const QuaderBridgeProject *project,
                      QuaderBridgeProjectView *out, QuaderBridgeError *error);
  int (*scene_entry)(const QuaderBridgeProject *project, uint32_t index,
                     QuaderBridgeSceneEntry *out, QuaderBridgeError *error);
  /* Reopens the manifest and scene and checks both saved identities. The scene
   * owns its storage independently of the project handle. */
  int (*open_scene)(const QuaderBridgeProject *project, const char *scene_id,
                    QuaderBridgeScene **out, QuaderBridgeError *error);
  void (*release_scene)(QuaderBridgeScene *scene);
  int (*scene_view)(const QuaderBridgeScene *scene, QuaderBridgeSceneView *out,
                    QuaderBridgeError *error);
  int (*mesh_view)(const QuaderBridgeScene *scene, uint32_t index,
                   QuaderBridgeMeshView *out, QuaderBridgeError *error);
  int (*material_identity)(const QuaderBridgeScene *scene, uint32_t index,
                           QuaderBridgeMaterialIdentity *out,
                           QuaderBridgeError *error);
  int (*material_view)(QuaderBridgeScene *scene, uint32_t index,
                       QuaderBridgeMaterialView *out, QuaderBridgeError *error);
  int (*read_texture)(QuaderBridgeScene *scene, uint32_t material_index,
                      uint32_t slot, QuaderBridgeBlob **out,
                      QuaderBridgeError *error);
  int (*blob_view)(const QuaderBridgeBlob *blob, QuaderBridgeBlobView *out,
                   QuaderBridgeError *error);
  void (*release_blob)(QuaderBridgeBlob *blob);
  int (*scene_source_fingerprint)(const QuaderBridgeScene *scene,
                                  QuaderBridgeSourceFingerprint *out,
                                  QuaderBridgeError *error);
  /* Lazily decodes the descriptor, without reading textures. Embedded material
   * fingerprints identify the containing scene's decoded bytes. */
  int (*material_source_fingerprint)(QuaderBridgeScene *scene, uint32_t index,
                                     QuaderBridgeSourceFingerprint *out,
                                     QuaderBridgeError *error);
  /* Direct authored lights, including hidden ordinary hierarchy members.
   * Excludes instance definition storage and occurrence leaves. Numerical
   * light validation occurs only when this view is requested. */
  int (*light_view)(const QuaderBridgeScene *scene, uint32_t index,
                    QuaderBridgeLightView *out, QuaderBridgeError *error);
} QuaderBridgeApi;

/* Resolve this one symbol with GetProcAddress. Returns null for other ABIs. */
QUADER_BRIDGE_EXPORT const QuaderBridgeApi *
quader_bridge_get_api(uint32_t requested_version);

#ifdef __cplusplus
}
#endif
#endif
