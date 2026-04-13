# Flipper Custom Apps

Custom Flipper Zero applications by dwgx.

## Apps

### x_ble_spam
BLE advertisement spammer with ESP32 boost support.
- 39 device profiles: Apple (AirPods, AirTag, Vision Pro, AppleTV...), Samsung (Buds, Watch), Android Fast Pair, Windows Swift Pair, LoveSpouse
- Single-file architecture using official `furi_hal_bt_extra_beacon_*` API
- Optional ESP32 dual-radio boost via Ghost ESP UART — transparent enhancement, works without ESP32 too
- Rotate mode cycles through all devices automatically

**Build:**
```bash
cd x_ble_spam
ufbt build
```

## ESP32 Boost Feature
When an ESP32 with Ghost ESP firmware is connected via GPIO UART (TX=13, RX=14, GND=8, 3V3=9), the app automatically enables dual-radio broadcasting for ~10-20m range instead of ~1-3m. If no ESP32 is detected, the app works exactly as before.

## License
For personal/educational use only.
