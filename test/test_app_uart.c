#include "unity.h"

#include "app_uart.h"
#include "mock_app_ble.h"
#include "ruuvi_boards.h"
#include "ruuvi_interface_communication_ble_advertising.h"
#include "mock_ruuvi_driver_error.h"
#include "mock_ruuvi_endpoint_ca_uart.h"
#include "mock_ruuvi_interface_communication_uart.h"
#include "mock_ruuvi_interface_scheduler.h"
#include "mock_ruuvi_interface_watchdog.h"
#include "mock_ruuvi_interface_scheduler.h"
#include "mock_ruuvi_library_ringbuffer.h"

#include <string.h>

/** https://stackoverflow.com/questions/3385515/static-assert-in-c, Nordic Mainframe */
#define STATIC_ASSERT(COND,MSG) typedef char static_assertion_##MSG[(COND)?1:-1]

/** https://stackoverflow.com/questions/3553296/sizeof-single-struct-member-in-c Joey Adams*/
#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

STATIC_ASSERT (MEMBER_SIZE (ri_adv_scan_t, addr) == MEMBER_SIZE (re_ca_uart_ble_adv_t,
               mac), \
               Size_mismatch);

const uint8_t mock_mac[BLE_MAC_ADDRESS_LENGTH] = {0xFA, 0xEB, 0xDC, 0xCD, 0xBE, 0xAF};
const uint8_t mock_data[] =
{
    0x05U, 0x0FU, 0x27U, 0x40U,
    0x35U, 0xC4U, 0x54U, 0x54U, 0x50U, 0x00U, 0xC8U, 0xFCU,
    0x20U, 0xA4U, 0x56U, 0xF0U, 0x30U, 0xE5U, 0xC9U, 0x44U, 0x54U, 0x29U, 0xE3U,
    0x8DU
};

static uint8_t t_ring_buffer[128] = {0};
static bool t_buffer_wlock = false;
static bool t_buffer_rlock = false;

static rl_ringbuffer_t t_uart_ring_buffer =
{
    .head = 0,
    .tail = 0,
    .block_size = sizeof (uint8_t),
    .storage_size = sizeof (t_ring_buffer),
    .index_mask = (sizeof (t_ring_buffer) / sizeof (uint8_t)) - 1,
    .storage = t_ring_buffer,
    .lock = app_uart_ringbuffer_lock_dummy,
    .writelock = &t_buffer_wlock,
    .readlock  = &t_buffer_rlock
};

static size_t mock_sends = 0;
// Mock sending fp for data through uart.
static rd_status_t mock_send (ri_comm_message_t * const msg)
{
    mock_sends++;
    return RD_SUCCESS;
}

static ri_comm_channel_t mock_uart =
{
    .send = &mock_send,
    .on_evt = app_uart_isr
};


void setUp (void)
{
    mock_sends = 0;
}

void tearDown (void)
{
}

/**
 * @brief Initialize UART peripheral with values read from ruuvi_boards.h.
 *
 * After initialization UART peripheral is active and ready to handle incoming data
 * and send data out.
 *
 * @retval RD_SUCCESS On successful init.
 * @retval RD_ERROR_INVALID_STATE If UART is already initialized.
 */
void test_app_uart_init_ok (void)
{
    static ri_uart_init_t config =
    {
        .hwfc_enabled = RB_HWFC_ENABLED,
        .parity_enabled = RB_PARITY_ENABLED,
        .cts  = RB_UART_CTS_PIN,
        .rts  = RB_UART_RTS_PIN,
        .tx   = RB_UART_TX_PIN,
        .rx   = RB_UART_RX_PIN,
        .baud = RI_UART_BAUD_115200 //!< XXX hardcoded, should come from board.
    };
    ri_uart_init_ExpectAnyArgsAndReturn (RD_SUCCESS);
    ri_uart_init_ReturnThruPtr_channel (&mock_uart);
    ri_uart_config_ExpectWithArrayAndReturn (&config, 1, RD_SUCCESS);
    rd_status_t err_code = app_uart_init ();
    TEST_ASSERT (RD_SUCCESS == err_code);
}
void test_app_uart_init_twice (void)
{
    ri_uart_init_t config = {0};
    memset (&config, 0, sizeof (ri_uart_init_t));
    config.hwfc_enabled = RB_HWFC_ENABLED;
    config.parity_enabled = RB_PARITY_ENABLED;
    config.cts  = RB_UART_CTS_PIN;
    config.rts  = RB_UART_RTS_PIN;
    config.tx   = RB_UART_TX_PIN;
    config.rx   = RB_UART_RX_PIN;
    config.baud = RI_UART_BAUD_115200; //!< XXX hardcoded, should come from board.
    ri_uart_init_ExpectAnyArgsAndReturn (RD_SUCCESS);
    ri_uart_config_ExpectWithArrayAndReturn (&config, 1, RD_SUCCESS);
    rd_status_t err_code = app_uart_init ();
    ri_uart_init_ExpectAnyArgsAndReturn (RD_ERROR_INVALID_STATE);
    err_code |= app_uart_init ();
    TEST_ASSERT (RD_ERROR_INVALID_STATE == err_code);
}

/**
 * @brief Send a scanned BLE broadcast through UART.
 *
 * The format is defined by ruuvi.endpoints.c/
 *
 * @param[in] scan Scan result from BLE advertisement module.
 * @retval RD_SUCCESS If encoding and queuing data to UART was successful.
 * @retval RD_ERROR_NULL If scan was NULL.
 * @retval RD_ERROR_INVALID_DATA If scan cannot be encoded for any reason.
 * @retval RD_ERROR_DATA_SIZE If scan had larger advertisement size than allowed by
 *                            encoding module.
 */
void test_app_uart_send_broadcast_ok (void)
{
    rd_status_t err_code = RD_SUCCESS;
    ri_adv_scan_t scan = {0};
    memcpy (scan.addr, &mock_mac, sizeof (scan.addr));
    scan.rssi = -50;
    memcpy (scan.data, &mock_data, sizeof (mock_data));
    scan.data_len = sizeof (mock_data);
    test_app_uart_init_ok();
    re_ca_uart_encode_ExpectAnyArgsAndReturn (RD_SUCCESS);
    err_code |= app_uart_send_broadcast (&scan);
    TEST_ASSERT (RD_SUCCESS == err_code);
    TEST_ASSERT (1 == mock_sends);
}

void test_app_uart_send_broadcast_null (void)
{
    rd_status_t err_code = RD_SUCCESS;
    test_app_uart_init_ok();
    err_code |= app_uart_send_broadcast (NULL);
    TEST_ASSERT (RD_ERROR_NULL == err_code);
    TEST_ASSERT (0 == mock_sends);
}

void test_app_uart_send_broadcast_encoding_error (void)
{
    rd_status_t err_code = RD_SUCCESS;
    ri_adv_scan_t scan = {0};
    memcpy (scan.addr, &mock_mac, sizeof (scan.addr));
    scan.rssi = -50;
    memcpy (scan.data, &mock_data, sizeof (mock_data));
    scan.data_len = sizeof (mock_data);
    test_app_uart_init_ok();
    re_ca_uart_encode_ExpectAnyArgsAndReturn (RD_ERROR_INTERNAL);
    err_code |= app_uart_send_broadcast (&scan);
    TEST_ASSERT (RD_ERROR_INVALID_DATA == err_code);
    TEST_ASSERT (0 == mock_sends);
}

void test_app_uart_send_broadcast_error_size (void)
{
    rd_status_t err_code = RD_SUCCESS;
    ri_adv_scan_t scan = {0};
    memcpy (scan.addr, &mock_mac, sizeof (scan.addr));
    scan.rssi = -50;
    memcpy (scan.data, &mock_data, sizeof (mock_data));
    scan.data_len = 255U;
    test_app_uart_init_ok();
    err_code |= app_uart_send_broadcast (&scan);
    TEST_ASSERT (RD_ERROR_DATA_SIZE == err_code);
    TEST_ASSERT (0 == mock_sends);
}

/**
 * @brief Handle Scan events.
 *
 * Received data is put to scheduler queue, new scan with new PHY is started on timeout.
 *
 * @param[in] evt Type of event, either RI_COMM_RECEIVED on data or
 *                RI_COMM_TIMEOUT on scan timeout.
 * @param[in] p_data NULL on timeout, ri_adv_scan_t* on received.
 * @param[in] data_len 0 on timeout, size of ri_adv_scan_t on received.
 * @retval RD_SUCCESS on successful handling on event.
 * @retval RD_ERR_NO_MEM if received event could not be put to scheduler queue.
 * @return Error code from scanning if scan cannot be started.
 *
 * @note parameters are not const to maintain compatibility with the event handler
 *       signature.
 **/

void test_app_uart_isr_received (void)
{
    rd_status_t err_code = RD_SUCCESS;
    uint8_t data[] =
    {
        RE_CA_UART_STX,
        2 + CMD_IN_LEN,
        RE_CA_UART_SET_CH_37,
        0x01U,
        RE_CA_UART_FIELD_DELIMITER,
        0xB6U, 0x78U, //crc
        RE_CA_UART_ETX
    };
    ri_scheduler_event_put_ExpectAndReturn (data, 8, &app_uart_parser,
                                            RD_SUCCESS);
    rd_error_check_ExpectAnyArgs();
    err_code |= app_uart_isr (RI_COMM_RECEIVED,
                              (void *) &data[0], 8);
    TEST_ASSERT (RD_SUCCESS == err_code);
}

void test_app_uart_isr_unknown (void)
{
    rd_status_t err_code = RD_SUCCESS;
    rd_error_check_ExpectAnyArgs();
    err_code |= app_uart_isr (RI_COMM_TIMEOUT, NULL, 0);
    TEST_ASSERT (RD_SUCCESS == err_code);
}

void test_app_uart_parser_ok (void)
{
    uint8_t data[] =
    {
        RE_CA_UART_STX,
        2 + CMD_IN_LEN,
        RE_CA_UART_SET_CH_37,
        0x01U,
        RE_CA_UART_FIELD_DELIMITER,
        0xB6U, 0x78U, //crc
        RE_CA_UART_ETX
    };
    ri_scheduler_event_put_ExpectAndReturn (data, 8, &app_uart_parser,
                                            RD_SUCCESS);
    rd_error_check_ExpectAnyArgs();
    app_uart_isr (RI_COMM_RECEIVED,
                  (void *) &data[0], 8);
    re_ca_uart_payload_t payload = {0};
    re_ca_uart_decode_ExpectAndReturn ( (uint8_t *) &data[0],
                                        (re_ca_uart_payload_t *) &payload, RD_SUCCESS);
    rl_ringbuffer_empty_ExpectAnyArgsAndReturn (true);
    re_ca_uart_encode_ExpectAnyArgsAndReturn (RD_SUCCESS);
    ri_watchdog_feed_IgnoreAndReturn (RD_SUCCESS);
    app_uart_parser ( (void *) data, 8);
    TEST_ASSERT (1 == mock_sends);
}

void test_app_uart_parser_part_ok (void)
{
    uint8_t data_part1[] =
    {
        RE_CA_UART_STX,
        2 + CMD_IN_LEN,
        RE_CA_UART_SET_CH_37,
    };
    ri_scheduler_event_put_ExpectAndReturn (data_part1, 3 , &app_uart_parser,
                                            RD_SUCCESS);
    rd_error_check_ExpectAnyArgs();
    app_uart_isr (RI_COMM_RECEIVED,
                  (void *) &data_part1[0], 3);

    re_ca_uart_payload_t payload = {0};

    re_ca_uart_decode_ExpectAndReturn ( (uint8_t *) &data_part1[0],
                                        (re_ca_uart_payload_t *) &payload, RE_ERROR_DECODING_CRC);
    rl_ringbuffer_queue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_queue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    rl_ringbuffer_queue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_queue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    rl_ringbuffer_queue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_queue_ReturnThruPtr_buffer(&t_uart_ring_buffer);

    rl_ringbuffer_dequeue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_dequeue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    rl_ringbuffer_dequeue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_dequeue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    rl_ringbuffer_dequeue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_dequeue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    rl_ringbuffer_dequeue_ExpectAnyArgsAndReturn (RL_ERROR_NO_DATA);
    rl_ringbuffer_dequeue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    re_ca_uart_payload_t payload_dec = {0};
    re_ca_uart_decode_ExpectAnyArgsAndReturn (RE_ERROR_DECODING_CRC);

    rl_ringbuffer_queue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_queue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    rl_ringbuffer_queue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_queue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    rl_ringbuffer_queue_ExpectAnyArgsAndReturn (RL_SUCCESS);
    rl_ringbuffer_queue_ReturnThruPtr_buffer(&t_uart_ring_buffer);
    ri_watchdog_feed_IgnoreAndReturn (RD_SUCCESS);
    app_uart_parser ( (void *) data_part1, 3);
    TEST_ASSERT (0 == mock_sends);
}

