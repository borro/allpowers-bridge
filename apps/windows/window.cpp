#include "window.h"
#include "AllpowersModels.h"
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

static QString labelStyle(const QString &color, int size, bool bold = false) {
  return QString("color:%1;font-size:%2px;font-weight:%3;")
      .arg(color)
      .arg(size)
      .arg(bold ? 700 : 400);
}
static QLabel *text(const QString &s, int size = 14, const QString &color = "#b0c0db",
                    bool bold = false) {
  auto *l = new QLabel(s);
  l->setStyleSheet(labelStyle(color, size, bold));
  return l;
}
static void setText(QLabel *label, const QString &value) {
  if (label->text() != value)
    label->setText(value);
}
static QString duration(quint32 seconds) {
  auto minutes = seconds / 60, days = minutes / 1440, hours = minutes / 60 % 24;
  return days ? QString("%1 д %2 ч").arg(days).arg(hours)
              : QString("%1 ч %2 мин").arg(hours).arg(minutes % 60);
}

class Select : public QComboBox {
public:
  using QComboBox::QComboBox;

protected:
  void paintEvent(QPaintEvent *event) override {
    QComboBox::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(isEnabled() ? QColor("#b9cbe7") : QColor("#60718b"), 1.8, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    int x = width() - 23, y = height() / 2;
    QPolygonF line;
    line << QPointF(x - 4, y - 2) << QPointF(x, y + 2) << QPointF(x + 4, y - 2);
    p.drawPolyline(line);
  }
};
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
MonitorWindow::MonitorWindow(bool testMode, QWidget *parent)
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
    QComboBox{background:#19283e;border:1px solid #354b6c;border-radius:9px;padding:10px 38px 10px 12px;color:#eaf2ff;min-height:19px;}
    QComboBox:hover{border-color:#6488ba;}QComboBox:focus{border-color:#69b3ff;}QComboBox:disabled{color:#71829c;border-color:#24344c;}
    QComboBox::drop-down{border:0;width:34px;}QComboBox::down-arrow{image:none;width:0;height:0;}
    QComboBox QAbstractItemView{background:#1c2c44;border:1px solid #48617f;color:#edf5ff;selection-background-color:#345987;outline:0;padding:5px;}
    QComboBox QAbstractItemView::item{min-height:34px;padding:5px;}
    QCheckBox{color:#bdcce2;spacing:8px;}QCheckBox::indicator{width:17px;height:17px;border:1px solid #627a9d;border-radius:4px;background:#17263d;}
    QCheckBox::indicator:checked{background:#54a0ff;border-color:#89beff;}
    QPlainTextEdit{background:#172235;color:#b9cee9;border:1px solid #2b405c;border-radius:10px;padding:12px;font-family:Consolas;font-size:13px;}
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
  layout->addWidget(text("Телеметрия через Bluetooth компьютера", 13, "#8ca6cb"));
  auto *connection = new QHBoxLayout;
  devices = new Select;
  devices->setObjectName("devices");
  devices->setPlaceholderText("Найдите и выберите станцию");
  devices->setMinimumWidth(360);
  connection->addWidget(devices, 1);
  scanButton = new QPushButton("Найти станции");
  scanButton->setObjectName("scan");
  connection->addWidget(scanButton);
  connectButton = new QPushButton("Подключить");
  connectButton->setObjectName("primary");
  connection->addWidget(connectButton);
  layout->addLayout(connection);
  auto *options = new QHBoxLayout;
  stationsOnly = new Check("Только станции");
  stationsOnly->setChecked(true);
  options->addWidget(stationsOnly);
  options->addStretch();
  profile = new Select;
  profile->addItems(
      {"Авто по имени станции", "Старый протокол · Old10", "Протокол V2 · Legacy16", "R2500 V2"});
  profile->setCurrentIndex(qBound(0, settings["profile"].toInt(), 3));
  options->addWidget(profile);
  layout->addLayout(options);
  status = text("Отключено", 14, "#9eb6d7");
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *navigation = new QHBoxLayout;
  auto *group = new QButtonGroup(this);
  pages = new QStackedWidget;
  int pageIndex = 0;
  for (const QString &name : {QString("Показания"), QString("История"), QString("Диагностика")}) {
    auto *button = new QPushButton(name);
    button->setCheckable(true);
    group->addButton(button, pageIndex);
    navigation->addWidget(button);
    auto *shortcut = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(pageIndex + 1)), this);
    connect(shortcut, &QShortcut::activated, button, &QPushButton::click);
    ++pageIndex;
  }
  navigation->addStretch();
  layout->addLayout(navigation);
  pages->addWidget(overviewPage());
  pages->addWidget(historyPage());
  pages->addWidget(diagnosticPage());
  layout->addWidget(pages, 1);
  auto *footer =
      text("Только просмотр  ·  Прямое BLE  ·  История сохраняется на компьютере", 12, "#7d96b9");
  layout->addWidget(footer);
  ble = new BleTransport(this, offline);
  connect(group, &QButtonGroup::idClicked, this, [this, group](int id) {
    pages->setCurrentIndex(id);
    group->button(id)->setChecked(true);
    settings["page"] = QStringList{"Показания", "История", "Диагностика"}.value(id);
    saveSettings();
    if (id == 1)
      chart->update();
  });
  int savedPage =
      QStringList{"Показания", "История", "Диагностика"}.indexOf(settings["page"].toString());
  group->button(qMax(0, savedPage))->click();
  connect(scanButton, &QPushButton::clicked, ble, &BleTransport::scan);
  connect(new QShortcut(QKeySequence("F5"), this), &QShortcut::activated, scanButton,
          &QPushButton::click);
  connect(stationsOnly, &QCheckBox::toggled, this, [this] { rebuildDevices(); });
  connect(devices, &QComboBox::currentIndexChanged, this, [this] {
    updateControls();
    if (!ble->active()) {
      selectedAddress = devices->currentData().toString();
      emit historyLoad(selectedAddress, QDateTime::currentSecsSinceEpoch());
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
    selectedAddress = address;
    settings["address"] = address;
    settings["name"] = discovered[address]["name"];
    settings["profile"] = profile->currentIndex();
    saveSettings();
    ble->start(address, profile->currentIndex());
  });
  connect(ble, &BleTransport::stateChanged, this, [this] {
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
    emit historyLog(
        QString::fromUtf8(QJsonDocument(ble->diagnostics()).toJson(QJsonDocument::Compact)));
  });
  connect(ble, &BleTransport::found, this,
          [this](QString address, QString name, int rssi, int model) {
            discovered[address] = {{"name", name}, {"rssi", rssi}, {"model", model}};
            rebuildDevices();
          });
  connect(ble, &BleTransport::packet, this,
          [this](QString hex, bool valid) { lastPacket = (valid ? "RX " : "INVALID ") + hex; });
  connect(ble, &BleTransport::snapshot, this, [this](ap_snapshot s) {
    if (!offline)
      updateSnapshot(s);
  });
  history = new HistoryWorker;
  history->moveToThread(&historyThread);
  connect(&historyThread, &QThread::finished, history, &QObject::deleteLater);
  connect(this, &MonitorWindow::openHistory, history, &HistoryWorker::open);
  connect(this, &MonitorWindow::historySession, history, &HistoryWorker::newSession);
  connect(this, &MonitorWindow::historySample, history, &HistoryWorker::sample);
  connect(this, &MonitorWindow::historyLoad, history, &HistoryWorker::load);
  connect(this, &MonitorWindow::historyExport, history, &HistoryWorker::exportFile);
  connect(this, &MonitorWindow::historyLog, history, &HistoryWorker::log);
  connect(this, &MonitorWindow::settingsSave, history, &HistoryWorker::saveSettings);
  connect(ble, &BleTransport::newSession, this, &MonitorWindow::historySession);
  connect(history, &HistoryWorker::loaded, this, [this](HistoryPoints values, QJsonObject stats) {
    historyBusy = false;
    historyStats = stats;
    if (offline)
      return;
    points = values;
    chart->setPoints(points, QDateTime::currentSecsSinceEpoch());
    exportButton->setEnabled(!points.isEmpty());
  });
  connect(history, &HistoryWorker::problem, this, [this](QString message) {
    historyBusy = false;
    historyError = message;
    refreshDiagnostics();
  });
  connect(history, &HistoryWorker::exported, this, [this](QString path, bool ok) {
    status->setText(ok ? "История сохранена: " + path : "Не удалось сохранить CSV");
  });
  historyThread.start();
  historyStarted = true;
  saveSettings();
  emit openHistory(QDir(dataPath).filePath("history.sqlite3"));
  emit historyLoad(selectedAddress, QDateTime::currentSecsSinceEpoch());
  updateControls();
}
MonitorWindow::~MonitorWindow() {
  // QObject deletes the central widget before later child objects. Stop signals
  // here, while controls still exist, rather than from the transport destructor.
  ble->disconnect(this);
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
  auto card = [&](const QString &title, QLabel *&value) {
    auto *frame = new QFrame;
    frame->setObjectName("card");
    auto *l = new QVBoxLayout(frame);
    l->setContentsMargins(22, 20, 22, 20);
    l->addWidget(text(title, 14));
    value = text("—", 44, "#f1f6ff", true);
    l->addWidget(value);
    cards->addWidget(frame, 1);
  };
  card("Заряд", soc);
  card("Вход / заряд", input);
  card("Выход", output);
  layout->addLayout(cards);
  auto *details = new QFrame;
  details->setObjectName("card");
  auto *grid = new QGridLayout(details);
  grid->setContentsMargins(22, 20, 22, 20);
  grid->setVerticalSpacing(18);
  grid->addWidget(text("Источник питания"), 0, 0);
  source = text("—", 20, "#f1f6ff", true);
  grid->addWidget(source, 0, 1, Qt::AlignRight);
  grid->addWidget(text("Оставшееся время"), 1, 0);
  runtime = text("—", 20, "#f1f6ff", true);
  grid->addWidget(runtime, 1, 1, Qt::AlignRight);
  layout->addWidget(details);
  auto *outputs = new QHBoxLayout;
  for (auto pair : {qMakePair(QString("AC"), &outputAC), qMakePair(QString("USB"), &outputUSB),
                    qMakePair(QString("DC"), &outputDC)}) {
    auto *f = new QFrame;
    f->setObjectName("card");
    auto *l = new QVBoxLayout(f);
    *pair.second = text(pair.first + "  —", 19, "#91a6c3", true);
    (*pair.second)->setAlignment(Qt::AlignCenter);
    l->addWidget(*pair.second);
    outputs->addWidget(f, 1);
  }
  layout->addLayout(outputs);
  freshness = text("Ожидание данных", 13, "#8ca6cb");
  layout->addWidget(freshness);
  layout->addStretch();
  return page;
}
QWidget *MonitorWindow::historyPage() {
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *toolbar = new QHBoxLayout;
  horizon = new Select;
  horizon->addItem("Последний час", 3600);
  horizon->addItem("24 часа", 86400);
  horizon->addItem("48 часов", 172800);
  int saved = settings["horizon"].toInt(86400);
  int index = horizon->findData(saved);
  horizon->setCurrentIndex(qMax(0, index));
  toolbar->addWidget(horizon);
  logScale = new Check("Лог. время");
  logScale->setToolTip("Больше места недавним секундам; колёсико меняет масштаб");
  logScale->setChecked(settings["log_time"].toBool(false));
  toolbar->addWidget(logScale);
  toolbar->addStretch();
  liveButton = new QPushButton("К текущему моменту");
  toolbar->addWidget(liveButton);
  exportButton = new QPushButton("Экспорт CSV");
  exportButton->setEnabled(false);
  exportButton->setToolTip("Для экспорта нужна история выбранной станции");
  toolbar->addWidget(exportButton);
  layout->addLayout(toolbar);
  auto *legend = new QHBoxLayout;
  int series = 0;
  for (const QString &name : {QString("Заряд, %"), QString("Вход, Вт"), QString("Выход, Вт")}) {
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
  legend->addWidget(text("Батарея", 12, "#ff6f7c"));
  legend->addWidget(text("Сеть", 12, "#54e0aa"));
  layout->addLayout(legend);
  chart = new HistoryChart;
  layout->addWidget(chart, 1);
  chart->setHorizon(saved);
  chart->setLogarithmic(logScale->isChecked());
  layout->addWidget(text(
      "Колёсико — масштаб · Перетаскивание — время · Наведение — точные значения", 12, "#819cc1"));
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
        this, "Экспорт истории", QDir::homePath() + "/allpowers-history.csv", "CSV (*.csv)");
    if (!path.isEmpty())
      emit historyExport(selectedAddress, QDateTime::currentSecsSinceEpoch(), path);
  });
  return page;
}
QWidget *MonitorWindow::diagnosticPage() {
  auto *page = new QWidget;
  auto *l = new QVBoxLayout(page);
  l->setContentsMargins(0, 0, 0, 0);
  auto *row = new QHBoxLayout;
  row->addWidget(text("Состояние соединения и приложения", 16, "#d6e5f8", true));
  row->addStretch();
  auto *copy = new QPushButton("Скопировать");
  row->addWidget(copy);
  l->addLayout(row);
  technical = new QPlainTextEdit;
  technical->setReadOnly(true);
  l->addWidget(technical, 1);
  connect(copy, &QPushButton::clicked, this, [this] {
    QApplication::clipboard()->setText(
        QString::fromUtf8(QJsonDocument(diagnostics()).toJson(QJsonDocument::Indented)));
  });
  return page;
}
void MonitorWindow::rebuildDevices() {
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
    if (stationsOnly->isChecked() && !d["model"].toInt())
      continue;
    devices->addItem(
        QString("%1  ·  %2  ·  %3 dBm").arg(d["name"].toString(), address).arg(d["rssi"].toInt()),
        address);
  }
  int selected = devices->findData(previous);
  devices->setCurrentIndex(selected >= 0 ? selected : devices->count() ? 0 : -1);
  updateControls();
}
void MonitorWindow::updateControls() {
  bool active = ble->active(), scanning = ble->scanning();
  scanButton->setEnabled(!scanning && !offline);
  scanButton->setText(scanning ? "Поиск…" : "Найти станции");
  bool canConnect = active || (!scanning && devices->currentIndex() >= 0 && !offline);
  connectButton->setEnabled(canConnect);
  connectButton->setText(active ? "Отключить" : "Подключить");
  connectButton->setToolTip(offline ? "Демонстрация: Bluetooth не используется"
                            : active ? "Завершить соединение со станцией"
                            : scanning ? "Дождитесь завершения поиска"
                            : devices->count() == 0 ? "Сначала найдите и выберите станцию"
                                                    : "Подключиться к выбранной станции");
  devices->setEnabled(!active && !scanning);
  profile->setEnabled(!active);
  stationsOnly->setEnabled(!active);
  devices->setToolTip(active ? "Для смены станции сначала отключитесь"
                             : "Выберите доступную станцию");
  profile->setToolTip(active ? "Профиль меняется после отключения"
                             : "Обычно достаточно автоматического определения");
  stationsOnly->setToolTip(active ? "Фильтр меняется после отключения"
                                  : "Скрыть неизвестные устройства из списка");
  auto info = ble->diagnostics();
  QString phase = ble->phase();
  if (scanning)
    phase += " · поиск устройств…";
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
  setText(input, value(2, s.input_w, " Вт"));
  setText(output, value(4, s.output_w, " Вт"));
  setText(source, s.source == 2 ? "Розетка" : s.source == 1 ? "Аккумулятор" : "Не определён");
  setText(runtime, value(8, s.runtime_s, "") == "—" ? "—" : duration(s.runtime_s));
  auto state = [](QLabel *label, const QString &name, quint32 known, bool enabled) {
    setText(label, name + (!known ? "  —" : enabled ? "  ВКЛ" : "  ВЫКЛ"));
    label->setStyleSheet(labelStyle(known && enabled ? "#54e0aa" : "#91a6c3", 19, true));
  };
  state(outputAC, "AC", s.known & AllpowersModels::AcOutput, s.ac_output);
  state(outputUSB, "USB", s.known & AllpowersModels::UsbOutput, s.usb_output);
  state(outputDC, "DC", s.known & AllpowersModels::DcOutput, s.dc_output);
  setText(freshness, s.fresh
                         ? QString("Данные актуальны · обновление %1 с назад")
                               .arg(s.main_age_ms / 1000., 0, 'f', 1)
                         : "Нет актуальной телеметрии · последнее значение может быть устаревшим");
  auto now = QDateTime::currentSecsSinceEpoch();
  if (!offline && ble->active() && s.main_seen && now != lastSample && !historyBusy) {
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
  info["language"] = "C++17";
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
  info["history"] = historyStats;
  info["history_error"] = historyError;
  info["history_path"] = QDir(dataPath).filePath("history.sqlite3");
  info["last_packet"] = lastPacket;
  info["chart_render_ms"] = chart->renderMs();
  info["chart_vertices"] = chart->vertices();
  info["history_worker_busy"] = historyBusy;
  info["uptime_seconds"] = uptime.elapsed() / 1000.;
  return info;
}
void MonitorWindow::refreshDiagnostics() {
  if (technical)
    technical->setPlainText(
        QString::fromUtf8(QJsonDocument(diagnostics()).toJson(QJsonDocument::Indented)));
}
void MonitorWindow::saveSettings() {
  if (historyStarted)
    emit settingsSave(QDir(dataPath).filePath("settings.json"), settings);
}
void MonitorWindow::closeEvent(QCloseEvent *event) {
  ble->stop();
  saveSettings();
  event->accept();
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
  status->setText("Демонстрация интерфейса · Bluetooth не используется");
  refreshDiagnostics();
}
