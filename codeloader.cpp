/*
    Copyright 2022 - 2025 Benjamin Vedder	benjamin@vedder.se

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

#include "codeloader.h"
#include "qqmlcontext.h"
#include "utility.h"
#include "vesctasks.h"
#include <memory>
#include <QFileDialog>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QDirIterator>
#include <QStandardPaths>

CodeLoader::CodeLoader(QObject *parent) : QObject(parent)
{
    mVesc = nullptr;
    mAbortDownloadUpload = false;
    mQmlEngine = nullptr;

    m_serialFetchOngoing = false;
    m_serialPkgTotalSize = 0;
    m_serialPkgExpectedOffset = 0;
    m_serialPkgId = 0;
    m_serialPkgRetries = 0;
    m_serialFetchTimer = new QTimer(this);
    m_serialFetchTimer->setSingleShot(true);
    connect(m_serialFetchTimer, &QTimer::timeout, this, &CodeLoader::onSerialFetchTimeout);

    static bool resourceLoaded = false;
    if (!resourceLoaded) {
        if (loadPackageArchiveResource()) {
            qDebug() << "Loaded package archive resource";
        } else {
            qWarning() << "Could not load package archive resource. Please update the archive in order to use VESC packages.";
        }

        resourceLoaded = true;
    }
}

VescInterface *CodeLoader::vesc() const
{
    return mVesc;
}

void CodeLoader::setVesc(VescInterface *vesc)
{
    if (mVesc) {
        if (mVesc->commands()) {
            disconnect(mVesc->commands(), &Commands::customAppDataReceived, this, &CodeLoader::onCustomAppDataReceived);
            disconnect(mVesc->commands(), &Commands::qmluiAppRx, this, &CodeLoader::onQmluiAppRx);
        }
        disconnect(mVesc, &VescInterface::portConnectedChanged, this, &CodeLoader::onPortConnectedChanged);
    }

    mVesc = vesc;

    if (mVesc) {
        if (mVesc->commands()) {
            connect(mVesc->commands(), &Commands::customAppDataReceived, this, &CodeLoader::onCustomAppDataReceived);
            connect(mVesc->commands(), &Commands::qmluiAppRx, this, &CodeLoader::onQmluiAppRx);
        }
        connect(mVesc, &VescInterface::portConnectedChanged, this, &CodeLoader::onPortConnectedChanged);
    }
}

bool CodeLoader::lispErase(int size)
{
    if (!mVesc) {
        return false;
    }

    if (!mVesc->isPortConnected()) {
        mVesc->emitMessageDialog(tr("Erase old code"), tr("Not Connected"), false);
        return false;
    }

    auto waitEraseRes = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(8000);
            task.connectSignal(mVesc->commands(), &Commands::lispEraseCodeRx,
                               [&res](bool erRes) { res = erRes ? 1 : -1; });
            return SetupResult::Continue;
        })});
        return res;
    };

    mVesc->commands()->lispEraseCode(size);

    int erRes = waitEraseRes();
    if (erRes != 1) {
        QString msg = tr("Unknown failure");

        if (erRes == -10) {
            msg = tr("Erase timed out");
        } else if (erRes == -1) {
            msg = tr("Erasing LispBM Code failed");
        }

        mVesc->emitMessageDialog(tr("Erase LispBM"), msg, false);
        return false;
    }

    return true;
}

QString CodeLoader::reduceLispFile(QString fileData)
{
    QString res;
    auto lines = fileData.split("\n");

    for (auto line : lines) {
        bool insideString = false;
        int indComment = -1;

        // Find the earliest unquoted semicolon
        for (int i = 0; i < line.length(); ++i) {
            if (line[i] == '"') {
                insideString = !insideString;
            } else if (line[i] == ';' && !insideString) {
                indComment = i;
                break;
            }
        }

        if (indComment >= 0) {
            line.truncate(indComment);
        }

        while (line.startsWith(" ") || line.startsWith("\t")) {
            line.remove(0, 1);
        }

        while (line.endsWith(" ") || line.endsWith("\t")) {
            line.chop(1);
        }

        if (!line.startsWith("(import", Qt::CaseInsensitive) &&
            !line.isEmpty()) {
            res.append(line + "\n");
        }
    }

    // Print files before and after reduction
#if 0
    QDir().mkpath("lispReduceBefore");
    QDir().mkpath("lispReduceAfter");
    int fileNum = QDir("lispReduceBefore").
                  entryInfoList(QDir::NoFilter, QDir::Name).size();

    QFile fBefore(QString("lispReduceBefore/lispBeforeReduce%1.lbm").arg(fileNum));
    if (fBefore.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fBefore.write(fileData.toUtf8());
        fBefore.close();
    }

    QFile fAfter(QString("lispReduceAfter/lispAfterReduce%1.lbm").arg(fileNum));
    if (fAfter.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fAfter.write(res.toUtf8());
        fAfter.close();
    }
#endif

    return res;
}

QByteArray CodeLoader::lispPackImports(QString codeStr, QString editorPath, bool reduceLisp)
{
    VByteArray vb;
    vb.vbAppendUint16(0); // Flags: 0
    if (reduceLisp) {
        vb.append(reduceLispFile(codeStr).toLocal8Bit());
    } else {
        vb.append(codeStr.toLocal8Bit());
    }

    if (vb.at(vb.size() - 1) != '\0') {
        vb.append('\0');
    }

    auto dbg = [this](QString title, QString msg, bool isGood) {
        if (mVesc) {
            mVesc->emitMessageDialog(title, msg, isGood);
        } else {
            if (isGood) {
                qDebug() << title << ":" << msg;
            } else {
                qWarning() << title << ":" << msg;
            }
        }
    };

    // Create and append data table
    {
        QList<QPair<QString, QByteArray> > files;
        auto lines = codeStr.split("\n");
        int line_num = 0;

        for (const auto &line : lines) {
            line_num++;

            QString path;
            QString tag;
            bool isInvalid;

            if (getImportFromLine(line, path, tag, isInvalid)) {
                if (isInvalid) {
                    dbg(tr("Append Imports"),
                        tr("Line: %1: Invalid import tag.").arg(line_num),
                        false);
                    return QByteArray();
                }

                auto pkgErrorMsg = "If you are importing from a package downloaded from the package store, "
                                   "you might need to update the package archive. That can be done from "
                                   "the VESC Packages page.";

                bool isPkgImport = false;
                QString pkgImportName;
                if (path.startsWith("pkg::")) {
                    path.remove(0, 5);

                    auto atInd = path.indexOf("@");
                    if (atInd > 0) {
                        pkgImportName = path.mid(0, atInd);
                        path.remove(0, atInd + 1);
                    } else {
                        dbg(tr("Append Imports"),
                            tr("Line: %1: Invalid import tag.").arg(line_num),
                            false);
                        return QByteArray();
                    }

                    isPkgImport = true;
                } else if (path.startsWith("pkg@")) {
                    path.remove(0, 4);
                    isPkgImport = true;
                }

                QFileInfo fi(editorPath + "/" + path);
                if (!fi.exists()) {
                    fi = QFileInfo(path);
                }

                if (fi.exists()) {
                    QFile f(fi.absoluteFilePath());
                    if (f.open(QIODevice::ReadOnly)) {
                        auto fileData = f.readAll();

                        if (isPkgImport) {
                            auto pkg = unpackVescPackage(fileData);
                            auto imports = lispUnpackImports(pkg.lispData);

                            if (pkgImportName.isEmpty()) {
                                auto importData = imports.first.toLocal8Bit();
                                importData.append('\0'); // Pad with 0 in case it is a text file
                                files.append(qMakePair(tag, importData));
                            } else {
                                bool found = false;
                                for (const auto &i : imports.second) {
                                    if (i.first == pkgImportName) {
                                        auto importData = i.second;
                                        importData.append('\0'); // Pad with 0 in case it is a text file
                                        files.append(qMakePair(tag, importData));
                                        found = true;
                                        break;
                                    }
                                }

                                if (!found) {
                                    mVesc->emitMessageDialog(tr("Append Imports"),
                                                             tr("Tag %1 not found in package %2. %3").
                                                             arg(pkgImportName).arg(path).arg(pkgErrorMsg),
                                                             false);
                                    return QByteArray();
                                }
                            }
                        } else {
                            bool hasExtension = fi.absoluteFilePath().endsWith(".lbm", Qt::CaseInsensitive)
                                || fi.absoluteFilePath().endsWith(".lisp", Qt::CaseInsensitive);
                            if (reduceLisp && hasExtension) {
                                fileData = reduceLispFile(QString::fromUtf8(fileData)).toUtf8();
                            }
                            fileData.append('\0'); // Pad with 0 in case it is a text file
                            files.append(qMakePair(tag, fileData));
                        }
                    } else {
                        dbg(tr("Append Imports"),
                            tr("Line: %1: Imported file cannot be opened.").arg(line_num),
                            false);
                        return QByteArray();
                    }
                } else {
                    dbg(tr("Append Imports"),
                        tr("Line: %1: Imported file not found: %2. %3").
                        arg(line_num).arg(fi.absoluteFilePath()).arg(pkgErrorMsg),
                        false);
                    return QByteArray();
                }
            }
        }

        int file_table_size = 0;
        for (auto f: files) {
            file_table_size += f.first.length() + 9;
        }

        vb.vbAppendInt16(files.size());

        int file_offset = vb.size() + file_table_size - 2;

        for (auto f: files) {
            // Align on 4 bytes in case this is loaded as code
            while (file_offset % 4 != 0) {
                file_offset++;
            }

            vb.vbAppendString(f.first);
            vb.vbAppendInt32(file_offset);
            vb.vbAppendInt32(f.second.size());
            file_offset += f.second.size();
        }

        for (auto f: files) {
            while ((vb.size() - 2) % 4 != 0) {
                vb.append('\0');
            }
            vb.append(f.second);
        }
    }

    return std::move(vb);
}

QPair<QString, QList<QPair<QString, QByteArray> > > CodeLoader::lispUnpackImports(QByteArray data)
{
    // In case the import starts with a flag it is removed here. Note: The flag is a
    // bit confusing and there is a slightly more consistent way of handling it, but
    // I left it as is to not break compatibility.
    if (data.size() > 2 && data[0] == '\0' && data[1] == '\0') {
        data.remove(0, 2);
    }

    QList<QPair<QString, QByteArray> > imports;
    int end = data.indexOf(QByteArray("\0", 1));

    if (end > 0) {
        VByteArray vb = data.mid(end + 1);

        if (vb.size() > 3) {
            auto num_imports = vb.vbPopFrontInt16();
            if (num_imports > 0 && num_imports < 500) {
                for (int i = 0;i < num_imports;i++) {
                    auto name = vb.vbPopFrontString();
                    auto offset = vb.vbPopFrontInt32();
                    auto len = vb.vbPopFrontInt32() - 1; // Remove 1 as the files are padded with one 0
                    imports.append(qMakePair(name, data.mid(offset, len)));
                }
            }
        }

        data = data.left(end);
    }

    return qMakePair(QString::fromLocal8Bit(data), imports);
}

bool CodeLoader::lispUpload(VByteArray vb)
{
    quint16 crc = Packet::crc16((const unsigned char*)vb.constData(), uint32_t(vb.size()));
    VByteArray data;
    data.vbAppendUint32(vb.size() - 2);
    data.vbAppendUint16(crc);
    data.append(vb);

    // The ESP32 partition table has 512k space for LispBM scripts. The STM32
    // has one 128k flash page. Subtract 6 bytes for fw size and crc.
    auto fwParams = mVesc->getLastFwRxParams();
    int max_size = 1024 * 512 - 6;
    if (fwParams.hwType == HW_TYPE_VESC) {
        max_size = 1024 * 128 - 6;
    }

    if (data.size() > max_size) {
        mVesc->emitMessageDialog(tr("Upload Code"), tr("Not enough space"), false);
        return false;
    }

    auto waitWriteRes = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(1000);
            task.connectSignal(mVesc->commands(), &Commands::lispWriteCodeRx,
                               [&res](bool erRes, quint32) { res = erRes ? 1 : -1; });
            return SetupResult::Continue;
        })});
        return res;
    };

    auto writeChunk = [this, waitWriteRes](QByteArray data, quint32 offset) {
        int tries = 5;
        int res = -10;
        while (tries > 0) {
            mVesc->commands()->lispWriteCode(data, offset);
            if ((res = waitWriteRes()) >= 0) {
                return res;
            }
            tries--;
        }

        return res;
    };

    auto sizeTotal = data.size();
    quint32 offset = 0;
    bool ok = true;
    mAbortDownloadUpload = false;
    while (data.size() > 0 && !mAbortDownloadUpload) {
        const int chunkSize = 384;
        int sz = data.size() > chunkSize ? chunkSize : data.size();

        if (writeChunk(data.mid(0, sz), offset) < 0) {
            mVesc->emitMessageDialog(tr("Upload Code"), tr("Write failed"), false);
            ok = false;
            break;
        }

        emit lispUploadProgress(sizeTotal - data.size(), sizeTotal);

        offset += sz;
        data.remove(0, sz);
    }

    if (mAbortDownloadUpload) {
        ok = false;
    }

    emit lispUploadProgress(sizeTotal, sizeTotal);

    return ok;
}

bool CodeLoader::lispUploadString(QString codeStr, QString editorPath, bool reduceLisp)
{
    VByteArray vb = lispPackImports(codeStr, editorPath, reduceLisp);

    if (vb.isEmpty()) {
        return false;
    }

    return lispUpload(vb);
}

bool CodeLoader::lispUploadFromPath(QString path, bool reduceLisp)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        QFileInfo fi(f);
        VByteArray lispData = lispPackImports(f.readAll(), fi.canonicalPath(), reduceLisp);
        f.close();

        if (!lispData.isEmpty()) {
            bool ok = lispErase(lispData.size() + 100);
            if (ok) {
                return lispUpload(lispData);
            }
        }
    }

    return false;
}

bool CodeLoader::lispStream(VByteArray vb, qint8 mode)
{
    if (!mVesc->isPortConnected()) {
        mVesc->emitMessageDialog(tr("Stream code"), tr("Not Connected"), false);
        return false;
    }

    auto waitWriteRes = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(4000);
            task.connectSignal(mVesc->commands(), &Commands::lispStreamCodeRx,
                               [&res](qint32, qint16 result) { res = result; });
            return SetupResult::Continue;
        })});
        return res;
    };

    qint32 offset = 0;
    qint32 size_tot = vb.size();
    bool ok = true;
    while (vb.size() > 0) {
        const int chunkSize = 384;
        int sz = vb.size() > chunkSize ? chunkSize : vb.size();

        mVesc->commands()->lispStreamCode(vb.mid(0, sz), offset, size_tot, mode);
        auto writeRes = waitWriteRes();
        if (writeRes != 0) {
            mVesc->emitMessageDialog(tr("Stream Code"), tr("Stream failed. Result: %1").arg(writeRes), false);
            ok = false;
            break;
        }

        offset += sz;
        vb.remove(0, sz);
    }

    return ok;
}

QString CodeLoader::lispRead(QWidget *parent, QString &lispPath)
{
    if (!mVesc->isPortConnected()) {
        mVesc->emitMessageDialog(tr("Read code"), tr("Not Connected"), false);
        return "";
    }

    QByteArray lispData;
    int lenLispLast = 0;
    auto conn = connect(mVesc->commands(), &Commands::lispReadCodeRx,
                        [&](int lenLisp, int ofsLisp, QByteArray data) {
        if (lispData.size() <= ofsLisp) {
            lispData.append(data);
        }
        lenLispLast = lenLisp;
    });

    auto getLispChunk = [&](int size, int offset, int tries, int timeout) {
        bool res = false;

        for (int j = 0;j < tries;j++) {
            mVesc->commands()->lispReadCode(size, offset);
            res = Utility::waitSignal(mVesc->commands(), &Commands::lispReadCodeRx, timeout);
            if (res) {
                break;
            }
        }
        return res;
    };

    QString res = "";
    mAbortDownloadUpload = false;

    if (getLispChunk(10, 0, 5, 1500)) {
        while (lispData.size() < lenLispLast && !mAbortDownloadUpload) {
            int dataLeft = lenLispLast - lispData.size();
            if (!getLispChunk(dataLeft > 400 ? 400 : dataLeft, lispData.size(), 5, 1500)) {
                break;
            }

            emit lispUploadProgress(lispData.size(), lenLispLast);
        }

        if (mAbortDownloadUpload) {
            return res;
        }

        if (lispData.size() == lenLispLast) {
            auto unpacked = lispUnpackImports(lispData);

            res = unpacked.first;
            auto num_imports = unpacked.second.length();

            if (num_imports > 0) {
                auto reply = QMessageBox::warning(parent,
                                                  tr("Imports"),
                                                  tr("%1 imports found. Do you want to save them as files?").arg(num_imports),
                                                  QMessageBox::Yes | QMessageBox::No);

                QMap<QString, QString> importPaths;
                for (const auto &line : res.split('\n')) {
                    QString path;
                    QString tag;
                    bool isInvalid;
                    if (getImportFromLine(line, path, tag, isInvalid)) {
                        if (!isInvalid) {
                            importPaths.insert(tag, path);
                        }
                    }
                }

                if (reply == QMessageBox::Yes) {
                    QString dirName = QFileDialog::getExistingDirectory(parent, tr("Choose Directory"));

                    if (!dirName.isEmpty()) {
                        QFile fileLisp(dirName + "/From VESC.lbm");
                        if (!fileLisp.exists()) {
                            if (fileLisp.open(QIODevice::WriteOnly)) {
                                fileLisp.write(res.toUtf8());
                                fileLisp.close();
                                lispPath = QFileInfo(fileLisp).canonicalFilePath();
                            }
                        }

                        for (const auto &i : unpacked.second) {
                            QString fileName = dirName + "/" + i.first + ".bin";

                            if (importPaths.contains(i.first)) {
                                const auto &path = importPaths[i.first];

                                if (!path.startsWith("/") &&
                                        !path.startsWith("\\") &&
                                        !path.startsWith("pkg::") &&
                                        !path.startsWith("pkg@")) {
                                    fileName = dirName + "/" + path;
                                }
                            }

                            QFile file(fileName);
                            QFileInfo fi(file);
                            QDir().mkpath(fi.path());

                            if (file.exists()) {
                                auto reply = QMessageBox::question(parent,
                                                                   tr("Replace File"),
                                                                   tr("File %1 exists. Do you want to replace it?").arg(i.first));

                                if (reply != QMessageBox::Yes) {
                                    continue;
                                }
                            }

                            if (!file.open(QIODevice::WriteOnly)) {
                                QMessageBox::critical(parent, tr("Save Import"),
                                                      "Could not open\n" + file.fileName() + "\nfor writing");
                                return QByteArray();
                            }

                            file.write(i.second);
                            file.close();
                        }
                    }
                }
            }

            return res;
        }
    }

    mVesc->emitMessageDialog(tr("Get LispBM"),
                             tr("Could not read LispBM code"),
                             false);

    disconnect(conn);

    return "";
}

QVariantMap CodeLoader::lispReadWithPath()
{
    QVariantMap res;
    QString path = "From VESC";
    QString code = lispRead(nullptr, path);
    res.insert("path", path);
    res.insert("code", code);
    return res;
}

bool CodeLoader::qmlErase(int size)
{
    if (!mVesc) {
        return false;
    }

    if (!mVesc->isPortConnected()) {
        mVesc->emitMessageDialog(tr("Erase Qml"), tr("Not Connected"), false);
        return false;
    }

    auto waitEraseRes = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(6000);
            task.connectSignal(mVesc->commands(), &Commands::eraseQmluiResReceived,
                               [&res](bool erRes) { res = erRes ? 1 : -1; });
            return SetupResult::Continue;
        })});
        return res;
    };

    mVesc->commands()->qmlUiErase(size);

    int erRes = waitEraseRes();
    if (erRes != 1) {
        QString msg = "Unknown failure";

        if (erRes == -10) {
            msg = "Erase timed out";
        } else if (erRes == -1) {
            msg = "Erasing QMLUI failed";
        }

        mVesc->emitMessageDialog(tr("Erase Qml"), msg, false);
        return false;
    }

    return true;
}

QByteArray CodeLoader::qmlCompress(QString script)
{
    script.prepend("import \"qrc:/mobile\";");
    script.prepend("import Vedder.vesc.vescinterface 1.0;");
    return qCompress(script.toUtf8(), 9);
}

bool CodeLoader::qmlUpload(QByteArray script, bool isFullscreen)
{
    auto waitWriteRes = [this]() {
        int res = -10;
        runTree(Group{SignalWaitTaskItem([this, &res](SignalWaitTask &task) {
            task.setTimeout(1000);
            task.connectSignal(mVesc->commands(), &Commands::writeQmluiResReceived,
                               [&res](bool erRes, quint32) { res = erRes ? 1 : -1; });
            return SetupResult::Continue;
        })});
        return res;
    };

    auto writeChunk = [this, waitWriteRes](QByteArray data, quint32 offset) {
        int tries = 5;
        int res = -10;
        while (tries > 0) {
            mVesc->commands()->qmlUiWrite(data, offset);
            if ((res = waitWriteRes()) >= 0) {
                return res;
            }
            tries--;
        }

        return res;
    };

    VByteArray vb;
    vb.vbAppendUint16(isFullscreen ? 2 : 1);
    vb.append(script);
    quint16 crc = Packet::crc16((const unsigned char*)vb.constData(),
                                uint32_t(vb.size()));
    VByteArray data;
    data.vbAppendUint32(vb.size() - 2);
    data.vbAppendUint16(crc);
    data.append(vb);

    if (data.size() > (1024 * 120)) {
        mVesc->emitMessageDialog(tr("Upload Qml"), tr("Not enough space"), false);
        return false;
    }

    quint32 offset = 0;
    bool ok = true;
    while (data.size() > 0) {
        const int chunkSize = 384;
        int sz = data.size() > chunkSize ? chunkSize : data.size();

        if (writeChunk(data.mid(0, sz), offset) < 0) {
            mVesc->emitMessageDialog(tr("Upload Qml"), tr("Qml write failed"), false);
            ok = false;
            break;
        }

        offset += sz;
        data.remove(0, sz);
    }

    return ok;
}

QByteArray CodeLoader::packVescPackage(VescPackage pkg)
{
    VByteArray data;

    data.vbAppendString("VESC Packet");

    if (!pkg.name.isEmpty()) {
        auto dataRaw = pkg.name.toUtf8();
        data.vbAppendString("name");
        data.vbAppendInt32(dataRaw.size());
        data.append(dataRaw);
    }

    if (!pkg.description.isEmpty()) {
        auto dataRaw = pkg.description.toUtf8();
        data.vbAppendString("description");
        data.vbAppendInt32(dataRaw.size());
        data.append(dataRaw);
    }

    if (!pkg.description_md.isEmpty()) {
        auto dataRaw = pkg.description_md.toUtf8();
        data.vbAppendString("description_md");
        data.vbAppendInt32(dataRaw.size());
        data.append(dataRaw);
    }

    if (!pkg.lispData.isEmpty()) {
        data.vbAppendString("lispData");
        data.vbAppendInt32(pkg.lispData.size());
        data.append(pkg.lispData);
    }

    if (!pkg.qmlFile.isEmpty()) {
        auto dataRaw = pkg.qmlFile.toUtf8();
        data.vbAppendString("qmlFile");
        data.vbAppendInt32(dataRaw.size());
        data.append(dataRaw);
    }

    if (!pkg.pkgDescQml.isEmpty()) {
        auto dataRaw = pkg.pkgDescQml.toUtf8();
        data.vbAppendString("pkgDescQml");
        data.vbAppendInt32(dataRaw.size());
        data.append(dataRaw);
    }

    data.vbAppendString("qmlIsFullscreen");
    data.vbAppendInt32(1);
    data.vbAppendInt8(pkg.qmlIsFullscreen);

    return qCompress(data, 9);
}

VescPackage CodeLoader::unpackVescPackage(QByteArray data)
{
    VescPackage pkg;
    VByteArray vb(qUncompress(data));

    // Yes, packet is a typo and should say package...
    // That does not matter in practice and changing that now breaks
    // compatibility.
    if (vb.vbPopFrontString() != "VESC Packet") {
        qWarning() << "Invalid VESC Package";
        return pkg;
    }

    pkg.compressedData = data;

    while (!vb.isEmpty()) {
        QString name = vb.vbPopFrontString();

        if (name.isEmpty()) {
            qWarning() << "Empty name";
            break;
        }

        if (name == "name") {
            auto len = vb.vbPopFrontInt32();
            auto dataRaw = vb.left(len);
            vb.remove(0, len);
            pkg.name = QString::fromUtf8(dataRaw);
            pkg.loadOk = true;
        } else if (name == "description") {
            auto len = vb.vbPopFrontInt32();
            auto dataRaw = vb.left(len);
            vb.remove(0, len);
            pkg.description = QString::fromUtf8(dataRaw);
            pkg.loadOk = true;
        } else if (name == "description_md") {
            auto len = vb.vbPopFrontInt32();
            auto dataRaw = vb.left(len);
            vb.remove(0, len);
            pkg.description_md = QString::fromUtf8(dataRaw);
            pkg.loadOk = true;
        } else if (name == "lispData") {
            auto len = vb.vbPopFrontInt32();
            auto dataRaw = vb.left(len);
            vb.remove(0, len);
            pkg.lispData = dataRaw;
            pkg.loadOk = true;
        } else if (name == "qmlFile") {
            auto len = vb.vbPopFrontInt32();
            auto dataRaw = vb.left(len);
            vb.remove(0, len);
            pkg.qmlFile = QString::fromUtf8(dataRaw);
            pkg.loadOk = true;
        } else if (name == "qmlIsFullscreen") {
            vb.vbPopFrontInt32(); // Discard length
            pkg.qmlIsFullscreen = vb.vbPopFrontInt8();
        } else if (name == "pkgDescQml") {
            auto len = vb.vbPopFrontInt32();
            auto dataRaw = vb.left(len);
            vb.remove(0, len);
            pkg.pkgDescQml = QString::fromUtf8(dataRaw);
            pkg.loadOk = true;
        } else {
            // Unknown identifier, skip
            auto len = vb.vbPopFrontInt32();
            vb.remove(0, len);
        }
    }

    return pkg;
}

VescPackage CodeLoader::unpackVescPackageFromPath(QString path)
{
    if (path.startsWith("file:/")) {
        path.remove(0, 6);
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (mVesc) {
            mVesc->emitMessageDialog(tr("Unpack VESC Package"),
                                     tr("Could not open package file for reading."),
                                     false, false);
        }

        return VescPackage();
    }

    auto pkg = unpackVescPackage(f.readAll());

    if (!pkg.loadOk) {
        if (mVesc) {
            mVesc->emitMessageDialog(tr("Unpack VESC Package"),
                                     tr("Invalid package file."),
                                     false, false);
        }
    }

    return pkg;
}

bool CodeLoader::installVescPackage(VescPackage pkg)
{
    if (!pkg.loadOk) {
        mVesc->emitMessageDialog(tr("Write Package"), tr("Package is not valid."), false);
        return false;
    }

    bool res = true;
    QByteArray qml;

    if (!pkg.qmlFile.isEmpty()) {
        qml = qmlCompress(pkg.qmlFile);
        res = qmlErase(qml.size() + 100);

        if (res) {
            res = qmlUpload(qml, pkg.qmlIsFullscreen);
        }
    } else {
        if (mVesc && mVesc->isPortConnected() && mVesc->getLastFwRxParams().hasQmlApp) {
            res = qmlErase(16);
        }
    }

    if (res) {
        if (!pkg.lispData.isEmpty()) {
            res = lispErase(pkg.lispData.size() + 100);

            if (res) {
                res = lispUpload(VByteArray(pkg.lispData));

                if (res) {
                    mVesc->commands()->lispSetRunning(1);
                }
            }
        } else {
            res = lispErase(16);
        }
    }

    Utility::sleepWithEventLoop(500);
    mVesc->reloadFirmware();

    return res;
}

bool CodeLoader::installVescPackage(QByteArray data)
{
    return installVescPackage(unpackVescPackage(data));
}

bool CodeLoader::installVescPackageFromPath(QString path)
{
    if (path.startsWith("file:/")) {
        path.remove(0, 6);
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        mVesc->emitMessageDialog(tr("Write Package"),
                                 tr("Could not open package file for reading."),
                                 false, false);
        return false;
    }

    return installVescPackage(f.readAll());
}

bool CodeLoader::loadPackageArchiveResource()
{
    bool res = false;

    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString path = appDataLoc + "/vesc_pkg_all.rcc";

    if(!QDir(appDataLoc).exists()) {
            QDir().mkpath(appDataLoc);
    }

    QFile file(path);
    if (file.exists()) {
        QResource::unregisterResource(path);
        res = QResource::registerResource(path);
    }

    return res;
}

QVariantList CodeLoader::reloadPackageArchive()
{
    QVariantList res;

    auto scanDir = [&](const QString &dirPath, bool isRes) {
        if (!QDir(dirPath).exists()) {
            return;
        }

        QDirIterator it(dirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFileInfo fi(it.next());
            if (fi.isFile() && fi.fileName().toLower().endsWith(".vescpkg")) {
                QFile f(fi.absoluteFilePath());
                if (f.open(QIODevice::ReadOnly)) {
                    auto pkg = unpackVescPackage(f.readAll());
                    if (pkg.loadOk) {
                        pkg.isLibrary = isRes && fi.absoluteFilePath().contains("/lib_");
                        res.append(QVariant::fromValue(pkg));
                    }
                }
            }
        }
    };

    scanDir("://vesc_packages", true);

    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    scanDir(appDataLoc + "/packages", false);

    return res;
}

bool CodeLoader::downloadPackageArchive()
{
    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if(!QDir(appDataLoc).exists()) {
        QDir().mkpath(appDataLoc);
    }
    QString path = appDataLoc + "/vesc_pkg_all.rcc";
    QResource::unregisterResource(path);

    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::WriteOnly)) {
        emit packageArchiveDownloaded(false);
        return false;
    }

    QFile *filePtr = file.get();

    runTree(Group{NetworkReplyTaskItem([this, filePtr](NetworkReplyTask &task) {
        task.setUrl(QUrl("http://home.vedder.se/vesc_pkg/vesc_pkg_all.rcc"));
        task.setOutputDevice(filePtr);
        task.setProgressCallback([this](qint64 bytesReceived, qint64 bytesTotal) {
            emit downloadProgress(bytesReceived, bytesTotal);
        });
        return SetupResult::Continue;
    }, [this, file, path](const NetworkReplyTask &task, DoneWith doneWith) {
        file->close();
        bool success = (doneWith == DoneWith::Success);
        if (success) {
            QResource::registerResource(path);
            syncFsToIndexedDb();
        }
        emit packageArchiveDownloaded(success);
        return DoneResult::Success;
    })});

    return true;
}

void CodeLoader::fetchPackageFromSerial(int pkgId)
{
    if (m_serialFetchOngoing) {
        qWarning() << "Serial package fetch already in progress.";
        return;
    }

    if (!mVesc || !mVesc->isPortConnected()) {
        if (mVesc) {
            mVesc->emitMessageDialog(tr("Fetch Package"),
                                     tr("Not connected to VESC."),
                                     false, false);
        }
        emit packageArchiveDownloaded(false);
        return;
    }

    mAbortDownloadUpload = false;
    m_serialFetchOngoing = true;
    m_serialPkgId = pkgId;
    m_serialPkgBuffer.clear();
    m_serialPkgTotalSize = 0;
    m_serialPkgExpectedOffset = 0;
    m_serialPkgRetries = 0;

    emit downloadProgress(0, 100);

    requestNextChunk();
}

void CodeLoader::requestNextChunk()
{
    if (!m_serialFetchOngoing || mAbortDownloadUpload || !mVesc || !mVesc->isPortConnected()) {
        m_serialFetchOngoing = false;
        m_serialFetchTimer->stop();
        return;
    }

    int chunkSize = 384;
    if (m_serialPkgTotalSize > 0) {
        int remaining = m_serialPkgTotalSize - m_serialPkgExpectedOffset;
        if (remaining <= 0) {
            finalizeSerialPackage();
            return;
        }
        if (remaining < chunkSize) {
            chunkSize = remaining;
        }
    }

    if (mVesc->commands()) {
        // Primary path: COMM_CUSTOM_APP_DATA request with subcommand 0x02
        VByteArray vb;
        vb.vbAppendUint8(0x02); // Subcommand 0x02: FETCH_CHUNK
        vb.vbAppendInt32(m_serialPkgId);
        vb.vbAppendInt32(m_serialPkgExpectedOffset);
        vb.vbAppendInt32(chunkSize);
        mVesc->commands()->sendCustomAppData(vb);

        // Fallback/parallel for pkgId 0: standard VESC QML UI app get command
        if (m_serialPkgId == 0) {
            mVesc->commands()->qmlUiAppGet(chunkSize, m_serialPkgExpectedOffset);
        }
    }

    m_serialFetchTimer->start(2500);
}

void CodeLoader::onCustomAppDataReceived(QByteArray data)
{
    if (!m_serialFetchOngoing || data.isEmpty()) {
        return;
    }

    // Format 1 (Tagged with subcommand 0x02):
    // [0x02 (uint8), pkgId (int32), totalSize (int32), offset (int32), chunkData...]
    if (static_cast<quint8>(data.at(0)) == 0x02 && data.size() >= 13) {
        VByteArray vb(data);
        vb.remove(0, 1);
        int respPkgId = vb.vbPopFrontInt32();
        int totalSize = vb.vbPopFrontInt32();
        int offset = vb.vbPopFrontInt32();

        if (respPkgId == m_serialPkgId) {
            handleChunk(totalSize, offset, vb);
            return;
        }
    }

    // Format 2: [totalSize (int32), offset (int32), chunkData...]
    if (data.size() >= 8) {
        VByteArray vb(data);
        int totalSize = vb.vbPopFrontInt32();
        int offset = vb.vbPopFrontInt32();

        if (offset == m_serialPkgExpectedOffset && (totalSize == 0 || (totalSize >= offset && totalSize < 50000000))) {
            handleChunk(totalSize, offset, vb);
            return;
        }
    }

    // Format 3: Raw chunk payload
    handleChunk(0, m_serialPkgExpectedOffset, data);
}

void CodeLoader::onQmluiAppRx(int lenQml, int ofsQml, QByteArray data)
{
    if (!m_serialFetchOngoing) {
        return;
    }

    handleChunk(lenQml, ofsQml, data);
}

void CodeLoader::handleChunk(int totalSize, int offset, const QByteArray &chunkData)
{
    if (!m_serialFetchOngoing) {
        return;
    }

    m_serialFetchTimer->stop();

    if (chunkData.isEmpty()) {
        if (m_serialPkgBuffer.isEmpty()) {
            m_serialFetchOngoing = false;
            if (mVesc) {
                mVesc->emitMessageDialog(tr("Fetch Package"),
                                         tr("No package found on connected VESC."),
                                         false, false);
            }
            emit packageArchiveDownloaded(false);
            return;
        } else {
            finalizeSerialPackage();
            return;
        }
    }

    if (totalSize > 0 && m_serialPkgTotalSize == 0) {
        m_serialPkgTotalSize = totalSize;
    }

    if (offset == m_serialPkgExpectedOffset) {
        m_serialPkgBuffer.append(chunkData);
        m_serialPkgExpectedOffset = m_serialPkgBuffer.size();
        m_serialPkgRetries = 0;

        int reportedTotal = m_serialPkgTotalSize > 0 ? m_serialPkgTotalSize : (m_serialPkgExpectedOffset + chunkData.size());
        emit downloadProgress(m_serialPkgExpectedOffset, reportedTotal);

        if (m_serialPkgTotalSize > 0 && m_serialPkgBuffer.size() >= m_serialPkgTotalSize) {
            finalizeSerialPackage();
        } else {
            requestNextChunk();
        }
    } else if (offset < m_serialPkgExpectedOffset) {
        // Stale or duplicate chunk; restart watchdog
        m_serialFetchTimer->start(2500);
    } else {
        // Gap detected: re-request from current expected offset
        requestNextChunk();
    }
}

void CodeLoader::finalizeSerialPackage()
{
    m_serialFetchOngoing = false;
    m_serialFetchTimer->stop();

    if (m_serialPkgBuffer.isEmpty()) {
        if (mVesc) {
            mVesc->emitMessageDialog(tr("Fetch Package"),
                                     tr("Received empty package from VESC."),
                                     false, false);
        }
        emit packageArchiveDownloaded(false);
        return;
    }

    VescPackage pkg = unpackVescPackage(m_serialPkgBuffer);

    // Fallback: If not formatted as a full VESC package, check if it's raw QML or compressed QML
    if (!pkg.loadOk) {
        QByteArray uncompressed = qUncompress(m_serialPkgBuffer);
        if (uncompressed.isEmpty()) {
            uncompressed = m_serialPkgBuffer;
        }

        if (uncompressed.contains("import QtQuick") || uncompressed.contains("import ")) {
            pkg.name = "VESC Serial App";
            pkg.description = "Extracted directly from connected VESC controller.";
            pkg.qmlFile = QString::fromUtf8(uncompressed);
            pkg.loadOk = true;
            m_serialPkgBuffer = packVescPackage(pkg);
        }
    }

    if (!pkg.loadOk) {
        if (mVesc) {
            mVesc->emitMessageDialog(tr("Fetch Package"),
                                     tr("Failed to parse package archive received from VESC."),
                                     false, false);
        }
        emit packageArchiveDownloaded(false);
        return;
    }

    // Save package into persistent packages folder
    QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString pkgDir = appDataLoc + "/packages";
    if (!QDir(pkgDir).exists()) {
        QDir().mkpath(pkgDir);
    }

    QString safeName = pkg.name;
    safeName.replace(" ", "_").replace("/", "_").replace("\\", "_").replace(":", "_");
    if (safeName.isEmpty()) {
        safeName = "serial_package";
    }

    QString pkgPath = pkgDir + "/" + safeName + ".vescpkg";
    QFile f(pkgPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(m_serialPkgBuffer);
        f.close();
        syncFsToIndexedDb();
    }

    if (mVesc) {
        mVesc->emitStatusMessage(tr("Successfully fetched package '%1' from VESC").arg(pkg.name), true);
    }

    emit packageArchiveDownloaded(true);
}

void CodeLoader::onSerialFetchTimeout()
{
    if (!m_serialFetchOngoing) {
        return;
    }

    if (m_serialPkgRetries < 3) {
        m_serialPkgRetries++;
        qWarning() << "Serial package fetch timeout, retry" << m_serialPkgRetries;
        requestNextChunk();
    } else {
        if (m_serialPkgTotalSize == 0 && m_serialPkgBuffer.size() > 0) {
            finalizeSerialPackage();
        } else {
            m_serialFetchOngoing = false;
            m_serialPkgBuffer.clear();
            if (mVesc) {
                mVesc->emitMessageDialog(tr("Fetch Package"),
                                         tr("Serial package fetch timed out. VESC did not respond."),
                                         false, false);
            }
            emit packageArchiveDownloaded(false);
        }
    }
}

void CodeLoader::onPortConnectedChanged()
{
    if (m_serialFetchOngoing && (!mVesc || !mVesc->isPortConnected())) {
        m_serialFetchOngoing = false;
        m_serialFetchTimer->stop();
        m_serialPkgBuffer.clear();
        emit packageArchiveDownloaded(false);
    }
}

void CodeLoader::abortDownloadUpload()
{
    mAbortDownloadUpload = true;
    if (m_serialFetchOngoing) {
        m_serialFetchOngoing = false;
        m_serialFetchTimer->stop();
        m_serialPkgBuffer.clear();
        emit packageArchiveDownloaded(false);
    }
}

bool CodeLoader::createPackageFromDescription(QString path, VescPackage *pkgRes, bool reduceLisp)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open package description file.";
        return false;
    }

    QString qmlData = QString::fromUtf8(f.readAll());
    f.close();

    QQmlEngine *engine = new QQmlEngine(this);
    QQmlComponent component(engine);
    component.setData(qmlData.toUtf8(), QUrl());
    QQuickItem *qmlItem = qobject_cast<QQuickItem*>(component.create());

    if (!qmlItem) {
        qWarning() << "Loading QML package description failed";
        qWarning() << component.errorString();
        engine->deleteLater();
        return false;
    }

    VescPackage pkg;
    pkg.pkgDescQml = qmlData;

    bool result = true;
    QString pkgOutput = "";

    auto prop = qmlItem->property("pkgName");
    if (prop.isValid()) {
        pkg.name = prop.toString();
        qDebug() << "Package name found:" << pkg.name;
    }

    prop = qmlItem->property("pkgDescriptionMd");
    if (prop.isValid()) {
        auto mdPath = prop.toString();

        if (!mdPath.isEmpty()) {
            QFile f(mdPath);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qWarning() << "Could not open markdown file.";
                result = false;
            }

            QString desc = QString::fromUtf8(f.readAll());
            f.close();

            qDebug() << "Package description found!";
            pkg.description_md = desc;
            pkg.description = Utility::md2html(desc);
        }
    }

    prop = qmlItem->property("pkgLisp");
    if (prop.isValid()) {
        auto lispPath = prop.toString();

        if (!lispPath.isEmpty()) {
            QFile f(lispPath);
            if (f.open(QIODevice::ReadOnly)) {
                QFileInfo fi(f);
                pkg.lispData = lispPackImports(f.readAll(), fi.canonicalPath(), reduceLisp);

                // Empty array means an error. Otherwise, lispPackImports() always returns data.
                if (pkg.lispData.isEmpty()) {
                    qWarning() << "Errors when processing LispBM imports.";
                    result = false;
                }

                f.close();

                qDebug() << "Package LispBM found and parsed!";
            } else {
                qWarning() << "Could not open LispBM file.";
                result = false;
            }
        }
    }

    prop = qmlItem->property("pkgQml");
    if (prop.isValid()) {
        auto qmlPath = prop.toString();

        if (!qmlPath.isEmpty()) {
            QFile f(qmlPath);
            if (f.open(QIODevice::ReadOnly)) {
                pkg.qmlFile = f.readAll();
                f.close();
                qDebug() << "Package qml found!";
            } else {
                qWarning() << "Could not open qml file.";
                result = false;
            }
        }
    }

    prop = qmlItem->property("pkgQmlIsFullscreen");
    if (prop.isValid()) {
        pkg.qmlIsFullscreen = prop.toBool();
        qDebug() << "QML fullscreen:" << pkg.qmlIsFullscreen;
    }

    prop = qmlItem->property("pkgOutput");
    if (prop.isValid()) {
        pkgOutput = prop.toString();
    }

    if (pkgOutput.isEmpty()) {
        qWarning() << "No package output specified";
        result = false;
    } else {
        QFile file(pkgOutput);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(packVescPackage(pkg));
            file.close();
            qDebug() << "Package saved as" << pkgOutput;
        } else {
            qWarning() << QString("Could not open %1 for writing.").arg(pkgOutput);
            result = false;
        }
    }

    engine->deleteLater();

    if (pkgRes != nullptr) {
        *pkgRes = pkg;
    }

    return result;
}

bool CodeLoader::shouldShowPackage(VescPackage pkg)
{
    if (!mVesc) {
        return true;
    }

    if (!mVesc->isPortConnected()) {
        return true;
    }

    return shouldShowPackageFromRxpReuseEngine(pkg, mVesc->getLastFwRxParams());
}

bool CodeLoader::shouldShowPackageFromRxpReuseEngine(VescPackage pkg, FW_RX_PARAMS rxp, bool *runOk)
{
    bool res = true;
    if (!pkg.pkgDescQml.isEmpty()) {
        if (mQmlEngine == nullptr) {
            mQmlEngine = new QQmlEngine(this);
        }

        QQmlComponent component(mQmlEngine);
        component.setData(pkg.pkgDescQml.toUtf8(), QUrl());
        QQuickItem *qmlItem = qobject_cast<QQuickItem*>(component.create());

        if (qmlItem == nullptr) {
            qWarning() << "Failed to run isCompatible because qmlItem could not be created";
            return res;
        }

        QVariant returnedValue;
        QMetaObject::invokeMethod(qmlItem,
                                  "isCompatible",
                                  Q_RETURN_ARG(QVariant, returnedValue),
                                  Q_ARG(QVariant, QVariant::fromValue(rxp)));

        qmlItem->deleteLater();

        if (runOk != nullptr) {
            *runOk = returnedValue.isValid();
        }

        if (returnedValue.isValid()) {
            res = returnedValue.toBool();
        } else {
            qWarning() << "Failed to run isCompatible from package" << pkg.name;
        }
    }

    return res;
}

bool CodeLoader::shouldShowPackageFromRxp(VescPackage pkg, FW_RX_PARAMS rxp, bool *runOk)
{
    bool res = true;
    if (!pkg.pkgDescQml.isEmpty()) {
        QQmlEngine *engine = new QQmlEngine();
        QQmlComponent component(engine);
        component.setData(pkg.pkgDescQml.toUtf8(), QUrl());
        QQuickItem *qmlItem = qobject_cast<QQuickItem*>(component.create());

        if (qmlItem == nullptr) {
            qWarning() << "Failed to run isCompatible because qmlItem could not be created";
            engine->deleteLater();
            return res;
        }

        QVariant returnedValue;
        QMetaObject::invokeMethod(qmlItem,
                                  "isCompatible",
                                  Q_RETURN_ARG(QVariant, returnedValue),
                                  Q_ARG(QVariant, QVariant::fromValue(rxp)));

        qmlItem->deleteLater();
        engine->deleteLater();

        if (runOk != nullptr) {
            *runOk = returnedValue.isValid();
        }

        if (returnedValue.isValid()) {
            res = returnedValue.toBool();
        } else {
            qWarning() << "Failed to run isCompatible from package" << pkg.name;
        }
    }

    return res;
}

bool CodeLoader::getImportFromLine(QString line, QString &path, QString &tag, bool &isInvalid)
{
    bool res = false;
    isInvalid = false;

    while (line.startsWith(" ")) {
        line.remove(0, 1);
    }

    while (line.startsWith("( ")) {
        line.remove(1, 1);
    }

    if (line.startsWith("(import ", Qt::CaseInsensitive)) {
        int start = line.indexOf("\"");
        int end = line.lastIndexOf("\"");

        if (start > 0 && end > start) {
            path = line.mid(start + 1, end - start - 1);
            tag = line.mid(end + 1).replace("\r", "").replace(" ", "").replace(")", "").replace("'", "");
            if (tag.indexOf(";") >= 0) {
                tag = tag.mid(0, tag.indexOf(";"));
            }
        } else {
            isInvalid = true;
        }

        res = true;
    }

    if (path.isEmpty() || tag.isEmpty()) {
        isInvalid = true;
    }

    return res;
}
