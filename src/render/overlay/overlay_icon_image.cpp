#include "render/overlay/overlay_icon_image.hpp"
#include "render/viewport_overlay_policy.hpp"
#include <algorithm>
#include <cmath>

namespace quader {
namespace {
// The shared viewport scale is sanitized to at most 15x. Keep one native texel
// available per displayed pixel at that upper bound, then let Filament's mip chain
// handle every smaller presentation without depending on a fixed-DPI asset.
constexpr std::uint32_t kPointLightIconTextureExtent = 1024u;
static_assert(static_cast<float>(kPointLightIconTextureExtent) >=
              kOverlayPointLightGizmoSizePixels *
                  kViewportPixelScaleMaximum);

struct IconColor {
  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;
  float alpha = 0.0f;
};

float unit_clamp(float value) { return std::clamp(value, 0.0f, 1.0f); }

float antialiased_coverage(float signed_distance) {
  constexpr float kPixelWidth =
      2.0f / static_cast<float>(kPointLightIconTextureExtent);
  return unit_clamp(0.5f - signed_distance / kPixelWidth);
}

float rounded_box_signed_distance(float x, float y, float center_x,
                                  float center_y, float half_width,
                                  float half_height, float radius) {
  const float qx = std::abs(x - center_x) - half_width + radius;
  const float qy = std::abs(y - center_y) - half_height + radius;
  const float outside_x = std::max(qx, 0.0f);
  const float outside_y = std::max(qy, 0.0f);
  return std::hypot(outside_x, outside_y) +
         std::min(std::max(qx, qy), 0.0f) - radius;
}

float bulb_half_width(float y) {
  constexpr float kBulbCenterY = -0.31f;
  constexpr float kBulbRadius = 0.54f;
  const float circle_y = y - kBulbCenterY;
  const float circle_width =
      std::sqrt(std::max(kBulbRadius * kBulbRadius - circle_y * circle_y,
                         0.0f));

  float taper_width = 0.0f;
  if (y >= 0.03f && y <= 0.42f) {
    const float taper = (y - 0.03f) / (0.42f - 0.03f);
    taper_width = 0.40f + (0.21f - 0.40f) * taper;
  }
  return std::max(circle_width, taper_width);
}

float bulb_signed_distance(float x, float y) {
  constexpr float kTop = -0.85f;
  constexpr float kBottom = 0.42f;
  const float horizontal = std::abs(x) - bulb_half_width(y);
  const float vertical = std::max(kTop - y, y - kBottom);
  return std::max(horizontal, vertical);
}

void composite_icon_color(IconColor &destination, IconColor source,
                          float coverage) {
  const float source_alpha = unit_clamp(source.alpha * coverage);
  const float result_alpha =
      source_alpha + destination.alpha * (1.0f - source_alpha);
  if (result_alpha <= 0.0f) {
    return;
  }
  const float retained = destination.alpha * (1.0f - source_alpha);
  destination.red =
      (source.red * source_alpha + destination.red * retained) / result_alpha;
  destination.green = (source.green * source_alpha +
                       destination.green * retained) /
                      result_alpha;
  destination.blue = (source.blue * source_alpha +
                      destination.blue * retained) /
                     result_alpha;
  destination.alpha = result_alpha;
}

std::byte icon_channel_byte(float value) {
  return static_cast<std::byte>(
      static_cast<std::uint8_t>(std::lround(unit_clamp(value) * 255.0f)));
}

std::vector<std::byte> make_point_light_icon_rgba() {
  constexpr std::size_t kChannelCount = 4u;
  const std::size_t pixel_count =
      static_cast<std::size_t>(kPointLightIconTextureExtent) *
      kPointLightIconTextureExtent;
  std::vector<std::byte> pixels(pixel_count * kChannelCount);

  for (std::uint32_t row = 0u; row < kPointLightIconTextureExtent; ++row) {
    const float y =
        (static_cast<float>(row) + 0.5f) * 2.0f /
            static_cast<float>(kPointLightIconTextureExtent) -
        1.0f;
    for (std::uint32_t column = 0u; column < kPointLightIconTextureExtent;
         ++column) {
      const float x =
          (static_cast<float>(column) + 0.5f) * 2.0f /
              static_cast<float>(kPointLightIconTextureExtent) -
          1.0f;
      IconColor color;

      const float body_distance = bulb_signed_distance(x, y);
      composite_icon_color(color, {0.10f, 0.07f, 0.025f, 1.0f},
                           antialiased_coverage(body_distance - 0.025f));

      const float body_coverage = antialiased_coverage(body_distance);
      const float edge =
          unit_clamp(std::abs(x) / std::max(bulb_half_width(y), 0.001f));
      const float lower_warmth = unit_clamp((y + 0.15f) / 0.57f);
      const IconColor bulb_color{
          1.0f,
          0.86f - edge * 0.20f - lower_warmth * 0.08f,
          0.12f - edge * 0.08f,
          1.0f,
      };
      composite_icon_color(color, bulb_color, body_coverage);

      const float highlight_x = (x + 0.20f) / 0.18f;
      const float highlight_y = (y + 0.48f) / 0.25f;
      const float highlight = unit_clamp(
          1.0f - std::hypot(highlight_x, highlight_y));
      composite_icon_color(color, {1.0f, 1.0f, 0.88f, 0.72f},
                           highlight * body_coverage);

      const float base_distance =
          rounded_box_signed_distance(x, y, 0.0f, 0.60f, 0.245f, 0.25f,
                                      0.075f);
      composite_icon_color(color, {0.055f, 0.05f, 0.045f, 1.0f},
                           antialiased_coverage(base_distance - 0.018f));
      composite_icon_color(color, {0.15f, 0.145f, 0.14f, 1.0f},
                           antialiased_coverage(base_distance));

      constexpr float kBandCenters[] = {0.43f, 0.56f, 0.69f};
      for (float band_center : kBandCenters) {
        const float band_distance = rounded_box_signed_distance(
            x, y, 0.0f, band_center, 0.215f, 0.052f, 0.032f);
        composite_icon_color(color, {0.035f, 0.03f, 0.028f, 1.0f},
                             antialiased_coverage(band_distance - 0.012f));
        const float band_luminance =
            0.72f + 0.20f * unit_clamp(1.0f - std::abs(x) / 0.215f);
        composite_icon_color(
            color,
            {band_luminance, band_luminance, band_luminance * 0.96f, 1.0f},
            antialiased_coverage(band_distance));
      }

      const std::size_t offset =
          (static_cast<std::size_t>(row) * kPointLightIconTextureExtent +
           column) *
          kChannelCount;
      pixels[offset] = icon_channel_byte(color.red);
      pixels[offset + 1u] = icon_channel_byte(color.green);
      pixels[offset + 2u] = icon_channel_byte(color.blue);
      pixels[offset + 3u] = icon_channel_byte(color.alpha);
    }
  }
  return pixels;
}

}
const OverlayIconImage& overlay_icon_image(ViewportOverlayIconKind kind) {
    static const OverlayIconImage point{kPointLightIconTextureExtent,kPointLightIconTextureExtent,
        std::make_shared<const std::vector<std::byte>>(make_point_light_icon_rgba())};
    static const OverlayIconImage absent;
    return kind==ViewportOverlayIconKind::PointLight?point:absent;
}
}
