#include "history.h"
#include <QCoreApplication>
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

QString historyBoard(const HistoryPoint &p) {
  if (p.session.startsWith("usb-live:"))
    return p.session.section(':', 1, 1);
  return p.address.startsWith("usb-board:") ? p.address.mid(10) : QString{};
}
quint32 historyEpoch(const HistoryPoint &p) {
  if (p.session.startsWith("usb-live:"))
    return p.session.section(':', 2, 2).toUInt();
  return p.address.startsWith("usb-board:") && p.session.startsWith("usb-")
             ? p.session.section("-gap-", 0, 0).mid(4).toUInt()
             : 0;
}
HistoryPoints historyByEpoch(const HistoryPoints &dated, const HistoryPoints &undated) {
  auto result = dated;
  result.append(undated);
  std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    if (historyBoard(a) != historyBoard(b))
      return historyBoard(a) < historyBoard(b);
    if (historyEpoch(a) != historyEpoch(b))
      return historyEpoch(a) < historyEpoch(b);
    if (!historyEpoch(a) && a.wall != b.wall)
      return a.wall < b.wall;
    if (a.tick != b.tick)
      return a.tick < b.tick;
    return a.sequence < b.sequence;
  });
  return result;
}
bool joinEpochPoints(const HistoryPoint &a, const HistoryPoint &b) {
  const bool sameSession = a.address == b.address && a.session == b.session;
  const bool sameBoot = historyEpoch(a) && historyEpoch(a) == historyEpoch(b) &&
                        historyBoard(a) == historyBoard(b) && !a.session.contains("-gap-") &&
                        !b.session.contains("-gap-");
  const int limit = a.stride == 1 && b.stride == 1 ? 2 : 90;
  return a.data.fresh && b.data.fresh && (sameSession || sameBoot) && b.tick >= a.tick &&
         b.tick - a.tick <= limit;
}

bool joinPoints(const HistoryPoint &a, const HistoryPoint &b) {
  auto dt = b.wall - a.wall;
  if (b.bridgeFromPrevious)
    return a.data.fresh && b.data.fresh && dt > 0;
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
namespace {
HistoryPoint persistedPoint(const QString &address, const QString &session, const ap_snapshot &s,
                            qint64 wall, qint64 tick, int stride) {
  HistoryPoint point;
  point.address = address;
  point.session = session;
  point.wall = wall;
  point.tick = tick;
  point.stride = stride;
  point.data.fresh = s.fresh;
  point.data.known = s.known;
  point.data.soc = s.soc;
  point.data.input_w = s.input_w;
  point.data.output_w = s.output_w;
  point.data.source = s.source;
  point.data.ac_output = s.ac_output;
  point.data.usb_output = s.usb_output;
  point.data.dc_output = s.dc_output;
  return point;
}
HistoryPoint queryPoint(const QSqlQuery &query, int offset, bool archive = false) {
  ap_snapshot snapshot{};
  snapshot.fresh = query.value(offset + 4).toUInt();
  snapshot.known = query.value(offset + 5).toUInt();
  snapshot.soc = query.value(offset + 6).toUInt();
  snapshot.input_w = query.value(offset + 7).toUInt();
  snapshot.output_w = query.value(offset + 8).toUInt();
  snapshot.source = query.value(offset + 9).toUInt();
  snapshot.ac_output = query.value(offset + 10).toUInt();
  snapshot.usb_output = query.value(offset + 11).toUInt();
  snapshot.dc_output = query.value(offset + 12).toUInt();
  auto point =
      persistedPoint(query.value(offset).toString(), query.value(offset + 3).toString(), snapshot,
                     query.value(offset + 1).toLongLong(), query.value(offset + 2).toLongLong(),
                     query.value(offset + 13).toInt());
  if (archive)
    point.sequence = query.value(offset + 15).toUInt();
  return point;
}
} // namespace
HistoryPoints mergeHistory(const HistoryPoints &local, const HistoryPoints &remote) {
  QMap<QPair<QString, qint64>, HistoryPoint> selected;
  QSet<QPair<QString, qint64>> seconds;
  for (const auto *list : {&local, &remote})
    for (const auto &p : *list)
      if (p.stride == 1)
        seconds.insert({p.address, p.wall / 60});
  for (const auto *list : {&local, &remote})
    for (const auto &p : *list) {
      if (p.stride == 60 && seconds.contains({p.address, p.wall / 60}))
        continue;
      selected.insert({p.address, p.wall}, p);
    }
  auto result = selected.values();
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.wall < b.wall; });
  return result;
}

HistoryPoints mergeUsbHistory(const HistoryPoints &station, const HistoryPoints &board,
                              const QString &currentBoardSession) {
  auto orderedBoard = board;
  std::sort(orderedBoard.begin(), orderedBoard.end(),
            [](const auto &a, const auto &b) { return a.wall < b.wall; });
  for (auto &p : orderedBoard)
    p.bridgeFromPrevious = false;
  QSet<qint64> recordedMinutes;
  for (const auto &p : station)
    recordedMinutes.insert(p.wall / 60);
  HistoryPoints result = station;
  for (const auto &p : orderedBoard)
    if (!recordedMinutes.contains(p.wall / 60))
      result.append(p);
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.wall < b.wall; });
  for (auto &p : result)
    p.bridgeFromPrevious = false;

  // These ranges are supported by actual consecutive board records. They can
  // bridge application restarts/clock changes without drawing a second trace.
  struct Range {
    qint64 from, to;
  };
  QList<Range> coverage;
  const HistoryPoint *previous = nullptr;
  for (const auto &p : orderedBoard) {
    if (!p.data.fresh) {
      previous = nullptr;
      continue;
    }
    if (previous && joinPoints(*previous, p))
      coverage.last().to = p.wall;
    else
      coverage.append({p.wall, p.wall});
    previous = &p;
  }
  // The verified live USB connection extends the current boot's most recent
  // minute sample. Never extend an earlier boot or a firmware-marked gap.
  if (!coverage.isEmpty() && !orderedBoard.isEmpty() &&
      orderedBoard.last().session == currentBoardSession && orderedBoard.last().data.fresh)
    coverage.last().to += 90;
  qsizetype range = 0;
  for (qsizetype i = 1; i < result.size(); ++i) {
    const auto &a = result[i - 1];
    auto &b = result[i];
    if (joinPoints(a, b) || !a.data.fresh || !b.data.fresh || b.wall <= a.wall)
      continue;
    if (a.address != b.address && !a.address.startsWith("usb-board:") &&
        !b.address.startsWith("usb-board:"))
      continue;
    while (range < coverage.size() && coverage[range].to < a.wall)
      ++range;
    b.bridgeFromPrevious =
        range < coverage.size() && coverage[range].from <= a.wall && coverage[range].to >= b.wall;
  }
  return result;
}

HistoryPoints sharedUsbHistory(const HistoryPoints &station, const HistoryPoints &archive,
                               const HistoryPoints &undated, const QString &currentSession) {
  auto result = mergeUsbHistory(station, archive, currentSession);
  result.append(undated);
  std::stable_sort(result.begin(), result.end(),
                   [](const auto &a, const auto &b) { return a.wall < b.wall; });
  return result;
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
  // The user-approved archive transition discards the old USB cache. Station
  // history is independent. The new archive preserves records without dates.
  if (q.exec("PRAGMA table_info(usb_minutes)")) {
    bool exists = false, current = false;
    while (q.next()) {
      exists = true;
      current |= q.value(1).toString() == "sequence";
    }
    if (exists && !current && !q.exec("DROP TABLE usb_minutes")) {
      db.close();
      return;
    }
  }
  for (const QString &table : {QString("live"), QString("minutes"), QString("usb_minutes")}) {
    QString key = table == "live" ? "wall" : "bucket";
    if (!q.exec("CREATE TABLE IF NOT EXISTS " + table +
                " (address TEXT NOT NULL,wall INTEGER NOT NULL,tick INTEGER NOT NULL,session TEXT "
                "NOT NULL,fresh INTEGER NOT NULL,known INTEGER NOT NULL,soc INTEGER NOT "
                "NULL,input_w INTEGER NOT NULL,output_w INTEGER NOT NULL,source INTEGER NOT "
                "NULL,ac_output INTEGER NOT NULL,usb_output INTEGER NOT NULL,dc_output INTEGER NOT "
                "NULL,stride INTEGER NOT NULL,bucket INTEGER NOT NULL," +
                (table == "usb_minutes"
                     ? QString("sequence INTEGER NOT NULL,UNIQUE(address,session,tick,sequence))")
                     : "UNIQUE(address," + key + "))"))) {
      db.close();
      return;
    }
    q.exec("CREATE INDEX IF NOT EXISTS " + table + "_wall ON " + table + "(wall)");
  }
  if (!q.exec("CREATE UNIQUE INDEX IF NOT EXISTS usb_minutes_record ON "
              "usb_minutes(address,session,tick,sequence)")) {
    db.close();
    return;
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
bool HistoryStore::setBuffered(bool enabled) {
  if (buffered_ == enabled)
    return true;
  if (!enabled && !flush())
    return false;
  QSqlQuery query(db);
  if (!query.exec(enabled ? "PRAGMA synchronous=FULL" : "PRAGMA synchronous=NORMAL"))
    return false;
  buffered_ = enabled;
  bufferedVersion_ = -1;
  return true;
}
bool HistoryStore::readBufferedTables(CachedTables &tables) const {
  for (int index = 0; index < 2; ++index) {
    QSqlQuery query(db);
    if (!query.exec("SELECT rowid,* FROM " + QString(index ? "minutes" : "live") +
                    " ORDER BY rowid"))
      return false;
    while (query.next())
      tables[index].append(
          {query.value(0).toLongLong(), query.value(15).toLongLong(), queryPoint(query, 1)});
    if (query.lastError().isValid())
      return false;
  }
  return true;
}
void HistoryStore::applyBufferedPoint(CachedTables &tables, const HistoryPoint &point,
                                      const std::array<qint64, 2> &rowids) const {
  for (int index = 0; index < 2; ++index) {
    auto &rows = tables[index];
    rows.removeIf([&](const CachedRow &row) {
      return row.point.wall < point.wall - 172800 ||
             (row.point.address == point.address &&
              (index ? row.bucket == point.wall / 60 : row.point.wall == point.wall));
    });
    auto stored = point;
    stored.stride = index ? 60 : 1;
    rows.append({rowids[index], point.wall / 60, std::move(stored)});
    const int limit = qMax(0, index ? minuteLimit : liveLimit);
    if (rows.size() > limit)
      rows.remove(0, rows.size() - limit);
  }
}
bool HistoryStore::prepareBufferedTables() const {
  if (!refreshPointCacheVersion())
    return false;
  if (bufferedVersion_ == pointCacheVersion_)
    return true;
  CachedTables tables;
  if (!readBufferedTables(tables))
    return false;
  QSqlQuery count(db);
  if (!count.exec("SELECT COUNT(*) FROM usb_minutes") || !count.next())
    return false;
  bufferedUsbCount_ = count.value(0).toInt();
  for (const auto &point : pending_)
    applyBufferedPoint(tables, point);
  bufferedTables_ = std::move(tables);
  bufferedVersion_ = pointCacheVersion_;
  return true;
}
bool HistoryStore::stage(const QString &address, const ap_snapshot &snapshot, qint64 wall,
                         qint64 tick, const QString &board, quint32 epoch) {
  const auto session = !board.isEmpty() && epoch
                           ? QString("usb-live:%1:%2:%3").arg(board).arg(epoch).arg(session_)
                           : session_;
  const auto key = address + ":" + QString::number(wall) + ":" + session;
  if (!valid() || !buffered_ || pending_.size() >= 3600 || lastKey == key ||
      !prepareBufferedTables())
    return false;
  auto point = persistedPoint(address, session, snapshot, wall, tick, 1);
  pending_.append(point);
  applyBufferedPoint(bufferedTables_, point);
  lastKey = key;
  return true;
}
bool HistoryStore::flush() {
  if (pending_.isEmpty())
    return true;
  if (!valid() || !db.transaction())
    return false;
  CachedTables tables;
  bool ok = readBufferedTables(tables);
  // Simulate the original arrival-order retention in RAM. Physical cleanup is
  // done once per table, after the complete batch, including clock rollbacks.
  for (const auto &point : pending_) {
    if (!ok)
      break;
    std::array<qint64, 2> rowids{};
    const auto &s = point.data;
    for (int index = 0; index < 2; ++index) {
      QSqlQuery query(db);
      query.prepare("INSERT OR REPLACE INTO " + QString(index ? "minutes" : "live") +
                    " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
      for (const auto &value : QVariantList{
               point.address, point.wall, point.tick, point.session, int(s.fresh), int(s.known),
               int(s.soc), int(s.input_w), int(s.output_w), int(s.source), int(s.ac_output),
               int(s.usb_output), int(s.dc_output), index ? 60 : 1, point.wall / 60})
        query.addBindValue(value);
      if (!query.exec()) {
        ok = false;
        break;
      }
      rowids[index] = query.lastInsertId().toLongLong();
    }
    if (ok)
      applyBufferedPoint(tables, point, rowids);
  }
  for (int index = 0; ok && index < 2; ++index) {
    QStringList placeholders;
    for (qsizetype i = 0; i < tables[index].size(); ++i)
      placeholders.append("?");
    QSqlQuery query(db);
    query.prepare("DELETE FROM " + QString(index ? "minutes" : "live") +
                  (placeholders.isEmpty()
                       ? QString{}
                       : " WHERE rowid NOT IN (" + placeholders.join(',') + ")"));
    for (const auto &row : tables[index])
      query.addBindValue(row.rowid);
    ok = query.exec();
  }
  if (ok)
    ok = db.commit();
  if (!ok)
    db.rollback();
  if (ok)
    pending_.clear();
  // A failed commit can race with an external writer too. Rebase the retained
  // pending batch on the next read; never present uncommitted SQL as durable.
  bufferedVersion_ = -1;
  pointCache_.clear();
  return ok;
}
bool HistoryStore::record(const QString &address, const ap_snapshot &s, qint64 wall, qint64 tick,
                          const QString &board, quint32 epoch) {
  const auto session = !board.isEmpty() && epoch
                           ? QString("usb-live:%1:%2:%3").arg(board).arg(epoch).arg(session_)
                           : session_;
  QString key = address + ":" + QString::number(wall) + ":" + session;
  if (!valid() || lastKey == key)
    return false;
  const bool cached = refreshPointCacheVersion() && pointCache_.contains(address);
  if (!db.transaction())
    return false;
  bool ok = true;
  bool cacheMetadata = cached;
  std::array<qint64, 2> inserted{}, retained{};
  std::array<bool, 2> nonempty{};
  for (int index = 0; index < 2; ++index) {
    QString table = index ? "minutes" : "live";
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO " + table + " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (auto value :
         QVariantList{address, wall, tick, session, int(s.fresh), int(s.known), int(s.soc),
                      int(s.input_w), int(s.output_w), int(s.source), int(s.ac_output),
                      int(s.usb_output), int(s.dc_output), index ? 60 : 1, wall / 60})
      q.addBindValue(value);
    const bool written = q.exec();
    ok = written && ok;
    if (written)
      inserted[index] = q.lastInsertId().toLongLong();
    q.prepare("DELETE FROM " + table + " WHERE wall < ?");
    q.addBindValue(wall - 172800);
    ok = q.exec() && ok;
    q.prepare("DELETE FROM " + table + " WHERE rowid IN (SELECT rowid FROM " + table +
              " ORDER BY rowid DESC LIMIT -1 OFFSET ?)");
    q.addBindValue(index ? minuteLimit : liveLimit);
    ok = q.exec() && ok;
    if (cached) {
      QSqlQuery metadata(db);
      if (metadata.exec("SELECT MIN(rowid) FROM " + table) && metadata.next()) {
        nonempty[index] = !metadata.value(0).isNull();
        retained[index] = metadata.value(0).toLongLong();
      } else {
        cacheMetadata = false;
      }
    }
  }
  if (ok)
    ok = db.commit();
  else
    db.rollback();
  if (ok) {
    lastKey = key;
    // data_version changes only for writes by another connection. Check again
    // after committing in case another writer committed before our transaction.
    if (!refreshPointCacheVersion() || !cacheMetadata || !pointCache_.contains(address)) {
      pointCache_.clear();
    } else {
      auto tables = pointCache_.take(address);
      // The SQL retention limits apply globally, so other addresses must reload
      // before being read again. The current address can be updated exactly.
      pointCache_.clear();
      for (int index = 0; index < 2; ++index) {
        auto &rows = tables[index];
        rows.removeIf([&](const CachedRow &row) {
          return !nonempty[index] || row.rowid < retained[index] ||
                 row.point.wall < wall - 172800 ||
                 (index ? row.bucket == wall / 60 : row.point.wall == wall);
        });
        if (nonempty[index] && inserted[index] >= retained[index])
          rows.append({inserted[index], wall / 60,
                       persistedPoint(address, session, s, wall, tick, index ? 60 : 1)});
      }
      pointCache_.insert(address, std::move(tables));
    }
  } else {
    pointCache_.clear();
  }
  return ok;
}
bool HistoryStore::importPoints(const HistoryPoints &points, qint64 now) {
  if (!valid() || points.size() > 2880 || !db.transaction())
    return false;
  bool ok = true;
  using Epoch = QPair<QString, QString>;
  QMap<Epoch, qint64> anchors;
  QSet<Epoch> conflicting;
  const auto epochKey = [](const HistoryPoint &p) {
    return Epoch{p.address, p.session.section("-gap-", 0, 0)};
  };
  const auto anchor = [&](const HistoryPoint &p) {
    if (!p.wall)
      return;
    const auto key = epochKey(p);
    const qint64 base = p.wall - p.tick;
    if (anchors.contains(key) && qAbs(anchors[key] - base) > 2)
      conflicting.insert(key);
    else if (!anchors.contains(key))
      anchors.insert(key, base);
  };
  QSqlQuery retained(db);
  if (!retained.exec("SELECT address,session,wall,tick FROM usb_minutes WHERE wall>0"))
    ok = false;
  while (retained.next()) {
    HistoryPoint p;
    p.address = retained.value(0).toString();
    p.session = retained.value(1).toString();
    p.wall = retained.value(2).toLongLong();
    p.tick = retained.value(3).toLongLong();
    anchor(p);
  }
  for (const auto &p : points)
    anchor(p);
  QSet<QString> boards;
  for (const auto &p : points) {
    if (!p.address.startsWith("usb-board:") || p.stride != 60 || p.tick < 0 ||
        (p.wall && p.wall < 1577836800) || p.wall > now + 2 || p.data.soc > 100) {
      ok = false;
      break;
    }
    const auto &s = p.data;
    boards.insert(p.address);
    qint64 wall = p.wall;
    const auto key = epochKey(p);
    if (!wall && anchors.contains(key) && !conflicting.contains(key)) {
      const auto candidate = anchors[key] + p.tick;
      if (candidate >= 1577836800 && candidate <= now + 2)
        wall = candidate;
    }
    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO usb_minutes VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(address,session,tick,sequence) DO UPDATE SET "
        "wall=CASE WHEN usb_minutes.wall=0 THEN excluded.wall ELSE usb_minutes.wall END,"
        "bucket=CASE WHEN usb_minutes.wall=0 THEN excluded.bucket ELSE usb_minutes.bucket END");
    for (const auto &v :
         QVariantList{p.address, wall, p.tick, p.session, int(s.fresh), int(s.known), int(s.soc),
                      int(s.input_w), int(s.output_w), int(s.source), int(s.ac_output),
                      int(s.usb_output), int(s.dc_output), 60, wall / 60, qint64(p.sequence)})
      q.addBindValue(v);
    if (!q.exec()) {
      ok = false;
      break;
    }
  }
  QSqlQuery prune(db);
  // Resolve earlier imports of this epoch too, including gaps, without changing
  // known timestamps. Never infer time across a boot boundary.
  for (auto it = anchors.cbegin(); it != anchors.cend(); ++it) {
    if (conflicting.contains(it.key()))
      continue;
    prune.prepare("UPDATE usb_minutes SET wall=?+tick,bucket=(?+tick)/60 WHERE address=? "
                  "AND (session=? OR session LIKE ? ESCAPE '!') AND wall=0 AND ?+tick BETWEEN "
                  "1577836800 AND ?");
    auto escaped = it.key().second;
    escaped.replace("!", "!!").replace("%", "!%").replace("_", "!_");
    for (const auto &v : QVariantList{it.value(), it.value(), it.key().first, it.key().second,
                                      escaped + "-gap-%", it.value(), now + 2})
      prune.addBindValue(v);
    ok = prune.exec() && ok;
  }
  for (const auto &board : boards) {
    prune.prepare("DELETE FROM usb_minutes WHERE rowid IN (SELECT rowid FROM usb_minutes "
                  "WHERE address=? ORDER BY rowid DESC LIMIT -1 OFFSET ?)");
    prune.addBindValue(board);
    prune.addBindValue(minuteLimit);
    ok = prune.exec() && ok;
  }
  if (ok && db.commit()) {
    pointCache_.clear();
    bufferedVersion_ = -1;
    return true;
  }
  db.rollback();
  pointCache_.clear();
  return false;
}
void HistoryWorker::importPoints(HistoryPoints points, qint64 now) {
  if (!store || !store->importPoints(points, now)) {
    emit problem(QCoreApplication::translate("Allpowers", "Could not save USB board archive"));
    return;
  }
  if (!points.isEmpty())
    load(points.first().address, now);
}
bool HistoryStore::refreshPointCacheVersion() const {
  QSqlQuery query(db);
  if (!query.exec("PRAGMA data_version") || !query.next()) {
    pointCache_.clear();
    pointCacheVersion_ = -1;
    return false;
  }
  const auto version = query.value(0).toLongLong();
  if (version != pointCacheVersion_)
    pointCache_.clear();
  pointCacheVersion_ = version;
  return true;
}
bool HistoryStore::loadPointCache(const QString &address) const {
  CachedTables tables;
  for (int index = 0; index < 2; ++index) {
    QSqlQuery query(db);
    query.prepare("SELECT rowid,* FROM " + QString(index ? "minutes" : "live") +
                  " WHERE address=?");
    query.addBindValue(address);
    if (!query.exec())
      return false;
    while (query.next())
      tables[index].append(
          {query.value(0).toLongLong(), query.value(15).toLongLong(), queryPoint(query, 1)});
    if (query.lastError().isValid())
      return false;
  }
  pointCache_.insert(address, std::move(tables));
  return true;
}
HistoryPoints HistoryStore::points(const QString &address, qint64 now) const {
  if (buffered_ && !address.startsWith("usb-board:") &&
      (prepareBufferedTables() || !pending_.isEmpty())) {
    HistoryPoints output;
    QSet<qint64> buckets;
    for (int index = 0; index < 2; ++index)
      for (const auto &row : bufferedTables_[index]) {
        if (row.point.address != address || row.point.wall < now - 172800 || row.point.wall > now ||
            (index && buckets.contains(row.bucket)))
          continue;
        if (!index)
          buckets.insert(row.bucket);
        output.append(row.point);
      }
    std::sort(output.begin(), output.end(),
              [](const auto &a, const auto &b) { return a.wall < b.wall; });
    return output;
  }
  if (address.startsWith("usb-board:") || !refreshPointCacheVersion() ||
      (!pointCache_.contains(address) && !loadPointCache(address)))
    return readPoints(address, now);
  HistoryPoints output;
  QSet<qint64> buckets;
  const auto &tables = pointCache_.constFind(address).value();
  for (int index = 0; index < 2; ++index)
    for (const auto &row : tables[index]) {
      if (row.point.wall < now - 172800 || row.point.wall > now ||
          (index && buckets.contains(row.bucket)))
        continue;
      if (!index)
        buckets.insert(row.bucket);
      output.append(row.point);
    }
  std::sort(output.begin(), output.end(),
            [](const auto &a, const auto &b) { return a.wall < b.wall; });
  return output;
}
HistoryPoints HistoryStore::readPoints(const QString &address, qint64 now) const {
  HistoryPoints output;
  QSet<qint64> buckets;
  const QStringList tables = address.startsWith("usb-board:") ? QStringList{"usb_minutes"}
                                                              : QStringList{"live", "minutes"};
  for (const QString &table : tables) {
    QSqlQuery q(db);
    q.prepare("SELECT * FROM " + table + " WHERE address=? AND wall BETWEEN ? AND ? ORDER BY wall");
    q.addBindValue(address);
    q.addBindValue(table == "usb_minutes" ? qint64(1577836800) : now - 172800);
    q.addBindValue(now);
    q.exec();
    while (q.next()) {
      auto bucket = q.value(14).toLongLong();
      if (table != "live" && buckets.contains(bucket))
        continue;
      if (table == "live")
        buckets.insert(bucket);
      output.append(queryPoint(q, 0, table == "usb_minutes"));
    }
  }
  std::sort(output.begin(), output.end(),
            [](const auto &a, const auto &b) { return a.wall < b.wall; });
  return output;
}
HistoryPoints HistoryStore::undatedPoints(const QString &address) const {
  HistoryPoints output;
  QSqlQuery q(db);
  q.prepare("SELECT * FROM usb_minutes WHERE address=? AND wall=0 ORDER BY rowid");
  q.addBindValue(address);
  if (!q.exec())
    return output;
  while (q.next()) {
    HistoryPoint p;
    p.address = q.value(0).toString();
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
    p.stride = 60;
    p.sequence = q.value(15).toUInt();
    output.append(p);
  }
  return output;
}
QJsonObject HistoryStore::stats() const {
  if (buffered_ && (prepareBufferedTables() || !pending_.isEmpty()))
    return {{"live", int(bufferedTables_[0].size())},
            {"minutes", int(bufferedTables_[1].size())},
            {"usb_minutes", bufferedUsbCount_},
            {"pending", pendingCount()}};
  QJsonObject result;
  for (const QString &t : {QString("live"), QString("minutes"), QString("usb_minutes")}) {
    QSqlQuery q(db);
    q.exec("SELECT COUNT(*) FROM " + t);
    q.next();
    result[t] = q.value(0).toInt();
  }
  return result;
}
bool HistoryStore::exportCsv(const QString &address, qint64 now, const QString &path) const {
  return exportHistoryCsv(points(address, now), path);
}
bool exportHistoryCsv(const HistoryPoints &points, const QString &path) {
  class SaveFileTransaction final : public HistoryCsvTransaction {
    QSaveFile file;

  public:
    bool open(const QString &path) override {
      file.setFileName(path);
      file.setDirectWriteFallback(false);
      return file.open(QIODevice::WriteOnly | QIODevice::Text);
    }
    QIODevice &device() override {
      return file;
    }
    bool commit() override {
      return file.commit();
    }
    void cancel() override {
      file.cancelWriting();
    }
  };
  return exportHistoryCsv(points, path, std::make_unique<SaveFileTransaction>());
}
bool exportHistoryCsv(const HistoryPoints &points, const QString &path,
                      std::unique_ptr<HistoryCsvTransaction> transaction) {
  if (!transaction || !transaction->open(path))
    return false;
  if (!writeHistoryCsv(points, transaction->device())) {
    transaction->cancel();
    return false;
  }
  if (!transaction->commit()) {
    transaction->cancel();
    return false;
  }
  return true;
}
bool writeHistoryCsv(const HistoryPoints &points, QIODevice &device) {
  QTextStream out(&device);
  out.setGenerateByteOrderMark(true);
  out << "address,wall,tick,session,fresh,known,soc,input_w,output_w,source,ac_output,usb_output,"
         "dc_output,stride,sequence\n";
  for (const auto &p : points) {
    const auto &s = p.data;
    out << '"' << QString(p.address).replace('"', "\"\"") << "\"," << p.wall << ',' << p.tick << ','
        << p.session << ',' << s.fresh << ',' << s.known << ',' << s.soc << ',' << s.input_w << ','
        << s.output_w << ',' << s.source << ',' << s.ac_output << ',' << s.usb_output << ','
        << s.dc_output << ',' << p.stride << ',' << p.sequence << '\n';
  }
  out.flush();
  return out.status() == QTextStream::Ok;
}
void HistoryWorker::exportPoints(HistoryPoints points, QString path) {
  emit exported(path, exportHistoryCsv(points, path));
}
HistoryWorker::~HistoryWorker() {
  delete store;
}
void HistoryWorker::reportBufferState(QString error) {
  if (!error.isEmpty() && error != bufferError)
    emit problem(error);
  bufferError = std::move(error);
  emit bufferStateChanged(store ? store->pendingCount() : 0, bufferError);
}
void HistoryWorker::setBuffered(bool enabled) {
  if (store && !store->setBuffered(enabled)) {
    reportBufferState(QCoreApplication::translate("Allpowers", "Could not save history point"));
    return;
  }
  buffered = enabled;
  if (enabled && store) {
    if (!flushTimer) {
      flushTimer = new QTimer(this);
      flushTimer->setTimerType(Qt::PreciseTimer);
      flushTimer->setInterval(60000);
      connect(flushTimer, &QTimer::timeout, this, [this] { flush(); });
    }
    flushTimer->start();
  } else if (flushTimer) {
    flushTimer->stop();
  }
}
bool HistoryWorker::flush() {
  const bool ok = !store || store->flush();
  reportBufferState(ok ? QString{}
                       : QCoreApplication::translate("Allpowers", "Could not save history point"));
  return ok;
}
void HistoryWorker::open(const QString &path) {
  directory = QFileInfo(path).absolutePath();
  store = new HistoryStore(path);
  if (!store->valid())
    emit problem(store->error());
  else {
    if (buffered)
      setBuffered(true);
    emit opened();
  }
}
void HistoryWorker::newSession() {
  if (store)
    store->newSession();
}
void HistoryWorker::sample(QString a, ap_snapshot s, qint64 w, qint64 t) {
  writeSample(a, s, w, t);
}
void HistoryWorker::sampleUsb(QString a, ap_snapshot s, qint64 w, qint64 t, QString board,
                              quint32 epoch) {
  writeSample(a, s, w, t, board, epoch);
}
void HistoryWorker::writeSample(QString a, ap_snapshot s, qint64 w, qint64 t, const QString &board,
                                quint32 epoch) {
  if (store && store->valid()) {
    if (buffered) {
      if (store->pendingCount() >= 3600)
        flush();
      if (!store->stage(a, s, w, t, board, epoch))
        reportBufferState(QCoreApplication::translate("Allpowers", "Could not save history point"));
      else
        reportBufferState(bufferError);
    } else if (!store->record(a, s, w, t, board, epoch)) {
      emit problem(QCoreApplication::translate("Allpowers", "Could not save history point"));
    }
    load(a, w);
  }
  emit sampleFinished();
}
void HistoryWorker::load(QString a, qint64 now) {
  if (store && store->valid()) {
    if (a.startsWith("usb-board:"))
      emit undatedLoaded(a, store->undatedPoints(a));
    emit loaded(a, store->points(a, now), store->stats());
  }
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
  const auto data = QJsonDocument(settings).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    emit problem(QCoreApplication::translate("Allpowers", "Could not save settings") + ": " +
                 file.errorString());
}
