#pragma once
#include <QList>
#include <QObject>
#include <QString>
struct UsbPort {
  QString serial, port, description;
};
QList<UsbPort> allpowersUsbPorts();
class UsbChannel : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void open(const QString &port) = 0;
  virtual void close() = 0;
  virtual void write(const QByteArray &) = 0;
signals:
  void opened();
  void received(QByteArray bytes);
  void failed(QString error);
};
UsbChannel *nativeUsbChannel(QObject *parent);
