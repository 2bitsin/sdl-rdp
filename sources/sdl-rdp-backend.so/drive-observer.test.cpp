#include "_detail/drive-observer.hpp"

#include "_detail/client-channels.hpp"

#include <freerdp/channels/channels.h>
#include <freerdp/channels/rdpdr.h>
#include <span>

namespace Headless {
namespace {
void ObserveDrive(DriveCapture& capture, std::span<BYTE const> bytes) {
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
}

DriveObserver::DriveObserver(Client& client)
    : instance(client.Instance().get()), original(instance->ReceiveChannelData) {
  Expects(!active, "one drive observer per thread");
  active                       = this;
  instance->ReceiveChannelData = Receive;
}
DriveObserver::~DriveObserver() {
  instance->ReceiveChannelData = original;
  active                       = nullptr;
}
bool DriveObserver::Send(Backend::DrivePacket const& packet) const {
  return SendStaticChannel(instance, RDPDR_CHANNEL_NAME, packet.Bytes());
}
DriveCapture& DriveObserver::Observed() {
  return observed;
}
DriveCapture const& DriveObserver::Observed() const {
  return observed;
}
BOOL DriveObserver::Receive(freerdp* instance, UINT16 id, BYTE const* data, size_t size, UINT32 flags, size_t total) {
  auto& self = *active;
  if (id == freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME)) {
    if ((flags & CHANNEL_FLAG_FIRST) && size >= 4) ObserveDrive(self.observed, { data, size });
    if (self.observed.hold) return TRUE;
  }
  return self.original(instance, id, data, size, flags, total);
}
}
