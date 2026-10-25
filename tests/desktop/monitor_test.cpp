#include "integration.h"
#include "ipc.h"
#include "service_client.h"
#include "service_host.h"
#include "transport_router.h"
#include "usb_transport.h"
#include "history_operation_fixture.h"
#include "../../firmware/R2500Adapter/HistoryWire.h"
#include "../../firmware/R2500Adapter/HistorySample.h"
#include "../../firmware/R2500Adapter/HistoryExport.h"
#include "window.h"
#include "monitor_application.h"
#include <QAbstractItemView>
#include <QBuffer>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QProcess>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QUuid>
#include <QWheelEvent>
#include <algorithm>
#include <memory>

class TestArchiveSource:public HistoryFormat::ExportSource {
public:
  QVector<HistoryFormat::Record> input,memory;
  std::unique_ptr<HistoryFormat::FrozenArchive> frozen;
  int busyOpen=0,busyRead=0,errorAt=-1,endAt=-1;
  HistoryFormat::ReadState open(HistoryFormat::ExportMetadata &metadata)override{
    if(busyOpen-->0)return HistoryFormat::ReadState::Busy;
    memory.resize(input.size());
    frozen=std::make_unique<HistoryFormat::FrozenArchive>(memory.data(),quint32(memory.size()));
    return frozen->freeze(input.constData(),0,quint32(input.size()),quint32(input.size()),7,10,metadata);
  }
  HistoryFormat::ReadState read(uint32_t ordinal,HistoryFormat::Record &record)override{
    if(busyRead-->0)return HistoryFormat::ReadState::Busy;
    if(int(ordinal)==errorAt)return HistoryFormat::ReadState::Error;
    if(int(ordinal)==endAt)return HistoryFormat::ReadState::End;
    return frozen?frozen->read(ordinal,record):HistoryFormat::ReadState::Error;
  }
  void close()override{frozen.reset();}
};
class TestArchiveSink:public HistoryFormat::ExportSink {
public:
  QByteArray bytes;size_t chunk=4096;int failedWrites=0;
  size_t write(const char *data,size_t length)override{
    if(failedWrites-->0)return 0;
    const auto size=qMin(chunk,length);bytes.append(data,qsizetype(size));return size;
  }
};
static HistoryFormat::Record archiveRecord(quint32 sequence){
  auto r=HistoryFormat::example();r.magic=HistoryFormat::header(0x7f,3);
  r.sequence=sequence;r.session=7;r.uptimeSeconds=sequence;
  r.epochSeconds=quint32(QDateTime::currentSecsSinceEpoch()-60);r.flags|=HistoryFormat::Clock;
  r.crc=HistoryFormat::crc32(&r,offsetof(HistoryFormat::Record,crc));return r;
}
#include <QSaveFile>
#include <windows.h>

class ScriptedBleChannel : public BleChannel {
public:
  quint64 generation=0;
  int attempts=0, closes=0, writes=0, scans=0;
  bool readyImmediately=true;
  void scan() override { ++scans; }
  void connectToDevice(const QString &,quint64 epoch) override {
    ++attempts;generation=epoch;
    if(readyImmediately) emit ready(epoch);
  }
  void close() override { ++closes; }
  void write(const QByteArray &) override { ++writes; }
  void feed(const QByteArray &bytes) { emit received(generation,bytes); }
};
class ReportTransport : public TelemetryTransport {
  bool running=false;
public:
  int attempts=0, discoveries=0, stops=0;
  using TelemetryTransport::TelemetryTransport;
  bool active() const override { return running; }
  bool connected() const override { return running; }
  bool scanning() const override { return false; }
  QString address() const override { return "02:00:00:00:00:01"; }
  QString phase() const override { return running ? "connected" : "stopped"; }
  QJsonObject diagnostics() const override {
    return {{"active",running},{"connected",running},{"discovery_attempts",discoveries},
      {"connection_attempts",attempts},{"connections",attempts},
      {"bluetooth_used",attempts>0 || discoveries>0},{"stops",stops}};
  }
  void scan() override { ++discoveries; }
  void start(const QString &,int) override { running=true;++attempts; }
  void resume(const QString &id,const QString &,int model) override {
    scan();start(id,model);
    QTimer::singleShot(0,this,[this]{stop();});
  }
  void stop() override { running=false;++stops;emit stateChanged(); }
};
class FlushFailureTransaction : public HistoryCsvTransaction {
  class Sink : public QIODevice {
  public:
    int writes=0;
    Sink() { open(WriteOnly); }
    qint64 readData(char *,qint64) override { return -1; }
    qint64 writeData(const char *,qint64) override { ++writes;return -1; }
  } sink;
  QSaveFile staged;
  QString *openedPath;
public:
  explicit FlushFailureTransaction(QString *path) : openedPath(path) {}
  bool open(const QString &path) override {
    *openedPath=path;staged.setFileName(path);staged.setDirectWriteFallback(false);
    return staged.open(QIODevice::WriteOnly);
  }
  QIODevice &device() override { return sink; }
  bool commit() override { return staged.commit(); }
  void cancel() override { staged.cancelWriting(); }
};

class FakeUsbChannel : public UsbChannel {
public:
  QByteArray writes;
  QString openedPort;
  int closes = 0;
  void open(const QString &port) override {
    openedPort = port;
    emit opened();
  }
  void close() override {
    closes++;
  }
  void write(const QByteArray &data) override {
    writes += data;
  }
  void feed(const QByteArray &data) {
    emit received(data);
  }
};
static QList<UsbPort> fakeUsbPorts() {
  return {{"R2500-000000000001", "COM987", "Synthetic Waveshare USB"}};
}
static QByteArray usbCycle(const QString &board = "waveshare-147", int frames = 10) {
  const QList<QJsonObject> rows = {
      {{"adapter", "UPSDEV"},
       {"version", "R"},
       {"fresh", true},
       {"gatt_connected", true},
       {"soc", 50},
       {"charge_w", 0},
       {"output_w", 150},
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
      {{"graph_live_latest_s", 120}},
      {{"history_ready", true},
       {"history_records", 1},
       {"history_latest", 1},
       {"history_oldest", 1},
       {"history_export_version", 2},
       {"history_session", 7},
       {"history_clock", false}},
      {{"board", board}}};
  QByteArray data;
  for (const auto &o : rows)
    data += QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
  return data;
}
static QByteArray usbNonceCycle(const QJsonValue &capability=QJsonValue(QJsonValue::Undefined)) {
  QByteArray bytes;
  for(const auto &line:usbCycle().split('\n')){
    if(line.isEmpty())continue;
    auto row=QJsonDocument::fromJson(line).object();
    if(row.contains("history_ready")){
      row["history_export_version"]=3;row["history_generation"]=10;
      if(!capability.isUndefined())row["history_nonce_bits"]=capability;
    }
    bytes+=QJsonDocument(row).toJson(QJsonDocument::Compact)+'\n';
  }
  return bytes;
}

class FailingCsvDevice : public QIODevice {
public:
  int writes=0;
  FailingCsvDevice() { open(QIODevice::WriteOnly); }
  qint64 readData(char *,qint64) override { return -1; }
  qint64 writeData(const char *,qint64) override { ++writes;return -1; }
};
class MonitorTests : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
private slots:
  void usbOperationIdentityNegotiationFailsClosed() {
    for(int mode=0;mode<9;++mode){
      auto *io=new FakeUsbChannel;int calls=0;
      auto entropy=[&]{
        ++calls;
        if(mode==5)return QByteArray{};
        if(mode==6)return QByteArray(15,0);
        if(mode==7)return QByteArray(16,0);
        auto raw=QByteArray(16,0);qToLittleEndian<quint32>(0x12345678u,reinterpret_cast<uchar *>(raw.data())+4);return raw;
      };
      UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},entropy);
      QJsonValue capability=mode==0||mode==7?QJsonValue(QJsonValue::Undefined):QJsonValue(128);
      if(mode==2)capability=64;
      if(mode==3)capability="128";
      if(mode==4)capability=QJsonValue(QJsonValue::Null);
      if(mode==8)capability=0;
      usb.resume("R2500-000000000001",{},0);io->feed(usbNonceCycle(capability));
      QVERIFY(usb.connected());usb.requestHistory();
      if(mode<=1){
        QCOMPARE(calls,1);QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("reading"));
        if(mode==0){QVERIFY(io->writes.contains("HISTORY-EXPORT 305419896\n"));QCOMPARE(usb.diagnostics()["usb_history_nonce_bits"].toInt(),0);}
        else {QVERIFY(io->writes.contains("HISTORY-EXPORT 1 00000000785634120000000000000000\n"));QCOMPARE(usb.diagnostics()["usb_history_nonce_bits"].toInt(),128);}
      }else{
        QCOMPARE(calls,mode==5||mode==6||mode==7?1:0);
        QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("error"));
        QVERIFY(!io->writes.contains("HISTORY-EXPORT "));
      }
    }
  }
  void bleDegradationRunsRealRecoveryLifecycle() {
    const auto main=QByteArray::fromHex("a565b1000108010264000000e696006f");
    const auto settings=QByteArray::fromHex("a565b10001060300010000030275");
    for(bool mainOnly : {true,false}) {
      qint64 now=0;
      auto *io=new ScriptedBleChannel;
      BleTransport transport(nullptr,false,true,io,[&]{return now;});
      QSignalSpy snapshots(&transport,&TelemetryTransport::snapshot);
      transport.resume("02:00:00:00:00:01","Synthetic R2500",3);
      QCOMPARE(io->attempts,1);QVERIFY(transport.connected());
      for(now=0;now<=10000;now+=1000) {
        io->feed(main);io->feed(settings);transport.refresh();
        QVERIFY(qvariant_cast<ap_snapshot>(snapshots.last()[0]).fresh);
      }
      // First healthy, then a continuous stream containing only one group.
      for(now=11000;now<=70000;now+=1000) {
        io->feed(mainOnly ? main : settings);transport.refresh();
        QCOMPARE(bool(qvariant_cast<ap_snapshot>(snapshots.last()[0]).fresh),now<=25000);
        QCOMPARE(io->attempts,1);
        QVERIFY(transport.connected());
      }
      now=70001;transport.refresh();
      QVERIFY(!transport.connected());
      QCOMPARE(transport.diagnostics()["drops"].toInteger(),1);
      QCOMPARE(transport.diagnostics()["retry_at_ms"].toInteger(),72001);
      now=72000;transport.refresh();QCOMPARE(io->attempts,1);
      now=72001;transport.refresh();QCOMPARE(io->attempts,2);
      QVERIFY(transport.connected());
      QVERIFY(!qvariant_cast<ap_snapshot>(snapshots.last()[0]).fresh);
      const auto oldGeneration=io->generation-2;
      emit io->received(oldGeneration,main); // Previous connection cannot refill the new state.
      io->feed(main);io->feed(settings);transport.refresh();
      QVERIFY(qvariant_cast<ap_snapshot>(snapshots.last()[0]).fresh);
      QVERIFY(io->writes>=4); // Read-only polling continued before reconnect.
    }
    qint64 now=0;
    auto *io=new ScriptedBleChannel;io->readyImmediately=false;
    BleTransport transport(nullptr,false,true,io,[&]{return now;});
    transport.resume("02:00:00:00:00:01",{},3);
    now=29999;transport.refresh();QCOMPARE(transport.diagnostics()["drops"].toInteger(),0);
    now=30000;transport.refresh();QCOMPARE(transport.diagnostics()["drops"].toInteger(),1);
    now=32000;transport.refresh();QCOMPARE(io->attempts,2);
    // A subscribed channel with no first usable snapshot has its own bounded
    // telemetry grace period rather than remaining connected indefinitely.
    now=0;
    auto *silent=new ScriptedBleChannel;
    BleTransport noFirstSnapshot(nullptr,false,true,silent,[&]{return now;});
    noFirstSnapshot.resume("02:00:00:00:00:02",{},3);
    now=45000;noFirstSnapshot.refresh();QVERIFY(noFirstSnapshot.connected());
    now=45001;noFirstSnapshot.refresh();QVERIFY(!noFirstSnapshot.connected());
    QCOMPARE(noFirstSnapshot.diagnostics()["retry_at_ms"].toInteger(),47001);
    now=47001;noFirstSnapshot.refresh();QCOMPARE(silent->attempts,2);
  }
  void directVerificationCliRejectsOwnersBeforeTransportCreation() {
    QTemporaryDir dir;
    const auto oldDirectory=qgetenv("ALLPOWERS_DATA_DIR");
    auto restore=qScopeGuard([&]{qputenv("ALLPOWERS_DATA_DIR",oldDirectory);});
    qputenv("ALLPOWERS_DATA_DIR",dir.path().toUtf8());
    QFile settings(dir.filePath("settings.json"));QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write("{\"address\":\"02:00:00:00:00:01\",\"profile\":3}");settings.close();
    for(bool installed : {false,true}) {
      MonitorRuntime runtime;
      runtime.leaseKey="ALLPOWERS.Test.Cli."+QUuid::createUuid().toString();
      ConnectionLease owner(runtime.leaseKey);
      if(!installed) QVERIFY(owner.acquire());
      int constructed=0,serviceQueries=0;
      runtime.serviceInstalled=[&]{++serviceQueries;return installed;};
      runtime.makeTransport=[&](QObject *parent,bool){++constructed;return new ReportTransport(parent);};
      const auto report=dir.filePath(installed ? "service.json" : "lease.json");
      QCOMPARE(runMonitorApplication(*qobject_cast<QApplication *>(qApp),
        {"AllpowersMonitor","--verify-station","10","--report",report},runtime),5);
      QCOMPARE(serviceQueries,1);QCOMPARE(constructed,0);
      QFile file(report);QVERIFY(file.open(QIODevice::ReadOnly));
      const auto result=QJsonDocument::fromJson(file.readAll()).object();
      QCOMPARE(result["status"].toString(),QString("BUSY"));
      QVERIFY(!result["bluetooth_used"].toBool());
    }
  }
  void timedCliReportRetainsStoppedTransportActivity() {
    QTemporaryDir dir;
    const auto oldDirectory=qgetenv("ALLPOWERS_DATA_DIR");
    const bool oldQuit=qApp->quitOnLastWindowClosed();
    auto restore=qScopeGuard([&]{qputenv("ALLPOWERS_DATA_DIR",oldDirectory);qApp->setQuitOnLastWindowClosed(oldQuit);});
    qApp->setQuitOnLastWindowClosed(false);
    qputenv("ALLPOWERS_DATA_DIR",dir.path().toUtf8());
    QFile settings(dir.filePath("settings.json"));QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write("{\"address\":\"02:00:00:00:00:01\",\"profile\":3}");settings.close();
    MonitorRuntime runtime;
    runtime.makeTransport=[](QObject *parent,bool){return new ReportTransport(parent);};
    const auto report=dir.filePath("timed.json");
    QCOMPARE(runMonitorApplication(*qobject_cast<QApplication *>(qApp),
      {"AllpowersMonitor","--test-seconds","1","--report",report},runtime),0);
    QFile file(report);QVERIFY(file.open(QIODevice::ReadOnly));
    const auto result=QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(result["requested_mode"].toString(),QString("normal"));
    QCOMPARE(result["status"].toString(),QString("PASS"));
    QVERIFY(result["bluetooth_used"].toBool());
    QVERIFY(!result["active"].toBool());
    QCOMPARE(result["discovery_attempts"].toInteger(),1);
    QCOMPARE(result["connection_attempts"].toInteger(),1);
    QVERIFY(result["stops"].toInteger()>=1);
  }
  void csvFailuresPreserveSameExistingDestination() {
    QTemporaryDir dir;
    const auto path=dir.filePath("history.csv");
    HistoryPoint old;old.address="previous-good-export";old.wall=1;
    QVERIFY(exportHistoryCsv({old},path));
    auto read=[&]{QFile file(path);if(!file.open(QIODevice::ReadOnly)) return QByteArray();return file.readAll();};
    const auto good=read();QVERIFY(good.contains("previous-good-export"));
    QString opened;
    QVERIFY(!exportHistoryCsv({},path,std::make_unique<FlushFailureTransaction>(&opened)));
    QCOMPARE(opened,path);QCOMPARE(read(),good);
    // Windows sharing denial allows temporary writes but rejects replacement
    // of this same existing destination at the real QSaveFile::commit boundary.
    HANDLE lock=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,
      FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    QVERIFY(lock!=INVALID_HANDLE_VALUE);
    auto close=qScopeGuard([&]{CloseHandle(lock);});
    QSaveFile probe(path);probe.setDirectWriteFallback(false);
    QVERIFY(probe.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(writeHistoryCsv({},probe));
    QCOMPARE(probe.error(),QFileDevice::NoError);
    QVERIFY(!probe.commit()); // Failure is replacement, not opening/writing.
    QCOMPARE(read(),good);
    QVERIFY(!exportHistoryCsv({},path));
    QCOMPARE(read(),good);
  }
  void initTestCase() {
    QApplication::setStyle("Fusion");
    qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8());
    auto fonts = QDir(qEnvironmentVariable("SystemRoot") + "/Fonts");
    for (const QString &name : {QString("segoeui.ttf"), QString("segoeuib.ttf")})
      QFontDatabase::addApplicationFont(fonts.filePath(name));
  }
  void failedDemoIsolationStopsBeforeDataAccess() {
    QTemporaryDir dir;
    const auto invalidTemp=dir.filePath("not-a-directory");
    QFile blocker(invalidTemp);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("invalid temporary root");blocker.close();
    const auto data=dir.filePath("real-data");
    QVERIFY(QDir().mkpath(data));
    QFile settings(data+"/settings.json");
    QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write("sentinel");settings.close();
    for(const auto &mode : {"--demo","--self-test"}) {
      QProcess child;
      auto env=QProcessEnvironment::systemEnvironment();
      env.insert("TEMP",invalidTemp);env.insert("TMP",invalidTemp);
      env.insert("QT_QPA_PLATFORM","offscreen");
      env.insert("ALLPOWERS_DATA_DIR",data);
      child.setProcessEnvironment(env);
      child.start(QCoreApplication::applicationDirPath()+"/AllpowersMonitor.exe",
                  {mode,"--test-seconds","1","--report",dir.filePath("report.json")});
      QVERIFY(child.waitForFinished(10000));
      QCOMPARE(child.exitCode(),3);
      QVERIFY(!QFile::exists(dir.filePath("report.json")));
      QVERIFY(!QFile::exists(data+"/history.sqlite3"));
      QVERIFY(settings.open(QIODevice::ReadOnly));
      QCOMPARE(settings.readAll(),QByteArray("sentinel"));settings.close();
    }
  }
  void boundedHistoryAndCompletion() {
    HistorySampleQueue queue;
    HistoryPoint point;point.wall=1;
    QVERIFY(queue.submit(point).has_value());
    for(int i=2;i<=10000;++i){
      point.wall=i;
      QVERIFY(!queue.submit(point).has_value());
    }
    QVERIFY(queue.busy() && queue.waiting());
    const auto next=queue.complete();
    QVERIFY(next.has_value());
    QCOMPARE(next->wall,10000);
    QVERIFY(queue.busy() && !queue.waiting());
    QVERIFY(!queue.complete().has_value());
    QVERIFY(!queue.busy());
    HistoryWorker worker;
    QSignalSpy completed(&worker,&HistoryWorker::sampleFinished);
    worker.sample("test",{},1,1); // No store is still a completed job.
    QCOMPARE(completed.size(),1);
    QTemporaryDir dir;
    worker.open(dir.path()); // A directory cannot be opened as a SQLite file.
    worker.sample("test",{},2,2);
    QCOMPARE(completed.size(),2);
  }
  void csvFlushAndAtomicReplacement() {
    QTemporaryDir dir;
    const auto path=dir.filePath("history.csv");
    QFile original(path);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("old content"),11);
    original.close();
    QVERIFY(exportHistoryCsv({},path)); // Short CSV is buffered until flush.
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto bytes=original.readAll();
    QVERIFY(bytes.contains("address,wall,tick,session"));
    QVERIFY(!bytes.contains("old content"));
    original.close();
    FailingCsvDevice failed;
    QVERIFY(!writeHistoryCsv({},failed));
    QCOMPARE(failed.writes,1); // Failure of the final buffered write.
    QVERIFY(!exportHistoryCsv({},dir.path()));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(),bytes);
  }
  void sharedDecoderAndGeneration() {
    Session session;
    auto token = session.reset(3);
    QVERIFY(session.receive(token, QByteArray::fromHex("a565b1000108010264000000e696006f"), 100));
    QVERIFY(session.receive(token, QByteArray::fromHex("a565b10001060300010000030275"), 200));
    auto s = session.snapshot(201);
    QCOMPARE(s.soc, 100u);
    QCOMPARE(s.output_w, 230u);
    QCOMPARE(s.runtime_s, 2304000u);
    QVERIFY(s.fresh);
    QCOMPARE(s.source, 2u);
    session.reset(3);
    QVERIFY(!session.receive(token, QByteArray::fromHex("a565b1000108010264000000e696006f"), 300));
    QVERIFY(!session.snapshot(301).main_seen);
  }
  void usbTelemetryArchiveAndStaleData() {
    qint64 now = 0;
    auto *io = new FakeUsbChannel;
    UsbTransport usb(nullptr, false, io, fakeUsbPorts, [&] { return now; },deterministicHistoryEntropy());
    QSignalSpy data(&usb, &TelemetryTransport::snapshot),
        archive(&usb, &TelemetryTransport::historyImported);
    usb.resume("R2500-000000000001", {}, 0);
    QCOMPARE(io->openedPort, QString("COM987"));
    auto cycle = usbCycle();
    for (char c : cycle)
      io->feed(QByteArray(1, c)); // Arbitrary USB packet fragmentation.
    QVERIFY(usb.connected());
    QCOMPARE(usb.address(), QString("02:00:00:00:00:03"));
    auto snapshot = qvariant_cast<ap_snapshot>(data.last()[0]);
    QCOMPARE(snapshot.soc, 50u);
    QCOMPARE(snapshot.output_w, 150u);
    QCOMPARE(snapshot.source, 1u);
    QVERIFY(snapshot.fresh);
    QVERIFY(snapshot.ac_output);
    usb.requestHistory();
    QVERIFY(io->writes.contains("HISTORY-EXPORT 1 1\n"));
    io->feed("{\"history_record\":true,\"sequence\":1,\"session\":7,\"uptime_s\":100,\"epoch\":0,"
             "\"soc\":50,\"charge_w\":0,\"output_w\":150,\"runtime_minutes\":30,\"flags\":13,"
             "\"crc\":3397945926}\n");
    io->feed("{\"history_record\":true,\"sequence\":1,\"session\":8,\"uptime_s\":100,\"epoch\":0,"
             "\"soc\":50,\"charge_w\":0,\"output_w\":150,\"runtime_minutes\":30,\"flags\":13,"
             "\"crc\":3055024743}\n");
    io->feed("{\"history_record\":true,\"sequence\":1,\"session\":7,\"uptime_s\":100,\"epoch\":0,"
             "\"soc\":50,\"charge_w\":0,\"output_w\":150,\"runtime_minutes\":30,\"flags\":13,"
             "\"crc\":0}\n{\"history_dump_done\":true}\n");
    QCOMPARE(archive.size(), 1);
    auto points = qvariant_cast<HistoryPoints>(archive.first()[0]);
    QCOMPARE(points.size(), 1);
    QCOMPARE(points.first().address, QString("usb-board:R2500-000000000001"));
    QCOMPARE(points.first().data.known & quint32(16 | 32 | 64), 0u); // AHP1 has no output provenance.
    QVERIFY(qAbs(points.first().wall - (QDateTime::currentSecsSinceEpoch() - 20)) <= 1);
    QCOMPARE(usb.diagnostics()["usb_history_undated"].toInt(), 0); // Duplicate sequence is discarded before timestamp mapping.
    QCOMPARE(usb.diagnostics()["usb_history_received"].toInt(), 1);
    QCOMPARE(usb.diagnostics()["usb_history_bad"].toInt(), 1);
    QTemporaryDir dir;
    HistoryStore store(dir.filePath("history.sqlite3"));
    QVERIFY2(store.valid(),qPrintable(store.error()));
    QVERIFY2(store.importPoints(points, QDateTime::currentSecsSinceEpoch()),qPrintable(store.error()));
    QVERIFY(store.importPoints(points, QDateTime::currentSecsSinceEpoch()));
    QCOMPARE(store.stats()["usb_minutes"].toInt(), 1);
    auto shifted = points;
    shifted.first().wall -= 60;
    QVERIFY(store.importPoints(shifted, QDateTime::currentSecsSinceEpoch()));
    QCOMPARE(store.stats()["usb_minutes"].toInt(), 1);
    QCOMPARE(store.stats()["minutes"].toInt(), 0);
    QCOMPARE(store.points(usb.address(), QDateTime::currentSecsSinceEpoch()).size(), 0);
    for (now = 1000; now <= 16000; now += 1000)
      io->feed(cycle);
    snapshot = qvariant_cast<ap_snapshot>(data.last()[0]);
    QVERIFY(!snapshot.fresh);
    io->feed(usbCycle("pocket-096", 11));
    snapshot = qvariant_cast<ap_snapshot>(data.last()[0]);
    QVERIFY(snapshot.fresh);
    QCOMPARE(usb.diagnostics()["usb_board"].toString(), QString("pocket-096"));
    io->feed("broken json\n" + QByteArray(10000, 'x') + '\n');
    QVERIFY(usb.diagnostics()["usb_bad_lines"].toInt() >= 2);
    io->feed("{\"diagnostic\":true,\"rows\":[{\"label\":\"HEAP\",\"value\":\"123456\"}]}\n");
    QCOMPARE(usb.diagnostics()["usb_diagnostics"].toObject()["HEAP"].toString(), QString("123456"));
    now += 7000;
    QTRY_VERIFY(!usb.connected());
    usb.stop();
    QVERIFY(!usb.active());
    QVERIFY(!io->writes.contains("BOOT"));
    QVERIFY(!io->writes.contains("TIME "));
    QVERIFY(!io->writes.contains("SELECT"));
    QVERIFY(!io->writes.contains("RETRY"));
  }
  void transportSelectionAndDriverPlacement() {
    QTemporaryDir dir;
    qputenv("ALLPOWERS_DATA_DIR", dir.path().toUtf8());
    const auto restore =
        qScopeGuard([this] { qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8()); });
    auto factory = [](const QString &mode, QObject *parent) -> TelemetryTransport * {
      if (mode == "usb")
        return new UsbTransport(parent, true);
      return new BleTransport(parent, true, false);
    };
    auto *router = new TransportRouter(nullptr, true, false, "bluetooth", factory);
    MonitorWindow window(true, nullptr, router);
    window.show();
    auto *mode = window.findChild<QComboBox *>("transportMode");
    QVERIFY(mode->isEnabled());
    mode->setCurrentIndex(1);
    QVERIFY(router->usb());
    QVERIFY(!window.findChild<QComboBox *>("profile")->isVisible());
    QVERIFY(window.findChild<QComboBox *>("historySource")->isVisible() ==
            (window.findChild<QStackedWidget *>()->currentIndex() == 1));
    auto *driver = window.findChild<QPushButton *>("integrationManagement");
    QVERIFY(driver);
    QVERIFY(!driver->isVisible());
    window.findChild<QButtonGroup *>()->button(2)->click();
    QVERIFY(driver->isVisible());
    mode->setCurrentIndex(0);
    QVERIFY(!router->usb());
    window.close();
  }
  void usbArchiveRecoversGapsAndPreservesUnknownOutputs() {
    for (int mode=0;mode<3;++mode) {
      auto *io=new FakeUsbChannel;
      UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
      QSignalSpy archive(&usb,&TelemetryTransport::historyImported);
      usb.resume("R2500-000000000001",{},0);
      io->feed(usbCycle());
      io->feed("{\"history_ready\":true,\"history_records\":2,\"history_latest\":3,\"history_oldest\":1,\"history_export_version\":2,\"history_session\":7}\n");
      usb.requestHistory();
      QVERIFY(io->writes.contains("HISTORY-EXPORT 1 2\n"));
      QByteArray memory(int(HistoryFormat::kBytes),0);
      HistoryFormat::MemoryFlash flash(reinterpret_cast<uint8_t*>(memory.data()));
      HistoryFormat::Ring ring(flash);
      QVERIFY(ring.recover());
      auto r=HistoryFormat::example();
      r.magic=HistoryFormat::header(1|2|4|8|16|32|(mode?64:0),2);
      r.flags=HistoryFormat::Fresh|HistoryFormat::Clock|(mode==2?HistoryFormat::UsbOutput:0);
      r.epochSeconds=quint32(QDateTime::currentSecsSinceEpoch()-120);
      r.uptimeSeconds=100;r.session=7;
      QVERIFY(ring.append(r));
      HistoryFormat::Record records[2]={r,{}};
      flash.partial=8;QVERIFY(!ring.append(r));
      HistoryFormat::Ring recovered(flash);QVERIFY(recovered.recover());
      r.epochSeconds+=60;r.uptimeSeconds+=60;
      QVERIFY(recovered.append(r));QCOMPARE(r.sequence,3u);records[1]=r;
      quint32 cursor=1;HistoryFormat::Record next;
      for(int i=0;i<2;++i){
        QVERIFY(HistoryFormat::atOrAfter(records,0,2,2,cursor,next));
        char line[512];int length=HistoryFormat::serialize(next,line,sizeof(line));
        QVERIFY(length>0&&size_t(length)<sizeof(line));
        io->feed(QByteArray(line,length));cursor=HistoryFormat::next(next.sequence);
      }
      io->feed("{\"history_dump_done\":true}\n");
      QCOMPARE(archive.size(),1);
      const auto points=qvariant_cast<HistoryPoints>(archive.first()[0]);
      QCOMPARE(points.size(),2);
      for(const auto &p:points){
        QCOMPARE(p.data.model,2u);
        QCOMPARE(bool(p.data.known&64),mode!=0);
        QCOMPARE(bool(p.data.usb_output),mode==2);
      }
    }
  }
  void usbDecodedOutputsSurviveArchiveRoundTrip() {
    for(int protocol:{1,2,3})for(bool on:{false,true}){
      Telemetry telemetry;auto model=AllpowersModels::Model(protocol);
      QByteArray main=protocol==1?QByteArray::fromHex("0f32006400c800070441"):
                                   QByteArray::fromHex("a565b1000108010264000000e696006f");
      if(protocol==1)main[0]=on?7:0;
      else {
        main[7]=on?3:0;main[main.size()-1]=0;quint8 sum=0;
        for(char byte:main)sum^=quint8(byte);main[main.size()-1]=char(sum);
      }
      QVERIFY(decodeStationTelemetry(telemetry,reinterpret_cast<const uint8_t*>(main.constData()),size_t(main.size()),100,model));
      if(protocol==3){
        auto settings=QByteArray::fromHex("a565b10001060300010000030275");
        settings[7]=on?0x10:0;settings[settings.size()-1]=0;quint8 sum=0;
        for(char byte:settings)sum^=quint8(byte);settings[settings.size()-1]=char(sum);
        QVERIFY(decodeStationTelemetry(telemetry,reinterpret_cast<const uint8_t*>(settings.constData()),size_t(settings.size()),100,model));
      }
      // This is the exact conversion used by HistoryStore::makeRecord.
      auto record=HistoryFormat::sample(telemetry,101);
      record.session=7;record.uptimeSeconds=100;
      record.epochSeconds=quint32(QDateTime::currentSecsSinceEpoch()-60);record.flags|=HistoryFormat::Clock;
      QByteArray memory(int(HistoryFormat::kBytes),0);
      HistoryFormat::MemoryFlash flash(reinterpret_cast<uint8_t*>(memory.data()));
      HistoryFormat::Ring ring(flash);QVERIFY(ring.recover());QVERIFY(ring.append(record));
      char line[512];int length=HistoryFormat::serialize(record,line,sizeof(line));
      QVERIFY(length>0&&size_t(length)<sizeof(line));
      auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
      QSignalSpy archive(&usb,&TelemetryTransport::historyImported);
      usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());usb.requestHistory();
      io->feed(QByteArray(line,length));io->feed("{\"history_dump_done\":true}\n");
      QCOMPARE(archive.size(),1);const auto points=qvariant_cast<HistoryPoints>(archive.first()[0]);
      QCOMPARE(points.size(),1);const auto &s=points.first().data;
      QCOMPARE(s.model,quint32(protocol));
      QVERIFY(s.known&AllpowersModels::AcOutput);QVERIFY(s.known&AllpowersModels::DcOutput);
      QCOMPARE(bool(s.ac_output),on);QCOMPARE(bool(s.dc_output),on);
      QCOMPARE(bool(s.known&AllpowersModels::UsbOutput),protocol!=2);
      QCOMPARE(bool(s.usb_output),on&&protocol!=2);
    }
  }
  void usbLegacyArchiveScansSequenceWindow() {
    auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
    usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
    io->feed("{\"history_ready\":true,\"history_records\":2,\"history_latest\":3,\"history_session\":7}\n");
    usb.requestHistory();
    // A legacy board can have records {1,3}; count=2 does not imply first=2.
    QVERIFY(io->writes.contains("HISTORY-READ 4294964419 2880\n"));
  }
  void usbEarlyArchiveDoneCannotClaimComplete() {
    auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
    usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());usb.requestHistory();
    io->feed("{\"history_dump_done\":true}\n");
    QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("partial"));
  }
  void usbVersionedExportFaultsAndFrozenSnapshot() {
    const QStringList scenarios={"complete","busy","short","dropped","corrupt","early",
      "session","session-change","generation","status","source-error","source-end","retention","unsolicited","noise","timeout","empty"};
    for(const auto &scenario:scenarios){
      qint64 now=0;auto *io=new FakeUsbChannel;
      UsbTransport usb(nullptr,false,io,fakeUsbPorts,[&]{return now;},deterministicHistoryEntropy());
      QSignalSpy archive(&usb,&TelemetryTransport::historyImported);
      usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
      io->feed("{\"history_ready\":true,\"history_records\":2,\"history_latest\":3,\"history_oldest\":1,\"history_export_version\":3,\"history_generation\":10,\"history_session\":7}\n");
      usb.requestHistory();QVERIFY(io->writes.contains("HISTORY-EXPORT 1\n"));
      TestArchiveSource source;source.input={archiveRecord(1),archiveRecord(3)};
      if(scenario=="empty")source.input.clear();
      TestArchiveSink sink;
      if(scenario=="busy")source.busyOpen=source.busyRead=5;
      if(scenario=="short"){sink.chunk=7;sink.failedWrites=4;}
      if(scenario=="source-error")source.errorAt=1;
      if(scenario=="source-end")source.endAt=1;
      HistoryFormat::ExportSession writer(source,sink);writer.start(1,0);
      for(quint32 tick=0;writer.active()&&tick<10000;++tick){
        writer.pump(tick);
        if(scenario=="retention"&&tick==3){source.input.removeFirst();source.input.append(archiveRecord(4));}
      }
      QVERIFY(!writer.active());
      auto lines=sink.bytes.split('\n');QByteArray stream;
      for(auto line:lines){
        if(line.isEmpty())continue;
        auto object=QJsonDocument::fromJson(line).object();
        const bool firstRecord=object["history_record"].toBool()&&object["ordinal"].toInt()==0;
        if(scenario=="dropped"&&firstRecord)continue;
        if(scenario=="corrupt"&&firstRecord)object["crc"]=0;
        if(scenario=="generation"&&firstRecord)object["generation"]=11;
        if(scenario=="session"&&object["history_export_begin"].toBool())object["export_session"]=8;
        if(scenario=="timeout"&&object["history_dump_done"].toBool())continue;
        stream+=QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
        if(object["history_export_begin"].toBool()&&scenario=="status")
          stream+="{\"history_ready\":true,\"history_records\":3,\"history_latest\":4,\"history_oldest\":1,\"history_export_version\":3,\"history_generation\":11,\"history_session\":7}\n";
        if(object["history_export_begin"].toBool()&&scenario=="unsolicited")
          stream+="{\"station_notification_hex\":\"a565\",\"valid\":false}\n";
        if(object["history_export_begin"].toBool()&&scenario=="session-change")
          stream+="{\"history_ready\":true,\"history_records\":2,\"history_latest\":3,\"history_session\":8}\n";
        if(object["history_export_begin"].toBool()&&scenario=="noise")stream+="partial unsolicited JSON\n";
      }
      if(scenario=="early")stream="{\"history_dump_done\":true}\n";
      for(char byte:stream)io->feed(QByteArray(1,byte));
      if(scenario=="timeout"){now=70001;QTest::qWait(600);}
      const QString expected=scenario=="session"||scenario=="source-error"||scenario=="timeout"?"error":
        scenario=="dropped"||scenario=="corrupt"||scenario=="generation"||scenario=="early"||scenario=="source-end"||scenario=="session-change"||scenario=="noise"?"partial":"complete";
      QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),expected);
      QCOMPARE(usb.diagnostics()["usb_history_complete"].toBool(),expected=="complete");
      QCOMPARE(archive.size(),1);
      if(expected=="complete"){
        const auto points=qvariant_cast<HistoryPoints>(archive.first()[0]);
        QCOMPARE(points.size(),scenario=="empty"?0:2);
        if(!points.isEmpty()){QCOMPARE(points.first().data.model,3u);QCOMPARE(points.first().data.known,127u);}
      }
    }
  }
  void usbLegacyProjectionAcceptedByFrozenOldParser() {
    auto v1=archiveRecord(1);v1.magic=HistoryFormat::kMagic;v1.crc=HistoryFormat::crc32(&v1,28);
    auto v2=archiveRecord(3);v2.magic=HistoryFormat::header(0x3f,2);v2.crc=HistoryFormat::crc32(&v2,28);
    TestArchiveSource source;source.input={v1,v2};TestArchiveSink sink;
    HistoryFormat::ExportSession writer(source,sink);writer.start(0,0,1,3);
    for(quint32 tick=0;writer.active()&&tick<100;++tick)writer.pump(tick);
    QVERIFY(!writer.active());int count=0;
    for(const auto &line:sink.bytes.split('\n')){
      const auto o=QJsonDocument::fromJson(line).object();if(!o["history_record"].toBool())continue;
      // Frozen pre-AHP2 reconstruction: it always puts AHP1 in the CRC bytes.
      HistoryFormat::Record old{};old.magic=0x31504841;
      old.sequence=quint32(o["sequence"].toDouble());old.session=quint32(o["session"].toDouble());
      old.uptimeSeconds=quint32(o["uptime_s"].toDouble());old.epochSeconds=quint32(o["epoch"].toDouble());
      old.soc=quint8(o["soc"].toInt());old.chargeWatts=quint16(o["charge_w"].toInt());
      old.outputWatts=quint16(o["output_w"].toInt());old.runtimeMinutes=quint16(o["runtime_minutes"].toInt());
      old.flags=quint8(o["flags"].toInt());old.crc=quint32(o["crc"].toDouble());
      QCOMPARE(old.crc,HistoryFormat::crc32(&old,28));
      if(old.sequence==3)QVERIFY(!(old.flags&HistoryFormat::Fresh));
      ++count;
    }
    QCOMPARE(count,2);QCOMPARE(source.input[1].magic,v2.magic);QCOMPARE(source.input[1].crc,v2.crc);
  }
  void usbLegacyPaginationRecoversRetainedRecords() {
    for(int shape:{0,1,2,3}){
      TestArchiveSource source;
      if(shape==0){source.input.append(archiveRecord(1));for(quint32 n=3;n<=2881;++n)source.input.append(archiveRecord(n));}
      if(shape==1)source.input={archiveRecord(1),archiveRecord(3)};
      if(shape==2)source.input={archiveRecord(UINT32_MAX),archiveRecord(1)};
      if(shape==3){source.input={archiveRecord(1),archiveRecord(2817)};
        // Initial recovery retains 1 and sector-start 2817. Torn slots
        // 2818..2944 force the next append to sector-start 2945; record 1
        // remains in RAM after its physical flash sector is erased.
        for(quint32 sequence=2945;sequence<=5822;++sequence)source.input.append(archiveRecord(sequence));}
      const auto latest=source.input.last().sequence;const auto expected=source.input.size();
      auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
      QSignalSpy imported(&usb,&TelemetryTransport::historyImported);
      usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
      QJsonObject inventory{{"history_ready",true},{"history_records",int(expected)},
        {"history_latest",double(latest)},{"history_session",7}};
      io->feed(QJsonDocument(inventory).toJson(QJsonDocument::Compact)+'\n');usb.requestHistory();
      int pages=0;
      while(usb.diagnostics()["usb_history_busy"].toBool()&&pages<10){
        const auto at=io->writes.lastIndexOf("HISTORY-READ ");
        if(at<0)break;const auto command=io->writes.mid(at).split('\n').first().split(' ');
        if(command.size()!=3)break;
        const quint32 first=command[1].toUInt(),span=command[2].toUInt();io->writes.clear();
        TestArchiveSink sink;HistoryFormat::ExportSession writer(source,sink);writer.start(0,0,first,span);
        for(quint32 tick=0;writer.active()&&tick<12000;++tick)writer.pump(tick);
        QVERIFY(!writer.active());
        if(shape==1)sink.bytes.prepend(sink.bytes.left(sink.bytes.indexOf('\n')+1)); // Retransmission is deduplicated.
        for(qsizetype offset=0;offset<sink.bytes.size();offset+=2048)io->feed(sink.bytes.mid(offset,2048));
        ++pages;
        // Legacy requires a final unchanged STATUS inventory after receiving
        // all advertised unique records; the unversioned done alone is insufficient.
        io->feed(QJsonDocument(inventory).toJson(QJsonDocument::Compact)+'\n');
      }
      QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("complete"));
      QCOMPARE(usb.diagnostics()["usb_history_received"].toInt(),int(expected));
      QCOMPARE(imported.size(),1);QCOMPARE(qvariant_cast<HistoryPoints>(imported.first()[0]).size(),expected);
      if(shape==0)QCOMPARE(pages,2);if(shape==3)QCOMPARE(pages,3);
    }
  }
  void usbLegacyPaginationHasExplicitBoundsAndInventoryChanges() {
    for(int mode:{0,1,2,3,4,5}){
      qint64 now=0;auto *io=new FakeUsbChannel;
      UsbTransport usb(nullptr,false,io,fakeUsbPorts,[&]{return now;},deterministicHistoryEntropy());
      usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
      io->feed("{\"history_ready\":true,\"history_records\":2,\"history_latest\":3,\"history_session\":7}\n");
      usb.requestHistory();
      if(mode==0){
        io->feed("{\"history_dump_done\":true}\n{\"history_dump_done\":true}\n{\"history_dump_done\":true}\n");
        QCOMPARE(usb.diagnostics()["usb_history_pages"].toInt(),3);
        QVERIFY(io->writes.contains(" 128\n"));
      }else if(mode==1)io->feed("{\"history_ready\":true,\"history_records\":2,\"history_latest\":3,\"history_session\":8}\n");
      else if(mode==2)io->feed("{\"history_ready\":true,\"history_records\":3,\"history_latest\":4,\"history_session\":7}\n");
      else if(mode==3){now=120001;QTest::qWait(600);}
      else {
        if(mode==4){
          for(quint32 sequence:{1u,3u}){char line[512];const auto record=archiveRecord(sequence);
            const int size=HistoryFormat::serializeLegacy(record,line,sizeof(line));io->feed(QByteArray(line,size));}
          io->feed("{\"history_dump_done\":true}\n");
        }
        io->feed("{\"history_ready\":false,\"history_records\":2,\"history_latest\":3,\"history_session\":7,\"history_error\":\"read\"}\n");
        if(mode==5)io->feed("{\"history_dump_done\":true}\n");
      }
      QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),mode>=3?QString("error"):QString("partial"));
      QVERIFY(!usb.diagnostics()["usb_history_complete"].toBool());
    }
  }
  void usbLegacyReadErrorAfterFinalRecordIsError() {
    for(bool error:{false,true}){
      auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
      usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
      const QByteArray inventory="{\"history_ready\":true,\"history_records\":2,\"history_latest\":3,\"history_session\":7}\n";
      io->feed(inventory);usb.requestHistory();
      const auto command=io->writes.mid(io->writes.lastIndexOf("HISTORY-READ ")).split('\n').first().split(' ');
      TestArchiveSource source;source.input={archiveRecord(1),archiveRecord(3)};
      source.busyOpen=source.busyRead=3;if(error)source.errorAt=2;
      TestArchiveSink sink;sink.chunk=11;sink.failedWrites=2;
      HistoryFormat::ExportSession writer(source,sink);writer.start(0,0,command[1].toUInt(),command[2].toUInt());
      for(quint32 tick=0;writer.active()&&tick<1000;++tick)writer.pump(tick);
      QVERIFY(!writer.active());io->feed(sink.bytes);io->feed(inventory);
      QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),error?QString("error"):QString("complete"));
      QCOMPARE(usb.diagnostics()["usb_history_received"].toInt(),2);
    }
  }
  void usbInvalidAndEmptyInventoriesCannotClaimComplete() {
    for(const auto &inventory:QList<QByteArray>{
      "{\"history_ready\":true,\"history_records\":2,\"history_latest\":0,\"history_session\":7}\n",
      "{\"history_ready\":true,\"history_records\":0,\"history_latest\":4,\"history_session\":7}\n",
      "{\"history_ready\":true,\"history_records\":0,\"history_latest\":0,\"history_session\":0}\n",
      "{\"history_ready\":true,\"history_records\":2,\"history_latest\":4,\"history_oldest\":0,\"history_export_version\":2,\"history_session\":7}\n",
      "{\"history_ready\":true,\"history_records\":0,\"history_latest\":0,\"history_oldest\":4,\"history_export_version\":2,\"history_session\":7}\n"}){
      auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
      usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());io->feed(inventory);usb.requestHistory();
      QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("error"));
      QVERIFY(!usb.diagnostics()["usb_history_complete"].toBool());
    }
    auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
    usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
    const QByteArray empty="{\"history_ready\":true,\"history_records\":0,\"history_latest\":0,\"history_session\":7}\n";
    io->feed(empty);io->writes.clear();usb.requestHistory();
    QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("reading"));
    QVERIFY(io->writes.contains("STATUS\n"));
    io->feed(empty);
    QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("complete"));
  }
  void usbEmptyArchivesRequireProtocolSpecificConfirmation() {
    for(int mode:{0,1,2,3,4}){
      auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
      usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
      const QByteArray empty="{\"history_ready\":true,\"history_records\":0,\"history_latest\":0,\"history_session\":7}\n";
      if(mode==0){
        io->feed("{\"history_ready\":true,\"history_records\":0,\"history_latest\":0,\"history_oldest\":0,\"history_export_version\":2,\"history_session\":7}\n");
        usb.requestHistory();
        QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("partial"));
      }else {
        io->feed(empty);usb.requestHistory();
        QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("reading"));
        if(mode==1)io->feed("{\"history_ready\":true,\"history_records\":1,\"history_latest\":1,\"history_session\":7}\n");
        if(mode==2)io->feed("{\"history_ready\":true,\"history_records\":0,\"history_latest\":0,\"history_session\":8}\n");
        if(mode==3)io->feed("{\"history_ready\":false,\"history_records\":0,\"history_latest\":0,\"history_session\":7}\n");
        if(mode==4)io->feed("{\"history_ready\":true,\"history_records\":1,\"history_latest\":0,\"history_session\":7}\n");
        QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),mode>=3?QString("error"):QString("partial"));
      }
      QVERIFY(!usb.diagnostics()["usb_history_complete"].toBool());
    }
    auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
    usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
    io->feed("{\"history_ready\":true,\"history_records\":0,\"history_latest\":0,\"history_oldest\":0,\"history_export_version\":3,\"history_session\":7}\n");
    usb.requestHistory();QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("reading"));
    TestArchiveSource source;TestArchiveSink sink;
    HistoryFormat::ExportSession writer(source,sink);writer.start(1,0);
    for(quint32 tick=0;writer.active()&&tick<100;++tick)writer.pump(tick);
    QVERIFY(!writer.active());io->feed(sink.bytes);
    QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("complete"));
    QVERIFY(usb.diagnostics()["usb_history_complete"].toBool());
  }
  void usbVersionedRetryResynchronizesAbandonedLine() {
    for(int mode:{0,1,2,3,4}){
      qint64 now=0;auto *io=new FakeUsbChannel;
      UsbTransport usb(nullptr,false,io,fakeUsbPorts,[&]{return now;},deterministicHistoryEntropy());
      QSignalSpy imported(&usb,&TelemetryTransport::historyImported);
      usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
      io->feed("{\"history_ready\":true,\"history_records\":2,\"history_latest\":3,\"history_oldest\":1,\"history_export_version\":3,\"history_session\":7}\n");
      usb.requestHistory();TestArchiveSource source;source.input={archiveRecord(1),archiveRecord(3)};
      TestArchiveSink sink;sink.chunk=7;
      HistoryFormat::ExportSession writer(source,sink);writer.start(1,0);
      bool partial=false;
      for(quint32 tick=0;tick<1000;++tick){
        writer.pump(tick);
        const auto tail=sink.bytes.mid(sink.bytes.lastIndexOf('\n')+1);
        if(tail.contains("\"history_record\":true")&&!tail.contains('\n')){partial=true;break;}
      }
      QVERIFY(partial);QVERIFY(writer.active());QCOMPARE(writer.acknowledged(),0u);
      const auto prefix=sink.bytes.size();io->feed(sink.bytes);
      if(mode==0||mode==3)writer.pump(62001); // Actual firmware hard deadline.
      if(mode==1)writer.cancel();
      now=70001;QTest::qWait(600); // Actual desktop deadline releases request 1.
      QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),QString("error"));
      imported.clear();usb.requestHistory();QVERIFY(io->writes.contains("HISTORY-EXPORT 2\n"));
      if(mode!=2)writer.cancel(); // Production beginHistoryExport also cancels an inactive predecessor.
      writer.start(2,62002);sink.failedWrites=3;sink.chunk=13;
      for(quint32 tick=62003;tick<62006;++tick){writer.pump(tick);QCOMPARE(sink.bytes.size(),prefix);QCOMPARE(writer.acknowledged(),0u);}
      for(quint32 tick=62006;writer.active()&&tick<64000;++tick)writer.pump(tick);
      QVERIFY(!writer.active());QCOMPARE(writer.acknowledged(),3u);
      auto retry=sink.bytes.mid(prefix);QVERIFY(retry.startsWith('\n'));
      // Delayed complete frames of the old request are also outside request 2.
      TestArchiveSource previous;previous.input=source.input;TestArchiveSink prior;
      HistoryFormat::ExportSession oldWriter(previous,prior);oldWriter.start(1,0);
      for(quint32 tick=0;oldWriter.active()&&tick<100;++tick)oldWriter.pump(tick);
      QVERIFY(!oldWriter.active());retry.insert(1,prior.bytes);
      if(mode>=3){
        const auto ownBegin=retry.indexOf("\"request\":2");QVERIFY(ownBegin>=0);
        const auto newline=retry.indexOf('\n',ownBegin);QVERIFY(newline>=0);
        if(mode==3)retry.insert(newline+1,"corruption after the new begin\n");
        else for(const auto &row:prior.bytes.split('\n')){
          if(QJsonDocument::fromJson(row).object()["history_record"].toBool()){
            retry.insert(newline+1,row+'\n');break;
          }
        }
      }
      for(qsizetype offset=0;offset<retry.size();offset+=17)io->feed(retry.mid(offset,17));
      QCOMPARE(usb.diagnostics()["usb_history_result"].toString(),mode==3?QString("partial"):QString("complete"));
      QCOMPARE(usb.diagnostics()["usb_history_complete"].toBool(),mode!=3);
      QCOMPARE(imported.size(),1);QCOMPARE(qvariant_cast<HistoryPoints>(imported.first()[0]).size(),2);
    }
  }
  void usbLegacyMissingRecordContinuesPagination() {
    auto *io=new FakeUsbChannel;UsbTransport usb(nullptr,false,io,fakeUsbPorts,{},deterministicHistoryEntropy());
    usb.resume("R2500-000000000001",{},0);io->feed(usbCycle());
    io->feed("{\"history_ready\":true,\"history_records\":2880,\"history_latest\":2881,\"history_session\":7}\n");
    usb.requestHistory();io->writes.clear();
    io->feed("{\"history_dump_done\":true}\n");
    QVERIFY(io->writes.contains("HISTORY-READ "));
  }
  void usbHistoryOverlayKeepsProvenanceAndPrefersStation() {
    HistoryPoint live;
    live.address = "02:00:00:00:00:03";
    live.wall = 600;
    live.tick = 10;
    live.session = "desktop";
    live.data.fresh = 1;
    HistoryPoint archive = live;
    archive.address = "usb-board:R2500-000000000001";
    archive.session = "board";
    archive.stride = 60;
    archive.wall = 540;
    archive.tick = 540;
    auto overlap = archive;
    overlap.wall = 620;
    overlap.tick = 620;
    auto result = mergeUsbHistory({live}, {overlap, archive});
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.first().address, archive.address);
    QCOMPARE(result.first().wall, 540);
    QCOMPARE(result.last().address, live.address);
    QVERIFY(joinPoints(result.first(), result.last()));
    QCOMPARE(mergeUsbHistory({}, {archive}).size(), 1);
    QCOMPARE(mergeUsbHistory({live}, {}).size(), 1);
  }
  void usbHistoryDrawsOneFusedLine() {
    HistoryChart chart;
    chart.resize(720, 360);
    const qint64 now = 100000;
    HistoryPoints board;
    for (int age : {180, 120, 60}) {
      HistoryPoint p;
      p.address = "usb-board:R2500-000000000001";
      p.session = "usb-7";
      p.wall = now - age;
      p.tick = 1000 - age;
      p.stride = 60;
      p.data.fresh = 1;
      p.data.known = 5;
      p.data.soc = 40;
      p.data.output_w = 100;
      board.append(p);
    }
    HistoryPoint live = board[1];
    live.address = "02:00:00:00:00:03";
    live.session = "desktop";
    live.stride = 1;
    live.data.soc = 70;
    live.data.output_w = 200;
    const auto shown = mergeUsbHistory({live}, board);
    QCOMPARE(shown.size(), 3);
    auto coloredAt = [&](const QImage &image, int age, double fraction, bool white = false) {
      const int x = qRound(54 + (chart.width() - 116) * (1. - age / 240.));
      const int y = qRound(chart.height() - 48 - (chart.height() - 78) * fraction);
      for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy) {
          const auto color = image.pixelColor(x + dx, y + dy);
          if (white ? color.red() > 200 && color.green() > 200 && color.blue() > 200
                    : color.green() > 170 && color.red() < 140 && color.blue() < 210)
            return true;
        }
      return false;
    };
    chart.setHorizon(240);
    chart.setPoints(shown, now);
    QImage image(chart.size(), QImage::Format_ARGB32_Premultiplied);
    chart.render(&image);
    QVERIFY(joinPoints(shown[0], shown[1]));
    QVERIFY(joinPoints(shown[1], shown[2]));
    QVERIFY(coloredAt(image, 150, .55));
    QVERIFY(coloredAt(image, 90, .55));
    QVERIFY(coloredAt(image, 150, .75, true)); // A single power segment to the live sample.
    QVERIFY(!coloredAt(image, 150, .5, true)); // No second 100 W archive line underneath.
    // A real missing/stale board sample must still leave a visible gap.
    board[1].data.fresh = 0;
    chart.setPoints(mergeUsbHistory({live}, board), now);
    chart.render(&image);
    QVERIFY(!coloredAt(image, 150, .55));
    QVERIFY(!coloredAt(image, 90, .55));
    // A boot boundary must not be connected either.
    board[1].data.fresh = 1;
    board[1].session = "usb-8";
    chart.setPoints(mergeUsbHistory({live}, board), now);
    chart.render(&image);
    QVERIFY(!coloredAt(image, 150, .55));
    QVERIFY(!coloredAt(image, 90, .55));
    // The live tail can connect only to the verified current board boot.
    live.wall = board.last().wall + 65;
    auto tail = mergeUsbHistory({live}, {board.last()});
    QVERIFY(!joinPoints(tail[0], tail[1]));
    tail = mergeUsbHistory({live}, {board.last()}, "usb-7");
    QVERIFY(joinPoints(tail[0], tail[1]));
    HistoryPoint decoded = tail[1];
    QVERIFY(Ipc::point(Ipc::point(decoded), decoded));
    QVERIFY(!decoded.bridgeFromPrevious);
    tail = mergeUsbHistory({live}, {board.last()}, "usb-9");
    QVERIFY(!joinPoints(tail[0], tail[1]));
  }
  void usbWindowRecordsLiveAndMergesArchive() {
    QTemporaryDir dir;
    qputenv("ALLPOWERS_DATA_DIR", dir.path().toUtf8());
    const auto restore =
        qScopeGuard([this] { qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8()); });
    // The retained board archive must appear immediately, before another dump.
    const auto now = QDateTime::currentSecsSinceEpoch();
    HistoryPoint old;
    old.address = "usb-board:R2500-000000000001";
    old.session = "old-board-session";
    old.wall = now - 300;
    old.tick = 10;
    old.stride = 60;
    old.data.fresh = 1;
    old.data.known = 7;
    old.data.soc = 60;
    old.data.output_w = 120;
    {
      HistoryStore store(dir.filePath("history.sqlite3"));
      QVERIFY(store.importPoints({old}, now));
    }
    auto *io = new FakeUsbChannel;
    auto *usb = new UsbTransport(nullptr, false, io, fakeUsbPorts,{},deterministicHistoryEntropy());
    MonitorWindow window(false, nullptr, usb);
    window.show();
    usb->resume("R2500-000000000001", {}, 0);
    io->feed(usbCycle());
    QTRY_COMPARE(window.findChild<QLabel *>("chargeValue")->text(), QString("50%"));
    const auto shots = qEnvironmentVariable("ALLPOWERS_TEST_SHOTS");
    if (!shots.isEmpty()) {
      QDir().mkpath(shots);
      QTest::qWait(30);
      window.grab().save(QDir(shots).filePath("usb-overview.png"));
    }
    QTRY_VERIFY(window.diagnostics()["history"].toObject()["live"].toInt() > 0);
    window.findChild<QButtonGroup *>()->button(1)->click();
    auto *scope = window.findChild<QComboBox *>("historySource");
    QVERIFY(scope->isVisible());
    QCOMPARE(scope->currentIndex(), 0);
    auto hasOldArchive = [&] {
      const auto &shown = window.historyChart()->points();
      return std::any_of(shown.cbegin(), shown.cend(), [&](const auto &p) {
        return p.address == old.address && p.wall == old.wall;
      });
    };
    QTRY_VERIFY(hasOldArchive());
    QTRY_VERIFY(window.historyChart()->points().size() >= 2);
    QTRY_VERIFY(io->writes.contains("HISTORY-EXPORT 1 1\n")); // Automatic archive read.
    const QByteArray dump =
        "{\"history_record\":true,\"sequence\":1,\"session\":7,\"uptime_s\":100,\"epoch\":0,"
        "\"soc\":50,\"charge_w\":0,\"output_w\":150,\"runtime_minutes\":30,\"flags\":13,"
        "\"crc\":3397945926}\n{\"history_dump_done\":true}\n";
    io->feed(dump);
    QTRY_COMPARE(window.diagnostics()["history"].toObject()["usb_minutes"].toInt(), 2);
    QVERIFY(hasOldArchive());
    QVERIFY(window.findChild<QLabel *>("usbHistoryNote")->text().contains("2"));
    scope->setCurrentIndex(1);
    QTRY_COMPARE(window.historyChart()->points().size(), 2);
    scope->setCurrentIndex(0);
    QTRY_VERIFY(hasOldArchive());
    QVERIFY(std::any_of(window.historyChart()->points().cbegin(),
                        window.historyChart()->points().cend(),
                        [](const auto &p) { return !p.address.startsWith("usb-board:"); }));
    usb->requestHistory();
    io->feed(dump);
    QTRY_COMPARE(window.diagnostics()["history"].toObject()["usb_minutes"].toInt(), 2);
    const auto csvPath = dir.filePath("combined.csv");
    QVERIFY(exportHistoryCsv(window.historyChart()->points(), csvPath));
    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly));
    const auto exported = csv.readAll();
    QVERIFY(exported.contains("usb-board:R2500-000000000001"));
    QVERIFY(exported.contains("02:00:00:00:00:03"));
    QTRY_VERIFY(window.findChild<QPushButton *>("usbHistoryRead")->isVisible());
    if (!shots.isEmpty()) {
      window.grab().save(QDir(shots).filePath("usb-history.png"));
      window.findChild<QButtonGroup *>()->button(2)->click();
      io->feed("{\"diagnostic\":true,\"rows\":[{\"label\":\"HEAP\",\"value\":\"123456\"},{"
               "\"label\":\"USB\",\"value\":\"HOST\"}]}\n");
      window.findChild<QTabWidget *>("diagnosticTabs")->setCurrentIndex(1);
      QTest::qWait(30);
      window.grab().save(QDir(shots).filePath("usb-diagnostics.png"));
    }
    usb->resume("R2500-000000000002", {}, 0);
    QTRY_VERIFY(!hasOldArchive()); // Switching boards must remove the prior overlay.
    window.close();
    QVERIFY(!usb->active());
    QVERIFY(!io->writes.contains("SELECT"));
    QVERIFY(!io->writes.contains("BOOT"));
  }
  void savedUsbResumesWithoutComputerBluetooth() {
    QTemporaryDir dir;
    qputenv("ALLPOWERS_DATA_DIR", dir.path().toUtf8());
    const auto restore =
        qScopeGuard([this] { qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8()); });
    QFile settings(dir.filePath("settings.json"));
    QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write("{\"transport\":\"usb\",\"usb_serial\":\"R2500-000000000001\",\"address\":\"02:"
                   "00:00:00:00:02\"}");
    settings.close();
    int bluetoothCreates = 0;
    FakeUsbChannel *io = nullptr;
    auto factory = [&](const QString &mode, QObject *parent) -> TelemetryTransport * {
      if (mode == "usb") {
        io = new FakeUsbChannel;
        return new UsbTransport(parent, false, io, fakeUsbPorts,{},deterministicHistoryEntropy());
      }
      bluetoothCreates++;
      return new BleTransport(parent, true, false);
    };
    auto *router = new TransportRouter(nullptr, false, false, "usb", factory,
                                     "ALLPOWERS.Test." + dir.path().section('/', -1));
    MonitorWindow window(false, nullptr, router);
    window.show();
    QTRY_VERIFY(io && !io->openedPort.isEmpty());
    QCOMPARE(bluetoothCreates, 0);
    QVERIFY(router->usb());
    io->feed(usbCycle("pocket-096"));
    QTRY_COMPARE(window.diagnostics()["address"].toString(), QString("02:00:00:00:00:03"));
    window.close();
  }
  void switchingServiceViewerToUsbDoesNotStopSystemConnection() {
    QTemporaryDir ui, service;
    qputenv("ALLPOWERS_DATA_DIR", ui.path().toUtf8());
    const auto restore =
        qScopeGuard([this] { qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8()); });
    const auto pipe = "AllpowersTest-" + QUuid::createUuid().toString(QUuid::Id128);
    ServiceHost host({service.path(), pipe, true, false});
    QVERIFY(host.start());
    auto factory = [&](const QString &mode, QObject *parent) -> TelemetryTransport * {
      if (mode == "usb")
        return new UsbTransport(parent, true);
      return new ServiceTransport(parent, pipe, false);
    };
    auto *router = new TransportRouter(nullptr, false, true, "bluetooth", factory);
    MonitorWindow window(false, nullptr, router);
    window.show();
    QTRY_VERIFY(router->available());
    router->resume("02:00:00:00:00:03", "Synthetic station", 3);
    QTRY_VERIFY(router->active());
    window.findChild<QComboBox *>("transportMode")->setCurrentIndex(1);
    QVERIFY(router->usb());
    QTest::qWait(30);
    QVERIFY(host.diagnostics()["diagnostics"].toObject()["active"].toBool());
    window.close();
    QVERIFY(host.diagnostics()["diagnostics"].toObject()["active"].toBool());
  }
  void ipcRejectsInvalidAndSplitFrames() {
    auto bytes = Ipc::frame({{"v", 1}, {"op", "state"}});
    QByteArray partial = bytes.left(3);
    QList<QJsonObject> messages;
    QVERIFY(Ipc::consume(partial, messages));
    QVERIFY(messages.isEmpty());
    partial += bytes.mid(3);
    QVERIFY(Ipc::consume(partial, messages));
    QCOMPARE(messages.size(), 1);
    QByteArray bad = Ipc::frame({{"v", 2}, {"op", "state"}});
    QVERIFY(!Ipc::consume(bad, messages));
    bad = QByteArray::fromHex("ffffffff");
    QVERIFY(!Ipc::consume(bad, messages));
    ap_snapshot data{};
    data.abi_version = 1;
    data.soc = 100;
    auto json = Ipc::snapshot(data);
    ap_snapshot decoded{};
    QVERIFY(Ipc::snapshot(json, decoded));
    QCOMPARE(decoded.soc, 100u);
    json["soc"] = 101;
    QVERIFY(!Ipc::snapshot(json, decoded));
    json["soc"] = 0;
    json["runtime_s"] = -1;
    QVERIFY(!Ipc::snapshot(json, decoded));
  }
  void serviceOwnsConnectionAcrossMonitorClose() {
    QTemporaryDir ui, serverData;
    qputenv("ALLPOWERS_DATA_DIR", ui.path().toUtf8());
    const auto restore =
        qScopeGuard([this] { qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8()); });
    const QString pipe = "AllpowersTest-" + QUuid::createUuid().toString(QUuid::Id128);
    ServiceHost host({serverData.path(), pipe, true, false});
    QVERIFY2(host.start(), qPrintable(host.error()));
    {
      auto *client = new ServiceTransport(nullptr, pipe, false);
      MonitorWindow window(false, nullptr, client);
      window.show();
      QTRY_VERIFY(client->diagnostics()["service"].toBool());
      QTRY_VERIFY(client->diagnostics().contains("application"));
      client->resume("02:00:00:00:00:03", "AP R2500 V2.0", 3);
      QTRY_VERIFY(client->active());
      QTRY_COMPARE(client->address(), QString("02:00:00:00:00:03"));
      QCOMPARE(host.diagnostics()["diagnostics"].toObject()["connections"].toInt(), 0);
      ServiceTransport second(nullptr, pipe, false);
      QTRY_VERIFY(second.active());
      window.close();
      QTest::qWait(30);
      QVERIFY(second.active()); // Closing a viewer must never stop the system UPS.
      QSignalSpy historyReceived(&second, &ServiceTransport::historyReady);
      ap_snapshot sample{};
      sample.abi_version = 1;
      sample.model = 3;
      sample.fresh = sample.main_seen = sample.settings_seen = 1;
      sample.known = 1 | 512;
      sample.soc = 42;
      sample.source = 2;
      QVERIFY(host.injectLabSnapshot(sample));
      QTRY_VERIFY(!historyReceived.isEmpty());
      QTRY_VERIFY(
          host.diagnostics()["diagnostics"].toObject()["history"].toObject()["live"].toInt() > 0);
      second.stop();
      QTRY_VERIFY(!client->active());
    }
    QFile config(serverData.filePath("settings.json"));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(config.readAll()).object()["profile"].toInt(), 3);
    HistoryStore localHistory(ui.filePath("history.sqlite3"));
    QCOMPARE(localHistory.stats()["live"].toInt(),
             0); // The viewer did not write a duplicate database.
    QLocalSocket malformed;
    malformed.connectToServer(pipe);
    QTRY_COMPARE(malformed.state(), QLocalSocket::ConnectedState);
    malformed.write(Ipc::frame({{"v", 99}, {"op", "select"}}));
    malformed.flush();
    QTRY_COMPARE(malformed.state(), QLocalSocket::UnconnectedState);
    QVERIFY(!host.diagnostics()["diagnostics"].toObject()["active"].toBool());
    QVERIFY(!integrationAction("not-an-operation").isEmpty());
    if (!integrationStatus()["virtual_machine"].toBool())
      QVERIFY(integrationAction("install").contains("test VM"));
  }
  void serviceAndLegacyHistoryMerge() {
    HistoryPoint minute;
    minute.address = "test";
    minute.wall = 120;
    minute.stride = 60;
    auto live = minute;
    live.wall = 125;
    live.stride = 1;
    live.data.soc = 55;
    auto duplicate = live;
    duplicate.data.soc = 56;
    auto merged = mergeHistory({minute, live}, {duplicate});
    QCOMPARE(merged.size(), 1);
    QCOMPARE(merged[0].data.soc, 56u);
    QTemporaryDir out;
    QVERIFY(exportHistoryCsv(merged, out.filePath("combined.csv")));
  }
  void failedSelectionKeepsPreviousStation() {
    QTemporaryDir data;
    const QString pipe = "AllpowersTest-" + QUuid::createUuid().toString(QUuid::Id128);
    ServiceHost host({data.path(), pipe, true, false});
    QVERIFY(host.start());
    ServiceTransport client(nullptr, pipe, false);
    QTRY_VERIFY(client.available());
    client.resume("02:00:00:00:00:01", "Synthetic station", 3);
    QTRY_VERIFY(client.active());
    QTRY_COMPARE(client.address(), QString("02:00:00:00:00:01"));
    const auto settings = data.filePath("settings.json");
    QVERIFY(QFile::rename(settings, settings + ".saved"));
    QVERIFY(QDir().mkdir(settings)); // An unwritable target must not interrupt the working UPS.
    client.resume("02:00:00:00:00:02", "Other station", 1);
    QTRY_VERIFY(client.diagnostics()["error"].toString().contains("Could not save"));
    QVERIFY(client.active());
    QCOMPARE(client.address(), QString("02:00:00:00:00:01"));
  }
  void fixtureSession() {
    QFile file(QString(FIXTURE_DIR) + "/notifications.json");
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto rows = QJsonDocument::fromJson(file.readAll()).array();
    Session session;
    auto token = session.reset(3);
    int accepted = 0;
    for (int i = 0; i < rows.size(); ++i)
      accepted += session.receive(token, QByteArray::fromHex(rows[i].toString().toLatin1()),
                                  quint32(i * 100));
    QVERIFY(accepted > 20);
    QCOMPARE(session.snapshot(100000).valid_frames, quint32(accepted));
  }
  void historyBoundedCompatibleAndExport() {
    QTemporaryDir dir;
    auto path = dir.filePath("history.sqlite3");
    ap_snapshot s{};
    s.fresh = 1;
    s.known = 383;
    s.soc = 74;
    s.source = 1;
    s.output_w = 148;
    {
      HistoryStore h(path, 3, 4);
      QVERIFY(h.valid());
      for (int i = 0; i < 400; ++i)
        QVERIFY(h.record("test", s, 10000 + i, 100 + i));
      QCOMPARE(h.stats()["live"].toInt(), 3);
      QCOMPARE(h.stats()["minutes"].toInt(), 4);
      QVERIFY(!h.record("test", s, 10399, 499));
      auto points = h.points("test", 10400);
      QVERIFY(!points.isEmpty());
      QVERIFY(h.exportCsv("test", 10400, dir.filePath("out.csv")));
      h.newSession();
      QVERIFY(h.record("test", s, 9000, 500));
      QVERIFY(!h.points("test", 11000).isEmpty());
    }
    HistoryStore reloaded(path, 3, 4);
    QCOMPARE(reloaded.stats()["live"].toInt(), 3);
  }
  void transitionsAndGaps() {
    HistoryPoint a;
    a.address = "A";
    a.session = "session";
    a.wall = 100;
    a.tick = 1;
    a.data.fresh = 1;
    a.data.source = 2;
    auto b = a;
    b.wall++;
    b.tick++;
    b.data.source = 1;
    QVERIFY(joinPoints(a, b));
    QCOMPARE(sourceChange(a, b), 1);
    b.data.source = 0;
    QCOMPARE(sourceChange(a, b), 0);
    b.data.source = 1;
    b.session = "new";
    QVERIFY(!joinPoints(a, b));
    b = a;
    b.wall++;
    b.tick += 50;
    QVERIFY(!joinPoints(a, b));
    QCOMPARE(logPosition(0, 86400), 1.);
    QCOMPARE(logPosition(86400, 86400), 0.);
  }
  void settingsSurviveImmediateClose() {
    QTemporaryDir data;
    qputenv("ALLPOWERS_DATA_DIR", data.path().toUtf8());
    {
      MonitorWindow window(true);
      window.show();
      window.loadDemo();
      window.close();
    }
    qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8());
    QFile file(data.filePath("settings.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto settings = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(settings["page"].toString(), QString("История"));
    QCOMPARE(settings["horizon"].toInt(), 3600);
  }
  void f11RestoresPreviousWindowMode() {
    MonitorWindow window(true);
    window.resize(1120, 820);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    const auto originalSize = window.size();
    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QTest::keyClick(&window, Qt::Key_F11);
    QTRY_VERIFY(!window.isFullScreen());
    QVERIFY(!window.isMaximized());
    QCOMPARE(window.size(), originalSize);
    window.showMaximized();
    QTRY_VERIFY(window.isMaximized());
    window.findChild<QButtonGroup *>()->button(2)->click();
    auto *log = window.findChild<LiveLog *>("eventLog");
    log->setFocus();
    QTest::keyClick(log, Qt::Key_F11);
    QTRY_VERIFY(window.isFullScreen());
    QTest::keyClick(log, Qt::Key_F11);
    QTRY_VERIFY(!window.isFullScreen());
    QVERIFY(window.isMaximized());
  }
  void startupResumesSavedStationAndCanBeCancelled() {
    QTemporaryDir data;
    qputenv("ALLPOWERS_DATA_DIR", data.path().toUtf8());
    const auto restore =
        qScopeGuard([this] { qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8()); });
    QFile file(data.filePath("settings.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QString address = "02:00:00:00:00:01"; // Synthetic, locally administered address.
    file.write(
        QJsonDocument(QJsonObject{{"address", address}, {"name", "AP R2500 V2.0"}, {"profile", 3}})
            .toJson());
    file.close();
    {
      auto *backend = new BleTransport(nullptr, true);
      MonitorWindow window(false, nullptr, backend);
      window.show();
      QTRY_VERIFY(backend->active());
      QCOMPARE(backend->address(), address);
      QVERIFY(!backend->connected());
      QCOMPARE(backend->diagnostics()["connections"].toInt(), 0);
      QCOMPARE(window.findChild<QComboBox *>("devices")->currentData().toString(), address);
      QCOMPARE(window.findChild<QComboBox *>("profile")->currentIndex(), 3);
      auto *button = window.findChild<QPushButton *>("primary");
      QVERIFY(button->isEnabled());
      QCOMPARE(button->text(), QString("Отключить"));
      emit backend->found("02:00:00:00:00:02", "AP R600 V2.0", -30, 2);
      QCOMPARE(backend->address(), address);
      button->click();
      QVERIFY(!backend->active());
      emit backend->stateChanged();
      QTest::qWait(50);
      QVERIFY(!backend->active());
      // The saved entry can be retried without a separate manual scan.
      button->click();
      QVERIFY(backend->active());
      QCOMPARE(backend->address(), address);
      button->click();
      window.close();
    }
    {
      MonitorWindow demo(true);
      demo.show();
      QTest::qWait(30);
      QVERIFY(!demo.findChild<BleTransport *>()->active());
    }
    {
      auto *backend = new BleTransport(nullptr, true);
      MonitorWindow closed(false, nullptr, backend);
      closed.show();
      closed.close();
      QTest::qWait(30);
      QVERIFY(!backend->active());
    }
  }
  void startupWithoutSavedStationStaysIdle() {
    QTemporaryDir data;
    qputenv("ALLPOWERS_DATA_DIR", data.path().toUtf8());
    const auto restore =
        qScopeGuard([this] { qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8()); });
    auto *backend = new BleTransport(nullptr, true);
    MonitorWindow window(false, nullptr, backend);
    window.show();
    QTest::qWait(30);
    QVERIFY(!backend->active());
    QVERIFY(backend->address().isEmpty());
  }
  void overviewHasLargeSourceAndRuntimeCards() {
    MonitorWindow window(true);
    window.show();
    window.loadDemo();
    window.findChild<QButtonGroup *>()->button(0)->click();
    window.resize(930, 710);
    QTest::qWait(20);
    auto *source = window.findChild<QLabel *>("powerSource");
    auto *runtime = window.findChild<QLabel *>("remainingTime");
    QVERIFY(source && runtime);
    QVERIFY(source->parentWidget() != runtime->parentWidget());
    QCOMPARE(source->font().pixelSize(), 44);
    QCOMPARE(runtime->font().pixelSize(), 44);
    for (const auto &value :
         {QString("Розетка"), QString("Аккумулятор"), QString("Не определён")}) {
      source->setText(value);
      QVERIFY(source->fontMetrics().horizontalAdvance(value) <= source->contentsRect().width());
    }
    source->setText("Аккумулятор");
    for (const auto &value : {QString("6 ч 14 мин"), QString("26 д 16 ч")}) {
      runtime->setText(value);
      QVERIFY(runtime->fontMetrics().horizontalAdvance(value) <= runtime->contentsRect().width());
    }
    window.grab().save("overview-large-cards.png");
  }
  void overviewScalesWithWindow() {
    MonitorWindow window(true);
    window.show();
    window.loadDemo();
    window.findChild<QButtonGroup *>()->button(0)->click();
    window.resize(930, 710);
    QTest::qWait(20);
    auto *charge = window.findChild<AdaptiveLabel *>("chargeValue");
    auto *source = window.findChild<AdaptiveLabel *>("powerSource");
    auto *runtime = window.findChild<AdaptiveLabel *>("remainingTime");
    QVERIFY(charge && source && runtime);
    const int smallFont = charge->fittedFont().pixelSize();
    const int smallCard = charge->parentWidget()->height();
    for (const auto &size : {QSize(1920, 1080), QSize(3840, 2160)}) {
      window.resize(size);
      QTest::qWait(20);
      QVERIFY(charge->parentWidget()->height() > smallCard);
      QVERIFY(charge->fittedFont().pixelSize() > smallFont * 1.5);
      source->setText("Аккумулятор");
      runtime->setText("26 д 16 ч");
      for (auto *label : window.findChildren<AdaptiveLabel *>()) {
        const QFontMetrics metrics(label->fittedFont());
        QVERIFY2(metrics.horizontalAdvance(label->text()) <= label->contentsRect().width(),
                 qPrintable(label->text()));
        QVERIFY(metrics.height() <= label->contentsRect().height());
      }
      window.grab().save(QString("overview-%1x%2.png").arg(size.width()).arg(size.height()));
    }
    window.resize(930, 710);
    QTest::qWait(20);
    QCOMPARE(charge->fittedFont().pixelSize(), smallFont);
    source->setText("Не определён");
    runtime->setText("999 д 23 ч");
    for (auto *label : {source, runtime}) {
      const QFontMetrics metrics(label->fittedFont());
      QVERIFY(metrics.horizontalAdvance(label->text()) <= label->contentsRect().width());
    }
  }
  void dropdownFitsVisibleRows_data() {
    QTest::addColumn<int>("items");
    QTest::addColumn<bool>("lastSelected");
    for (int count : {1, 2, 3, 4, 8, 12})
      for (bool last : {false, true})
        QTest::newRow(qPrintable(QString("%1-%2").arg(count).arg(last))) << count << last;
  }
  void dropdownFitsVisibleRows() {
    QFETCH(int, items);
    QFETCH(bool, lastSelected);
    QTemporaryDir data;
    qputenv("ALLPOWERS_DATA_DIR", data.path().toUtf8());
    const auto restoreData =
        qScopeGuard([this] { qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8()); });
    MonitorWindow window(true);
    window.show();
    window.findChild<QComboBox *>("transportMode")->setCurrentIndex(1);
    auto *select = window.findChild<Select *>("historySource");
    const bool animate = QApplication::isEffectEnabled(Qt::UI_AnimateCombo);
    const auto restoreEffect =
        qScopeGuard([animate] { QApplication::setEffectEnabled(Qt::UI_AnimateCombo, animate); });
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, true);
    // The offscreen platform may report all UI effects unavailable.
    const bool effectiveAnimation = QApplication::isEffectEnabled(Qt::UI_AnimateCombo);
    if (items != 2) {
      select->clear();
      for (int i = 0; i < items; ++i)
        select->addItem(QString("Пункт %1 · длинная подпись").arg(i + 1));
    }
    select->setCurrentIndex(lastSelected ? items - 1 : 0);
    window.findChild<QButtonGroup *>()->button(1)->click();
    for (int attempt = 0; attempt < 2; ++attempt) {
      select->showPopup();
      QTest::qWait(220); // Include the completion of native popup effects.
      auto *view = select->view();
      const auto viewport = view->viewport()->rect();
      qInfo() << "Popup rows" << items << "selected" << select->currentIndex() << "popup"
              << view->window()->size() << "viewport" << viewport << "margins"
              << view->window()->contentsMargins() << "scroll"
              << view->verticalScrollBar()->minimum() << view->verticalScrollBar()->maximum();
      const auto screen = select->screen()->availableGeometry();
      const auto top = select->mapToGlobal(QPoint(0, 0)).y();
      const int space = qMax(top - screen.top(), screen.bottom() + 1 - top - select->height());
      int contentHeight = 0;
      for (int row = 0; row < items; ++row)
        contentHeight += view->sizeHintForRow(row);
      const int insets = view->window()->height() - viewport.height();
      const auto shots = qEnvironmentVariable("ALLPOWERS_TEST_SHOTS");
      if (items == 2 && attempt == 0 && !shots.isEmpty()) {
        QDir().mkpath(shots);
        auto joined = window.grab();
        {
          QPainter painter(&joined);
          painter.drawPixmap(window.mapFromGlobal(view->window()->pos()), view->window()->grab());
        }
        joined.save(QDir(shots).filePath(QString("dropdown-%1.png").arg(select->currentIndex())));
      }
      if (items <= select->maxVisibleItems() && contentHeight + insets <= space) {
        QCOMPARE(view->verticalScrollBar()->maximum(), 0);
        for (int row = 0; row < items; ++row)
          QVERIFY2(view->visualRect(select->model()->index(row, 0)).top() >= viewport.top() &&
                       view->visualRect(select->model()->index(row, 0)).bottom() <=
                           viewport.bottom(),
                   "A short popup must display each row completely");
      } else {
        QVERIFY(view->verticalScrollBar()->maximum() > 0);
        const auto selected = view->visualRect(select->model()->index(select->currentIndex(), 0));
        QVERIFY(selected.top() >= viewport.top() && selected.bottom() <= viewport.bottom());
      }
      select->hidePopup();
      QCOMPARE(QApplication::isEffectEnabled(Qt::UI_AnimateCombo), effectiveAnimation);
    }
  }
  void dropdownKeyboardAndLayout() {
    MonitorWindow window(true);
    window.show();
    auto *select = window.findChild<Select *>("profile");
    QVERIFY(select);
    select->showPopup();
    QTest::qWait(50);
    auto *view = select->view();
    QVERIFY(view->isVisible());
    const auto fieldTop = select->mapToGlobal(QPoint(0, 0));
    QCOMPARE(view->window()->geometry().left(), fieldTop.x());
    QCOMPARE(view->window()->geometry().top(), fieldTop.y() + select->height());
    QCOMPARE(view->window()->width(), select->width());
    QVERIFY(!view->window()->mask().contains(view->window()->rect().bottomLeft()));
    QVERIFY(view->window()->mask().contains(QPoint(0, 0)));
    QRect previous;
    for (int i = 0; i < select->count(); ++i) {
      auto row = view->visualRect(select->model()->index(i, 0));
      QCOMPARE(row.height(), 40);
      QVERIFY(row.width() >= select->width() - 12);
      QVERIFY(row.height() >= view->fontMetrics().height() + 12);
      if (i)
        QCOMPARE(row.top(), previous.bottom() + 1);
      previous = row;
    }
    QVERIFY(view->viewport()->height() >= previous.bottom());
    window.grab().save("dropdown-window.png");
    view->window()->grab().save("dropdown-popup.png");
    auto joined = window.grab();
    {
      QPainter painter(&joined);
      painter.drawPixmap(window.mapFromGlobal(view->window()->pos()), view->window()->grab());
    }
    joined.save("dropdown-joined.png");
    QTest::keyClick(view, Qt::Key_End);
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(select->currentIndex(), select->count() - 1);
    QVERIFY(!view->isVisible());
    select->showPopup();
    QTest::keyClick(view, Qt::Key_Home);
    QTest::keyClick(view, Qt::Key_Escape);
    QCOMPARE(select->currentIndex(), select->count() - 1);

    auto *devices = window.findChild<Select *>("devices");
    auto *transport = window.findChild<BleTransport *>();
    emit transport->found("demo-a", "AP R2500 V2", -70, 3);
    emit transport->found("demo-b", "AP R600", -80, 1);
    devices->showPopup();
    const QString first = devices->itemData(0).toString();
    emit transport->found("demo-b", "AP R600", -30, 1);
    emit transport->found("demo-c", "AP S700", -50, 1);
    QCOMPARE(devices->count(), 2);
    QCOMPARE(devices->itemData(0).toString(), first);
    devices->hidePopup();
    QCOMPARE(devices->count(), 3);
    QCOMPARE(devices->itemData(0).toString(), QString("demo-b"));
    QCOMPARE(devices->currentData().toString(), first);

    Select nearBottom;
    nearBottom.addItems({"First", "Second", "Third"});
    nearBottom.resize(250, 42);
    nearBottom.move(100, nearBottom.screen()->availableGeometry().bottom() - 60);
    nearBottom.show();
    nearBottom.showPopup();
    const auto upper = nearBottom.view()->window();
    QCOMPARE(upper->geometry().bottom() + 1, nearBottom.mapToGlobal(QPoint(0, 0)).y());
    QCOMPARE(upper->width(), nearBottom.width());
    QVERIFY(!upper->mask().contains(QPoint(0, 0)));
    QVERIFY(upper->mask().contains(upper->rect().bottomLeft()));
    nearBottom.hidePopup();
  }
  void logFollowsAndPauses() {
    LiveLog log;
    log.resize(600, 240);
    log.show();
    for (int i = 0; i < 100; ++i)
      log.appendEntry(QString("Event %1").arg(i));
    QTest::qWait(20);
    QVERIFY(log.following());
    log.verticalScrollBar()->setValue(20);
    QVERIFY(!log.following());
    auto cursor = log.textCursor();
    cursor.setPosition(10);
    cursor.setPosition(18, QTextCursor::KeepAnchor);
    log.setTextCursor(cursor);
    const int position = log.verticalScrollBar()->value();
    const QString selected = log.textCursor().selectedText();
    log.appendEntry("Arrives while reading older entries");
    QCOMPARE(log.verticalScrollBar()->value(), position);
    QCOMPARE(log.textCursor().selectedText(), selected);
    QVERIFY(!log.following());
    log.followTail();
    QVERIFY(log.following());
    for (int i = 0; i < 2100; ++i)
      log.appendEntry(QString("Event %1").arg(i));
    QCOMPARE(log.document()->blockCount(), 2000);
    QVERIFY(log.following());
    QVERIFY(log.toPlainText().endsWith("Event 2099"));
    QVERIFY(!log.toPlainText().contains("Event 99\n"));
    log.verticalScrollBar()->setValue(400);
    const auto firstVisible = log.cursorForPosition(QPoint(4, 4)).block().text();
    for (int i = 0; i < 100; ++i)
      log.appendEntry(QString("Later %1").arg(i));
    QCOMPARE(log.cursorForPosition(QPoint(4, 4)).block().text(), firstVisible);
    QVERIFY(!log.following());
  }
  void diagnosticLogKeepsGrowing() {
    MonitorWindow window(true);
    window.show();
    auto *transport = window.findChild<BleTransport *>();
    auto *log = window.findChild<LiveLog *>("eventLog");
    QVERIFY(transport && log);
    for (int i = 0; i < 80; ++i)
      emit transport->packet(QString("test %1").arg(i), true);
    auto *pages = window.findChild<QStackedWidget *>();
    pages->setCurrentIndex(2);
    QTest::qWait(20);
    QVERIFY(log->toPlainText().contains("RX test 0"));
    QVERIFY(log->toPlainText().endsWith("RX test 79"));
    QVERIFY(log->following());
    window.grab().save("diagnostic-log.png");
    log->verticalScrollBar()->setValue(0);
    emit transport->packet("newest", false);
    QCOMPARE(log->verticalScrollBar()->value(), 0);
    auto *follow = window.findChild<QPushButton *>("logFollow");
    QVERIFY(follow->isEnabled());
    follow->click();
    QVERIFY(log->following());
    QVERIFY(!follow->isEnabled());
  }
  void diagnosticTableUpdatesInPlace() {
    MonitorWindow window(true);
    window.show();
    window.findChild<QButtonGroup *>()->button(2)->click();
    window.findChild<QTabWidget *>("diagnosticTabs")->setCurrentIndex(1);
    auto *table = window.findChild<QTableWidget *>("diagnosticState");
    QVERIFY(table);
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QString("Параметр"));
    QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
    auto cell = [table](const QString &key) -> QTableWidgetItem * {
      for (int row = 0; row < table->rowCount(); ++row) {
        auto *item = table->item(row, 1);
        if (item && item->data(Qt::UserRole).toString() == key)
          return item;
      }
      return nullptr;
    };
    auto *fresh = cell("fresh");
    QVERIFY(fresh && cell("main_age_ms") && cell("source_raw"));
    QCOMPARE(fresh->text(), QString("Нет"));
    QCOMPARE(cell("main_age_ms")->text(), QString("—"));
    QCOMPARE(cell("source_raw")->text(), QString("—"));
    table->selectRow(fresh->row());
    table->verticalScrollBar()->setValue(10);
    const auto scroll = table->verticalScrollBar()->value();
    const auto selected = table->selectedItems().first();
    window.loadDemo();
    QCOMPARE(cell("fresh"), fresh);
    QCOMPARE(fresh->text(), QString("Да"));
    QCOMPARE(cell("main_age_ms")->text(), QString("0.1 с"));
    QCOMPARE(cell("source_raw")->text(), QString("0x0"));
    QCOMPARE(table->selectedItems().first(), selected);
    QCOMPARE(table->verticalScrollBar()->value(), scroll);
    auto *copy = window.findChild<QPushButton *>("diagnosticCopy");
    QCOMPARE(copy->text(), QString("Скопировать JSON"));
    copy->click();
    const auto copied =
        QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object();
    QVERIFY(copied["fresh"].toBool());
    QCOMPARE(copied["main_age_ms"].toInt(), 100);
    window.findChild<QButtonGroup *>()->button(2)->click();
    table->verticalScrollBar()->setValue(0);
    QTest::qWait(20);
    window.grab().save("diagnostic-table.png");
  }
  void nativeControlsAndChart() {
    MonitorWindow window(true);
    window.resize(1120, 820);
    window.show();
    window.loadDemo();
    QTest::qWait(100);
    auto *connect = window.findChild<QPushButton *>("primary");
    QVERIFY(connect);
    QVERIFY(!connect->isEnabled());
    QVERIFY(!connect->toolTip().isEmpty());
    auto *chart = window.historyChart();
    QVERIFY(chart->following());
    HistoryPoints full;
    auto now = QDateTime::currentSecsSinceEpoch();
    for (int i = 0; i < 6480; ++i) {
      HistoryPoint p;
      p.address = "test";
      p.session = "benchmark";
      p.wall = i < 2880 ? now - (2880 - i) * 60 - 3600 : now - 3599 + i - 2880;
      p.tick = p.wall;
      p.stride = i < 2880 ? 60 : 1;
      p.data.fresh = 1;
      p.data.known = 383;
      p.data.soc = quint32(50 + i % 50);
      p.data.input_w = quint32(i % 300);
      p.data.output_w = quint32(i % 2000);
      full.append(p);
    }
    chart->setPoints(full, now);
    chart->setHorizon(172800);
    QImage image(chart->size(), QImage::Format_ARGB32_Premultiplied);
    double worst = 0;
    QList<double> timings;
    for (int i = 0; i < 30; ++i) {
      chart->render(&image);
      worst = qMax(worst, chart->renderMs());
      timings.append(chart->renderMs());
    }
    std::sort(timings.begin(), timings.end());
    qInfo() << "Chart (6480 samples): median" << timings[15] << "ms; p95" << timings[28]
            << "ms; cold worst" << worst << "ms; vertices:" << chart->vertices();
    QList<double> redrawn;
    for (int i = 0; i < 20; ++i) {
      chart->setHorizon(172800 - i * 100);
      chart->render(&image);
      redrawn.append(chart->renderMs());
    }
    std::sort(redrawn.begin(), redrawn.end());
    qInfo() << "Full chart redraw: median" << redrawn[10] << "ms; p95" << redrawn[18] << "ms; max"
            << redrawn.last() << "ms";
    QFile benchmark("render-benchmark.json");
    if (benchmark.open(QIODevice::WriteOnly))
      benchmark.write(QJsonDocument(QJsonObject{{"samples", 6480},
                                                {"cached_median_ms", timings[15]},
                                                {"cached_p95_ms", timings[28]},
                                                {"full_median_ms", redrawn[10]},
                                                {"full_p95_ms", redrawn[18]},
                                                {"full_max_ms", redrawn.last()}})
                          .toJson());
    QVERIFY(chart->vertices() <= chart->width() * 12);
    QVERIFY(worst < 250);
    QPointF center = chart->rect().center();
    QWheelEvent wheel(center, chart->mapToGlobal(center.toPoint()), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(chart, &wheel);
    QVERIFY(!chart->following());
    chart->followLive();
    QVERIFY(chart->following());
  }
};
QTEST_MAIN(MonitorTests)
#include "monitor_test.moc"
