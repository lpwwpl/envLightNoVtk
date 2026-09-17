#ifndef STANDARDSKYVIEWER_H
#define STANDARDSKYVIEWER_H

#include <QMainWindow>
#include <QString>
#include <QVector3D>

#include "SkyPerspectiveWidget.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDateTime;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
QT_END_NAMESPACE

class SkySceneWidget;

// ================================================================
// CIE Standard General Sky 简化查看器
// 说明：CIE 天空分布由 environment_light.h 中的 CIESkyModel 负责，
// 本类只负责 CIE 天空 ENU 基、太阳位置、Viewer Camera、有限半径天空球可视化、传感器显示参数和导出。
// Viewer Camera 先独立转换到世界 ENU；球面交点方向再转换到 CIE 天空局部 ENU，两个变换互不绑定。
// ================================================================
class StandardSkyViewer final : public QMainWindow
{
    Q_OBJECT

public:
    // 功能：创建简化的 CIE Standard Sky 查看器并建立界面。
    explicit StandardSkyViewer(QWidget* parent = nullptr);

    // 功能：销毁窗口及其 Qt 子对象。
    ~StandardSkyViewer() override;

private slots:
    // 功能：合并连续 UI 变化，延迟触发一次天空重渲染。
    void scheduleRender();

    // 功能：读取当前 UI 参数并刷新 SkyPerspectiveWidget。
    void updateRender();

    // 功能：根据 Automatic/Manual Sun 模式更新地点与手动太阳控件状态。
    void onSunModeChanged();

    // 功能：根据传感器类型更新光谱控件与显示参考值单位。
    void onSensorTypeChanged();

    // 功能：同步 Speos 风格 Focal/Observer 几何、采样、分辨率和 Viewer Camera FOV。
    void onSensorGeometryChanged();

    // 功能：Focal 模式下保持当前角中心不变，由 HFOV/VFOV 反算物理 X/Y 范围。
    void onSensorFovChanged();

    // 功能：处理光谱范围和采样数变化并重建波长列表。
    void onWavelengthChanged();

    // 功能：把 Viewer Camera 恢复为默认姿态。
    void onResetView();

    // 功能：使用 ENU 导航方式让 Viewer Camera 朝向当前太阳。
    void onAimAtSun();

    // 功能：按输出宽高导出当前 CIE 天空 PNG。
    void onExportPng();

    // 功能：打开早期版本的离线世界地图 + 城市搜索列表，选择地点和 IANA 时区。
    void onChooseZone();

    // 功能：Automatic from Zone 状态变化时刷新 UTC 偏移。
    void onAutoTimeZoneToggled(bool checked);

    // 功能：日期时间变化时重新计算 DST-aware UTC 偏移。
    void onDateTimeChanged(const QDateTime& dateTime);

    // 功能：显示波长变化时刷新结果单位和图像。
    void onDisplayWavelengthChanged(int index);

    // 功能：接收 SkyPerspectiveWidget 的交互相机变化并同步 Viewer Camera 控件。
    void onCameraChanged(double azimuth, double altitude, double roll, double horizontalFov, double verticalFov);

private:
    // 功能：创建 CIE 全局参数、地点、传感器、Viewer Camera 和结果显示界面。
    void setupUi();

    // 功能：把当前界面状态转换为 SkyPerspectiveParameters。
    SkyPerspectiveParameters currentParameters() const;

    // 功能：读取 E/N/U 输入并构造右手正交 CIE 天空基；该基只用于 World -> CIE Sky 变换，不旋转 Viewer Camera。
    void skyBasis(QVector3D& east, QVector3D& north, QVector3D& up) const;

    // 功能：把天空局部 ENU 方向转换为世界方向。
    QVector3D skyToWorld(const QVector3D& localDirection) const;

    // 功能：根据 Automatic/Manual Sun 模式返回天空局部 ENU 太阳方向。
    QVector3D currentSunLocalDirection() const;

    // 功能：返回经过当前 ENU 基旋转后的太阳世界方向。
    QVector3D currentSunWorldDirection() const;

    // 功能：返回 CIE 1~15 类型的简短说明。
    static QString skyTypeDescription(int zeroBasedType);

    // 功能：把 ENU 方向转换为 0~360° 方位角。
    static double vectorAzimuthDeg(const QVector3D& direction);

    // 功能：把 ENU 方向转换为 -90~90° 高度角。
    static double vectorAltitudeDeg(const QVector3D& direction);

    // 功能：根据 IANA Zone 和日期计算 UTC 偏移，并保留手动覆盖能力。
    void updateTimeZoneFromZone();

    // 功能：根据传感器类型更新光谱控件与 Tone Mapping 参考值单位。
    void updateSensorUiState();

    // 功能：根据 Focal Frame 的 X/Y 尺寸计算派生 HFOV/VFOV，并同步输出采样。
    void updateSensorGeometryUi();

    // 功能：根据输出尺寸和波长状态更新导出按钮文字。
    void updateExportButtonText();

    // 功能：按照 Start/End/Sampling 重建可选波长列表。
    void rebuildWavelengthList();

    // 功能：把 QDoubleSpinBox 的变化连接到延迟渲染槽。
    void connectRenderSpin(QDoubleSpinBox* spin);

    // 功能：把 QSpinBox 的变化连接到延迟渲染槽。
    void connectRenderIntSpin(QSpinBox* spin);

    // 功能：把 QComboBox 的变化连接到延迟渲染槽。
    void connectRenderCombo(QComboBox* combo);

    // 功能：把 QCheckBox 的变化连接到延迟渲染槽。
    void connectRenderCheck(QCheckBox* check);

private:
    SkySceneWidget* m_sceneWidget = nullptr;
    SkyPerspectiveWidget* m_skyWidget = nullptr;
    QTimer* m_renderTimer = nullptr;

    // CIE 全局参数：天空类型、天顶亮度、太阳模式和用户可编辑 ENU 基。
    QComboBox* m_cieTypeCombo = nullptr;
    QDoubleSpinBox* m_luminanceSpin = nullptr;
    QComboBox* m_sunModeCombo = nullptr;
    QDoubleSpinBox* m_eastXSpin = nullptr;
    QDoubleSpinBox* m_eastYSpin = nullptr;
    QDoubleSpinBox* m_eastZSpin = nullptr;
    QDoubleSpinBox* m_northXSpin = nullptr;
    QDoubleSpinBox* m_northYSpin = nullptr;
    QDoubleSpinBox* m_northZSpin = nullptr;
    QDoubleSpinBox* m_upXSpin = nullptr;
    QDoubleSpinBox* m_upYSpin = nullptr;
    QDoubleSpinBox* m_upZSpin = nullptr;

    // Time zone and location.
    QLineEdit* m_zoneEdit = nullptr;
    QPushButton* m_zoneMapButton = nullptr;
    QCheckBox* m_autoTimeZoneCheck = nullptr;
    QLabel* m_timeZoneIdLabel = nullptr;
    QDoubleSpinBox* m_timeZoneSpin = nullptr;
    QString m_timeZoneId;
    QDoubleSpinBox* m_longitudeSpin = nullptr;
    QDoubleSpinBox* m_latitudeSpin = nullptr;
    QDateTimeEdit* m_dateTimeEdit = nullptr;

    // Sun source.
    QDoubleSpinBox* m_manualSunAzimuthSpin = nullptr;
    QDoubleSpinBox* m_manualSunAltitudeSpin = nullptr;
    QDoubleSpinBox* m_directNormalIlluminanceSpin = nullptr;

    // Radiance Sensor 简化参数。
    QComboBox* m_sensorTypeCombo = nullptr;
    QComboBox* m_layerCombo = nullptr;
    QComboBox* m_observerTypeCombo = nullptr;
    QComboBox* m_sensorObserverTypeCombo = nullptr;
    QDoubleSpinBox* m_sensorFocalSpin = nullptr;
    QDoubleSpinBox* m_sensorXStartSpin = nullptr;
    QDoubleSpinBox* m_sensorXEndSpin = nullptr;
    QSpinBox* m_sensorXSamplingSpin = nullptr;
    QDoubleSpinBox* m_sensorXResolutionSpin = nullptr;
    QCheckBox* m_sensorXMirrorCheck = nullptr;
    QDoubleSpinBox* m_sensorYStartSpin = nullptr;
    QDoubleSpinBox* m_sensorYEndSpin = nullptr;
    QSpinBox* m_sensorYSamplingSpin = nullptr;
    QDoubleSpinBox* m_sensorYResolutionSpin = nullptr;
    QCheckBox* m_sensorYMirrorCheck = nullptr;
    QSpinBox* m_outputWidthSpin = nullptr;
    QSpinBox* m_outputHeightSpin = nullptr;

    // Wavelength / conversion model.
    QDoubleSpinBox* m_wavelengthStartSpin = nullptr;
    QDoubleSpinBox* m_wavelengthEndSpin = nullptr;
    QSpinBox* m_wavelengthSamplingSpin = nullptr;
    QComboBox* m_displayWavelengthCombo = nullptr;
    QDoubleSpinBox* m_spectralTemperatureSpin = nullptr;

    // Viewer Camera：Local Camera + Xc/Yc/Zc + RPY/FOV；相机先在世界 ENU 中生成原点/射线，再与有限天空球求交，因此独立于 CIE E/N/U。
    QCheckBox* m_localCameraCheck = nullptr;
    QDoubleSpinBox* m_cameraXcSpin = nullptr;
    QDoubleSpinBox* m_cameraYcSpin = nullptr;
    QDoubleSpinBox* m_cameraZcSpin = nullptr;
    QDoubleSpinBox* m_cameraAzimuthSpin = nullptr;
    QDoubleSpinBox* m_cameraAltitudeSpin = nullptr;
    QDoubleSpinBox* m_cameraRollSpin = nullptr;
    QDoubleSpinBox* m_cameraHfovSpin = nullptr;
    QDoubleSpinBox* m_cameraVfovSpin = nullptr;
    QPushButton* m_aimSunButton = nullptr;
    QPushButton* m_resetViewButton = nullptr;

    // Result colour scheme and export.
    QComboBox* m_colorSchemeCombo = nullptr;
    QComboBox* m_toneMapCombo = nullptr;
    QDoubleSpinBox* m_referenceLuminanceSpin = nullptr;
    QDoubleSpinBox* m_exposureSpin = nullptr;
    QDoubleSpinBox* m_gammaSpin = nullptr;
    QCheckBox* m_showSunDiskCheck = nullptr;
    QCheckBox* m_showSunGlowCheck = nullptr;
    QCheckBox* m_showHorizonCheck = nullptr;
    QPushButton* m_exportButton = nullptr;
};

#endif // STANDARDSKYVIEWER_H
