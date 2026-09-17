#ifndef WEBBLEBRIDGE_H
#define WEBBLEBRIDGE_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

enum WebBleState {
    WEBBLE_DISCONNECTED = 0,
    WEBBLE_CONNECTING = 1,
    WEBBLE_CONNECTED = 2,
    WEBBLE_ERROR = -1
};

typedef void (*WebBleRxCallback)(const uint8_t *data, int len, void *userData);
typedef void (*WebBleStateCallback)(int state, const char *name, const char *addr, void *userData);
typedef void (*WebBleScanCallback)(const char *name, const char *addr, void *userData);

/**
 * @brief Initialize the Web Bluetooth subsystem.
 */
int webble_init(void);

/**
 * @brief Check if Web Bluetooth API is supported in current browser.
 * @return 1 if supported, 0 otherwise.
 */
int webble_is_supported(void);

/**
 * @brief Prompt user to select a BLE device with Nordic UART Service (NUS).
 *        Must be invoked in response to a user interaction (e.g. button click).
 * @return 1 if request initiated, 0 on failure.
 */
int webble_request_device(void);

/**
 * @brief Connect to GATT server of selected/specified BLE device.
 * @param addr Device ID / address string, or NULL for last selected device.
 * @return 1 on success/initiated, 0 on failure.
 */
int webble_connect(const char *addr);

/**
 * @brief Disconnect active BLE GATT connection.
 * @return 1 on success, 0 on failure.
 */
int webble_disconnect(void);

/**
 * @brief Check if BLE device is currently connected.
 * @return 1 if connected, 0 otherwise.
 */
int webble_is_connected(void);

/**
 * @brief Check if BLE device is currently in connecting state.
 * @return 1 if connecting, 0 otherwise.
 */
int webble_is_connecting(void);

/**
 * @brief Write raw bytes to Nordic UART Service RX characteristic.
 * @param data Byte buffer.
 * @param len Number of bytes.
 * @return Number of bytes accepted, or -1 on error.
 */
int webble_write(const uint8_t *data, int len);

/**
 * @brief Register C/C++ callbacks for BLE events.
 */
void webble_set_callbacks(WebBleRxCallback rxCb,
                          WebBleStateCallback stateCb,
                          WebBleScanCallback scanCb,
                          void *userData);

#ifdef __cplusplus
}
#endif

#endif // WEBBLEBRIDGE_H
