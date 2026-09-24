#include <sdl-rdp/freerdp-facade/releases-sound.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <array>

namespace Backend {
auto ReleasesSound::operator()(RdpsndServerContext* sound) const -> void {
  auto* const channels = sound->vcm;
  rdpsnd_server_context_free(sound);
  // 2bitsin/FreeRDP#1: rdpsnd_main.c:1052 skips the close without an own thread; Open returns the channel.
  auto                 name    = std::to_array(RDPSND_CHANNEL_NAME);
  VirtualChannel const channel { WTSVirtualChannelOpen(channels, WTS_CURRENT_SESSION, name.data()) };
}
}
