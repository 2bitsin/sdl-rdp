#pragma once
#include "headless-client.hpp"
#include "drive-wire.hpp"
#include <freerdp/channels/rdpdr.h>

namespace Headless {
inline void ShareDrive(Client& client, char const* path, char const* name = "share") {
  Expects(path != nullptr, "shared directory supplied");
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  Expects(freerdp_settings_set_bool(client.instance->context->settings, FreeRDP_AudioPlayback, FALSE),
    "drive-only client has no audio device");
  const char* arguments[]{"drive", name, path};
  Expects(freerdp_client_add_device_channel(client.instance->context->settings, 3, arguments), "drive device configured");
  client.instance->LoadChannels = [](freerdp* instance) -> BOOL {
    auto entry = freerdp_load_channel_addin_entry("rdpdr", nullptr, nullptr,
      FREERDP_ADDIN_CHANNEL_STATIC | FREERDP_ADDIN_CHANNEL_ENTRYEX);
    return entry && freerdp_channels_client_load_ex(instance->context->channels,
      instance->context->settings, reinterpret_cast<PVIRTUALCHANNELENTRYEX>(entry),
      instance->context->settings) == 0;
  };
}
struct DriveObserver {
  inline static thread_local DriveObserver* active = nullptr;
  freerdp* instance;
  pReceiveChannelData original;
  unsigned requests = 0;
  std::vector<Backend::DrivePacket> io;
  std::vector<std::pair<unsigned, unsigned>> replies;
  bool hold = false;
  explicit DriveObserver(Client& client) : instance(client.instance.get()), original(instance->ReceiveChannelData) {
    Expects(!active, "one drive observer per thread");
    active = this;
    instance->ReceiveChannelData = Receive;
  }
  ~DriveObserver() { instance->ReceiveChannelData = original; active = nullptr; }
  static BOOL Receive(freerdp* instance, UINT16 id, BYTE const* data, size_t size, UINT32 flags, size_t total) {
    auto& self = *active;
    if (id == freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME)) {
      if ((flags & CHANNEL_FLAG_FIRST) && size >= 4) {
        Backend::DrivePacket packet;
        packet.Append({data, size});
        if (packet.Get(2) == RDPDR_CTYP_CORE) {
          auto type = packet.Get(2);
          if (type == PAKID_CORE_DEVICE_IOREQUEST) { ++self.requests; self.io.push_back(packet); }
          if (type == PAKID_CORE_DEVICE_REPLY) {
            auto device = packet.Get(4), status = packet.Get(4);
            self.replies.emplace_back(device, status);
          }
        }
      }
      if (self.hold) return TRUE;
    }
    return self.original(instance, id, data, size, flags, total);
  }
  bool Send(Backend::DrivePacket const& packet) {
    auto id = freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME);
    return id && instance->SendChannelData(instance, id, packet.bytes.data(), packet.bytes.size());
  }
};

}
