#ifndef WEBSERIALBRIDGE_H
#define WEBSERIALBRIDGE_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*WebSerialRxCallback)(int portId, void *userData);
typedef void (*WebSerialErrorCallback)(int portId, int errorCode, const char *errorMsg, void *userData);

/**
 * @brief Initialize the WebSerial subsystem and register event listeners.
 */
int webserial_init(void);

/**
 * @brief Check if WebSerial API is supported in the current browser.
 * @return 1 if supported, 0 otherwise.
 */
int webserial_is_supported(void);

/**
 * @brief Request user permission to access a serial port via navigator.serial.requestPort().
 *        Must be called from a user gesture handler (e.g. click).
 * @return >= 0 portId on success, -1 on error/cancel.
 */
int webserial_request_port(void);

/**
 * @brief Get the number of currently authorized serial ports.
 */
int webserial_get_port_count(void);

/**
 * @brief Synchronize the authorized ports list from navigator.serial.getPorts().
 */
int webserial_sync_ports(void);

/**
 * @brief Get information about a serial port by index/id.
 * @param portId Index of the port.
 * @param nameBuf Buffer to receive port name string.
 * @param nameMax Maximum size of nameBuf.
 * @param vendorId Output for USB Vendor ID.
 * @param productId Output for USB Product ID.
 * @return 1 on success, 0 on failure.
 */
int webserial_get_port_info(int portId, char *nameBuf, int nameMax, int *vendorId, int *productId);

/**
 * @brief Open a serial port with the specified configuration.
 * @param portId Index of the port to open.
 * @param baudRate Baud rate (e.g. 115200).
 * @param dataBits Number of data bits (5, 6, 7, 8).
 * @param stopBits Stop bits (1, 2).
 * @param parity Parity (0 = none, 1 = odd, 2 = even).
 * @param flowControl Flow control (0 = none, 1 = hardware).
 * @return 1 on success, 0 on failure.
 */
int webserial_open(int portId, int baudRate, int dataBits, int stopBits, int parity, int flowControl);

/**
 * @brief Close an open serial port.
 * @param portId Index of the port to close.
 * @return 1 on success, 0 on failure.
 */
int webserial_close(int portId);

/**
 * @brief Check if a serial port is currently open.
 * @param portId Index of the port.
 * @return 1 if open, 0 if closed.
 */
int webserial_is_open(int portId);

/**
 * @brief Write data to an open serial port.
 * @param portId Index of the port.
 * @param data Pointer to data bytes.
 * @param len Number of bytes to write.
 * @return Number of bytes accepted for write, or -1 on error.
 */
int webserial_write(int portId, const uint8_t *data, int len);

/**
 * @brief Read available data from the serial port receive buffer.
 * @param portId Index of the port.
 * @param buf Pointer to destination buffer.
 * @param maxLen Maximum number of bytes to read.
 * @return Number of bytes read into buf.
 */
int webserial_read(int portId, uint8_t *buf, int maxLen);

/**
 * @brief Return the number of bytes available in the receive buffer for reading.
 * @param portId Index of the port.
 */
int webserial_bytes_available(int portId);

/**
 * @brief Clear / flush receive and transmit buffers.
 * @param portId Index of the port.
 */
void webserial_flush(int portId);

/**
 * @brief Register C++ callback functions for incoming data and errors.
 * @param portId Index of the port.
 * @param rxCb Function called when new bytes are received.
 * @param errCb Function called on error.
 * @param userData Pointer passed back to the callbacks.
 */
void webserial_set_callbacks(int portId, WebSerialRxCallback rxCb, WebSerialErrorCallback errCb, void *userData);

#ifdef __cplusplus
}
#endif

#endif // WEBSERIALBRIDGE_H
