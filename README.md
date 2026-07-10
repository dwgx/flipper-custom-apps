# Flipper Custom Apps / Flipper 自定义应用

BLE advertisement spammer for Flipper Zero — fake device popups on demand.

Flipper Zero 的 BLE 广播轰炸器——随时伪造设备弹窗。

## Overview / 概述

Custom Flipper Zero application that broadcasts spoofed Bluetooth Low Energy advertisement packets, triggering pairing popups and device notifications on nearby Apple, Samsung, Android, Windows, and LoveSpouse devices.

Runs entirely on the Flipper's built-in radio. Optionally connects an ESP32 as a second radio for dual-broadcast.

自定义 Flipper Zero 应用，发送伪造的 BLE 广播包，在附近的 Apple、三星、Android、Windows 和 LoveSpouse 设备上触发配对弹窗和设备通知。

完全使用 Flipper 内置射频运行。可选连接 ESP32 作为第二射频，实现双通道广播。

## Features / 功能

- **X-BLE Spam** — single external FAP app (category: Bluetooth, v1.1)
- 40 selectable BLE device profiles + `[ALL] Rotate` auto-cycle mode:
  - **Apple Proximity Pair**: AirPods (1/2/3/Pro/Pro 2/Max), Beats 系列, AirTag, Hermes AirTag
  - **Apple Nearby Action**: Apple Vision Pro, AppleTV 配对, HomePod Setup, HomeKit, Transfer Number, Sign In 等
  - **Samsung**: Buds Pro/2/Live/FE/Buds2, Galaxy Watch 4/5/6/Ultra (EasySetup)
  - **Android** Fast Pair (random model IDs)
  - **Windows** Swift Pair (advertises as "Flipper Z")
  - **LoveSpouse** Play / Stop
- Random BLE MAC address per advertisement cycle
- On-device UI: Prev/Next profile selection, Start/Stop, live broadcast status, LED feedback (blue = OK, red = error)
- Uses official `furi_hal_bt_extra_beacon_*` API, cleanly restores normal BT on exit
- **ESP32 Boost**: dual-radio broadcasting when ESP32 connected; graceful fallback when absent

---

- **X-BLE Spam** — 单个外部 FAP 应用（分类：Bluetooth，版本 1.1）
- 40 个可选 BLE 设备配置 + `[ALL] Rotate` 自动轮播模式
- 每次广播随机生成 BLE MAC 地址
- 设备端 UI：左右选择配置、一键开始/停止、实时广播状态、LED 指示
- 使用官方 `furi_hal_bt_extra_beacon_*` API，退出时干净恢复正常蓝牙
- **ESP32 增强**：接入 ESP32 时双射频同时广播，未接入时正常单射频运行

## Tech Stack / 技术栈

- **Language / 语言**: C
- **Platform / 平台**: Flipper Zero firmware (`furi`, `furi_hal_bt`, `furi_hal_serial`, `gui`, `extra_beacon`, `expansion` APIs)
- **App manifest**: `application.fam` (`FlipperAppType.EXTERNAL`)
- **Build tool / 构建工具**: [`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt) <!-- TODO: confirm; inferred from build commands, no ufbt config committed -->

## Project Structure / 项目结构

```
.
├── LICENSE
├── README.md
├── SECURITY.md
└── x_ble_spam/
    ├── application.fam     # Flipper app manifest
    ├── x_ble_spam.c        # Entry point, UI, BLE packet builders, worker thread
    ├── esp_boost.c         # ESP32 UART boost (Ghost ESP / Marauder commands)
    ├── esp_boost.h         # ESP boost interface
    └── x_ble_spam.png      # App icon
```

## Getting Started / 快速开始

### Prerequisites / 前置条件

- Flipper Zero
- [`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt) installed (`pip install ufbt`) <!-- TODO: confirm build toolchain -->
- (Optional) ESP32 flashed with Ghost ESP or Marauder firmware

### Build / 构建

```bash
cd x_ble_spam
ufbt build
```

Produces a `.fap` — copy to Flipper's `apps/Bluetooth/` directory. Or build and launch directly:

```bash
cd x_ble_spam
ufbt launch
```
<!-- TODO: confirm launch command / deploy path against your ufbt setup -->

## Usage / 使用方法

1. Launch **X-BLE Spam** from Flipper's Bluetooth apps menu / 从蓝牙应用菜单启动
2. Left/Right to select device profile / 左右选择设备配置
3. OK to start broadcasting, OK again to stop / OK 开始广播，再按 OK 停止
4. Select `[ALL] Rotate` to cycle all profiles automatically / 选择 `[ALL] Rotate` 自动轮播所有配置
5. Back to stop and exit / Back 停止并退出

Status line shows `Broadcasting  BLE:OK` / `BLE:ERR`. `[ESP]` tag appears when ESP32 boost is active.

状态栏显示 `Broadcasting  BLE:OK` / `BLE:ERR`。连接 ESP32 时显示 `[ESP]` 标签。

## ESP32 Boost / ESP32 增强

When an ESP32 running Ghost ESP or Marauder firmware is connected via Flipper GPIO UART, the app forwards `blespam` commands so both radios broadcast simultaneously.

当通过 Flipper GPIO UART 连接运行 Ghost ESP 或 Marauder 固件的 ESP32 时，应用会转发 `blespam` 命令，双射频同时广播。

- UART: `FuriHalSerialIdUsart` @ **115200 baud**
- Dual-fire: sends both Ghost ESP (`blespam -apple`) and Marauder (`blespam -t apple`) command styles — ESP32 runs whichever matches its firmware
- Stop: sends `blespam -s` and `stopscan`
- Wiring: `TX=13, RX=14, GND=8, 3V3=9` <!-- TODO: confirm pin numbers against your ESP32 wiring -->
- If no ESP32 detected (UART busy/acquire fails), boost skipped, Flipper radio works alone

## Status / 状态

个人项目，X-BLE Spam v1.1，功能完整可用。

Personal project. X-BLE Spam v1.1, fully functional.

## License / 许可证

Custom license — see [LICENSE](LICENSE).

- Personal study, educational use, and technical research only
- No commercial use, redistribution, resale, or sublicensing without written permission
- Not an OSI-approved open-source license

自定义许可证——仅限个人学习、教育用途和技术研究。商业使用、再分发、转售需书面授权。详见 [LICENSE](LICENSE)。
