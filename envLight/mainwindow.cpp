#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QImage>
#include <QPixmap>
#include "OpenGLSceneWidget.h"
#include "environment_light.h"
#include <memory>
#include <cmath>
#include <QMenuBar>
#include <QMenu>
#include <QAction>

#define M_PI 3.14159265358979323846

// ================================================================
// 生成环境光全景图（带 Reinhard 色调映射和曝光控制）
// ================================================================
Image generateEnvironmentPanorama(const EnvironmentLight& model,
    int width, int height,
    double exposure) {
    Image img(width, height);
    for (int y = 0; y < height; ++y) {
        double v = double(y) / (height - 1);
        double theta = M_PI * v;
        for (int x = 0; x < width; ++x) {
            double u = double(x) / (width - 1);
            double phi = 2.0 * M_PI * u;
            Direction dir{ theta, phi };
            auto sample = model.sample(dir);
            // 应用曝光
            double r = sample.color.r * sample.radiance * exposure;
            double g = sample.color.g * sample.radiance * exposure;
            double b = sample.color.b * sample.radiance * exposure;
            // Reinhard 色调映射
            double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            double scale = 1.0 / (1.0 + lum);
            r *= scale; g *= scale; b *= scale;
            // 钳位
            r = std::max(0.0, std::min(1.0, r));
            g = std::max(0.0, std::min(1.0, g));
            b = std::max(0.0, std::min(1.0, b));
            img.at(x, y) = sRGB(uint8_t(r * 255), uint8_t(g * 255), uint8_t(b * 255));
        }
    }
    return img;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_hasPanorama(false) {
    setWindowTitle("Main Application");
    createMenu();
    setupUI();
    setupConnections();
}

MainWindow::~MainWindow() {}

void MainWindow::showSkyViewer() {
    // 每次创建新的 MainWidget 窗口并显示
    CIEWidget* viewer = new CIEWidget();
    viewer->setAttribute(Qt::WA_DeleteOnClose); // 关闭时自动删除
    viewer->show();
}

void MainWindow::createMenu() {
    // 创建菜单栏
    m_menuBar = this->menuBar();

    // 添加 "View" 菜单
    QMenu* viewMenu = m_menuBar->addMenu("&CIE Sky");

    // 添加一个 Action
    QAction* skyAction = new QAction("CIE Sky Viewer", this);
    viewMenu->addAction(skyAction);

    // 连接信号
    connect(skyAction, &QAction::triggered, this, &MainWindow::showSkyViewer);
}

void MainWindow::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QHBoxLayout* mainLayout = new QHBoxLayout(central);
    // ---------- 左侧面板 ----------
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setAlignment(Qt::AlignTop);

    // 全局参数分组
    QGroupBox* globalGroup = new QGroupBox("全局参数", this);
    QFormLayout* globalLayout = new QFormLayout(globalGroup);

    // 相机参数分组
    QGroupBox* cameraGroup = new QGroupBox("相机参数", this);
    QFormLayout* formLayout = new QFormLayout(cameraGroup);

    m_localCameraCheck = new QCheckBox("Local Camera");
    m_localCameraCheck->setChecked(true);
    m_localCameraCheck->setToolTip("选中：使用当前 CameraTransform 局部相机坐标方式。\n未选中：使用 SkyPerspectiveWidget::cameraRay 的 ENU 导航相机方式。");
    formLayout->addRow("相机模式:", m_localCameraCheck);

    m_cxSpin = new QDoubleSpinBox; m_cxSpin->setRange(-1, 1); m_cxSpin->setSingleStep(0.05); m_cxSpin->setValue(0.5);
    m_cySpin = new QDoubleSpinBox; m_cySpin->setRange(-1, 1); m_cySpin->setSingleStep(0.05); m_cySpin->setValue(0.2);
    m_czSpin = new QDoubleSpinBox; m_czSpin->setRange(-1, 1); m_czSpin->setSingleStep(0.05); m_czSpin->setValue(0.3);
    formLayout->addRow("相机 Xc 移动:", m_cxSpin);
    formLayout->addRow("相机 Yc 移动:", m_cySpin);
    formLayout->addRow("相机 Zc 移动 (Forward):", m_czSpin);

    m_yawSpin = new QDoubleSpinBox; m_yawSpin->setRange(-180, 180); m_yawSpin->setValue(30);
    m_pitchSpin = new QDoubleSpinBox; m_pitchSpin->setRange(-180, 180); m_pitchSpin->setValue(20);
    m_rollSpin = new QDoubleSpinBox; m_rollSpin->setRange(-180, 180); m_rollSpin->setValue(0);
    formLayout->addRow("偏航 (Yaw):", m_yawSpin);
    formLayout->addRow("俯仰 (Pitch):", m_pitchSpin);
    formLayout->addRow("横滚 (Roll):", m_rollSpin);

    m_hfovSpin = new QDoubleSpinBox; m_hfovSpin->setRange(10, 170); m_hfovSpin->setValue(90);
    m_vfovSpin = new QDoubleSpinBox; m_vfovSpin->setRange(10, 170); m_vfovSpin->setValue(60);
    formLayout->addRow("水平 FOV:", m_hfovSpin);
    formLayout->addRow("垂直 FOV:", m_vfovSpin);
    //m_northPanoramaSpin = new QDoubleSpinBox;
    //m_northPanoramaSpin->setRange(0.0, 360.0);
    //m_northPanoramaSpin->setDecimals(1);
    //m_northPanoramaSpin->setSingleStep(1.0);
    //m_northPanoramaSpin->setSuffix("°");
    //m_northPanoramaSpin->setValue(180.0);
    //m_northPanoramaSpin->setWrapping(true);
    //m_northPanoramaSpin->setKeyboardTracking(false);
    //m_northPanoramaSpin->setToolTip("全景源图中地理北向的位置：0°=左侧接缝，180°=图像中心，360°=右侧/左侧接缝");
    //formLayout->addRow("全景北向位置:", m_northPanoramaSpin);

	////////////////////////////////////////////
	auto createDirectionSpin = [](
		double value) -> QDoubleSpinBox*
	{
		QDoubleSpinBox* spin =
			new QDoubleSpinBox;

		spin->setRange(-1.0, 1.0);
		spin->setDecimals(6);
		spin->setSingleStep(0.01);
		spin->setValue(value);
		spin->setKeyboardTracking(false);

		return spin;
	};

	m_panoEastX = createDirectionSpin(1.0);
	m_panoEastY = createDirectionSpin(0.0);
	m_panoEastZ = createDirectionSpin(0.0);

	m_panoNorthX = createDirectionSpin(0.0);
	m_panoNorthY = createDirectionSpin(1.0);
	m_panoNorthZ = createDirectionSpin(0.0);

	m_panoUpX = createDirectionSpin(0.0);
	m_panoUpY = createDirectionSpin(0.0);
	m_panoUpZ = createDirectionSpin(1.0);


	QWidget* northWidget =
		new QWidget(globalGroup);

	QHBoxLayout* northLayout =
		new QHBoxLayout(northWidget);

	northLayout->setContentsMargins(0, 0, 0, 0);


	QWidget* eastWidget =
		new QWidget(globalGroup);

	QHBoxLayout* eastLayout =
		new QHBoxLayout(eastWidget);

	eastLayout->setContentsMargins(0, 0, 0, 0);
	eastLayout->addWidget(m_panoEastX);
	eastLayout->addWidget(m_panoEastY);
	eastLayout->addWidget(m_panoEastZ);
	//eastLayout->addWidget(m_applyPanoEastBtn);

	globalLayout->addRow(
		"全景 E 向量:",
		eastWidget);


	northLayout->addWidget(m_panoNorthX);
	northLayout->addWidget(m_panoNorthY);
	northLayout->addWidget(m_panoNorthZ);

	globalLayout->addRow(
		"全景 N 向量:",
		northWidget);


	QWidget* upWidget =
		new QWidget(globalGroup);

	QHBoxLayout* upLayout =
		new QHBoxLayout(upWidget);

	upLayout->setContentsMargins(0, 0, 0, 0);

	upLayout->addWidget(m_panoUpX);
	upLayout->addWidget(m_panoUpY);
	upLayout->addWidget(m_panoUpZ);

	globalLayout->addRow(
		"全景 U 向量:",
		upWidget);

	//////////////////////////////////////////////
    m_flipVerticalCheck = new QCheckBox("上下翻转透视图");
    m_flipVerticalCheck->setChecked(false);
    m_flipVerticalCheck->setToolTip("切换透视投影的垂直方向。该设置同时作用于 Perspective、Panorama ROI 和 OpenGL 视锥。");
    formLayout->addRow("透视图方向:", m_flipVerticalCheck);

    m_outWSpin = new QSpinBox; m_outWSpin->setRange(64, 2048); m_outWSpin->setValue(800);
    m_outHSpin = new QSpinBox; m_outHSpin->setRange(64, 2048); m_outHSpin->setValue(600);
    globalLayout->addRow("输出宽度:", m_outWSpin);
    globalLayout->addRow("输出高度:", m_outHSpin);

    m_loadBtn = new QPushButton("加载全景图");
    globalLayout->addRow(m_loadBtn);

    globalGroup->setLayout(globalLayout);
    leftLayout->addWidget(globalGroup);

    cameraGroup->setLayout(formLayout);
    leftLayout->addWidget(cameraGroup);

    leftLayout->addStretch();
    mainLayout->addWidget(leftPanel, 1);

    // ---------- 右侧显示区域 ----------
    QVBoxLayout* rightLayout = new QVBoxLayout;

    m_openGLWidget = new OpenGLSceneWidget(this);
    m_openGLWidget->setFixedSize(600, 300);
    rightLayout->addWidget(m_openGLWidget);

    m_panoramaLabel = new PanoramaLabel(this);
    //m_panoramaLabel->setNorthDirectionDegrees(m_northPanoramaSpin->value());
    m_panoramaLabel->setAlignment(Qt::AlignCenter);
    m_panoramaLabel->setFixedSize(600, 300);
    m_panoramaLabel->setText("未加载全景图");
    rightLayout->addWidget(m_panoramaLabel);

    m_perspectiveLabel = new QLabel(this);
    m_perspectiveLabel->setAlignment(Qt::AlignCenter);
    m_perspectiveLabel->setFixedSize(600, 300);
    m_perspectiveLabel->setStyleSheet("border: 1px solid gray; background-color: #2a2a2a;");
    m_perspectiveLabel->setText("透视视图");
    rightLayout->addWidget(m_perspectiveLabel);

    mainLayout->addLayout(rightLayout, 2);

    connect(m_openGLWidget, &OpenGLSceneWidget::perspectiveViewReady,
        this, &MainWindow::onPerspectiveViewReady);

    //onModelChanged(0);
}

void MainWindow::setupConnections() {
    connect(m_loadBtn, &QPushButton::clicked, this, &MainWindow::onLoadImage);
    connect(m_cxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    connect(m_cySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    connect(m_czSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    connect(m_yawSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    connect(m_pitchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    connect(m_rollSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    connect(m_hfovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    connect(m_vfovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    //connect(m_northPanoramaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);

	auto connectAxis =
		[this](
			PanoAxis axis,
			QDoubleSpinBox* x,
			QDoubleSpinBox* y,
			QDoubleSpinBox* z)
	{
		auto changed =
			[this, axis](double)
		{
			if (!m_updatingPanoramaUI)
				updatePanoramaBasis(axis);
		};

		connect(
			x,
			QOverload<double>::of(
				&QDoubleSpinBox::valueChanged),
			this,
			changed);

		connect(
			y,
			QOverload<double>::of(
				&QDoubleSpinBox::valueChanged),
			this,
			changed);

		connect(
			z,
			QOverload<double>::of(
				&QDoubleSpinBox::valueChanged),
			this,
			changed);
	};


	connectAxis(
		PanoAxis::East,
		m_panoEastX,
		m_panoEastY,
		m_panoEastZ);

	connectAxis(
		PanoAxis::North,
		m_panoNorthX,
		m_panoNorthY,
		m_panoNorthZ);

	connectAxis(
		PanoAxis::Up,
		m_panoUpX,
		m_panoUpY,
		m_panoUpZ);


    connect(m_localCameraCheck, &QCheckBox::toggled, this, &MainWindow::onUpdateParameters);
    connect(m_flipVerticalCheck, &QCheckBox::toggled, this, &MainWindow::onUpdateParameters);
    connect(m_outWSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);
    connect(m_outHSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onUpdateParameters);

    //connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
    //    this, &MainWindow::onModelChanged);
    //connect(m_generateEnvBtn, &QPushButton::clicked,
    //    this, &MainWindow::onGenerateEnvironment);
}

void MainWindow::onLoadImage() {
    QString fileName = QFileDialog::getOpenFileName(this, "打开全景图", "",
        "图像文件 (*.jpg *.jpeg *.png *.bmp *.tga *.exr *.hdr);;所有文件 (*.*)");
    if (fileName.isEmpty()) return;

    QByteArray pathUtf8 = fileName.toUtf8();
    HDRImage img;
    if (!PanoramaProcessor::loadImage(std::string(pathUtf8.constData()), img)) {
        QMessageBox::warning(this, "错误", "加载图像失败！");
        return;
    }

    // Keep the original scene-linear HDR image for all calculations/perspective sampling.
    m_panorama = img;
    m_hasPanorama = true;
    //m_panoramaLabel->setNorthDirectionDegrees(m_northPanoramaSpin->value());
    m_openGLWidget->setPanorama(m_panorama);
    m_openGLWidget->update();

    // Tone mapping is display-only. It does not change m_panorama.
    Image preview = PanoramaProcessor::toneMapForDisplay(m_panorama, 1.0f, 2.2f);
    m_panoramaLabel->setPanoramaImage(preview);

    onUpdateParameters();
    QMessageBox::information(this, "成功",
        QString("全景图加载成功: %1x%2（HDR/EXR 原始浮点亮度已保留）")
        .arg(img.width).arg(img.height));
}

void MainWindow::onUpdateParameters() {
    if (!m_hasPanorama) return;
    double cx = m_cxSpin->value();
    double cy = m_cySpin->value();
    double cz = m_czSpin->value();
    double yaw = m_yawSpin->value();
    double pitch = m_pitchSpin->value();
    double roll = m_rollSpin->value();
    double hfov = m_hfovSpin->value();
    double vfov = m_vfovSpin->value();
    //double northPanoramaDeg = m_northPanoramaSpin->value();
    bool flipVertical = m_flipVerticalCheck->isChecked();
    bool localCamera = m_localCameraCheck->isChecked();
    int outW = m_outWSpin->value();
    int outH = m_outHSpin->value();
	///////////////////////////////
	PanoramaBasis basis;

	const bool ok =
		PanoramaProcessor::
		makePanoramaBasisFromNorthUp(
			m_panoNorthX->value(),
			m_panoNorthY->value(),
			m_panoNorthZ->value(),

			m_panoUpX->value(),
			m_panoUpY->value(),
			m_panoUpZ->value(),

			basis);
	// N 与 U 不能为零，也不能平行。
	if (!ok)
		return;
	m_panoramaBasis = basis;
	//////////////////////////////

    PanoramaProcessor p;
	std::vector<QPointF> corners =
		p.computeCornerUVs(
			cx,
			cy,
			cz,
			yaw,
			pitch,
			roll,
			hfov,
			vfov,
			outW,
			outH,
			m_panoramaBasis,
			flipVertical,
			localCamera);

	m_panoramaLabel->setPanoramaBasis(m_panoramaBasis);
    //m_panoramaLabel->setNorthDirectionDegrees(northPanoramaDeg);
    //std::vector<QPointF> corners = p.computeCornerUVs(cx, cy, cz, yaw, pitch, roll, hfov, vfov, outW, outH, northPanoramaDeg, flipVertical);
    if (corners.size() >= 3) {
        m_panoramaLabel->setCorners(corners);
    }
    else {
        m_panoramaLabel->clearCorners();
    }

    //m_openGLWidget->setCameraParameters(cx, cy, cz, yaw, pitch, roll, hfov, vfov, outW, outH, northPanoramaDeg, flipVertical);
	m_openGLWidget->setCameraParameters(
		cx,
		cy,
		cz,
		yaw,
		pitch,
		roll,
		hfov,
		vfov,
		outW,
		outH,
		m_panoramaBasis,
		flipVertical,
		localCamera);
}

void MainWindow::onPerspectiveViewReady(const QImage& img) {
    QPixmap pix = QPixmap::fromImage(img);
    pix = pix.scaled(m_perspectiveLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_perspectiveLabel->setPixmap(pix);
}

void MainWindow::updatePanoramaBasis(
	PanoAxis editedAxis)
{
	if (m_updatingPanoramaUI)
		return;


	// ----------------------------------------------------
	// 最近修改的两个“不同轴”作为用户输入轴。
	// ----------------------------------------------------

	if (editedAxis != m_lastPanoAxis)
	{
		m_prevPanoAxis =
			m_lastPanoAxis;

		m_lastPanoAxis =
			editedAxis;
	}


	QVector3D e(
		static_cast<float>(
			m_panoEastX->value()),
		static_cast<float>(
			m_panoEastY->value()),
		static_cast<float>(
			m_panoEastZ->value()));

	QVector3D n(
		static_cast<float>(
			m_panoNorthX->value()),
		static_cast<float>(
			m_panoNorthY->value()),
		static_cast<float>(
			m_panoNorthZ->value()));

	QVector3D u(
		static_cast<float>(
			m_panoUpX->value()),
		static_cast<float>(
			m_panoUpY->value()),
		static_cast<float>(
			m_panoUpZ->value()));


	// ----------------------------------------------------
	// 判断现在使用的是哪两个轴。
	// ----------------------------------------------------

	const bool useE =
		m_prevPanoAxis == PanoAxis::East ||
		m_lastPanoAxis == PanoAxis::East;

	const bool useN =
		m_prevPanoAxis == PanoAxis::North ||
		m_lastPanoAxis == PanoAxis::North;

	const bool useU =
		m_prevPanoAxis == PanoAxis::Up ||
		m_lastPanoAxis == PanoAxis::Up;


	constexpr float eps = 1.0e-6f;

	PanoAxis calculatedAxis;


	// ====================================================
	// N + U -> E
	// ====================================================

	if (useN && useU)
	{
		if (n.lengthSquared() < eps ||
			u.lengthSquared() < eps)
			return;

		n.normalize();
		u.normalize();

		e = QVector3D::crossProduct(
			n,
			u);

		if (e.lengthSquared() < eps)
			return;

		e.normalize();

		calculatedAxis =
			PanoAxis::East;
	}

	// ====================================================
	// U + E -> N
	// ====================================================

	else if (useU && useE)
	{
		if (u.lengthSquared() < eps ||
			e.lengthSquared() < eps)
			return;

		u.normalize();
		e.normalize();

		n = QVector3D::crossProduct(
			u,
			e);

		if (n.lengthSquared() < eps)
			return;

		n.normalize();

		calculatedAxis =
			PanoAxis::North;
	}

	// ====================================================
	// E + N -> U
	// ====================================================

	else
	{
		if (e.lengthSquared() < eps ||
			n.lengthSquared() < eps)
			return;

		e.normalize();
		n.normalize();

		u = QVector3D::crossProduct(
			e,
			n);

		if (u.lengthSquared() < eps)
			return;

		u.normalize();

		calculatedAxis =
			PanoAxis::Up;
	}


	// ----------------------------------------------------
	// 只写回“自动计算”的第三个轴。
	//
	// 不修改用户当前正在输入的两个轴。
	// ----------------------------------------------------

	m_updatingPanoramaUI = true;

	switch (calculatedAxis)
	{
	case PanoAxis::East:

		m_panoEastX->setValue(e.x());
		m_panoEastY->setValue(e.y());
		m_panoEastZ->setValue(e.z());

		break;


	case PanoAxis::North:

		m_panoNorthX->setValue(n.x());
		m_panoNorthY->setValue(n.y());
		m_panoNorthZ->setValue(n.z());

		break;


	case PanoAxis::Up:

		m_panoUpX->setValue(u.x());
		m_panoUpY->setValue(u.y());
		m_panoUpZ->setValue(u.z());

		break;
	}

	m_updatingPanoramaUI = false;


	// ----------------------------------------------------
	// panorama_processor 保持原样。
	//
	// 最终仍使用已有的 N + U 接口建立真正正交的 basis。
	// ----------------------------------------------------

	PanoramaBasis basis;

	const bool ok =
		PanoramaProcessor::
		makePanoramaBasisFromNorthUp(
			n.x(),
			n.y(),
			n.z(),

			u.x(),
			u.y(),
			u.z(),

			basis);

	if (!ok)
		return;


	m_panoramaBasis =
		basis;


	// ----------------------------------------------------
	// OpenGL sphere
	// ----------------------------------------------------

	if (m_openGLWidget)
	{
		m_openGLWidget->
			setPanoramaBasis(
				m_panoramaBasis);
	}


	// ----------------------------------------------------
	// Perspective + ROI + camera
	// ----------------------------------------------------

	if (m_hasPanorama)
		onUpdateParameters();
}