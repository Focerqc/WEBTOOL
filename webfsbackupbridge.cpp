#include "webfsbackupbridge.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>

EM_JS(int, js_webfs_is_supported, (), {
    if (typeof window !== 'undefined' && (('showDirectoryPicker' in window) || ('indexedDB' in window))) {
        return 1;
    }
    return 0;
});

EM_JS(void, js_webfs_init, (), {
    if (typeof window === 'undefined') return;

    if (!window._webFsBackupBridge) {
        window._webFsBackupBridge = {
            dirHandle: null,
            dirName: '',
            dbName: 'VescWebBackupDB',
            storeName: 'dirHandles',

            openDb: function() {
                var self = this;
                return new Promise(function(resolve, reject) {
                    if (!window.indexedDB) {
                        return reject(new Error("IndexedDB not supported"));
                    }
                    var req = window.indexedDB.open(self.dbName, 2);
                    req.onupgradeneeded = function(e) {
                        var db = e.target.result;
                        if (!db.objectStoreNames.contains('dirHandles')) {
                            db.createObjectStore('dirHandles');
                        }
                        if (!db.objectStoreNames.contains('backups')) {
                            db.createObjectStore('backups');
                        }
                    };
                    req.onsuccess = function(e) {
                        resolve(e.target.result);
                    };
                    req.onerror = function(e) {
                        reject(req.error);
                    };
                });
            },

            saveHandleToDb: function(handle) {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction('dirHandles', 'readwrite');
                        tx.objectStore('dirHandles').put(handle, 'currentBackupDir');
                        tx.oncomplete = function() { resolve(true); };
                        tx.onerror = function() { reject(tx.error); };
                    });
                });
            },

            loadHandleFromDb: function() {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction('dirHandles', 'readonly');
                        var req = tx.objectStore('dirHandles').get('currentBackupDir');
                        req.onsuccess = function() {
                            resolve(req.result || null);
                        };
                        req.onerror = function() {
                            resolve(null);
                        };
                    });
                }).catch(function(err) {
                    console.warn("[WEBFS] Failed to open IndexedDB for saved dir handle:", err);
                    return null;
                });
            },

            saveBackupToDb: function(subfolderName, files) {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction('backups', 'readwrite');
                        tx.objectStore('backups').put(files, subfolderName);
                        tx.oncomplete = function() { resolve(true); };
                        tx.onerror = function() { reject(tx.error); };
                    });
                });
            },

            loadBackupFromDb: function(subfolderName) {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction('backups', 'readonly');
                        var req = tx.objectStore('backups').get(subfolderName);
                        req.onsuccess = function() { resolve(req.result || null); };
                        req.onerror = function() { resolve(null); };
                    });
                });
            },

            listBackupsFromDb: function() {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction('backups', 'readonly');
                        var store = tx.objectStore('backups');
                        var req = store.openCursor();
                        var list = [];
                        req.onsuccess = function(e) {
                            var cursor = e.target.result;
                            if (cursor) {
                                var folderName = cursor.key;
                                var files = cursor.value || {};
                                var info = {
                                    folderName: folderName,
                                    name: folderName,
                                    canId: -1,
                                    date: '',
                                    timestamp: ''
                                };
                                if (files['backup_info.json']) {
                                    try {
                                        var parsed = JSON.parse(files['backup_info.json']);
                                        if (parsed.deviceName) info.name = parsed.deviceName;
                                        if (parsed.canId !== undefined) info.canId = parsed.canId;
                                        if (parsed.dateFormatted) info.date = parsed.dateFormatted;
                                        if (parsed.timestamp) info.timestamp = parsed.timestamp;
                                    } catch (ex) {}
                                }
                                list.push(info);
                                cursor.continue();
                            } else {
                                resolve(list);
                            }
                        };
                        req.onerror = function() { resolve([]); };
                    });
                }).catch(function() { return []; });
            },

            verifyPermission: async function(handle, readWrite) {
                if (!handle) return false;
                var opts = {};
                if (readWrite) opts.mode = 'readwrite';
                try {
                    if ((await handle.queryPermission(opts)) === 'granted') {
                        return true;
                    }
                    if ((await handle.requestPermission(opts)) === 'granted') {
                        return true;
                    }
                } catch (e) {
                    console.warn("[WEBFS] verifyPermission error:", e);
                }
                return false;
            }
        };

        // Attempt to reload handle silently on init
        window._webFsBackupBridge.loadHandleFromDb().then(function(handle) {
            if (handle) {
                window._webFsBackupBridge.dirHandle = handle;
                window._webFsBackupBridge.dirName = handle.name || 'Saved Folder';
                console.log("[WEBFS] Restored saved backup directory handle from IndexedDB:", window._webFsBackupBridge.dirName);
            }
        });
    }
});

EM_JS(char*, js_webfs_get_saved_folder_name, (), {
    if (typeof window !== 'undefined') {
        var name = '';
        if (window._webFsBackupBridge && window._webFsBackupBridge.dirName) {
            name = window._webFsBackupBridge.dirName;
        } else if (!('showDirectoryPicker' in window)) {
            name = 'Browser Storage (Downloads)';
        }
        if (name && name.length > 0) {
            var lengthBytes = lengthBytesUTF8(name) + 1;
            var stringOnWasmHeap = _malloc(lengthBytes);
            stringToUTF8(name, stringOnWasmHeap, lengthBytes);
            return stringOnWasmHeap;
        }
    }
    return 0;
});

EM_JS(void, js_webfs_select_folder, (WebFsFolderCallback cb, void* userData), {
    if (typeof window === 'undefined' || !window._webFsBackupBridge) {
        if (cb) {
            var msg = "Not initialized";
            var buf = _malloc(lengthBytesUTF8(msg) + 1);
            stringToUTF8(msg, buf, lengthBytesUTF8(msg) + 1);
            dynCall('viip', cb, [0, buf, userData]);
            _free(buf);
        }
        return;
    }

    if (typeof window.showDirectoryPicker !== 'function') {
        var msg = "Browser Storage (Downloads)";
        if (cb) {
            var buf = _malloc(lengthBytesUTF8(msg) + 1);
            stringToUTF8(msg, buf, lengthBytesUTF8(msg) + 1);
            dynCall('viip', cb, [1, buf, userData]);
            _free(buf);
        }
        return;
    }

    window.showDirectoryPicker({ mode: 'readwrite' }).then(async function(handle) {
        window._webFsBackupBridge.dirHandle = handle;
        window._webFsBackupBridge.dirName = handle.name || 'Backup Folder';
        await window._webFsBackupBridge.saveHandleToDb(handle);

        console.log("[WEBFS] Selected directory:", window._webFsBackupBridge.dirName);
        if (cb) {
            var buf = _malloc(lengthBytesUTF8(window._webFsBackupBridge.dirName) + 1);
            stringToUTF8(window._webFsBackupBridge.dirName, buf, lengthBytesUTF8(window._webFsBackupBridge.dirName) + 1);
            dynCall('viip', cb, [1, buf, userData]);
            _free(buf);
        }
    }).catch(function(err) {
        console.warn("[WEBFS] showDirectoryPicker cancelled or failed:", err);
        if (cb) {
            var errStr = (err && err.message) ? err.message : "Cancelled";
            var buf = _malloc(lengthBytesUTF8(errStr) + 1);
            stringToUTF8(errStr, buf, lengthBytesUTF8(errStr) + 1);
            dynCall('viip', cb, [0, buf, userData]);
            _free(buf);
        }
    });
});

EM_JS(void, js_webfs_save_backup, (const char* subfolderNamePtr, const char* filesJsonPtr, WebFsActionCallback cb, void* userData), {
    var subfolderName = UTF8ToString(subfolderNamePtr);
    var filesJsonStr = UTF8ToString(filesJsonPtr);

    async function doSave() {
        var bridge = window._webFsBackupBridge;
        if (!bridge) throw new Error("Bridge not initialized");

        var handle = bridge.dirHandle;
        if (!handle && typeof window.showDirectoryPicker === 'function') {
            try {
                handle = await window.showDirectoryPicker({ mode: 'readwrite' });
                bridge.dirHandle = handle;
                bridge.dirName = handle.name;
                await bridge.saveHandleToDb(handle);
            } catch (ePick) {
                console.warn("[WEBFS] Directory picker skipped or cancelled:", ePick);
            }
        }

        if (handle) {
            var hasPerm = await bridge.verifyPermission(handle, true);
            if (!hasPerm && typeof window.showDirectoryPicker === 'function') {
                try {
                    handle = await window.showDirectoryPicker({ mode: 'readwrite' });
                    bridge.dirHandle = handle;
                    bridge.dirName = handle.name;
                    await bridge.saveHandleToDb(handle);
                } catch (ePick) {
                    console.warn("[WEBFS] Re-prompt directory picker skipped:", ePick);
                }
            }
        }

        var files = JSON.parse(filesJsonStr);

        if (handle) {
            var subDir = await handle.getDirectoryHandle(subfolderName, { create: true });
            for (var filename in files) {
                if (Object.prototype.hasOwnProperty.call(files, filename)) {
                    var content = files[filename];
                    var fileHandle = await subDir.getFileHandle(filename, { create: true });
                    var writable = await fileHandle.createWritable();
                    await writable.write(content);
                    await writable.close();
                }
            }
        }

        // Persist to IndexedDB backups store
        try {
            await bridge.saveBackupToDb(subfolderName, files);
        } catch (eDb) {
            console.warn("[WEBFS] Failed saving to IndexedDB:", eDb);
        }

        // On browsers without Directory Picker (mobile Safari, mobile Chrome), trigger file download!
        if (!handle) {
            var dlBlob = new Blob([filesJsonStr], { type: 'application/json' });
            var dlUrl = URL.createObjectURL(dlBlob);
            var dlA = document.createElement('a');
            dlA.href = dlUrl;
            dlA.download = subfolderName + '.json';
            document.body.appendChild(dlA);
            dlA.click();
            setTimeout(function() {
                if (dlA.parentNode) dlA.parentNode.removeChild(dlA);
                URL.revokeObjectURL(dlUrl);
            }, 1500);
            return "Saved to browser storage and downloaded " + subfolderName + ".json";
        }

        return "Saved successfully to " + subfolderName;
    }

    doSave().then(function(resMsg) {
        if (cb) {
            var buf = _malloc(lengthBytesUTF8(resMsg) + 1);
            stringToUTF8(resMsg, buf, lengthBytesUTF8(resMsg) + 1);
            dynCall('viip', cb, [1, buf, userData]);
            _free(buf);
        }
    }).catch(function(err) {
        console.error("[WEBFS] Save backup failed:", err);
        if (cb) {
            var errStr = (err && err.message) ? err.message : "Write failed";
            var buf = _malloc(lengthBytesUTF8(errStr) + 1);
            stringToUTF8(errStr, buf, lengthBytesUTF8(errStr) + 1);
            dynCall('viip', cb, [0, buf, userData]);
            _free(buf);
        }
    });
});

EM_JS(void, js_webfs_list_backups, (WebFsListCallback cb, void* userData), {
    async function doList() {
        var bridge = window._webFsBackupBridge;
        if (!bridge) throw new Error("Bridge not initialized");

        var handle = bridge.dirHandle;
        if (!handle) {
            handle = await bridge.loadHandleFromDb();
            if (handle) {
                bridge.dirHandle = handle;
                bridge.dirName = handle.name;
            }
        }

        var map = {};

        // 1. If we have a directory handle with permission, read filesystem entries
        if (handle) {
            var hasPerm = await bridge.verifyPermission(handle, false);
            if (hasPerm) {
                for await (const entry of handle.values()) {
                    if (entry.kind === 'directory') {
                        var folderName = entry.name;
                        var info = {
                            folderName: folderName,
                            name: folderName,
                            canId: -1,
                            date: '',
                            timestamp: ''
                        };

                        try {
                            var subDir = await handle.getDirectoryHandle(folderName);
                            try {
                                var infoHandle = await subDir.getFileHandle('backup_info.json');
                                var file = await infoHandle.getFile();
                                var text = await file.text();
                                var parsed = JSON.parse(text);
                                if (parsed.deviceName) info.name = parsed.deviceName;
                                if (parsed.canId !== undefined) info.canId = parsed.canId;
                                if (parsed.dateFormatted) info.date = parsed.dateFormatted;
                                if (parsed.timestamp) info.timestamp = parsed.timestamp;
                            } catch (eInfo) {
                                var parts = folderName.split('_');
                                if (parts.length >= 1) info.name = parts[0];
                                if (parts.length >= 2 && parts[1].startsWith('CAN')) {
                                    info.canId = parseInt(parts[1].replace('CAN', ''));
                                }
                            }
                            map[folderName] = info;
                        } catch (eDir) {
                            console.warn("[WEBFS] Error inspecting dir entry:", folderName, eDir);
                        }
                    }
                }
            }
        }

        // 2. Read IndexedDB backups and merge
        try {
            var dbList = await bridge.listBackupsFromDb();
            for (var i = 0; i < dbList.length; i++) {
                var item = dbList[i];
                if (!map[item.folderName]) {
                    map[item.folderName] = item;
                }
            }
        } catch (eDb) {
            console.warn("[WEBFS] Error querying IndexedDB backups:", eDb);
        }

        var list = [];
        for (var k in map) {
            if (Object.prototype.hasOwnProperty.call(map, k)) {
                list.push(map[k]);
            }
        }

        // Sort latest backups first
        list.sort(function(a, b) {
            return b.folderName.localeCompare(a.folderName);
        });

        return JSON.stringify(list);
    }

    doList().then(function(jsonStr) {
        if (cb) {
            var buf = _malloc(lengthBytesUTF8(jsonStr) + 1);
            stringToUTF8(jsonStr, buf, lengthBytesUTF8(jsonStr) + 1);
            dynCall('viip', cb, [1, buf, userData]);
            _free(buf);
        }
    }).catch(function(err) {
        console.error("[WEBFS] List backups failed:", err);
        if (cb) {
            var errStr = JSON.stringify([]);
            var buf = _malloc(lengthBytesUTF8(errStr) + 1);
            stringToUTF8(errStr, buf, lengthBytesUTF8(errStr) + 1);
            dynCall('viip', cb, [0, buf, userData]);
            _free(buf);
        }
    });
});

EM_JS(void, js_webfs_read_backup, (const char* subfolderNamePtr, WebFsReadCallback cb, void* userData), {
    var subfolderName = UTF8ToString(subfolderNamePtr);

    async function doRead() {
        var bridge = window._webFsBackupBridge;
        if (!bridge) throw new Error("Bridge not initialized");

        var res = {
            mcconf: "",
            appconf: "",
            customconf: "",
            info: ""
        };

        var handle = bridge.dirHandle;
        if (handle) {
            try {
                var subDir = await handle.getDirectoryHandle(subfolderName);
                try {
                    var mcHandle = await subDir.getFileHandle("mcconf.xml");
                    res.mcconf = await (await mcHandle.getFile()).text();
                } catch (e) {}
                try {
                    var appHandle = await subDir.getFileHandle("appconf.xml");
                    res.appconf = await (await appHandle.getFile()).text();
                } catch (e) {}
                try {
                    var customHandle = await subDir.getFileHandle("customconf.xml");
                    res.customconf = await (await customHandle.getFile()).text();
                } catch (e) {}
                try {
                    var infoHandle = await subDir.getFileHandle("backup_info.json");
                    res.info = await (await infoHandle.getFile()).text();
                } catch (e) {}

                if (res.mcconf || res.appconf || res.customconf) {
                    return JSON.stringify(res);
                }
            } catch (eH) {
                console.warn("[WEBFS] Read from dirHandle failed, checking IndexedDB:", eH);
            }
        }

        // Fallback: read from IndexedDB
        var files = await bridge.loadBackupFromDb(subfolderName);
        if (files) {
            res.mcconf = files["mcconf.xml"] || "";
            res.appconf = files["appconf.xml"] || "";
            res.customconf = files["customconf.xml"] || "";
            res.info = files["backup_info.json"] || "";
            return JSON.stringify(res);
        }

        return JSON.stringify(res);
    }

    doRead().then(function(jsonRes) {
        if (cb) {
            var buf = _malloc(lengthBytesUTF8(jsonRes) + 1);
            stringToUTF8(jsonRes, buf, lengthBytesUTF8(jsonRes) + 1);
            dynCall('viip', cb, [1, buf, userData]);
            _free(buf);
        }
    }).catch(function(err) {
        console.error("[WEBFS] Read backup failed:", err);
        if (cb) {
            var errStr = JSON.stringify({ error: err.message });
            var buf = _malloc(lengthBytesUTF8(errStr) + 1);
            stringToUTF8(errStr, buf, lengthBytesUTF8(errStr) + 1);
            dynCall('viip', cb, [0, buf, userData]);
            _free(buf);
        }
    });
});

EM_JS(void, js_webfs_download_file, (const char* filenamePtr, const char* contentPtr, const char* mimePtr), {
    var filename = UTF8ToString(filenamePtr);
    var content = UTF8ToString(contentPtr);
    var mime = (mimePtr && UTF8ToString(mimePtr)) || 'application/xml';

    var blob = new Blob([content], { type: mime });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    setTimeout(function() {
        if (a.parentNode) a.parentNode.removeChild(a);
        URL.revokeObjectURL(url);
    }, 1500);
});

EM_JS(void, js_webfs_open_file_dialog, (const char* acceptPtr, WebFsFileContentCallback cb, void* userData), {
    var accept = (acceptPtr && UTF8ToString(acceptPtr)) || '.xml';
    var input = document.createElement('input');
    input.type = 'file';
    input.accept = accept;
    input.style.display = 'none';
    input.onchange = function(e) {
        var file = e.target.files && e.target.files[0];
        if (!file) {
            if (input.parentNode) input.parentNode.removeChild(input);
            return;
        }
        var fileName = file.name || 'config.xml';
        var reader = new FileReader();
        reader.onload = function(evt) {
            var text = evt.target.result || '';
            var nameLen = lengthBytesUTF8(fileName) + 1;
            var nameBuf = _malloc(nameLen);
            stringToUTF8(fileName, nameBuf, nameLen);

            var textLen = lengthBytesUTF8(text) + 1;
            var textBuf = _malloc(textLen);
            stringToUTF8(text, textBuf, textLen);

            if (cb) {
                dynCall('viip', cb, [nameBuf, textBuf, userData]);
            }

            _free(nameBuf);
            _free(textBuf);
            if (input.parentNode) input.parentNode.removeChild(input);
        };
        reader.onerror = function(err) {
            console.error('[WEBFS] Failed to read uploaded file:', err);
            if (input.parentNode) input.parentNode.removeChild(input);
        };
        reader.readAsText(file);
    };
    document.body.appendChild(input);
    input.click();
});

#endif // __EMSCRIPTEN__

void webfs_init(void)
{
#if defined(__EMSCRIPTEN__)
    js_webfs_init();
#endif
}

int webfs_is_supported(void)
{
#if defined(__EMSCRIPTEN__)
    return js_webfs_is_supported();
#else
    return 0;
#endif
}

const char *webfs_get_saved_folder_name(void)
{
#if defined(__EMSCRIPTEN__)
    static std::string s_name;
    char *namePtr = js_webfs_get_saved_folder_name();
    if (namePtr) {
        s_name = namePtr;
        free(namePtr);
        return s_name.c_str();
    }
    return "";
#else
    return "";
#endif
}

void webfs_select_folder(WebFsFolderCallback callback, void *userData)
{
#if defined(__EMSCRIPTEN__)
    js_webfs_select_folder(callback, userData);
#else
    if (callback) {
        callback(0, "Not supported on native", userData);
    }
#endif
}

void webfs_save_backup(const char *subfolderName, const char *filesJson, WebFsActionCallback callback, void *userData)
{
#if defined(__EMSCRIPTEN__)
    js_webfs_save_backup(subfolderName, filesJson, callback, userData);
#else
    if (callback) {
        callback(0, "Not supported on native", userData);
    }
#endif
}

void webfs_list_backups(WebFsListCallback callback, void *userData)
{
#if defined(__EMSCRIPTEN__)
    js_webfs_list_backups(callback, userData);
#else
    if (callback) {
        callback(0, "[]", userData);
    }
#endif
}

void webfs_read_backup(const char *subfolderName, WebFsReadCallback callback, void *userData)
{
#if defined(__EMSCRIPTEN__)
    js_webfs_read_backup(subfolderName, callback, userData);
#else
    if (callback) {
        callback(0, "{}", userData);
    }
#endif
}

void webfs_download_file(const char *filename, const char *content, const char *mimeType)
{
#if defined(__EMSCRIPTEN__)
    js_webfs_download_file(filename, content, mimeType);
#else
    (void)filename;
    (void)content;
    (void)mimeType;
#endif
}

void webfs_open_file_dialog(const char *acceptExtensions, WebFsFileContentCallback callback, void *userData)
{
#if defined(__EMSCRIPTEN__)
    js_webfs_open_file_dialog(acceptExtensions, callback, userData);
#else
    (void)acceptExtensions;
    (void)callback;
    (void)userData;
#endif
}

