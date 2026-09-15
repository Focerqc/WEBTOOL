#ifndef QSERIALPORT_WASM_H
#define QSERIALPORT_WASM_H

#include <QIODevice>
#include <QString>
#include <QList>
#include <QVariant>

class QSerialPort;

class QSerialPortInfo
{
public:
    QSerialPortInfo();
    explicit QSerialPortInfo(const QString &name);
    explicit QSerialPortInfo(const QSerialPort &port);
    QSerialPortInfo(const QSerialPortInfo &other);
    ~QSerialPortInfo();

    QSerialPortInfo &operator=(const QSerialPortInfo &other);

    void swap(QSerialPortInfo &other);

    QString portName() const;
    QString systemLocation() const;
    QString description() const;
    QString manufacturer() const;
    QString serialNumber() const;

    quint16 vendorIdentifier() const;
    quint16 productIdentifier() const;

    bool hasVendorIdentifier() const;
    bool hasProductIdentifier() const;

    bool isNull() const;
    bool isBusy() const;

    static QList<QSerialPortInfo> availablePorts();
    static QList<qint32> standardBaudRates();

    int internalPortId() const { return mPortId; }

private:
    int mPortId;
    QString mName;
    QString mSystemLocation;
    QString mDescription;
    QString mManufacturer;
    quint16 mVendorId;
    quint16 mProductId;
    bool mIsNull;
};

class QSerialPort : public QIODevice
{
    Q_OBJECT

public:
    enum Direction {
        Input = 1,
        Output = 2,
        AllDirections = Input | Output
    };
    Q_DECLARE_FLAGS(Directions, Direction)

    enum BaudRate {
        Baud1200 = 1200,
        Baud2400 = 2400,
        Baud4800 = 4800,
        Baud9600 = 9600,
        Baud19200 = 19200,
        Baud38400 = 38400,
        Baud57600 = 57600,
        Baud115200 = 115200,
        UnknownBaud = -1
    };

    enum DataBits {
        Data5 = 5,
        Data6 = 6,
        Data7 = 7,
        Data8 = 8,
        UnknownDataBits = -1
    };

    enum Parity {
        NoParity = 0,
        EvenParity = 2,
        OddParity = 3,
        SpaceParity = 4,
        MarkParity = 5,
        UnknownParity = -1
    };

    enum StopBits {
        OneStop = 1,
        OneAndHalfStop = 3,
        TwoStop = 2,
        UnknownStopBits = -1
    };

    enum FlowControl {
        NoFlowControl = 0,
        HardwareControl = 1,
        SoftwareControl = 2,
        UnknownFlowControl = -1
    };

    enum SerialPortError {
        NoError,
        DeviceNotFoundError,
        PermissionError,
        OpenError,
        WriteError,
        ReadError,
        ResourceError,
        UnsupportedOperationError,
        UnknownError,
        TimeoutError,
        NotOpenError
    };

    explicit QSerialPort(QObject *parent = nullptr);
    explicit QSerialPort(const QString &name, QObject *parent = nullptr);
    explicit QSerialPort(const QSerialPortInfo &info, QObject *parent = nullptr);
    virtual ~QSerialPort() override;

    void setPortName(const QString &name);
    QString portName() const;

    void setPort(const QSerialPortInfo &info);

    bool open(OpenMode mode) override;
    void close() override;

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

    bool flush();

    bool setBaudRate(qint32 baudRate, Directions directions = AllDirections);
    qint32 baudRate(Directions directions = AllDirections) const;

    bool setDataBits(DataBits dataBits);
    DataBits dataBits() const;

    bool setParity(Parity parity);
    Parity parity() const;

    bool setStopBits(StopBits stopBits);
    StopBits stopBits() const;

    bool setFlowControl(FlowControl flowControl);
    FlowControl flowControl() const;

    SerialPortError error() const;
    void clearError();

    bool setDataTerminalReady(bool set) { Q_UNUSED(set); return true; }
    bool isDataTerminalReady() { return false; }
    bool setRequestToSend(bool set) { Q_UNUSED(set); return true; }
    bool isRequestToSend() { return false; }

    static bool requestWebSerialPermission();

signals:
    void errorOccurred(QSerialPort::SerialPortError error);
    void baudRateChanged(qint32 baudRate, QSerialPort::Directions directions);

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    void init();
    static void onRxDataStatic(int portId, void *userData);
    static void onErrorStatic(int portId, int errorCode, const char *errorMsg, void *userData);

    int mPortId;
    QString mPortName;
    qint32 mBaudRate;
    DataBits mDataBits;
    Parity mParity;
    StopBits mStopBits;
    FlowControl mFlowControl;
    SerialPortError mLastError;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(QSerialPort::Directions)

#endif // QSERIALPORT_WASM_H
