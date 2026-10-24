#pragma once
#include "bluetooth.h"
#include "chart.h"
#include "controls.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>

QString monitorDataDirectory();
class MonitorWindow : public QMainWindow {
  Q_OBJECT
  BleTransport *ble;
  QThread historyThread;
  HistoryWorker *history;
  HistoryChart *chart;
  QComboBox *devices, *profile, *horizon;
  QCheckBox *stationsOnly, *logScale;
  QPushButton *scanButton, *connectButton, *exportButton, *liveButton;
  QLabel *status, *soc, *input, *output, *source, *runtime, *freshness, *outputAC, *outputUSB,
      *outputDC;
  QStackedWidget *pages;
  QTableWidget *technical;
  LiveLog *eventLog;
  QJsonObject settings, historyStats;
  QMap<QString, QJsonObject> discovered;
  QString selectedAddress, lastPacket, historyError;
  QString dataPath;
  ap_snapshot last{};
  HistoryPoints points;
  QElapsedTimer uptime;
  bool historyBusy = false, offline = false;
  bool wasScanning = false;
  bool historyStarted = false;
  bool deviceListPending = false;
  qint64 lastSample = -1;
  void rebuildDevices();
  void updateControls();
  void updateSnapshot(ap_snapshot);
  void refreshDiagnostics();
  void appendLog(const QString &message);
  void saveSettings();
  QWidget *overviewPage();
  QWidget *historyPage();
  QWidget *diagnosticPage();

protected:
  void closeEvent(QCloseEvent *) override;

public:
  explicit MonitorWindow(bool offline = false, QWidget *parent = nullptr);
  ~MonitorWindow() override;
  void loadDemo();
  HistoryChart *historyChart() const {
    return chart;
  }
  QJsonObject diagnostics() const;
signals:
  void openHistory(QString path);
  void historySession();
  void historySample(QString address, ap_snapshot data, qint64 wall, qint64 tick);
  void historyLoad(QString address, qint64 now);
  void historyExport(QString address, qint64 now, QString path);
  void historyLog(QString message);
  void settingsSave(QString path, QJsonObject settings);
};
