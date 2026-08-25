#ifndef CIEWIDGET_H
#define CIEWIDGET_H

#include <QDate>
#include <QMainWindow>
#include <QTimer>

#include "EpwData.hpp"
#include "SkyPerspectiveWidget.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;
QT_END_NAMESPACE

class SkyPolarWidget;

class CIEWidget : public QMainWindow
{
    Q_OBJECT

public:
    explicit CIEWidget(QWidget* parent = nullptr);
    ~CIEWidget();

private slots:
    void onSkyTypeChanged(int index);
    void onCoefficientChanged();
    void onLoadEPW();
    void onSliderTime(int value);
    void onRenderTimeout();
    void onPerspectiveControlsChanged();
    void onScaleModeChanged();
    void onWeatherModeChanged();
    void onResetCamera();
    void onExportPerspective();

private:
    void setupUI();

    void applyEpwRecord(
        const EpwDocument& document,
        const EpwRecord& record);

    double epwMidpointHour(
        int hour,
        int minute,
        int recordsPerHour) const;

    QVector3D currentSunDirection() const;

    SkyPerspectiveParameters
    currentPerspectiveParameters() const;

    void updatePerspectiveView();
    void updateScaleInputsFromCurrentRecord();
    void updateWeatherInputsFromCurrentRecord();
    WeatherVisualState selectedWeatherState() const;

private:
    SkyPolarWidget* m_skyWidget = nullptr;
    SkyPerspectiveWidget* m_perspectiveWidget = nullptr;
    QTabWidget* m_viewTabs = nullptr;

    QComboBox* m_skyTypeCombo = nullptr;
    QDoubleSpinBox* m_spinA = nullptr;
    QDoubleSpinBox* m_spinB = nullptr;
    QDoubleSpinBox* m_spinC = nullptr;
    QDoubleSpinBox* m_spinD = nullptr;
    QDoubleSpinBox* m_spinE = nullptr;

    QPushButton* m_loadEpwButton = nullptr;
    QSlider* m_timeSlider = nullptr;
    QLabel* m_sliderInfo = nullptr;
    QLabel* m_locationInfo = nullptr;

    QDoubleSpinBox* m_cameraAzimuthSpin = nullptr;
    QDoubleSpinBox* m_cameraAltitudeSpin = nullptr;
    QDoubleSpinBox* m_cameraFovSpin = nullptr;

    QComboBox* m_scaleModeCombo = nullptr;
    QDoubleSpinBox* m_targetValueSpin = nullptr;
    QDoubleSpinBox* m_directNormalSpin = nullptr;
    QLabel* m_scaleUnitLabel = nullptr;

    QComboBox* m_colorModeCombo = nullptr;
    QComboBox* m_toneMapCombo = nullptr;
    QDoubleSpinBox* m_referenceValueSpin = nullptr;
    QDoubleSpinBox* m_exposureSpin = nullptr;
    QDoubleSpinBox* m_gammaSpin = nullptr;
    QCheckBox* m_showSunDiskCheck = nullptr;
    QCheckBox* m_showSunGlowCheck = nullptr;
    QCheckBox* m_showHorizonCheck = nullptr;

    QComboBox* m_weatherModeCombo = nullptr;
    QDoubleSpinBox* m_weatherIntensitySpin = nullptr;
    QCheckBox* m_animateWeatherCheck = nullptr;
    QCheckBox* m_showWeatherParticlesCheck = nullptr;
    QCheckBox* m_showWeatherGroundCheck = nullptr;
    QLabel* m_weatherStatusLabel = nullptr;

    QPushButton* m_resetCameraButton = nullptr;
    QPushButton* m_exportButton = nullptr;

    QTimer* m_renderTimer = nullptr;

    EpwDocument m_epwDocument;
    bool m_epwLoaded = false;
    bool m_customCoefficients = false;

    double m_latitude = 39.9;
    double m_longitude = 116.4;
    double m_timeZone = 8.0;

    QDate m_currentDate =
        QDate(2018, 4, 21);
    double m_currentDecimalHour = 10.5;

    double m_currentDhi = 100.0;
    double m_currentDni = 600.0;
    double m_currentDiffuseIlluminance = 10000.0;
    double m_currentDirectIlluminance = 70000.0;
    double m_currentZenithLuminance = 5000.0;
    WeatherVisualState m_currentWeather;
};

#endif // CIEWIDGET_H
