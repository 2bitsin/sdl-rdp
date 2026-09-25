#include <sdl-rdp/headless-client.test/drive/observer.hpp>

#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>

#include <freerdp/channels/channels.h>
#include <freerdp/channels/rdpdr.h>
#include <oxbox/utilities/span.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::headless_client_test::drive::detail::observer {
using sdl_rdp::headless_client_test::client::ClientHandle;
using sdl_rdp::headless_client_test::client::SendStaticChannel;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;

namespace {
auto ObserveDrive(DriveCapture& capture, std::span<std::byte const> bytes) -> void {
  DrivePacket packet;
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

DriveObserver::DriveObserver(Client& client) : instance(ClientHandle(client)), original(instance.ReceiveChannelData) {
  ObserverSet::Of(*instance.context).Add(*this);
  // abi: pReceiveChannelData, UINT16 is uint16_t, BYTE is uint8_t, UINT32 is uint32_t, BOOL is int
  instance.ReceiveChannelData = [](freerdp* receiver, std::uint16_t id, std::uint8_t const* data, std::size_t size,
                                   std::uint32_t flags, std::size_t total) -> int {
    Expects(receiver != nullptr, "the channel data names its client");
    Expects(data != nullptr, "channel data is supplied");
    Expects(receiver->context != nullptr, "the receiving client has its context");
    return ObserverSet::Of(*receiver->context)
        .Held<DriveObserver>()
        ->Receive(id, std::as_bytes(std::span(data, size)), flags, total);
  };
}
DriveObserver::~DriveObserver() {
  instance.ReceiveChannelData = original;
  ObserverSet::Of(*instance.context).Remove<DriveObserver>();
}
auto DriveObserver::Send(DrivePacket const& packet) -> bool {
  return SendStaticChannel(instance, RDPDR_CHANNEL_NAME, std::span(packet.Bytes()));
}
auto DriveObserver::Observed() -> DriveCapture& {
  return observed;
}
auto DriveObserver::Observed() const -> DriveCapture const& {
  return observed;
}
auto DriveObserver::Receive(std::uint16_t id, std::span<std::byte const> data, std::uint32_t flags, std::size_t total)
    -> bool {
  if (id == freerdp_channels_get_id_by_name(&instance, RDPDR_CHANNEL_NAME)) {
    if ((flags & CHANNEL_FLAG_FIRST) && data.size() >= 4) ObserveDrive(observed, data);
    if (observed.hold) return true;
  }
  return original(&instance, id, oxbox::utilities::SpanCast<std::uint8_t const>(data).data(), data.size(), flags,
                  total);
}
}
