# Host simulator: renders the firmware's real UI code with LVGL on a computer
# and saves screenshots. Run through ./run.sh.
cmake_minimum_required(VERSION 3.16)
project(touchsky_sim C)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH is not set: load the ESP-IDF environment (export.sh) first")
endif()
set(IDF "$ENV{IDF_PATH}")
set(FW "${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main")

# Same LVGL release as the firmware (firmware/main/idf_component.yml). Only the
# sources are fetched; they are compiled here with the simulator's lv_conf.h.
include(FetchContent)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(lvgl)

file(GLOB_RECURSE LVGL_SOURCES "${lvgl_SOURCE_DIR}/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC "${lvgl_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -w)

# Optional live window (run.sh --window). Without SDL2 only screenshots work.
find_program(SDL2_CONFIG sdl2-config)
if(SDL2_CONFIG)
    execute_process(COMMAND ${SDL2_CONFIG} --cflags OUTPUT_VARIABLE SDL_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${SDL2_CONFIG} --libs OUTPUT_VARIABLE SDL_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "-lSDL2main" "" SDL_LIBS "${SDL_LIBS}") # we have our own main()
    separate_arguments(SDL_CFLAGS NATIVE_COMMAND "${SDL_CFLAGS}")
    separate_arguments(SDL_LIBS NATIVE_COMMAND "${SDL_LIBS}")
    target_compile_definitions(lvgl PUBLIC SIM_SDL)
    target_compile_options(lvgl PUBLIC ${SDL_CFLAGS})
    target_link_libraries(lvgl PUBLIC ${SDL_LIBS})
endif()

add_executable(touchsky_sim
    main.c
    "${FW}/radar_ui.c" "${FW}/settings_ui.c"
    "${FW}/geo.c" "${FW}/adsb.c" "${FW}/mapdata.c" "${FW}/settings.c"
    "${IDF}/components/json/cJSON/cJSON.c")
target_include_directories(touchsky_sim PRIVATE
    shim "${FW}" "${IDF}/components/json/cJSON")
target_compile_options(touchsky_sim PRIVATE -Wall)
target_link_libraries(touchsky_sim lvgl m)
