// Geographic helpers. Pure C, no ESP-IDF dependency (host-testable).
#pragma once

#include <stdbool.h>

// What the scope shows: a disc of radius_px pixels centred on (cx, cy),
// covering range_nm nautical miles around (center_lat, center_lon). North is up.
typedef struct {
    double center_lat, center_lon; // degrees, north and east positive
    double range_nm;               // distance represented by radius_px
    int cx, cy;                    // pixel position of the centre
    int radius_px;
} geo_view_t;

// Great-circle distance in nautical miles between two points given in degrees.
double geo_distance_nm(double lat1, double lon1, double lat2, double lon2);

// Point reached from (lat, lon) after dist_nm along a great circle leaving on
// bearing_deg (0 = north, clockwise). *lon2 is wrapped to [-180, 180).
void geo_destination(double lat, double lon, double bearing_deg, double dist_nm,
                     double *lat2, double *lon2);

// Projects (lat, lon) to screen pixels. Always writes *x and *y, even for
// points outside the disc (lines crossing the edge need them).
// Screen y grows downwards: a point north of the centre has y < cy.
// A point at range_nm from the centre lands radius_px pixels from (cx, cy).
// Returns true when the point is within range_nm of the centre.
bool geo_project(const geo_view_t *view, double lat, double lon, int *x, int *y);
