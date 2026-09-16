#include "wasm_serial_bridge.h"
#include "vescinterface.h"

#include <QByteArray>
#include <QMetaObject>
#include <QDebug>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>

EM_JS(void, register_wasm_serial_bridge_js, (), {
    var target = (typeof window !== 'undefined') ? window : ((typeof globalThis !== 'undefined') ? globalThis : self);

    target.__wasm_serial_feed_rx = function(uint8Array) {
        if (!uint8Array || !uint8Array.length) return;
        var len = uint8Array.length;
        var ptr = _malloc(len);
        HEAPU8.set(uint8Array, ptr);
        _wasm_serial_rx(ptr, len);
        _free(ptr);
    };

    target.__wasm_serial_set_connected_js = function(connected) {
        if (typeof _wasm_serial_set_connected === 'function') {
            _wasm_serial_set_connected(connected ? 1 : 0);
        }
    };

    if (typeof window !== 'undefined') {
        window.__wasm_serial_feed_rx = target.__wasm_serial_feed_rx;
        window.__wasm_serial_set_connected_js = target.__wasm_serial_set_connected_js;
    }
});
#endif

extern "C" {

void wasm_serial_bridge_init(void) {
#if defined(__EMSCRIPTEN__)
    register_wasm_serial_bridge_js();

    // Ensure bridge functions are also registered on the browser window thread in multithreaded WASM
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined') {
            window.__wasm_serial_feed_rx = function(uint8Array) {
                if (!uint8Array || !uint8Array.length) return;
                var len = uint8Array.length;
                if (typeof Module !== 'undefined' && typeof Module._malloc === 'function' && typeof Module._wasm_serial_rx === 'function') {
                    var ptr = Module._malloc(len);
                    Module.HEAPU8.set(uint8Array, ptr);
                    Module._wasm_serial_rx(ptr, len);
                    Module._free(ptr);
                } else if (typeof _malloc === 'function' && typeof _wasm_serial_rx === 'function') {
                    var ptr = _malloc(len);
                    HEAPU8.set(uint8Array, ptr);
                    _wasm_serial_rx(ptr, len);
                    _free(ptr);
                }
            };

            window.__wasm_serial_set_connected_js = function(connected) {
                if (typeof Module !== 'undefined' && typeof Module._wasm_serial_set_connected === 'function') {
                    Module._wasm_serial_set_connected(connected ? 1 : 0);
                } else if (typeof _wasm_serial_set_connected === 'function') {
                    _wasm_serial_set_connected(connected ? 1 : 0);
                }
            };
        }
    });
#endif
}

#if defined(__EMSCRIPTEN__)
static struct WasmSerialBridgeAutoInit {
    WasmSerialBridgeAutoInit() {
        wasm_serial_bridge_init();
    }
} s_wasmSerialBridgeAutoInit;
#endif

WASM_EXPORT void wasm_serial_rx(const uint8_t* data, int len) {
    if (!data || len <= 0) {
        return;
    }

    VescInterface *vi = VescInterface::instance();
    if (!vi) {
        return;
    }

    QByteArray chunk(reinterpret_cast<const char*>(data), len);

    // Thread-safe dispatch to the Qt event loop (handles both single-thread and pthread worker)
    QMetaObject::invokeMethod(vi, [vi, chunk]() {
        if (!vi->isPortConnected()) {
            vi->connectSerial("WebSerial", 115200);
        }
        vi->processRawRx(chunk);
    }, Qt::QueuedConnection);
}

WASM_EXPORT void wasm_serial_set_connected(int connected) {
    VescInterface *vi = VescInterface::instance();
    if (!vi) {
        return;
    }

    QMetaObject::invokeMethod(vi, [vi, connected]() {
        if (connected) {
            vi->connectSerial("WebSerial", 115200);
        } else {
            vi->disconnectPort();
        }
    }, Qt::QueuedConnection);
}

void wasm_serial_tx(const uint8_t* data, int len) {
    if (!data || len <= 0) {
        return;
    }

#if defined(__EMSCRIPTEN__)
    // In wasm_multithread, Qt runs on a worker pthread.
    // MAIN_THREAD_ASYNC_EM_ASM dispatches to the browser's main UI thread where window.wasm_serial_tx is defined.
    MAIN_THREAD_ASYNC_EM_ASM({
        var ptr = $0;
        var len = $1;
        if (typeof window !== 'undefined' && typeof window.wasm_serial_tx === 'function') {
            var chunk = HEAPU8.slice(ptr, ptr + len);
            try {
                window.wasm_serial_tx(chunk);
            } catch (err) {
                console.error("wasm_serial_tx dispatch error:", err);
            }
        }
    }, data, len);
#else
    (void)data;
    (void)len;
#endif
}

} // extern "C"
