#include "webblebridge.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>

EM_JS(int, js_webble_is_supported, (), {
    if (typeof navigator !== 'undefined' && 'bluetooth' in navigator) {
        return 1;
    }
    return 0;
});

EM_JS(int, js_webble_init, (), {
    if (typeof window === 'undefined') return 0;
    if (!('bluetooth' in navigator)) {
        console.warn("[WEBBLE] Web Bluetooth API is not supported in this browser.");
        return 0;
    }

    if (!window._webBleBridge) {
        window._webBleBridge = {
            devices: {},
            activeDevice: null,
            gattServer: null,
            rxChar: null,
            txChar: null,
            isConnecting: false,
            isConnected: false,
            writeQueue: [],
            isWriting: false,
            bytesRx: 0,
            bytesTx: 0,
            rxCallback: null,
            stateCallback: null,
            scanCallback: null,
            userData: null,

            NUS_SERVICE_UUID: '6e400001-b5a3-f393-e0a9-e50e24dcca9e',
            NUS_RX_UUID:      '6e400002-b5a3-f393-e0a9-e50e24dcca9e',
            NUS_TX_UUID:      '6e400003-b5a3-f393-e0a9-e50e24dcca9e',

            dispatchState: function(state, name, addr) {
                var self = this;
                if (typeof window !== 'undefined' && window.__logToScreen) {
                    var sStr = state === 2 ? 'Connected' : (state === 1 ? 'Connecting' : (state === 0 ? 'Disconnected' : 'Error'));
                    window.__logToScreen('[BLE STATE] ' + sStr + ' (' + (name || 'device') + ')', state === 2 ? '#4ade80' : '#facc15');
                }
                if (typeof window !== 'undefined' && typeof window.updateBleHud === 'function') {
                    window.updateBleHud();
                }
                if (self.stateCallback) {
                    try {
                        var namePtr = 0, addrPtr = 0;
                        if (name && typeof allocateUTF8 === 'function') namePtr = allocateUTF8(name);
                        if (addr && typeof allocateUTF8 === 'function') addrPtr = allocateUTF8(addr);

                        var table = (typeof wasmTable !== 'undefined') ? wasmTable : (typeof Module !== 'undefined' ? Module['wasmTable'] : null);
                        if (table) {
                            table.get(self.stateCallback)(state, namePtr, addrPtr, self.userData);
                        }
                        if (namePtr && typeof _free === 'function') _free(namePtr);
                        if (addrPtr && typeof _free === 'function') _free(addrPtr);
                    } catch (e) {
                        console.error("[WEBBLE] Error dispatching state callback:", e);
                    }
                }
            },

            dispatchScan: function(name, addr) {
                var self = this;
                if (self.scanCallback) {
                    try {
                        var namePtr = 0, addrPtr = 0;
                        if (name && typeof allocateUTF8 === 'function') namePtr = allocateUTF8(name);
                        if (addr && typeof allocateUTF8 === 'function') addrPtr = allocateUTF8(addr);

                        var table = (typeof wasmTable !== 'undefined') ? wasmTable : (typeof Module !== 'undefined' ? Module['wasmTable'] : null);
                        if (table) {
                            table.get(self.scanCallback)(namePtr, addrPtr, self.userData);
                        }
                        if (namePtr && typeof _free === 'function') _free(namePtr);
                        if (addrPtr && typeof _free === 'function') _free(addrPtr);
                    } catch (e) {
                        console.error("[WEBBLE] Error dispatching scan callback:", e);
                    }
                }
            },

            dispatchRx: function(u8Array) {
                var self = this;
                if (!u8Array || !u8Array.length) return;
                self.bytesRx += u8Array.length;
                if (typeof window !== 'undefined' && typeof window.updateBleHud === 'function') {
                    window.updateBleHud();
                }

                if (self.rxCallback) {
                    try {
                        var len = u8Array.length;
                        var allocFn = (typeof Module !== 'undefined' && Module._malloc) ? Module._malloc : _malloc;
                        var freeFn = (typeof Module !== 'undefined' && Module._free) ? Module._free : _free;
                        var ptr = allocFn(len);
                        HEAPU8.set(u8Array, ptr);

                        var table = (typeof wasmTable !== 'undefined') ? wasmTable : (typeof Module !== 'undefined' ? Module['wasmTable'] : null);
                        if (table) {
                            table.get(self.rxCallback)(ptr, len, self.userData);
                        }
                        freeFn(ptr);
                    } catch (e) {
                        console.error("[WEBBLE] Error dispatching RX callback:", e);
                    }
                }
            },

            processWriteQueue: async function() {
                var self = this;
                if (self.isWriting) return;
                self.isWriting = true;

                try {
                    while (self.writeQueue.length > 0 && self.isConnected && self.rxChar) {
                        var chunk = self.writeQueue.shift();
                        if (self.rxChar.writeValueWithoutResponse) {
                            await self.rxChar.writeValueWithoutResponse(chunk);
                        } else {
                            await self.rxChar.writeValue(chunk);
                        }
                        self.bytesTx += chunk.length;
                        if (typeof window !== 'undefined' && typeof window.updateBleHud === 'function') {
                            window.updateBleHud();
                        }
                    }
                } catch (err) {
                    console.warn("[WEBBLE] Write queue error:", err);
                } finally {
                    self.isWriting = false;
                    if (self.writeQueue.length > 0 && self.isConnected && self.rxChar) {
                        setTimeout(function() { self.processWriteQueue(); }, 5);
                    }
                }
            }
        };

        console.log("[WEBBLE] Web Bluetooth bridge initialized.");
    }
    return 1;
});

EM_JS(int, js_webble_request_device, (), {
    if (!window._webBleBridge || !navigator.bluetooth) return 0;
    var bridge = window._webBleBridge;

    navigator.bluetooth.requestDevice({
        acceptAllDevices: true,
        optionalServices: [bridge.NUS_SERVICE_UUID]
    }).then(function(device) {
        console.log("[WEBBLE] Device selected:", device.name, device.id);
        bridge.devices[device.id] = device;
        bridge.activeDevice = device;

        var dName = device.name || "VESC BLE";
        bridge.dispatchScan(dName, device.id);

        // Auto-connect once user selects the device in the browser dialog
        js_webble_connect(0);
    }).catch(function(err) {
        console.warn("[WEBBLE] requestDevice cancelled or error:", err);
        if (typeof window !== 'undefined' && window.__logToScreen) {
            window.__logToScreen('[BLE] Scan cancelled / error: ' + (err.message || err), '#f87171');
        }
    });

    return 1;
});

EM_JS(int, js_webble_connect, (const char* addrPtr), {
    if (!window._webBleBridge || !navigator.bluetooth) return 0;
    var bridge = window._webBleBridge;

    var targetDevice = null;
    if (addrPtr) {
        var addrStr = UTF8ToString(addrPtr);
        if (bridge.devices[addrStr]) {
            targetDevice = bridge.devices[addrStr];
        }
    }
    if (!targetDevice) {
        targetDevice = bridge.activeDevice;
    }
    if (!targetDevice) {
        console.warn("[WEBBLE] connect called with no active or matched device.");
        return 0;
    }

    bridge.activeDevice = targetDevice;
    bridge.isConnecting = true;
    bridge.isConnected = false;
    bridge.dispatchState(1, targetDevice.name || "VESC BLE", targetDevice.id);

    (async function() {
        try {
            console.log("[WEBBLE] Connecting GATT server to:", targetDevice.name);
            targetDevice.addEventListener('gattserverdisconnected', function(ev) {
                console.log("[WEBBLE] GATT server disconnected:", ev);
                bridge.isConnected = false;
                bridge.isConnecting = false;
                bridge.gattServer = null;
                bridge.rxChar = null;
                bridge.txChar = null;
                bridge.writeQueue = [];
                bridge.dispatchState(0, targetDevice.name || "VESC BLE", targetDevice.id);
            });

            var server = await targetDevice.gatt.connect();
            bridge.gattServer = server;
            console.log("[WEBBLE] GATT server connected. Querying NUS service:", bridge.NUS_SERVICE_UUID);

            var service = await server.getPrimaryService(bridge.NUS_SERVICE_UUID);
            console.log("[WEBBLE] NUS primary service found.");

            var rxChar = await service.getCharacteristic(bridge.NUS_RX_UUID);
            var txChar = await service.getCharacteristic(bridge.NUS_TX_UUID);
            bridge.rxChar = rxChar;
            bridge.txChar = txChar;

            console.log("[WEBBLE] Starting notifications on TX characteristic...");
            await txChar.startNotifications();
            txChar.addEventListener('characteristicvaluechanged', function(event) {
                var val = event.target.value;
                if (val && val.byteLength > 0) {
                    var u8 = new Uint8Array(val.buffer, val.byteOffset, val.byteLength);
                    bridge.dispatchRx(u8);
                }
            });

            bridge.isConnecting = false;
            bridge.isConnected = true;
            bridge.dispatchState(2, targetDevice.name || "VESC BLE", targetDevice.id);
            console.log("[WEBBLE] BLE connection fully established!");
        } catch (err) {
            console.error("[WEBBLE] BLE connect failed:", err);
            bridge.isConnecting = false;
            bridge.isConnected = false;
            bridge.dispatchState(-1, (err.message || String(err)), targetDevice.id);
        }
    })();

    return 1;
});

EM_JS(int, js_webble_disconnect, (), {
    if (!window._webBleBridge) return 0;
    var bridge = window._webBleBridge;

    bridge.isConnected = false;
    bridge.isConnecting = false;
    bridge.writeQueue = [];

    if (bridge.gattServer && bridge.gattServer.connected) {
        try {
            bridge.gattServer.disconnect();
        } catch (e) {
            console.warn("[WEBBLE] Disconnect error:", e);
        }
    }
    bridge.gattServer = null;
    bridge.rxChar = null;
    bridge.txChar = null;
    bridge.dispatchState(0, "", "");
    return 1;
});

EM_JS(int, js_webble_is_connected, (), {
    if (!window._webBleBridge) return 0;
    return (window._webBleBridge.isConnected && window._webBleBridge.gattServer && window._webBleBridge.gattServer.connected) ? 1 : 0;
});

EM_JS(int, js_webble_is_connecting, (), {
    if (!window._webBleBridge) return 0;
    return window._webBleBridge.isConnecting ? 1 : 0;
});

EM_JS(int, js_webble_write, (const uint8_t *dataPtr, int len), {
    if (!window._webBleBridge || !dataPtr || len <= 0) return -1;
    var bridge = window._webBleBridge;
    if (!bridge.isConnected || !bridge.rxChar) return -1;

    // Chunk packet into 20-byte BLE payloads
    var CHUNK_SIZE = 20;
    var offset = 0;
    while (offset < len) {
        var chunkSize = Math.min(CHUNK_SIZE, len - offset);
        var chunk = new Uint8Array(HEAPU8.buffer, dataPtr + offset, chunkSize).slice();
        bridge.writeQueue.push(chunk);
        offset += chunkSize;
    }

    if (!bridge.isWriting) {
        bridge.processWriteQueue();
    }
    return len;
});

EM_JS(void, js_webble_set_callbacks, (WebBleRxCallback rxCb, WebBleStateCallback stateCb, WebBleScanCallback scanCb, void *userData), {
    if (!window._webBleBridge) return;
    window._webBleBridge.rxCallback = rxCb;
    window._webBleBridge.stateCallback = stateCb;
    window._webBleBridge.scanCallback = scanCb;
    window._webBleBridge.userData = userData;
});

#endif // __EMSCRIPTEN__

int webble_init(void) {
#if defined(__EMSCRIPTEN__)
    return js_webble_init();
#else
    return 0;
#endif
}

int webble_is_supported(void) {
#if defined(__EMSCRIPTEN__)
    return js_webble_is_supported();
#else
    return 0;
#endif
}

int webble_request_device(void) {
#if defined(__EMSCRIPTEN__)
    webble_init();
    return js_webble_request_device();
#else
    return 0;
#endif
}

int webble_connect(const char *addr) {
#if defined(__EMSCRIPTEN__)
    webble_init();
    return js_webble_connect(addr);
#else
    (void)addr;
    return 0;
#endif
}

int webble_disconnect(void) {
#if defined(__EMSCRIPTEN__)
    return js_webble_disconnect();
#else
    return 0;
#endif
}

int webble_is_connected(void) {
#if defined(__EMSCRIPTEN__)
    return js_webble_is_connected();
#else
    return 0;
#endif
}

int webble_is_connecting(void) {
#if defined(__EMSCRIPTEN__)
    return js_webble_is_connecting();
#else
    return 0;
#endif
}

int webble_write(const uint8_t *data, int len) {
#if defined(__EMSCRIPTEN__)
    return js_webble_write(data, len);
#else
    (void)data;
    (void)len;
    return -1;
#endif
}

void webble_set_callbacks(WebBleRxCallback rxCb,
                          WebBleStateCallback stateCb,
                          WebBleScanCallback scanCb,
                          void *userData) {
#if defined(__EMSCRIPTEN__)
    webble_init();
    js_webble_set_callbacks(rxCb, stateCb, scanCb, userData);
#else
    (void)rxCb;
    (void)stateCb;
    (void)scanCb;
    (void)userData;
#endif
}
