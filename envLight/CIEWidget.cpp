#include "CIEWidget.h"

#include "EpwReader.h"
#include "SkyPolarWidget.h"
#include "SunSky.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

bool validNonNegative(double value)
{
    return std::isfinite(value)
        && value >= 0.0;
}

QString skyTypeName(int type)
{
    switch (type) {
    case 1:
        return "CIE Standard Overcast";
    case 2:
        return "Overcast, slight sun brightening";
    case 3:
        return "Overcast, moderate gradation";
    case 4:
        return "Overcast, moderate + sun brightening";
    case 5:
        return "Uniform luminance";
    case 6:
        return "Partly cloudy, slight sun brightening";
    case 7:
        return "Partly cloudy, brighter circumsolar";
    case 8:
        return "Partly cloudy, distinct solar corona";
    case 9:
        return "Partly cloudy, obscured sun";
    case 10:
        return "Partly cloudy, bright circumsolar";
    case 11:
        return "White-blue, distinct solar corona";
    case 12:
        return "CIE Clear Sky, low turbidity";
    case 13:
        return "CIE Clear Sky, polluted";
    case 14:
        return "Cloudless turbid, broad corona";
    case 15:
        return "White-blue turbid, broad corona";
    default:
        return "Unknown";
    }
}

} // namespace

CIEWidget::CIEWidget(QWidget* parent)
    : QMainWindow(parent)
{
    m_renderTimer =
        new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(60);

    connect(
        m_renderTimer,
        &QTimer::timeout,
        this,
        &CIEWidget::onRenderTimeout);

    setupUI();

    m_currentWeather.description = tr("未加载 EPW：无降水");
    m_skyTypeCombo->setCurrentIndex(11);
    updateScaleInputsFromCurrentRecord();
    updateWeatherInputsFromCurrentRecord();
    updatePerspectiveView();
}

CIEWidget::~CIEWidget() = default;

void CIEWidget::setupUI()
{
    QWidget* central =
        new QWidget(this);
    setCentralWidget(central);

    QHBoxLayout* centralLayout =
        new QHBoxLayout(central);

    QSplitter* splitter =
        new QSplitter(Qt::Horizontal, central);

    centralLayout->addWidget(splitter);

    // ---------------- Left parameter panel ----------------
    QScrollArea* parameterScroll =
        new QScrollArea(splitter);
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setMinimumWidth(330);

    QWidget* parameterPanel =
        new QWidget;
    QVBoxLayout* parameterLayout =
        new QVBoxLayout(parameterPanel);

    // Sky type.
    QGroupBox* skyGroup =
        new QGroupBox(tr("CIE 天空类型"));

    QVBoxLayout* skyLayout =
        new QVBoxLayout(skyGroup);

    m_skyTypeCombo =
        new QComboBox;

    for (int type = 1; type <= 15; ++type) {
        m_skyTypeCombo->addItem(
            QString("%1. %2")
                .arg(type, 2, 10, QChar('0'))
                .arg(skyTypeName(type)));
    }

    skyLayout->addWidget(m_skyTypeCombo);
    parameterLayout->addWidget(skyGroup);

    // A-E coefficients.
    QGroupBox* coefficientGroup =
        new QGroupBox(tr("CIE 参数 A-E"));

    QGridLayout* coefficientLayout =
        new QGridLayout(coefficientGroup);

    auto addCoefficientSpin =
        [&](const QString& name,
            QDoubleSpinBox*& spin,
            double minimum,
            double maximum) {

        const int row =
            coefficientLayout->rowCount();

        coefficientLayout->addWidget(
            new QLabel(name),
            row,
            0);

        spin =
            new QDoubleSpinBox;

        spin->setRange(
            minimum,
            maximum);
        spin->setDecimals(3);
        spin->setSingleStep(0.05);

        coefficientLayout->addWidget(
            spin,
            row,
            1);
    };

    addCoefficientSpin(
        "A", m_spinA, -5.0, 5.0);
    addCoefficientSpin(
        "B", m_spinB, -5.0, 5.0);
    addCoefficientSpin(
        "C", m_spinC, -5.0, 30.0);
    addCoefficientSpin(
        "D", m_spinD, -10.0, 5.0);
    addCoefficientSpin(
        "E", m_spinE, -5.0, 5.0);

    parameterLayout->addWidget(
        coefficientGroup);

    // EPW controls.
    QGroupBox* epwGroup =
        new QGroupBox(tr("EPW 时间与地点"));

    QVBoxLayout* epwLayout =
        new QVBoxLayout(epwGroup);

    m_loadEpwButton =
        new QPushButton(tr("加载 EPW"));

    m_locationInfo =
        new QLabel(tr("默认地点：北京"));
    m_locationInfo->setWordWrap(true);

    m_timeSlider =
        new QSlider(Qt::Horizontal);
    m_timeSlider->setRange(0, 0);
    m_timeSlider->setEnabled(false);

    m_sliderInfo =
        new QLabel(tr("未加载 EPW"));

    epwLayout->addWidget(
        m_loadEpwButton);
    epwLayout->addWidget(
        m_locationInfo);
    epwLayout->addWidget(
        m_timeSlider);
    epwLayout->addWidget(
        m_sliderInfo);

    parameterLayout->addWidget(epwGroup);

    // Absolute scale.
    QGroupBox* scaleGroup =
        new QGroupBox(tr("绝对量标定"));

    QFormLayout* scaleLayout =
        new QFormLayout(scaleGroup);

    m_scaleModeCombo =
        new QComboBox;

    m_scaleModeCombo->addItem(
        tr("EPW 水平散射辐照度"),
        static_cast<int>(
            SkyAbsoluteScaleMode::
                DiffuseHorizontalIrradiance));

    m_scaleModeCombo->addItem(
        tr("EPW 水平散射照度"),
        static_cast<int>(
            SkyAbsoluteScaleMode::
                DiffuseHorizontalIlluminance));

    m_scaleModeCombo->addItem(
        tr("EPW 天顶亮度"),
        static_cast<int>(
            SkyAbsoluteScaleMode::
                ZenithLuminance));

    m_targetValueSpin =
        new QDoubleSpinBox;
    m_targetValueSpin->setRange(
        0.0,
        100000000.0);
    m_targetValueSpin->setDecimals(3);

    m_directNormalSpin =
        new QDoubleSpinBox;
    m_directNormalSpin->setRange(
        0.0,
        100000000.0);
    m_directNormalSpin->setDecimals(3);

    m_scaleUnitLabel =
        new QLabel("W/m²");

    scaleLayout->addRow(
        tr("标定方式"),
        m_scaleModeCombo);
    scaleLayout->addRow(
        tr("散射天空目标值"),
        m_targetValueSpin);
    scaleLayout->addRow(
        tr("太阳直射法向值"),
        m_directNormalSpin);
    scaleLayout->addRow(
        tr("当前单位"),
        m_scaleUnitLabel);

    parameterLayout->addWidget(scaleGroup);

    // Camera.
    QGroupBox* cameraGroup =
        new QGroupBox(tr("透视相机"));

    QFormLayout* cameraLayout =
        new QFormLayout(cameraGroup);

    m_cameraAzimuthSpin =
        new QDoubleSpinBox;
    m_cameraAzimuthSpin->setRange(
        0.0,
        359.9);
    m_cameraAzimuthSpin->setDecimals(1);
    m_cameraAzimuthSpin->setSingleStep(5.0);
    m_cameraAzimuthSpin->setSuffix("°");
    m_cameraAzimuthSpin->setValue(180.0);

    m_cameraAltitudeSpin =
        new QDoubleSpinBox;
    m_cameraAltitudeSpin->setRange(
        -89.0,
        89.0);
    m_cameraAltitudeSpin->setDecimals(1);
    m_cameraAltitudeSpin->setSingleStep(5.0);
    m_cameraAltitudeSpin->setSuffix("°");
    m_cameraAltitudeSpin->setValue(20.0);

    m_cameraFovSpin =
        new QDoubleSpinBox;
    m_cameraFovSpin->setRange(
        10.0,
        170.0);
    m_cameraFovSpin->setDecimals(1);
    m_cameraFovSpin->setSingleStep(5.0);
    m_cameraFovSpin->setSuffix("°");
    m_cameraFovSpin->setValue(90.0);

    m_resetCameraButton =
        new QPushButton(tr("重置相机"));

    cameraLayout->addRow(
        tr("观察方位 Az"),
        m_cameraAzimuthSpin);
    cameraLayout->addRow(
        tr("观察仰角 Alt"),
        m_cameraAltitudeSpin);
    cameraLayout->addRow(
        tr("垂直视场 VFOV"),
        m_cameraFovSpin);
    cameraLayout->addRow(
        m_resetCameraButton);

    parameterLayout->addWidget(cameraGroup);

    // EPW weather visual effects.
    QGroupBox* weatherGroup =
        new QGroupBox(tr("雨雪与能见度效果"));

    QFormLayout* weatherLayout =
        new QFormLayout(weatherGroup);

    m_weatherModeCombo = new QComboBox;
    m_weatherModeCombo->addItem(tr("自动读取 EPW"), -1);
    m_weatherModeCombo->addItem(tr("关闭天气粒子"), 0);
    m_weatherModeCombo->addItem(tr("手动：雨"), 1);
    m_weatherModeCombo->addItem(tr("手动：雪"), 2);
    m_weatherModeCombo->addItem(tr("手动：雨夹雪"), 3);
    m_weatherModeCombo->addItem(tr("手动：雾"), 4);
    m_weatherModeCombo->addItem(tr("手动：冻雨"), 5);
    m_weatherModeCombo->addItem(tr("手动：冰雹/冰粒"), 6);

    m_weatherIntensitySpin = new QDoubleSpinBox;
    m_weatherIntensitySpin->setRange(0.0, 1.0);
    m_weatherIntensitySpin->setDecimals(2);
    m_weatherIntensitySpin->setSingleStep(0.05);
    m_weatherIntensitySpin->setValue(0.6);
    m_weatherIntensitySpin->setEnabled(false);

    m_animateWeatherCheck =
        new QCheckBox(tr("播放雨雪动画"));
    m_animateWeatherCheck->setChecked(true);

    m_showWeatherParticlesCheck =
        new QCheckBox(tr("显示雨丝/雪花粒子"));
    m_showWeatherParticlesCheck->setChecked(true);

    m_showWeatherGroundCheck =
        new QCheckBox(tr("显示湿地面/积雪地面"));
    m_showWeatherGroundCheck->setChecked(true);

    m_weatherStatusLabel = new QLabel(tr("当前：无 EPW 天气数据"));
    m_weatherStatusLabel->setWordWrap(true);

    weatherLayout->addRow(tr("天气来源"), m_weatherModeCombo);
    weatherLayout->addRow(tr("手动强度 0-1"), m_weatherIntensitySpin);
    weatherLayout->addRow(m_animateWeatherCheck);
    weatherLayout->addRow(m_showWeatherParticlesCheck);
    weatherLayout->addRow(m_showWeatherGroundCheck);
    weatherLayout->addRow(tr("EPW 判定"), m_weatherStatusLabel);

    parameterLayout->addWidget(weatherGroup);

    // Display.
    QGroupBox* displayGroup =
        new QGroupBox(tr("显示设置"));

    QFormLayout* displayLayout =
        new QFormLayout(displayGroup);

    m_colorModeCombo =
        new QComboBox;

    m_colorModeCombo->addItem(
        tr("自然天空预览"),
        static_cast<int>(
            SkyColorMode::NaturalPreview));
    m_colorModeCombo->addItem(
        tr("科学伪彩"),
        static_cast<int>(
            SkyColorMode::FalseColor));
    m_colorModeCombo->addItem(
        tr("亮度灰度"),
        static_cast<int>(
            SkyColorMode::GrayscaleLuminance));

    m_toneMapCombo =
        new QComboBox;

    m_toneMapCombo->addItem(
        tr("固定参考值（推荐比较类型）"),
        static_cast<int>(
            SkyToneMapMode::FixedReference));

    m_toneMapCombo->addItem(
        tr("每帧自动峰值"),
        static_cast<int>(
            SkyToneMapMode::AutoPeak));

    m_referenceValueSpin =
        new QDoubleSpinBox;
    m_referenceValueSpin->setRange(
        0.001,
        100000000.0);
    m_referenceValueSpin->setDecimals(3);
    m_referenceValueSpin->setValue(50.0);

    m_exposureSpin =
        new QDoubleSpinBox;
    m_exposureSpin->setRange(
        0.01,
        20.0);
    m_exposureSpin->setDecimals(2);
    m_exposureSpin->setSingleStep(0.1);
    m_exposureSpin->setValue(1.0);

    m_gammaSpin =
        new QDoubleSpinBox;
    m_gammaSpin->setRange(
        0.1,
        5.0);
    m_gammaSpin->setDecimals(2);
    m_gammaSpin->setSingleStep(0.1);
    m_gammaSpin->setValue(2.2);

    m_showSunDiskCheck =
        new QCheckBox(tr("显示物理太阳盘"));
    m_showSunDiskCheck->setChecked(true);

    m_showSunGlowCheck =
        new QCheckBox(tr("自然预览太阳光晕"));
    m_showSunGlowCheck->setChecked(true);

    m_showHorizonCheck =
        new QCheckBox(tr("显示地平线"));
    m_showHorizonCheck->setChecked(true);

    m_exportButton =
        new QPushButton(tr("导出 1920×1080 PNG"));

    displayLayout->addRow(
        tr("颜色模式"),
        m_colorModeCombo);
    displayLayout->addRow(
        tr("色调映射"),
        m_toneMapCombo);
    displayLayout->addRow(
        tr("显示参考值"),
        m_referenceValueSpin);
    displayLayout->addRow(
        tr("曝光"),
        m_exposureSpin);
    displayLayout->addRow(
        tr("Gamma"),
        m_gammaSpin);
    displayLayout->addRow(
        m_showSunDiskCheck);
    displayLayout->addRow(
        m_showSunGlowCheck);
    displayLayout->addRow(
        m_showHorizonCheck);
    displayLayout->addRow(
        m_exportButton);

    parameterLayout->addWidget(displayGroup);
    parameterLayout->addStretch(1);

    parameterScroll->setWidget(parameterPanel);

    // ---------------- Right view tabs ----------------
    m_viewTabs =
        new QTabWidget(splitter);

    m_skyWidget =
        new SkyPolarWidget(m_viewTabs);

    m_perspectiveWidget =
        new SkyPerspectiveWidget(m_viewTabs);

    m_viewTabs->addTab(
        m_skyWidget,
        tr("天空半球分析"));

    m_viewTabs->addTab(
        m_perspectiveWidget,
        tr("透视天空"));

    splitter->addWidget(parameterScroll);
    splitter->addWidget(m_viewTabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({350, 1000});

    // ---------------- Connections ----------------
    connect(
        m_loadEpwButton,
        &QPushButton::clicked,
        this,
        &CIEWidget::onLoadEPW);

    connect(
        m_timeSlider,
        &QSlider::valueChanged,
        this,
        &CIEWidget::onSliderTime);

    connect(
        m_skyTypeCombo,
        QOverload<int>::of(
            &QComboBox::currentIndexChanged),
        this,
        &CIEWidget::onSkyTypeChanged);

    auto connectCoefficient =
        [this](QDoubleSpinBox* spin) {

        connect(
            spin,
            QOverload<double>::of(
                &QDoubleSpinBox::valueChanged),
            this,
            [this](double) {
                m_renderTimer->start();
            });
    };

    connectCoefficient(m_spinA);
    connectCoefficient(m_spinB);
    connectCoefficient(m_spinC);
    connectCoefficient(m_spinD);
    connectCoefficient(m_spinE);

    connect(
        m_scaleModeCombo,
        QOverload<int>::of(
            &QComboBox::currentIndexChanged),
        this,
        &CIEWidget::onScaleModeChanged);

    auto connectPerspectiveSpin =
        [this](QDoubleSpinBox* spin) {
        connect(
            spin,
            QOverload<double>::of(
                &QDoubleSpinBox::valueChanged),
            this,
            [this](double) {
                onPerspectiveControlsChanged();
            });
    };

    connectPerspectiveSpin(
        m_cameraAzimuthSpin);
    connectPerspectiveSpin(
        m_cameraAltitudeSpin);
    connectPerspectiveSpin(
        m_cameraFovSpin);
    connectPerspectiveSpin(
        m_targetValueSpin);
    connectPerspectiveSpin(
        m_directNormalSpin);
    connectPerspectiveSpin(
        m_referenceValueSpin);
    connectPerspectiveSpin(
        m_exposureSpin);
    connectPerspectiveSpin(
        m_gammaSpin);

    auto connectPerspectiveCombo =
        [this](QComboBox* combo) {
        connect(
            combo,
            QOverload<int>::of(
                &QComboBox::currentIndexChanged),
            this,
            [this](int) {
                onPerspectiveControlsChanged();
            });
    };

    connectPerspectiveCombo(
        m_colorModeCombo);
    connectPerspectiveCombo(
        m_toneMapCombo);

    connect(
        m_showSunDiskCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) {
            onPerspectiveControlsChanged();
        });

    connect(
        m_showSunGlowCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) {
            onPerspectiveControlsChanged();
        });

    connect(
        m_showHorizonCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) {
            onPerspectiveControlsChanged();
        });

    connect(
        m_weatherModeCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &CIEWidget::onWeatherModeChanged);

    connect(
        m_weatherIntensitySpin,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double) { onPerspectiveControlsChanged(); });

    connect(
        m_animateWeatherCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) { onPerspectiveControlsChanged(); });

    connect(
        m_showWeatherParticlesCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) { onPerspectiveControlsChanged(); });

    connect(
        m_showWeatherGroundCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) { onPerspectiveControlsChanged(); });

    connect(
        m_resetCameraButton,
        &QPushButton::clicked,
        this,
        &CIEWidget::onResetCamera);

    connect(
        m_exportButton,
        &QPushButton::clicked,
        this,
        &CIEWidget::onExportPerspective);

    connect(
        m_perspectiveWidget,
        &SkyPerspectiveWidget::cameraChanged,
        this,
        [this](
            double azimuth,
            double altitude,
            double vfov) {

            const QSignalBlocker blockAzimuth(
                m_cameraAzimuthSpin);
            const QSignalBlocker blockAltitude(
                m_cameraAltitudeSpin);
            const QSignalBlocker blockFov(
                m_cameraFovSpin);

            m_cameraAzimuthSpin->setValue(
                azimuth);
            m_cameraAltitudeSpin->setValue(
                altitude);
            m_cameraFovSpin->setValue(
                vfov);
        });
}

void CIEWidget::onSkyTypeChanged(
    int index)
{
    const SSLib::CIESkyCoefficients coefficients =
        SSLib::CIEStandardSkyCoefficients(index);

    const QSignalBlocker blockA(m_spinA);
    const QSignalBlocker blockB(m_spinB);
    const QSignalBlocker blockC(m_spinC);
    const QSignalBlocker blockD(m_spinD);
    const QSignalBlocker blockE(m_spinE);

    m_spinA->setValue(coefficients.a);
    m_spinB->setValue(coefficients.b);
    m_spinC->setValue(coefficients.c);
    m_spinD->setValue(coefficients.d);
    m_spinE->setValue(coefficients.e);

    m_customCoefficients = false;

    m_skyWidget->setCieSkyType(index);
    updatePerspectiveView();
}

void CIEWidget::onCoefficientChanged()
{
    SSLib::CIESkyCoefficients coefficients;

    coefficients.a =
        static_cast<float>(
            m_spinA->value());
    coefficients.b =
        static_cast<float>(
            m_spinB->value());
    coefficients.c =
        static_cast<float>(
            m_spinC->value());
    coefficients.d =
        static_cast<float>(
            m_spinD->value());
    coefficients.e =
        static_cast<float>(
            m_spinE->value());

    m_customCoefficients = true;

    m_skyWidget->setCustomCoefficients(
        coefficients);

    updatePerspectiveView();
}

void CIEWidget::onRenderTimeout()
{
    onCoefficientChanged();
}

void CIEWidget::onLoadEPW()
{
    const QString path =
        QFileDialog::getOpenFileName(
            this,
            tr("打开 EPW 文件"),
            QString(),
            tr("EPW 文件 (*.epw)"));

    if (path.isEmpty())
        return;

    EpwDocument document;

    if (!EpwReader::read(
            path,
            document)) {

        QMessageBox::critical(
            this,
            tr("EPW 错误"),
            tr("EPW 文件解析失败。"));

        return;
    }

    m_epwDocument = document;
    m_epwLoaded = true;

    m_latitude =
        document.location.latitude;
    m_longitude =
        document.location.longitude;
    m_timeZone =
        document.location.timeZone;

    m_skyWidget->setLocation(
        m_latitude,
        m_longitude,
        m_timeZone);

    m_locationInfo->setText(
        QString(
            "%1  纬度 %2°  经度 %3°  UTC%4")
            .arg(document.location.city)
            .arg(m_latitude, 0, 'f', 4)
            .arg(m_longitude, 0, 'f', 4)
            .arg(
                m_timeZone >= 0.0
                    ? QString("+%1")
                        .arg(m_timeZone)
                    : QString::number(
                        m_timeZone)));

    if (document.records.isEmpty()) {
        m_timeSlider->setRange(0, 0);
        m_timeSlider->setEnabled(false);
        m_sliderInfo->setText(
            tr("EPW 无有效数据"));
        return;
    }

    m_timeSlider->setRange(
        0,
        document.records.size() - 1);
    m_timeSlider->setEnabled(true);
    m_timeSlider->setValue(0);

    applyEpwRecord(
        document,
        document.records.first());
}

void CIEWidget::onSliderTime(
    int value)
{
    if (!m_epwLoaded ||
        value < 0 ||
        value >= m_epwDocument.records.size()) {
        return;
    }

    const EpwRecord& record =
        m_epwDocument.records[value];

    applyEpwRecord(
        m_epwDocument,
        record);
}

void CIEWidget::applyEpwRecord(
    const EpwDocument& document,
    const EpwRecord& record)
{
    const double midpoint =
        epwMidpointHour(
            record.hour,
            record.minute,
            document.recordsPerHour);

    const double radiationFactor =
        std::max(
            1,
            document.recordsPerHour);

    m_currentDhi =
        validNonNegative(record.dhi)
        ? record.dhi * radiationFactor
        : 0.0;

    m_currentDni =
        validNonNegative(record.dni)
        ? record.dni * radiationFactor
        : 0.0;

    m_currentDiffuseIlluminance =
        validNonNegative(
            record.diffuseHorizontalIlluminance)
        ? record.diffuseHorizontalIlluminance
        : 0.0;

    m_currentDirectIlluminance =
        validNonNegative(
            record.directNormalIlluminance)
        ? record.directNormalIlluminance
        : 0.0;

    m_currentZenithLuminance =
        validNonNegative(
            record.zenithLuminance)
        ? record.zenithLuminance
        : 0.0;

    m_currentWeather = deriveWeatherVisualState(
        record,
        document.recordsPerHour);

    m_currentDate =
        QDate(
            record.year,
            record.month,
            record.day);

    m_currentDecimalHour = midpoint;

    m_skyWidget->setDateTime(
        m_currentDate,
        m_currentDecimalHour);

    m_skyWidget
        ->setDiffuseHorizontalIrradiance(
            m_currentDhi);

    m_sliderInfo->setText(
        QString(
            "%1-%2-%3 %4:%5  中点 %6 h")
            .arg(record.year)
            .arg(
                record.month,
                2,
                10,
                QChar('0'))
            .arg(
                record.day,
                2,
                10,
                QChar('0'))
            .arg(
                record.hour,
                2,
                10,
                QChar('0'))
            .arg(
                record.minute,
                2,
                10,
                QChar('0'))
            .arg(
                midpoint,
                0,
                'f',
                2));

    updateScaleInputsFromCurrentRecord();
    updateWeatherInputsFromCurrentRecord();
    updatePerspectiveView();
}

double CIEWidget::epwMidpointHour(
    int hour,
    int minute,
    int recordsPerHour) const
{
    const double intervalHours =
        1.0 / std::max(
            1,
            recordsPerHour);

    const double endHour =
        static_cast<double>(hour - 1)
        + static_cast<double>(minute)
            / 60.0;

    return endHour
        - intervalHours * 0.5;
}

QVector3D CIEWidget::currentSunDirection() const
{
    const SSLib::Vec3f sun =
        SSLib::SunDirection(
            static_cast<float>(
                m_currentDecimalHour),
            static_cast<float>(
                m_timeZone),
            m_currentDate.dayOfYear(),
            static_cast<float>(
                m_latitude),
            static_cast<float>(
                m_longitude));

    QVector3D direction(
        sun[0],
        sun[1],
        sun[2]);

    if (direction.lengthSquared()
        < 1.0e-12f) {
        return QVector3D(
            0.0f,
            0.0f,
            1.0f);
    }

    return direction.normalized();
}

SkyPerspectiveParameters
CIEWidget::currentPerspectiveParameters() const
{
    SkyPerspectiveParameters parameters;

    parameters.cieSkyType =
        m_skyTypeCombo->currentIndex();

    parameters.customCoefficients =
        m_customCoefficients;

    parameters.coefficients.a =
        static_cast<float>(
            m_spinA->value());
    parameters.coefficients.b =
        static_cast<float>(
            m_spinB->value());
    parameters.coefficients.c =
        static_cast<float>(
            m_spinC->value());
    parameters.coefficients.d =
        static_cast<float>(
            m_spinD->value());
    parameters.coefficients.e =
        static_cast<float>(
            m_spinE->value());

    parameters.scaleMode =
        static_cast<SkyAbsoluteScaleMode>(
            m_scaleModeCombo
                ->currentData()
                .toInt());

    parameters.targetValue =
        m_targetValueSpin->value();

    parameters.directNormalValue =
        m_directNormalSpin->value();

    parameters.sunDirection =
        currentSunDirection();

    parameters.cameraAzimuthDeg =
        m_cameraAzimuthSpin->value();
    parameters.cameraPitchDeg =
        m_cameraAltitudeSpin->value();
    parameters.verticalFovDeg =
        m_cameraFovSpin->value();

    parameters.colorMode =
        static_cast<SkyColorMode>(
            m_colorModeCombo
                ->currentData()
                .toInt());

    parameters.toneMapMode =
        static_cast<SkyToneMapMode>(
            m_toneMapCombo
                ->currentData()
                .toInt());

    parameters.displayReferenceValue =
        m_referenceValueSpin->value();

    parameters.exposure =
        m_exposureSpin->value();

    parameters.gamma =
        m_gammaSpin->value();

    parameters.showSunDisk =
        m_showSunDiskCheck->isChecked();

    parameters.showSunGlow =
        m_showSunGlowCheck->isChecked();

    parameters.showHorizon =
        m_showHorizonCheck->isChecked();

    parameters.weather = selectedWeatherState();
    parameters.animateWeather =
        m_animateWeatherCheck->isChecked();
    parameters.showWeatherParticles =
        m_showWeatherParticlesCheck->isChecked();
    parameters.showWeatherGround =
        m_showWeatherGroundCheck->isChecked();

    return parameters;
}

void CIEWidget::updatePerspectiveView()
{
    if (!m_perspectiveWidget)
        return;

    m_perspectiveWidget->setParameters(
        currentPerspectiveParameters());
}

void CIEWidget::updateScaleInputsFromCurrentRecord()
{
    const SkyAbsoluteScaleMode mode =
        static_cast<SkyAbsoluteScaleMode>(
            m_scaleModeCombo
                ->currentData()
                .toInt());

    const QSignalBlocker targetBlocker(
        m_targetValueSpin);
    const QSignalBlocker directBlocker(
        m_directNormalSpin);
    const QSignalBlocker referenceBlocker(
        m_referenceValueSpin);

    switch (mode) {
    case SkyAbsoluteScaleMode::
        DiffuseHorizontalIlluminance:

        m_targetValueSpin->setValue(
            m_currentDiffuseIlluminance);
        m_directNormalSpin->setValue(
            m_currentDirectIlluminance);
        m_referenceValueSpin->setValue(
            5000.0);
        m_scaleUnitLabel->setText(
            "lx / cd·m⁻²");
        break;

    case SkyAbsoluteScaleMode::
        ZenithLuminance:

        m_targetValueSpin->setValue(
            m_currentZenithLuminance);
        m_directNormalSpin->setValue(
            m_currentDirectIlluminance);
        m_referenceValueSpin->setValue(
            5000.0);
        m_scaleUnitLabel->setText(
            "cd/m²；直射值为 lx");
        break;

    case SkyAbsoluteScaleMode::
        DiffuseHorizontalIrradiance:
    default:

        m_targetValueSpin->setValue(
            m_currentDhi);
        m_directNormalSpin->setValue(
            m_currentDni);
        m_referenceValueSpin->setValue(
            50.0);
        m_scaleUnitLabel->setText(
            "W/m²；天空像素为 W/(m²·sr)");
        break;
    }
}

WeatherVisualState CIEWidget::selectedWeatherState() const
{
    const int mode = m_weatherModeCombo->currentData().toInt();

    if (mode < 0)
        return m_currentWeather;

    WeatherVisualState state = m_currentWeather;
    state.intensity = m_weatherIntensitySpin->value();

    switch (mode) {
    case 0:
        state.precipitation = PrecipitationKind::None;
        state.intensity = 0.0;
        state.fogDensity = 0.0;
        state.description = tr("手动关闭天气效果");
        break;
    case 1:
        state.precipitation = PrecipitationKind::Rain;
        state.description = tr("手动雨天");
        break;
    case 2:
        state.precipitation = PrecipitationKind::Snow;
        state.description = tr("手动雪天");
        break;
    case 3:
        state.precipitation = PrecipitationKind::Mixed;
        state.description = tr("手动雨夹雪");
        break;
    case 4:
        state.precipitation = PrecipitationKind::None;
        state.fogDensity = state.intensity;
        state.description = tr("手动雾天");
        break;
    case 5:
        state.precipitation = PrecipitationKind::FreezingRain;
        state.description = tr("手动冻雨");
        break;
    case 6:
        state.precipitation = PrecipitationKind::Hail;
        state.description = tr("手动冰雹/冰粒");
        break;
    default:
        break;
    }

    return state;
}

void CIEWidget::updateWeatherInputsFromCurrentRecord()
{
    if (!m_weatherStatusLabel)
        return;

    m_weatherStatusLabel->setText(m_currentWeather.description);

    if (m_weatherModeCombo->currentData().toInt() < 0) {
        const QSignalBlocker blocker(m_weatherIntensitySpin);
        m_weatherIntensitySpin->setValue(m_currentWeather.intensity);
    }
}

void CIEWidget::onWeatherModeChanged()
{
    const bool manual = m_weatherModeCombo->currentData().toInt() > 0;
    m_weatherIntensitySpin->setEnabled(manual);
    updateWeatherInputsFromCurrentRecord();
    updatePerspectiveView();
}

void CIEWidget::onPerspectiveControlsChanged()
{
    updatePerspectiveView();
}

void CIEWidget::onScaleModeChanged()
{
    updateScaleInputsFromCurrentRecord();
    updatePerspectiveView();
}

void CIEWidget::onResetCamera()
{
    m_cameraAzimuthSpin->setValue(180.0);
    m_cameraAltitudeSpin->setValue(20.0);
    m_cameraFovSpin->setValue(90.0);
    updatePerspectiveView();
}

void CIEWidget::onExportPerspective()
{
    const QString path =
        QFileDialog::getSaveFileName(
            this,
            tr("导出透视天空"),
            "cie_sky_perspective.png",
            tr("PNG 图片 (*.png)"));

    if (path.isEmpty())
        return;

    updatePerspectiveView();

    if (!m_perspectiveWidget->savePng(
            path,
            QSize(1920, 1080))) {

        QMessageBox::warning(
            this,
            tr("导出失败"),
            tr("无法保存 PNG 文件。"));
    }
}
