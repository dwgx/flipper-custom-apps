#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EspBoostCmdApple,
    EspBoostCmdSamsung,
    EspBoostCmdGoogle,
    EspBoostCmdWindows,
    EspBoostCmdRandom,
} EspBoostCmd;

typedef struct EspBoost EspBoost;

EspBoost* esp_boost_init(void);
void esp_boost_free(EspBoost* boost);
bool esp_boost_is_connected(EspBoost* boost);
void esp_boost_start(EspBoost* boost, EspBoostCmd cmd);
void esp_boost_stop(EspBoost* boost);
