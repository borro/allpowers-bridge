#include "controls.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QListView>
#include <QPainter>
#include <QProxyStyle>
#include <QScreen>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QTextCursor>

namespace {
class SelectStyle : public QProxyStyle {
public:
  SelectStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
  int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                QStyleHintReturn *data) const override {
    // Use a list popup below the field, not the platform menu/checkmark layout.
    if (hint == SH_ComboBox_Popup)
      return false;
    return QProxyStyle::styleHint(hint, option, widget, data);
  }
};
class SelectItem : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    return {option.fontMetrics.horizontalAdvance(index.data().toString()) + 32, 40};
  }
  void paint(QPainter *p, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const auto row = option.rect.adjusted(3, 2, -3, -2);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    p->setPen(Qt::NoPen);
    p->setBrush(selected ? QColor("#345987") : hover ? QColor("#293e5b") : QColor("#19283e"));
    p->drawRoundedRect(row, 6, 6);
    p->setFont(option.font);
    p->setPen(option.state & QStyle::State_Enabled ? QColor("#edf5ff") : QColor("#71829c"));
    auto textRect = row.adjusted(12, 0, -12, 0);
    p->drawText(
        textRect, Qt::AlignVCenter | Qt::AlignLeft,
        option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, textRect.width()));
    p->restore();
  }
};
} // namespace

Select::Select(QWidget *parent) : QComboBox(parent) {
  auto *listStyle = new SelectStyle;
  listStyle->setParent(this);
  setStyle(listStyle);
  auto *list = new QListView;
  list->setItemDelegate(new SelectItem(list));
  list->setUniformItemSizes(true);
  list->setMouseTracking(true);
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setStyleSheet(
      "QListView{background:#19283e;color:#edf5ff;border:1px solid #48617f;"
      "padding:4px;outline:0;font-family:'Segoe UI';font-size:14px;}"
      "QScrollBar:vertical{background:#19283e;width:10px;margin:0;}"
      "QScrollBar::handle:vertical{background:#526d91;min-height:24px;border-radius:5px;}"
      "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
      "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:none;}");
  setView(list);
  QFont fieldFont("Segoe UI");
  fieldFont.setPixelSize(14);
  setFont(fieldFont);
  setMinimumHeight(42);
  setMaxVisibleItems(8);
  setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  setMinimumContentsLength(20);
  setCursor(Qt::PointingHandCursor);
}
void Select::showPopup() {
  view()->setMinimumWidth(qMin(width(), screen()->availableGeometry().width() - 16));
  QComboBox::showPopup();
  update();
}
void Select::hidePopup() {
  QComboBox::hidePopup();
  update();
  emit popupClosed();
}
void Select::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const bool opened = view()->isVisible();
  p.setBrush(isEnabled() ? QColor("#19283e") : QColor("#172339"));
  p.setPen(QPen(hasFocus() || opened          ? QColor("#69b3ff")
                : underMouse() && isEnabled() ? QColor("#6488ba")
                                              : QColor("#354b6c"),
                1));
  p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 9, 9);
  p.setFont(font());
  p.setPen(isEnabled() && currentIndex() >= 0 ? QColor("#eaf2ff") : QColor("#8195b0"));
  p.drawText(rect().adjusted(12, 0, -40, 0), Qt::AlignVCenter | Qt::AlignLeft,
             fontMetrics().elidedText(currentIndex() < 0 ? placeholderText() : currentText(),
                                      Qt::ElideRight, qMax(0, width() - 52)));
  p.setPen(QPen(isEnabled() ? QColor("#b9cbe7") : QColor("#60718b"), 1.8, Qt::SolidLine,
                Qt::RoundCap, Qt::RoundJoin));
  const qreal x = width() - 22, y = height() / 2.;
  const qreal direction = opened ? -1 : 1;
  p.drawPolyline(QPolygonF{QPointF(x - 4, y - 2 * direction), QPointF(x, y + 2 * direction),
                           QPointF(x + 4, y - 2 * direction)});
}

LiveLog::LiveLog(QWidget *parent) : QPlainTextEdit(parent) {
  setReadOnly(true);
  setMaximumBlockCount(2000);
  setLineWrapMode(QPlainTextEdit::NoWrap);
  connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this] { emit followingChanged(following()); });
}
bool LiveLog::following() const {
  return verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 1;
}
void LiveLog::appendEntry(const QString &message) {
  const bool follow = following();
  const int position = verticalScrollBar()->value();
  const int oldBlocks = document()->blockCount();
  // A document cursor leaves the user's selection/caret untouched.
  QTextCursor end(document());
  end.movePosition(QTextCursor::End);
  if (!document()->isEmpty())
    end.insertBlock();
  end.insertText(message);
  const int removed = qMax(0, oldBlocks + message.count('\n') + 1 - maximumBlockCount());
  verticalScrollBar()->setValue(follow ? verticalScrollBar()->maximum()
                                       : qMax(0, position - removed));
  emit followingChanged(following());
}
void LiveLog::followTail() {
  verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}
