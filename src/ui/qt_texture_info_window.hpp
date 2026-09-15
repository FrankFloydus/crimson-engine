////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QMainWindow>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

class QSplitter;
class QTableWidget;
class QToolButton;
class QWidget;

namespace quader {

/// Stable owner/source identity carried with one texture information request.
struct TextureInfoContext final {
  std::string project_identity;
  std::uint64_t activation_generation = 0u;
  std::uint64_t dependency_revision = 0u;
  std::string material_identity;
  std::string texture_identity;

  bool operator==(const TextureInfoContext &) const = default;
};

/**
 * Toolkit-neutral immutable texture information supplied to the Qt presenter.
 * Pixel storage only needs to remain alive for the duration of present().
 */
struct TextureInfoPresentation final {
  TextureInfoContext context;
  std::string name;
  std::string location;
  std::string format;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::uintmax_t encoded_byte_size = 0u;
  bool source_has_alpha = false;
  std::span<const std::byte> rgba8;
  std::uint64_t content_revision = 0u;
};

/// Localized visible text used by the reusable texture information window.
struct QtTextureInfoLabels final {
  QString window_title = QStringLiteral("Texture Information");
  QString property = QStringLiteral("Property");
  QString value = QStringLiteral("Value");
  QString name = QStringLiteral("name");
  QString location = QStringLiteral("location");
  QString format = QStringLiteral("format");
  QString size = QStringLiteral("size");
  QString file_size = QStringLiteral("file size");
  QString red_channel = QStringLiteral("Red channel");
  QString green_channel = QStringLiteral("Green channel");
  QString blue_channel = QStringLiteral("Blue channel");
  QString alpha_channel = QStringLiteral("Alpha channel");
};

class QtTextureInfoCanvas;

/**
 * Retained modeless texture information tool. One instance can be retargeted to
 * another project material without recreating its native window or controls.
 */
class QtTextureInfoWindow final : public QMainWindow {
public:
  explicit QtTextureInfoWindow(
      QWidget *parent = nullptr,
      const QtUiScaleMetrics &metrics = QtUiScaleMetrics{});
  ~QtTextureInfoWindow() override;

  QtTextureInfoWindow(const QtTextureInfoWindow &) = delete;
  QtTextureInfoWindow &operator=(const QtTextureInfoWindow &) = delete;

  [[nodiscard]] bool present(const TextureInfoPresentation &presentation);
  void clear();
  void set_labels(QtTextureInfoLabels labels);
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  [[nodiscard]] bool has_presentation() const noexcept {
    return has_presentation_;
  }
  [[nodiscard]] const TextureInfoContext *current_context() const noexcept;
  [[nodiscard]] std::uint64_t masked_image_rebuild_count() const noexcept {
    return masked_image_rebuild_count_;
  }

  [[nodiscard]] static QString
  format_storage_size(std::uintmax_t encoded_byte_size);

private:
  /** Identifies decoded source pixels whose channel-masked image can be reused. */
  struct SourceKey final {
    std::string project_identity;
    std::uint64_t activation_generation = 0u;
    std::string texture_identity;
    std::string fallback_material_identity;
    std::string fallback_location;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint64_t content_revision = 0u;

    bool operator==(const SourceKey &) const = default;
  };

  /** Assigns one bit to each independently visible image channel. */
  enum Channel : std::uint8_t {
    Red = 1u << 0u,
    Green = 1u << 1u,
    Blue = 1u << 2u,
    Alpha = 1u << 3u,
  };

  [[nodiscard]] static SourceKey
  source_key(const TextureInfoPresentation &presentation);
  [[nodiscard]] static bool valid_pixels(
      const TextureInfoPresentation &presentation) noexcept;
  void set_channel_enabled(Channel channel, bool enabled);
  void rebuild_masked_image(bool reset_view);
  void refresh_labels();
  void refresh_table(const TextureInfoPresentation &presentation);
  void refresh_channel_properties();

  QtTextureInfoLabels labels_;
  TextureInfoContext current_context_;
  SourceKey source_key_;
  bool has_presentation_ = false;
  bool has_source_key_ = false;
  std::uint8_t channel_mask_ = Red | Green | Blue | Alpha;
  std::uint8_t masked_channel_mask_ = 0u;
  std::uint64_t masked_image_rebuild_count_ = 0u;
  QtUiScaleMetrics ui_scale_metrics_;

  QtTextureInfoCanvas *canvas_ = nullptr;
  QSplitter *splitter_ = nullptr;
  QTableWidget *table_ = nullptr;
  QToolButton *red_button_ = nullptr;
  QToolButton *green_button_ = nullptr;
  QToolButton *blue_button_ = nullptr;
  QToolButton *alpha_button_ = nullptr;

  class ImageStorage;
  std::unique_ptr<ImageStorage> images_;
};

} // namespace quader
