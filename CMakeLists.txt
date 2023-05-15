idf_component_register(
  SRCS "src/atem.cpp" "src/atem_packet.cpp" "src/atem_command.cpp"
  INCLUDE_DIRS "include"
  REQUIRES "esp_event" "lwip" "log" "config_manager"
)
