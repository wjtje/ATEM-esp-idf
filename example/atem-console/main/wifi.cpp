
#include <FreeRTOSConfig.h>
#include <argtable3/argtable3.h>
#include <esp_console.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <portmacro.h>
#include <sdkconfig.h>
#include <string.h>

static const char* TAG{"wifi"};

esp_netif_t* netif_sta = NULL;
EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;
static bool reconnect = true;

static void got_ip_handler(void* arg, esp_event_base_t event_base,
                           int32_t event_id, void* event_data) {
  xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
  xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

  ESP_LOGI(TAG, "Connected to WIFI!");
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  if (reconnect) {
    ESP_LOGI(TAG, "sta disconnect, reconnect...");
    esp_wifi_connect();
  } else {
    ESP_LOGI(TAG, "sta disconnect");
  }
  xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
  xEventGroupSetBits(wifi_event_group, DISCONNECTED_BIT);
}

static struct {
  struct arg_str* ssid;
  struct arg_str* password;
  struct arg_end* end;
} wifi_args;

static int wifi_cmd(int argc, char** argv) {
  if (arg_parse(argc, argv, (void**)&wifi_args)) {
    arg_print_errors(stderr, wifi_args.end, argv[0]);
    return 1;
  }

  ESP_LOGI(TAG, "wifi connecting to '%s'", wifi_args.ssid->sval[0]);
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

  int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);

  wifi_config_t wifi_config = {0};

  strlcpy((char*)wifi_config.sta.ssid, wifi_args.ssid->sval[0],
          sizeof(wifi_config.sta.ssid));
  strlcpy((char*)wifi_config.sta.password, wifi_args.password->sval[0],
          sizeof(wifi_config.sta.password));

  if (bits & CONNECTED_BIT) {
    reconnect = false;
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    xEventGroupWaitBits(wifi_event_group, DISCONNECTED_BIT, 0, 1,
                        portTICK_PERIOD_MS);
  }

  reconnect = true;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  esp_wifi_connect();

  xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1,
                      5000 / portTICK_PERIOD_MS);

  return 0;
}

void wifi_init() {
  static bool initialized = false;
  if (initialized) return;

  wifi_event_group = xEventGroupCreate();

  netif_sta = esp_netif_create_default_wifi_sta();
  assert(netif_sta);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, NULL,
      NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_handler, NULL, NULL));

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_start());

  initialized = true;

  // Register WIFI cmd
  wifi_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
  wifi_args.password = arg_str1(NULL, NULL, "<pass>", "password of AP");
  wifi_args.end = arg_end(2);

  const esp_console_cmd_t wifi_c_cmd = {
      .command = "wifi",
      .help = "join specified AP",
      .hint = NULL,
      .func = &wifi_cmd,
      .argtable = &wifi_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_c_cmd));
}