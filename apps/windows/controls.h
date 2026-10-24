#pragma once
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>

class AdaptiveLabel : public QLabel {
  Q_OBJECT
public:
  explicit AdaptiveLabel(const QString &text, QWidget *parent = nullptr);
  QFont fittedFont() const;

protected:
  void paintEvent(QPaintEvent *) override;
};

class Select : public QComboBox {
  Q_OBJECT
  QWidget *popup = nullptr;
  bool popupAbove = false;

public:
  explicit Select(QWidget *parent = nullptr);
  void showPopup() override;
  void hidePopup() override;
signals:
  void popupClosed();

protected:
  bool eventFilter(QObject *, QEvent *) override;
  void paintEvent(QPaintEvent *) override;
};

class LiveLog : public QPlainTextEdit {
  Q_OBJECT
public:
  explicit LiveLog(QWidget *parent = nullptr);
  bool following() const;
  void appendEntry(const QString &message);
  void followTail();
signals:
  void followingChanged(bool following);
};
