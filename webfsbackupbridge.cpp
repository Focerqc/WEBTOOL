#include "webfsbackupbridge.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>

EM_JS(int, js_webfs_is_supported, (), {
    if (typeof window !== "undefined" && (("showDirectoryPicker" in window) || ("indexedDB" in window))) {
        return 1;
    }
    return 0;
});

EM_JS(int, js_webfs_is_directory_picker_supported, (), {
    if (typeof window !== "undefined" && typeof window.showDirectoryPicker === "function") {
        return 1;
    }
    return 0;
});

EM_JS(void, js_webfs_init, (), {
    if (typeof window === "undefined") return;

    if (!window._webFsBackupBridge) {
        window._webFsBackupBridge = {
            dirHandle: null,
            dirName: "",
            dbName: "VescWebBackupDB",
            storeName: "dirHandles",

            openDb: function() {
                var self = this;
                return new Promise(function(resolve, reject) {
                    if (!window.indexedDB) {
                        return reject(new Error("IndexedDB not supported"));
                    }
                    var req = window.indexedDB.open(self.dbName, 2);
                    req.onupgradeneeded = function(e) {
                        var db = e.target.result;
                        if (!db.objectStoreNames.contains("dirHandles")) {
                            db.createObjectStore("dirHandles");
                        }
                        if (!db.objectStoreNames.contains("backups")) {
                            db.createObjectStore("backups");
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
                        var tx = db.transaction("dirHandles", "readwrite");
                        tx.objectStore("dirHandles").put(handle, "currentBackupDir");
                        tx.oncomplete = function() { resolve(true); };
                        tx.onerror = function() { reject(tx.error); };
                    });
                });
            },

            loadHandleFromDb: function() {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction("dirHandles", "readonly");
                        var req = tx.objectStore("dirHandles").get("currentBackupDir");
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
                        var tx = db.transaction("backups", "readwrite");
                        tx.objectStore("backups").put(files, subfolderName);
                        tx.oncomplete = function() { resolve(true); };
                        tx.onerror = function() { reject(tx.error); };
                    });
                });
            },

            loadBackupFromDb: function(subfolderName) {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction("backups", "readonly");
                        var req = tx.objectStore("backups").get(subfolderName);
                        req.onsuccess = function() { resolve(req.result || null); };
                        req.onerror = function() { resolve(null); };
                    });
                });
            },

            listBackupsFromDb: function() {
                var self = this;
                return self.openDb().then(function(db) {
                    return new Promise(function(resolve, reject) {
                        var tx = db.transaction("backups", "readonly");
                        var store = tx.objectStore("backups");
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
                                    date: "",
                                    timestamp: ""
                                };
                                if (files["backup_info.json"]) {
                                    try {
                                        var parsed = JSON.parse(files["backup_info.json"]);
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
                if (readWrite) opts.mode = "readwrite";
                try {
                    if ((await handle.queryPermission(opts)) === "granted") {
                        return true;
                    }
                    if ((await handle.requestPermission(opts)) === "granted") {
                        return true;
                    }
                } catch (e) {
                    console.warn("[WEBFS] verifyPermission error:", e);
                }
                return false;
            }
        };

        // Modal for saving/exporting files on iPad/Bluefy/mobile
        window._showWebFsFileModal = function(filename, content, mime) {
            if (typeof document === "undefined") return;
            var existing = document.getElementById("webfs-file-modal");
            if (existing && existing.parentNode) {
                existing.parentNode.removeChild(existing);
            }

            var sizeKb = (new Blob([content]).size / 1024).toFixed(1);
            var modal = document.createElement("div");
            modal.id = "webfs-file-modal";
            modal.style.cssText = "position:fixed;top:0;left:0;right:0;bottom:0;z-index:9999999;background:rgba(0,0,0,0.75);display:flex;align-items:center;justify-content:center;padding:16px;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;";

            var card = document.createElement("div");
            card.style.cssText = "background:#1c1e22;color:#f0f0f0;border:1px solid #444;border-radius:12px;max-width:440px;width:100%;max-height:90vh;overflow-y:auto;padding:20px;box-sizing:border-box;box-shadow:0 12px 36px rgba(0,0,0,0.8);display:flex;flex-direction:column;gap:12px;";

            card.innerHTML = 
                "<div style=\"display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #333;padding-bottom:10px;\">" +
                    "<div style=\"font-weight:bold;font-size:15px;color:#fff;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;padding-right:8px;\">" +
                        "&#128196; " + filename +
                    "</div>" +
                    "<span style=\"font-size:11px;background:#333;color:#aaa;padding:2px 8px;border-radius:10px;white-space:nowrap;\">" + sizeKb + " KB</span>" +
                "</div>" +
                "<div style=\"font-size:12px;color:#bbb;line-height:1.4;\">" +
                    "Save, share, or copy your XML configuration:" +
                "</div>" +
                "<div style=\"display:flex;flex-direction:column;gap:8px;\">" +
                    "<button id=\"webfs-btn-copy\" style=\"background:#2ecc71;color:#000;border:none;border-radius:6px;padding:11px 14px;font-size:13px;font-weight:bold;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:6px;\">" +
                        "&#128203; Copy XML to Clipboard" +
                    "</button>" +
                    "<button id=\"webfs-btn-share\" style=\"background:#3498db;color:#fff;border:none;border-radius:6px;padding:11px 14px;font-size:13px;font-weight:bold;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:6px;\">" +
                        "&#128228; Save to Files / Share Sheet" +
                    "</button>" +
                    "<button id=\"webfs-btn-download\" style=\"background:#3a3f47;color:#eee;border:1px solid #555;border-radius:6px;padding:10px 14px;font-size:13px;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:6px;\">" +
                        "&#128190; Direct Download (.xml)" +
                    "</button>" +
                    "<button id=\"webfs-btn-view\" style=\"background:transparent;color:#95a5a6;border:1px dashed #555;border-radius:6px;padding:8px 14px;font-size:12px;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:6px;\">" +
                        "&#128065; View XML Text" +
                    "</button>" +
                "</div>" +
                "<div id=\"webfs-view-area\" style=\"display:none;flex-direction:column;gap:6px;margin-top:4px;\">" +
                    "<textarea id=\"webfs-xml-text\" readonly style=\"width:100%;height:140px;background:#111;color:#0f0;font-family:monospace;font-size:11px;padding:8px;border-radius:4px;border:1px solid #333;box-sizing:border-box;resize:vertical;\"></textarea>" +
                    "<button id=\"webfs-btn-select-all\" style=\"background:#444;color:#fff;border:none;border-radius:4px;padding:6px;font-size:11px;cursor:pointer;\">Select All Text</button>" +
                "</div>" +
                "<button id=\"webfs-btn-close\" style=\"background:#2a2e33;color:#ccc;border:none;border-radius:6px;padding:9px;font-size:13px;cursor:pointer;margin-top:4px;\">" +
                    "Done / Close" +
                "</button>";

            modal.appendChild(card);
            document.body.appendChild(modal);

            var copyBtn = document.getElementById("webfs-btn-copy");
            var shareBtn = document.getElementById("webfs-btn-share");
            var dlBtn = document.getElementById("webfs-btn-download");
            var viewBtn = document.getElementById("webfs-btn-view");
            var closeBtn = document.getElementById("webfs-btn-close");
            var viewArea = document.getElementById("webfs-view-area");
            var xmlText = document.getElementById("webfs-xml-text");
            var selectAllBtn = document.getElementById("webfs-btn-select-all");

            xmlText.value = content;

            function updateCopyStatus() {
                copyBtn.innerHTML = "&#10003; Copied to Clipboard!";
                copyBtn.style.background = "#27ae60";
                setTimeout(function() {
                    copyBtn.innerHTML = "&#128203; Copy XML to Clipboard";
                    copyBtn.style.background = "#2ecc71";
                }, 2500);
            }

            copyBtn.onclick = function() {
                if (navigator.clipboard && navigator.clipboard.writeText) {
                    navigator.clipboard.writeText(content).then(updateCopyStatus).catch(function() {
                        xmlText.select();
                        document.execCommand("copy");
                        updateCopyStatus();
                    });
                } else {
                    xmlText.select();
                    document.execCommand("copy");
                    updateCopyStatus();
                }
            };

            shareBtn.onclick = function() {
                if (navigator.share) {
                    try {
                        var f = new File([content], filename, { type: mime || "application/xml" });
                        if (navigator.canShare && navigator.canShare({ files: [f] })) {
                            navigator.share({ files: [f], title: filename }).catch(function(e){ console.log(e); });
                            return;
                        }
                    } catch (ex) {}
                    navigator.share({ title: filename, text: content }).catch(function(e){ console.log(e); });
                } else {
                    alert("Web Share is not supported in this browser. Please use 'Copy XML to Clipboard' or 'Direct Download'.");
                }
            };

            dlBtn.onclick = function() {
                try {
                    var blob = new Blob([content], { type: mime || "application/xml" });
                    var url = URL.createObjectURL(blob);
                    var a = document.createElement("a");
                    a.href = url;
                    a.download = filename;
                    document.body.appendChild(a);
                    a.click();
                    setTimeout(function() {
                        if (a.parentNode) a.parentNode.removeChild(a);
                        URL.revokeObjectURL(url);
                    }, 2000);
                } catch(e) {
                    console.error(e);
                }
            };

            viewBtn.onclick = function() {
                if (viewArea.style.display === "none") {
                    viewArea.style.display = "flex";
                    viewBtn.innerHTML = "Hide XML Text";
                } else {
                    viewArea.style.display = "none";
                    viewBtn.innerHTML = "&#128065; View XML Text";
                }
            };

            selectAllBtn.onclick = function() {
                xmlText.focus();
                xmlText.select();
            };

            function dismiss() {
                if (modal && modal.parentNode) {
                    modal.parentNode.removeChild(modal);
                }
            }

            closeBtn.onclick = dismiss;
            modal.onclick = function(e) {
                if (e.target === modal) dismiss();
            };
        };

        // Modal for importing/loading XML on iPad/Bluefy/mobile
        window._showWebFsImportModal = function(accept, cb) {
            if (typeof document === "undefined") return;
            var existing = document.getElementById("webfs-import-modal");
            if (existing && existing.parentNode) {
                existing.parentNode.removeChild(existing);
            }

            var modal = document.createElement("div");
            modal.id = "webfs-import-modal";
            modal.style.cssText = "position:fixed;top:0;left:0;right:0;bottom:0;z-index:9999999;background:rgba(0,0,0,0.75);display:flex;align-items:center;justify-content:center;padding:16px;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;";

            var card = document.createElement("div");
            card.style.cssText = "background:#1c1e22;color:#f0f0f0;border:1px solid #444;border-radius:12px;max-width:440px;width:100%;max-height:90vh;overflow-y:auto;padding:20px;box-sizing:border-box;box-shadow:0 12px 36px rgba(0,0,0,0.8);display:flex;flex-direction:column;gap:12px;";

            card.innerHTML = 
                "<div style=\"display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #333;padding-bottom:10px;\">" +
                    "<div style=\"font-weight:bold;font-size:15px;color:#fff;\">" +
                        "&#128193; Load Configuration (XML)" +
                    "</div>" +
                "</div>" +
                "<div style=\"font-size:12px;color:#bbb;line-height:1.4;\">" +
                    "Select an XML file from your device, or paste the XML text directly:" +
                "</div>" +
                "<div style=\"display:flex;flex-direction:column;gap:8px;\">" +
                    "<label style=\"background:#3498db;color:#fff;border-radius:6px;padding:11px 14px;font-size:13px;font-weight:bold;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:6px;text-align:center;\">" +
                        "&#128193; Choose File from Device" +
                        "<input type=\"file\" id=\"webfs-file-input\" accept=\"" + (accept || ".xml") + "\" style=\"display:none;\" />" +
                    "</label>" +
                    "<div style=\"text-align:center;font-size:11px;color:#777;margin:2px 0;\">&mdash; OR PASTE XML BELOW &mdash;</div>" +
                    "<textarea id=\"webfs-import-text\" placeholder=\"Paste <MCConfiguration>, <APPConfiguration>, or <CustomConfiguration> XML here...\" style=\"width:100%;height:110px;background:#111;color:#eee;font-family:monospace;font-size:11px;padding:8px;border-radius:4px;border:1px solid #333;box-sizing:border-box;resize:vertical;\"></textarea>" +
                    "<button id=\"webfs-btn-load-pasted\" style=\"background:#2ecc71;color:#000;border:none;border-radius:6px;padding:11px 14px;font-size:13px;font-weight:bold;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:6px;\">" +
                        "&#10003; Load Pasted XML" +
                    "</button>" +
                "</div>" +
                "<button id=\"webfs-btn-import-cancel\" style=\"background:#2a2e33;color:#ccc;border:none;border-radius:6px;padding:9px;font-size:13px;cursor:pointer;margin-top:4px;\">" +
                    "Cancel" +
                "</button>";

            modal.appendChild(card);
            document.body.appendChild(modal);

            function dismiss() {
                if (modal && modal.parentNode) {
                    modal.parentNode.removeChild(modal);
                }
            }

            var fileInput = document.getElementById("webfs-file-input");
            var importText = document.getElementById("webfs-import-text");
            var loadPastedBtn = document.getElementById("webfs-btn-load-pasted");
            var cancelBtn = document.getElementById("webfs-btn-import-cancel");

            fileInput.onchange = function(e) {
                var file = e.target.files && e.target.files[0];
                if (!file) return;
                var reader = new FileReader();
                reader.onload = function(evt) {
                    var text = evt.target.result || "";
                    dismiss();
                    if (cb) cb(file.name || "config.xml", text);
                };
                reader.readAsText(file);
            };

            loadPastedBtn.onclick = function() {
                var text = importText.value.trim();
                if (!text) {
                    alert("Please paste XML content first.");
                    return;
                }
                dismiss();
                if (cb) cb("pasted_config.xml", text);
            };

            cancelBtn.onclick = dismiss;
            modal.onclick = function(e) {
                if (e.target === modal) dismiss();
            };
        };

        // Modal for saving/sharing ZIP bundles on iPad/Bluefy/mobile
        window._showWebFsZipModal = function(zipFilename, zipBlob, fileNamesList) {
            if (typeof document === "undefined") return;
            var existing = document.getElementById("webfs-file-modal");
            if (existing && existing.parentNode) {
                existing.parentNode.removeChild(existing);
            }

            var sizeKb = (zipBlob.size / 1024).toFixed(1);
            var modal = document.createElement("div");
            modal.id = "webfs-file-modal";
            modal.style.cssText = "position:fixed;top:0;left:0;right:0;bottom:0;z-index:9999999;background:rgba(0,0,0,0.75);display:flex;align-items:center;justify-content:center;padding:16px;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;";

            var card = document.createElement("div");
            card.style.cssText = "background:#1c1e22;color:#f0f0f0;border:1px solid #444;border-radius:12px;max-width:440px;width:100%;max-height:90vh;overflow-y:auto;padding:20px;box-sizing:border-box;box-shadow:0 12px 36px rgba(0,0,0,0.8);display:flex;flex-direction:column;gap:12px;";

            var fileListHtml = "";
            for (var i = 0; i < fileNamesList.length; i++) {
                fileListHtml += "<div style=\"font-family:monospace;font-size:12px;color:#2ecc71;padding:2px 0;\">&#128196; " + fileNamesList[i] + "</div>";
            }

            card.innerHTML = 
                "<div style=\"display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #333;padding-bottom:10px;\">" +
                    "<div style=\"font-weight:bold;font-size:15px;color:#fff;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;padding-right:8px;\">" +
                        "&#128230; " + zipFilename +
                    "</div>" +
                    "<span style=\"font-size:11px;background:#333;color:#aaa;padding:2px 8px;border-radius:10px;white-space:nowrap;\">" + sizeKb + " KB</span>" +
                "</div>" +
                "<div style=\"font-size:12px;color:#bbb;line-height:1.4;\">" +
                    "All XML configuration files bundled into a single ZIP archive:" +
                "</div>" +
                "<div style=\"background:#111;padding:10px;border-radius:6px;border:1px solid #333;\">" +
                    fileListHtml +
                "</div>" +
                "<div style=\"display:flex;flex-direction:column;gap:8px;\">" +
                    "<button id=\"webfs-btn-zip-share\" style=\"background:#3498db;color:#fff;border:none;border-radius:6px;padding:12px 14px;font-size:14px;font-weight:bold;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:8px;\">" +
                        "&#128228; Save ZIP to Files / Share Sheet" +
                    "</button>" +
                    "<button id=\"webfs-btn-zip-download\" style=\"background:#3a3f47;color:#eee;border:1px solid #555;border-radius:6px;padding:11px 14px;font-size:13px;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:8px;\">" +
                        "&#128190; Direct Download (.zip)" +
                    "</button>" +
                "</div>" +
                "<button id=\"webfs-btn-zip-close\" style=\"background:#2a2e33;color:#ccc;border:none;border-radius:6px;padding:9px;font-size:13px;cursor:pointer;margin-top:4px;\">" +
                    "Done / Close" +
                "</button>";

            modal.appendChild(card);
            document.body.appendChild(modal);

            var shareBtn = document.getElementById("webfs-btn-zip-share");
            var dlBtn = document.getElementById("webfs-btn-zip-download");
            var closeBtn = document.getElementById("webfs-btn-zip-close");

            function dismiss() {
                if (modal && modal.parentNode) {
                    modal.parentNode.removeChild(modal);
                }
            }

            shareBtn.onclick = function() {
                if (navigator.share && navigator.canShare) {
                    try {
                        var fileObj = new File([zipBlob], zipFilename, { type: "application/zip" });
                        if (navigator.canShare({ files: [fileObj] })) {
                            navigator.share({ files: [fileObj], title: zipFilename }).catch(function(e){ console.log(e); });
                            return;
                        }
                    } catch (ex) {
                        console.warn("[WEBFS] Share failed:", ex);
                    }
                }
                dlBtn.click();
            };

            dlBtn.onclick = function() {
                try {
                    var url = URL.createObjectURL(zipBlob);
                    var a = document.createElement("a");
                    a.href = url;
                    a.download = zipFilename;
                    a.style.display = "none";
                    document.body.appendChild(a);
                    a.click();
                    setTimeout(function() {
                        if (a.parentNode) a.parentNode.removeChild(a);
                        URL.revokeObjectURL(url);
                    }, 3000);
                } catch(e) {
                    console.error("[WEBFS] Download error:", e);
                }
            };

            closeBtn.onclick = dismiss;
            modal.onclick = function(e) {
                if (e.target === modal) dismiss();
            };
        };

        // Attempt to reload handle silently on init
        window._webFsBackupBridge.loadHandleFromDb().then(function(handle) {
            if (handle) {
                window._webFsBackupBridge.dirHandle = handle;
                window._webFsBackupBridge.dirName = handle.name || "Saved Folder";
                console.log("[WEBFS] Restored saved backup directory handle from IndexedDB:", window._webFsBackupBridge.dirName);
            }
        });
    }
});

EM_JS(char*, js_webfs_get_saved_folder_name, (), {
    if (typeof window !== "undefined") {
        var name = "";
        if (window._webFsBackupBridge && window._webFsBackupBridge.dirName) {
            name = window._webFsBackupBridge.dirName;
        } else if (!("showDirectoryPicker" in window)) {
            name = "Browser Storage (Downloads)";
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
    if (typeof window === "undefined" || !window._webFsBackupBridge) {
        if (cb) {
            var msg = "Not initialized";
            var buf = _malloc(lengthBytesUTF8(msg) + 1);
            stringToUTF8(msg, buf, lengthBytesUTF8(msg) + 1);
            dynCall("viip", cb, [0, buf, userData]);
            _free(buf);
        }
        return;
    }

    if (typeof window.showDirectoryPicker !== "function") {
        var msg = "Browser Storage (Downloads)";
        if (cb) {
            var buf = _malloc(lengthBytesUTF8(msg) + 1);
            stringToUTF8(msg, buf, lengthBytesUTF8(msg) + 1);
            dynCall("viip", cb, [1, buf, userData]);
            _free(buf);
        }
        return;
    }

    window.showDirectoryPicker({ mode: "readwrite" }).then(async function(handle) {
        window._webFsBackupBridge.dirHandle = handle;
        window._webFsBackupBridge.dirName = handle.name || "Backup Folder";
        await window._webFsBackupBridge.saveHandleToDb(handle);

        console.log("[WEBFS] Selected directory:", window._webFsBackupBridge.dirName);
        if (cb) {
            var buf = _malloc(lengthBytesUTF8(window._webFsBackupBridge.dirName) + 1);
            stringToUTF8(window._webFsBackupBridge.dirName, buf, lengthBytesUTF8(window._webFsBackupBridge.dirName) + 1);
            dynCall("viip", cb, [1, buf, userData]);
            _free(buf);
        }
    }).catch(function(err) {
        console.warn("[WEBFS] showDirectoryPicker cancelled or failed:", err);
        if (cb) {
            var errStr = (err && err.message) ? err.message : "Cancelled";
            var buf = _malloc(lengthBytesUTF8(errStr) + 1);
            stringToUTF8(errStr, buf, lengthBytesUTF8(errStr) + 1);
            dynCall("viip", cb, [0, buf, userData]);
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
        if (!handle && typeof window.showDirectoryPicker === "function") {
            try {
                handle = await window.showDirectoryPicker({ mode: "readwrite" });
                bridge.dirHandle = handle;
                bridge.dirName = handle.name;
                await bridge.saveHandleToDb(handle);
            } catch (ePick) {
                console.warn("[WEBFS] Directory picker skipped or cancelled:", ePick);
            }
        }

        if (handle) {
            var hasPerm = await bridge.verifyPermission(handle, true);
            if (!hasPerm && typeof window.showDirectoryPicker === "function") {
                try {
                    handle = await window.showDirectoryPicker({ mode: "readwrite" });
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

        // On browsers without Directory Picker (mobile Safari, mobile Chrome, Bluefy), provide file modal/share!
        if (!handle) {
            try {
                var dlBlob = new Blob([filesJsonStr], { type: "application/json" });
                var dlUrl = URL.createObjectURL(dlBlob);
                var dlA = document.createElement("a");
                dlA.href = dlUrl;
                dlA.download = subfolderName + ".json";
                dlA.style.display = "none";
                document.body.appendChild(dlA);
                dlA.click();
                setTimeout(function() {
                    if (dlA.parentNode) dlA.parentNode.removeChild(dlA);
                    URL.revokeObjectURL(dlUrl);
                }, 3000);
            } catch (eDl) {}

            if (window._showWebFsFileModal) {
                var isMobile = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent) || (navigator.maxTouchPoints && navigator.maxTouchPoints > 2);
                if (isMobile) {
                    window._showWebFsFileModal(subfolderName + ".json", filesJsonStr, "application/json");
                }
            }
            return "Saved to Browser Storage (IndexedDB).";
        }

        return "Saved successfully to " + subfolderName;
    }

    doSave().then(function(resMsg) {
        if (cb) {
            var buf = _malloc(lengthBytesUTF8(resMsg) + 1);
            stringToUTF8(resMsg, buf, lengthBytesUTF8(resMsg) + 1);
            dynCall("viip", cb, [1, buf, userData]);
            _free(buf);
        }
    }).catch(function(err) {
        console.error("[WEBFS] Save backup failed:", err);
        if (cb) {
            var errStr = (err && err.message) ? err.message : "Write failed";
            var buf = _malloc(lengthBytesUTF8(errStr) + 1);
            stringToUTF8(errStr, buf, lengthBytesUTF8(errStr) + 1);
            dynCall("viip", cb, [0, buf, userData]);
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
                    if (entry.kind === "directory") {
                        var folderName = entry.name;
                        var info = {
                            folderName: folderName,
                            name: folderName,
                            canId: -1,
                            date: "",
                            timestamp: ""
                        };

                        try {
                            var subDir = await handle.getDirectoryHandle(folderName);
                            try {
                                var infoHandle = await subDir.getFileHandle("backup_info.json");
                                var file = await infoHandle.getFile();
                                var text = await file.text();
                                var parsed = JSON.parse(text);
                                if (parsed.deviceName) info.name = parsed.deviceName;
                                if (parsed.canId !== undefined) info.canId = parsed.canId;
                                if (parsed.dateFormatted) info.date = parsed.dateFormatted;
                                if (parsed.timestamp) info.timestamp = parsed.timestamp;
                            } catch (eInfo) {
                                var parts = folderName.split("_");
                                if (parts.length >= 1) info.name = parts[0];
                                if (parts.length >= 2 && parts[1].startsWith("CAN")) {
                                    info.canId = parseInt(parts[1].replace("CAN", ""));
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
            dynCall("viip", cb, [1, buf, userData]);
            _free(buf);
        }
    }).catch(function(err) {
        console.error("[WEBFS] List backups failed:", err);
        if (cb) {
            var errStr = JSON.stringify([]);
            var buf = _malloc(lengthBytesUTF8(errStr) + 1);
            stringToUTF8(errStr, buf, lengthBytesUTF8(errStr) + 1);
            dynCall("viip", cb, [0, buf, userData]);
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
            dynCall("viip", cb, [1, buf, userData]);
            _free(buf);
        }
    }).catch(function(err) {
        console.error("[WEBFS] Read backup failed:", err);
        if (cb) {
            var errStr = JSON.stringify({ error: err.message });
            var buf = _malloc(lengthBytesUTF8(errStr) + 1);
            stringToUTF8(errStr, buf, lengthBytesUTF8(errStr) + 1);
            dynCall("viip", cb, [0, buf, userData]);
            _free(buf);
        }
    });
});

EM_JS(void, js_webfs_download_file, (const char* filenamePtr, const char* contentPtr, const char* mimePtr), {
    var filename = UTF8ToString(filenamePtr);
    var content = UTF8ToString(contentPtr);
    var mime = (mimePtr && UTF8ToString(mimePtr)) || "application/xml";

    // 1. If Web Share API is supported, attempt native share sheet (e.g. iOS Safari "Save to Files")
    var isShared = false;
    if (typeof navigator !== "undefined" && navigator.share && navigator.canShare) {
        try {
            var fileObj = new File([content], filename, { type: mime });
            if (navigator.canShare({ files: [fileObj] })) {
                navigator.share({
                    files: [fileObj],
                    title: filename
                }).then(function() {
                    console.log("[WEBFS] Shared successfully:", filename);
                }).catch(function(e) {
                    console.log("[WEBFS] Share dismissed or failed, showing action modal:", e);
                    if (window._showWebFsFileModal) {
                        window._showWebFsFileModal(filename, content, mime);
                    }
                });
                isShared = true;
            }
        } catch(err) {
            console.warn("[WEBFS] Web Share attempt threw:", err);
        }
    }

    // 2. Standard anchor download fallback
    try {
        var blob = new Blob([content], { type: mime });
        var url = URL.createObjectURL(blob);
        var a = document.createElement("a");
        a.href = url;
        a.download = filename;
        a.style.display = "none";
        document.body.appendChild(a);
        a.click();
        setTimeout(function() {
            if (a.parentNode) a.parentNode.removeChild(a);
            URL.revokeObjectURL(url);
        }, 3000);
    } catch (eDl) {
        console.warn("[WEBFS] Anchor download failed:", eDl);
    }

    // 3. If on mobile / iPad / Bluefy or not triggered via share, open file action modal overlay
    if (window._showWebFsFileModal) {
        var isMobile = /iPhone|iPad|iPod|Android/i.test(navigator.userAgent) || (navigator.maxTouchPoints && navigator.maxTouchPoints > 2);
        if (!isShared || isMobile) {
            window._showWebFsFileModal(filename, content, mime);
        }
    }
});

EM_JS(void, js_webfs_open_file_dialog, (const char* acceptPtr, WebFsFileContentCallback cb, void* userData), {
    var accept = (acceptPtr && UTF8ToString(acceptPtr)) || ".xml";

    function handleResult(fileName, text) {
        var nameLen = lengthBytesUTF8(fileName) + 1;
        var nameBuf = _malloc(nameLen);
        stringToUTF8(fileName, nameBuf, nameLen);

        var textLen = lengthBytesUTF8(text) + 1;
        var textBuf = _malloc(textLen);
        stringToUTF8(text, textBuf, textLen);

        if (cb) {
            dynCall("viip", cb, [nameBuf, textBuf, userData]);
        }

        _free(nameBuf);
        _free(textBuf);
    }

    if (window._showWebFsImportModal) {
        window._showWebFsImportModal(accept, handleResult);
    } else {
        var input = document.createElement("input");
        input.type = "file";
        input.accept = accept;
        input.style.display = "none";
        input.onchange = function(e) {
            var file = e.target.files && e.target.files[0];
            if (!file) {
                if (input.parentNode) input.parentNode.removeChild(input);
                return;
            }
            var fileName = file.name || "config.xml";
            var reader = new FileReader();
            reader.onload = function(evt) {
                var text = evt.target.result || "";
                handleResult(fileName, text);
                if (input.parentNode) input.parentNode.removeChild(input);
            };
            reader.readAsText(file);
        };
        document.body.appendChild(input);
        input.click();
    }
});

EM_JS(void, js_webfs_download_zip_bundle, (const char* zipFilenamePtr, const char* filesJsonPtr), {
    var zipFilename = (zipFilenamePtr && UTF8ToString(zipFilenamePtr)) || "vesc_backup.zip";
    var filesJson = (filesJsonPtr && UTF8ToString(filesJsonPtr)) || "{}";

    var filesMap = {};
    try {
        filesMap = JSON.parse(filesJson);
    } catch(e) {
        console.error("[WEBFS] Failed to parse filesJson:", e);
        return;
    }

    // CRC-32 Table & computation
    var crcTable = (function() {
        var c, table = [];
        for (var n = 0; n < 256; n++) {
            c = n;
            for (var k = 0; k < 8; k++) {
                c = ((c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1));
            }
            table[n] = c;
        }
        return table;
    })();

    function crc32(u8) {
        var crc = 0 ^ (-1);
        for (var i = 0; i < u8.length; i++) {
            crc = (crc >>> 8) ^ crcTable[(crc ^ u8[i]) & 0xFF];
        }
        return (crc ^ (-1)) >>> 0;
    }

    var encoder = new TextEncoder();
    var localHeaders = [];
    var centralHeaders = [];
    var offset = 0;
    var fileNamesList = [];

    var date = new Date();
    var dosTime = (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1);
    var dosDate = ((date.getFullYear() - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate();

    for (var name in filesMap) {
        if (!filesMap.hasOwnProperty(name)) continue;
        var data = filesMap[name];
        if (typeof data !== "string") {
            data = JSON.stringify(data, null, 2);
        }
        var dataBytes = encoder.encode(data);
        var nameBytes = encoder.encode(name);
        var crc = crc32(dataBytes);
        var size = dataBytes.length;
        fileNamesList.push(name);

        // Local header: 30 + nameLen + size
        var lh = new Uint8Array(30 + nameBytes.length + size);
        var view = new DataView(lh.buffer);
        view.setUint32(0, 0x04034b50, true);
        view.setUint16(4, 20, true);
        view.setUint16(6, 0x0800, true); // UTF-8
        view.setUint16(8, 0, true);      // Store
        view.setUint16(10, dosTime, true);
        view.setUint16(12, dosDate, true);
        view.setUint32(14, crc, true);
        view.setUint32(18, size, true);
        view.setUint32(22, size, true);
        view.setUint16(26, nameBytes.length, true);
        view.setUint16(28, 0, true);
        lh.set(nameBytes, 30);
        lh.set(dataBytes, 30 + nameBytes.length);
        localHeaders.push(lh);

        // Central directory header: 46 + nameLen
        var ch = new Uint8Array(46 + nameBytes.length);
        var chView = new DataView(ch.buffer);
        chView.setUint32(0, 0x02014b50, true);
        chView.setUint16(4, 20, true);
        chView.setUint16(6, 20, true);
        chView.setUint16(8, 0x0800, true);
        chView.setUint16(10, 0, true);
        chView.setUint16(12, dosTime, true);
        chView.setUint16(14, dosDate, true);
        chView.setUint32(16, crc, true);
        chView.setUint32(20, size, true);
        chView.setUint32(24, size, true);
        chView.setUint16(28, nameBytes.length, true);
        chView.setUint16(30, 0, true);
        chView.setUint16(32, 0, true);
        chView.setUint16(34, 0, true);
        chView.setUint16(36, 0, true);
        chView.setUint32(38, 0, true);
        chView.setUint32(42, offset, true);
        ch.set(nameBytes, 46);
        centralHeaders.push(ch);

        offset += lh.length;
    }

    var cdOffset = offset;
    var cdSize = 0;
    for (var i = 0; i < centralHeaders.length; i++) {
        cdSize += centralHeaders[i].length;
    }

    // End of central directory: 22 bytes
    var eocd = new Uint8Array(22);
    var eocdView = new DataView(eocd.buffer);
    eocdView.setUint32(0, 0x06054b50, true);
    eocdView.setUint16(4, 0, true);
    eocdView.setUint16(6, 0, true);
    eocdView.setUint16(8, centralHeaders.length, true);
    eocdView.setUint16(10, centralHeaders.length, true);
    eocdView.setUint32(12, cdSize, true);
    eocdView.setUint32(16, cdOffset, true);
    eocdView.setUint16(20, 0, true);

    var totalChunks = localHeaders.concat(centralHeaders, [eocd]);
    var zipBlob = new Blob(totalChunks, { type: "application/zip" });

    // Show Zip modal
    if (window._showWebFsZipModal) {
        window._showWebFsZipModal(zipFilename, zipBlob, fileNamesList);
    }
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

int webfs_is_directory_picker_supported(void)
{
#if defined(__EMSCRIPTEN__)
    return js_webfs_is_directory_picker_supported();
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

void webfs_download_zip_bundle(const char *zipFilename, const char *filesJson)
{
#if defined(__EMSCRIPTEN__)
    js_webfs_download_zip_bundle(zipFilename, filesJson);
#else
    (void)zipFilename;
    (void)filesJson;
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
