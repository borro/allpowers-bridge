#include "widget.h"
#include "integration.h"
#include "notices.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <cmath>

bool setWidgetStartup(QSettings &store, bool enabled, const QString &executable) {
  if (enabled)
    store.setValue("AllpowersWidget", '"' + QDir::toNativeSeparators(executable) + '"');
  else
    store.remove("AllpowersWidget");
  store.sync();
  return store.status() == QSettings::NoError;
}

WidgetCard::WidgetCard(bool desktop, std::function<quintptr()> hostFinder)
    : DesktopSurface(desktop, std::move(hostFinder)) {
  setWindowTitle(QStringLiteral("ALLPOWERS Widget"));
  setFixedSize(300, 210);
  setCursor(Qt::SizeAllCursor);
  connect(this, &DesktopSurface::surfaceChanged, this, [this] { presentImage(renderedImage()); });
}
QString WidgetCard::reading(const ap_snapshot &s, quint32 mask, quint32 value, QString suffix) {
  return (s.known & mask) ? QString::number(value) + suffix : QStringLiteral("—");
}
QString WidgetCard::remainingTime(const ap_snapshot &s) {
  if (!(s.known & 8))
    return QStringLiteral("—");
  const auto days = s.runtime_s / 86400, hours = s.runtime_s / 3600, minutes = s.runtime_s / 60;
  if (days)
    return QStringLiteral("%1 д %2 ч").arg(days).arg(hours % 24);
  if (hours)
    return QStringLiteral("%1 ч %2 мин").arg(hours).arg(minutes % 60);
  return QStringLiteral("%1 мин").arg(minutes);
}
void WidgetCard::setDeviceName(QString name) {
  name = name.trimmed();
  if (name.isEmpty())
    name = QStringLiteral("Нет устройства");
  if (deviceName_ == name)
    return;
  deviceName_ = std::move(name);
  setAccessibleName(deviceName_ + " " + reading(snapshot_, 1, snapshot_.soc, "%"));
  update();
  if (attached())
    presentImage(renderedImage());
}
void WidgetCard::setTelemetry(ap_snapshot value, QString phase) {
  snapshot_ = value;
  phase_ = std::move(phase);
  const QString status =
      !value.fresh && value.known ? QStringLiteral("Данные устарели · ") + phase_ : phase_;
  setToolTip(status);
  setAccessibleName(deviceName_ + " " + reading(value, 1, value.soc, "%"));
  setAccessibleDescription(status);
  update();
  if (attached())
    presentImage(renderedImage());
}
void WidgetCard::setHistory(HistoryPoints points, qint64 now) {
  points_ = std::move(points);
  now_ = now;
  update();
  if (attached())
    presentImage(renderedImage());
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
  text({154, 30, 134, 16}, QStringLiteral("осталось"), 9, QColor("#98adc9"));
  text({154, 47, 134, 26}, remainingTime(s), 12, foreground);
  text({12, 82, 138, 24}, QStringLiteral("Вход  ") + reading(s, 2, s.input_w, " Вт"), 10,
       QColor("#59d9aa"));
  text({154, 82, 140, 24}, QStringLiteral("Выход  ") + reading(s, 4, s.output_w, " Вт"), 10,
       QColor("#6ab4ff"));
  const QRectF plot(14, 126, 272, 54);
  double maximum = 1;
  for (const auto &v : points_)
    if (v.wall >= now_ - 3600 && v.wall <= now_ && v.data.fresh) {
      if (v.data.known & 2)
        maximum = qMax(maximum, double(v.data.input_w));
      if (v.data.known & 4)
        maximum = qMax(maximum, double(v.data.output_w));
    }
  maximum = qMax(10., std::ceil(maximum / 10) * 10);
  text({14, 108, 180, 16}, QString::number(maximum, 'f', 0) + " Вт", 8, QColor("#8ea5c4"));
  p.setPen(QPen(QColor("#2b3b53"), 1));
  p.drawLine(plot.bottomLeft(), plot.bottomRight());
  p.drawLine(plot.topLeft(), plot.topRight());
  for (quint32 mask : {2u, 4u}) {
    p.setPen(QPen(QColor(mask == 2 ? "#59d9aa" : "#6ab4ff"), 1.6));
    auto point = [&](const HistoryPoint &v) {
      double watts = mask == 2 ? v.data.input_w : v.data.output_w;
      return QPointF(plot.left() + (v.wall - (now_ - 3600)) * plot.width() / 3600.,
                     plot.bottom() - watts * plot.height() / maximum);
    };
    for (qsizetype i = 1; i < points_.size(); ++i) {
      const auto &a = points_[i - 1], &b = points_[i];
      if (a.wall >= now_ - 3600 && b.wall <= now_ && (a.data.known & mask) &&
          (b.data.known & mask) && joinPoints(a, b))
        p.drawLine(point(a), point(b));
    }
  }
  text({14, 184, 180, 16}, QStringLiteral("−1 час"), 8, QColor("#8ea5c4"));
  text({240, 184, 50, 16}, QStringLiteral("сейчас"), 8, QColor("#8ea5c4"));
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
  return DesktopSurface::event(event);
}

WidgetController::WidgetController(QString path, bool offline, bool desktop,
                                   TransportRouter::Factory factory)
    : directory(std::move(path)), card_(desktop),
      transport(this, offline, !offline && !factory && integrationInstalled(), "bluetooth",
                factory),
      tray(QIcon(":/assets/allpowers.ico"), this), offline_(offline) {
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
  history = new HistoryWorker;
  history->moveToThread(&historyThread);
  connect(&historyThread, &QThread::finished, history, &QObject::deleteLater);
  connect(this, &WidgetController::openHistory, history, &HistoryWorker::open);
  connect(this, &WidgetController::loadHistory, history, &HistoryWorker::load);
  connect(this, &WidgetController::sample, history, &HistoryWorker::sample);
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
            refreshHistory();
          });
  connect(history, &HistoryWorker::problem, this, [this](QString message) {
    historyError = message;
    refresh();
  });
  historyThread.start();
  emit openHistory(directory + "/history.sqlite3");
  connect(&transport, &TelemetryTransport::snapshot, this, &WidgetController::receive);
  connect(&transport, &TelemetryTransport::stateChanged, this, &WidgetController::refresh);
  connect(&transport, &TelemetryTransport::newSession, this, &WidgetController::newSession);
  connect(&transport, &TelemetryTransport::historyReady, this, [this](HistoryPoints points) {
    remotePoints = std::move(points);
    refreshHistory();
  });
  connect(&transport, &TelemetryTransport::historyImported, this, [this](HistoryPoints points) {
    emit importHistory(points, QDateTime::currentSecsSinceEpoch());
  });
  menu.addAction(QStringLiteral("Настройки…"), this, &WidgetController::configure);
  menu.addAction(QStringLiteral("Подключить"), this, &WidgetController::connectStation);
  menu.addAction(QStringLiteral("Отключить"), this, &WidgetController::disconnectStation);
  auto *lock = menu.addAction(QStringLiteral("Закрепить положение"));
  lock->setCheckable(true);
  lock->setChecked(locked);
  connect(lock, &QAction::toggled, this, [this](bool value) {
    locked = value;
    card_.setLocked(value);
    save();
  });
  menu.addAction(QStringLiteral("Лицензии…"), this, [] { showNotices(nullptr); });
  menu.addSeparator();
  menu.addAction(QStringLiteral("Выйти"), qApp, &QApplication::quit);
  tray.setContextMenu(&menu);
  connect(&tray, &QSystemTrayIcon::activated, this, [this](auto reason) {
    if (reason == QSystemTrayIcon::DoubleClick)
      configure();
  });
  connect(&card_, &WidgetCard::menuRequested, this, [this](QPoint point) { menu.popup(point); });
  connect(&card_, &WidgetCard::positionChanged, this, &WidgetController::save);
  connect(&card_, &DesktopSurface::attachmentChanged, this, [this] {
    if (!card_.attached() && !offline_)
      tray.showMessage("ALLPOWERS Widget", card_.attachmentError());
    refresh();
  });
  timer.setInterval(1000);
  connect(&timer, &QTimer::timeout, this, [this] {
    if (!offline_ && receivedAt >= 0 && clock.elapsed() - receivedAt > 5000)
      latest.fresh = false;
    refresh();
  });
  timer.start();
}
WidgetController::~WidgetController() {
  timer.stop();
  transport.disconnect(this);
  disconnectStation();
  save();
  // All posted history writes complete before the worker thread quits.
  QMetaObject::invokeMethod(history, [] {}, Qt::BlockingQueuedConnection);
  historyThread.quit();
  historyThread.wait();
}
void WidgetController::save() {
  settings["locked"] = locked;
  settings["screen"] = card_.desktopScreen();
  settings["x"] = card_.desktopOffset().x();
  settings["y"] = card_.desktopOffset().y();
  QSaveFile file(directory + "/settings.json");
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(settings).toJson()) < 0 ||
      !file.commit()) {
    historyError = QStringLiteral("Не удалось сохранить настройки виджета");
    refresh();
  }
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
  receivedAt = clock.elapsed();
  const QString address = transport.address();
  if (address != historyAddress) {
    historyAddress = address;
    stored.clear();
    remotePoints.clear();
    sampleAt = -1;
    emit newSession();
    emit loadHistory(address, QDateTime::currentSecsSinceEpoch());
  }
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (!offline_ && !transport.remote() && transport.active() && value.main_seen &&
      !address.isEmpty() && now != sampleAt) {
    sampleAt = now;
    emit sample(address, value, now, clock.elapsed() / 1000);
  }
  refresh();
}
void WidgetController::refresh() {
  const QString endpoint = transport.usb() ? transport.endpoint() : QString{};
  if (endpoint != usbEndpoint) {
    usbEndpoint = endpoint;
    usbPoints.clear();
    if (!endpoint.isEmpty())
      emit loadHistory("usb-board:" + endpoint, QDateTime::currentSecsSinceEpoch());
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
  plotted = mergeHistory(stored, remotePoints);
  if (transport.usb()) {
    const auto boot = transport.diagnostics()["history_session"].toInteger();
    plotted = mergeUsbHistory(plotted, usbPoints,
                              transport.connected() ? QString("usb-%1").arg(boot) : QString{});
  }
  card_.setHistory(plotted, QDateTime::currentSecsSinceEpoch());
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
  dialog.setWindowTitle(QStringLiteral("ALLPOWERS Widget — настройки"));
  dialog.setMinimumWidth(420);
  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout;
  layout->addLayout(form);
  QComboBox mode, devices, profile;
  mode.addItem("Bluetooth", "bluetooth");
  mode.addItem("USB", "usb");
  mode.setCurrentIndex(transport.usb() ? 1 : 0);
  profile.addItems({QStringLiteral("Автоматический"), "Old10", "Legacy16", "R2500 V2"});
  profile.setCurrentIndex(qBound(0, settings["profile"].toInt(), 3));
  auto addSaved = [&] {
    devices.clear();
    if (mode.currentData() == settings["transport"].toString("bluetooth") &&
        !settings["endpoint"].toString().isEmpty())
      devices.addItem(settings["name"].toString(settings["endpoint"].toString()),
                      settings["endpoint"].toString());
  };
  addSaved();
  form->addRow(QStringLiteral("Подключение"), &mode);
  form->addRow(QStringLiteral("Устройство"), &devices);
  form->addRow(QStringLiteral("Профиль станции"), &profile);
  QPushButton scan(QStringLiteral("Найти устройства"));
  layout->addWidget(&scan);
  QCheckBox autostart(QStringLiteral("Запускать при входе в Windows"));
  const QString registryPath =
      "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
  QSettings registry(registryPath, QSettings::NativeFormat);
  autostart.setChecked(registry.contains("AllpowersWidget"));
  autostart.setEnabled(!offline_);
  layout->addWidget(&autostart);
  QLabel note(
      transport.remote()
          ? QStringLiteral("Bluetooth обслуживает служба ИБП. Виджет только читает её данные.")
          : QStringLiteral("Перед подключением отключите станцию в основном мониторе."));
  note.setWordWrap(true);
  layout->addWidget(&note);
  QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
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
  if (devices.currentData().toString().isEmpty() &&
      !(mode.currentIndex() == 0 && integrationInstalled())) {
    tray.showMessage(
        "ALLPOWERS Widget",
        QStringLiteral("Устройство не выбрано. Откройте настройки и выполните поиск."));
    return;
  }
  if (!offline_ &&
      !setWidgetStartup(registry, autostart.isChecked(), QCoreApplication::applicationFilePath())) {
    tray.showMessage("ALLPOWERS Widget", QStringLiteral("Не удалось изменить автозапуск."));
    return;
  }
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
  usbEndpoint.clear();
  historyAddress.clear();
  transport.setMode(mode.currentData().toString());
  save();
  connectStation();
  refresh();
}
void WidgetController::loadDemo() {
  const auto now = QDateTime::currentSecsSinceEpoch();
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
  card_.setTelemetry(latest, QStringLiteral("Демонстрационные данные"));
  timer.stop();
}
QJsonObject WidgetController::diagnostics() const {
  auto result = card_.nativeDiagnostics();
  const QJsonObject values{
      {"history_error", historyError},
      {"desktop_attached", card_.attached()},
      {"desktop_error", card_.attachmentError()},
      {"points", plotted.size()},
      {"usb_history_points", usbPoints.size()},
      {"input_segments", WidgetCard::graphSegments(plotted, 2, QDateTime::currentSecsSinceEpoch())},
      {"output_segments",
       WidgetCard::graphSegments(plotted, 4, QDateTime::currentSecsSinceEpoch())},
      {"bluetooth_used",
       !offline_ && !transport.usb() && !transport.remote() && transport.active()}};
  for (auto it = values.begin(); it != values.end(); ++it)
    result[it.key()] = it.value();
  return result;
}
