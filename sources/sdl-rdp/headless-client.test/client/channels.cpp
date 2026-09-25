#include <sdl-rdp/headless-client.test/client/channels.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/cmdline.h>
#include <oxbox/utilities/span.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sdl_rdp::headless_client_test::client::detail::channels {
namespace {
auto LoadAddin(freerdp& instance, std::string const& name) -> bool {
  auto& context = *instance.context;
  // C ABI: the addin loader returns every entry point as its generic PVIRTUALCHANNELENTRY; ENTRYEX asked for this one.
  auto entry = reinterpret_cast<PVIRTUALCHANNELENTRYEX>(freerdp_load_channel_addin_entry(
      name.c_str(), nullptr, nullptr, FREERDP_ADDIN_CHANNEL_STATIC | FREERDP_ADDIN_CHANNEL_ENTRYEX));
  return entry && freerdp_channels_client_load_ex(context.channels, context.settings, entry, context.settings) == 0;
}
}

auto LoadStaticChannel(freerdp& instance, std::string const& name) -> bool {
  return LoadAddin(instance, name);
}
auto LoadDynamicChannel(freerdp& instance, std::string const& name) -> bool {
  std::array const channel{ name.c_str() };
  return freerdp_client_add_dynamic_channel(instance.context->settings, channel.size(), channel.data())
         && LoadAddin(instance, "drdynvc");
}
auto SendStaticChannel(freerdp& instance, std::string const& name, std::span<std::byte const> bytes) -> bool {
  auto id = freerdp_channels_get_id_by_name(&instance, name.c_str());
  return id
         && instance.SendChannelData(&instance, id, oxbox::utilities::SpanCast<std::uint8_t const>(bytes).data(),
                                     bytes.size());
}
}
