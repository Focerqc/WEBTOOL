#ifndef BLEUART_WASM_H
#define BLEUART_WASM_H

#include <QObject>
#include <QVariantMap>
#include <QByteArray>
#include <QQmlEngine>

class BleUart : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BleUart)

public:
    explicit BleUart(QObject *parent = nullptr);
    virtual ~BleUart();

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void startConnect(QString addr);
    Q_INVOKABLE void disconnectBle();
    Q_INVOKABLE bool isConnected();
    Q_INVOKABLE bool isConnecting();
    Q_INVOKABLE void emitScanDone();

signals:
    void dataRx(QByteArray data);
    void scanDone(QVariantMap devs, bool done);
    void bleError(QString info);
    void connected();
    void unintentionalDisconnect();

public slots:
    void writeData(QByteArray data);

private:
    static void onRxStatic(const uint8_t *data, int len, void *userData);
    static void onStateStatic(int state, const char *name, const char *addr, void *userData);
    static void onScanStatic(const char *name, const char *addr, void *userData);

    void handleRx(const QByteArray &chunk);
    void handleState(int state, const QString &name, const QString &addr);
    void handleScan(const QString &name, const QString &addr);

    QVariantMap mDevs;
    bool mIsConnected;
    bool mIsConnecting;
};

#endif // BLEUART_WASM_H
