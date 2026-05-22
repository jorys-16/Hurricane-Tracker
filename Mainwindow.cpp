#include "mainwindow.h"
#include "hurricaneparser.h"

#include <QMenuBar>
#include <QToolBar>
#include <QFileDialog>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QHeaderView>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QGraphicsRectItem>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <QStyle>
#include <QMessageBox>
#include <cmath>
#include <algorithm>   // max
#include <stdexcept>   // runtime_error  (exception handling)

using namespace std;

// ══════════════════════════════════════════════════════════════════════════════
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Hurricane Tracker Qt 6");
    resize(1500, 880);

    m_timer = new QTimer(this);
    m_timer->setInterval(TICK_MS);          // ~60 fps; speed is segment-time
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onTick);

    // Load the animated hurricane sprite once. Try the compiled resource first,
    // then fall back to a source-tree path if the qrc hasn't been baked in.
    m_movie = new QMovie(":/images/hurricane.gif", QByteArray(), this);
    if (!m_movie->isValid()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString& candidate : {
                 appDir + "/images/hurricane.gif",
                 appDir + "/../images/hurricane.gif",
                 appDir + "/../../images/hurricane.gif",
                 appDir + "/../../../images/hurricane.gif",
                 QString("images/hurricane.gif") }) {
            m_movie->setFileName(candidate);
            if (m_movie->isValid()) {
                qInfo("Loaded hurricane GIF from: %s", qPrintable(candidate));
                break;
            }
        }
    }
    if (!m_movie->isValid()) {
        qWarning("hurricane.gif not found — sprite will be a colored halo only.");
    }
    m_movie->setCacheMode(QMovie::CacheAll);
    connect(m_movie, &QMovie::frameChanged, this, [this]() {
        if (!m_spritePixmap) return;
        QPixmap frame = m_movie->currentPixmap();
        if (frame.isNull()) return;
        const QPixmap scaled = frame.scaled(56, 56,
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        m_spritePixmap->setPixmap(scaled);
        m_spritePixmap->setOffset(-scaled.width() / 2.0, -scaled.height() / 2.0);
    });
    m_movie->start();

    setupUI();
    setupMenuBar();
    setupToolBar();
    drawGridAndBackground();
    drawLegend();
}

// ── UI setup ─────────────────────────────────────────────────────────────────
void MainWindow::setupUI() {
    // Scene + View
    m_scene = new QGraphicsScene(0, 0, MAP_W, MAP_H, this);
    m_scene->setBackgroundBrush(QColor("#07111f"));

    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing, true);
    m_view->setRenderHint(QPainter::TextAntialiasing, true);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setMinimumWidth(750);
    m_view->setStyleSheet("background-color:#07111f; border:none;");
    m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);

    // Data table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels(
        {"Name","Date","Time","Lat","Lon","Wind (mph)","Category"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setMinimumWidth(370);
    m_table->setStyleSheet(
        "QTableWidget {"
        "  background:#0d1b2a; color:#cdd6f4;"
        "  gridline-color:#1e3a5f; alternate-background-color:#0a1628; }"
        "QHeaderView::section {"
        "  background:#1e3a5f; color:#89b4fa;"
        "  padding:5px; border:none; font-weight:bold; }"
        "QTableWidget::item:selected { background:#264070; }"
        );

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_view);
    splitter->addWidget(m_table);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setStyleSheet("QSplitter::handle { background:#1e3a5f; width:2px; }");

    setCentralWidget(splitter);

    // Status bar
    m_statusLbl = new QLabel("No data loaded — File › Open CSV to begin.");
    statusBar()->addWidget(m_statusLbl);
    statusBar()->setStyleSheet("background:#0d1b2a; color:#6c7086; padding:2px 8px;");

    // App-wide dark theme
    setStyleSheet(
        "QMainWindow             { background:#07111f; }"
        "QMenuBar                { background:#0d1b2a; color:#cdd6f4; padding:2px; }"
        "QMenuBar::item:selected { background:#1e3a5f; }"
        "QMenu                   { background:#0d1b2a; color:#cdd6f4; }"
        "QMenu::item:selected    { background:#1e3a5f; }"
        "QToolBar                { background:#0d1b2a; border:none; padding:4px; spacing:6px; }"
        "QPushButton             { background:#1e3a5f; color:#cdd6f4;"
        "                          padding:5px 14px; border:1px solid #264070;"
        "                          border-radius:3px; }"
        "QPushButton:hover       { background:#264070; }"
        "QPushButton:disabled    { background:#0d1b2a; color:#45475a; }"
        "QComboBox               { background:#1e3a5f; color:#cdd6f4;"
        "                          padding:4px 8px; border:1px solid #264070;"
        "                          border-radius:3px; min-width:60px; }"
        "QSlider::groove:horizontal { height:6px; background:#1e3a5f; border-radius:3px; }"
        "QSlider::handle:horizontal { background:#89b4fa; width:14px; height:14px;"
        "                             margin:-5px 0; border-radius:7px; }"
        "QSlider::sub-page:horizontal { background:#89b4fa; border-radius:3px; }"
        );
}

void MainWindow::setupMenuBar() {
    QMenu* fileMenu = menuBar()->addMenu("File");
    QAction* openAct = fileMenu->addAction("Open CSV…");
    openAct->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction("Quit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(openAct, &QAction::triggered, this, &MainWindow::openCSV);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    QMenu* viewMenu = menuBar()->addMenu("View");
    QAction* fitAct = viewMenu->addAction("Fit to Window");
    connect(fitAct, &QAction::triggered, this, [this](){
        m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    });
}

void MainWindow::setupToolBar() {
    QToolBar* tb = addToolBar("Animation");
    tb->setMovable(false);

    m_playBtn  = new QPushButton("▶  Play", this);
    m_resetBtn = new QPushButton("⟲  Reset", this);
    m_playBtn->setEnabled(false);
    m_resetBtn->setEnabled(false);
    connect(m_playBtn,  &QPushButton::clicked, this, &MainWindow::onPlayPause);
    connect(m_resetBtn, &QPushButton::clicked, this, &MainWindow::onReset);

    QLabel* speedLbl = new QLabel(" Speed ");
    speedLbl->setStyleSheet("color:#89b4fa; font-weight:bold;");
    m_speedBox = new QComboBox(this);
    m_speedBox->addItem("0.5×", 400);
    m_speedBox->addItem("1×",   200);   // default
    m_speedBox->addItem("2×",   100);
    m_speedBox->addItem("5×",    40);
    m_speedBox->addItem("10×",   20);
    m_speedBox->setCurrentIndex(1);
    connect(m_speedBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSpeedChanged);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 0);
    m_slider->setEnabled(false);
    connect(m_slider, &QSlider::valueChanged, this, &MainWindow::onSliderMoved);

    m_dateLbl = new QLabel("—");
    m_dateLbl->setMinimumWidth(280);
    m_dateLbl->setStyleSheet("color:#cdd6f4; font-family:'Courier New'; padding:0 8px;");

    tb->addWidget(m_playBtn);
    tb->addWidget(m_resetBtn);
    tb->addWidget(speedLbl);
    tb->addWidget(m_speedBox);
    tb->addSeparator();
    tb->addWidget(m_slider);
    tb->addWidget(m_dateLbl);
}

// ── Map background + lat/lon grid ────────────────────────────────────────────
void MainWindow::drawGridAndBackground() {
    // NASA Atlantic basin photo as the basemap. Try the compiled resource
    // first; if it isn't present (qrc not picked up by CMake yet) fall back
    // to the source-tree path so the user sees something either way.
    QPixmap base(":/images/atlantic-basin.png");
    if (base.isNull()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString& candidate : {
                 appDir + "/images/atlantic-basin.png",
                 appDir + "/../images/atlantic-basin.png",
                 appDir + "/../../images/atlantic-basin.png",
                 appDir + "/../../../images/atlantic-basin.png",
                 QString("images/atlantic-basin.png") }) {
            if (base.load(candidate)) {
                qInfo("Loaded basemap from: %s", qPrintable(candidate));
                break;
            }
        }
    }
    if (base.isNull()) {
        qWarning("atlantic-basin.png not found — basemap will be blank.");
    } else {
        QPixmap scaled = base.scaled(MAP_W, MAP_H,
                                     Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
        m_basemap = m_scene->addPixmap(scaled);
        m_basemap->setZValue(-100);
        // Slightly dim the basemap so the colored track and labels stay readable.
        m_basemap->setOpacity(0.85);
    }

    QPen gridPen(QColor(255, 255, 255, 35), 0.5);
    QFont labelFont("Courier New", 7);
    QColor labelColor(255, 255, 255, 130);

    for (double lat = 10; lat <= 50; lat += 10) {
        double y = latToY(lat);
        m_scene->addLine(0, y, MAP_W, y, gridPen);
        auto* lbl = m_scene->addText(QString("%1°N").arg((int)lat), labelFont);
        lbl->setDefaultTextColor(labelColor);
        lbl->setPos(3, y - 13);
    }
    for (double lon = -100; lon <= -20; lon += 10) {
        double x = lonToX(lon);
        m_scene->addLine(x, 0, x, MAP_H, gridPen);
        auto* lbl = m_scene->addText(QString("%1°W").arg((int)abs(lon)), labelFont);
        lbl->setDefaultTextColor(labelColor);
        lbl->setPos(x + 2, MAP_H - 15);
    }

    QFont titleFont("Courier New", 13, QFont::Bold);
    auto* title = m_scene->addText("ATLANTIC HURRICANE TRACKER", titleFont);
    title->setDefaultTextColor(QColor("#89b4fa"));
    title->setPos(10, 8);
}

// ── Saffir-Simpson legend ────────────────────────────────────────────────────
void MainWindow::drawLegend() {
    struct Entry { int cat; QString label; };
    const QList<Entry> entries = {
        { 5, "Cat 5  ≥ 157 mph" },
        { 4, "Cat 4  130–156 mph" },
        { 3, "Cat 3  111–129 mph" },
        { 2, "Cat 2   96–110 mph" },
        { 1, "Cat 1   74–95 mph" },
        { 0, "Tropical Storm" },
        {-1, "Tropical Dep." }
    };

    const double lx = MAP_W - 178.0;
    const double ly0 = 10.0;
    const double rowH = 19.0;
    const double boxH = entries.size() * rowH + 26.0;

    m_scene->addRect(lx - 6, ly0 - 4, 172, boxH,
                     QPen(QColor("#1e3a5f")), QBrush(QColor(7, 17, 31, 210)));

    QFont hdrFont("Courier New", 8, QFont::Bold);
    QFont entFont("Courier New", 7);

    auto* hdr = m_scene->addText("LEGEND", hdrFont);
    hdr->setDefaultTextColor(QColor("#89b4fa"));
    hdr->setPos(lx, ly0);

    double ly = ly0 + 22.0;
    for (const auto& e : entries) {
        QColor c = categoryColor(e.cat);
        m_scene->addEllipse(lx, ly + 1, 11, 11,
                            QPen(c.lighter(140), 1), QBrush(c));
        auto* txt = m_scene->addText(e.label, entFont);
        txt->setDefaultTextColor(QColor("#cdd6f4"));
        txt->setPos(lx + 16, ly);
        ly += rowH;
    }
}

// ── File open ────────────────────────────────────────────────────────────────
void MainWindow::openCSV() {
    const QString defaultDir =
        QCoreApplication::applicationDirPath() + "/../../data";
    QString path = QFileDialog::getOpenFileName(
        this, "Open Hurricane CSV", defaultDir, "CSV Files (*.csv);;All Files (*)");
    if (path.isEmpty()) return;

    // parse() throws on an unreadable file or empty/malformed input. Catch it
    // here so a bad selection surfaces as a dialog and leaves the current view
    // untouched, rather than tearing down the scene before we have valid data.
    QVector<HurricanePoint> pts;
    try {
        pts = HurricaneParser::parse(path);
    }
    catch (const runtime_error& e) {
        QMessageBox::warning(this, "Could Not Load File", QString(e.what()));
        m_statusLbl->setText("Error: " + QString(e.what()));
        return;
    }
    m_points = pts;

    // Reset scene + animation state
    m_timer->stop();
    setPlaying(false);
    clearTrackItems();
    m_scene->clear();
    drawGridAndBackground();
    drawLegend();
    populateTable(m_points);

    m_animIdx     = 0;
    m_segProgress = 0.0;
    m_slider->blockSignals(true);
    m_slider->setRange(0, m_points.size() - 1);
    m_slider->setValue(0);
    m_slider->blockSignals(false);

    m_slider->setEnabled(true);
    m_playBtn->setEnabled(true);
    m_resetBtn->setEnabled(true);

    // Show the starting vertex and park the sprite on it.
    drawSegment(0);
    snapSpriteToIndex(0);
    updateDateLabel(0);

    m_statusLbl->setText(
        QString("Loaded %1 points for %2  ·  Press Play to animate")
            .arg(m_points.size()).arg(m_points[0].name));
}

// ── Table population ─────────────────────────────────────────────────────────
void MainWindow::populateTable(const QVector<HurricanePoint>& points) {
    m_table->setRowCount(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        m_table->setItem(i, 0, new QTableWidgetItem(p.name));
        m_table->setItem(i, 1, new QTableWidgetItem(p.date));
        m_table->setItem(i, 2, new QTableWidgetItem(p.time));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(p.lat, 'f', 1)));
        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(p.lon, 'f', 1)));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(p.windMph)));
        auto* catItem = new QTableWidgetItem(categoryName(p.category));
        catItem->setForeground(categoryColor(p.category));
        m_table->setItem(i, 6, catItem);
    }
}

// ── Animation core ───────────────────────────────────────────────────────────
void MainWindow::onPlayPause() {
    if (m_points.isEmpty()) return;

    // If we're at the end, restart from the beginning.
    if (m_animIdx >= m_points.size() - 1) {
        clearTrackItems();
        m_animIdx     = 0;
        m_segProgress = 0.0;
        m_slider->blockSignals(true);
        m_slider->setValue(0);
        m_slider->blockSignals(false);
        drawSegment(0);
        snapSpriteToIndex(0);
        updateDateLabel(0);
    }
    setPlaying(!m_playing);
}

void MainWindow::onReset() {
    m_timer->stop();
    setPlaying(false);
    clearTrackItems();
    m_animIdx     = 0;
    m_segProgress = 0.0;
    m_slider->blockSignals(true);
    m_slider->setValue(0);
    m_slider->blockSignals(false);
    if (!m_points.isEmpty()) {
        drawSegment(0);
        snapSpriteToIndex(0);
        updateDateLabel(0);
    }
}

void MainWindow::onTick() {
    if (m_points.isEmpty() || m_animIdx >= m_points.size() - 1) {
        setPlaying(false);
        return;
    }

    m_segProgress += static_cast<double>(TICK_MS) / m_segMs;

    // If we've reached the next point, finalize that segment and advance.
    if (m_segProgress >= 1.0) {
        m_segProgress = 0.0;
        ++m_animIdx;
        drawSegment(m_animIdx);
        updateDateLabel(m_animIdx);
        m_slider->blockSignals(true);
        m_slider->setValue(m_animIdx);
        m_slider->blockSignals(false);

        if (m_animIdx >= m_points.size() - 1) {
            snapSpriteToIndex(m_animIdx);
            setPlaying(false);
            return;
        }
    }

    // Smooth interpolation between m_animIdx and m_animIdx+1.
    const HurricanePoint& a = m_points[m_animIdx];
    const HurricanePoint& b = m_points[m_animIdx + 1];
    const double xa = lonToX(a.lon), ya = latToY(a.lat);
    const double xb = lonToX(b.lon), yb = latToY(b.lat);
    const double t  = m_segProgress;
    const double x  = xa + (xb - xa) * t;
    const double y  = ya + (yb - ya) * t;

    // Heading angle, in scene-space (Y inverted). 0° points east.
    const double headingDeg = atan2(yb - ya, xb - xa) * 180.0 / M_PI;

    // Halo color tracks the upcoming point's category for a nice "rising power"
    // visual as the storm intensifies.
    updateSpriteAt(x, y, b.category, headingDeg);
}

void MainWindow::onSliderMoved(int value) {
    if (m_points.isEmpty()) return;
    // Scrubbing pauses playback for predictability.
    if (m_playing) setPlaying(false);
    rebuildTrackUpTo(value);
    m_animIdx = value;
    m_segProgress = 0.0;
    snapSpriteToIndex(value);
    updateDateLabel(value);
}

void MainWindow::onSpeedChanged(int idx) {
    // Speed combo stores segment-duration in ms (1× = 200 ms / point).
    m_segMs = m_speedBox->itemData(idx).toDouble();
}

void MainWindow::setPlaying(bool playing) {
    m_playing = playing;
    if (playing) {
        m_timer->start();
        m_playBtn->setText("⏸  Pause");
    } else {
        m_timer->stop();
        m_playBtn->setText("▶  Play");
    }
}

// ── Track drawing ────────────────────────────────────────────────────────────
void MainWindow::clearTrackItems() {
    for (QGraphicsItem* it : m_trackItems) {
        m_scene->removeItem(it);
        delete it;
    }
    m_trackItems.clear();
    if (m_spriteHalo)   { m_scene->removeItem(m_spriteHalo);   delete m_spriteHalo;   m_spriteHalo   = nullptr; }
    if (m_spritePixmap) { m_scene->removeItem(m_spritePixmap); delete m_spritePixmap; m_spritePixmap = nullptr; }
}

void MainWindow::drawSegment(int i) {
    if (i < 0 || i >= m_points.size()) return;
    const HurricanePoint& p = m_points[i];

    const double x = lonToX(p.lon);
    const double y = latToY(p.lat);
    const QColor col = categoryColor(p.category);

    // Edge from previous point: thickness scales with category (Stanford req)
    if (i > 0) {
        const HurricanePoint& q = m_points[i - 1];
        const double px = lonToX(q.lon);
        const double py = latToY(q.lat);
        const int    catForLine = max(p.category, q.category);
        const double width = 1.2 + max(0, catForLine) * 0.9; // Cat 5 ≈ 5.7 px
        QPen edgePen(col, width);
        edgePen.setCapStyle(Qt::RoundCap);
        m_trackItems.append(m_scene->addLine(px, py, x, y, edgePen));
    }

    // Vertex dot
    constexpr double R = 5.0;
    m_trackItems.append(
        m_scene->addEllipse(x - R, y - R, R * 2, R * 2,
                            QPen(col.lighter(160), 1.0), QBrush(col)));

    // Category number when at hurricane strength (Stanford req #2)
    if (p.category >= 1) {
        QFont catFont("Courier New", 8, QFont::Bold);
        auto* txt = m_scene->addText(QString::number(p.category), catFont);
        txt->setDefaultTextColor(Qt::white);
        txt->setPos(x + 6, y - 16);
        m_trackItems.append(txt);
    }
}

void MainWindow::rebuildTrackUpTo(int idx) {
    clearTrackItems();
    for (int i = 0; i <= idx && i < m_points.size(); ++i) {
        drawSegment(i);
    }
    snapSpriteToIndex(idx);
}

void MainWindow::updateSpriteAt(double x, double y, int category, double rotationDeg) {
    const QColor col = categoryColor(category);

    // Halo: tinted glow under the sprite, sized by category strength.
    const double haloR = 20.0 + max(0, category) * 4.0;  // Cat 5 ≈ 40 px
    QColor haloCol = col;
    haloCol.setAlpha(95);

    if (!m_spriteHalo) {
        m_spriteHalo = m_scene->addEllipse(
            x - haloR, y - haloR, haloR * 2, haloR * 2,
            QPen(Qt::NoPen), QBrush(haloCol));
    } else {
        m_spriteHalo->setBrush(haloCol);
        m_spriteHalo->setRect(x - haloR, y - haloR, haloR * 2, haloR * 2);
    }
    m_spriteHalo->setZValue(50);

    // Animated hurricane GIF (pushed into the pixmap by QMovie::frameChanged).
    if (!m_spritePixmap) {
        const QPixmap initial = m_movie->currentPixmap().scaled(
            56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_spritePixmap = m_scene->addPixmap(initial);
        m_spritePixmap->setOffset(-initial.width() / 2.0,
                                  -initial.height() / 2.0);
        // Setting offset to -w/2,-h/2 puts the item's local origin at the
        // pixmap centre, so rotation pivots around the eye of the storm.
        m_spritePixmap->setTransformOriginPoint(0, 0);
    }
    m_spritePixmap->setPos(x, y);
    m_spritePixmap->setRotation(rotationDeg);
    m_spritePixmap->setZValue(51);
}

void MainWindow::snapSpriteToIndex(int idx) {
    if (idx < 0 || idx >= m_points.size()) return;
    const HurricanePoint& p = m_points[idx];
    // Preserve current rotation if mid-track; reset to 0 at the very start.
    const double rot = (m_spritePixmap && idx > 0) ? m_spritePixmap->rotation() : 0.0;
    updateSpriteAt(lonToX(p.lon), latToY(p.lat), p.category, rot);
}

void MainWindow::updateDateLabel(int idx) {
    if (idx < 0 || idx >= m_points.size()) {
        m_dateLbl->setText("—");
        return;
    }
    const HurricanePoint& p = m_points[idx];
    m_dateLbl->setText(
        QString("%1  %2   ·   %3 (%4 mph)   ·   %5 / %6")
            .arg(p.date, -7)
            .arg(p.time)
            .arg(categoryName(p.category))
            .arg(p.windMph)
            .arg(idx + 1)
            .arg(m_points.size()));
}

// ── Coordinate conversion ────────────────────────────────────────────────────
double MainWindow::lonToX(double lon) const {
    return (lon - LON_MIN) / (LON_MAX - LON_MIN) * MAP_W;
}

double MainWindow::latToY(double lat) const {
    return (1.0 - (lat - LAT_MIN) / (LAT_MAX - LAT_MIN)) * MAP_H;
}
