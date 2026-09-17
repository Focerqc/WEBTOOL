#include "bleuart_wasm.h"
#include "webblebridge.h"
#include <QDebug>
#include <QMetaObject>

BleUart::BleUart(QObject *parent)
    : QObject(parent)
    , mIsConnected(false)
    , mIsConnecting(false)
{
    webble_init();
    webble_set_callbacks(&BleUart::onRxStatic,
                         &BleUart::onStateStatic,
                         &BleUart::onScanStatic,
                         this);
}

BleUart::~BleUart()
{
    disconnectBle();
    webble_set_callbacks(nullptr, nullptr, nullptr, nullptr);
}

void BleUart::startScan()
{
    if (!webble_is_supported()) {
        emit bleError(tr("Web Bluetooth is not supported in this browser. Please use Chrome or Edge."));
        emit scanDone(mDevs, true);
        return;
    }

    qDebug() << "[BLE WASM] Requesting Bluetooth device via Web Bluetooth picker...";
    mDevs.clear();
    webble_request_device();
}

void BleUart::startConnect(QString addr)
{
    qDebug() << "[BLE WASM] Connecting to BLE device:" << addr;
    mIsConnecting = true;
    mIsConnected = false;
    webble_connect(addr.isEmpty() ? nullptr : addr.toUtf8().constData());
}

void BleUart::disconnectBle()
{
    qDebug() << "[BLE WASM] Disconnecting BLE...";
    mIsConnecting = false;
    mIsConnected = false;
    webble_disconnect();
}

bool BleUart::isConnected()
{
    return webble_is_connected() || mIsConnected;
}

bool BleUart::isConnecting()
{
    return webble_is_connecting() || mIsConnecting;
}

void BleUart::emitScanDone()
{
    emit scanDone(mDevs, true);
}

void BleUart::writeData(QByteArray data)
{
    if (data.isEmpty()) return;
    webble_write(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
}

// ---------------------------------------------------------------------------
// Static Callbacks from JavaScript
// ---------------------------------------------------------------------------

void BleUart::onRxStatic(const uint8_t *data, int len, void *userData)
{
    if (!userData || !data || len <= 0) return;
    BleUart *self = static_cast<BleUart*>(userData);
    QByteArray chunk(reinterpret_cast<const char*>(data), len);

    QMetaObject::invokeMethod(self, [self, chunk]() {
        self->handleRx(chunk);
    }, Qt::QueuedConnection);
}

void BleUart::onStateStatic(int state, const char *name, const char *addr, void *userData)
{
    if (!userData) return;
    BleUart *self = static_cast<BleUart*>(userData);
    QString nameStr = QString::fromUtf8(name ? name : "");
    QString addrStr = QString::fromUtf8(addr ? addr : "");

    QMetaObject::invokeMethod(self, [self, state, nameStr, addrStr]() {
        self->handleState(state, nameStr, addrStr);
    }, Qt::QueuedConnection);
}

void BleUart::onScanStatic(const char *name, const char *addr, void *userData)
{
    if (!userData) return;
    BleUart *self = static_cast<BleUart*>(userData);
    QString nameStr = QString::fromUtf8(name ? name : "VESC BLE");
    QString addrStr = QString::fromUtf8(addr ? addr : "");

    QMetaObject::invokeMethod(self, [self, nameStr, addrStr]() {
        self->handleScan(nameStr, addrStr);
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Internal Event Handlers (Executed on Qt Event Thread)
// ---------------------------------------------------------------------------

void BleUart::handleRx(const QByteArray &chunk)
{
    emit dataRx(chunk);
}

void BleUart::handleState(int state, const QString &name, const QString &addr)
{
    Q_UNUSED(name);
    Q_UNUSED(addr);

    if (state == WEBBLE_CONNECTED) {
        mIsConnected = true;
        mIsConnecting = false;
        qDebug() << "[BLE WASM] Device connected successfully!";
        emit connected();
    } else if (state == WEBBLE_CONNECTING) {
        mIsConnecting = true;
        mIsConnected = false;
    } else if (state == WEBBLE_DISCONNECTED) {
        bool wasConnected = mIsConnected;
        mIsConnected = false;
        mIsConnecting = false;
        qDebug() << "[BLE WASM] Device disconnected.";
        if (wasConnected) {
            emit unintentionalDisconnect();
        }
    } else if (state == WEBBLE_ERROR) {
        mIsConnected = false;
        mIsConnecting = false;
        qWarning() << "[BLE WASM] Device error:" << name;
        emit bleError(name.isEmpty() ? tr("Bluetooth connection error") : name);
    }
}

void BleUart::handleScan(const QString &name, const QString &addr)
{
    if (!addr.isEmpty()) {
        mDevs.insert(addr, name);
    } else {
        mDevs.insert("webble_default", name);
    }
    emit scanDone(mDevs, false);
}
