#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include "CIEWidget.h"
#include "panorama_processor.h"
class QMenuBar;

class PanoramaLabel;
class OpenGLSceneWidget;
enum class PanoAxis
{
	East,
	North,
	Up
};
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onLoadImage();
    void onUpdateParameters();
    void onPerspectiveViewReady(const QImage& img);
    void createMenu();
    void showSkyViewer();
private:
    void setupUI();
    void setupConnections();
	void updatePanoramaBasis(PanoAxis editedAxis);

    OpenGLSceneWidget* m_openGLWidget;
    PanoramaLabel* m_panoramaLabel;

    // 原有相机参数
    QDoubleSpinBox* m_cxSpin, * m_cySpin, * m_czSpin;
    QDoubleSpinBox* m_yawSpin, * m_pitchSpin, * m_rollSpin;
    QDoubleSpinBox* m_hfovSpin, * m_vfovSpin;
    //QDoubleSpinBox* m_northPanoramaSpin;
    QCheckBox* m_localCameraCheck;
    QCheckBox* m_flipVerticalCheck;
    QSpinBox* m_outWSpin, * m_outHSpin;
    QPushButton* m_loadBtn;
    QLabel* m_perspectiveLabel;

    CIEWidget* m_cieWidget;
    QMenuBar* m_menuBar;

	///////////////////////////////
	QDoubleSpinBox* m_panoEastX;
	QDoubleSpinBox* m_panoEastY;
	QDoubleSpinBox* m_panoEastZ;

	QDoubleSpinBox* m_panoNorthX;
	QDoubleSpinBox* m_panoNorthY;
	QDoubleSpinBox* m_panoNorthZ;

	QDoubleSpinBox* m_panoUpX;
	QDoubleSpinBox* m_panoUpY;
	QDoubleSpinBox* m_panoUpZ;

	PanoramaBasis m_panoramaBasis;

	PanoAxis m_prevPanoAxis = PanoAxis::North;
	PanoAxis m_lastPanoAxis = PanoAxis::Up;

	bool m_updatingPanoramaUI = false;


    HDRImage m_panorama;
    bool m_hasPanorama;
};

#endif // MAINWINDOW_H