#ifndef WASM_SERIAL_BRIDGE_H
#define WASM_SERIAL_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize JavaScript global bindings for Web Serial bridge.
 */
void wasm_serial_bridge_init(void);

/**
 * @brief Forward incoming serial bytes from JavaScript into VESC Tool's packet pipeline.
 * @param data Pointer to incoming byte buffer.
 * @param len Number of bytes.
 */
WASM_EXPORT void wasm_serial_rx(const uint8_t* data, int len);

/**
 * @brief Notify VESC Tool of Web Serial connection state change.
 * @param connected 1 if connected, 0 if disconnected.
 */
WASM_EXPORT void wasm_serial_set_connected(int connected);

/**
 * @brief Transmit outgoing serial bytes from VESC Tool to JavaScript / Web Serial.
 * @param data Pointer to outgoing byte buffer.
 * @param len Number of bytes.
 */
void wasm_serial_tx(const uint8_t* data, int len);

#ifdef __cplusplus
}
#endif

#endif // WASM_SERIAL_BRIDGE_H
