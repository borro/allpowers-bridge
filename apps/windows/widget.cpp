#include "widget.h"
#include "i18n.h"
#include "integration.h"
#include "notices.h"
#include "runtime_log.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>

bool setWidgetStartup(QSettings &store, bool enabled, const QString &executable) {
  if (enabled)
    store.setValue("AllpowersWidget", '"' + QDir::toNativeSeparators(executable) + '"');
  else
    store.remove("AllpowersWidget");
  store.sync();
  return store.status() == QSettings::NoError;
}

WidgetCard::WidgetCard(bool desktop, std::function<quintptr()> hostFinder)
    : DesktopSurface(desktop, std::move(hostFinder)), graphAnimation_(this),
      graphFrameTimer_(this) {
  setWindowTitle(QStringLiteral("ALLPOWERS Widget"));
  setFixedSize(300, 210);
  setCursor(Qt::SizeAllCursor);
  graphAnimation_.setDuration(900);
  // Keep Qt's continuous animation clock, publishing at about 30 fps. The
  // final frame is published immediately, regardless of the repaint cadence.
  graphFrameTimer_.setInterval(33);
  graphFrameTimer_.setTimerType(Qt::PreciseTimer);
  const auto repaintGraph = [this] {
    update();
    if (attached())
      presentImage(renderedImage());
  };
  connect(&graphFrameTimer_, &QTimer::timeout, this, repaintGraph);
  connect(&graphAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
    const double progress = value.toDouble();
    graphNow_ = fromNow_ + (double(now_) - fromNow_) * progress;
    graphMaximum_ = fromMaximum_ + (targetMaximum_ - fromMaximum_) * progress;
  });
  connect(&graphAnimation_, &QAbstractAnimation::stateChanged, this,
          [this, repaintGraph](QAbstractAnimation::State state) {
            if (state == QAbstractAnimation::Running)
              graphFrameTimer_.start();
            else {
              graphFrameTimer_.stop();
              repaintGraph();
            }
          });
  connect(this, &DesktopSurface::surfaceChanged, this, [this] {
    rebuildGraph();
    presentImage(renderedImage());
  });
}
WidgetCard::~WidgetCard() {
  graphAnimation_.disconnect(this);
  graphFrameTimer_.stop();
  graphAnimation_.stop();
}
QString WidgetCard::reading(const ap_snapshot &s, quint32 mask, quint32 value, QString suffix) {
  return (s.known & mask) ? QString::number(value) + suffix : QStringLiteral("—");
}
QString WidgetCard::remainingTime(const ap_snapshot &s) {
  if (!(s.known & 8))
    return QStringLiteral("—");
  return formatDuration(s.runtime_s, true);
}
void WidgetCard::setDeviceName(QString name) {
  name = name.trimmed();
  if (name.isEmpty())
    name = QCoreApplication::translate("Allpowers", "No device");
  if (deviceName_ == name)
    return;
  deviceName_ = std::move(name);
  staticCard_ = {};
  setAccessibleName(deviceName_ + " " + reading(snapshot_, 1, snapshot_.soc, "%"));
  update();
  if (attached())
    presentImage(renderedImage());
}
void WidgetCard::setTelemetry(ap_snapshot value, QString phase) {
  const bool changed = !telemetrySet_ || (snapshot_.known & 15u) != (value.known & 15u) ||
                       bool(snapshot_.fresh) != bool(value.fresh) ||
                       ((value.known & 1u) && snapshot_.soc != value.soc) ||
                       ((value.known & 2u) && snapshot_.input_w != value.input_w) ||
                       ((value.known & 4u) && snapshot_.output_w != value.output_w) ||
                       ((value.known & 8u) && remainingTime(snapshot_) != remainingTime(value));
  const bool statusChanged = !telemetrySet_ || phase_ != phase ||
                             bool(snapshot_.fresh) != bool(value.fresh) ||
                             bool(snapshot_.known) != bool(value.known);
  snapshot_ = value;
  telemetrySet_ = true;
  phase_ = std::move(phase);
  if (statusChanged) {
    const QString status = !value.fresh && value.known
                               ? QCoreApplication::translate("Allpowers", "Data stale · ") + phase_
                               : phase_;
    setToolTip(status);
    setAccessibleDescription(status);
  }
  if (!changed)
    return;
  staticCard_ = {};
  setAccessibleName(deviceName_ + " " + reading(value, 1, value.soc, "%"));
  update();
  if (attached())
    presentImage(renderedImage());
}
void WidgetCard::setHistory(HistoryPoints points, qint64 now) {
  points_ = std::move(points);
  double maximum = 10;
  for (const auto &point : std::as_const(points_))
    if (point.wall >= now - 3600 && point.wall <= now && point.data.fresh) {
      if (point.data.known & 2)
        maximum = qMax(maximum, double(point.data.input_w));
      if (point.data.known & 4)
        maximum = qMax(maximum, double(point.data.output_w));
    }
  maximum = std::ceil(maximum / 10) * 10;
  const bool changed = now != now_ || maximum != targetMaximum_;
  const bool initial = !now_ || now < now_ || now - now_ > 3;
  now_ = now;
  if (changed) {
    graphAnimation_.stop();
    fromNow_ = graphNow_;
    fromMaximum_ = graphMaximum_;
    targetMaximum_ = maximum;
    if (initial) {
      graphNow_ = double(now);
      graphMaximum_ = maximum;
    } else {
      graphAnimation_.setStartValue(0.);
      graphAnimation_.setEndValue(1.);
      graphAnimation_.start();
    }
  }
  rebuildGraph();
  update();
  if (attached())
    presentImage(renderedImage());
}
double WidgetCard::graphPosition(double age) {
  // Compress the latest seconds while retaining the full hour and logarithmic history.
  static const double inverseLogHorizon = 1. / std::log1p(3600. / 4.);
  return 1. - std::log1p(qBound(0., age / 4., 3600. / 4.)) * inverseLogHorizon;
}
void WidgetCard::rebuildGraph() {
  // Simplify each connected run once per history update, not on every animation
  // frame. Preserve extrema within half a physical pixel before simplifying
  // runs, checking both ends of the animation at its largest vertical scale.
  // Retained vertices still use the exact logarithmic position on every frame.
  const double start = graphNow_;
  const double end = double(now_);
  const double height = 54. / qMin(graphMaximum_, targetMaximum_);
  const double tolerance = .15 / desktopScale();
  const double toleranceSquared = tolerance * tolerance;
  const double columns = 2. * desktopScale();
  const auto &points = std::as_const(points_);
  auto distanceSquared = [](QPointF point, QPointF a, QPointF b) {
    const auto line = b - a;
    const double length = QPointF::dotProduct(line, line);
    const double fraction =
        length > 0 ? qBound(0., QPointF::dotProduct(point - a, line) / length, 1.) : 0.;
    const auto delta = point - (a + fraction * line);
    return QPointF::dotProduct(delta, delta);
  };
  for (int series = 0; series < 2; ++series) {
    auto &graph = graph_[series];
    graph.clear();
    const quint32 mask = series == 0 ? 2u : 4u;
    QList<GraphPoint> run;
    auto appendRun = [&] {
      if (run.size() < 2)
        return;
      QList<QPointF> from, to;
      from.reserve(run.size());
      to.reserve(run.size());
      for (const auto &point : run) {
        const double y = double(point.watts) * height;
        from.append({272. * graphPosition(start - double(point.wall)), y});
        to.append({272. * graphPosition(end - double(point.wall)), y});
      }
      QList<qsizetype> candidates;
      for (qsizetype first = 0; first < run.size();) {
        const auto fromColumn = qFloor(from[first].x() * columns);
        const auto toColumn = qFloor(to[first].x() * columns);
        qsizetype next = first + 1, minimum = first, maximum = first;
        while (next < run.size() && qFloor(from[next].x() * columns) == fromColumn &&
               qFloor(to[next].x() * columns) == toColumn) {
          if (run[next].watts < run[minimum].watts)
            minimum = next;
          if (run[next].watts > run[maximum].watts)
            maximum = next;
          ++next;
        }
        std::array<qsizetype, 4> extrema{first, minimum, maximum, next - 1};
        std::sort(extrema.begin(), extrema.end());
        for (auto index : extrema)
          if (candidates.isEmpty() || candidates.last() != index)
            candidates.append(index);
        first = next;
      }
      QList<bool> keep(candidates.size(), false);
      keep.first() = keep.last() = true;
      QList<QPair<qsizetype, qsizetype>> spans{{0, candidates.size() - 1}};
      while (!spans.isEmpty()) {
        const auto [first, last] = spans.takeLast();
        double largest = toleranceSquared;
        qsizetype selected = -1;
        for (qsizetype i = first + 1; i < last; ++i) {
          const auto a = candidates[first], b = candidates[last], point = candidates[i];
          const double error = qMax(distanceSquared(from[point], from[a], from[b]),
                                    distanceSquared(to[point], to[a], to[b]));
          if (error > largest) {
            largest = error;
            selected = i;
          }
        }
        if (selected >= 0) {
          keep[selected] = true;
          spans.append({first, selected});
          spans.append({selected, last});
        }
      }
      for (qsizetype i = 0; i < candidates.size(); ++i)
        if (keep[i]) {
          const auto &point = run[candidates[i]];
          graph.append({point.wall, point.watts, i == 0});
        }
      run.clear();
    };
    auto vertex = [mask](const HistoryPoint &point) {
      return GraphPoint{point.wall, mask == 2 ? point.data.input_w : point.data.output_w, false};
    };
    for (qsizetype i = 1; i < points.size(); ++i) {
      const auto &a = points[i - 1], &b = points[i];
      if (a.wall >= now_ - 3600 && b.wall <= now_ && (a.data.known & mask) &&
          (b.data.known & mask) && joinPoints(a, b)) {
        if (run.isEmpty())
          run.append(vertex(a));
        run.append(vertex(b));
      } else {
        appendRun();
      }
    }
    appendRun();
  }
}
int WidgetCard::graphSegments(const HistoryPoints &points, quint32 mask, qint64 now) {
  int result = 0;
  for (qsizetype i = 1; i < points.size(); ++i)
    if (points[i - 1].wall >= now - 3600 && points[i].wall <= now &&
        (points[i - 1].data.known & mask) && (points[i].data.known & mask) &&
        joinPoints(points[i - 1], points[i]))
      ++result;
  return result;
}
void WidgetCard::paintEvent(QPaintEvent *) {
  QPainter p(this);
  drawCard(p);
}
QImage WidgetCard::renderedImage() const {
  const qreal scale = desktopScale();
  QImage frame(QSize(qRound(width() * scale), qRound(height() * scale)),
               QImage::Format_ARGB32_Premultiplied);
  frame.setDevicePixelRatio(scale);
  frame.fill(Qt::transparent);
  QPainter painter(&frame);
  drawCard(painter);
  return frame;
}
void WidgetCard::drawCard(QPainter &p) const {
  const qreal scale = p.device()->devicePixelRatioF();
  const QSize pixels(qRound(width() * scale), qRound(height() * scale));
  const QString maximum = QString::number(graphMaximum_, 'f', 0);
  if (staticCard_.size() != pixels || staticCard_.devicePixelRatio() != scale ||
      staticMaximum_ != maximum) {
    staticCard_ = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
    staticCard_.setDevicePixelRatio(scale);
    staticCard_.fill(Qt::transparent);
    QPainter background(&staticCard_);
    drawStaticCard(background);
    staticMaximum_ = maximum;
  }
  p.drawImage(QPoint(0, 0), staticCard_);
  drawGraph(p);
}
void WidgetCard::drawStaticCard(QPainter &p) const {
  p.fillRect(rect(), QColor("#101b2d"));
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setPen(QPen(QColor("#30415b"), 1));
  p.drawRect(rect().adjusted(0, 0, -1, -1));
  p.setRenderHint(QPainter::Antialiasing);
  auto text = [&](QRect r, QString value, int size, QColor color, bool bold = false) {
    p.setFont(QFont("Segoe UI", size, bold ? QFont::DemiBold : QFont::Normal));
    p.setPen(color);
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, value);
  };
  const auto &s = snapshot_;
  const QColor foreground = s.fresh ? QColor("#eef5ff") : QColor("#97a7be");
  p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
  text({12, 5, 244, 22}, p.fontMetrics().elidedText(deviceName_, Qt::ElideRight, 244), 10,
       QColor("#b7c9e2"), true);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#b7c9e2"));
  for (int i = 0; i < 3; ++i)
    p.drawEllipse(QPointF(279, 12 + i * 4), 1.2, 1.2);
  p.setBrush(Qt::NoBrush);
  text({12, 29, 136, 48}, reading(s, 1, s.soc, "%"), 30, foreground, true);
  text({154, 30, 134, 16}, QCoreApplication::translate("Allpowers", "remaining"), 9,
       QColor("#98adc9"));
  text({154, 47, 134, 26}, remainingTime(s), 12, foreground);
  text({12, 82, 138, 24},
       QCoreApplication::translate("Allpowers", "Input  ") +
           reading(s, 2, s.input_w, QCoreApplication::translate("Allpowers", " W")),
       10, QColor("#65baff"));
  text({154, 82, 140, 24},
       QCoreApplication::translate("Allpowers", "Output  ") +
           reading(s, 4, s.output_w, QCoreApplication::translate("Allpowers", " W")),
       10, QColor("#eff5ff"));
  const QRectF plot(14, 126, 272, 54);
  const double maximum = graphMaximum_;
  text({14, 108, 180, 16},
       QString::number(maximum, 'f', 0) + QCoreApplication::translate("Allpowers", " W"), 8,
       QColor("#8ea5c4"));
  p.setPen(QPen(QColor("#2b3b53"), 1));
  p.drawLine(plot.bottomLeft(), plot.bottomRight());
  p.drawLine(plot.topLeft(), plot.topRight());
  text({14, 184, 180, 16}, QCoreApplication::translate("Allpowers", "−1 hour · log time"), 8,
       QColor("#8ea5c4"));
  text({240, 184, 50, 16}, QCoreApplication::translate("Allpowers", "now"), 8, QColor("#8ea5c4"));
}
void WidgetCard::drawGraph(QPainter &p) const {
  const QRectF plot(14, 126, 272, 54);
  const double maximum = graphMaximum_;
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(Qt::NoBrush);
  p.setClipRect(plot.adjusted(-3, -1, 3, 3));
  for (int series = 0; series < 2; ++series) {
    p.setPen(QPen(QColor(series == 0 ? "#65baff" : "#eff5ff"), 1.6));
    QList<QLineF> lines;
    lines.reserve(graph_[series].size());
    QPointF previous;
    for (const auto &point : graph_[series]) {
      const QPointF pixel(plot.left() +
                              graphPosition(graphNow_ - double(point.wall)) * plot.width(),
                          plot.bottom() - double(point.watts) * plot.height() / maximum);
      if (!point.starts)
        lines.append({previous, pixel});
      previous = pixel;
    }
    p.drawLines(lines);
  }
  p.restore();
}
void WidgetCard::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton)
    return;
  if (event->position().x() >= width() - 40 && event->position().y() < 30) {
    emit menuRequested(event->globalPosition().toPoint());
    return;
  }
  if (locked_)
    return;
  dragging = true;
  dragOrigin = event->globalPosition().toPoint();
  savedOffset = desktopOffset();
  capturePointer();
}
void WidgetCard::mouseMoveEvent(QMouseEvent *event) {
  if (dragging) {
    auto screen = QGuiApplication::screenAt(event->globalPosition().toPoint());
    if (screen && screen->name() != desktopScreen()) {
      setDesktopPosition(screen->name(),
                         event->globalPosition().toPoint() - screen->geometry().topLeft());
      dragOrigin = event->globalPosition().toPoint();
      savedOffset = desktopOffset();
    } else
      setDesktopPosition(desktopScreen(),
                         savedOffset + event->globalPosition().toPoint() - dragOrigin);
  }
}
void WidgetCard::mouseReleaseEvent(QMouseEvent *) {
  if (dragging) {
    dragging = false;
    releasePointer();
    emit positionChanged();
  }
}
void WidgetCard::contextMenuEvent(QContextMenuEvent *event) {
  emit menuRequested(event->globalPos());
}
bool WidgetCard::event(QEvent *event) {
  if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide) {
    if (dragging) {
      dragging = false;
      emit positionChanged();
    }
  }
  const bool result = DesktopSurface::event(event);
  switch (event->type()) {
  case QEvent::LanguageChange:
  case QEvent::LocaleChange:
  case QEvent::ApplicationFontChange:
  case QEvent::FontChange:
  case QEvent::StyleChange:
  case QEvent::ApplicationPaletteChange:
  case QEvent::PaletteChange:
  case QEvent::DevicePixelRatioChange:
    staticCard_ = {};
    telemetrySet_ = false;
    if (event->type() == QEvent::DevicePixelRatioChange)
      rebuildGraph();
    update();
    if (attached())
      presentImage(renderedImage());
    break;
  default:
    break;
  }
  return result;
}

qint64 WidgetController::wallNow() const {
  return runtime_.wallClock ? runtime_.wallClock() : QDateTime::currentSecsSinceEpoch();
}
qint64 WidgetController::elapsedNow() const {
  return runtime_.elapsedClock ? runtime_.elapsedClock() : clock.elapsed();
}
WidgetController::WidgetController(QString path, bool offline, bool desktop,
                                   TransportRouter::Factory factory, WidgetRuntime runtime)
    : directory(std::move(path)), card_(desktop),
      transport(this, offline, !offline && !factory && integrationInstalled(), "bluetooth",
                factory),
      tray(QIcon(":/assets/allpowers.ico"), this), runtime_(std::move(runtime)), offline_(offline) {
  qRegisterMetaType<ap_snapshot>();
  qRegisterMetaType<HistoryPoints>();
  clock.start();
  QDir().mkpath(directory);
  QFile file(directory + "/settings.json");
  if (file.open(QIODevice::ReadOnly))
    settings = QJsonDocument::fromJson(file.readAll()).object();
  transport.setMode(settings["transport"].toString());
  locked = settings["locked"].toBool();
  card_.setLocked(locked);
  history = runtime_.makeHistoryWorker ? runtime_.makeHistoryWorker() : new HistoryWorker;
  history->moveToThread(&historyThread);
  connect(&historyThread, &QThread::finished, history, &QObject::deleteLater);
  connect(this, &WidgetController::openHistory, history, [worker = history](const QString &path) {
    worker->setBuffered(true);
    worker->open(path);
  });
  connect(history, &HistoryWorker::opened, this, [this] { historyReady = true; });
  connect(this, &WidgetController::loadHistory, history, &HistoryWorker::load);
  connect(this, &WidgetController::sample, history, &HistoryWorker::sample);
  connect(history, &HistoryWorker::sampleFinished, this, [this] {
    if (auto next = sampleQueue.complete())
      emit sample(next->address, next->data, next->wall, next->tick);
  });
  connect(this, &WidgetController::newSession, history, &HistoryWorker::newSession);
  connect(this, &WidgetController::importHistory, history, &HistoryWorker::importPoints);
  connect(history, &HistoryWorker::loaded, this,
          [this](QString address, HistoryPoints points, QJsonObject) {
            const bool board = transport.usb() && !transport.endpoint().isEmpty() &&
                               address == "usb-board:" + transport.endpoint();
            if (!board && address != historyAddress)
              return;
            if (board)
              usbPoints = std::move(points);
            else
              stored = std::move(points);
            historyDirty = true;
            requestRefresh();
          });
  connect(history, &HistoryWorker::problem, this, [this](QString message) {
    RuntimeLog::record("history.error", {{"message", message}});
    historyError = message;
    requestRefresh();
  });
  connect(history, &HistoryWorker::bufferStateChanged, this, [this](int pending, QString error) {
    bufferedSamples = pending;
    if (!historyFlushError.isEmpty() && historyError == historyFlushError)
      historyError = error;
    historyFlushError = std::move(error);
    requestRefresh();
  });
  historyThread.start();
  emit openHistory(directory + "/history.sqlite3");
  connect(&transport, &TelemetryTransport::snapshot, this, &WidgetController::receive);
  connect(&transport, &TelemetryTransport::stateChanged, this, &WidgetController::requestRefresh);
  connect(&transport, &TelemetryTransport::newSession, this, [this] {
    sampleQueue.clearPending();
    emit newSession();
  });
  connect(&transport, &TelemetryTransport::historyReady, this, [this](HistoryPoints points) {
    remotePoints = std::move(points);
    historyDirty = true;
    requestRefresh();
  });
  connect(&transport, &TelemetryTransport::historyImported, this,
          [this](HistoryPoints points) { emit importHistory(points, wallNow()); });
  menu.addAction(QCoreApplication::translate("Allpowers", "Settings…"), this,
                 &WidgetController::configure);
  menu.addAction(QCoreApplication::translate("Allpowers", "Connect"), this,
                 &WidgetController::connectStation);
  menu.addAction(QCoreApplication::translate("Allpowers", "Disconnect"), this,
                 &WidgetController::disconnectStation);
  auto *lock = menu.addAction(QCoreApplication::translate("Allpowers", "Lock position"));
  lock->setCheckable(true);
  lock->setChecked(locked);
  connect(lock, &QAction::toggled, this, [this](bool value) {
    locked = value;
    card_.setLocked(value);
    save();
  });
  menu.addAction(QCoreApplication::translate("Allpowers", "Licenses…"), this,
                 [] { showNotices(nullptr); });
  menu.addSeparator();
  menu.addAction(QCoreApplication::translate("Allpowers", "Open diagnostics folder"), this,
                 [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(directory)); });
  menu.addAction(QCoreApplication::translate("Allpowers", "Quit"), this, [] {
    RuntimeLog::record("widget.quit_menu");
    qApp->quit();
  });
  tray.setContextMenu(&menu);
  connect(&tray, &QSystemTrayIcon::activated, this, [this](auto reason) {
    if (reason == QSystemTrayIcon::DoubleClick)
      configure();
  });
  connect(&card_, &WidgetCard::menuRequested, this, [this](QPoint point) { menu.popup(point); });
  connect(&card_, &WidgetCard::positionChanged, this, &WidgetController::save);
  connect(&card_, &DesktopSurface::attachmentChanged, this, [this] {
    RuntimeLog::record("widget.attachment", card_.nativeDiagnostics());
    if (!card_.attached() && !offline_)
      tray.showMessage("ALLPOWERS Widget", card_.attachmentError());
    requestRefresh();
  });
  timer.setInterval(1000);
  connect(&timer, &QTimer::timeout, this, [this] {
    if (!offline_ && receivedAt >= 0 && elapsedNow() - receivedAt > 5000)
      latest.fresh = false;
    requestRefresh();
  });
  timer.start();
  new TransportTrace(&transport, this, [this] {
    auto info = diagnostics();
    info.remove("transport"); // The trace already includes the complete transport context.
    return info;
  });
}
WidgetController::~WidgetController() {
  RuntimeLog::record("widget.destroy_begin", diagnostics());
  timer.stop();
  transport.disconnect(this);
  disconnectStation();
  save();
  if (auto pending = sampleQueue.takePending())
    emit sample(pending->address, pending->data, pending->wall, pending->tick);
  // Drain posted samples, then commit the in-memory tail on its owning thread.
  bool flushed = false;
  QMetaObject::invokeMethod(
      history, [worker = history, &flushed] { flushed = worker->flush(); },
      Qt::BlockingQueuedConnection);
  if (!flushed)
    RuntimeLog::record("history.flush_failed");
  historyThread.quit();
  historyThread.wait();
  RuntimeLog::record("widget.destroy_end");
}
bool WidgetController::save() {
  settings["locked"] = locked;
  settings["screen"] = card_.desktopScreen();
  settings["x"] = card_.desktopOffset().x();
  settings["y"] = card_.desktopOffset().y();
  QSaveFile file(directory + "/settings.json");
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(settings).toJson()) < 0 ||
      !file.commit()) {
    historyError = QCoreApplication::translate("Allpowers", "Could not save widget settings");
    refresh();
    return false;
  }
  return true;
}
void WidgetController::start() {
  if (!offline_)
    tray.show();
  card_.setDesktopPosition(settings["screen"].toString(),
                           {settings["x"].toInt(24), settings["y"].toInt(24)});
  if (!offline_) {
    if (settings["endpoint"].toString().isEmpty() && !transport.remote())
      QTimer::singleShot(0, this, &WidgetController::configure);
    else
      connectStation();
  }
}
void WidgetController::connectStation() {
  if (offline_ || transport.remote())
    return;
  const QString endpoint = settings["endpoint"].toString();
  if (endpoint.isEmpty()) {
    configure();
    return;
  }
  transport.resume(endpoint, settings["name"].toString(), settings["profile"].toInt());
}
void WidgetController::disconnectStation() {
  // Closing a viewer must not stop the installed shared UPS service.
  if (!transport.remote())
    transport.stop();
}
void WidgetController::receive(ap_snapshot value) {
  if (offline_)
    return;
  latest = value;
  receivedAt = elapsedNow();
  const QString address = transport.address();
  if (address != historyAddress) {
    historyAddress = address;
    stored.clear();
    remotePoints.clear();
    historyDirty = true;
    sampleAt = -1;
    sampleQueue.clearPending();
    emit newSession();
    emit loadHistory(address, wallNow());
  }
  const qint64 now = wallNow();
  if (!offline_ && !transport.remote() && transport.active() && value.main_seen &&
      !address.isEmpty() && now != sampleAt) {
    sampleAt = now;
    HistoryPoint point;
    point.address = address;
    point.data = value;
    point.wall = now;
    point.tick = elapsedNow() / 1000;
    if (auto next = sampleQueue.submit(std::move(point)))
      emit sample(next->address, next->data, next->wall, next->tick);
  }
  refresh();
}
void WidgetController::requestRefresh() {
  if (refreshPending)
    return;
  refreshPending = true;
  QMetaObject::invokeMethod(
      this,
      [this] {
        if (refreshPending)
          refresh();
      },
      Qt::QueuedConnection);
}
void WidgetController::refresh() {
  refreshPending = false;
  const QString endpoint = transport.usb() ? transport.endpoint() : QString{};
  if (endpoint != usbEndpoint) {
    usbEndpoint = endpoint;
    usbPoints.clear();
    historyDirty = true;
    if (!endpoint.isEmpty())
      emit loadHistory("usb-board:" + endpoint, wallNow());
  }
  QString phase = historyError.isEmpty() ? transport.phase() : historyError;
  if (!card_.attachmentError().isEmpty())
    phase = card_.attachmentError();
  QString name = transport.diagnostics()["name"].toString().trimmed();
  if (name.isEmpty())
    name = settings["name"].toString();
  card_.setDeviceName(name);
  card_.setTelemetry(latest, phase);
  tray.setToolTip((QStringLiteral("ALLPOWERS · ") +
                   WidgetCard::reading(latest, 1, latest.soc, "%") + "\n" + phase)
                      .left(127));
  if (!offline_)
    refreshHistory();
}
void WidgetController::refreshHistory() {
  const bool usb = transport.usb();
  QString session;
  if (usb) {
    const auto boot = transport.diagnostics()["history_session"].toInteger();
    if (transport.connected())
      session = QString("usb-%1").arg(boot);
  }
  const bool changed = historyDirty || usb != plottedUsb || session != plottedUsbSession;
  if (changed) {
    plotted = mergeHistory(stored, remotePoints);
    if (usb)
      plotted = mergeUsbHistory(plotted, usbPoints, session);
    historyDirty = false;
    plottedUsb = usb;
    plottedUsbSession = std::move(session);
  }
  const auto now = wallNow();
  if (changed || now != plottedAt) {
    plottedAt = now;
    card_.setHistory(plotted, now);
  }
}
void WidgetController::configure() {
  if (settingsDialog) {
    settingsDialog->showNormal();
    settingsDialog->raise();
    settingsDialog->activateWindow();
    return;
  }
  QDialog dialog;
  settingsDialog = &dialog;
  const auto clearDialog = qScopeGuard([this] { settingsDialog = nullptr; });
  dialog.setObjectName("widgetSettings");
  dialog.setWindowTitle(QCoreApplication::translate("Allpowers", "ALLPOWERS Widget — settings"));
  dialog.setMinimumWidth(420);
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  QComboBox mode, devices, profile, language;
  populateUiLanguages(language, settings["ui_language"].toString());
  form->addRow(QCoreApplication::translate("Allpowers", "Interface language"), &language);
  QLabel languageNote(QCoreApplication::translate(
      "Allpowers", "Language changes apply after restarting the widget."));
  languageNote.setWordWrap(true);
  layout->addWidget(&languageNote);
  mode.addItem("Bluetooth", "bluetooth");
  mode.addItem("USB", "usb");
  mode.setCurrentIndex(transport.usb() ? 1 : 0);
  profile.addItems(
      {QCoreApplication::translate("Allpowers", "Automatic"), "Old10", "Legacy16", "R2500 V2"});
  profile.setCurrentIndex(qBound(0, settings["profile"].toInt(), 3));
  auto addSaved = [&] {
    devices.clear();
    if (mode.currentData() == settings["transport"].toString("bluetooth") &&
        !settings["endpoint"].toString().isEmpty())
      devices.addItem(settings["name"].toString(settings["endpoint"].toString()),
                      settings["endpoint"].toString());
  };
  addSaved();
  const auto initialMode = mode.currentData().toString();
  const auto initialEndpoint = devices.currentData().toString();
  const int initialProfile = profile.currentIndex();
  form->addRow(QCoreApplication::translate("Allpowers", "Connection"), &mode);
  form->addRow(QCoreApplication::translate("Allpowers", "Device"), &devices);
  form->addRow(QCoreApplication::translate("Allpowers", "Station profile"), &profile);
  QPushButton scan(QCoreApplication::translate("Allpowers", "Find devices"));
  layout->addWidget(&scan);
  QCheckBox autostart(QCoreApplication::translate("Allpowers", "Start when signing in to Windows"));
  const QString registryPath =
      "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
  QSettings registry(registryPath, QSettings::NativeFormat);
  const bool initialStartup = registry.contains("AllpowersWidget");
  autostart.setChecked(initialStartup);
  autostart.setEnabled(!offline_);
  layout->addWidget(&autostart);
  QLabel note(
      transport.remote()
          ? QCoreApplication::translate(
                "Allpowers", "The UPS service handles Bluetooth. The widget only reads its data.")
          : QCoreApplication::translate(
                "Allpowers", "Disconnect the station in the main monitor before connecting."));
  note.setWordWrap(true);
  layout->addWidget(&note);
  QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  translateDialogButtons(buttons);
  layout->addWidget(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  // Discovery uses a separate inactive router so cancelling settings never
  // disconnects the running widget or modifies its saved transport.
  TransportRouter discovery(&dialog, offline_, false, mode.currentData().toString());
  connect(&mode, &QComboBox::currentIndexChanged, &dialog, [&] {
    discovery.setMode(mode.currentData().toString());
    addSaved();
  });
  connect(&discovery, &TelemetryTransport::found, &dialog, [&](QString id, QString name, int, int) {
    int row = devices.findData(id);
    if (row < 0)
      devices.addItem(name.isEmpty() ? id : name, id);
    else
      devices.setItemText(row, name.isEmpty() ? id : name);
  });
  connect(&scan, &QPushButton::clicked, &discovery, &TelemetryTransport::scan);
  if (dialog.exec() != QDialog::Accepted)
    return;
  const bool languageChanged =
      language.currentData().toString() != normalizedUiLanguage(settings["ui_language"].toString());
  if (languageChanged) {
    const auto previousLanguage = settings["ui_language"];
    settings["ui_language"] = language.currentData().toString();
    if (!save()) {
      settings["ui_language"] = previousLanguage;
      tray.showMessage("ALLPOWERS Widget", historyError);
      return;
    }
    tray.showMessage(
        "ALLPOWERS Widget",
        QCoreApplication::translate(
            "Allpowers", "Language saved. Close and reopen the application to apply it."));
  }
  const bool connectionChanged = mode.currentData().toString() != initialMode ||
                                 devices.currentData().toString() != initialEndpoint ||
                                 profile.currentIndex() != initialProfile;
  const bool startupChanged = autostart.isChecked() != initialStartup;
  if (!connectionChanged && !startupChanged)
    return;
  if (connectionChanged && devices.currentData().toString().isEmpty() &&
      !(mode.currentIndex() == 0 && integrationInstalled())) {
    tray.showMessage("ALLPOWERS Widget",
                     QCoreApplication::translate(
                         "Allpowers", "No device selected. Open settings and scan for devices."));
    return;
  }
  if (startupChanged && !offline_ &&
      !setWidgetStartup(registry, autostart.isChecked(), QCoreApplication::applicationFilePath())) {
    tray.showMessage("ALLPOWERS Widget",
                     QCoreApplication::translate("Allpowers", "Could not change startup setting."));
    return;
  }
  if (!connectionChanged)
    return;
  disconnectStation();
  settings["transport"] = mode.currentData().toString();
  settings["endpoint"] = devices.currentData().toString();
  settings["name"] = devices.currentText();
  settings["profile"] = profile.currentIndex();
  latest = {};
  receivedAt = -1;
  stored.clear();
  remotePoints.clear();
  usbPoints.clear();
  historyDirty = true;
  usbEndpoint.clear();
  historyAddress.clear();
  transport.setMode(mode.currentData().toString());
  save();
  connectStation();
  refresh();
}
void WidgetController::loadDemo() {
  const auto now = wallNow();
  stored.clear();
  for (int i = 0; i <= 3600; ++i) {
    ap_snapshot value{};
    value.known = 15;
    value.fresh = true;
    value.soc = 74;
    value.input_w = 260 + int(45 * std::sin(i / 180.));
    value.output_w = 170 + int(65 * std::sin(i / 290.));
    value.runtime_s = 2 * 86400 + 3 * 3600;
    // Deliberate demo gap to exercise missing-data rendering.
    if (i >= 1500 && i < 1680)
      value.fresh = false;
    stored.append({"demo", "demo", now - 3600 + i, i, value, 1});
  }
  latest = stored.last().data;
  plotted = stored;
  card_.setDeviceName("AP R2500 V2.0");
  card_.setHistory(stored, now);
  card_.setTelemetry(latest, QCoreApplication::translate("Allpowers", "Demo data"));
  timer.stop();
}
QJsonObject WidgetController::diagnostics() const {
  auto result = card_.nativeDiagnostics();
  const QJsonObject values{{"history_error", historyError},
                           {"history_flush_error", historyFlushError},
                           {"history_buffered_samples", bufferedSamples},
                           {"diagnostic_log", RuntimeLog::path()},
                           {"transport", transport.diagnostics()},
                           {"snapshot_age_ms", receivedAt < 0 ? -1 : elapsedNow() - receivedAt},
                           {"fresh", bool(latest.fresh)},
                           {"history_ready", historyReady},
                           {"ui_language", qApp->property("ui_language").toString()},
                           {"history_worker_busy", sampleQueue.busy()},
                           {"history_sample_pending", sampleQueue.waiting()},
                           {"desktop_attached", card_.attached()},
                           {"desktop_error", card_.attachmentError()},
                           {"points", plotted.size()},
                           {"usb_history_points", usbPoints.size()},
                           {"input_segments", WidgetCard::graphSegments(plotted, 2, wallNow())},
                           {"output_segments", WidgetCard::graphSegments(plotted, 4, wallNow())},
                           {"bluetooth_used", transport.diagnostics()["bluetooth_used"].toBool()}};
  for (auto it = values.begin(); it != values.end(); ++it)
    result[it.key()] = it.value();
  return result;
}
