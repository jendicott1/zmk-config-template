/*
 * ZMK Custom Module: UART Command Control (v2 - with BLE Control)
 *
 * This module initializes a UART peripheral, reads incoming data,
 * and maps specific string commands to ZMK Consumer Control (HID)
 * events OR ZMK Bluetooth control functions.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zmk/hid.h>
#include <zmk/ble.h> // NEW: Required for Bluetooth control functions
#include <zmk/events/activity_state_changed.h>
#include <zmk/event_manager.h>
#include <string.h>
#include <ctype.h>

LOG_MODULE_REGISTER(uart_control, LOG_LEVEL_INF);

#define UART_DEVICE_NODE DT_ALIAS(uart_pi)
#define RX_BUF_SIZE 16
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_buf_len = 0;

// Command mapping structure for Media Keys
struct command_map_media {
    const char *command;
    enum zmk_hid_consumer_control code;
};

// Define the Media Key command map
static const struct command_map_media media_command_list[] = {
    {"PLAY", ZMK_HID_CONSUMER_CONTROL_PLAY_PAUSE},
    {"VOLU", ZMK_HID_CONSUMER_CONTROL_VOLUME_UP},
    {"VOLD", ZMK_HID_CONSUMER_CONTROL_VOLUME_DOWN},
    {"NEXT", ZMK_HID_CONSUMER_CONTROL_SCAN_NEXT_TRACK},
    {"PREV", ZMK_HID_CONSUMER_CONTROL_SCAN_PREVIOUS_TRACK},
};
#define NUM_MEDIA_COMMANDS (sizeof(media_command_list) / sizeof(media_command_list[0]))


static void send_consumer_event(enum zmk_hid_consumer_control code) {
    // Send press
    if (zmk_hid_consumer_control_press(code) == 0) {
        k_sleep(K_MSEC(50));
        // Send release
        zmk_hid_consumer_control_release(code);
    } else {
        LOG_ERR("Failed to send consumer event.");
    }
}

static void process_command() {
    // Standard command processing start (null termination and uppercase)
    if (rx_buf_len < RX_BUF_SIZE) {
        rx_buf[rx_buf_len] = '\0';
    } else {
        rx_buf[RX_BUF_SIZE - 1] = '\0';
    }
    for (int i = 0; i < rx_buf_len; i++) {
        rx_buf[i] = toupper(rx_buf[i]);
    }

    LOG_INF("Received command: %s (len: %d)", rx_buf, rx_buf_len);
    
    const char *command_str = (const char *)rx_buf;


    // --- 1. Process BLE Control Commands ---
    if (strcmp(command_str, "CLEAR") == 0) {
        LOG_INF("BLE Command: Clearing bonds.");
        zmk_ble_clear_bonds();
        return;
    }
    if (strcmp(command_str, "PAIR") == 0) {
        LOG_INF("BLE Command: Entering pairing mode.");
        zmk_ble_set_bondable(true);
        return;
    }
    if (strcmp(command_str, "DISCO") == 0) {
        LOG_INF("BLE Command: Disconnecting all devices.");
        zmk_ble_disconnect_all();
        return;
    }
    if (strcmp(command_str, "ADV") == 0) {
        LOG_INF("BLE Command: Starting advertising.");
        zmk_ble_advertiser_start();
        return;
    }


    // --- 2. Process Media Key Commands ---
    for (int i = 0; i < NUM_MEDIA_COMMANDS; i++) {
        if (strncmp(command_str, media_command_list[i].command, strlen(media_command_list[i].command)) == 0) {
            send_consumer_event(media_command_list[i].code);
            return;
        }
    }

    LOG_WRN("Unknown command received: %s", command_str);
}


// UART IRQ and Thread functions remain the same
static void uart_control_cb(const struct device *dev, void *user_data) {
    if (!uart_irq_update(dev) || !uart_irq_rx_ready(dev)) {
        return;
    }

    while (true) {
        uint8_t byte;
        int ret = uart_fifo_read(dev, &byte, 1);

        if (ret <= 0) {
            break;
        }

        if (byte == '\n' || byte == '\r') {
            if (rx_buf_len > 0) {
                process_command();
                rx_buf_len = 0; 
            }
        } else if (rx_buf_len < (RX_BUF_SIZE - 1)) {
            rx_buf[rx_buf_len++] = byte;
        } else {
            LOG_ERR("Command buffer overflowed!");
            rx_buf_len = 0;
        }
    }
}

#define UART_CONTROL_STACK_SIZE 512
K_THREAD_STACK_DEFINE(uart_control_stack_area, UART_CONTROL_STACK_SIZE);
static struct k_thread uart_control_thread_data;

void uart_control_thread(void *p1, void *p2, void *p3) {
    const struct device *uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready!");
        return;
    }
    uart_irq_callback_set(uart_dev, uart_control_cb, NULL);
    uart_irq_rx_enable(uart_dev);

    LOG_INF("UART control module initialized and listening.");

    while (1) {
        k_sleep(K_SECONDS(10));
    }
}

int uart_control_init(void) {
    k_thread_create(&uart_control_thread_data, uart_control_stack_area,
                    K_THREAD_STACK_SIZE_ADJUST(UART_CONTROL_STACK_SIZE),
                    uart_control_thread, NULL, NULL, NULL,
                    K_PRIO_COOP(10), 0, K_NO_WAIT);
    k_thread_name_set(&uart_control_thread_data, "uart_control");
    return 0;
}

SYS_INIT(uart_control_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
