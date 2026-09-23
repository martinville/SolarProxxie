set(web_assets "${CMAKE_CURRENT_BINARY_DIR}/assets")
execute_process(COMMAND "${PYTHON}" "${PROJECT_DIR}/tools/build_web_assets.py" --output "${web_assets}"
                RESULT_VARIABLE asset_result)
if(NOT asset_result EQUAL 0)
    message(FATAL_ERROR "Web asset compression failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_DIR}/web/index.html" "${PROJECT_DIR}/web/style.css" "${PROJECT_DIR}/web/app.js"
    "${PROJECT_DIR}/web/logo.svg")
idf_component_register(
    SRCS "app_main.c" "config/config.c" "config/validation.c" "config/migration.c"
         "security/security.c" "network/network.c" "network/dns.c" "network/reachability.c" "network/offline.c" "network/dongle_gui.c" "network/clients.c"
         "router/router.c" "protocol/inteless_parser.c" "protocol/telemetry_store.c" "protocol/stream.c" "protocol/dongle_identity.c" "protocol/cloud_emulator.c"
         "modbus/modbus_decoder.c" "modbus/register_map.c"
         "mqtt/mqtt.c" "mqtt/discovery.c" "protocol/pcap.c" "web/server.c" "ota/ota.c" "diagnostics/diagnostics.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server esp_http_client esp_https_ota app_update
             mqtt json mbedtls esp_timer esp_driver_gpio esp_system esp_hw_support lwip spi_flash
    EMBED_FILES "${web_assets}/index.html.gz" "${web_assets}/style.css.gz" "${web_assets}/app.js.gz"
                "${web_assets}/logo.svg.gz")
