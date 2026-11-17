#include "radar_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "geo.h"
#include "lvgl.h"

#define SCOPE 600        // scope square, pixels
#define RADIUS 285       // outer range ring
#define PANEL_W 424
#define LIST_ROWS 12     // closest aircraft listed in the panel
#define MAX_LABELS 40    // closest aircraft labelled on the scope
#define MAX_AIRPORT_LABELS 40
#define RUNWAYS_MAX_RANGE_NM 50 // runways and medium airports are noise beyond
#define ANIM_PERIOD_MS 500      // aircraft move under 1 px per step at 25 nm

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_RING RGB565(0, 90, 45)
#define C_COAST RGB565(80, 130, 150)
#define C_BORDER RGB565(70, 70, 95)
#define C_RUNWAY RGB565(230, 230, 230)
#define C_AIRPORT RGB565(210, 170, 0)

static const mapdata_t *s_map;
static geo_view_t s_view = {.cx = SCOPE / 2, .cy = SCOPE / 2, .radius_px = RADIUS};
static uint16_t *s_fb; // SCOPE x SCOPE, RGB565, static map layer

static lv_obj_t *s_screen, *s_marks, *s_planes;
static lv_obj_t *s_clock, *s_status, *s_count, *s_names, *s_alts, *s_dists;

static adsb_aircraft_t s_aircraft[ADSB_MAX_AIRCRAFT];
static int s_n;
static bool s_online;
static uint32_t s_received_ms; // lv_tick_get() when s_aircraft arrived

// Where each aircraft is drawn, extrapolated from its last report.
typedef struct {
    int16_t x, y; // relative to the scope
    bool in_range;
} plane_pos_t;
static plane_pos_t s_pos[ADSB_MAX_AIRCRAFT];

// ---- static layer: drawn straight into the frame buffer ----

static void put(int x, int y, uint16_t color)
{
    int dx = x - s_view.cx, dy = y - s_view.cy;
    if (dx * dx + dy * dy <= RADIUS * RADIUS) { // also keeps x, y inside the buffer
        s_fb[y * SCOPE + x] = color;
    }
}

static void line(int x0, int y0, int x1, int y1, uint16_t color)
{
    // Bresenham
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        put(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void ring(int radius, uint16_t color)
{
    // One pixel per step of arc length is enough to leave no gap.
    for (int i = 0, n = (int)(2 * M_PI * radius) + 1; i < n; i++) {
        double a = 2 * M_PI * i / n;
        put(s_view.cx + (int)lround(radius * cos(a)), s_view.cy + (int)lround(radius * sin(a)), color);
    }
}

// Draws a segment given in microdegrees when at least one end is in range.
// ponytail: a segment with both ends outside is skipped even if it crosses the
// disc; only visible as a rare gap at the rim with very long segments.
static void geo_line(int32_t lat0, int32_t lon0, int32_t lat1, int32_t lon1, uint16_t color)
{
    int x0, y0, x1, y1;
    bool in0 = geo_project(&s_view, lat0 / 1e6, lon0 / 1e6, &x0, &y0);
    bool in1 = geo_project(&s_view, lat1 / 1e6, lon1 / 1e6, &x1, &y1);
    if (in0 || in1) {
        line(x0, y0, x1, y1, color);
    }
}

static lv_obj_t *mark(const char *text, int x, int y, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(s_marks);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static void draw_map(void)
{
    memset(s_fb, 0, SCOPE * SCOPE * sizeof(*s_fb));
    lv_obj_clean(s_marks);

    // Microdegree box around the disc, to skip most of Europe cheaply.
    double dlat = s_view.range_nm / 60.0 * 1.1;
    double dlon = dlat / fmax(cos(s_view.center_lat * M_PI / 180), 0.05);
    int32_t lat_min = (int32_t)((s_view.center_lat - dlat) * 1e6), lat_max = (int32_t)((s_view.center_lat + dlat) * 1e6);
    int32_t lon_min = (int32_t)((s_view.center_lon - dlon) * 1e6), lon_max = (int32_t)((s_view.center_lon + dlon) * 1e6);

    for (uint32_t i = 0; i < s_map->n_lines; i++) {
        map_line_t l = mapdata_line(s_map, i);
        if (l.max_lat < lat_min || l.min_lat > lat_max || l.max_lon < lon_min || l.min_lon > lon_max) {
            continue;
        }
        uint16_t color = l.kind == MAP_LINE_COAST ? C_COAST : C_BORDER;
        map_point_t a = mapdata_point(s_map, l.first_point);
        for (uint32_t j = 1; j < l.n_points; j++) {
            map_point_t b = mapdata_point(s_map, l.first_point + j);
            geo_line(a.lat, a.lon, b.lat, b.lon, color);
            a = b;
        }
    }

    // Range rings at each quarter, with their distance, and the north mark.
    for (int q = 1; q <= 4; q++) {
        ring(RADIUS * q / 4, C_RING);
        char text[16];
        snprintf(text, sizeof(text), "%g", s_view.range_nm * q / 4);
        mark(text, s_view.cx + 4, s_view.cy - RADIUS * q / 4 + 2, lv_color_make(0, 150, 75));
    }
    line(s_view.cx - 6, s_view.cy, s_view.cx + 6, s_view.cy, C_RING);
    line(s_view.cx, s_view.cy - 6, s_view.cx, s_view.cy + 6, C_RING);
    mark("N", s_view.cx - 5, s_view.cy - RADIUS - 15, lv_color_make(0, 150, 75));

    bool close = s_view.range_nm <= RUNWAYS_MAX_RANGE_NM;
    if (close) {
        for (uint32_t i = 0; i < s_map->n_runways; i++) {
            map_runway_t r = mapdata_runway(s_map, i);
            if (r.lat1 >= lat_min && r.lat1 <= lat_max && r.lon1 >= lon_min && r.lon1 <= lon_max) {
                geo_line(r.lat1, r.lon1, r.lat2, r.lon2, C_RUNWAY);
            }
        }
    }
    int labels = 0;
    for (uint32_t i = 0; i < s_map->n_airports; i++) {
        map_airport_t a = mapdata_airport(s_map, i);
        int x, y;
        if (a.lat < lat_min || a.lat > lat_max || a.lon < lon_min || a.lon > lon_max ||
            (a.size == MAP_AIRPORT_MEDIUM && !close) ||
            !geo_project(&s_view, a.lat / 1e6, a.lon / 1e6, &x, &y)) {
            continue;
        }
        int half = a.size == MAP_AIRPORT_LARGE ? 2 : 1;
        for (int dy = -half; dy <= half; dy++) {
            for (int dx = -half; dx <= half; dx++) {
                put(x + dx, y + dy, C_AIRPORT);
            }
        }
        if (labels < MAX_AIRPORT_LABELS) {
            mark(a.icao, x + 5, y + 3, lv_color_make(210, 170, 0));
            labels++;
        }
    }
    lv_obj_invalidate(s_screen);
}

// ---- aircraft layer: redrawn by LVGL on top of the map ----

// Moves every aircraft to where it should be now. Returns true when one of
// them changed pixel, i.e. when the layer needs redrawing.
static bool place_planes(void)
{
    float elapsed_s = lv_tick_elaps(s_received_ms) / 1000.0f;
    bool moved = false;
    for (int i = 0; i < s_n; i++) {
        double lat, lon;
        int x, y;
        adsb_extrapolate(&s_aircraft[i], elapsed_s, &lat, &lon);
        bool in_range = geo_project(&s_view, lat, lon, &x, &y);
        plane_pos_t p = {(int16_t)x, (int16_t)y, in_range};
        if (p.x != s_pos[i].x || p.y != s_pos[i].y || p.in_range != s_pos[i].in_range) {
            s_pos[i] = p;
            moved = true;
        }
    }
    return moved;
}

static void tick_planes(lv_timer_t *timer)
{
    if (lv_screen_active() == s_screen && place_planes()) {
        lv_obj_invalidate(s_planes);
    }
}

static void draw_planes(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t origin;
    lv_obj_get_coords(lv_event_get_target(e), &origin);
    lv_color_t live = lv_color_make(60, 255, 130), stale = lv_color_make(110, 110, 110);

    int labels = 0;
    for (int i = 0; i < s_n; i++) {
        const adsb_aircraft_t *ac = &s_aircraft[i];
        if (!s_pos[i].in_range) {
            continue;
        }
        int x = s_pos[i].x + origin.x1, y = s_pos[i].y + origin.y1;
        lv_color_t color = !s_online || ac->on_ground ? stale : live;

        if (ac->has_track) {
            // Arrow pointing along the track: (0,-9) is the nose before rotation.
            static const int8_t shape[3][2] = {{0, -9}, {-6, 7}, {6, 7}};
            float t = ac->track_deg * (float)M_PI / 180, c = cosf(t), s = sinf(t);
            lv_draw_triangle_dsc_t tri;
            lv_draw_triangle_dsc_init(&tri);
            tri.color = color;
            for (int k = 0; k < 3; k++) {
                tri.p[k].x = x + (int)lroundf(shape[k][0] * c - shape[k][1] * s);
                tri.p[k].y = y + (int)lroundf(shape[k][0] * s + shape[k][1] * c);
            }
            lv_draw_triangle(layer, &tri);
        } else {
            lv_draw_rect_dsc_t dot;
            lv_draw_rect_dsc_init(&dot);
            dot.bg_color = color;
            lv_area_t area = {x - 3, y - 3, x + 3, y + 3};
            lv_draw_rect(layer, &dot, &area);
        }

        if (!ac->on_ground && labels < MAX_LABELS) {
            labels++;
            char text[24], alt[12];
            adsb_format_alt(ac, alt, sizeof(alt));
            snprintf(text, sizeof(text), "%s\n%s", adsb_name(ac), alt);
            lv_draw_label_dsc_t label;
            lv_draw_label_dsc_init(&label);
            label.text = text;
            label.text_local = 1; // drawn later: LVGL must copy the stack buffer
            label.font = &lv_font_montserrat_14;
            label.color = color;
            lv_area_t area = {x + 11, y - 8, x + 110, y + 28};
            lv_draw_label(layer, &label, &area);
        }
    }
}

static void refresh_list(void)
{
    char names[LIST_ROWS * 10] = "", alts[LIST_ROWS * 10] = "", dists[LIST_ROWS * 10] = "";
    size_t ln = 0, la = 0, ld = 0;
    for (int i = 0; i < s_n && i < LIST_ROWS; i++) {
        char alt[12];
        adsb_format_alt(&s_aircraft[i], alt, sizeof(alt));
        ln += snprintf(names + ln, sizeof(names) - ln, "%s\n", adsb_name(&s_aircraft[i]));
        la += snprintf(alts + la, sizeof(alts) - la, "%s\n", alt);
        ld += snprintf(dists + ld, sizeof(dists) - ld, "%.0f nm\n", s_aircraft[i].dist_nm);
    }
    lv_label_set_text(s_names, names);
    lv_label_set_text(s_alts, alts);
    lv_label_set_text(s_dists, dists);
    lv_label_set_text_fmt(s_count, "%d aircraft within %g nm", s_n, s_view.range_nm);
}

void radar_ui_update(const adsb_aircraft_t *list, int count)
{
    memcpy(s_aircraft, list, count * sizeof(*list));
    s_n = count;
    s_received_ms = lv_tick_get();
    place_planes();
    s_online = true;
    lv_label_set_text(s_status, "Live");
    lv_obj_set_style_text_color(s_status, lv_color_make(60, 255, 130), 0);
    refresh_list();
    lv_obj_invalidate(s_planes);
}

void radar_ui_offline(const char *reason)
{
    s_online = false;
    lv_label_set_text(s_status, reason);
    lv_obj_set_style_text_color(s_status, lv_color_make(255, 90, 60), 0);
    lv_obj_invalidate(s_planes);
}

void radar_ui_show(const settings_t *settings)
{
    s_view.center_lat = settings->lat;
    s_view.center_lon = settings->lon;
    s_view.range_nm = settings->range_nm;
    s_n = 0; // aircraft of the previous centre mean nothing here
    refresh_list();
    radar_ui_offline("Connecting...");
    draw_map();
    lv_screen_load(s_screen);
}

// ---- construction ----

static void tick_clock(lv_timer_t *timer)
{
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    if (utc.tm_year < 2024 - 1900) { // SNTP has not answered yet
        lv_label_set_text(s_clock, "--:--Z");
    } else {
        lv_label_set_text_fmt(s_clock, "%02d:%02dZ", utc.tm_hour, utc.tm_min);
    }
}

static void on_settings_clicked(lv_event_t *e)
{
    void (*on_settings)(void) = lv_event_get_user_data(e);
    on_settings();
}

static lv_obj_t *panel_label(lv_obj_t *parent, const lv_font_t *font, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

void radar_ui_create(const mapdata_t *map, void (*on_settings)(void))
{
    s_map = map;
    s_fb = heap_caps_aligned_alloc(64, SCOPE * SCOPE * sizeof(*s_fb), MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(s_fb ? ESP_OK : ESP_ERR_NO_MEM);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *canvas = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(canvas, s_fb, SCOPE, SCOPE, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, 0, 0);

    // Two bare, transparent layers over the map: text marks, then aircraft.
    lv_obj_t **layers[] = {&s_marks, &s_planes};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *layer = lv_obj_create(s_screen);
        lv_obj_remove_style_all(layer);
        lv_obj_set_size(layer, SCOPE, SCOPE);
        lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        *layers[i] = layer;
    }
    lv_obj_add_event_cb(s_planes, draw_planes, LV_EVENT_DRAW_MAIN, NULL);

    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, SCOPE, 0);
    lv_obj_set_size(panel, PANEL_W, SCOPE);
    lv_obj_set_style_bg_color(panel, lv_color_make(16, 20, 26), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_clock = panel_label(panel, &lv_font_montserrat_28, 20, 16);
    s_status = panel_label(panel, &lv_font_montserrat_20, 200, 22);
    s_count = panel_label(panel, &lv_font_montserrat_14, 20, 64);
    lv_obj_set_style_text_color(s_count, lv_color_make(150, 160, 170), 0);
    s_names = panel_label(panel, &lv_font_montserrat_20, 20, 96);
    s_alts = panel_label(panel, &lv_font_montserrat_20, 180, 96);
    s_dists = panel_label(panel, &lv_font_montserrat_20, 310, 96);

    lv_obj_t *button = lv_button_create(panel);
    lv_obj_set_size(button, 160, 50);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_add_event_cb(button, on_settings_clicked, LV_EVENT_CLICKED, on_settings);
    lv_obj_t *text = lv_label_create(button);
    lv_label_set_text(text, "Settings");
    lv_obj_center(text);

    lv_timer_create(tick_clock, 1000, NULL);
    lv_timer_create(tick_planes, ANIM_PERIOD_MS, NULL);
}
