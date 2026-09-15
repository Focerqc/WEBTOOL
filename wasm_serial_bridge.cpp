#include "wasm_serial_bridge.h"
#include "vescinterface.h"

#include <QByteArray>
#include <QMetaObject>
#include <QDebug>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/em_js.h>
#endif

extern "C" {

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
