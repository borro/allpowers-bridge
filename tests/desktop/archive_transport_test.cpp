#include "history.h"
#include "chart.h"
#include "usb_hid.h"
#include "usb_transport.h"
#include "../../core/src/HidTelemetryServer.h"
#include "../../core/src/HistoryClock.h"
#include "../../core/src/StatusWire.h"
#include <QDateTime>
#include <QTimeZone>
#include <QSet>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QBuffer>
#include <deque>
using namespace HidTelemetry;
namespace {
HistoryPoint point(int epoch, int sequence, qint64 tick, qint64 wall=0) {
  HistoryPoint p;p.address="usb-board:R2500-000000000001";p.session=QString("usb-%1").arg(epoch);
  p.sequence=sequence;p.tick=tick;p.wall=wall;p.stride=60;p.data.fresh=1;p.data.known=7;
  p.data.soc=60;p.data.output_w=120;return p;
}
struct Channel:UsbChannel {
  std::function<void(QByteArray)> output;
  void open(const QString &)override{emit opened();}
  void close()override{}
  void write(const QByteArray &b)override{if(output)output(b);}
  bool hid()const override{return true;}
  void feed(const QByteArray &b){emit received(b);}
};
struct Source:Provider {
  QList<HistoryFormat::Record> records,frozen[2];
  HistoryFormat::ClockAnchor anchor;int clockWrites=0;
  size_t text(Op, uint8_t *out,size_t capacity)override{
    HistoryFormat::StatusSnapshot f;f.boardId="waveshare-147";f.nowMs=100000;
    f.state.connected=true;strcpy(f.state.identity.address,"02:00:00:00:00:01");strcpy(f.state.identity.name,"test");
    f.state.telemetry.protocol=3;f.state.telemetry.known=7;f.state.telemetry.soc=60;
    f.state.telemetry.mainSeen=true;f.state.telemetry.mainAt=f.nowMs;f.state.telemetry.validFrames=1;
    f.log.ready=true;f.log.session=5;f.log.generation=1;f.log.count=records.size();
    f.log.oldest=records.first().sequence;f.log.latest=records.last().sequence;f.log.liveLatest=100;
    size_t used=0;for(uint8_t i=0;i<HistoryFormat::kStatusRows;++i){
      int n=HistoryFormat::serializeStatus(f,i,reinterpret_cast<char*>(out+used),capacity-used);
      if(n<=0||size_t(n)>=capacity-used)return 0;used+=n;
    }return used;
  }
  Result clock(uint32_t epoch,uint32_t uptime)override{
    if(anchor.synced())return Result::Ok;uint32_t base=0;
    if(!anchor.candidate(epoch,uptime,100,base))return Result::Invalid;
    anchor.commit(base);++clockWrites;return Result::Ok;
  }
  HistoryFormat::ReadState open(unsigned slot,HistoryFormat::ExportMetadata &m,uint32_t &digest)override{
    frozen[slot]=records;m={5,1,uint32_t(records.size()),records.first().sequence,records.last().sequence};
    digest=HistoryFormat::kDigestSeed;for(const auto &r:records)digest=HistoryFormat::digestRecord(digest,r);
    return HistoryFormat::ReadState::Record;
  }
  bool record(unsigned slot,uint32_t at,HistoryFormat::Record &r)override{
    if(at>=uint32_t(frozen[slot].size()))return false;r=frozen[slot][at];return true;
  }
  void close(unsigned slot)override{frozen[slot].clear();}
};
}
class ArchiveTests:public QObject {
  Q_OBJECT
private slots:
  void durableEpochAnchorsAndUnknownRecords(){
    QTemporaryDir dir;const auto path=dir.filePath("history.sqlite3");const qint64 now=1800001000;
    const auto a=point(1,1,20),b=point(1,2,80),other=point(2,3,20);
    {
      HistoryStore store(path);QVERIFY(store.valid());
      QVERIFY(store.importPoints({a,b,other},now));QCOMPARE(store.undatedPoints(a.address).size(),3);
      QVERIFY(store.points(a.address,now).isEmpty());
    }
    {
      HistoryStore store(path);QCOMPARE(store.undatedPoints(a.address).size(),3);
      auto anchor=point(1,4,140,now-100);
      QVERIFY(store.importPoints({anchor},now));
      const auto dated=store.points(a.address,now);QCOMPARE(dated.size(),3);
      QCOMPARE(dated[0].wall,now-220);QCOMPARE(dated[1].wall,now-160);
      QCOMPARE(store.undatedPoints(a.address).size(),1);
      QVERIFY(store.importPoints({a,b,other,anchor},now));
      QCOMPARE(store.stats()["usb_minutes"].toInt(),4);
      auto second=point(2,5,80,now-20);QVERIFY(store.importPoints({second},now));
      QVERIFY(store.undatedPoints(a.address).isEmpty());QCOMPARE(store.points(a.address,now).size(),5);
      auto wrong=point(3,6,1,now+10);QVERIFY(!store.importPoints({wrong},now));
      QCOMPARE(store.stats()["usb_minutes"].toInt(),5);
    }
  }
  void sameSecondRecordsRemainDistinct(){
    QTemporaryDir dir;HistoryStore store(dir.filePath("history.sqlite3"));
    auto a=point(1,1,20),b=point(1,2,20,1800000000);
    QVERIFY(store.importPoints({a,b},1800000001));
    QVERIFY(store.importPoints({a,b},1800000001));
    const auto rows=store.points(a.address,1800000001);QCOMPARE(rows.size(),2);
    QCOMPARE(rows[0].wall,qint64(1800000000));QCOMPARE(rows[1].wall,qint64(1800000000));
  }
  void conflictingAnchorsDoNotInventDates(){
    QTemporaryDir dir;HistoryStore store(dir.filePath("history.sqlite3"));const qint64 now=1800001000;
    auto a=point(1,1,10,now-100),b=point(1,2,70,now-10),unknown=point(1,3,20);
    QVERIFY(store.importPoints({a,b,unknown},now));QCOMPARE(store.undatedPoints(a.address).size(),1);
    auto gap=point(2,4,10);gap.session+="-gap-4";
    auto anchor=point(2,5,70,now-20);QVERIFY(store.importPoints({gap,anchor},now));
    QCOMPARE(store.undatedPoints(a.address).size(),1);
  }
  void liveEpochSurvivesStoreReopenAndBoardRestart(){
    QTemporaryDir dir;const auto path=dir.filePath("history.sqlite3");const qint64 now=1800001000;
    ap_snapshot sample{};sample.fresh=1;sample.main_seen=1;sample.known=7;sample.output_w=151;
    const QString board="R2500-000000000001", address="02:00:00:00:00:01";
    {
      HistoryStore store(path);
      QVERIFY(store.record(address,sample,now-3,100,board,4));
      QVERIFY(store.record(address,sample,now-2,101,board,4));
      QVERIFY(store.record(address,sample,now-1,7,board,5));
      QVERIFY(store.record(address,sample,now,8));
    }
    HistoryStore reopened(path);const auto rows=reopened.points(address,now);
    QCOMPARE(rows.size(),4);QCOMPARE(historyEpoch(rows[0]),quint32(4));
    QCOMPARE(historyBoard(rows[0]),board);QCOMPARE(rows[0].tick,qint64(100));
    QVERIFY(joinEpochPoints(rows[0],rows[1]));QVERIFY(!joinEpochPoints(rows[1],rows[2]));
    QCOMPARE(historyEpoch(rows[2]),quint32(5));QCOMPARE(historyEpoch(rows[3]),quint32(0));
    QVERIFY(historyBoard(rows[3]).isEmpty());
  }
  void bothChartsKeepIdenticalArchiveAndLiveReadings(){
    const qint64 now=1800001000;
    auto archive=point(4,2,40,now-60), undated=point(1,1,7);
    auto live=point(4,0,100,now);live.address="02:00:00:00:00:01";
    live.session="usb-live:R2500-000000000001:4:application";live.stride=1;live.data.output_w=173;
    QVERIFY(joinEpochPoints(archive,live));
    auto other=live;other.session="usb-live:R2500-000000000002:4:application";
    QVERIFY(!joinEpochPoints(archive,other));
    other.session="usb-live:R2500-000000000001:5:application";
    QVERIFY(!joinEpochPoints(archive,other));
    HistoryChart timed,epochs;timed.resize(900,400);epochs.resize(900,400);epochs.setEpochMode(true);
    auto verify=[&](const HistoryPoints &raw) {
      timed.setPoints(raw,now+1);epochs.setPoints(raw,now+1);
      auto csv=[](HistoryPoints rows){
        rows=historyByEpoch(rows,{});QBuffer out;out.open(QIODevice::WriteOnly);
        if(!writeHistoryCsv(rows,out))return QByteArray{};return out.data();
      };
      QCOMPARE(timed.sourcePoints().size(),raw.size());QCOMPARE(epochs.sourcePoints().size(),raw.size());
      QVERIFY(!csv(raw).isEmpty());QCOMPARE(csv(timed.sourcePoints()),csv(epochs.sourcePoints()));
      QCOMPARE(timed.sourcePoints().last().data.output_w,raw.last().data.output_w);
      QCOMPARE(epochs.sourcePoints().last().data.output_w,raw.last().data.output_w);
      QCOMPARE(timed.sourcePoints().first().wall,qint64(0));
      QCOMPARE(timed.points().size(),raw.size()-1);
      QVERIFY(std::all_of(timed.points().cbegin(),timed.points().cend(),[](const auto &p){return p.wall>0;}));
      QCOMPARE(epochs.points().size(),raw.size());
      QVERIFY(timed.axisTicks().first().first>0);
      QCOMPARE(timed.axisTicks().last().first,now+1);
    };
    auto rows=sharedUsbHistory({live},{archive},{undated},"usb-4");QCOMPARE(rows.size(),3);verify(rows);
    auto latest=live;latest.tick++;latest.wall++;latest.data.output_w=192;
    rows=sharedUsbHistory({live,latest},{archive},{undated},"usb-4");QCOMPARE(rows.size(),4);verify(rows);
    const auto shots=qEnvironmentVariable("ALLPOWERS_TEST_SHOTS");
    if(!shots.isEmpty()){timed.grab().save(shots+"/shared-time.png");epochs.grab().save(shots+"/shared-epochs.png");}
  }
  void timeChartHidesUndatedRecordsWithoutRemovingHistory(){
    HistoryChart chart;const HistoryPoints rows{point(1,1,7)};
    chart.setPoints(rows,1800001000);QVERIFY(chart.points().isEmpty());
    QCOMPARE(chart.sourcePoints().size(),1);
    chart.setEpochMode(true);chart.setPoints(rows,1800001000);
    QCOMPARE(chart.points().size(),1);QCOMPARE(chart.sourcePoints().first().wall,qint64(0));
    chart.setEpochMode(false);chart.setPoints(rows,1800001000);QVERIFY(chart.points().isEmpty());
  }
  void epochsIncludeDatedAndUndatedRecordsWithoutInventingDates(){
    const auto undated=point(1,1,7), second=point(2,2,20,1800000010),
               third=point(2,3,80,1800000070), tenth=point(10,4,10);
    const auto raw=historyByEpoch({third,second},{tenth,undated});
    QCOMPARE(raw.size(),4);QCOMPARE(historyEpoch(raw[0]),quint32(1));
    QCOMPARE(raw[1].sequence,quint32(2));QCOMPARE(raw[2].sequence,quint32(3));
    QCOMPARE(historyEpoch(raw[3]),quint32(10));QCOMPARE(raw[0].wall,qint64(0));
    QCOMPARE(raw[1].wall,qint64(1800000010));
    QVERIFY(joinEpochPoints(raw[1],raw[2]));QVERIFY(!joinEpochPoints(raw[0],raw[1]));
    auto gap=raw[2];gap.session+="-gap-3";QVERIFY(!joinEpochPoints(raw[1],gap));
    QCOMPARE(historyEpoch(gap),quint32(2));
    HistoryChart chart;chart.resize(800,400);chart.setEpochMode(true);chart.setPoints(raw,1800001000);
    const auto ticks=chart.axisTicks();QCOMPARE(ticks.size(),3);
    QCOMPARE(ticks[0].second,QCoreApplication::translate("Allpowers","Epoch %1").arg(1));
    QCOMPARE(ticks[1].second,QCoreApplication::translate("Allpowers","Epoch %1").arg(2));
    QCOMPARE(ticks[2].second,QCoreApplication::translate("Allpowers","Epoch %1").arg(10));
    QVERIFY(chart.points()[0].wall<chart.points()[1].wall);
    QVERIFY(chart.points()[2].wall<chart.points()[3].wall);
    QCOMPARE(raw[0].wall,qint64(0));
    QBuffer csv;csv.open(QIODevice::WriteOnly);QVERIFY(writeHistoryCsv(raw,csv));
    QVERIFY(csv.data().contains("1800000010"));QVERIFY(!csv.data().contains("1800001000"));
    const auto shots=qEnvironmentVariable("ALLPOWERS_TEST_SHOTS");
    if(!shots.isEmpty())chart.grab().save(shots+"/epochs-mixed.png");
  }
  void epochAxisHandlesEmptySingleAndManyRecords(){
    HistoryChart chart;chart.resize(800,400);chart.setEpochMode(true);
    for(int count : {0,1,2,2880}) {
      HistoryPoints rows;for(int i=0;i<count;++i) rows.append(point(4,i+1,i*60));
      chart.setPoints(rows,1800001000);const auto ticks=chart.axisTicks();
      QCOMPARE(ticks.size(),count?1:0);
      if(count) QCOMPARE(ticks.first().second,QCoreApplication::translate("Allpowers","Epoch %1").arg(4));
      for(int i=1;i<count;++i)QVERIFY(chart.points()[i].wall>chart.points()[i-1].wall);
    }
    HistoryPoints rows;for(int i=1;i<=100;++i)rows.append(point(i,i,7));
    chart.setPoints(rows,1800001000);auto ticks=chart.axisTicks();QSet<QString> labels;
    for(const auto &tick:ticks)labels.insert(tick.second);
    QCOMPARE(labels.size(),ticks.size());QVERIFY(ticks.size()<=8);
    // Zooming the epoch view and receiving another sample must not reset its range.
    QWheelEvent wheel(QPointF(400,200),chart.mapToGlobal(QPoint(400,200)),QPoint(),QPoint(0,120),
                      Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(&chart,&wheel);const auto zoomed=chart.axisTicks();
    chart.setPoints(rows,1800001001);QCOMPARE(chart.axisTicks(),zoomed);
  }
  void timeAxisDisambiguatesDaysAndRoundedLogTicks(){
    HistoryChart chart;chart.resize(800,400);chart.setLogarithmic(true);chart.setHorizon(86400);
    const auto end=QDateTime(QDate(2026,10,30),QTime(22,58,15),QTimeZone::systemTimeZone()).toSecsSinceEpoch();
    chart.setPoints({},end);auto ticks=chart.axisTicks();
    QVERIFY(ticks.first().second.startsWith("29.10\n"));
    QVERIFY(ticks.last().second.startsWith("30.10\n"));QCOMPARE(ticks.last().first,end);
    const auto shots=qEnvironmentVariable("ALLPOWERS_TEST_SHOTS");
    if(!shots.isEmpty())chart.grab().save(shots+"/dated-axis.png");
    chart.setHorizon(30);ticks=chart.axisTicks();QSet<QString> labels;
    for(const auto &tick:ticks)labels.insert(tick.second);
    QCOMPARE(labels.size(),ticks.size());
  }
  void currentHidArchivePreservesUndatedEpochs(){
    Source source;const auto now=QDateTime::currentSecsSinceEpoch();
    for(int i=0;i<3;++i){auto r=HistoryFormat::example();r.sequence=i+1;r.session=i==2?2:1;
      r.uptimeSeconds=20+i*60;if(i==1){r.flags|=HistoryFormat::Clock;r.epochSeconds=uint32_t(now-10);}
      r.crc=HistoryFormat::crc32(&r,28);source.records.append(r);}
    Server server(source);auto *channel=new Channel;const QString serial="R2500-000000000001";
    UsbTransport transport(nullptr,false,channel,[&]{return QList<UsbPort>{{serial,"synthetic","test"}};}, {},
      []{return QByteArray(16,'a');});
    std::deque<QByteArray> commands;QString error;unsigned entropy=0;
    HidSession session({[&](QByteArray b){commands.push_back(b);},[&](QByteArray b){channel->feed(b);},
      [&](QString e){error=e;},[&]{QByteArray b(16,0);b[0]=char(++entropy);return b;},[=]{return now;}});
    channel->output=[&](QByteArray b){session.command(b,0);};
    QSignalSpy imported(&transport,&TelemetryTransport::historyImported);
    transport.resume(serial,{},0);uint8_t cap[Payload];capabilities(cap,42,true);
    QVERIFY(session.start(QByteArray(reinterpret_cast<char*>(cap),Payload),0));
    auto drain=[&]{
      int limit=100;while(!commands.empty()&&--limit){auto bytes=commands.front();commands.pop_front();Request r;
        QVERIFY(decode(reinterpret_cast<const uint8_t*>(bytes.data())+1,63,r));
        uint8_t body[MessageLimit];size_t size=0;auto result=server.respond(r,1,body,size);
        for(size_t offset=0;offset<size;){uint8_t report[64]{};report[0]=ResponseReport;
          offset+=fragment(r,result,body,size,offset,report+1);
          session.input(QByteArray(reinterpret_cast<char*>(report),64),1);}
      }QVERIFY(limit>0);
    };
    drain();Live live;live.boot=42;live.uptimeSeconds=100;live.model=3;live.known=7;
    live.address[0]=2;live.address[5]=1;live.flags=Connected|Fresh|MainSeen;live.firmware='R';
    uint8_t encoded[Payload];encode(live,encoded);session.live(QByteArray(reinterpret_cast<char*>(encoded),Payload));
    drain();QCOMPARE(source.clockWrites,1);session.live(QByteArray(reinterpret_cast<char*>(encoded),Payload));
    drain();QCOMPARE(source.clockWrites,1);
    transport.requestHistory();drain();QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(imported.size(),1);auto points=qvariant_cast<HistoryPoints>(imported[0][0]);QCOMPARE(points.size(),3);
    QTemporaryDir dir;HistoryStore store(dir.filePath("history.sqlite3"));QVERIFY(store.importPoints(points,now));
    QCOMPARE(store.points(points[0].address,now).size(),2);QCOMPARE(store.undatedPoints(points[0].address).size(),1);
    QCOMPARE(transport.diagnostics()["usb_history_result"].toString(),QString("complete"));
    channel->output={};
  }
};
QTEST_MAIN(ArchiveTests)
#include "archive_transport_test.moc"
