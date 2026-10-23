#include "window.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

class MonitorTests : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
private slots:
  void initTestCase() {
    qputenv("ALLPOWERS_DATA_DIR", temp.path().toUtf8());
    auto fonts = QDir(qEnvironmentVariable("SystemRoot") + "/Fonts");
    for (const QString &name : {QString("segoeui.ttf"), QString("segoeuib.ttf")})
      QFontDatabase::addApplicationFont(fonts.filePath(name));
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
