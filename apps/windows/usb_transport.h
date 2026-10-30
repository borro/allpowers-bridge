#pragma once
#include "history_operation_id.h"
#include "transport.h"
#include "usb_serial.h"
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <functional>
class UsbTransport : public TelemetryTransport {
  Q_OBJECT
public:
  using Ports = std::function<QList<UsbPort>()>;
  using Now = std::function<qint64()>;
  explicit UsbTransport(QObject *parent = nullptr, bool offline = false,
                        UsbChannel *channel = nullptr, Ports ports = {}, Now now = {},
                        HistoryOperationIds::Entropy entropy = {});
  ~UsbTransport() override;
  bool active() const override {
    return active_;
  }
  bool connected() const override {
    return opened_ && verified_ && !station_.isEmpty();
  }
  bool scanning() const override {
    return false;
  }
  bool usb() const override {
    return true;
  }
  QString address() const override {
    return station_;
  }
  QString endpoint() const override {
    return serial_;
  }
  QString phase() const override;
  QJsonObject diagnostics() const override;
  void scan() override;
  void start(const QString &id, int model) override {
    resume(id, {}, model);
  }
  void resume(const QString &, const QString &, int) override;
  void stop() override;
  void requestHistory() override;
  // Advance lifecycle deadlines when the transport uses an externally owned clock.
  void refresh() {
    tick();
  }

private:
  UsbChannel *channel = nullptr;
  Ports ports;
  Now elapsedMs;
  HistoryOperationIds::Entropy operationEntropy;
  QTimer timer;
  QElapsedTimer clock;
  QByteArray buffer;
  QString serial_, port_, station_, name_, board_, error_;
  QJsonObject status_, details_, boardDiagnostics;
  ap_snapshot last{};
  bool offline_ = false, active_ = false, opened_ = false, verified_ = false, pending = false,
       knownPending = false;
  bool importing = false, historyAttempted = false;
  qint64 openedAt = 0, statusAt = -1, frameAt = -1, retryAt = 0, diagAt = 0, anchorWall = 0,
         importDeadline = 0;
  quint32 lastFrames = 0, anchorTick = 0, currentSession = 0;
  int badLines = 0, historyBad = 0, historyUndated = 0;
  quint64 receivedBytes = 0, receivedLines = 0, commands = 0, openAttempts = 0, failures = 0;
  qint64 bytesAt = -1;
  HistoryPoints importPoints;
  QSet<quint32> importSequences;
  quint32 importRequest = 0, requestCounter = 0, importSession = 0, importGeneration = 0,
          importExpected = 0, importOldest = 0, importLatest = 0, importCursor = 0,
          importDigest = 2166136261u, legacyFirst = 0, legacyPages = 0, legacySpan = 0,
          legacyScanned = 0, legacyReceivedPage = 0;
  int importVersion = 0;
  bool importBegun = false, importCompromised = false, legacyVerification = false;
  bool importNonceRequired = false;
  QByteArray importNonce;
  void connectPort();
  void receive(const QByteArray &);
  void row(const QJsonObject &);
  void publish();
  void tick();
  void fail(QString);
  void command(const QByteArray &);
  void finishImport(const QString &result, const QString &reason = {});
  void nextLegacyPage();
};
