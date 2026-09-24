#include "_detail/client-channels.hpp"

#include <array>
#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/cmdline.h>

namespace Headless {
namespace {
BOOL LoadAddin(freerdp* instance, char const* name, std::span<char const* const> dynamic) {
  auto* settings = instance->context->settings;
  auto  entry    = reinterpret_cast<PVIRTUALCHANNELENTRYEX>(freerdp_load_channel_addin_entry(
      name, nullptr, nullptr, FREERDP_ADDIN_CHANNEL_STATIC | FREERDP_ADDIN_CHANNEL_ENTRYEX));
  return entry && (dynamic.empty() || freerdp_client_add_dynamic_channel(settings, dynamic.size(), dynamic.data())) &&
         freerdp_channels_client_load_ex(instance->context->channels, settings, entry, settings) == 0;
}
}

BOOL LoadStaticChannel(freerdp* instance, char const* name) {
  return LoadAddin(instance, name, { });
}
BOOL LoadDynamicChannel(freerdp* instance, char const* name) {
  std::array const channel{ name };
  return LoadAddin(instance, "drdynvc", channel);
}
bool SendStaticChannel(freerdp* instance, char const* name, std::span<BYTE const> bytes) {
  auto id = freerdp_channels_get_id_by_name(instance, name);
  return id && instance->SendChannelData(instance, id, bytes.data(), bytes.size());
}
}
