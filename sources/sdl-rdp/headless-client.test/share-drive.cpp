#include <sdl-rdp/headless-client.test/share-drive.hpp>

#include <sdl-rdp/headless-client.test/client-channels.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cmdline.h>
#include <array>

namespace Headless {
auto ShareDrive(Client& client, char const* path, char const* name) -> void {
  Expects(path != nullptr, "shared directory supplied");
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  Expects(freerdp_settings_set_bool(client.Instance()->context->settings, FreeRDP_AudioPlayback, FALSE),
          "drive-only client has no audio device");
  std::array<char const*, 3> arguments{ "drive", name, path };
  Expects(freerdp_client_add_device_channel(client.Instance()->context->settings, 3, arguments.data()),
          "drive device configured");
  client.Instance()->LoadChannels = [](freerdp* instance) -> BOOL { return LoadStaticChannel(instance, "rdpdr"); };
}
}
