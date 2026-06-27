////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader_poly::document_internal {

inline constexpr float epsilon = 0.000001F;
inline constexpr float face_area_score_epsilon = epsilon * epsilon;
inline constexpr float generated_uv_map_units_per_world_unit = 32.0F;
inline constexpr float generated_uv_tile_size_map_units = 64.0F;
inline constexpr float generated_uv_tiles_per_world_unit = generated_uv_map_units_per_world_unit / generated_uv_tile_size_map_units;
inline constexpr float pi = 3.14159265358979323846F;
inline constexpr float tau = pi * 2.0F;
inline constexpr float min_primitive_dimension = 0.001F;
inline constexpr float max_primitive_dimension = 10000.0F;
inline constexpr float hull_distance_epsilon = 0.001F;
inline constexpr float hull_normal_epsilon = 0.000001F;
inline constexpr float edge_bevel_min_profile = 0.0F;
inline constexpr float edge_bevel_max_profile = 1.0F;
inline constexpr float edge_bevel_square_profile_threshold = 0.950F;
inline constexpr float edge_bevel_square_exponent = 10000.0F;
inline constexpr float edge_bevel_circle_exponent = 2.0F;
inline constexpr float edge_bevel_line_exponent = 1.0F;
inline constexpr float edge_bevel_square_in_exponent = 0.0F;
inline constexpr float edge_bevel_exponent_epsilon = 0.0001F;

} // namespace quader_poly::document_internal
