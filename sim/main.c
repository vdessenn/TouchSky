// Host simulator: runs the firmware's radar and settings screens headless
// and saves each as a BMP. Aircraft come from a captured adsb.lol response.
// Usage: touchsky_sim <map.bin> <adsb.json> <lat> <lon> <range_nm> <out_dir>
//        touchsky_sim --window <map.bin> <adsb.json|live> <lat> <lon> <range_nm>
// The second form opens a window (mouse = finger) and needs SDL2: the aircraft
// are the captured response, or polled from adsb.lol every 5 s with "live".
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adsb.h"
#include "lvgl.h"
#include "mapdata.h"
#include "net.h"
#include "radar_ui.h"
#include "settings.h"
#include "settings_ui.h"

#define WIDTH 1024
#define HEIGHT 600

static uint16_t s_frame[WIDTH * HEIGHT]; // what the panel would show, RGB565
static uint32_t s_now_ms;

static uint32_t tick(void)
{
    return s_now_ms;
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    lv_display_flush_ready(display); // direct mode: LVGL drew into s_frame already
}

// Lets a little simulated time pass (timers, clock label) and renders.
static void render(void)
{
    for (int i = 0; i < 3; i++) {
        s_now_ms += 600;
        lv_timer_handler();
    }
    lv_refr_now(NULL);
}

static void save_bmp(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    // 24-bit BMP; a negative height stores rows top-down. Rows of 1024 pixels
    // need no padding.
    uint32_t size = 54 + WIDTH * HEIGHT * 3;
    uint8_t header[54] = {'B', 'M'};
    int32_t fields[] = {size, 0, 54, 40, WIDTH, -HEIGHT, 1 | 24 << 16, 0, WIDTH * HEIGHT * 3};
    memcpy(header + 2, fields, sizeof(fields)); // little-endian host
    fwrite(header, 1, sizeof(header), f);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        uint16_t p = s_frame[i];
        uint8_t bgr[3] = {(p & 0x1F) * 255 / 31, ((p >> 5) & 0x3F) * 255 / 63, (p >> 11) * 255 / 31};
        fwrite(bgr, 1, 3, f);
    }
    fclose(f);
    printf("%s\n", path);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    rewind(f);
    char *data = malloc(*len + 1);
    if (fread(data, 1, *len, f) != *len) {
        perror(path);
        exit(1);
    }
    data[*len] = '\0';
    fclose(f);
    return data;
}

// net.h stand-ins: the simulator has no radio.
int net_scan(char ssids[][NET_SSID_LEN], int max)
{
    const char *fake[] = {"HomeNetwork", "Neighbours", "CoffeeShop"};
    int n = max < 3 ? max : 3;
    for (int i = 0; i < n; i++) {
        strcpy(ssids[i], fake[i]);
    }
    return n;
}

static void nothing(void) {}
static void nothing_saved(const settings_t *s) {}

#ifdef SIM_SDL
#include <pthread.h>
#include <unistd.h>

#define POLL_S 5

static mapdata_t s_map;
static settings_t s_settings; // written by the UI thread, read by the poll thread under s_lock
static adsb_aircraft_t s_replay[ADSB_MAX_AIRCRAFT];
static int s_replay_n;
static bool s_live;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static adsb_aircraft_t s_fresh[ADSB_MAX_AIRCRAFT]; // last poll, handed to the UI thread
static int s_fresh_n = -1;                         // -1 nothing new, -2 poll failed

// Output of a shell command (malloc'd, NUL-terminated), NULL if it failed.
static char *run(const char *cmd)
{
    FILE *p = popen(cmd, "r");
    if (!p) {
        return NULL;
    }
    size_t len = 0, cap = 1 << 17;
    char *buf = malloc(cap);
    for (;;) {
        if (len + 4096 > cap) {
            buf = realloc(buf, cap *= 2);
        }
        size_t got = fread(buf + len, 1, cap - len - 1, p);
        if (!got) {
            break;
        }
        len += got;
    }
    buf[len] = '\0';
    if (pclose(p) != 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

// LVGL is not thread-safe: this thread only fetches and parses, the UI thread
// picks the result up in the main loop.
static void *poll_thread(void *unused)
{
    static adsb_aircraft_t list[ADSB_MAX_AIRCRAFT];
    for (;;) {
        pthread_mutex_lock(&s_lock);
        settings_t s = s_settings;
        pthread_mutex_unlock(&s_lock);

        char url[128], cmd[192];
        adsb_build_url(url, sizeof(url), s.lat, s.lon, s.range_nm);
        snprintf(cmd, sizeof(cmd), "curl -sf --max-time 10 '%s'", url);
        char *body = run(cmd);
        int n = body ? adsb_parse(body, s.lat, s.lon, list, ADSB_MAX_AIRCRAFT) : -1;
        free(body);
        fprintf(stderr, n < 0 ? "poll failed\n" : "poll: %d aircraft\n", n);

        pthread_mutex_lock(&s_lock);
        if (n >= 0) {
            memcpy(s_fresh, list, n * sizeof(*list));
        }
        s_fresh_n = n >= 0 ? n : -2;
        pthread_mutex_unlock(&s_lock);
        sleep(POLL_S);
    }
    return NULL;
}

static void on_saved(const settings_t *s)
{
    pthread_mutex_lock(&s_lock);
    s_settings = *s;
    pthread_mutex_unlock(&s_lock);
    radar_ui_show(s);
    if (!s_live) {
        radar_ui_update(s_replay, s_replay_n);
    }
}

static void on_settings(void)
{
    settings_ui_show(&s_map, &s_settings, on_saved);
}

static int run_window(void)
{
    lv_init();
    lv_display_t *display = lv_sdl_window_create(WIDTH, HEIGHT); // also sets LVGL's tick
    lv_sdl_window_set_title(display, "TouchSky simulator");
    lv_sdl_mouse_create();

    radar_ui_create(&s_map, on_settings);
    radar_ui_show(&s_settings);
    if (s_live) {
        pthread_t thread;
        pthread_create(&thread, NULL, poll_thread, NULL);
    } else {
        radar_ui_update(s_replay, s_replay_n);
    }
    // Closing the window exits the process (LV_SDL_DIRECT_EXIT).
    for (;;) {
        pthread_mutex_lock(&s_lock);
        if (s_fresh_n == -2) {
            radar_ui_offline("Poll failed");
        } else if (s_fresh_n >= 0) {
            radar_ui_update(s_fresh, s_fresh_n);
        }
        s_fresh_n = -1;
        pthread_mutex_unlock(&s_lock);
        lv_timer_handler();
        usleep(5000);
    }
}
#endif

int main(int argc, char **argv)
{
    bool window = argc > 1 && strcmp(argv[1], "--window") == 0;
    if (window) {
        argc--;
        argv++;
    }
    if (argc != (window ? 6 : 7)) {
        fprintf(stderr,
                "usage: %s <map.bin> <adsb.json> <lat> <lon> <range_nm> <out_dir>\n"
                "       %s --window <map.bin> <adsb.json|live> <lat> <lon> <range_nm>\n",
                argv[0], argv[0]);
        return 2;
    }
    const char *out = window ? NULL : argv[6];
    settings_t settings = {.ssid = "HomeNetwork", .range_nm = atoi(argv[5])};
    if (!settings_parse_coord(argv[3], -90, 90, &settings.lat) ||
        !settings_parse_coord(argv[4], -180, 180, &settings.lon) ||
        !settings_complete(&settings)) {
        fprintf(stderr, "bad centre or range (range is 25, 50, 100 or 250)\n");
        return 2;
    }

    size_t map_len, json_len;
    char *blob = read_file(argv[1], &map_len);
    mapdata_t map;
    if (!mapdata_open(&map, blob, map_len)) {
        fprintf(stderr, "%s: not a valid map\n", argv[1]);
        return 1;
    }
    bool live = window && strcmp(argv[2], "live") == 0;
    static adsb_aircraft_t list[ADSB_MAX_AIRCRAFT];
    int n = 0;
    if (!live) {
        char *json = read_file(argv[2], &json_len);
        n = adsb_parse(json, settings.lat, settings.lon, list, ADSB_MAX_AIRCRAFT);
        if (n < 0) {
            fprintf(stderr, "%s: not an adsb.lol response\n", argv[2]);
            return 1;
        }
    }

    if (window) {
#ifdef SIM_SDL
        s_map = map;
        s_settings = settings;
        s_live = live;
        memcpy(s_replay, list, n * sizeof(*list));
        s_replay_n = n;
        return run_window();
#else
        fprintf(stderr, "built without SDL2: brew install sdl2, then rebuild\n");
        return 2;
#endif
    }

    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *display = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_buffers(display, s_frame, NULL, sizeof(s_frame), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);

    radar_ui_create(&map, nothing);
    radar_ui_show(&settings);
    radar_ui_update(list, n);
    render();
    save_bmp(out, "radar.bmp");

    settings_ui_show(&map, &settings, nothing_saved);
    render();
    save_bmp(out, "settings.bmp");
    return 0;
}
