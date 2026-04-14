#include "esp_boost.h"
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <string.h>
#include <stdlib.h>

#define TAG "EspBoost"
#define ESP_BOOST_BAUDRATE 115200
#define ESP_BOOST_UART_CH FuriHalSerialIdUsart
#define ESP_RX_BUF_SIZE 256
#define ESP_PROBE_TIMEOUT_MS 1500

struct EspBoost {
    FuriHalSerialHandle* serial;
    EspFirmwareType firmware;
    bool connected;
    bool active;
    // RX buffer for async callback
    uint8_t rx_buf[ESP_RX_BUF_SIZE];
    volatile size_t rx_len;
};

// Ghost ESP commands
static const char* ghost_cmd[] = {
    [EspBoostCmdApple] = "blespam -apple\n",
    [EspBoostCmdSamsung] = "blespam -samsung\n",
    [EspBoostCmdGoogle] = "blespam -google\n",
    [EspBoostCmdWindows] = "blespam -ms\n",
    [EspBoostCmdRandom] = "blespam -random\n",
};
static const char* ghost_stop = "blespam -s\n";

// Marauder commands
static const char* marauder_cmd[] = {
    [EspBoostCmdApple] = "blespam -t apple\n",
    [EspBoostCmdSamsung] = "blespam -t samsung\n",
    [EspBoostCmdGoogle] = "blespam -t google\n",
    [EspBoostCmdWindows] = "blespam -t microsoft\n",
    [EspBoostCmdRandom] = "blespam -t random\n",
};
static const char* marauder_stop = "stopscan\n";

static void esp_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UNUSED(handle);
    EspBoost* boost = ctx;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(boost->rx_len < ESP_RX_BUF_SIZE - 1) {
            boost->rx_buf[boost->rx_len++] = byte;
        }
    }
}

static void esp_send(EspBoost* boost, const char* cmd) {
    if(!boost || !boost->serial) return;
    furi_hal_serial_tx(boost->serial, (const uint8_t*)cmd, strlen(cmd));
}

static void esp_flush_rx(EspBoost* boost) {
    boost->rx_len = 0;
    memset(boost->rx_buf, 0, ESP_RX_BUF_SIZE);
}

static EspFirmwareType esp_detect_firmware(EspBoost* boost) {
    // Try to get a response from the ESP32
    // Send newline first to clear any pending input
    esp_flush_rx(boost);
    esp_send(boost, "\n");
    furi_delay_ms(200);
    esp_flush_rx(boost);

    // Ghost ESP responds to "info\n" with version info containing "Ghost" or "GhostESP"
    esp_send(boost, "info\n");
    furi_delay_ms(ESP_PROBE_TIMEOUT_MS);

    if(boost->rx_len > 0) {
        boost->rx_buf[boost->rx_len] = 0; // null-terminate
        FURI_LOG_I(TAG, "ESP response (%d bytes): %.64s", (int)boost->rx_len, boost->rx_buf);

        // Check for Ghost ESP signature
        if(strstr((char*)boost->rx_buf, "Ghost") ||
           strstr((char*)boost->rx_buf, "ghost") ||
           strstr((char*)boost->rx_buf, "GHOST") ||
           strstr((char*)boost->rx_buf, "blespam")) {
            FURI_LOG_I(TAG, "Detected: Ghost ESP");
            return EspFirmwareGhost;
        }
    }

    // Try Marauder: responds to empty line or "help\n"
    esp_flush_rx(boost);
    esp_send(boost, "help\n");
    furi_delay_ms(ESP_PROBE_TIMEOUT_MS);

    if(boost->rx_len > 0) {
        boost->rx_buf[boost->rx_len] = 0;
        FURI_LOG_I(TAG, "ESP response2 (%d bytes): %.64s", (int)boost->rx_len, boost->rx_buf);

        if(strstr((char*)boost->rx_buf, "marauder") ||
           strstr((char*)boost->rx_buf, "Marauder") ||
           strstr((char*)boost->rx_buf, "MARAUDER") ||
           strstr((char*)boost->rx_buf, "blespam") ||
           strstr((char*)boost->rx_buf, "stopscan") ||
           strstr((char*)boost->rx_buf, "scanap")) {
            FURI_LOG_I(TAG, "Detected: Marauder");
            return EspFirmwareMarauder;
        }

        // Got some response but can't identify - assume Marauder (more common on "Marauder" boards)
        if(boost->rx_len > 10) {
            FURI_LOG_I(TAG, "Unknown firmware, assuming Marauder");
            return EspFirmwareMarauder;
        }
    }

    FURI_LOG_I(TAG, "No ESP32 response detected");
    return EspFirmwareNone;
}

EspBoost* esp_boost_init(void) {
    EspBoost* boost = malloc(sizeof(EspBoost));
    memset(boost, 0, sizeof(EspBoost));

    boost->serial = furi_hal_serial_control_acquire(ESP_BOOST_UART_CH);
    if(!boost->serial) {
        FURI_LOG_I(TAG, "UART busy (another app holds it?)");
        free(boost);
        return NULL;
    }

    furi_hal_serial_init(boost->serial, ESP_BOOST_BAUDRATE);

    // Set up async RX to read responses
    furi_hal_serial_async_rx_start(boost->serial, esp_rx_cb, boost, false);

    // Detect firmware
    boost->firmware = esp_detect_firmware(boost);

    if(boost->firmware == EspFirmwareNone) {
        FURI_LOG_I(TAG, "No ESP32 found, boost disabled");
        furi_hal_serial_async_rx_stop(boost->serial);
        furi_hal_serial_deinit(boost->serial);
        furi_hal_serial_control_release(boost->serial);
        free(boost);
        return NULL;
    }

    boost->connected = true;
    FURI_LOG_I(
        TAG,
        "ESP32 boost enabled (%s)",
        boost->firmware == EspFirmwareGhost ? "Ghost" : "Marauder");
    return boost;
}

void esp_boost_free(EspBoost* boost) {
    if(!boost) return;
    if(boost->active) esp_boost_stop(boost);
    if(boost->serial) {
        furi_hal_serial_async_rx_stop(boost->serial);
        furi_hal_serial_deinit(boost->serial);
        furi_hal_serial_control_release(boost->serial);
    }
    free(boost);
}

bool esp_boost_is_connected(EspBoost* boost) {
    return boost && boost->connected;
}

EspFirmwareType esp_boost_get_firmware(EspBoost* boost) {
    if(!boost) return EspFirmwareNone;
    return boost->firmware;
}

void esp_boost_start(EspBoost* boost, EspBoostCmd cmd) {
    if(!boost || !boost->connected) return;
    if(cmd >= 5) return; // bounds check

    // Stop previous if active
    if(boost->active) {
        esp_boost_stop(boost);
        furi_delay_ms(50);
    }

    const char* cmd_str;
    if(boost->firmware == EspFirmwareGhost) {
        cmd_str = ghost_cmd[cmd];
    } else {
        cmd_str = marauder_cmd[cmd];
    }

    FURI_LOG_I(TAG, "Start: %s", cmd_str);
    esp_send(boost, cmd_str);
    boost->active = true;
}

void esp_boost_stop(EspBoost* boost) {
    if(!boost || !boost->connected) return;

    if(boost->firmware == EspFirmwareGhost) {
        esp_send(boost, ghost_stop);
    } else {
        esp_send(boost, marauder_stop);
    }

    boost->active = false;
    FURI_LOG_I(TAG, "Stopped");
}
