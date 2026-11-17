// LVGL configuration for the host simulator. Anything not set here keeps
// LVGL's default; fonts and colour depth match firmware/sdkconfig.defaults.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1

#ifdef SIM_SDL // set by CMakeLists.txt when SDL2 is installed: sim/run.sh --window
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL.h> // sdl2-config puts the SDL2 folder itself on the include path
#endif

#endif
