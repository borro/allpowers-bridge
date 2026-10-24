#pragma once
#include <QImage>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>
#include <functional>

// Native DWM surface is independent of Qt's QWidget backing-store lifecycle.
class DesktopSurface : public QWidget {
  Q_OBJECT
  QTimer retry;
  quintptr shell = 0, handle = 0;
  QString screenName_, error_;
  QPoint offset_{24, 24};
  bool enabled_, published_ = false;
  std::function<quintptr()> hostFinder_;
  QImage frame_;
  void attach();
  void publish();

public:
  explicit DesktopSurface(bool enabled = true, std::function<quintptr()> hostFinder = {});
  ~DesktopSurface() override;
  void setDesktopPosition(QString screen, QPoint offset);
  QString desktopScreen() const {
    return screenName_;
  }
  QPoint desktopOffset() const {
    return offset_;
  }
  qreal desktopScale() const;
  bool attached() const {
    return shell != 0 && handle != 0;
  }
  quintptr nativeHandle() const {
    return handle;
  }
  QString attachmentError() const {
    return error_;
  }
  void presentImage(const QImage &image);
  void capturePointer();
  void releasePointer();
  QJsonObject nativeDiagnostics() const;
  static qintptr dispatchNative(quintptr window, quint32 message, quintptr wparam, qintptr lparam);
signals:
  void attachmentChanged();
  void surfaceChanged();
};
