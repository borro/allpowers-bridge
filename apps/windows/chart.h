#pragma once
#include "history.h"
#include <QElapsedTimer>
#include <QPixmap>
#include <QWidget>
class QPainter;

class HistoryChart : public QWidget {
  Q_OBJECT
  HistoryPoints points_;
  qint64 now_ = 0, end_ = 0;
  double span_ = 86400;
  bool logarithmic_ = false, following_ = true, series_[3] = {true, true, true};
  QPoint hover_ = {-1, -1}, drag_;
  bool dragging_ = false;
  qint64 dragEnd_ = 0;
  double lastRenderMs_ = 0;
  int vertices_ = 0;
  QPixmap cache_;
  bool dirty_ = true;
  void renderBase(QPainter &);
  QRectF plot() const;
  double xAt(qint64 time) const;
  qint64 timeAt(double x) const;
  void clamp();

protected:
  void paintEvent(QPaintEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void leaveEvent(QEvent *) override;
  void wheelEvent(QWheelEvent *) override;

public:
  explicit HistoryChart(QWidget *parent = nullptr);
  void setPoints(const HistoryPoints &, qint64 now);
  const HistoryPoints &points() const {
    return points_;
  }
  double renderMs() const {
    return lastRenderMs_;
  }
  int vertices() const {
    return vertices_;
  }
  bool following() const {
    return following_;
  }
public slots:
  void setHorizon(int seconds);
  void setLogarithmic(bool value);
  void setSeries(int index, bool enabled);
  void followLive();
signals:
  void viewChanged();
};
