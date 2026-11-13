cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Clock_CubeMX)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
option(CLOCK_RGB_TEST "Run the preserved RGB/workqueue hardware smoke test" OFF)
option(CLOCK_BUZZER_ACTIVE "Drive an active buzzer with GPIO instead of PWM" OFF)
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Middlewares/FreeRTOS/tasks.c
    Middlewares/FreeRTOS/list.c
    Middlewares/FreeRTOS/queue.c
    Middlewares/FreeRTOS/timers.c
    Middlewares/FreeRTOS/event_groups.c
    Middlewares/FreeRTOS/stream_buffer.c
    Middlewares/FreeRTOS/portable/heap_4.c
    Middlewares/FreeRTOS/portable/GCC/ARM_CM4F/port.c
    app/workqueue.c
    app/app.c
    app/wifi.c
    app/weather.c
    app/ui.c
    app/alarm/alarm_engine.c
    app/alarm/alarm_store.c
    app/alarm/alarm_menu.c
    app/alarm/alarm_page.c
    app/alarm/alarm_service.c
    app/web/clock_http.c
    app/web/clock_web.c
    app/web/web_page.c
    app/web/web_session.c
    app/onenet/onenet_service.c
    app/onenet/onenet_model.c
    app/onenet/onenet_ca.c
    BSP/Buzzer/buzzer.c
    BSP/Keys/board_keys.c
    BSP/BL24C512/bl24c512.c
    BSP/AHT20/aht20.c
    BSP/RTC/clock_rtc.c
    BSP/Console/clock_console.c
    BSP/ESP_AT/esp_at.c
    BSP/ESP_AT/esp_at_parse.c
    BSP/ST7789/st7789.c
)

# Only the files actually used by the legacy Keil clock, with their original
# GB2312 execution bytes. Newly authored HAL/application files stay UTF-8.
set(CLOCK_LEGACY_SOURCES
    app/page/welcome_page.c
    app/page/error_page.c
    app/page/wifi_page.c
    app/page/main_page.c
    app/page/image_error.c
    app/page/icon_duoyun.c
    app/font/font16_maple.c
    app/font/font24_maple.c
    app/font/font24_maple_bold.c
    app/font/font32_maple.c
    app/font/font42_maple.c
    app/font/font54_maple_medium.c
    app/font/font62_maple_bold.c
    app/font/font76_maple_bold.c
    app/image/image_star.c
    app/image/image_wifi.c
    app/image/icon_wifi.c
    app/image/icon_wenduji.c
    app/image/icon_qing.c
    app/image/icon_leizhengyu.c
    app/image/icon_yintian.c
    app/image/icon_yueliang.c
    app/image/icon_zhongxue.c
    app/image/icon_zhongyu.c
    app/image/icon_na.c
)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CLOCK_LEGACY_SOURCES})
set_source_files_properties(${CLOCK_LEGACY_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-finput-charset=GBK;-fexec-charset=GBK")

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Middlewares/FreeRTOS/include
    Middlewares/FreeRTOS/portable
    Middlewares/FreeRTOS/portable/GCC/ARM_CM4F
    app
    app/page
    app/alarm
    app/web
    app/onenet
    BSP/Buzzer
    BSP/Keys
    BSP/BL24C512
    BSP/AHT20
    BSP/RTC
    BSP/Console
    BSP/ESP_AT
    BSP/ST7789
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    CLOCK_RGB_TEST=$<BOOL:${CLOCK_RGB_TEST}>
    CLOCK_BUZZER_ACTIVE=$<BOOL:${CLOCK_BUZZER_ACTIVE}>
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/clock_config_local.h")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CLOCK_LOCAL_CONFIG=1)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/onenet_config_local.h")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CLOCK_ONENET_LOCAL_CONFIG=1)
endif()
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    VERBATIM)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
