#include "history.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>
#include <algorithm>
#include <cmath>

bool joinPoints(const HistoryPoint &a, const HistoryPoint &b) {
  auto dt = b.wall - a.wall;
  int limit = a.stride == 1 && b.stride == 1 ? 2 : 90;
  return a.data.fresh && b.data.fresh && a.address == b.address && a.session == b.session &&
         dt > 0 && dt <= limit && qAbs((b.tick - a.tick) - dt) <= 2;
}
int sourceChange(const HistoryPoint &a, const HistoryPoint &b) {
  auto x = a.data.source, y = b.data.source;
  return joinPoints(a, b) && (x == 1 || x == 2) && (y == 1 || y == 2) && x != y ? int(y) : 0;
}
double logPosition(double age, double horizon) {
  return 1 - std::log1p(qBound(0., age, horizon)) / std::log1p(horizon);
}

HistoryStore::HistoryStore(const QString &path, int live, int minutes)
    : liveLimit(live), minuteLimit(minutes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  connection_ = QUuid::createUuid().toString(QUuid::Id128);
  db = QSqlDatabase::addDatabase("QSQLITE", connection_);
  db.setDatabaseName(path);
  db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=300");
  if (!db.open())
    return;
  QSqlQuery q(db);
  q.exec("PRAGMA journal_mode=WAL");
  q.exec("PRAGMA synchronous=NORMAL");
  q.exec("PRAGMA wal_autocheckpoint=64");
  q.exec("PRAGMA journal_size_limit=1048576");
  for (const QString &table : {QString("live"), QString("minutes")}) {
    QString key = table == "live" ? "wall" : "bucket";
    if (!q.exec("CREATE TABLE IF NOT EXISTS " + table +
                " (address TEXT NOT NULL,wall INTEGER NOT NULL,tick INTEGER NOT NULL,session TEXT "
                "NOT NULL,fresh INTEGER NOT NULL,known INTEGER NOT NULL,soc INTEGER NOT "
                "NULL,input_w INTEGER NOT NULL,output_w INTEGER NOT NULL,source INTEGER NOT "
                "NULL,ac_output INTEGER NOT NULL,usb_output INTEGER NOT NULL,dc_output INTEGER NOT "
                "NULL,stride INTEGER NOT NULL,bucket INTEGER NOT NULL,UNIQUE(address," +
                key + "))")) {
      db.close();
      return;
    }
    q.exec("CREATE INDEX IF NOT EXISTS " + table + "_wall ON " + table + "(wall)");
  }
  newSession();
}
HistoryStore::~HistoryStore() {
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connection_);
}
QString HistoryStore::error() const {
  return db.lastError().text();
}
void HistoryStore::newSession() {
  session_ = QUuid::createUuid().toString(QUuid::Id128);
  lastKey.clear();
}
bool HistoryStore::record(const QString &address, const ap_snapshot &s, qint64 wall, qint64 tick) {
  QString key = address + ":" + QString::number(wall) + ":" + session_;
  if (!valid() || lastKey == key)
    return false;
  if (!db.transaction())
    return false;
  bool ok = true;
  for (int index = 0; index < 2; ++index) {
    QString table = index ? "minutes" : "live";
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO " + table + " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (auto value :
         QVariantList{address, wall, tick, session_, int(s.fresh), int(s.known), int(s.soc),
                      int(s.input_w), int(s.output_w), int(s.source), int(s.ac_output),
                      int(s.usb_output), int(s.dc_output), index ? 60 : 1, wall / 60})
      q.addBindValue(value);
    ok = q.exec() && ok;
    q.prepare("DELETE FROM " + table + " WHERE wall < ?");
    q.addBindValue(wall - 172800);
    ok = q.exec() && ok;
    q.prepare("DELETE FROM " + table + " WHERE rowid IN (SELECT rowid FROM " + table +
              " ORDER BY rowid DESC LIMIT -1 OFFSET ?)");
    q.addBindValue(index ? minuteLimit : liveLimit);
    ok = q.exec() && ok;
  }
  if (ok)
    ok = db.commit();
  else
    db.rollback();
  if (ok)
    lastKey = key;
  return ok;
}
HistoryPoints HistoryStore::points(const QString &address, qint64 now) const {
  HistoryPoints output;
  QSet<qint64> buckets;
  for (const QString &table : {QString("live"), QString("minutes")}) {
    QSqlQuery q(db);
    q.prepare("SELECT * FROM " + table + " WHERE address=? AND wall BETWEEN ? AND ? ORDER BY wall");
    q.addBindValue(address);
    q.addBindValue(now - 172800);
    q.addBindValue(now);
    q.exec();
    while (q.next()) {
      auto bucket = q.value(14).toLongLong();
      if (table == "minutes" && buckets.contains(bucket))
        continue;
      if (table == "live")
        buckets.insert(bucket);
      HistoryPoint p;
      p.address = q.value(0).toString();
      p.wall = q.value(1).toLongLong();
      p.tick = q.value(2).toLongLong();
      p.session = q.value(3).toString();
      p.data.fresh = q.value(4).toUInt();
      p.data.known = q.value(5).toUInt();
      p.data.soc = q.value(6).toUInt();
      p.data.input_w = q.value(7).toUInt();
      p.data.output_w = q.value(8).toUInt();
      p.data.source = q.value(9).toUInt();
      p.data.ac_output = q.value(10).toUInt();
      p.data.usb_output = q.value(11).toUInt();
      p.data.dc_output = q.value(12).toUInt();
      p.stride = q.value(13).toInt();
      output.append(p);
    }
  }
  std::sort(output.begin(), output.end(),
            [](const auto &a, const auto &b) { return a.wall < b.wall; });
  return output;
}
QJsonObject HistoryStore::stats() const {
  QJsonObject result;
  for (const QString &t : {QString("live"), QString("minutes")}) {
    QSqlQuery q(db);
    q.exec("SELECT COUNT(*) FROM " + t);
    q.next();
    result[t] = q.value(0).toInt();
  }
  return result;
}
bool HistoryStore::exportCsv(const QString &address, qint64 now, const QString &path) const {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;
  QTextStream out(&file);
  out.setGenerateByteOrderMark(true);
  out << "address,wall,tick,session,fresh,known,soc,input_w,output_w,source,ac_output,usb_output,"
         "dc_output,stride\n";
  for (const auto &p : points(address, now)) {
    const auto &s = p.data;
    out << '"' << QString(p.address).replace('"', "\"\"") << "\"," << p.wall << ',' << p.tick << ','
        << p.session << ',' << s.fresh << ',' << s.known << ',' << s.soc << ',' << s.input_w << ','
        << s.output_w << ',' << s.source << ',' << s.ac_output << ',' << s.usb_output << ','
        << s.dc_output << ',' << p.stride << '\n';
  }
  return out.status() == QTextStream::Ok;
}
HistoryWorker::~HistoryWorker() {
  delete store;
}
void HistoryWorker::open(const QString &path) {
  directory = QFileInfo(path).absolutePath();
  store = new HistoryStore(path);
  if (!store->valid())
    emit problem(store->error());
}
void HistoryWorker::newSession() {
  if (store)
    store->newSession();
}
void HistoryWorker::sample(QString a, ap_snapshot s, qint64 w, qint64 t) {
  if (store && store->valid()) {
    if (!store->record(a, s, w, t))
      emit problem("Не удалось сохранить точку истории");
    load(a, w);
  }
}
void HistoryWorker::load(QString a, qint64 now) {
  if (store && store->valid())
    emit loaded(store->points(a, now), store->stats());
}
void HistoryWorker::exportFile(QString a, qint64 now, QString path) {
  emit exported(path, store && store->exportCsv(a, now, path));
}
void HistoryWorker::log(QString message) {
  if (directory.isEmpty())
    return;
  auto path = QDir(directory).filePath("monitor.log");
  if (QFileInfo(path).size() > 1024 * 1024) {
    QFile::remove(path + ".2");
    QFile::rename(path + ".1", path + ".2");
    QFile::rename(path, path + ".1");
  }
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Append))
    file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " " + message + "\n")
                   .toUtf8());
}
void HistoryWorker::saveSettings(QString path, QJsonObject settings) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(settings).toJson()) < 0 ||
      !file.commit())
    emit problem("Не удалось сохранить настройки");
}
