/*
    Copyright 2016 - 2020 Benjamin Vedder	benjamin@vedder.se

    This file is part of VESC Tool.

    VESC Tool is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    VESC Tool is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include <QDebug>
#include <QHostInfo>
#include <QNetworkDatagram>
#include <QFileInfo>
#include <QThread>
#include <cmath>
#include <QRegularExpression>
#include <QDateTime>
#include <QDir>
#include <cmath>
#include "lzokay/lzokay.hpp"
#include "vescinterface.h"
#include "vesctasks.h"
#include "utility.h"
#include "heatshrink/heatshrinkif.h"
#include "hexfile.h"
#include "webfsbackupbridge.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>

#ifdef HAS_SERIALPORT
#ifdef Q_OS_WASM
#include "qserialport_wasm.h"
#else
#include <QSerialPortInfo>
#endif
#endif

#include <QNetworkAccessManager>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>

#ifdef HAS_CANBUS
#include <QCanBus>
#endif

#if defined(__EMSCRIPTEN__) || defined(Q_OS_WASM)
#include "wasm_serial_bridge.h"
#endif

#ifndef VT_INTRO_VERSION
#define VT_INTRO_VERSION 1
#endif

VescInterface *VescInterface::sInstance = nullptr;

VescInterface::VescInterface(QObject *parent) : QObject(parent)
{
    sInstance = this;
#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    wasm_serial_bridge_init();
    webfs_init();
#endif
    mMcConfig = new ConfigParams(this);
    mAppConfig = new ConfigParams(this);
    mInfoConfig = new ConfigParams(this);
    mFwConfig = new ConfigParams(this);
    mCustomConfigsLoaded = false;
    mCustomConfigRxDone = false;
    mQmlHwLoaded = false;
    mQmlAppLoaded = false;
    m_qmlAsyncLoading = false;
    m_qmlFetchingHw = false;
    m_qmlTotalLen = -1;
    m_qmlRetries = 0;
    mPacket = new Packet(this);
    mCommands = new Commands(this);

    // Compatible firmwares
    mFwVersionReceived = false;
    mFwRetries = 0;
    mFwPollCnt = 0;
    mFwTxt = "x.x";
    mFwPair = qMakePair(-1, -1);
    mIsUploadingFw = false;
    mIsLastFwBootloader = false;
    mFwSupportsConfiguration = false;

    mCancelSwdUpload = false;
    mCancelFwUpload = false;
    mFwUploadStatus = "FW Upload Status";
    mFwUploadProgress = -1.0;
    mFwIsBootloader = false;

    mTimer = new QTimer(this);
    mTimer->setInterval(20);
    mTimer->start();

    m_qmlTimeoutTimer = new QTimer(this);
    m_qmlTimeoutTimer->setSingleShot(true);
    connect(m_qmlTimeoutTimer, &QTimer::timeout, this, &VescInterface::handleQmlUiTimeout);
    connect(mCommands, &Commands::qmluiHwRx, this, [this](int len, int ofs, QByteArray data) {
        handleQmlUiChunk(true, len, ofs, data);
    });
    connect(mCommands, &Commands::qmluiAppRx, this, [this](int len, int ofs, QByteArray data) {
        handleQmlUiChunk(false, len, ofs, data);
    });

    m_customConfigAsyncLoading = false;
    m_customConfigCurrentIdx = 0;
    m_customConfigTotalLen = -1;
    m_customConfigRetries = 0;
    m_customConfigTimeoutTimer = new QTimer(this);
    m_customConfigTimeoutTimer->setSingleShot(true);
    connect(m_customConfigTimeoutTimer, &QTimer::timeout, this, &VescInterface::handleCustomConfigTimeout);
    connect(mCommands, &Commands::customConfigChunkRx, this, &VescInterface::handleCustomConfigChunk);

    m_fwStep = FwUploadStep::Idle;
    m_fwIsOngoing = false;
    m_fwFwdCan = false;
    m_fwAutoDisconnect = false;
    m_fwSupportsLzo = false;
    m_fwIsLzo = true;
    m_fwBlAddr = 0;
    m_fwBlStartAddr = 0;
    m_fwBlTotalSize = 0;
    m_fwAppAddr = 0;
    m_fwAppStartAddr = 0;
    m_fwAppTotalSize = 0;
    m_fwRetries = 0;
    m_fwLzoFailures = 0;
    m_fwTimeoutTimer = new QTimer(this);
    m_fwTimeoutTimer->setSingleShot(true);
    connect(m_fwTimeoutTimer, &QTimer::timeout, this, &VescInterface::onFwTimeout);
    connect(mCommands, &Commands::eraseBootloaderResReceived, this, &VescInterface::onEraseBootloaderResReceived);
    connect(mCommands, &Commands::eraseNewAppResReceived, this, &VescInterface::onEraseNewAppResReceived);
    connect(mCommands, &Commands::writeNewAppDataResReceived, this, &VescInterface::onWriteNewAppDataResReceived);

    QTimer::singleShot(250, this, [this]() {
        reloadFirmwareResources();
    });

    mLastConnType = static_cast<conn_t>(mSettings.value("connection_type", CONN_NONE).toInt());
    mLastTcpServer = mSettings.value("tcp_server", "127.0.0.1").toString();
    mLastTcpPort = mSettings.value("tcp_port", 65102).toInt();
    mLastTcpHubServer = mSettings.value("tcp_hub_server", "veschub.vedder.se").toString();
    mLastTcpHubPort = mSettings.value("tcp_hub_port", 65101).toInt();
    mLastTcpHubVescID = mSettings.value("tcp_hub_vesc_id", "").toString();
    mLastTcpHubVescPass = mSettings.value("tcp_hub_vesc_pass", "").toString();
    mLastUdpServer = QHostAddress(mSettings.value("udp_server", "127.0.0.1").toString());
    mLastUdpPort = mSettings.value("udp_port", 65102).toInt();

    mSendCanBefore = false;
    mCanIdBefore = 0;
    mWasConnected = false;
    mAutoconnectOngoing = false;
    mAutoconnectProgress = 0.0;
    mIgnoreCanChange = false;

    mCanTmpFwdActive = false;
    mCanTmpFwdSendCanLast = false;
    mCanTmpFwdIdLast = -1;

    mIgnoreCustomConfigs = false;
    mIgnoreTestVersion = false;

    mFwSwapDone = false;
    mBlockFwSwap = false;

#ifdef Q_OS_ANDROID
    QJniObject activity = QJniObject::callStaticObjectMethod(
                "org/qtproject/qt/android/QtNative", "activity", "()Landroid/app/Activity;");

    if (activity.isValid()) {
        QJniObject serviceName = QJniObject::getStaticObjectField<jstring>(
                    "android/content/Context","POWER_SERVICE");
        if (serviceName.isValid()) {
            QJniObject powerMgr = activity.callObjectMethod(
                        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",serviceName.object<jobject>());
            if (powerMgr.isValid()) {
                jint levelAndFlags = QJniObject::getStaticField<jint>(
                            "android/os/PowerManager","PARTIAL_WAKE_LOCK");
                QJniObject tag = QJniObject::fromString( "VESC Tool" );
                mWakeLock = powerMgr.callObjectMethod("newWakeLock",
                                                       "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
                                                       levelAndFlags,tag.object<jstring>());
            }
        }
    }
#endif
    mWakeLockActive = false;

    // Serial
#ifdef HAS_SERIALPORT
    mSerialPort = new QSerialPort(this);
    mLastSerialPort = mSettings.value("serial_port", "").toString();
    mLastSerialBaud = mSettings.value("serial_baud", 115200).toInt();

    connect(mSerialPort, &QIODevice::readyRead, this, &VescInterface::serialDataAvailable);
    connect(mSerialPort, &QSerialPort::errorOccurred,
            this, &VescInterface::serialPortError);
#endif

    // CANbus
#ifdef HAS_CANBUS
    mCanDevice = nullptr;
    mLastCanDeviceInterface = mSettings.value("CANbusDeviceInterface", "can0").toString();
    mLastCanDeviceBitrate = mSettings.value("CANbusDeviceBitrate", 500000).toInt();
    mLastCanBackend = mSettings.value("CANbusBackend", "socketcan").toString();
    mLastCanDeviceID = mSettings.value("CANbusLastDeviceID", 0).toInt();
    mCANbusScanning = false;
#endif

#ifdef HAS_POS
    mPosSource = nullptr;
#endif

    // TCP
    mTcpSocket = new QTcpSocket(this);
    mTcpConnected = false;

    connect(mTcpSocket, &QIODevice::readyRead, this, &VescInterface::tcpInputDataAvailable);
    connect(mTcpSocket, &QAbstractSocket::connected, this, &VescInterface::tcpInputConnected);
    connect(mTcpSocket, &QAbstractSocket::disconnected, this, &VescInterface::tcpInputDisconnected);
    connect(mTcpSocket, &QAbstractSocket::errorOccurred,
            this, &VescInterface::tcpInputError);

    // UDP
    mUdpSocket = new QUdpSocket(this);
    mUdpConnected = false;
    connect(mUdpSocket, &QIODevice::readyRead, this, &VescInterface::udpInputDataAvailable);
    connect(mUdpSocket, &QAbstractSocket::errorOccurred,
            this, &VescInterface::udpInputError);

    // BLE
#ifdef HAS_BLUETOOTH
    mBleUart = new BleUart(this);
    mLastBleAddr = mSettings.value("ble_addr").toString();

    {
        int size = mSettings.beginReadArray("bleNames");
        for (int i = 0; i < size; ++i) {
            mSettings.setArrayIndex(i);
            QString address = mSettings.value("address").toString();
            QString name = mSettings.value("name").toString();
            mBleNames.insert(address, name);
        }
        mSettings.endArray();
    }

    {
        int size = mSettings.beginReadArray("blePreferred");
        for (int i = 0; i < size; ++i) {
            mSettings.setArrayIndex(i);
            QString address = mSettings.value("address").toString();
            bool pref = mSettings.value("preferred").toBool();
            mBlePreferred.insert(address, pref);
        }
        mSettings.endArray();
    }

    connect(mBleUart, &BleUart::dataRx, this, &VescInterface::bleDataRx);
    connect(mBleUart, &BleUart::connected, [this]{
        setLastConnectionType(CONN_BLE);
        mSettings.setValue("ble_addr", mLastBleAddr);
    });
    connect(mBleUart, &BleUart::unintentionalDisconnect, this, &VescInterface::bleUnintentionalDisconnect);
#else
    mBleUart = new BleUartDummy(this);
#endif

    mTcpServer = new TcpServerSimple(this);
    mTcpServer->setUsePacket(true);
    connect(mTcpServer->packet(), &Packet::packetReceived, [this](const QByteArray &packet) {
        mPacket->sendPacket(packet);
    });
    connect(mPacket, &Packet::packetReceived, [this](const QByteArray &packet) {
        mTcpServer->packet()->sendPacket(packet);
    });

    mTimerBroadcast = new QTimer(this);
    mTimerBroadcast->setInterval(1000);
    mTimerBroadcast->start();
    connect(mTimerBroadcast, &QTimer::timeout, [this]() {
        if (mTcpServer->isServerRunning() && isPortConnected()) {
            QUdpSocket *udp = new QUdpSocket(this);
            QString dgram = QString("%1::%2::%3").
                    arg(getLastFwRxParams().hw).
                    arg(Utility::getNetworkAddresses().first().toString()).
                    arg(mTcpServer->lastPort());
            udp->writeDatagram(dgram.toLocal8Bit().data(), dgram.size(), QHostAddress::Broadcast, 65109);
        }
    });

    mTimerConfigUpdate = new QTimer(this);
    mTimerConfigUpdate->setInterval(1000);
    mTimerConfigUpdate->start();
    connect(mTimerConfigUpdate, &QTimer::timeout, [this]() {
        if (isPortConnected()) {
            for (int i = 0;i < mCustomConfigs.size();i++) {
                if (mCustomConfigs.at(i)->updateCnt() == 0) {
                    mCommands->customConfigGet(i, false);
                }
            }
        }
    });

    mUdpServer = new UdpServerSimple(this);
    mUdpServer->setUsePacket(true);
    connect(mUdpServer->packet(), &Packet::packetReceived, [this](const QByteArray &packet) {
        mPacket->sendPacket(packet);
    });
    connect(mPacket, &Packet::packetReceived, [this](const QByteArray &packet) {
        mUdpServer->packet()->sendPacket(packet);
    });

    {
        int size = mSettings.beginReadArray("profiles");
        for (int i = 0; i < size; ++i) {
            mSettings.setArrayIndex(i);
            MCCONF_TEMP cfg;
            cfg.current_min_scale = mSettings.value("current_min_scale", 1.0).toDouble();
            cfg.current_max_scale = mSettings.value("current_max_scale", 1.0).toDouble();
            cfg.erpm_or_speed_min = mSettings.value("erpm_or_speed_min").toDouble();
            cfg.erpm_or_speed_max = mSettings.value("erpm_or_speed_max").toDouble();
            cfg.duty_min = mSettings.value("duty_min").toDouble();
            cfg.duty_max = mSettings.value("duty_max").toDouble();
            cfg.watt_min = mSettings.value("watt_min").toDouble();
            cfg.watt_max = mSettings.value("watt_max").toDouble();
            cfg.name = mSettings.value("name").toString();
            mProfiles.append(QVariant::fromValue(cfg));
        }
        mSettings.endArray();
    }

    {
        int size = mSettings.beginReadArray("tcpHubDevices");
        for (int i = 0; i < size; ++i) {
            mSettings.setArrayIndex(i);
            TCP_HUB_DEVICE dev;
            dev.server = mSettings.value("server").toString();
            dev.port = mSettings.value("port").toInt();
            dev.id = mSettings.value("id").toString();
            dev.password = mSettings.value("pw").toString();
            mTcpHubDevs.append(QVariant::fromValue(dev));
        }
        mSettings.endArray();
    }

    {
        int size = mSettings.beginReadArray("pairedUuids");
        for (int i = 0; i < size; ++i) {
            mSettings.setArrayIndex(i);
            QString uuid = mSettings.value("uuid").toString().toUpper();
            mPairedUuids.append(uuid.replace(" ", ""));
        }
        mSettings.endArray();
    }

    {
        int size = mSettings.beginReadArray("configurationBackups");
        for (int i = 0; i < size; ++i) {
            CONFIG_BACKUP cfg;
            mSettings.setArrayIndex(i);
            QString uuid = mSettings.value("uuid").toString();
            cfg.vesc_uuid = uuid;
            cfg.mcconf_xml_compressed = mSettings.value("mcconf").toString();
            cfg.appconf_xml_compressed = mSettings.value("appconf").toString();
            cfg.customconf_xml_compressed = mSettings.value("customconf").toString();
            cfg.name = mSettings.value("name", QString("")).toString();
            mConfigurationBackups.insert(uuid, cfg);
        }
        mSettings.endArray();
    }

    {
        int size = mSettings.beginReadArray("lastFwUuids");
        for (int i = 0; i < size; ++i) {
            mSettings.setArrayIndex(i);
            QString uuidLocal = mSettings.value("uuidLocal", "").toString().toUpper();
            QString uuidCan = mSettings.value("uuidCan", "").toString().toUpper();
            int canId = mSettings.value("canId", -1).toInt();

            if (!uuidLocal.isEmpty() && !uuidCan.isEmpty() && canId >= 0) {
                mLastFwUuids[uuidLocal] = qMakePair(uuidCan, canId);
            }
        }
        mSettings.endArray();
    }
    
    QLocale systemLocale;
    bool useImperialByDefault = systemLocale.measurementSystem() == QLocale::ImperialSystem;

    mUseImperialUnits = mSettings.value("useImperialUnits", useImperialByDefault).toBool();
    mKeepScreenOn = mSettings.value("keepScreenOn", true).toBool();
    mUseWakeLock = mSettings.value("useWakeLock", false).toBool();
    mLoadQmlUiOnConnect = mSettings.value("loadQmlUiOnConnect", true).toBool();
    mAllowScreenRotation = mSettings.value("allowScreenRotation", false).toBool();
    mSpeedGaugeUseNegativeValues =  mSettings.value("speedGaugeUseNegativeValues", true).toBool();
    mAskQmlLoad =  mSettings.value("askQmlLoad", true).toBool();

    mCommands->setAppConfig(mAppConfig);
    mCommands->setMcConfig(mMcConfig);

    // Other signals/slots
    connect(mTimer, &QTimer::timeout, this, &VescInterface::timerSlot);
    connect(mPacket, &Packet::dataToSend, this, &VescInterface::packetDataToSend);
    connect(mPacket, &Packet::packetReceived, this, &VescInterface::packetReceived);
    connect(mCommands, &Commands::dataToSend, this, &VescInterface::cmdDataToSend);
    connect(mCommands, &Commands::fwVersionReceived, this, &VescInterface::fwVersionReceived);
    connect(mCommands, &Commands::ackReceived, this, &VescInterface::ackReceived);
    connect(mMcConfig, &ConfigParams::updated, this, &VescInterface::mcconfUpdated);
    connect(mAppConfig, &ConfigParams::updated, this, &VescInterface::appconfUpdated);

    // Sanity-check motor parameters
    connect(mCommands, &Commands::mcConfigWriteSent, [this](bool checkSet) {
        if (!checkSet) {
            return;
        }

        QString notes = "";

        if ((mMcConfig->getParamDouble("l_current_max") * 1.3) > mMcConfig->getParamDouble("l_abs_current_max") ||
                (fabs(mMcConfig->getParamDouble("l_current_min")) * 1.3) > mMcConfig->getParamDouble("l_abs_current_max")) {
            notes += tr("<b>Current Checks</b><br>"
                        "The absolute maximum current is set close to the maximum motor current. This can cause "
                        "overcurrent faults and stop the motor when requesting high currents. Please check your configuration "
                        "and make sure that it is correct. It is recommended to set the absolute maximum current to around 1.5 "
                        "times the motor current (or braking current if it is higher).");
        }

        if (mMcConfig->getParamBool("l_slow_abs_current")) {
            if (!notes.isEmpty()) {
                notes.append("<br><br>");
            }

            notes += tr("<b>Limit Checks</b><br>"
                        "Slow ABS Current Limit is set. This should generally not be done as it greatly increases the "
                        "chance of permanently damaging the motor controller. Only change this setting if you know "
                        "exactly what you are doing! In general ABS overcurrent faults indicate that something is "
                        "wrong with the configuration or that the setup is pushed beyond reasonable limits.");
        }

        if (mMcConfig->hasParam("foc_overmod_factor") && mMcConfig->getParamDouble("foc_overmod_factor") >= 1.16) {
            notes += tr("<b>Overmodulation Check</b><br>"
                        "The FOC overmodulation factor is set to more than 1.15, which causes high distortion for minimal "
                        "gain. It is best to leave it at 1.15 or less, which is similar to BLDC commutation.");
        }

        if (!notes.isEmpty()) {
            emitMessageDialog(tr("Potential Configuration Issues"), notes, false, true);
        }
    });

    connect(mCommands, &Commands::valuesSetupReceived, [this](SETUP_VALUES v) {
        mLastSetupValues = v;
        mLastSetupTime = QDateTime::currentDateTimeUtc();
    });

    connect(mCommands, &Commands::valuesImuReceived, [this](IMU_VALUES v) {
        mLastImuValues = v;
        mLastImuTime = QDateTime::currentDateTimeUtc();
    });

    connect(mCommands, &Commands::valuesReceived, [this](MC_VALUES v) {
        if (mRtLogFile.isOpen()) {
            int msPos = -1;
            double lat = 0.0;
            double lon = 0.0;
            double alt = 0.0;
            double gVel = 0.0;
            double vVel = 0.0;
            double hAcc = 0.0;
            double vAcc = 0.0;

#ifdef HAS_POS
            if (mLastPos.isValid() && mLastPosTime.isValid() &&
                    mLastPosTime.secsTo(QDateTime::currentDateTime()) < 3) {
                msPos = mLastPos.timestamp().time().msecsSinceStartOfDay();
                lat = mLastPos.coordinate().latitude();
                lon = mLastPos.coordinate().longitude();

                if (!std::isnan(mLastPos.coordinate().altitude())) {
                    alt = mLastPos.coordinate().altitude();
                }

                if (mLastPos.hasAttribute(QGeoPositionInfo::GroundSpeed)) {
                    gVel = mLastPos.attribute(QGeoPositionInfo::GroundSpeed);
                }

                if (mLastPos.hasAttribute(QGeoPositionInfo::VerticalSpeed)) {
                    vVel = mLastPos.attribute(QGeoPositionInfo::VerticalSpeed);
                }

                if (mLastPos.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)) {
                    hAcc = mLastPos.attribute(QGeoPositionInfo::HorizontalAccuracy);
                }

                if (mLastPos.hasAttribute(QGeoPositionInfo::VerticalAccuracy)) {
                    vAcc = mLastPos.attribute(QGeoPositionInfo::VerticalAccuracy);
                }
            }
#endif

            auto t = QDateTime::currentDateTimeUtc().time();
            QTextStream os(&mRtLogFile);

            int msSetup = -1;
            if (mLastSetupTime.isValid()) {
                msSetup = mLastSetupTime.time().msecsSinceStartOfDay();
            }

            int msImu = -1;
            if (mLastImuTime.isValid()) {
                msImu = mLastImuTime.time().msecsSinceStartOfDay();
            }

            os << t.msecsSinceStartOfDay() << ";";
            os << v.v_in << ";";
            os << v.temp_mos << ";";
            os << v.temp_mos_1 << ";";
            os << v.temp_mos_2 << ";";
            os << v.temp_mos_3 << ";";
            os << v.temp_motor << ";";
            os << v.current_motor << ";";
            os << v.current_in << ";";
            os << v.id << ";";
            os << v.iq << ";";
            os << v.rpm << ";";
            os << v.duty_now << ";";
            os << v.amp_hours << ";";
            os << v.amp_hours_charged << ";";
            os << v.watt_hours << ";";
            os << v.watt_hours_charged << ";";
            os << v.tachometer << ";";
            os << v.tachometer_abs << ";";
            os << v.position << ";";
            os << v.fault_code << ";";
            os << v.vesc_id << ";";
            os << v.vd << ";";
            os << v.vq << ";";

            os << msSetup << ";";
            os << mLastSetupValues.amp_hours << ";";
            os << mLastSetupValues.amp_hours_charged << ";";
            os << mLastSetupValues.watt_hours << ";";
            os << mLastSetupValues.watt_hours_charged << ";";
            os << mLastSetupValues.battery_level << ";";
            os << mLastSetupValues.battery_wh << ";";
            os << mLastSetupValues.current_in << ";";
            os << mLastSetupValues.current_motor << ";";
            os << mLastSetupValues.speed << ";";
            os << mLastSetupValues.tachometer << ";";
            os << mLastSetupValues.tachometer_abs << ";";
            os << mLastSetupValues.num_vescs << ";";

            os << msImu << ";";
            os << mLastImuValues.roll << ";";
            os << mLastImuValues.pitch << ";";
            os << mLastImuValues.yaw << ";";
            os << mLastImuValues.accX << ";";
            os << mLastImuValues.accY << ";";
            os << mLastImuValues.accZ << ";";
            os << mLastImuValues.gyroX << ";";
            os << mLastImuValues.gyroY << ";";
            os << mLastImuValues.gyroZ << ";";

            os << msPos << ";";
            os << Qt::fixed << qSetRealNumberPrecision(8) << lat << ";";
            os << Qt::fixed << qSetRealNumberPrecision(8) << lon << ";";
            os << alt << ";";
            os << gVel << ";";
            os << vVel << ";";
            os << hAcc << ";";
            os << vAcc << ";";
            os << "\n";
            os.flush();

            LOG_DATA d;
            d.values = v;
            d.setupValues = mLastSetupValues;
            d.imuValues = mLastImuValues;
            d.valTime = t.msecsSinceStartOfDay();
            d.setupValTime = msSetup;
            d.imuValTime = msImu;
            d.posTime = msPos;
            d.lat = lat;
            d.lon = lon;
            d.alt = alt;
            d.gVel = gVel;
            d.vVel = vVel;
            d.hAcc = hAcc;
            d.vAcc = vAcc;
            mRtLogData.append(d);
        }
    });

    mDeserialFailedMessageShown = false;
    connect(mCommands, &Commands::deserializeConfigFailed, [this](bool isMc, bool isApp) {
        if (!mDeserialFailedMessageShown) {
            mDeserialFailedMessageShown = true;
            QString configName = "unknown";
            if (isMc) {
                configName = "motor";
            } else if (isApp) {
                configName = "app";
            }

#if VT_IS_TEST_VERSION
            emitMessageDialog("Deserializing " + configName + " configuration failed",
                              "Could not deserialize " + configName +
                              " configuration. You probably need to update the VESC firmware, as "
                              "a new iteration of the test version has been made.",
                              false, false);
#else
            emitMessageDialog("Deserializing " + configName + " configuration failed",
                              "Could not deserialize " + configName +
                              " configuration. This probably means "
                              "that something is wrong with your firmware, or this VESC Tool version.",
                              false, false);
#endif
        }
    });

    connect(mCommands, &Commands::customConfigRx, this, &VescInterface::customConfigRx);

    connect(mCommands, &Commands::customConfigAckReceived, [this](int confId) {
        ConfigParams *custConf = customConfig(confId);
        QString name;
        if (custConf) {
            name = custConf->getLongName("hw_name");
        } else {
            name = tr("Custom config %1").arg(confId);
        }
        emit ackReceived(tr("%1 write OK").arg(name));
    });

#if VT_IS_TEST_VERSION
    QTimer::singleShot(1000, [this]() {
        if (!mIgnoreTestVersion) {
            emitMessageDialog("VESC Tool Test Version",
                              "Warning: This is a test version of VESC Tool. The included firmwares are NOT compatible with "
                              "released firmwares and should only be used with this test version. When using a release version "
                              "of VESC Tool, the firmware must be upgraded even if the version number is the same.",
                              false);
        }
    });
#endif
}

VescInterface::~VescInterface()
{
    storeSettings();
    closeRtLogFile();

    if (mWakeLockActive) {
        setWakeLock(false);
    }

    Utility::stopGnssForegroundService();

    if (sInstance == this) {
        sInstance = nullptr;
    }
}

void VescInterface::processRawRx(const QByteArray &data)
{
    if (mPacket) {
        mPacket->processData(data);
    }
}

Commands *VescInterface::commands() const
{
    return mCommands;
}

ConfigParams *VescInterface::mcConfig()
{
    return mMcConfig;
}

ConfigParams *VescInterface::appConfig()
{
    return mAppConfig;
}

ConfigParams *VescInterface::infoConfig()
{
    return mInfoConfig;
}

ConfigParams *VescInterface::fwConfig()
{
    return mFwConfig;
}

QStringList VescInterface::getSupportedFirmwares()
{
    QList<QPair<int, int> > fwPairs = getSupportedFirmwarePairs();
    QStringList fws;

    for (int i = 0;i < fwPairs.size();i++) {
        fws.append(QString("%1.%2").arg(fwPairs.at(i).first).arg(fwPairs.at(i).second, 2, 10, QLatin1Char('0')));
    }
    return fws;
}

QList<QPair<int, int> > VescInterface::getSupportedFirmwarePairs()
{
    QList<QPair<int, int> > fws;

    ConfigParam *p = mInfoConfig->getParam("fw_version");

    if (p) {
        QStringList strs = p->enumNames;

        for (int i = 0;i < strs.size();i++) {
            QStringList mami = strs.at(i).split(".");
            QPair<int, int> fw = qMakePair(mami.at(0).toInt(), mami.at(1).toInt());
            fws.append(fw);
        }
    }

    return fws;
}

QString VescInterface::getFirmwareNow()
{
    return mFwTxt;
}

QPair<int, int> VescInterface::getFirmwareNowPair()
{
    return mFwPair;
}

void VescInterface::emitStatusMessage(const QString &msg, bool isGood)
{
    emit statusMessage(msg, isGood);
}

void VescInterface::emitMessageDialog(const QString &title, const QString &msg, bool isGood, bool richText)
{
    emit messageDialog(title, msg, isGood, richText);
}

bool VescInterface::fwRx()
{
    return mFwVersionReceived;
}

void VescInterface::storeSettings()
{
    mSettings.remove("bleNames");
    {
        mSettings.beginWriteArray("bleNames");
        QHashIterator<QString, QString> i(mBleNames);
        int ind = 0;
        while (i.hasNext()) {
            i.next();
            mSettings.setArrayIndex(ind);
            mSettings.setValue("address", i.key());
            mSettings.setValue("name", i.value());
            ind++;
        }
        mSettings.endArray();
    }

    mSettings.remove("blePreferred");
    {
        mSettings.beginWriteArray("blePreferred");
        QHashIterator<QString, bool> i(mBlePreferred);
        int ind = 0;
        while (i.hasNext()) {
            i.next();
            mSettings.setArrayIndex(ind);
            mSettings.setValue("address", i.key());
            mSettings.setValue("preferred", i.value());
            ind++;
        }
        mSettings.endArray();
    }

    mSettings.remove("profiles");
    mSettings.beginWriteArray("profiles");
    for (int i = 0; i < mProfiles.size(); ++i) {
        auto cfg = mProfiles.value(i).value<MCCONF_TEMP>();
        mSettings.setArrayIndex(i);
        mSettings.setValue("current_min_scale", cfg.current_min_scale);
        mSettings.setValue("current_max_scale", cfg.current_max_scale);
        mSettings.setValue("erpm_or_speed_min", cfg.erpm_or_speed_min);
        mSettings.setValue("erpm_or_speed_max", cfg.erpm_or_speed_max);
        mSettings.setValue("duty_min", cfg.duty_min);
        mSettings.setValue("duty_max", cfg.duty_max);
        mSettings.setValue("watt_min", cfg.watt_min);
        mSettings.setValue("watt_max", cfg.watt_max);
        mSettings.setValue("name", cfg.name);
    }
    mSettings.endArray();

    mSettings.remove("tcpHubDevices");
    mSettings.beginWriteArray("tcpHubDevices");
    for (int i = 0; i < mTcpHubDevs.size(); ++i) {
        auto dev = mTcpHubDevs.value(i).value<TCP_HUB_DEVICE>();
        mSettings.setArrayIndex(i);
        mSettings.setValue("server", dev.server);
        mSettings.setValue("port", dev.port);
        mSettings.setValue("id", dev.id);
        mSettings.setValue("pw", dev.password);
    }
    mSettings.endArray();

    mSettings.beginWriteArray("pairedUuids");
    for (int i = 0;i < mPairedUuids.size();i++) {
        mSettings.setArrayIndex(i);
        mSettings.setValue("uuid", mPairedUuids.at(i));
    }
    mSettings.endArray();

    mSettings.remove("configurationBackups");
    {
        mSettings.beginWriteArray("configurationBackups");
        QHashIterator<QString, CONFIG_BACKUP> i(mConfigurationBackups);
        int ind = 0;
        while (i.hasNext()) {
            i.next();
            mSettings.setArrayIndex(ind);
            mSettings.setValue("uuid", i.key());
            mSettings.setValue("mcconf", i.value().mcconf_xml_compressed);
            mSettings.setValue("appconf", i.value().appconf_xml_compressed);
            mSettings.setValue("customconf", i.value().customconf_xml_compressed);
            mSettings.setValue("name", i.value().name);
            ind++;
        }
        mSettings.endArray();
    }

    mSettings.remove("lastFwUuids");
    {
        mSettings.beginWriteArray("lastFwUuids");
        QMapIterator<QString, QPair<QString, int> > i(mLastFwUuids);
        int ind = 0;
        while (i.hasNext()) {
            i.next();
            mSettings.setArrayIndex(ind++);
            mSettings.setValue("uuidLocal", i.key());
            mSettings.setValue("uuidCan", i.value().first);
            mSettings.setValue("canId", i.value().second);
        }
        mSettings.endArray();
    }

    mSettings.setValue("useImperialUnits", mUseImperialUnits);
    mSettings.setValue("keepScreenOn", mKeepScreenOn);
    mSettings.setValue("useWakeLock", mUseWakeLock);
    mSettings.setValue("loadQmlUiOnConnect", mLoadQmlUiOnConnect);
    mSettings.setValue("darkMode", Utility::isDarkMode());
    mSettings.setValue("allowScreenRotation", mAllowScreenRotation);
    mSettings.setValue("speedGaugeUseNegativeValues", mSpeedGaugeUseNegativeValues);
    mSettings.setValue("askQmlLoad", mAskQmlLoad);
    mSettings.sync();
}

QVariantList VescInterface::getProfiles()
{
    return mProfiles;
}

void VescInterface::addProfile(QVariant profile)
{
    mProfiles.append(profile);
    emit profilesUpdated();
}

void VescInterface::clearProfiles()
{
    mProfiles.clear();
    emit profilesUpdated();
}

void VescInterface::deleteProfile(int index)
{
    if (index >= 0 && mProfiles.length() > index) {
        mProfiles.removeAt(index);
        emit profilesUpdated();
    }
}

void VescInterface::moveProfileUp(int index)
{
    if (index > 0 && index < mProfiles.size()) {
        mProfiles.swapItemsAt(index, index - 1);
        emit profilesUpdated();
    }
}

void VescInterface::moveProfileDown(int index)
{
    if (index >= 0 && index < (mProfiles.size() - 1)) {
        mProfiles.swapItemsAt(index, index + 1);
        emit profilesUpdated();
    }
}

MCCONF_TEMP VescInterface::getProfile(int index)
{
    MCCONF_TEMP conf = createMcconfTemp();

    if (index >= 0 && mProfiles.length() > index) {
        conf = mProfiles.value(index).value<MCCONF_TEMP>();
    }

    return conf;
}

void VescInterface::updateProfile(int index, QVariant profile)
{
    if (index >= 0 && mProfiles.length() > index) {
        mProfiles[index] = profile;
        emit profilesUpdated();
    }
}

bool VescInterface::isProfileInUse(int index)
{
    MCCONF_TEMP conf = getProfile(index);

    bool res = true;

    if (!Utility::almostEqual(conf.current_max_scale,
                              mMcConfig->getParamDouble("l_current_max_scale"), 0.0001)) {
        res = false;
    }

    if (!Utility::almostEqual(conf.current_min_scale,
                              mMcConfig->getParamDouble("l_current_min_scale"), 0.0001)) {
        res = false;
    }

    if (!Utility::almostEqual(conf.duty_max,
                              mMcConfig->getParamDouble("l_max_duty"), 0.0001)) {
        res = false;
    }

    if (!Utility::almostEqual(conf.duty_min,
                              mMcConfig->getParamDouble("l_min_duty"), 0.0001)) {
        res = false;
    }

    if (!Utility::almostEqual(conf.watt_max,
                              mMcConfig->getParamDouble("l_watt_max"), 0.0001)) {
        res = false;
    }

    if (!Utility::almostEqual(conf.watt_min,
                              mMcConfig->getParamDouble("l_watt_min"), 0.0001)) {
        res = false;
    }

    double speedFact = ((mMcConfig->getParamInt("si_motor_poles") / 2.0) * 60.0 *
            mMcConfig->getParamDouble("si_gear_ratio")) /
            (mMcConfig->getParamDouble("si_wheel_diameter") * M_PI);

    if (!Utility::almostEqual(conf.erpm_or_speed_max * speedFact,
                              mMcConfig->getParamDouble("l_max_erpm"), 0.0001)) {
        res = false;
    }

    if (!Utility::almostEqual(conf.erpm_or_speed_min * speedFact,
                              mMcConfig->getParamDouble("l_min_erpm"), 0.0001)) {
        res = false;
    }

    return res;
}

MCCONF_TEMP VescInterface::createMcconfTemp()
{
    MCCONF_TEMP conf;
    conf.name = "Unnamed Profile";
    conf.current_min_scale = 1.0;
    conf.current_max_scale = 1.0;
    conf.duty_min = 0.05;
    conf.duty_max = 0.95;
    conf.erpm_or_speed_min = -5.0;
    conf.erpm_or_speed_max = 5.0;
    conf.watt_min = -500.0;
    conf.watt_max = 500.0;
    return conf;
}

void VescInterface::updateMcconfFromProfile(MCCONF_TEMP profile)
{
    double speedFact = ((double(mMcConfig->getParamInt("si_motor_poles")) / 2.0) * 60.0 *
            mMcConfig->getParamDouble("si_gear_ratio")) /
            (mMcConfig->getParamDouble("si_wheel_diameter") * M_PI);

    mMcConfig->updateParamDouble("l_current_min_scale", profile.current_min_scale);
    mMcConfig->updateParamDouble("l_current_max_scale", profile.current_max_scale);
    mMcConfig->updateParamDouble("l_watt_min", profile.watt_min);
    mMcConfig->updateParamDouble("l_watt_max", profile.watt_max);
    mMcConfig->updateParamDouble("l_min_erpm", profile.erpm_or_speed_min * speedFact);
    mMcConfig->updateParamDouble("l_max_erpm", profile.erpm_or_speed_max * speedFact);
    mMcConfig->updateParamDouble("l_min_duty", profile.duty_min);
    mMcConfig->updateParamDouble("l_max_duty", profile.duty_max);
}

QStringList VescInterface::getPairedUuids()
{
    return mPairedUuids;
}

bool VescInterface::addPairedUuid(QString uuid)
{
    bool res = false;

    uuid = uuid.replace(" ", "").toUpper();

    QRegularExpression hexMatcher("^[0-9A-F]{24}$",
                                  QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = hexMatcher.match(uuid);
    if (!match.hasMatch()) {
        emitMessageDialog("Add VESC",
                          "The UUID must consist of 24 hexadecimal characters.",
                          false, false);
        return false;
    }

    if (hasPairedUuid(uuid)) {
        emitMessageDialog("Add VESC",
                          "This VESC already is in your paired UUID list.",
                          true, false);
    } else {
        mPairedUuids.append(uuid);
        emit pairingListUpdated();
        res = true;
    }

    return res;
}

bool VescInterface::deletePairedUuid(QString uuid)
{
    bool res = false;

    uuid = uuid.replace(" ", "").toUpper();

    for (int i = 0;i < mPairedUuids.size();i++) {
        QString str = mPairedUuids.at(i);
        if (str.replace(" ", "").toUpper() == uuid) {
            mPairedUuids.removeAt(i);
            emit pairingListUpdated();
            res = true;
            break;
        }
    }

    return res;
}

void VescInterface::clearPairedUuids()
{
    mPairedUuids.clear();
    emit pairingListUpdated();
}

bool VescInterface::hasPairedUuid(QString uuid)
{
    bool res = false;

    uuid = uuid.replace(" ", "").toUpper();

    for (int i = 0;i < mPairedUuids.size();i++) {
        QString str = mPairedUuids.at(i);
        if (str.replace(" ", "").toUpper() == uuid) {
            res = true;
            break;
        }
    }

    return res;
}

QString VescInterface::getConnectedUuid()
{
    QString res;

    if (isPortConnected()) {
        res = mUuidStr;
    }

    return res;
}

bool VescInterface::isIntroDone()
{
    if (mSettings.contains("introVersion")) {
        if (mSettings.value("introVersion").toInt() != VT_INTRO_VERSION) {
            mSettings.setValue("intro_done", false);
        }
    } else {
        mSettings.setValue("intro_done", false);
    }

    return mSettings.value("intro_done", false).toBool();
}

void VescInterface::setIntroDone(bool done)
{
    mSettings.setValue("introVersion", VT_INTRO_VERSION);
    mSettings.setValue("intro_done", done);
}

QString VescInterface::getLastTcpServer() const
{
    return mLastTcpServer;
}

int VescInterface::getLastTcpPort() const
{
    return mLastTcpPort;
}

QString VescInterface::getLastUdpServer() const
{
    return mLastUdpServer.toString();
}

int VescInterface::getLastUdpPort() const
{
    return mLastUdpPort;
}

bool VescInterface::swdEraseFlash()
{
    int erRes = -10;

    mCommands->bmEraseFlashAll();
    emit fwUploadStatus("Erasing flash...", 0.0, true);

    runTree(Group{SignalWaitTaskItem([this, &erRes](SignalWaitTask &task) {
        task.setTimeout(20000);
        task.connectSignal(mCommands, &Commands::bmEraseFlashAllRes,
                           [&erRes](int r) { erRes = r; });
        return SetupResult::Continue;
    })});

    if (erRes != 1) {
        QString msg = "Unknown failure";

        if (erRes == -10) {
            msg = "Erase timed out";
        } else if (erRes == -3) {
            msg = "Erase failed";
        } else if (erRes == -2) {
            msg = "Could not recognize target";
        } else if (erRes == -1) {
            msg = "Not connected to target";
        }

        emitMessageDialog("SWD Upload", msg, false, false);
        emit fwUploadStatus(msg, 0.0, false);

        return false;
    }

    emit fwUploadStatus("Erase done", 0.0, false);

    return true;
}

bool VescInterface::swdUploadFw(QByteArray newFirmware, uint32_t startAddr,
                                bool verify, bool isLzo)
{
    bool supportsLzo = mCommands->getLimitedCompatibilityCommands().
            contains(int(COMM_BM_WRITE_FLASH_LZO));

    if (mLastFwParams.hwType != HW_TYPE_VESC) {
        supportsLzo = false;
    }

    auto waitBmWriteRes = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(3000);
            task.connectSignal(mCommands, &Commands::bmWriteFlashRes,
                               [&res](int wrRes) { res = wrRes; });
            return SetupResult::Continue;
        })});
        return res;
    };

    auto writeChunk = [this, &waitBmWriteRes, &verify]
            (uint32_t addr, QByteArray chunk, QByteArray chunkLzo) {
        for (int i = 0;i < 3;i++) {
            if (chunkLzo.isEmpty()) {
                mCommands->bmWriteFlash(addr, chunk);
            } else {
                mCommands->bmWriteFlashLzo(addr, quint16(chunk.size()), chunkLzo);
            }

            int res = waitBmWriteRes();

            if (verify && (!mCommands->isLimitedMode() ||
                           mCommands->getLimitedCompatibilityCommands().
                           contains(int(COMM_BM_MEM_READ)))) {
                QByteArray rdData = mCommands->bmReadMemWait(addr, quint16(chunk.size()));

                if (rdData.size() != chunk.size()) {
                    return -11;
                }

                if (rdData != chunk) {
                    return -12;
                }
            }

            if (res != -10) {
                return res;
            }
        }

        return -20;
    };

    mCancelSwdUpload = false;
    int addr = int(startAddr);
    int szTot = newFirmware.size();
    int uploadSize = 2;
    int compChunks = 0;
    int nonCompChunks = 0;

    while (newFirmware.size() > 0) {
        const int chunkSize = 400;

        int sz = newFirmware.size() > chunkSize ? chunkSize : newFirmware.size();

        QByteArray in = newFirmware.mid(0, sz);
        std::size_t outMaxSize = chunkSize + chunkSize / 16 + 64 + 3;
        unsigned char out[1000];
        std::size_t out_len = sz;

        if (supportsLzo && isLzo) {
            lzokay::EResult error = lzokay::compress((const uint8_t*)in.constData(), sz, out, outMaxSize, out_len);
            if (error < lzokay::EResult::Success) {
                qWarning() << "LZO Compress Error" << int(error);
                isLzo = false;
            }
        }

        int res = 1;
        if (supportsLzo && isLzo && (out_len + 2) < uint32_t(sz)) {
            compChunks++;
            uploadSize += out_len + 2;
            res = writeChunk(uint32_t(addr), in, QByteArray((const char*)out, int(out_len)));
        } else {
            nonCompChunks++;
            uploadSize += sz;
            res = writeChunk(uint32_t(addr), in, QByteArray());
        }

        newFirmware.remove(0, sz);
        addr += sz;

        if (res == 1) {
            emit fwUploadStatus("Uploading firmware over SWD", double(addr - startAddr) / double(szTot), true);
        } else {
            QString msg = "Unknown failure";

            if (res == -20) {
                msg = "Timed out";
            } else if (res == -2) {
                msg = "Write failed";
            } else if (res == -1) {
                msg = "Not connected to target";
            } else if (res == -11) {
                msg = "Verification failed (-11)";
            } else if (res == -12) {
                msg = "Verification failed (-12)";
            }

            emitMessageDialog("SWD Upload", msg, false, false);
            emit fwUploadStatus(msg, 0.0, false);

            return false;
        }

        if (mCancelSwdUpload) {
            emit fwUploadStatus("Upload cancelled", 0.0, false);
            return false;
        }
    }

    if (supportsLzo && isLzo) {
        qDebug() << "Uploaded:" << uploadSize << "Initial Size:" << szTot << "Compression Ratio:"
                 << double(uploadSize) / double(szTot) << "Compressed chunks:" << compChunks
                 << "Incompressible chunks:" << nonCompChunks;
    }

    emit fwUploadStatus("Upload done", 1.0, false);

    return true;
}

void VescInterface::swdCancel()
{
    mCancelSwdUpload = true;
}

bool VescInterface::swdReboot()
{
    auto waitBmReboot = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(3000);
            task.connectSignal(mCommands, &Commands::bmRebootRes,
                               [&res](int wrRes) { res = wrRes; });
            return SetupResult::Continue;
        })});
        return res;
    };

    mCommands->bmReboot();
    int res = waitBmReboot();
    if (res == -10) {
        QString msg = "Reboot: Unknown failure";

        if (res == -10) {
            msg = "Reboot: Timed out";
        } else if (res == -1) {
            msg = "Reboot: Not connected to target";
        } else if (res == -2) {
            msg = "Reboot: Flash done failed";
        }

        emitMessageDialog("SWD Upload", msg, false, false);
        emit fwUploadStatus(msg, 1.0, false);

        return false;
    }

    return true;
}

bool VescInterface::swdUploadFromFile(QString path, uint32_t startAddr, bool verify)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emitMessageDialog("SWD Upload", "Could not open file: " + path, false, false);
        return false;
    }
    QByteArray data = file.readAll();
    file.close();
    return swdUploadFw(data, startAddr, verify);
}

bool VescInterface::fwEraseNewApp(bool fwdCan, quint32 fwSize)
{
    auto waitEraseRes = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(20000);
            task.connectSignal(mCommands, &Commands::eraseNewAppResReceived,
                               [&res](bool erRes) { res = erRes ? 1 : -1; });
            return SetupResult::Continue;
        })});
        return res;
    };

    mCommands->eraseNewApp(fwdCan, fwSize, mLastFwParams.hwType, mLastFwParams.hw);
    emit fwUploadStatus("Erasing buffer", 0.0, true);
    int erRes = waitEraseRes();
    if (erRes != 1) {
        QString msg = QString("Unknown failure: %1").arg(erRes);

        if (erRes == -10) {
            msg = "Erase timed out";
        } else if (erRes == -1) {
            msg = "Erasing buffer failed";
        }

        emitMessageDialog("Firmware Upload", msg, false, false);
        emit fwUploadStatus(msg, 0.0, false);

        return false;
    }

    emit fwUploadStatus("Erase done", 0.0, false);

    return true;
}

bool VescInterface::fwEraseBootloader(bool fwdCan)
{
    auto waitEraseRes = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(20000);
            task.connectSignal(mCommands, &Commands::eraseBootloaderResReceived,
                               [&res](bool erRes) { res = erRes ? 1 : -1; });
            return SetupResult::Continue;
        })});
        return res;
    };

    mCommands->eraseBootloader(fwdCan, mLastFwParams.hwType, mLastFwParams.hw);
    emit fwUploadStatus("Erasing bootloader...", 0.0, true);
    int erRes = waitEraseRes();
    if (erRes != 1) {
        QString msg = "Unknown failure";

        if (erRes == -10) {
            msg = "Erase timed out";
        } else if (erRes == -1) {
            msg = "Erasing bootloader failed";
        }

        emitMessageDialog("Firmware Upload", msg, false, false);
        emit fwUploadStatus(msg, 0.0, false);

        return false;
    }

    emit fwUploadStatus("Erase done", 0.0, false);

    return true;
}

bool VescInterface::fwUpload(QByteArray &newFirmware, bool isBootloader, bool fwdCan, bool isLzo, bool autoDisconnect)
{
    if (isBootloader) {
        QByteArray empty;
        return fwUploadAsync(empty, newFirmware, fwdCan, isLzo, false);
    } else {
        QByteArray empty;
        return fwUploadAsync(newFirmware, empty, fwdCan, isLzo, autoDisconnect);
    }
}

bool VescInterface::fwUploadAsync(QByteArray appFw, QByteArray blFw, bool fwdCan, bool isLzo, bool autoDisconnect)
{
    if (!isPortConnected()) {
        emitMessageDialog("Firmware Upload", "Not connected to device.", false, false);
        return false;
    }

    if (m_fwIsOngoing) {
        emitMessageDialog("Firmware Upload", "Firmware upload is already in progress.", false, false);
        return false;
    }

    if (appFw.isEmpty() && blFw.isEmpty()) {
        emitMessageDialog("Firmware Upload", "No firmware data to upload.", false, false);
        return false;
    }

    if (fwdCan && mCommands->getSendCan()) {
        emitMessageDialog("Firmware Upload",
                          "CAN forwarding must be disabled when uploading firmware to "
                          "all VESCs at the same time.", false, false);
        mFwUploadStatus = "CAN check failed";
        mFwUploadProgress = -1.0;
        emit fwUploadStatus(mFwUploadStatus, mFwUploadProgress, false);
        return false;
    }

    m_fwBlData.clear();
    m_fwBlAddr = 0;
    m_fwBlStartAddr = 0;
    m_fwBlTotalSize = 0;

    if (!blFw.isEmpty()) {
        m_fwBlData = Utility::removeFirmwareHeader(blFw);
        int addr = 0;
        switch (mLastFwParams.hwType) {
        case HW_TYPE_VESC:
            addr += (1024 * 128 * 3);
            break;
        case HW_TYPE_VESC_BMS:
            addr += 0x0803E000 - 0x08020000;
            break;
        case HW_TYPE_CUSTOM_MODULE:
            if (mLastFwParams.hw == "hm1") {
                addr += 0x0803E000 - 0x08020000;
            } else {
                addr += 0x0801E000 - 0x08010000;
            }
            break;
        }
        m_fwBlAddr = addr;
        m_fwBlStartAddr = addr;
        m_fwBlTotalSize = m_fwBlData.size();
    }

    m_fwAppData.clear();
    m_fwAppAddr = 0;
    m_fwAppStartAddr = 0;
    m_fwAppTotalSize = 0;
    m_fwSupportsLzo = false;

    if (!appFw.isEmpty()) {
        m_fwAppData = Utility::removeFirmwareHeader(appFw);
        int szTot = m_fwAppData.size();

        m_fwSupportsLzo = mCommands->getLimitedCompatibilityCommands().contains(int(COMM_WRITE_NEW_APP_DATA_LZO));
        if (mLastFwParams.hwType != HW_TYPE_VESC) {
            m_fwSupportsLzo = false;
        }

        bool useHeatshrink = false;
        if (szTot > 393208 && szTot < 700000) {
            useHeatshrink = true;
            qDebug() << "Firmware is big, using heatshrink compression library";
            int szOld = szTot;
            HeatshrinkIf hs;
            m_fwAppData = hs.encode(m_fwAppData);
            szTot = m_fwAppData.size();
            qDebug() << "New size:" << szTot << "(" << 100.0 * (double)szTot / (double)szOld << "%)";
            m_fwSupportsLzo = false;

            if (szTot > 393208) {
                emitMessageDialog(tr("Firmware too big"),
                                  tr("The firmware you are trying to upload is too large for the "
                                     "bootloader even after compression."), false);
                return false;
            }
        }

        if (szTot > 5000000) {
            emitMessageDialog(tr("Firmware too big"),
                              tr("The firmware you are trying to upload is unreasonably "
                                 "large, most likely it is an invalid file"), false);
            return false;
        }

        quint16 crc = Packet::crc16((const unsigned char*)m_fwAppData.constData(),
                                    uint32_t(m_fwAppData.size()));
        VByteArray sizeCrc;
        if (useHeatshrink) {
            uint32_t szShift = 0xCC;
            szShift <<= 24;
            szShift |= szTot;
            sizeCrc.vbAppendUint32(szShift);
        } else {
            sizeCrc.vbAppendUint32(szTot);
        }
        sizeCrc.vbAppendUint16(crc);
        m_fwAppData.prepend(sizeCrc);

        m_fwAppAddr = 0;
        m_fwAppStartAddr = 0;
        m_fwAppTotalSize = m_fwAppData.size();
    }

    mCancelFwUpload = false;
    m_fwIsOngoing = true;
    m_fwFwdCan = fwdCan;
    m_fwAutoDisconnect = autoDisconnect;
    m_fwIsLzo = isLzo;
    m_fwRetries = 0;
    m_fwLzoFailures = 0;
    mFwUploadProgress = 0.0;

    if (!m_fwBlData.isEmpty()) {
        if (mCommands->getLimitedSupportsEraseBootloader()) {
            m_fwStep = FwUploadStep::ErasingBootloader;
            mFwUploadStatus = "Erasing bootloader...";
            emit fwUploadStatus(mFwUploadStatus, 0.0, true);
            mCommands->eraseBootloader(m_fwFwdCan, mLastFwParams.hwType, mLastFwParams.hw);
            m_fwTimeoutTimer->start(15000);
        } else {
            m_fwStep = FwUploadStep::UploadingBootloader;
            sendNextBootloaderChunk();
        }
    } else if (!m_fwAppData.isEmpty()) {
        m_fwStep = FwUploadStep::ErasingApp;
        mFwUploadStatus = "Erasing buffer";
        emit fwUploadStatus(mFwUploadStatus, 0.0, true);
        mCommands->eraseNewApp(m_fwFwdCan, quint32(m_fwAppData.size()), mLastFwParams.hwType, mLastFwParams.hw);
        m_fwTimeoutTimer->start(20000);
    } else {
        m_fwIsOngoing = false;
        return false;
    }

    return true;
}

void VescInterface::sendNextBootloaderChunk()
{
    if (mCancelFwUpload) {
        finishFwUpload(false, "Upload cancelled");
        return;
    }

    if (m_fwBlData.isEmpty()) {
        if (!m_fwAppData.isEmpty()) {
            m_fwStep = FwUploadStep::ErasingApp;
            m_fwRetries = 0;
            mFwUploadStatus = "Erasing buffer";
            emit fwUploadStatus(mFwUploadStatus, 0.0, true);
            mCommands->eraseNewApp(m_fwFwdCan, quint32(m_fwAppData.size()), mLastFwParams.hwType, mLastFwParams.hw);
            m_fwTimeoutTimer->start(20000);
        } else {
            finishFwUpload(true, "Bootloader upload done");
        }
        return;
    }

    const int chunkSize = 384;
    int sz = std::min(chunkSize, m_fwBlData.size());
    QByteArray chunk = m_fwBlData.mid(0, sz);

    mCommands->writeNewAppData(chunk, uint32_t(m_fwBlAddr), m_fwFwdCan, mLastFwParams.hwType, mLastFwParams.hw);
    m_fwTimeoutTimer->start(3000);

    if (m_fwBlTotalSize > 0) {
        double prog = double(m_fwBlAddr - m_fwBlStartAddr) / double(m_fwBlTotalSize);
        if (!m_fwAppData.isEmpty()) {
            mFwUploadProgress = prog * 0.2;
        } else {
            mFwUploadProgress = prog;
        }
    }
    mFwUploadStatus = "Uploading Bootloader";
    emit fwUploadStatus(mFwUploadStatus, mFwUploadProgress, true);
}

void VescInterface::sendNextAppChunk()
{
    if (mCancelFwUpload) {
        finishFwUpload(false, "Upload cancelled");
        return;
    }

    if (m_fwAppData.isEmpty()) {
        finalizeFwUpload();
        return;
    }

    const int chunkSize = 384;
    int sz = std::min(chunkSize, m_fwAppData.size());
    QByteArray chunk = m_fwAppData.mid(0, sz);

    bool hasData = false;
    for (char b : chunk) {
        if (b != (char)0xff) {
            hasData = true;
            break;
        }
    }

    if (!hasData) {
        m_fwAppAddr += sz;
        m_fwAppData.remove(0, sz);
        if (m_fwAppTotalSize > 0) {
            double prog = double(m_fwAppAddr - m_fwAppStartAddr) / double(m_fwAppTotalSize);
            if (m_fwBlTotalSize > 0) {
                mFwUploadProgress = 0.2 + prog * 0.8;
            } else {
                mFwUploadProgress = prog;
            }
        }
        QTimer::singleShot(0, this, &VescInterface::sendNextAppChunk);
        return;
    }

    bool sentLzo = false;
    if (m_fwIsLzo && m_fwSupportsLzo) {
        std::size_t outMaxSize = chunkSize + chunkSize / 16 + 64 + 3;
        unsigned char out[1000];
        std::size_t out_len = sz;
        lzokay::EResult error = lzokay::compress((const uint8_t*)chunk.constData(), sz, out, outMaxSize, out_len);
        if (error >= lzokay::EResult::Success && (out_len + 2) < uint32_t(sz)) {
            sentLzo = true;
            mCommands->writeNewAppDataLzo(QByteArray((const char*)out, int(out_len)), uint32_t(m_fwAppAddr), uint16_t(sz), m_fwFwdCan);
        }
    }

    if (!sentLzo) {
        mCommands->writeNewAppData(chunk, uint32_t(m_fwAppAddr), m_fwFwdCan, mLastFwParams.hwType, mLastFwParams.hw);
    }

    m_fwTimeoutTimer->start(3000);

    if (m_fwAppTotalSize > 0) {
        double prog = double(m_fwAppAddr - m_fwAppStartAddr) / double(m_fwAppTotalSize);
        if (m_fwBlTotalSize > 0) {
            mFwUploadProgress = 0.2 + prog * 0.8;
        } else {
            mFwUploadProgress = prog;
        }
    }
    mFwUploadStatus = "Uploading Firmware";
    emit fwUploadStatus(mFwUploadStatus, mFwUploadProgress, true);
}

void VescInterface::onEraseBootloaderResReceived(bool ok)
{
    if (!m_fwIsOngoing || m_fwStep != FwUploadStep::ErasingBootloader) {
        return;
    }
    m_fwTimeoutTimer->stop();

    if (!ok) {
        finishFwUpload(false, "Erasing bootloader failed");
        return;
    }

    emit fwUploadStatus("Erase done", 0.0, false);
    m_fwStep = FwUploadStep::UploadingBootloader;
    m_fwRetries = 0;
    sendNextBootloaderChunk();
}

void VescInterface::onEraseNewAppResReceived(bool ok)
{
    if (!m_fwIsOngoing || m_fwStep != FwUploadStep::ErasingApp) {
        return;
    }
    m_fwTimeoutTimer->stop();

    if (!ok) {
        finishFwUpload(false, "Erasing buffer failed");
        return;
    }

    emit fwUploadStatus("Erase done", 0.0, false);
    m_fwStep = FwUploadStep::UploadingApp;
    m_fwRetries = 0;
    sendNextAppChunk();
}

void VescInterface::onWriteNewAppDataResReceived(bool ok, bool hasOffset, quint32 offset)
{
    Q_UNUSED(hasOffset);
    Q_UNUSED(offset);

    if (!m_fwIsOngoing) {
        return;
    }

    if (m_fwStep == FwUploadStep::UploadingBootloader) {
        m_fwTimeoutTimer->stop();
        if (ok) {
            const int chunkSize = 384;
            int sz = std::min(chunkSize, m_fwBlData.size());
            m_fwBlAddr += sz;
            m_fwBlData.remove(0, sz);
            m_fwRetries = 0;
            sendNextBootloaderChunk();
        } else {
            if (m_fwRetries < 5) {
                m_fwRetries++;
                qWarning() << "[FW_UPLOAD] Bootloader write retry" << m_fwRetries << "at addr" << m_fwBlAddr;
                sendNextBootloaderChunk();
            } else {
                finishFwUpload(false, "Write bootloader failed");
            }
        }
    } else if (m_fwStep == FwUploadStep::UploadingApp) {
        m_fwTimeoutTimer->stop();
        if (ok) {
            const int chunkSize = 384;
            int sz = std::min(chunkSize, m_fwAppData.size());
            m_fwAppAddr += sz;
            m_fwAppData.remove(0, sz);
            m_fwRetries = 0;
            m_fwLzoFailures = 0;
            sendNextAppChunk();
        } else {
            if (m_fwSupportsLzo) {
                m_fwLzoFailures++;
                if (m_fwLzoFailures > 3) {
                    qWarning() << "[FW_UPLOAD] LZO failing, disabling LZO for remaining upload";
                    m_fwSupportsLzo = false;
                }
            }
            if (m_fwRetries < 5) {
                m_fwRetries++;
                qWarning() << "[FW_UPLOAD] App write retry" << m_fwRetries << "at addr" << m_fwAppAddr;
                sendNextAppChunk();
            } else {
                finishFwUpload(false, "Write firmware failed");
            }
        }
    }
}

void VescInterface::finalizeFwUpload()
{
    if (!m_fwIsOngoing) {
        return;
    }

    m_fwStep = FwUploadStep::Finalizing;
    mFwUploadProgress = 1.0;
    mFwUploadStatus = "Upload done";
    emit fwUploadStatus(mFwUploadStatus, 1.0, false);

    mCommands->jumpToBootloader(m_fwFwdCan, mLastFwParams.hwType, mLastFwParams.hw);

    QTimer::singleShot(500, this, [this]() {
        if (m_fwAutoDisconnect) {
            disconnectPort();
        }
        finishFwUpload(true, "Firmware upload completed successfully!");
    });
}

void VescInterface::finishFwUpload(bool success, const QString &message)
{
    m_fwTimeoutTimer->stop();
    m_fwIsOngoing = false;
    m_fwStep = FwUploadStep::Idle;
    m_fwAppData.clear();
    m_fwBlData.clear();
    mFwUploadProgress = success ? 1.0 : -1.0;
    mFwUploadStatus = message;

    emit fwUploadStatus(message, mFwUploadProgress, false);
    if (!success) {
        emitMessageDialog("Firmware Upload", message, false, false);
    }
    emit fwUploadFinished(success, message);
}

void VescInterface::onFwTimeout()
{
    if (!m_fwIsOngoing) {
        return;
    }

    qWarning() << "[FW_UPLOAD] Timeout during step" << (int)m_fwStep << "retry" << m_fwRetries;

    if (m_fwStep == FwUploadStep::ErasingBootloader) {
        if (m_fwRetries < 2) {
            m_fwRetries++;
            mCommands->eraseBootloader(m_fwFwdCan, mLastFwParams.hwType, mLastFwParams.hw);
            m_fwTimeoutTimer->start(15000);
            return;
        }
        finishFwUpload(false, "Erasing bootloader timed out");
    } else if (m_fwStep == FwUploadStep::ErasingApp) {
        if (m_fwRetries < 2) {
            m_fwRetries++;
            mCommands->eraseNewApp(m_fwFwdCan, quint32(m_fwAppData.size()), mLastFwParams.hwType, mLastFwParams.hw);
            m_fwTimeoutTimer->start(20000);
            return;
        }
        finishFwUpload(false, "Erasing buffer timed out");
    } else if (m_fwStep == FwUploadStep::UploadingBootloader) {
        if (m_fwRetries < 5) {
            m_fwRetries++;
            sendNextBootloaderChunk();
            return;
        }
        finishFwUpload(false, "Bootloader upload timed out");
    } else if (m_fwStep == FwUploadStep::UploadingApp) {
        if (m_fwRetries < 5) {
            m_fwRetries++;
            sendNextAppChunk();
            return;
        }
        finishFwUpload(false, "Firmware upload timed out");
    }
}

void VescInterface::fwUploadCancel()
{
    mCancelFwUpload = true;
    if (m_fwTimeoutTimer) {
        m_fwTimeoutTimer->stop();
    }
    if (m_fwIsOngoing) {
        finishFwUpload(false, "Upload cancelled");
    }
}

double VescInterface::getFwUploadProgress()
{
    return mFwUploadProgress;
}

QString VescInterface::getFwUploadStatus()
{
    return mFwUploadStatus;
}

bool VescInterface::isCurrentFwBootloader()
{
    return mIsLastFwBootloader;
}

bool VescInterface::openRtLogFile(QString outDirectory)
{
    if (outDirectory.startsWith("file:/")) {
        outDirectory.remove(0, 6);
    }

    if (!QDir(outDirectory).exists()) {
        QDir().mkpath(outDirectory);
    }

    if (!QDir(outDirectory).exists()) {
        emitMessageDialog("Log to file",
                          "Output directory does not exist",
                          false, false);
        return false;
    }

    QDateTime d = QDateTime::currentDateTime();
    mRtLogFile.setFileName(QString("%1/%2-%3-%4_%5-%6-%7.csv").
                           arg(outDirectory).
                           arg(d.date().year(), 2, 10, QChar('0')).
                           arg(d.date().month(), 2, 10, QChar('0')).
                           arg(d.date().day(), 2, 10, QChar('0')).
                           arg(d.time().hour(), 2, 10, QChar('0')).
                           arg(d.time().minute(), 2, 10, QChar('0')).
                           arg(d.time().second(), 2, 10, QChar('0')));

    bool res = mRtLogFile.open(QIODevice::WriteOnly | QIODevice::Text);

    if (mRtLogFile.isOpen()) {
        QTextStream os(&mRtLogFile);
        os << "ms_today" << ";";
        os << "input_voltage" << ";";
        os << "temp_mos_max" << ";";
        os << "temp_mos_1" << ";";
        os << "temp_mos_2" << ";";
        os << "temp_mos_3" << ";";
        os << "temp_motor" << ";";
        os << "current_motor" << ";";
        os << "current_in" << ";";
        os << "d_axis_current" << ";";
        os << "q_axis_current" << ";";
        os << "erpm" << ";";
        os << "duty_cycle" << ";";
        os << "amp_hours_used" << ";";
        os << "amp_hours_charged" << ";";
        os << "watt_hours_used" << ";";
        os << "watt_hours_charged" << ";";
        os << "tachometer" << ";";
        os << "tachometer_abs" << ";";
        os << "encoder_position" << ";";
        os << "fault_code" << ";";
        os << "vesc_id" << ";";
        os << "d_axis_voltage" << ";";
        os << "q_axis_voltage" << ";";

        os << "ms_today_setup" << ";";
        os << "amp_hours_setup" << ";";
        os << "amp_hours_charged_setup" << ";";
        os << "watt_hours_setup" << ";";
        os << "watt_hours_charged_setup" << ";";
        os << "battery_level" << ";";
        os << "battery_wh_tot" << ";";
        os << "current_in_setup" << ";";
        os << "current_motor_setup" << ";";
        os << "speed_meters_per_sec" << ";";
        os << "tacho_meters" << ";";
        os << "tacho_abs_meters" << ";";
        os << "num_vescs" << ";";

        os << "ms_today_imu" << ";";
        os << "roll" << ";";
        os << "pitch" << ";";
        os << "yaw" << ";";
        os << "accX" << ";";
        os << "accY" << ";";
        os << "accZ" << ";";
        os << "gyroX" << ";";
        os << "gyroY" << ";";
        os << "gyroZ" << ";";

        os << "gnss_posTime" << ";";
        os << "gnss_lat" << ";";
        os << "gnss_lon" << ";";
        os << "gnss_alt" << ";";
        os << "gnss_gVel" << ";";
        os << "gnss_vVel" << ";";
        os << "gnss_hAcc" << ";";
        os << "gnss_vAcc" << ";";
        os << "\n";
        os.flush();
    }

    if (!res) {
        emitMessageDialog("Log to file",
                          "Could not open file for writing.",
                          false, false);
    }

    mRtLogData.clear();

    if (res) {
#ifdef HAS_POS
        if (mPosSource != nullptr) {
            mPosSource->deleteLater();
        }

        mPosSource = QGeoPositionInfoSource::createDefaultSource(this);

        if (mPosSource) {
            connect(mPosSource, &QGeoPositionInfoSource::positionUpdated,
                    [this](QGeoPositionInfo info) {
                mLastPos = info;
                mLastPosTime = QDateTime::currentDateTimeUtc();
            });
            mPosSource->setUpdateInterval(5);
            mPosSource->startUpdates();
        } else {
            emitMessageDialog("Postioning",
                              "Could not get Positioning Working.",
                              false, false);
        }
#endif
    }

    return res;
}

void VescInterface::closeRtLogFile()
{
    if (mRtLogFile.isOpen()) {
        mRtLogFile.close();
    }
}

bool VescInterface::isRtLogOpen()
{
    return mRtLogFile.isOpen();
}

QString VescInterface::rtLogFilePath()
{
    QFileInfo fi(mRtLogFile);
    return fi.canonicalFilePath();
}

QVector<LOG_DATA> VescInterface::getRtLogData()
{
    return mRtLogData;
}

bool VescInterface::loadRtLogFile(QString file)
{
    bool res = false;

    QFile inFile(file);

    if (inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        auto data = inFile.readAll();
        inFile.close();
        return loadRtLogFile(data);
    } else {
        emitMessageDialog("Read Log File",
                          "Could not open\n" +
                          file +
                          "\nfor reading.",
                          false, false);
    }

    return res;
}

bool VescInterface::loadRtLogFile(QByteArray data)
{
    bool res = false;

    QTextStream in(&data);
    int lineNum = 0;

    mRtLogData.clear();
    while (!in.atEnd()) {
        QStringList tokens = in.readLine().split(";");

        if (tokens.size() == 1) {
            tokens = tokens.at(0).split(",");
        }

        if (tokens.size() < 22) {
            continue;
        }

        if (lineNum > 0) {
            LOG_DATA d;
            d.valTime = tokens.at(0).toInt();
            d.values.v_in = tokens.at(1).toDouble();
            d.values.temp_mos = tokens.at(2).toDouble();
            d.values.temp_mos_1 = tokens.at(3).toDouble();
            d.values.temp_mos_2 = tokens.at(4).toDouble();
            d.values.temp_mos_3 = tokens.at(5).toDouble();
            d.values.temp_motor = tokens.at(6).toDouble();
            d.values.current_motor = tokens.at(7).toDouble();
            d.values.current_in = tokens.at(8).toDouble();
            d.values.id = tokens.at(9).toDouble();
            d.values.iq = tokens.at(10).toDouble();
            d.values.rpm = tokens.at(11).toDouble();
            d.values.duty_now = tokens.at(12).toDouble();
            d.values.amp_hours = tokens.at(13).toDouble();
            d.values.amp_hours_charged = tokens.at(14).toDouble();
            d.values.watt_hours = tokens.at(15).toDouble();
            d.values.watt_hours_charged = tokens.at(16).toDouble();
            d.values.tachometer = tokens.at(17).toInt();
            d.values.tachometer_abs = tokens.at(18).toInt();
            d.values.position = tokens.at(19).toDouble();
            d.values.fault_code = mc_fault_code(tokens.at(20).toInt());
            d.values.vesc_id = tokens.at(21).toInt();

            if (tokens.size() >= 55) {
                d.values.vd = tokens.at(22).toDouble();
                d.values.vq = tokens.at(23).toDouble();

                d.setupValTime = tokens.at(24).toInt();
                d.setupValues.amp_hours = tokens.at(25).toDouble();
                d.setupValues.amp_hours_charged = tokens.at(26).toDouble();
                d.setupValues.watt_hours = tokens.at(27).toDouble();
                d.setupValues.watt_hours_charged = tokens.at(28).toDouble();
                d.setupValues.battery_level = tokens.at(29).toDouble();
                d.setupValues.battery_wh = tokens.at(30).toDouble();
                d.setupValues.current_in = tokens.at(31).toDouble();
                d.setupValues.current_motor = tokens.at(32).toDouble();
                d.setupValues.speed = tokens.at(33).toDouble();
                d.setupValues.tachometer = tokens.at(34).toDouble();
                d.setupValues.tachometer_abs = tokens.at(35).toDouble();
                d.setupValues.num_vescs = tokens.at(36).toInt();

                d.imuValTime = tokens.at(37).toInt();
                d.imuValues.roll = tokens.at(38).toDouble();
                d.imuValues.pitch = tokens.at(39).toDouble();
                d.imuValues.yaw = tokens.at(40).toDouble();
                d.imuValues.accX = tokens.at(41).toDouble();
                d.imuValues.accY = tokens.at(42).toDouble();
                d.imuValues.accZ = tokens.at(43).toDouble();
                d.imuValues.gyroX = tokens.at(44).toDouble();
                d.imuValues.gyroY = tokens.at(45).toDouble();
                d.imuValues.gyroZ = tokens.at(46).toDouble();

                d.posTime = tokens.at(47).toInt();
                d.lat = tokens.at(48).toDouble();
                d.lon = tokens.at(49).toDouble();
                d.alt = tokens.at(50).toDouble();
                d.gVel = tokens.at(51).toDouble();
                d.vVel = tokens.at(52).toDouble();
                d.hAcc = tokens.at(53).toDouble();
                d.vAcc = tokens.at(54).toDouble();
            }

            mRtLogData.append(d);
        }

        lineNum++;
    }

    res = true;

    emitStatusMessage(QString("Loaded %1 log entries").arg(lineNum - 1), true);

    return res;
}

LOG_DATA VescInterface::getRtLogSample(double progress)
{
    LOG_DATA d;

    int sample = int(double(mRtLogData.size() - 1) * progress);
    if (sample >= 0 && sample < mRtLogData.size()) {
        d = mRtLogData.at(sample);
    }

    return d;
}

LOG_DATA VescInterface::getRtLogSampleAtValTimeFromStart(int time)
{
    LOG_DATA d;

    if (mRtLogData.size() > 0) {
        d = mRtLogData.first();
        int startTime = d.valTime;

        for (LOG_DATA dn: mRtLogData) {
            int timeMs = dn.valTime - startTime;
            if (timeMs < 0) { // Handle midnight
                timeMs += 60 * 60 * 24;
            }

            if (timeMs >= time) {
                d = dn;
                break;
            }
        }
    }

    return d;
}

bool VescInterface::useImperialUnits()
{
    return mUseImperialUnits;
}

void VescInterface::setUseImperialUnits(bool useImperialUnits)
{
    bool changed = useImperialUnits != mUseImperialUnits;
    mUseImperialUnits = useImperialUnits;
    if (changed) {
        emit useImperialUnitsChanged(mUseImperialUnits);
    }
}

bool VescInterface::keepScreenOn()
{
    return mKeepScreenOn;
}

void VescInterface::setKeepScreenOn(bool on)
{
    mKeepScreenOn = on;
}

bool VescInterface::useWakeLock()
{
    return mUseWakeLock;
}

void VescInterface::setUseWakeLock(bool on)
{
    mUseWakeLock = on;
}

bool VescInterface::setWakeLock(bool lock)
{
#ifdef Q_OS_ANDROID
    if (mWakeLock.isValid()) {
        mWakeLock.callMethod<void>("setReferenceCounted", "(Z)V", false);

        if (lock) {
            mWakeLock.callMethod<void>("acquire", "()V");
            mWakeLockActive = true;
        } else {
            mWakeLock.callMethod<void>("release", "()V");
            mWakeLockActive = false;
        }

        return true;
    } else {
        emitMessageDialog("Wake Lock", "Could not aquire wake lock", false, false);
        return false;
    }
#else
    (void)lock;
    return true;
#endif
}

bool VescInterface::getLoadQmlUiOnConnect() const
{
    return mLoadQmlUiOnConnect;
}

void VescInterface::setLoadQmlUiOnConnect(bool loadQmlUiOnConnect)
{
    mLoadQmlUiOnConnect = loadQmlUiOnConnect;
}

bool VescInterface::getAllowScreenRotation() const
{
    return mAllowScreenRotation;
}

void VescInterface::setAllowScreenRotation(bool allowScreenRotation)
{
    mAllowScreenRotation = allowScreenRotation;
    Utility::allowScreenRotation(allowScreenRotation);
}

bool VescInterface::speedGaugeUseNegativeValues()
{
    return mSpeedGaugeUseNegativeValues;
}

void VescInterface::setSpeedGaugeUseNegativeValues(bool useNegativeValues)
{
    mSpeedGaugeUseNegativeValues = useNegativeValues;
}

bool VescInterface::askQmlLoad() const
{
    return mAskQmlLoad;
}

void VescInterface::setAskQmlLoad(bool newAskQmlLoad)
{
    mAskQmlLoad = newAskQmlLoad;
}

#ifdef HAS_SERIALPORT
QString VescInterface::getLastSerialPort() const
{
    return mLastSerialPort;
}

int VescInterface::getLastSerialBaud() const
{
    return mLastSerialBaud;
}
#endif

#ifdef HAS_CANBUS
QString VescInterface::getLastCANbusInterface() const
{
    return mLastCanDeviceInterface;
}

int VescInterface::getLastCANbusBitrate() const
{
    return mLastCanDeviceBitrate;
}
#endif

#ifdef HAS_BLUETOOTH
BleUart *VescInterface::bleDevice()
{
    return mBleUart;
}

void VescInterface::storeBleName(QString address, QString name)
{
    mBleNames.insert(address, name);
}

QString VescInterface::getBleName(QString address)
{
    QString res;
    if(mBleNames.contains(address)) {
        res = mBleNames[address];
    }
    return res;
}

QString VescInterface::getLastBleAddr() const
{
    return mLastBleAddr;
}

void VescInterface::storeBlePreferred(QString address, bool preferred)
{
    mBlePreferred.insert(address, preferred);
}

bool VescInterface::getBlePreferred(QString address)
{
    bool res = false;
    if(mBlePreferred.contains(address)) {
        res = mBlePreferred[address];
    }
    return res;
}
#endif

bool VescInterface::isPortConnected()
{
    bool res = false;

#ifdef HAS_SERIALPORT
    if (mSerialPort->isOpen()) {
        res = true;
    }
#endif

#ifdef HAS_CANBUS
    if (isCANbusConnected() && !mCANbusScanning) {
        res = true;
    }
#endif

    if (mTcpConnected) {
        res = true;
    }

    if (mUdpConnected) {
        res = true;
    }

#ifdef HAS_BLUETOOTH
    if (mBleUart->isConnected()) {
        res = true;
    }
#endif

    return res;
}

void VescInterface::disconnectPort()
{
    if (m_fwIsOngoing && m_fwStep != FwUploadStep::Finalizing) {
        finishFwUpload(false, "Disconnected from VESC during firmware upload.");
    }
#ifdef HAS_SERIALPORT
    if(mSerialPort->isOpen()) {
        mSerialPort->flush();
        mSerialPort->close();
        updateFwRx(false);
    }
#endif

#ifdef HAS_CANBUS
    if(isCANbusConnected()) {
        mCanDevice->disconnectDevice();
        delete mCanDevice;
        mCanDevice = nullptr;
    }
#endif

    if (mTcpConnected) {
        mTcpSocket->flush();
        mTcpSocket->close();
        updateFwRx(false);
    }

    if (mUdpConnected) {
        mUdpSocket->close();
        mUdpConnected = false;
        updateFwRx(false);
    }

#ifdef HAS_BLUETOOTH
    if (mBleUart->isConnected()) {
        mBleUart->disconnectBle();
        updateFwRx(false);
    }
#endif

    mFwRetries = 0;
}

bool VescInterface::reconnectLastPort()
{
    if (mLastConnType == CONN_SERIAL) {
#ifdef HAS_SERIALPORT
        return connectSerial(mLastSerialPort, mLastSerialBaud);
#else
        return false;
#endif
    } else if (mLastConnType == CONN_TCP) {
        connectTcp(mLastTcpServer, mLastTcpPort);
        return true;
    } else if (mLastConnType == CONN_TCP_HUB) {
        connectTcpHub(mLastTcpHubServer, mLastTcpHubPort, mLastTcpHubVescID, mLastTcpHubVescPass);
        return true;
    } else if (mLastConnType == CONN_UDP) {
        connectUdp(mLastUdpServer.toString(), mLastUdpPort);
        return true;
    } else if (mLastConnType == CONN_BLE) {
#ifdef HAS_BLUETOOTH
        mBleUart->startConnect(mLastBleAddr);
#endif
        return true;
    } else if (mLastConnType == CONN_CANBUS) {
#ifdef HAS_CANBUS
        return connectCANbus(mLastCanBackend, mLastCanDeviceInterface, mLastCanDeviceBitrate);
#else
        return false;
#endif
    } else {
#ifdef HAS_SERIALPORT
        auto ports = listSerialPorts();
        if (!ports.isEmpty()) {
            return connectSerial(ports.first().value<VSerialInfo_t>().systemPath);
        } else  {
            emit messageDialog(tr("Reconnect"), tr("No ports found"), false, false);
            return false;
        }
#else
        emit messageDialog(tr("Reconnect"),
                           tr("Please specify the connection manually "
                              "the first time you are connecting."),
                           false, false);
        return false;
#endif
    }
}

bool VescInterface::lastPortAvailable()
{
    bool res = false;

#ifdef HAS_SERIALPORT
    if (mLastConnType == CONN_SERIAL) {
        auto ports = listSerialPorts();
        for (const auto &port : ports) {
            auto p = port.value<VSerialInfo_t>();
            if (p.systemPath == mLastSerialPort) {
                res = true;
                break;
            }
        }
    }
#endif

    return res;
}

bool VescInterface::autoconnect()
{
#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    emit autoConnectProgressUpdated(1.0, true);
    emit autoConnectFinished();
    return false;
#else
    bool res = false;

#ifdef HAS_SERIALPORT
    auto ports = listSerialPorts();
    mAutoconnectOngoing = true;
    mAutoconnectProgress = 0.0;

    disconnectPort();
    disconnect(mCommands, &Commands::fwVersionReceived, this, &VescInterface::fwVersionReceived);

    for (int i = 0;i < ports.size();i++) {
        VSerialInfo_t serial = ports[i].value<VSerialInfo_t>();

        if (!connectSerial(serial.systemPath)) {
            continue;
        }

        mSerialPort->flush();
        Utility::sleepWithEventLoop(100);
        mPacket->resetState();

        bool gotFw = false;
        runTree(Group{SignalWaitTaskItem([this, &gotFw](SignalWaitTask &task) {
            task.setTimeout(500);
            task.connectSignal(mCommands, &Commands::fwVersionReceived,
                               [&gotFw](FW_RX_PARAMS) { gotFw = true; });
            return SetupResult::Continue;
        })});

        if (gotFw) {
            // A firmware version was received.
            res = true;
            break;
        } else {
            mAutoconnectProgress = double(i) / double(ports.size());
            emit autoConnectProgressUpdated(mAutoconnectProgress, false);
            disconnectPort();
        }
    }

    connect(mCommands, &Commands::fwVersionReceived, this, &VescInterface::fwVersionReceived);
#endif

    emit autoConnectProgressUpdated(1.0, true);
    emit autoConnectFinished();
    mAutoconnectOngoing = false;
    return res;
#endif
}

QString VescInterface::getConnectedPortName()
{
    QString res = tr("Not connected");
    bool connected = false;

#ifdef HAS_SERIALPORT
    if (mSerialPort->isOpen()) {
        res = tr("Connected (serial) to %1").arg(mSerialPort->portName());
        connected = true;
    }
#endif

#ifdef HAS_CANBUS
    if (isCANbusConnected()) {
        res = tr("Connected (CAN bus) to %1").arg(mLastCanDeviceInterface);
        connected = true;
    }
#endif

    if (mTcpConnected) {
        res = tr("Connected (TCP) to %1:%2").arg(mLastTcpServer).arg(mLastTcpPort);
        connected = true;
    }

    if (mUdpConnected) {
        res = tr("Connected (UDP) to %1:%2").arg(mLastUdpServer.toString()).arg(mLastUdpPort);
        connected = true;
    }

#ifdef HAS_BLUETOOTH
    if (mBleUart->isConnected()) {
#if  defined(Q_OS_IOS)|defined(Q_OS_MACX)
        QStringList ids = mLastBleAddr.split('-');
        QString id = ids[0].left(3) + '-' + ids[1].left(2) + '-' + ids[2].left(2) + '-' + ids[3].left(2) + '-' + ids[4].right(3);
        res = tr("Connected (BLE) to %1").arg(id);
#else
        res = tr("Connected (BLE) to %1").arg(mLastBleAddr);
#endif
        connected = true;
    }
#endif

    if (connected && mCommands->isLimitedMode()) {
        res += tr(", limited mode");
    }

    return res;
}

bool VescInterface::connectSerial(QString port, int baudrate)
{
#ifdef HAS_SERIALPORT
    bool found = false;
#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    if (port.contains("WebSerial", Qt::CaseInsensitive) || port.startsWith("webserial://")) {
        found = true;
        port = "WebSerial";
    }
#endif
    for (auto ser: listSerialPorts()) {
        VSerialInfo_t info = ser.value<VSerialInfo_t>();

        //Allow for partial matches (otherwise on WIN we must specify a strange port path, eg. '\\.\COM4')
        if (info.systemPath.contains(port, Qt::CaseInsensitive)) {
            found = true;
            port = info.systemPath;
            break;
        }
    }

    if (!found) {
        emit statusMessage(tr("Invalid serial port: %1").arg(port), false);
        return false;
    }

    if(!mSerialPort->isOpen()) {
        // TODO: Maybe this test works on other OSes as well
#if defined(Q_OS_UNIX) && !defined(Q_OS_WASM)
        QFileInfo fi(port);
        if (fi.exists()) {
            if (!fi.isWritable()) {
                emit statusMessage(tr("Serial port is not writable"), false);
                emit serialPortNotWritable(port);
                return false;
            }
        }
#endif

        mSerialPort->setPortName(port);
        mSerialPort->open(QIODevice::ReadWrite);

        if(!mSerialPort->isOpen()) {
            return false;
        }

        mSerialPort->setBaudRate(baudrate);
        mSerialPort->setDataBits(QSerialPort::Data8);
        mSerialPort->setParity(QSerialPort::NoParity);
        mSerialPort->setStopBits(QSerialPort::OneStop);
        mSerialPort->setFlowControl(QSerialPort::NoFlowControl);
    }

    mLastSerialPort = port;
    mLastSerialBaud = baudrate;
    mSettings.setValue("serial_port", mLastSerialPort);
    mSettings.setValue("serial_baud", mLastSerialBaud);
    setLastConnectionType(CONN_SERIAL);
    return true;
#else
    (void)port;
    (void)baudrate;
    emit messageDialog(tr("Connect serial"),
                       tr("Serial port support is not enabled in this build "
                          "of VESC Tool."),
                       false, false);
    return false;
#endif
}

bool VescInterface::pairSerialPort()
{
#if defined(HAS_SERIALPORT) && defined(Q_OS_WASM)
    return QSerialPort::requestWebSerialPermission();
#else
    return true;
#endif
}

QVariantList VescInterface::listSerialPorts()
{
    QVariantList res;

#ifdef HAS_SERIALPORT
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();

    for (const QSerialPortInfo &port : ports) {
        VSerialInfo_t info;
        info.name = port.portName();
        info.systemPath = port.systemLocation();
        int index = res.size();

        //qDebug() << port.portName()  << ":  " << port.manufacturer() << "  " << port.productIdentifier();
        if (port.manufacturer().startsWith("STMicroelectronics") || port.productIdentifier() == 22336) {
            info.name.insert(0, "VESC - ");
            info.isVesc = true;
            index = 0;
        } else {
            info.isVesc = false;
        }

        if (port.productIdentifier() == 0x1001) {
            info.name.insert(0, "ESP32 - ");
            info.isEsp = true;
            index = 0;
        }

        res.insert(index, QVariant::fromValue(info));
    }
#endif

    return res;
}

QList<QString> VescInterface::listCANbusInterfaces()
{
    QList<QString> res;
#ifdef HAS_CANBUS
#ifdef Q_OS_UNIX
    QFile devicesFile("/proc/net/dev");

    if (devicesFile.open(QIODevice::ReadOnly)) {
        QTextStream in(&devicesFile);
        do {
            QString line = in.readLine();
            for ( int i = 0; i<10; i++) {
                QString interface = QString("can").append(QString::number(i));
                if (line.contains(interface)) {
                    res.append(interface);
                }
            }
        } while (!in.atEnd());

        devicesFile.close();
    }
#endif
#endif
    return res;
}

QStringList VescInterface::listCANbusInterfaceNames()
{
    QStringList res;
    for (const auto &s : listCANbusInterfaces()) {
        res.append(s);
    }
    return res;
}

static QByteArray readFirmwareFileHelper(const QString &path, QString &errorMsg)
{
    QString normalizedPath = path;
    if (normalizedPath.startsWith("file:/")) {
        QUrl u(normalizedPath);
        if (u.isLocalFile()) {
            normalizedPath = u.toLocalFile();
        } else {
            normalizedPath.remove(0, 6);
        }
    }

    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMsg = QString("Could not open file: %1").arg(normalizedPath);
        return QByteArray();
    }

    if (file.size() > 5000000) {
        errorMsg = "The selected file is too large to be a firmware.";
        return QByteArray();
    }

    if (normalizedPath.toLower().endsWith(".hex")) {
        file.close();
        QMap<quint32, QByteArray> fwMap;
        if (!HexFile::parseFile(normalizedPath, fwMap)) {
            errorMsg = "Failed to parse HEX firmware file.";
            return QByteArray();
        }

        QMapIterator<quint32, QByteArray> it(fwMap);
        QByteArray data;
        bool startSet = false;
        unsigned int startOffset = 0;

        while (it.hasNext()) {
            it.next();
            if (!startSet) {
                startSet = true;
                startOffset = it.key();
            }

            while ((quint32(data.size()) + startOffset) < it.key()) {
                data.append(char(0xFF));
            }

            data.append(it.value());
        }
        return data;
    }

    return file.readAll();
}

bool VescInterface::fwUploadQueued(QString fwPath, QString blPath, bool fwdCan)
{
    QByteArray appData;
    QByteArray blData;
    QString err;

    if (!fwPath.isEmpty()) {
        appData = readFirmwareFileHelper(fwPath, err);
        if (appData.isEmpty()) {
            emitMessageDialog("Firmware Upload", err.isEmpty() ? "Firmware file is empty." : err, false, false);
            return false;
        }
    }

    if (!blPath.isEmpty()) {
        blData = readFirmwareFileHelper(blPath, err);
        if (blData.isEmpty()) {
            emitMessageDialog("Firmware Upload", err.isEmpty() ? "Bootloader file is empty." : err, false, false);
            return false;
        }
    }

    if (appData.isEmpty() && blData.isEmpty()) {
        emitMessageDialog("Firmware Upload", "No firmware or bootloader file specified.", false, false);
        return false;
    }

    return fwUploadAsync(appData, blData, fwdCan, true, !appData.isEmpty());
}

bool VescInterface::fwUploadFromFile(QString path, bool isBootloader, bool fwdCan)
{
    if (isBootloader) {
        return fwUploadQueued("", path, fwdCan);
    } else {
        return fwUploadQueued(path, "", fwdCan);
    }
}

bool VescInterface::connectCANbus(QString backend, QString ifName, int bitrate)
{
#ifdef HAS_CANBUS
    QString errorString;

    mCANbusScanning = false;
    mCanDevice = QCanBus::instance()->createDevice(backend, ifName, &errorString);
    if (!mCanDevice) {
        QString msg = tr("Error creating device '%1' using backend '%2', reason: '%3'").arg(mLastCanDeviceInterface).arg(mLastCanBackend).arg(errorString);
        emit statusMessage(msg, false);
        qWarning() << msg;
        return false;
    }

    connect(mCanDevice, &QCanBusDevice::framesReceived, this, &VescInterface::CANbusDataAvailable);
    connect(mCanDevice, &QCanBusDevice::errorOccurred, this, &VescInterface::CANbusError);

    mCanDevice->setConfigurationParameter(QCanBusDevice::LoopbackKey, false);
    mCanDevice->setConfigurationParameter(QCanBusDevice::ReceiveOwnKey, false);
    // bitrate change not supported yet by socketcan. It is possible to set the rate when
    // configuring the CAN network interface using the ip link command.
    // mCanDevice->setConfigurationParameter(QCanBusDevice::BitRateKey, bitrate);
    mCanDevice->setConfigurationParameter(QCanBusDevice::CanFdKey, false);
    mCanDevice->setConfigurationParameter(QCanBusDevice::ReceiveOwnKey, false);

    if (!mCanDevice->connectDevice()) {
        QString msg = tr("Connection error: %1").arg(mCanDevice->errorString());
        emit statusMessage(msg, false);
        qWarning() << msg;

        delete mCanDevice;
        mCanDevice = nullptr;
        return false;
    }

    QThread::msleep(10);

    mLastCanBackend = backend;
    mLastCanDeviceInterface = ifName;
    mLastCanDeviceBitrate = bitrate;

    mSettings.setValue("CANbusBackend", mLastCanBackend);
    mSettings.setValue("CANbusDeviceInterface", mLastCanDeviceInterface);
    mSettings.setValue("CANbusDeviceBitrate", mLastCanDeviceBitrate);
    mSettings.setValue("CANbusLastDeviceID", mLastCanDeviceID);
    setLastConnectionType(CONN_CANBUS);
    return true;
#else
    (void)backend;
    (void)ifName;
    (void)bitrate;
    emit messageDialog(tr("Connect serial"),
                       tr("CAN bus support is not enabled in this build "
                          "of VESC Tool."),
                       false, false);
    return false;
#endif
}

bool VescInterface::isCANbusConnected()
{
#ifdef HAS_CANBUS
    if (mCanDevice != nullptr) {
        if (mCanDevice->state() == QCanBusDevice::ConnectedState) {
            return true;
        }
    }
#endif
    return false;
}

void VescInterface::setCANbusReceiverID(int node_ID)
{
#ifdef HAS_CANBUS
    mLastCanDeviceID = node_ID;
#else
    (void)node_ID;
#endif
}

void VescInterface::connectTcp(QString server, int port)
{
    mLastTcpServer = server;
    mLastTcpPort = port;
    mLastTcpHubVescID = "";
    mLastTcpHubVescPass = "";

    QHostAddress host;
    host.setAddress(server);

    // Try DNS lookup
    if (host.isNull()) {
        QList<QHostAddress> addresses = QHostInfo::fromName(server).addresses();

        if (!addresses.isEmpty()) {
            host.setAddress(addresses.first().toString());
        }
    }

    mTcpSocket->abort();
    mTcpSocket->connectToHost(host,port);
}

void VescInterface::connectTcpHub(QString server, int port, QString id, QString pass)
{
    mLastTcpHubServer = server;
    mLastTcpHubPort = port;
    mLastTcpHubVescID = id;
    mLastTcpHubVescPass = pass;

    QHostAddress host;
    host.setAddress(server);

    // Try DNS lookup
    if (host.isNull()) {
        QList<QHostAddress> addresses = QHostInfo::fromName(server).addresses();

        if (!addresses.isEmpty()) {
            host.setAddress(addresses.first().toString());
        }
    }

    mTcpSocket->abort();
    mTcpSocket->connectToHost(host,port);
}

void VescInterface::connectUdp(QString server, int port)
{
    QHostAddress host;
    host.setAddress(server);

    // Try DNS lookup
    if (host.isNull()) {
        QList<QHostAddress> addresses = QHostInfo::fromName(server).addresses();

        if (!addresses.isEmpty()) {
            host.setAddress(addresses.first().toString());
        }
    }

    if(host.isNull()) // Can't lookup
        return;

    mUdpConnected = true;
    mLastUdpServer = host;
    mLastUdpPort = port;
    setLastConnectionType(CONN_UDP);
    updateFwRx(false);
}

void VescInterface::connectBle(QString address)
{
#ifdef HAS_BLUETOOTH
    mBleUart->startConnect(address);
    mLastBleAddr = address;
#else
    (void)address;
#endif
}

bool VescInterface::isAutoconnectOngoing() const
{
    return mAutoconnectOngoing;
}

double VescInterface::getAutoconnectProgress() const
{
    return mAutoconnectProgress;
}

void VescInterface::scanCANbus()
{
#ifdef HAS_CANBUS
    if (!isCANbusConnected()) {
        return;
    }

    mCANbusScanning = true;
    mCanNodesID.clear();

    QCanBusFrame frame;
    frame.setExtendedFrameFormat(true);
    frame.setFrameType(QCanBusFrame::UnknownFrame);
    frame.setFlexibleDataRateFormat(false);
    frame.setBitrateSwitch(false);

    unsigned int i = 0;
    runTree(Group{PollTimerTaskItem([this, &frame, &i](PollTimerTask &task) {
        task.setInterval(15);
        task.setTimeout(0); // no external timeout — we finish from within
        task.setOnTick([this, &frame, &i, &task]() {
            frame.setFrameId(i | uint32_t(CAN_PACKET_PING << 8));
            mCanDevice->writeFrame(frame);
            i++;
            if (i >= 254) {
                task.finish();
            }
        });
        return SetupResult::Continue;
    })});
#endif
    return;
}

QVector<int> VescInterface::scanCan()
{
    QVector<int> canDevs;

    if (!isPortConnected()) {
        return canDevs;
    }

    canTmpOverride(false, 0);

    bool timeout = false;

    commands()->pingCan();
    runTree(Group{SignalWaitTaskItem([this, &canDevs, &timeout](SignalWaitTask &task) {
        task.setTimeout(10000); // generous timeout for CAN scan
        task.connectSignal(commands(), &Commands::pingCanRx,
                           [&canDevs, &timeout](QVector<int> devs, bool isTimeout) {
            for (int dev: devs) {
                canDevs.append(dev);
            }
            timeout = isTimeout;
        });
        return SetupResult::Continue;
    })});

    if (!timeout) {
        mCanDevsLast = canDevs;
    } else {
        canDevs.clear();
    }

    canTmpOverrideEnd();

    return canDevs;
}

QVector<int> VescInterface::getCanDevsLast() const
{
    return mCanDevsLast;
}

void VescInterface::ignoreCanChange(bool ignore)
{
    mIgnoreCanChange = ignore;
}

bool VescInterface::isIgnoringCanChanges()
{
    return mIgnoreCanChange;
}


/**
 * @brief VescInterface::canTmpOverride
 * Temporarily override CAN-forwading and ignore CAN-changes while doing so. Useful
 * to temporarily send one or more CAN-packets from commands to devices on the CAN-bus.
 *
 * This command should only be used briefly, after which canTmpOverrideEnd() should
 * be used to restore the old CAN-forwarding state.
 *
 * This command can be used multiple times without running canTmpOverrideEnd()
 * in-between, as the old CAN-state is only saved if overriding was not currently
 * active.
 *
 * @param fwdCan
 * Enable CAN-forwarding. Can also be set to false to send to the local divice
 * if CAN-forwarding is already active.
 *
 * @param canId
 * CAN-id to forward to
 */
void VescInterface::canTmpOverride(bool fwdCan, int canId)
{
    if (m_qmlAsyncLoading || m_customConfigAsyncLoading) {
        qWarning().noquote() << "[VESC_IF] Warning: canTmpOverride called while async loading is in progress! fwdCan:" << fwdCan << "canId:" << canId;
    }

    if (!mCanTmpFwdActive) {
        mCanTmpFwdActive = true;
        mCanTmpFwdSendCanLast = mCommands->getSendCan();
        mCanTmpFwdIdLast = mCommands->getCanSendId();
    }

    ignoreCanChange(true);
    mCommands->setSendCan(fwdCan, canId);
}

/**
 * @brief VescInterface::canTmpOverrideEnd
 * Restore the CAN-forwarding state after using canTmpOverride.
 */
void VescInterface::canTmpOverrideEnd()
{
    if (mCanTmpFwdActive) {
        mCanTmpFwdActive = false;
        mCommands->setSendCan(mCanTmpFwdSendCanLast, mCanTmpFwdIdLast);
        ignoreCanChange(false);
    }
}

void VescInterface::setSendCanAndReload(bool sendCan, int canId)
{
    if (canId < 0) {
        sendCan = false;
    }
    qDebug().noquote() << QString("[VESC_IF] Switching CAN target: sendCan=%1, canId=%2. Reloading firmware version...").arg(sendCan).arg(canId);
    mCommands->setSendCan(sendCan, canId);
    updateFwRx(false);
    mCommands->resetFwTimeout();
    mCommands->getFwVersion();
}

void VescInterface::reloadFirmwareVersion()
{
    qDebug().noquote() << "[VESC_IF] Reloading firmware version for current target...";
    updateFwRx(false);
    mCommands->resetFwTimeout();
    mCommands->getFwVersion();
}

bool VescInterface::tcpServerStart(int port)
{
    bool res = mTcpServer->startServer(port);

    if (!res) {
        emitMessageDialog("Start TCP Server",
                          "Could not start TCP server: " + mTcpServer->errorString(),
                          false, false);
    }

    return res;
}

void VescInterface::tcpServerStop()
{
    mTcpServer->stopServer();
}

bool VescInterface::tcpServerIsRunning()
{
    return mTcpServer->isServerRunning();
}

bool VescInterface::tcpServerIsClientConnected()
{
    return mTcpServer->isClientConnected();
}

QString VescInterface::tcpServerClientIp()
{
    return mTcpServer->getConnectedClientIp();
}

bool VescInterface::tcpServerConnectToHub(QString server, int port, QString id, QString pass)
{
    bool res = mTcpServer->connectToHub(server, port, id, pass);

    if (res) {
        mLastTcpHubServer = server;
        mLastTcpHubPort = port;
        mLastTcpHubVescID = id;
        mLastTcpHubVescPass = pass;
    } else {
        emitMessageDialog("Connecto to Hub",
                          "Could not connect to hub",
                          false, false);
    }

    return res;
}

bool VescInterface::udpServerStart(int port)
{
    bool res = mUdpServer->startServer(port);

    if (!res) {
        emitMessageDialog("Start UDP Server",
                          "Could not start UDP server: " + mUdpServer->errorString(),
                          false, false);
    }

    return res;
}

void VescInterface::udpServerStop()
{
    mUdpServer->stopServer();
}

bool VescInterface::udpServerIsRunning()
{
    return mUdpServer->isServerRunning();
}

bool VescInterface::udpServerIsClientConnected()
{
    return mUdpServer->isClientConnected();
}

QString VescInterface::udpServerClientIp()
{
    return mUdpServer->getConnectedClientIp();
}

void VescInterface::emitConfigurationChanged()
{
    emit configurationChanged();
}

#ifdef HAS_SERIALPORT
void VescInterface::serialDataAvailable()
{
    while (mSerialPort->bytesAvailable() > 0) {
        mPacket->processData(mSerialPort->readAll());
    }
}

void VescInterface::serialPortError(QSerialPort::SerialPortError error)
{
    QString message;
    switch (error) {
    case QSerialPort::NoError:
        break;

    default:
        message = "Serial port error: " + mSerialPort->errorString();
        break;
    }

    if(!message.isEmpty()) {
        emit statusMessage(message, false);

        if (mSerialPort->isOpen()) {
            mSerialPort->close();
        }

       updateFwRx(false);
    }
}
#endif

#ifdef HAS_CANBUS
void VescInterface::CANbusDataAvailable()
{
    QCanBusFrame frame;
    QByteArray payload;
    unsigned short rxbuf_len = 0;
    unsigned short crc;
    char commands_send;

    while (mCanDevice->framesAvailable() > 0) {
        frame = mCanDevice->readFrame();
        if (frame.isValid() && (frame.frameType() == QCanBusFrame::DataFrame)) {
            int packet_type = frame.frameId() >> 8;
            payload = frame.payload();

            switch(packet_type) {
            case CAN_PACKET_PONG:
                mCanNodesID.append(payload[0]);
                emit CANbusNewNode(payload[0]);
                break;

            case CAN_PACKET_PROCESS_SHORT_BUFFER:
                payload.remove(0,2);

                rxbuf_len = payload.size();
                crc = Packet::crc16((const unsigned char*)payload.data(), rxbuf_len);

                // add stop, start, length and crc for the packet decoder
                payload.prepend((unsigned char) rxbuf_len);
                payload.prepend(2);
                payload.append((unsigned char)(crc>>8));
                payload.append((unsigned char)(crc & 0xFF));
                payload.append(3);
                mPacket->processData(payload);
                break;

            case CAN_PACKET_FILL_RX_BUFFER:
                payload.remove(0,1);    // discard index
                mCanRxBuffer.append(payload);
                break;

            case CAN_PACKET_FILL_RX_BUFFER_LONG:
                payload.remove(0,2);    // discard the 2 byte index
                mCanRxBuffer.append(payload);
                break;

            case CAN_PACKET_PROCESS_RX_BUFFER:
                commands_send = payload[1];
                rxbuf_len = (unsigned short)payload[2] << 8 | (unsigned char)payload[3];

                if (rxbuf_len > 512) {
                    return;
                }
                unsigned char len_high = payload[2];
                unsigned char len_low = payload[3];

                unsigned char crc_high = payload[4];
                unsigned char crc_low = payload[5];

                if (Packet::crc16((const unsigned char*)mCanRxBuffer.data(), rxbuf_len) ==
                        ((unsigned short) crc_high << 8 | (unsigned short) crc_low)) {
                    switch (commands_send) {
                        case 0:
                            break;
                        case 1:
                            // add stop, start, length and crc for the packet decoder
                            if (len_high == 0) {
                                mCanRxBuffer.prepend(len_low);
                                mCanRxBuffer.prepend(2);
                            } else {
                                mCanRxBuffer.prepend(len_low);
                                mCanRxBuffer.prepend(len_high);
                                mCanRxBuffer.prepend(3); // size is 16 bit long
                            }

                            mCanRxBuffer.append(crc_high);
                            mCanRxBuffer.append(crc_low);
                            mCanRxBuffer.append(3);
                            mPacket->processData(mCanRxBuffer);
                            break;
                        case 2:
                            //commands_process_packet(rx_buffer, rxbuf_len, 0);
                            break;
                        default:
                            break;
                    }
                }
                mCanRxBuffer.clear();
                break;
            }
        }
    }
}

void VescInterface::CANbusError(QCanBusDevice::CanBusError error)
{
    QString message;
    switch (error) {
    case QCanBusDevice::NoError:
        break;

    default:
        message = "CAN bus error: " + mCanDevice->errorString();
        break;
    }

    if(!message.isEmpty()) {
        emit statusMessage(message, false);
        mCanDevice->disconnectDevice();
        updateFwRx(false);
    }
}
#endif

void VescInterface::tcpInputConnected()
{
    mTcpSocket->setSocketOption(QAbstractSocket::LowDelayOption, true);

    if (!mLastTcpHubVescID.isEmpty()) {
        QString login = QString("VESCTOOL:%1:%2\n\0").arg(mLastTcpHubVescID).arg(mLastTcpHubVescPass);
        mTcpSocket->write(login.toLocal8Bit());

        mSettings.setValue("tcp_hub_server", mLastTcpHubServer);
        mSettings.setValue("tcp_hub_port", mLastTcpHubPort);
        mSettings.setValue("tcp_hub_vesc_id", mLastTcpHubVescID);
        mSettings.setValue("tcp_hub_vesc_pass", mLastTcpHubVescPass);
        setLastConnectionType(CONN_TCP_HUB);

        TCP_HUB_DEVICE devNow;
        devNow.server = mLastTcpHubServer;
        devNow.port = mLastTcpHubPort;
        devNow.id = mLastTcpHubVescID;
        devNow.password = mLastTcpHubVescPass;

        bool found = false;
        for (const auto &i: std::as_const(mTcpHubDevs)) {
            auto dev = i.value<TCP_HUB_DEVICE>();
            if (dev.uuid() == devNow.uuid()) {
                found = true;
                updateTcpHubPassword(dev.uuid(), mLastTcpHubVescPass);
                break;
            }
        }

        if (!found) {
            mTcpHubDevs.append(QVariant::fromValue(devNow));
            storeSettings();
        }
    } else {
        mSettings.setValue("tcp_server", mLastTcpServer);
        mSettings.setValue("tcp_port", mLastTcpPort);
        setLastConnectionType(CONN_TCP);
    }

    mTcpConnected = true;
    updateFwRx(false);
}

void VescInterface::tcpInputDisconnected()
{
    mTcpConnected = false;
    updateFwRx(false);
}

void VescInterface::tcpInputDataAvailable()
{
    while (mTcpSocket->bytesAvailable() > 0) {
        mPacket->processData(mTcpSocket->readAll());
    }
}

void VescInterface::udpInputDataAvailable()
{
    while (mUdpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = mUdpSocket->receiveDatagram();
        mPacket->processData(datagram.data());
    }
}

void VescInterface::tcpInputError(QAbstractSocket::SocketError socketError)
{
    (void)socketError;

    QString errorStr = mTcpSocket->errorString();
    emit statusMessage(tr("TCP Error") + errorStr, false);
    mTcpSocket->close();
    updateFwRx(false);
}

void VescInterface::udpInputError(QAbstractSocket::SocketError socketError)
{
    (void)socketError;

    QString errorStr = mUdpSocket->errorString();
    emit statusMessage(tr("UDP Error") + errorStr, false);
    mUdpSocket->close();
    mUdpConnected = false;
    updateFwRx(false);
}

#ifdef HAS_BLUETOOTH
void VescInterface::bleDataRx(QByteArray data)
{
    mPacket->processData(data);
}

void VescInterface::bleUnintentionalDisconnect()
{
   emit unintentionalBleDisconnect();
}
#endif

void VescInterface::timerSlot()
{
    // Poll the serial port as well since readyRead is not emitted recursively. This
    // can be a problem when waiting for input with an additional event loop, such as
    // when using QMessageBox.
#ifdef HAS_SERIALPORT
    serialDataAvailable();
#endif
#ifdef HAS_CANBUS
    if (mCanDevice != nullptr) {
        CANbusDataAvailable();
    }
#endif
    tcpInputDataAvailable();

#ifdef HAS_CANBUS
    if (mCanDeviceInterfaces != listCANbusInterfaces()) {
        mCanDeviceInterfaces = listCANbusInterfaces();
        emit CANbusInterfaceListUpdated();
    }
#endif

    if (!mIgnoreCanChange) {
        if (isPortConnected()) {
            if (mSendCanBefore != mCommands->getSendCan() ||
                    (mCommands->getSendCan() &&
                     mCanIdBefore != mCommands->getCanSendId())) {
                updateFwRx(false);
                mFwRetries = 0;
            }

            mFwPollCnt++;
            if (mFwPollCnt >= 4) {
                mFwPollCnt = 0;
                if (!mFwVersionReceived) {
                    mCommands->getFwVersion();
                    mFwRetries++;

                    // Timeout if the firmware cannot be read
                    if (mFwRetries >= 25) {
                        emit statusMessage(tr("No firmware read response"), false);
                        emit messageDialog(tr("Read Firmware Version"),
                                           tr("Could not read firmware version. Make sure that "
                                              "the selected port really belongs to the VESC. If "
                                              "you are using UART, make sure that the port is enabled, "
                                              "connected correctly (rx to tx and tx to rx) and uses "
                                              "the correct baudrate"),
                                           false, false);
                        disconnectPort();
                    }
                }
            }
        } else {
            updateFwRx(false);
            mFwRetries = 0;
        }
        mSendCanBefore = mCommands->getSendCan();
        mCanIdBefore = mCommands->getCanSendId();
    }

    // Update fw upload bar and label
    double fwProg = getFwUploadProgress();
    QString fwStatus = getFwUploadStatus();
    if (fwProg > -0.1) {
        mIsUploadingFw = true;
        mIsLastFwBootloader = isCurrentFwBootloader();
        emit fwUploadStatus(fwStatus, fwProg, true);
    } else {
        // The firmware upload just finished or failed
        if (mIsUploadingFw) {
            mFwRetries = 0;
            if (fwStatus.compare("FW Upload Done") == 0) {
                emit fwUploadStatus(fwStatus, 1.0, false);
                if (mIsLastFwBootloader) {
                    emitMessageDialog("Bootloader Upload",
                                      "Bootloader upload finished! You can now upload new firmware "
                                      "to the VESC.",
                                      true, false);
                } else {
                    disconnectPort();
                    emitMessageDialog("Firmware Upload",
                                      "Firmware upload finished! Give the VESC around 10 "
                                      "seconds to apply the firmware and reboot, then reconnect.",
                                      true, false);
                }
            } else {
                emit fwUploadStatus(fwStatus, 0.0, false);
            }
        }
        mIsUploadingFw = false;
    }

    if (mWasConnected != isPortConnected()) {
        mWasConnected = isPortConnected();

        if (!isPortConnected()) {
            if (!getSupportedFirmwarePairs().contains(Utility::configLatestSupported())) {
                Utility::configLoadLatest(this);
            }

            mDeserialFailedMessageShown = false;
            mPacket->resetState();
            mFwSwapDone = false;
        }

        emit portConnectedChanged();
    }
}

void VescInterface::packetDataToSend(QByteArray data)
{
#ifdef HAS_SERIALPORT
    if (mSerialPort->isOpen()) {
        mSerialPort->write(data);
    }
#endif

#ifdef HAS_CANBUS
    if (isCANbusConnected()) {
        // Sending a frame while a frame is received seems to cause problems,
        // so always delay sending a bit in case a frame that expects a reply
        // was sent just previously. TODO: Figure out what the problem is.
        // Guess: Something in the CANable firmware.
        QThread::msleep(5);

        QCanBusFrame frame;
        frame.setExtendedFrameFormat(true);
        frame.setFrameType(QCanBusFrame::UnknownFrame);
        frame.setFlexibleDataRateFormat(false);
        frame.setBitrateSwitch(false);

        // Remove start byte and length
        if (data[0] == char(2)) {
            data.remove(0, 2);
        } else if (data[0] == char(3)) {
            data.remove(0, 3);
        } else if (data[0] == char(4)) {
            data.remove(0, 4);
        }

        // Remove CRC and stop byte
        data.truncate(data.size() - 3);

        // Since we already are on the CAN-bus, we can send packets that
        // are supposed to be forwarded directly to the correct device.
        int target_id = mLastCanDeviceID;
        if (data.at(0) == COMM_FORWARD_CAN) {
            target_id = uint8_t(data.at(1));
            data.remove(0, 2);
        }

        if (data.size() <= 6) { // Send packet in a single frame
            data.prepend(char(0)); // Process packet at receiver
            data.prepend(char(254)); // VESC Tool sender ID

            frame.setFrameId(uint32_t(target_id) |
                             uint32_t(CAN_PACKET_PROCESS_SHORT_BUFFER << 8));
            frame.setPayload(data);

            mCanDevice->writeFrame(frame);
            mCanDevice->waitForFramesWritten(5);
        } else {
            int len = data.size();
            QByteArray payload;
            int end_a = 0;

            unsigned short crc = Packet::crc16(
                        reinterpret_cast<const unsigned char*>(data.data()),
                        uint32_t(len));

            for (int i = 0;i < len;i += 7) {
                if (i > 255) {
                    break;
                }

                end_a = i + 7;

                payload[0] = char(i);
                payload.append(data.left(7));
                data.remove(0,7);
                frame.setPayload(payload);
                frame.setFrameId(uint32_t(target_id) |
                                 uint32_t(CAN_PACKET_FILL_RX_BUFFER << 8));

                mCanDevice->writeFrame(frame);
                mCanDevice->waitForFramesWritten(5);
//                QThread::msleep(5);
                payload.clear();
            }

            for (int i = end_a;i < len;i += 6) {
                payload[0] = char(i >> 8);
                payload[1] = char(i & 0xFF);

                payload.append(data.left(6));
                data.remove(0,6);
                frame.setPayload(payload);
                frame.setFrameId(uint32_t(target_id) |
                                 uint32_t(CAN_PACKET_FILL_RX_BUFFER_LONG << 8));

                mCanDevice->writeFrame(frame);
                mCanDevice->waitForFramesWritten(5);
//                QThread::msleep(5);
                payload.clear();
            }

            payload[0] = char(254); // vesc tool node ID
            payload[1] = char(0); // process
            payload[2] = char(len >> 8);
            payload[3] = char(len & 0xFF);
            payload[4] = char(crc >> 8);
            payload[5] = char(crc & 0xFF);
            frame.setPayload(payload);
            frame.setFrameId(uint32_t(target_id) |
                             uint32_t(CAN_PACKET_PROCESS_RX_BUFFER << 8));

            mCanDevice->writeFrame(frame);
            mCanDevice->waitForFramesWritten(5);
        }
    }
#endif

    if (mTcpConnected && mTcpSocket->isOpen()) {
        mTcpSocket->write(data);
        mTcpSocket->flush();
    }

    if (mUdpConnected) {
        mUdpSocket->writeDatagram(data, mLastUdpServer, mLastUdpPort);
    }

#ifdef HAS_BLUETOOTH
    if (mBleUart->isConnected()) {
        mBleUart->writeData(data);
    }
#endif
}

void VescInterface::packetReceived(QByteArray data)
{
    mCommands->processPacket(data);
}

void VescInterface::cmdDataToSend(QByteArray data)
{
    mPacket->sendPacket(data);
}

void VescInterface::fwVersionReceived(FW_RX_PARAMS params)
{
    // Do not reload configs when the firmware version is read from somewhere else.
    if (mFwVersionReceived) {
        return;
    }

    mLastFwParams = params;

    QString uuidStr = Utility::uuid2Str(params.uuid, true);
    mUuidStr = uuidStr.toUpper();
    mUuidStr.replace(" ", "");
    mFwSupportsConfiguration = false;

    if (!mCommands->getSendCan()) {
        mUuidStrLocal = mUuidStr;
    }

    if (mSettings.value("reconnectLastCan", true).toBool() &&
            !mUuidStrLocal.isEmpty() && mLastFwUuids.contains(mUuidStrLocal)) {

        auto pair = mLastFwUuids[mUuidStrLocal];

        if (pair.second >= 0 && pair.first != mUuidStr && !mFwSwapDone && !mBlockFwSwap) {
            FW_RX_PARAMS pRx;
            bool ok = Utility::getFwVersionBlockingCan(this, &pRx, pair.second, 1500);
            if (ok && Utility::uuid2Str(pRx.uuid, false) == pair.first) {
                mCommands->setSendCan(true, pair.second);
                return;
            }
        }
    }

    if (!mUuidStrLocal.isEmpty()) {
        int canId = -1;
        if (mCommands->getSendCan()) {
            canId = mCommands->getCanSendId();
        }

        mLastFwUuids[mUuidStrLocal] = qMakePair(mUuidStr, canId);
    }

    mFwSwapDone = true;

#ifdef HAS_BLUETOOTH
    if (mBleUart->isConnected()) {
        if (params.isPaired && !hasPairedUuid(mUuidStr)) {
            disconnectPort();
            emitMessageDialog("Pairing",
                              "This device is not paired to your local version of VESC Tool. You can either "
                              "add the UUID to the pairing list manually, or connect over USB and set the app "
                              "pairing flag to false for this VESC. Then you can pair to this version of VESC "
                              "tool, or leave the device unpaired.",
                              false, false);
            return;
        }
    }
#endif

    auto fwPairs = getSupportedFirmwarePairs();

    // Make sure that we start from the latest firmware
    if (!fwPairs.contains(Utility::configLatestSupported())) {
        Utility::configLoadLatest(this);
        fwPairs = getSupportedFirmwarePairs();
    }

    if (fwPairs.isEmpty()) {
        emit messageDialog(tr("No Supported Firmwares"),
                           tr("This version of VESC Tool does not seem to have any supported "
                              "firmwares. Something is probably wrong with the motor configuration "
                              "file."),
                           false, false);
        updateFwRx(false);
        mFwRetries = 0;
        disconnectPort();
        return;
    }

    QPair<int, int> highest_supported = *std::max_element(fwPairs.begin(), fwPairs.end());
    QPair<int, int> fw_connected = qMakePair(params.major, params.minor);

    mCommands->setLimitedSupportsFwdAllCan(fw_connected >= qMakePair(3, 45));
    mCommands->setLimitedSupportsEraseBootloader(fw_connected >= qMakePair(3, 59));

    // This bug is fixed in firmware 5.03 beta 53.
    if (fw_connected >= qMakePair(5, 03)) {
        if (fw_connected == qMakePair(5, 03)) {
            if (params.isTestFw == 0 || params.isTestFw > 52) {
                mCommands->setMaxPowerLossBug(false);
            } else {
                mCommands->setMaxPowerLossBug(true);
            }
        } else {
            mCommands->setMaxPowerLossBug(false);
        }
    } else {
        mCommands->setMaxPowerLossBug(true);
    }

    QVector<int> compCommands;
    if (fw_connected >= qMakePair(3, 47)) {
        compCommands.append(int(COMM_GET_VALUES));
        compCommands.append(int(COMM_SET_DUTY));
        compCommands.append(int(COMM_SET_CURRENT));
        compCommands.append(int(COMM_SET_CURRENT_BRAKE));
        compCommands.append(int(COMM_SET_RPM));
        compCommands.append(int(COMM_SET_POS));
        compCommands.append(int(COMM_SET_HANDBRAKE));
        compCommands.append(int(COMM_SET_DETECT));
        compCommands.append(int(COMM_SET_SERVO_POS));
        compCommands.append(int(COMM_SAMPLE_PRINT));
        compCommands.append(int(COMM_TERMINAL_CMD));
        compCommands.append(int(COMM_PRINT));
        compCommands.append(int(COMM_ROTOR_POSITION));
        compCommands.append(int(COMM_EXPERIMENT_SAMPLE));

        // TODO: Maybe detect shouldn't be backwards compatible.
        compCommands.append(int(COMM_DETECT_MOTOR_PARAM));
        compCommands.append(int(COMM_DETECT_MOTOR_R_L));
        compCommands.append(int(COMM_DETECT_MOTOR_FLUX_LINKAGE));
        compCommands.append(int(COMM_DETECT_ENCODER));
        compCommands.append(int(COMM_DETECT_HALL_FOC));

        compCommands.append(int(COMM_REBOOT));
        compCommands.append(int(COMM_ALIVE));
        compCommands.append(int(COMM_GET_DECODED_PPM));
        compCommands.append(int(COMM_GET_DECODED_ADC));
        compCommands.append(int(COMM_GET_DECODED_CHUK));
        compCommands.append(int(COMM_FORWARD_CAN));
        compCommands.append(int(COMM_SET_CHUCK_DATA));
        compCommands.append(int(COMM_CUSTOM_APP_DATA));
        compCommands.append(int(COMM_NRF_START_PAIRING));

        // GPD stuff is quite experimental...
        compCommands.append(int(COMM_GPD_SET_FSW));
        compCommands.append(int(COMM_GPD_BUFFER_NOTIFY));
        compCommands.append(int(COMM_GPD_BUFFER_SIZE_LEFT));
        compCommands.append(int(COMM_GPD_FILL_BUFFER));
        compCommands.append(int(COMM_GPD_OUTPUT_SAMPLE));
        compCommands.append(int(COMM_GPD_SET_MODE));
        compCommands.append(int(COMM_GPD_FILL_BUFFER_INT8));
        compCommands.append(int(COMM_GPD_FILL_BUFFER_INT16));
        compCommands.append(int(COMM_GPD_SET_BUFFER_INT_SCALE));

        compCommands.append(int(COMM_GET_VALUES_SETUP));
        compCommands.append(int(COMM_SET_MCCONF_TEMP));
        compCommands.append(int(COMM_SET_MCCONF_TEMP_SETUP));
        compCommands.append(int(COMM_GET_VALUES_SELECTIVE));
        compCommands.append(int(COMM_GET_VALUES_SETUP_SELECTIVE));

        // TODO: Maybe detect shouldn't be backwards compatible.
        compCommands.append(int(COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP));
        compCommands.append(int(COMM_DETECT_APPLY_ALL_FOC));

        compCommands.append(int(COMM_PING_CAN));
        compCommands.append(int(COMM_APP_DISABLE_OUTPUT));
    }

    if (fw_connected >= qMakePair(3, 52)) {
        compCommands.append(int(COMM_TERMINAL_CMD_SYNC));
        compCommands.append(int(COMM_GET_IMU_DATA));
    }

    if (fw_connected >= qMakePair(3, 54)) {
        compCommands.append(int(COMM_BM_CONNECT));
        compCommands.append(int(COMM_BM_ERASE_FLASH_ALL));
        compCommands.append(int(COMM_BM_WRITE_FLASH));
        compCommands.append(int(COMM_BM_REBOOT));
        compCommands.append(int(COMM_BM_DISCONNECT));
    }

    if (fw_connected >= qMakePair(3, 59)) {
        compCommands.append(int(COMM_BM_MAP_PINS_DEFAULT));
        compCommands.append(int(COMM_BM_MAP_PINS_NRF5X));
    }

    if (fw_connected >= qMakePair(3, 60)) {
        compCommands.append(int(COMM_PLOT_INIT));
        compCommands.append(int(COMM_PLOT_DATA));
        compCommands.append(int(COMM_PLOT_ADD_GRAPH));
        compCommands.append(int(COMM_PLOT_SET_GRAPH));
    }

    if (fw_connected >= qMakePair(3, 62)) {
        compCommands.append(int(COMM_GET_DECODED_BALANCE));
        compCommands.append(int(COMM_BM_MEM_READ));
    }

    if (fw_connected >= qMakePair(3, 63)) {
        compCommands.append(int(COMM_WRITE_NEW_APP_DATA_LZO));
        compCommands.append(int(COMM_WRITE_NEW_APP_DATA_ALL_CAN_LZO));
        compCommands.append(int(COMM_BM_WRITE_FLASH_LZO));
    }

    if (fw_connected >= qMakePair(3, 64)) {
        compCommands.append(int(COMM_SET_CURRENT_REL));
        compCommands.append(int(COMM_SET_BATTERY_CUT));
    }

    if (fw_connected >= qMakePair(5, 00)) {
        compCommands.append(int(COMM_SET_CURRENT_REL));
    }

    if (fw_connected >= qMakePair(5, 02)) {
        compCommands.append(int(COMM_CAN_FWD_FRAME));
        compCommands.append(int(COMM_SET_BATTERY_CUT));
        compCommands.append(int(COMM_SET_BLE_NAME));
        compCommands.append(int(COMM_SET_BLE_PIN));
        compCommands.append(int(COMM_SET_CAN_MODE));
        compCommands.append(int(COMM_GET_IMU_CALIBRATION));
        compCommands.append(int(COMM_GET_MCCONF_TEMP));
        compCommands.append(int(COMM_GET_CUSTOM_CONFIG_XML));
        compCommands.append(int(COMM_GET_CUSTOM_CONFIG));
        compCommands.append(int(COMM_GET_CUSTOM_CONFIG_DEFAULT));
        compCommands.append(int(COMM_SET_CUSTOM_CONFIG));
        compCommands.append(int(COMM_BMS_GET_VALUES));
        compCommands.append(int(COMM_BMS_SET_CHARGE_ALLOWED));
        compCommands.append(int(COMM_BMS_SET_BALANCE_OVERRIDE));
        compCommands.append(int(COMM_BMS_RESET_COUNTERS));
        compCommands.append(int(COMM_BMS_FORCE_BALANCE));
        compCommands.append(int(COMM_BMS_ZERO_CURRENT_OFFSET));
        compCommands.append(int(COMM_JUMP_TO_BOOTLOADER_HW));
        compCommands.append(int(COMM_ERASE_NEW_APP_HW));
        compCommands.append(int(COMM_WRITE_NEW_APP_DATA_HW));
        compCommands.append(int(COMM_ERASE_BOOTLOADER_HW));
        compCommands.append(int(COMM_JUMP_TO_BOOTLOADER_ALL_CAN_HW));
        compCommands.append(int(COMM_ERASE_NEW_APP_ALL_CAN_HW));
        compCommands.append(int(COMM_WRITE_NEW_APP_DATA_ALL_CAN_HW));
        compCommands.append(int(COMM_ERASE_BOOTLOADER_ALL_CAN_HW));
        compCommands.append(int(COMM_SET_ODOMETER));
    }

    if (fw_connected >= qMakePair(5, 03)) {
        compCommands.append(int(COMM_PSW_GET_STATUS));
        compCommands.append(int(COMM_PSW_SWITCH));
        compCommands.append(int(COMM_BMS_FWD_CAN_RX));
        compCommands.append(int(COMM_BMS_HW_DATA));
        compCommands.append(int(COMM_GET_BATTERY_CUT));
        compCommands.append(int(COMM_BM_HALT_REQ));
        compCommands.append(int(COMM_GET_QML_UI_HW));
        compCommands.append(int(COMM_GET_QML_UI_APP));
        compCommands.append(int(COMM_CUSTOM_HW_DATA));
        compCommands.append(int(COMM_QMLUI_ERASE));
        compCommands.append(int(COMM_QMLUI_WRITE));
        compCommands.append(int(COMM_IO_BOARD_GET_ALL));
        compCommands.append(int(COMM_IO_BOARD_SET_PWM));
        compCommands.append(int(COMM_IO_BOARD_SET_DIGITAL));
        compCommands.append(int(COMM_BM_MEM_WRITE));
        compCommands.append(int(COMM_BMS_BLNC_SELFTEST));
        compCommands.append(int(COMM_GET_EXT_HUM_TMP));
        compCommands.append(int(COMM_GET_STATS));
    }

    if (fw_connected >= qMakePair(6, 00)) {
        compCommands.append(int(COMM_RESET_STATS));
        compCommands.append(int(COMM_LISP_READ_CODE));
        compCommands.append(int(COMM_LISP_WRITE_CODE));
        compCommands.append(int(COMM_LISP_ERASE_CODE));
        compCommands.append(int(COMM_LISP_SET_RUNNING));
        compCommands.append(int(COMM_LISP_GET_STATS));
        compCommands.append(int(COMM_LISP_PRINT));
        compCommands.append(int(COMM_BMS_SET_BATT_TYPE));
        compCommands.append(int(COMM_BMS_GET_BATT_TYPE));
        compCommands.append(int(COMM_LISP_REPL_CMD));
        compCommands.append(int(COMM_LISP_STREAM_CODE));
        compCommands.append(int(COMM_FILE_LIST));
        compCommands.append(int(COMM_FILE_READ));
        compCommands.append(int(COMM_FILE_WRITE));
        compCommands.append(int(COMM_FILE_MKDIR));
        compCommands.append(int(COMM_FILE_REMOVE));
        compCommands.append(int(COMM_LOG_START));
        compCommands.append(int(COMM_LOG_STOP));
        compCommands.append(int(COMM_LOG_CONFIG_FIELD));
        compCommands.append(int(COMM_LOG_DATA_F32));
        compCommands.append(int(COMM_SET_APPCONF_NO_STORE));
        compCommands.append(int(COMM_GET_GNSS));
        compCommands.append(int(COMM_LOG_DATA_F64));
    }

    const bool isFwPair = fwPairs.contains(fw_connected);
    const bool isSupportedFw = Utility::configSupportedFws().contains(fw_connected);
    qDebug().noquote() << QString("[VESC_IF] FW %1.%2 connected. isFwPair: %3, isSupportedFw: %4, hwType: %5")
        .arg(fw_connected.first).arg(fw_connected.second)
        .arg(isFwPair).arg(isSupportedFw).arg(int(params.hwType));

    if (params.hwType == HW_TYPE_VESC && (isFwPair || isSupportedFw)) {
        compCommands.append(int(COMM_SET_MCCONF));
        compCommands.append(int(COMM_GET_MCCONF));
        compCommands.append(int(COMM_GET_MCCONF_DEFAULT));
        compCommands.append(int(COMM_SET_APPCONF));
        compCommands.append(int(COMM_GET_APPCONF));
        compCommands.append(int(COMM_GET_APPCONF_DEFAULT));

        if (!isFwPair) {
            qDebug().noquote() << QString("[VESC_IF] Loading matching configuration for FW %1.%2")
                .arg(fw_connected.first).arg(fw_connected.second);
            bool loaded = Utility::configLoad(this, fw_connected.first, fw_connected.second);
            qDebug().noquote() << QString("[VESC_IF] configLoad result for FW %1.%2: %3")
                .arg(fw_connected.first).arg(fw_connected.second).arg(loaded);
        }

        mFwSupportsConfiguration = true;
    } else {
        qWarning().noquote() << QString("[VESC_IF] Warning: FW %1.%2 not considered configuration-supported! Keeping default latest config.")
            .arg(fw_connected.first).arg(fw_connected.second);
    }

    // Only store the config version if the loaded firmware is the latest one. This will
    // give a warning when that config is loaded with a new firmware version.
    mMcConfig->setStoreConfigVersion(Utility::configLatestSupported() == fw_connected);

    // Hack for old unity devices with a firmware fork
    if ((fw_connected >= qMakePair(3, 100) && fw_connected <= qMakePair(3, 103)) ||
        (fw_connected >= qMakePair(23, 34) && fw_connected <= qMakePair(23, 46))) {
        compCommands.clear();
    }

    mCommands->setLimitedCompatibilityCommands(compCommands);

    bool wasReceived = mFwVersionReceived;
    mCommands->setLimitedMode(false);

    if (params.major < 0) {
        updateFwRx(false);
        mFwRetries = 0;
        disconnectPort();
        emit messageDialog(tr("Error"), tr("The firmware on the connected device is too old. Please "
                                           "update it using a programmer."), false, false);
    } else if (fw_connected > highest_supported) {
        mCommands->setLimitedMode(true);
        updateFwRx(true);
        if (!wasReceived) {
            emit messageDialog(tr("Warning"), tr("The connected device has newer firmware than this version of "
                                                "VESC Tool supports. It is recommended that you update VESC "
                                                "Tool to the latest version. Alternatively, the firmware on "
                                                "the connected device can be downgraded in the firmware page. "
                                                "Until then, limited communication mode will be used."), false, false);
        }
    } else if (!fwPairs.contains(fw_connected)) {
        if (fw_connected >= qMakePair(1, 1)) {
            mCommands->setLimitedMode(true);
            updateFwRx(true);
            if (!wasReceived) {
                if (mFwSupportsConfiguration) {
                    if (params.hwType == HW_TYPE_VESC && mSettings.value("showFwUpdateAvailable", true).toBool()) {
                        emit messageDialog(tr("Firmware Update Available"),
                                           tr("The connected VESC-based ESC has old, but mostly compatible firmware. This is "
                                              "fine if your setup works properly.<br><br>"
                                              "Check out the firmware changelog (from the help menu or firmware page) to decide "
                                              "if you want to use some of the new features that have been added after your "
                                              "firmware version was released. Keep in mind that you only should upgrade firmware "
                                              "if you have time to test it after the upgrade and carefully make sure that "
                                              "everything works as expected.<br><br>"
                                              "This message can be disabled from the settings."),
                                           false, false);
                    }
                } else {
                    if (params.hwType == HW_TYPE_VESC) {
                        emit messageDialog(tr("Warning"), tr("The connected VESC-based ESC has too old firmware. Since the "
                                                             "connected VESC has firmware with bootloader support, it can be "
                                                             "updated from the Firmware page. "
                                                             "Until then, limited communication mode will be used."), false, false);
                    }
                }
            }
        } else {
            updateFwRx(false);
            mFwRetries = 0;
            disconnectPort();
            if (!wasReceived) {
                emit messageDialog(tr("Error"), tr("The firmware on the connected device is too old. Please "
                                                   "update it using a programmer."), false, false);
            }
        }
    } else {
        updateFwRx(true);
        if (fw_connected < highest_supported) {
            if (!wasReceived) {
                emit messageDialog(tr("Warning"), tr("The connected device has compatible, but old "
                                                    "firmware. It is recommended that you update it."), false, false);
            }
        }

        QString fwStr = QString("VESC Firmware Version %1.%2").arg(params.major).arg(params.minor);
        if (!params.hw.isEmpty()) {
            fwStr += ", Hardware: " + params.hw;
        }

        if (!uuidStr.isEmpty()) {
            fwStr += ", UUID: " + uuidStr;
        }

        emit statusMessage(fwStr, true);
    }

    if (params.major >= 0) {
        mFwTxt = QString("Fw: %1.%2").arg(params.major).arg(params.minor);
        mFwPair = qMakePair(params.major, params.minor);
        mHwTxt = params.hw;
        if (!params.hw.isEmpty()) {
            mFwTxt += ", Hw: " + params.hw;
        }

        if (!uuidStr.isEmpty()) {
            mFwTxt += "\n" + uuidStr;
        }
    }

    // Check for known issues in firmware
    QString fwParam = QString("fw_%1.%2").arg(params.major).arg(params.minor);
    if (mFwConfig->hasParam(fwParam)) {
        auto fwInfoCfg = mFwConfig->getParam(fwParam);
        if (fwInfoCfg) {
            emitMessageDialog("Firmware Known Issues", fwInfoCfg->description, false, true);
        }
    }

    if (params.isTestFw > 0 && !VT_IS_TEST_VERSION) {
        emitMessageDialog("Test Firmware",
                          "The connected VESC-based device has test firmware and this is not a test build of VESC Tool. "
                          "You should update the firmware urgently, this may not be a safe situation.",
                          false, false);
    }

    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString confCacheDir;
    if (params.hwConfCrc > 0) {
        confCacheDir = appDataLoc + "/hw_cache/" + QString::number(params.hwConfCrc);
        QDir().mkpath(confCacheDir);
    }

    while (!mCustomConfigs.isEmpty()) {
        mCustomConfigs.last()->deleteLater();
        mCustomConfigs.removeLast();
    }

    // Read custom configs
#if !defined(Q_OS_WASM) && !defined(__EMSCRIPTEN__)
    if (!mIgnoreCustomConfigs && params.customConfigNum > 0) {
        bool readConfigsOk = true;
        for (int i = 0;i < params.customConfigNum;i++) {
            QString confCacheFile;
            if (!confCacheDir.isEmpty()) {
                confCacheFile = confCacheDir + "/conf_custom_" + QString::number(i) + ".bin";
            }

            if (!confCacheFile.isEmpty()) {
                QFile f(confCacheFile);
                if (f.exists() && f.open(QIODevice::ReadOnly)) {
                    mCustomConfigs.append(new ConfigParams(this));
                    connect(mCustomConfigs.last(), &ConfigParams::updateRequested, [this]() {
                        mCommands->customConfigGet(mCustomConfigs.size() - 1, false);
                    });
                    connect(mCustomConfigs.last(), &ConfigParams::updateRequestDefault, [this]() {
                        mCommands->customConfigGet(mCustomConfigs.size() - 1, true);
                    });

                    auto confData = f.readAll();
                    f.close();

                    if (mCustomConfigs.last()->loadCompressedParamsXml(confData)) {
                        emitStatusMessage(QString("Got cached %1").arg(mCustomConfigs.last()->getLongName("hw_name")), true);
                        continue;
                    } else {
                        mCustomConfigs.last()->deleteLater();
                        mCustomConfigs.removeLast();
                    }
                }
            }

            QByteArray configData;
            int confIndLast = 0;
            int lenConfLast = -1;
            auto conn = connect(mCommands, &Commands::customConfigChunkRx,
                    [&](int confInd, int lenConf, int ofsConf, QByteArray data) {
                if (configData.size() <= ofsConf) {
                    configData.append(data);
                }
                confIndLast = confInd;
                lenConfLast = lenConf;
            });

            auto getChunk = [&](int size, int offset, int tries, int timeout) {
                bool res = false;

                for (int j = 0;j < tries;j++) {
                    mCommands->customConfigGetChunk(i, size, offset);
                    res = Utility::waitSignal(mCommands, &Commands::customConfigChunkRx, timeout);
                    if (res) {
                        break;
                    }
                }
                return res;
            };

            if (getChunk(10, 0, 5, 1500)) {
                while (configData.size() < lenConfLast) {
                    int dataLeft = lenConfLast - configData.size();
                    if (!getChunk(dataLeft > 400 ? 400 : dataLeft, configData.size(), 5, 1500)) {
                        break;
                    }
                }

                if (configData.size() == lenConfLast) {
                    mCustomConfigs.append(new ConfigParams(this));
                    connect(mCustomConfigs.last(), &ConfigParams::updateRequested, [this]() {
                        mCommands->customConfigGet(mCustomConfigs.size() - 1, false);
                    });
                    connect(mCustomConfigs.last(), &ConfigParams::updateRequestDefault, [this]() {
                        mCommands->customConfigGet(mCustomConfigs.size() - 1, true);
                    });

                    if (!mCustomConfigs.last()->loadCompressedParamsXml(configData)) {
                        readConfigsOk = false;
                        disconnect(conn);
                        break;
                    }

                    emitStatusMessage(QString("Got %1").arg(mCustomConfigs.last()->getLongName("hw_name")), true);

                    if (!confCacheFile.isEmpty()) {
                        QFile f(confCacheFile);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(configData);
                            f.close();
                            emitStatusMessage(QString("Cached %1").arg(confCacheFile), true);
                        }
                    }
                } else {
                    emitMessageDialog("Get Custom Config",
                                      "Could not read custom config from hardware",
                                      false, false);
                    readConfigsOk = false;
                    disconnect(conn);
                    break;
                }
            }

            disconnect(conn);
        }

        mCustomConfigsLoaded = readConfigsOk;
    }
#endif

    // Read qmlui HW
#if !defined(Q_OS_WASM) && !defined(__EMSCRIPTEN__)
    if (mLoadQmlUiOnConnect && params.hasQmlHw) {
        bool cacheLoadOk = false;

        QString confCacheFile;
        if (!confCacheDir.isEmpty()) {
            confCacheFile = confCacheDir + "/qml_hw.bin";
        }

        if (!confCacheFile.isEmpty()) {
            QFile f(confCacheFile);
            if (f.exists() && f.open(QIODevice::ReadOnly)) {
                auto qmlData = f.readAll();
                f.close();

                mQmlHw = QString::fromUtf8(qUncompress(qmlData));
                mQmlHwLoaded = true;
                emitStatusMessage("Got cached qmlui HW", true);
                cacheLoadOk = true;
            }
        }

        if (!cacheLoadOk) {
            QByteArray qmlData;
            int lenQmlLast = -1;
            auto conn = connect(mCommands, &Commands::qmluiHwRx,
                                [&](int lenQml, int ofsQml, QByteArray data) {
                if (qmlData.size() <= ofsQml) {
                    qmlData.append(data);
                }
                lenQmlLast = lenQml;
            });

            auto getQmlChunk = [&](int size, int offset, int tries, int timeout) {
                bool res = false;

                for (int j = 0;j < tries;j++) {
                    mCommands->qmlUiHwGet(size, offset);
                    res = Utility::waitSignal(mCommands, &Commands::qmluiHwRx, timeout);
                    if (res) {
                        break;
                    }
                }
                return res;
            };

            if (getQmlChunk(10, 0, 5, 1500)) {
                while (qmlData.size() < lenQmlLast) {
                    int dataLeft = lenQmlLast - qmlData.size();
                    if (!getQmlChunk(dataLeft > 400 ? 400 : dataLeft, qmlData.size(), 5, 1500)) {
                        break;
                    }
                }

                if (qmlData.size() == lenQmlLast) {
                    mQmlHw = QString::fromUtf8(qUncompress(qmlData));
                    mQmlHwLoaded = true;
                    emitStatusMessage("Got qmlui HW", true);

                    if (!confCacheFile.isEmpty()) {
                        QFile f(confCacheFile);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(qmlData);
                            f.close();
                            emitStatusMessage(QString("Cached %1").arg(confCacheFile), true);
                        }
                    }
                } else {
                    mQmlHwLoaded = false;
                    emitMessageDialog("Get qmlui HW",
                                      "Could not read qmlui HW from hardware",
                                      false, false);
                    disconnect(conn);
                }
            }

            disconnect(conn);
        }
    }
#endif

    // Read qmlui APP
#if !defined(Q_OS_WASM) && !defined(__EMSCRIPTEN__)
    if (mLoadQmlUiOnConnect && params.hasQmlApp) {
        bool cacheLoadOk = false;

        QString confCacheFile;
        if (!confCacheDir.isEmpty()) {
            confCacheFile = confCacheDir + "/qml_app.bin";
        }

        if (!confCacheFile.isEmpty()) {
            QFile f(confCacheFile);
            if (f.exists() && f.open(QIODevice::ReadOnly)) {
                auto qmlData = f.readAll();
                f.close();

                mQmlApp = QString::fromUtf8(qUncompress(qmlData));
                mQmlAppLoaded = true;
                emitStatusMessage("Got cached qmlui App", true);
                cacheLoadOk = true;
            }
        }

        if (!cacheLoadOk) {
            QByteArray qmlData;
            int lenQmlLast = -1;
            auto conn = connect(mCommands, &Commands::qmluiAppRx,
                                [&](int lenQml, int ofsQml, QByteArray data) {
                if (qmlData.size() <= ofsQml) {
                    qmlData.append(data);
                }
                lenQmlLast = lenQml;
            });

            auto getQmlChunk = [&](int size, int offset, int tries, int timeout) {
                bool res = false;

                for (int j = 0;j < tries;j++) {
                    mCommands->qmlUiAppGet(size, offset);
                    res = Utility::waitSignal(mCommands, &Commands::qmluiAppRx, timeout);
                    if (res) {
                        break;
                    }
                }
                return res;
            };

            if (getQmlChunk(10, 0, 5, 1500)) {
                while (qmlData.size() < lenQmlLast) {
                    int dataLeft = lenQmlLast - qmlData.size();
                    if (!getQmlChunk(dataLeft > 400 ? 400 : dataLeft, qmlData.size(), 5, 1500)) {
                        break;
                    }
                }

                if (qmlData.size() == lenQmlLast) {
                    mQmlApp = QString::fromUtf8(qUncompress(qmlData));
                    mQmlAppLoaded = true;
                    emitStatusMessage("Got qmlui App", true);

                    if (!confCacheFile.isEmpty()) {
                        QFile f(confCacheFile);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(qmlData);
                            f.close();
                            emitStatusMessage(QString("Cached %1").arg(confCacheFile), true);
                        }
                    }
                } else {
                    mQmlAppLoaded = false;
                    emitMessageDialog("Get qmlui App",
                                      "Could not read qmlui App from hardware",
                                      false, false);
                    disconnect(conn);
                }
            }

            disconnect(conn);
        }
    }

    if (params.hasQmlApp || params.hasQmlHw) {
        emit qmlLoadDone();
    }
#else
    if (!mIgnoreCustomConfigs && params.customConfigNum > 0) {
        startCustomConfigAsyncLoad(params, confCacheDir);
    } else {
        if (mLoadQmlUiOnConnect && (params.hasQmlApp || params.hasQmlHw)) {
            startQmlUiAsyncLoad(params, confCacheDir);
        } else if (params.hasQmlApp || params.hasQmlHw) {
            emit qmlLoadDone();
        }
    }
    return;
#endif

    for (int i = 0;i < mCustomConfigs.size();i++) {
        commands()->customConfigGet(i, false);
    }

    mCustomConfigRxDone = true;
    emit customConfigLoadDone();
}

void VescInterface::appconfUpdated()
{
    emit statusMessage(tr("App config updated"), true);
}

void VescInterface::mcconfUpdated()
{
    emit statusMessage(tr("Motor config updated"), true);

    if (isPortConnected() && fwRx()) {
        QPair<int, int> fw_connected = qMakePair(mLastFwParams.major, mLastFwParams.minor);

        if (fw_connected >= qMakePair(5, 03)) {
            if (mMcConfig->getConfigVersion() != VT_CONFIG_VERSION) {
                emitMessageDialog("Configuration Loaded",
                                  "The loaded motor configuration file is from a different firmware and/or different "
                                  "version of VESC Tool. If it does not work properly you should run the motor wizard "
                                  "again or re-measure the parameters manually.\n\n"
                                  ""
                                  "When updating firmware it is always best to reset to the default configuration and "
                                  "run the detection and/or wizards again.",
                                  false);
            }
        }
    }
}

void VescInterface::ackReceived(QString ackType)
{
    emit statusMessage(ackType, true);
}

void VescInterface::customConfigRx(int confId, QByteArray data)
{
    ConfigParams *params = customConfig(confId);
    if (params) {
        auto vb = VByteArray(data);
        if (params->deSerialize(vb)) {
            params->updateDone();
            emitStatusMessage(tr("%1 updated").arg(params->getLongName("hw_name")), true);
        } else {
            emitMessageDialog(tr("Custom Configuration"),
                              tr("Could not deserialize custom config %1").arg(confId),
                              false, false);
        }
    }
}

bool VescInterface::isBlockFwSwap() const
{
    return mBlockFwSwap;
}

void VescInterface::setBlockFwSwap(bool newBlockFwSwap)
{
    mBlockFwSwap = newBlockFwSwap;
}

void VescInterface::disconnectFwVersionReceived()
{
    disconnect(mCommands, &Commands::fwVersionReceived, this, &VescInterface::fwVersionReceived);
}

void VescInterface::reconnectFwVersionReceived()
{
    connect(mCommands, &Commands::fwVersionReceived, this, &VescInterface::fwVersionReceived);
}

bool VescInterface::showFwUpdateAvailable() const
{
    return mSettings.value("showFwUpdateAvailable", true).toBool();
}

void VescInterface::setShowFwUpdateAvailable(bool set)
{
    mSettings.setValue("showFwUpdateAvailable", set);
}

bool VescInterface::ignoreCustomConfigs() const
{
    return mIgnoreCustomConfigs;
}

void VescInterface::setIgnoreCustomConfigs(bool newIgnoreCustomConfigs)
{
    mIgnoreCustomConfigs = newIgnoreCustomConfigs;
}

bool VescInterface::ignoreTestVersion() const
{
    return mIgnoreTestVersion;
}

void VescInterface::setIgnoreTestVersion(bool ignore)
{
    mIgnoreTestVersion = ignore;
}

bool VescInterface::reconnectLastCan()
{
    return mSettings.value("reconnectLastCan", true).toBool();
}

void VescInterface::setReconnectLastCan(bool set)
{
    mSettings.setValue("reconnectLastCan", set);
}

bool VescInterface::scanCanOnConnect()
{
    return mSettings.value("scanCanOnConnect", true).toBool();
}

void VescInterface::setScanCanOnConnect(bool set)
{
    mSettings.setValue("scanCanOnConnect", set);
}

int VescInterface::getLastTcpHubPort() const
{
    return mLastTcpHubPort;
}

QVariantList VescInterface::getTcpHubDevs()
{
    return mTcpHubDevs;
}

void VescInterface::clearTcpHubDevs()
{
    mTcpHubDevs.clear();
}

bool VescInterface::updateTcpHubPassword(QString uuid, QString newPass)
{
    for (int i = 0;i < mTcpHubDevs.size();i++) {
        auto dev = mTcpHubDevs.at(i).value<TCP_HUB_DEVICE>();
        if (dev.uuid() == uuid) {
            dev.password = newPass;
            mTcpHubDevs[i] = QVariant::fromValue(dev);
            return true;
        }
    }

    return false;
}

bool VescInterface::connectTcpHubUuid(QString uuid)
{
    for (const auto &i: std::as_const(mTcpHubDevs)) {
        auto dev = i.value<TCP_HUB_DEVICE>();
        if (dev.uuid() == uuid) {
            connectTcpHub(dev.server, dev.port, dev.id, dev.password);
            return true;
        }
    }

    return false;
}

bool VescInterface::reloadFirmwareResources()
{
    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString fwStr = QString::number(VT_VERSION, 'f', 2);

    QString pathLatestFw = appDataLoc + "/res_fw_" + fwStr + ".rcc";
    QString pathEsp32Fw = appDataLoc + "/res_fw_esp32.rcc";
    QString pathArchiveFw = appDataLoc + "/res_fw.rcc";
    QString pathConfigs = appDataLoc + "/res_config.rcc";

    bool anyRegistered = false;

    if (QFileInfo::exists(pathLatestFw)) {
        QResource::unregisterResource(pathLatestFw);
        if (QResource::registerResource(pathLatestFw)) {
            anyRegistered = true;
            qDebug() << "[FW_RES] Registered latest firmware resource:" << pathLatestFw;
        }
    }

    if (QFileInfo::exists(pathEsp32Fw)) {
        QResource::unregisterResource(pathEsp32Fw);
        if (QResource::registerResource(pathEsp32Fw)) {
            anyRegistered = true;
            qDebug() << "[FW_RES] Registered ESP32 / Express firmware resource:" << pathEsp32Fw;
        }
    }

    if (QFileInfo::exists(pathArchiveFw)) {
        QResource::unregisterResource(pathArchiveFw);
        if (QResource::registerResource(pathArchiveFw)) {
            anyRegistered = true;
            qDebug() << "[FW_RES] Registered archive firmware resource:" << pathArchiveFw;
        }
    }

    if (QFileInfo::exists(pathConfigs)) {
        QResource::unregisterResource(pathConfigs);
        if (QResource::registerResource(pathConfigs)) {
            anyRegistered = true;
            qDebug() << "[FW_RES] Registered config resource:" << pathConfigs;
        }
    }

    return anyRegistered;
}

bool VescInterface::downloadFwArchive()
{
    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir(appDataLoc).exists()) {
        QDir().mkpath(appDataLoc);
    }
    QString path = appDataLoc + "/res_fw.rcc";
    QResource::unregisterResource(path);

    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::WriteOnly)) {
        emit fwArchiveDlProgress("Could not open local file", 0.0);
        emit fwArchiveDownloaded(false);
        return false;
    }

    QFile *filePtr = file.get();

    runTree(Group{NetworkReplyTaskItem([this, filePtr](NetworkReplyTask &task) {
        task.setUrl(QUrl("http://home.vedder.se/vesc_fw_archive/res_fw.rcc"));
        task.setOutputDevice(filePtr);
        task.setProgressCallback([this, filePtr](qint64 bytesReceived, qint64 bytesTotal) {
            emit fwArchiveDlProgress("Downloading " + QFileInfo(*filePtr).fileName(),
                                     (double)bytesReceived / (double)bytesTotal);
        });
        return SetupResult::Continue;
    }, [this, file, path](const NetworkReplyTask &task, DoneWith doneWith) {
        file->close();
        bool success = (doneWith == DoneWith::Success);
        if (success) {
            emit fwArchiveDlProgress("Download Done", 1.0);
            reloadFirmwareResources();
            syncFsToIndexedDb();
        } else {
            emit fwArchiveDlProgress("Download Failed", 0.0);
        }
        emit fwArchiveDownloaded(success);
        return DoneResult::Success;
    })});

    return true;
}

bool VescInterface::downloadFwLatest()
{
    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir(appDataLoc).exists()) {
        QDir().mkpath(appDataLoc);
    }

    QString fwStr = QString::number(VT_VERSION, 'f', 2);
    QUrl url1(QString("http://home.vedder.se/vesc_fw_archive/res_fw_") + fwStr + ".rcc");
    QString path1 = appDataLoc + "/res_fw_" + fwStr + ".rcc";

    QUrl url2("http://home.vedder.se/vesc_fw_archive/res_fw_esp32.rcc");
    QString path2 = appDataLoc + "/res_fw_esp32.rcc";

    QResource::unregisterResource(path1);
    QResource::unregisterResource(path2);

    auto file1 = std::make_shared<QFile>(path1);
    auto file2 = std::make_shared<QFile>(path2);

    if (!file1->open(QIODevice::WriteOnly) || !file2->open(QIODevice::WriteOnly)) {
        emit fwArchiveDlProgress("Could not open local file", 0.0);
        emit fwArchiveDownloaded(false);
        return false;
    }

    QFile *filePtr1 = file1.get();
    QFile *filePtr2 = file2.get();

    auto state = std::make_shared<QPair<int, bool>>(0, false);

    auto onOneDone = [this, file1, path1, file2, path2, state](bool ok) {
        state->first++;
        if (ok) state->second = true;
        if (state->first == 2) {
            if (state->second) {
                emit fwArchiveDlProgress("Download Done", 1.0);
                reloadFirmwareResources();
                syncFsToIndexedDb();
            } else {
                emit fwArchiveDlProgress("Download Failed", 0.0);
            }
            emit fwArchiveDownloaded(state->second);
        }
    };

    runTree(Group{NetworkReplyTaskItem([this, filePtr1, url1](NetworkReplyTask &task) {
        task.setUrl(url1);
        task.setOutputDevice(filePtr1);
        task.setProgressCallback([this, filePtr1](qint64 bytesReceived, qint64 bytesTotal) {
            emit fwArchiveDlProgress("Downloading " + QFileInfo(*filePtr1).fileName(),
                                     (double)bytesReceived / (double)bytesTotal);
        });
        return SetupResult::Continue;
    }, [file1, path1, onOneDone](const NetworkReplyTask &task, DoneWith doneWith) {
        file1->close();
        bool ok = (doneWith == DoneWith::Success);
        if (ok) QResource::registerResource(path1);
        onOneDone(ok);
        return DoneResult::Success;
    })});

    runTree(Group{NetworkReplyTaskItem([this, filePtr2, url2](NetworkReplyTask &task) {
        task.setUrl(url2);
        task.setOutputDevice(filePtr2);
        task.setProgressCallback([this, filePtr2](qint64 bytesReceived, qint64 bytesTotal) {
            emit fwArchiveDlProgress("Downloading " + QFileInfo(*filePtr2).fileName(),
                                     (double)bytesReceived / (double)bytesTotal);
        });
        return SetupResult::Continue;
    }, [file2, path2, onOneDone](const NetworkReplyTask &task, DoneWith doneWith) {
        file2->close();
        bool ok = (doneWith == DoneWith::Success);
        if (ok) QResource::registerResource(path2);
        onOneDone(ok);
        return DoneResult::Success;
    })});

    return true;
}

bool VescInterface::downloadConfigs()
{
    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if(!QDir(appDataLoc).exists()) {
        QDir().mkpath(appDataLoc);
    }
    QString path = appDataLoc + "/res_config.rcc";
    QResource::unregisterResource(path);

    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::WriteOnly)) {
        emit fwArchiveDlProgress("Could not open local file", 0.0);
        emit configsDownloaded(false);
        return false;
    }

    QFile *filePtr = file.get();

    runTree(Group{NetworkReplyTaskItem([this, filePtr](NetworkReplyTask &task) {
        task.setUrl(QUrl("http://home.vedder.se/vesc_fw_archive/res_config.rcc"));
        task.setOutputDevice(filePtr);
        task.setProgressCallback([this](qint64 bytesReceived, qint64 bytesTotal) {
            emit fwArchiveDlProgress("Downloading...", (double)bytesReceived / (double)bytesTotal);
        });
        return SetupResult::Continue;
    }, [this, file, path](const NetworkReplyTask &task, DoneWith doneWith) {
        file->close();
        bool success = (doneWith == DoneWith::Success);
        if (success) {
            emit fwArchiveDlProgress("Download Done", 1.0);
            reloadFirmwareResources();
            syncFsToIndexedDb();
            emit configsDownloaded(true);
        } else {
            emit fwArchiveDlProgress("Download Failed", 0.0);
            emit configsDownloaded(false);
        }
        return DoneResult::Success;
    })});

    return true;
}

QString VescInterface::getLastTcpHubServer() const
{
    return mLastTcpHubServer;
}

QString VescInterface::getLastTcpHubVescPass() const
{
    return mLastTcpHubVescPass;
}

QString VescInterface::getLastTcpHubVescID() const
{
    return mLastTcpHubVescID;
}

bool VescInterface::getFwSupportsConfiguration() const
{
    return mFwSupportsConfiguration;
}

bool VescInterface::confStoreBackup(bool can, QString name)
{
#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    (void)can;
    startWebBackup(mCommands->getSendCan() ? mCommands->getCanSendId() : -1, name);
    return true;
#else
    if (!isPortConnected()) {
        emitMessageDialog("Backup Configuration", "The VESC must be connected to perform this operation.", false, false);
        return false;
    }

    if (mLastFwParams.hwType != HW_TYPE_VESC) {
        emitMessageDialog("Backup Configuration", "This only works for motor controllers.", false, false);
        return false;
    }

    QStringList uuidsOk;

    auto storeConf = [this, &uuidsOk, &name]() {
        QString uuid;
        if (!Utility::configLoadCompatible(this, uuid)) {
            return false;
        }

        uuidsOk.append(uuid);

        ConfigParams *pMc = mcConfig();
        ConfigParams *pApp = appConfig();
        ConfigParams *pCustom = customConfig(0);

        commands()->getMcconf();
        bool rxMc = Utility::waitSignal(pMc, &ConfigParams::updated, 1500);
        commands()->getAppConf();
        bool rxApp = Utility::waitSignal(pApp, &ConfigParams::updated, 1500);

        bool rxCustom = false;
        if (pCustom != nullptr) {
            commands()->customConfigGet(0, false);
            rxCustom = Utility::waitSignal(pCustom, &ConfigParams::updated, 1500);
        }

        if (rxMc && rxApp) {
            CONFIG_BACKUP cfg;
            cfg.name = name;
            cfg.vesc_uuid = uuid;
            cfg.mcconf_xml_compressed = pMc->saveCompressed("mcconf");
            cfg.appconf_xml_compressed = pApp->saveCompressed("appconf");

            if (rxCustom) {
                cfg.customconf_xml_compressed = pCustom->saveCompressed("customconf");
            }

            mConfigurationBackups.insert(uuid, cfg);
            return true;
        } else {
            emitMessageDialog("Backup Configuration", "Reading configuration timed out.", false, false);
            return false;
        }
    };

    bool res = true;

    bool canLastFwd = commands()->getSendCan();
    int canLastId = commands()->getCanSendId();
    auto fwLast = getFirmwareNowPair();

    QVector<int> canDevs;

    if (can) {
        canDevs = scanCan();
        ignoreCanChange(true);
        commands()->setSendCan(false);
    }

    FW_RX_PARAMS fwp;
    Utility::getFwVersionBlocking(this, &fwp);

    if (fwp.hwType == HW_TYPE_VESC) {
        res = storeConf();
    }

    if (res && can) {
        for (const int &d : canDevs) {
            commands()->setSendCan(true, d);

            Utility::getFwVersionBlocking(this, &fwp);

            if (fwp.hwType == HW_TYPE_VESC) {
                res = storeConf();
            }

            if (!res) {
                break;
            }
        }
    }

    commands()->setSendCan(canLastFwd, canLastId);
    ignoreCanChange(false);
    if (!getSupportedFirmwarePairs().contains(fwLast)) {
        Utility::configLoad(this, fwLast.first, fwLast.second);
    }

    if (res) {
        storeSettings();
        emit configurationBackupsChanged();

        // Refresh configs
        commands()->getMcconf();
        commands()->getAppConf();
        if (customConfig(0) != nullptr) {
            commands()->customConfigGet(0, false);
        }

        QString uuidsStr;
        for (const auto &s : uuidsOk) {
            uuidsStr += s + "\n";
        }

        emitMessageDialog("Backup Configuration",
                          "Configuration backup successful for the following VESC UUIDs:\n" + uuidsStr,
                          true, false);
    }

    return res;
#endif
}

bool VescInterface::confRestoreBackup(bool can)
{
#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    (void)can;
    requestWebBackupList();
    return true;
#else
    if (!isPortConnected()) {
        emitMessageDialog("Restore Configuration", "The VESC must be connected to perform this operation.", false, false);
        return false;
    }

    if (mLastFwParams.hwType != HW_TYPE_VESC) {
        emitMessageDialog("Restore Configuration", "This only works for motor controllers.", false, false);
        return false;
    }

    QStringList missingConfigs;
    QStringList uuidsOk;

    auto restoreConf = [this, &missingConfigs, &uuidsOk]() {
        QString uuid;
        if (!Utility::configLoadCompatible(this, uuid)) {
            return false;
        }

        ConfigParams *pMc = mcConfig();
        ConfigParams *pApp = appConfig();
        ConfigParams *pCustom = customConfig(0);

        commands()->getMcconf();
        bool rxMc = Utility::waitSignal(pMc, &ConfigParams::updated, 2000);
        commands()->getAppConf();
        bool rxApp = Utility::waitSignal(pApp, &ConfigParams::updated, 2000);

        bool rxCustom = false;
        if (pCustom != nullptr) {
            commands()->customConfigGet(0, false);
            rxCustom = Utility::waitSignal(pCustom, &ConfigParams::updated, 2000);
        }

        if (rxMc && rxApp) {
            if (mConfigurationBackups.contains(uuid)) {
                pMc->loadCompressed(mConfigurationBackups[uuid].mcconf_xml_compressed, "mcconf");
                pApp->loadCompressed(mConfigurationBackups[uuid].appconf_xml_compressed, "appconf");

                if (rxCustom) {
                    pCustom->loadCompressed(mConfigurationBackups[uuid].customconf_xml_compressed, "customconf");
                }

                // Try a few times, as BLE seems to drop the response sometimes.
                bool txMc = false, txApp = false, txCustom = true;;
                for (int i = 0;i < 2;i++) {
                    commands()->setMcconf(false);
                    txMc = Utility::waitSignal(commands(), &Commands::ackReceived, 2000);
                    commands()->setAppConf();
                    txApp = Utility::waitSignal(commands(), &Commands::ackReceived, 2000);

                    if (rxCustom) {
                        commands()->customConfigSet(0, pCustom);
                        txCustom = Utility::waitSignal(commands(), &Commands::ackReceived, 2000);
                    }

                    if (txApp && txMc && txCustom) {
                        break;
                    }
                }

                uuidsOk.append(uuid);

                if (!txMc) {
                    emitMessageDialog("Restore Configuration",
                                      "No response when writing Motor config to " + uuid + ".", false, false);
                }

                if (!txApp) {
                    emitMessageDialog("Restore Configuration",
                                      "No response when writing App config to " + uuid + ".", false, false);
                }

                if (!txCustom) {
                    emitMessageDialog("Restore Configuration",
                                      "No response when writing " + pCustom->getLongName("hw_name") + " configuration to " + uuid + ".", false, false);
                }

                return txMc && txApp;
            } else {
                missingConfigs.append(uuid);
            }
            return true;
        } else {
            emitMessageDialog("Restore Configuration", "Reading configuration timed out.", false, false);
            return false;
        }
    };

    bool res = true;

    bool canLastFwd = commands()->getSendCan();
    int canLastId = commands()->getCanSendId();
    auto fwLast = getFirmwareNowPair();
    QVector<int> canDevs;

    if (can) {
        canDevs = scanCan();
        ignoreCanChange(true);
        commands()->setSendCan(false);
    }

    FW_RX_PARAMS fwp;
    Utility::getFwVersionBlocking(this, &fwp);

    if (fwp.hwType == HW_TYPE_VESC) {
        res = restoreConf();
    }

    if (res && can) {
        for (const int &d : canDevs) {
            commands()->setSendCan(true, d);

            Utility::getFwVersionBlocking(this, &fwp);

            if (fwp.hwType == HW_TYPE_VESC) {
                res = restoreConf();
            }

            if (!res) {
                break;
            }
        }
    }

    commands()->setSendCan(canLastFwd, canLastId);
    ignoreCanChange(false);
    if (!getSupportedFirmwarePairs().contains(fwLast)) {
        Utility::configLoad(this, fwLast.first, fwLast.second);
    }

    if (res) {
        storeSettings();
        emit configurationBackupsChanged();

        // Refresh configs
        commands()->getMcconf();
        commands()->getAppConf();
        if (customConfig(0) != nullptr) {
            commands()->customConfigGet(0, false);
        }

        if (!uuidsOk.isEmpty()) {
            QString uuidsStr;
            for (const auto &s : uuidsOk) {
                uuidsStr += s + "\n";
            }

            emitMessageDialog("Restore Configuration",
                              "Configuration restoration successful for the following VESC UUIDs:\n" + uuidsStr,
                              true, false);
        }

        if (!missingConfigs.empty()) {
            QString missing;
            for (const auto &s : missingConfigs) {
                missing += s + "\n";
            }

            emitMessageDialog("Restore Configurations",
                              "The following UUIDs did not have any backups:\n" + missing,
                              false, false);
        }
    }

    return res;
#endif
}

bool VescInterface::confLoadBackup(QString uuid)
{
    if (mConfigurationBackups.contains(uuid)) {
        mMcConfig->loadCompressed(mConfigurationBackups[uuid].mcconf_xml_compressed, "mcconf");
        mAppConfig->loadCompressed(mConfigurationBackups[uuid].appconf_xml_compressed, "appconf");
        return true;
    } else {
        return false;
    }
}

QStringList VescInterface::confListBackups()
{
    QStringList res;
    QHashIterator<QString, CONFIG_BACKUP> i(mConfigurationBackups);
    while (i.hasNext()) {
        i.next();
        res.append(i.key());
    }

    return res;
}

void VescInterface::confClearBackups()
{
    mConfigurationBackups.clear();
    storeSettings();
    emit configurationBackupsChanged();
}

QString VescInterface::confBackupName(QString uuid)
{
    QString res;
    if (mConfigurationBackups.contains(uuid)) {
        res = mConfigurationBackups[uuid].name;
    }
    return res;
}

bool VescInterface::hasWebFsBackupApi()
{
    return webfs_is_supported() != 0;
}

bool VescInterface::isDirectoryPickerSupported()
{
    return webfs_is_directory_picker_supported() != 0;
}

QString VescInterface::getSavedBackupFolderName()
{
    return QString::fromUtf8(webfs_get_saved_folder_name());
}

void VescInterface::selectBackupFolder()
{
    webfs_select_folder([](int success, const char *folderName, void *userData) {
        auto self = static_cast<VescInterface*>(userData);
        if (self) {
            QString name = QString::fromUtf8(folderName ? folderName : "");
            emit self->backupFolderSelected(name, success != 0);
            if (success) {
                emit self->statusMessage(tr("Selected backup folder: %1").arg(name), true);
            }
        }
    }, this);
}

void VescInterface::startWebBackup(int canId, QString customName)
{
    if (!isPortConnected()) {
        emit webBackupFinished(false, tr("Device not connected."), "");
        emitMessageDialog(tr("Backup Configuration"), tr("The VESC must be connected to perform a backup."), false, false);
        return;
    }

    int targetId = canId;
    if (targetId < 0) {
        if (mCommands->getSendCan()) {
            targetId = mCommands->getCanSendId();
        } else {
            targetId = -1;
        }
    }

    if (targetId >= 0) {
        mCommands->setSendCan(true, targetId);
    }

    QString devName = customName;
    if (devName.trimmed().isEmpty()) {
        if (targetId >= 0) {
            devName = QString("Thor (CAN %1)").arg(targetId);
        } else {
            devName = mLastFwParams.hw.isEmpty() ? "VESC" : mLastFwParams.hw;
        }
    }

    qDebug().noquote() << QString("[WEBFS_BACKUP] Starting async backup for device: %1 (CAN ID: %2)").arg(devName).arg(targetId);
    emit webBackupProgress(tr("Reading motor configuration from %1...").arg(devName), 0.20);

    struct BackupContext {
        int canId;
        QString devName;
        QString mcXml;
        QString appXml;
        QString customXml;
        QTimer *timeoutTimer;
        QMetaObject::Connection connMc;
        QMetaObject::Connection connApp;
        QMetaObject::Connection connCustom;
    };

    auto ctx = new BackupContext();
    ctx->canId = targetId;
    ctx->devName = devName;
    ctx->timeoutTimer = new QTimer(this);
    ctx->timeoutTimer->setSingleShot(true);

    auto cleanup = [this, ctx]() {
        QObject::disconnect(ctx->connMc);
        QObject::disconnect(ctx->connApp);
        QObject::disconnect(ctx->connCustom);
        ctx->timeoutTimer->stop();
        ctx->timeoutTimer->deleteLater();
        delete ctx;
    };

    auto stepSaveFiles = [this, ctx, cleanup]() {
        emit webBackupProgress(tr("Writing configuration files to folder..."), 0.85);

        QString cleanDev = ctx->devName;
        cleanDev.replace(" ", "_").replace("(", "").replace(")", "").replace("/", "_").replace("\\", "_");
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
        QString subfolder = QString("%1_%2").arg(cleanDev).arg(timestamp);

        QJsonObject filesObj;
        filesObj["mcconf.xml"] = ctx->mcXml;
        filesObj["appconf.xml"] = ctx->appXml;
        if (!ctx->customXml.isEmpty()) {
            filesObj["customconf.xml"] = ctx->customXml;
        }

        QJsonObject infoObj;
        infoObj["deviceName"] = ctx->devName;
        infoObj["canId"] = ctx->canId;
        infoObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        infoObj["dateFormatted"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        infoObj["hw"] = mLastFwParams.hw;
        infoObj["uuid"] = mUuidStr;
        infoObj["hasMc"] = !ctx->mcXml.isEmpty();
        infoObj["hasApp"] = !ctx->appXml.isEmpty();
        infoObj["hasCustom"] = !ctx->customXml.isEmpty();

        filesObj["backup_info.json"] = QString::fromUtf8(QJsonDocument(infoObj).toJson(QJsonDocument::Indented));

        QByteArray filesJson = QJsonDocument(filesObj).toJson(QJsonDocument::Compact);

        struct SaveCbData {
            VescInterface *self;
            QString folder;
        };
        auto cbData = new SaveCbData{this, subfolder};

        webfs_save_backup(subfolder.toUtf8().constData(), filesJson.constData(), [](int success, const char *msg, void *userData) {
            auto d = static_cast<SaveCbData*>(userData);
            if (d && d->self) {
                QString resMsg = QString::fromUtf8(msg ? msg : "");
                emit d->self->webBackupProgress(tr("Backup completed!"), 1.0);
                emit d->self->webBackupFinished(success != 0, resMsg, d->folder);
                if (success) {
                    emit d->self->statusMessage(tr("Backup saved to %1").arg(d->folder), true);
                } else {
                    emit d->self->statusMessage(tr("Backup failed: %1").arg(resMsg), false);
                }
            }
            delete d;
        }, cbData);

        cleanup();
    };

    auto stepGetCustom = [this, ctx, stepSaveFiles]() {
        QObject::disconnect(ctx->connApp);
        ctx->timeoutTimer->stop();

        if (customConfigNum() > 0 && customConfig(0) != nullptr) {
            emit webBackupProgress(tr("Reading custom configuration..."), 0.65);
            ctx->connCustom = QObject::connect(customConfig(0), &ConfigParams::updated, this, [this, ctx, stepSaveFiles]() {
                ctx->customXml = customConfig(0)->getXmlString("customconf");
                stepSaveFiles();
            });

            ctx->timeoutTimer->disconnect();
            QObject::connect(ctx->timeoutTimer, &QTimer::timeout, this, [ctx, stepSaveFiles]() {
                qWarning() << "[WEBFS_BACKUP] Custom config read timed out, proceeding with motor/app configs";
                stepSaveFiles();
            });
            ctx->timeoutTimer->start(3500);
            mCommands->customConfigGet(0, false);
        } else {
            stepSaveFiles();
        }
    };

    auto stepGetApp = [this, ctx, stepGetCustom]() {
        QObject::disconnect(ctx->connMc);
        ctx->timeoutTimer->stop();

        emit webBackupProgress(tr("Reading app configuration..."), 0.45);
        ctx->connApp = QObject::connect(mAppConfig, &ConfigParams::updated, this, [this, ctx, stepGetCustom]() {
            ctx->appXml = mAppConfig->getXmlString("appconf");
            stepGetCustom();
        });

        ctx->timeoutTimer->disconnect();
        QObject::connect(ctx->timeoutTimer, &QTimer::timeout, this, [this, ctx, stepGetCustom]() {
            qWarning() << "[WEBFS_BACKUP] App config read timed out, using cached app config";
            ctx->appXml = mAppConfig->getXmlString("appconf");
            stepGetCustom();
        });
        ctx->timeoutTimer->start(3500);
        mCommands->getAppConf();
    };

    ctx->connMc = QObject::connect(mMcConfig, &ConfigParams::updated, this, [this, ctx, stepGetApp]() {
        ctx->mcXml = mMcConfig->getXmlString("mcconf");
        stepGetApp();
    });

    QObject::connect(ctx->timeoutTimer, &QTimer::timeout, this, [this, ctx, stepGetApp]() {
        qWarning() << "[WEBFS_BACKUP] Motor config read timed out, using cached motor config";
        ctx->mcXml = mMcConfig->getXmlString("mcconf");
        stepGetApp();
    });
    ctx->timeoutTimer->start(3500);
    mCommands->getMcconfForce(true);
}

void VescInterface::requestWebBackupList()
{
    webfs_list_backups([](int success, const char *jsonList, void *userData) {
        auto self = static_cast<VescInterface*>(userData);
        if (self) {
            QVariantList resultList;
            if (success && jsonList) {
                QJsonDocument doc = QJsonDocument::fromJson(QByteArray(jsonList));
                if (doc.isArray()) {
                    QJsonArray arr = doc.array();
                    for (int i = 0; i < arr.size(); i++) {
                        resultList.append(arr.at(i).toVariant());
                    }
                }
            }
            emit self->webBackupListReady(resultList);
        }
    }, this);
}

void VescInterface::restoreFromWebBackup(QString subfolderName, int canId)
{
    if (!isPortConnected()) {
        emit webRestoreFinished(false, tr("Device not connected."));
        return;
    }

    if (canId >= 0) {
        mCommands->setSendCan(true, canId);
    }

    struct RestoreCbData {
        VescInterface *self;
        QString subfolder;
    };
    auto cbData = new RestoreCbData{this, subfolderName};

    webfs_read_backup(subfolderName.toUtf8().constData(), [](int success, const char *jsonData, void *userData) {
        auto d = static_cast<RestoreCbData*>(userData);
        if (!d || !d->self) {
            delete d;
            return;
        }

        auto self = d->self;
        QString subfolder = d->subfolder;
        delete d;

        if (!success || !jsonData) {
            emit self->webRestoreFinished(false, tr("Failed to read backup from folder: %1").arg(subfolder));
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(jsonData));
        if (!doc.isObject()) {
            emit self->webRestoreFinished(false, tr("Invalid backup data format."));
            return;
        }

        QJsonObject obj = doc.object();
        QString mcXml = obj.value("mcconf").toString();
        QString appXml = obj.value("appconf").toString();
        QString customXml = obj.value("customconf").toString();

        bool restoredAny = false;
        if (!mcXml.isEmpty()) {
            self->mcConfig()->loadXmlString(mcXml, "mcconf");
            self->commands()->setMcconf(false);
            restoredAny = true;
        }
        if (!appXml.isEmpty()) {
            self->appConfig()->loadXmlString(appXml, "appconf");
            self->commands()->setAppConf();
            restoredAny = true;
        }
        if (!customXml.isEmpty() && self->customConfig(0) != nullptr) {
            self->customConfig(0)->loadXmlString(customXml, "customconf");
            self->commands()->customConfigSet(0, self->customConfig(0));
            restoredAny = true;
        }

        if (restoredAny) {
            emit self->webRestoreFinished(true, tr("Configuration restored successfully from %1").arg(subfolder));
            emit self->statusMessage(tr("Restored backup: %1").arg(subfolder), true);
            QTimer::singleShot(500, self, [self]() {
                self->commands()->getMcconfForce(true);
                self->commands()->getAppConf();
            });
        } else {
            emit self->webRestoreFinished(false, tr("No configuration files found in backup."));
        }
    }, cbData);
}

bool VescInterface::isWasm() const
{
#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}

bool VescInterface::exportXml(ConfigParams *cfg, QString configName, QString defaultFileName)
{
    if (!cfg) return false;
    QString xml = cfg->getXmlString(configName);
    if (xml.isEmpty()) {
        emitStatusMessage(tr("Failed to generate XML"), false);
        return false;
    }

    if (defaultFileName.trimmed().isEmpty()) {
        if (configName.contains("MC", Qt::CaseInsensitive)) {
            defaultFileName = "mcconf.xml";
        } else if (configName.contains("APP", Qt::CaseInsensitive)) {
            defaultFileName = "appconf.xml";
        } else {
            defaultFileName = "customconf.xml";
        }
    }
    if (!defaultFileName.endsWith(".xml", Qt::CaseInsensitive)) {
        defaultFileName += ".xml";
    }

#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    webfs_download_file(defaultFileName.toUtf8().constData(), xml.toUtf8().constData(), "application/xml;charset=utf-8");
    emitStatusMessage(tr("Saved %1").arg(defaultFileName), true);
    return true;
#else
    return false;
#endif
}

void VescInterface::exportAllXmls(int canId, QString customName)
{
    if (canId >= 0) {
        mCommands->setSendCan(true, canId);
    }

    QString devName = customName.trimmed();
    if (devName.isEmpty()) {
        if (canId >= 0) {
            devName = QString("CAN%1").arg(canId);
        } else if (!mLastFwParams.hw.isEmpty()) {
            devName = mLastFwParams.hw;
        } else {
            devName = "VESC";
        }
    }
    devName.replace(" ", "_").replace("(", "").replace(")", "");

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    QString zipFilename = QString("vesc_backup_%1_%2.zip").arg(devName).arg(timestamp);

    QJsonObject filesMap;

    if (mMcConfig) {
        QString xml = mMcConfig->getXmlString("MCConfiguration");
        if (!xml.isEmpty()) {
            filesMap.insert("mcconf.xml", xml);
        }
    }

    if (mAppConfig) {
        QString xml = mAppConfig->getXmlString("APPConfiguration");
        if (!xml.isEmpty()) {
            filesMap.insert("appconf.xml", xml);
        }
    }

    if (customConfigNum() > 0 && customConfig(0) != nullptr) {
        QString hwName = customConfig(0)->getLongName("hw_name");
        QString customFn = (hwName.isEmpty() ? "refloat" : hwName.toLower().replace(QRegularExpression("[^a-z0-9]"), "_")) + "_customconf.xml";
        QString xml = customConfig(0)->getXmlString("CustomConfiguration");
        if (!xml.isEmpty()) {
            filesMap.insert(customFn, xml);
        }
    }

    QJsonObject infoObj;
    infoObj.insert("deviceName", devName);
    infoObj.insert("hardware", mLastFwParams.hw);
    infoObj.insert("firmware", QString("%1.%2").arg(mLastFwParams.major).arg(mLastFwParams.minor));
    infoObj.insert("uuid", mUuidStr);
    infoObj.insert("canId", canId);
    infoObj.insert("timestamp", timestamp);
    infoObj.insert("dateFormatted", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    QJsonDocument infoDoc(infoObj);
    filesMap.insert("backup_info.json", QString::fromUtf8(infoDoc.toJson(QJsonDocument::Indented)));

    QJsonDocument doc(filesMap);
    QByteArray filesJson = doc.toJson(QJsonDocument::Compact);

#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    webfs_download_zip_bundle(zipFilename.toUtf8().constData(), filesJson.constData());
    emitStatusMessage(tr("Exported XML ZIP bundle: %1").arg(zipFilename), true);
#else
    emitStatusMessage(tr("Exported XML configuration files"), true);
#endif
}

void VescInterface::exportBackupXmls(QString subfolderName)
{
#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    struct ExpCtx {
        VescInterface *self;
        QString subfolder;
    };
    auto ctx = new ExpCtx{this, subfolderName};

    webfs_read_backup(subfolderName.toUtf8().constData(), [](int success, const char *jsonData, void *userData) {
        auto d = static_cast<ExpCtx*>(userData);
        if (!d || !d->self) {
            delete d;
            return;
        }
        auto self = d->self;
        QString subfolder = d->subfolder;
        delete d;

        if (!success || !jsonData) {
            self->emitStatusMessage(tr("Failed to read backup for export"), false);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(jsonData));
        if (!doc.isObject()) return;
        QJsonObject obj = doc.object();

        QJsonObject filesMap;
        QString mcXml = obj.value("mcconf").toString();
        QString appXml = obj.value("appconf").toString();
        QString customXml = obj.value("customconf").toString();
        QString infoJson = obj.value("info").toString();

        if (!mcXml.isEmpty()) filesMap.insert("mcconf.xml", mcXml);
        if (!appXml.isEmpty()) filesMap.insert("appconf.xml", appXml);
        if (!customXml.isEmpty()) filesMap.insert("customconf.xml", customXml);
        if (!infoJson.isEmpty()) filesMap.insert("backup_info.json", infoJson);

        QJsonDocument outDoc(filesMap);
        QByteArray filesJson = outDoc.toJson(QJsonDocument::Compact);

        QString zipFilename = subfolder + ".zip";
        webfs_download_zip_bundle(zipFilename.toUtf8().constData(), filesJson.constData());
        self->emitStatusMessage(tr("Exported XML ZIP for %1").arg(subfolder), true);
    }, ctx);
#else
    (void)subfolderName;
#endif
}

void VescInterface::importXml(ConfigParams *cfg, QString configName)
{
    if (!cfg) return;

#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)
    struct LoadCtx {
        VescInterface *self;
        ConfigParams *cfg;
        QString configName;
    };
    auto ctx = new LoadCtx{this, cfg, configName};

    webfs_open_file_dialog(".xml,text/xml,application/xml", [](const char *filename, const char *content, void *userData) {
        auto d = static_cast<LoadCtx*>(userData);
        if (d && d->cfg && content) {
            QString xmlStr = QString::fromUtf8(content);
            QString fn = QString::fromUtf8(filename ? filename : "config.xml");
            bool ok = d->cfg->loadXmlString(xmlStr, d->configName);
            if (ok) {
                if (d->self) {
                    d->self->emitStatusMessage(tr("Loaded %1 successfully").arg(fn), true);
                    if (d->configName.contains("Custom", Qt::CaseInsensitive)) {
                        emit d->self->customConfigLoadDone();
                    }
                }
            } else {
                if (d->self) {
                    d->self->emitStatusMessage(tr("Failed to parse %1: %2").arg(fn).arg(d->cfg->xmlStatus()), false);
                }
            }
        }
        delete d;
    }, ctx);
#endif
}

bool VescInterface::deserializeFailedSinceConnected()
{
    return mDeserialFailedMessageShown;
}

FW_RX_PARAMS VescInterface::getLastFwRxParams()
{
    return mLastFwParams;
}

int VescInterface::customConfigNum()
{
    if (mCustomConfigsLoaded) {
        return mCustomConfigs.size();
    } else {
        return 0;
    }
}

bool VescInterface::customConfigsLoaded()
{
    return mCustomConfigsLoaded;
}

bool VescInterface::customConfigRxDone()
{
    return mCustomConfigRxDone;
}

ConfigParams *VescInterface::customConfig(int configNum)
{
    if (customConfigsLoaded() && configNum < mCustomConfigs.size()) {
        return mCustomConfigs[configNum];
    } else {
        return nullptr;
    }
}

bool VescInterface::qmlHwLoaded()
{
    return mQmlHwLoaded;
}

bool VescInterface::qmlAppLoaded()
{
    return mQmlAppLoaded;
}

bool VescInterface::qmlAsyncLoading()
{
    return m_qmlAsyncLoading;
}

bool VescInterface::customConfigAsyncLoading()
{
    return m_customConfigAsyncLoading;
}

QString VescInterface::adaptQmlToQt6(const QString &qml)
{
    if (qml.isEmpty()) {
        return qml;
    }

    QString res = qml;

    // 1. QtQuick.Dialogs:
    // Qt 5 packages used e.g. "import QtQuick.Dialogs 1.3 as Dl" or "import QtQuick.Dialogs 1.2"
    // In Qt 6, QtQuick.Dialogs is versionless or 6.x.
    static const QRegularExpression reDialogs(R"(\bimport\s+QtQuick\.Dialogs\s+1(?:\.\d+)?(\s+as\s+\w+)?)");
    res.replace(reDialogs, R"(import QtQuick.Dialogs\1)");

    // 2. QtGraphicalEffects:
    // In Qt 6, QtGraphicalEffects was moved to Qt5Compat.GraphicalEffects.
    static const QRegularExpression reEffects(R"(\bimport\s+QtGraphicalEffects(?:\s+1(?:\.\d+)?)?(\s+as\s+\w+)?)");
    res.replace(reEffects, R"(import Qt5Compat.GraphicalEffects\1)");

    // 3. QtQuick.Controls 1.x:
    // In Qt 6, Controls 1.x is removed; redirect to QtQuick.Controls.
    static const QRegularExpression reControls(R"(\bimport\s+QtQuick\.Controls\s+1(?:\.\d+)?(\s+as\s+\w+)?)");
    res.replace(reControls, R"(import QtQuick.Controls\1)");

    // 4. Vedder.vesc.*:
    // In Qt 6, all C++ types are registered under the module "Vedder.vesc".
    static const QRegularExpression reVedder(R"(\bimport\s+Vedder\.vesc\.\w+(?:\s+1(?:\.\d+)?)?(\s+as\s+\w+)?)");
    res.replace(reVedder, R"(import Vedder.vesc\1)");

    // 5. Qt.labs.settings:
    // In Qt 6, Settings was moved into QtCore ("import QtCore").
    static const QRegularExpression reSettings(R"(\bimport\s+Qt\.labs\.settings(?:\s+1(?:\.\d+)?)?(\s+as\s+\w+)?)");
    res.replace(reSettings, R"(import QtCore\1)");

    // 6. Qt.labs.* generic version stripping (e.g. Qt.labs.folderlistmodel 2.1)
    static const QRegularExpression reLabs(R"(\bimport\s+(Qt\.labs\.\w+)\s+\d+(?:\.\d+)?(\s+as\s+\w+)?)");
    res.replace(reLabs, R"(import \1\2)");

    // 7. FileDialog Qt 5 vs Qt 6 property differences:
    // In Qt 6 FileDialog, "selectExisting" was removed and replaced by "fileMode".
    static const QRegularExpression reSelectExistingTrue(R"(\bselectExisting\s*:\s*true\b)");
    res.replace(reSelectExistingTrue, R"(fileMode: FileDialog.OpenFile)");

    static const QRegularExpression reSelectExistingFalse(R"(\bselectExisting\s*:\s*false\b)");
    res.replace(reSelectExistingFalse, R"(fileMode: FileDialog.SaveFile)");

    static const QRegularExpression reSelectExistingGeneric(R"(\bselectExisting\s*:\s*\w+)");
    res.replace(reSelectExistingGeneric, R"(// selectExisting removed in Qt6)");

    static const QRegularExpression reSidebarVisible(R"(\bsidebarVisible\s*:\s*(?:true|false|\w+))");
    res.replace(reSidebarVisible, R"(// sidebarVisible removed in Qt6)");

    if (res != qml) {
        qDebug().noquote() << "[QML Adapter] Adapted legacy Qt5 QML imports for Qt6 compatibility.";
    }

    return res;
}

QString VescInterface::qmlHw()
{
    return mQmlHwLoaded ? adaptQmlToQt6(mQmlHw) : "";
}

QString VescInterface::qmlApp()
{
    return mQmlAppLoaded ? adaptQmlToQt6(mQmlApp) : "";
}

void VescInterface::updateFwRx(bool fwRx)
{
    bool change = mFwVersionReceived != fwRx;
    mFwVersionReceived = fwRx;

    if (!mFwVersionReceived) {
        mCustomConfigsLoaded = false;
        mCustomConfigRxDone = false;
        mQmlHwLoaded = false;
        mQmlAppLoaded = false;
        mQmlHw.clear();
        mQmlApp.clear();
        while (!mCustomConfigs.isEmpty()) {
            mCustomConfigs.last()->deleteLater();
            mCustomConfigs.removeLast();
        }
        if (m_qmlAsyncLoading) {
            m_qmlAsyncLoading = false;
            if (m_qmlTimeoutTimer) {
                m_qmlTimeoutTimer->stop();
            }
            m_qmlBuffer.clear();
        }
        if (m_customConfigAsyncLoading) {
            m_customConfigAsyncLoading = false;
            if (m_customConfigTimeoutTimer) {
                m_customConfigTimeoutTimer->stop();
            }
            m_customConfigBuffer.clear();
        }
    }

    if (change) {
        emit fwRxChanged(mFwVersionReceived, mCommands->isLimitedMode());
    }
}

void VescInterface::setLastConnectionType(conn_t type)
{
    mLastConnType = type;
    mSettings.setValue("connection_type", type);
}

void VescInterface::startQmlUiAsyncLoad(const FW_RX_PARAMS &params, const QString &confCacheDir)
{
    m_qmlParams = params;
    m_qmlCacheDir = confCacheDir;
    m_qmlAsyncLoading = true;
    m_qmlRetries = 0;
    m_qmlBuffer.clear();
    m_qmlTotalLen = -1;

    // Check caches first
    if (params.hasQmlHw && !confCacheDir.isEmpty()) {
        QFile f(confCacheDir + "/qml_hw.bin");
        if (f.exists() && f.open(QIODevice::ReadOnly)) {
            QByteArray data = f.readAll();
            f.close();
            mQmlHw = adaptQmlToQt6(QString::fromUtf8(qUncompress(data)));
            mQmlHwLoaded = !mQmlHw.isEmpty();
            if (mQmlHwLoaded) {
                emitStatusMessage("Got cached qmlui HW", true);
            }
        }
    }

    if (params.hasQmlApp && !confCacheDir.isEmpty()) {
        QFile f(confCacheDir + "/qml_app.bin");
        if (f.exists() && f.open(QIODevice::ReadOnly)) {
            QByteArray data = f.readAll();
            f.close();
            mQmlApp = adaptQmlToQt6(QString::fromUtf8(qUncompress(data)));
            mQmlAppLoaded = !mQmlApp.isEmpty();
            if (mQmlAppLoaded) {
                emitStatusMessage("Got cached qmlui App", true);
            }
        }
    }

    bool needHw = params.hasQmlHw && !mQmlHwLoaded;
    bool needApp = params.hasQmlApp && !mQmlAppLoaded;

    if (!needHw && !needApp) {
        m_qmlAsyncLoading = false;
        emit qmlLoadDone();
        return;
    }

    if (needHw) {
        m_qmlFetchingHw = true;
        emitStatusMessage("Requesting qmlui HW from controller...", true);
        mCommands->qmlUiHwGet(10, 0);
        if (m_qmlTimeoutTimer) {
            m_qmlTimeoutTimer->start(2500);
        }
    } else {
        m_qmlFetchingHw = false;
        emitStatusMessage("Requesting qmlui App from controller...", true);
        mCommands->qmlUiAppGet(10, 0);
        if (m_qmlTimeoutTimer) {
            m_qmlTimeoutTimer->start(2500);
        }
    }
}

void VescInterface::handleQmlUiChunk(bool isHw, int lenQml, int ofsQml, const QByteArray &data)
{
    if (!m_qmlAsyncLoading) {
        return;
    }

    if (isHw != m_qmlFetchingHw) {
        return;
    }

    if (m_qmlTimeoutTimer) {
        m_qmlTimeoutTimer->stop();
    }
    m_qmlRetries = 0;
    m_qmlTotalLen = lenQml;

    if (m_qmlTotalLen <= 0) {
        if (m_qmlFetchingHw && m_qmlParams.hasQmlApp && !mQmlAppLoaded) {
            m_qmlFetchingHw = false;
            m_qmlBuffer.clear();
            m_qmlTotalLen = -1;
            m_qmlRetries = 0;
            emitStatusMessage("Requesting qmlui App from controller...", true);
            mCommands->qmlUiAppGet(10, 0);
            if (m_qmlTimeoutTimer) {
                m_qmlTimeoutTimer->start(2500);
            }
        } else {
            m_qmlAsyncLoading = false;
            m_qmlBuffer.clear();
            emit qmlLoadDone();
        }
        return;
    }

    if (ofsQml <= m_qmlBuffer.size()) {
        if (ofsQml < m_qmlBuffer.size()) {
            m_qmlBuffer.truncate(ofsQml);
        }
        m_qmlBuffer.append(data);
    }

    QString targetName = m_qmlFetchingHw ? "HW QML" : "App QML";
    emitStatusMessage(QString("Loading %1 (%2 / %3 bytes)...")
                      .arg(targetName)
                      .arg(m_qmlBuffer.size())
                      .arg(m_qmlTotalLen), true);

    if (m_qmlBuffer.size() >= m_qmlTotalLen) {
        QByteArray uncompressed = qUncompress(m_qmlBuffer);
        if (!uncompressed.isEmpty()) {
            if (m_qmlFetchingHw) {
                mQmlHw = adaptQmlToQt6(QString::fromUtf8(uncompressed));
                mQmlHwLoaded = true;
                emitStatusMessage("Got qmlui HW", true);
                if (!m_qmlCacheDir.isEmpty()) {
                    QFile f(m_qmlCacheDir + "/qml_hw.bin");
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(m_qmlBuffer);
                        f.close();
                        emitStatusMessage(QString("Cached %1/qml_hw.bin").arg(m_qmlCacheDir), true);
                    }
                }
            } else {
                mQmlApp = adaptQmlToQt6(QString::fromUtf8(uncompressed));
                mQmlAppLoaded = true;
                emitStatusMessage("Got qmlui App", true);
                if (!m_qmlCacheDir.isEmpty()) {
                    QFile f(m_qmlCacheDir + "/qml_app.bin");
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(m_qmlBuffer);
                        f.close();
                        emitStatusMessage(QString("Cached %1/qml_app.bin").arg(m_qmlCacheDir), true);
                    }
                }
            }
        } else {
            emitStatusMessage(QString("Failed to uncompress %1").arg(targetName), false);
        }

        if (m_qmlFetchingHw && m_qmlParams.hasQmlApp && !mQmlAppLoaded) {
            m_qmlFetchingHw = false;
            m_qmlBuffer.clear();
            m_qmlTotalLen = -1;
            m_qmlRetries = 0;
            emitStatusMessage("Requesting qmlui App from controller...", true);
            mCommands->qmlUiAppGet(10, 0);
            if (m_qmlTimeoutTimer) {
                m_qmlTimeoutTimer->start(2500);
            }
        } else {
            m_qmlAsyncLoading = false;
            m_qmlBuffer.clear();
            emit qmlLoadDone();
        }
    } else {
        int dataLeft = m_qmlTotalLen - m_qmlBuffer.size();
        int chunkSize = dataLeft > 384 ? 384 : dataLeft;
        if (m_qmlFetchingHw) {
            mCommands->qmlUiHwGet(chunkSize, m_qmlBuffer.size());
        } else {
            mCommands->qmlUiAppGet(chunkSize, m_qmlBuffer.size());
        }
        if (m_qmlTimeoutTimer) {
            m_qmlTimeoutTimer->start(2500);
        }
    }
}

void VescInterface::handleQmlUiTimeout()
{
    if (!m_qmlAsyncLoading) {
        return;
    }

    m_qmlRetries++;
    if (m_qmlRetries <= 5) {
        int currentSize = m_qmlBuffer.size();
        int dataLeft = (m_qmlTotalLen > 0) ? (m_qmlTotalLen - currentSize) : 10;
        int chunkSize = dataLeft > 384 ? 384 : (dataLeft <= 0 ? 10 : dataLeft);

        if (m_qmlFetchingHw) {
            mCommands->qmlUiHwGet(chunkSize, currentSize);
        } else {
            mCommands->qmlUiAppGet(chunkSize, currentSize);
        }
        if (m_qmlTimeoutTimer) {
            m_qmlTimeoutTimer->start(2500);
        }
    } else {
        if (m_qmlTimeoutTimer) {
            m_qmlTimeoutTimer->stop();
        }
        QString targetName = m_qmlFetchingHw ? "qmlui HW" : "qmlui App";
        emitMessageDialog("Get " + targetName,
                          "Could not read " + targetName + " from hardware (timeout)",
                          false, false);

        if (m_qmlFetchingHw && m_qmlParams.hasQmlApp && !mQmlAppLoaded) {
            m_qmlFetchingHw = false;
            m_qmlBuffer.clear();
            m_qmlTotalLen = -1;
            m_qmlRetries = 0;
            emitStatusMessage("Requesting qmlui App from controller...", true);
            mCommands->qmlUiAppGet(10, 0);
            if (m_qmlTimeoutTimer) {
                m_qmlTimeoutTimer->start(2500);
            }
        } else {
            m_qmlAsyncLoading = false;
            m_qmlBuffer.clear();
            emit qmlLoadDone();
        }
    }
}

void VescInterface::startCustomConfigAsyncLoad(const FW_RX_PARAMS &params, const QString &confCacheDir)
{
    m_customConfigParams = params;
    m_customConfigCacheDir = confCacheDir;
    m_customConfigAsyncLoading = true;
    m_customConfigCurrentIdx = 0;
    m_customConfigRetries = 0;
    m_customConfigBuffer.clear();
    m_customConfigTotalLen = -1;

    qDebug().noquote() << QString("[CUSTOM_CFG] Starting async load for %1 custom configs...").arg(params.customConfigNum);

    while (!mCustomConfigs.isEmpty()) {
        mCustomConfigs.last()->deleteLater();
        mCustomConfigs.removeLast();
    }

    // Fast path: load as many as possible from cache first
    while (m_customConfigCurrentIdx < params.customConfigNum) {
        int i = m_customConfigCurrentIdx;
        QString confCacheFile;
        if (!confCacheDir.isEmpty()) {
            confCacheFile = confCacheDir + "/conf_custom_" + QString::number(i) + ".bin";
        }

        bool cached = false;
        if (!confCacheFile.isEmpty()) {
            QFile f(confCacheFile);
            if (f.exists() && f.open(QIODevice::ReadOnly)) {
                auto confData = f.readAll();
                f.close();

                ConfigParams *cfg = new ConfigParams(this);
                connect(cfg, &ConfigParams::updateRequested, [this, i]() {
                    mCommands->customConfigGet(i, false);
                });
                connect(cfg, &ConfigParams::updateRequestDefault, [this, i]() {
                    mCommands->customConfigGet(i, true);
                });

                if (cfg->loadCompressedParamsXml(confData)) {
                    mCustomConfigs.append(cfg);
                    emitStatusMessage(QString("Got cached %1").arg(cfg->getLongName("hw_name")), true);
                    qDebug().noquote() << QString("[CUSTOM_CFG] Loaded cached custom config %1: %2").arg(i).arg(cfg->getLongName("hw_name"));
                    m_customConfigCurrentIdx++;
                    cached = true;
                } else {
                    delete cfg;
                }
            }
        }

        if (!cached) {
            break;
        }
    }

    if (m_customConfigCurrentIdx >= params.customConfigNum) {
        qDebug().noquote() << "[CUSTOM_CFG] All custom configs loaded from cache.";
        m_customConfigAsyncLoading = false;
        mCustomConfigsLoaded = (mCustomConfigs.size() > 0);
        for (int i = 0; i < mCustomConfigs.size(); i++) {
            commands()->customConfigGet(i, false);
        }
        mCustomConfigRxDone = true;
        emit customConfigLoadDone();

        if (mLoadQmlUiOnConnect && (params.hasQmlApp || params.hasQmlHw)) {
            startQmlUiAsyncLoad(params, confCacheDir);
        } else if (params.hasQmlApp || params.hasQmlHw) {
            emit qmlLoadDone();
        }
        return;
    }

    int idx = m_customConfigCurrentIdx;
    emitStatusMessage(QString("Requesting custom config %1 from controller...").arg(idx), true);
    qDebug().noquote() << QString("[CUSTOM_CFG] Requesting initial chunk for custom config %1...").arg(idx);
    mCommands->customConfigGetChunk(idx, 10, 0);
    if (m_customConfigTimeoutTimer) {
        m_customConfigTimeoutTimer->start(2500);
    }
}

void VescInterface::handleCustomConfigChunk(int confInd, int lenConf, int ofsConf, const QByteArray &data)
{
    if (!m_customConfigAsyncLoading) {
        return;
    }

    if (confInd != m_customConfigCurrentIdx) {
        return;
    }

    if (m_customConfigTimeoutTimer) {
        m_customConfigTimeoutTimer->stop();
    }
    m_customConfigRetries = 0;
    m_customConfigTotalLen = lenConf;

    if (m_customConfigTotalLen <= 0) {
        qWarning().noquote() << QString("[CUSTOM_CFG] Custom config %1 reported total length %2, skipping.").arg(confInd).arg(lenConf);
        m_customConfigCurrentIdx++;
        m_customConfigBuffer.clear();
        m_customConfigTotalLen = -1;

        if (m_customConfigCurrentIdx < m_customConfigParams.customConfigNum) {
            emitStatusMessage(QString("Requesting custom config %1 from controller...").arg(m_customConfigCurrentIdx), true);
            mCommands->customConfigGetChunk(m_customConfigCurrentIdx, 10, 0);
            if (m_customConfigTimeoutTimer) {
                m_customConfigTimeoutTimer->start(2500);
            }
        } else {
            m_customConfigAsyncLoading = false;
            mCustomConfigsLoaded = (mCustomConfigs.size() > 0);
            for (int i = 0; i < mCustomConfigs.size(); i++) {
                commands()->customConfigGet(i, false);
            }
            mCustomConfigRxDone = true;
            emit customConfigLoadDone();
            qDebug().noquote() << QString("[CUSTOM_CFG] Finished async loading. Total custom configs: %1. Emitted customConfigLoadDone().").arg(mCustomConfigs.size());

            if (mLoadQmlUiOnConnect && (m_customConfigParams.hasQmlApp || m_customConfigParams.hasQmlHw)) {
                startQmlUiAsyncLoad(m_customConfigParams, m_customConfigCacheDir);
            } else if (m_customConfigParams.hasQmlApp || m_customConfigParams.hasQmlHw) {
                emit qmlLoadDone();
            }
        }
        return;
    }

    if (ofsConf <= m_customConfigBuffer.size()) {
        if (ofsConf < m_customConfigBuffer.size()) {
            m_customConfigBuffer.truncate(ofsConf);
        }
        m_customConfigBuffer.append(data);
    }

    emitStatusMessage(QString("Loading Custom Config %1 (%2 / %3 bytes)...")
                      .arg(confInd)
                      .arg(m_customConfigBuffer.size())
                      .arg(m_customConfigTotalLen), true);

    if (m_customConfigBuffer.size() >= m_customConfigTotalLen) {
        ConfigParams *cfg = new ConfigParams(this);
        int idx = m_customConfigCurrentIdx;
        connect(cfg, &ConfigParams::updateRequested, [this, idx]() {
            mCommands->customConfigGet(idx, false);
        });
        connect(cfg, &ConfigParams::updateRequestDefault, [this, idx]() {
            mCommands->customConfigGet(idx, true);
        });

        if (cfg->loadCompressedParamsXml(m_customConfigBuffer)) {
            mCustomConfigs.append(cfg);
            QString hwName = cfg->getLongName("hw_name");
            emitStatusMessage(QString("Got %1").arg(hwName), true);
            qDebug().noquote() << QString("[CUSTOM_CFG] Successfully loaded custom config %1: %2 (%3 params)")
                .arg(idx).arg(hwName).arg(cfg->getSerializeOrder().size());

            if (!m_customConfigCacheDir.isEmpty()) {
                QString confCacheFile = m_customConfigCacheDir + "/conf_custom_" + QString::number(idx) + ".bin";
                QFile f(confCacheFile);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(m_customConfigBuffer);
                    f.close();
                    emitStatusMessage(QString("Cached %1").arg(confCacheFile), true);
                }
            }
        } else {
            delete cfg;
            qWarning().noquote() << QString("[CUSTOM_CFG] Failed to decompress/load XML for custom config %1").arg(idx);
        }

        m_customConfigBuffer.clear();
        m_customConfigTotalLen = -1;
        m_customConfigCurrentIdx++;

        if (m_customConfigCurrentIdx < m_customConfigParams.customConfigNum) {
            emitStatusMessage(QString("Requesting custom config %1 from controller...").arg(m_customConfigCurrentIdx), true);
            mCommands->customConfigGetChunk(m_customConfigCurrentIdx, 10, 0);
            if (m_customConfigTimeoutTimer) {
                m_customConfigTimeoutTimer->start(2500);
            }
        } else {
            m_customConfigAsyncLoading = false;
            mCustomConfigsLoaded = (mCustomConfigs.size() > 0);
            for (int i = 0; i < mCustomConfigs.size(); i++) {
                commands()->customConfigGet(i, false);
            }
            mCustomConfigRxDone = true;
            emit customConfigLoadDone();
            qDebug().noquote() << QString("[CUSTOM_CFG] Finished async loading. Total custom configs: %1. Emitted customConfigLoadDone().").arg(mCustomConfigs.size());

            if (mLoadQmlUiOnConnect && (m_customConfigParams.hasQmlApp || m_customConfigParams.hasQmlHw)) {
                startQmlUiAsyncLoad(m_customConfigParams, m_customConfigCacheDir);
            } else if (m_customConfigParams.hasQmlApp || m_customConfigParams.hasQmlHw) {
                emit qmlLoadDone();
            }
        }
    } else {
        int dataLeft = m_customConfigTotalLen - m_customConfigBuffer.size();
        int chunkSize = dataLeft > 384 ? 384 : dataLeft;
        mCommands->customConfigGetChunk(confInd, chunkSize, m_customConfigBuffer.size());
        if (m_customConfigTimeoutTimer) {
            m_customConfigTimeoutTimer->start(2500);
        }
    }
}

void VescInterface::handleCustomConfigTimeout()
{
    if (!m_customConfigAsyncLoading) {
        return;
    }

    m_customConfigRetries++;
    if (m_customConfigRetries <= 5) {
        int currentSize = m_customConfigBuffer.size();
        int dataLeft = (m_customConfigTotalLen > 0) ? (m_customConfigTotalLen - currentSize) : 10;
        int chunkSize = dataLeft > 384 ? 384 : (dataLeft <= 0 ? 10 : dataLeft);

        qDebug().noquote() << QString("[CUSTOM_CFG] Timeout waiting for custom config %1, retry %2/5...").arg(m_customConfigCurrentIdx).arg(m_customConfigRetries);
        mCommands->customConfigGetChunk(m_customConfigCurrentIdx, chunkSize, currentSize);
        if (m_customConfigTimeoutTimer) {
            m_customConfigTimeoutTimer->start(2500);
        }
    } else {
        if (m_customConfigTimeoutTimer) {
            m_customConfigTimeoutTimer->stop();
        }
        qWarning().noquote() << QString("[CUSTOM_CFG] Failed to read custom config %1 after 5 retries.").arg(m_customConfigCurrentIdx);
        emitMessageDialog("Get Custom Config",
                          QString("Could not read custom config %1 from hardware (timeout)").arg(m_customConfigCurrentIdx),
                          false, false);

        m_customConfigBuffer.clear();
        m_customConfigTotalLen = -1;
        m_customConfigCurrentIdx++;

        if (m_customConfigCurrentIdx < m_customConfigParams.customConfigNum) {
            emitStatusMessage(QString("Requesting custom config %1 from controller...").arg(m_customConfigCurrentIdx), true);
            mCommands->customConfigGetChunk(m_customConfigCurrentIdx, 10, 0);
            if (m_customConfigTimeoutTimer) {
                m_customConfigTimeoutTimer->start(2500);
            }
        } else {
            m_customConfigAsyncLoading = false;
            mCustomConfigsLoaded = (mCustomConfigs.size() > 0);
            for (int i = 0; i < mCustomConfigs.size(); i++) {
                commands()->customConfigGet(i, false);
            }
            mCustomConfigRxDone = true;
            emit customConfigLoadDone();
            qDebug().noquote() << QString("[CUSTOM_CFG] Finished async loading. Total custom configs: %1. Emitted customConfigLoadDone().").arg(mCustomConfigs.size());

            if (mLoadQmlUiOnConnect && (m_customConfigParams.hasQmlApp || m_customConfigParams.hasQmlHw)) {
                startQmlUiAsyncLoad(m_customConfigParams, m_customConfigCacheDir);
            } else if (m_customConfigParams.hasQmlApp || m_customConfigParams.hasQmlHw) {
                emit qmlLoadDone();
            }
        }
    }
}
