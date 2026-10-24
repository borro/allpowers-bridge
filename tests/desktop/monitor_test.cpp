#include "window.h"
#include <QAbstractItemView>
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
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QWheelEvent>

class MonitorTests : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
private slots:
  void initTestCase() {
    QApplication::setStyle("Fusion");
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
