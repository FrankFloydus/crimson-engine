////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QColor>
#include <QMainWindow>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace quader {

class QtColorSwatchButton;

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

/// Presents one saved, staged, or invalid texture dependency in the form.
struct QtStandardMaterialTexturePresentation final {
  QString project_relative_path;
  QString diagnostic;

  bool
  operator==(const QtStandardMaterialTexturePresentation &) const = default;
};

/// Immutable Standard Material draft presentation supplied by editor APIs.
struct QtStandardMaterialEditorDocument final {
  QString stable_id;
  QString name;
  QString project_relative_path;
  QColor albedo = Qt::white;
  double roughness = 1.0;
  double metallic = 0.0;
  double normal_strength = 1.0;
  bool direct_x_normal = false;
  std::array<QtStandardMaterialTexturePresentation, 4> textures;
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
  QString properties_group = QStringLiteral("Material");
  QString name = QStringLiteral("Name");
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
  QString direct_x_normal =
      QStringLiteral("DirectX normal map (invert green channel)");
  QString choose_color = QStringLiteral("Choose Color...");
  QString choose_albedo_title = QStringLiteral("Choose Albedo Color");
  QString browse_texture = QStringLiteral("Browse...");
  QString clear_texture = QStringLiteral("Clear");
  QString no_texture = QStringLiteral("No texture");
  QString untitled_material = QStringLiteral("Untitled Material");
  QString unsaved = QStringLiteral("Unsaved");
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
  using ScalarEditedCallback =
      std::function<void(QtStandardMaterialScalar, double)>;
  using NormalConventionEditedCallback = std::function<void(bool)>;
  using TextureActionCallback = std::function<void(
      QtStandardMaterialTextureSlot, QtStandardMaterialTextureAction)>;
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
  [[nodiscard]] bool has_document() const { return has_document_; }

  void set_file_action_callback(FileActionCallback callback);
  void set_name_edited_callback(NameEditedCallback callback);
  void set_albedo_edited_callback(AlbedoEditedCallback callback);
  void set_scalar_edited_callback(ScalarEditedCallback callback);
  void set_normal_convention_edited_callback(
      NormalConventionEditedCallback callback);
  void set_texture_action_callback(TextureActionCallback callback);
  void set_preview_reset_callback(PreviewResetCallback callback);
  void set_presentation_state_changed_callback(
      PresentationStateChangedCallback callback);
  void set_close_request_handler(CloseRequestHandler handler);

  void restore_presentation_state(QtStandardMaterialPreviewMesh mesh,
                                  QtStandardMaterialPreviewShading shading);
  [[nodiscard]] QtStandardMaterialPreviewMesh preview_mesh() const {
    return preview_mesh_;
  }
  [[nodiscard]] QtStandardMaterialPreviewShading preview_shading() const {
    return preview_shading_;
  }

  /// Transfers ownership of one native-surface container or placeholder.
  void install_preview_widget(QWidget *widget);
  [[nodiscard]] QWidget *preview_surface_host() const {
    return preview_surface_host_;
  }

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  static constexpr std::size_t kTextureSlotCount = 4;
  static constexpr std::size_t kPreviewMeshCount = 5;

  /// Groups the stock Qt controls belonging to one texture slot row.
  struct TextureControls final {
    QLabel *label = nullptr;
    QLineEdit *path = nullptr;
    QPushButton *browse = nullptr;
    QPushButton *clear = nullptr;
  };

  void set_preview_mesh(QtStandardMaterialPreviewMesh mesh, bool notify);
  void set_preview_shading(QtStandardMaterialPreviewShading shading,
                           bool notify);
  void refresh_document_controls();
  void refresh_labels();
  void refresh_title_and_status();
  void choose_albedo_color();
  void emit_file_action(QtStandardMaterialEditorFileAction action);
  void emit_texture_action(QtStandardMaterialTextureSlot slot,
                           QtStandardMaterialTextureAction action);

  QtStandardMaterialEditorLabels labels_;
  QtStandardMaterialEditorDocument document_;
  bool has_document_ = false;
  bool refreshing_ = false;
  QtStandardMaterialPreviewMesh preview_mesh_ =
      QtStandardMaterialPreviewMesh::Sphere;
  QtStandardMaterialPreviewShading preview_shading_ =
      QtStandardMaterialPreviewShading::Rendered;

  QAction *new_action_ = nullptr;
  QAction *open_action_ = nullptr;
  QAction *save_action_ = nullptr;
  QAction *save_as_action_ = nullptr;
  QAction *close_action_ = nullptr;
  QAction *reset_preview_action_ = nullptr;
  std::array<QAction *, kPreviewMeshCount> preview_mesh_actions_{};
  QComboBox *shading_combo_ = nullptr;
  QWidget *preview_surface_host_ = nullptr;
  QVBoxLayout *preview_surface_layout_ = nullptr;
  QLabel *preview_placeholder_ = nullptr;
  QWidget *preview_widget_ = nullptr;
  QGroupBox *properties_group_ = nullptr;
  QLabel *name_label_ = nullptr;
  QLineEdit *name_edit_ = nullptr;
  QLabel *type_label_ = nullptr;
  QLineEdit *type_edit_ = nullptr;
  QLabel *albedo_label_ = nullptr;
  QtColorSwatchButton *albedo_button_ = nullptr;
  QLabel *roughness_label_ = nullptr;
  QDoubleSpinBox *roughness_spin_ = nullptr;
  QLabel *metallic_label_ = nullptr;
  QDoubleSpinBox *metallic_spin_ = nullptr;
  QLabel *normal_strength_label_ = nullptr;
  QDoubleSpinBox *normal_strength_spin_ = nullptr;
  QCheckBox *direct_x_normal_ = nullptr;
  std::array<TextureControls, kTextureSlotCount> texture_controls_{};

  FileActionCallback file_action_;
  NameEditedCallback name_edited_;
  AlbedoEditedCallback albedo_edited_;
  ScalarEditedCallback scalar_edited_;
  NormalConventionEditedCallback normal_convention_edited_;
  TextureActionCallback texture_action_;
  PreviewResetCallback preview_reset_;
  PresentationStateChangedCallback presentation_state_changed_;
  CloseRequestHandler close_request_handler_;
};

} // namespace quader
