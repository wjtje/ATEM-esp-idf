#include <argtable3/argtable3.h>

#include "atem.h"

namespace atem {

// atem send [-v] CMD DATA
static struct {
  struct arg_rex *send;
  struct arg_lit *verbose;
  struct arg_str *cmd;
  struct arg_str *data;
  struct arg_end *end;
} atem_args_send;

static int atem_cmd_send(const char *cmd, const char *data, bool verbose) {
  if (strlen(cmd) != 4) return 1;

  size_t data_len = (strlen(data) / 2);
  auto atem_cmd = new AtemCommand(cmd, 8 + data_len);
  const char *pos = data;

  // Parse hexstring into buffer
  for (size_t i = 0; i < data_len; i++) {
    sscanf(pos, "%2hhx", &atem_cmd->GetData<char *>()[i]);
    pos += 2;
  }

  if (verbose)
    ESP_LOG_BUFFER_HEXDUMP("Sending data", atem_cmd->GetRawData(),
                           atem_cmd->GetLength(), ESP_LOG_INFO);

  Atem::GetInstance()->SendCommands({atem_cmd});
  return 0;
}

// atem info
static struct {
  struct arg_rex *info;
  struct arg_end *end;
} atem_args_info;

static int atem_cmd_info() {
  Atem *atem = Atem::GetInstance();
  auto top_ = atem->GetTopology();

  if (atem->Connected() && atem->GetProductId() != nullptr)
    printf(
        "Connected ATEM:\nModel:\t\t\t%s\nProtocol "
        "version:\t%u.%u\nTopology:\n\tME:\t\t%u\n\tSources:\t%u\n\tDSK:\t\t%"
        "u\n\tAUX:\t\t%u\n\tMixminus:\t%u\n\tMediaplayers:\t%u\n\tRS485:\t\t%"
        "u\n\tHyperdecks:\t%u\n\tUSK:\t\t%u\n\tStingers:\t%u\n\tSupersources:"
        "\t%u\n",
        atem->GetProductId(), atem->GetProtocolVersion().major,
        atem->GetProtocolVersion().minor, top_.me, top_.sources, top_.dsk,
        top_.aux, top_.mixminus_outputs, top_.mediaplayers, top_.rs485,
        top_.hyperdecks, top_.usk, top_.stingers, top_.supersources);
  else
    printf("No ATEM is connected\n");

  return 0;
}

// atem [-h] {COMMAND}
static struct {
  struct arg_lit *help;
  struct arg_str *command;
  struct arg_end *end;
} atem_args;

static int atem_cmd(int argc, char **argv) {
  // Check other commands
  if (!arg_parse(argc, argv, (void **)&atem_args_send)) {
    return atem_cmd_send(atem_args_send.cmd->sval[0],
                         atem_args_send.data->sval[0],
                         atem_args_send.verbose->count > 0);
  } else if (!arg_parse(argc, argv, (void **)&atem_args_info)) {
    return atem_cmd_info();
  }

  // Default command
  int nerrors = arg_parse(argc, argv, (void **)&atem_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, atem_args.end, argv[0]);
    return 1;
  }

  int exitcode = 0;

  // Help
  if (atem_args.help->count > 0) {
    if (atem_args.command->count > 0) {
      if (!strcmp(atem_args.command->sval[0], "send")) {
        fputs("Usage: atem", stdout);
        arg_print_syntax(stdout, (void **)&atem_args_send, "\n");
        arg_print_glossary(stdout, (void **)&atem_args_send,
                           "         %-20s %s\n");
      } else if (!strcmp(atem_args.command->sval[0], "info")) {
        fputs("Usage: atem", stdout);
        arg_print_syntax(stdout, (void **)&atem_args_info, "\n");
        arg_print_glossary(stdout, (void **)&atem_args_info,
                           "         %-20s %s\n");
      } else {
        fputs("Command not found", stdout);
      }
    } else {
      // Give overview
      fputs("Usage: atem", stdout);
      arg_print_syntax(stdout, (void **)&atem_args, "\n");

      fputs("       atem", stdout);
      arg_print_syntax(stdout, (void **)&atem_args_send, "\n");

      fputs("       atem", stdout);
      arg_print_syntax(stdout, (void **)&atem_args_info, "\n");
    }

    exitcode = 0;
    goto exit;
  }

exit:
  return exitcode;
}

esp_err_t Atem::repl_() {
  // Default command
  atem_args.help =
      arg_lit0("h", "help",
               "Displays a help section containing all posible combinations");
  atem_args.command = arg_str0(NULL, NULL, "COMMAND", NULL);
  atem_args.end = arg_end(2);

  // Send command
  atem_args_send.send = arg_rex1(NULL, NULL, "send", NULL, 0, NULL);
  atem_args_send.verbose = arg_lit0("v", "verbose", "verbose messages");
  atem_args_send.cmd = arg_str1(NULL, NULL, "CMD", "");
  atem_args_send.data =
      arg_str1(NULL, NULL, "DATA", "Data encoded in a hex string");
  atem_args_send.end = arg_end(4);

  // info command
  atem_args_info.info = arg_rex1(NULL, NULL, "info", NULL, 0, NULL);
  atem_args_info.end = arg_end(1);

  // Check if allocated
  if (arg_nullcheck((void **)&atem_args) ||
      arg_nullcheck((void **)&atem_args_send) ||
      arg_nullcheck((void **)&atem_args_info)) {
    ESP_LOGE("ATEM REPL", "Failed to allocate");
    return ESP_ERR_NO_MEM;
  }

  const esp_console_cmd_t cmd = {
      .command = "atem",
      .help = "",
      .hint = NULL,
      .func = &atem_cmd,
      .argtable = &atem_args,
  };

  return esp_console_cmd_register(&cmd);
}

}  // namespace atem
