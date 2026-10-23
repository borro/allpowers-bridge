#pragma once
#include "session.h"
#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QThread>

struct HistoryPoint {
  QString address, session;
  qint64 wall = 0, tick = 0;
  ap_snapshot data{};
  int stride = 1;
};
using HistoryPoints = QList<HistoryPoint>;
Q_DECLARE_METATYPE(HistoryPoints)
bool joinPoints(const HistoryPoint &, const HistoryPoint &);
int sourceChange(const HistoryPoint &, const HistoryPoint &);
double logPosition(double age, double horizon);

class HistoryStore {
  QString connection_;
  QSqlDatabase db;
  QString session_;
  int liveLimit, minuteLimit;
  QString lastKey;

public:
  HistoryStore(const QString &path, int live = 3600, int minutes = 2880);
  ~HistoryStore();
  bool valid() const {
    return db.isOpen();
  }
  QString error() const;
  void newSession();
  bool record(const QString &, const ap_snapshot &, qint64 wall, qint64 tick);
  HistoryPoints points(const QString &, qint64 now) const;
  QJsonObject stats() const;
  bool exportCsv(const QString &, qint64, const QString &) const;
};

// All SQLite work, including exports, lives on this worker's thread.
class HistoryWorker : public QObject {
  Q_OBJECT
  HistoryStore *store = nullptr;
  QString directory;

public:
  ~HistoryWorker() override;
public slots:
  void open(const QString &path);
  void newSession();
  void sample(QString address, ap_snapshot snapshot, qint64 wall, qint64 tick);
  void load(QString address, qint64 now);
  void exportFile(QString address, qint64 now, QString path);
  void log(QString message);
  void saveSettings(QString path, QJsonObject settings);
signals:
  void loaded(HistoryPoints points, QJsonObject stats);
  void problem(QString message);
  void exported(QString path, bool ok);
};
