#include "NativeHostedPlugin.h"
#include <deque>
#include <lilv/lilv.h>
#include <lv2/urid/urid.h>
namespace cydaw::plugins {
// Core exposes instance state but not preset discovery. Read the explicit
// bundle with its bundled Lilv runtime, then pass standard Turtle state to
// Core. No plugin is instantiated by this catalogue and no global LV2 scan
// occurs.
void NativeHostedPlugin::loadLv2Programs(const base::File &bundle) {
  auto *world = lilv_world_new();
  if (!world)
    return;
  struct Cleanup {
    LilvWorld *value;
    ~Cleanup() { lilv_world_free(value); }
  } cleanup{world};
  std::deque<std::string> uris;
  LV2_URID_Map map{&uris, [](void *p, const char *uri) -> LV2_URID {
                     auto &list = *static_cast<std::deque<std::string> *>(p);
                     for (std::size_t i = 0; i < list.size(); ++i)
                       if (list[i] == uri)
                         return LV2_URID(i + 1);
                     list.emplace_back(uri);
                     return LV2_URID(list.size());
                   }};
  LV2_URID_Unmap unmap{
      &uris, [](void *p, LV2_URID id) -> const char * {
        auto &list = *static_cast<std::deque<std::string> *>(p);
        return id && id <= list.size() ? list[id - 1].c_str() : nullptr;
      }};
  auto path = bundle.getFullPathName().replaceCharacter('\\', '/') + "/";
  auto *bundleUri = lilv_new_file_uri(world, nullptr, path.toRawUTF8());
  lilv_world_load_bundle(world, bundleUri);
  lilv_node_free(bundleUri);
  auto *id = lilv_new_uri(world, description.descriptiveName.toRawUTF8());
  const auto *plugin =
      lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), id);
  lilv_node_free(id);
  if (!plugin)
    return;
  auto *type = lilv_new_uri(world, "http://lv2plug.in/ns/ext/presets#Preset");
  auto *related = lilv_plugin_get_related(plugin, type);
  lilv_node_free(type);
  if (!related)
    return;
  LILV_FOREACH(nodes, i, related) {
    if (programs_.size() >= 4096)
      break;
    const auto *preset = lilv_nodes_get(related, i);
    lilv_world_load_resource(world, preset);
    auto *state = lilv_state_new_from_world(world, &map, preset);
    if (!state)
      continue;
    const auto *name = lilv_state_get_label(state);
    char *text = lilv_state_to_string(world, &map, &unmap, state,
                                      lilv_node_as_uri(preset), nullptr);
    if (text) {
      const auto n = std::strlen(text);
      if (n <= 64 * 1024 * 1024) {
        Program program;
        program.name =
            name ? base::Text(name) : base::Text(lilv_node_as_uri(preset));
        program.state.assign(text, text + n);
        programs_.push_back(std::move(program));
      }
      lilv_free(text);
    }
    lilv_state_free(state);
  }
  lilv_nodes_free(related);
}
} // namespace cydaw::plugins
