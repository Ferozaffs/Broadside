// Time of day x weather -> everything the renderer needs to light the scene.
#pragma once
#include "raylib.h"
#include "raymath.h"

enum TimeOfDay : int { TOD_NOON = 0, TOD_SUNSET = 1, TOD_NIGHT = 2 };
enum Weather : int { WEATHER_CLEAR = 0, WEATHER_STORM = 1, WEATHER_FOG = 2 };
inline const char* TimeName(int t) { return t == TOD_SUNSET ? "Sunset" : t == TOD_NIGHT ? "Night" : "Noon"; }
inline const char* WeatherName(int w) { return w == WEATHER_STORM ? "Storm" : w == WEATHER_FOG ? "Fog" : "Clear"; }

struct Atmos {
    Vector3 sunDir{0, 1, 0};
    Vector3 sunColor{2.5f, 2.2f, 1.9f};  // HDR irradiance of the sun (or the moon at night)
    Vector3 zenith{0.09f, 0.2f, 0.42f}, horizon{0.55f, 0.6f, 0.66f};
    Vector3 fogColor{0.6f, 0.62f, 0.66f};
    float fogDensity = 0.00022f;  // above-water distance fog, per metre
    float haze = 0.15f;           // how much the sky itself fades into fog towards the horizon
    float cloud = 0.15f;          // overcast amount
    float night = 0.0f;           // stars + moon disc
    float exposure = 1.1f;
    float rain = 0.0f;
    float lightning = 0.0f;       // current flash (set per frame)
    float lanterns = 0.2f;        // lantern brightness / how much they matter
    Vector3 ambient{0.55f, 0.6f, 0.68f};  // sky light on particles (0..1)
    Vector3 smokeSun{1.0f, 0.9f, 0.8f};   // sun on particles (0..1)
    // the sea's own colour (the campaign's regions change it): light scattered in the body, through the crests, deep down
    Vector3 waterScatter{0.0065f, 0.041f, 0.057f}, waterPeak{0.012f, 0.11f, 0.085f}, waterDeep{0.005f, 0.036f, 0.064f};
    float snow = 0.0f, ash = 0.0f;        // flakes in the air (the Frozen North's snow, the Ember Reach's ash)
};

inline Vector3 V3Lerp(Vector3 a, Vector3 b, float t) { return Vector3Lerp(a, b, t); }

inline Atmos MakeAtmos(int tod, int weather, Vector2 windDir) {
    Atmos a;
    // the sun sits upwind so it lights the sea the ships are sailing into
    float az = atan2f(windDir.y, windDir.x) + 2.4f;
    auto dirAt = [&](float elev) {
        return Vector3Normalize({cosf(az) * cosf(elev), sinf(elev), sinf(az) * cosf(elev)});
    };
    float bright = 1.0f;  // how lit the grey of clouds and fog is
    switch (tod) {
        case TOD_SUNSET:
            a.sunDir = dirAt(0.07f);
            a.sunColor = {2.6f, 1.2f, 0.45f};
            a.zenith = {0.10f, 0.13f, 0.30f};
            a.horizon = {0.95f, 0.52f, 0.30f};
            a.fogColor = {0.72f, 0.48f, 0.36f};
            a.exposure = 1.25f;
            a.lanterns = 0.55f;
            a.ambient = {0.62f, 0.45f, 0.42f};
            a.smokeSun = {1.0f, 0.55f, 0.3f};
            bright = 0.55f;
            break;
        case TOD_NIGHT:
            a.sunDir = dirAt(0.6f);           // the moon
            a.sunColor = {0.012f, 0.017f, 0.03f};  // a sliver of moon: night is for lanterns, flashes and tracers
            a.zenith = {0.0005f, 0.001f, 0.003f};
            a.horizon = {0.002f, 0.0032f, 0.006f};
            a.fogColor = {0.003f, 0.0045f, 0.007f};
            a.night = 1.0f;
            a.exposure = 1.25f;
            a.lanterns = 1.0f;
            a.ambient = {0.01f, 0.013f, 0.022f};
            a.smokeSun = {0.025f, 0.03f, 0.045f};
            bright = 0.012f;
            break;
        default:  // noon
            a.sunDir = dirAt(0.95f);
            a.sunColor = {2.6f, 2.35f, 2.0f};
            a.zenith = {0.08f, 0.21f, 0.47f};
            a.horizon = {0.58f, 0.65f, 0.72f};
            a.fogColor = {0.62f, 0.66f, 0.70f};
            a.lanterns = 0.15f;
            break;
    }
    Vector3 grey = Vector3Scale({0.30f, 0.32f, 0.35f}, bright);
    switch (weather) {
        case WEATHER_STORM:
            a.cloud = 0.92f;
            a.sunColor = Vector3Scale(a.sunColor, tod == TOD_NIGHT ? 0.5f : 0.28f);
            a.zenith = V3Lerp(a.zenith, Vector3Scale(grey, 0.6f), 0.85f);
            a.horizon = V3Lerp(a.horizon, grey, 0.8f);
            a.fogColor = V3Lerp(a.fogColor, Vector3Scale(grey, 0.9f), 0.85f);
            a.fogDensity = 0.0009f;
            a.haze = 0.6f;
            a.rain = 1.0f;
            a.exposure *= 1.2f;
            a.lanterns = fmaxf(a.lanterns, 0.75f);
            a.ambient = Vector3Scale(a.ambient, 0.55f);
            a.smokeSun = Vector3Scale(a.smokeSun, 0.35f);
            break;
        case WEATHER_FOG:
            a.cloud = 0.7f;
            a.sunColor = Vector3Scale(a.sunColor, 0.45f);
            a.fogColor = V3Lerp(a.horizon, Vector3Scale({0.62f, 0.65f, 0.68f}, bright), 0.6f);
            a.horizon = a.fogColor;
            a.zenith = V3Lerp(a.zenith, a.fogColor, 0.6f);
            a.fogDensity = 0.0068f;  // ships fade out around 200-300 m, their lanterns a lot further
            a.haze = 1.0f;
            a.lanterns = fmaxf(a.lanterns, 0.85f);
            a.smokeSun = Vector3Scale(a.smokeSun, 0.5f);
            break;
        default:
            break;
    }
    return a;
}
