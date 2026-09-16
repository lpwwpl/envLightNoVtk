#include "StandardSkyViewer.h"

#include "SunSky.hpp"
#include "SkySceneWidget.h"
#include "environment_light.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

struct CityLocation
{
    const char* name;
    const char* country;
    double latitude;
    double longitude;
    const char* timeZoneId;
};

// 离线 Zone 数据库与早期版本一致，Map 选择器不依赖 QtLocation、QtWebEngine 或在线地图服务。
static const CityLocation kCityLocations[] = {
    {"ANSYS example", "France", 43.086667, 6.048889, "Europe/Paris"},
    {"Paris", "France", 48.8566, 2.3522, "Europe/Paris"},
    {"London", "United Kingdom", 51.5074, -0.1278, "Europe/London"},
    {"Berlin", "Germany", 52.5200, 13.4050, "Europe/Berlin"},
    {"Madrid", "Spain", 40.4168, -3.7038, "Europe/Madrid"},
    {"Rome", "Italy", 41.9028, 12.4964, "Europe/Rome"},
    {"Stockholm", "Sweden", 59.3293, 18.0686, "Europe/Stockholm"},
    {"Moscow", "Russia", 55.7558, 37.6173, "Europe/Moscow"},
    {"Istanbul", "Turkey", 41.0082, 28.9784, "Europe/Istanbul"},
    {"Cairo", "Egypt", 30.0444, 31.2357, "Africa/Cairo"},
    {"Casablanca", "Morocco", 33.5731, -7.5898, "Africa/Casablanca"},
    {"Lagos", "Nigeria", 6.5244, 3.3792, "Africa/Lagos"},
    {"Nairobi", "Kenya", -1.2921, 36.8219, "Africa/Nairobi"},
    {"Johannesburg", "South Africa", -26.2041, 28.0473, "Africa/Johannesburg"},
    {"Dubai", "UAE", 25.2048, 55.2708, "Asia/Dubai"},
    {"Riyadh", "Saudi Arabia", 24.7136, 46.6753, "Asia/Riyadh"},
    {"Delhi", "India", 28.6139, 77.2090, "Asia/Kolkata"},
    {"Mumbai", "India", 19.0760, 72.8777, "Asia/Kolkata"},
    {"Bangkok", "Thailand", 13.7563, 100.5018, "Asia/Bangkok"},
    {"Singapore", "Singapore", 1.3521, 103.8198, "Asia/Singapore"},
    {"Jakarta", "Indonesia", -6.2088, 106.8456, "Asia/Jakarta"},
    {"Beijing", "China", 39.9042, 116.4074, "Asia/Shanghai"},
    {"Shanghai", "China", 31.2304, 121.4737, "Asia/Shanghai"},
    {"Guangzhou", "China", 23.1291, 113.2644, "Asia/Shanghai"},
    {"Shenzhen", "China", 22.5431, 114.0579, "Asia/Shanghai"},
    {"Chengdu", "China", 30.5728, 104.0668, "Asia/Shanghai"},
    {"Chongqing", "China", 29.4316, 106.9123, "Asia/Shanghai"},
    {"Wuhan", "China", 30.5928, 114.3055, "Asia/Shanghai"},
    {"Xi'an", "China", 34.3416, 108.9398, "Asia/Shanghai"},
    {"Kunming", "China", 25.0389, 102.7183, "Asia/Shanghai"},
    {"Urumqi", "China", 43.8256, 87.6168, "Asia/Shanghai"},
    {"Hong Kong", "China", 22.3193, 114.1694, "Asia/Hong_Kong"},
    {"Taipei", "Taiwan", 25.0330, 121.5654, "Asia/Taipei"},
    {"Seoul", "South Korea", 37.5665, 126.9780, "Asia/Seoul"},
    {"Tokyo", "Japan", 35.6762, 139.6503, "Asia/Tokyo"},
    {"Sydney", "Australia", -33.8688, 151.2093, "Australia/Sydney"},
    {"Melbourne", "Australia", -37.8136, 144.9631, "Australia/Melbourne"},
    {"Auckland", "New Zealand", -36.8509, 174.7645, "Pacific/Auckland"},
    {"New York", "USA", 40.7128, -74.0060, "America/New_York"},
    {"Boston", "USA", 42.3601, -71.0589, "America/New_York"},
    {"Chicago", "USA", 41.8781, -87.6298, "America/Chicago"},
    {"Denver", "USA", 39.7392, -104.9903, "America/Denver"},
    {"Los Angeles", "USA", 34.0522, -118.2437, "America/Los_Angeles"},
    {"San Francisco", "USA", 37.7749, -122.4194, "America/Los_Angeles"},
    {"Anchorage", "USA", 61.2181, -149.9003, "America/Anchorage"},
    {"Honolulu", "USA", 21.3069, -157.8583, "Pacific/Honolulu"},
    {"Toronto", "Canada", 43.6532, -79.3832, "America/Toronto"},
    {"Vancouver", "Canada", 49.2827, -123.1207, "America/Vancouver"},
    {"Mexico City", "Mexico", 19.4326, -99.1332, "America/Mexico_City"},
    {"Bogota", "Colombia", 4.7110, -74.0721, "America/Bogota"},
    {"Lima", "Peru", -12.0464, -77.0428, "America/Lima"},
    {"Santiago", "Chile", -33.4489, -70.6693, "America/Santiago"},
    {"Buenos Aires", "Argentina", -34.6037, -58.3816, "America/Argentina/Buenos_Aires"},
    {"Sao Paulo", "Brazil", -23.5505, -46.6333, "America/Sao_Paulo"}
};

constexpr int kCityLocationCount = static_cast<int>(sizeof(kCityLocations) / sizeof(kCityLocations[0]));

QString cityDisplayName(int index)
{
    if (index < 0 || index >= kCityLocationCount)
    {
        return QString();
    }
    return QString::fromUtf8(kCityLocations[index].name) + QString::fromUtf8(", ") + QString::fromUtf8(kCityLocations[index].country);
}

// 功能：把地图点击位置吸附到最近的内置城市，从而得到稳定的 IANA Zone、经纬度和 DST 规则。
int nearestCityIndex(double longitude, double latitude)
{
    int bestIndex = 0;
    double bestDistance = std::numeric_limits<double>::max();
    const double cosLatitude = std::max(0.15, std::cos(latitude * kDegToRad));
    for (int i = 0; i < kCityLocationCount; ++i)
    {
        double longitudeDelta = kCityLocations[i].longitude - longitude;
        if (longitudeDelta > 180.0)
        {
            longitudeDelta -= 360.0;
        }
        else if (longitudeDelta < -180.0)
        {
            longitudeDelta += 360.0;
        }
        const double x = longitudeDelta * cosLatitude;
        const double y = kCityLocations[i].latitude - latitude;
        const double distanceSquared = x * x + y * y;
        if (distanceSquared < bestDistance)
        {
            bestDistance = distanceSquared;
            bestIndex = i;
        }
    }
    return bestIndex;
}

class ZoneSelectionReceiver
{
public:
    virtual ~ZoneSelectionReceiver() = default;
    virtual void onZoneMapSelection(int index) = 0;
};

// 功能：恢复早期版本的离线世界 Zone 地图；地图仅用于定位和选择，不承担 GIS 精确边界功能。
class WorldZoneMapWidget final : public QWidget
{
public:
    explicit WorldZoneMapWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(720, 420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Click the map to select the nearest built-in city/time zone."));
    }

    void setSelectionReceiver(ZoneSelectionReceiver* receiver)
    {
        m_receiver = receiver;
    }

    void setSelectedIndex(int index)
    {
        if (index < 0 || index >= kCityLocationCount || m_selectedIndex == index)
        {
            return;
        }
        m_selectedIndex = index;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF map = mapRect();
        painter.fillRect(rect(), QColor(247, 248, 250));
        painter.fillRect(map, QColor(222, 238, 249));
        painter.setPen(QPen(QColor(145, 164, 178), 1.0));
        painter.drawRect(map);

        // 经纬网帮助用户辨认大致位置；城市经纬度和 IANA Zone 才是后续太阳计算真正使用的数据。
        painter.setPen(QPen(QColor(190, 205, 216), 0.8, Qt::DashLine));
        for (int longitude = -150; longitude <= 150; longitude += 30)
        {
            painter.drawLine(geoPoint(longitude, -90.0), geoPoint(longitude, 90.0));
        }
        for (int latitude = -60; latitude <= 60; latitude += 30)
        {
            painter.drawLine(geoPoint(-180.0, latitude), geoPoint(180.0, latitude));
        }
        painter.setPen(QPen(QColor(145, 164, 178), 1.2));
        painter.drawLine(geoPoint(-180.0, 0.0), geoPoint(180.0, 0.0));
        painter.drawLine(geoPoint(0.0, -90.0), geoPoint(0.0, 90.0));

        // 大陆轮廓沿用早期版本的离线示意图，仅用于视觉定位，不代表精确行政或时区边界。
        painter.setPen(QPen(QColor(116, 134, 104), 1.0));
        painter.setBrush(QColor(219, 226, 204));
        drawGeoPolygon(painter, {{-168, 72}, {-145, 66}, {-128, 55}, {-125, 40}, {-115, 30}, {-100, 19}, {-84, 10}, {-75, 20}, {-62, 45}, {-80, 58}, {-105, 70}, {-140, 72}});
        drawGeoPolygon(painter, {{-82, 12}, {-70, 8}, {-52, -2}, {-45, -20}, {-55, -38}, {-68, -55}, {-76, -32}, {-80, -8}});
        drawGeoPolygon(painter, {{-11, 35}, {-5, 55}, {12, 70}, {30, 70}, {45, 58}, {40, 43}, {20, 35}});
        drawGeoPolygon(painter, {{-18, 35}, {10, 37}, {34, 31}, {50, 10}, {42, -34}, {18, -35}, {2, -22}, {-12, 5}});
        drawGeoPolygon(painter, {{25, 72}, {65, 76}, {110, 72}, {160, 62}, {170, 48}, {145, 35}, {125, 20}, {105, 5}, {80, 8}, {58, 25}, {42, 43}});
        drawGeoPolygon(painter, {{112, -11}, {154, -12}, {153, -39}, {132, -44}, {115, -35}, {110, -22}});
        drawGeoPolygon(painter, {{-52, 82}, {-22, 75}, {-28, 60}, {-48, 60}, {-62, 72}});

        for (int i = 0; i < kCityLocationCount; ++i)
        {
            const QPointF point = geoPoint(kCityLocations[i].longitude, kCityLocations[i].latitude);
            if (i == m_selectedIndex)
            {
                painter.setPen(QPen(QColor(190, 30, 30), 2.0));
                painter.setBrush(QColor(255, 245, 245));
                painter.drawEllipse(point, 7.0, 7.0);
                painter.setBrush(QColor(190, 30, 30));
                painter.drawEllipse(point, 3.0, 3.0);
            }
            else
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(45, 94, 145, 185));
                painter.drawEllipse(point, 2.6, 2.6);
            }
        }

        if (m_selectedIndex >= 0 && m_selectedIndex < kCityLocationCount)
        {
            const CityLocation& city = kCityLocations[m_selectedIndex];
            const QPointF point = geoPoint(city.longitude, city.latitude);
            const QString label = cityDisplayName(m_selectedIndex);
            QFont font = painter.font();
            font.setBold(true);
            painter.setFont(font);
            const QFontMetrics metrics(font);
            const QSize textSize = metrics.size(Qt::TextSingleLine, label);
            QRectF bubble(point.x() + 10.0, point.y() - textSize.height() - 9.0, textSize.width() + 12.0, textSize.height() + 8.0);
            if (bubble.right() > map.right())
            {
                bubble.moveLeft(point.x() - bubble.width() - 10.0);
            }
            if (bubble.top() < map.top())
            {
                bubble.moveTop(point.y() + 10.0);
            }
            painter.setPen(QPen(QColor(80, 80, 80), 1.0));
            painter.setBrush(QColor(255, 255, 255, 235));
            painter.drawRoundedRect(bubble, 4.0, 4.0);
            painter.setPen(QColor(35, 35, 35));
            painter.drawText(bubble.adjusted(6.0, 4.0, -6.0, -4.0), Qt::AlignLeft | Qt::AlignVCenter, label);
        }

        painter.setPen(QColor(80, 90, 100));
        painter.drawText(QRectF(map.left(), map.bottom() + 5.0, map.width(), 20.0), Qt::AlignCenter, tr("Click anywhere: selection snaps to the nearest built-in city / IANA time zone"));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPointF position = event->position();
#else
        const QPointF position = event->localPos();
#endif
        const QRectF map = mapRect();
        if (!map.contains(position))
        {
            QWidget::mousePressEvent(event);
            return;
        }
        const double longitude = ((position.x() - map.left()) / map.width()) * 360.0 - 180.0;
        const double latitude = 90.0 - ((position.y() - map.top()) / map.height()) * 180.0;
        const int index = nearestCityIndex(longitude, latitude);
        setSelectedIndex(index);
        if (m_receiver)
        {
            m_receiver->onZoneMapSelection(index);
        }
    }

private:
    QRectF mapRect() const
    {
        return QRectF(rect()).adjusted(12.0, 12.0, -12.0, -32.0);
    }

    QPointF geoPoint(double longitude, double latitude) const
    {
        const QRectF map = mapRect();
        const double x = map.left() + (longitude + 180.0) / 360.0 * map.width();
        const double y = map.top() + (90.0 - latitude) / 180.0 * map.height();
        return QPointF(x, y);
    }

    void drawGeoPolygon(QPainter& painter, std::initializer_list<QPointF> longitudeLatitudePoints)
    {
        QPolygonF polygon;
        polygon.reserve(static_cast<int>(longitudeLatitudePoints.size()));
        for (const QPointF& point : longitudeLatitudePoints)
        {
            polygon << geoPoint(point.x(), point.y());
        }
        painter.drawPolygon(polygon);
    }

    int m_selectedIndex = 0;
    ZoneSelectionReceiver* m_receiver = nullptr;
};

// 功能：恢复早期版本的“地图 + 搜索列表 + 当前 Zone 信息”选择器，避免简化版下拉框丢失空间定位体验。
class LocationPickerDialog final : public QDialog, private ZoneSelectionReceiver
{
public:
    explicit LocationPickerDialog(double currentLongitude, double currentLatitude, QWidget* parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(tr("Choose Zone on Map"));
        resize(1120, 650);
        setModal(true);
        QVBoxLayout* root = new QVBoxLayout(this);
        QLabel* help = new QLabel(tr("Select a city/time-zone from the offline map or search list. The selected Zone, longitude, latitude and IANA time zone are returned together."));
        help->setWordWrap(true);
        root->addWidget(help);
        QHBoxLayout* body = new QHBoxLayout;
        root->addLayout(body, 1);
        m_map = new WorldZoneMapWidget(this);
        m_map->setSelectionReceiver(this);
        body->addWidget(m_map, 1);
        QWidget* side = new QWidget(this);
        side->setMinimumWidth(285);
        side->setMaximumWidth(360);
        QVBoxLayout* sideLayout = new QVBoxLayout(side);
        sideLayout->setContentsMargins(0, 0, 0, 0);
        QLabel* searchLabel = new QLabel(tr("Search city / country"), side);
        m_search = new QLineEdit(side);
        m_search->setPlaceholderText(tr("e.g. Shanghai, Paris, USA"));
        sideLayout->addWidget(searchLabel);
        sideLayout->addWidget(m_search);
        m_list = new QListWidget(side);
        m_list->setAlternatingRowColors(true);
        for (int i = 0; i < kCityLocationCount; ++i)
        {
            QListWidgetItem* item = new QListWidgetItem(cityDisplayName(i), m_list);
            item->setData(Qt::UserRole, i);
            item->setToolTip(QString::fromUtf8(kCityLocations[i].timeZoneId));
        }
        sideLayout->addWidget(m_list, 1);
        QGroupBox* selectedGroup = new QGroupBox(tr("Selected location"), side);
        QFormLayout* selectedForm = new QFormLayout(selectedGroup);
        m_zoneLabel = new QLabel;
        m_zoneLabel->setWordWrap(true);
        m_timeZoneLabel = new QLabel;
        m_timeZoneLabel->setWordWrap(true);
        m_coordinateLabel = new QLabel;
        selectedForm->addRow(tr("Zone"), m_zoneLabel);
        selectedForm->addRow(tr("Time zone"), m_timeZoneLabel);
        selectedForm->addRow(tr("Coordinates"), m_coordinateLabel);
        sideLayout->addWidget(selectedGroup);
        body->addWidget(side);
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Use selected Zone"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_search, &QLineEdit::textChanged, this, &LocationPickerDialog::onSearchTextChanged);
        connect(m_list, &QListWidget::currentItemChanged, this, &LocationPickerDialog::onListSelectionChanged);
        root->addWidget(buttons);
        setSelectedIndex(nearestCityIndex(currentLongitude, currentLatitude), true);
    }

    QString zoneName() const
    {
        return cityDisplayName(m_selectedIndex);
    }

    QString timeZoneId() const
    {
        return QString::fromUtf8(kCityLocations[m_selectedIndex].timeZoneId);
    }

    double longitude() const
    {
        return kCityLocations[m_selectedIndex].longitude;
    }

    double latitude() const
    {
        return kCityLocations[m_selectedIndex].latitude;
    }

private:
    void onZoneMapSelection(int index) override
    {
        setSelectedIndex(index, true);
    }

    void onSearchTextChanged(const QString& text)
    {
        const QString needle = text.trimmed();
        for (int row = 0; row < m_list->count(); ++row)
        {
            QListWidgetItem* item = m_list->item(row);
            item->setHidden(!needle.isEmpty() && !item->text().contains(needle, Qt::CaseInsensitive));
        }
    }

    void onListSelectionChanged(QListWidgetItem* current, QListWidgetItem*)
    {
        if (!current)
        {
            return;
        }
        setSelectedIndex(current->data(Qt::UserRole).toInt(), false);
    }

    void setSelectedIndex(int index, bool syncList)
    {
        if (index < 0 || index >= kCityLocationCount)
        {
            return;
        }
        m_selectedIndex = index;
        m_map->setSelectedIndex(index);
        if (syncList)
        {
            for (int row = 0; row < m_list->count(); ++row)
            {
                QListWidgetItem* item = m_list->item(row);
                if (item->data(Qt::UserRole).toInt() == index)
                {
                    const QSignalBlocker blocker(m_list);
                    m_list->setCurrentItem(item);
                    m_list->scrollToItem(item, QAbstractItemView::PositionAtCenter);
                    break;
                }
            }
        }
        const CityLocation& city = kCityLocations[index];
        m_zoneLabel->setText(cityDisplayName(index));
        m_timeZoneLabel->setText(QString::fromUtf8(city.timeZoneId));
        m_coordinateLabel->setText(tr("Lon %1°, Lat %2°").arg(city.longitude, 0, 'f', 4).arg(city.latitude, 0, 'f', 4));
    }

    WorldZoneMapWidget* m_map = nullptr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_zoneLabel = nullptr;
    QLabel* m_timeZoneLabel = nullptr;
    QLabel* m_coordinateLabel = nullptr;
    int m_selectedIndex = 0;
};

QDoubleSpinBox* makeAngleSpin(double minimum, double maximum, double value, double step)
{
    QDoubleSpinBox* spin = new QDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setDecimals(2);
    spin->setSingleStep(step);
    spin->setSuffix(QString::fromUtf8("°"));
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
}

QDoubleSpinBox* makeVectorSpin(double value)
{
    QDoubleSpinBox* spin = new QDoubleSpinBox;
    spin->setRange(-1.0, 1.0);
    spin->setDecimals(6);
    spin->setSingleStep(0.01);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
}

QDoubleSpinBox* makeCameraPositionSpin(double value)
{
    QDoubleSpinBox* spin = new QDoubleSpinBox;
    spin->setRange(-1.0, 1.0);
    spin->setDecimals(4);
    spin->setSingleStep(0.05);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
}

QSpinBox* makeSamplingSpin(int value)
{
    QSpinBox* spin = new QSpinBox;
    spin->setRange(64, 8192);
    spin->setSingleStep(64);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
}

QWidget* makeVectorRow(QWidget* parent, QDoubleSpinBox*& xSpin, QDoubleSpinBox*& ySpin, QDoubleSpinBox*& zSpin, double x, double y, double z)
{
    QWidget* row = new QWidget(parent);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    xSpin = makeVectorSpin(x);
    ySpin = makeVectorSpin(y);
    zSpin = makeVectorSpin(z);
    layout->addWidget(xSpin);
    layout->addWidget(ySpin);
    layout->addWidget(zSpin);
    return row;
}

Direction environmentDirectionFromENU(const QVector3D& direction)
{
    QVector3D d = direction;
    if (d.lengthSquared() < 1.0e-12f)
    {
        d = QVector3D(0.0f, 0.0f, 1.0f);
    }
    d.normalize();
    const double theta = std::acos(std::max(-1.0, std::min(1.0, static_cast<double>(d.z()))));
    double phi = std::atan2(static_cast<double>(d.x()), static_cast<double>(d.y()));
    if (phi < 0.0)
    {
        phi += 2.0 * kPi;
    }
    return Direction(theta, phi);
}
}

StandardSkyViewer::StandardSkyViewer(QWidget* parent) : QMainWindow(parent)
{
    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(50);
    connect(m_renderTimer, &QTimer::timeout, this, &StandardSkyViewer::updateRender);
    setupUi();
    onSunModeChanged();
    updateSensorUiState();
    updateRender();
}

StandardSkyViewer::~StandardSkyViewer() = default;

void StandardSkyViewer::connectRenderSpin(QDoubleSpinBox* spin)
{
    if (!spin)
    {
        return;
    }
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StandardSkyViewer::scheduleRender);
}

void StandardSkyViewer::connectRenderIntSpin(QSpinBox* spin)
{
    if (!spin)
    {
        return;
    }
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StandardSkyViewer::scheduleRender);
}

void StandardSkyViewer::connectRenderCombo(QComboBox* combo)
{
    if (!combo)
    {
        return;
    }
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StandardSkyViewer::scheduleRender);
}

void StandardSkyViewer::connectRenderCheck(QCheckBox* check)
{
    if (!check)
    {
        return;
    }
    connect(check, &QCheckBox::toggled, this, &StandardSkyViewer::scheduleRender);
}

void StandardSkyViewer::setupUi()
{
    setWindowTitle(tr("CIE Standard General Sky"));
    resize(1080, 760);

    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QHBoxLayout* rootLayout = new QHBoxLayout(central);

    QScrollArea* parameterScroll = new QScrollArea(central);
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setMinimumWidth(420);
    parameterScroll->setMaximumWidth(500);
    rootLayout->addWidget(parameterScroll);

    QWidget* parameterPanel = new QWidget;
    QVBoxLayout* parameterPanelLayout = new QVBoxLayout(parameterPanel);
    parameterPanelLayout->setContentsMargins(4, 4, 4, 4);
    parameterScroll->setWidget(parameterPanel);

    QTabWidget* propertyTabs = new QTabWidget(parameterPanel);
    QWidget* skyLocationTab = new QWidget(propertyTabs);
    QWidget* sensorViewerTab = new QWidget(propertyTabs);
    QVBoxLayout* skyLayout = new QVBoxLayout(skyLocationTab);
    QVBoxLayout* sensorLayout = new QVBoxLayout(sensorViewerTab);
    propertyTabs->addTab(skyLocationTab, tr("Sky && Location"));
    propertyTabs->addTab(sensorViewerTab, tr("Sensor && Viewer"));
    parameterPanelLayout->addWidget(propertyTabs);

    // CIE 只需要天空类型、绝对天顶亮度和一个 ENU 基；15 类天空的分布公式直接交给 environment_light.h 中的 CIESkyModel。
    QGroupBox* cieGroup = new QGroupBox(tr("CIE 全局参数"));
    QFormLayout* cieLayout = new QFormLayout(cieGroup);
    m_cieTypeCombo = new QComboBox;
    for (int type = 0; type < 15; ++type)
    {
        m_cieTypeCombo->addItem(QString("%1. %2").arg(type + 1, 2, 10, QChar('0')).arg(skyTypeDescription(type)), type);
    }
    m_cieTypeCombo->setCurrentIndex(11);
    m_luminanceSpin = new QDoubleSpinBox;
    m_luminanceSpin->setRange(0.0, 100000000.0);
    m_luminanceSpin->setDecimals(3);
    m_luminanceSpin->setSingleStep(100.0);
    m_luminanceSpin->setSuffix(" cd/m²");
    m_luminanceSpin->setValue(1000.0);
    m_luminanceSpin->setKeyboardTracking(false);
    m_sunModeCombo = new QComboBox;
    m_sunModeCombo->addItem(tr("Automatic"), 0);
    m_sunModeCombo->addItem(tr("Manual"), 1);
    cieLayout->addRow(tr("CIE Type"), m_cieTypeCombo);
    cieLayout->addRow(tr("Zenith luminance"), m_luminanceSpin);
    cieLayout->addRow(tr("Sun type"), m_sunModeCombo);
    cieLayout->addRow(tr("ENU E 向量"), makeVectorRow(cieGroup, m_eastXSpin, m_eastYSpin, m_eastZSpin, 1.0, 0.0, 0.0));
    cieLayout->addRow(tr("ENU N 向量"), makeVectorRow(cieGroup, m_northXSpin, m_northYSpin, m_northZSpin, 0.0, 1.0, 0.0));
    cieLayout->addRow(tr("ENU U 向量"), makeVectorRow(cieGroup, m_upXSpin, m_upYSpin, m_upZSpin, 0.0, 0.0, 1.0));
    skyLayout->addWidget(cieGroup);

    QGroupBox* locationGroup = new QGroupBox(tr("Time zone and location"));
    QFormLayout* locationLayout = new QFormLayout(locationGroup);
    m_zoneEdit = new QLineEdit(tr("ANSYS example, France"));
    m_zoneEdit->setReadOnly(true);
    m_zoneEdit->setToolTip(tr("Zone is selected from the offline map. Click Map... to change city / time zone."));
    m_zoneMapButton = new QPushButton(tr("Map..."));
    QWidget* zoneRow = new QWidget(locationGroup);
    QHBoxLayout* zoneRowLayout = new QHBoxLayout(zoneRow);
    zoneRowLayout->setContentsMargins(0, 0, 0, 0);
    zoneRowLayout->addWidget(m_zoneEdit, 1);
    zoneRowLayout->addWidget(m_zoneMapButton);
    m_timeZoneSpin = new QDoubleSpinBox;
    m_timeZoneSpin->setRange(-12.0, 14.0);
    m_timeZoneSpin->setDecimals(2);
    m_timeZoneSpin->setSingleStep(0.5);
    m_timeZoneSpin->setPrefix("UTC ");
    m_timeZoneSpin->setValue(2.0);
    m_timeZoneSpin->setKeyboardTracking(false);
    m_autoTimeZoneCheck = new QCheckBox(tr("Automatic from Zone (DST aware)"));
    m_autoTimeZoneCheck->setChecked(true);
    m_timeZoneId = QString::fromUtf8("Europe/Paris");
    m_timeZoneIdLabel = new QLabel(m_timeZoneId);
    m_longitudeSpin = makeAngleSpin(-180.0, 180.0, 6.048889, 0.1);
    m_longitudeSpin->setDecimals(6);
    m_latitudeSpin = makeAngleSpin(-90.0, 90.0, 43.086667, 0.1);
    m_latitudeSpin->setDecimals(6);
    m_dateTimeEdit = new QDateTimeEdit(QDateTime(QDate(2026, 9, 7), QTime(9, 55, 59)));
    m_dateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_dateTimeEdit->setCalendarPopup(true);
    locationLayout->addRow(tr("Zone"), zoneRow);
    locationLayout->addRow(tr("Time zone"), m_timeZoneSpin);
    locationLayout->addRow(QString(), m_autoTimeZoneCheck);
    locationLayout->addRow(tr("IANA zone"), m_timeZoneIdLabel);
    locationLayout->addRow(tr("Longitude"), m_longitudeSpin);
    locationLayout->addRow(tr("Latitude"), m_latitudeSpin);
    locationLayout->addRow(tr("Date and time"), m_dateTimeEdit);
    skyLayout->addWidget(locationGroup);

    QGroupBox* sunGroup = new QGroupBox(tr("Sun"));
    QFormLayout* sunLayout = new QFormLayout(sunGroup);
    m_manualSunAzimuthSpin = makeAngleSpin(0.0, 359.99, 180.0, 1.0);
    m_manualSunAltitudeSpin = makeAngleSpin(-89.9, 89.9, 25.0, 1.0);
    m_directNormalIlluminanceSpin = new QDoubleSpinBox;
    m_directNormalIlluminanceSpin->setRange(0.0, 200000.0);
    m_directNormalIlluminanceSpin->setDecimals(1);
    m_directNormalIlluminanceSpin->setSingleStep(1000.0);
    m_directNormalIlluminanceSpin->setSuffix(" lx");
    m_directNormalIlluminanceSpin->setValue(50000.0);
    m_directNormalIlluminanceSpin->setKeyboardTracking(false);
    sunLayout->addRow(tr("Manual sun Az"), m_manualSunAzimuthSpin);
    sunLayout->addRow(tr("Manual sun Alt"), m_manualSunAltitudeSpin);
    sunLayout->addRow(tr("Direct normal illuminance"), m_directNormalIlluminanceSpin);
    skyLayout->addWidget(sunGroup);
    skyLayout->addStretch();

    QGroupBox* sensorGroup = new QGroupBox(tr("Radiance Sensor"));
    QFormLayout* sensorForm = new QFormLayout(sensorGroup);
    m_sensorTypeCombo = new QComboBox;
    m_sensorTypeCombo->addItem(tr("Photometric"), static_cast<int>(SkyMeasurementType::Photometric));
    m_sensorTypeCombo->addItem(tr("Radiometric"), static_cast<int>(SkyMeasurementType::Radiometric));
    m_sensorTypeCombo->addItem(tr("Colorimetric"), static_cast<int>(SkyMeasurementType::Colorimetric));
    m_sensorTypeCombo->addItem(tr("Spectral"), static_cast<int>(SkyMeasurementType::Spectral));
    m_layerCombo = new QComboBox;
    m_layerCombo->addItem(tr("Combined sky + sun"), static_cast<int>(SkyMeasurementLayer::Combined));
    m_layerCombo->addItem(tr("Diffuse sky only"), static_cast<int>(SkyMeasurementLayer::DiffuseSkyOnly));
    m_layerCombo->addItem(tr("Direct sun only"), static_cast<int>(SkyMeasurementLayer::DirectSunOnly));
    m_observerTypeCombo = new QComboBox;
    m_observerTypeCombo->addItem(tr("CIE 1931 2°"), 0);
    m_outputWidthSpin = makeSamplingSpin(800);
    m_outputHeightSpin = makeSamplingSpin(600);
    sensorForm->addRow(tr("General Type"), m_sensorTypeCombo);
    sensorForm->addRow(tr("Layer"), m_layerCombo);
    sensorForm->addRow(tr("Observer"), m_observerTypeCombo);
    sensorForm->addRow(tr("Output width"), m_outputWidthSpin);
    sensorForm->addRow(tr("Output height"), m_outputHeightSpin);
    sensorLayout->addWidget(sensorGroup);

    QGroupBox* wavelengthGroup = new QGroupBox(tr("Wavelength"));
    QFormLayout* wavelengthLayout = new QFormLayout(wavelengthGroup);
    m_wavelengthStartSpin = new QDoubleSpinBox;
    m_wavelengthStartSpin->setRange(350.0, 2000.0);
    m_wavelengthStartSpin->setDecimals(1);
    m_wavelengthStartSpin->setSuffix(" nm");
    m_wavelengthStartSpin->setValue(400.0);
    m_wavelengthEndSpin = new QDoubleSpinBox;
    m_wavelengthEndSpin->setRange(350.0, 2000.0);
    m_wavelengthEndSpin->setDecimals(1);
    m_wavelengthEndSpin->setSuffix(" nm");
    m_wavelengthEndSpin->setValue(700.0);
    m_wavelengthSamplingSpin = new QSpinBox;
    m_wavelengthSamplingSpin->setRange(3, 401);
    m_wavelengthSamplingSpin->setValue(13);
    m_displayWavelengthCombo = new QComboBox;
    m_spectralTemperatureSpin = new QDoubleSpinBox;
    m_spectralTemperatureSpin->setRange(1000.0, 20000.0);
    m_spectralTemperatureSpin->setDecimals(0);
    m_spectralTemperatureSpin->setSuffix(" K");
    m_spectralTemperatureSpin->setValue(6500.0);
    wavelengthLayout->addRow(tr("Start"), m_wavelengthStartSpin);
    wavelengthLayout->addRow(tr("End"), m_wavelengthEndSpin);
    wavelengthLayout->addRow(tr("Sampling"), m_wavelengthSamplingSpin);
    wavelengthLayout->addRow(tr("Display wavelength"), m_displayWavelengthCombo);
    wavelengthLayout->addRow(tr("Spectral temperature"), m_spectralTemperatureSpin);
    sensorLayout->addWidget(wavelengthGroup);

    // Viewer Camera 与主界面保持同一套参数：Local Camera 决定 Xc/Yc/Zc 与 RPY 使用局部相机语义还是 ENU 导航语义。
    QGroupBox* cameraGroup = new QGroupBox(tr("Viewer Camera"));
    QFormLayout* cameraLayout = new QFormLayout(cameraGroup);
    m_localCameraCheck = new QCheckBox(tr("Local Camera"));
    m_localCameraCheck->setChecked(true);
    m_localCameraCheck->setToolTip(tr("Checked: Xc/Yc/Zc local camera convention. Unchecked: ENU navigation camera, Az=0° North and 90° East."));
    m_cameraXcSpin = makeCameraPositionSpin(0.0);
    m_cameraYcSpin = makeCameraPositionSpin(0.0);
    m_cameraZcSpin = makeCameraPositionSpin(0.0);
    m_cameraXcSpin->setToolTip(tr("Finite viewer sky sphere radius is 1.0. Local Camera=true uses camera-local Xc/Yc/Zc translation; false uses ENU E/N/U translation. Translation changes the sphere hit direction, not the CIE sky model itself."));
    m_cameraYcSpin->setToolTip(m_cameraXcSpin->toolTip());
    m_cameraZcSpin->setToolTip(m_cameraXcSpin->toolTip());
    m_cameraAzimuthSpin = makeAngleSpin(-180.0, 359.9, 0.0, 5.0);
    m_cameraAltitudeSpin = makeAngleSpin(-180.0, 180.0, 20.0, 5.0);
    m_cameraRollSpin = makeAngleSpin(-180.0, 180.0, 0.0, 1.0);
    m_cameraHfovSpin = makeAngleSpin(10.0, 170.0, 90.0, 1.0);
    m_cameraVfovSpin = makeAngleSpin(10.0, 170.0, 60.0, 1.0);
    m_aimSunButton = new QPushButton(tr("Aim at Sun"));
    m_resetViewButton = new QPushButton(tr("Reset Camera"));
    QWidget* cameraButtons = new QWidget(cameraGroup);
    QHBoxLayout* cameraButtonLayout = new QHBoxLayout(cameraButtons);
    cameraButtonLayout->setContentsMargins(0, 0, 0, 0);
    cameraButtonLayout->addWidget(m_aimSunButton);
    cameraButtonLayout->addWidget(m_resetViewButton);
    cameraLayout->addRow(tr("Mode"), m_localCameraCheck);
    cameraLayout->addRow(tr("Xc"), m_cameraXcSpin);
    cameraLayout->addRow(tr("Yc"), m_cameraYcSpin);
    cameraLayout->addRow(tr("Zc"), m_cameraZcSpin);
    cameraLayout->addRow(tr("Yaw / Azimuth"), m_cameraAzimuthSpin);
    cameraLayout->addRow(tr("Pitch / Altitude"), m_cameraAltitudeSpin);
    cameraLayout->addRow(tr("Roll"), m_cameraRollSpin);
    cameraLayout->addRow(tr("HFOV"), m_cameraHfovSpin);
    cameraLayout->addRow(tr("VFOV"), m_cameraVfovSpin);
    cameraLayout->addRow(cameraButtons);
    sensorLayout->addWidget(cameraGroup);

    QGroupBox* displayGroup = new QGroupBox(tr("Display && Export"));
    QFormLayout* displayLayout = new QFormLayout(displayGroup);
    m_colorSchemeCombo = new QComboBox;
    m_colorSchemeCombo->addItem(tr("Natural preview"), static_cast<int>(SkyColorMode::NaturalPreview));
    m_colorSchemeCombo->addItem(tr("Grayscale"), static_cast<int>(SkyColorMode::GrayscaleLuminance));
    m_colorSchemeCombo->addItem(tr("False color"), static_cast<int>(SkyColorMode::FalseColor));
    m_toneMapCombo = new QComboBox;
    m_toneMapCombo->addItem(tr("Fixed reference"), static_cast<int>(SkyToneMapMode::FixedReference));
    m_toneMapCombo->addItem(tr("Auto peak"), static_cast<int>(SkyToneMapMode::AutoPeak));
    m_referenceLuminanceSpin = new QDoubleSpinBox;
    m_referenceLuminanceSpin->setRange(0.000001, 1000000000.0);
    m_referenceLuminanceSpin->setDecimals(4);
    m_referenceLuminanceSpin->setValue(1000.0);
    m_exposureSpin = new QDoubleSpinBox;
    m_exposureSpin->setRange(0.001, 100.0);
    m_exposureSpin->setDecimals(3);
    m_exposureSpin->setValue(1.0);
    m_gammaSpin = new QDoubleSpinBox;
    m_gammaSpin->setRange(0.1, 5.0);
    m_gammaSpin->setDecimals(2);
    m_gammaSpin->setValue(2.2);
    m_showSunDiskCheck = new QCheckBox(tr("Show sun disk"));
    m_showSunDiskCheck->setChecked(true);
    m_showSunGlowCheck = new QCheckBox(tr("Show sun glow"));
    m_showSunGlowCheck->setChecked(true);
    m_showHorizonCheck = new QCheckBox(tr("Show horizon"));
    m_showHorizonCheck->setChecked(true);
    m_exportButton = new QPushButton;
    displayLayout->addRow(tr("Color"), m_colorSchemeCombo);
    displayLayout->addRow(tr("Tone mapping"), m_toneMapCombo);
    displayLayout->addRow(tr("Reference"), m_referenceLuminanceSpin);
    displayLayout->addRow(tr("Exposure"), m_exposureSpin);
    displayLayout->addRow(tr("Gamma"), m_gammaSpin);
    displayLayout->addRow(QString(), m_showSunDiskCheck);
    displayLayout->addRow(QString(), m_showSunGlowCheck);
    displayLayout->addRow(QString(), m_showHorizonCheck);
    displayLayout->addRow(m_exportButton);
    sensorLayout->addWidget(displayGroup);
    sensorLayout->addStretch();

    QWidget* resultPanel = new QWidget(central);
    QVBoxLayout* resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setAlignment(Qt::AlignTop);
    QLabel* sceneTitle = new QLabel(tr("CIE Sky 3D Geometry"));
    sceneTitle->setAlignment(Qt::AlignCenter);
    m_sceneWidget = new SkySceneWidget(resultPanel);
    m_sceneWidget->setMinimumSize(360, 200);
    m_sceneWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QLabel* resultTitle = new QLabel(tr("CIE Standard Sky Preview"));
    resultTitle->setAlignment(Qt::AlignCenter);
    m_skyWidget = new SkyPerspectiveWidget(resultPanel);
    m_skyWidget->setMinimumSize(360, 240);
    m_skyWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    resultLayout->addWidget(sceneTitle);
    resultLayout->addWidget(m_sceneWidget, 1);
    resultLayout->addWidget(resultTitle);
    resultLayout->addWidget(m_skyWidget, 2);
    rootLayout->addWidget(resultPanel, 1);

    connect(m_sunModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StandardSkyViewer::onSunModeChanged);
    connect(m_zoneMapButton, &QPushButton::clicked, this, &StandardSkyViewer::onChooseZone);
    connect(m_autoTimeZoneCheck, &QCheckBox::toggled, this, &StandardSkyViewer::onAutoTimeZoneToggled);
    connect(m_dateTimeEdit, &QDateTimeEdit::dateTimeChanged, this, &StandardSkyViewer::onDateTimeChanged);
    connect(m_sensorTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StandardSkyViewer::onSensorTypeChanged);
    connect(m_wavelengthStartSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StandardSkyViewer::onWavelengthChanged);
    connect(m_wavelengthEndSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StandardSkyViewer::onWavelengthChanged);
    connect(m_wavelengthSamplingSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StandardSkyViewer::onWavelengthChanged);
    connect(m_displayWavelengthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StandardSkyViewer::onDisplayWavelengthChanged);
    connect(m_aimSunButton, &QPushButton::clicked, this, &StandardSkyViewer::onAimAtSun);
    connect(m_resetViewButton, &QPushButton::clicked, this, &StandardSkyViewer::onResetView);
    connect(m_exportButton, &QPushButton::clicked, this, &StandardSkyViewer::onExportPng);
    connect(m_skyWidget, &SkyPerspectiveWidget::cameraChanged, this, &StandardSkyViewer::onCameraChanged);

    connectRenderCombo(m_cieTypeCombo);
    connectRenderSpin(m_luminanceSpin);
    connectRenderSpin(m_eastXSpin);
    connectRenderSpin(m_eastYSpin);
    connectRenderSpin(m_eastZSpin);
    connectRenderSpin(m_northXSpin);
    connectRenderSpin(m_northYSpin);
    connectRenderSpin(m_northZSpin);
    connectRenderSpin(m_upXSpin);
    connectRenderSpin(m_upYSpin);
    connectRenderSpin(m_upZSpin);
    connectRenderSpin(m_timeZoneSpin);
    connectRenderSpin(m_longitudeSpin);
    connectRenderSpin(m_latitudeSpin);
    connectRenderSpin(m_manualSunAzimuthSpin);
    connectRenderSpin(m_manualSunAltitudeSpin);
    connectRenderSpin(m_directNormalIlluminanceSpin);
    connectRenderCombo(m_layerCombo);
    connectRenderIntSpin(m_outputWidthSpin);
    connectRenderIntSpin(m_outputHeightSpin);
    connectRenderSpin(m_spectralTemperatureSpin);
    connectRenderCheck(m_localCameraCheck);
    connectRenderSpin(m_cameraXcSpin);
    connectRenderSpin(m_cameraYcSpin);
    connectRenderSpin(m_cameraZcSpin);
    connectRenderSpin(m_cameraAzimuthSpin);
    connectRenderSpin(m_cameraAltitudeSpin);
    connectRenderSpin(m_cameraRollSpin);
    connectRenderSpin(m_cameraHfovSpin);
    connectRenderSpin(m_cameraVfovSpin);
    connectRenderCombo(m_colorSchemeCombo);
    connectRenderCombo(m_toneMapCombo);
    connectRenderSpin(m_referenceLuminanceSpin);
    connectRenderSpin(m_exposureSpin);
    connectRenderSpin(m_gammaSpin);
    connectRenderCheck(m_showSunDiskCheck);
    connectRenderCheck(m_showSunGlowCheck);
    connectRenderCheck(m_showHorizonCheck);

    rebuildWavelengthList();
    updateExportButtonText();
}

void StandardSkyViewer::scheduleRender()
{
    if (m_renderTimer)
    {
        m_renderTimer->start();
    }
}

void StandardSkyViewer::updateRender()
{
    if (!m_skyWidget)
    {
        return;
    }
    const SkyPerspectiveParameters parameters = currentParameters();
    m_skyWidget->setParameters(parameters);
    if (m_sceneWidget)
    {
        m_sceneWidget->setSceneState(m_skyWidget->sceneState());
    }
    updateExportButtonText();
}

void StandardSkyViewer::onSunModeChanged()
{
    const bool automatic = m_sunModeCombo->currentData().toInt() == 0;
    m_zoneEdit->setEnabled(automatic);
    m_zoneMapButton->setEnabled(automatic);
    m_autoTimeZoneCheck->setEnabled(automatic);
    m_timeZoneSpin->setEnabled(automatic && !m_autoTimeZoneCheck->isChecked());
    m_timeZoneIdLabel->setEnabled(automatic);
    m_longitudeSpin->setEnabled(automatic);
    m_latitudeSpin->setEnabled(automatic);
    m_dateTimeEdit->setEnabled(automatic);
    m_manualSunAzimuthSpin->setEnabled(!automatic);
    m_manualSunAltitudeSpin->setEnabled(!automatic);
    if (automatic)
    {
        updateTimeZoneFromZone();
    }
    scheduleRender();
}

void StandardSkyViewer::onSensorTypeChanged()
{
    updateSensorUiState();
    updateExportButtonText();
    scheduleRender();
}

void StandardSkyViewer::onWavelengthChanged()
{
    double start = m_wavelengthStartSpin->value();
    double end = m_wavelengthEndSpin->value();
    if (end <= start)
    {
        end = std::min(m_wavelengthEndSpin->maximum(), start + 1.0);
        QSignalBlocker blocker(m_wavelengthEndSpin);
        m_wavelengthEndSpin->setValue(end);
    }
    rebuildWavelengthList();
    updateSensorUiState();
    updateExportButtonText();
    scheduleRender();
}

void StandardSkyViewer::onResetView()
{
    m_cameraXcSpin->setValue(0.0);
    m_cameraYcSpin->setValue(0.0);
    m_cameraZcSpin->setValue(0.0);
    m_cameraAzimuthSpin->setValue(0.0);
    m_cameraAltitudeSpin->setValue(20.0);
    m_cameraRollSpin->setValue(0.0);
    m_cameraHfovSpin->setValue(90.0);
    m_cameraVfovSpin->setValue(60.0);
    scheduleRender();
}

void StandardSkyViewer::onAimAtSun()
{
    const QVector3D sun = currentSunLocalDirection();
    if (sun.lengthSquared() < 1.0e-12f)
    {
        return;
    }
    // ENU Az/Alt 能直接表示太阳方向，因此 Aim at Sun 自动切换到非 Local Camera，避免把导航角和传统本地 Euler 角混用。
    m_localCameraCheck->setChecked(false);
    m_cameraAzimuthSpin->setValue(vectorAzimuthDeg(sun));
    m_cameraAltitudeSpin->setValue(std::max(-89.0, std::min(89.0, vectorAltitudeDeg(sun))));
    m_cameraRollSpin->setValue(0.0);
    scheduleRender();
}

void StandardSkyViewer::onExportPng()
{
    const QSize outputSize(std::max(1, m_outputWidthSpin->value()), std::max(1, m_outputHeightSpin->value()));
    const qint64 pixelCount = static_cast<qint64>(outputSize.width()) * outputSize.height();
    if (pixelCount > 16000000LL)
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Large image"), tr("The requested output is %1 × %2 (%3 million pixels). Continue?").arg(outputSize.width()).arg(outputSize.height()).arg(pixelCount / 1000000.0, 0, 'f', 1), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
        {
            return;
        }
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Export CIE Standard Sky"), QString::fromUtf8("cie_standard_sky.png"), tr("PNG image (*.png)"));
    if (path.isEmpty())
    {
        return;
    }
    if (!path.endsWith(".png", Qt::CaseInsensitive))
    {
        path += ".png";
    }
    updateRender();
    if (!m_skyWidget->savePng(path, outputSize))
    {
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write the PNG file."));
    }
}

void StandardSkyViewer::onChooseZone()
{
    LocationPickerDialog dialog(m_longitudeSpin->value(), m_latitudeSpin->value(), this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const QSignalBlocker zoneBlocker(m_zoneEdit);
    const QSignalBlocker longitudeBlocker(m_longitudeSpin);
    const QSignalBlocker latitudeBlocker(m_latitudeSpin);
    m_zoneEdit->setText(dialog.zoneName());
    m_timeZoneId = dialog.timeZoneId();
    m_timeZoneIdLabel->setText(m_timeZoneId);
    m_longitudeSpin->setValue(dialog.longitude());
    m_latitudeSpin->setValue(dialog.latitude());
    updateTimeZoneFromZone();
    scheduleRender();
}

void StandardSkyViewer::onAutoTimeZoneToggled(bool checked)
{
    m_timeZoneSpin->setEnabled(m_sunModeCombo->currentData().toInt() == 0 && !checked);
    updateTimeZoneFromZone();
    scheduleRender();
}

void StandardSkyViewer::onDateTimeChanged(const QDateTime& dateTime)
{
    Q_UNUSED(dateTime);
    updateTimeZoneFromZone();
    scheduleRender();
}

void StandardSkyViewer::onDisplayWavelengthChanged(int index)
{
    Q_UNUSED(index);
    updateSensorUiState();
    updateExportButtonText();
    scheduleRender();
}

void StandardSkyViewer::onCameraChanged(double azimuth, double altitude, double roll, double horizontalFov, double verticalFov)
{
    QSignalBlocker blockAzimuth(m_cameraAzimuthSpin);
    QSignalBlocker blockAltitude(m_cameraAltitudeSpin);
    QSignalBlocker blockRoll(m_cameraRollSpin);
    QSignalBlocker blockHorizontalFov(m_cameraHfovSpin);
    QSignalBlocker blockVerticalFov(m_cameraVfovSpin);
    m_cameraAzimuthSpin->setValue(azimuth);
    m_cameraAltitudeSpin->setValue(altitude);
    m_cameraRollSpin->setValue(roll);
    m_cameraHfovSpin->setValue(horizontalFov);
    m_cameraVfovSpin->setValue(verticalFov);
    if (m_sceneWidget && m_skyWidget)
    {
        m_sceneWidget->setSceneState(m_skyWidget->sceneState());
    }
}

SkyPerspectiveParameters StandardSkyViewer::currentParameters() const
{
    SkyPerspectiveParameters p;
    p.cieSkyType = m_cieTypeCombo->currentData().toInt();
    p.customCoefficients = false;
    p.scaleMode = SkyAbsoluteScaleMode::ZenithLuminance;
    p.targetValue = m_luminanceSpin->value();
    p.directNormalValue = m_directNormalIlluminanceSpin->value();

    QVector3D east;
    QVector3D north;
    QVector3D up;
    skyBasis(east, north, up);
    p.skyEastDirection = east;
    p.skyNorthDirection = north;
    p.skyZenithDirection = up;
    // Transform B（World -> CIE Sky）：E/N/U 只定义 CIE 天空在世界中的朝向；worldToSky() 会把球面交点方向投影回 CIE 局部 ENU。
    p.cameraRelativeToSkyBasis = false;

    // StandardSkyViewer 不再重复实现 CIE a,b,c,d,e 公式。environment_light.h 的 CIESkyModel 只负责相对天空形状，absoluteScale() 再把天顶方向精确缩放到用户输入的 Lz。
    const QVector3D sunLocal = currentSunLocalDirection();
    const Direction sunDirection = environmentDirectionFromENU(sunLocal);
    p.environmentLight = std::make_shared<CIESkyModel>(static_cast<CieSkyType>(p.cieSkyType + 1), sunDirection.theta, sunDirection.phi, 1.0, sRGB(0.72, 0.84, 1.0), 1.0);
    p.sunDirection = currentSunWorldDirection();

    // Transform A（Viewer Camera -> World ENU）：相机姿态和 Xc/Yc/Zc 先独立生成世界 ENU 射线/原点，不再跟着 CIE E/N/U 一起旋转，因此修改天空 ENU 会真正改变观察结果。
    p.useSensorFrameProjection = false;
    p.localCamera = m_localCameraCheck->isChecked();
    p.cameraPositionLocal = QVector3D(static_cast<float>(m_cameraXcSpin->value()), static_cast<float>(m_cameraYcSpin->value()), static_cast<float>(m_cameraZcSpin->value()));
    // Standard CIE 使用半径 1.0 的 Viewer 天空球。Xc/Yc/Zc 只改变相机射线与该球的交点方向，CIESkyModel 仍然只按最终方向计算亮度。
    p.useFiniteSkySphere = true;
    p.skySphereRadius = 1.0;
    p.cameraAzimuthDeg = m_cameraAzimuthSpin->value();
    p.cameraPitchDeg = m_cameraAltitudeSpin->value();
    p.cameraRollDeg = m_cameraRollSpin->value();
    p.horizontalFovDeg = m_cameraHfovSpin->value();
    p.verticalFovDeg = m_cameraVfovSpin->value();

    p.measurementType = static_cast<SkyMeasurementType>(m_sensorTypeCombo->currentData().toInt());
    p.measurementLayer = static_cast<SkyMeasurementLayer>(m_layerCombo->currentData().toInt());
    p.spectralStartNm = m_wavelengthStartSpin->value();
    p.spectralEndNm = m_wavelengthEndSpin->value();
    p.spectralSampling = m_wavelengthSamplingSpin->value();
    p.spectralTemperatureK = m_spectralTemperatureSpin->value();
    const double selectedWavelength = m_displayWavelengthCombo->count() > 0 ? m_displayWavelengthCombo->currentData().toDouble() : -1.0;
    p.spectralDisplayAllWavelengths = selectedWavelength < 0.0;
    p.displayWavelengthNm = p.spectralDisplayAllWavelengths ? 0.5 * (p.spectralStartNm + p.spectralEndNm) : selectedWavelength;

    p.colorMode = static_cast<SkyColorMode>(m_colorSchemeCombo->currentData().toInt());
    p.toneMapMode = static_cast<SkyToneMapMode>(m_toneMapCombo->currentData().toInt());
    p.displayReferenceValue = m_referenceLuminanceSpin->value();
    p.exposure = m_exposureSpin->value();
    p.gamma = m_gammaSpin->value();
    p.showSunDisk = m_showSunDiskCheck->isChecked();
    p.showSunGlow = m_showSunGlowCheck->isChecked();
    p.showHorizon = m_showHorizonCheck->isChecked();
    p.baseGroundColor = QColor(0, 0, 0);
    p.animateWeather = false;
    p.showWeatherParticles = false;
    p.showWeatherGround = false;
    return p;
}

void StandardSkyViewer::skyBasis(QVector3D& east, QVector3D& north, QVector3D& up) const
{
    east = QVector3D(static_cast<float>(m_eastXSpin->value()), static_cast<float>(m_eastYSpin->value()), static_cast<float>(m_eastZSpin->value()));
    north = QVector3D(static_cast<float>(m_northXSpin->value()), static_cast<float>(m_northYSpin->value()), static_cast<float>(m_northZSpin->value()));
    const QVector3D requestedUp(static_cast<float>(m_upXSpin->value()), static_cast<float>(m_upYSpin->value()), static_cast<float>(m_upZSpin->value()));

    // 用 E、N 做 Gram-Schmidt，再用 E×N 得 U；若用户输入的 U 与结果反向，则翻转 N/U，保证三组输入的朝向尽量一致。
    if (east.lengthSquared() < 1.0e-12f)
    {
        east = QVector3D(1.0f, 0.0f, 0.0f);
    }
    east.normalize();
    north -= QVector3D::dotProduct(north, east) * east;
    if (north.lengthSquared() < 1.0e-12f)
    {
        QVector3D fallbackUp = requestedUp.lengthSquared() > 1.0e-12f ? requestedUp.normalized() : QVector3D(0.0f, 0.0f, 1.0f);
        north = QVector3D::crossProduct(fallbackUp, east);
    }
    if (north.lengthSquared() < 1.0e-12f)
    {
        north = QVector3D(0.0f, 1.0f, 0.0f);
    }
    north.normalize();
    up = QVector3D::crossProduct(east, north).normalized();
    if (requestedUp.lengthSquared() > 1.0e-12f && QVector3D::dotProduct(up, requestedUp) < 0.0f)
    {
        north = -north;
        up = -up;
    }
}

QVector3D StandardSkyViewer::skyToWorld(const QVector3D& localDirection) const
{
    QVector3D east;
    QVector3D north;
    QVector3D up;
    skyBasis(east, north, up);
    return (localDirection.x() * east + localDirection.y() * north + localDirection.z() * up).normalized();
}

QVector3D StandardSkyViewer::currentSunLocalDirection() const
{
    if (m_sunModeCombo->currentData().toInt() == 0)
    {
        const QDateTime dateTime = m_dateTimeEdit->dateTime();
        const QTime time = dateTime.time();
        const double decimalHour = time.hour() + time.minute() / 60.0 + time.second() / 3600.0;
        const SSLib::Vec3f sun = SSLib::SunDirection(static_cast<float>(decimalHour), static_cast<float>(m_timeZoneSpin->value()), dateTime.date().dayOfYear(), static_cast<float>(m_latitudeSpin->value()), static_cast<float>(m_longitudeSpin->value()));
        QVector3D direction(sun[0], sun[1], sun[2]);
        if (direction.lengthSquared() < 1.0e-12f)
        {
            return QVector3D(0.0f, 0.0f, 1.0f);
        }
        return direction.normalized();
    }

    const double azimuth = m_manualSunAzimuthSpin->value() * kDegToRad;
    const double altitude = m_manualSunAltitudeSpin->value() * kDegToRad;
    return QVector3D(static_cast<float>(std::cos(altitude) * std::sin(azimuth)), static_cast<float>(std::cos(altitude) * std::cos(azimuth)), static_cast<float>(std::sin(altitude))).normalized();
}

QVector3D StandardSkyViewer::currentSunWorldDirection() const
{
    return skyToWorld(currentSunLocalDirection());
}

QString StandardSkyViewer::skyTypeDescription(int zeroBasedType)
{
    static const char* descriptions[15] = {
        "Overcast, steep gradation",
        "Overcast, moderate gradation",
        "Overcast, slight solar brightening",
        "Overcast, moderate solar brightening",
        "Uniform overcast",
        "Partly cloudy, slight brightening",
        "Partly cloudy, brighter circumsolar",
        "Partly cloudy, strong circumsolar",
        "Partly cloudy blue sky",
        "Partly cloudy bright sky",
        "Clear blue sky",
        "Clear standard sky",
        "Clear turbid sky",
        "Clear turbid bright sky",
        "Clear white sky"
    };
    if (zeroBasedType < 0 || zeroBasedType >= 15)
    {
        return tr("Unknown");
    }
    return tr(descriptions[zeroBasedType]);
}

double StandardSkyViewer::vectorAzimuthDeg(const QVector3D& direction)
{
    QVector3D d = direction;
    if (d.lengthSquared() < 1.0e-12f)
    {
        return 0.0;
    }
    d.normalize();
    double azimuth = std::atan2(static_cast<double>(d.x()), static_cast<double>(d.y())) * kRadToDeg;
    if (azimuth < 0.0)
    {
        azimuth += 360.0;
    }
    return azimuth;
}

double StandardSkyViewer::vectorAltitudeDeg(const QVector3D& direction)
{
    QVector3D d = direction;
    if (d.lengthSquared() < 1.0e-12f)
    {
        return 0.0;
    }
    d.normalize();
    return std::asin(std::max(-1.0, std::min(1.0, static_cast<double>(d.z())))) * kRadToDeg;
}

void StandardSkyViewer::updateTimeZoneFromZone()
{
    if (!m_autoTimeZoneCheck->isChecked() || m_timeZoneId.isEmpty())
    {
        return;
    }
    const QTimeZone zone(m_timeZoneId.toUtf8());
    if (!zone.isValid())
    {
        return;
    }
    const QDateTime localDateTime(m_dateTimeEdit->date(), m_dateTimeEdit->time(), zone);
    if (!localDateTime.isValid())
    {
        return;
    }
    QSignalBlocker blocker(m_timeZoneSpin);
    m_timeZoneSpin->setValue(localDateTime.offsetFromUtc() / 3600.0);
}

void StandardSkyViewer::updateSensorUiState()
{
    const SkyMeasurementType type = static_cast<SkyMeasurementType>(m_sensorTypeCombo->currentData().toInt());
    const bool spectralFamily = type != SkyMeasurementType::Photometric;
    const bool spectralPlanes = type == SkyMeasurementType::Spectral;
    m_wavelengthStartSpin->setEnabled(spectralFamily);
    m_wavelengthEndSpin->setEnabled(spectralFamily);
    m_spectralTemperatureSpin->setEnabled(spectralFamily);
    m_wavelengthSamplingSpin->setEnabled(spectralPlanes);
    m_displayWavelengthCombo->setEnabled(spectralPlanes);

    QString suffix;
    if (type == SkyMeasurementType::Photometric || type == SkyMeasurementType::Colorimetric)
    {
        suffix = QString::fromUtf8(" cd/m²");
    }
    else if (type == SkyMeasurementType::Radiometric)
    {
        suffix = QString::fromUtf8(" W/(m²·sr)");
    }
    else
    {
        const bool allWavelengths = m_displayWavelengthCombo->count() > 0 && m_displayWavelengthCombo->currentData().toDouble() < 0.0;
        suffix = allWavelengths ? QString::fromUtf8(" cd/m²") : QString::fromUtf8(" W/(m²·sr·nm)");
    }
    m_referenceLuminanceSpin->setSuffix(suffix);
}

void StandardSkyViewer::updateExportButtonText()
{
    if (!m_exportButton)
    {
        return;
    }
    QString extra;
    if (static_cast<SkyMeasurementType>(m_sensorTypeCombo->currentData().toInt()) == SkyMeasurementType::Spectral && m_displayWavelengthCombo->count() > 0)
    {
        const double wavelength = m_displayWavelengthCombo->currentData().toDouble();
        extra = wavelength < 0.0 ? tr(" [All wavelengths]") : tr(" @ %1 nm").arg(wavelength, 0, 'f', 1);
    }
    m_exportButton->setText(tr("Export PNG (%1 × %2)%3").arg(m_outputWidthSpin->value()).arg(m_outputHeightSpin->value()).arg(extra));
}

void StandardSkyViewer::rebuildWavelengthList()
{
    const double start = m_wavelengthStartSpin->value();
    const double end = m_wavelengthEndSpin->value();
    const int count = std::max(3, m_wavelengthSamplingSpin->value());
    bool keepAll = true;
    double previous = 0.5 * (start + end);
    if (m_displayWavelengthCombo->count() > 0)
    {
        const double current = m_displayWavelengthCombo->currentData().toDouble();
        keepAll = current < 0.0;
        if (!keepAll)
        {
            previous = current;
        }
    }
    QSignalBlocker blocker(m_displayWavelengthCombo);
    m_displayWavelengthCombo->clear();
    m_displayWavelengthCombo->addItem(tr("All wavelengths (True Color)"), -1.0);
    int nearestIndex = 1;
    double nearestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < count; ++i)
    {
        const double t = count > 1 ? static_cast<double>(i) / static_cast<double>(count - 1) : 0.0;
        const double wavelength = start + t * (end - start);
        m_displayWavelengthCombo->addItem(tr("%1 nm").arg(wavelength, 0, 'f', 1), wavelength);
        const double distance = std::abs(wavelength - previous);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestIndex = i + 1;
        }
    }
    m_displayWavelengthCombo->setCurrentIndex(keepAll ? 0 : nearestIndex);
}
