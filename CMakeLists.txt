idf_component_register(
  SRCS "src/atem.cpp"
  INCLUDE_DIRS "include"
  REQUIRES "esp_event" "esp_timer" "lwip" "log" "helpers" "config_manager"
)
