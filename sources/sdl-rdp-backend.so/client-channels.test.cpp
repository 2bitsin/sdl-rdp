#include "_detail/client-channels.hpp"

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/cmdline.h>
#include <array>

namespace Headless {
namespace {
auto LoadAddin(freerdp* instance, char const* name, std::span<char const* const> dynamic) -> BOOL {
  auto* settings = instance->context->settings;
  auto  entry    = reinterpret_cast<PVIRTUALCHANNELENTRYEX>(freerdp_load_channel_addin_entry(
      name, nullptr, nullptr, FREERDP_ADDIN_CHANNEL_STATIC | FREERDP_ADDIN_CHANNEL_ENTRYEX));
  return entry && (dynamic.empty() || freerdp_client_add_dynamic_channel(settings, dynamic.size(), dynamic.data()))
         && freerdp_channels_client_load_ex(instance->context->channels, settings, entry, settings) == 0;
}
}

auto LoadStaticChannel(freerdp* instance, char const* name) -> BOOL {
  return LoadAddin(instance, name, { });
}
auto LoadDynamicChannel(freerdp* instance, char const* name) -> BOOL {
  std::array const channel{ name };
  return LoadAddin(instance, "drdynvc", channel);
}
auto SendStaticChannel(freerdp* instance, char const* name, std::span<BYTE const> bytes) -> bool {
  auto id = freerdp_channels_get_id_by_name(instance, name);
  return id && instance->SendChannelData(instance, id, bytes.data(), bytes.size());
}
}
