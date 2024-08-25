#include <atem.h>
#include <esp_console.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include <stdio.h>

static const char* TAG{"Main"};

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    ESP_LOGI(TAG, "retry to connect to the AP");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
  }
}

void wifi_init() {
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &event_handler, NULL));

  wifi_config_t wifi_config{
      .sta =
          {
              .ssid = CONFIG_WIFI_SSID,
              .password = CONFIG_WIFI_PSK,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  } else {
    ESP_ERROR_CHECK(ret);
  }

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_netif_init());
  wifi_init();

  // Create connection with ATEM
  atem::Atem* _atem = new atem::Atem(CONFIG_ATEM_IP);

  // Switch between sources ME 1
  atem::Source preview_source;
  for (;;) {
    if (!_atem->Connected()) goto wait;
    if (xSemaphoreTake(_atem->GetStateMutex(), pdMS_TO_TICKS(250)) != pdTRUE)
      goto wait;

    // Get the current preview source
    if (!_atem->GetPreviewInput(&preview_source, 0)) {
      ESP_LOGE(TAG, "Failed to get current preview source");
      goto next;
    }

    // Check if the next source is valid for this ATEM
    preview_source = atem::Source(int(preview_source) + 1);
    if (!_atem->GetInputProperties().contains(preview_source)) {
      preview_source = atem::Source::BLACK;
    }

    // Change the preview input
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        _atem->SendCommands({new atem::cmd::PreviewInput(preview_source, 0)}));

  next:
    xSemaphoreGive(_atem->GetStateMutex());
  wait:
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
