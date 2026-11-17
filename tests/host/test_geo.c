#include <math.h>
#include "unity.h"
#include "geo.h"

void setUp(void) {}
void tearDown(void) {}

#define LAT0 48.85
#define LON0 2.35

static geo_view_t view(double range, int cx, int cy, int r)
{
    geo_view_t v = {LAT0, LON0, range, cx, cy, r};
    return v;
}

static double dlon(double range_nm_offset) // east offset in degrees for a distance in nm at LAT0
{
    return range_nm_offset / 60.0 / cos(LAT0 * M_PI / 180.0);
}

void test_distance_zero(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, geo_distance_nm(48.85, 2.35, 48.85, 2.35));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, geo_distance_nm(-33.0, -151.0, -33.0, -151.0));
}

void test_distance_one_degree_lat(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 60.0, geo_distance_nm(10.0, 20.0, 11.0, 20.0));
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 60.0, geo_distance_nm(-45.5, 5.0, -44.5, 5.0));
}

void test_distance_symmetric(void)
{
    double a = geo_distance_nm(49.0097, 2.5479, 51.47, -0.4543);
    double b = geo_distance_nm(51.47, -0.4543, 49.0097, 2.5479);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, a, b);
}

void test_distance_one_degree_lon_at_60(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(0.3, 30.0, geo_distance_nm(60.0, 10.0, 60.0, 11.0));
}

void test_distance_cdg_lhr(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(2.0, 188.0, geo_distance_nm(49.0097, 2.5479, 51.4700, -0.4543));
}

void test_project_center(void)
{
    geo_view_t v = view(25, 400, 240, 200);
    int x = -1, y = -1;
    TEST_ASSERT_TRUE(geo_project(&v, LAT0, LON0, &x, &y));
    TEST_ASSERT_EQUAL_INT(400, x);
    TEST_ASSERT_EQUAL_INT(240, y);
}

static void check_axes(double range, int cx, int cy, int r, int tol_ns, int tol_ew)
{
    geo_view_t v = view(range, cx, cy, r);
    int x, y;
    // north at range
    geo_project(&v, LAT0 + range / 60.0, LON0, &x, &y);
    TEST_ASSERT_INT_WITHIN(tol_ns, cy - r, y);
    TEST_ASSERT_INT_WITHIN(2, cx, x);
    // south
    geo_project(&v, LAT0 - range / 60.0, LON0, &x, &y);
    TEST_ASSERT_INT_WITHIN(tol_ns, cy + r, y);
    TEST_ASSERT_INT_WITHIN(2, cx, x);
    // east / west at half range
    TEST_ASSERT_TRUE(geo_project(&v, LAT0, LON0 + dlon(range / 2), &x, &y));
    TEST_ASSERT_INT_WITHIN(tol_ew, cx + r / 2, x);
    TEST_ASSERT_INT_WITHIN(tol_ew, cy, y);
    TEST_ASSERT_TRUE(geo_project(&v, LAT0, LON0 - dlon(range / 2), &x, &y));
    TEST_ASSERT_INT_WITHIN(tol_ew, cx - r / 2, x);
    TEST_ASSERT_INT_WITHIN(tol_ew, cy, y);
}

void test_project_axes_25nm(void) { check_axes(25, 400, 240, 200, 2, 3); }
void test_project_axes_25nm_offset(void) { check_axes(25, 123, 77, 150, 2, 3); }
void test_project_axes_250nm(void) { check_axes(250, 512, 300, 300, 6, 6); }

static void check_outside(double range, int r, int tol)
{
    geo_view_t v = view(range, 400, 240, r);
    int x = -9999, y = -9999;
    TEST_ASSERT_TRUE(geo_project(&v, LAT0 + range / 60.0 * 0.5, LON0, &x, &y));
    TEST_ASSERT_FALSE(geo_project(&v, LAT0 + range / 60.0 * 1.5, LON0, &x, &y));
    TEST_ASSERT_INT_WITHIN(tol + 2, 240 - (int)(1.5 * r), y); // still written
    TEST_ASSERT_FALSE(geo_project(&v, LAT0, LON0 + dlon(range * 1.5), &x, &y));
    TEST_ASSERT_FALSE(geo_project(&v, LAT0 - range / 60.0 * 1.5, LON0, &x, &y));
    x = y = -9999;
    TEST_ASSERT_FALSE(geo_project(&v, LAT0 + range / 60.0 * 2, LON0, &x, &y));
    TEST_ASSERT_INT_WITHIN(tol, 240 - 2 * r, y);
    TEST_ASSERT_INT_WITHIN(tol, 400, x);
}

void test_project_outside_25nm(void) { check_outside(25, 200, 4); }
void test_project_outside_250nm(void) { check_outside(250, 300, 6); }

void test_project_north_is_up_east_is_right(void)
{
    geo_view_t v = view(50, 100, 100, 100);
    int x, y;
    geo_project(&v, LAT0 + 0.1, LON0 + 0.1, &x, &y);
    TEST_ASSERT_TRUE(y < 100);
    TEST_ASSERT_TRUE(x > 100);
    geo_project(&v, LAT0 - 0.1, LON0 - 0.1, &x, &y);
    TEST_ASSERT_TRUE(y > 100);
    TEST_ASSERT_TRUE(x < 100);
}

void test_destination_round_trip(void)
{
    for (int b = 0; b < 360; b += 45) {
        double lat, lon;
        geo_destination(LAT0, LON0, b, 12.5, &lat, &lon);
        TEST_ASSERT_DOUBLE_WITHIN(1e-6, 12.5, geo_distance_nm(LAT0, LON0, lat, lon));
    }
}

void test_destination_directions(void)
{
    double lat, lon;
    geo_destination(LAT0, LON0, 0, 30, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, LAT0 + 0.5, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, LON0, lon);
    geo_destination(LAT0, LON0, 180, 30, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, LAT0 - 0.5, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, LON0, lon);
    geo_destination(LAT0, LON0, 90, 30, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, LAT0, lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, LON0 + dlon(30), lon);
    geo_destination(LAT0, LON0, 270, 30, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, LAT0, lat);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, LON0 - dlon(30), lon);
}

void test_destination_zero_distance(void)
{
    double lat, lon;
    geo_destination(LAT0, LON0, 123, 0, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, LAT0, lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, LON0, lon);
}

void test_destination_wraps_longitude(void)
{
    double lat, lon;
    geo_destination(0, 179.9, 90, 30, &lat, &lon); // 30 nm is about 0.5 degree at the equator
    TEST_ASSERT_DOUBLE_WITHIN(0.01, -179.6, lon);
    geo_destination(0, -179.9, 270, 30, &lat, &lon);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 179.6, lon);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_destination_round_trip);
    RUN_TEST(test_destination_directions);
    RUN_TEST(test_destination_zero_distance);
    RUN_TEST(test_destination_wraps_longitude);
    RUN_TEST(test_distance_zero);
    RUN_TEST(test_distance_one_degree_lat);
    RUN_TEST(test_distance_symmetric);
    RUN_TEST(test_distance_one_degree_lon_at_60);
    RUN_TEST(test_distance_cdg_lhr);
    RUN_TEST(test_project_center);
    RUN_TEST(test_project_axes_25nm);
    RUN_TEST(test_project_axes_25nm_offset);
    RUN_TEST(test_project_axes_250nm);
    RUN_TEST(test_project_outside_25nm);
    RUN_TEST(test_project_outside_250nm);
    RUN_TEST(test_project_north_is_up_east_is_right);
    return UNITY_END();
}
