#pragma once

/* Compile-time diagnostics. Set to 0 to remove the corresponding log calls. */
#define SENSOR_DIAG_LOG_ENABLED       0
#define MQTT_PAYLOAD_LOG_ENABLED      0
#define RADAR_UART_HEX_DUMP_ENABLED   0
#ifndef LD2410_UART_SELFTEST_ENABLED
#define LD2410_UART_SELFTEST_ENABLED  0
#endif

/* Bounds the optional dump to actual received UART bytes. */
#define RADAR_UART_HEX_DUMP_MAX_BYTES 64

/* Temporary bounded capture: first 8 seconds, at most 1024 raw RX bytes.
 * Set to 0 to remove raw dumps, frame tracing and state-transition logging. */
#ifndef LD2410_UART_DIAGNOSTICS_ENABLED
#define LD2410_UART_DIAGNOSTICS_ENABLED 0
#endif
#define LD2410_DIAG_WINDOW_US 8000000LL
#define LD2410_DIAG_RX_BUDGET 1024U
#define LD2410_DIAG_CHUNK_MAX 64U
#define LD2410_DIAG_FRAME_BUDGET 32U
