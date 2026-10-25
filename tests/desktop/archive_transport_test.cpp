#include "usb_transport.h"
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
  HistoryFormat::ReadState open(HistoryFormat::ExportMetadata &metadata) override {
    ++opens;maxActiveSnapshots=std::max(maxActiveSnapshots,activeSnapshots+1);
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
    QVERIFY(loop.channel->commands.contains("HISTORY-EXPORT 2\n"));
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
      QVERIFY(loop.channel->commands.contains("HISTORY-EXPORT "+QByteArray::number(request+1)+'\n'));
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
    QVERIFY(loop.channel->commands.contains("HISTORY-EXPORT 1\n"));
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
    desktop.requestHistory();QVERIFY(loop.channel->commands.contains("HISTORY-EXPORT 2\n"));
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
    QVERIFY(loop.channel->commands.contains(newObject?"HISTORY-EXPORT 1\n":"HISTORY-EXPORT 2\n"));
    QVERIFY(completeWithin(loop,*desktop,12000));
    QCOMPARE(imported.size(),1);const auto points=qvariant_cast<HistoryPoints>(imported.last()[0]);
    QCOMPARE(points.size(),101);QCOMPARE(points.last().data.soc,42u);
    QCOMPARE(loop.source.opens,2);QCOMPARE(loop.source.maxActiveSnapshots,1);
    QCOMPARE(loop.source.activeSnapshots,0);QVERIFY(!loop.source.frozen);
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
QTEST_GUILESS_MAIN(ArchiveTransportTests)
#include "archive_transport_test.moc"
