#include <sdl-rdp/headless-client.test/drive-observer.hpp>

#include <sdl-rdp/headless-client.test/client-channels.hpp>

#include <freerdp/channels/channels.h>
#include <freerdp/channels/rdpdr.h>
#include <oxbox/utilities/span.hpp>
#include <cstdint>
#include <span>

namespace Headless {
namespace {
auto ObserveDrive(DriveCapture& capture, std::span<BYTE const> bytes) -> void {
  Backend::DrivePacket packet;
  packet.Append(std::as_bytes(bytes));
  if (packet.Read<uint16_t>() == RDPDR_CTYP_CORE) {
    auto type = packet.Read<uint16_t>();
    if (type == PAKID_CORE_DEVICE_IOREQUEST) {
      ++capture.requests;
      capture.io.push_back(packet);
    }
    if (type == PAKID_CORE_DEVICE_REPLY) {
      auto device = packet.Read<uint32_t>();
      auto status = packet.Read<uint32_t>();
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
auto DriveObserver::Send(Backend::DrivePacket const& packet) const -> bool {
  return SendStaticChannel(instance, RDPDR_CHANNEL_NAME,
                           oxbox::utilities::SpanCast<std::uint8_t const>(std::span(packet.Bytes())));
}
auto DriveObserver::Observed() -> DriveCapture& {
  return observed;
}
auto DriveObserver::Observed() const -> DriveCapture const& {
  return observed;
}
auto DriveObserver::Receive(freerdp* instance, UINT16 id, BYTE const* data, size_t size, UINT32 flags, size_t total)
    -> BOOL {
  auto& self = *active;
  if (id == freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME)) {
    if ((flags & CHANNEL_FLAG_FIRST) && size >= 4) ObserveDrive(self.observed, { data, size });
    if (self.observed.hold) return TRUE;
  }
  return self.original(instance, id, data, size, flags, total);
}
}
