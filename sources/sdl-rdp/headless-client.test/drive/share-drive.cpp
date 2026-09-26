#include <sdl-rdp/headless-client.test/drive/share-drive.hpp>

#include <sdl-rdp/headless-client.test/client/channels.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cmdline.h>
#include <array>

namespace sdl_rdp::headless_client_test::drive::detail::share_drive {
using sdl_rdp::headless_client_test::client::ChannelLoader;
using sdl_rdp::headless_client_test::client::LoadStaticChannel;
using sdl_rdp::utilities::Expects;

auto ShareDrive(Client& client, std::filesystem::path const& path, std::string const& name) -> void {
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  auto* const settings = client.Instance()->context->settings;
  auto const  silent   = freerdp_settings_set_bool(settings, FreeRDP_AudioPlayback, false);
  Expects(silent, "drive-only client has no audio device");
  std::array<char const*, 3> arguments { "drive", name.c_str(), path.c_str() };
  auto const                 added     = freerdp_client_add_device_channel(settings, 3, arguments.data());
  Expects(added, "drive device configured");
  client.Instance()->LoadChannels = ChannelLoader<LoadStaticChannel, "rdpdr">;
}
}
