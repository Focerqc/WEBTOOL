#include "qserialport_wasm.h"
#include "webserialbridge.h"
#include "wasm_serial_bridge.h"
#include <QDebug>
#include <QCoreApplication>
#include <cstdio>

// ---------------------------------------------------------------------------
// QSerialPortInfo Implementation
// ---------------------------------------------------------------------------

QSerialPortInfo::QSerialPortInfo()
    : mPortId(-1)
    , mVendorId(0)
    , mProductId(0)
    , mIsNull(true)
{
}

QSerialPortInfo::QSerialPortInfo(const QString &name)
    : mPortId(-1)
    , mName(name)
    , mSystemLocation(name)
    , mVendorId(0)
    , mProductId(0)
    , mIsNull(false)
{
    webserial_init();
    int count = webserial_get_port_count();
    for (int i = 0; i < count; i++) {
        char nameBuf[128] = {0};
        int vid = 0, pid = 0;
        if (webserial_get_port_info(i, nameBuf, sizeof(nameBuf) - 1, &vid, &pid)) {
            QString pName = QString::fromUtf8(nameBuf);
            if (pName == name || QString("WebSerial%1").arg(i) == name) {
                mPortId = i;
                mName = pName;
                mSystemLocation = QString("webserial://port%1").arg(i);
                mDescription = pName;
                mVendorId = static_cast<quint16>(vid);
                mProductId = static_cast<quint16>(pid);
                break;
            }
        }
    }
}

QSerialPortInfo::QSerialPortInfo(const QSerialPort &port)
    : QSerialPortInfo(port.portName())
{
}

QSerialPortInfo::QSerialPortInfo(const QSerialPortInfo &other)
    : mPortId(other.mPortId)
    , mName(other.mName)
    , mSystemLocation(other.mSystemLocation)
    , mDescription(other.mDescription)
    , mManufacturer(other.mManufacturer)
    , mVendorId(other.mVendorId)
    , mProductId(other.mProductId)
    , mIsNull(other.mIsNull)
{
}

QSerialPortInfo::~QSerialPortInfo()
{
}

QSerialPortInfo &QSerialPortInfo::operator=(const QSerialPortInfo &other)
{
    if (this != &other) {
        mPortId = other.mPortId;
        mName = other.mName;
        mSystemLocation = other.mSystemLocation;
        mDescription = other.mDescription;
        mManufacturer = other.mManufacturer;
        mVendorId = other.mVendorId;
        mProductId = other.mProductId;
        mIsNull = other.mIsNull;
    }
    return *this;
}

void QSerialPortInfo::swap(QSerialPortInfo &other)
{
    std::swap(mPortId, other.mPortId);
    std::swap(mName, other.mName);
    std::swap(mSystemLocation, other.mSystemLocation);
    std::swap(mDescription, other.mDescription);
    std::swap(mManufacturer, other.mManufacturer);
    std::swap(mVendorId, other.mVendorId);
    std::swap(mProductId, other.mProductId);
    std::swap(mIsNull, other.mIsNull);
}

QString QSerialPortInfo::portName() const
{
    return mName;
}

QString QSerialPortInfo::systemLocation() const
{
    return mSystemLocation;
}

QString QSerialPortInfo::description() const
{
    return mDescription;
}

QString QSerialPortInfo::manufacturer() const
{
    return mManufacturer;
}

QString QSerialPortInfo::serialNumber() const
{
    return QString();
}

quint16 QSerialPortInfo::vendorIdentifier() const
{
    return mVendorId;
}

quint16 QSerialPortInfo::productIdentifier() const
{
    return mProductId;
}

bool QSerialPortInfo::hasVendorIdentifier() const
{
    return mVendorId != 0;
}

bool QSerialPortInfo::hasProductIdentifier() const
{
    return mProductId != 0;
}

bool QSerialPortInfo::isNull() const
{
    return mIsNull;
}

bool QSerialPortInfo::isBusy() const
{
    if (mPortId >= 0) {
        return webserial_is_open(mPortId) != 0;
    }
    return false;
}

QList<QSerialPortInfo> QSerialPortInfo::availablePorts()
{
    QList<QSerialPortInfo> list;
    webserial_init();
    webserial_sync_ports();

    int count = webserial_get_port_count();
    for (int i = 0; i < count; i++) {
        char nameBuf[128] = {0};
        int vid = 0, pid = 0;
        if (webserial_get_port_info(i, nameBuf, sizeof(nameBuf) - 1, &vid, &pid)) {
            QSerialPortInfo info;
            info.mPortId = i;
            info.mName = QString::fromUtf8(nameBuf);
            info.mSystemLocation = QString("webserial://port%1").arg(i);
            info.mDescription = info.mName;
            info.mVendorId = static_cast<quint16>(vid);
            info.mProductId = static_cast<quint16>(pid);
            if (vid == 0x0483 || pid == 22336 || pid == 0x5740) {
                info.mManufacturer = "STMicroelectronics";
            } else if (vid == 0x10c4) {
                info.mManufacturer = "Silicon Labs";
            } else if (vid == 0x303a) {
                info.mManufacturer = "Espressif";
            }
            info.mIsNull = false;
            list.append(info);
        }
    }
    return list;
}

QList<qint32> QSerialPortInfo::standardBaudRates()
{
    return { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
}

// ---------------------------------------------------------------------------
// QSerialPort Implementation
// ---------------------------------------------------------------------------

QSerialPort::QSerialPort(QObject *parent)
    : QIODevice(parent)
{
    init();
}

QSerialPort::QSerialPort(const QString &name, QObject *parent)
    : QIODevice(parent)
{
    init();
    setPortName(name);
}

QSerialPort::QSerialPort(const QSerialPortInfo &info, QObject *parent)
    : QIODevice(parent)
{
    init();
    setPort(info);
}

QSerialPort::~QSerialPort()
{
    if (isOpen()) {
        close();
    }
}

void QSerialPort::init()
{
    webserial_init();
    mPortId = -1;
    mBaudRate = Baud115200;
    mDataBits = Data8;
    mParity = NoParity;
    mStopBits = OneStop;
    mFlowControl = NoFlowControl;
    mLastError = NoError;
}

void QSerialPort::setPortName(const QString &name)
{
    mPortName = name;
    mPortId = -1;

    // Check if name is in format webserial://portN or contains port index
    if (name.startsWith("webserial://port")) {
        bool ok = false;
        int id = name.mid(16).toInt(&ok);
        if (ok) {
            mPortId = id;
            return;
        }
    }

    auto ports = QSerialPortInfo::availablePorts();
    for (const auto &p : ports) {
        if (p.portName() == name || p.systemLocation() == name) {
            mPortId = p.internalPortId();
            break;
        }
    }
}

QString QSerialPort::portName() const
{
    return mPortName;
}

void QSerialPort::setPort(const QSerialPortInfo &info)
{
    mPortName = info.portName();
    mPortId = info.internalPortId();
}

bool QSerialPort::open(OpenMode mode)
{
    if (isOpen()) {
        close();
    }

    if (mPortId < 0) {
        setPortName(mPortName);
        if (mPortId < 0) {
            mPortId = 0;
        }
    }

    webserial_set_callbacks(mPortId, onRxDataStatic, onErrorStatic, this);

    webserial_open(mPortId, mBaudRate, (int)mDataBits, (int)mStopBits, (int)mParity, (int)mFlowControl);

    OpenMode openMode = (mode == QIODevice::NotOpen) ? QIODevice::ReadWrite : mode;
    setOpenMode(openMode);
    mLastError = NoError;
    return true;
}

void QSerialPort::close()
{
    if (mPortId >= 0) {
        webserial_close(mPortId);
    }
    setOpenMode(QIODevice::NotOpen);
}

qint64 QSerialPort::bytesAvailable() const
{
    qint64 bytes = QIODevice::bytesAvailable();
    if (mPortId >= 0) {
        bytes += webserial_bytes_available(mPortId);
    }
    return bytes;
}

bool QSerialPort::flush()
{
    if (mPortId >= 0) {
        webserial_flush(mPortId);
        return true;
    }
    return false;
}

bool QSerialPort::setBaudRate(qint32 baudRate, Directions directions)
{
    Q_UNUSED(directions);
    mBaudRate = baudRate;
    emit baudRateChanged(mBaudRate, directions);
    return true;
}

qint32 QSerialPort::baudRate(Directions directions) const
{
    Q_UNUSED(directions);
    return mBaudRate;
}

bool QSerialPort::setDataBits(DataBits dataBits)
{
    mDataBits = dataBits;
    return true;
}

QSerialPort::DataBits QSerialPort::dataBits() const
{
    return mDataBits;
}

bool QSerialPort::setParity(Parity parity)
{
    mParity = parity;
    return true;
}

QSerialPort::Parity QSerialPort::parity() const
{
    return mParity;
}

bool QSerialPort::setStopBits(StopBits stopBits)
{
    mStopBits = stopBits;
    return true;
}

QSerialPort::StopBits QSerialPort::stopBits() const
{
    return mStopBits;
}

bool QSerialPort::setFlowControl(FlowControl flowControl)
{
    mFlowControl = flowControl;
    return true;
}

QSerialPort::FlowControl QSerialPort::flowControl() const
{
    return mFlowControl;
}

QSerialPort::SerialPortError QSerialPort::error() const
{
    return mLastError;
}

void QSerialPort::clearError()
{
    mLastError = NoError;
}

bool QSerialPort::requestWebSerialPermission()
{
    int res = webserial_request_port();
    return res >= 0;
}

qint64 QSerialPort::readData(char *data, qint64 maxlen)
{
    if (mPortId < 0 || maxlen <= 0) return 0;
    return webserial_read(mPortId, reinterpret_cast<uint8_t*>(data), static_cast<int>(maxlen));
}

qint64 QSerialPort::writeData(const char *data, qint64 maxSize)
{
    if (maxSize <= 0 || !data) return 0;

    printf("[SERIAL TX] writeData called with %lld bytes\n", (long long)maxSize);
    fflush(stdout);

    wasm_serial_tx(reinterpret_cast<const uint8_t*>(data), static_cast<int>(maxSize));

    if (mPortId >= 0) {
        webserial_write(mPortId, reinterpret_cast<const uint8_t*>(data), static_cast<int>(maxSize));
    }
    return maxSize;
}

void QSerialPort::onRxDataStatic(int portId, void *userData)
{
    Q_UNUSED(portId);
    if (userData) {
        auto self = static_cast<QSerialPort*>(userData);
        emit self->readyRead();
    }
}

void QSerialPort::onErrorStatic(int portId, int errorCode, const char *errorMsg, void *userData)
{
    Q_UNUSED(portId);
    if (userData) {
        auto self = static_cast<QSerialPort*>(userData);
        self->mLastError = static_cast<SerialPortError>(errorCode);
        if (errorMsg) {
            self->setErrorString(QString::fromUtf8(errorMsg));
        }
        emit self->errorOccurred(self->mLastError);
    }
}
