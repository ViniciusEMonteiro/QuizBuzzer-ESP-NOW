idf_component_register(
    SRCS "../../src/protocol.cpp" "../../src/device_config.cpp"
         "../../src/master_controller.cpp" "../../src/slave_controller.cpp"
         "../../src/button_manager.cpp" "../../src/led_manager.cpp"
    INCLUDE_DIRS "../../include")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
