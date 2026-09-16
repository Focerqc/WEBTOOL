#include "wasm_serial_bridge.h"
#include "vescinterface.h"

#include <QByteArray>
#include <QMetaObject>
#include <QDebug>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>

EM_JS(void, js_wasm_serial_tx, (const uint8_t* ptr, int len), {
    if (window.wasm_serial_tx && ptr && len > 0) {
        const bytes = new Uint8Array(HEAPU8.buffer, ptr, len);
        window.wasm_serial_tx(bytes);
    }
});

EM_JS(void, js_wasm_serial_set_connected, (int connected), {
    if (typeof window !== 'undefined' && window.__wasm_serial_on_state_change) {
        window.__wasm_serial_on_state_change(!!connected);
    }
});

EM_JS(void, register_wasm_serial_bridge_js, (), {
    var target = (typeof window !== 'undefined') ? window : ((typeof globalThis !== 'undefined') ? globalThis : self);

    target.__wasm_serial_feed_rx = function(uint8Array) {
        if (!uint8Array || !uint8Array.length) return;
        var len = uint8Array.length;
        var ptr = (typeof Module !== 'undefined' && Module._malloc) ? Module._malloc(len) : _malloc(len);
        if (ptr % 8 !== 0) {
            console.error('[WASM SERIAL] Unaligned malloc pointer returned:', ptr, 'len:', len);
        }
        HEAPU8.set(uint8Array, ptr);
        if (typeof Module !== 'undefined' && Module._wasm_serial_rx) {
            Module._wasm_serial_rx(ptr, len);
        } else {
            _wasm_serial_rx(ptr, len);
        }
        if (typeof Module !== 'undefined' && Module._free) {
            Module._free(ptr);
        } else {
            _free(ptr);
        }
    };

    target.__wasm_serial_set_connected_js = function(connected) {
        if (typeof Module !== 'undefined' && typeof Module._wasm_serial_set_connected === 'function') {
            Module._wasm_serial_set_connected(connected ? 1 : 0);
        } else if (typeof _wasm_serial_set_connected === 'function') {
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
    qDebug() << "[BRIDGE RX]" << chunk.size() << "bytes queued to VescInterface";

    // Thread-safe dispatch to the Qt event loop (handles both single-thread and pthread worker)
    QMetaObject::invokeMethod(vi, [vi, chunk]() {
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
#if defined(__EMSCRIPTEN__)
        js_wasm_serial_set_connected(connected);
#endif
    }, Qt::QueuedConnection);
}

void wasm_serial_tx(const uint8_t* data, int len) {
#if defined(__EMSCRIPTEN__)
    js_wasm_serial_tx(data, len);
#else
    (void)data;
    (void)len;
#endif
}

} // extern "C"
