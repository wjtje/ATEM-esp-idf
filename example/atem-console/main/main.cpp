#include <argtable3/argtable3.h>
#include <atem.h>
#include <esp_console.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "wifi.h"

static const char* TAG{"Main"};

static atem::Atem* _atem = nullptr;

static void atem_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data) {
  ESP_LOGI(TAG, "Got ATEM event with id: %lu", event_id);

  // Example usage
  // it's important to take the semaphore (e.g. lock the state)
  // before requesting any state from the atem object.
  // You can compare the event_id to ATEM_EVENT_* to see what data has been
  // changed
  if (xSemaphoreTake(_atem->GetStateMutex(), pdMS_TO_TICKS(100))) {
    atem::Source preview;
    if (_atem->GetPreviewInput(&preview, 0)) {
      // If GetPreviewInput returns true, that means the preview variable now
      // has the preview source
      // There are multiple reasons the command can fail
      // - The ME you are trying to read doesn't exsist
      // - You didn't lock the state
      // - The ATEM isn't connected
    }

    // Do not take the state to long.
    // Try todo al little as possible between the Take an Give functions
    xSemaphoreGive(_atem->GetStateMutex());
  }
}

// MARK: atem connect [address]
static struct {
  struct arg_rex* connect;
  struct arg_str* address;
  struct arg_end* end;
} atem_connect_args;

static int atem_connect() {
  if (atem_connect_args.connect->count <= 0) return 1;

  if (_atem != nullptr) delete _atem;
  _atem = new atem::Atem(atem_connect_args.address->sval[0]);

  return (_atem != nullptr) ? 0 : 1;
}

// MARK: atem preview [--me] [source]
static struct {
  struct arg_rex* preview;
  struct arg_int* me;
  struct arg_int* source;
  struct arg_end* end;
} atem_preview_args;

static int atem_preview() {
  int me = 0;
  if (atem_preview_args.me->count > 0) {
    me = *atem_preview_args.me->ival;
  }

  if (atem_preview_args.source->count > 0) {
    // Set preview source
    _atem->SendCommands({
        new atem::cmd::PreviewInput(
            (atem::Source)(*atem_preview_args.source->ival), (uint8_t)me),
    });
  } else {
    // Get preview source
    atem::Source source;

    if (xSemaphoreTake(_atem->GetStateMutex(), pdMS_TO_TICKS(50))) {
      if (_atem->GetPreviewInput(&source, (uint8_t)me)) {
        printf("Current preview source for me: %i is %u\n", me,
               uint16_t(source));
      } else {
        printf("State not available\n");
      }
      xSemaphoreGive(_atem->GetStateMutex());
    } else {
      printf("Failed to lock the state\n");
      return 2;
    }
  }

  return 0;
}

// MARK: atem [-h|help]
static struct {
  struct arg_lit* help;
  struct arg_str* command;
  struct arg_end* end;
} atem_args;

static int atem_cmd(int argc, char** argv) {
  if (!arg_parse(argc, argv, (void**)&atem_connect_args)) {
    return atem_connect();
  } else if (!arg_parse(argc, argv, (void**)&atem_preview_args)) {
    return atem_preview();
  }

  // Default command
  if (arg_parse(argc, argv, (void**)&atem_args)) {
    arg_print_errors(stderr, atem_args.end, argv[0]);
    return 1;
  }

  if (atem_args.help->count < 0) return 0;  // No help command
  if (atem_args.command->count > 0) {
    if (!strcmp(atem_args.command->sval[0], "connect")) {
      fputs("Usage: atem", stdout);
      arg_print_syntax(stdout, (void**)&atem_connect_args, "\n");
      arg_print_glossary(stdout, (void**)&atem_connect_args.address,
                         "         %-20s %s\n");
    } else if (!strcmp(atem_args.command->sval[0], "preview")) {
      fputs("Usage: atem", stdout);
      arg_print_syntax(stdout, (void**)&atem_preview_args, "\n");
      arg_print_glossary(stdout, (void**)&atem_preview_args.me,
                         "         %-20s %s\n");
    } else {
      fputs("Command not found", stdout);
    }
  } else {
    // Overview
    fputs("Usage: atem", stdout);
    arg_print_syntax(stdout, (void**)&atem_args, "\n");

    fputs("       atem", stdout);
    arg_print_syntax(stdout, (void**)&atem_connect_args, "\n");

    fputs("       atem", stdout);
    arg_print_syntax(stdout, (void**)&atem_preview_args, "\n");
  }

  return 0;
}

extern "C" void app_main(void) {
  // Only show error messages by default
  esp_log_level_set("*", ESP_LOG_ERROR);
  esp_log_level_set(TAG, ESP_LOG_INFO);
  esp_log_level_set("wifi", ESP_LOG_INFO);

  // Setup NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  } else {
    ESP_ERROR_CHECK(ret);
  }

  // Start background proccesses
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_netif_init());

  wifi_init();

  // Set callback for any ATEM data
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      atem::ATEM_EVENT, ESP_EVENT_ANY_ID, &atem_handler, NULL, NULL));

  // Init other arg tables
  atem_connect_args.connect = arg_rex1(NULL, NULL, "connect", NULL, 0,
                                       "Creates a new connection to an ATEM");
  atem_connect_args.address = arg_str1(NULL, NULL, "<address>",
                                       "The address of the ATEM to connect to");
  atem_connect_args.end = arg_end(2);

  atem_preview_args.preview = arg_rex1(NULL, NULL, "preview", NULL, 0,
                                       "Gets or sets the preview source");
  atem_preview_args.me =
      arg_int0(NULL, "me", "me", "Which ME to control, defaults to 0 (ME 1)");
  atem_preview_args.source = arg_int0(
      NULL, NULL, "source",
      "Which preview source to set it, if empty it will return the current");
  atem_preview_args.end = arg_end(3);

  // Register ATEM cmd
  atem_args.help = arg_lit0(
      "h", "help", "Displays a help section containing all possible commands");
  atem_args.command = arg_str0(NULL, NULL, "command", NULL);
  atem_args.end = arg_end(2);

  const esp_console_cmd_t atem_c_cmd = {
      .command = "atem",
      .help = "",
      .hint = NULL,
      .func = &atem_cmd,
      .argtable = &atem_args,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&atem_c_cmd));

  // Start console
  esp_console_dev_uart_config_t console_uart =
      ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  esp_console_repl_config_t console_repl = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  esp_console_repl_t* repl;

  ESP_ERROR_CHECK(
      esp_console_new_repl_uart(&console_uart, &console_repl, &repl));
  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
