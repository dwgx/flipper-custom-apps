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
#define ESP_PROBE_DELAY_MS 300

struct EspBoost {
    FuriHalSerialHandle* serial;
    bool connected;
    bool active;
};

static const char* cmd_strings[] = {
    [EspBoostCmdApple] = "blespam -apple\n",
    [EspBoostCmdSamsung] = "blespam -samsung\n",
    [EspBoostCmdGoogle] = "blespam -google\n",
    [EspBoostCmdWindows] = "blespam -ms\n",
    [EspBoostCmdRandom] = "blespam -random\n",
};

static void esp_send(EspBoost* boost, const char* cmd) {
    if(!boost || !boost->serial) return;
    furi_hal_serial_tx(boost->serial, (const uint8_t*)cmd, strlen(cmd));
}

EspBoost* esp_boost_init(void) {
    EspBoost* boost = malloc(sizeof(EspBoost));
    memset(boost, 0, sizeof(EspBoost));

    boost->serial = furi_hal_serial_control_acquire(ESP_BOOST_UART_CH);
    if(!boost->serial) {
        FURI_LOG_I(TAG, "UART unavailable, ESP boost disabled");
        free(boost);
        return NULL;
    }

    furi_hal_serial_init(boost->serial, ESP_BOOST_BAUDRATE);
    esp_send(boost, "info\n");
    furi_delay_ms(ESP_PROBE_DELAY_MS);
    boost->connected = true;
    FURI_LOG_I(TAG, "ESP32 boost enabled");
    return boost;
}

void esp_boost_free(EspBoost* boost) {
    if(!boost) return;
    if(boost->active) esp_boost_stop(boost);
    if(boost->serial) {
        furi_hal_serial_deinit(boost->serial);
        furi_hal_serial_control_release(boost->serial);
    }
    free(boost);
}

bool esp_boost_is_connected(EspBoost* boost) {
    return boost && boost->connected;
}

void esp_boost_start(EspBoost* boost, EspBoostCmd cmd) {
    if(!boost || !boost->connected) return;
    if(cmd >= sizeof(cmd_strings) / sizeof(cmd_strings[0])) return;
    if(boost->active) {
        esp_send(boost, "blespam -s\n");
        furi_delay_ms(30);
    }
    FURI_LOG_I(TAG, "Start: %s", cmd_strings[cmd]);
    esp_send(boost, cmd_strings[cmd]);
    boost->active = true;
}

void esp_boost_stop(EspBoost* boost) {
    if(!boost || !boost->connected) return;
    esp_send(boost, "blespam -s\n");
    boost->active = false;
    FURI_LOG_I(TAG, "Stopped");
}
