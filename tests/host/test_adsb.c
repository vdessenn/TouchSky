#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "adsb.h"
#include "geo.h"

void setUp(void) {}
void tearDown(void) {}

#define CLAT 48.85
#define CLON 2.35

static adsb_aircraft_t out[ADSB_MAX_AIRCRAFT];

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    TEST_ASSERT_EQUAL(n, (long)fread(b, 1, n, f));
    b[n] = 0;
    fclose(f);
    return b;
}

static const adsb_aircraft_t *find_hex(int n, const char *hex)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(out[i].hex, hex)) return &out[i];
    return NULL;
}

// ---- URL ----
void test_url_exact(void)
{
    char buf[128];
    TEST_ASSERT_TRUE(adsb_build_url(buf, sizeof buf, 48.85, 2.35, 25));
    TEST_ASSERT_EQUAL_STRING("https://api.adsb.lol/v2/point/48.8500/2.3500/25", buf);
}

void test_url_negative(void)
{
    char buf[128];
    TEST_ASSERT_TRUE(adsb_build_url(buf, sizeof buf, -33.8688, -151.2093, 100));
    TEST_ASSERT_EQUAL_STRING("https://api.adsb.lol/v2/point/-33.8688/-151.2093/100", buf);
}

void test_url_small_buffer(void)
{
    const char *want = "https://api.adsb.lol/v2/point/48.8500/2.3500/25";
    size_t n = strlen(want);
    char buf[128];
    TEST_ASSERT_FALSE(adsb_build_url(buf, 0, 48.85, 2.35, 25));
    TEST_ASSERT_FALSE(adsb_build_url(buf, 10, 48.85, 2.35, 25));
    TEST_ASSERT_FALSE(adsb_build_url(buf, n, 48.85, 2.35, 25)); // no room for NUL
    TEST_ASSERT_TRUE(adsb_build_url(buf, n + 1, 48.85, 2.35, 25));
    TEST_ASSERT_EQUAL_STRING(want, buf);
}

// ---- parse, real fixture ----
void test_parse_fixture(void)
{
    char *json = slurp("fixtures/adsb_point.json");
    int n = adsb_parse(json, CLAT, CLON, out, ADSB_MAX_AIRCRAFT);
    free(json);
    TEST_ASSERT_EQUAL_INT(40, n); // all 40 entries have lat and lon
    for (int i = 0; i < n; i++) {
        if (i) TEST_ASSERT_TRUE(out[i - 1].dist_nm <= out[i].dist_nm);
        TEST_ASSERT_TRUE(out[i].dist_nm <= 26.0f);
        TEST_ASSERT_TRUE(out[i].dist_nm >= 0.0f);
        size_t l = strlen(out[i].callsign);
        if (l) {
            TEST_ASSERT_NOT_EQUAL(' ', out[i].callsign[0]);
            TEST_ASSERT_NOT_EQUAL(' ', out[i].callsign[l - 1]);
        }
        TEST_ASSERT_DOUBLE_WITHIN(0.1, geo_distance_nm(CLAT, CLON, out[i].lat, out[i].lon), out[i].dist_nm);
        if (out[i].on_ground) TEST_ASSERT_EQUAL_INT(0, out[i].alt_ft);
    }
}

void test_parse_fixture_known_aircraft(void)
{
    char *json = slurp("fixtures/adsb_point.json");
    int n = adsb_parse(json, CLAT, CLON, out, ADSB_MAX_AIRCRAFT);
    free(json);
    const adsb_aircraft_t *a = find_hex(n, "3848bd"); // "FJCGD   ", alt_baro 1200
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("FJCGD", a->callsign);
    TEST_ASSERT_TRUE(a->has_alt);
    TEST_ASSERT_FALSE(a->on_ground);
    TEST_ASSERT_EQUAL_INT(1200, a->alt_ft);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.087f, a->seen_pos_s);
    a = find_hex(n, "4cae98"); // "RYR994L ", alt_baro 35725
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("RYR994L", a->callsign);
    TEST_ASSERT_TRUE(a->has_alt);
    TEST_ASSERT_EQUAL_INT(35725, a->alt_ft);
    a = find_hex(n, "493297"); // on the ground, no flight field
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_TRUE(a->on_ground);
    TEST_ASSERT_EQUAL_INT(0, a->alt_ft);
    TEST_ASSERT_EQUAL_STRING("", a->callsign);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 48.951203, a->lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 2.435767, a->lon);
}

void test_parse_fixture_truncated_keeps_closest(void)
{
    char *json = slurp("fixtures/adsb_point.json");
    static adsb_aircraft_t all[ADSB_MAX_AIRCRAFT];
    int n = adsb_parse(json, CLAT, CLON, all, ADSB_MAX_AIRCRAFT);
    int m = adsb_parse(json, CLAT, CLON, out, 5);
    free(json);
    TEST_ASSERT_EQUAL_INT(40, n);
    TEST_ASSERT_EQUAL_INT(5, m);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_STRING(all[i].hex, out[i].hex);
        TEST_ASSERT_EQUAL_FLOAT(all[i].dist_nm, out[i].dist_nm);
    }
}

// ---- parse, inline JSON ----
void test_parse_missing_latlon_skipped(void)
{
    const char *j = "{\"ac\":["
        "{\"hex\":\"aaaaaa\",\"lat\":48.9},"
        "{\"hex\":\"bbbbbb\",\"lon\":2.4},"
        "{\"hex\":\"cccccc\"},"
        "{\"hex\":\"dddddd\",\"lat\":48.9,\"lon\":2.4}]}";
    TEST_ASSERT_EQUAL_INT(1, adsb_parse(j, CLAT, CLON, out, 10));
    TEST_ASSERT_EQUAL_STRING("dddddd", out[0].hex);
}

void test_parse_optional_fields_absent(void)
{
    const char *j = "{\"ac\":[{\"hex\":\"abc123\",\"lat\":48.9,\"lon\":2.4}]}";
    TEST_ASSERT_EQUAL_INT(1, adsb_parse(j, CLAT, CLON, out, 10));
    TEST_ASSERT_EQUAL_STRING("abc123", out[0].hex);
    TEST_ASSERT_EQUAL_STRING("", out[0].callsign);
    TEST_ASSERT_FALSE(out[0].has_track);
    TEST_ASSERT_FALSE(out[0].has_alt);
    TEST_ASSERT_FALSE(out[0].on_ground);
    TEST_ASSERT_EQUAL_INT(0, out[0].alt_ft);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out[0].gs_kt);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out[0].seen_pos_s);
}

void test_parse_all_fields(void)
{
    const char *j = "{\"ac\":[{\"hex\":\"abc123\",\"flight\":\"  AF123 \",\"lat\":48.9,"
                    "\"lon\":2.4,\"alt_baro\":36000,\"track\":123.5,\"gs\":450.5,\"seen_pos\":1.5}]}";
    TEST_ASSERT_EQUAL_INT(1, adsb_parse(j, CLAT, CLON, out, 10));
    TEST_ASSERT_EQUAL_STRING("AF123", out[0].callsign);
    TEST_ASSERT_TRUE(out[0].has_alt);
    TEST_ASSERT_FALSE(out[0].on_ground);
    TEST_ASSERT_EQUAL_INT(36000, out[0].alt_ft);
    TEST_ASSERT_TRUE(out[0].has_track);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.5f, out[0].track_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 450.5f, out[0].gs_kt);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, out[0].seen_pos_s);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 48.9, out[0].lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.4, out[0].lon);
}

void test_parse_ground_inline(void)
{
    const char *j = "{\"ac\":[{\"hex\":\"abc123\",\"lat\":48.9,\"lon\":2.4,\"alt_baro\":\"ground\"}]}";
    TEST_ASSERT_EQUAL_INT(1, adsb_parse(j, CLAT, CLON, out, 10));
    TEST_ASSERT_TRUE(out[0].on_ground);
    TEST_ASSERT_EQUAL_INT(0, out[0].alt_ft);
}

void test_parse_sorted_and_distance(void)
{
    const char *j = "{\"ac\":["
        "{\"hex\":\"far000\",\"lat\":49.2,\"lon\":2.35},"
        "{\"hex\":\"near00\",\"lat\":48.86,\"lon\":2.35},"
        "{\"hex\":\"mid000\",\"lat\":48.85,\"lon\":2.6}]}";
    TEST_ASSERT_EQUAL_INT(3, adsb_parse(j, CLAT, CLON, out, 10));
    TEST_ASSERT_EQUAL_STRING("near00", out[0].hex);
    TEST_ASSERT_EQUAL_STRING("mid000", out[1].hex);
    TEST_ASSERT_EQUAL_STRING("far000", out[2].hex);
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_DOUBLE_WITHIN(0.1, geo_distance_nm(CLAT, CLON, out[i].lat, out[i].lon), out[i].dist_nm);
}

void test_parse_truncation_keeps_closest(void)
{
    const char *j = "{\"ac\":["
        "{\"hex\":\"far000\",\"lat\":49.2,\"lon\":2.35},"
        "{\"hex\":\"near00\",\"lat\":48.86,\"lon\":2.35},"
        "{\"hex\":\"mid000\",\"lat\":48.85,\"lon\":2.6}]}";
    TEST_ASSERT_EQUAL_INT(2, adsb_parse(j, CLAT, CLON, out, 2));
    TEST_ASSERT_EQUAL_STRING("near00", out[0].hex);
    TEST_ASSERT_EQUAL_STRING("mid000", out[1].hex);
}

void test_parse_empty_ac(void)
{
    TEST_ASSERT_EQUAL_INT(0, adsb_parse("{\"ac\":[]}", CLAT, CLON, out, 10));
}

void test_parse_invalid(void)
{
    TEST_ASSERT_EQUAL_INT(-1, adsb_parse("not json", CLAT, CLON, out, 10));
    TEST_ASSERT_EQUAL_INT(-1, adsb_parse("{\"ac\":[", CLAT, CLON, out, 10));
    TEST_ASSERT_EQUAL_INT(-1, adsb_parse("", CLAT, CLON, out, 10));
}

void test_parse_no_ac(void)
{
    TEST_ASSERT_EQUAL_INT(-1, adsb_parse("{\"msg\":\"No error\",\"total\":0}", CLAT, CLON, out, 10));
    TEST_ASSERT_EQUAL_INT(-1, adsb_parse("{\"ac\":null}", CLAT, CLON, out, 10));
}

// ---- extrapolate ----
static adsb_aircraft_t moving(float track, float gs, float seen_pos)
{
    adsb_aircraft_t a;
    memset(&a, 0, sizeof a);
    a.lat = CLAT;
    a.lon = CLON;
    a.has_track = true;
    a.track_deg = track;
    a.gs_kt = gs;
    a.seen_pos_s = seen_pos;
    return a;
}

void test_extrapolate_east(void)
{
    adsb_aircraft_t a = moving(90, 360, 0); // 0.1 nm/s
    double lat, lon;
    adsb_extrapolate(&a, 10, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 1.0, geo_distance_nm(CLAT, CLON, lat, lon));
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, CLAT, lat);
    TEST_ASSERT_TRUE(lon > CLON);
}

void test_extrapolate_counts_position_age(void)
{
    adsb_aircraft_t a = moving(0, 360, 4); // position already 4 s old in the response
    double lat, lon;
    adsb_extrapolate(&a, 6, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 1.0, geo_distance_nm(CLAT, CLON, lat, lon));
    TEST_ASSERT_TRUE(lat > CLAT);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, CLON, lon);
}

void test_extrapolate_capped(void)
{
    adsb_aircraft_t a = moving(180, 360, 0);
    double lat, lon;
    adsb_extrapolate(&a, 3600, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 0.1 * ADSB_MAX_EXTRAPOLATION_S, geo_distance_nm(CLAT, CLON, lat, lon));
    TEST_ASSERT_TRUE(lat < CLAT);
}

static void check_stays_put(const adsb_aircraft_t *a, float elapsed_s)
{
    double lat, lon;
    adsb_extrapolate(a, elapsed_s, &lat, &lon);
    TEST_ASSERT_EQUAL_DOUBLE(a->lat, lat);
    TEST_ASSERT_EQUAL_DOUBLE(a->lon, lon);
}

void test_extrapolate_stays_put(void)
{
    adsb_aircraft_t a = moving(90, 360, 0);
    check_stays_put(&a, 0); // no time has passed
    a.has_track = false;    // direction unknown
    check_stays_put(&a, 10);
    a = moving(90, 0, 0);   // not moving
    check_stays_put(&a, 10);
}

// ---- name ----
void test_name(void)
{
    adsb_aircraft_t a;
    memset(&a, 0, sizeof a);
    strcpy(a.hex, "3848bd");
    TEST_ASSERT_EQUAL_STRING("3848bd", adsb_name(&a));
    strcpy(a.callsign, "FJCGD");
    TEST_ASSERT_EQUAL_STRING("FJCGD", adsb_name(&a));
}

// ---- format_alt ----
static void fmt(bool has_alt, bool ground, int alt, const char *want)
{
    adsb_aircraft_t a;
    char buf[16];
    memset(&a, 0, sizeof a);
    a.has_alt = has_alt;
    a.on_ground = ground;
    a.alt_ft = alt;
    adsb_format_alt(&a, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING(want, buf);
}

void test_alt_special(void)
{
    fmt(false, true, 0, "GND");
    fmt(false, false, 0, "---");
}

void test_alt_flight_level(void)
{
    fmt(true, false, 35000, "FL350");
    fmt(true, false, 10000, "FL100");
    fmt(true, false, 41000, "FL410");
    fmt(true, false, 35725, "FL357");
}

void test_alt_below_10000(void)
{
    fmt(true, false, 9975, "9975ft");
    fmt(true, false, 4500, "4500ft");
    fmt(true, false, 1200, "1200ft");
}

void test_alt_rounding(void)
{
    fmt(true, false, 34960, "FL350");
    fmt(true, false, 34940, "FL349");
}

void test_alt_small_buffer(void)
{
    adsb_aircraft_t a;
    memset(&a, 0, sizeof a);
    a.has_alt = true;
    a.alt_ft = 35000;
    for (size_t size = 1; size <= 6; size++) {
        char buf[12];
        memset(buf, 'X', sizeof buf);
        adsb_format_alt(&a, buf, size);
        TEST_ASSERT_TRUE(memchr(buf, 0, size) != NULL); // NUL inside the given size
        for (size_t i = size; i < sizeof buf; i++) TEST_ASSERT_EQUAL_CHAR('X', buf[i]); // no overflow
    }
    char buf[4] = {'X', 'X', 'X', 'X'};
    adsb_format_alt(&a, buf, 0);
    TEST_ASSERT_EQUAL_CHAR('X', buf[0]); // size 0: nothing written
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_url_exact);
    RUN_TEST(test_url_negative);
    RUN_TEST(test_url_small_buffer);
    RUN_TEST(test_parse_fixture);
    RUN_TEST(test_parse_fixture_known_aircraft);
    RUN_TEST(test_parse_fixture_truncated_keeps_closest);
    RUN_TEST(test_parse_missing_latlon_skipped);
    RUN_TEST(test_parse_optional_fields_absent);
    RUN_TEST(test_parse_all_fields);
    RUN_TEST(test_parse_ground_inline);
    RUN_TEST(test_parse_sorted_and_distance);
    RUN_TEST(test_parse_truncation_keeps_closest);
    RUN_TEST(test_parse_empty_ac);
    RUN_TEST(test_parse_invalid);
    RUN_TEST(test_parse_no_ac);
    RUN_TEST(test_extrapolate_east);
    RUN_TEST(test_extrapolate_counts_position_age);
    RUN_TEST(test_extrapolate_capped);
    RUN_TEST(test_extrapolate_stays_put);
    RUN_TEST(test_name);
    RUN_TEST(test_alt_special);
    RUN_TEST(test_alt_flight_level);
    RUN_TEST(test_alt_below_10000);
    RUN_TEST(test_alt_rounding);
    RUN_TEST(test_alt_small_buffer);
    return UNITY_END();
}
