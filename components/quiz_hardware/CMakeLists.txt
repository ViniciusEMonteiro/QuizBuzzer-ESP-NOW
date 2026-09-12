idf_component_register(
    SRCS "../../src/espnow_manager.cpp" "../../src/hardware.cpp" "../../src/debug.cpp"
    INCLUDE_DIRS "../../include"
    REQUIRES quiz_core esp_wifi esp_event esp_netif nvs_flash esp_driver_gpio
             esp_timer esp_system esp_driver_uart)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
