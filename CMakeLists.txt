idf_component_register(
  SRCS "src/atem.cpp" "src/atem_cmd.cpp"
  INCLUDE_DIRS "include"
  REQUIRES "helpers" "esp_event" "esp_timer" "wcaf_log" "lwip" "config_manager" "esp_netif"
)
