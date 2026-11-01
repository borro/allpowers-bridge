#pragma once
#include "bluetooth.h"
#include "chart.h"
#include "controls.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QHash>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVariant>
#include <array>

QString monitorDataDirectory();
class MonitorWindow : public QMainWindow {
  Q_OBJECT
  TelemetryTransport *ble;
  QThread historyThread;
  HistoryWorker *history;
  HistoryChart *chart;
  QComboBox *devices, *profile, *horizon, *transportMode, *historySource;
  QCheckBox *stationsOnly, *logScale;
  QPushButton *scanButton, *connectButton, *exportButton, *liveButton, *readBoard;
  QLabel *status, *soc, *input, *output, *source, *runtime, *freshness, *outputAC, *outputUSB,
      *outputDC, *transportNote, *footer, *usbHistoryNote;
  QStackedWidget *pages;
  QWidget *usbHistoryControls;
  QTableWidget *technical;
  LiveLog *eventLog;
  QJsonObject settings, historyStats;
  QMap<QString, QJsonObject> discovered;
  QString selectedAddress, lastPacket, historyError, historyFlushError, usbEndpoint;
  QString dataPath;
  ap_snapshot last{};
  HistoryPoints points;
  HistoryPoints localPoints, servicePoints, usbPoints, usbUndated;
  QElapsedTimer uptime;
  bool historyBusy = false, offline = false;
  int bufferedSamples = 0;
  bool wasScanning = false;
  bool historyStarted = false;
  bool deviceListPending = false;
  qint64 lastSample = -1;
  void rebuildDevices();
  void updateControls(const QJsonObject *transportInfo = nullptr);
  void updateSnapshot(ap_snapshot);
  void refreshDiagnostics(bool force = false);
  void appendLog(const QString &message);
  void showIntegration();
  void saveSettings();
  void changeTransport();
  QString historyKey() const;
  void refreshHistory();
  void receiveHistoryBufferState(int pending, QString error);
  QWidget *overviewPage();
  QWidget *historyPage();
  QWidget *diagnosticPage();

  bool uiRefreshPending = false, historyDirty = true, chartHistoryDirty = true,
       overviewShown = false;
  bool plottedUsb = false, plottedRemote = false;
  int plottedSource = -1;
  qint64 plottedAt = -1;
  QString plottedAddress, plottedEndpoint, plottedSession, lastStateLog;
  QString lastStateLogAddress, lastStateLogEndpoint;
  qint64 lastStateLogSession = -1;
  HistoryPoints demoPoints;
  std::array<quint32, 12> overviewKey{};
  QVariantList controlsKey;
  QSet<QString> diagnosticKeys;
  QHash<QTableWidgetItem *, QJsonValue> diagnosticValues;
  void requestUiRefresh();
  void refreshVisiblePage();
  void updateOverview(bool force = false);
  bool ensureHistory();
  void updateHistoryView();

protected:
  void closeEvent(QCloseEvent *) override;
  void showEvent(QShowEvent *) override;

public:
  // An injected transport becomes a child of the window. An injected history
  // worker must have no parent; ownership transfers to the window's worker thread.
  explicit MonitorWindow(bool offline = false, QWidget *parent = nullptr,
                         TelemetryTransport *transport = nullptr,
                         HistoryWorker *historyWorker = nullptr);
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
  void historyUsbSample(QString address, ap_snapshot data, qint64 wall, qint64 tick, QString board,
                        quint32 epoch);
  void historyLoad(QString address, qint64 now);
  void historyExport(QString address, qint64 now, QString path);
  void historyExportPoints(HistoryPoints points, QString path);
  void historyImport(HistoryPoints points, qint64 now);
  void historyLog(QString message);
  void settingsSave(QString path, QJsonObject settings);
};
