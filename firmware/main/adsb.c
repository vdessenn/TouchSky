#include "adsb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "geo.h"

bool adsb_build_url(char *buf, size_t size, double lat, double lon, int range_nm)
{
    int n = snprintf(buf, size, "https://api.adsb.lol/v2/point/%.4f/%.4f/%d", lat, lon, range_nm);
    return n > 0 && (size_t)n < size;
}

// Copies src without surrounding spaces, truncated to size - 1 characters.
static void copy_trimmed(char *dst, size_t size, const char *src)
{
    while (*src == ' ') {
        src++;
    }
    size_t len = strlen(src);
    while (len > 0 && src[len - 1] == ' ') {
        len--;
    }
    if (len >= size) {
        len = size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int by_distance(const void *a, const void *b)
{
    float da = ((const adsb_aircraft_t *)a)->dist_nm, db = ((const adsb_aircraft_t *)b)->dist_nm;
    return (da > db) - (da < db);
}

int adsb_parse(const char *json, double center_lat, double center_lon,
               adsb_aircraft_t *out, int max)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *list = cJSON_GetObjectItem(root, "ac");
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return -1;
    }

    // Keep the max closest: fill the array, then replace the farthest entry
    // whenever a closer aircraft shows up.
    int n = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        const cJSON *lat = cJSON_GetObjectItem(item, "lat");
        const cJSON *lon = cJSON_GetObjectItem(item, "lon");
        if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
            continue;
        }
        adsb_aircraft_t ac = {.lat = lat->valuedouble, .lon = lon->valuedouble};
        ac.dist_nm = (float)geo_distance_nm(center_lat, center_lon, ac.lat, ac.lon);

        const cJSON *v = cJSON_GetObjectItem(item, "hex");
        if (cJSON_IsString(v)) {
            copy_trimmed(ac.hex, sizeof(ac.hex), v->valuestring);
        }
        v = cJSON_GetObjectItem(item, "flight");
        if (cJSON_IsString(v)) {
            copy_trimmed(ac.callsign, sizeof(ac.callsign), v->valuestring);
        }
        v = cJSON_GetObjectItem(item, "alt_baro");
        if (cJSON_IsNumber(v)) {
            ac.has_alt = true;
            ac.alt_ft = v->valueint;
        } else if (cJSON_IsString(v) && strcmp(v->valuestring, "ground") == 0) {
            ac.has_alt = true;
            ac.on_ground = true;
        }
        v = cJSON_GetObjectItem(item, "track");
        if (cJSON_IsNumber(v)) {
            ac.has_track = true;
            ac.track_deg = (float)v->valuedouble;
        }
        v = cJSON_GetObjectItem(item, "gs");
        if (cJSON_IsNumber(v)) {
            ac.gs_kt = (float)v->valuedouble;
        }
        v = cJSON_GetObjectItem(item, "seen_pos");
        if (cJSON_IsNumber(v)) {
            ac.seen_pos_s = (float)v->valuedouble;
        }

        if (n < max) {
            out[n++] = ac;
            continue;
        }
        int farthest = 0;
        for (int i = 1; i < n; i++) {
            if (out[i].dist_nm > out[farthest].dist_nm) {
                farthest = i;
            }
        }
        if (n > 0 && ac.dist_nm < out[farthest].dist_nm) {
            out[farthest] = ac;
        }
    }
    cJSON_Delete(root);
    qsort(out, n, sizeof(*out), by_distance);
    return n;
}

void adsb_extrapolate(const adsb_aircraft_t *ac, float elapsed_s, double *lat, double *lon)
{
    float age_s = fminf(fmaxf(ac->seen_pos_s + elapsed_s, 0), ADSB_MAX_EXTRAPOLATION_S);
    if (!ac->has_track || ac->gs_kt <= 0 || age_s == 0) {
        *lat = ac->lat;
        *lon = ac->lon;
        return;
    }
    geo_destination(ac->lat, ac->lon, ac->track_deg, ac->gs_kt * age_s / 3600, lat, lon);
}

const char *adsb_name(const adsb_aircraft_t *ac)
{
    return ac->callsign[0] ? ac->callsign : ac->hex;
}

void adsb_format_alt(const adsb_aircraft_t *ac, char *buf, size_t size)
{
    if (ac->on_ground) {
        snprintf(buf, size, "GND");
    } else if (!ac->has_alt) {
        snprintf(buf, size, "---");
    } else if (ac->alt_ft >= 10000) {
        snprintf(buf, size, "FL%03d", (ac->alt_ft + 50) / 100);
    } else {
        snprintf(buf, size, "%dft", ac->alt_ft);
    }
}
