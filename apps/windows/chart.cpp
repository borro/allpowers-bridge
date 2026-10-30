#include "chart.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
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
bool HistoryChart::visible(const HistoryPoint &p) const {
  return p.wall >= end_ - span_ && p.wall <= end_;
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
  span_ = qBound(30., span_, epochMode_ ? qMax(60., double(now_)) : 172800.);
  if (epochMode_) {
    end_ = qBound(qMin(now_, qint64(span_)), end_, now_);
    return;
  }
  const qint64 earliest =
      points_.isEmpty() ? now_ - 172800 : qMin(now_ - 172800, points_.first().wall);
  end_ = qBound(qMin(now_, earliest + qint64(span_)), end_, now_);
}
void HistoryChart::setEpochMode(bool enabled) {
  if (epochMode_ == enabled)
    return;
  epochMode_ = enabled;
  following_ = true;
  end_ = 0;
}
void HistoryChart::setPoints(const HistoryPoints &p, qint64 now) {
  source_ = epochMode_ ? historyByEpoch(p, {}) : p;
  if (!epochMode_)
    std::stable_sort(source_.begin(), source_.end(),
                     [](const auto &a, const auto &b) { return a.wall < b.wall; });
  points_ = source_;
  epochs_.clear();
  if (epochMode_) {
    constexpr qint64 width = 60000;
    for (qsizetype first = 0; first < points_.size();) {
      qsizetype last = first + 1;
      const auto epoch = historyEpoch(points_[first]);
      const auto board = historyBoard(points_[first]);
      while (last < points_.size() && historyEpoch(points_[last]) == epoch &&
             historyBoard(points_[last]) == board)
        ++last;
      const qint64 from = epochs_.size() * width;
      epochs_.append({epoch, board, from, from + width});
      for (auto i = first; i < last; ++i)
        points_[i].wall = from + (last - first == 1 ? width / 2
                                                    : width / 10 + (i - first) * (width * 8 / 10) /
                                                                       (last - first - 1));
      first = last;
    }
    now = qMax<qint64>(width, epochs_.size() * width);
    if (following_ || !end_)
      span_ = now;
  }
  if (!epochMode_)
    points_.removeIf([](const auto &point) { return point.wall <= 0; });
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
  if (epochMode_)
    span_ = now_;
  clamp();
  dirty_ = true;
  update();
  emit viewChanged();
}
QList<QPair<qint64, QString>> HistoryChart::axisTicks() const {
  QList<QPair<qint64, QString>> ticks;
  const auto r = plot();
  if (epochMode_) {
    QSet<QString> boards;
    for (const auto &band : epochs_)
      if (!band.board.isEmpty())
        boards.insert(band.board);
    double previousX = -1e9;
    for (const auto &band : epochs_) {
      const auto from = qMax(band.from, end_ - qint64(span_));
      const auto to = qMin(band.to, end_);
      if (from >= to)
        continue;
      const auto middle = from + (to - from) / 2;
      const auto x = xAt(middle);
      if (x - previousX < 90)
        continue;
      auto label = band.epoch ? QCoreApplication::translate("Allpowers", "Epoch %1").arg(band.epoch)
                              : QCoreApplication::translate("Allpowers", "Unknown epoch");
      if (boards.size() > 1 && !band.board.isEmpty())
        label += "\n#" + band.board.right(6);
      ticks.append({middle, label});
      previousX = x;
    }
  } else {
    const auto endDate = QDateTime::fromSecsSinceEpoch(end_, displayTimeZone()).date();
    for (int i = 0; i <= 5; ++i) {
      const auto time = timeAt(r.left() + r.width() * i / 5.);
      if (!ticks.isEmpty() && ticks.last().first == time)
        continue;
      const auto when = QDateTime::fromSecsSinceEpoch(time, displayTimeZone());
      ticks.append({time, when.toString(span_ >= 86400 || when.date() != endDate ? "dd.MM\nHH:mm:ss"
                                                                                 : "HH:mm:ss")});
    }
  }
  return ticks;
}
bool HistoryChart::linked(const HistoryPoint &a, const HistoryPoint &b) const {
  return epochMode_ ? joinEpochPoints(a, b) : joinPoints(a, b);
}
void HistoryChart::renderBase(QPainter &painter) {
  vertices_ = 0;
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), QColor("#172235"));
  auto r = plot();
  if (epochMode_) {
    painter.save();
    painter.setClipRect(r);
    for (qsizetype i = 0; i < epochs_.size(); ++i) {
      const auto &band = epochs_[i];
      const double left = xAt(band.from), right = xAt(band.to);
      painter.fillRect(QRectF(left, r.top(), right - left, r.height()),
                       i % 2 ? QColor("#1d2d43") : QColor("#172235"));
      if (i) {
        painter.setPen(QPen(QColor("#62738c"), 1, Qt::DashLine));
        painter.drawLine(QPointF(left, r.top()), QPointF(left, r.bottom()));
      }
    }
    painter.restore();
  }
  double peak = 0;
  for (const auto &p : points_)
    if (visible(p) && p.data.fresh) {
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
  for (const auto &tick : axisTicks()) {
    const double x = xAt(tick.first);
    painter.drawText(QRectF(x - 45, r.bottom() + 9, 90, 32), Qt::AlignCenter | Qt::TextWordWrap,
                     tick.second);
  }
  painter.save();
  painter.setClipRect(r.adjusted(-4, -4, 4, 4));
  for (int i = 1; i < points_.size(); ++i) {
    auto p = points_.at(i);
    if (!visible(p))
      continue;
    const auto &previous = points_.at(i - 1);
    int change = linked(previous, p) && (previous.data.source == 1 || previous.data.source == 2) &&
                         (p.data.source == 1 || p.data.source == 2) &&
                         previous.data.source != p.data.source
                     ? int(p.data.source)
                     : 0;
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
        painter.drawEllipse(path[0], 2.5, 2.5);
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
      if (!visible(p))
        continue;
      const auto &s = p.data;
      quint32 mask = series == 0 ? 1 : series == 1 ? 2 : 4;
      if (!s.fresh || !(s.known & mask)) {
        flush();
        started = false;
        previous = nullptr;
        continue;
      }
      if (previous && !linked(*previous, p)) {
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
    painter.drawText(r, Qt::AlignCenter,
                     epochMode_
                         ? QCoreApplication::translate("Allpowers", "No archive records")
                         : QCoreApplication::translate(
                               "Allpowers", "History will appear after connecting to a station"));
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
    if (visible(*it)) {
      painter.setPen(QPen(QColor("#657a98"), 1, Qt::DashLine));
      painter.drawLine(QPointF(xAt(it->wall), r.top()), QPointF(xAt(it->wall), r.bottom()));
      auto value = [](const ap_snapshot &s, quint32 mask, quint32 n) {
        return s.known & mask ? QString::number(n) : QString("—");
      };
      const auto epoch = historyEpoch(*it);
      const auto originalWall = epochMode_ ? source_[it - points_.cbegin()].wall : it->wall;
      QString heading;
      if (epochMode_ || !originalWall) {
        if (!epoch) {
          heading = QCoreApplication::translate("Allpowers", "Unknown epoch");
          if (originalWall)
            heading += " · " + QDateTime::fromSecsSinceEpoch(originalWall, displayTimeZone())
                                   .toString("dd.MM.yyyy HH:mm:ss");
        } else if (!it->sequence) {
          heading =
              QCoreApplication::translate("Allpowers", "Epoch %1 · live reading · uptime %2 s")
                  .arg(epoch)
                  .arg(it->tick);
        } else {
          heading = QCoreApplication::translate("Allpowers", "Epoch %1 · record %2 · uptime %3 s")
                        .arg(epoch)
                        .arg(it->sequence)
                        .arg(it->tick);
        }
      } else {
        heading = QDateTime::fromSecsSinceEpoch(originalWall, displayTimeZone())
                      .toString("dd.MM.yyyy HH:mm:ss");
      }
      QString text =
          heading + "\n" +
          QCoreApplication::translate("Allpowers", "Charge %1%   Input %2 W   Output %3 W\n%4 · %5")
              .arg(value(it->data, 1, it->data.soc), value(it->data, 2, it->data.input_w),
                   value(it->data, 4, it->data.output_w),
                   it->data.source == 2 ? QCoreApplication::translate("Allpowers", "Mains")
                   : it->data.source == 1
                       ? QCoreApplication::translate("Allpowers", "Battery")
                       : QCoreApplication::translate("Allpowers", "Unknown power source"),
                   it->data.fresh ? QCoreApplication::translate("Allpowers", "Fresh data")
                                  : QCoreApplication::translate("Allpowers", "Data gap"));
      const bool archive = it->address.startsWith("usb-board:");
      if (archive && !epochMode_)
        text += QCoreApplication::translate("Allpowers", "\nUSB archive · epoch %1 · record %2")
                    .arg(epoch)
                    .arg(it->sequence);
      QRectF box(r.left() + 12, r.top() + 12, qMin(420., r.width() - 24),
                 archive && !epochMode_ ? 90 : 70);
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
  span_ = qBound(30., span_ * std::pow(.8, event->angleDelta().y() / 120.),
                 epochMode_ ? qMax(60., double(now_)) : 172800.);
  end_ = anchor + qint64(logarithmic_ ? std::expm1((1 - fraction) * std::log1p(span_))
                                      : (1 - fraction) * span_);
  following_ = false;
  dirty_ = true;
  clamp();
  update();
  emit viewChanged();
  event->accept();
}
