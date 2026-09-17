#include "webserialbridge.h"
#include <cstdio>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>

EM_JS(int, js_webserial_is_supported, (), {
    if (typeof navigator !== 'undefined' && 'serial' in navigator) {
        return 1;
    }
    return 0;
});

EM_JS(int, js_webserial_init, (), {
    if (typeof window === 'undefined') return 0;
    if (!('serial' in navigator)) {
        console.warn("WebSerial API is not supported in this browser environment.");
        return 0;
    }

    if (!window._webSerialBridge) {
        window._webSerialBridge = {
            ports: [],
            initialized: true,
            findOrAddPort: function(serialPort) {
                for (var i = 0; i < this.ports.length; i++) {
                    if (this.ports[i].port === serialPort) {
                        return i;
                    }
                }
                var info = serialPort.getInfo ? serialPort.getInfo() : {};
                var vid = info.usbVendorId || 0;
                var pid = info.usbProductId || 0;
                var name = "WebSerial USB (" + vid.toString(16).padStart(4, '0') + ":" + pid.toString(16).padStart(4, '0') + ")";
                if (vid === 0x0483 || pid === 22336 || pid === 0x5740) {
                    name = "VESC USB (" + (pid === 22336 ? "Default" : pid.toString(16)) + ")";
                } else if (vid === 0x10c4) {
                    name = "CP210x UART Bridge";
                } else if (vid === 0x1a86) {
                    name = "CH340 Serial";
                } else if (vid === 0x303a) {
                    name = "ESP32 USB/JTAG";
                }

                var portObj = {
                    port: serialPort,
                    reader: null,
                    writer: null,
                    rxQueue: [],
                    isOpen: false,
                    isReading: false,
                    rxCallback: null,
                    errCallback: null,
                    userData: null,
                    vendorId: vid,
                    productId: pid,
                    name: name
                };
                var id = this.ports.length;
                this.ports.push(portObj);
                return id;
            }
        };

        // Populate initially authorized ports
        navigator.serial.getPorts().then(function(ports) {
            for (var i = 0; i < ports.length; i++) {
                window._webSerialBridge.findOrAddPort(ports[i]);
            }
        }).catch(function(err) {
            console.error("Error fetching initial WebSerial ports:", err);
        });

        // Listen for device plug/unplug events
        navigator.serial.addEventListener('connect', function(event) {
            console.log("WebSerial device connected:", event.target);
            window._webSerialBridge.findOrAddPort(event.target);
        });

        navigator.serial.addEventListener('disconnect', function(event) {
            console.log("WebSerial device disconnected:", event.target);
            var bridge = window._webSerialBridge;
            for (var i = 0; i < bridge.ports.length; i++) {
                if (bridge.ports[i].port === event.target) {
                    bridge.ports[i].isOpen = false;
                    break;
                }
            }
        });
    }

    return 1;
});

EM_JS(int, js_webserial_sync_ports, (), {
    if (!window._webSerialBridge || !navigator.serial) return 0;
    navigator.serial.getPorts().then(function(ports) {
        for (var i = 0; i < ports.length; i++) {
            window._webSerialBridge.findOrAddPort(ports[i]);
        }
    }).catch(function(e) {
        console.error("Sync ports error:", e);
    });
    return window._webSerialBridge.ports.length;
});

EM_JS(int, js_webserial_request_port, (), {
    if (!window._webSerialBridge || !navigator.serial) return -1;
    // Async requestPort prompt triggered by user action
    navigator.serial.requestPort({
        // Accept all serial USB devices
        filters: []
    }).then(function(port) {
        var id = window._webSerialBridge.findOrAddPort(port);
        console.log("WebSerial port granted permission:", id, port);
    }).catch(function(err) {
        console.warn("User cancelled or WebSerial requestPort failed:", err);
    });
    return window._webSerialBridge.ports.length;
});

EM_JS(int, js_webserial_get_port_count, (), {
    if (!window._webSerialBridge) return 0;
    return window._webSerialBridge.ports.length;
});

EM_JS(int, js_webserial_get_port_info, (int portId, char *nameBuf, int nameMax, int *vendorId, int *productId), {
    if (!window._webSerialBridge) return 0;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return 0;

    var p = bridge.ports[portId];
    if (nameBuf && nameMax > 0) {
        var nameStr = p.name || "WebSerial Port";
        stringToUTF8(nameStr, nameBuf, nameMax);
    }
    if (vendorId) {
        setValue(vendorId, p.vendorId || 0, 'i32');
    }
    if (productId) {
        setValue(productId, p.productId || 0, 'i32');
    }
    return 1;
});

EM_JS(int, js_webserial_open, (int portId, int baudRate, int dataBits, int stopBits, int parity, int flowControl), {
    if (!window._webSerialBridge) return 0;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return 0;

    var p = bridge.ports[portId];
    if (p.isOpen) return 1;

    if (p.port && (p.port.readable || p.port.writable)) {
        console.log("[SERIAL] Port is already open, skipping open call.");
        p.isOpen = true;
        return 1;
    }

    var parityStr = "none";
    if (parity === 1) parityStr = "odd";
    else if (parity === 2) parityStr = "even";

    var flowStr = (flowControl === 1) ? "hardware" : "none";

    var options = {
        baudRate: baudRate || 115200,
        dataBits: dataBits || 8,
        stopBits: stopBits || 1,
        parity: parityStr,
        flowControl: flowStr,
        bufferSize: 32768
    };

    p.isOpen = true;
    p.rxQueue = [];

    p.port.open(options).then(function() {
        console.log("WebSerial port", portId, "opened successfully at", baudRate, "baud.");
        if (typeof window !== 'undefined' && window.__logToScreen) {
            window.__logToScreen('[SERIAL] Connected successfully at ' + baudRate + ' baud.', '#4ade80');
        }
        if (typeof isSerialConnected !== 'undefined') isSerialConnected = true;
        if (typeof updateSerialHud === 'function') updateSerialHud();

        if (p.port.writable) {
            p.writer = p.port.writable.getWriter();
        }

        // Start continuous reading loop
        (async function readStreamLoop() {
            p.isReading = true;
            while (p.port && p.port.readable && p.isOpen) {
                try {
                    p.reader = p.port.readable.getReader();
                    while (p.isOpen) {
                        const { value, done } = await p.reader.read();
                        if (done) break;
                        if (value && value.length > 0) {
                            p.rxQueue.push(value);
                            if (typeof bytesRx !== 'undefined') bytesRx += value.length;
                            if (typeof updateSerialHud === 'function') updateSerialHud();
                            if (typeof window !== 'undefined' && window.__logToScreen) {
                                if (value.length <= 256) {
                                    var hexStr = Array.from(value).map(function(b) { return b.toString(16).padStart(2, '0'); }).join(' ');
                                    window.__logToScreen('[SERIAL RX] (' + value.length + ' bytes): ' + hexStr, '#a5f3fc');
                                } else {
                                    window.__logToScreen('[SERIAL RX] (' + value.length + ' bytes, large payload)', '#a5f3fc');
                                }
                            }

                            if (p.rxCallback) {
                                try {
                                    if (typeof wasmTable !== 'undefined') {
                                        wasmTable.get(p.rxCallback)(portId, p.userData);
                                    } else if (typeof Module !== 'undefined' && Module['wasmTable']) {
                                        Module['wasmTable'].get(p.rxCallback)(portId, p.userData);
                                    }
                                } catch (cbErr) {
                                    console.error("WebSerial callback dispatch failed:", cbErr);
                                }
                            }
                        }
                    }
                } catch (err) {
                    console.warn("WebSerial read error:", err);
                    if (typeof window !== 'undefined' && window.__logToScreen) {
                        window.__logToScreen('[SERIAL READ ERROR] ' + (err.message || err), '#f87171');
                    }
                    await new Promise(function(resolve) { setTimeout(resolve, 100); });
                } finally {
                    if (p.reader) {
                        try { p.reader.releaseLock(); } catch(e){}
                        p.reader = null;
                    }
                }
            }
            p.isReading = false;
        })();
    }).catch(function(err) {
        console.error("Failed to open WebSerial port:", err);
        p.isOpen = false;
        if (typeof window !== 'undefined' && window.__logToScreen) {
            window.__logToScreen('[SERIAL ERROR] Failed to open port: ' + (err.message || err), '#f87171');
        }
        if (typeof isSerialConnected !== 'undefined') isSerialConnected = false;
        if (typeof updateSerialHud === 'function') updateSerialHud();
        if (p.errCallback) {
            try {
                if (typeof wasmTable !== 'undefined') {
                    wasmTable.get(p.errCallback)(portId, 1, p.userData);
                } else if (typeof Module !== 'undefined' && Module['wasmTable']) {
                    Module['wasmTable'].get(p.errCallback)(portId, 1, p.userData);
                }
            } catch(e){}
        }
    });

    return 1;
});

EM_JS(int, js_webserial_close, (int portId), {
    if (!window._webSerialBridge) return 0;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return 0;

    var p = bridge.ports[portId];
    p.isOpen = false;

    if (typeof isSerialConnected !== 'undefined') isSerialConnected = false;
    if (typeof updateSerialHud === 'function') updateSerialHud();
    if (typeof window !== 'undefined' && window.__logToScreen) {
        window.__logToScreen('[SERIAL] Disconnected.');
    }

    (async function() {
        try {
            if (p.reader) {
                await p.reader.cancel();
                p.reader.releaseLock();
                p.reader = null;
            }
            if (p.writer) {
                p.writer.releaseLock();
                p.writer = null;
            }
            if (p.port) {
                await p.port.close();
            }
            console.log("WebSerial port", portId, "closed.");
        } catch (err) {
            console.warn("Error during WebSerial port close:", err);
        }
    })();

    return 1;
});

EM_JS(int, js_webserial_is_open, (int portId), {
    if (!window._webSerialBridge) return 0;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return 0;
    return bridge.ports[portId].isOpen ? 1 : 0;
});

EM_JS(int, js_webserial_write, (int portId, const uint8_t *data, int len), {
    if (!window._webSerialBridge) return -1;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return -1;

    var p = bridge.ports[portId];
    if (!p.isOpen || !p.writer) return -1;

    try {
        var heap = (typeof HEAPU8 !== 'undefined' && HEAPU8.buffer && !HEAPU8.buffer.detached) ? HEAPU8 : new Uint8Array(Module.HEAPU8.buffer);
        var chunk = heap.slice(data, data + len);
        if (typeof bytesTx !== 'undefined') bytesTx += len;
        if (typeof updateSerialHud === 'function') updateSerialHud();
        if (typeof window !== 'undefined' && window.__logToScreen) {
            if (len <= 256) {
                var hexStr = Array.from(chunk).map(function(b) { return b.toString(16).padStart(2, '0'); }).join(' ');
                window.__logToScreen('[SERIAL TX] (' + len + ' bytes): ' + hexStr, '#fef08a');
            } else {
                window.__logToScreen('[SERIAL TX] (' + len + ' bytes, large payload)', '#fef08a');
            }
        }

        // Sequential promise chaining to avoid overlapping WebSerial write operations
        if (!p.writePromise) {
            p.writePromise = Promise.resolve();
        }
        p.writePromise = p.writePromise.then(function() {
            if (!p.writer || !p.isOpen) return;
            return p.writer.write(chunk);
        }).catch(function(err) {
            console.error("WebSerial async write error:", err);
            if (typeof window !== 'undefined' && window.__logToScreen) {
                window.__logToScreen('[SERIAL TX ERROR] ' + (err.message || err), '#f87171');
            }
        });

        return len;
    } catch (err) {
        console.error("WebSerial write exception:", err);
        return -1;
    }
});

EM_JS(int, js_webserial_read, (int portId, uint8_t *buf, int maxLen), {
    if (!window._webSerialBridge) return 0;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return 0;

    var p = bridge.ports[portId];
    if (!p.rxQueue || p.rxQueue.length === 0 || maxLen <= 0) return 0;

    var heap = (typeof HEAPU8 !== 'undefined' && HEAPU8.buffer && !HEAPU8.buffer.detached) ? HEAPU8 : new Uint8Array(Module.HEAPU8.buffer);
    var bytesRead = 0;
    while (p.rxQueue.length > 0 && bytesRead < maxLen) {
        var firstChunk = p.rxQueue[0];
        var needed = maxLen - bytesRead;
        if (firstChunk.length <= needed) {
            heap.set(firstChunk, buf + bytesRead);
            bytesRead += firstChunk.length;
            p.rxQueue.shift();
        } else {
            var slice = firstChunk.subarray(0, needed);
            var remainder = firstChunk.subarray(needed);
            heap.set(slice, buf + bytesRead);
            bytesRead += needed;
            p.rxQueue[0] = remainder;
            break;
        }
    }
    return bytesRead;
});

EM_JS(int, js_webserial_bytes_available, (int portId), {
    if (!window._webSerialBridge) return 0;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return 0;

    var p = bridge.ports[portId];
    if (!p.rxQueue) return 0;

    var total = 0;
    for (var i = 0; i < p.rxQueue.length; i++) {
        total += p.rxQueue[i].length;
    }
    return total;
});

EM_JS(void, js_webserial_flush, (int portId), {
    if (!window._webSerialBridge) return;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return;
    bridge.ports[portId].rxQueue = [];
});

EM_JS(void, js_webserial_set_callbacks, (int portId, void *rxCb, void *errCb, void *userData), {
    if (!window._webSerialBridge) return;
    var bridge = window._webSerialBridge;
    if (portId < 0 || portId >= bridge.ports.length) return;

    var p = bridge.ports[portId];
    p.rxCallback = rxCb;
    p.errCallback = errCb;
    p.userData = userData;
});

#endif // __EMSCRIPTEN__

extern "C" {

int webserial_init(void) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_init();
#else
    return 0;
#endif
}

int webserial_is_supported(void) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_is_supported();
#else
    return 0;
#endif
}

int webserial_request_port(void) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_request_port();
#else
    return -1;
#endif
}

int webserial_get_port_count(void) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_get_port_count();
#else
    return 0;
#endif
}

int webserial_sync_ports(void) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_sync_ports();
#else
    return 0;
#endif
}

int webserial_get_port_info(int portId, char *nameBuf, int nameMax, int *vendorId, int *productId) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_get_port_info(portId, nameBuf, nameMax, vendorId, productId);
#else
    (void)portId; (void)nameBuf; (void)nameMax; (void)vendorId; (void)productId;
    return 0;
#endif
}

int webserial_open(int portId, int baudRate, int dataBits, int stopBits, int parity, int flowControl) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_open(portId, baudRate, dataBits, stopBits, parity, flowControl);
#else
    (void)portId; (void)baudRate; (void)dataBits; (void)stopBits; (void)parity; (void)flowControl;
    return 0;
#endif
}

int webserial_close(int portId) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_close(portId);
#else
    (void)portId;
    return 0;
#endif
}

int webserial_is_open(int portId) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_is_open(portId);
#else
    (void)portId;
    return 0;
#endif
}

int webserial_write(int portId, const uint8_t *data, int len) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_write(portId, data, len);
#else
    (void)portId; (void)data; (void)len;
    return -1;
#endif
}

int webserial_read(int portId, uint8_t *buf, int maxLen) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_read(portId, buf, maxLen);
#else
    (void)portId; (void)buf; (void)maxLen;
    return 0;
#endif
}

int webserial_bytes_available(int portId) {
#if defined(__EMSCRIPTEN__)
    return js_webserial_bytes_available(portId);
#else
    (void)portId;
    return 0;
#endif
}

void webserial_flush(int portId) {
#if defined(__EMSCRIPTEN__)
    js_webserial_flush(portId);
#else
    (void)portId;
#endif
}

void webserial_set_callbacks(int portId, WebSerialRxCallback rxCb, WebSerialErrorCallback errCb, void *userData) {
#if defined(__EMSCRIPTEN__)
    js_webserial_set_callbacks(portId, (void*)rxCb, (void*)errCb, userData);
#else
    (void)portId; (void)rxCb; (void)errCb; (void)userData;
#endif
}

}
