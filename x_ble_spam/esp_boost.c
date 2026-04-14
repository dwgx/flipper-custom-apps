#include "esp_boost.h"
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <expansion/expansion.h>
#include <string.h>
#include <stdlib.h>

#define TAG "EspBoost"
#define ESP_BOOST_BAUDRATE 115200
#define ESP_BOOST_UART_CH FuriHalSerialIdUsart

struct EspBoost {
    FuriHalSerialHandle* serial;
    Expansion* expansion;
    bool connected;
    bool active;
};

// Dual-fire: send both Ghost ESP and Marauder commands.
// The ESP32 executes whichever matches its firmware, ignores the other.

static const char* ghost_start[] = {
    [EspBoostCmdApple] = "blespam -apple\n",
    [EspBoostCmdSamsung] = "blespam -samsung\n",
    [EspBoostCmdGoogle] = "blespam -google\n",
    [EspBoostCmdWindows] = "blespam -ms\n",
    [EspBoostCmdRandom] = "blespam -random\n",
};

static const char* marauder_start[] = {
    [EspBoostCmdApple] = "blespam -t apple\n",
    [EspBoostCmdSamsung] = "blespam -t samsung\n",
    [EspBoostCmdGoogle] = "blespam -t google\n",
    [EspBoostCmdWindows] = "blespam -t microsoft\n",
    [EspBoostCmdRandom] = "blespam -t random\n",
};

static void esp_send(EspBoost* boost, const char* cmd) {
    if(!boost || !boost->serial) return;
    furi_hal_serial_tx(boost->serial, (const uint8_t*)cmd, strlen(cmd));
    furi_hal_serial_tx_wait_complete(boost->serial);
}

EspBoost* esp_boost_init(void) {
    EspBoost* boost = malloc(sizeof(EspBoost));
    memset(boost, 0, sizeof(EspBoost));

    // MUST disable expansion module before acquiring UART
    boost->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(boost->expansion);

    if(furi_hal_serial_control_is_busy(ESP_BOOST_UART_CH)) {
        FURI_LOG_I(TAG, "UART busy, ESP boost disabled");
        expansion_enable(boost->expansion);
        furi_record_close(RECORD_EXPANSION);
        free(boost);
        return NULL;
    }

    boost->serial = furi_hal_serial_control_acquire(ESP_BOOST_UART_CH);
    if(!boost->serial) {
        FURI_LOG_I(TAG, "UART acquire failed");
        expansion_enable(boost->expansion);
        furi_record_close(RECORD_EXPANSION);
        free(boost);
        return NULL;
    }

    furi_hal_serial_init(boost->serial, ESP_BOOST_BAUDRATE);
    boost->connected = true;
    FURI_LOG_I(TAG, "ESP32 UART acquired, boost ready");
    return boost;
}

void esp_boost_free(EspBoost* boost) {
    if(!boost) return;
    if(boost->active) esp_boost_stop(boost);
    if(boost->serial) {
        furi_hal_serial_deinit(boost->serial);
        furi_hal_serial_control_release(boost->serial);
    }
    // Re-enable expansion module
    if(boost->expansion) {
        expansion_enable(boost->expansion);
        furi_record_close(RECORD_EXPANSION);
    }
    free(boost);
}

bool esp_boost_is_connected(EspBoost* boost) {
    return boost && boost->connected;
}

void esp_boost_start(EspBoost* boost, EspBoostCmd cmd) {
    if(!boost || !boost->connected) return;
    if(cmd >= 5) return;

    if(boost->active) {
        esp_boost_stop(boost);
        furi_delay_ms(50);
    }

    FURI_LOG_I(TAG, "Dual-fire start: cmd=%d", cmd);
    esp_send(boost, ghost_start[cmd]);
    furi_delay_ms(20);
    esp_send(boost, marauder_start[cmd]);
    boost->active = true;
}

void esp_boost_stop(EspBoost* boost) {
    if(!boost || !boost->connected) return;
    esp_send(boost, "blespam -s\n");
    furi_delay_ms(20);
    esp_send(boost, "stopscan\n");
    boost->active = false;
    FURI_LOG_I(TAG, "Stopped");
}
