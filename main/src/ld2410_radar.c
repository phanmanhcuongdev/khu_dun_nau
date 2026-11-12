#include "ld2410_radar.h"
#include "board_pins.h"
#include "diagnostic_config.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "LD2410";
static bool s_initialized = false;
static bool s_uart_initialized = false;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static size_t s_last_rx_bytes = 0;
static uint8_t s_latest[23];
static bool s_latest_pending;
static uint32_t s_parser_errors, s_timeouts;
static uint64_t s_total_rx_bytes;
static QueueHandle_t s_uart_events;
static uint32_t s_uart_overflows, s_uart_errors, s_echo_frames, s_unknown_frames;
/* Remember only fully accepted diagnostic TX frames; never feed this into RX. */
static uint8_t s_tx_history[6][16];
static size_t s_tx_lengths[6], s_tx_history_count;
#if LD2410_UART_DIAGNOSTICS_ENABLED
static bool s_diag_started;
static int64_t s_diag_start_us;
static size_t s_diag_rx_dumped, s_diag_frames_logged;
#endif
static void ld2410_uart_task(void *arg);
static int64_t s_last_valid_frame_us = 0;
static bool s_normal_frame_seen = false;

typedef enum {
    UART_TEST_IDLE,
    UART_TEST_ENTER_SEND,
    UART_TEST_ENTER_WAIT,
    UART_TEST_VERSION_SEND,
    UART_TEST_VERSION_WAIT,
    UART_TEST_EXIT_SEND,
    UART_TEST_EXIT_WAIT,
    UART_TEST_DONE,
} uart_test_state_t;

static uart_test_state_t s_test_state = UART_TEST_IDLE;
#if LD2410_UART_SELFTEST_ENABLED
static int64_t s_test_deadline_us = 0;
#endif
static uint8_t s_test_retries = 0;
static bool s_test_tx_ok = true;
static unsigned s_test_tx_attempts;
static bool s_test_enter_ack = false;
static bool s_test_version_ack = false;
static bool s_test_exit_ack = false;
static bool s_test_reported = false;
static char s_test_version[32] = "NOT RECEIVED";
#define RADAR_RX_STREAM_SIZE 1024
static uint8_t s_rx_stream[RADAR_RX_STREAM_SIZE];
static size_t s_rx_stream_len = 0;

#define LD2410_CMD_ENTER_CONFIG 0x00FF
#define LD2410_CMD_READ_VERSION 0x00A0
#define LD2410_CMD_EXIT_CONFIG  0x00FE
#define LD2410_ACK_MASK         0x0100
#define LD2410_TEST_TIMEOUT_US  750000LL

static void ld2410_selftest_step(void);
static void ld2410_process_stream(void);

static inline bool ld2410_diag_active(void)
{
#if LD2410_UART_DIAGNOSTICS_ENABLED
    return s_diag_started && esp_timer_get_time() - s_diag_start_us < LD2410_DIAG_WINDOW_US;
#else
    return false;
#endif
}

static void ld2410_diag_begin(void)
{
#if LD2410_UART_DIAGNOSTICS_ENABLED
    if (!s_diag_started) {
        s_diag_started = true;
        s_diag_start_us = esp_timer_get_time();
    }
#endif
}

static void ld2410_set_test_state(uart_test_state_t next)
{
#if LD2410_UART_DIAGNOSTICS_ENABLED
    static const char *const names[] = {"IDLE", "SEND_ENABLE", "WAIT_ENABLE",
        "SEND_VERSION", "WAIT_VERSION", "SEND_END", "WAIT_END", "DONE"};
    if (next != s_test_state && ld2410_diag_active())
        ESP_LOGI("LD2410_TEST", "STATE %s -> %s relative_us=%lld",
                 names[s_test_state], names[next], (long long)(esp_timer_get_time()-s_diag_start_us));
#endif
    s_test_state = next;
}

/* Compare full RX frame bytes only; command-shaped data alone isn't exact echo. */
static bool ld2410_is_tx_echo(const uint8_t *frame, size_t len)
{
    for (size_t i = 0; i < s_tx_history_count; ++i)
        if (len == s_tx_lengths[i] && memcmp(frame, s_tx_history[i], len) == 0) return true;
    return false;
}

static void ld2410_diag_rx(const uint8_t *bytes, size_t len)
{
#if LD2410_UART_DIAGNOSTICS_ENABLED
    if (!ld2410_diag_active() || s_diag_rx_dumped >= LD2410_DIAG_RX_BUDGET) return;
    size_t n = len;
    if (n > LD2410_DIAG_CHUNK_MAX) n = LD2410_DIAG_CHUNK_MAX;
    if (n > LD2410_DIAG_RX_BUDGET-s_diag_rx_dumped) n = LD2410_DIAG_RX_BUDGET-s_diag_rx_dumped;
    ESP_LOGI("LD2410_TEST", "RAW_RX relative_us=%lld read_len=%u dump_len=%u omitted=%u",
             (long long)(esp_timer_get_time()-s_diag_start_us), (unsigned)len, (unsigned)n, (unsigned)(len-n));
    ESP_LOG_BUFFER_HEX_LEVEL("LD2410_TEST", bytes, n, ESP_LOG_INFO);
    s_diag_rx_dumped += n;
#else
    (void)bytes; (void)len;
#endif
}

static void ld2410_diag_frame(const char *type, size_t total, bool structural)
{
#if LD2410_UART_DIAGNOSTICS_ENABLED
    if (ld2410_diag_active() && s_diag_frames_logged < LD2410_DIAG_FRAME_BUDGET) {
        s_diag_frames_logged++;
        ESP_LOGI("LD2410_TEST", "RX classification=%s length=%u structural_valid=%d relative_us=%lld",
                 type, (unsigned)total, structural, (long long)(esp_timer_get_time()-s_diag_start_us));
        /* Already bounded to <=74 bytes by the framing checks. */
        ESP_LOG_BUFFER_HEX_LEVEL("LD2410_TEST", s_rx_stream, total, ESP_LOG_INFO);
    }
#else
    (void)type; (void)total; (void)structural;
#endif
}

static void ld2410_selftest_report(void)
{
    if (s_test_reported) return;
    s_test_reported = true;
    bool bidirectional = s_uart_initialized && s_test_tx_attempts > 0 && s_test_tx_ok && s_test_enter_ack && s_test_version_ack && s_test_exit_ack;
    ESP_LOGI("LD2410_TEST", "========== LD2410 UART SELF-TEST ==========");
    ESP_LOGI("LD2410_TEST", "UART initialization : %s", s_uart_initialized ? "PASS" : "FAIL");
    ESP_LOGI("LD2410_TEST", "ESP TX write         : %s", s_test_tx_attempts > 0 && s_test_tx_ok ? "PASS" : "FAIL");
    ESP_LOGI("LD2410_TEST", "Radar ACK 0x01FF     : %s", s_test_enter_ack ? "PASS" : "FAIL");
    ESP_LOGI("LD2410_TEST", "Firmware ACK 0x01A0  : %s", s_test_version_ack ? "PASS" : "FAIL");
    ESP_LOGI("LD2410_TEST", "Firmware version     : %s", s_test_version);
    ESP_LOGI("LD2410_TEST", "Radar ACK 0x01FE     : %s", s_test_exit_ack ? "PASS" : "FAIL");
    ESP_LOGI("LD2410_TEST", "Normal radar frames  : %s", s_normal_frame_seen ? "PASS" : "NOT TESTED");
    ESP_LOGI("LD2410_TEST", "Bidirectional UART   : %s", bidirectional ? "PASS" : (s_test_exit_ack ? "FAIL" : "INCOMPLETE"));
    ESP_LOGI("LD2410_TEST", "RX byte count=%llu", (unsigned long long)s_total_rx_bytes);
    ESP_LOGI("LD2410_TEST", "Parser errors=%lu timeouts=%lu", (unsigned long)s_parser_errors, (unsigned long)s_timeouts);
    ESP_LOGI("LD2410_TEST", "Echo frames=%lu unknown frames=%lu UART overflows=%lu UART errors=%lu",
             (unsigned long)s_echo_frames, (unsigned long)s_unknown_frames,
             (unsigned long)s_uart_overflows, (unsigned long)s_uart_errors);
    ESP_LOGI("LD2410_TEST", "==========================================");
}

#if LD2410_UART_SELFTEST_ENABLED
static void ld2410_selftest_send(uint16_t command, const uint8_t *params, size_t params_len)
{
    /* Only the three read-only diagnostic transaction commands are allowed. */
    if (!((command == LD2410_CMD_ENTER_CONFIG && params_len == 2 && params &&
           params[0] == 1 && params[1] == 0) ||
          ((command == LD2410_CMD_READ_VERSION || command == LD2410_CMD_EXIT_CONFIG) && params_len == 0))) {
        s_test_tx_ok = false;
        ld2410_set_test_state(UART_TEST_EXIT_SEND);
        ESP_LOGE("LD2410_TEST", "Rejected invalid diagnostic command/payload");
        return;
    }
    uint8_t frame[16] = {0xFD, 0xFC, 0xFB, 0xFA};
    uint16_t data_len = (uint16_t)(2 + params_len);
    frame[4] = (uint8_t)data_len;
    frame[5] = (uint8_t)(data_len >> 8);
    frame[6] = (uint8_t)command;
    frame[7] = (uint8_t)(command >> 8);
    if (params_len > 0) memcpy(&frame[8], params, params_len);
    size_t total = 8 + params_len;
    frame[total++] = 0x04; frame[total++] = 0x03; frame[total++] = 0x02; frame[total++] = 0x01;
    s_test_tx_attempts++;
    int written = uart_write_bytes(RADAR_UART_NUM, (const char *)frame, total);
    s_test_tx_ok = s_test_tx_ok && (written == (int)total);
    if (written == (int)total && s_tx_history_count < 6) {
        memcpy(s_tx_history[s_tx_history_count], frame, total);
        s_tx_lengths[s_tx_history_count++] = total;
    }
#if LD2410_UART_DIAGNOSTICS_ENABLED
    if (ld2410_diag_active()) ESP_LOGI("LD2410_TEST", "RAW_TX relative_us=%lld requested=%u accepted=%d",
        (long long)(esp_timer_get_time()-s_diag_start_us), (unsigned)total, written);
#endif
    ESP_LOGI("LD2410_TEST", "TX command=0x%04X bytes=%u write_return=%d status=%s",
             command, (unsigned)total, written, written == (int)total ? "ACCEPTED" : "FAILED");
    ESP_LOG_BUFFER_HEX_LEVEL("LD2410_TEST", frame, total, ESP_LOG_INFO);
    if (written != (int)total) ESP_LOGE("LD2410_TEST", "UART TX failed/short write: %d of %u", written, (unsigned)total);
    s_test_deadline_us = esp_timer_get_time() + LD2410_TEST_TIMEOUT_US;
}

static void ld2410_selftest_fail_or_retry(uart_test_state_t send_state, const char *step)
{
    s_timeouts++;
    if (s_test_retries < 1) {
        s_test_retries++;
        ESP_LOGW("LD2410_TEST", "%s TIMEOUT; retrying once", step);
        ld2410_set_test_state(send_state);
    } else {
        ESP_LOGW("LD2410_TEST", "%s TIMEOUT after retry", step);
        s_test_retries = 0;
        ld2410_set_test_state(UART_TEST_EXIT_SEND);
    }
}

#endif

esp_err_t ld2410_radar_init(void)
{
    if (s_initialized) {
        return s_uart_initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    // 1. Cấu hình chân OUT làm input
    gpio_config_t conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << RADAR_OUT_PIN),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    esp_err_t err = gpio_config(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi gpio_config RADAR_OUT (pin %d): %d", RADAR_OUT_PIN, err);
        ld2410_selftest_report();
        return err;
    }

    s_initialized = true; /* OUT fallback survives subsequent UART failures. */

    // 2. Cấu hình UART1 cho Radar (256000 bps, 8-N-1).
    // GPIO_NUM_x are C enum constants, so do not use them in a preprocessor #if.
    uart_config_t uart_config = {
        .baud_rate = RADAR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    err = uart_driver_install(RADAR_UART_NUM, 4096, 256, 16, &s_uart_events, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi uart_driver_install UART%d: %d", RADAR_UART_NUM, err);
        ld2410_selftest_report();
        return err;
    }

    err = uart_param_config(RADAR_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi uart_param_config: %d", err);
        uart_driver_delete(RADAR_UART_NUM);
        ld2410_selftest_report();
        return err;
    }

    err = uart_set_pin(RADAR_UART_NUM, RADAR_TX_PIN, RADAR_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK) {
        s_uart_initialized = true;
        s_initialized = true;
        if (xTaskCreate(ld2410_uart_task, "ld2410_rx", 4096, NULL, 5, NULL) != pdPASS) {
            s_uart_initialized = false;
            /* OUT remains usable even when UART worker allocation fails. */
            uart_driver_delete(RADAR_UART_NUM);
            ESP_LOGE("LD2410_TEST", "RX task allocation failed");
            ld2410_selftest_report();
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI("DIAG_RADAR", "UART1 RX=%d TX=%d baud=%d 8N1",
                 RADAR_RX_PIN, RADAR_TX_PIN, RADAR_BAUD_RATE);
        ESP_LOGI("LD2410_TEST", "UART1 initialized; RX=GPIO%d TX=GPIO%d baud=%d; Self-test %s",
                 RADAR_RX_PIN, RADAR_TX_PIN, RADAR_BAUD_RATE,
                 LD2410_UART_SELFTEST_ENABLED ? "enabled" : "disabled");
        ESP_LOGI(TAG, "Khoi tao Radar LD2410 thanh cong (RX:%d, TX:%d, OUT:%d, Baud:%d)",
                 RADAR_RX_PIN, RADAR_TX_PIN, RADAR_OUT_PIN, RADAR_BAUD_RATE);
    } else {
        uart_driver_delete(RADAR_UART_NUM);
        s_initialized = true; /* retain OUT fallback */
        ESP_LOGE(TAG, "Loi uart_set_pin: %d", err);
        ld2410_selftest_report();
        return err;
    }
    return ESP_OK;
}

int ld2410_read_out_pin(void)
{
    return gpio_get_level(RADAR_OUT_PIN);
}

bool ld2410_radar_uart_is_initialized(void)
{
    return s_uart_initialized;
}

size_t ld2410_radar_get_last_rx_bytes(void)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    size_t n = s_last_rx_bytes;
    portEXIT_CRITICAL(&s_snapshot_lock);
    return n;
}

uint32_t ld2410_radar_get_last_frame_age_ms(void)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    int64_t last = s_last_valid_frame_us;
    portEXIT_CRITICAL(&s_snapshot_lock);
    if (last == 0) return UINT32_MAX;
    int64_t elapsed_us = esp_timer_get_time() - last;
    return elapsed_us > UINT32_MAX * 1000LL ? UINT32_MAX : (uint32_t)(elapsed_us / 1000LL);
}

static void ld2410_selftest_handle_ack(uint16_t ack_command, uint16_t status,
                                       const uint8_t *payload, size_t payload_len)
{
    ESP_LOGI("LD2410_TEST", "ACK candidate cmd=0x%04X status=0x%04X payload=%u", ack_command, status, (unsigned)payload_len);
    uint16_t expected = s_test_state == UART_TEST_ENTER_WAIT ? 0x01FF :
                        s_test_state == UART_TEST_VERSION_WAIT ? 0x01A0 :
                        s_test_state == UART_TEST_EXIT_WAIT ? 0x01FE : 0;
    if (!expected || ack_command != expected) {
        ESP_LOGW("LD2410_TEST", "ACK semantic_valid=0 unexpected command=0x%04X expected=0x%04X", ack_command, expected);
        return;
    }
    size_t required = expected == 0x01FF ? 4 : expected == 0x01A0 ? 8 : 0;
    bool payload_valid = payload_len == required;
    if (payload_valid && expected == 0x01FF)
        payload_valid = payload[0] == 1 && payload[1] == 0 &&
                        (payload[2] != 0 || payload[3] != 0);
    bool malformed = status == 0 && !payload_valid;
    ESP_LOGI("LD2410_TEST", "ACK expected=0x%04X semantic_valid=%d status=0x%04X payload_valid=%d",
             expected, status == 0 && payload_valid, status, payload_valid);
    if (status != 0 || malformed) {
        if (malformed) s_parser_errors++;
        ESP_LOGW("LD2410_TEST", "Expected ACK failed: %s", malformed ? "MALFORMED" : "NACK");
        s_test_retries = 0;
        ld2410_set_test_state(expected == 0x01FE ? UART_TEST_DONE : UART_TEST_EXIT_SEND);
        if (s_test_state == UART_TEST_DONE) ld2410_selftest_report();
        return;
    }
    if (s_test_state == UART_TEST_ENTER_WAIT && ack_command == (LD2410_CMD_ENTER_CONFIG | LD2410_ACK_MASK)) {
        s_test_enter_ack = true;
        s_test_retries = 0;
        ld2410_set_test_state(UART_TEST_VERSION_SEND);
    } else if (s_test_state == UART_TEST_VERSION_WAIT && ack_command == (LD2410_CMD_READ_VERSION | LD2410_ACK_MASK)) {
        s_test_version_ack = true;
        if (payload_len >= 8) {
            uint16_t type = (uint16_t)(payload[0] | (payload[1] << 8));
            uint8_t minor = payload[2];
            uint8_t major = payload[3];
            uint32_t bugfix = (uint32_t)payload[4] | ((uint32_t)payload[5] << 8) |
                              ((uint32_t)payload[6] << 16) | ((uint32_t)payload[7] << 24);
            snprintf(s_test_version, sizeof(s_test_version), "type=%u V%u.%02u.%08lX",
                     type, major, minor, (unsigned long)bugfix);
            ESP_LOGI("LD2410_TEST", "Firmware version=%s", s_test_version);
        } else {
            snprintf(s_test_version, sizeof(s_test_version), "ACK payload too short (%u)", (unsigned)payload_len);
            s_test_version_ack = false;
        }
        s_test_retries = 0;
        ld2410_set_test_state(UART_TEST_EXIT_SEND);
    } else if (s_test_state == UART_TEST_EXIT_WAIT && ack_command == (LD2410_CMD_EXIT_CONFIG | LD2410_ACK_MASK)) {
        s_test_exit_ack = true;
        ld2410_set_test_state(UART_TEST_DONE);
        ld2410_selftest_report();
    }
}

/* Single framing pass: never search for ACK headers inside measurement payloads.
 * Both frame families retain partial bytes; all complete frames are drained. */
static void ld2410_process_stream(void)
{
    const uint8_t ack_header[] = {0xFD,0xFC,0xFB,0xFA};
    const uint8_t measurement_header[] = {0xF4,0xF3,0xF2,0xF1};
    const uint8_t ack_footer[] = {4,3,2,1};
    const uint8_t measurement_footer[] = {0xF8,0xF7,0xF6,0xF5};
    while (s_rx_stream_len >= 4) {
        bool ack = memcmp(s_rx_stream, ack_header, 4) == 0;
        bool measurement = memcmp(s_rx_stream, measurement_header, 4) == 0;
        size_t consumed = 1;
        if (ack || measurement) {
            if (s_rx_stream_len < 6) return;
            size_t n = s_rx_stream[4] | ((size_t)s_rx_stream[5] << 8);
            size_t total = n + 10;
            if ((ack && (n < 2 || n > 64)) ||
                (measurement && n != 13 && n != 35)) {
                ld2410_diag_frame("INVALID", 6, false);
                s_parser_errors++;
                ESP_LOGW("LD2410_TEST", "Invalid %s length=%u errors=%lu", ack ? "ACK" : "measurement", (unsigned)n, (unsigned long)s_parser_errors);
            } else {
                if (s_rx_stream_len < total) return;
                bool good = memcmp(s_rx_stream + 6 + n, ack ? ack_footer : measurement_footer, 4) == 0;
                if (measurement) good = good && s_rx_stream[7] == 0xAA &&
                    s_rx_stream[6] == (n == 13 ? 2 : 1) && s_rx_stream[8] <= 3 &&
                    s_rx_stream[6+n-2] == 0x55 && s_rx_stream[6+n-1] == 0;
                if (!good) {
                    ld2410_diag_frame("INVALID", total, false);
                    s_parser_errors++;
                    ESP_LOGW("LD2410_TEST", "Invalid %s frame length=%u errors=%lu", ack ? "ACK" : "measurement", (unsigned)total, (unsigned long)s_parser_errors);
                } else {
                    consumed = total;
                    if (ack) {
                        uint16_t command = s_rx_stream[6] | (s_rx_stream[7]<<8);
                        if (ld2410_is_tx_echo(s_rx_stream, total)) {
                            s_echo_frames++;
                            ld2410_diag_frame("COMMAND_ECHO", total, true);
                            /* Do not interpret ENABLE parameter 0x0001 as NACK status. */
                        } else if ((command & 0xFF00) != 0x0100) {
                            s_unknown_frames++;
                            ld2410_diag_frame("UNKNOWN", total, true);
                        } else if (n < 4) {
                            s_parser_errors++;
                            ld2410_diag_frame("INVALID", total, true);
                        } else {
                            ld2410_diag_frame("ACK", total, true);
                            ld2410_selftest_handle_ack(command,
                                s_rx_stream[8] | (s_rx_stream[9]<<8), s_rx_stream+10, n-4);
                        }
                    } else {
                        ld2410_diag_frame("MEASUREMENT", total, true);
                        if (!s_normal_frame_seen) ESP_LOGI("LD2410_TEST", "RX type=measurement length=%u valid=1", (unsigned)total);
                        portENTER_CRITICAL(&s_snapshot_lock);
                        memcpy(s_latest, s_rx_stream, sizeof(s_latest));
                        s_latest_pending = true;
                        s_last_valid_frame_us = esp_timer_get_time();
                        portEXIT_CRITICAL(&s_snapshot_lock);
                        s_normal_frame_seen = true;
                    }
                }
            }
        }
        memmove(s_rx_stream, s_rx_stream+consumed, s_rx_stream_len-consumed);
        s_rx_stream_len -= consumed;
    }
}

/* UART events are metadata, not a second byte consumer. Never flush the RX ring. */
static void ld2410_process_uart_events(void)
{
    uart_event_t event;
    for (unsigned i = 0; s_uart_events && i < 16 && xQueueReceive(s_uart_events, &event, 0) == pdTRUE; ++i) {
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            s_uart_overflows++;
            s_rx_stream_len = 0; /* partial frame may straddle lost bytes */
            ESP_LOGW("LD2410_TEST", "UART overflow event=%d count=%lu", event.type, (unsigned long)s_uart_overflows);
        } else if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR || event.type == UART_BREAK) {
            s_uart_errors++;
            ESP_LOGW("LD2410_TEST", "UART error event=%d count=%lu", event.type, (unsigned long)s_uart_errors);
        }
    }
}

static void ld2410_receive_bytes(const uint8_t *bytes, size_t n)
{
    s_total_rx_bytes += n;
    portENTER_CRITICAL(&s_snapshot_lock);
    s_last_rx_bytes = n;
    portEXIT_CRITICAL(&s_snapshot_lock);
    /* Raw bytes exactly as read, before framing or any stream transformation. */
    ld2410_diag_rx(bytes, n);
    for (size_t i = 0; i < n; ++i) {
        if (s_rx_stream_len == sizeof(s_rx_stream)) {
            s_rx_stream_len = 0;
            s_parser_errors++;
            ESP_LOGW("LD2410_TEST", "Parser stream buffer overflow");
        }
        s_rx_stream[s_rx_stream_len++] = bytes[i];
        ld2410_process_stream();
    }
}

static void ld2410_uart_poll_once(void)
{
    uint8_t bytes[256];
    ld2410_diag_begin();
    ld2410_process_uart_events();
    int n = uart_read_bytes(RADAR_UART_NUM, bytes, sizeof(bytes), pdMS_TO_TICKS(10));
    if (n > 0) ld2410_receive_bytes(bytes, (size_t)n);
    else if (n < 0) {
        s_uart_errors++;
        if (s_uart_errors == 1) ESP_LOGE("LD2410_TEST", "UART read failed");
    }
    /* Process queued replies before expiring their deadline, including when
     * the worker was delayed by scheduling. WAIT is entered before the next read. */
    ld2410_selftest_step();
}

static void ld2410_uart_task(void *arg)
{
    (void)arg;
    ESP_LOGI("LD2410_TEST", "UART1 RX=%d TX=%d baud=%d", RADAR_RX_PIN, RADAR_TX_PIN, RADAR_BAUD_RATE);
    for (;;) {
        ld2410_uart_poll_once();
        vTaskDelay(1);
    }
}

static void ld2410_selftest_step(void)
{
#if LD2410_UART_SELFTEST_ENABLED
    static const uint8_t enter_param[] = {0x01, 0x00};
    int64_t now = esp_timer_get_time();
    switch (s_test_state) {
    case UART_TEST_IDLE:
        ld2410_set_test_state(UART_TEST_ENTER_SEND);
        ESP_LOGI("LD2410_TEST", "Self-test started");
        /* fall through */
    case UART_TEST_ENTER_SEND:
        ld2410_selftest_send(LD2410_CMD_ENTER_CONFIG, enter_param, sizeof(enter_param));
        ld2410_set_test_state(UART_TEST_ENTER_WAIT);
        break;
    case UART_TEST_VERSION_SEND:
        ESP_LOGI("LD2410_TEST", "Firmware query sent");
        ld2410_selftest_send(LD2410_CMD_READ_VERSION, NULL, 0);
        ld2410_set_test_state(UART_TEST_VERSION_WAIT);
        break;
    case UART_TEST_EXIT_SEND:
        ld2410_selftest_send(LD2410_CMD_EXIT_CONFIG, NULL, 0);
        ld2410_set_test_state(UART_TEST_EXIT_WAIT);
        break;
    case UART_TEST_ENTER_WAIT:
        if (now > s_test_deadline_us) ld2410_selftest_fail_or_retry(UART_TEST_ENTER_SEND, "Enter configuration ACK");
        break;
    case UART_TEST_VERSION_WAIT:
        if (now > s_test_deadline_us) ld2410_selftest_fail_or_retry(UART_TEST_VERSION_SEND, "Firmware version ACK");
        break;
    case UART_TEST_EXIT_WAIT:
        if (now > s_test_deadline_us) {
            s_timeouts++;
            if (s_test_retries < 1) {
                s_test_retries++;
                ESP_LOGW("LD2410_TEST", "Exit configuration ACK TIMEOUT; retrying once");
                ld2410_set_test_state(UART_TEST_EXIT_SEND);
            } else {
                ESP_LOGW("LD2410_TEST", "Exit configuration ACK TIMEOUT after retry; radar mode unconfirmed");
                ld2410_set_test_state(UART_TEST_DONE);
                ld2410_selftest_report();
            }
        }
        break;
    case UART_TEST_DONE:
    default:
        break;
    }
#else
    ld2410_set_test_state(UART_TEST_DONE);
#endif
}

bool ld2410_parse_target_data(uint8_t *target_state, uint16_t *move_dist, 
                              uint8_t *move_energy, uint16_t *stat_dist, 
                              uint8_t *stat_energy)
{
    int presence = 0;
    int out_pin = 0;
    radar_target_mode_t mode = RADAR_MODE_NO_TARGET;
    bool valid = false;
    esp_err_t err = ESP_OK;

    bool uart_valid = false;
    esp_err_t ret = ld2410_radar_read(&presence, &out_pin, &mode,
                                      move_dist, move_energy,
                                      stat_dist, stat_energy,
                                       &valid, &uart_valid, &err);
    if (ret == ESP_OK && valid) {
        if (target_state) *target_state = (uint8_t)mode;
        return true;
    }
    return false;
}

esp_err_t ld2410_radar_read(int *presence_out, int *out_pin_val, radar_target_mode_t *mode_out,
                            uint16_t *move_dist, uint8_t *move_energy,
                            uint16_t *stat_dist, uint8_t *stat_energy,
                             bool *valid, bool *uart_valid, esp_err_t *err_code)
{
    if (uart_valid) *uart_valid = false;
    if (!s_initialized) {
        if (valid) *valid = false;
        if (err_code) *err_code = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Luôn đọc chân phần cứng OUT
    int out_val = gpio_get_level(RADAR_OUT_PIN);
    if (out_pin_val) *out_pin_val = out_val;

    // The RX task is the only UART consumer. Copy the latest measurement once.
    uint8_t frame[23];
    portENTER_CRITICAL(&s_snapshot_lock);
    bool pending = s_latest_pending && esp_timer_get_time() - s_last_valid_frame_us <= 2000000LL;
    if (pending) memcpy(frame, s_latest, sizeof(frame));
    s_latest_pending = false;
    portEXIT_CRITICAL(&s_snapshot_lock);
    if (pending) {
        uint8_t state = frame[8];
        if (mode_out) *mode_out = (radar_target_mode_t)state;
        if (move_dist) *move_dist = frame[9] | (frame[10] << 8);
        if (move_energy) *move_energy = frame[11];
        if (stat_dist) *stat_dist = frame[12] | (frame[13] << 8);
        if (stat_energy) *stat_energy = frame[14];
        if (presence_out) *presence_out = state > 0 || out_val > 0;
        if (valid) *valid = true;
        if (uart_valid) *uart_valid = true;
        if (err_code) *err_code = ESP_OK;
        return ESP_OK;
    }

    // No complete valid UART frame yet: OUT remains the existing valid fallback.
    if (presence_out) *presence_out = out_val;
    if (mode_out)    *mode_out = (out_val > 0) ? RADAR_MODE_STATIC : RADAR_MODE_NO_TARGET;
    if (valid) *valid = true;
    if (err_code) *err_code = ESP_OK;
    return ESP_OK;
}
