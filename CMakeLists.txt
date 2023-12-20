idf_component_register(
  SRCS "src/atem.cpp" "src/atem_packet.cpp" "src/atem_command.cpp" "src/atem_state.cpp"
  INCLUDE_DIRS "include"
  REQUIRES "esp_event" "lwip" "log" "console" "heap"
)
