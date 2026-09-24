////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QColor>
#include <QImage>
#include <QMainWindow>
#include <QPointer>
#include <QSize>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QColorDialog;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QFormLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace quader {

class QtColorSwatchButton;
class QtTextureInfoWindow;
namespace qt_settings_detail {
class QtSettingsSection;
}

/// Selects one native File-menu command in the Standard Material editor.
enum class QtStandardMaterialEditorFileAction : std::uint8_t {
  New,
  Open,
  Save,
  SaveAs,
};

/// Selects the retained test mesh displayed by the material preview.
enum class QtStandardMaterialPreviewMesh : std::uint8_t {
  Sphere,
  Box,
  RoundedBox,
  Cylinder,
  Plane,
};

/// Selects the isolated preview viewport's material presentation mode.
enum class QtStandardMaterialPreviewShading : std::uint8_t {
  Wireframe,
  Shaded,
  Rendered,
};

/// Selects the material's diffuse response independently of preview draw mode.
enum class QtStandardMaterialDiffuseModel : std::uint8_t {
  Burley,
  Lambert,
};

/// Identifies one scalar control in the Standard Material property form.
enum class QtStandardMaterialScalar : std::uint8_t {
  Roughness,
  Metallic,
  NormalStrength,
};

/// Identifies one texture row in the Standard Material property form.
enum class QtStandardMaterialTextureSlot : std::uint8_t {
  Albedo,
  Roughness,
  Metallic,
  Normal,
};

/// Selects Browse or Clear intent for a material texture row.
enum class QtStandardMaterialTextureAction : std::uint8_t {
  Browse,
  Clear,
};

/// Identifies one editable component of the shared texture transform.
enum class QtStandardMaterialTextureTransformComponent : std::uint8_t {
  CenterU,
  CenterV,
  ScaleU,
  ScaleV,
  OffsetU,
  OffsetV,
  RotationDegrees,
};

/// Presentation values for the Standard Material texture-coordinate transform.
struct QtStandardMaterialTextureTransform final {
  double center_u = 0.5;
  double center_v = 0.5;
  double scale_u = 1.0;
  double scale_v = 1.0;
  double offset_u = 0.0;
  double offset_v = 0.0;
  double rotation_degrees = 0.0;

  bool operator==(const QtStandardMaterialTextureTransform &) const = default;
};

/// One localized packaged environment offered by the preview menu.
struct QtStandardMaterialPreviewEnvironment final {
  QString stable_id;
  QString display_name;

  bool operator==(const QtStandardMaterialPreviewEnvironment &) const =
      default;
};

/// Presents one saved, staged, or invalid texture dependency in the form.
struct QtStandardMaterialTexturePresentation final {
  QString project_relative_path;
  QString diagnostic;
  QImage preview_image;
  QString format;
  QString color_space;
  QSize pixel_size;
  qint64 encoded_byte_size = 0;
  std::uint64_t content_revision = 0;
  QString stable_texture_id;
  QString absolute_path;
  bool source_has_alpha = false;

  bool operator==(
      const QtStandardMaterialTexturePresentation &other) const noexcept {
    return project_relative_path == other.project_relative_path &&
           diagnostic == other.diagnostic && format == other.format &&
           color_space == other.color_space && pixel_size == other.pixel_size &&
           encoded_byte_size == other.encoded_byte_size &&
           content_revision == other.content_revision &&
           stable_texture_id == other.stable_texture_id &&
           absolute_path == other.absolute_path &&
           source_has_alpha == other.source_has_alpha &&
           preview_image.isNull() == other.preview_image.isNull();
  }
};

/// Immutable Standard Material draft presentation supplied by editor APIs.
struct QtStandardMaterialEditorDocument final {
  QString stable_id;
  std::uint32_t session_generation = 0u;
  QString name;
  QString project_relative_path;
  QColor albedo = Qt::white;
  double roughness = 1.0;
  double metallic = 0.0;
  double normal_strength = 1.0;
  bool direct_x_normal = false;
  QtStandardMaterialDiffuseModel diffuse_model = QtStandardMaterialDiffuseModel::Burley;
  std::array<QtStandardMaterialTexturePresentation, 4> textures;
  QtStandardMaterialTextureTransform texture_transform;
  bool dirty = false;
  QString status_message;

  bool operator==(const QtStandardMaterialEditorDocument &) const = default;
};

/// Localized text used by the retained Standard Material editor presentation.
struct QtStandardMaterialEditorLabels final {
  QString window_title = QStringLiteral("Standard Material Editor");
  QString file_menu = QStringLiteral("File");
  QString new_material = QStringLiteral("New");
  QString open_material = QStringLiteral("Open...");
  QString save_material = QStringLiteral("Save");
  QString save_material_as = QStringLiteral("Save As...");
  QString close_window = QStringLiteral("Close");
  QString view_menu = QStringLiteral("View");
  QString preview_mesh_menu = QStringLiteral("Preview Mesh");
  QString reset_preview = QStringLiteral("Reset Preview");
  QString environment_menu = QStringLiteral("Environment");
  QString show_environment_background = QStringLiteral("Show Background");
  QString sphere = QStringLiteral("Sphere");
  QString box = QStringLiteral("Box");
  QString rounded_box = QStringLiteral("Rounded Box");
  QString cylinder = QStringLiteral("Cylinder");
  QString plane = QStringLiteral("Plane");
  QString wireframe = QStringLiteral("Wireframe");
  QString shaded = QStringLiteral("Shaded");
  QString rendered = QStringLiteral("Rendered");
  QString preview_unavailable =
      QStringLiteral("Material preview is unavailable");
  QString preview_navigation = QStringLiteral(
      "MMB Tumble - Shift+MMB Pan - Ctrl+MMB Environment - Wheel Zoom");
  QString properties_group = QStringLiteral("Material");
  QString name = QStringLiteral("Name");
  QString diffuse_model = QStringLiteral("Diffuse Model");
  QString diffuse_burley = QStringLiteral("Burley");
  QString diffuse_lambert = QStringLiteral("Lambert");
  QString type = QStringLiteral("Type");
  QString standard_material = QStringLiteral("Standard Material");
  QString albedo = QStringLiteral("Albedo");
  QString albedo_texture = QStringLiteral("Albedo Texture");
  QString roughness = QStringLiteral("Roughness");
  QString roughness_texture = QStringLiteral("Roughness Texture");
  QString metallic = QStringLiteral("Metallic");
  QString metallic_texture = QStringLiteral("Metallic Texture");
  QString normal_strength = QStringLiteral("Normal Strength");
  QString normal_texture = QStringLiteral("Normal Texture");
  QString direct_x_normal = QStringLiteral("DirectX");
  QString choose_albedo_title = QStringLiteral("Choose Albedo Color");
  QString browse_texture = QStringLiteral("Browse...");
  QString clear_texture = QStringLiteral("Clear");
  QString no_texture = QStringLiteral("No texture");
  QString untitled_material = QStringLiteral("Untitled Material");
  QString unsaved = QStringLiteral("Unsaved");
  QString inspect_texture = QStringLiteral("Inspect texture");
  QString texture_transform = QStringLiteral("Texture Transform");
  QString texture_center = QStringLiteral("Center (tiles)");
  QString texture_scale = QStringLiteral("UV Tiling");
  QString texture_offset = QStringLiteral("Offset (tiles)");
  QString texture_rotation = QStringLiteral("Rotation (\u00B0)");
  QString texture_transform_tooltip = QStringLiteral(
      "Shared material transform: affects every texture channel and every surface using this material, after face UV mapping.");
  QString texture_center_tooltip = QStringLiteral(
      "Pivot for material UV tiling and rotation, measured in texture tiles. The default 0.5, 0.5 is the image center.");
  QString texture_scale_tooltip = QStringLiteral(
      "Multiply material UV repetitions. Larger magnitudes make the texture smaller; negative values mirror it. Applies to every surface using this material.");
  QString texture_offset_tooltip = QStringLiteral(
      "Offset the material UV coordinates in texture tiles after tiling and rotation. One unit equals a full texture tile.");
  QString texture_rotation_tooltip = QStringLiteral(
      "Rotate material UV coordinates around Center, after tiling and before Offset. Applies to every surface using this material.");
  QString texture_inspector = QStringLiteral("Texture Inspector");
  QString texture_info_property = QStringLiteral("Property");
  QString texture_info_value = QStringLiteral("Value");
  QString texture_info_name = QStringLiteral("name");
  QString texture_info_location = QStringLiteral("location");
  QString texture_info_format = QStringLiteral("format");
  QString texture_info_size = QStringLiteral("size");
  QString texture_info_file_size = QStringLiteral("file size");
  QString texture_info_red_channel = QStringLiteral("Red channel");
  QString texture_info_green_channel = QStringLiteral("Green channel");
  QString texture_info_blue_channel = QStringLiteral("Blue channel");
  QString texture_info_alpha_channel = QStringLiteral("Alpha channel");
};

/**
 * Retained stock-Qt Standard Material editor. Domain owners supply immutable
 * draft snapshots and receive semantic edit intent through callbacks.
 */
class QtStandardMaterialEditorWindow final : public QMainWindow {
public:
  using FileActionCallback =
      std::function<void(QtStandardMaterialEditorFileAction)>;
  using NameEditedCallback = std::function<void(QString)>;
  using AlbedoEditedCallback = std::function<void(QColor)>;
  using AlbedoPreviewBeginCallback = std::function<bool()>;
  using AlbedoPreviewFinishCallback = std::function<void(bool)>;
  using ScalarEditedCallback =
      std::function<void(QtStandardMaterialScalar, double)>;
  using NormalConventionEditedCallback = std::function<void(bool)>;
  using DiffuseModelEditedCallback = std::function<void(QtStandardMaterialDiffuseModel)>;
  using TextureActionCallback = std::function<void(
      QtStandardMaterialTextureSlot, QtStandardMaterialTextureAction)>;
  using TextureTransformEditedCallback =
      std::function<void(QtStandardMaterialTextureTransformComponent, double)>;
  using PreviewResetCallback = std::function<void()>;
  using PresentationStateChangedCallback = std::function<void()>;
  using CloseRequestHandler = std::function<bool()>;

  explicit QtStandardMaterialEditorWindow(QWidget *parent = nullptr);
  ~QtStandardMaterialEditorWindow() override;

  QtStandardMaterialEditorWindow(const QtStandardMaterialEditorWindow &) =
      delete;
  QtStandardMaterialEditorWindow &
  operator=(const QtStandardMaterialEditorWindow &) = delete;

  void set_labels(QtStandardMaterialEditorLabels labels);
  void set_document(QtStandardMaterialEditorDocument document);
  void clear_document();
  /// Closes transient dialogs without accessing an outgoing runtime owner.
  void abandon_interactions();
  [[nodiscard]] bool has_document() const { return has_document_; }

  void set_file_action_callback(FileActionCallback callback);
  void set_name_edited_callback(NameEditedCallback callback);
  void set_albedo_edited_callback(AlbedoEditedCallback callback);
  void set_albedo_preview_begin_callback(AlbedoPreviewBeginCallback callback);
  void set_albedo_preview_finish_callback(AlbedoPreviewFinishCallback callback);
  void set_scalar_edited_callback(ScalarEditedCallback callback);
  void set_normal_convention_edited_callback(
      NormalConventionEditedCallback callback);
  void set_diffuse_model_edited_callback(DiffuseModelEditedCallback callback);
  void set_texture_action_callback(TextureActionCallback callback);
  void set_texture_transform_edited_callback(
      TextureTransformEditedCallback callback);
  void set_preview_reset_callback(PreviewResetCallback callback);
  void set_presentation_state_changed_callback(
      PresentationStateChangedCallback callback);
  void set_close_request_handler(CloseRequestHandler handler);

  /// Reapplies retained chrome metrics without changing saved window geometry.
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  void set_preview_environments(
      std::vector<QtStandardMaterialPreviewEnvironment> environments);
  void restore_presentation_state(
      QtStandardMaterialPreviewMesh mesh,
      QtStandardMaterialPreviewShading shading,
      QString environment_id = QStringLiteral("lightroom_14b"),
      bool show_environment_background = false);
  [[nodiscard]] QtStandardMaterialPreviewMesh preview_mesh() const {
    return preview_mesh_;
  }
  [[nodiscard]] QtStandardMaterialPreviewShading preview_shading() const {
    return preview_shading_;
  }
  [[nodiscard]] const QString &preview_environment() const {
    return preview_environment_;
  }
  [[nodiscard]] bool preview_environment_background_visible() const {
    return preview_environment_background_visible_;
  }

  /// Transfers ownership of one native-surface container or placeholder.
  void install_preview_widget(QWidget *widget);
  [[nodiscard]] QWidget *preview_surface_host() const {
    return preview_surface_host_;
  }

protected:
  bool event(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  static constexpr std::size_t kScalarCount = 3;
  static constexpr std::size_t kTextureSlotCount = 4;
  static constexpr std::size_t kTextureTransformCount = 7;
  static constexpr std::size_t kPreviewMeshCount = 5;

  /// Retains the latest numeric scrub value until its bounded dispatch cadence.
  struct LiveNumberDispatch final {
    QTimer *timer = nullptr;
    std::optional<double> pending_value;
  };

  /// Groups the stock Qt controls belonging to one texture slot row.
  struct TextureControls final {
    QLabel *label = nullptr;
    QToolButton *preview = nullptr;
    QLabel *path = nullptr;
    QToolButton *browse = nullptr;
    QToolButton *clear = nullptr;
    QString preview_icon_document_id;
    QSize preview_icon_pixel_size;
    std::uint64_t preview_icon_content_revision = 0u;
    bool preview_icon_has_image = false;
    bool preview_icon_initialized = false;
  };

  /// Groups the numeric field and dispatch state for one transform component.
  struct TextureTransformControls final {
    QLabel *label = nullptr;
    QDoubleSpinBox *spin = nullptr;
    LiveNumberDispatch live_dispatch;
  };

  void dispatch_scalar_edit(QtStandardMaterialScalar scalar, double value,
                            bool coalesce);
  void flush_scalar_edit(QtStandardMaterialScalar scalar);
  void dispatch_texture_transform_edit(
      QtStandardMaterialTextureTransformComponent component, double value,
      bool coalesce);
  void flush_texture_transform_edit(
      QtStandardMaterialTextureTransformComponent component);
  [[nodiscard]] bool cancel_pending_number_edits();
  void flush_pending_number_edits();
  void set_preview_mesh(QtStandardMaterialPreviewMesh mesh, bool notify);
  void set_preview_shading(QtStandardMaterialPreviewShading shading,
                           bool notify);
  void set_preview_environment(QString environment_id, bool notify);
  void set_preview_environment_background(bool visible, bool notify);
  void rebuild_environment_menu();
  void refresh_document_controls();
  void refresh_form_metrics();
  void refresh_labels();
  void refresh_title_and_status();
  void choose_albedo_color();
  void emit_file_action(QtStandardMaterialEditorFileAction action);
  void emit_texture_action(QtStandardMaterialTextureSlot slot,
                           QtStandardMaterialTextureAction action);
  void show_texture_inspector(QtStandardMaterialTextureSlot slot);
  void refresh_texture_inspector();
  void invalidate_texture_preview_icons();
  void refresh_texture_palette_icons();

  QtStandardMaterialEditorLabels labels_;
  QtStandardMaterialEditorDocument document_;
  bool has_document_ = false;
  bool refreshing_ = false;
  QtStandardMaterialPreviewMesh preview_mesh_ =
      QtStandardMaterialPreviewMesh::Sphere;
  QtStandardMaterialPreviewShading preview_shading_ =
      QtStandardMaterialPreviewShading::Rendered;
  QString preview_environment_ = QStringLiteral("lightroom_14b");
  bool preview_environment_background_visible_ = false;
  std::vector<QtStandardMaterialPreviewEnvironment> preview_environments_;

  QAction *new_action_ = nullptr;
  QAction *open_action_ = nullptr;
  QAction *save_action_ = nullptr;
  QAction *save_as_action_ = nullptr;
  QAction *close_action_ = nullptr;
  QAction *reset_preview_action_ = nullptr;
  QMenu *environment_menu_ = nullptr;
  QActionGroup *environment_action_group_ = nullptr;
  QAction *show_environment_background_action_ = nullptr;
  std::vector<QAction *> environment_actions_;
  std::array<QAction *, kPreviewMeshCount> preview_mesh_actions_{};
  QComboBox *shading_combo_ = nullptr;
  QWidget *preview_surface_host_ = nullptr;
  QVBoxLayout *preview_surface_layout_ = nullptr;
  QLabel *preview_placeholder_ = nullptr;
  QWidget *preview_widget_ = nullptr;
  qt_settings_detail::QtSettingsSection *properties_group_ = nullptr;
  QFormLayout *properties_form_ = nullptr;
  QFormLayout *texture_transform_form_ = nullptr;
  bool form_metrics_refresh_pending_ = false;
  QLabel *name_label_ = nullptr;
  QLineEdit *name_edit_ = nullptr;
  QLabel *diffuse_model_label_ = nullptr;
  QComboBox *diffuse_model_combo_ = nullptr;
  QLabel *albedo_label_ = nullptr;
  QtColorSwatchButton *albedo_button_ = nullptr;
  QPointer<QColorDialog> active_albedo_dialog_;
  std::uint64_t interaction_generation_ = 0u;
  QLabel *roughness_label_ = nullptr;
  QDoubleSpinBox *roughness_spin_ = nullptr;
  QLabel *metallic_label_ = nullptr;
  QDoubleSpinBox *metallic_spin_ = nullptr;
  QLabel *normal_strength_label_ = nullptr;
  QDoubleSpinBox *normal_strength_spin_ = nullptr;
  std::array<LiveNumberDispatch, kScalarCount> scalar_live_dispatches_{};
  QCheckBox *direct_x_normal_ = nullptr;
  std::array<TextureControls, kTextureSlotCount> texture_controls_{};
  qt_settings_detail::QtSettingsSection *texture_transform_group_ = nullptr;
  std::array<TextureTransformControls, kTextureTransformCount>
      texture_transform_controls_{};
  QtTextureInfoWindow *texture_inspector_window_ = nullptr;
  std::optional<QtStandardMaterialTextureSlot> inspected_texture_slot_;
  QtUiScaleMetrics ui_scale_metrics_;

  FileActionCallback file_action_;
  NameEditedCallback name_edited_;
  AlbedoEditedCallback albedo_edited_;
  AlbedoPreviewBeginCallback albedo_preview_begin_;
  AlbedoPreviewFinishCallback albedo_preview_finish_;
  ScalarEditedCallback scalar_edited_;
  NormalConventionEditedCallback normal_convention_edited_;
  DiffuseModelEditedCallback diffuse_model_edited_;
  TextureActionCallback texture_action_;
  TextureTransformEditedCallback texture_transform_edited_;
  PreviewResetCallback preview_reset_;
  PresentationStateChangedCallback presentation_state_changed_;
  CloseRequestHandler close_request_handler_;
};

} // namespace quader
