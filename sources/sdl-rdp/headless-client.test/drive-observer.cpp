#include <sdl-rdp/headless-client.test/drive-observer.hpp>

#include <sdl-rdp/headless-client.test/client-channels.hpp>

#include <freerdp/channels/channels.h>
#include <freerdp/channels/rdpdr.h>
#include <oxbox/utilities/span.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Headless {
namespace {
auto ObserveDrive(DriveCapture& capture, std::span<std::byte const> bytes) -> void {
  Backend::DrivePacket packet;
  packet.Append(bytes);
  if (packet.Read<std::uint16_t>() == RDPDR_CTYP_CORE) {
    auto type = packet.Read<std::uint16_t>();
    if (type == PAKID_CORE_DEVICE_IOREQUEST) {
      ++capture.requests;
      capture.io.push_back(packet);
    }
    if (type == PAKID_CORE_DEVICE_REPLY) {
      auto device = packet.Read<std::uint32_t>();
      auto status = packet.Read<std::uint32_t>();
      capture.replies.emplace_back(device, status);
    }
  }
}
}

DriveObserver::DriveObserver(Client& client)
    : instance(client.Instance().get()), original(instance->ReceiveChannelData) {
  Expects(!active, "one drive observer per thread");
  active = this;
  // abi: pReceiveChannelData, UINT16 is uint16_t, BYTE is uint8_t, UINT32 is uint32_t, BOOL is int
  instance->ReceiveChannelData = [](freerdp* receiver, std::uint16_t id, std::uint8_t const* data, std::size_t size,
                                    std::uint32_t flags, std::size_t total) -> int {
    Expects(receiver == active->instance, "the observed client receives");
    return active->Receive(id, std::as_bytes(std::span(data, size)), flags, total);
  };
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
auto DriveObserver::Receive(std::uint16_t id, std::span<std::byte const> data, std::uint32_t flags, std::size_t total)
    -> bool {
  if (id == freerdp_channels_get_id_by_name(instance, RDPDR_CHANNEL_NAME)) {
    if ((flags & CHANNEL_FLAG_FIRST) && data.size() >= 4) ObserveDrive(observed, data);
    if (observed.hold) return true;
  }
  return original(instance, id, oxbox::utilities::SpanCast<std::uint8_t const>(data).data(), data.size(), flags, total);
}
}
