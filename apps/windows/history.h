#pragma once
#include "session.h"
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSqlDatabase>
#include <QThread>
#include <QTimer>
#include <array>
#include <functional>
#include <memory>
#include <optional>

struct HistoryPoint {
  QString address, session;
  qint64 wall = 0, tick = 0;
  ap_snapshot data{};
  int stride = 1;
  quint32 sequence = 0;
  // Derived by USB fusion only; never persisted or accepted over IPC.
  bool bridgeFromPrevious = false;
};
using HistoryPoints = QList<HistoryPoint>;
quint32 historyEpoch(const HistoryPoint &);
QString historyBoard(const HistoryPoint &);
HistoryPoints sharedUsbHistory(const HistoryPoints &station, const HistoryPoints &archive,
                               const HistoryPoints &undated, const QString &currentSession);
HistoryPoints historyByEpoch(const HistoryPoints &dated, const HistoryPoints &undated);
bool joinEpochPoints(const HistoryPoint &, const HistoryPoint &);
// One dispatched write and one coalesced newest sample, independent of worker
// speed. Completion is explicit even when SQLite cannot accept the sample.
class HistorySampleQueue {
  bool running = false;
  std::optional<HistoryPoint> pending;

public:
  std::optional<HistoryPoint> submit(HistoryPoint point) {
    if (running) {
      pending = std::move(point);
      return {};
    }
    running = true;
    return point;
  }
  std::optional<HistoryPoint> complete() {
    auto next = takePending();
    running = next.has_value();
    return next;
  }
  std::optional<HistoryPoint> takePending() {
    auto next = std::move(pending);
    pending.reset();
    return next;
  }
  void clearPending() {
    pending.reset();
  }
  bool busy() const {
    return running;
  }
  bool waiting() const {
    return pending.has_value();
  }
};
Q_DECLARE_METATYPE(HistoryPoints)
bool joinPoints(const HistoryPoint &, const HistoryPoint &);
int sourceChange(const HistoryPoint &, const HistoryPoint &);
double logPosition(double age, double horizon);
HistoryPoints mergeHistory(const HistoryPoints &local, const HistoryPoints &service);
// Fuse samples without changing their provenance. Archive continuity can prove
// a connection across a change of acquisition clock/session.
HistoryPoints mergeUsbHistory(const HistoryPoints &station, const HistoryPoints &board,
                              const QString &currentBoardSession = {});
bool exportHistoryCsv(const HistoryPoints &, const QString &path);
// Transaction boundary used for alternate storage and deterministic I/O faults.
// A failed write/commit must leave the previous destination untouched.
class HistoryCsvTransaction {
public:
  virtual ~HistoryCsvTransaction() = default;
  virtual bool open(const QString &path) = 0;
  virtual QIODevice &device() = 0;
  virtual bool commit() = 0;
  virtual void cancel() = 0;
};
bool exportHistoryCsv(const HistoryPoints &, const QString &path,
                      std::unique_ptr<HistoryCsvTransaction> transaction);
bool writeHistoryCsv(const HistoryPoints &, QIODevice &device);

class HistoryStore {
  QString connection_;
  QSqlDatabase db;
  QString session_;
  int liveLimit, minuteLimit;
  QString lastKey;
  struct CachedRow {
    qint64 rowid, bucket;
    HistoryPoint point;
  };
  using CachedTables = std::array<QList<CachedRow>, 2>;
  mutable QMap<QString, CachedTables> pointCache_;
  mutable qint64 pointCacheVersion_ = -1;
  bool refreshPointCacheVersion() const;
  bool loadPointCache(const QString &address) const;
  HistoryPoints readPoints(const QString &address, qint64 now) const;
  bool buffered_ = false;
  HistoryPoints pending_;
  mutable CachedTables bufferedTables_;
  mutable qint64 bufferedVersion_ = -1;
  mutable int bufferedUsbCount_ = 0;
  bool readBufferedTables(CachedTables &tables) const;
  bool prepareBufferedTables() const;
  void applyBufferedPoint(CachedTables &, const HistoryPoint &,
                          const std::array<qint64, 2> &rowids = {}) const;

public:
  HistoryStore(const QString &path, int live = 3600, int minutes = 2880);
  ~HistoryStore();
  bool valid() const {
    return db.isOpen();
  }
  QString error() const;
  void newSession();
  bool setBuffered(bool enabled);
  bool stage(const QString &, const ap_snapshot &, qint64 wall, qint64 tick,
             const QString &board = {}, quint32 epoch = 0);
  bool flush();
  int pendingCount() const {
    return int(pending_.size());
  }
  bool record(const QString &, const ap_snapshot &, qint64 wall, qint64 tick,
              const QString &board = {}, quint32 epoch = 0);
  bool importPoints(const HistoryPoints &, qint64 now);
  HistoryPoints points(const QString &, qint64 now) const;
  HistoryPoints undatedPoints(const QString &) const;
  QJsonObject stats() const;
  bool exportCsv(const QString &, qint64, const QString &) const;
};

// All SQLite work, including exports, lives on this worker's thread.
class HistoryWorker : public QObject {
  Q_OBJECT
  HistoryStore *store = nullptr;
  QString directory;
  bool buffered = false;
  QTimer *flushTimer = nullptr;
  QString bufferError;
  void reportBufferState(QString error = {});
  void writeSample(QString address, ap_snapshot snapshot, qint64 wall, qint64 tick,
                   const QString &board = {}, quint32 epoch = 0);

public:
  ~HistoryWorker() override;
public slots:
  void exportPoints(HistoryPoints points, QString path);
  void importPoints(HistoryPoints points, qint64 now);
  virtual void open(const QString &path);
  void setBuffered(bool enabled);
  bool flush();
  void newSession();
  virtual void sample(QString address, ap_snapshot snapshot, qint64 wall, qint64 tick);
  void sampleUsb(QString address, ap_snapshot snapshot, qint64 wall, qint64 tick, QString board,
                 quint32 epoch);
  void load(QString address, qint64 now);
  void exportFile(QString address, qint64 now, QString path);
  void log(QString message);
  void saveSettings(QString path, QJsonObject settings);
signals:
  void opened();
  void sampleFinished();
  void loaded(QString address, HistoryPoints points, QJsonObject stats);
  void undatedLoaded(QString address, HistoryPoints points);
  void problem(QString message);
  void exported(QString path, bool ok);
  void bufferStateChanged(int pending, QString error);
};
