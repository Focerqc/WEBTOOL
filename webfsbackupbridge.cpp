#include "webfsbackupbridge.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>

EM_JS(int, js_webfs_is_supported, (), {
    if (typeof window !== 'undefined' && 'showDirectoryPicker' in window) {
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
                    var req = window.indexedDB.open(self.dbName, 1);
                    req.onupgradeneeded = function(e) {
                        var db = e.target.result;
                        if (!db.objectStoreNames.contains(self.storeName)) {
                            db.createObjectStore(self.storeName);
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
                        var tx = db.transaction(self.storeName, 'readwrite');
                        tx.objectStore(self.storeName).put(handle, 'currentBackupDir');
                        tx.oncomplete = function() { resolve(true); };
                        tx.onerror = function() { reject(tx.error); };
                    });
                });
            },

            loadHandleFromDb: function() {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction(self.storeName, 'readonly');
                        var req = tx.objectStore(self.storeName).get('currentBackupDir');
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
    if (typeof window !== 'undefined' && window._webFsBackupBridge && window._webFsBackupBridge.dirName) {
        var name = window._webFsBackupBridge.dirName;
        var lengthBytes = lengthBytesUTF8(name) + 1;
        var stringOnWasmHeap = _malloc(lengthBytes);
        stringToUTF8(name, stringOnWasmHeap, lengthBytes);
        return stringOnWasmHeap;
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
        if (!handle) {
            handle = await window.showDirectoryPicker({ mode: 'readwrite' });
            bridge.dirHandle = handle;
            bridge.dirName = handle.name;
            await bridge.saveHandleToDb(handle);
        } else {
            var hasPerm = await bridge.verifyPermission(handle, true);
            if (!hasPerm) {
                handle = await window.showDirectoryPicker({ mode: 'readwrite' });
                bridge.dirHandle = handle;
                bridge.dirName = handle.name;
                await bridge.saveHandleToDb(handle);
            }
        }

        var files = JSON.parse(filesJsonStr);
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

        if (!handle) {
            return JSON.stringify([]);
        }

        var hasPerm = await bridge.verifyPermission(handle, false);
        if (!hasPerm) {
            return JSON.stringify([]);
        }

        var list = [];
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
                        // Infer from folder name (e.g. Thor_CAN3_2026-09-18_06-30-00)
                        var parts = folderName.split('_');
                        if (parts.length >= 1) info.name = parts[0];
                        if (parts.length >= 2 && parts[1].startsWith('CAN')) {
                            info.canId = parseInt(parts[1].replace('CAN', ''));
                        }
                    }
                    list.push(info);
                } catch (eDir) {
                    console.warn("[WEBFS] Error inspecting dir entry:", folderName, eDir);
                }
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
        var handle = bridge.dirHandle;
        if (!handle) throw new Error("No directory handle available");

        var subDir = await handle.getDirectoryHandle(subfolderName);
        var res = {
            mcconf: "",
            appconf: "",
            customconf: "",
            info: ""
        };

        try {
            var mcHandle = await subDir.getFileHandle("mcconf.xml");
            var mcFile = await mcHandle.getFile();
            res.mcconf = await mcFile.text();
        } catch (e) {}

        try {
            var appHandle = await subDir.getFileHandle("appconf.xml");
            var appFile = await appHandle.getFile();
            res.appconf = await appFile.text();
        } catch (e) {}

        try {
            var customHandle = await subDir.getFileHandle("customconf.xml");
            var customFile = await customHandle.getFile();
            res.customconf = await customFile.text();
        } catch (e) {}

        try {
            var infoHandle = await subDir.getFileHandle("backup_info.json");
            var infoFile = await infoHandle.getFile();
            res.info = await infoFile.text();
        } catch (e) {}

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
