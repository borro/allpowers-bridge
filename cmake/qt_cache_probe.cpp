#include <QApplication>
#include <QBluetoothUuid>
#include <QImageReader>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSslSocket>
#include <QTest>
#include <QVariant>
#include <QWidget>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget widget;
    widget.resize(32, 32);
    widget.show();
    QTest::qWait(1);
    // Exercise Bluetooth linkage without discovery or a device connection.
    const QBluetoothUuid uuid(QBluetoothUuid::ServiceClassUuid::GenericAccess);
    if (uuid.isNull() || !QImageReader::supportedImageFormats().contains("ico")) return 1;
    const auto protocols = QSslSocket::supportedProtocols();
    (void)protocols;
    auto database = QSqlDatabase::addDatabase("QSQLITE");
    database.setDatabaseName(":memory:");
    if (!database.open()) return 2;
    QSqlQuery query;
    return query.exec("SELECT 42") && query.next() && query.value(0).toInt() == 42 ? 0 : 3;
}
