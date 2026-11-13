#include "NativeHostedPlugin.h"
#include <ysfx.h>
namespace cydaw::plugins {
// The public Core provider returns a Status on compilation failure. Retain
// detailed script-editor diagnostics with its same bundled EEL/ysfx runtime.
base::Text nativeScriptDiagnostics(const base::File &file) {
  base::Text messages;
  if (file.getSize() > 16 * 1024 * 1024)
    return "JSFX source exceeds 16 MiB";
  auto *config = ysfx_config_new();
  if (!config)
    return {};
  struct Config {
    ysfx_config_t *value;
    ~Config() { ysfx_config_free(value); }
  } configuration{config};
  ysfx_set_user_data(config, reinterpret_cast<intptr_t>(&messages));
  ysfx_set_log_reporter(
      config, [](intptr_t context, ysfx_log_level level, const char *message) {
        auto &text = *reinterpret_cast<base::Text *>(context);
        if (text.getNumBytesAsUTF8() < 65536)
          text += base::Text(ysfx_log_level_string(level)) + ": " +
                  base::Text::fromUTF8(message) + "\n";
      });
  ysfx_guess_file_roots(config, file.getFullPathName().toRawUTF8());
  ysfx_register_builtin_audio_formats(config);
  auto *fx = ysfx_new(config);
  if (!fx)
    return messages;
  struct Script {
    ysfx_t *value;
    ~Script() { ysfx_free(value); }
  } script{fx};
  if (ysfx_load_file(fx, file.getFullPathName().toRawUTF8(), 0) &&
      ysfx_compile(fx, 0))
    return {};
  return messages;
}
} // namespace cydaw::plugins
