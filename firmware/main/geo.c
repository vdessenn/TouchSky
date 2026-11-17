#include "geo.h"

#include <math.h>

#define DEG_TO_RAD (M_PI / 180.0)
#define EARTH_RADIUS_NM 3440.065

double geo_distance_nm(double lat1, double lon1, double lat2, double lon2)
{
    // Haversine
    double p1 = lat1 * DEG_TO_RAD, p2 = lat2 * DEG_TO_RAD;
    double sp = sin((p2 - p1) / 2), sl = sin((lon2 - lon1) * DEG_TO_RAD / 2);
    double a = sp * sp + cos(p1) * cos(p2) * sl * sl;
    return 2 * EARTH_RADIUS_NM * asin(sqrt(a));
}

void geo_destination(double lat, double lon, double bearing_deg, double dist_nm,
                     double *lat2, double *lon2)
{
    double p1 = lat * DEG_TO_RAD, b = bearing_deg * DEG_TO_RAD, d = dist_nm / EARTH_RADIUS_NM;
    double p2 = asin(sin(p1) * cos(d) + cos(p1) * sin(d) * cos(b));
    double dl = atan2(sin(b) * sin(d) * cos(p1), cos(d) - sin(p1) * sin(p2));
    *lat2 = p2 / DEG_TO_RAD;
    *lon2 = fmod(lon + dl / DEG_TO_RAD + 540.0, 360.0) - 180.0;
}

bool geo_project(const geo_view_t *view, double lat, double lon, int *x, int *y)
{
    // Azimuthal equidistant: true distance and initial bearing from the centre,
    // so range rings are exact at any range.
    double p1 = view->center_lat * DEG_TO_RAD, p2 = lat * DEG_TO_RAD;
    double dl = (lon - view->center_lon) * DEG_TO_RAD;
    double dist = geo_distance_nm(view->center_lat, view->center_lon, lat, lon);
    double bearing = atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl));
    double r = dist * view->radius_px / view->range_nm;
    *x = view->cx + (int)lround(r * sin(bearing));
    *y = view->cy - (int)lround(r * cos(bearing));
    return dist <= view->range_nm;
}
