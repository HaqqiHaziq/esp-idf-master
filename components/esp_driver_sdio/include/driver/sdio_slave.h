/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h" // for TickType_t
#include "hal/sdio_slave_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDIO_SLAVE_RECV_MAX_BUFFER  (4096-4)

typedef void(*sdio_event_cb_t)(uint8_t event);

/// Configuration of SDIO slave
typedef struct {
    sdio_slave_timing_t timing;             ///< timing of sdio_slave. see `sdio_slave_timing_t`.
    sdio_slave_sending_mode_t sending_mode; ///< mode of sdio_slave. `SDIO_SLAVE_MODE_STREAM` if the data needs to be sent as much as possible; `SDIO_SLAVE_MODE_PACKET` if the data should be sent in packets.
    int                 send_queue_size;    ///< max buffers that can be queued before sending.
    size_t              recv_buffer_size;
    ///< If buffer_size is too small, it costs more CPU time to handle larger number of buffers.
    ///< If buffer_size is too large, the space larger than the transaction length is left blank but still counts a buffer, and the buffers are easily run out.
    ///< Should be set according to length of data really transferred.
    ///< All data that do not fully fill a buffer is still counted as one buffer. E.g. 10 bytes data costs 2 buffers if the size is 8 bytes per buffer.
    ///< Buffer size of the slave pre-defined between host and slave before communication. All receive buffer given to the driver should be larger than this.
    sdio_event_cb_t     event_cb;           ///< when the host interrupts slave, this callback will be called with interrupt number (0-7).
    uint32_t            flags; ///< Features to be enabled for the slave, combinations of ``SDIO_SLAVE_FLAG_*``.
#define SDIO_SLAVE_FLAG_DAT2_DISABLED       BIT(0)      /**< It is required by the SD specification that all 4 data
        lines should be used and pulled up even in 1-bit mode or SPI mode. However, as a feature, the user can specify
        this flag to make use of DAT2 pin in 1-bit mode. Note that the host cannot read CCCR registers to know we don't
        support 4-bit mode anymore, please do this at your own risk.
        */
#define SDIO_SLAVE_FLAG_HOST_INTR_DISABLED  BIT(1)      /**< The DAT1 line is used as the interrupt line in SDIO
        protocol. However, as a feature, the user can specify this flag to make use of DAT1 pin of the slave in 1-bit
        mode. Note that the host has to do polling to the interrupt registers to know whether there are interrupts from
        the slave. And it cannot read CCCR registers to know we don't support 4-bit mode anymore, please do this at
        your own risk.
        */
#define SDIO_SLAVE_FLAG_INTERNAL_PULLUP     BIT(2)      /**< Enable internal pullups for enabled pins. It is required
        by the SD specification that all the 4 data lines should be pulled up even in 1-bit mode or SPI mode. Note that
        the internal pull-ups are not sufficient for stable communication, please do connect external pull-ups on the
        bus. This is only for example and debug use.
        */
#define SDIO_SLAVE_FLAG_DEFAULT_SPEED       BIT(3)      /**< Disable the highspeed support of the hardware. */
#define SDIO_SLAVE_FLAG_HIGH_SPEED          0           /**< Enable the highspeed support of the hardware. This is the
        default option. The host will see highspeed capability, but the mode actually used is determined by the host. */
} sdio_slave_config_t;

/** Handle of a receive buffer, register a handle by calling ``sdio_slave_recv_register_buf``. Use the handle to load the buffer to the
 *  driver, or call ``sdio_slave_recv_unregister_buf`` if it is no longer used.
 */
typedef void *sdio_slave_buf_handle_t;

/** Initialize the sdio slave driver
 *
 * @param config Configuration of the sdio slave driver.
 *
 * @return
 *     - ESP_ERR_NOT_FOUND if no free interrupt found.
 *     - ESP_ERR_INVALID_STATE if already initialized.
 *     - ESP_ERR_NO_MEM if fail due to memory allocation failed.
 *     - ESP_OK if success
 */
esp_err_t sdio_slave_initialize(sdio_slave_config_t *config);

/** De-initialize the sdio slave driver to release the resources.
 */
void sdio_slave_deinit(void);

/** Start hardware for sending and receiving, as well as set the IOREADY1 to 1.
 *
 * @note The driver will continue sending from previous data and PKT_LEN counting, keep data received as well as start receiving from current TOKEN1 counting.
 * See ``sdio_slave_reset``.
 *
 * @return
 *  - ESP_ERR_INVALID_STATE if already started.
 *  - ESP_OK otherwise.
 */
esp_err_t sdio_slave_start(void);

/** Stop hardware from sending and receiving, also set IOREADY1 to 0.
 *
 * @note this will not clear the data already in the driver, and also not reset the PKT_LEN and TOKEN1 counting. Call ``sdio_slave_reset`` to do that.
 */
void sdio_slave_stop(void);

/** Clear the data still in the driver, as well as reset the PKT_LEN and TOKEN1 counting.
 *
 * @return
 *  - ESP_ERR_INVALID_STATE if already started.
 *  - ESP_OK otherwise.
 */
esp_err_t sdio_slave_reset(void);

/** Reset sdio hardware, and clear the data still in the driver, as well as reset the PKT_LEN and TOKEN1 counting.
 *
 * @return
 *  - ESP_ERR_INVALID_STATE if already started.
 *  - ESP_OK otherwise.
 */
esp_err_t sdio_slave_reset_hw(void);

/*---------------------------------------------------------------------------
 *                  Receive
 *--------------------------------------------------------------------------*/
/** Register buffer used for receiving. All buffers should be registered before used, and then can be used (again) in the driver by the handle returned.
 *
 * @param start The start address of the buffer.
 *
 * @note The driver will use and only use the amount of space specified in the `recv_buffer_size` member set in the `sdio_slave_config_t`.
 *       All buffers should be larger than that. The buffer is used by the DMA, so it should be DMA capable and 32-bit aligned.
 *
 * @return The buffer handle if success, otherwise NULL.
 */
sdio_slave_buf_handle_t sdio_slave_recv_register_buf(uint8_t *start);

/** Unregister buffer from driver, and free the space used by the descriptor pointing to the buffer.
 *
 * @param handle Handle to the buffer to release.
 *
 * @return ESP_OK if success, ESP_ERR_INVALID_ARG if the handle is NULL or the buffer is being used.
 */
esp_err_t sdio_slave_recv_unregister_buf(sdio_slave_buf_handle_t handle);

/** Load buffer to the queue waiting to receive data. The driver takes ownership of the buffer until the buffer is returned by
 *  ``sdio_slave_send_get_finished`` after the transaction is finished.
 *
 * @param handle Handle to the buffer ready to receive data.
 *
 * @return
 *     - ESP_ERR_INVALID_ARG    if invalid handle or the buffer is already in the queue. Only after the buffer is returened by
 *                              ``sdio_slave_recv`` can you load it again.
 *     - ESP_OK if success
 */
esp_err_t sdio_slave_recv_load_buf(sdio_slave_buf_handle_t handle);

/** Get buffer of received data if exist with packet information. The driver returns the ownership of the buffer to the app.
 *
 * When you see return value is ``ESP_ERR_NOT_FINISHED``, you should call this API iteratively until the return value is ``ESP_OK``.
 * All the continuous buffers returned with ``ESP_ERR_NOT_FINISHED``, together with the last buffer returned with ``ESP_OK``, belong to one packet from the host.
 *
 * You can call simpler ``sdio_slave_recv`` instead, if the host never send data longer than the Receiving buffer size,
 * or you don't care about the packet boundary (e.g. the data is only a byte stream).
 *
 * @param handle_ret Handle of the buffer holding received data. Use this handle in ``sdio_slave_recv_load_buf()`` to receive in the same buffer again.
 * @param wait Time to wait before data received.
 *
 * @note Call ``sdio_slave_load_buf`` with the handle to re-load the buffer onto the link list, and receive with the same buffer again.
 *       The address and length of the buffer got here is the same as got from `sdio_slave_get_buffer`.
 *
 * @return
 *     - ESP_ERR_INVALID_ARG    if handle_ret is NULL
 *     - ESP_ERR_TIMEOUT        if timeout before receiving new data
 *     - ESP_ERR_NOT_FINISHED   if returned buffer is not the end of a packet from the host, should call this API again until the end of a packet
 *     - ESP_OK if success
 */
esp_err_t sdio_slave_recv_packet(sdio_slave_buf_handle_t* handle_ret, TickType_t wait);

/** Get received data if exist. The driver returns the ownership of the buffer to the app.
 *
 * @param handle_ret Handle to the buffer holding received data. Use this handle in ``sdio_slave_recv_load_buf`` to receive in the same buffer again.
 * @param[out] out_addr Output of the start address, set to NULL if not needed.
 * @param[out] out_len Actual length of the data in the buffer, set to NULL if not needed.
 * @param wait Time to wait before data received.
 *
 * @note Call ``sdio_slave_load_buf`` with the handle to re-load the buffer onto the link list, and receive with the same buffer again.
 *       The address and length of the buffer got here is the same as got from `sdio_slave_get_buffer`.
 *
 * @return
 *     - ESP_ERR_INVALID_ARG    if handle_ret is NULL
 *     - ESP_ERR_TIMEOUT        if timeout before receiving new data
 *     - ESP_OK if success
 */
esp_err_t sdio_slave_recv(sdio_slave_buf_handle_t* handle_ret, uint8_t **out_addr, size_t *out_len, TickType_t wait);

/** Retrieve the buffer corresponding to a handle.
 *
 * @param handle Handle to get the buffer.
 * @param len_o Output of buffer length
 *
 * @return buffer address if success, otherwise NULL.
 */
uint8_t* sdio_slave_recv_get_buf(sdio_slave_buf_handle_t handle, size_t *len_o);

/*---------------------------------------------------------------------------
 *                  Send
 *--------------------------------------------------------------------------*/
/** Put a new sending transfer into the send queue. The driver takes ownership of the buffer until the buffer is returned by
 *  ``sdio_slave_send_get_finished`` after the transaction is finished.
 *
 * @param addr Address for data to be sent. The buffer should be DMA capable and 32-bit aligned.
 * @param len Length of the data, should not be longer than 4092 bytes (may support longer in the future).
 * @param arg Argument to returned in ``sdio_slave_send_get_finished``. The argument can be used to indicate which transaction is done,
 *            or as a parameter for a callback. Set to NULL if not needed.
 * @param wait Time to wait if the buffer is full.
 *
 * @return
 *     - ESP_ERR_INVALID_ARG if the length is not greater than 0.
 *     - ESP_ERR_TIMEOUT if the queue is still full until timeout.
 *     - ESP_OK if success.
 */
esp_err_t sdio_slave_send_queue(uint8_t* addr, size_t len, void* arg, TickType_t wait);

/** Return the ownership of a finished transaction.
 * @param out_arg Argument of the finished transaction. Set to NULL if unused.
 * @param wait Time to wait if there's no finished sending transaction.
 *
 * @return ESP_ERR_TIMEOUT if no transaction finished, or ESP_OK if succeed.
 */
esp_err_t sdio_slave_send_get_finished(void** out_arg, TickType_t wait);

/** Start a new sending transfer, and wait for it (blocked) to be finished.
 *
 * @param addr Start address of the buffer to send
 * @param len Length of buffer to send.
 *
 * @return
 *     - ESP_ERR_INVALID_ARG if the length of descriptor is not greater than 0.
 *     - ESP_ERR_TIMEOUT if the queue is full or host do not start a transfer before timeout.
 *     - ESP_OK if success.
 */
esp_err_t sdio_slave_transmit(uint8_t* addr, size_t len);

/*---------------------------------------------------------------------------
 *                  Host
 *--------------------------------------------------------------------------*/
/** Read the sdio slave register shared with host.
 *
 * @param pos register address, 0-27 or 32-63.
 *
 * @note register 28 to 31 are reserved for interrupt vector.
 *
 * @return value of the register.
 */
uint8_t sdio_slave_read_reg(int pos);

/** Write the sdio slave register shared with host.
 *
 * @param pos register address, 0-11, 14-15, 18-19, 24-27 and 32-63, other address are reserved.
 * @param reg the value to write.
 *
 * @note register 29 and 31 are used for interrupt vector.
 *
 * @return ESP_ERR_INVALID_ARG if address wrong, otherwise ESP_OK.
 */
esp_err_t sdio_slave_write_reg(int pos, uint8_t reg);

/** Get the interrupt enable for host.
 *
 * @return the interrupt mask.
 */
sdio_slave_hostint_t sdio_slave_get_host_intena(void);

/** Set the interrupt enable for host.
 *
 * @param mask Enable mask for host interrupt.
 */
void sdio_slave_set_host_intena(sdio_slave_hostint_t mask);

/** Interrupt the host by general purpose interrupt.
 *
 * @param pos Interrupt num, 0-7.
 *
 * @return
 *     - ESP_ERR_INVALID_ARG if interrupt num error
 *     - ESP_OK otherwise
 */
esp_err_t sdio_slave_send_host_int(uint8_t pos);

/** Clear general purpose interrupt to host.
 *
 * @param mask Interrupt bits to clear, by bit mask.
 */
void sdio_slave_clear_host_int(sdio_slave_hostint_t mask);

/** Wait for general purpose interrupt from host.
 *
 * @param pos Interrupt source number to wait for.
 * is set.
 * @param wait Time to wait before interrupt triggered.
 *
 * @note this clears the interrupt at the same time.
 *
 * @return ESP_OK if success, ESP_ERR_TIMEOUT if timeout.
 */
esp_err_t sdio_slave_wait_int(int pos, TickType_t wait);

#ifdef __cplusplus
}
#endif
