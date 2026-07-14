////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_standard_material_editor_window.hpp"

#include "ui/qt_color_swatch_button.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <utility>

namespace quader {
namespace {

constexpr int kDefaultWidth = 1040;
constexpr int kDefaultHeight = 680;
constexpr int kPreviewStretch = 3;
constexpr int kPropertiesStretch = 2;
constexpr int kScalarDecimals = 3;
constexpr double kScalarStep = 0.01;

constexpr std::size_t texture_index(QtStandardMaterialTextureSlot slot) {
  return static_cast<std::size_t>(slot);
}

constexpr std::size_t mesh_index(QtStandardMaterialPreviewMesh mesh) {
  return static_cast<std::size_t>(mesh);
}

QtStandardMaterialPreviewShading shading_from_index(int index) {
  switch (index) {
  case 0:
    return QtStandardMaterialPreviewShading::Wireframe;
  case 1:
    return QtStandardMaterialPreviewShading::Shaded;
  default:
    return QtStandardMaterialPreviewShading::Rendered;
  }
}

int shading_index(QtStandardMaterialPreviewShading shading) {
  return static_cast<int>(shading);
}

} // namespace

QtStandardMaterialEditorWindow::QtStandardMaterialEditorWindow(QWidget *parent)
    : QMainWindow(parent, Qt::Tool) {
  setObjectName(QStringLiteral("standardMaterialEditor.window"));
  setAttribute(Qt::WA_DeleteOnClose, false);
  resize(kDefaultWidth, kDefaultHeight);

  QMenu *file_menu = menuBar()->addMenu(QString());
  file_menu->setObjectName(QStringLiteral("standardMaterialEditor.fileMenu"));
  new_action_ = file_menu->addAction(style()->standardIcon(QStyle::SP_FileIcon),
                                     QString());
  new_action_->setObjectName(QStringLiteral("standardMaterialEditor.file.new"));
  open_action_ = file_menu->addAction(
      style()->standardIcon(QStyle::SP_DialogOpenButton), QString());
  open_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.file.open"));
  file_menu->addSeparator();
  save_action_ = file_menu->addAction(
      style()->standardIcon(QStyle::SP_DialogSaveButton), QString());
  save_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.file.save"));
  save_as_action_ = file_menu->addAction(
      style()->standardIcon(QStyle::SP_DialogSaveButton), QString());
  save_as_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.file.saveAs"));
  file_menu->addSeparator();
  close_action_ = file_menu->addAction(
      style()->standardIcon(QStyle::SP_DialogCloseButton), QString());
  close_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.file.close"));

  const auto set_document_shortcut =
      [](QAction *action, Qt::KeyboardModifiers modifiers, Qt::Key key) {
        action->setShortcut(QKeySequence(QKeyCombination(modifiers, key)));
        action->setShortcutContext(Qt::WindowShortcut);
      };
  set_document_shortcut(new_action_, Qt::ControlModifier, Qt::Key_N);
  set_document_shortcut(open_action_, Qt::ControlModifier, Qt::Key_O);
  set_document_shortcut(save_action_, Qt::ControlModifier, Qt::Key_S);
  set_document_shortcut(save_as_action_, Qt::ControlModifier | Qt::AltModifier,
                        Qt::Key_S);
  set_document_shortcut(close_action_, Qt::ControlModifier, Qt::Key_W);

  QMenu *view_menu = menuBar()->addMenu(QString());
  view_menu->setObjectName(QStringLiteral("standardMaterialEditor.viewMenu"));
  QMenu *preview_mesh_menu = view_menu->addMenu(QString());
  preview_mesh_menu->setObjectName(
      QStringLiteral("standardMaterialEditor.previewMeshMenu"));
  auto *preview_mesh_group = new QActionGroup(this);
  preview_mesh_group->setExclusive(true);
  for (std::size_t index = 0; index < kPreviewMeshCount; ++index) {
    QAction *action = preview_mesh_menu->addAction(QString());
    action->setCheckable(true);
    preview_mesh_group->addAction(action);
    preview_mesh_actions_[index] = action;
  }
  preview_mesh_actions_[mesh_index(QtStandardMaterialPreviewMesh::Sphere)]
      ->setObjectName(
          QStringLiteral("standardMaterialEditor.previewMesh.sphere"));
  preview_mesh_actions_[mesh_index(QtStandardMaterialPreviewMesh::Box)]
      ->setObjectName(QStringLiteral("standardMaterialEditor.previewMesh.box"));
  preview_mesh_actions_[mesh_index(QtStandardMaterialPreviewMesh::RoundedBox)]
      ->setObjectName(
          QStringLiteral("standardMaterialEditor.previewMesh.roundedBox"));
  preview_mesh_actions_[mesh_index(QtStandardMaterialPreviewMesh::Cylinder)]
      ->setObjectName(
          QStringLiteral("standardMaterialEditor.previewMesh.cylinder"));
  preview_mesh_actions_[mesh_index(QtStandardMaterialPreviewMesh::Plane)]
      ->setObjectName(
          QStringLiteral("standardMaterialEditor.previewMesh.plane"));
  view_menu->addSeparator();
  reset_preview_action_ = view_menu->addAction(QString());
  reset_preview_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.preview.reset"));

  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setObjectName(QStringLiteral("standardMaterialEditor.splitter"));

  auto *preview_panel = new QWidget(splitter);
  preview_panel->setObjectName(
      QStringLiteral("standardMaterialEditor.previewPanel"));
  auto *preview_panel_layout = new QVBoxLayout(preview_panel);
  auto *preview_toolbar = new QHBoxLayout();
  preview_toolbar->addStretch(1);
  shading_combo_ = new QComboBox(preview_panel);
  shading_combo_->setObjectName(
      QStringLiteral("standardMaterialEditor.previewShading"));
  preview_toolbar->addWidget(shading_combo_);
  preview_panel_layout->addLayout(preview_toolbar);

  preview_surface_host_ = new QWidget(preview_panel);
  preview_surface_host_->setObjectName(
      QStringLiteral("standardMaterialEditor.previewHost"));
  preview_surface_layout_ = new QVBoxLayout(preview_surface_host_);
  preview_surface_layout_->setContentsMargins(0, 0, 0, 0);
  preview_placeholder_ = new QLabel(preview_surface_host_);
  preview_placeholder_->setObjectName(
      QStringLiteral("standardMaterialEditor.previewPlaceholder"));
  preview_placeholder_->setAlignment(Qt::AlignCenter);
  preview_placeholder_->setWordWrap(true);
  preview_surface_layout_->addWidget(preview_placeholder_, 1);
  preview_panel_layout->addWidget(preview_surface_host_, 1);
  splitter->addWidget(preview_panel);

  auto *properties_scroll = new QScrollArea(splitter);
  properties_scroll->setObjectName(
      QStringLiteral("standardMaterialEditor.propertiesScroll"));
  properties_scroll->setWidgetResizable(true);
  auto *properties_content = new QWidget(properties_scroll);
  auto *properties_layout = new QVBoxLayout(properties_content);
  properties_group_ = new QGroupBox(properties_content);
  properties_group_->setObjectName(
      QStringLiteral("standardMaterialEditor.properties"));
  auto *form = new QFormLayout(properties_group_);

  name_label_ = new QLabel(properties_group_);
  name_edit_ = new QLineEdit(properties_group_);
  name_edit_->setObjectName(QStringLiteral("standardMaterialEditor.name"));
  name_label_->setBuddy(name_edit_);
  form->addRow(name_label_, name_edit_);

  type_label_ = new QLabel(properties_group_);
  type_edit_ = new QLineEdit(properties_group_);
  type_edit_->setObjectName(QStringLiteral("standardMaterialEditor.type"));
  type_edit_->setReadOnly(true);
  type_label_->setBuddy(type_edit_);
  form->addRow(type_label_, type_edit_);

  albedo_label_ = new QLabel(properties_group_);
  albedo_button_ = new QtColorSwatchButton(properties_group_);
  albedo_button_->setObjectName(
      QStringLiteral("standardMaterialEditor.albedo"));
  albedo_label_->setBuddy(albedo_button_);
  form->addRow(albedo_label_, albedo_button_);

  const auto create_texture_controls = [this, form](
                                           QtStandardMaterialTextureSlot slot,
                                           const QString &stable_name) {
    TextureControls &controls = texture_controls_[texture_index(slot)];
    controls.label = new QLabel(properties_group_);
    auto *row = new QWidget(properties_group_);
    auto *row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    controls.path = new QLineEdit(row);
    controls.path->setObjectName(
        QStringLiteral("standardMaterialEditor.%1.path").arg(stable_name));
    controls.path->setReadOnly(true);
    controls.browse = new QPushButton(row);
    controls.browse->setObjectName(
        QStringLiteral("standardMaterialEditor.%1.browse").arg(stable_name));
    controls.clear = new QPushButton(row);
    controls.clear->setObjectName(
        QStringLiteral("standardMaterialEditor.%1.clear").arg(stable_name));
    row_layout->addWidget(controls.path, 1);
    row_layout->addWidget(controls.browse);
    row_layout->addWidget(controls.clear);
    controls.label->setBuddy(controls.path);
    form->addRow(controls.label, row);
    QObject::connect(
        controls.browse, &QPushButton::clicked, this, [this, slot]() {
          emit_texture_action(slot, QtStandardMaterialTextureAction::Browse);
        });
    QObject::connect(
        controls.clear, &QPushButton::clicked, this, [this, slot]() {
          emit_texture_action(slot, QtStandardMaterialTextureAction::Clear);
        });
  };
  create_texture_controls(QtStandardMaterialTextureSlot::Albedo,
                          QStringLiteral("albedoTexture"));

  roughness_label_ = new QLabel(properties_group_);
  roughness_spin_ = new QDoubleSpinBox(properties_group_);
  roughness_spin_->setObjectName(
      QStringLiteral("standardMaterialEditor.roughness"));
  roughness_spin_->setRange(0.0, 1.0);
  roughness_spin_->setDecimals(kScalarDecimals);
  roughness_spin_->setSingleStep(kScalarStep);
  roughness_spin_->setAccelerated(true);
  roughness_label_->setBuddy(roughness_spin_);
  form->addRow(roughness_label_, roughness_spin_);
  create_texture_controls(QtStandardMaterialTextureSlot::Roughness,
                          QStringLiteral("roughnessTexture"));

  metallic_label_ = new QLabel(properties_group_);
  metallic_spin_ = new QDoubleSpinBox(properties_group_);
  metallic_spin_->setObjectName(
      QStringLiteral("standardMaterialEditor.metallic"));
  metallic_spin_->setRange(0.0, 1.0);
  metallic_spin_->setDecimals(kScalarDecimals);
  metallic_spin_->setSingleStep(kScalarStep);
  metallic_spin_->setAccelerated(true);
  metallic_label_->setBuddy(metallic_spin_);
  form->addRow(metallic_label_, metallic_spin_);
  create_texture_controls(QtStandardMaterialTextureSlot::Metallic,
                          QStringLiteral("metallicTexture"));

  normal_strength_label_ = new QLabel(properties_group_);
  normal_strength_spin_ = new QDoubleSpinBox(properties_group_);
  normal_strength_spin_->setObjectName(
      QStringLiteral("standardMaterialEditor.normalStrength"));
  normal_strength_spin_->setRange(0.0, 2.0);
  normal_strength_spin_->setDecimals(kScalarDecimals);
  normal_strength_spin_->setSingleStep(kScalarStep);
  normal_strength_spin_->setAccelerated(true);
  normal_strength_label_->setBuddy(normal_strength_spin_);
  form->addRow(normal_strength_label_, normal_strength_spin_);
  create_texture_controls(QtStandardMaterialTextureSlot::Normal,
                          QStringLiteral("normalTexture"));

  direct_x_normal_ = new QCheckBox(properties_group_);
  direct_x_normal_->setObjectName(
      QStringLiteral("standardMaterialEditor.directXNormal"));
  form->addRow(direct_x_normal_);
  properties_layout->addWidget(properties_group_);
  properties_layout->addStretch(1);
  properties_scroll->setWidget(properties_content);
  splitter->addWidget(properties_scroll);
  splitter->setStretchFactor(0, kPreviewStretch);
  splitter->setStretchFactor(1, kPropertiesStretch);
  setCentralWidget(splitter);
  statusBar()->setObjectName(
      QStringLiteral("standardMaterialEditor.statusBar"));

  QObject::connect(new_action_, &QAction::triggered, this, [this]() {
    emit_file_action(QtStandardMaterialEditorFileAction::New);
  });
  QObject::connect(open_action_, &QAction::triggered, this, [this]() {
    emit_file_action(QtStandardMaterialEditorFileAction::Open);
  });
  QObject::connect(save_action_, &QAction::triggered, this, [this]() {
    emit_file_action(QtStandardMaterialEditorFileAction::Save);
  });
  QObject::connect(save_as_action_, &QAction::triggered, this, [this]() {
    emit_file_action(QtStandardMaterialEditorFileAction::SaveAs);
  });
  QObject::connect(close_action_, &QAction::triggered, this,
                   [this]() { close(); });
  QObject::connect(reset_preview_action_, &QAction::triggered, this, [this]() {
    if (preview_reset_) {
      preview_reset_();
    }
  });
  for (std::size_t index = 0; index < kPreviewMeshCount; ++index) {
    QObject::connect(preview_mesh_actions_[index], &QAction::triggered, this,
                     [this, index]() {
                       set_preview_mesh(
                           static_cast<QtStandardMaterialPreviewMesh>(index),
                           true);
                     });
  }
  QObject::connect(shading_combo_, &QComboBox::currentIndexChanged, this,
                   [this](int index) {
                     set_preview_shading(shading_from_index(index), true);
                   });
  QObject::connect(name_edit_, &QLineEdit::editingFinished, this, [this]() {
    if (!refreshing_ && name_edited_) {
      name_edited_(name_edit_->text());
    }
  });
  QObject::connect(albedo_button_, &QPushButton::clicked, this,
                   [this]() { choose_albedo_color(); });
  QObject::connect(roughness_spin_, &QDoubleSpinBox::valueChanged, this,
                   [this](double value) {
                     if (!refreshing_ && scalar_edited_) {
                       scalar_edited_(QtStandardMaterialScalar::Roughness,
                                      value);
                     }
                   });
  QObject::connect(metallic_spin_, &QDoubleSpinBox::valueChanged, this,
                   [this](double value) {
                     if (!refreshing_ && scalar_edited_) {
                       scalar_edited_(QtStandardMaterialScalar::Metallic,
                                      value);
                     }
                   });
  QObject::connect(normal_strength_spin_, &QDoubleSpinBox::valueChanged, this,
                   [this](double value) {
                     if (!refreshing_ && scalar_edited_) {
                       scalar_edited_(QtStandardMaterialScalar::NormalStrength,
                                      value);
                     }
                   });
  QObject::connect(direct_x_normal_, &QCheckBox::toggled, this,
                   [this](bool direct_x) {
                     if (!refreshing_ && normal_convention_edited_) {
                       normal_convention_edited_(direct_x);
                     }
                   });

  refresh_labels();
  set_preview_mesh(QtStandardMaterialPreviewMesh::Sphere, false);
  set_preview_shading(QtStandardMaterialPreviewShading::Rendered, false);
  refresh_document_controls();
}

QtStandardMaterialEditorWindow::~QtStandardMaterialEditorWindow() = default;

void QtStandardMaterialEditorWindow::set_labels(
    QtStandardMaterialEditorLabels labels) {
  labels_ = std::move(labels);
  refresh_labels();
}

void QtStandardMaterialEditorWindow::set_document(
    QtStandardMaterialEditorDocument document) {
  if (has_document_ && document_ == document) {
    return;
  }
  document_ = std::move(document);
  has_document_ = true;
  refresh_document_controls();
}

void QtStandardMaterialEditorWindow::clear_document() {
  document_ = {};
  has_document_ = false;
  refresh_document_controls();
}

void QtStandardMaterialEditorWindow::set_file_action_callback(
    FileActionCallback callback) {
  file_action_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_name_edited_callback(
    NameEditedCallback callback) {
  name_edited_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_albedo_edited_callback(
    AlbedoEditedCallback callback) {
  albedo_edited_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_scalar_edited_callback(
    ScalarEditedCallback callback) {
  scalar_edited_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_normal_convention_edited_callback(
    NormalConventionEditedCallback callback) {
  normal_convention_edited_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_texture_action_callback(
    TextureActionCallback callback) {
  texture_action_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_preview_reset_callback(
    PreviewResetCallback callback) {
  preview_reset_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_presentation_state_changed_callback(
    PresentationStateChangedCallback callback) {
  presentation_state_changed_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_close_request_handler(
    CloseRequestHandler handler) {
  close_request_handler_ = std::move(handler);
}

void QtStandardMaterialEditorWindow::restore_presentation_state(
    QtStandardMaterialPreviewMesh mesh,
    QtStandardMaterialPreviewShading shading) {
  set_preview_mesh(mesh, false);
  set_preview_shading(shading, false);
}

void QtStandardMaterialEditorWindow::install_preview_widget(QWidget *widget) {
  if (preview_widget_ == widget) {
    return;
  }
  if (preview_widget_ != nullptr) {
    preview_surface_layout_->removeWidget(preview_widget_);
    preview_widget_->hide();
    preview_widget_->deleteLater();
  }
  preview_widget_ = widget;
  if (preview_widget_ == nullptr) {
    preview_placeholder_->show();
    return;
  }
  preview_widget_->setParent(preview_surface_host_);
  preview_surface_layout_->addWidget(preview_widget_, 1);
  preview_placeholder_->hide();
  preview_widget_->show();
}

void QtStandardMaterialEditorWindow::closeEvent(QCloseEvent *event) {
  if (close_request_handler_ && !close_request_handler_()) {
    event->ignore();
    return;
  }
  QMainWindow::closeEvent(event);
}

void QtStandardMaterialEditorWindow::set_preview_mesh(
    QtStandardMaterialPreviewMesh mesh, bool notify) {
  const bool changed = preview_mesh_ != mesh;
  preview_mesh_ = mesh;
  const std::size_t index = mesh_index(mesh);
  if (index < preview_mesh_actions_.size() &&
      preview_mesh_actions_[index] != nullptr) {
    const QSignalBlocker blocker(preview_mesh_actions_[index]);
    preview_mesh_actions_[index]->setChecked(true);
  }
  if (notify && changed && presentation_state_changed_) {
    presentation_state_changed_();
  }
}

void QtStandardMaterialEditorWindow::set_preview_shading(
    QtStandardMaterialPreviewShading shading, bool notify) {
  const bool changed = preview_shading_ != shading;
  preview_shading_ = shading;
  {
    const QSignalBlocker blocker(shading_combo_);
    shading_combo_->setCurrentIndex(shading_index(shading));
  }
  if (notify && changed && presentation_state_changed_) {
    presentation_state_changed_();
  }
}

void QtStandardMaterialEditorWindow::refresh_document_controls() {
  refreshing_ = true;
  properties_group_->setEnabled(has_document_);
  save_action_->setEnabled(has_document_);
  save_as_action_->setEnabled(has_document_);

  const QSignalBlocker name_blocker(name_edit_);
  const QSignalBlocker roughness_blocker(roughness_spin_);
  const QSignalBlocker metallic_blocker(metallic_spin_);
  const QSignalBlocker normal_strength_blocker(normal_strength_spin_);
  const QSignalBlocker convention_blocker(direct_x_normal_);
  name_edit_->setText(has_document_ ? document_.name : QString());
  albedo_button_->set_swatch_color(has_document_ ? document_.albedo
                                                 : QColor(Qt::white));
  roughness_spin_->setValue(has_document_ ? document_.roughness : 1.0);
  metallic_spin_->setValue(has_document_ ? document_.metallic : 0.0);
  normal_strength_spin_->setValue(has_document_ ? document_.normal_strength
                                                : 1.0);
  direct_x_normal_->setChecked(has_document_ && document_.direct_x_normal);
  for (std::size_t index = 0; index < kTextureSlotCount; ++index) {
    TextureControls &controls = texture_controls_[index];
    const QtStandardMaterialTexturePresentation presentation =
        has_document_ ? document_.textures[index]
                      : QtStandardMaterialTexturePresentation{};
    controls.path->setText(presentation.project_relative_path);
    controls.path->setToolTip(presentation.diagnostic);
    controls.path->setAccessibleDescription(presentation.diagnostic);
    controls.clear->setEnabled(has_document_ &&
                               !presentation.project_relative_path.isEmpty());
  }
  refreshing_ = false;
  refresh_title_and_status();
}

void QtStandardMaterialEditorWindow::refresh_labels() {
  QMenu *file_menu =
      findChild<QMenu *>(QStringLiteral("standardMaterialEditor.fileMenu"));
  QMenu *view_menu =
      findChild<QMenu *>(QStringLiteral("standardMaterialEditor.viewMenu"));
  QMenu *preview_mesh_menu = findChild<QMenu *>(
      QStringLiteral("standardMaterialEditor.previewMeshMenu"));
  if (file_menu != nullptr) {
    file_menu->setTitle(labels_.file_menu);
  }
  if (view_menu != nullptr) {
    view_menu->setTitle(labels_.view_menu);
  }
  if (preview_mesh_menu != nullptr) {
    preview_mesh_menu->setTitle(labels_.preview_mesh_menu);
  }
  new_action_->setText(labels_.new_material);
  open_action_->setText(labels_.open_material);
  save_action_->setText(labels_.save_material);
  save_as_action_->setText(labels_.save_material_as);
  close_action_->setText(labels_.close_window);
  reset_preview_action_->setText(labels_.reset_preview);
  const std::array<QString, kPreviewMeshCount> mesh_labels{
      labels_.sphere, labels_.box, labels_.rounded_box, labels_.cylinder,
      labels_.plane};
  for (std::size_t index = 0; index < kPreviewMeshCount; ++index) {
    preview_mesh_actions_[index]->setText(mesh_labels[index]);
  }
  {
    const QSignalBlocker blocker(shading_combo_);
    shading_combo_->clear();
    shading_combo_->addItem(labels_.wireframe);
    shading_combo_->addItem(labels_.shaded);
    shading_combo_->addItem(labels_.rendered);
    shading_combo_->setCurrentIndex(shading_index(preview_shading_));
  }
  shading_combo_->setToolTip(labels_.view_menu);
  shading_combo_->setAccessibleName(labels_.view_menu);
  preview_placeholder_->setText(labels_.preview_unavailable);
  properties_group_->setTitle(labels_.properties_group);
  name_label_->setText(labels_.name);
  type_label_->setText(labels_.type);
  type_edit_->setText(labels_.standard_material);
  albedo_label_->setText(labels_.albedo);
  albedo_button_->setText(labels_.choose_color);
  roughness_label_->setText(labels_.roughness);
  metallic_label_->setText(labels_.metallic);
  normal_strength_label_->setText(labels_.normal_strength);
  direct_x_normal_->setText(labels_.direct_x_normal);
  const std::array<QString, kTextureSlotCount> texture_labels{
      labels_.albedo_texture, labels_.roughness_texture,
      labels_.metallic_texture, labels_.normal_texture};
  for (std::size_t index = 0; index < kTextureSlotCount; ++index) {
    TextureControls &controls = texture_controls_[index];
    controls.label->setText(texture_labels[index]);
    controls.path->setPlaceholderText(labels_.no_texture);
    controls.browse->setText(labels_.browse_texture);
    controls.clear->setText(labels_.clear_texture);
  }
  refresh_title_and_status();
}

void QtStandardMaterialEditorWindow::refresh_title_and_status() {
  if (!has_document_) {
    setWindowTitle(labels_.window_title);
    statusBar()->clearMessage();
    return;
  }
  const QString display_name = document_.name.trimmed().isEmpty()
                                   ? labels_.untitled_material
                                   : document_.name;
  setWindowTitle(QStringLiteral("%1%2 - %3")
                     .arg(display_name,
                          document_.dirty ? QStringLiteral(" *") : QString(),
                          labels_.window_title));
  QString status = document_.project_relative_path.isEmpty()
                       ? labels_.unsaved
                       : document_.project_relative_path;
  if (!document_.status_message.isEmpty()) {
    status += QStringLiteral(" | ") + document_.status_message;
  }
  statusBar()->showMessage(status);
}

void QtStandardMaterialEditorWindow::choose_albedo_color() {
  if (!has_document_) {
    return;
  }
  QColorDialog dialog(albedo_button_->swatch_color(), this);
  dialog.setWindowTitle(labels_.choose_albedo_title);
  dialog.setOption(QColorDialog::DontUseNativeDialog, true);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QColor color = dialog.selectedColor();
  if (color.isValid() && albedo_edited_) {
    albedo_edited_(color);
  }
}

void QtStandardMaterialEditorWindow::emit_file_action(
    QtStandardMaterialEditorFileAction action) {
  if (file_action_) {
    file_action_(action);
  }
}

void QtStandardMaterialEditorWindow::emit_texture_action(
    QtStandardMaterialTextureSlot slot,
    QtStandardMaterialTextureAction action) {
  if (has_document_ && texture_action_) {
    texture_action_(slot, action);
  }
}

} // namespace quader
