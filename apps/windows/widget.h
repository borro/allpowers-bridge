#pragma once
#include "desktop_surface.h"
#include "history.h"
#include "transport_router.h"
#include <QElapsedTimer>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>
class QSettings;
class QDialog;
class QPainter;
bool setWidgetStartup(QSettings &store, bool enabled, const QString &executable);

class WidgetCard : public DesktopSurface {
  Q_OBJECT
  ap_snapshot snapshot_{};
  HistoryPoints points_;
  QString phase_ = QStringLiteral("Нет подключения");
  QString deviceName_ = QStringLiteral("Нет устройства");
  qint64 now_ = 0;
  QPoint dragOrigin, savedOffset;
  bool dragging = false, locked_ = false;
  void drawCard(QPainter &painter) const;

public:
  explicit WidgetCard(bool desktop = true, std::function<quintptr()> hostFinder = {});
  void setTelemetry(ap_snapshot value, QString phase);
  void setDeviceName(QString name);
  void setHistory(HistoryPoints points, qint64 now);
  QImage renderedImage() const;
  void setLocked(bool value) {
    locked_ = value;
    setCursor(value ? Qt::ArrowCursor : Qt::SizeAllCursor);
  }
  static QString reading(const ap_snapshot &, quint32 mask, quint32 value, QString suffix);
  static QString remainingTime(const ap_snapshot &);
  static int graphSegments(const HistoryPoints &, quint32 mask, qint64 now);
signals:
  void menuRequested(QPoint position);
  void positionChanged();

protected:
  bool event(QEvent *) override;
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void contextMenuEvent(QContextMenuEvent *) override;
};

class WidgetController : public QObject {
  Q_OBJECT
  QString directory;
  QJsonObject settings;
  WidgetCard card_;
  TransportRouter transport;
  QSystemTrayIcon tray;
  QMenu menu;
  QThread historyThread;
  HistoryWorker *history;
  QElapsedTimer clock;
  QTimer timer;
  ap_snapshot latest{};
  qint64 receivedAt = -1, sampleAt = -1;
  QString historyAddress, historyError;
  HistoryPoints stored, remotePoints;
  bool offline_, locked = false;
  QPointer<QDialog> settingsDialog;
  void save();
  void refresh();
  void receive(ap_snapshot);
  void connectStation();
  void disconnectStation();
  Q_INVOKABLE void configure();

public:
  explicit WidgetController(QString directory, bool offline = false, bool desktop = true);
  ~WidgetController() override;
  WidgetCard *card() {
    return &card_;
  }
  void start();
  void loadDemo();
  QJsonObject diagnostics() const;
signals:
  void openHistory(QString path);
  void loadHistory(QString address, qint64 now);
  void sample(QString address, ap_snapshot value, qint64 wall, qint64 tick);
  void newSession();
  void importHistory(HistoryPoints points, qint64 now);
};
