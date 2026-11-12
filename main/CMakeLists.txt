if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(PCCC_PRIVATE_CONFIG "${PROJECT_DIR}/private/provisioning.json" CACHE FILEPATH "Local private JSON (never ship)")
    execute_process(COMMAND ${PYTHON} "${PROJECT_DIR}/tools/generate_build_config.py" "${PROJECT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/private" "${PCCC_PRIVATE_CONFIG}" COMMAND_ERROR_IS_FATAL ANY)
    file(GLOB_RECURSE PCCC_IDENTITY_INPUTS CONFIGURE_DEPENDS
        "${PROJECT_DIR}/main/*.c" "${PROJECT_DIR}/main/include/*.h"
        "${PROJECT_DIR}/components/*.c" "${PROJECT_DIR}/components/*.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PCCC_IDENTITY_INPUTS} "${PROJECT_DIR}/sdkconfig" "${PROJECT_DIR}/dependencies.lock")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PCCC_PRIVATE_CONFIG}" "${PROJECT_DIR}/tools/generate_build_config.py")
endif()
idf_component_register(
    SRCS 
        "main.c"
        "src/flame_sensor.c"
        "src/mq2_sensor.c"
        "src/adc_shared.c"
        "src/gy906_sensor.c"
        "src/oled_display.c"
        "src/oled_ui.c"
        "src/ld2410_radar.c"
        "src/wifi_app.c"
        "src/tb_mqtt.c"
        "src/buzzer.c"
        "src/touch_btn.c"
        "src/sensor_pipeline.c"
        "src/risk_engine.c"
        "src/latch_store.c"
        "src/local_alarm_controller.c"
        "src/event_buffer.c"
        "src/cloud_codec.c"
        "src/mqtt_delivery.c"
        "src/critical_journal.c"
    INCLUDE_DIRS 
        "include"
        "${CMAKE_CURRENT_BINARY_DIR}/private"
    EMBED_TXTFILES
        "isrgrootx1.pem"
    REQUIRES
        rgb_status
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_uart
        esp_adc
        esp_timer
        log
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        espressif__mqtt
        nopnop2002__ssd1306
)
