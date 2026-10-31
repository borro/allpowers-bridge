#include "i18n.h"
#include "usb_transport.h"
#include "widget.h"
#include "archive_fixture.h"
#include <QApplication>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
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
#include <cstring>
#include <algorithm>
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
       {"history_export_version", 3}, {"history_nonce_bits", 128}, {"history_generation", 10},
       {"history_session", 7},
       {"history_clock", false}},
      {{"board", board}}};
  QByteArray data;
  for (const auto &row : rows)
    data += QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
  return data;
}

// Count native allocations without exposing renderer internals. Only this EXE's
// CreateDIBSection import is replaced, and the test restores it before pumping events.
class DibAllocationProbe {
  using Create = decltype(&::CreateDIBSection);
  inline static Create original = nullptr;
  inline static int allocations = 0;
  inline static void *latestPixels = nullptr;
  inline static bool failNextAllocation = false;
  void **slot = nullptr;
  static HBITMAP WINAPI counted(HDC dc, const BITMAPINFO *info, UINT usage, void **bits,
                                HANDLE section, DWORD offset) {
    ++allocations;
    if (failNextAllocation) {
      failNextAllocation = false;
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    const auto bitmap = original(dc, info, usage, bits, section, offset);
    if (bitmap) latestPixels = *bits;
    return bitmap;
  }
  bool replace(void *value) {
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
    *slot = value;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    return true;
  }
public:
  DibAllocationProbe() {
    auto *base = reinterpret_cast<uchar *>(GetModuleHandleW(nullptr));
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto *entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);
    for (; entry->Name && !slot; ++entry) {
      if (!entry->OriginalFirstThunk) continue;
      auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + entry->OriginalFirstThunk);
      auto *addresses = reinterpret_cast<IMAGE_THUNK_DATA *>(base + entry->FirstThunk);
      for (; names->u1.AddressOfData; ++names, ++addresses) {
        if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
        auto *name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
        if (strcmp(reinterpret_cast<const char *>(name->Name), "CreateDIBSection")) continue;
        slot = reinterpret_cast<void **>(&addresses->u1.Function);
        original = reinterpret_cast<Create>(*slot);
        allocations = 0;
        latestPixels = nullptr;
        failNextAllocation = false;
        if (!replace(reinterpret_cast<void *>(&counted))) slot = nullptr;
        break;
      }
    }
  }
  ~DibAllocationProbe() { if (slot) replace(reinterpret_cast<void *>(original)); }
  DibAllocationProbe(const DibAllocationProbe &) = delete;
  DibAllocationProbe &operator=(const DibAllocationProbe &) = delete;
  bool active() const { return slot != nullptr; }
  int count() const { return allocations; }
  const QRgb *pixels() const { return static_cast<const QRgb *>(latestPixels); }
  void failNext() { failNextAllocation = true; }
};

class WidgetTests : public QObject {
  Q_OBJECT
private slots:
  void settingsAcceptAndCancelPreserveConnectionChoices() {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"transport\":\"usb\",\"endpoint\":\"R2500-000000000001\","
               "\"name\":\"Synthetic USB\",\"profile\":0,\"ui_language\":\"en\"}"); file.close();
    WidgetController widget(dir.path(), true, false);
    auto read = [&] {
      QFile saved(path); if (!saved.open(QIODevice::ReadOnly)) return QJsonObject{};
      return QJsonDocument::fromJson(saved.readAll()).object();
    };
    auto configure = [&](bool accept, bool changeLanguage) {
      bool visited = false;
      QTimer::singleShot(0, &widget, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        QTimer::singleShot(0, dialog, &QDialog::reject); // close even after an assertion fails
        QComboBox *profile = nullptr, *language = nullptr;
        for (auto *combo : dialog->findChildren<QComboBox *>()) {
          if (combo->findText("R2500 V2") >= 0) profile = combo;
          if (combo->findData("zh-TW") >= 0) language = combo;
        }
        QVERIFY(profile && language);
        for (auto *check : dialog->findChildren<QCheckBox *>()) QVERIFY(!check->isEnabled());
        if (changeLanguage) language->setCurrentIndex(language->findData("ru"));
        else profile->setCurrentIndex(3);
        visited = true;
        if (accept) dialog->accept(); else dialog->reject();
      });
      QVERIFY(QMetaObject::invokeMethod(&widget, "configure", Qt::DirectConnection));
      QVERIFY(visited);
    };
    configure(false, false); QCOMPARE(read()["profile"].toInt(), 0);
    configure(true, false); QCOMPARE(read()["profile"].toInt(), 3);
    QCOMPARE(read()["endpoint"].toString(), QString("R2500-000000000001"));
    configure(true, true); QCOMPARE(read()["ui_language"].toString(), QString("ru"));
    QCOMPARE(read()["profile"].toInt(), 3);
    QVERIFY(!widget.diagnostics()["bluetooth_used"].toBool());
  }
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
  void controllerDisplaysBufferedHistoryAndFlushesOnExit() {
    QTemporaryDir dir;
    const qint64 base = QDateTime::currentSecsSinceEpoch();
    qint64 now = base;
    WidgetHistoryTransport *transport = nullptr;
    WidgetRuntime runtime;
    runtime.wallClock = [&] { return now; };
    runtime.elapsedClock = [&] { return (now - base) * 1000; };
    auto widget = std::make_unique<WidgetController>(
        dir.path(), false, false,
        [&](const QString &, QObject *parent) {
          return transport = new WidgetHistoryTransport(parent);
        }, runtime);
    QTRY_VERIFY(widget->diagnostics()["history_ready"].toBool());
    transport->resume("buffered-station", {}, 3);
    for (int i = 0; i < 5; ++i) {
      now = base + i;
      transport->feed(10 + i);
      QTRY_COMPARE(widget->diagnostics()["history_buffered_samples"].toInt(), i + 1);
      QTRY_COMPARE(widget->diagnostics()["points"].toInt(), i + 1);
    }
    {
      HistoryStore disk(dir.filePath("history.sqlite3"));
      QVERIFY(disk.valid());
      QVERIFY(disk.points("buffered-station", now).isEmpty());
    }
    widget.reset(); // Exit must commit the complete tail before the worker stops.
    HistoryStore disk(dir.filePath("history.sqlite3"));
    const auto points = disk.points("buffered-station", now);
    QCOMPARE(points.size(), 5);
    for (int i = 0; i < points.size(); ++i) {
      QCOMPARE(points[i].wall, base + i);
      QCOMPARE(points[i].data.soc, quint32(10 + i));
    }
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
  void staticLayerCacheFollowsTelemetryAndLanguage() {
    initializeUiFonts();
    WidgetCard card(false);
    ap_snapshot value{};
    QString name = "Synthetic station", phase = QCoreApplication::translate("Allpowers", "No connection");
    card.setTelemetry(value, phase);
    QCOMPARE(card.toolTip(), phase);
    QVERIFY(!card.accessibleName().isEmpty());
    constexpr qint64 now = 100000;
    HistoryPoint point;
    point.address = "synthetic"; point.session = "live";
    point.wall = point.tick = now - 1;
    point.data.fresh = 1; point.data.known = 6;
    point.data.input_w = 100; point.data.output_w = 40;
    const HistoryPoints points{point};
    value.fresh = 1; value.known = 15; value.soc = 74;
    value.input_w = 80; value.output_w = 40; value.runtime_s = 7200;
    auto freshImage = [&] {
      WidgetCard fresh(false);
      fresh.setDeviceName(name); fresh.setTelemetry(value, phase); fresh.setHistory(points, now);
      return fresh.renderedImage();
    };
    card.setDeviceName(name); card.setTelemetry(value, phase); card.setHistory(points, now);
    QCOMPARE(card.renderedImage(), freshImage());
    const std::array<std::function<void()>, 9> changes{
      [&] { value.soc = 31; }, [&] { value.input_w = 120; },
      [&] { value.output_w = 65; }, [&] { value.runtime_s = 10800; },
      [&] { value.fresh = 0; }, [&] { value.known &= ~quint32(4); },
      [&] { value.known = 0; }, [&] { value.known = 15; value.fresh = 1; },
      [&] { name = QStringLiteral("Очень длинное название станции — test long station name"); }
    };
    for (const auto &change : changes) {
      change(); card.setDeviceName(name); card.setTelemetry(value, phase);
      QCOMPARE(card.renderedImage(), freshImage());
    }
    const auto beforeMetadata = card.renderedImage();
    value.main_age_ms = 1234; value.valid_frames = 123; phase = "Updated status";
    card.setTelemetry(value, phase);
    QCOMPARE(card.renderedImage(), beforeMetadata);
    QCOMPARE(card.toolTip(), phase);
    QCOMPARE(card.accessibleDescription(), phase);
    for (const auto &language : {"en", "ru", "zh-CN", "zh-TW"}) {
      UiLanguage selected(language);
      QVERIFY(selected.valid());
      QCoreApplication::processEvents();
      card.setTelemetry(value, phase);
      QCOMPARE(card.renderedImage(), freshImage());
    }
  }
  void staticLayerMaximumFollowsAnimation() {
    WidgetCard card(false);
    HistoryPoint point;
    point.address = "synthetic"; point.session = "live";
    point.wall = point.tick = 99999;
    point.data.known = 2; point.data.fresh = 1; point.data.input_w = 100;
    card.setHistory({point}, 100000);
    auto axis = [](const QImage &image) {
      const auto s = image.devicePixelRatio();
      return image.copy(qRound(14*s), qRound(108*s), qRound(180*s), qRound(16*s));
    };
    const auto before = axis(card.renderedImage());
    point.data.input_w = 200;
    card.setHistory({point}, 100001);
    QCOMPARE(axis(card.renderedImage()), before);
    auto *animation = card.findChild<QVariantAnimation *>();
    QVERIFY(animation);
    animation->setCurrentTime(animation->duration()/2);
    WidgetCard middle(false);
    auto midPoint = point; midPoint.data.input_w = 150;
    middle.setHistory({midPoint}, 100001);
    QCOMPARE(axis(card.renderedImage()), axis(middle.renderedImage()));
    animation->setCurrentTime(animation->duration());
    WidgetCard finished(false); finished.setHistory({point}, 100001);
    QCOMPARE(axis(card.renderedImage()), axis(finished.renderedImage()));
    QVERIFY(axis(card.renderedImage()) != before);
  }
  void unchangedControllerUpdatesKeepMergedHistory() {
    struct MemoryHistory : HistoryWorker {
      void open(const QString &) override { emit opened(); }
      void sample(QString, ap_snapshot, qint64, qint64) override { emit sampleFinished(); }
    };
    QTemporaryDir directory;
    QFile settings(directory.filePath("settings.json")); QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write("{\"endpoint\":\"synthetic\",\"name\":\"Synthetic\"}"); settings.close();
    qint64 now = 100000;
    MemoryHistory *worker = nullptr;
    WidgetHistoryTransport *transport = nullptr;
    WidgetRuntime runtime;
    runtime.wallClock = [&] { return now; }; runtime.elapsedClock = [] { return qint64(0); };
    runtime.makeHistoryWorker = [&] { return worker = new MemoryHistory; };
    WidgetController widget(directory.path(), false, false,
      [&](const QString &, QObject *parent) { return transport = new WidgetHistoryTransport(parent); }, runtime);
    widget.start();
    QTRY_VERIFY(widget.diagnostics()["history_ready"].toBool());
    transport->feed(50);
    QTRY_VERIFY(!widget.diagnostics()["history_worker_busy"].toBool());
    HistoryPoints points;
    for (int age : {3, 2, 1}) {
      HistoryPoint point; point.address = "synthetic"; point.session = "test";
      point.wall = point.tick = now-age; point.data.fresh = 1; point.data.known = 4;
      point.data.output_w = age == 2 ? 100 : 20; points.append(point);
    }
    auto deliver = [&] {
      QMetaObject::invokeMethod(worker, [worker, points] { emit worker->loaded("synthetic", points, {}); }, Qt::QueuedConnection);
    };
    deliver();
    QTRY_COMPARE(widget.card()->historyPoints().size(), 3);
    auto *animation = widget.card()->findChild<QVariantAnimation *>(); QVERIFY(animation);
    // Finish the initial vertical-scale transition before testing a new second.
    animation->setCurrentTime(animation->duration());
    QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
    const auto original = widget.card()->historyPoints();
    for (int i = 0; i < 20; ++i) { emit transport->stateChanged(); transport->feed(50); }
    QCoreApplication::processEvents();
    QVERIFY(widget.card()->historyPoints().constData() == original.constData());
    ++now; emit transport->stateChanged();
    QTRY_COMPARE(animation->state(), QAbstractAnimation::Running);
    QVERIFY(widget.card()->historyPoints().constData() == original.constData());
    animation->pause(); animation->setCurrentTime(450);
    for (int i = 0; i < 20; ++i) emit transport->stateChanged();
    QCoreApplication::processEvents();
    QCOMPARE(animation->currentTime(), 450);
    QVERIFY(widget.card()->historyPoints().constData() == original.constData());
    points.last().data.output_w = 90;
    deliver();
    QTRY_COMPARE(widget.card()->historyPoints().last().data.output_w, 90u);
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
    auto *animation = card.findChild<QVariantAnimation *>();
    QVERIFY(animation);
    QCOMPARE(animation->state(), QAbstractAnimation::Running);
    auto *frames = card.findChild<QTimer *>();
    QVERIFY(frames && frames->isActive());
    // Inspect exact animation positions even if the runner is descheduled.
    animation->setCurrentTime(animation->duration() / 2);
    const auto moving = card.renderedImage();
    QVERIFY(moving != before);
    animation->setCurrentTime(animation->duration());
    QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
    QVERIFY(!frames->isActive()); // Idle cards must not keep waking the repaint timer.
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
  void denseGraphPreservesPeaksAndGapsDuringAnimation() {
    WidgetCard card(false);
    constexpr qint64 now = 100000;
    HistoryPoints points;
    for (int age = 3599; age >= 0; --age) {
      HistoryPoint point;
      point.address = "synthetic";
      point.session = age > 35 ? "previous" : "live";
      point.wall = point.tick = now - age;
      point.data.fresh = age < 7 || age > 9;
      point.data.known = 6;
      point.data.input_w = age == 90 ? 100 : 20;
      point.data.output_w = age == 15 || age == 2500 ? 100 : 40;
      points.append(point);
    }
    card.setHistory(points, now);
    const auto initial = card.renderedImage();
    card.setHistory(points, now + 1);
    // Subpixel representatives on the compressed left side may change; the
    // recent, visibly moving half must not jump when the new second arrives.
    QCOMPARE(card.renderedImage().copy(150, 126, 140, 54), initial.copy(150, 126, 140, 54));
    auto *animation = card.findChild<QVariantAnimation *>();
    QVERIFY(animation);
    QImage previous;
    for (int elapsed : {0, 150, 300, 450, 600, 750, 900}) {
      animation->setCurrentTime(elapsed);
      const double shift = double(elapsed) / animation->duration();
      const auto image = card.renderedImage();
      auto hasLine = [&](double age, int y, bool input) {
        const int x = qRound(14 + WidgetCard::graphPosition(age + shift) * 272);
        for (int dx = -2; dx <= 2; ++dx)
          for (int dy = -2; dy <= 2; ++dy) {
            const auto color = image.pixelColor(x + dx, y + dy);
            if (input ? color.blue() > 180 && color.red() < 150
                      : color.red() > 180 && color.green() > 190 && color.blue() > 200)
              return true;
          }
        return false;
      };
      QVERIFY(hasLine(90, 127, true));
      QVERIFY(hasLine(15, 127, false));
      QVERIFY(hasLine(2500, 127, false)); // Preserve a peak compressed into a fraction of a pixel.
      const int gapX = qRound(14 + WidgetCard::graphPosition(8 + shift) * 272);
      for (int y = 129; y < 177; ++y) {
        const auto color = image.pixelColor(gapX, y);
        QVERIFY(color.blue() < 150); // No line bridging the stale run.
      }
      if (!previous.isNull())
        QVERIFY(image != previous); // Intermediate frames still advance smoothly.
      previous = image;
    }
    QCOMPARE(card.historyPoints().size(), points.size()); // Keep the complete source history.
  }
  void usbArchiveFillsWidgetGraph_data() {
    QTest::addColumn<QString>("board");
    QTest::newRow("waveshare") << QString("waveshare-147");
    QTest::newRow("pocket") << QString("pocket-096");
  }
  void usbArchiveFillsWidgetGraph() {
    QFETCH(QString, board);
    QTemporaryDir temporary;
    const auto base = QDateTime::currentSecsSinceEpoch();
    qint64 now = base;
    WidgetRuntime runtime;
    runtime.wallClock = [&] { return now; };
    runtime.elapsedClock = [&] { return (now - base)*1000; };
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
                            }, runtime);
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
    QTRY_VERIFY(io->writes.contains("HISTORY-EXPORT "));
    QVector<HistoryFormat::Record> records;
    for(int i=0;i<2;++i){auto r=HistoryFormat::example();r.sequence=i+1;r.session=7;r.uptimeSeconds=100+i*60;
      r.outputWatts=150;r.soc=50;records.append(r);}
    io->feed(archiveBytes(io,records));
    QTRY_COMPARE(widget.diagnostics()["usb_history_points"].toInt(), 3);
    QVERIFY(widget.diagnostics()["history_error"].toString().isEmpty());
    QTRY_VERIFY(widget.diagnostics()["output_segments"].toInt() > 0);
    const auto &points = widget.card()->historyPoints();
    QVERIFY(points.size() >= 3);
    QVERIFY(joinPoints(points[points.size() - 2], points.last()));
    QVERIFY(!joinPoints(points.first(), points[1])); // The genuine older-boot gap remains.
    // After importing history, live data must still reach both the graph model
    // and rendered graph pixels, rather than merely updating the numeric label.
    auto *animation = widget.card()->findChild<QVariantAnimation *>();
    QVERIFY(animation);
    auto finishAnimation = [&] {
      if (animation->state() == QAbstractAnimation::Running)
        animation->setCurrentTime(animation->duration());
      QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
    };
    finishAnimation();
    auto graphImage = widget.card()->renderedImage().copy(14, 126, 272, 54);
    auto previousWall = widget.card()->historyPoints().last().wall;
    for (int power : {300, 70}) {
      ++now;
      io->feed(widgetUsbStatus(board, power, power));
      QTRY_COMPARE(widget.card()->historyPoints().last().data.output_w, quint32(power));
      QVERIFY(widget.card()->historyPoints().last().wall > previousWall);
      const auto &live = widget.card()->historyPoints();
      QVERIFY(joinPoints(live[live.size()-2], live.last()));
      // Receipt keeps the zero-animation frame at the previous time/scale;
      // compare completed frames instead of racing the first animation tick.
      finishAnimation();
      const auto nextImage = widget.card()->renderedImage().copy(14, 126, 272, 54);
      QVERIFY(nextImage != graphImage);
      graphImage = nextImage;
      previousWall = widget.card()->historyPoints().last().wall;
    }
    // Reconnection re-reads history automatically and deduplicates retained records.
    io->writes.clear();
    usb->resume("R2500-000000000001", {}, 0);
    io->feed(widgetUsbStatus(board, 11));
    QTRY_VERIFY(io->writes.contains("HISTORY-EXPORT "));
    io->feed(archiveBytes(io,records));
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
    auto window = reinterpret_cast<HWND>(card.nativeHandle());
    if (unobscured(window, {8, 130}, card.desktopScale()))
      QTRY_COMPARE(composedPixel(window, {8, 130}, card.desktopScale()), RGB(16, 27, 45));
    else
      qInfo()
          << "Desktop pixel is covered by another application; checking publication and geometry";
    RECT expected{};
    GetWindowRect(window, &expected);
    for (int i = 0; i < 20; ++i) {
      value.soc = 50 + i;
      card.setTelemetry(value, QStringLiteral("Демонстрационные данные"));
      RECT current{};
      GetWindowRect(window, &current);
      QCOMPARE(current.left, expected.left);
      QCOMPARE(current.top, expected.top);
      QCOMPARE(current.right, expected.right);
      QCOMPARE(current.bottom, expected.bottom);
      QVERIFY(IsWindowVisible(window));
    }
  }
  void nativeImageBufferReuseAndRecovery() {
    if (QGuiApplication::platformName() != "windows")
      QSKIP("Requires native Windows platform");
    const auto origin = QGuiApplication::primaryScreen()->geometry().topLeft();
    auto makeHost = [&] {
      auto window = CreateWindowExW(WS_EX_LAYERED, L"STATIC", L"ALLPOWERS image buffer test",
        WS_POPUP | WS_CLIPCHILDREN, origin.x()+31, origin.y()+47, 1200, 900,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
      if (window) {
        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      }
      return window;
    };
    HWND host = makeHost(); QVERIFY(host);
    const auto closeHost = qScopeGuard([&] { if(host) DestroyWindow(host); });
    const auto objects = [] { return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS); };
    // Warm native composition before measuring resources owned by the surface.
    {
      DesktopSurface warm(true, [&] { return reinterpret_cast<quintptr>(host); });
      warm.setFixedSize(90, 60); warm.setDesktopPosition({}, {80, 80});
      const auto s = warm.desktopScale();
      QImage frame(QSize(qRound(90*s), qRound(60*s)), QImage::Format_ARGB32_Premultiplied);
      frame.fill(Qt::blue); warm.presentImage(frame);
      QVERIFY(warm.nativeDiagnostics()["native_frame_published"].toBool());
      composedPixel(reinterpret_cast<HWND>(warm.nativeHandle()), {4, 4}, s);
    }
    const DWORD beforeSurface = objects();
    QByteArray paddedStorage;
    {
      DesktopSurface surface(true, [&] { return reinterpret_cast<quintptr>(host); });
      surface.setFixedSize(90, 60); surface.setDesktopPosition({}, {80, 80});
      const auto scale = surface.desktopScale();
      QImage frame(QSize(qRound(90*scale), qRound(60*scale)), QImage::Format_ARGB32_Premultiplied);
      frame.fill(Qt::red);
      const DWORD beforeBuffer = objects();
      {
        DibAllocationProbe allocations;
        QVERIFY(allocations.active());
        surface.presentImage(frame);
        QCOMPARE(allocations.count(), 1);
        QVERIFY(surface.nativeDiagnostics()["native_frame_published"].toBool());
        QCOMPARE(objects(), beforeBuffer + 2);
        for(int i=0;i<100;++i) {
          frame.fill(i%2 ? Qt::green : Qt::red); surface.presentImage(frame);
          QCOMPARE(objects(), beforeBuffer + 2);
          QCOMPARE(allocations.count(), 1); // Stable resources alone would miss allocate/free churn.
        }
        const auto *oldPixels = allocations.pixels();
        QVERIFY(oldPixels);
        QImage allocationFrame(frame.width()+3, frame.height()+5, frame.format());
        allocationFrame.fill(Qt::blue);
        allocations.failNext();
        surface.presentImage(allocationFrame);
        QCOMPARE(allocations.count(), 2);
        QVERIFY(!surface.attachmentError().isEmpty());
        QCOMPARE(objects(), beforeBuffer + 2); // The failed DIB's temporary DC was released.
        QCOMPARE(oldPixels[0], frame.pixel(0, 0)); // Last successful allocation and image survive.
        surface.presentImage(allocationFrame);
        QCOMPARE(allocations.count(), 3);
        QVERIFY(surface.nativeDiagnostics()["native_frame_published"].toBool());
        QVERIFY(surface.attachmentError().isEmpty());
        QCOMPARE(objects(), beforeBuffer + 2);
        QCOMPARE(allocations.pixels()[0], allocationFrame.pixel(0, 0));
      }
      surface.setFixedSize(120, 80); surface.setDesktopPosition({}, {80, 80});
      QImage resized(QSize(qRound(120*scale), qRound(80*scale)), QImage::Format_ARGB32_Premultiplied);
      resized.fill(Qt::green);
      const QRgb *retainedPixels = nullptr;
      {
        DibAllocationProbe allocations;
        QVERIFY(allocations.active());
        surface.presentImage(resized);
        QCOMPARE(allocations.count(), 1);
        retainedPixels = allocations.pixels();
        QVERIFY(retainedPixels);
      }
      QVERIFY(surface.nativeDiagnostics()["native_frame_published"].toBool());
      QCOMPARE(objects(), beforeBuffer + 2);
      HWND child = reinterpret_cast<HWND>(surface.nativeHandle());
      const auto style = GetWindowLongPtrW(child, GWL_EXSTYLE);
      SetWindowLongPtrW(child, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
      QImage failed(resized.width()+17, resized.height()+19, QImage::Format_ARGB32_Premultiplied);
      failed.fill(Qt::red); surface.presentImage(failed);
      QVERIFY(!surface.nativeDiagnostics()["native_frame_published"].toBool());
      QVERIFY(!surface.attachmentError().isEmpty());
      QCOMPARE(objects(), beforeBuffer + 2); // Failed replacement must release temporary handles.
      SetWindowLongPtrW(child, GWL_EXSTYLE, style);
      const qsizetype stride = resized.width()*4 + 16;
      paddedStorage = QByteArray(stride*resized.height(), char(0xee));
      for(int y=0;y<resized.height();++y) {
        const QRgb color = y < resized.height()/3 ? qRgb(255,0,0)
                          : y < resized.height()*2/3 ? qRgb(0,255,0) : qRgb(0,0,255);
        auto *row = reinterpret_cast<QRgb *>(paddedStorage.data()+y*stride);
        std::fill_n(row, resized.width(), color);
        row[0] = qRgb(y & 255, (y*7) & 255, (y*13) & 255);
      }
      QImage padded(reinterpret_cast<uchar *>(paddedStorage.data()), resized.width(), resized.height(),
                    stride, QImage::Format_ARGB32_Premultiplied);
      surface.presentImage(padded);
      QVERIFY(surface.nativeDiagnostics()["native_frame_published"].toBool());
      QVERIFY(surface.attachmentError().isEmpty());
      QCOMPARE(objects(), beforeBuffer + 2);
      // Inspect the actual retained DIB too: composition can be obscured on a
      // shared desktop, but padded rows must still be copied without skew.
      for (int y : {0, resized.height()/2, resized.height()-1})
        QCOMPARE(retainedPixels[y*resized.width()], padded.pixel(0, y));
      if(unobscured(child, {4, 76}, scale))
        QTRY_COMPARE(composedPixel(child, {4, 76}, scale), RGB(0,0,255));
      surface.presentImage(QImage{});
      DestroyWindow(host); host = makeHost(); QVERIFY(host);
      surface.setDesktopPosition({}, {80, 80});
      QVERIFY(surface.nativeDiagnostics()["native_frame_published"].toBool());
      QCOMPARE(objects(), beforeBuffer + 2);
      child = reinterpret_cast<HWND>(surface.nativeHandle());
      if(unobscured(child, {4, 76}, scale))
        QTRY_COMPARE(composedPixel(child, {4, 76}, scale), RGB(0,0,255));
    }
    QCOMPARE(objects(), beforeSurface);
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
    int hostChecks = 0;
    WidgetCard surface(true, [&] { ++hostChecks; return reinterpret_cast<quintptr>(host); });
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
    HWND child = reinterpret_cast<HWND>(surface.nativeHandle());
    // Match Explorer's composition mode and sample the actual screen output.
    // The child's GDI DC can look correct while DWM displays a black rectangle.
    const qreal scale = surface.desktopScale();
    if (unobscured(child, {8, 130}, scale))
      QTRY_COMPARE(composedPixel(child, {8, 130}, scale), RGB(16, 27, 45));
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
    };
    // Inject each drag synchronously. Pumping while native capture is active
    // mixes the synthetic coordinates with queued WM_MOUSEMOVE messages from
    // the real cursor, especially under the slower coverage debugger.
    QCoreApplication::processEvents();
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
    QCoreApplication::processEvents();
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
    QCoreApplication::processEvents();
    QCOMPARE(surface.desktopOffset(), beforeNative + QPoint(20, 10));
    surface.setLocked(true);
    const QPoint locked = surface.desktopOffset();
    mouse(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, local + QPoint(20, 20), global + QPoint(20, 20), Qt::NoButton,
          Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QCOMPARE(surface.desktopOffset(), locked);
    const int previousChecks = hostChecks;
    QTRY_VERIFY(hostChecks > previousChecks); // An actual health check must preserve position.
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
      QTRY_VERIFY(widget.diagnostics()["history_ready"].toBool());
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
  void historyReadinessFollowsWorker_data() {
    QTest::addColumn<bool>("fail");
    QTest::newRow("opened") << false;
    QTest::newRow("failed") << true;
  }
  void historyReadinessFollowsWorker() {
    QFETCH(bool, fail);
    struct ControlledOpen : HistoryWorker {
      QSemaphore &entered, &release;
      bool fail;
      ControlledOpen(QSemaphore &e, QSemaphore &r, bool f) : entered(e), release(r), fail(f) {}
      void open(const QString &path) override {
        entered.release();
        // Failure bound only: the test releases the worker explicitly.
        if (!release.tryAcquire(1, 10000)) {
          emit problem("Controlled open was not released");
          return;
        }
        HistoryWorker::open(fail ? QFileInfo(path).absolutePath() : path);
      }
    };
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSemaphore entered, release;
    WidgetRuntime runtime;
    runtime.makeHistoryWorker = [&] { return new ControlledOpen(entered, release, fail); };
    WidgetController widget(directory.path(), true, false, {}, runtime);
    const auto unblock = qScopeGuard([&] { release.release(); });
    widget.loadDemo();
    QTRY_VERIFY(entered.available() == 1);
    QVERIFY(!widget.diagnostics()["history_ready"].toBool());
    QVERIFY(widget.diagnostics()["history_error"].toString().isEmpty());
    release.release();
    if (fail) {
      QTRY_VERIFY(!widget.diagnostics()["history_error"].toString().isEmpty());
      QVERIFY(!widget.diagnostics()["history_ready"].toBool());
    } else {
      QTRY_VERIFY(widget.diagnostics()["history_ready"].toBool());
      QVERIFY(widget.diagnostics()["history_error"].toString().isEmpty());
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
    // Parent waits for "owned", then kills us to test lease recovery after a
    // crash. This is only an orphan-child lifetime bound, not synchronization.
    Sleep(30000);
    return 0;
  }
  QApplication application(argc, argv);
  UiLanguage language("ru");
  WidgetTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "widget_test.moc"
