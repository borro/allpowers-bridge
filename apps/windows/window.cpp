#include "window.h"
#include "AllpowersModels.h"
#include "i18n.h"
#include "integration.h"
#include "notices.h"
#include "product-version.h"
#include "service_client.h"
#include "transport_router.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <memory>

static QString labelStyle(const QString &color, int size, bool bold = false) {
  return QString("color:%1;font-size:%2px;font-weight:%3;")
      .arg(color)
      .arg(size)
      .arg(bold ? 700 : 400);
}
static QLabel *text(const QString &s, int size = 14, const QString &color = "#b0c0db",
                    bool bold = false, bool adaptive = false) {
  QLabel *l = adaptive ? new AdaptiveLabel(s) : new QLabel(s);
  l->setStyleSheet(labelStyle(color, size, bold));
  return l;
}
static void setText(QLabel *label, const QString &value) {
  if (label->text() != value)
    label->setText(value);
}

class Check : public QCheckBox {
public:
  using QCheckBox::QCheckBox;

protected:
  void paintEvent(QPaintEvent *event) override {
    QCheckBox::paintEvent(event);
    if (!isChecked())
      return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#fff"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    double y = height() / 2.;
    QPolygonF tick;
    tick << QPointF(4, y) << QPointF(8, y + 4) << QPointF(14, y - 4);
    p.drawPolyline(tick);
  }
};

QString monitorDataDirectory() {
  QString path = qEnvironmentVariable("ALLPOWERS_DATA_DIR");
  if (!path.isEmpty())
    return path;
  QString base = qEnvironmentVariable("LOCALAPPDATA");
  if (base.isEmpty())
    base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  return QDir(base).filePath("AllpowersMonitor");
}
MonitorWindow::MonitorWindow(bool testMode, QWidget *parent, TelemetryTransport *transport)
    : QMainWindow(parent), offline(testMode) {
  qRegisterMetaType<ap_snapshot>();
  qRegisterMetaType<HistoryPoints>();
  uptime.start();
  dataPath = monitorDataDirectory();
  QDir().mkpath(dataPath);
  QFile file(QDir(dataPath).filePath("settings.json"));
  if (file.open(QIODevice::ReadOnly))
    settings = QJsonDocument::fromJson(file.readAll()).object();
  selectedAddress = settings["address"].toString();
  if (settings["transport"] == "usb")
    selectedAddress.clear();
  setWindowTitle("ALLPOWERS Monitor");
  resize(1120, 820);
  setMinimumSize(930, 710);
  setStyleSheet(R"(
    QMainWindow,QWidget#root{background:#0e1727;color:#ecf3ff;font-family:'Segoe UI';font-size:14px;}
    QFrame#card{background:#172235;border:1px solid #223149;border-radius:14px;}
    QPushButton{background:#20324c;border:1px solid #354b6c;border-radius:9px;color:#eaf2ff;padding:10px 18px;font-weight:600;min-height:19px;}
    QPushButton:hover{background:#2b4161;border-color:#6488ba;}QPushButton:pressed{background:#344e73;}
    QPushButton#primary{background:#398bf2;border-color:#398bf2;color:#fff;}QPushButton#primary:hover{background:#55a0ff;}
    QPushButton:disabled{background:#172339;border-color:#24344c;color:#71829c;}
    QPushButton#primary:disabled{background:#172339;border-color:#24344c;color:#71829c;}
    QPushButton:checked{background:#294366;border-color:#5481ba;color:#fff;}
    QTabWidget::pane{border:0;}
    QTabBar::tab{background:#172235;color:#a9bedb;padding:10px 18px;border-bottom:2px solid #24344c;margin-bottom:8px;}
    QTabBar::tab:selected{color:#edf5ff;border-color:#69b3ff;}
    QTabBar::tab:hover{background:#20324c;}
    QCheckBox{color:#bdcce2;spacing:8px;}QCheckBox::indicator{width:17px;height:17px;border:1px solid #627a9d;border-radius:4px;background:#17263d;}
    QCheckBox::indicator:checked{background:#54a0ff;border-color:#89beff;}
    QPlainTextEdit{background:#172235;color:#b9cee9;border:1px solid #2b405c;border-radius:10px;padding:12px;font-family:Consolas;font-size:13px;}
    QTableWidget{background:#172235;alternate-background-color:#1b293e;color:#eaf2ff;border:1px solid #2b405c;font-family:'Segoe UI';font-size:14px;selection-background-color:#345987;selection-color:#fff;gridline-color:#24344c;}
    QTableWidget::item{padding:7px 12px;border:0;}
    QHeaderView::section{background:#20324c;color:#bdd0ea;border:0;border-bottom:1px solid #354b6c;padding:10px 12px;font-family:'Segoe UI';font-size:13px;font-weight:600;}
    QScrollBar:vertical{background:#172235;width:10px;margin:2px;}
    QScrollBar::handle:vertical{background:#526d91;min-height:24px;border-radius:4px;}
    QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
    QScrollBar:horizontal{background:#172235;height:10px;margin:2px;}
    QScrollBar::handle:horizontal{background:#526d91;min-width:24px;border-radius:4px;}
    QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}
    QScrollBar::add-page,QScrollBar::sub-page{background:none;}
    QToolTip{background:#253b59;color:#fff;border:1px solid #52759f;padding:7px;}
  )");
  auto *root = new QWidget;
  root->setObjectName("root");
  setCentralWidget(root);
  auto *layout = new QVBoxLayout(root);
  layout->setContentsMargins(28, 24, 28, 20);
  layout->setSpacing(16);
  auto *heading = new QHBoxLayout;
  heading->addWidget(text("ALLPOWERS", 31, "#f1f6ff", true));
  heading->addStretch();
  heading->addWidget(text(QString("MONITOR  ·  %1").arg(APP_VERSION), 12, "#7d9cc8"));
  layout->addLayout(heading);
  auto *sourceRow = new QHBoxLayout;
  transportMode = new Select;
  transportMode->setObjectName("transportMode");
  transportMode->addItem(QCoreApplication::translate("Allpowers", "Computer Bluetooth"),
                         "bluetooth");
  transportMode->addItem("USB · Waveshare / Pocket", "usb");
  transportMode->setCurrentIndex(settings["transport"] == "usb" ? 1 : 0);
  sourceRow->addWidget(transportMode);
  transportNote =
      text(QCoreApplication::translate("Allpowers", "Select telemetry source"), 13, "#8ca6cb");
  sourceRow->addWidget(transportNote, 1);
  layout->addLayout(sourceRow);
  auto *connection = new QHBoxLayout;
  devices = new Select;
  devices->setObjectName("devices");
  devices->setPlaceholderText(
      QCoreApplication::translate("Allpowers", "Find and select a station"));
  devices->setMinimumWidth(360);
  connect(static_cast<Select *>(devices), &Select::popupClosed, this, [this] {
    if (deviceListPending)
      rebuildDevices();
  });
  connection->addWidget(devices, 1);
  scanButton = new QPushButton(QCoreApplication::translate("Allpowers", "Find stations"));
  scanButton->setObjectName("scan");
  connection->addWidget(scanButton);
  connectButton = new QPushButton(QCoreApplication::translate("Allpowers", "Connect"));
  connectButton->setObjectName("primary");
  connection->addWidget(connectButton);
  layout->addLayout(connection);
  auto *options = new QHBoxLayout;
  stationsOnly = new Check(QCoreApplication::translate("Allpowers", "Stations only"));
  stationsOnly->setChecked(true);
  options->addWidget(stationsOnly);
  options->addStretch();
  profile = new Select;
  profile->setObjectName("profile");
  profile->addItems({QCoreApplication::translate("Allpowers", "Auto by station name"),
                     QCoreApplication::translate("Allpowers", "Old protocol · Old10"),
                     QCoreApplication::translate("Allpowers", "V2 protocol · Legacy16"),
                     "R2500 V2"});
  profile->setCurrentIndex(qBound(0, settings["profile"].toInt(), 3));
  options->addWidget(profile);
  layout->addLayout(options);
  status = text(QCoreApplication::translate("Allpowers", "Disconnected"), 14, "#9eb6d7");
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *navigation = new QHBoxLayout;
  auto *group = new QButtonGroup(this);
  pages = new QStackedWidget;
  int pageIndex = 0;
  for (const QString &name : {QCoreApplication::translate("Allpowers", "Overview"),
                              QCoreApplication::translate("Allpowers", "History"),
                              QCoreApplication::translate("Allpowers", "Diagnostics")}) {
    auto *button = new QPushButton(name);
    button->setCheckable(true);
    group->addButton(button, pageIndex);
    navigation->addWidget(button);
    auto *shortcut = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(pageIndex + 1)), this);
    connect(shortcut, &QShortcut::activated, button, &QPushButton::click);
    ++pageIndex;
  }
  navigation->addStretch();
  auto *about = new QPushButton(QCoreApplication::translate("Allpowers", "About"));
  connect(about, &QPushButton::clicked, this, [this] { showNotices(this); });
  auto *language = new Select;
  populateUiLanguages(*language, settings["ui_language"].toString());
  language->setToolTip(
      QCoreApplication::translate("Allpowers", "Interface language · applies after restart"));
  navigation->addWidget(language);
  connect(language, &QComboBox::currentIndexChanged, this, [this, language] {
    settings["ui_language"] = language->currentData().toString();
    saveSettings();
    QMessageBox::information(
        this, QCoreApplication::translate("Allpowers", "Interface language"),
        QCoreApplication::translate(
            "Allpowers", "Language saved. Close and reopen the application to apply it."));
  });
  navigation->addWidget(about);
  layout->addLayout(navigation);
  pages->addWidget(overviewPage());
  pages->addWidget(historyPage());
  pages->addWidget(diagnosticPage());
  layout->addWidget(pages, 1);
  footer =
      text(QCoreApplication::translate("Allpowers", "Read only · History saved on this computer"),
           12, "#7d96b9");
  layout->addWidget(footer);
  ble = transport ? transport
                  : new TransportRouter(this, offline, integrationInstalled(),
                                        transportMode->currentData().toString());
  ble->setParent(this);
  transportMode->setEnabled(bool(qobject_cast<TransportRouter *>(ble)));
  if (ble->usb())
    transportMode->setCurrentIndex(1);
  if (ble->remote())
    footer->setText(QCoreApplication::translate(
        "Allpowers", "Read only  ·  Windows service  ·  Shared computer history"));
  connect(transportMode, &QComboBox::currentIndexChanged, this, &MonitorWindow::changeTransport);
  connect(group, &QButtonGroup::idClicked, this, [this, group](int id) {
    pages->setCurrentIndex(id);
    group->button(id)->setChecked(true);
    settings["page"] = QStringList{"overview", "history", "diagnostics"}.value(id);
    saveSettings();
    if (id == 1)
      chart->update();
    if (id == 2)
      refreshDiagnostics();
  });
  const QString savedPageId = settings["page"].toString();
  int savedPage = QStringList{"overview", "history", "diagnostics"}.indexOf(savedPageId);
  if (savedPage < 0)
    savedPage = QStringList{QString::fromUtf8("Показания"), QString::fromUtf8("История"),
                            QString::fromUtf8("Диагностика")}
                    .indexOf(savedPageId);
  group->button(qMax(0, savedPage))->click();
  connect(scanButton, &QPushButton::clicked, ble, &TelemetryTransport::scan);
  connect(new QShortcut(QKeySequence("F5"), this), &QShortcut::activated, scanButton,
          &QPushButton::click);
  auto *fullScreen = new QShortcut(QKeySequence(Qt::Key_F11), this);
  fullScreen->setAutoRepeat(false);
  connect(fullScreen, &QShortcut::activated, this, [this, wasMaximized = false]() mutable {
    if (isFullScreen()) {
      if (wasMaximized)
        showMaximized();
      else
        showNormal();
    } else {
      wasMaximized = isMaximized();
      showFullScreen();
    }
  });
  connect(stationsOnly, &QCheckBox::toggled, this, [this] { rebuildDevices(); });
  connect(devices, &QComboBox::currentIndexChanged, this, [this] {
    updateControls();
    if (!ble->active()) {
      if (!ble->usb())
        selectedAddress = devices->currentData().toString();
      emit historyLoad(historyKey(), QDateTime::currentSecsSinceEpoch());
    }
  });
  connect(connectButton, &QPushButton::clicked, this, [this] {
    if (ble->active()) {
      ble->stop();
      return;
    }
    auto address = devices->currentData().toString();
    if (address.isEmpty())
      return;
    if (ble->usb())
      settings["usb_serial"] = address;
    else {
      selectedAddress = address;
      settings["address"] = address;
      settings["name"] = discovered[address]["name"];
      settings["profile"] = profile->currentIndex();
    }
    saveSettings();
    ble->resume(address, discovered[address]["name"].toString(), profile->currentIndex());
  });
  connect(ble, &TelemetryTransport::stateChanged, this, [this] {
    if (ble->usb()) {
      const auto info = ble->diagnostics();
      const auto id = ble->endpoint();
      if (id != usbEndpoint) {
        usbEndpoint = id;
        usbPoints.clear();
        refreshHistory();
        if (!id.isEmpty())
          emit historyLoad("usb-board:" + id, QDateTime::currentSecsSinceEpoch());
      }
      const auto board = info["usb_board"].toString();
      if (!id.isEmpty() && !board.isEmpty()) {
        const auto label = QString("%1 · %2 · %3")
                               .arg(board == "waveshare-147" ? "Waveshare 1.47″" : "Pocket 0.96″",
                                    info["usb_port"].toString(), info["name"].toString());
        if (discovered[id]["name"] != label) {
          discovered[id]["name"] = label;
          rebuildDevices();
        }
      }
    }
    if (ble->usb() && !ble->address().isEmpty() && selectedAddress != ble->address()) {
      selectedAddress = ble->address();
      localPoints.clear();
      points.clear();
      refreshHistory();
      emit historyLoad(selectedAddress, QDateTime::currentSecsSinceEpoch());
    }
    if (ble->remote() && !ble->address().isEmpty()) {
      const auto info = ble->diagnostics();
      if (selectedAddress != ble->address()) {
        localPoints.clear();
        servicePoints.clear();
        points.clear();
        selectedAddress = ble->address();
        emit historyLoad(selectedAddress, QDateTime::currentSecsSinceEpoch());
      }
      auto known = discovered.value(selectedAddress);
      known["name"] = info["name"];
      known["model"] = int(ap_model_hint(info["name"].toString().toUtf8().constData()));
      discovered[selectedAddress] = known;
      const QSignalBlocker blocker(profile);
      profile->setCurrentIndex(qBound(0, info["profile"].toInt(), 3));
      rebuildDevices();
    }
    bool scanning = ble->scanning();
    if (scanning && !wasScanning) {
      auto current = discovered.value(ble->address());
      discovered.clear();
      if (ble->active() && !current.isEmpty())
        discovered[ble->address()] = current;
      rebuildDevices();
    }
    wasScanning = scanning;
    updateControls();
    refreshDiagnostics();
    const auto info = ble->diagnostics();
    appendLog(ble->phase() +
              (info["error"].toString().isEmpty() ? QString() : " · " + info["error"].toString()));
  });
  connect(ble, &TelemetryTransport::found, this,
          [this](QString address, QString name, int rssi, int model) {
            discovered[address] = {{"name", name}, {"rssi", rssi}, {"model", model}};
            rebuildDevices();
          });
  connect(ble, &TelemetryTransport::packet, this, [this](QString hex, bool valid) {
    lastPacket = (valid ? "RX " : "INVALID ") + hex;
    appendLog(lastPacket);
  });
  connect(ble, &TelemetryTransport::snapshot, this, [this](ap_snapshot s) {
    if (!offline)
      updateSnapshot(s);
  });
  history = new HistoryWorker;
  history->moveToThread(&historyThread);
  connect(&historyThread, &QThread::finished, history, &QObject::deleteLater);
  connect(this, &MonitorWindow::openHistory, history, &HistoryWorker::open);
  connect(this, &MonitorWindow::historySession, history, &HistoryWorker::newSession);
  connect(this, &MonitorWindow::historySample, history, &HistoryWorker::sample);
  connect(history, &HistoryWorker::sampleFinished, this, [this] { historyBusy=false; });
  connect(this, &MonitorWindow::historyLoad, history, &HistoryWorker::load);
  connect(this, &MonitorWindow::historyExport, history, &HistoryWorker::exportFile);
  connect(this, &MonitorWindow::historyExportPoints, history, &HistoryWorker::exportPoints);
  connect(this, &MonitorWindow::historyImport, history, &HistoryWorker::importPoints);
  connect(this, &MonitorWindow::historyLog, history, &HistoryWorker::log);
  connect(this, &MonitorWindow::settingsSave, history, &HistoryWorker::saveSettings);
  connect(ble, &TelemetryTransport::newSession, this, &MonitorWindow::historySession);
  connect(history, &HistoryWorker::loaded, this,
          [this](QString address, HistoryPoints values, QJsonObject stats) {
            const bool boardArchive = ble->usb() && !ble->endpoint().isEmpty() &&
                                      address == "usb-board:" + ble->endpoint();
            if (!boardArchive && address != selectedAddress)
              return;
            historyStats = stats;
            if (offline)
              return;
            if (boardArchive)
              usbPoints = values;
            else
              localPoints = values;
            refreshHistory();
          });
  connect(ble, &TelemetryTransport::historyReady, this, [this](HistoryPoints values) {
    servicePoints = values;
    refreshHistory();
  });
  connect(ble, &TelemetryTransport::logReceived, this,
          [this](QString message) { appendLog(message); });
  connect(ble, &TelemetryTransport::historyImported, this, [this](HistoryPoints values) {
    emit historyImport(values, QDateTime::currentSecsSinceEpoch());
  });
  connect(history, &HistoryWorker::problem, this, [this](QString message) {
    historyError = message;
    appendLog(QCoreApplication::translate("Allpowers", "History error · ") + message);
    refreshDiagnostics();
  });
  connect(history, &HistoryWorker::exported, this, [this](QString path, bool ok) {
    status->setText(ok ? QCoreApplication::translate("Allpowers", "History saved: ") + path
                       : QCoreApplication::translate("Allpowers", "Could not save CSV"));
  });
  historyThread.start();
  historyStarted = true;
  saveSettings();
  emit openHistory(QDir(dataPath).filePath("history.sqlite3"));
  emit historyLoad(selectedAddress, QDateTime::currentSecsSinceEpoch());
  updateControls();
  appendLog(QString("ALLPOWERS Monitor %1 · %2")
                .arg(APP_VERSION,
                     offline ? QCoreApplication::translate("Allpowers", "demo without Bluetooth")
                             : QCoreApplication::translate("Allpowers", "ready to connect")));
  if (!selectedAddress.isEmpty()) {
    const QString name = settings["name"].toString();
    discovered[selectedAddress] = {{"name", name.isEmpty() ? selectedAddress : name},
                                   {"model", int(ap_model_hint(name.toUtf8().constData()))}};
    rebuildDevices();
    const auto address = selectedAddress;
    const int model = profile->currentIndex();
    QTimer::singleShot(0, this, [this, address, name, model] {
      if (!offline && !ble->remote() && isVisible() && !ble->active())
        ble->resume(address, name, model);
    });
  }
  if (ble->usb()) {
    const auto id = settings["usb_serial"].toString();
    if (!id.isEmpty())
      discovered[id] = {
          {"name", QCoreApplication::translate("Allpowers", "Saved USB board · %1").arg(id)}};
    rebuildDevices();
    QTimer::singleShot(0, this, [this, id] {
      if (!offline) {
        ble->scan();
        if (!id.isEmpty())
          ble->resume(id, {}, 0);
      }
    });
  }
}
QString MonitorWindow::historyKey() const {
  return ble->usb() && historySource->currentIndex() == 1 ? "usb-board:" + ble->endpoint()
                                                          : selectedAddress;
}
void MonitorWindow::refreshHistory() {
  points = ble->remote() ? mergeHistory(localPoints, servicePoints) : localPoints;
  if (ble->usb()) {
    const auto boot = ble->diagnostics()["history_session"].toInteger();
    points = historySource->currentIndex() == 1
                 ? usbPoints
                 : mergeUsbHistory(points, usbPoints,
                                   ble->connected() ? QString("usb-%1").arg(boot) : QString{});
  }
  chart->setPoints(points, QDateTime::currentSecsSinceEpoch());
  exportButton->setEnabled(!points.isEmpty());
  usbHistoryNote->setVisible(ble->usb());
  usbHistoryNote->setText(
      QCoreApplication::translate("Allpowers", "USB archive: %1 points · station unspecified")
          .arg(usbPoints.size()));
}
void MonitorWindow::changeTransport() {
  auto *router = qobject_cast<TransportRouter *>(ble);
  if (!router)
    return;
  const auto mode = transportMode->currentData().toString();
  discovered.clear();
  selectedAddress.clear();
  lastPacket.clear();
  localPoints.clear();
  servicePoints.clear();
  usbPoints.clear();
  usbEndpoint.clear();
  points.clear();
  lastSample = -1;
  historySource->setCurrentIndex(0);
  settings["transport"] = mode;
  router->setMode(mode);
  if (!router->usb())
    selectedAddress = settings["address"].toString();
  chart->setPoints({}, QDateTime::currentSecsSinceEpoch());
  ap_snapshot empty{};
  empty.abi_version = 1;
  updateSnapshot(empty);
  emit historySession();
  emit historyLoad(historyKey(), QDateTime::currentSecsSinceEpoch());
  const auto endpoint = router->usb() ? settings["usb_serial"].toString() : selectedAddress;
  const auto name =
      router->usb() ? QCoreApplication::translate("Allpowers", "Saved USB board · %1").arg(endpoint)
                    : settings["name"].toString();
  if (!endpoint.isEmpty())
    discovered[endpoint] = {{"name", name},
                            {"model", int(ap_model_hint(name.toUtf8().constData()))}};
  rebuildDevices();
  saveSettings();
  updateControls();
  if (!offline) {
    if (router->usb())
      router->scan();
    if (!endpoint.isEmpty() && !router->remote())
      router->resume(endpoint, name, profile->currentIndex());
  }
}
MonitorWindow::~MonitorWindow() {
  // QObject deletes the central widget before later child objects. Stop signals
  // here, while controls still exist, rather than from the transport destructor.
  ble->disconnect(this);
  if (!ble->remote())
    ble->stop();
  delete ble;
  ble = nullptr;
  // Finish queued history/settings writes before terminating their event loop.
  QMetaObject::invokeMethod(history, [] {}, Qt::BlockingQueuedConnection);
  historyThread.quit();
  historyThread.wait();
}
QWidget *MonitorWindow::overviewPage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto *cards = new QHBoxLayout;
  cards->setSpacing(12);
  auto card = [&](QHBoxLayout *row, const QString &title, QLabel *&value) {
    auto *frame = new QFrame;
    frame->setObjectName("card");
    frame->setMinimumHeight(112);
    auto *l = new QVBoxLayout(frame);
    l->setContentsMargins(22, 20, 22, 20);
    l->addWidget(text(title, 14, "#b0c0db", false, true), 1);
    value = text("—", 44, "#f1f6ff", true, true);
    l->addWidget(value, 3);
    row->addWidget(frame, 1);
  };
  card(cards, QCoreApplication::translate("Allpowers", "Charge"), soc);
  card(cards, QCoreApplication::translate("Allpowers", "Input / charging"), input);
  card(cards, QCoreApplication::translate("Allpowers", "Output"), output);
  soc->setObjectName("chargeValue");
  layout->addLayout(cards, 3);
  auto *details = new QHBoxLayout;
  details->setSpacing(12);
  card(details, QCoreApplication::translate("Allpowers", "Power source"), source);
  card(details, QCoreApplication::translate("Allpowers", "Time remaining"), runtime);
  source->setObjectName("powerSource");
  runtime->setObjectName("remainingTime");
  layout->addLayout(details, 3);
  auto *outputs = new QHBoxLayout;
  for (auto pair : {qMakePair(QString("AC"), &outputAC), qMakePair(QString("USB"), &outputUSB),
                    qMakePair(QString("DC"), &outputDC)}) {
    auto *f = new QFrame;
    f->setObjectName("card");
    f->setMinimumHeight(44);
    auto *l = new QVBoxLayout(f);
    *pair.second = text(pair.first + "  —", 19, "#91a6c3", true, true);
    (*pair.second)->setAlignment(Qt::AlignCenter);
    l->addWidget(*pair.second);
    outputs->addWidget(f, 1);
  }
  layout->addLayout(outputs, 1);
  freshness = text(QCoreApplication::translate("Allpowers", "Waiting for data"), 13, "#8ca6cb");
  layout->addWidget(freshness);
  return page;
}
QWidget *MonitorWindow::historyPage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *toolbar = new QHBoxLayout;
  horizon = new Select;
  horizon->setObjectName("horizon");
  horizon->addItem(QCoreApplication::translate("Allpowers", "Last hour"), 3600);
  horizon->addItem(QCoreApplication::translate("Allpowers", "24 hours"), 86400);
  horizon->addItem(QCoreApplication::translate("Allpowers", "48 hours"), 172800);
  int saved = settings["horizon"].toInt(86400);
  int index = horizon->findData(saved);
  horizon->setCurrentIndex(qMax(0, index));
  toolbar->addWidget(horizon);
  logScale = new Check(QCoreApplication::translate("Allpowers", "Log time"));
  logScale->setToolTip(QCoreApplication::translate(
      "Allpowers", "More space for recent seconds; use the wheel to zoom"));
  logScale->setChecked(settings["log_time"].toBool(false));
  toolbar->addWidget(logScale);
  toolbar->addStretch();
  liveButton = new QPushButton(QCoreApplication::translate("Allpowers", "Back to live"));
  toolbar->addWidget(liveButton);
  exportButton = new QPushButton(QCoreApplication::translate("Allpowers", "Export CSV"));
  exportButton->setEnabled(false);
  exportButton->setToolTip(QCoreApplication::translate(
      "Allpowers", "History for the selected station is required to export"));
  toolbar->addWidget(exportButton);
  layout->addLayout(toolbar);
  usbHistoryControls = new QWidget;
  auto *usbTools = new QHBoxLayout(usbHistoryControls);
  usbTools->setContentsMargins(0, 0, 0, 0);
  historySource = new Select;
  historySource->setObjectName("historySource");
  historySource->addItems({QCoreApplication::translate("Allpowers", "Station history + USB"),
                           QCoreApplication::translate("Allpowers", "USB board archive only")});
  usbTools->addWidget(historySource, 1);
  auto *readBoard = new QPushButton(QCoreApplication::translate("Allpowers", "Read from board"));
  readBoard->setObjectName("usbHistoryRead");
  usbTools->addWidget(readBoard);
  layout->addWidget(usbHistoryControls);
  usbHistoryControls->hide();
  historySource->setToolTip(QCoreApplication::translate(
      "Allpowers",
      "The board archive automatically supplements the main chart. Application records take "
      "priority where they overlap. The archive does not store the station address."));
  usbHistoryNote = text("", 12, "#819cc1");
  usbHistoryNote->setObjectName("usbHistoryNote");
  usbHistoryNote->setWordWrap(true);
  usbHistoryNote->hide();
  layout->addWidget(usbHistoryNote);
  connect(historySource, &QComboBox::currentIndexChanged, this, [this] {
    refreshHistory();
    emit historyLoad(historyKey(), QDateTime::currentSecsSinceEpoch());
  });
  connect(readBoard, &QPushButton::clicked, this, [this] { ble->requestHistory(); });
  auto *legend = new QHBoxLayout;
  int series = 0;
  for (const QString &name : {QCoreApplication::translate("Allpowers", "Charge, %"),
                              QCoreApplication::translate("Allpowers", "Input, W"),
                              QCoreApplication::translate("Allpowers", "Output, W")}) {
    auto *check = new Check(name);
    check->setChecked(true);
    legend->addWidget(check);
    int id = series++;
    check->setStyleSheet("QCheckBox{color:" + QStringList{"#54e0aa", "#65baff", "#eff5ff"}.at(id) +
                         ";}");
    connect(check, &QCheckBox::toggled, this,
            [this, id](bool enabled) { chart->setSeries(id, enabled); });
  }
  legend->addStretch();
  legend->addWidget(text(QCoreApplication::translate("Allpowers", "Battery"), 12, "#ff6f7c"));
  legend->addWidget(text(QCoreApplication::translate("Allpowers", "Mains"), 12, "#54e0aa"));
  layout->addLayout(legend);
  chart = new HistoryChart;
  layout->addWidget(chart, 1);
  chart->setHorizon(saved);
  chart->setLogarithmic(logScale->isChecked());
  layout->addWidget(text(
      QCoreApplication::translate("Allpowers", "Wheel — zoom · Drag — time · Hover — exact values"),
      12, "#819cc1"));
  connect(horizon, &QComboBox::currentIndexChanged, this, [this] {
    settings["horizon"] = horizon->currentData().toInt();
    saveSettings();
    chart->setHorizon(horizon->currentData().toInt());
  });
  connect(logScale, &QCheckBox::toggled, this, [this](bool enabled) {
    chart->setLogarithmic(enabled);
    settings["log_time"] = enabled;
    saveSettings();
  });
  connect(liveButton, &QPushButton::clicked, chart, &HistoryChart::followLive);
  connect(exportButton, &QPushButton::clicked, this, [this] {
    auto path = QFileDialog::getSaveFileName(
        this, QCoreApplication::translate("Allpowers", "Export history"),
        QDir::homePath() + "/allpowers-history.csv", "CSV (*.csv)");
    if (!path.isEmpty()) {
      emit historyExportPoints(points, path);
    }
  });
  return page;
}
QWidget *MonitorWindow::diagnosticPage() {
  auto *page = new QWidget;
  auto *l = new QVBoxLayout(page);
  l->setContentsMargins(0, 0, 0, 0);
  auto *row = new QHBoxLayout;
  row->addWidget(
      text(QCoreApplication::translate("Allpowers", "Diagnostics"), 16, "#d6e5f8", true));
  row->addStretch();
  auto *integration = new QPushButton(QCoreApplication::translate("Allpowers", "Windows UPS…"));
  integration->setObjectName("integrationManagement");
  integration->setToolTip(QCoreApplication::translate(
      "Allpowers", "Experimental Windows service and driver integration"));
  connect(integration, &QPushButton::clicked, this, &MonitorWindow::showIntegration);
  row->addWidget(integration);
  auto *follow = new QPushButton(QCoreApplication::translate("Allpowers", "Latest entries ↓"));
  follow->setObjectName("logFollow");
  row->addWidget(follow);
  auto *copy = new QPushButton(QCoreApplication::translate("Allpowers", "Copy"));
  copy->setObjectName("diagnosticCopy");
  row->addWidget(copy);
  l->addLayout(row);
  auto *tabs = new QTabWidget;
  tabs->setObjectName("diagnosticTabs");
  eventLog = new LiveLog;
  eventLog->setObjectName("eventLog");
  tabs->addTab(eventLog, QCoreApplication::translate("Allpowers", "Event log"));
  technical = new QTableWidget(0, 2);
  technical->setObjectName("diagnosticState");
  technical->setHorizontalHeaderLabels({QCoreApplication::translate("Allpowers", "Parameter"),
                                        QCoreApplication::translate("Allpowers", "Value")});
  technical->verticalHeader()->hide();
  technical->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  technical->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
  technical->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  technical->setColumnWidth(0, 290);
  technical->setEditTriggers(QAbstractItemView::NoEditTriggers);
  technical->setSelectionBehavior(QAbstractItemView::SelectRows);
  technical->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  technical->setAlternatingRowColors(true);
  technical->setShowGrid(false);
  technical->setWordWrap(true);
  connect(technical->horizontalHeader(), &QHeaderView::sectionResized, technical,
          &QTableWidget::resizeRowsToContents);
  const QPair<QString, QString> fields[] = {
      {"", QCoreApplication::translate("Allpowers", "Connection")},
      {"phase", QCoreApplication::translate("Allpowers", "Status")},
      {"name", QCoreApplication::translate("Allpowers", "Station name")},
      {"address", QCoreApplication::translate("Allpowers", "Bluetooth address")},
      {"active", QCoreApplication::translate("Allpowers", "Connection requested")},
      {"connected", QCoreApplication::translate("Allpowers", "Telemetry connected")},
      {"scanning", QCoreApplication::translate("Allpowers", "Scanning for devices")},
      {"error", QCoreApplication::translate("Allpowers", "Connection error")},
      {"connections", QCoreApplication::translate("Allpowers", "Successful connections")},
      {"drops", QCoreApplication::translate("Allpowers", "Connection drops")},
      {"polls", QCoreApplication::translate("Allpowers", "Requests sent")},
      {"packets", QCoreApplication::translate("Allpowers", "Packets received")},
      {"mtu", QCoreApplication::translate("Allpowers", "BLE MTU size")},
      {"transport", QCoreApplication::translate("Allpowers", "Transport")},
      {"service", QCoreApplication::translate("Allpowers", "Service UUID")},
      {"notify", QCoreApplication::translate("Allpowers", "Notification UUID")},
      {"write", QCoreApplication::translate("Allpowers", "Write UUID")},
      {"service", QCoreApplication::translate("Allpowers", "Windows service")},
      {"ups_present", QCoreApplication::translate("Allpowers", "System UPS available")},
      {"ups_reason", QCoreApplication::translate("Allpowers", "Why UPS is unavailable")},
      {"driver_error", QCoreApplication::translate("Allpowers", "Driver error")},
      {"", QCoreApplication::translate("Allpowers", "USB board")},
      {"usb_port", QCoreApplication::translate("Allpowers", "USB port")},
      {"usb_serial", QCoreApplication::translate("Allpowers", "Board serial number")},
      {"usb_board", QCoreApplication::translate("Allpowers", "Board model")},
      {"usb_verified", QCoreApplication::translate("Allpowers", "Firmware verified")},
      {"usb_bad_lines", QCoreApplication::translate("Allpowers", "Invalid USB lines")},
      {"usb_history_busy", QCoreApplication::translate("Allpowers", "Archive loading")},
      {"usb_history_imported", QCoreApplication::translate("Allpowers", "Archive points received")},
      {"usb_history_undated",
       QCoreApplication::translate("Allpowers", "Skipped without reliable date")},
      {"usb_history_bad", QCoreApplication::translate("Allpowers", "Corrupt archive records")},
      {"usb_history_error", QCoreApplication::translate("Allpowers", "Archive read error")},
      {"", QCoreApplication::translate("Allpowers", "Telemetry")},
      {"fresh", QCoreApplication::translate("Allpowers", "Data fresh")},
      {"model", QCoreApplication::translate("Allpowers", "Protocol profile")},
      {"valid_frames", QCoreApplication::translate("Allpowers", "Valid frames")},
      {"invalid_frames", QCoreApplication::translate("Allpowers", "Invalid frames")},
      {"main_age_ms", QCoreApplication::translate("Allpowers", "Reading age")},
      {"settings_age_ms", QCoreApplication::translate("Allpowers", "Settings age")},
      {"source_raw", QCoreApplication::translate("Allpowers", "Power source code")},
      {"known", QCoreApplication::translate("Allpowers", "Known fields mask")},
      {"last_packet", QCoreApplication::translate("Allpowers", "Last packet")},
      {"", QCoreApplication::translate("Allpowers", "History and chart")},
      {"history.live", QCoreApplication::translate("Allpowers", "Live history points")},
      {"history.minutes", QCoreApplication::translate("Allpowers", "Minute points")},
      {"history_worker_busy", QCoreApplication::translate("Allpowers", "Saving history")},
      {"history_error", QCoreApplication::translate("Allpowers", "History error")},
      {"history_path", QCoreApplication::translate("Allpowers", "History file")},
      {"chart_render_ms", QCoreApplication::translate("Allpowers", "Chart rendering time")},
      {"chart_vertices", QCoreApplication::translate("Allpowers", "Chart vertices")},
      {"", QCoreApplication::translate("Allpowers", "Application")},
      {"application", QCoreApplication::translate("Allpowers", "Version")},
      {"uptime_seconds", QCoreApplication::translate("Allpowers", "Uptime")},
      {"qt", "Qt"},
      {"language", QCoreApplication::translate("Allpowers", "Core language")},
      {"core_abi", QCoreApplication::translate("Allpowers", "Core ABI version")}};
  for (const auto &field : fields) {
    const int row = technical->rowCount();
    technical->insertRow(row);
    auto *label = new QTableWidgetItem(field.second);
    technical->setItem(row, 0, label);
    if (field.first.isEmpty()) {
      technical->setSpan(row, 0, 1, 2);
      label->setFlags(Qt::ItemIsEnabled);
      label->setBackground(QColor("#20324c"));
      label->setForeground(QColor("#8fc1ff"));
      auto font = label->font();
      font.setBold(true);
      label->setFont(font);
    } else {
      auto *value = new QTableWidgetItem("—");
      value->setData(Qt::UserRole, field.first);
      technical->setItem(row, 1, value);
    }
    technical->setRowHeight(row, 36);
  }
  tabs->addTab(technical, QCoreApplication::translate("Allpowers", "Current state"));
  l->addWidget(tabs, 1);
  auto *hint = text(
      QCoreApplication::translate("Allpowers", "New entries below · scroll up to pause following"),
      12, "#819cc1");
  l->addWidget(hint);
  follow->setEnabled(false);
  connect(follow, &QPushButton::clicked, eventLog, &LiveLog::followTail);
  connect(eventLog, &LiveLog::followingChanged, this, [follow, hint](bool following) {
    follow->setEnabled(!following);
    hint->setText(following
                      ? QCoreApplication::translate(
                            "Allpowers", "New entries below · scroll up to pause following")
                      : QCoreApplication::translate(
                            "Allpowers", "Viewing older entries · new entries keep arriving"));
  });
  connect(tabs, &QTabWidget::currentChanged, this, [this, follow, hint, copy](int index) {
    follow->setVisible(index == 0);
    hint->setVisible(index == 0);
    copy->setText(index == 0 ? QCoreApplication::translate("Allpowers", "Copy")
                             : QCoreApplication::translate("Allpowers", "Copy JSON"));
    copy->setMinimumWidth(index == 0 ? 135 : 190);
    refreshDiagnostics();
  });
  connect(copy, &QPushButton::clicked, this, [this, tabs] {
    QApplication::clipboard()->setText(
        tabs->currentIndex() == 0
            ? eventLog->toPlainText()
            : QString::fromUtf8(QJsonDocument(diagnostics()).toJson(QJsonDocument::Indented)));
  });
  return page;
}
void MonitorWindow::rebuildDevices() {
  if (devices->view()->isVisible()) {
    deviceListPending = true;
    return;
  }
  deviceListPending = false;
  QString previous = devices->currentData().toString();
  if (previous.isEmpty())
    previous = selectedAddress;
  const QSignalBlocker block(devices);
  devices->clear();
  auto addresses = discovered.keys();
  std::sort(addresses.begin(), addresses.end(), [this](const QString &a, const QString &b) {
    auto left = discovered[a], right = discovered[b];
    if (bool(left["model"].toInt()) != bool(right["model"].toInt()))
      return bool(left["model"].toInt());
    return left["rssi"].toInt() > right["rssi"].toInt();
  });
  for (const auto &address : addresses) {
    auto d = discovered[address];
    if (!ble->usb() && stationsOnly->isChecked() && !d["model"].toInt() &&
        address != selectedAddress)
      continue;
    const QString signal = d.contains("rssi")
                               ? QString("%1 dBm").arg(d["rssi"].toInt())
                               : QCoreApplication::translate("Allpowers", "saved station");
    devices->addItem(ble->usb()
                         ? d["name"].toString()
                         : QString("%1  ·  %2  ·  %3").arg(d["name"].toString(), address, signal),
                     address);
  }
  int selected = devices->findData(previous);
  devices->setCurrentIndex(selected >= 0 ? selected : devices->count() ? 0 : -1);
  updateControls();
}
void MonitorWindow::updateControls() {
  const bool usb = ble->usb();
  profile->setVisible(!usb);
  stationsOnly->setVisible(!usb);
  usbHistoryControls->setVisible(usb);
  transportNote->setText(
      usb ? QCoreApplication::translate("Allpowers", "Board receives BLE · computer reads USB")
      : ble->remote()
          ? QCoreApplication::translate("Allpowers", "Windows service handles Bluetooth")
          : QCoreApplication::translate("Allpowers", "Telemetry via computer Bluetooth"));
  footer->setText(
      usb ? QCoreApplication::translate("Allpowers", "USB read only · board manages BLE connection")
      : ble->remote()
          ? QCoreApplication::translate("Allpowers", "Read only · Windows service · Shared history")
          : QCoreApplication::translate("Allpowers",
                                        "Read only · Computer Bluetooth · Local history"));
  devices->setPlaceholderText(
      usb ? QCoreApplication::translate("Allpowers", "Find and select a USB board")
          : QCoreApplication::translate("Allpowers", "Find and select a station"));
  bool active = ble->active(), scanning = ble->scanning();
  scanButton->setEnabled(!active && !scanning && !offline && ble->available());
  scanButton->setToolTip(
      !ble->available()
          ? QCoreApplication::translate("Allpowers", "Start the service through Windows UPS")
      : active
          ? QCoreApplication::translate("Allpowers", "Disconnect the station before scanning again")
      : usb ? QCoreApplication::translate("Allpowers", "Find connected Waveshare and Pocket boards")
            : QCoreApplication::translate("Allpowers", "Find available Bluetooth stations"));
  scanButton->setText(scanning ? QCoreApplication::translate("Allpowers", "Scanning…")
                      : usb    ? QCoreApplication::translate("Allpowers", "Find boards")
                               : QCoreApplication::translate("Allpowers", "Find stations"));
  bool canConnect =
      ble->available() && (active || (!scanning && devices->currentIndex() >= 0 && !offline));
  connectButton->setEnabled(canConnect);
  connectButton->setText(active ? QCoreApplication::translate("Allpowers", "Disconnect")
                                : QCoreApplication::translate("Allpowers", "Connect"));
  connectButton->setToolTip(
      !ble->available()
          ? QCoreApplication::translate("Allpowers", "Start the service through Windows UPS")
      : offline  ? QCoreApplication::translate("Allpowers", "Demo: Bluetooth is not used")
      : active   ? usb ? QCoreApplication::translate("Allpowers",
                                                     "Close USB port; the board will keep running")
                       : QCoreApplication::translate("Allpowers", "Disconnect from the station")
      : scanning ? QCoreApplication::translate("Allpowers", "Wait for the scan to finish")
      : devices->count() == 0
          ? QCoreApplication::translate("Allpowers", "Find and select a station first")
          : QCoreApplication::translate("Allpowers", "Connect to selected station"));
  devices->setEnabled(!active && !scanning);
  profile->setEnabled(!active);
  stationsOnly->setEnabled(!active);
  devices->setToolTip(
      active ? QCoreApplication::translate("Allpowers", "Disconnect before switching stations")
             : QCoreApplication::translate("Allpowers", "Select an available station"));
  profile->setToolTip(
      active
          ? QCoreApplication::translate("Allpowers", "Disconnect before changing the profile")
          : QCoreApplication::translate("Allpowers", "Automatic detection is usually sufficient"));
  stationsOnly->setToolTip(
      active ? QCoreApplication::translate("Allpowers", "Disconnect before changing the filter")
             : QCoreApplication::translate("Allpowers", "Hide unknown devices from the list"));
  auto info = ble->diagnostics();
  QString phase = ble->phase();
  if (scanning)
    phase += QCoreApplication::translate("Allpowers", " · scanning for devices…");
  if (!info["error"].toString().isEmpty() && !active)
    phase += " · " + info["error"].toString();
  setText(status, phase);
  status->setStyleSheet(labelStyle(ble->connected() ? "#54e0aa" : "#9eb6d7", 14));
}
void MonitorWindow::updateSnapshot(ap_snapshot s) {
  last = s;
  auto value = [&](quint32 mask, quint32 n, const QString &unit) {
    return s.known & mask ? QString::number(n) + unit : QString("—");
  };
  setText(soc, value(1, s.soc, "%"));
  setText(input, value(2, s.input_w, QCoreApplication::translate("Allpowers", " W")));
  setText(output, value(4, s.output_w, QCoreApplication::translate("Allpowers", " W")));
  setText(source, s.source == 2   ? QCoreApplication::translate("Allpowers", "Mains")
                  : s.source == 1 ? QCoreApplication::translate("Allpowers", "Battery")
                                  : QCoreApplication::translate("Allpowers", "Unknown"));
  setText(runtime, value(8, s.runtime_s, "") == "—" ? "—" : formatDuration(s.runtime_s));
  auto state = [](QLabel *label, const QString &name, quint32 known, bool enabled) {
    setText(label, name + (!known    ? "  —"
                           : enabled ? QCoreApplication::translate("Allpowers", "  ON")
                                     : QCoreApplication::translate("Allpowers", "  OFF")));
    label->setStyleSheet(labelStyle(known && enabled ? "#54e0aa" : "#91a6c3", 19, true));
  };
  state(outputAC, "AC", s.known & AllpowersModels::AcOutput, s.ac_output);
  state(outputUSB, "USB", s.known & AllpowersModels::UsbOutput, s.usb_output);
  state(outputDC, "DC", s.known & AllpowersModels::DcOutput, s.dc_output);
  setText(freshness,
          s.fresh ? QCoreApplication::translate("Allpowers", "Data fresh · updated %1 s ago")
                        .arg(s.main_age_ms / 1000., 0, 'f', 1)
                  : QCoreApplication::translate("Allpowers",
                                                "No fresh telemetry · last value may be stale"));
  auto now = QDateTime::currentSecsSinceEpoch();
  if (!offline && !ble->remote() && ble->active() && s.main_seen && now != lastSample &&
      !historyBusy) {
    historyBusy = true;
    lastSample = now;
    emit historySample(selectedAddress, s, now, uptime.elapsed() / 1000);
  }
  if (pages->currentIndex() == 2)
    refreshDiagnostics();
}
QJsonObject MonitorWindow::diagnostics() const {
  auto info = ble->diagnostics();
  info["application"] = APP_VERSION;
  info["language"] = "C++23";
  info["ui_language"] = qApp->property("ui_language").toString();
  info["qt"] = qVersion();
  info["core_abi"] = int(ap_abi_version());
  info["valid_frames"] = int(last.valid_frames);
  info["invalid_frames"] = int(last.invalid_frames);
  info["fresh"] = bool(last.fresh);
  info["known"] = int(last.known);
  info["model"] = int(last.model);
  info["source_raw"] = int(last.source_raw);
  info["main_age_ms"] = double(last.main_age_ms);
  info["settings_age_ms"] = double(last.settings_age_ms);
  if (!ble->remote()) {
    info["history"] = historyStats;
    info["history_error"] = historyError;
    info["history_path"] = QDir(dataPath).filePath("history.sqlite3");
  } else
    info["legacy_history_path"] = QDir(dataPath).filePath("history.sqlite3");
  info["last_packet"] = lastPacket;
  info["chart_render_ms"] = chart->renderMs();
  info["chart_vertices"] = chart->vertices();
  info["chart_points"] = points.size();
  info["usb_archive_points"] = usbPoints.size();
  info["history_worker_busy"] = historyBusy;
  info["uptime_seconds"] = uptime.elapsed() / 1000.;
  return info;
}
void MonitorWindow::refreshDiagnostics() {
  const auto info = diagnostics();
  const auto usbDetails = info["usb_diagnostics"].toObject();
  for (auto it = usbDetails.begin(); it != usbDetails.end(); ++it) {
    const QString key = "usb_detail:" + it.key();
    bool found = false;
    for (int i = 0; i < technical->rowCount(); ++i) {
      auto *v = technical->item(i, 1);
      if (v && v->data(Qt::UserRole) == key) {
        found = true;
        break;
      }
    }
    if (!found) {
      int row = technical->rowCount();
      technical->insertRow(row);
      technical->setItem(
          row, 0,
          new QTableWidgetItem(QCoreApplication::translate("Allpowers", "Board · ") + it.key()));
      auto *v = new QTableWidgetItem;
      v->setData(Qt::UserRole, key);
      technical->setItem(row, 1, v);
    }
  }
  for (int row = 0; row < technical->rowCount(); ++row) {
    auto *item = technical->item(row, 1);
    if (!item)
      continue;
    const auto key = item->data(Qt::UserRole).toString();
    QJsonValue value(info);
    if (key.startsWith("usb_detail:"))
      value = usbDetails[key.mid(11)];
    else
      for (const auto &part : key.split('.'))
        value = value.toObject().value(part);
    QString display = "—";
    if (value.isBool())
      display = value.toBool() ? QCoreApplication::translate("Allpowers", "Yes")
                               : QCoreApplication::translate("Allpowers", "No");
    else if (value.isString() && !value.toString().isEmpty())
      display = value.toString();
    else if (value.isDouble())
      display = QString::number(value.toDouble(), 'f', 0);
    if (key == "main_age_ms" || key == "settings_age_ms") {
      const bool seen = key == "main_age_ms" ? last.main_seen : last.settings_seen;
      display = seen ? QString::number(value.toDouble() / 1000., 'f', 1) +
                           QCoreApplication::translate("Allpowers", " s")
                     : "—";
    } else if (key == "chart_render_ms")
      display = QString::number(value.toDouble(), 'f', 2) +
                QCoreApplication::translate("Allpowers", " ms");
    else if (key == "uptime_seconds") {
      const auto seconds = qint64(value.toDouble());
      display = QString("%1:%2:%3")
                    .arg(seconds / 3600, 2, 10, QChar('0'))
                    .arg(seconds / 60 % 60, 2, 10, QChar('0'))
                    .arg(seconds % 60, 2, 10, QChar('0'));
    } else if (key == "mtu" && value.toInt() == 0)
      display = "—";
    else if (key == "model")
      display = QStringList{QCoreApplication::translate("Allpowers", "Automatic"), "Old10",
                            "Legacy16", "R2500 V2"}
                    .value(value.toInt(), QString::number(value.toInt()));
    else if (key == "known" || key == "source_raw")
      display = "0x" + QString::number(value.toInteger(), 16).toUpper();
    if (key == "source_raw" && !(last.known & AllpowersModels::SourceRaw))
      display = "—";
    if (item->text() != display) {
      item->setText(display);
      item->setToolTip(display);
      technical->resizeRowToContents(row);
    }
  }
}
void MonitorWindow::appendLog(const QString &message) {
  eventLog->appendEntry(QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + "  " + message);
  emit historyLog(message);
}
void MonitorWindow::saveSettings() {
  if (historyStarted)
    emit settingsSave(QDir(dataPath).filePath("settings.json"), settings);
}
void MonitorWindow::closeEvent(QCloseEvent *event) {
  if (!ble->remote())
    ble->stop();
  saveSettings();
  event->accept();
}
void MonitorWindow::showIntegration() {
  auto *dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(QCoreApplication::translate("Allpowers", "Windows UPS"));
  dialog->resize(600, 300);
  dialog->setStyleSheet("QDialog{background:#0e1727;color:#ecf3ff;}QLabel{color:#bdcce2;font-"
                        "family:'Segoe UI';font-size:14px;}");
  auto *layout = new QVBoxLayout(dialog);
  auto *description = new QLabel(
      ble->usb()
          ? QCoreApplication::translate(
                "Allpowers", "The USB board can already appear as a UPS in Windows. No additional "
                             "driver is needed to read its telemetry in the monitor.")
          : QCoreApplication::translate(
                "Allpowers",
                "Optional integration: a service receives BLE data and a driver reports it to "
                "Windows power management. The monitor also works independently."));
  description->setWordWrap(true);
  layout->addWidget(description);
  const auto info = integrationStatus();
  auto *status =
      new QLabel(info["installed"].toBool()
                     ? QCoreApplication::translate("Allpowers",
                                                   "Service installed. The monitor uses its data.")
                     : QCoreApplication::translate(
                           "Allpowers", "Service not installed. The monitor uses direct BLE."));
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *notice = new QLabel(QCoreApplication::translate(
      "Allpowers",
      "Experimental version: install and removal are available only in a test VM. A signed driver "
      "package is required. This program does not change Secure Boot or boot settings."));
  notice->setWordWrap(true);
  layout->addWidget(notice);
  auto *row = new QHBoxLayout;
  layout->addLayout(row);
  for (auto action :
       {qMakePair(QCoreApplication::translate("Allpowers", "Install / update"), QString("install")),
        qMakePair(QCoreApplication::translate("Allpowers", "Start"), QString("start")),
        qMakePair(QCoreApplication::translate("Allpowers", "Stop"), QString("stop")),
        qMakePair(QCoreApplication::translate("Allpowers", "Remove"), QString("remove"))}) {
    auto *button = new QPushButton(action.first);
    row->addWidget(button);
    button->setEnabled(!offline && info["virtual_machine"].toBool());
    button->setToolTip(QCoreApplication::translate(
        "Allpowers", "Administrator rights required; history and settings are preserved"));
    connect(button, &QPushButton::clicked, dialog, [dialog, status, operation = action.second] {
      for (auto *b : dialog->findChildren<QPushButton *>())
        b->setEnabled(false);
      status->setText(QCoreApplication::translate("Allpowers", "Windows operation in progress…"));
      auto result = std::make_shared<bool>(false);
      auto *thread =
          QThread::create([result, operation] { *result = elevateIntegrationAction(operation); });
      // The dialog context disconnects this callback if the window is closed
      // while UAC or installation is running. The worker never touches widgets.
      connect(thread, &QThread::finished, dialog, [result, status, dialog] {
        status->setText(
            *result ? QCoreApplication::translate(
                          "Allpowers", "Done. Restart the monitor to apply the operating mode.")
                    : QCoreApplication::translate(
                          "Allpowers", "Operation failed. Check the package signature, guest "
                                       "Windows test mode, and administrator rights."));
        for (auto *b : dialog->findChildren<QPushButton *>())
          b->setEnabled(true);
      });
      connect(thread, &QThread::finished, thread, &QObject::deleteLater);
      thread->start();
    });
  }
  dialog->show();
}
void MonitorWindow::loadDemo() {
  offline = true;
  auto now = QDateTime::currentSecsSinceEpoch();
  HistoryPoints demo;
  for (int i = 0; i < 3600; ++i) {
    HistoryPoint p;
    p.address = "demo";
    p.session = "demo";
    p.wall = now - 3599 + i;
    p.tick = i;
    p.data.fresh = 1;
    p.data.known = 383;
    p.data.soc = quint32(85 - i / 150);
    p.data.input_w = i < 1800 ? 0 : 120;
    p.data.output_w = quint32(150 + 40 * std::sin(i / 50.));
    p.data.source = i < 1800 ? 1 : 2;
    p.data.ac_output = 1;
    demo.append(p);
  }
  points = demo;
  auto s = demo.last().data;
  s.main_seen = 1;
  s.runtime_s = 22440;
  s.main_age_ms = 100;
  updateSnapshot(s);
  chart->setPoints(demo, now);
  horizon->setCurrentIndex(horizon->findData(3600));
  chart->setHorizon(3600);
  if (auto *tabs = findChild<QButtonGroup *>())
    tabs->button(1)->click();
  status->setText(
      QCoreApplication::translate("Allpowers", "Interface demo · Bluetooth is not used"));
  refreshDiagnostics();
}
