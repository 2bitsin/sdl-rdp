#pragma once
#include "drive-packet.hpp"
#include "headless-client.hpp"

#include <freerdp/channels/rdpdr.h>

namespace Headless {
inline void ShareDrive(Client& client, char const* path, char const* name = "share") {
  Expects(path != nullptr, "shared directory supplied");
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  Expects(freerdp_settings_set_bool(client.Instance()->context->settings, FreeRDP_AudioPlayback, FALSE),
          "drive-only client has no audio device");
  std::array<char const*, 3> arguments{ "drive", name, path };
  Expects(freerdp_client_add_device_channel(client.Instance()->context->settings, 3, arguments.data()),
          "drive device configured");
  client.Instance()->LoadChannels = [](freerdp* instance) -> BOOL {
    auto entry = freerdp_load_channel_addin_entry("rdpdr", nullptr, nullptr,
                                                  FREERDP_ADDIN_CHANNEL_STATIC | FREERDP_ADDIN_CHANNEL_ENTRYEX);
    return entry && freerdp_channels_client_load_ex(instance->context->channels, instance->context->settings,
                                                    reinterpret_cast<PVIRTUALCHANNELENTRYEX>(entry),
                                                    instance->context->settings) == 0;
  };
}
struct DriveCapture {
  unsigned                                   requests = 0;
  std::vector<Backend::DrivePacket>          io;
  std::vector<std::pair<unsigned, unsigned>> replies;
  bool                                       hold     = false;
};
inline void ObserveDrive(DriveCapture& capture, std::span<BYTE const> bytes) {
  Backend::DrivePacket packet;
  packet.Append(bytes);
  if (packet.Get(2) == RDPDR_CTYP_CORE) {
    auto type = packet.Get(2);
    if (type == PAKID_CORE_DEVICE_IOREQUEST) {
      ++capture.requests;
      capture.io.push_back(packet);
    }
    if (type == PAKID_CORE_DEVICE_REPLY) {
      auto device = packet.Get(4);
      auto status = packet.Get(4);
      capture.replies.emplace_back(device, status);
    }
  }
}
struct DriveObserver {
public:
           DriveObserver(DriveObserver const&) = delete;
           DriveObserver(DriveObserver&&)      = delete;
  explicit DriveObserver(Client& client) : instance(client.Instance().get()), original(instance->ReceiveChannelData) {
    Expects(!active, "one drive observer per thread");
    active                       = this;
    instance->ReceiveChannelData = Receive;
  }
  ~DriveObserver() {
    instance->ReceiveChannelData = original;
    active                       = nullptr;
  }
  DriveObserver& operator = (DriveObserver const&) = delete;
  DriveObserver& operator = (DriveObserver&&)      = delete;
  bool           Send(Backend::DrivePacket const& packet) const {
    auto id = freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME);
    return id && instance->SendChannelData(instance, id, packet.Bytes().data(), packet.Bytes().size());
  }
  DriveCapture&       Observed() { return observed; }
  DriveCapture const& Observed() const { return observed; }

private:
  static BOOL Receive(freerdp* instance, UINT16 id, BYTE const* data, size_t size, UINT32 flags, size_t total) {
    auto& self = *active;
    if (id == freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME)) {
      if ((flags & CHANNEL_FLAG_FIRST) && size >= 4) ObserveDrive(self.observed, { data, size });
      if (self.observed.hold) return TRUE;
    }
    return self.original(instance, id, data, size, flags, total);
  }
  DriveCapture                              observed;
  inline static thread_local DriveObserver* active   = nullptr;
  freerdp*                                  instance;
  pReceiveChannelData                       original;
};

}
