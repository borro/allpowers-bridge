#include "i18n.h"
#include "usb_transport.h"
#include "history_operation_fixture.h"
#include "frozen_b989/usb_transport.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>


#include "../../firmware/R2500Adapter/OutputController.h"
#include <QDateTime>
#include <QJsonArray>
#include <QSignalSpy>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

namespace {
struct ArchiveSource : HistoryFormat::ExportSource {
  QVector<HistoryFormat::Record> rows,memory;
  std::unique_ptr<HistoryFormat::FrozenArchive> frozen;
  int opens=0;
  int closeCalls=0,closedSnapshots=0,activeSnapshots=0,maxActiveSnapshots=0;
  quint32 session=7,generation=10;
  bool openError=false;
  HistoryFormat::ReadState open(HistoryFormat::ExportMetadata &metadata) override {
    ++opens;if(openError)return HistoryFormat::ReadState::Error;
    maxActiveSnapshots=std::max(maxActiveSnapshots,activeSnapshots+1);
    activeSnapshots=1;memory.resize(rows.size());
    frozen=std::make_unique<HistoryFormat::FrozenArchive>(memory.data(),quint32(memory.size()));
    return frozen->freeze(rows.constData(),0,quint32(rows.size()),quint32(rows.size()),session,generation,metadata);
  }
  HistoryFormat::ReadState read(uint32_t ordinal,HistoryFormat::Record &record) override {
    return frozen ? frozen->read(ordinal,record) : HistoryFormat::ReadState::Error;
  }
  void close() override {
    ++closeCalls;if(frozen)++closedSnapshots;
    frozen.reset();activeSnapshots=0;memory.clear();memory.squeeze();
  }
};
struct StatusInput : HistoryFormat::StatusProvider {
  ArchiveSource &source;
  int captures=0;
  qint64 historyBusyFrom=-1,historyBusyUntil=-1;
  explicit StatusInput(ArchiveSource &input) : source(input) {}
  bool capture(HistoryFormat::StatusSnapshot &frame,uint32_t now) override {
    ++captures;frame={};frame.nowMs=now;
    auto &state=frame.state;state.connected=true;
    strcpy(state.identity.address,"02:00:00:00:00:03");strcpy(state.identity.name,"AP R2500 V2.0");
    auto &data=state.telemetry;data.mainSeen=data.settingsSeen=true;
    data.mainAt=data.settingsAt=now;data.validFrames=1+now/1000;
    data.protocol=3;data.known=0x37f;data.soc=67;data.outputWatts=120;data.minutes=120;
    data.source=data.lastKnownSource=PowerSource::Battery;data.sourceRaw=0xffff;data.mainFlags=2;
    frame.ups.runtime_seconds=7200;frame.parserPassed=true;frame.displayReady=true;
    frame.boardId="waveshare-147";frame.panelName="synthetic";
    auto &log=frame.log;log.ready=log.selfTest=log.clockSynced=true;log.session=source.session;log.generation=source.generation;
    log.count=quint32(source.rows.size());log.error="none";
    if(!source.rows.isEmpty()){
      log.oldest=source.rows.first().sequence;log.latest=source.rows.last().sequence;
      log.liveLatest=source.rows.last().uptimeSeconds;
    }
    if(now>=historyBusyFrom&&now<historyBusyUntil){frame.historyAvailable=false;frame.log={};}
    return true;
  }
};
struct LoopChannel : UsbChannel {
  QByteArray commands;
  bool opened_=false;
  int opens=0,closes=0;
  void open(const QString &) override {opened_=true;++opens;emit opened();}
  void close() override {if(opened_)++closes;opened_=false;}
  void write(const QByteArray &bytes) override {commands+=bytes;}
  void feed(const QByteArray &bytes) {if(opened_)emit received(bytes);}
};
struct LoopSink : HistoryFormat::ExportSink {
  LoopChannel *&channel;qint64 &now;
  QByteArray wire,pending;
  size_t chunk=4096;
  qint64 stalledFrom=-1,stalledUntil=-1;
  explicit LoopSink(LoopChannel *&input,qint64 &clock) :channel(input),now(clock){}
  size_t write(const char *bytes,size_t length) override {
    if(!channel->opened_ || (now>=stalledFrom&&now<stalledUntil))return 0;
    const size_t accepted=std::min(chunk,length);
    wire.append(bytes,qsizetype(accepted));pending.append(bytes,qsizetype(accepted));return accepted;
  }
};
struct FirmwareLoop {
  qint64 now=0;
  LoopChannel *channel=new LoopChannel;
  ArchiveSource source;
  StatusInput input{source};
  LoopSink sink{channel,now};
  HistoryFormat::OutputController output{source,sink,input};
  explicit FirmwareLoop(int count) {
    const auto base=quint32(QDateTime::currentSecsSinceEpoch());
    for(int i=0;i<count;++i){
      auto record=HistoryFormat::example();record.sequence=quint32(i+1);record.session=7;
      record.uptimeSeconds=quint32(i*60);record.soc=quint8(i%101);
      const quint32 age=count>1 ? quint32((qint64(count-1-i)*172680)/(count-1)) : 0;
      record.epochSeconds=base-60-age;record.flags=HistoryFormat::Fresh|HistoryFormat::Battery|HistoryFormat::AcOutput|HistoryFormat::Clock;
      if(i%5==0){record.flags&=quint8(~HistoryFormat::Clock);record.epochSeconds=0;} // Exercise the actual production graph-time anchor too.
      record.magic=i%2 ? HistoryFormat::header(i%3 ? 0x37f : 0x3f,i%3 ? 3 : 2) : HistoryFormat::kMagic;
      record.crc=HistoryFormat::crc32(&record,offsetof(HistoryFormat::Record,crc));source.rows.append(record);
    }
  }
  // All host commands enter the production dispatcher before a device pump.
  // Accepted bytes reach the PC only afterwards, avoiding impossible reentrant I/O.
  void step(bool deliver=true) {
    for(int end;(end=channel->commands.indexOf('\n'))>=0;){
      const auto command=channel->commands.left(end);channel->commands.remove(0,end+1);
      if(command=="DIAG"){
        static const char diagnostic[]="{\"diagnostic\":true,\"rows\":[]}\n";
        output.queueOptional(diagnostic,sizeof(diagnostic)-1);
      }
      else output.command(command.constData(),quint32(now));
    }
    output.pump(quint32(now));
    if(deliver&&!sink.pending.isEmpty()){
      const auto bytes=std::exchange(sink.pending,{});channel->feed(bytes);
    }
    now+=5;
  }
  int archiveRows() const {
    int count=0;for(const auto &line:sink.wire.split('\n'))
      if(QJsonDocument::fromJson(line).object()["history_record"].toBool())++count;
    return count;
  }
  bool validLines() const {
    for(const auto &line:sink.wire.split('\n')){
      if(line.trimmed().isEmpty())continue;
      QJsonParseError error;const auto value=QJsonDocument::fromJson(line,&error);
      if(error.error!=QJsonParseError::NoError||!value.isObject())return false;
    }
    return true;
  }
};
QList<UsbPort> loopPorts() {return {{"R2500-000000000001","COM987","Synthetic firmware loop"}};}
bool hasHistoryRequest(const QByteArray &commands,quint32 request) {
  for(const auto &line:commands.split('\n')){
    const auto fields=line.split(' ');
    if(fields.size()>=2&&fields[0]=="HISTORY-EXPORT"&&fields[1]==QByteArray::number(request))return true;
  }
  return false;
}
bool bootstrap(FirmwareLoop &loop,UsbTransport &desktop) {
  desktop.resume("R2500-000000000001",{},0);
  for(const auto end=loop.now+5000;loop.now<end&&
      (!desktop.connected()||!desktop.diagnostics()["history_ready"].toBool());)loop.step();
  return desktop.connected()&&desktop.diagnostics()["history_ready"].toBool();
}
QByteArray matchingBeginWithPartialRow(FirmwareLoop &loop) {
  loop.sink.pending.clear();
  for(const auto end=loop.now+6000;loop.now<end;){
    loop.step(false);
    if(loop.sink.pending.contains("\"history_export_begin\":true")&&
       loop.sink.pending.contains("\"history_record\":true"))
      return std::exchange(loop.sink.pending,{});
  }
  return {};
}
QByteArray invalidMatchingBegin(QByteArray bytes,quint32 invalidSession) {
  int start=0;
  for(int end;(end=bytes.indexOf('\n',start))>=0;start=end+1){
    auto object=QJsonDocument::fromJson(bytes.mid(start,end-start)).object();
    if(object["history_export_begin"].toBool()){
      object["export_session"]=double(invalidSession);
      bytes.replace(start,end-start,QJsonDocument(object).toJson(QJsonDocument::Compact));return bytes;
    }
  }
  return {};
}
QByteArray completeBeginAtLineBoundary(FirmwareLoop &loop) {
  loop.sink.pending.clear();
  for(const auto end=loop.now+6000;loop.now<end;){
    loop.step(false);
    const auto lines=loop.sink.pending.split('\n');
    for(qsizetype i=0;i+1<lines.size();++i)
      if(QJsonDocument::fromJson(lines[i]).object()["history_export_begin"].toBool())
        return std::exchange(loop.sink.pending,{});
  }
  return {};
}
QByteArray capturedArchiveFrames(const QByteArray &wire,quint32 request) {
  QByteArray frames;
  for(const auto &line:wire.split('\n')){
    const auto object=QJsonDocument::fromJson(line).object();
    if(object["request"].toInteger()==request&&object["history_export_version"].toInt()==3&&
       (object["history_export_begin"].toBool()||object["history_record"].toBool()||object["history_dump_done"].toBool()))
      frames+=line+'\n';
  }
  return frames;
}
bool completeWithin(FirmwareLoop &loop,UsbTransport &desktop,qint64 duration) {
  for(const auto end=loop.now+duration;loop.now<end;){
    loop.step();if(loop.now%500==0)desktop.refresh();
    if(desktop.diagnostics()["usb_history_result"]=="complete")return true;
  }
  return false;
}
void dateEveryRecord(FirmwareLoop &loop) {
  const auto epoch=quint32(QDateTime::currentSecsSinceEpoch()-60);
  for(auto &row:loop.source.rows){row.epochSeconds=epoch;row.flags|=HistoryFormat::Clock;
    row.crc=HistoryFormat::crc32(&row,offsetof(HistoryFormat::Record,crc));}
}
struct AcceptedStatuses {
  QVector<qint64> at;
  quint32 frames=0;
  void received(const ap_snapshot &snapshot,qint64 now) {
    if(snapshot.main_seen&&snapshot.main_age_ms==0&&snapshot.valid_frames!=frames){
      frames=snapshot.valid_frames;at.append(now);
    }
  }
  qint64 maxGap(qint64 end) const {
    qint64 result=at.isEmpty()?end:end-at.last();
    for(qsizetype i=1;i<at.size();++i)result=std::max(result,at[i]-at[i-1]);
    return result;
  }
};
// The two CDC directions have independent FIFO delivery. In particular a host
// command can wait while previously accepted firmware bytes are still buffered.
struct DuplexQueues {
  QByteArray h2d,d2h,wire,hostWire,dispatchedWire,deliveredWire;
  QStringList trace;
  bool deviceWritable=true;
};
struct DuplexChannel : UsbChannel {
  DuplexQueues &queues;
  bool opened_=false;
  explicit DuplexChannel(DuplexQueues &value):queues(value){}
  void open(const QString &) override {opened_=true;queues.trace.append("host open");emit opened();}
  void close() override {opened_=false;queues.trace.append("host close");}
  void write(const QByteArray &bytes) override {queues.h2d+=bytes;queues.hostWire+=bytes;queues.trace.append("host enqueue "+QString::fromLatin1(bytes).trimmed());}
  void deliver(qsizetype size) {
    if(!opened_||queues.d2h.isEmpty())return;
    const auto bytes=queues.d2h.left(size);queues.d2h.remove(0,bytes.size());queues.deliveredWire+=bytes;emit received(bytes);
  }
};
struct DuplexSink : HistoryFormat::ExportSink {
  DuplexQueues &queues;
  size_t chunk=4096;
  explicit DuplexSink(DuplexQueues &value):queues(value){}
  size_t write(const char *bytes,size_t size) override {
    if(!queues.deviceWritable)return 0;
    const auto accepted=std::min(chunk,size);
    queues.d2h.append(bytes,qsizetype(accepted));queues.wire.append(bytes,qsizetype(accepted));return accepted;
  }
};
struct DuplexFirmware {
  qint64 now=0;
  DuplexQueues queues;
  ArchiveSource source;
  StatusInput input{source};
  DuplexSink sink{queues};
  HistoryFormat::OutputController output{source,sink,input};
  explicit DuplexFirmware(int count) {
    const auto epoch=quint32(QDateTime::currentSecsSinceEpoch()-60);
    for(int i=0;i<count;++i){
      auto record=HistoryFormat::example();record.sequence=quint32(i+1);record.session=source.session;
      record.magic=HistoryFormat::kMagic;
      record.uptimeSeconds=quint32(i*60);record.epochSeconds=epoch;record.soc=quint8(i%101);
      record.flags|=HistoryFormat::Clock;
      record.crc=HistoryFormat::crc32(&record,offsetof(HistoryFormat::Record,crc));source.rows.append(record);
    }
  }
  void dispatchHostCommands(int limit=100) {
    for(int sent=0;sent<limit;++sent){
      const auto end=queues.h2d.indexOf('\n');if(end<0)return;
      const auto command=queues.h2d.left(end);queues.h2d.remove(0,end+1);
      queues.dispatchedWire+=command+'\n';
      queues.trace.append("device dispatch "+QString::fromLatin1(command));
      output.command(command.constData(),quint32(now));
    }
  }
  void pumpDevice() {output.pump(quint32(now));now+=5;}
  void deliverDeviceBytes(DuplexChannel &channel,qsizetype limit=65536) {channel.deliver(limit);}
  void step(DuplexChannel &channel) {dispatchHostCommands();pumpDevice();deliverDeviceBytes(channel);}
  bool bootstrap(UsbTransport &desktop,DuplexChannel &channel) {
    desktop.resume("R2500-000000000001",{},0);
    for(const auto end=now+5000;now<end&&(!desktop.connected()||!desktop.diagnostics()["history_ready"].toBool());)step(channel);
    return desktop.connected()&&desktop.diagnostics()["history_ready"].toBool();
  }
};
}

class ArchiveTransportTests : public QObject {
  Q_OBJECT
public:
  static void initMain() {
    std::fprintf(stderr,"archive-transport: initMain (before QCoreApplication)\n");
    std::fflush(stderr);
  }
private slots:
  void initTestCase() {
    std::fprintf(stderr,"archive-transport: initTestCase\n");
    std::fflush(stderr);
  }
  void init() {
    const char *function=QTest::currentTestFunction();
    const char *data=QTest::currentDataTag();
    std::fprintf(stderr,"archive-transport: begin %s [%s]\n",function?function:"",data?data:"");
    std::fflush(stderr);
  }
  void cleanup() {
    const char *function=QTest::currentTestFunction();
    const char *data=QTest::currentDataTag();
    std::fprintf(stderr,"archive-transport: end %s [%s]\n",function?function:"",data?data:"");
    std::fflush(stderr);
  }
  void cleanupTestCase() {
    std::fprintf(stderr,"archive-transport: cleanupTestCase\n");
    std::fflush(stderr);
  }
  void releasedTransportReceivesFullMixedArchiveWithWatchdog_data() {
    QTest::addColumn<bool>("shortWrites");
    QTest::newRow("full-writes")<<false;
    QTest::newRow("short-writes-and-temporary-stall")<<true;
  }
  void releasedTransportReceivesFullMixedArchiveWithWatchdog() {
    QFETCH(bool,shortWrites);
    FirmwareLoop loop(2880);
    if(shortWrites){loop.sink.chunk=256;loop.sink.stalledFrom=9000;loop.sink.stalledUntil=11000;}
    if(shortWrites){loop.input.historyBusyFrom=9000;loop.input.historyBusyUntil=11000;}
    HistoricalB989::UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    AcceptedStatuses status;
    connect(&desktop,&TelemetryTransport::snapshot,this,[&](ap_snapshot snapshot){status.received(snapshot,loop.now);});
    QSignalSpy imported(&desktop,&TelemetryTransport::historyImported);
    desktop.resume("R2500-000000000001",{},0);
    int firstClose=-1;bool started=false;
    for(;loop.now<60000;){
      loop.step();
      if(loop.now%500==0)desktop.advanceClock();
      if(loop.source.opens)started=true;
      if(loop.channel->closes>0&&firstClose<0)firstClose=int(loop.now);
      if(!imported.isEmpty())break;
    }
    const auto points=imported.isEmpty()?HistoryPoints{}:qvariant_cast<HistoryPoints>(imported.last()[0]);
    qInfo()<<"released actual transport: elapsed"<<loop.now<<"first_close"<<firstClose
           <<"writer_rows"<<loop.archiveRows()<<"imported"<<points.size()<<"status_captures"<<loop.input.captures
           <<"max_accepted_status_gap"<<status.maxGap(loop.now)<<"max_snapshots"<<loop.source.maxActiveSnapshots;
    QVERIFY(started);QVERIFY(loop.now>6000);
    QCOMPARE(loop.channel->closes,0);
    QVERIFY(desktop.connected());QCOMPARE(points.size(),2880);
    QCOMPARE(loop.archiveRows(),2880);QVERIFY(loop.validLines());
    QVERIFY(status.maxGap(loop.now)<6000);QVERIFY(status.at.size()>6);
    QCOMPARE(loop.source.maxActiveSnapshots,1);QCOMPARE(loop.source.activeSnapshots,0);
    QCOMPARE(loop.source.closedSnapshots,loop.source.opens);QVERIFY(!loop.source.frozen);
    QCOMPARE(desktop.diagnostics()["usb_bad_lines"].toInt(),0);
    QSet<qint64> ticks;for(const auto &point:points)ticks.insert(point.tick);QCOMPARE(ticks.size(),2880);
    for(int i=0;i<points.size();++i){QCOMPARE(points[i].tick,qint64(i*60));QCOMPARE(points[i].data.soc,quint32(i%101));}
  }
  void currentV3ReceivesFullMixedArchiveThroughProductionScheduler_data() {
    releasedTransportReceivesFullMixedArchiveWithWatchdog_data();
  }
  void currentV3ReceivesFullMixedArchiveThroughProductionScheduler() {
    QFETCH(bool,shortWrites);
    FirmwareLoop loop(2880);
    dateEveryRecord(loop);
    if(shortWrites){loop.sink.chunk=256;loop.sink.stalledFrom=9000;loop.sink.stalledUntil=11000;}
    loop.input.historyBusyFrom=9000;loop.input.historyBusyUntil=11000;
    UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    AcceptedStatuses status;
    connect(&desktop,&TelemetryTransport::snapshot,this,[&](ap_snapshot snapshot){status.received(snapshot,loop.now);});
    QSignalSpy imported(&desktop,&TelemetryTransport::historyImported);
    desktop.resume("R2500-000000000001",{},0);
    bool changed=false;
    for(;loop.now<65000;){
      if(!changed&&loop.now>=10000){
        changed=true;loop.source.rows.remove(0,100);
        auto next=loop.source.rows.last();
        for(int i=0;i<50;++i){++next.sequence;next.uptimeSeconds+=60;
          next.crc=HistoryFormat::crc32(&next,offsetof(HistoryFormat::Record,crc));loop.source.rows.append(next);}
        ++loop.source.generation;
      }
      loop.step();if(loop.now%500==0)desktop.refresh();
      if(desktop.diagnostics()["usb_history_result"]=="complete")break;
    }
    qInfo()<<"current v3 actual transport: elapsed"<<loop.now<<"writer_rows"<<loop.archiveRows()
           <<"status_captures"<<loop.input.captures<<"result"<<desktop.diagnostics()["usb_history_result"]
           <<"max_accepted_status_gap"<<status.maxGap(loop.now)<<"max_snapshots"<<loop.source.maxActiveSnapshots;
    QVERIFY(loop.now>6000);QCOMPARE(loop.channel->closes,0);QVERIFY(desktop.connected());
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("complete"));
    QCOMPARE(imported.size(),1);const auto points=qvariant_cast<HistoryPoints>(imported.first()[0]);
    QCOMPARE(points.size(),2880);QCOMPARE(loop.archiveRows(),2880);QVERIFY(loop.validLines());
    QCOMPARE(desktop.diagnostics()["usb_bad_lines"].toInt(),0);
    QVERIFY(status.maxGap(loop.now)<6000);QCOMPARE(loop.source.maxActiveSnapshots,1);
    QCOMPARE(loop.source.activeSnapshots,0);QVERIFY(!loop.source.frozen);
    QSet<qint64> ticks;for(const auto &point:points)ticks.insert(point.tick);QCOMPARE(ticks.size(),2880);
    for(int i=0;i<points.size();++i){QCOMPARE(points[i].tick,qint64(i*60));QCOMPARE(points[i].data.soc,quint32(i%101));}
    QCOMPARE(points[1].data.model,3u); // AHP2 metadata survives the actual v3 path.
    QVERIFY(changed);QCOMPARE(points.first().tick,0);QCOMPARE(points.last().tick,qint64(2879*60));
    QCOMPARE(loop.source.rows.size(),2830); // Live retention/append never changes the frozen export.
  }
  void earlyDesktopErrorCanReplaceActiveFirmwareRequest() {
    FirmwareLoop loop(3);loop.sink.chunk=17;
    UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QSignalSpy imported(&desktop,&TelemetryTransport::historyImported);
    desktop.resume("R2500-000000000001",{},0);
    for(;loop.now<4000&&(!desktop.connected()||!desktop.diagnostics()["history_ready"].toBool());)loop.step();
    QVERIFY(desktop.connected());desktop.requestHistory();
    // Hold accepted bytes until the old writer has emitted a complete begin
    // and a partial record, exactly the state present on a fragmented CDC read.
    loop.sink.pending.clear();
    for(;loop.now<6000;){
      loop.step(false);
      if(loop.sink.pending.contains("\"history_export_begin\":true")&&
         loop.sink.pending.contains("\"history_record\":true"))break;
    }
    QVERIFY(loop.output.active());
    auto bytes=std::exchange(loop.sink.pending,{});
    int start=0;bool changed=false;
    for(int end;(end=bytes.indexOf('\n',start))>=0;start=end+1){
      const auto line=bytes.mid(start,end-start);auto object=QJsonDocument::fromJson(line).object();
      if(object["history_export_begin"].toBool()){
        object["export_session"]=8;
        bytes.replace(start,end-start,QJsonDocument(object).toJson(QJsonDocument::Compact));changed=true;break;
      }
    }
    QVERIFY(changed);loop.channel->feed(bytes);
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("error"));
    QVERIFY(loop.output.active());
    desktop.requestHistory(); // The real desktop click, not ExportSession::start.
    QVERIFY(hasHistoryRequest(loop.channel->commands,2));
    for(const auto deadline=loop.now+5000;loop.now<deadline;){
      loop.step();if(loop.now%500==0)desktop.refresh();
      if(desktop.diagnostics()["usb_history_result"]=="complete")break;
    }
    qInfo()<<"actual dispatcher retry: result"<<desktop.diagnostics()["usb_history_result"]
           <<"source_opens"<<loop.source.opens<<"elapsed"<<loop.now;
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("complete"));
    QVERIFY(imported.size()>=2);QCOMPARE(qvariant_cast<HistoryPoints>(imported.last()[0]).size(),3);
    QCOMPARE(loop.source.maxActiveSnapshots,1);QCOMPARE(loop.source.activeSnapshots,0);QVERIFY(!loop.source.frozen);
  }
  void repeatedReplacementIgnoresCapturedOldFramesBeforeAuthoritativeBegin() {
    FirmwareLoop loop(3);loop.sink.chunk=17;dateEveryRecord(loop);
    loop.source.session=6;
    UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QSignalSpy imported(&desktop,&TelemetryTransport::historyImported);
    QVERIFY(bootstrap(loop,desktop));desktop.requestHistory();
    QVERIFY(completeWithin(loop,desktop,6000));
    const auto oldFrames=capturedArchiveFrames(loop.sink.wire,1);
    QVERIFY(oldFrames.contains("\"history_dump_done\":true"));
    // A genuinely captured prior export session supplies the stale frames.
    loop.source.session=7;loop.output.command("STATUS",quint32(loop.now));
    for(const auto end=loop.now+5000;loop.now<end&&desktop.diagnostics()["history_session"].toInteger()!=7;)loop.step();
    QCOMPARE(desktop.diagnostics()["history_session"].toInteger(),7);
    desktop.requestHistory();
    for(quint32 request=2;request<=4;++request){
      const auto packet=invalidMatchingBegin(matchingBeginWithPartialRow(loop),8);
      QVERIFY(!packet.isEmpty());QVERIFY(loop.output.active());
      loop.channel->feed(packet);
      QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("error"));
      desktop.requestHistory();
      QVERIFY(hasHistoryRequest(loop.channel->commands,request+1));
      loop.channel->feed(oldFrames); // Stale begin/record/footer: old session and request.
      QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("reading"));
    }
    QVERIFY(completeWithin(loop,desktop,6000));
    QCOMPARE(qvariant_cast<HistoryPoints>(imported.last()[0]).size(),3);
    QCOMPARE(loop.source.opens,5); // Exactly one attempt per real request, never a hidden writer.start.
    QCOMPARE(loop.source.maxActiveSnapshots,1);QCOMPARE(loop.source.activeSnapshots,0);
    QCOMPARE(loop.source.closedSnapshots,loop.source.opens);QVERIFY(!loop.source.frozen);
    QCOMPARE(loop.channel->closes,0);
  }
  void staleFramesAfterAuthoritativeBeginDoNotDamageCurrentImport() {
    FirmwareLoop loop(3);loop.sink.chunk=17;dateEveryRecord(loop);
    UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QVERIFY(bootstrap(loop,desktop));desktop.requestHistory();
    QVERIFY(completeWithin(loop,desktop,6000));
    const auto previous=capturedArchiveFrames(loop.sink.wire,1);
    QByteArray oldRecord;
    for(const auto &line:previous.split('\n'))if(QJsonDocument::fromJson(line).object()["history_record"].toBool()){oldRecord=line+'\n';break;}
    QVERIFY(!oldRecord.isEmpty());desktop.requestHistory();
    const auto begin=completeBeginAtLineBoundary(loop);QVERIFY(!begin.isEmpty());loop.channel->feed(begin);
    QVERIFY(desktop.diagnostics()["usb_history_frozen"].toBool());
    loop.channel->feed(previous); // Delayed valid begin, records and footer of the previous request.
    for(const auto end=loop.now+6000;loop.now<end&&desktop.diagnostics()["usb_history_result"]=="reading";)loop.step();
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("complete"));
    QVERIFY(desktop.diagnostics()["usb_history_complete"].toBool());
  }
  void reopenedTransportGetsStatusAndFreshOperation_data() {
    QTest::addColumn<bool>("newObject");
    QTest::newRow("same-transport-next-request")<<false;
    QTest::newRow("new-transport-recycled-request-one")<<true;
  }
  void previousBootFramesWithRecycledRequestAreIgnoredAfterBegin() {
    FirmwareLoop loop(3);loop.sink.chunk=17;dateEveryRecord(loop);loop.source.session=6;
    auto desktop=std::make_unique<UsbTransport>(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QVERIFY(bootstrap(loop,*desktop));desktop->requestHistory();QVERIFY(completeWithin(loop,*desktop,6000));
    const auto oldFrames=capturedArchiveFrames(loop.sink.wire,1);QVERIFY(!oldFrames.isEmpty());
    desktop.reset();loop.channel=new LoopChannel;loop.source.session=7;
    desktop=std::make_unique<UsbTransport>(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QVERIFY(bootstrap(loop,*desktop));QCOMPARE(desktop->diagnostics()["history_session"].toInteger(),7);
    desktop->requestHistory();
    QVERIFY(hasHistoryRequest(loop.channel->commands,1));
    const auto begin=completeBeginAtLineBoundary(loop);QVERIFY(!begin.isEmpty());loop.channel->feed(begin);
    QVERIFY(desktop->diagnostics()["usb_history_frozen"].toBool());
    loop.channel->feed(oldFrames); // Same request=1, genuinely older export_session=6.
    QCOMPARE(desktop->diagnostics()["usb_history_result"].toString(),QString("reading"));
    QVERIFY(completeWithin(loop,*desktop,6000));QCOMPARE(loop.source.opens,2);
    QCOMPARE(loop.source.activeSnapshots,0);QCOMPARE(loop.source.maxActiveSnapshots,1);
  }
  void stalledOutputReleasesSnapshotAndRetryWorks() {
    FirmwareLoop loop(3);loop.sink.chunk=17;dateEveryRecord(loop);
    UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QVERIFY(bootstrap(loop,desktop));desktop.requestHistory();
    const auto prefix=matchingBeginWithPartialRow(loop);QVERIFY(!prefix.isEmpty());loop.channel->feed(prefix);
    QCOMPARE(loop.source.activeSnapshots,1);
    loop.sink.stalledFrom=loop.now;loop.sink.stalledUntil=loop.now+80000;
    for(const auto end=loop.now+72000;loop.now<end;){
      loop.step();if(loop.now%500==0)desktop.refresh();
      if(desktop.diagnostics()["usb_history_result"]=="error")break;
    }
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("error"));
    QCOMPARE(loop.source.activeSnapshots,0);QVERIFY(!loop.source.frozen);
    QCOMPARE(loop.source.closedSnapshots,loop.source.opens);
    loop.sink.stalledUntil=loop.now;
    desktop.requestHistory();QVERIFY(hasHistoryRequest(loop.channel->commands,2));
    QVERIFY(completeWithin(loop,desktop,6000));
    QCOMPARE(loop.source.opens,2);QCOMPARE(loop.source.closedSnapshots,2);
    QCOMPARE(loop.source.maxActiveSnapshots,1);QVERIFY(!loop.source.frozen);
  }
  void reopenedTransportGetsStatusAndFreshOperation() {
    QFETCH(bool,newObject);
    FirmwareLoop loop(100);loop.sink.chunk=128;dateEveryRecord(loop);
    auto desktop=std::make_unique<UsbTransport>(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QVERIFY(bootstrap(loop,*desktop));desktop->requestHistory();
    auto accepted=matchingBeginWithPartialRow(loop);QVERIFY(!accepted.isEmpty());
    loop.channel->feed(accepted);QVERIFY(loop.output.active());
    const auto before=loop.now;
    if(newObject){
      desktop.reset();loop.channel=new LoopChannel; // A reopened process really restarts its request counter at one.
      desktop=std::make_unique<UsbTransport>(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    }else desktop->stop();
    // New appends after disconnect must belong to the next captured operation,
    // rather than accidentally accepting the old request-one footer on reopen.
    auto appended=loop.source.rows.last();++appended.sequence;appended.uptimeSeconds+=60;appended.soc=42;
    appended.crc=HistoryFormat::crc32(&appended,offsetof(HistoryFormat::Record,crc));
    loop.source.rows.append(appended);++loop.source.generation;
    QSignalSpy imported(desktop.get(),&TelemetryTransport::historyImported);
    desktop->resume("R2500-000000000001",{},0);
    for(const auto end=loop.now+5000;loop.now<end&&(!desktop->connected()||!desktop->diagnostics()["history_ready"].toBool());)loop.step();
    QVERIFY2(desktop->connected(),"A reopened transport did not receive a valid STATUS while the old writer was active");
    QVERIFY(loop.now-before<5000);
    desktop->requestHistory();
    QVERIFY(hasHistoryRequest(loop.channel->commands,newObject?1:2));
    QVERIFY(completeWithin(loop,*desktop,12000));
    QCOMPARE(imported.size(),1);const auto points=qvariant_cast<HistoryPoints>(imported.last()[0]);
    QCOMPARE(points.size(),101);QCOMPARE(points.last().data.soc,42u);
    QCOMPARE(loop.source.opens,2);QCOMPARE(loop.source.maxActiveSnapshots,1);
    QCOMPARE(loop.source.activeSnapshots,0);QVERIFY(!loop.source.frozen);
  }
  void restartedClientDoesNotAcceptBufferedPreviousOperation_data() {
    QTest::addColumn<bool>("changedArchive");
    QTest::addColumn<int>("bufferedKind");
    QTest::addColumn<bool>("oldNumeric");
    QTest::addColumn<bool>("oldEmpty");
    QTest::newRow("changed-archive-old-begin")<<true<<0<<false<<false;
    QTest::newRow("same-archive-old-begin")<<false<<0<<false<<false;
    QTest::newRow("changed-archive-old-record")<<true<<1<<false<<false;
    QTest::newRow("same-archive-old-complete-footer")<<false<<2<<false<<false;
    QTest::newRow("changed-archive-old-complete-footer")<<true<<2<<false<<false;
    QTest::newRow("changed-archive-old-partial-record-and-delimiter")<<true<<3<<false<<false;
    QTest::newRow("changed-archive-replacement-short-writes-and-temporary-pause")<<true<<4<<false<<false;
    QTest::newRow("pre-update-numeric-begin")<<true<<0<<true<<false;
    QTest::newRow("pre-update-numeric-record")<<true<<1<<true<<false;
    QTest::newRow("pre-update-numeric-complete-footer")<<false<<2<<true<<false;
    QTest::newRow("pre-update-numeric-partial-record")<<true<<3<<true<<false;
    QTest::newRow("old-empty-before-new-nonempty")<<true<<2<<false<<true;
    QTest::newRow("old-numeric-empty-before-new-nonempty")<<true<<2<<true<<true;
  }
  void strongNonceRequirementSurvivesStatusChanges_data() {
    QTest::addColumn<int>("capability");
    QTest::newRow("capability-removed")<<0;
    QTest::newRow("unsupported-bits-mid-operation")<<64;
  }
  void realOpenFailureFooterIsCorrelatedBeforeMetadata_data() {
    QTest::addColumn<bool>("strong");QTest::addColumn<bool>("ownError");
    QTest::newRow("nonce-foreign-error-ignored")<<true<<false;
    QTest::newRow("nonce-own-error-fatal")<<true<<true;
    QTest::newRow("numeric-foreign-error-ignored")<<false<<false;
    QTest::newRow("numeric-own-error-fatal")<<false<<true;
  }
  void realOpenFailureFooterIsCorrelatedBeforeMetadata() {
    QFETCH(bool,strong);QFETCH(bool,ownError);
    FirmwareLoop loop(3);dateEveryRecord(loop);
    UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;},deterministicHistoryEntropy());
    if(strong)QVERIFY(bootstrap(loop,desktop));
    else {
      desktop.resume("R2500-000000000001",{},0);
      HistoryFormat::StatusSnapshot frame;QVERIFY(loop.input.capture(frame,quint32(loop.now)));
      // Model an old firmware handshake by omitting only its new capability.
      // The open-error archive bytes below are untouched production output.
      for(int row=0;row<HistoryFormat::kStatusRows;++row){
        char bytes[3072];const auto size=HistoryFormat::serializeStatus(frame,uint8_t(row),bytes,sizeof(bytes));
        QVERIFY(size>0);auto object=QJsonDocument::fromJson(QByteArray(bytes,size)).object();
        object.remove("history_nonce_bits");loop.channel->feed(QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n');
      }
      QVERIFY(desktop.connected());
    }
    desktop.requestHistory();const auto begin=completeBeginAtLineBoundary(loop);QVERIFY(!begin.isEmpty());
    loop.channel->feed(begin);QVERIFY(desktop.diagnostics()["usb_history_frozen"].toBool());
    const auto currentNonce=desktop.diagnostics()["usb_history_operation_nonce"].toString().toLatin1();
    const auto errorFooter=[&](quint32 request,const QByteArray &nonce){
      FirmwareLoop failed(0);failed.source.openError=true;failed.channel->opened_=true;
      const auto command="HISTORY-EXPORT "+QByteArray::number(request)+(nonce.isEmpty()?QByteArray{}:" "+nonce);
      failed.output.command(command.constData(),quint32(failed.now));
      for(;failed.now<2000;){failed.step(false);if(failed.source.opens&&!failed.output.active())break;}
      QByteArray footer;
      for(const auto &line:failed.sink.wire.split('\n'))
        if(QJsonDocument::fromJson(line).object()["history_dump_done"].toBool())footer=line+'\n';
      delete failed.channel;return footer;
    };
    const auto foreign=errorFooter(99,strong?QByteArray(32,'f'):QByteArray{});
    QVERIFY(!foreign.isEmpty());const auto frame=QJsonDocument::fromJson(foreign).object();
    QCOMPARE(frame["export_session"].toInt(),0);QCOMPARE(frame["generation"].toInt(),0);
    loop.channel->feed(foreign);
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("reading"));
    if(ownError){
      const auto own=errorFooter(1,currentNonce);QVERIFY(!own.isEmpty());loop.channel->feed(own);
      QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("error"));
    }else QVERIFY(completeWithin(loop,desktop,6000));
  }
  void strongNonceRequirementSurvivesStatusChanges() {
    QFETCH(int,capability);
    FirmwareLoop loop(3);loop.sink.chunk=17;dateEveryRecord(loop);
    UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QVERIFY(bootstrap(loop,desktop));desktop.requestHistory();
    const auto begin=completeBeginAtLineBoundary(loop);QVERIFY(!begin.isEmpty());
    QJsonObject inventory,foreign;
    for(const auto &line:loop.sink.wire.split('\n')){
      const auto object=QJsonDocument::fromJson(line).object();
      if(object.contains("history_ready"))inventory=object;
      if(object["history_export_begin"].toBool())foreign=object;
    }
    QVERIFY(!inventory.isEmpty());QVERIFY(!foreign.isEmpty());
    if(capability)inventory["history_nonce_bits"]=capability;else inventory.remove("history_nonce_bits");
    loop.channel->feed(QJsonDocument(inventory).toJson(QJsonDocument::Compact)+'\n');
    // Deliberately adversarial parser inputs, separate from the FIFO lifecycle
    // traces: a missing/foreign nonce must be rejected before bad metadata.
    foreign.remove("operation_nonce");foreign["export_session"]=0;foreign["count"]=99999;
    const auto missing=QJsonDocument(foreign).toJson(QJsonDocument::Compact)+'\n';
    loop.channel->feed(missing);
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("reading"));
    loop.channel->feed(begin);QVERIFY(desktop.diagnostics()["usb_history_frozen"].toBool());
    loop.channel->feed(missing);foreign["operation_nonce"]=QString(32,'f');
    loop.channel->feed(QJsonDocument(foreign).toJson(QJsonDocument::Compact)+'\n');
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),QString("reading"));
    QVERIFY(completeWithin(loop,desktop,6000));
    QCOMPARE(desktop.diagnostics()["usb_history_imported"].toInt(),3);
    QCOMPARE(desktop.diagnostics()["usb_history_nonce_bits"].toInt(),128);
  }
  void matchingNonceCorruptionRemainsStrict_data() {
    QTest::addColumn<int>("damage");QTest::addColumn<QString>("expected");
    QTest::newRow("matching-begin-invalid-session")<<0<<QString("error");
    QTest::newRow("matching-begin-wrong-request")<<1<<QString("error");
    QTest::newRow("matching-record-invalid-crc")<<2<<QString("partial");
    QTest::newRow("matching-footer-wrong-digest")<<3<<QString("partial");
    QTest::newRow("matching-footer-wrong-count")<<4<<QString("partial");
    QTest::newRow("matching-footer-error")<<5<<QString("error");
  }
  void rapidRestartsReleaseEveryReplacedSnapshot() {
    DuplexFirmware loop(3);loop.sink.chunk=17;
    std::unique_ptr<UsbTransport> desktop;
    DuplexChannel *channel=nullptr;
    for(quint32 attempt=1;attempt<=4;++attempt){
      channel=new DuplexChannel(loop.queues);
      auto raw=QByteArray(16,0);qToLittleEndian<quint32>(attempt,reinterpret_cast<uchar *>(raw.data()));
      desktop=std::make_unique<UsbTransport>(nullptr,false,channel,loopPorts,[&]{return loop.now;},[=]{return raw;});
      loop.queues.deviceWritable=true;QVERIFY(loop.bootstrap(*desktop,*channel));desktop->requestHistory();
      QVERIFY(hasHistoryRequest(loop.queues.h2d,1));loop.dispatchHostCommands();
      for(const auto end=loop.now+6000;loop.now<end&&loop.source.opens<int(attempt);)loop.pumpDevice();
      QCOMPARE(loop.source.opens,int(attempt));QCOMPARE(loop.source.activeSnapshots,1);
      if(attempt==4)break;
      desktop->stop();desktop.reset();loop.queues.deviceWritable=false;
      for(const auto end=loop.now+500;loop.now<end;)loop.pumpDevice();
      auto &changed=loop.source.rows.last();changed.soc=quint8(90+attempt);
      changed.crc=HistoryFormat::crc32(&changed,offsetof(HistoryFormat::Record,crc));
    }
    QSignalSpy imported(desktop.get(),&TelemetryTransport::historyImported);
    for(const auto end=loop.now+6000;loop.now<end;){
      loop.step(*channel);if(desktop->diagnostics()["usb_history_result"]=="complete")break;
    }
    QCOMPARE(desktop->diagnostics()["usb_history_result"].toString(),QString("complete"));
    QCOMPARE(imported.size(),1);const auto points=qvariant_cast<HistoryPoints>(imported.first()[0]);
    QCOMPARE(points.size(),3);QCOMPARE(points.last().data.soc,93u);
    QCOMPARE(loop.source.maxActiveSnapshots,1);QCOMPARE(loop.source.closedSnapshots,4);
    QCOMPARE(loop.source.activeSnapshots,0);QVERIFY(!loop.source.frozen);
    QCOMPARE(loop.queues.deliveredWire,loop.queues.wire);
    QCOMPARE(loop.queues.dispatchedWire+loop.queues.h2d,loop.queues.hostWire);
  }
  void matchingNonceCorruptionRemainsStrict() {
    QFETCH(int,damage);QFETCH(QString,expected);
    FirmwareLoop loop(3);dateEveryRecord(loop);
    UsbTransport desktop(nullptr,false,loop.channel,loopPorts,[&]{return loop.now;});
    QVERIFY(bootstrap(loop,desktop));desktop.requestHistory();
    loop.sink.pending.clear();
    for(const auto end=loop.now+6000;loop.now<end;){loop.step(false);if(loop.source.opens&& !loop.output.active())break;}
    QByteArray bytes;bool changed=false;
    for(const auto &line:loop.sink.pending.split('\n')){
      if(line.isEmpty())continue;
      auto frame=QJsonDocument::fromJson(line).object();
      if(!changed&&((damage<2&&frame["history_export_begin"].toBool())||
          (damage==2&&frame["history_record"].toBool())||(damage>=3&&frame["history_dump_done"].toBool()))){
        QVERIFY(frame["operation_nonce"].isString());
        if(damage==0)frame["export_session"]=0;
        if(damage==1)frame["request"]=999;
        if(damage==2)frame["crc"]=0;
        if(damage==3)frame["digest"]=0;
        if(damage==4)frame["count"]=2;
        if(damage==5)frame["status"]="error";
        changed=true;
      }
      bytes+=QJsonDocument(frame).toJson(QJsonDocument::Compact)+'\n';
    }
    QVERIFY(changed);loop.channel->feed(bytes);
    QCOMPARE(desktop.diagnostics()["usb_history_result"].toString(),expected);
    QVERIFY(!desktop.diagnostics()["usb_history_complete"].toBool());
    QCOMPARE(loop.source.activeSnapshots,0);
  }
  void restartedClientDoesNotAcceptBufferedPreviousOperation() {
    QFETCH(bool,changedArchive);
    QFETCH(int,bufferedKind);
    QFETCH(bool,oldNumeric);
    QFETCH(bool,oldEmpty);
    DuplexFirmware loop(3);
    const auto originalRows=loop.source.rows;
    if(oldEmpty)loop.source.rows.clear();
    if(bufferedKind>=3)loop.sink.chunk=17;
    auto rawA=QByteArray(16,0),rawB=QByteArray(16,0);rawA[15]=1;rawB[15]=2;
    auto channel=new DuplexChannel(loop.queues);
    auto desktop=std::make_unique<UsbTransport>(nullptr,false,channel,loopPorts,[&]{return loop.now;},[=]{return rawA;});
    QVERIFY(loop.bootstrap(*desktop,*channel));
    // A pre-update application issued this read-only numeric command. Its bytes
    // still pass through the same host FIFO and actual production dispatcher.
    if(oldNumeric)channel->write("HISTORY-EXPORT 1\n");else desktop->requestHistory();
    QVERIFY2(hasHistoryRequest(loop.queues.h2d,1),qPrintable(QString::fromLatin1(loop.queues.h2d)+QString::fromUtf8(QJsonDocument(desktop->diagnostics()).toJson())));loop.dispatchHostCommands();
    for(const auto end=loop.now+5000;loop.now<end&&loop.source.opens==0;)loop.pumpDevice();
    QCOMPARE(loop.source.opens,1);
    QVERIFY(!loop.queues.d2h.contains("\"history_export_begin\":true"));
    // Closing the application does not reboot the firmware. CDC temporarily
    // backpressures output, while the production dispatcher retains its job.
    desktop->stop();desktop.reset();loop.queues.deviceWritable=false;
    const auto closedAt=loop.now;
    for(;loop.now<closedAt+500;)loop.pumpDevice();
    QCOMPARE(loop.now-closedAt,500);QVERIFY(loop.output.active());
    channel=new DuplexChannel(loop.queues);
    desktop=std::make_unique<UsbTransport>(nullptr,false,channel,loopPorts,[&]{return loop.now;},[=]{return rawB;});
    loop.queues.deviceWritable=true;QVERIFY(loop.bootstrap(*desktop,*channel));
    // Deliver the new handshake, then hold host->device commands independently
    // while the still-running old job queues its genuine begin on device->host.
    const auto buffered=[&]{
      const auto &bytes=loop.queues.d2h;
      if(bufferedKind==0||bufferedKind==4)return bytes.contains("\"history_export_begin\":true")&&bytes.endsWith('\n');
      if(bufferedKind==2)return bytes.contains("\"history_dump_done\":true")&&bytes.endsWith('\n');
      if(bufferedKind==3)return bytes.contains("\"history_record\":true")&&!bytes.endsWith('\n');
      return bytes.contains("\"history_record\":true")&&bytes.endsWith('\n');
    };
    for(const auto end=loop.now+5000;loop.now<end&&!buffered();)loop.pumpDevice();
    QVERIFY(buffered());
    QVERIFY(loop.queues.d2h.contains("\"history_export_begin\":true"));
    desktop->requestHistory();QVERIFY2(hasHistoryRequest(loop.queues.h2d,1),qPrintable(QString::fromLatin1(loop.queues.h2d)+QString::fromUtf8(QJsonDocument(desktop->diagnostics()).toJson())));
    QCOMPARE(desktop->diagnostics()["usb_history_operation_nonce"].toString(),QString::fromLatin1(rawB.toHex()));
    if(changedArchive){
      if(oldEmpty)loop.source.rows=originalRows;
      else {
        auto appended=loop.source.rows.last();++appended.sequence;appended.uptimeSeconds+=60;appended.soc=93;
        appended.crc=HistoryFormat::crc32(&appended,offsetof(HistoryFormat::Record,crc));loop.source.rows.append(appended);
      }
    }
    const auto expected=loop.source.rows;
    QSignalSpy imported(desktop.get(),&TelemetryTransport::historyImported);
    bool importedBeforeNewJobFinished=false;
    connect(desktop.get(),&TelemetryTransport::historyImported,this,[&]{
      if(loop.source.closedSnapshots<2)importedBeforeNewJobFinished=true;
    });
    loop.dispatchHostCommands();
    if(bufferedKind==4){
      loop.queues.deviceWritable=false;
      for(const auto end=loop.now+250;loop.now<end;)loop.pumpDevice();
      loop.queues.deviceWritable=true;
    }
    for(const auto end=loop.now+5000;loop.now<end;){
      loop.pumpDevice();loop.deliverDeviceBytes(*channel);
      if(loop.source.opens==2&&!loop.output.active()&&loop.queues.d2h.isEmpty())break;
    }
    std::fprintf(stderr,"restart FIFO: changed=%d buffered=%d result=%s opens=%d active=%d\n",int(changedArchive),bufferedKind,
      qPrintable(desktop->diagnostics()["usb_history_result"].toString()),loop.source.opens,loop.source.activeSnapshots);
    std::fflush(stderr);
    QCOMPARE(loop.queues.deliveredWire,loop.queues.wire); // Every byte is delivered once in original device FIFO order.
    QCOMPARE(loop.queues.dispatchedWire+loop.queues.h2d,loop.queues.hostWire);
    int begins=0;
    for(const auto &line:loop.queues.wire.split('\n')){
      const auto frame=QJsonDocument::fromJson(line).object();
      if(!frame["history_export_begin"].toBool())continue;
      QCOMPARE(frame["request"].toInt(),1);QCOMPARE(frame["export_session"].toInt(),7);QCOMPARE(frame["generation"].toInt(),10);
      if(!begins&&oldNumeric)QVERIFY(!frame.contains("operation_nonce"));
      else QCOMPARE(frame["operation_nonce"].toString(),QString::fromLatin1((begins?rawB:rawA).toHex()));
      ++begins;
    }
    QCOMPARE(begins,2);
    QCOMPARE(desktop->diagnostics()["usb_history_result"].toString(),QString("complete"));
    QVERIFY(!importedBeforeNewJobFinished); // An identical stale archive cannot impersonate the new operation.
    QCOMPARE(imported.size(),1);const auto points=qvariant_cast<HistoryPoints>(imported.first()[0]);
    QCOMPARE(points.size(),expected.size());
    for(int i=0;i<points.size();++i){QCOMPARE(points[i].tick,qint64(expected[i].uptimeSeconds));QCOMPARE(points[i].data.soc,quint32(expected[i].soc));}
    QCOMPARE(loop.source.opens,2);QCOMPARE(loop.source.maxActiveSnapshots,1);QCOMPARE(loop.source.activeSnapshots,0);
    QVERIFY(!loop.source.frozen);
  }
  void frozenParserAndLifecycleMatchHistoricalSource() {
    QFile manifest(QString(FROZEN_B989_DIR)+"/provenance.json");
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const auto metadata=QJsonDocument::fromJson(manifest.readAll()).object();
    const auto hashes=metadata["original_sha256"].toObject();
    for(auto it=hashes.begin();it!=hashes.end();++it) {
      const auto name=it.key().section('/',-1);
      QFile file(QString(FROZEN_B989_DIR)+"/"+name);
      QVERIFY(file.open(QIODevice::ReadOnly));
      auto bytes=file.readAll();
      bytes.remove(0,bytes.indexOf('\n')+1); // Provenance comment only.
      if(name=="usb_transport.h") {
        bytes.replace("namespace HistoricalB989 {\n","");
        bytes.replace("  void advanceClock() { tick(); }\n","");
        const QByteArray suffix="\n} // namespace HistoricalB989\n";
        QVERIFY(bytes.endsWith(suffix));bytes.chop(suffix.size());
      } else if(name=="usb_transport.cpp") {
        bytes.replace("#include \"HistoryWire.h\"","#include \"../../firmware/R2500Adapter/HistoryWire.h\"");
        bytes.replace("namespace HistoricalB989 {\nnamespace {","namespace {");
        bytes.replace("B989HistoryFormat::","HistoryFormat::");
        const QByteArray suffix="\n} // namespace HistoricalB989\n";
        QVERIFY(bytes.endsWith(suffix));bytes.chop(suffix.size());
      } else bytes.replace("namespace B989HistoryFormat {","namespace HistoryFormat {");
      QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()),it.value().toString());
    }
  }
};
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  UiLanguage language("ru");
  ArchiveTransportTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "archive_transport_test.moc"
