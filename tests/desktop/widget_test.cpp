#include "i18n.h"
#include "usb_transport.h"
#include "widget.h"
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QProcess>
#include <QScopeGuard>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QWindow>
#include <windows.h>
#include <QSemaphore>
#include <QMutexLocker>
#include <QDateTime>
#include <memory>
#include <thread>

struct DelayedHistoryState {
  QSemaphore entered, permits;
  QMutex mutex;
  HistoryPoints attempted;
};
class DelayedHistoryWorker : public HistoryWorker {
  std::shared_ptr<DelayedHistoryState> state;
  bool badDatabase;
public:
  DelayedHistoryWorker(std::shared_ptr<DelayedHistoryState> value,bool bad=false)
      : state(std::move(value)),badDatabase(bad) {}
  void open(const QString &path) override {
    HistoryWorker::open(badDatabase ? QFileInfo(path).absolutePath() : path);
  }
  void sample(QString address,ap_snapshot snapshot,qint64 wall,qint64 tick) override {
    HistoryPoint point;point.address=address;point.data=snapshot;point.wall=wall;point.tick=tick;
    { QMutexLocker lock(&state->mutex);state->attempted.append(point); }
    state->entered.release();
    // A finite failure bound keeps an assertion failure from hanging teardown.
    if(!state->permits.tryAcquire(1,10000)) {
      emit problem("Controlled worker was not released");emit sampleFinished();return;
    }
    HistoryWorker::sample(address,snapshot,wall,tick);
  }
};
class WidgetHistoryTransport : public TelemetryTransport {
  QString id;
  bool running=false;
public:
  using TelemetryTransport::TelemetryTransport;
  bool active() const override { return running; }
  bool connected() const override { return running; }
  bool scanning() const override { return false; }
  QString address() const override { return id; }
  QString phase() const override { return "synthetic"; }
  QJsonObject diagnostics() const override { return {{"bluetooth_used",false}}; }
  void scan() override {}
  void start(const QString &address,int) override {id=address;running=true;emit newSession();}
  void resume(const QString &address,const QString &,int model) override {start(address,model);}
  void stop() override {running=false;}
  void feed(quint32 soc) {
    ap_snapshot value{};value.abi_version=1;value.main_seen=value.fresh=1;value.known=1;value.soc=soc;
    emit snapshot(value);
  }
};

static COLORREF composedPixel(HWND window, QPoint logical, qreal scale) {
  POINT point{qRound(logical.x() * scale), qRound(logical.y() * scale)};
  ClientToScreen(window, &point);
  HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, 1, 1);
  const auto old = SelectObject(memory, bitmap);
  BitBlt(memory, 0, 0, 1, 1, screen, point.x, point.y, SRCCOPY | CAPTUREBLT);
  const auto color = GetPixel(memory, 0, 0);
  SelectObject(memory, old);
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  return color;
}
static bool unobscured(HWND window, QPoint logical, qreal scale) {
  POINT point{qRound(logical.x() * scale), qRound(logical.y() * scale)};
  ClientToScreen(window, &point);
  return GetAncestor(WindowFromPoint(point), GA_ROOT) == GetAncestor(window, GA_ROOT);
}

class CountingTransport : public TelemetryTransport {
public:
  int starts = 0;
  bool running = false, service = false;
  explicit CountingTransport(QObject *parent, bool remote = false)
      : TelemetryTransport(parent), service(remote) {}
  bool active() const override {
    return running;
  }
  bool connected() const override {
    return running;
  }
  bool scanning() const override {
    return false;
  }
  QString address() const override {
    return "synthetic";
  }
  QString phase() const override {
    return running ? "connected" : "idle";
  }
  QJsonObject diagnostics() const override {
    return {{"connection_attempts",starts},{"bluetooth_used",starts>0}};
  }
  bool remote() const override {
    return service;
  }
  void scan() override {}
  void start(const QString &, int) override {
    ++starts;
    running = true;
  }
  void resume(const QString &id, const QString &, int model) override {
    start(id, model);
  }
  void stop() override {
    running = false;
  }
};

class WidgetUsbChannel : public UsbChannel {
public:
  QByteArray writes;
  QString port;
  void open(const QString &value) override {
    port = value;
    emit opened();
  }
  void close() override {}
  void write(const QByteArray &value) override {
    writes += value;
  }
  void feed(const QByteArray &value) {
    emit received(value);
  }
};
static QByteArray widgetUsbStatus(const QString &board, int frames, int output = 150) {
  const QList<QJsonObject> rows = {
      {{"adapter", "UPSDEV"},
       {"version", "R"},
       {"fresh", true},
       {"gatt_connected", true},
       {"soc", 50},
       {"charge_w", 0},
       {"output_w", output},
       {"runtime_s", 1800},
       {"source", "battery"},
       {"source_raw", 65535},
       {"ac_output", true},
       {"usb_output", false},
       {"dc_output", false},
       {"valid_frames", frames},
       {"invalid_frames", 0}},
      {{"station_name", "AP R2500 V2.0"}, {"station_address", "02:00:00:00:00:03"}},
      {{"telemetry_known", 895}, {"protocol", 3}, {"ups_source_known", true}},
      {{"graph_live_latest_s", 180}},
      {{"history_ready", true},
       {"history_records", 2},
       {"history_latest", 2},
       {"history_oldest", 1},
       {"history_export_version", 2},
       {"history_session", 7},
       {"history_clock", false}},
      {{"board", board}}};
  QByteArray data;
  for (const auto &row : rows)
    data += QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
  return data;
}

class WidgetTests : public QObject {
  Q_OBJECT
private slots:
  void realControllerCoalescesSlowWorkerAcrossStationAndSession() {
    QTemporaryDir dir;
    const auto base=QDateTime::currentSecsSinceEpoch();qint64 now=base;
    auto state=std::make_shared<DelayedHistoryState>();
    WidgetHistoryTransport *transport=nullptr;
    WidgetRuntime runtime;
    runtime.makeHistoryWorker=[&]{return new DelayedHistoryWorker(state);};
    runtime.wallClock=[&]{return now;};runtime.elapsedClock=[&]{return (now-base)*1000;};
    auto widget=std::make_unique<WidgetController>(dir.path(),false,false,
      [&](const QString &,QObject *parent){return transport=new WidgetHistoryTransport(parent);},runtime);
    auto unblock=qScopeGuard([&]{state->permits.release(100);});
    QSignalSpy dispatched(widget.get(),&WidgetController::sample);
    transport->resume("station-A",{},3);transport->feed(10);
    QVERIFY(state->entered.tryAcquire(1,5000));
    for(int i=0;i<1000;++i){++now;transport->feed(quint32(i%100));}
    QCOMPARE(dispatched.size(),1);
    QVERIFY(widget->diagnostics()["history_worker_busy"].toBool());
    QVERIFY(widget->diagnostics()["history_sample_pending"].toBool());
    transport->resume("station-B",{},3);++now;transport->feed(77);
    QCOMPARE(dispatched.size(),1);
    state->permits.release();
    QTRY_COMPARE(dispatched.size(),2);
    QVERIFY(state->entered.tryAcquire(1,5000));
    QCOMPARE(dispatched.last()[0].toString(),QString("station-B"));
    QCOMPARE(qvariant_cast<ap_snapshot>(dispatched.last()[1]).soc,77u);
    ++now;transport->feed(80);
    QVERIFY(widget->diagnostics()["history_sample_pending"].toBool());
    emit transport->newSession();
    QVERIFY(!widget->diagnostics()["history_sample_pending"].toBool());
    state->permits.release();
    QTRY_VERIFY(!widget->diagnostics()["history_worker_busy"].toBool());
    QCOMPARE(dispatched.size(),2); // Old-session pending sample was cancelled.
    // A new session on the same station must retain its own SQLite provenance.
    ++now;transport->feed(88);
    QTRY_COMPARE(dispatched.size(),3);
    QVERIFY(state->entered.tryAcquire(1,5000));state->permits.release();
    QTRY_VERIFY(!widget->diagnostics()["history_worker_busy"].toBool());
    widget.reset();
    HistoryStore stored(dir.filePath("history.sqlite3"));QVERIFY(stored.valid());
    const auto a=stored.points("station-A",now), b=stored.points("station-B",now);
    QCOMPARE(a.size(),1);QCOMPARE(a.first().data.soc,10u);
    QCOMPARE(b.size(),2);QCOMPARE(b.first().data.soc,77u);QCOMPARE(b.last().data.soc,88u);
    QVERIFY(b.first().session!=b.last().session);
  }
  void realControllerCompletesInvalidDatabaseJobs() {
    QTemporaryDir dir;qint64 now=QDateTime::currentSecsSinceEpoch();
    auto state=std::make_shared<DelayedHistoryState>();state->permits.release(100);
    WidgetHistoryTransport *transport=nullptr;
    WidgetRuntime runtime;
    runtime.makeHistoryWorker=[&]{return new DelayedHistoryWorker(state,true);};
    runtime.wallClock=[&]{return now;};
    WidgetController widget(dir.path(),false,false,
      [&](const QString &,QObject *parent){return transport=new WidgetHistoryTransport(parent);},runtime);
    QSignalSpy dispatched(&widget,&WidgetController::sample);
    transport->resume("station-A",{},3);transport->feed(10);
    QTRY_VERIFY(!widget.diagnostics()["history_worker_busy"].toBool());
    QVERIFY(!widget.diagnostics()["history_error"].toString().isEmpty());
    ++now;transport->feed(20);
    QTRY_COMPARE(dispatched.size(),2);
    QTRY_VERIFY(!widget.diagnostics()["history_worker_busy"].toBool());
  }
  void realControllerShutdownDrainsLatestWithControlledWorker() {
    QTemporaryDir dir;qint64 now=QDateTime::currentSecsSinceEpoch();
    auto state=std::make_shared<DelayedHistoryState>();
    WidgetHistoryTransport *transport=nullptr;
    WidgetRuntime runtime;
    runtime.makeHistoryWorker=[&]{return new DelayedHistoryWorker(state);};runtime.wallClock=[&]{return now;};
    auto widget=std::make_unique<WidgetController>(dir.path(),false,false,
      [&](const QString &,QObject *parent){return transport=new WidgetHistoryTransport(parent);},runtime);
    auto unblock=qScopeGuard([&]{state->permits.release(100);});
    QSignalSpy dispatched(widget.get(),&WidgetController::sample);
    transport->resume("station-A",{},3);transport->feed(10);
    QVERIFY(state->entered.tryAcquire(1,5000));
    for(int i=0;i<1000;++i){++now;transport->feed(quint32(i%100));}
    QCOMPARE(dispatched.size(),1);
    QSemaphore start;
    std::thread release([&]{start.acquire();state->permits.release(2);});
    QElapsedTimer shutdown;shutdown.start();start.release();widget.reset();release.join();
    QVERIFY(shutdown.elapsed()<5000);
    {QMutexLocker lock(&state->mutex);QCOMPARE(state->attempted.size(),2);
      QCOMPARE(state->attempted.last().data.soc,99u);}
    HistoryStore stored(dir.filePath("history.sqlite3"));QVERIFY(stored.valid());
    const auto points=stored.points("station-A",now);
    QCOMPARE(points.size(),2);QCOMPARE(points.last().data.soc,99u);
  }
  void activitySurvivesStopAndTransportSwitch() {
    TransportRouter router(nullptr,false,false,"bluetooth",
      [](const QString &,QObject *parent){return new CountingTransport(parent);},
      "ALLPOWERS.Test.Activity."+QUuid::createUuid().toString());
    QVERIFY(!router.diagnostics()["bluetooth_used"].toBool());
    router.resume("synthetic",{},3);
    router.stop();
    QVERIFY(router.diagnostics()["bluetooth_used"].toBool());
    QCOMPARE(router.diagnostics()["connection_attempts"].toInteger(),1);
    router.setMode("usb");
    QVERIFY(router.diagnostics()["bluetooth_used"].toBool());
    QCOMPARE(router.diagnostics()["connection_attempts"].toInteger(),1);
  }
  void recentGraphUpdatesRemainVisible() {
    WidgetCard card(false);
    const qint64 now = 100000;
    HistoryPoints points;
    for (int age : {3, 2, 1}) {
      HistoryPoint point;
      point.address = "synthetic";
      point.session = "live";
      point.wall = now - age;
      point.tick = now - age;
      point.data.fresh = 1;
      point.data.known = 4;
      point.data.output_w = age == 2 ? 100 : 60;
      points.append(point);
    }
    auto outputAt = [&](const QImage &image, int age, int y) {
      const int x = qRound(14 + WidgetCard::graphPosition(age) * 272);
      for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy) {
          const auto color = image.pixelColor(x + dx, y + dy);
          if (color.red() > 180 && color.green() > 190 && color.blue() > 200)
            return true;
        }
      return false;
    };
    card.setHistory(points, now);
    const auto before = card.renderedImage();
    QVERIFY(outputAt(before, 2, 127));
    card.setHistory(points, now + 1);
    QCOMPARE(card.renderedImage(), before); // No one-second position jump on receipt.
    QTest::qWait(400);
    const auto moving = card.renderedImage();
    QVERIFY(moving != before);
    QTest::qWait(600);
    const auto after = card.renderedImage();
    QVERIFY(after != moving);
    QVERIFY(outputAt(after, 3, 127));
    QVERIFY(!outputAt(after, 2, 127)); // A visible move each second, not 0.075 px.
    QVERIFY(after.copy(14, 126, 272, 54) != before.copy(14, 126, 272, 54));
    card.setHistory({points.last()}, now);
    QVERIFY(!outputAt(card.renderedImage(), 1, 127)); // No endpoint dot for an isolated sample.
    card.setHistory({points.last()}, now + 4);
    QVERIFY(!outputAt(card.renderedImage(), 5, 127)); // No fake live marker on stale history.
  }
  void usbArchiveFillsWidgetGraph_data() {
    QTest::addColumn<QString>("board");
    QTest::newRow("waveshare") << QString("waveshare-147");
    QTest::newRow("pocket") << QString("pocket-096");
  }
  void usbArchiveFillsWidgetGraph() {
    QFETCH(QString, board);
    QTemporaryDir temporary;
    const auto now = QDateTime::currentSecsSinceEpoch();
    HistoryPoint cached;
    cached.address = "usb-board:R2500-000000000001";
    cached.session = "cached-boot";
    cached.wall = now - 300;
    cached.tick = 10;
    cached.stride = 60;
    cached.data.fresh = 1;
    cached.data.known = 7;
    cached.data.soc = 50;
    cached.data.output_w = 100;
    auto other = cached;
    other.address = "usb-board:R2500-000000000002";
    other.wall = now - 600;
    {
      HistoryStore store(temporary.filePath("history.sqlite3"));
      QVERIFY(store.importPoints({cached, other}, now));
    }
    QFile settings(temporary.filePath("settings.json"));
    QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write("{\"transport\":\"usb\",\"endpoint\":\"R2500-000000000001\"}");
    settings.close();
    WidgetUsbChannel *io = nullptr;
    UsbTransport *usb = nullptr;
    WidgetController widget(temporary.path(), false, false,
                            [&](const QString &mode, QObject *parent) -> TelemetryTransport * {
                              if (mode != "usb")
                                return new CountingTransport(parent);
                              io = new WidgetUsbChannel;
                              usb = new UsbTransport(parent, false, io, [] {
                                return QList<UsbPort>{
                                    {"R2500-000000000001", "COM987", "Synthetic board"},
                                    {"R2500-000000000002", "COM988", "Other synthetic board"}};
                              });
                              return usb;
                            });
    QVERIFY(usb);
    usb->resume("R2500-000000000001", {}, 0);
    io->feed(widgetUsbStatus(board, 10));
    QTRY_COMPARE(widget.diagnostics()["usb_history_points"].toInt(), 1);
    QVERIFY(!widget.diagnostics()["bluetooth_used"].toBool());
    auto has = [&](const HistoryPoint &point) {
      for (const auto &shown : widget.card()->historyPoints())
        if (shown.address == point.address && shown.wall == point.wall)
          return true;
      return false;
    };
    QTRY_VERIFY(has(cached)); // Cached archive appears before the USB dump completes.
    QVERIFY(!has(other));
    QTRY_VERIFY(io->writes.contains("HISTORY-EXPORT 1 2\n"));
    const QByteArray dump =
        "{\"history_record\":true,\"sequence\":1,\"session\":7,\"uptime_s\":100,\"epoch\":0,"
        "\"soc\":50,\"charge_w\":0,\"output_w\":150,\"runtime_minutes\":30,\"flags\":13,"
        "\"crc\":3397945926}\n"
        "{\"history_record\":true,\"sequence\":2,\"session\":7,\"uptime_s\":160,\"epoch\":0,"
        "\"soc\":50,\"charge_w\":0,\"output_w\":150,\"runtime_minutes\":30,\"flags\":13,"
        "\"crc\":874804455}\n{\"history_dump_done\":true}\n";
    io->feed(dump);
    QTRY_COMPARE(widget.diagnostics()["usb_history_points"].toInt(), 3);
    QVERIFY(widget.diagnostics()["history_error"].toString().isEmpty());
    QTRY_VERIFY(widget.diagnostics()["output_segments"].toInt() > 0);
    const auto &points = widget.card()->historyPoints();
    QVERIFY(points.size() >= 3);
    QVERIFY(joinPoints(points[points.size() - 2], points.last()));
    QVERIFY(!joinPoints(points.first(), points[1])); // The genuine older-boot gap remains.
    // After importing history, live data must still reach both the graph model
    // and rendered graph pixels, rather than merely updating the numeric label.
    auto graphImage = widget.card()->renderedImage().copy(14, 126, 272, 54);
    auto previousWall = widget.card()->historyPoints().last().wall;
    for (int power : {300, 70}) {
      io->feed(widgetUsbStatus(board, power, power));
      QTRY_COMPARE(widget.card()->historyPoints().last().data.output_w, quint32(power));
      QVERIFY(widget.card()->historyPoints().last().wall > previousWall);
      const auto nextImage = widget.card()->renderedImage().copy(14, 126, 272, 54);
      QVERIFY(nextImage != graphImage);
      graphImage = nextImage;
      previousWall = widget.card()->historyPoints().last().wall;
    }
    // Reconnection re-reads history automatically and deduplicates retained records.
    io->writes.clear();
    usb->resume("R2500-000000000001", {}, 0);
    io->feed(widgetUsbStatus(board, 11));
    QTRY_VERIFY(io->writes.contains("HISTORY-EXPORT 1 2\n"));
    io->feed(dump);
    QTRY_COMPARE(widget.diagnostics()["usb_history_points"].toInt(), 3);
    usb->resume("R2500-000000000002", {}, 0);
    io->feed(widgetUsbStatus(board, 12));
    QTRY_COMPARE(widget.diagnostics()["usb_history_points"].toInt(), 1);
    QTRY_VERIFY(has(other));
    QVERIFY(!has(cached));
    QVERIFY(widget.diagnostics()["history_error"].toString().isEmpty());
    QVERIFY(!io->writes.contains("BOOT"));
    QVERIFY(!io->writes.contains("SELECT"));
    QVERIFY(!io->writes.contains("TIME "));
    auto *router = widget.findChild<TransportRouter *>();
    QVERIFY(router);
    router->setMode("bluetooth");
    QTRY_COMPARE(widget.diagnostics()["usb_history_points"].toInt(), 0);
    QVERIFY(!has(other));
  }
  void remainingTimeUsesDays() {
    ap_snapshot value{};
    QCOMPARE(WidgetCard::remainingTime(value), QStringLiteral("—"));
    value.known = 8;
    for (const auto &row : QList<QPair<quint32, QString>>{{0, "0 мин"},
                                                          {3599, "59 мин"},
                                                          {3600, "1 ч 0 мин"},
                                                          {86399, "23 ч 59 мин"},
                                                          {86400, "1 д 0 ч"},
                                                          {90000, "1 д 1 ч"},
                                                          {3 * 86400 + 5 * 3600, "3 д 5 ч"}}) {
      value.runtime_s = row.first;
      QCOMPARE(WidgetCard::remainingTime(value), row.second);
    }
    WidgetCard card(false);
    card.setDeviceName("AP R2500 V2.0");
    card.setTelemetry(value, QStringLiteral("USB · получение данных"));
    QVERIFY(card.accessibleName().startsWith("AP R2500 V2.0"));
  }
  void repeatedSettingsActivationReusesDialog() {
    QTemporaryDir temporary;
    WidgetController widget(temporary.path(), true, false);
    int visibleDialogs = -1;
    QTimer::singleShot(0, &widget, [&] {
      QTimer::singleShot(0, &widget, [&] {
        visibleDialogs = 0;
        for (auto *window : QApplication::topLevelWidgets())
          if (window->objectName() == "widgetSettings" && window->isVisible()) {
            ++visibleDialogs;
            qobject_cast<QDialog *>(window)->reject();
          }
      });
      QMetaObject::invokeMethod(&widget, "configure", Qt::DirectConnection);
    });
    QVERIFY(QMetaObject::invokeMethod(&widget, "configure", Qt::DirectConnection));
    QCOMPARE(visibleDialogs, 1);
  }
  void optionalStartupPreservesOtherApplications() {
    QTemporaryDir temporary;
    QSettings store(temporary.filePath("startup.ini"), QSettings::IniFormat);
    store.setValue("AnotherApplication", "unchanged");
    QVERIFY(!store.contains("AllpowersWidget"));
    QVERIFY(setWidgetStartup(store, true, "C:/Program Files/ALLPOWERS/AllpowersWidget.exe"));
    QCOMPARE(store.value("AllpowersWidget").toString(),
             QString("\"C:\\Program Files\\ALLPOWERS\\AllpowersWidget.exe\""));
    QVERIFY(setWidgetStartup(store, false, {}));
    QVERIFY(!store.contains("AllpowersWidget"));
    QCOMPARE(store.value("AnotherApplication").toString(), QString("unchanged"));
  }
  void ownershipBlocksBeforeTransportStart() {
    const auto key = "ALLPOWERS.Test." + QUuid::createUuid().toString(QUuid::Id128);
    CountingTransport *first = nullptr, *second = nullptr;
    TransportRouter a(
        nullptr, true, false, "bluetooth",
        [&](auto, QObject *p) { return first = new CountingTransport(p); }, key);
    TransportRouter b(
        nullptr, true, false, "usb",
        [&](auto, QObject *p) { return second = new CountingTransport(p); }, key);
    a.resume("synthetic", {}, 0);
    QCOMPARE(first->starts, 1);
    b.resume("synthetic", {}, 0);
    QCOMPARE(second->starts, 0);
    QVERIFY(b.phase().contains(QStringLiteral("занято")));
    a.stop();
    b.resume("synthetic", {}, 0);
    QCOMPARE(second->starts, 1);
    b.setMode("bluetooth");
    a.resume("synthetic", {}, 0);
    QCOMPARE(first->starts, 2);
  }
  void serviceViewerDoesNotClaimDirectConnection() {
    const auto key = "ALLPOWERS.Test." + QUuid::createUuid().toString(QUuid::Id128);
    ConnectionLease occupied(key);
    QVERIFY(occupied.acquire());
    CountingTransport *fake = nullptr;
    TransportRouter service(
        nullptr, true, true, "bluetooth",
        [&](auto, QObject *p) { return fake = new CountingTransport(p, true); }, key);
    service.resume("synthetic", {}, 0);
    QCOMPARE(fake->starts, 1);
    QVERIFY(!service.phase().contains(QStringLiteral("занято")));
  }
  void ownershipAcrossProcessAndCrash() {
    const auto key = "ALLPOWERS.Test." + QUuid::createUuid().toString(QUuid::Id128);
    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    child.setArguments({"--lease-child", key});
    child.start();
    QVERIFY(child.waitForStarted());
    QVERIFY(child.waitForReadyRead(5000));
    QCOMPARE(child.readAllStandardOutput().trimmed(), QByteArray("owned"));
    ConnectionLease lease(key);
    QVERIFY(!lease.acquire());
    child.kill();
    QVERIFY(child.waitForFinished());
    QVERIFY(lease.acquire());
    lease.release();
    ConnectionLease next(key);
    QVERIFY(next.acquire());
  }
  void unknownValuesAndGaps() {
    ap_snapshot value{};
    QCOMPARE(WidgetCard::reading(value, 1, 0, "%"), QStringLiteral("—"));
    value.known = 15;
    value.fresh = true;
    QCOMPARE(WidgetCard::reading(value, 1, 0, "%"), QString("0%"));
    HistoryPoints points;
    for (int i = 0; i < 5; ++i)
      points.append({"synthetic", "session", 100 + i, i, value, 1});
    QCOMPARE(WidgetCard::graphSegments(points, 2, 104), 4);
    points[2].data.fresh = false;
    QCOMPARE(WidgetCard::graphSegments(points, 2, 104), 2);
    points[2].data.fresh = true;
    points[2].data.known &= ~2u;
    QCOMPARE(WidgetCard::graphSegments(points, 2, 104), 2);
    points[2].data.known |= 2u;
    points[3].session = "another";
    QCOMPARE(WidgetCard::graphSegments(points, 2, 104), 2);
    QCOMPARE(WidgetCard::graphSegments(points, 2, 4000), 0);
    WidgetCard card(false);
    value.fresh = false;
    card.setTelemetry(value, QStringLiteral("Нет связи"));
    QVERIFY(card.accessibleDescription().contains(QStringLiteral("Данные устарели")));
    value.fresh = true;
    card.setTelemetry(value, QStringLiteral("Подключено"));
    QVERIFY(!card.accessibleDescription().contains(QStringLiteral("устарели")));
  }
  void nativeHostRecovery() {
    if (QGuiApplication::platformName() != "windows")
      QSKIP("Requires native Windows platform");
    HWND host = CreateWindowExW(0, L"STATIC", L"Synthetic desktop host", WS_POPUP, -2000, -2000,
                                800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    QVERIFY(host);
    DesktopSurface surface(true, [&] { return reinterpret_cast<quintptr>(host); });
    surface.setFixedSize(340, 260);
    surface.setDesktopPosition({}, {999999, 999999});
    QVERIFY(surface.attached());
    QVERIFY(surface.nativeDiagnostics()["native_child"].toBool());
    QVERIFY(surface.desktopOffset().x() < 999999);
    DestroyWindow(host);
    host = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT(!surface.attached(), 4000);
    host = CreateWindowExW(0, L"STATIC", L"Synthetic desktop host", WS_POPUP, -2000, -2000, 800,
                           600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    QVERIFY(host);
    QTRY_VERIFY_WITH_TIMEOUT(surface.attached(), 4000);
    QVERIFY(surface.nativeDiagnostics()["native_parent_matches"].toBool());
    DestroyWindow(host);
    host = nullptr;
  }
  void realDesktopComposition() {
    if (QGuiApplication::platformName() != "windows" ||
        !qEnvironmentVariableIsSet("ALLPOWERS_DESKTOP_SMOKE"))
      QSKIP("Explicit desktop smoke test only");
    WidgetCard card(true);
    card.setDesktopPosition({}, {24, 24});
    ap_snapshot value{};
    value.fresh = true;
    value.known = 15;
    value.soc = 74;
    card.setTelemetry(value, QStringLiteral("Демонстрационные данные"));
    QVERIFY(card.attached());
    QVERIFY(card.nativeDiagnostics()["native_frame_published"].toBool());
    QTest::qWait(500);
    auto window = reinterpret_cast<HWND>(card.nativeHandle());
    if (unobscured(window, {8, 130}, card.desktopScale()))
      QCOMPARE(composedPixel(window, {8, 130}, card.desktopScale()), RGB(16, 27, 45));
    else
      qInfo()
          << "Desktop pixel is covered by another application; checking publication and geometry";
    RECT expected{};
    GetWindowRect(window, &expected);
    for (int i = 0; i < 20; ++i) {
      value.soc = 50 + i;
      card.setTelemetry(value, QStringLiteral("Демонстрационные данные"));
      QTest::qWait(100);
      RECT current{};
      GetWindowRect(window, &current);
      QCOMPARE(current.left, expected.left);
      QCOMPARE(current.top, expected.top);
      QCOMPARE(current.right, expected.right);
      QCOMPARE(current.bottom, expected.bottom);
      QVERIFY(IsWindowVisible(window));
    }
  }
  void nativeCardPaintAndDrag() {
    if (QGuiApplication::platformName() != "windows")
      QSKIP("Requires native Windows platform");
    const auto origin = QGuiApplication::primaryScreen()->geometry().topLeft();
    HWND host = CreateWindowExW(WS_EX_LAYERED, L"STATIC", L"ALLPOWERS rendering test",
                                WS_POPUP | WS_CLIPCHILDREN, origin.x() + 31, origin.y() + 47, 1200,
                                900, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    QVERIFY(host);
    const auto closeHost = qScopeGuard([&] { DestroyWindow(host); });
    QVERIFY(SetLayeredWindowAttributes(host, 0, 255, LWA_ALPHA));
    // Test runners can supply STARTF_USESHOWWINDOW/SW_HIDE. ShowWindow's first
    // call would honor that instead of our argument, making pixel tests blind.
    SetWindowPos(host, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    QVERIFY(IsWindowVisible(host));
    WidgetCard surface(true, [&] { return reinterpret_cast<quintptr>(host); });
    surface.setDesktopPosition({}, {80, 80});
    ap_snapshot value{};
    value.known = 15;
    value.fresh = true;
    value.soc = 74;
    surface.setTelemetry(value, QStringLiteral("Synthetic telemetry"));
    QVERIFY(surface.attached());
    // The displayed HWND must be independent of Qt's hidden content widget.
    QVERIFY(!surface.windowHandle());
    QVERIFY(surface.nativeDiagnostics()["native_frame_published"].toBool());
    QTest::qWait(500);
    HWND child = reinterpret_cast<HWND>(surface.nativeHandle());
    // Match Explorer's composition mode and sample the actual screen output.
    // The child's GDI DC can look correct while DWM displays a black rectangle.
    HDC dc = GetDC(nullptr);
    QVERIFY(dc);
    const qreal scale = surface.desktopScale();
    POINT samplePoint{qRound(8 * scale), qRound(130 * scale)};
    ClientToScreen(child, &samplePoint);
    HDC capture = CreateCompatibleDC(dc);
    HBITMAP pixel = CreateCompatibleBitmap(dc, 1, 1);
    HGDIOBJ previous = SelectObject(capture, pixel);
    BitBlt(capture, 0, 0, 1, 1, dc, samplePoint.x, samplePoint.y, SRCCOPY | CAPTUREBLT);
    const COLORREF background = GetPixel(capture, 0, 0);
    SelectObject(capture, previous);
    DeleteObject(pixel);
    DeleteDC(capture);
    ReleaseDC(nullptr, dc);
    qInfo() << "Composed background:" << background;
    if (unobscured(child, {8, 130}, scale))
      QCOMPARE(background, RGB(16, 27, 45));
    else
      qInfo() << "Test pixel is covered by another application; checking publication and geometry";
    // A host offset must not accumulate on each bitmap update.
    RECT stationary{};
    GetWindowRect(child, &stationary);
    for (int i = 0; i < 20; ++i) {
      value.soc = 50 + i;
      surface.setTelemetry(value, QStringLiteral("Repeated update"));
      RECT current{};
      GetWindowRect(child, &current);
      QCOMPARE(current.left, stationary.left);
      QCOMPARE(current.top, stationary.top);
      QCOMPARE(current.right, stationary.right);
      QCOMPARE(current.bottom, stationary.bottom);
      QVERIFY(IsWindowVisible(child));
    }
    auto mouse = [&](QEvent::Type type, QPointF local, QPointF global, Qt::MouseButton button,
                     Qt::MouseButtons buttons) {
      QMouseEvent event(type, local, global, button, buttons, Qt::NoModifier);
      QCoreApplication::sendEvent(&surface, &event);
      QCoreApplication::processEvents();
    };
    const QPoint local(120, 110); // the body, not just the header
    const QPoint global = origin + surface.desktopOffset() + local;
    const QPoint before = surface.desktopOffset();
    RECT beforeRect{};
    GetWindowRect(child, &beforeRect);
    QSignalSpy moved(&surface, &WidgetCard::positionChanged);
    mouse(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, local + QPoint(60, 40), global + QPoint(60, 40), Qt::NoButton,
          Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, local, global + QPoint(60, 40), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(surface.desktopOffset(), before + QPoint(60, 40));
    QCOMPARE(moved.count(), 1);
    RECT afterRect{};
    GetWindowRect(child, &afterRect);
    QVERIFY(afterRect.left > beforeRect.left);
    QVERIFY(afterRect.top > beforeRect.top);
    const auto beforeNative = surface.desktopOffset();
    auto nativePoint = [&](QPoint p) {
      return MAKELPARAM(qRound(p.x() * scale), qRound(p.y() * scale));
    };
    SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, nativePoint(local));
    SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, nativePoint(local + QPoint(20, 10)));
    SendMessageW(child, WM_LBUTTONUP, 0, nativePoint(local));
    QCOMPARE(surface.desktopOffset(), beforeNative + QPoint(20, 10));
    surface.setLocked(true);
    const QPoint locked = surface.desktopOffset();
    mouse(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, local + QPoint(20, 20), global + QPoint(20, 20), Qt::NoButton,
          Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(surface.desktopOffset(), locked);
    QTest::qWait(1600); // the host health check must not reset position
    QCOMPARE(surface.desktopOffset(), locked);
  }
  void independentSettingsAndDemo() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString monitor = temporary.filePath("monitor.json");
    QFile sentinel(monitor);
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    sentinel.write("unchanged");
    sentinel.close();
    const QString folder = temporary.filePath("widget");
    {
      WidgetController widget(folder, true, false);
      widget.start();
      widget.loadDemo();
      QTest::qWait(1200);
      QVERIFY(widget.diagnostics()["history_error"].toString().isEmpty());
      QCOMPARE(widget.diagnostics()["points"].toInt(), 3601);
      QCOMPARE(widget.card()->size(), QSize(300, 210));
      QVERIFY(!widget.diagnostics()["bluetooth_used"].toBool());
    }
    QVERIFY(QFile::exists(folder + "/history.sqlite3"));
    QFile settings(folder + "/settings.json");
    QVERIFY(settings.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(settings.readAll()).object();
    QCOMPARE(saved["x"].toInt(), 24);
    QVERIFY(sentinel.open(QIODevice::ReadOnly));
    QCOMPARE(sentinel.readAll(), QByteArray("unchanged"));
    {
      WidgetController reopened(folder, true, false);
      reopened.start();
    }
  }
};

int main(int argc, char **argv) {
  if (argc == 3 && QByteArray(argv[1]) == "--lease-child") {
    ConnectionLease lease(QString::fromLocal8Bit(argv[2]));
    if (!lease.acquire())
      return 2;
    QFile output;
    output.open(stdout, QIODevice::WriteOnly);
    output.write("owned\n");
    output.flush();
    Sleep(30000);
    return 0;
  }
  QApplication application(argc, argv);
  UiLanguage language("ru");
  WidgetTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "widget_test.moc"
