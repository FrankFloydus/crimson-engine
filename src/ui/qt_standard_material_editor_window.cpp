////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_standard_material_editor_window.hpp"

#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_double_spin_box.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace quader {
namespace {

constexpr int kDefaultWidth = 1040;
constexpr int kDefaultHeight = 680;
constexpr int kPreviewStretch = 3;
constexpr int kPropertiesStretch = 2;
constexpr int kScalarDecimals = 3;
constexpr double kScalarStep = 0.01;
constexpr int kScalarSliderScale = 100;
constexpr int kLiveNumberDispatchIntervalMs = 16;
constexpr int kTexturePreviewExtent = 64;
constexpr int kTexturePreviewIconExtent = 56;
constexpr int kTextureInspectorWidth = 720;
constexpr int kTextureInspectorHeight = 620;
constexpr double kMinimumTextureZoom = 0.1;
constexpr double kMaximumTextureZoom = 64.0;

/// Preserves the filename end while fitting a project texture path in its row.
class ElidedPathLabel final : public QLabel {
public:
  explicit ElidedPathLabel(QWidget *parent = nullptr) : QLabel(parent) {
    setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    setMargin(3);
    setMinimumWidth(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    setMargin(metrics.px(3));
    setMinimumWidth(metrics.px(48));
    refresh_elision();
  }

  void set_full_text(QString full_text, QString placeholder) {
    full_text_ = std::move(full_text);
    placeholder_ = std::move(placeholder);
    setProperty("fullText", full_text_);
    refresh_elision();
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QLabel::resizeEvent(event);
    refresh_elision();
  }

private:
  void refresh_elision() {
    const QString source = full_text_.isEmpty() ? placeholder_ : full_text_;
    const int available_width = std::max(0, width() - 2 * margin());
    QLabel::setText(
        fontMetrics().elidedText(source, Qt::ElideLeft, available_width));
  }

  QString full_text_;
  QString placeholder_;
};

/// Paints one decoded texture with retained zoom and pan inspection state.
class TextureInspectorCanvas final : public QWidget {
public:
  explicit TextureInspectorCanvas(QWidget *parent = nullptr) : QWidget(parent) {
    setObjectName(
        QStringLiteral("standardMaterialEditor.textureInspector.canvas"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 180);
    refresh_properties();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    setMinimumSize(metrics.size(QSize(240, 180)));
    updateGeometry();
  }

  void set_image(QImage image) {
    image_ = std::move(image);
    reset_view();
  }

  void reset_view() {
    zoom_ = 1.0;
    pan_ = {};
    refresh_properties();
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), palette().brush(backgroundRole()));
    if (image_.isNull()) {
      return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(rect().center() + pan_);
    painter.scale(zoom_, zoom_);
    painter.drawImage(QPointF(-0.5 * image_.width(), -0.5 * image_.height()),
                      image_);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton ||
        event->button() == Qt::MiddleButton) {
      panning_ = true;
      previous_pointer_position_ = event->position();
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (!panning_) {
      QWidget::mouseMoveEvent(event);
      return;
    }
    pan_ += event->position() - previous_pointer_position_;
    previous_pointer_position_ = event->position();
    refresh_properties();
    update();
    event->accept();
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if ((event->button() == Qt::LeftButton ||
         event->button() == Qt::MiddleButton) &&
        panning_) {
      panning_ = false;
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton ||
        event->button() == Qt::MiddleButton) {
      reset_view();
      event->accept();
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }

  void wheelEvent(QWheelEvent *event) override {
    const double zoom_factor = std::pow(1.0015, event->angleDelta().y());
    zoom_ = std::clamp(zoom_ * zoom_factor, kMinimumTextureZoom,
                       kMaximumTextureZoom);
    refresh_properties();
    update();
    event->accept();
  }

private:
  void refresh_properties() {
    setProperty("zoom", zoom_);
    setProperty("panX", pan_.x());
    setProperty("panY", pan_.y());
  }

  QImage image_;
  QPointF pan_;
  QPointF previous_pointer_position_;
  double zoom_ = 1.0;
  bool panning_ = false;
};

/// Hosts the retained texture canvas and immutable file/format information.
class TextureInspectorWindow final : public QMainWindow {
public:
  TextureInspectorWindow(QWidget *parent, const QtUiScaleMetrics &metrics)
      : QMainWindow(parent, Qt::Tool),
        canvas_(new TextureInspectorCanvas(this)),
        information_(new QLabel(this)) {
    setObjectName(QStringLiteral("standardMaterialEditor.textureInspector"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    resize(metrics.size(
        QSize(kTextureInspectorWidth, kTextureInspectorHeight)));
    setCentralWidget(canvas_);
    information_->setObjectName(
        QStringLiteral("standardMaterialEditor.textureInspector.info"));
    information_->setAlignment(Qt::AlignCenter);
    statusBar()->addPermanentWidget(information_, 1);
    apply_ui_scale(metrics);
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    canvas_->apply_ui_scale(metrics);
    if (centralWidget() != nullptr) {
      centralWidget()->updateGeometry();
    }
    statusBar()->updateGeometry();
  }

  void present(const QtStandardMaterialTexturePresentation &texture,
               const QString &window_title) {
    setWindowTitle(window_title);
    canvas_->set_image(texture.preview_image);
    const QFileInfo path_info(texture.project_relative_path);
    const QString file_name = path_info.fileName().isEmpty()
                                  ? texture.project_relative_path
                                  : path_info.fileName();
    const QString format = texture.format.isEmpty()
                               ? path_info.suffix().toUpper()
                               : texture.format;
    const QSize pixel_size = texture.pixel_size.isValid()
                                 ? texture.pixel_size
                                 : texture.preview_image.size();
    qint64 byte_size = texture.encoded_byte_size;
    if (byte_size <= 0 && pixel_size.isValid()) {
      byte_size = static_cast<qint64>(pixel_size.width()) *
                  static_cast<qint64>(pixel_size.height()) * 4;
    }
    QStringList parts;
    if (!file_name.isEmpty()) {
      parts.push_back(file_name);
    }
    QString interpretation = format;
    if (!texture.color_space.isEmpty()) {
      interpretation += interpretation.isEmpty()
                            ? texture.color_space
                            : QStringLiteral(" • ") + texture.color_space;
    }
    if (!interpretation.isEmpty()) {
      parts.push_back(interpretation);
    }
    if (pixel_size.isValid()) {
      parts.push_back(QStringLiteral("%1 × %2")
                          .arg(pixel_size.width())
                          .arg(pixel_size.height()));
    }
    if (byte_size > 0) {
      parts.push_back(QLocale().formattedDataSize(byte_size, 1,
                                                  QLocale::DataSizeIecFormat));
    }
    information_->setText(parts.join(QStringLiteral("  |  ")));
  }

private:
  TextureInspectorCanvas *canvas_ = nullptr;
  QLabel *information_ = nullptr;
};

constexpr std::size_t texture_index(QtStandardMaterialTextureSlot slot) {
  return static_cast<std::size_t>(slot);
}

constexpr std::size_t scalar_index(QtStandardMaterialScalar scalar) {
  return static_cast<std::size_t>(scalar);
}

constexpr std::size_t texture_transform_index(
    QtStandardMaterialTextureTransformComponent component) {
  return static_cast<std::size_t>(component);
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
  setWindowFlag(Qt::WindowMaximizeButtonHint, true);
  resize(kDefaultWidth, kDefaultHeight);

  QMenu *file_menu = menuBar()->addMenu(QString());
  file_menu->setObjectName(QStringLiteral("standardMaterialEditor.fileMenu"));
  new_action_ = file_menu->addAction(QString());
  new_action_->setObjectName(QStringLiteral("standardMaterialEditor.file.new"));
  open_action_ = file_menu->addAction(QString());
  open_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.file.open"));
  file_menu->addSeparator();
  save_action_ = file_menu->addAction(QString());
  save_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.file.save"));
  save_as_action_ = file_menu->addAction(QString());
  save_as_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.file.saveAs"));
  file_menu->addSeparator();
  close_action_ = file_menu->addAction(QString());
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

  environment_menu_ = menuBar()->addMenu(QString());
  environment_menu_->setObjectName(
      QStringLiteral("standardMaterialEditor.environmentMenu"));
  rebuild_environment_menu();

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

  diffuse_model_label_ = new QLabel(properties_group_);
  diffuse_model_combo_ = new QComboBox(properties_group_);
  diffuse_model_combo_->setObjectName(
      QStringLiteral("standardMaterialEditor.diffuseModel"));
  diffuse_model_label_->setBuddy(diffuse_model_combo_);
  form->addRow(diffuse_model_label_, diffuse_model_combo_);

  albedo_label_ = new QLabel(properties_group_);
  albedo_button_ = new QtColorSwatchButton(properties_group_);
  albedo_button_->setObjectName(
      QStringLiteral("standardMaterialEditor.albedo"));
  albedo_label_->setBuddy(albedo_button_);
  form->addRow(albedo_label_, albedo_button_);

  const auto create_texture_controls =
      [this, form](
          QtStandardMaterialTextureSlot slot, const QString &stable_name) {
    TextureControls &controls = texture_controls_[texture_index(slot)];
    controls.label = new QLabel(properties_group_);
    auto *row = new QWidget(properties_group_);
    auto *row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    controls.preview = new QToolButton(row);
    controls.preview->setObjectName(
        QStringLiteral("standardMaterialEditor.%1.preview").arg(stable_name));
    controls.preview->setFixedSize(kTexturePreviewExtent,
                                   kTexturePreviewExtent);
    controls.preview->setIconSize(
        QSize(kTexturePreviewIconExtent, kTexturePreviewIconExtent));
    controls.path = new ElidedPathLabel(row);
    controls.path->setObjectName(
        QStringLiteral("standardMaterialEditor.%1.path").arg(stable_name));
    controls.browse = new QToolButton(row);
    controls.browse->setObjectName(
        QStringLiteral("standardMaterialEditor.%1.browse").arg(stable_name));
    controls.clear = new QToolButton(row);
    controls.clear->setObjectName(
        QStringLiteral("standardMaterialEditor.%1.clear").arg(stable_name));
    row_layout->addWidget(controls.preview);
    row_layout->addWidget(controls.path, 1);
    row_layout->addWidget(controls.browse);
    row_layout->addWidget(controls.clear);
    controls.label->setBuddy(controls.browse);
    form->addRow(controls.label, row);
    QObject::connect(
        controls.browse, &QToolButton::clicked, this, [this, slot]() {
          emit_texture_action(slot, QtStandardMaterialTextureAction::Browse);
        });
    QObject::connect(
        controls.clear, &QToolButton::clicked, this, [this, slot]() {
          emit_texture_action(slot, QtStandardMaterialTextureAction::Clear);
        });
    QObject::connect(controls.preview, &QToolButton::clicked, this,
                     [this, slot]() { show_texture_inspector(slot); });
  };
  create_texture_controls(QtStandardMaterialTextureSlot::Albedo,
                          QStringLiteral("albedoTexture"));

  roughness_label_ = new QLabel(properties_group_);
  roughness_spin_ = new QtDoubleSpinBox(properties_group_);
  roughness_spin_->setObjectName(
      QStringLiteral("standardMaterialEditor.roughness"));
  roughness_spin_->setRange(0.0, 1.0);
  roughness_spin_->setDecimals(kScalarDecimals);
  roughness_spin_->setSingleStep(kScalarStep);
  roughness_spin_->setAccelerated(true);
  roughness_slider_ = new QSlider(Qt::Horizontal, properties_group_);
  roughness_slider_->setObjectName(
      QStringLiteral("standardMaterialEditor.roughness.slider"));
  roughness_slider_->setRange(0, kScalarSliderScale);
  roughness_slider_->setSingleStep(1);
  auto *roughness_row = new QWidget(properties_group_);
  auto *roughness_layout = new QHBoxLayout(roughness_row);
  roughness_layout->setContentsMargins(0, 0, 0, 0);
  roughness_layout->addWidget(roughness_slider_, 1);
  roughness_layout->addWidget(roughness_spin_);
  roughness_label_->setBuddy(roughness_slider_);
  form->addRow(roughness_label_, roughness_row);
  create_texture_controls(QtStandardMaterialTextureSlot::Roughness,
                          QStringLiteral("roughnessTexture"));

  metallic_label_ = new QLabel(properties_group_);
  metallic_spin_ = new QtDoubleSpinBox(properties_group_);
  metallic_spin_->setObjectName(
      QStringLiteral("standardMaterialEditor.metallic"));
  metallic_spin_->setRange(0.0, 1.0);
  metallic_spin_->setDecimals(kScalarDecimals);
  metallic_spin_->setSingleStep(kScalarStep);
  metallic_spin_->setAccelerated(true);
  metallic_slider_ = new QSlider(Qt::Horizontal, properties_group_);
  metallic_slider_->setObjectName(
      QStringLiteral("standardMaterialEditor.metallic.slider"));
  metallic_slider_->setRange(0, kScalarSliderScale);
  metallic_slider_->setSingleStep(1);
  auto *metallic_row = new QWidget(properties_group_);
  auto *metallic_layout = new QHBoxLayout(metallic_row);
  metallic_layout->setContentsMargins(0, 0, 0, 0);
  metallic_layout->addWidget(metallic_slider_, 1);
  metallic_layout->addWidget(metallic_spin_);
  metallic_label_->setBuddy(metallic_slider_);
  form->addRow(metallic_label_, metallic_row);
  create_texture_controls(QtStandardMaterialTextureSlot::Metallic,
                          QStringLiteral("metallicTexture"));

  normal_strength_label_ = new QLabel(properties_group_);
  normal_strength_spin_ = new QtDoubleSpinBox(properties_group_);
  normal_strength_spin_->setObjectName(
      QStringLiteral("standardMaterialEditor.normalStrength"));
  normal_strength_spin_->setRange(-16.0, 16.0);
  normal_strength_spin_->setDecimals(kScalarDecimals);
  normal_strength_spin_->setSingleStep(kScalarStep);
  normal_strength_spin_->setAccelerated(true);
  normal_strength_slider_ = new QSlider(Qt::Horizontal, properties_group_);
  normal_strength_slider_->setObjectName(
      QStringLiteral("standardMaterialEditor.normalStrength.slider"));
  normal_strength_slider_->setRange(-16 * kScalarSliderScale,
                                    16 * kScalarSliderScale);
  normal_strength_slider_->setSingleStep(1);
  auto *normal_strength_row = new QWidget(properties_group_);
  auto *normal_strength_layout = new QHBoxLayout(normal_strength_row);
  normal_strength_layout->setContentsMargins(0, 0, 0, 0);
  normal_strength_layout->addWidget(normal_strength_slider_, 1);
  normal_strength_layout->addWidget(normal_strength_spin_);
  normal_strength_label_->setBuddy(normal_strength_slider_);
  form->addRow(normal_strength_label_, normal_strength_row);
  create_texture_controls(QtStandardMaterialTextureSlot::Normal,
                          QStringLiteral("normalTexture"));
  refresh_texture_palette_icons();

  direct_x_normal_ = new QCheckBox(properties_group_);
  direct_x_normal_->setObjectName(
      QStringLiteral("standardMaterialEditor.directXNormal"));
  form->addRow(direct_x_normal_);
  properties_layout->addWidget(properties_group_);

  texture_transform_group_ = new QGroupBox(properties_content);
  texture_transform_group_->setObjectName(
      QStringLiteral("standardMaterialEditor.textureTransform"));
  auto *transform_form = new QFormLayout(texture_transform_group_);
  const auto create_transform_controls =
      [this, transform_form](
          QtStandardMaterialTextureTransformComponent component,
          const QString &stable_name, double minimum, double maximum) {
        const std::size_t index = static_cast<std::size_t>(component);
        TextureTransformControls &controls = texture_transform_controls_[index];
        controls.label = new QLabel(texture_transform_group_);
        controls.spin = new QtDoubleSpinBox(texture_transform_group_);
        controls.spin->setObjectName(
            QStringLiteral("standardMaterialEditor.textureTransform.%1")
                .arg(stable_name));
        controls.spin->setRange(minimum, maximum);
        controls.spin->setDecimals(kScalarDecimals);
        controls.spin->setSingleStep(kScalarStep);
        controls.spin->setAccelerated(true);
        controls.slider = new QSlider(Qt::Horizontal, texture_transform_group_);
        controls.slider->setObjectName(
            QStringLiteral("standardMaterialEditor.textureTransform.%1.slider")
                .arg(stable_name));
        controls.slider->setRange(
            static_cast<int>(std::lround(minimum * kScalarSliderScale)),
            static_cast<int>(std::lround(maximum * kScalarSliderScale)));
        controls.slider->setSingleStep(1);
        auto *row = new QWidget(texture_transform_group_);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(controls.slider, 1);
        layout->addWidget(controls.spin);
        controls.label->setBuddy(controls.slider);
        transform_form->addRow(controls.label, row);

        controls.live_dispatch.timer = new QTimer(row);
        controls.live_dispatch.timer->setObjectName(
            controls.slider->objectName() +
            QStringLiteral(".dispatchTimer"));
        controls.live_dispatch.timer->setSingleShot(true);
        controls.live_dispatch.timer->setInterval(
            kLiveNumberDispatchIntervalMs);
        QObject::connect(
            controls.live_dispatch.timer, &QTimer::timeout, this,
            [this, component]() { flush_texture_transform_edit(component); });
        QObject::connect(
            controls.slider, &QSlider::sliderReleased, this,
            [this, component]() { flush_texture_transform_edit(component); });
        QObject::connect(
            controls.spin, &QDoubleSpinBox::valueChanged, this,
            [this, component, slider = controls.slider](double value) {
              {
                const QSignalBlocker blocker(slider);
                slider->setValue(
                    static_cast<int>(std::lround(value * kScalarSliderScale)));
              }
              dispatch_texture_transform_edit(component, value, false);
            });
        QObject::connect(
            controls.slider, &QSlider::valueChanged, this,
            [this, component, spin = controls.spin,
             slider = controls.slider](int slider_value) {
              const double value =
                  static_cast<double>(slider_value) / kScalarSliderScale;
              {
                const QSignalBlocker blocker(spin);
                spin->setValue(value);
              }
              dispatch_texture_transform_edit(component, value,
                                              slider->isSliderDown());
            });
      };
  create_transform_controls(
      QtStandardMaterialTextureTransformComponent::CenterU,
      QStringLiteral("centerU"), 0.0, 1.0);
  create_transform_controls(
      QtStandardMaterialTextureTransformComponent::CenterV,
      QStringLiteral("centerV"), 0.0, 1.0);
  create_transform_controls(QtStandardMaterialTextureTransformComponent::ScaleU,
                            QStringLiteral("scaleU"), -16.0, 16.0);
  create_transform_controls(QtStandardMaterialTextureTransformComponent::ScaleV,
                            QStringLiteral("scaleV"), -16.0, 16.0);
  create_transform_controls(
      QtStandardMaterialTextureTransformComponent::OffsetU,
      QStringLiteral("offsetU"), -16.0, 16.0);
  create_transform_controls(
      QtStandardMaterialTextureTransformComponent::OffsetV,
      QStringLiteral("offsetV"), -16.0, 16.0);
  create_transform_controls(
      QtStandardMaterialTextureTransformComponent::RotationDegrees,
      QStringLiteral("rotation"), -360.0, 360.0);
  properties_layout->addWidget(texture_transform_group_);
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
  const auto configure_scalar_live_dispatch =
      [this](QtStandardMaterialScalar scalar, QSlider *slider) {
        LiveNumberDispatch &live_dispatch =
            scalar_live_dispatches_[scalar_index(scalar)];
        live_dispatch.timer = new QTimer(slider);
        live_dispatch.timer->setObjectName(
            slider->objectName() + QStringLiteral(".dispatchTimer"));
        live_dispatch.timer->setSingleShot(true);
        live_dispatch.timer->setInterval(kLiveNumberDispatchIntervalMs);
        QObject::connect(live_dispatch.timer, &QTimer::timeout, this,
                         [this, scalar]() { flush_scalar_edit(scalar); });
        QObject::connect(slider, &QSlider::sliderReleased, this,
                         [this, scalar]() { flush_scalar_edit(scalar); });
      };
  configure_scalar_live_dispatch(QtStandardMaterialScalar::Roughness,
                                 roughness_slider_);
  configure_scalar_live_dispatch(QtStandardMaterialScalar::Metallic,
                                 metallic_slider_);
  configure_scalar_live_dispatch(QtStandardMaterialScalar::NormalStrength,
                                 normal_strength_slider_);
  QObject::connect(
      roughness_spin_, &QDoubleSpinBox::valueChanged, this,
      [this](double value) {
        {
          const QSignalBlocker blocker(roughness_slider_);
          roughness_slider_->setValue(
              static_cast<int>(std::lround(value * kScalarSliderScale)));
        }
        dispatch_scalar_edit(QtStandardMaterialScalar::Roughness, value,
                             false);
      });
  QObject::connect(roughness_slider_, &QSlider::valueChanged, this,
                   [this](int slider_value) {
                     const double value =
                         static_cast<double>(slider_value) / kScalarSliderScale;
                      {
                        const QSignalBlocker blocker(roughness_spin_);
                        roughness_spin_->setValue(value);
                      }
                      dispatch_scalar_edit(
                          QtStandardMaterialScalar::Roughness, value,
                          roughness_slider_->isSliderDown());
                    });
  QObject::connect(
      metallic_spin_, &QDoubleSpinBox::valueChanged, this,
      [this](double value) {
        {
          const QSignalBlocker blocker(metallic_slider_);
          metallic_slider_->setValue(
              static_cast<int>(std::lround(value * kScalarSliderScale)));
        }
        dispatch_scalar_edit(QtStandardMaterialScalar::Metallic, value,
                             false);
      });
  QObject::connect(
      metallic_slider_, &QSlider::valueChanged, this, [this](int slider_value) {
        const double value =
            static_cast<double>(slider_value) / kScalarSliderScale;
        {
          const QSignalBlocker blocker(metallic_spin_);
          metallic_spin_->setValue(value);
        }
        dispatch_scalar_edit(QtStandardMaterialScalar::Metallic, value,
                             metallic_slider_->isSliderDown());
      });
  QObject::connect(
      normal_strength_spin_, &QDoubleSpinBox::valueChanged, this,
      [this](double value) {
        {
          const QSignalBlocker blocker(normal_strength_slider_);
          normal_strength_slider_->setValue(
              static_cast<int>(std::lround(value * kScalarSliderScale)));
        }
        dispatch_scalar_edit(QtStandardMaterialScalar::NormalStrength, value,
                             false);
      });
  QObject::connect(normal_strength_slider_, &QSlider::valueChanged, this,
                   [this](int slider_value) {
                     const double value =
                         static_cast<double>(slider_value) / kScalarSliderScale;
                      {
                        const QSignalBlocker blocker(normal_strength_spin_);
                        normal_strength_spin_->setValue(value);
                      }
                      dispatch_scalar_edit(
                          QtStandardMaterialScalar::NormalStrength, value,
                          normal_strength_slider_->isSliderDown());
                    });
  QObject::connect(direct_x_normal_, &QCheckBox::toggled, this,
                   [this](bool direct_x) {
                     if (!refreshing_ && normal_convention_edited_) {
                       normal_convention_edited_(direct_x);
                     }
                   });
  QObject::connect(diffuse_model_combo_, &QComboBox::activated, this,
                   [this](int index) {
                     if (!refreshing_ && has_document_ && index >= 0 &&
                         diffuse_model_edited_) {
                       diffuse_model_edited_(static_cast<QtStandardMaterialDiffuseModel>(
                           diffuse_model_combo_->itemData(index).toInt()));
                     }
                   });

  refresh_labels();
  set_preview_mesh(QtStandardMaterialPreviewMesh::Sphere, false);
  set_preview_shading(QtStandardMaterialPreviewShading::Rendered, false);
  refresh_document_controls();
}

QtStandardMaterialEditorWindow::~QtStandardMaterialEditorWindow() = default;

void QtStandardMaterialEditorWindow::dispatch_scalar_edit(
    QtStandardMaterialScalar scalar, double value, bool coalesce) {
  if (refreshing_) {
    return;
  }

  LiveNumberDispatch &live_dispatch =
      scalar_live_dispatches_[scalar_index(scalar)];
  if (coalesce) {
    live_dispatch.pending_value = value;
    if (live_dispatch.timer != nullptr &&
        !live_dispatch.timer->isActive()) {
      live_dispatch.timer->start();
    }
    return;
  }

  live_dispatch.pending_value.reset();
  if (live_dispatch.timer != nullptr) {
    live_dispatch.timer->stop();
  }
  if (scalar_edited_) {
    scalar_edited_(scalar, value);
  }
}

void QtStandardMaterialEditorWindow::flush_scalar_edit(
    QtStandardMaterialScalar scalar) {
  LiveNumberDispatch &live_dispatch =
      scalar_live_dispatches_[scalar_index(scalar)];
  if (!live_dispatch.pending_value.has_value()) {
    return;
  }

  const double value = *live_dispatch.pending_value;
  live_dispatch.pending_value.reset();
  if (live_dispatch.timer != nullptr) {
    live_dispatch.timer->stop();
  }
  if (!refreshing_ && scalar_edited_) {
    scalar_edited_(scalar, value);
  }
}

void QtStandardMaterialEditorWindow::dispatch_texture_transform_edit(
    QtStandardMaterialTextureTransformComponent component, double value,
    bool coalesce) {
  if (refreshing_) {
    return;
  }

  LiveNumberDispatch &live_dispatch =
      texture_transform_controls_[texture_transform_index(component)]
          .live_dispatch;
  if (coalesce) {
    live_dispatch.pending_value = value;
    if (live_dispatch.timer != nullptr &&
        !live_dispatch.timer->isActive()) {
      live_dispatch.timer->start();
    }
    return;
  }

  live_dispatch.pending_value.reset();
  if (live_dispatch.timer != nullptr) {
    live_dispatch.timer->stop();
  }
  if (texture_transform_edited_) {
    texture_transform_edited_(component, value);
  }
}

void QtStandardMaterialEditorWindow::flush_texture_transform_edit(
    QtStandardMaterialTextureTransformComponent component) {
  LiveNumberDispatch &live_dispatch =
      texture_transform_controls_[texture_transform_index(component)]
          .live_dispatch;
  if (!live_dispatch.pending_value.has_value()) {
    return;
  }

  const double value = *live_dispatch.pending_value;
  live_dispatch.pending_value.reset();
  if (live_dispatch.timer != nullptr) {
    live_dispatch.timer->stop();
  }
  if (!refreshing_ && texture_transform_edited_) {
    texture_transform_edited_(component, value);
  }
}

bool QtStandardMaterialEditorWindow::cancel_pending_number_edits() {
  bool canceled = false;
  const auto cancel = [&canceled](LiveNumberDispatch &live_dispatch) {
    canceled = live_dispatch.pending_value.has_value() || canceled;
    live_dispatch.pending_value.reset();
    if (live_dispatch.timer != nullptr) {
      live_dispatch.timer->stop();
    }
  };
  for (LiveNumberDispatch &live_dispatch : scalar_live_dispatches_) {
    cancel(live_dispatch);
  }
  for (TextureTransformControls &controls : texture_transform_controls_) {
    cancel(controls.live_dispatch);
  }
  return canceled;
}

void QtStandardMaterialEditorWindow::flush_pending_number_edits() {
  std::array<std::optional<double>, kScalarCount> scalar_values;
  std::array<std::optional<double>, kTextureTransformCount> transform_values;
  const auto take = [](LiveNumberDispatch &live_dispatch) {
    std::optional<double> pending_value =
        std::exchange(live_dispatch.pending_value, std::nullopt);
    if (live_dispatch.timer != nullptr) {
      live_dispatch.timer->stop();
    }
    return pending_value;
  };
  for (std::size_t index = 0; index < scalar_values.size(); ++index) {
    scalar_values[index] = take(scalar_live_dispatches_[index]);
  }
  for (std::size_t index = 0; index < transform_values.size(); ++index) {
    transform_values[index] =
        take(texture_transform_controls_[index].live_dispatch);
  }

  const bool expected_has_document = has_document_;
  const QString expected_document_id = document_.stable_id;
  const auto document_is_unchanged = [this, expected_has_document,
                                      expected_document_id]() {
    return has_document_ == expected_has_document &&
           (!expected_has_document ||
            document_.stable_id == expected_document_id);
  };
  for (std::size_t index = 0; index < scalar_values.size(); ++index) {
    if (!scalar_values[index].has_value() || !document_is_unchanged()) {
      continue;
    }
    if (scalar_edited_) {
      scalar_edited_(static_cast<QtStandardMaterialScalar>(index),
                     *scalar_values[index]);
    }
  }
  for (std::size_t index = 0; index < transform_values.size(); ++index) {
    if (!transform_values[index].has_value() || !document_is_unchanged()) {
      continue;
    }
    if (texture_transform_edited_) {
      texture_transform_edited_(
          static_cast<QtStandardMaterialTextureTransformComponent>(index),
          *transform_values[index]);
    }
  }
}

void QtStandardMaterialEditorWindow::set_labels(
    QtStandardMaterialEditorLabels labels) {
  labels_ = std::move(labels);
  refresh_labels();
}

void QtStandardMaterialEditorWindow::set_document(
    QtStandardMaterialEditorDocument document) {
  const bool canceled_pending_edit = cancel_pending_number_edits();
  if (has_document_ && document_ == document) {
    if (canceled_pending_edit) {
      refresh_document_controls();
    }
    return;
  }
  const bool changed_document =
      has_document_ && (document_.stable_id != document.stable_id ||
                       document_.session_generation != document.session_generation);
  if (changed_document) {
    abandon_interactions();
  }
  bool inspected_texture_changed = false;
  if (has_document_ && !changed_document &&
      inspected_texture_slot_.has_value()) {
    const std::size_t index = texture_index(*inspected_texture_slot_);
    inspected_texture_changed =
        index >= document_.textures.size() ||
        index >= document.textures.size() ||
        !(document_.textures[index] == document.textures[index]);
  }
  document_ = std::move(document);
  has_document_ = true;
  if (changed_document && texture_inspector_window_ != nullptr) {
    texture_inspector_window_->hide();
    inspected_texture_slot_.reset();
  }
  refresh_document_controls();
  if (!changed_document && inspected_texture_changed) {
    refresh_texture_inspector();
  }
}

void QtStandardMaterialEditorWindow::clear_document() {
  abandon_interactions();
  (void)cancel_pending_number_edits();
  document_ = {};
  has_document_ = false;
  if (texture_inspector_window_ != nullptr) {
    texture_inspector_window_->hide();
  }
  inspected_texture_slot_.reset();
  refresh_document_controls();
}

void QtStandardMaterialEditorWindow::abandon_interactions() {
  ++interaction_generation_;
  if (active_albedo_dialog_ != nullptr) {
    const QPointer<QColorDialog> dialog = active_albedo_dialog_;
    active_albedo_dialog_.clear();
    dialog->reject();
  }
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

void QtStandardMaterialEditorWindow::set_albedo_preview_begin_callback(
    AlbedoPreviewBeginCallback callback) {
  albedo_preview_begin_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_albedo_preview_finish_callback(
    AlbedoPreviewFinishCallback callback) {
  albedo_preview_finish_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_scalar_edited_callback(
    ScalarEditedCallback callback) {
  scalar_edited_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_normal_convention_edited_callback(
    NormalConventionEditedCallback callback) {
  normal_convention_edited_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_diffuse_model_edited_callback(
    DiffuseModelEditedCallback callback) {
  diffuse_model_edited_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_texture_action_callback(
    TextureActionCallback callback) {
  texture_action_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_texture_transform_edited_callback(
    TextureTransformEditedCallback callback) {
  texture_transform_edited_ = std::move(callback);
}

void QtStandardMaterialEditorWindow::set_preview_environments(
    std::vector<QtStandardMaterialPreviewEnvironment> environments) {
  if (preview_environments_ == environments) {
    return;
  }
  preview_environments_ = std::move(environments);
  if (!preview_environments_.empty() &&
      std::ranges::none_of(
          preview_environments_, [this](const auto &environment) {
            return environment.stable_id == preview_environment_;
          })) {
    preview_environment_ = preview_environments_.front().stable_id;
  }
  rebuild_environment_menu();
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

void QtStandardMaterialEditorWindow::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (ui_scale_metrics_.revision() == metrics.revision() &&
      ui_scale_metrics_.factor() == metrics.factor()) {
    return;
  }
  ui_scale_metrics_ = metrics;
  const QSize preview_extent =
      metrics.size(QSize(kTexturePreviewExtent, kTexturePreviewExtent));
  const QSize preview_icon_extent = metrics.size(
      QSize(kTexturePreviewIconExtent, kTexturePreviewIconExtent));
  for (TextureControls &controls : texture_controls_) {
    controls.preview->setFixedSize(preview_extent);
    controls.preview->setIconSize(preview_icon_extent);
    static_cast<ElidedPathLabel *>(controls.path)->apply_ui_scale(metrics);
  }
  invalidate_texture_preview_icons();
  refresh_document_controls();
  if (texture_inspector_window_ != nullptr) {
    static_cast<TextureInspectorWindow *>(texture_inspector_window_)
        ->apply_ui_scale(metrics);
  }
  properties_group_->updateGeometry();
  updateGeometry();
}

void QtStandardMaterialEditorWindow::restore_presentation_state(
    QtStandardMaterialPreviewMesh mesh,
    QtStandardMaterialPreviewShading shading, QString environment_id,
    bool show_environment_background) {
  set_preview_mesh(mesh, false);
  set_preview_shading(shading, false);
  set_preview_environment(std::move(environment_id), false);
  set_preview_environment_background(show_environment_background, false);
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
  preview_widget_->setToolTip(labels_.preview_navigation);
  preview_widget_->setAccessibleDescription(labels_.preview_navigation);
  preview_surface_layout_->addWidget(preview_widget_, 1);
  preview_placeholder_->hide();
  preview_widget_->show();
}

bool QtStandardMaterialEditorWindow::event(QEvent *event) {
  const bool handled = QMainWindow::event(event);
  if (event != nullptr) {
    if (event->type() == QEvent::DevicePixelRatioChange) {
      invalidate_texture_preview_icons();
      refresh_document_controls();
    } else if (event->type() == QEvent::PaletteChange ||
               event->type() == QEvent::ApplicationPaletteChange) {
      refresh_texture_palette_icons();
    }
  }
  return handled;
}

void QtStandardMaterialEditorWindow::closeEvent(QCloseEvent *event) {
  if (active_albedo_dialog_ != nullptr) {
    abandon_interactions();
    if (albedo_preview_finish_) {
      albedo_preview_finish_(false);
    }
  }
  flush_pending_number_edits();
  if (close_request_handler_ && !close_request_handler_()) {
    event->ignore();
    return;
  }
  if (texture_inspector_window_ != nullptr) {
    texture_inspector_window_->hide();
  }
  inspected_texture_slot_.reset();
  QMainWindow::closeEvent(event);
}

void QtStandardMaterialEditorWindow::set_preview_mesh(
    QtStandardMaterialPreviewMesh mesh, bool notify) {
  const bool changed = preview_mesh_ != mesh;
  preview_mesh_ = mesh;
  const std::size_t index = mesh_index(mesh);
  if (index < preview_mesh_actions_.size() &&
      preview_mesh_actions_[index] != nullptr) {
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

void QtStandardMaterialEditorWindow::set_preview_environment(
    QString environment_id, bool notify) {
  const auto found = std::ranges::find_if(
      preview_environments_, [&environment_id](const auto &environment) {
        return environment.stable_id == environment_id;
      });
  if (!preview_environments_.empty() && found == preview_environments_.end()) {
    return;
  }
  const bool changed = preview_environment_ != environment_id;
  preview_environment_ = std::move(environment_id);
  for (QAction *action : environment_actions_) {
    if (action != nullptr) {
      action->setChecked(action->data().toString() == preview_environment_);
    }
  }
  if (notify && changed && presentation_state_changed_) {
    presentation_state_changed_();
  }
}

void QtStandardMaterialEditorWindow::set_preview_environment_background(
    bool visible, bool notify) {
  const bool changed =
      preview_environment_background_visible_ != visible;
  preview_environment_background_visible_ = visible;
  if (show_environment_background_action_ != nullptr) {
    const QSignalBlocker blocker(show_environment_background_action_);
    show_environment_background_action_->setChecked(visible);
  }
  if (notify && changed && presentation_state_changed_) {
    presentation_state_changed_();
  }
}

void QtStandardMaterialEditorWindow::rebuild_environment_menu() {
  if (environment_menu_ == nullptr) {
    return;
  }
  environment_menu_->clear();
  if (environment_action_group_ != nullptr) {
    delete environment_action_group_;
  }
  environment_action_group_ = new QActionGroup(environment_menu_);
  environment_action_group_->setExclusive(true);
  environment_actions_.clear();
  environment_actions_.reserve(preview_environments_.size());
  for (const QtStandardMaterialPreviewEnvironment &environment :
       preview_environments_) {
    QAction *action = environment_menu_->addAction(environment.display_name);
    action->setObjectName(
        QStringLiteral("standardMaterialEditor.environment.%1")
            .arg(environment.stable_id));
    action->setData(environment.stable_id);
    action->setCheckable(true);
    action->setChecked(environment.stable_id == preview_environment_);
    environment_action_group_->addAction(action);
    environment_actions_.push_back(action);
    QObject::connect(action, &QAction::triggered, this,
                     [this, stable_id = environment.stable_id]() {
                       set_preview_environment(stable_id, true);
                     });
  }
  if (!preview_environments_.empty()) {
    environment_menu_->addSeparator();
  }
  show_environment_background_action_ = environment_menu_->addAction(
      labels_.show_environment_background);
  show_environment_background_action_->setObjectName(
      QStringLiteral("standardMaterialEditor.environment.showBackground"));
  show_environment_background_action_->setCheckable(true);
  show_environment_background_action_->setChecked(
      preview_environment_background_visible_);
  QObject::connect(
      show_environment_background_action_, &QAction::toggled, this,
      [this](bool visible) {
        set_preview_environment_background(visible, true);
      });
}

void QtStandardMaterialEditorWindow::refresh_document_controls() {
  refreshing_ = true;
  properties_group_->setEnabled(has_document_);
  texture_transform_group_->setEnabled(has_document_);
  save_action_->setEnabled(has_document_);
  save_as_action_->setEnabled(has_document_);

  const QSignalBlocker name_blocker(name_edit_);
  const QSignalBlocker roughness_blocker(roughness_spin_);
  const QSignalBlocker roughness_slider_blocker(roughness_slider_);
  const QSignalBlocker metallic_blocker(metallic_spin_);
  const QSignalBlocker metallic_slider_blocker(metallic_slider_);
  const QSignalBlocker normal_strength_blocker(normal_strength_spin_);
  const QSignalBlocker normal_strength_slider_blocker(normal_strength_slider_);
  const QSignalBlocker convention_blocker(direct_x_normal_);
  const QSignalBlocker diffuse_model_blocker(diffuse_model_combo_);
  const QIcon no_texture_icon =
      style()->standardIcon(QStyle::SP_FileIcon, nullptr, this);
  name_edit_->setText(has_document_ ? document_.name : QString());
  albedo_button_->set_swatch_color(has_document_ ? document_.albedo
                                                 : QColor(Qt::white));
  roughness_spin_->setValue(has_document_ ? document_.roughness : 1.0);
  roughness_slider_->setValue(static_cast<int>(std::lround(
      (has_document_ ? document_.roughness : 1.0) * kScalarSliderScale)));
  metallic_spin_->setValue(has_document_ ? document_.metallic : 0.0);
  metallic_slider_->setValue(static_cast<int>(std::lround(
      (has_document_ ? document_.metallic : 0.0) * kScalarSliderScale)));
  normal_strength_spin_->setValue(has_document_ ? document_.normal_strength
                                                : 1.0);
  normal_strength_slider_->setValue(static_cast<int>(std::lround(
      (has_document_ ? document_.normal_strength : 1.0) * kScalarSliderScale)));
  direct_x_normal_->setChecked(has_document_ && document_.direct_x_normal);
  const int diffuse_model = static_cast<int>(
      has_document_ ? document_.diffuse_model : QtStandardMaterialDiffuseModel::Burley);
  if (diffuse_model_combo_->currentData().toInt() != diffuse_model ||
      diffuse_model_combo_->currentIndex() < 0) {
    diffuse_model_combo_->setCurrentIndex(diffuse_model_combo_->findData(diffuse_model));
  }
  for (std::size_t index = 0; index < kTextureSlotCount; ++index) {
    TextureControls &controls = texture_controls_[index];
    const QtStandardMaterialTexturePresentation presentation =
        has_document_ ? document_.textures[index]
                      : QtStandardMaterialTexturePresentation{};
    static_cast<ElidedPathLabel *>(controls.path)
        ->set_full_text(presentation.project_relative_path, labels_.no_texture);
    QString path_tooltip = presentation.project_relative_path;
    if (!presentation.diagnostic.isEmpty()) {
      if (!path_tooltip.isEmpty()) {
        path_tooltip += QStringLiteral("\n");
      }
      path_tooltip += presentation.diagnostic;
    }
    controls.path->setToolTip(path_tooltip);
    controls.path->setAccessibleDescription(presentation.diagnostic);
    const bool has_preview_image = !presentation.preview_image.isNull();
    const bool preview_icon_changed =
        !controls.preview_icon_initialized ||
        controls.preview_icon_document_id != document_.stable_id ||
        controls.preview_icon_pixel_size != presentation.pixel_size ||
        controls.preview_icon_content_revision !=
            presentation.content_revision ||
        controls.preview_icon_has_image != has_preview_image;
    if (preview_icon_changed) {
      if (has_preview_image) {
        const qreal device_pixel_ratio =
            std::max<qreal>(1.0, controls.preview->devicePixelRatioF());
        const int device_extent = ui_scale_metrics_.device_pixels(
            kTexturePreviewIconExtent, device_pixel_ratio);
        QPixmap pixmap = QPixmap::fromImage(presentation.preview_image.scaled(
            device_extent, device_extent, Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
        pixmap.setDevicePixelRatio(device_pixel_ratio);
        controls.preview->setIcon(QIcon(pixmap));
      } else {
        controls.preview->setIcon(no_texture_icon);
      }
      controls.preview_icon_document_id = document_.stable_id;
      controls.preview_icon_pixel_size = presentation.pixel_size;
      controls.preview_icon_content_revision = presentation.content_revision;
      controls.preview_icon_has_image = has_preview_image;
      controls.preview_icon_initialized = true;
    }
    controls.preview->setEnabled(has_document_ &&
                                 !presentation.preview_image.isNull());
    QString preview_tooltip = labels_.inspect_texture;
    if (!presentation.project_relative_path.isEmpty()) {
      preview_tooltip +=
          QStringLiteral("\n") + presentation.project_relative_path;
    }
    controls.preview->setToolTip(preview_tooltip);
    controls.clear->setEnabled(has_document_ &&
                               !presentation.project_relative_path.isEmpty());
    controls.browse->setEnabled(has_document_);
  }

  const QtStandardMaterialTextureTransform transform =
      has_document_ ? document_.texture_transform
                    : QtStandardMaterialTextureTransform{};
  const std::array<double, 7> transform_values{
      transform.center_u,        transform.center_v, transform.scale_u,
      transform.scale_v,         transform.offset_u, transform.offset_v,
      transform.rotation_degrees};
  for (std::size_t index = 0; index < transform_values.size(); ++index) {
    TextureTransformControls &controls = texture_transform_controls_[index];
    const QSignalBlocker spin_blocker(controls.spin);
    const QSignalBlocker slider_blocker(controls.slider);
    controls.spin->setValue(transform_values[index]);
    controls.slider->setValue(static_cast<int>(
        std::lround(transform_values[index] * kScalarSliderScale)));
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
  if (environment_menu_ != nullptr) {
    environment_menu_->setTitle(labels_.environment_menu);
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
  if (show_environment_background_action_ != nullptr) {
    show_environment_background_action_->setText(
        labels_.show_environment_background);
  }
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
  preview_surface_host_->setToolTip(labels_.preview_navigation);
  preview_surface_host_->setAccessibleDescription(labels_.preview_navigation);
  if (preview_widget_ != nullptr) {
    preview_widget_->setToolTip(labels_.preview_navigation);
    preview_widget_->setAccessibleDescription(labels_.preview_navigation);
  }
  preview_placeholder_->setText(labels_.preview_unavailable);
  properties_group_->setTitle(labels_.properties_group);
  name_label_->setText(labels_.name);
  diffuse_model_label_->setText(labels_.diffuse_model);
  diffuse_model_combo_->setAccessibleName(labels_.diffuse_model);
  {
    const QSignalBlocker blocker(diffuse_model_combo_);
    if (diffuse_model_combo_->count() == 0) {
      diffuse_model_combo_->addItem(labels_.diffuse_burley,
          static_cast<int>(QtStandardMaterialDiffuseModel::Burley));
      diffuse_model_combo_->addItem(labels_.diffuse_lambert,
          static_cast<int>(QtStandardMaterialDiffuseModel::Lambert));
    } else {
      diffuse_model_combo_->setItemText(0, labels_.diffuse_burley);
      diffuse_model_combo_->setItemText(1, labels_.diffuse_lambert);
    }
  }
  albedo_label_->setText(labels_.albedo);
  albedo_button_->setText({});
  albedo_button_->setAccessibleName(labels_.albedo);
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
    controls.preview->setAccessibleName(labels_.inspect_texture);
    controls.browse->setToolTip(labels_.browse_texture);
    controls.browse->setAccessibleName(labels_.browse_texture);
    controls.clear->setToolTip(labels_.clear_texture);
    controls.clear->setAccessibleName(labels_.clear_texture);
  }
  texture_transform_group_->setTitle(labels_.texture_transform);
  if (texture_transform_group_->toolTip() != labels_.texture_transform_tooltip) {
    texture_transform_group_->setToolTip(labels_.texture_transform_tooltip);
  }
  const std::array<QString, 7> transform_labels{
      labels_.texture_center + QStringLiteral(" U"),
      labels_.texture_center + QStringLiteral(" V"),
      labels_.texture_scale + QStringLiteral(" U"),
      labels_.texture_scale + QStringLiteral(" V"),
      labels_.texture_offset + QStringLiteral(" U"),
      labels_.texture_offset + QStringLiteral(" V"),
      labels_.texture_rotation};
  const std::array<QString, 7> transform_tooltips{
      labels_.texture_center_tooltip, labels_.texture_center_tooltip,
      labels_.texture_scale_tooltip, labels_.texture_scale_tooltip,
      labels_.texture_offset_tooltip, labels_.texture_offset_tooltip,
      labels_.texture_rotation_tooltip};
  for (std::size_t index = 0; index < transform_labels.size(); ++index) {
    TextureTransformControls &controls = texture_transform_controls_[index];
    if (controls.label->text() != transform_labels[index]) {
      controls.label->setText(transform_labels[index]);
    }
    if (controls.spin->accessibleName() != transform_labels[index]) {
      controls.spin->setAccessibleName(transform_labels[index]);
      controls.slider->setAccessibleName(transform_labels[index]);
    }
    if (controls.spin->toolTip() != transform_tooltips[index]) {
      controls.label->setToolTip(transform_tooltips[index]);
      controls.spin->setToolTip(transform_tooltips[index]);
      controls.slider->setToolTip(transform_tooltips[index]);
    }
  }
  refresh_document_controls();
  refresh_texture_inspector();
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
  if (!has_document_ || active_albedo_dialog_ != nullptr) {
    return;
  }
  flush_pending_number_edits();
  const std::uint64_t generation = interaction_generation_;
  if (albedo_preview_begin_ && !albedo_preview_begin_()) {
    return;
  }
  if (generation != interaction_generation_ || !has_document_) {
    return;
  }
  const QColor original = albedo_button_->swatch_color();
  QColorDialog dialog(original, this);
  active_albedo_dialog_ = &dialog;
  dialog.setObjectName(QStringLiteral("standardMaterialEditor.albedoDialog"));
  dialog.setWindowTitle(labels_.choose_albedo_title);
  dialog.setOption(QColorDialog::DontUseNativeDialog, true);
  QObject::connect(&dialog, &QColorDialog::currentColorChanged, this,
                   [this, generation](const QColor &color) {
    if (generation != interaction_generation_ || !color.isValid()) {
      return;
    }
    albedo_button_->set_swatch_color(color);
    if (albedo_edited_) {
      albedo_edited_(color);
    }
  });
  const bool accepted = dialog.exec() == QDialog::Accepted;
  if (generation != interaction_generation_) {
    return;
  }
  active_albedo_dialog_.clear();
  if (albedo_preview_finish_) {
    albedo_preview_finish_(accepted);
  }
  if (generation == interaction_generation_ && !accepted) {
    albedo_button_->set_swatch_color(original);
  }
}

void QtStandardMaterialEditorWindow::emit_file_action(
    QtStandardMaterialEditorFileAction action) {
  flush_pending_number_edits();
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

void QtStandardMaterialEditorWindow::show_texture_inspector(
    QtStandardMaterialTextureSlot slot) {
  if (!has_document_) {
    return;
  }
  const std::size_t index = texture_index(slot);
  if (index >= document_.textures.size() ||
      document_.textures[index].preview_image.isNull()) {
    return;
  }
  if (texture_inspector_window_ == nullptr) {
    texture_inspector_window_ =
        new TextureInspectorWindow(this, ui_scale_metrics_);
  }
  inspected_texture_slot_ = slot;
  static_cast<TextureInspectorWindow *>(texture_inspector_window_)
      ->present(document_.textures[index], labels_.texture_inspector);
  texture_inspector_window_->show();
  texture_inspector_window_->raise();
  texture_inspector_window_->activateWindow();
}

void QtStandardMaterialEditorWindow::refresh_texture_inspector() {
  if (texture_inspector_window_ == nullptr ||
      !inspected_texture_slot_.has_value()) {
    return;
  }
  const std::size_t index = texture_index(*inspected_texture_slot_);
  if (!has_document_ || index >= document_.textures.size() ||
      document_.textures[index].preview_image.isNull()) {
    texture_inspector_window_->hide();
    inspected_texture_slot_.reset();
    return;
  }
  static_cast<TextureInspectorWindow *>(texture_inspector_window_)
      ->present(document_.textures[index], labels_.texture_inspector);
}

void QtStandardMaterialEditorWindow::invalidate_texture_preview_icons() {
  for (TextureControls &controls : texture_controls_) {
    controls.preview_icon_initialized = false;
  }
}

void QtStandardMaterialEditorWindow::refresh_texture_palette_icons() {
  const QIcon browse_icon =
      style()->standardIcon(QStyle::SP_DialogOpenButton, nullptr, this);
  const QIcon clear_icon =
      style()->standardIcon(QStyle::SP_DialogCancelButton, nullptr, this);
  const QIcon no_texture_icon =
      style()->standardIcon(QStyle::SP_FileIcon, nullptr, this);
  for (TextureControls &controls : texture_controls_) {
    if (controls.browse != nullptr) {
      controls.browse->setIcon(browse_icon);
    }
    if (controls.clear != nullptr) {
      controls.clear->setIcon(clear_icon);
    }
    if (controls.preview != nullptr && controls.preview_icon_initialized &&
        !controls.preview_icon_has_image) {
      controls.preview->setIcon(no_texture_icon);
    }
  }
}

} // namespace quader
