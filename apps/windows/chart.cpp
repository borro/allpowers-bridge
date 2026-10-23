#include "chart.h"
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimeZone>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

static const QColor colors[] = {QColor("#54e0aa"), QColor("#65baff"), QColor("#eff5ff")};
static const QTimeZone &displayTimeZone() {
  static const auto zone = QTimeZone::systemTimeZone();
  return zone;
}
HistoryChart::HistoryChart(QWidget *p) : QWidget(p) {
  setMinimumHeight(300);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
}
QRectF HistoryChart::plot() const {
  return QRectF(54, 30, qMax(1, width() - 116), qMax(1, height() - 78));
}
double HistoryChart::xAt(qint64 time) const {
  auto r = plot();
  double age = double(end_ - time);
  return r.left() + r.width() * (logarithmic_ ? logPosition(age, span_) : 1 - age / span_);
}
qint64 HistoryChart::timeAt(double x) const {
  auto r = plot();
  double fraction = qBound(0., (x - r.left()) / r.width(), 1.);
  return end_ - qint64(logarithmic_ ? std::expm1((1 - fraction) * std::log1p(span_))
                                    : (1 - fraction) * span_);
}
void HistoryChart::clamp() {
  span_ = qBound(30., span_, 172800.);
  end_ = qBound(now_ - qint64(172800 - span_), end_, now_);
}
void HistoryChart::setPoints(const HistoryPoints &p, qint64 now) {
  points_ = p;
  now_ = now;
  if (following_ || !end_)
    end_ = now;
  clamp();
  dirty_ = true;
  update();
}
void HistoryChart::setHorizon(int seconds) {
  span_ = seconds;
  followLive();
}
void HistoryChart::setLogarithmic(bool value) {
  logarithmic_ = value;
  dirty_ = true;
  update();
}
void HistoryChart::setSeries(int index, bool enabled) {
  if (index >= 0 && index < 3) {
    series_[index] = enabled;
    dirty_ = true;
    update();
  }
}
void HistoryChart::followLive() {
  following_ = true;
  end_ = now_;
  clamp();
  dirty_ = true;
  update();
  emit viewChanged();
}
void HistoryChart::renderBase(QPainter &painter) {
  vertices_ = 0;
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), QColor("#172235"));
  auto r = plot();
  double peak = 0;
  for (const auto &p : points_)
    if (p.wall >= end_ - span_ && p.wall <= end_ && p.data.fresh) {
      if (p.data.known & 2)
        peak = qMax(peak, double(p.data.input_w));
      if (p.data.known & 4)
        peak = qMax(peak, double(p.data.output_w));
    }
  double ceiling = 100;
  if (peak > 0) {
    double magnitude = std::pow(10, std::floor(std::log10(peak)));
    for (double factor : {1., 2., 5., 10.})
      if (factor * magnitude >= peak) {
        ceiling = factor * magnitude;
        break;
      }
  }
  painter.setFont(QFont("Segoe UI", 9));
  for (int step = 0; step <= 4; ++step) {
    double y = r.bottom() - r.height() * step / 4.;
    painter.setPen(QColor("#2b3a50"));
    painter.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    painter.setPen(QColor("#a5b5cf"));
    painter.drawText(QRectF(0, y - 10, 46, 20), Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(step * 25) + "%");
    painter.drawText(QRectF(r.right() + 8, y - 10, 50, 20), Qt::AlignLeft | Qt::AlignVCenter,
                     QString::number(ceiling * step / 4., 'g', 4));
  }
  for (int i = 0; i <= 5; ++i) {
    double x = r.left() + r.width() * i / 5.;
    auto when = QDateTime::fromSecsSinceEpoch(timeAt(x), displayTimeZone());
    painter.drawText(QRectF(x - 45, r.bottom() + 13, 90, 28), Qt::AlignCenter,
                     when.toString(span_ > 86400 ? "dd.MM HH:mm" : "HH:mm:ss"));
  }
  painter.save();
  painter.setClipRect(r.adjusted(-1, -1, 1, 1));
  for (int i = 1; i < points_.size(); ++i) {
    auto p = points_[i];
    if (p.wall < end_ - span_ || p.wall > end_)
      continue;
    int change = sourceChange(points_[i - 1], p);
    if (change) {
      painter.setPen(QPen(change == 1 ? QColor("#ff6f7c") : QColor("#54e0aa"), 1, Qt::DashLine));
      painter.drawLine(QPointF(xAt(p.wall), r.top()), QPointF(xAt(p.wall), r.bottom()));
    }
  }
  // Per-column extrema preserve peaks while limiting painter work to screen width.
  for (int series = 0; series < 3; ++series) {
    if (!series_[series])
      continue;
    painter.setPen(QPen(colors[series], series == 0 ? 2.4 : 1.8));
    QPolygonF path;
    bool started = false;
    int column = -1, minAt = 0, maxAt = 0, order = 0;
    double first = 0, last = 0, lo = 0, hi = 0, x = 0;
    auto finishPath = [&] {
      if (path.size() > 1)
        painter.drawPolyline(path);
      else if (path.size() == 1)
        painter.drawPoint(path[0]);
      path.clear();
    };
    auto flush = [&] {
      if (column < 0)
        return;
      double xx = r.left() + column + .5;
      if (!started) {
        finishPath();
        started = true;
      }
      path.append(QPointF(xx, first));
      vertices_++;
      double previousY = first;
      for (double y : {minAt < maxAt ? lo : hi, minAt < maxAt ? hi : lo, last})
        if (y != previousY) {
          path.append(QPointF(xx, y));
          previousY = y;
          vertices_++;
        }
      column = -1;
    };
    const HistoryPoint *previous = nullptr;
    for (const auto &p : points_) {
      if (p.wall < end_ - span_ || p.wall > end_)
        continue;
      const auto &s = p.data;
      quint32 mask = series == 0 ? 1 : series == 1 ? 2 : 4;
      if (!s.fresh || !(s.known & mask)) {
        flush();
        started = false;
        previous = nullptr;
        continue;
      }
      if (previous && !joinPoints(*previous, p)) {
        flush();
        started = false;
      }
      x = xAt(p.wall);
      double value = series == 0 ? s.soc : series == 1 ? s.input_w : s.output_w;
      double y = r.bottom() - qBound(0., value / (series == 0 ? 100 : ceiling), 1.) * r.height();
      int next = int(x - r.left());
      if (next != column) {
        flush();
        column = next;
        first = last = lo = hi = y;
        order = minAt = maxAt = 0;
      } else {
        last = y;
        ++order;
        if (y < lo) {
          lo = y;
          minAt = order;
        }
        if (y > hi) {
          hi = y;
          maxAt = order;
        }
      }
      previous = &p;
    }
    flush();
    finishPath();
  }
  painter.restore();
  if (points_.isEmpty()) {
    painter.setPen(QColor("#a5b5cf"));
    painter.drawText(r, Qt::AlignCenter, "История появится после подключения к станции");
  }
}
void HistoryChart::paintEvent(QPaintEvent *) {
  QElapsedTimer timer;
  timer.start();
  QPainter painter(this);
  auto r = plot();
  auto dpr = devicePixelRatioF();
  QSize physical(qRound(width() * dpr), qRound(height() * dpr));
  if (dirty_ || cache_.size() != physical) {
    cache_ = QPixmap(physical);
    cache_.setDevicePixelRatio(dpr);
    QPainter layer(&cache_);
    renderBase(layer);
    dirty_ = false;
  }
  painter.drawPixmap(0, 0, cache_);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setFont(QFont("Segoe UI", 9));
  if (r.contains(hover_) && !points_.isEmpty()) {
    auto time = timeAt(hover_.x());
    auto it = std::lower_bound(points_.cbegin(), points_.cend(), time,
                               [](const auto &p, qint64 t) { return p.wall < t; });
    if (it == points_.cend())
      --it;
    else if (it != points_.cbegin() && qAbs((it - 1)->wall - time) < qAbs(it->wall - time))
      --it;
    if (it->wall >= end_ - span_ && it->wall <= end_) {
      painter.setPen(QPen(QColor("#657a98"), 1, Qt::DashLine));
      painter.drawLine(QPointF(xAt(it->wall), r.top()), QPointF(xAt(it->wall), r.bottom()));
      auto value = [](const ap_snapshot &s, quint32 mask, quint32 n) {
        return s.known & mask ? QString::number(n) : QString("—");
      };
      QString text =
          QDateTime::fromSecsSinceEpoch(it->wall, displayTimeZone())
              .toString("dd.MM.yyyy HH:mm:ss") +
          "\n" +
          QString("Заряд %1%   Вход %2 Вт   Выход %3 Вт\n%4 · %5")
              .arg(value(it->data, 1, it->data.soc), value(it->data, 2, it->data.input_w),
                   value(it->data, 4, it->data.output_w),
                   it->data.source == 2   ? "Розетка"
                   : it->data.source == 1 ? "Аккумулятор"
                                          : "Источник неизвестен",
                   it->data.fresh ? "Актуальные данные" : "Разрыв данных");
      QRectF box(r.left() + 12, r.top() + 12, qMin(420., r.width() - 24), 70);
      painter.fillRect(box, QColor("#23344c"));
      painter.setPen(QColor("#eff5ff"));
      painter.drawText(box.adjusted(10, 6, -10, -6), text);
    }
  }
  lastRenderMs_ = timer.nsecsElapsed() / 1e6;
}
void HistoryChart::mouseMoveEvent(QMouseEvent *event) {
  hover_ = event->position().toPoint();
  if (dragging_) {
    double scale = span_ / plot().width();
    end_ = dragEnd_ + qint64((event->position().x() - drag_.x()) * scale);
    clamp();
    following_ = false;
    dirty_ = true;
    emit viewChanged();
  }
  update();
}
void HistoryChart::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && plot().contains(event->position())) {
    dragging_ = true;
    drag_ = event->position().toPoint();
    dragEnd_ = end_;
    setCursor(Qt::ClosedHandCursor);
  }
}
void HistoryChart::mouseReleaseEvent(QMouseEvent *) {
  dragging_ = false;
  unsetCursor();
}
void HistoryChart::leaveEvent(QEvent *) {
  hover_ = {-1, -1};
  update();
}
void HistoryChart::wheelEvent(QWheelEvent *event) {
  if (!plot().contains(event->position())) {
    event->ignore();
    return;
  }
  auto anchor = timeAt(event->position().x());
  double fraction = qBound(0., (event->position().x() - plot().left()) / plot().width(), 1.);
  span_ = qBound(30., span_ * std::pow(.8, event->angleDelta().y() / 120.), 172800.);
  end_ = anchor + qint64(logarithmic_ ? std::expm1((1 - fraction) * std::log1p(span_))
                                      : (1 - fraction) * span_);
  following_ = false;
  dirty_ = true;
  clamp();
  update();
  emit viewChanged();
  event->accept();
}
