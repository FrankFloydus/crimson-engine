$input v_worldPosition

#include <bgfx_shader.sh>
#include "grid_depth.sh"

// Port of the user's Quader resources/materials/ground_grid.mat perspective helpers.
// Coverage integration and hierarchy are retained; only the renderer bindings change.
// The fixed XZ construction plane has no distance or perimeter opacity fade.
uniform vec4 u_gridMinor;
uniform vec4 u_gridMajor;
uniform vec4 u_gridAxisU;
uniform vec4 u_gridAxisV;
uniform vec4 u_gridParams;
uniform vec4 u_gridWidths;
    struct GridPixelFootprint {
        float inverse_gradient;
        float long_interval;
        float short_interval;
    };

    GridPixelFootprint grid_pixel_footprint(vec2 coordinate_derivatives) {
        GridPixelFootprint footprint;
        float gradient = length(coordinate_derivatives);
        // A constant coordinate has no resolvable line family. This floor is many
        // orders below meaningful world-units-per-pixel for the fixed grid plane.
        footprint.inverse_gradient = gradient > 1.0e-12 ? 1.0 / gradient : 0.0;
        vec2 intervals = abs(coordinate_derivatives) * footprint.inverse_gradient;
        footprint.long_interval = max(intervals.x, intervals.y);
        footprint.short_interval = min(intervals.x, intervals.y);
        return footprint;
    }

    float grid_pixel_cdf(float coordinate, GridPixelFootprint footprint) {
        float long_interval = footprint.long_interval;
        float short_interval = footprint.short_interval;
        // The vanishing-interval limit is a uniform distribution. Taking that limit
        // below 0.0001 pixel avoids division by a tiny interval; the maximum omitted
        // corner extent is below 0.00005 pixel.
        if (short_interval < 1.0e-4) {
            return clamp(coordinate / long_interval + 0.5, 0.0, 1.0);
        }
        float support_half_width = 0.5 * (long_interval + short_interval);
        float plateau_half_width = 0.5 * (long_interval - short_interval);
        if (coordinate <= -support_half_width) {
            return 0.0;
        }
        if (coordinate >= support_half_width) {
            return 1.0;
        }
        if (coordinate < -plateau_half_width) {
            float corner = coordinate + support_half_width;
            return corner * corner / (2.0 * long_interval * short_interval);
        }
        if (coordinate > plateau_half_width) {
            float corner = support_half_width - coordinate;
            return 1.0 - corner * corner / (2.0 * long_interval * short_interval);
        }
        return 0.5 + coordinate / long_interval;
    }

    float grid_band_coverage(
            float distance_pixels,
            float width_pixels,
            GridPixelFootprint footprint) {
        if (width_pixels <= 0.0 || footprint.inverse_gradient == 0.0) {
            return 0.0;
        }
        float half_width = 0.5 * width_pixels;
        return clamp(
            grid_pixel_cdf(half_width - distance_pixels, footprint)
                - grid_pixel_cdf(-half_width - distance_pixels, footprint),
            0.0, 1.0);
    }

    // Periodic primitives of (stripe coverage - mean coverage). Reducing phase
    // first keeps their values bounded even when a pixel spans many stripes.
    float grid_periodic_first_primitive(
            float coordinate, float period, float duty) {
        float phase = fract(coordinate / period + 0.5) - 0.5;
        return period * (sign(phase) * min(abs(phase), 0.5 * duty) - duty * phase);
    }

    float grid_periodic_second_primitive(
            float coordinate, float period, float duty) {
        float phase = fract(coordinate / period + 0.5) - 0.5;
        float half_band = 0.5 * duty;
        float inside_band = min(abs(phase), half_band);
        return period * period * (0.5 * inside_band * inside_band
            + half_band * max(abs(phase) - half_band, 0.0)
            - 0.5 * duty * phase * phase);
    }

    float grid_periodic_band_coverage(
            float coordinate,
            float spacing,
            float width_pixels,
            GridPixelFootprint footprint) {
        if (width_pixels <= 0.0 || footprint.inverse_gradient == 0.0) {
            return 0.0;
        }
        float period = spacing * footprint.inverse_gradient;
        if (width_pixels >= period) {
            return 1.0;
        }
        // Taking abs before reduction makes reflection symmetry explicit, including
        // negative world coordinates. The absolute phase remains world anchored.
        float phase = fract(abs(coordinate) / spacing);
        float nearest_phase = min(phase, 1.0 - phase);
        float distance_pixels = nearest_phase * period;
        float long_interval = footprint.long_interval;
        float short_interval = footprint.short_interval;
        float pixel_span = long_interval + short_interval;
        // If only one band (or one complementary gap) can intersect the pixel,
        // the isolated CDF is exact and avoids cancellation of large primitives.
        if (period >= width_pixels + pixel_span) {
            return grid_band_coverage(distance_pixels, width_pixels, footprint);
        }
        float gap = period - width_pixels;
        if (period >= gap + pixel_span) {
            return 1.0 - grid_band_coverage(0.5 * period - distance_pixels, gap, footprint);
        }
        float duty = width_pixels / period;
        // Here both the band and gap are smaller than the projected pixel span.
        // Integrate ALL intersecting stripes, retaining their average at distance.
        // The uniform limit omits less than 0.00025 coverage at this threshold.
        if (short_interval < 1.0e-3) {
            return clamp(duty + (
                grid_periodic_first_primitive(distance_pixels + 0.5 * long_interval, period, duty)
                    - grid_periodic_first_primitive(distance_pixels - 0.5 * long_interval, period, duty))
                / long_interval, 0.0, 1.0);
        }
        float outer_offset = 0.5 * pixel_span;
        float inner_offset = 0.5 * (long_interval - short_interval);
        return clamp(duty + (
            grid_periodic_second_primitive(distance_pixels + outer_offset, period, duty)
                - grid_periodic_second_primitive(distance_pixels + inner_offset, period, duty)
                - grid_periodic_second_primitive(distance_pixels - inner_offset, period, duty)
                + grid_periodic_second_primitive(distance_pixels - outer_offset, period, duty))
            / (long_interval * short_interval), 0.0, 1.0);
    }

    vec3 grid_family_coverages(
            float coordinate,
            GridPixelFootprint footprint,
            vec2 spacings,
            vec3 widths) {
        if (footprint.inverse_gradient == 0.0) {
            return vec3_splat(0.0);
        }
        // Keep a line and a gap in each base cell. When cells become smaller
        // than pixels, widen neither minor nor parent stripes beyond half a cell;
        // integrate their remaining area instead. This bounds distant opacity
        // without letting broad parent stripes consume the fine grid's palette.
        widths.xy = min(widths.xy, vec2_splat(0.5 * spacings.x * footprint.inverse_gradient));
        vec3 coverage = vec3_splat(0.0);
        // The hierarchy is nested. Line integration is linear, so removing the
        // coarser subset at the SAME width removes its whole slot, including AA edges.
        // This exclusion is independent of that slot's color opacity or own width.
        coverage.x = max(
            grid_periodic_band_coverage(coordinate, spacings.x, widths.x, footprint)
                - grid_periodic_band_coverage(coordinate, spacings.y, widths.x, footprint),
            0.0);
        float origin_distance_pixels = abs(coordinate) * footprint.inverse_gradient;
        coverage.y = max(
            grid_periodic_band_coverage(coordinate, spacings.y, widths.y, footprint)
                - grid_band_coverage(origin_distance_pixels, widths.y, footprint),
            0.0);
        coverage.z = grid_band_coverage(origin_distance_pixels, widths.z, footprint);
        return coverage;
    }

    vec4 perspective_grid_coverage(
            vec2 coords,
            vec2 derivative_x,
            vec2 derivative_y) {
        float base_spacing = max(u_gridParams.x, 1.0e-6);
        // Two world units are the 64-map-unit parent interval.
        // Integer multiples preserve nesting for any positive selected base spacing;
        // power-of-two spacings give exactly max(base, 2).
        float major_spacing = base_spacing * max(1.0, ceil(2.0 / base_spacing));
        vec2 spacings = vec2(base_spacing, major_spacing);
        vec3 normalized_widths = max(u_gridWidths.xyz, vec3_splat(0.0));
        // Width is measured perpendicular to each screen-space line. The default
        // minor occupies exactly one pixel of integrated area when fully resolved.
        // Slightly broader parent lines/axes remain distinguishable at equal alpha.
        vec3 widths = vec3(
            normalized_widths.x,
            normalized_widths.y,
            normalized_widths.z);
        GridPixelFootprint u_footprint = grid_pixel_footprint(vec2(derivative_x.x, derivative_y.x));
        GridPixelFootprint v_footprint = grid_pixel_footprint(vec2(derivative_x.y, derivative_y.y));
        vec3 u_coverage = grid_family_coverages(coords.x, u_footprint, spacings, widths);
        vec3 v_coverage = grid_family_coverages(coords.y, v_footprint, spacings, widths);
        vec2 tier_coverage = max(u_coverage.xy, v_coverage.xy);
        float total_coverage = clamp(max(
            dot(u_coverage, vec3_splat(1.0)), dot(v_coverage, vec3_splat(1.0))), 0.0, 1.0);

        // U is the axis along world +X (V coordinate zero); V has U coordinate zero.
        float axis_u_coverage = v_coverage.z;
        float axis_v_coverage = u_coverage.z;
        float occupied_coverage = max(axis_u_coverage, axis_v_coverage);
        float axis_u_alpha = axis_u_coverage * clamp(u_gridAxisU.a, 0.0, 1.0);
        float axis_v_alpha = axis_v_coverage * clamp(u_gridAxisV.a, 0.0, 1.0);
        float alpha = max(axis_u_alpha, axis_v_alpha);
        vec3 premultiplied_color = vec3_splat(0.0);
        float axis_alpha_sum = axis_u_alpha + axis_v_alpha;
        if (axis_alpha_sum > 0.0) {
            // Symmetric color mixing at the origin with a maximum, rather than a
            // sum, for opacity. Hiding one axis does not dim the other at the origin.
            premultiplied_color = alpha * (
                u_gridAxisU.rgb * axis_u_alpha
                    + u_gridAxisV.rgb * axis_v_alpha) / axis_alpha_sum;
        }

        // Higher-priority geometric coverage reserves its slot even at zero opacity.
        // Disjoint stripes inside one pixel contribute their combined area. The
        // two directional totals use a bounded maximum to avoid bright crossings.
        // Allocate that total by priority, reserving even transparent hierarchy
        // slots without discarding the remaining lower-tier area at high density.
        float major_weight = min(tier_coverage.y, max(total_coverage - occupied_coverage, 0.0));
        occupied_coverage += major_weight;
        float minor_weight = min(tier_coverage.x, max(total_coverage - occupied_coverage, 0.0));
        float major_alpha = major_weight * clamp(u_gridMajor.a, 0.0, 1.0);
        float minor_alpha = minor_weight * clamp(u_gridMinor.a, 0.0, 1.0);
        premultiplied_color += u_gridMajor.rgb * major_alpha
            + u_gridMinor.rgb * minor_alpha;
        alpha += major_alpha + minor_alpha;
        // The caller owns gridIntensity and the final transparent material blend.
        return alpha > 0.0 ? vec4(premultiplied_color / alpha, alpha) : vec4_splat(0.0);
    }


void main()
{
    vec2 coords = vec2(v_worldPosition.x, -v_worldPosition.z);
    vec4 grid = perspective_grid_coverage(coords, dFdx(coords), dFdy(coords));
    float alpha = clamp(grid.a * u_gridParams.y, 0.0, 1.0);
    if (alpha < 0.002) discard;
    // The large plane crosses the eye at ordinary orbit angles. Reproject the
    // interpolated world position instead of trusting rasterized forward depth
    // after those clipping intersections; retain depth-tested mesh occlusion.
    vec4 clip = mul(u_viewProj, vec4(v_worldPosition, 1.0));
    gl_FragDepth = gridFragmentDepth(clip.z, clip.w, u_gridParams.z, u_gridParams.w);
    // ONE / ONE_MINUS_SRC_ALPHA into the linear post-tone-map target.
    gl_FragColor = vec4(grid.rgb * alpha, alpha);
}
